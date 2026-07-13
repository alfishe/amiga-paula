#include <iostream>
#include <iomanip>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include "types.hpp"
#include "mod_loader.hpp"
#include "replayer.hpp"
#include "paula.hpp"
#include "audio.hpp"

std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <modfile.mod>\n";
    std::cout << "\nConsole MOD player with Amiga panning and A1200 filter\n";
    std::cout << "\nControls:\n";
    std::cout << "  q - Quit\n";
    std::cout << "  Space bar not available in console mode - press Ctrl+C to stop\n";
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

    mod::Paula paula;
    mod::Replayer replayer;
    mod::Audio audio;

    if (!audio.init(48000, 1024)) {
        std::cerr << "Error: Failed to initialize audio\n";
        return 1;
    }

    paula.setup(audio.getSampleRate(), mod::MODEL_A1200);
    replayer.setPaula(&paula);
    replayer.setModule(module.get());

    audio.setPaula(&paula);
    audio.setReplayer(&replayer);

    replayer.play();
    audio.start();

    std::cout << "\nPlaying: " << module->name << "\n";
    std::cout << "Sample rate: " << audio.getSampleRate() << " Hz\n";
    std::cout << "Filter: A1200 (LED filter responsive)\n";
    std::cout << "Panning: Amiga (L-R-R-L)\n";
    std::cout << "\nPress Ctrl+C to stop...\n\n";

    while (running && replayer.isPlaying()) {
        std::cout << "\rPos: " << std::setw(3) << replayer.getPosition()
                  << "/" << std::setw(3) << (int)module->songLength
                  << " | Pat: " << std::setw(3) << replayer.getPattern()
                  << " | Row: " << std::setw(2) << replayer.getRow()
                  << " | BPM: " << std::setw(3) << replayer.getBPM()
                  << " | Spd: " << std::setw(2) << replayer.getSpeed()
                  << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\n\nStopping playback...\n";

    audio.pause();
    replayer.stop();
    audio.close();

    SDL_Quit();

    return 0;
}
