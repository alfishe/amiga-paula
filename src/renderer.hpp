#pragma once

#include <cstdint>

namespace mod {

enum class RendererType {
    BLEP,    // Original BLEP synthesis (fast, accurate)
    PWM,     // PWM clock simulation with polyphase FIR decimation (authentic)
    WINUAE   // WinUAE/UADE sinc interpolation with Lankila's filter model
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void setup(double outputFreq) = 0;
    virtual void generateSamples(float* outL, float* outR, int32_t numSamples) = 0;

    virtual void writeByte(uint32_t address, uint8_t data) = 0;
    virtual void writeWord(uint32_t address, uint16_t data) = 0;
    virtual void writePtr(uint32_t address, const int8_t* ptr) = 0;

    virtual int8_t* getNullSamplePtr() = 0;
    virtual void clearState() = 0;
};

} // namespace mod
