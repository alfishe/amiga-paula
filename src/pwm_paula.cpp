#include "pwm_paula.hpp"
#include <cmath>
#include <cstring>
#include <numbers>

namespace mod {

void PwmPaula::setup(double freq) {
    outputFreq = freq;
    clearState();
    initDecimator(freq);

    // A1200 high-pass ~5.319Hz
    filterHi.setup(outputFreq, 5.319);

    // LED filter ~3090Hz, Q ~0.66
    filterLED.setup(outputFreq, 3090.533, 0.660225);
}

void PwmPaula::initDecimator(double freq) {
    // Calculate decimation factor: PWM clock / output rate
    decimationFactor = static_cast<int>(std::round(PWM_CLOCK_HZ / freq));
    decimationStep = PWM_CLOCK_HZ / freq;
    decimationPhase = 0.0;

    // Design lowpass FIR filter for post-decimation smoothing
    // Cutoff near output Nyquist to preserve full audio bandwidth
    // A1200 has no significant lowpass (34kHz is above audible range)
    double cutoffNorm = 0.48;  // Very close to Nyquist - preserve all audio

    // Kaiser window FIR design
    double beta = 2.5;  // Lower beta = wider transition band, less ringing

    auto bessel_i0 = [](double x) -> double {
        double sum = 1.0;
        double term = 1.0;
        for (int k = 1; k < 25; k++) {
            term *= (x * x) / (4.0 * k * k);
            sum += term;
        }
        return sum;
    };

    double i0Beta = bessel_i0(beta);
    int M = FIR_TAPS - 1;

    for (int n = 0; n <= M; n++) {
        // Sinc function (ideal lowpass impulse response)
        double sinc;
        int nShifted = n - M / 2;
        if (nShifted == 0) {
            sinc = 2.0 * cutoffNorm;
        } else {
            double x = 2.0 * std::numbers::pi * cutoffNorm * nShifted;
            sinc = std::sin(x) / (std::numbers::pi * nShifted);
        }

        // Kaiser window
        double arg = beta * std::sqrt(1.0 - std::pow((2.0 * n / M - 1.0), 2));
        double window = bessel_i0(arg) / i0Beta;

        firCoeffs[n] = static_cast<float>(sinc * window);
    }

    // Normalize filter for unity gain at DC
    float sum = 0.0f;
    for (auto c : firCoeffs) sum += c;
    for (auto& c : firCoeffs) c /= sum;

    // Clear history
    for (auto& h : firHistory) h.fill(0.0f);
    firHistoryIdx.fill(0);
}

void PwmPaula::clearState() {
    for (auto& v : voices) {
        v = Voice{};
    }
    for (auto& h : firHistory) h.fill(0.0f);
    firHistoryIdx.fill(0);
    decimationPhase = 0.0;
    filterHi.clear();
    filterLED.clear();
}

void PwmPaula::fetchNextSample(Voice& v) {
    if (v.sampleCounter == 0) {
        // Time to read from DMA
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

void PwmPaula::startDMA(int ch) {
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

    // Fetch first sample
    fetchNextSample(v);
}

void PwmPaula::stopDMA(int ch) {
    voices[ch].active = false;
}

void PwmPaula::audxper(int ch, uint16_t period) {
    auto& v = voices[ch];
    v.storedPeriod = (period == 0) ? 65535 : (period < 113 ? 113 : period);
}

void PwmPaula::audxvol(int ch, uint16_t vol) {
    int realVol = vol & 127;
    if (realVol > 64) realVol = 64;
    voices[ch].storedVolume = static_cast<uint8_t>(realVol);
}

void PwmPaula::audxlen(int ch, uint16_t len) {
    voices[ch].storedLength = len;
}

void PwmPaula::audxdat(int ch, const int8_t* src) {
    voices[ch].storedLocation = src ? src : nullSample.data();
}

void PwmPaula::writeByte(uint32_t address, uint8_t data) {
    if (address == 0xBFE001) {
        bool old = useLEDFilter;
        useLEDFilter = !!(data & 2);
        if (useLEDFilter != old) filterLED.clear();
    }
}

void PwmPaula::writeWord(uint32_t address, uint16_t data) {
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

void PwmPaula::writePtr(uint32_t address, const int8_t* ptr) {
    switch (address) {
        case 0xDFF0A0: audxdat(0, ptr); break;
        case 0xDFF0B0: audxdat(1, ptr); break;
        case 0xDFF0C0: audxdat(2, ptr); break;
        case 0xDFF0D0: audxdat(3, ptr); break;
    }
}

void PwmPaula::generateSamples(float* outL, float* outR, int32_t numSamples) {
    if (numSamples <= 0) return;

    // Amiga panning: ch0,ch3 -> L, ch1,ch2 -> R
    static constexpr int chanToOut[4] = {0, 1, 1, 0};  // 0=L, 1=R

    for (int32_t outIdx = 0; outIdx < numSamples; outIdx++) {
        // Determine how many PWM clock cycles for this output sample
        double nextPhase = decimationPhase + decimationStep;
        int pwmCycles = static_cast<int>(nextPhase) - static_cast<int>(decimationPhase);
        if (pwmCycles < 1) pwmCycles = 1;

        float accumL = 0.0f;
        float accumR = 0.0f;
        int countL = 0, countR = 0;

        // Run PWM clock cycles
        for (int pwm = 0; pwm < pwmCycles; pwm++) {
            float sampleL = 0.0f;
            float sampleR = 0.0f;

            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                auto& v = voices[ch];
                if (!v.active) continue;

                // Period counter - Paula fetches new sample when it expires
                if (v.periodCounter == 0) {
                    v.period = v.storedPeriod;
                    v.volume = v.storedVolume;
                    v.periodCounter = v.period;
                    fetchNextSample(v);
                }
                v.periodCounter--;

                // PWM volume modulation simulation:
                // On real Amiga, volume is PWM'd at ~3.55MHz rate
                // Volume 0-64 maps to duty cycle 0-64/64
                // We simulate by outputting sample * (volume/64)
                // The PWM artifacts emerge naturally from the high-rate sampling
                float sample = v.currentSample * (v.volume / 64.0f);

                if (chanToOut[ch] == 0)
                    sampleL += sample;
                else
                    sampleR += sample;
            }

            accumL += sampleL;
            accumR += sampleR;
            countL++;
            countR++;
        }

        // Average and normalize - no FIR, averaging provides sufficient anti-alias
        float outSampleL = (countL > 0) ? (accumL / countL) / 128.0f : 0.0f;
        float outSampleR = (countR > 0) ? (accumR / countR) / 128.0f : 0.0f;

        float out[2] = {outSampleL, outSampleR};

        if (useLEDFilter)
            filterLED.lowPassStereo(out, out);

        filterHi.highPassStereo(out, out);

        outL[outIdx] = out[0];
        outR[outIdx] = out[1];

        decimationPhase = nextPhase - static_cast<int>(decimationPhase);
    }
}

} // namespace mod
