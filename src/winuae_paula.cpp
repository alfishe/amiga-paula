#include "winuae_paula.hpp"
#include <cmath>
#include <cstring>
#include <numbers>

namespace mod {

// External sinc tables from sinctable.cpp
extern const int winsinc_integral[5][2048];
constexpr int SINC_QUEUE_MAX_AGE = 2048;

// Paula clock: 3546895 Hz (PAL)
static constexpr double PAULA_CLOCK = 3546895.0;

// Denormal offset to prevent FPU slowdown
static constexpr float DENORMAL_OFFSET = 1e-10f;

void WinuaePaula::setup(double freq, FilterModel model) {
    outputFreq = freq;
    filterModel = model;
    cyclesPerSample = PAULA_CLOCK / freq;
    cycleAccum = 0.0;

    // Calculate filter coefficients
    // A500 has two cascaded RC filters before the LED filter
    // Measured cutoffs: ~4.4kHz and ~33kHz
    double a500_f1 = 6200.0;  // Static RC filter 1
    double a500_f2 = 20000.0; // Static RC filter 2
    a500FilterA0_1 = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * a500_f1 / freq));
    a500FilterA0_2 = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * a500_f2 / freq));

    // LED filter: ~3.3kHz Butterworth-ish (3 cascaded poles)
    double led_f = 3275.0;
    ledFilterA0 = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * led_f / freq));

    clearState();
}

void WinuaePaula::clearState() {
    for (auto& v : voices) {
        v = Voice{};
        for (auto& sq : v.sincQueue) {
            sq = {0, 0};
        }
    }
    for (auto& fs : filterState) {
        fs = FilterState{};
    }
    cycleAccum = 0.0;
}

void WinuaePaula::fetchNextSample(Voice& v) {
    if (v.sampleCounter == 0) {
        if (!v.sampleJustStarted) {
            if (--v.lengthCounter == 0) {
                v.lengthCounter = v.storedLength;
                v.location = v.storedLocation;
            }
        }
        v.sampleJustStarted = false;

        v.audDat[0] = *v.location++;
        v.audDat[1] = *v.location++;
        v.sampleCounter = 2;
    }

    v.currentSample = v.audDat[0];
    v.audDat[0] = v.audDat[1];
    v.sampleCounter--;
}

void WinuaePaula::startDMA(int ch) {
    auto& v = voices[ch];
    if (!v.storedLocation) v.storedLocation = nullSample.data();

    v.location = v.storedLocation;
    v.lengthCounter = v.storedLength;
    v.sampleCounter = 0;
    v.sampleJustStarted = true;
    v.period = v.storedPeriod;
    v.volume = v.storedVolume;
    v.periodCounter = v.period;
    v.active = true;

    // Reset sinc state
    v.sincOutputState = 0;
    v.sincQueueTime = 0;
    v.sincQueueHead = 0;
    for (auto& sq : v.sincQueue) sq = {0, 0};

    fetchNextSample(v);
}

void WinuaePaula::stopDMA(int ch) {
    voices[ch].active = false;
}

void WinuaePaula::audxper(int ch, uint16_t period) {
    auto& v = voices[ch];
    v.storedPeriod = (period == 0) ? 65535 : (period < 113 ? 113 : period);
}

void WinuaePaula::audxvol(int ch, uint16_t vol) {
    int realVol = vol & 127;
    if (realVol > 64) realVol = 64;
    voices[ch].storedVolume = static_cast<uint8_t>(realVol);
}

void WinuaePaula::audxlen(int ch, uint16_t len) {
    voices[ch].storedLength = len;
}

void WinuaePaula::audxdat(int ch, const int8_t* src) {
    voices[ch].storedLocation = src ? src : nullSample.data();
}

void WinuaePaula::writeByte(uint32_t address, uint8_t data) {
    if (address == 0xBFE001) {
        bool old = useLEDFilter;
        useLEDFilter = !!(data & 2);
        if (useLEDFilter != old) {
            for (auto& fs : filterState) fs = FilterState{};
        }
    }
}

