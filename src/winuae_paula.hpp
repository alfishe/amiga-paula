#pragma once

#include <array>
#include <cstdint>
#include "renderer.hpp"
#include "filters.hpp"
#include "types.hpp"

namespace mod {

// WinUAE/UADE-style Paula emulation with Lankila's sinc interpolation
// Uses precomputed BLEP tables shaped to match filter model

class WinuaePaula : public IRenderer {
public:
    static constexpr int SINC_QUEUE_LENGTH = 256;
    static constexpr int SINC_QUEUE_MAX_AGE = 2048;

    enum FilterModel {
        FILTER_NONE = 0,
        FILTER_MODEL_A500,
        FILTER_MODEL_A1200
    };

    void setup(double freq, FilterModel model = FILTER_MODEL_A1200);
    void setup(double freq) override { setup(freq, FILTER_MODEL_A1200); }
    void clearState() override;

    void writeByte(uint32_t address, uint8_t data) override;
    void writeWord(uint32_t address, uint16_t data) override;
    void writePtr(uint32_t address, const int8_t* ptr) override;
    void generateSamples(float* outL, float* outR, int32_t numSamples) override;
    int8_t* getNullSamplePtr() override { return nullSample.data(); }

private:
    struct SincQueueEntry {
        int time;
        int output;
    };

    struct Voice {
        const int8_t* location = nullptr;
        const int8_t* storedLocation = nullptr;
        uint16_t lengthCounter = 0;
        uint16_t storedLength = 0;
        uint16_t period = 0;
        uint16_t storedPeriod = 0;
        uint8_t volume = 0;
        uint8_t storedVolume = 0;

        int8_t audDat[2] = {0, 0};
        int8_t currentSample = 0;
        int sampleCounter = 0;
        bool sampleJustStarted = false;
        bool active = false;

        int periodCounter = 0;

        // Sinc interpolation state
        int sincOutputState = 0;
        SincQueueEntry sincQueue[SINC_QUEUE_LENGTH];
        int sincQueueTime = 0;
        int sincQueueHead = 0;
    };

    void fetchNextSample(Voice& v);
    void startDMA(int ch);
    void stopDMA(int ch);
    void audxper(int ch, uint16_t period);
    void audxvol(int ch, uint16_t vol);
    void audxlen(int ch, uint16_t len);
    void audxdat(int ch, const int8_t* src);

    void sincPrehandler(int cycles);
    void sincHandler(int* output);
    int applyFilter(int input, int ch);

    void generateSincTables();

    std::array<Voice, NUM_CHANNELS> voices;
    std::array<int8_t, 2> nullSample = {0, 0};

    double outputFreq = 48000.0;
    double cyclesPerSample = 0.0;
    double cycleAccum = 0.0;

    FilterModel filterModel = FILTER_MODEL_A1200;
    bool useLEDFilter = false;

    // Filter state per channel (L, R)
    struct FilterState {
        float rc1 = 0, rc2 = 0, rc3 = 0, rc4 = 0, rc5 = 0;
    };
    std::array<FilterState, 2> filterState;

    float a500FilterA0_1 = 0;
    float a500FilterA0_2 = 0;
    float ledFilterA0 = 0;

    // Precomputed sinc tables: [5][SINC_QUEUE_MAX_AGE]
    // 0: A500 LED off, 1: A500 LED on, 2: A1200 LED off, 3: A1200 LED on, 4: vanilla
    std::array<std::array<int, SINC_QUEUE_MAX_AGE>, 5> sincTables;
};

} // namespace mod
