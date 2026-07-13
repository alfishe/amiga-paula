#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <memory>

namespace mod {

constexpr int NUM_CHANNELS = 4;
constexpr int MOD_ROWS = 64;
constexpr int MOD_SAMPLES = 31;
constexpr int MAX_PATTERNS = 128;

constexpr double AMIGA_PAL_XTAL_HZ = 28375160.0;
constexpr double AMIGA_PAL_CCK_HZ = AMIGA_PAL_XTAL_HZ / 8.0;
constexpr double CIA_PAL_CLK = AMIGA_PAL_CCK_HZ / 5.0;

constexpr int MIN_BPM = 32;
constexpr int MAX_BPM = 255;

struct Note {
    uint16_t period = 0;
    uint8_t sample = 0;
    uint8_t command = 0;
    uint8_t param = 0;
};

struct Sample {
    std::string name;
    int32_t length = 0;
    int8_t finetune = 0;
    int8_t volume = 0;
    int32_t loopStart = 0;
    int32_t loopLength = 2;
    int32_t offset = 0;
};

struct Module {
    std::string name;
    std::array<Sample, MOD_SAMPLES> samples;
    std::array<uint8_t, 128> patternTable{};
    uint8_t songLength = 0;
    uint8_t numPatterns = 0;
    std::vector<std::array<Note, MOD_ROWS * NUM_CHANNELS>> patterns;
    std::vector<int8_t> sampleData;
};

struct Channel {
    const int8_t* sampleStart = nullptr;
    const int8_t* loopStart = nullptr;
    const int8_t* waveStart = nullptr;

    int16_t period = 0;
    int16_t wantedPeriod = 0;
    int8_t volume = 0;
    uint8_t finetune = 0;

    uint16_t length = 0;
    uint16_t replen = 1;

    uint8_t sampleNum = 0;
    uint8_t tonePortSpeed = 0;
    int8_t tonePortDir = 0;
    uint8_t vibratoCmd = 0;
    uint8_t vibratoPos = 0;
    uint8_t tremoloCmd = 0;
    uint8_t tremoloPos = 0;
    uint8_t waveControl = 0;
    uint8_t glissFunk = 0;
    uint8_t sampleOffset = 0;
    int8_t loopCount = 0;
    int8_t pattPos = 0;
    uint8_t funkOffset = 0;

    uint16_t cmd = 0;
    uint16_t note = 0;

    int dmaBit = 0;
    int chanIndex = 0;
};

} // namespace mod