void WinuaePaula::writeWord(uint32_t address, uint16_t data) {
    switch (address) {
        case 0xDFF096:
            if (data & 0x8000) {
                if (data & 1) startDMA(0);
                if (data & 2) startDMA(1);
                if (data & 4) startDMA(2);
                if (data & 8) startDMA(3);
            } else {
                if (data & 1) stopDMA(0);
                if (data & 2) stopDMA(1);
                if (data & 4) stopDMA(2);
                if (data & 8) stopDMA(3);
            }
            break;
        case 0xDFF0A4: audxlen(0, data); break;
        case 0xDFF0B4: audxlen(1, data); break;
        case 0xDFF0C4: audxlen(2, data); break;
        case 0xDFF0D4: audxlen(3, data); break;
        case 0xDFF0A6: audxper(0, data); break;
        case 0xDFF0B6: audxper(1, data); break;
        case 0xDFF0C6: audxper(2, data); break;
        case 0xDFF0D6: audxper(3, data); break;
        case 0xDFF0A8: audxvol(0, data); break;
        case 0xDFF0B8: audxvol(1, data); break;
        case 0xDFF0C8: audxvol(2, data); break;
        case 0xDFF0D8: audxvol(3, data); break;
    }
}

void WinuaePaula::writePtr(uint32_t address, const int8_t* ptr) {
    switch (address) {
        case 0xDFF0A0: audxdat(0, ptr); break;
        case 0xDFF0B0: audxdat(1, ptr); break;
        case 0xDFF0C0: audxdat(2, ptr); break;
        case 0xDFF0D0: audxdat(3, ptr); break;
    }
}

void WinuaePaula::sincPrehandler(int cycles) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        auto& v = voices[i];
        if (!v.active) continue;

        int output = v.currentSample * v.volume;

        // If output state changes, record to sinc queue for BLEP mixing
        if (v.sincOutputState != output) {
            v.sincQueueHead = (v.sincQueueHead - 1) & (SINC_QUEUE_LENGTH - 1);
            v.sincQueue[v.sincQueueHead].time = v.sincQueueTime;
            v.sincQueue[v.sincQueueHead].output = output - v.sincOutputState;
            v.sincOutputState = output;
        }

        v.sincQueueTime += cycles;
    }
}

void WinuaePaula::sincHandler(int* output) {
    // Select sinc table based on filter model and LED state
    // Tables: 0=A500 off, 1=A500 on, 2=A1200 off, 3=A1200 on, 4=vanilla
    // A1200 with LED off has no significant filtering - use vanilla
    int tableIdx;
    if (filterModel == FILTER_MODEL_A500) {
        tableIdx = useLEDFilter ? 1 : 0;
    } else if (filterModel == FILTER_MODEL_A1200) {
        tableIdx = useLEDFilter ? 3 : 4;  // LED on: use A1200 table, LED off: vanilla
    } else {
        tableIdx = 4; // vanilla
    }
    const int* winsinc = winsinc_integral[tableIdx];

    for (int i = 0; i < NUM_CHANNELS; i++) {
        auto& v = voices[i];
        if (!v.active) {
            output[i] = 0;
            continue;
        }

        // The sum rings with harmonic components up to infinity...
        int sum = v.sincOutputState << 17;

        // ...but we cancel them through mixing in BLEPs instead
        int offsetpos = v.sincQueueHead & (SINC_QUEUE_LENGTH - 1);
        for (int j = 0; j < SINC_QUEUE_LENGTH; j++) {
            int age = v.sincQueueTime - v.sincQueue[offsetpos].time;
            if (age >= SINC_QUEUE_MAX_AGE || age < 0)
                break;
            sum -= winsinc[age] * v.sincQueue[offsetpos].output;
            offsetpos = (offsetpos + 1) & (SINC_QUEUE_LENGTH - 1);
        }

        int val = sum >> 16;
        if (val > 32767) val = 32767;
        else if (val < -32768) val = -32768;
        output[i] = val;
    }
}

