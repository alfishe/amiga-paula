#pragma once

#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include "renderer.hpp"
#include "replayer.hpp"

namespace mod {

class Audio {
public:
    Audio();
    ~Audio();

    bool init(int sampleRate = 48000, int bufferSize = 1024);
    void close();

    void setReplayer(Replayer* r) { replayer = r; }
    void setRenderer(IRenderer* r);
    IRenderer* getRenderer() const { return renderer; }

    void start();
    void pause();

    int getSampleRate() const { return outputRate; }

private:
    static void audioCallback(void* userdata, Uint8* stream, int len);
    void generateAudio(int16_t* stream, int numSamples);

    SDL_AudioDeviceID device = 0;
    int outputRate = 48000;

    Replayer* replayer = nullptr;
    IRenderer* renderer = nullptr;
    std::mutex rendererMutex;

    std::vector<float> mixBufferL;
    std::vector<float> mixBufferR;

    int32_t samplesPerTickInt = 0;
    uint64_t samplesPerTickFrac = 0;
    int32_t tickSampleCounter = 0;
    uint64_t tickSampleCounterFrac = 0;

    static constexpr uint64_t BPM_FRAC_SCALE = 1ULL << 32;

    void updateBpmTables();
    std::array<uint32_t, MAX_BPM - MIN_BPM + 1> samplesPerTickIntTab{};
    std::array<uint64_t, MAX_BPM - MIN_BPM + 1> samplesPerTickFracTab{};
};

} // namespace mod
