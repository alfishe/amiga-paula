#include <iostream>
#include <iomanip>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <conio.h>
#include <SDL.h>
#else
#include <termios.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#endif

#include "types.hpp"
#include "mod_loader.hpp"
#include "replayer.hpp"
#include "paula.hpp"
#include "pwm_paula.hpp"
#include "winuae_paula.hpp"
#include "audio.hpp"

std::atomic<bool> running{true};

#ifdef _WIN32
void enableRawMode() {}
void disableRawMode() {}

int readKey() {
    if (_kbhit()) {
        int ch = _getch();
        if (ch == 0 || ch == 224) {
            ch = _getch();
            if (ch == 75) return -1;  // Left arrow
            if (ch == 77) return -2;  // Right arrow
        }
        return ch;
    }
    return 0;
}
#else
struct termios origTermios;
bool termiosSet = false;

void disableRawMode() {
    if (termiosSet) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
        termiosSet = false;
    }
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &origTermios);
    termiosSet = true;
    atexit(disableRawMode);

    struct termios raw = origTermios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int readKey() {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 27) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'D') return -1;  // Left arrow
                    if (seq[1] == 'C') return -2;  // Right arrow
                }
            }
            return 27;  // ESC
        }
        return c;
    }
    return 0;
}
#endif

void signalHandler(int) {
    running = false;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <modfile.mod>\n";
    std::cout << "\nConsole MOD player with Amiga emulation\n";
    std::cout << "\nRenderers:\n";
    std::cout << "  BLEP   - Band-limited step synthesis (fast, accurate)\n";
    std::cout << "  PWM    - PWM clock simulation ~3.55MHz + FIR decimation (authentic)\n";
    std::cout << "  WinUAE - Lankila's sinc interpolation + filter model (WinUAE/UADE)\n";
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
#ifndef _WIN32
    std::signal(SIGTERM, signalHandler);
#endif

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
    mod::WinuaePaula winuaePaula;
    mod::Replayer replayer;
    mod::Audio audio;

    if (!audio.init(48000, 1024)) {
        std::cerr << "Error: Failed to initialize audio\n";
        return 1;
    }

    int sampleRate = audio.getSampleRate();
    blepPaula.setup(sampleRate, mod::MODEL_A1200);
    pwmPaula.setup(sampleRate);
    winuaePaula.setup(sampleRate, mod::WinuaePaula::FILTER_MODEL_A1200);

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

    enableRawMode();

    constexpr int NUM_RENDERERS = 3;
    const char* rendererNames[] = {"BLEP  ", "PWM   ", "WinUAE"};
    mod::IRenderer* renderers[] = { &blepPaula, &pwmPaula, &winuaePaula };
    int currentIdx = 0;

    auto switchRenderer = [&](int newIdx) {
        currentIdx = newIdx;
        currentType = static_cast<mod::RendererType>(newIdx);
        currentRenderer = renderers[newIdx];
        replayer.setRenderer(currentRenderer);
        audio.setRenderer(currentRenderer);

        const char* desc[] = {
            "BLEP (band-limited synthesis)",
            "PWM (3.55MHz + FIR decimation)",
            "WinUAE (Lankila sinc + filter model)"
        };
        std::cout << "\n>>> " << desc[newIdx] << " <<<\n";
    };

    while (running && replayer.isPlaying()) {
        int key = readKey();
        if (key == 'q' || key == 'Q' || key == 27) {
            running = false;
        } else if (key == -1) {  // Left arrow
            switchRenderer((currentIdx - 1 + NUM_RENDERERS) % NUM_RENDERERS);
        } else if (key == -2) {  // Right arrow
            switchRenderer((currentIdx + 1) % NUM_RENDERERS);
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

    disableRawMode();
    std::cout << "\n\nStopping playback...\n";

    audio.pause();
    replayer.stop();
    audio.close();

    SDL_Quit();

    return 0;
}
