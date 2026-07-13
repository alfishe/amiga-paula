#include "mod_loader.hpp"
#include <fstream>
#include <cstring>
#include <cctype>
#include <iostream>

namespace mod {

constexpr int MAX_SAMPLE_LENGTH = 65534;

static bool detectMod31(const std::vector<uint8_t>& data) {
    if (data.size() < 1084 + 1024) return false;

    const uint8_t* id = &data[1080];
    if (std::memcmp(id, "M.K.", 4) == 0) return true;
    if (std::memcmp(id, "M!K!", 4) == 0) return true;
    if (std::memcmp(id, "FLT4", 4) == 0) return true;
    if (std::memcmp(id, "4CHN", 4) == 0) return true;
    if (std::memcmp(id, "N.T.", 4) == 0) return true;

    return false;
}

std::unique_ptr<Module> loadMod(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return nullptr;
    }

    auto filesize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> buffer(filesize);
    file.read(reinterpret_cast<char*>(buffer.data()), filesize);

    bool is31Sample = detectMod31(buffer);
    int numSamples = is31Sample ? 31 : 15;

    auto mod = std::make_unique<Module>();
    const uint8_t* p = buffer.data();

    mod->name = std::string(reinterpret_cast<const char*>(p), 20);
    p += 20;

    int32_t sampleDataOffset = 0;
    for (int i = 0; i < numSamples; i++) {
        auto& s = mod->samples[i];
        s.name = std::string(reinterpret_cast<const char*>(p), 22);
        p += 22;

        int32_t length = ((p[0] << 8) | p[1]) * 2;
        p += 2;
        s.length = std::min(length, MAX_SAMPLE_LENGTH);

        s.finetune = *p++ & 0x0F;
        s.volume = *p++;

        s.loopStart = ((p[0] << 8) | p[1]) * 2;
        p += 2;
        s.loopLength = ((p[0] << 8) | p[1]) * 2;
        p += 2;

        if (s.loopLength < 2) s.loopLength = 2;

        if (s.loopLength > 2 && s.loopStart + s.loopLength > s.length) {
            if ((s.loopStart / 2) + s.loopLength <= s.length)
                s.loopStart /= 2;
        }

        s.offset = sampleDataOffset;
        sampleDataOffset += MAX_SAMPLE_LENGTH;
    }

    mod->songLength = *p++;
    p++;

    int numPatterns = 0;
    for (int i = 0; i < 128; i++) {
        mod->patternTable[i] = *p++;
        if (mod->patternTable[i] > numPatterns)
            numPatterns = mod->patternTable[i];
    }
    numPatterns++;
    mod->numPatterns = numPatterns;

    if (is31Sample) p += 4;

    mod->patterns.resize(numPatterns);
    for (int i = 0; i < numPatterns; i++) {
        for (int row = 0; row < MOD_ROWS; row++) {
            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                auto& note = mod->patterns[i][row * NUM_CHANNELS + ch];
                note.period = ((p[0] & 0x0F) << 8) | p[1];
                note.sample = ((p[0] & 0xF0) | (p[2] >> 4)) & 31;
                note.command = p[2] & 0x0F;
                note.param = p[3];
                p += 4;
            }
        }
    }

    mod->sampleData.resize(sampleDataOffset, 0);
    for (int i = 0; i < numSamples; i++) {
        auto& s = mod->samples[i];
        if (s.length > 0) {
            int32_t bytesToRead = std::min(s.length, static_cast<int32_t>(buffer.data() + buffer.size() - p));
            if (bytesToRead > 0) {
                std::memcpy(&mod->sampleData[s.offset], p, bytesToRead);
                p += bytesToRead;
            }
        }
    }

    for (int i = 0; i < numSamples; i++) {
        auto& s = mod->samples[i];
        if (s.loopLength < 2) s.loopLength = 2;

        // Clear first two bytes of non-looping samples to prevent beep after playback
        if (s.length >= 2 && s.loopStart + s.loopLength <= 2) {
            mod->sampleData[s.offset] = 0;
            mod->sampleData[s.offset + 1] = 0;
        }
    }

    std::cout << "Loaded: " << mod->name << std::endl;
    std::cout << "Patterns: " << numPatterns << ", Song length: " << (int)mod->songLength << std::endl;

    return mod;
}

} // namespace mod
