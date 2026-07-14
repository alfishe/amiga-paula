# BLEP Renderer Technical Documentation

## Overview

The BLEP (Band-Limited Step) renderer eliminates aliasing artifacts inherent in sample-and-hold DAC emulation by replacing instantaneous step discontinuities with band-limited transitions.

## Algorithm

### The Aliasing Problem

Paula's DAC produces a staircase waveform - when the output changes from one sample value to another, there's an instantaneous vertical step. In the frequency domain, this step contains infinite harmonics that fold back into the audible spectrum when resampled to lower rates (e.g., 48kHz output from 28kHz playback).

### Band-Limited Steps

Instead of outputting raw steps, each transition is convolved with a windowed sinc function (the "minBLEP"). The sinc function is the ideal lowpass filter impulse response - convolving with it removes frequencies above Nyquist.

```cpp
void Blep::add(float offset, float amplitude) {
    float f = offset * BLEP_SP;
    int32_t fInt = static_cast<int32_t>(f);
    const float* src = minBlepData + fInt;
    f -= fInt;

    int32_t i = index;
    for (int n = 0; n < BLEP_NS; n++) {
        buffer[i] += amplitude * lerp(src[0], src[1], f);
        src += BLEP_SP;
        i = (i + 1) & BLEP_RNS;
    }
    samplesLeft = BLEP_NS;
}
```

### BLEP Table

The `minBlepData` table contains 257 precomputed samples of a minimum-phase band-limited step function. Key parameters:

- `BLEP_SP = 8` - subpixel resolution for fractional sample positioning
- `BLEP_NS = 32` - number of output samples affected by each step
- Kaiser window with β ≈ 9 for stopband rejection

The minimum-phase design concentrates most of the energy near time zero, minimizing audible pre-ringing while maintaining anti-aliasing properties.

## Signal Chain

```
Per-channel phase accumulator → Sample fetch → BLEP insertion → Stereo sum → Analog filters → Output
```

### Phase Accumulator

Each voice maintains a phase accumulator that advances by `delta` per output sample:

```cpp
v.phase += v.delta;
if (v.phase >= 1.0f) {
    v.phase -= 1.0f;
    refetchPeriod(v);  // Load new sample
}
```

The fractional phase at transition time determines the BLEP offset for sub-sample accurate placement.

### Analog Filters

Post-mixing filter chain models the Amiga hardware:

| Model | Static LP | LED Filter | High-pass |
|-------|-----------|------------|-----------|
| A500 | ~4.4kHz | ~3.1kHz (when on) | ~5.1Hz |
| A1200 | None | ~3.1kHz (when on) | ~5.3Hz |

Filters are implemented as one-pole IIR (static LP, HP) and two-pole Sallen-Key (LED).

## Performance Characteristics

### Measured Results

Test module: tbl-tint_secondpart.mod @ 48kHz

| Metric | Value |
|--------|-------|
| MaxSlew | 37.3k |
| HF Energy | 1.08 |
| PSNR vs WinUAE | ~23dB |

### Comparison

- **vs PWM**: Similar slew rate, lower HF energy (1.08 vs 1.18). PWM preserves more of Paula's natural harmonics.
- **vs WinUAE**: Close match (PSNR ~23dB) since both use BLEP-style synthesis, but WinUAE has richer HF content (1.24).

## Implementation Notes

### Per-Channel BLEP Buffers

Each channel has its own BLEP ring buffer to handle overlapping transitions. The `buffer[]` array accumulates the "residual" - the difference between the ideal band-limited output and the raw sample value.

```cpp
float Blep::run(float input) {
    float output = input + buffer[index];
    buffer[index] = 0.0f;
    index = (index + 1) & BLEP_RNS;
    samplesLeft--;
    return output;
}
```

### Period Changes

When Paula's period register changes mid-sample, the BLEP needs to account for both the old and new delta values to place transitions correctly:

```cpp
void Paula::refetchPeriod(Voice& v) {
    v.blepPhase = v.phase;
    v.blepDelta = v.delta;
    v.delta = v.storedDelta;
    v.nextSampleStage = true;
}
```

The `blepPhase` and `blepDelta` preserve the state at transition time for accurate offset calculation.

## Code Reference

- `src/paula.cpp` - Main BLEP renderer implementation
- `src/paula.hpp` - Blep struct and Paula class definitions
- `minBlepData[]` - Precomputed 257-entry BLEP table
