#include <iostream>
#include <iomanip>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include <SDL2/SDL.h>

#include "types.hpp"
#include "mod_loader.hpp"
#include "replayer.hpp"
#include "paula.hpp"
#include "pwm_paula.hpp"
#include "audio.hpp"

std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <modfile.mod>\n";
    std::cout << "\nConsole MOD player with Amiga emulation\n";
    std::cout << "\nRenderers:\n";
    std::cout << "  BLEP - Band-limited step synthesis (fast, accurate)\n";
    std::cout << "  PWM  - PWM clock simulation ~3.55MHz + FIR decimation (authentic)\n";
    std::cout << "\nControls:\n";
    std::cout << "  Left/Right arrow - Switch renderer\n";
    std::cout << "  Q or Ctrl+C      - Quit\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string filename = argv[1];

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto module = mod::loadMod(filename);
    if (!module) {
        std::cerr << "Error: Failed to load module\n";
        return 1;
    }

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        std::cerr << "Error: SDL init failed\n";
        return 1;
    }

    mod::Paula blepPaula;
    mod::PwmPaula pwmPaula;
    mod::Replayer replayer;
    mod::Audio audio;

    if (!audio.init(48000, 1024)) {
        std::cerr << "Error: Failed to initialize audio\n";
        return 1;
    }

    int sampleRate = audio.getSampleRate();
    blepPaula.setup(sampleRate, mod::MODEL_A1200);
    pwmPaula.setup(sampleRate);

    // Start with BLEP renderer
    mod::IRenderer* currentRenderer = &blepPaula;
    mod::RendererType currentType = mod::RendererType::BLEP;

    replayer.setRenderer(currentRenderer);
    replayer.setModule(module.get());

    audio.setRenderer(currentRenderer);
    audio.setReplayer(&replayer);

    replayer.play();
    audio.start();

    std::cout << "\nPlaying: " << module->name << "\n";
    std::cout << "Sample rate: " << sampleRate << " Hz\n";
    std::cout << "Renderer: BLEP (use Left/Right arrows to switch)\n";
    std::cout << "\nPress 'q' or Ctrl+C to stop...\n\n";

    const char* rendererNames[] = {"BLEP", "PWM "};

    while (running && replayer.isPlaying()) {
        // Poll SDL events for keyboard input
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_LEFT:
                        if (currentType != mod::RendererType::BLEP) {
                            currentType = mod::RendererType::BLEP;
                            currentRenderer = &blepPaula;
                            replayer.setRenderer(currentRenderer);
                            audio.setRenderer(currentRenderer);
                            std::cout << "\n>>> Switched to BLEP renderer <<<\n";
                        }
                        break;
                    case SDLK_RIGHT:
                        if (currentType != mod::RendererType::PWM) {
                            currentType = mod::RendererType::PWM;
                            currentRenderer = &pwmPaula;
                            replayer.setRenderer(currentRenderer);
                            audio.setRenderer(currentRenderer);
                            std::cout << "\n>>> Switched to PWM renderer (3.55MHz + FIR) <<<\n";
                        }
                        break;
                    case SDLK_q:
                    case SDLK_ESCAPE:
                        running = false;
                        break;
                }
            }
        }

        std::cout << "\r[" << rendererNames[static_cast<int>(currentType)] << "] "
                  << "Pos: " << std::setw(3) << replayer.getPosition()
                  << "/" << std::setw(3) << (int)module->songLength
                  << " | Pat: " << std::setw(3) << replayer.getPattern()
                  << " | Row: " << std::setw(2) << replayer.getRow()
                  << " | BPM: " << std::setw(3) << replayer.getBPM()
                  << " | Spd: " << std::setw(2) << replayer.getSpeed()
                  << "   " << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\n\nStopping playback...\n";

    audio.pause();
    replayer.stop();
    audio.close();

    SDL_Quit();

    return 0;
}
