#pragma once

#include <cstdint>
#include <array>

namespace mod {

constexpr int BLEP_ZC = 16;
constexpr int BLEP_OS = 16;
constexpr int BLEP_SP = 16;
constexpr int BLEP_NS = (BLEP_ZC * BLEP_OS / BLEP_SP);
constexpr int BLEP_RNS = 31;

struct Blep {
    int32_t index = 0;
    int32_t samplesLeft = 0;
    std::array<float, BLEP_RNS + 1> buffer{};
    float lastValue = 0.0f;

    void add(float offset, float amplitude);
    float run(float input);
    void clear() {
        index = 0;
        samplesLeft = 0;
        buffer.fill(0.0f);
        lastValue = 0.0f;
    }
};

} // namespace mod