int WinuaePaula::applyFilter(int input, int ch) {
    auto& fs = filterState[ch];
    float normalOutput, ledOutput;

    switch (filterModel) {
        case FILTER_MODEL_A500:
            // A500 always has static RC filtering
            fs.rc1 = a500FilterA0_1 * input + (1.0f - a500FilterA0_1) * fs.rc1 + DENORMAL_OFFSET;
            fs.rc2 = a500FilterA0_2 * fs.rc1 + (1.0f - a500FilterA0_2) * fs.rc2;
            normalOutput = fs.rc2;

            fs.rc3 = ledFilterA0 * normalOutput + (1.0f - ledFilterA0) * fs.rc3;
            fs.rc4 = ledFilterA0 * fs.rc3 + (1.0f - ledFilterA0) * fs.rc4;
            fs.rc5 = ledFilterA0 * fs.rc4 + (1.0f - ledFilterA0) * fs.rc5;
            ledOutput = fs.rc5;
            break;

        case FILTER_MODEL_A1200:
            // A1200 has no static RC filter - only LED filter when enabled
            if (!useLEDFilter) {
                return input;  // No filtering at all
            }
            normalOutput = static_cast<float>(input);

            fs.rc2 = ledFilterA0 * normalOutput + (1.0f - ledFilterA0) * fs.rc2 + DENORMAL_OFFSET;
            fs.rc3 = ledFilterA0 * fs.rc2 + (1.0f - ledFilterA0) * fs.rc3;
            fs.rc4 = ledFilterA0 * fs.rc3 + (1.0f - ledFilterA0) * fs.rc4;
            ledOutput = fs.rc4;
            return static_cast<int>(std::clamp(ledOutput, -32768.0f, 32767.0f));

        default:
            return input;
    }

    float out = useLEDFilter ? ledOutput : normalOutput;
    if (out > 32767.0f) return 32767;
    if (out < -32768.0f) return -32768;
    return static_cast<int>(out);
}

void WinuaePaula::generateSamples(float* outL, float* outR, int32_t numSamples) {
    if (numSamples <= 0) return;

    // Amiga panning: ch0,ch3 -> L, ch1,ch2 -> R
    for (int32_t outIdx = 0; outIdx < numSamples; outIdx++) {
        // Calculate cycles until next output sample
        double nextAccum = cycleAccum + cyclesPerSample;
        int cycles = static_cast<int>(nextAccum) - static_cast<int>(cycleAccum);
        if (cycles < 1) cycles = 1;

        // Run Paula for these cycles
        for (int cyc = 0; cyc < cycles; cyc++) {
            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                auto& v = voices[ch];
                if (!v.active) continue;

                if (v.periodCounter == 0) {
                    v.period = v.storedPeriod;
                    v.volume = v.storedVolume;
                    v.periodCounter = v.period;
                    fetchNextSample(v);
                }
                v.periodCounter--;
            }
        }

        // Record state changes to sinc queue
        sincPrehandler(cycles);

        // Get interpolated output via sinc/BLEP
        int chOutput[NUM_CHANNELS];
        sincHandler(chOutput);

        // Mix channels with Amiga panning
        int left = chOutput[0] + chOutput[3];
        int right = chOutput[1] + chOutput[2];

        // Apply analog filters
        left = applyFilter(left, 0);
        right = applyFilter(right, 1);

        // Normalize to float (same scale as BLEP renderer)
        // Each channel: sample*vol range ~8192, two channels ~16384
        // BLEP outputs ~2.0 peak, so multiply by 2 to match
        outL[outIdx] = left / 16384.0f;
        outR[outIdx] = right / 16384.0f;

        cycleAccum = nextAccum - static_cast<int>(cycleAccum);
    }
}

} // namespace mod
