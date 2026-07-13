#include "audio.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace mod {

constexpr float AUDIO_GAIN = 2.0f;
constexpr float NORMALIZE_VALUE = AUDIO_GAIN * ((32767.0f + 1.0f) / NUM_CHANNELS);

Audio::Audio() = default;

Audio::~Audio() {
    close();
}

bool Audio::init(int sampleRate, int bufferSize) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        return false;
    }

    SDL_AudioSpec want{}, have{};
    want.freq = sampleRate;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = bufferSize;
    want.callback = audioCallback;
    want.userdata = this;

    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (device == 0) {
        return false;
    }

    outputRate = have.freq;

    int maxSamples = static_cast<int>(std::ceil(outputRate / (MIN_BPM / 2.5))) + 1;
    mixBufferL.resize(maxSamples);
    mixBufferR.resize(maxSamples);

    updateBpmTables();

    samplesPerTickInt = samplesPerTickIntTab[125 - MIN_BPM];
    samplesPerTickFrac = samplesPerTickFracTab[125 - MIN_BPM];
    tickSampleCounter = 0;
    tickSampleCounterFrac = 0;

    return true;
}

void Audio::updateBpmTables() {
    for (int bpm = MIN_BPM; bpm <= MAX_BPM; bpm++) {
        uint32_t ciaPeriod = 1773447 / bpm;
        double hz = CIA_PAL_CLK / (ciaPeriod + 1);
        double samplesPerTick = outputRate / hz;

        double intPart;
        double fracPart = std::modf(samplesPerTick, &intPart);

        int i = bpm - MIN_BPM;
        samplesPerTickIntTab[i] = static_cast<uint32_t>(intPart);
        samplesPerTickFracTab[i] = static_cast<uint64_t>(fracPart * BPM_FRAC_SCALE);
    }
}

void Audio::close() {
    if (device != 0) {
        SDL_PauseAudioDevice(device, 1);
        SDL_CloseAudioDevice(device);
        device = 0;
    }
}

void Audio::start() {
    if (device != 0) {
        SDL_PauseAudioDevice(device, 0);
    }
}

void Audio::pause() {
    if (device != 0) {
        SDL_PauseAudioDevice(device, 1);
    }
}

void Audio::setRenderer(IRenderer* r) {
    std::lock_guard<std::mutex> lock(rendererMutex);
    renderer = r;
}

void Audio::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* audio = static_cast<Audio*>(userdata);
    audio->generateAudio(reinterpret_cast<int16_t*>(stream), len / 4);
}

void Audio::generateAudio(int16_t* stream, int numSamples) {
    std::lock_guard<std::mutex> lock(rendererMutex);

    int samplesLeft = numSamples;
    int16_t* out = stream;

    while (samplesLeft > 0) {
        if (tickSampleCounter <= 0) {
            if (replayer && replayer->isPlaying()) {
                replayer->tick();

                int bpm = replayer->getBPM();
                if (bpm >= MIN_BPM && bpm <= MAX_BPM) {
                    int i = bpm - MIN_BPM;
                    samplesPerTickInt = samplesPerTickIntTab[i];
                    samplesPerTickFrac = samplesPerTickFracTab[i];
                }
            }

            tickSampleCounter = samplesPerTickInt;
            tickSampleCounterFrac += samplesPerTickFrac;
            if (tickSampleCounterFrac >= BPM_FRAC_SCALE) {
                tickSampleCounterFrac -= BPM_FRAC_SCALE;
                tickSampleCounter++;
            }
        }

        int samplesToMix = samplesLeft;
        if (tickSampleCounter > 0 && samplesToMix > tickSampleCounter)
            samplesToMix = tickSampleCounter;

        if (renderer) {
            renderer->generateSamples(mixBufferL.data(), mixBufferR.data(), samplesToMix);
        } else {
            std::memset(mixBufferL.data(), 0, samplesToMix * sizeof(float));
            std::memset(mixBufferR.data(), 0, samplesToMix * sizeof(float));
        }

        for (int i = 0; i < samplesToMix; i++) {
            float fL = mixBufferL[i] * NORMALIZE_VALUE;
            float fR = mixBufferR[i] * NORMALIZE_VALUE;

            int32_t sL = static_cast<int32_t>(fL);
            int32_t sR = static_cast<int32_t>(fR);

            *out++ = static_cast<int16_t>(std::clamp(sL, -32768, 32767));
            *out++ = static_cast<int16_t>(std::clamp(sR, -32768, 32767));
        }

        tickSampleCounter -= samplesToMix;
        samplesLeft -= samplesToMix;
    }
}

} // namespace mod
