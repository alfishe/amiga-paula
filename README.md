# amiga-paula

A ProTracker MOD player with cycle-accurate Paula emulation and authentic Amiga audio signal chain modeling.

## Overview

This project implements three distinct audio rendering backends for ProTracker module playback, each representing a different approach to Paula chip emulation. The replayer engine is derived from ProTracker 2.3D behavior, with accurate effect handling and CIA timing.

All renderers model the Amiga's hardwired channel-to-output mapping: channels 0 and 3 route to the left output, channels 1 and 2 to the right (the classic L-R-R-L panning configuration).

## Rendering Backends

### BLEP (Band-Limited Step)

The default renderer uses band-limited step synthesis to eliminate aliasing artifacts inherent in sample-and-hold DAC emulation. When Paula's output changes between sample values, the transition is convolved with a windowed sinc function rather than producing an instantaneous step discontinuity.

This approach preserves the spectral characteristics of the original waveform while suppressing the ultrasonic harmonics that would otherwise fold back into the audible spectrum during resampling. The BLEP table is precomputed using a Kaiser window with parameters tuned for the typical frequency content of tracker music.

Filter chain: configurable A500 or A1200 model with RC high-pass (~5.3 Hz), optional static low-pass, and Sallen-Key LED filter (~3090 Hz, Q ≈ 0.66).

### PWM (DMA Clock Simulation)

This renderer operates at Paula's native DMA clock rate of approximately 3.546895 MHz (PAL), simulating the pulse-width modulation mechanism used for volume control on the original hardware.

Paula implements volume attenuation through PWM at the color clock rate rather than through analog multiplication. This creates subtle intermodulation products and timing artifacts that contribute to the characteristic Amiga sound. The renderer captures sample values at full clock resolution, then decimates to the output sample rate using a polyphase FIR filter.

The decimation filter uses a Kaiser-windowed design with cutoff near the output Nyquist frequency. The ~74:1 decimation factor (3.55 MHz to 48 kHz) requires careful attention to passband flatness and stopband rejection to avoid audible coloration.

### WinUAE (Lankila Sinc Interpolation)

Implements the audio subsystem from WinUAE/UADE, based on Antti S. Lankila's work on accurate Amiga audio emulation.

The core algorithm maintains a queue of output state transitions per channel. At each output sample, the current state is convolved with precomputed BLEP tables that incorporate the frequency response of the target filter model. Five table variants exist:

| Index | Model | LED Filter |
|-------|-------|------------|
| 0 | A500 | Off |
| 1 | A500 | On |
| 2 | A1200 | Off |
| 3 | A1200 | On |
| 4 | Vanilla | N/A |

For A1200 emulation with LED filter disabled, the vanilla table is used since the A1200 has no static RC filtering in the audio path—only the switchable LED filter affects the signal.

The analog filter stage models the A500's cascaded RC network (two poles at approximately 6.2 kHz and 20 kHz) followed by the three-pole LED filter, or the A1200's LED-only topology.

## Filter Models

### A500

The original Amiga 500 audio output stage includes a static low-pass filter comprising two cascaded RC sections. This was likely intended as an anti-imaging filter for the 8-bit DAC, though the cutoff frequencies suggest cost-driven component selection rather than optimal filter design.

The LED filter (active when the power LED is dim) adds three additional poles at approximately 3.3 kHz, implemented as a Sallen-Key active filter in the original hardware. Combined with the static filtering, this produces significant high-frequency rolloff that defines the "warm" A500 sound.

### A1200

The A1200 omits the static RC filter entirely, passing the DAC output directly to the LED filter stage. With the LED filter disabled, the audio path is essentially flat from DC to the DAC's Nyquist frequency (approximately 14 kHz at typical replay rates, limited by Paula's minimum period of 124 cycles).

This cleaner signal path reveals more of the DAC's quantization characteristics and any aliasing present in the source samples.

## Building

Requirements:
- CMake 3.20+
- C++20 compiler
- SDL2

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./modplayer <module.mod>
```

Controls:
- Left/Right arrows: cycle through renderers
- Q or Escape: quit

The current renderer is displayed in the status line during playback.

## Technical Notes

### Sample Loop Handling

ProTracker modules specify non-looping samples with a loop length of 0 or 1 word. The first word of the sample data is conventionally zeroed, and the sample pointer returns to this position after playback completes, producing silence. The replayer handles this by setting `replen = 1` for samples with `loopLength <= 2`.

### Period Limits

Paula's period register accepts values from 0 to 65535, but periods below 113 produce unreliable behavior on real hardware due to DMA timing constraints. All renderers clamp the minimum period to 113 cycles.

### CIA Timing

BPM-to-Hz conversion follows the CIA timer formula: `Hz = (709379 * 5) / (BPM * 2)` for PAL systems. At the default 125 BPM, this yields approximately 50 Hz tick rate (one tick per vertical blank interval).

## License

The sinc interpolation tables are derived from WinUAE/UADE and are Copyright 2005 Antti S. Lankila.

## References

- [pt2-clone](https://github.com/8bitbubsy/pt2-clone) - Reference ProTracker implementation
- [WinUAE](https://github.com/tonioni/WinUAE) - Amiga emulator with Lankila audio code
- [UADE](https://gitlab.com/uade-music-player/uade) - Unix Amiga Delitracker Emulator
