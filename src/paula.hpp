#pragma once

#include <cstdint>
#include <array>
#include "types.hpp"
#include "blep.hpp"
#include "filters.hpp"

namespace mod {

enum AmigaModel { MODEL_A1200 = 0, MODEL_A500 = 1 };

class Paula {
public:
    void setup(double outputFreq, AmigaModel model);
    void generateSamples(float* outL, float* outR, int32_t numSamples);

    void writeByte(uint32_t address, uint8_t data);
    void writeWord(uint32_t address, uint16_t data);
    void writePtr(uint32_t address, const int8_t* ptr);

    int8_t* getNullSamplePtr() { return nullSample.data(); }
    void clearBlep();

private:
    struct Voice {
        bool active = false;
        bool sampleJustStarted = false;
        bool nextSampleStage = false;

        int8_t audDat[2] = {0, 0};
        const int8_t* location = nullptr;
        uint16_t lengthCounter = 0;
        int32_t sampleCounter = 0;
        float sample = 0.0f;
        float delta = 0.0f, phase = 0.0f;
        float blepDelta = 0.0f, blepPhase = 0.0f;

        const int8_t* storedLocation = nullptr;
        uint16_t storedLength = 0;
        float storedVol = 0.0f, storedDelta = 0.0f;
    };

    void refetchPeriod(Voice& v);
    void nextSample(Voice& v, Blep& b);
    void startDMA(int ch);
    void stopDMA(int ch);
    void audxper(int ch, uint16_t period);
    void audxvol(int ch, uint16_t vol);
    void audxlen(int ch, uint16_t len);
    void audxdat(int ch, const int8_t* src);

    std::array<Voice, NUM_CHANNELS> voices{};
    std::array<Blep, NUM_CHANNELS> bleps{};
    std::array<int8_t, 0xFFFF * 2> nullSample{};

    bool useLEDFilter = false;
    bool useLowpassFilter = false;
    bool useHighpassFilter = true;

    float periodToDeltaDiv = 0.0f;
    double outputFreq = 0.0;

    OnePoleFilter filterLo, filterHi;
    TwoPoleFilter filterLED;
};

} // namespace mod
