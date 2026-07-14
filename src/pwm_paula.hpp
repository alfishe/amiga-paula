#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include "types.hpp"
#include "filters.hpp"
#include "renderer.hpp"

namespace mod {

constexpr double PWM_CLOCK_HZ = AMIGA_PAL_CCK_HZ;  // ~3.546895 MHz

class PwmPaula : public IRenderer {
public:
    void setup(double outputFreq) override;
    void generateSamples(float* outL, float* outR, int32_t numSamples) override;

    void writeByte(uint32_t address, uint8_t data) override;
    void writeWord(uint32_t address, uint16_t data) override;
    void writePtr(uint32_t address, const int8_t* ptr) override;

    int8_t* getNullSamplePtr() override { return nullSample.data(); }
    void clearState() override;

private:
    static constexpr int FIR_TAPS = 127;
    static constexpr int NUM_CHANNELS = 4;

    struct Voice {
        bool active = false;
        bool sampleJustStarted = false;

        int8_t audDat[2] = {0, 0};
        const int8_t* location = nullptr;
        uint16_t lengthCounter = 0;
        int32_t sampleCounter = 0;

        int8_t currentSample = 0;
        uint8_t volume = 0;

        uint16_t period = 0;
        uint16_t periodCounter = 0;

        const int8_t* storedLocation = nullptr;
        uint16_t storedLength = 0;
        uint16_t storedPeriod = 0;
        uint8_t storedVolume = 0;
    };

    void fetchNextSample(Voice& v);
    void startDMA(int ch);
    void stopDMA(int ch);
    void audxper(int ch, uint16_t period);
    void audxvol(int ch, uint16_t vol);
    void audxlen(int ch, uint16_t len);
    void audxdat(int ch, const int8_t* src);

    void initDecimator(double outputFreq);
    float decimateSamples(const std::vector<float>& pwmBuffer, int channel);

    std::array<Voice, NUM_CHANNELS> voices{};
    std::array<int8_t, 0xFFFF * 2> nullSample{};

    bool useLEDFilter = false;
    double outputFreq = 48000.0;
    int decimationFactor = 74;
    double decimationPhase = 0.0;
    double decimationStep = 0.0;

    std::array<float, FIR_TAPS> firCoeffs{};
    std::array<std::array<float, FIR_TAPS>, 2> firHistory{};  // L/R
    std::array<int, 2> firHistoryIdx{};

    OnePoleFilter filterHi;
    TwoPoleFilter filterLED;

    // Punch enhancement state
    float prevOutL = 0, prevOutR = 0;
    float envL = 0, envR = 0;

    // Spatial enhancement state
    static constexpr int MAX_DELAY = 1024;  // ~21ms @ 48kHz
    std::array<float, MAX_DELAY> delayL{}, delayR{};
    int delayIdx = 0;

    // Spatial filter state
    float spatialLpL = 0, spatialLpR = 0;

public:
    enum class Mode {
        Clean = 0,
        Room_15dB,
        Room_14dB,
        Room_13dB,
        Room_12dB,
        Room_9dB,
        COUNT
    };
    void setMode(Mode m);
    Mode getMode() const { return currentMode; }
    void cycleMode(int delta);
    static const char* modeName(Mode m);

private:
    Mode currentMode = Mode::Clean;
    bool spatialEnabled = false;
    float spatialLevel = 0.0f;
    float spatialLpCoef = 0.0f;
    int spatialDelay = 0;
};

} // namespace mod
