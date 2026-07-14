# PWM Renderer Technical Documentation

## Overview

The PWM renderer simulates Paula's audio subsystem at the native DMA clock rate (~3.546895 MHz PAL). Unlike band-limited approaches, it runs the actual timing loop and decimates to the output sample rate using averaging.

## Signal Chain

```
Paula DMA @ 3.55MHz → Per-channel PWM volume → Stereo sum → Decimation → Analog filters → Punch enhancement → Output
```

## Decimation Strategy

### Averaging vs FIR

Early experiments used a Kaiser-windowed FIR filter for decimation. While mathematically correct for anti-aliasing, this approach had a critical flaw: **it smeared transients**.

Measured results with 127-tap FIR:
- MaxSlew: 37.3k (lower than BLEP's 37.3k)
- Transients audibly softened

The final implementation uses simple averaging over the ~74 PWM cycles per output sample. This preserves the step-like character of Paula's DAC output:

- MaxSlew: 41.8k (sharper than BLEP)
- HF Energy: 1.18 (between BLEP 1.08 and WinUAE 1.24)

The averaging itself provides sufficient anti-aliasing because Paula's sample rate (limited by minimum period 113) is well below the decimation ratio.

## Punch Enhancement

### Problem Statement

Clean PWM decimation produces accurate but somewhat clinical output. Real Amiga sound has more "energy" - sharper attacks, more presence. Several approaches were tested:

| Approach | Result |
|----------|--------|
| HPF (coupling caps) | Adds punch but introduces phase shift/ringing in bass |
| Slew limiter | Smooths transients - opposite of desired effect |
| DAC waveshaper | Adds harmonics but not transient definition |
| Pure edge enhance | Very energetic but harsh |
| Pure transient boost | Clean but less energy |

### Hybrid Solution: "Punch"

The final algorithm combines constant edge enhancement with envelope-gated transient boost:

```cpp
constexpr float edgeBlend = 0.08f;   // Constant edge enhancement
constexpr float attack = 0.3f;       // Envelope attack speed  
constexpr float release = 0.998f;    // Envelope release speed
constexpr float transBoost = 0.2f;   // Transient boost amount

float diffL = out[0] - prevOutL;
float diffR = out[1] - prevOutR;

// Envelope follower on difference magnitude
float magL = std::abs(diffL);
envL = (magL > envL) ? magL * attack + envL * (1-attack) : envL * release;

// Edge component (constant)
out[0] += diffL * edgeBlend;

// Transient component (envelope-gated)
out[0] += diffL * envL * transBoost;
```

### Parameter Derivation

Parameters were tuned by comparing against WinUAE and optimizing for:
1. Sharp transients (MaxSlew)
2. Rich harmonics (HF Energy)
3. Subjective "punch" without harshness

Final measurements on test module (tbl-tint_secondpart.mod):

| Renderer | MaxSlew | HF Energy | Character |
|----------|---------|-----------|-----------|
| PWM Clean | 41.8k | 1.03 | Accurate but clinical |
| PWM Edge only | 48.1k | 1.24 | Energetic but harsh |
| PWM Transient only | 53.5k | 1.21 | Clean attacks |
| **PWM Punch** | **50.4k** | **1.23** | **Balanced** |
| WinUAE | 34.0k | 1.07 | Reference |

Punch achieves WinUAE-level harmonics (1.23 vs 1.07) while maintaining sharper transients (50.4k vs 34k).

## Rejected Approaches

### Analog Modeling (Slew, DAC, Saturation, Crosstalk, Noise)

Full analog chain modeling was implemented and tested:

1. **Slew rate limiting** - LF347 op-amp slew (~13 V/µs). Made transients muddy.
2. **DAC waveshaper** - Static nonlinearity with asymmetric distortion. Subtle effect, not worth complexity.
3. **Coupling HPF** - Two cascaded HPFs at 7Hz and 15Hz. Created phase tilt in bass - punchy but ringy.
4. **Output saturation** - Soft/hard clipping. Reduced dynamics without benefit.
5. **Channel crosstalk** - -46dB bleed with LP filter. Inaudible.
6. **Noise floor** - -78dB pink noise. Inaudible.

Conclusion: These effects either degraded quality or were inaudible. The "analog warmth" people remember is largely the LED filter (already modeled) plus amplifier/speaker coloration (out of scope).

### FIR Filter Variations

| Cutoff | Taps | Beta | Result |
|--------|------|------|--------|
| 0.48 | 127 | 2.5 | Original - muddy transients |
| 0.52 | 63 | 2.0 | Better HF, still smeared |
| 0.65 | 15 | 1.5 | Too much aliasing |
| None | - | - | **Best** - sharp, natural |

The FIR was solving a problem that didn't exist. Paula's sample rates are low enough that averaging suffices.

## Phase Linearity

The punch enhancement is **phase-linear** - it only uses the current and previous sample. No IIR filtering in the signal path means no group delay variation with frequency. Bass transients remain tight.

Compare to HPF approach which introduced audible "tilt" on kick drums due to phase shift in the 50-200Hz range.
