# PWM Renderer Technical Documentation

## Overview

The PWM renderer simulates Paula's audio subsystem at the native DMA clock rate (~3.546895 MHz PAL). Unlike band-limited approaches, it runs the actual timing loop and decimates to the output sample rate.

## Signal Chain

```
Paula DMA @ 3.55MHz → Per-channel PWM volume → Stereo sum → CIC decimation → Analog filters → Punch enhancement → Room simulation (optional) → Output
```

## Decimation Strategy

### CIC (Boxcar Averaging)

The decimation uses averaging over ~74 PWM cycles per output sample. This is a **first-order CIC filter** (Cascaded Integrator-Comb), not "no filter":

- Boxcar of length N has sinc frequency response
- First null at output sample rate (48kHz)
- Rolloff approximately **-3.9 dB at Nyquist**
- Natural soft HF attenuation without phase smearing

This explains the "sharp but not harsh" character: the CIC passes Paula's useful harmonics while gently attenuating the very top, unlike a brick-wall FIR which would cut more aggressively.

### Why Not FIR

Early experiments used a Kaiser-windowed FIR filter (127 taps, cutoff 0.48). Problems:
- **Smeared transients** - group delay spread the attack energy
- Cut useful Paula harmonics that the CIC preserves
- MaxSlew dropped to 37.3k vs CIC's 41.8k

The CIC's gentle rolloff matches real analog anti-alias behavior better than a steep digital filter.

### CIC Artifacts

Note: CIC frequency response has nulls at multiples of output_rate/N. For certain Paula periods that align with these nulls, output may be slightly quieter. This is mathematically expected behavior.

## Punch Enhancement

**Note: This is sound design, not emulation.** The punch effect adds character beyond what Paula hardware produces. It should be validated against real hardware recordings, not other emulators.

### What It Actually Is

The "punch" algorithm is a combination of:

1. **High-shelf / tilt EQ** - `diff * edgeBlend` adds first difference, which is +6 dB/octave boost
2. **Upward expander on HF** - envelope-gated transient boost that increases HF content on attacks

This is essentially a **transient designer + exciter**.

### Implementation

```cpp
constexpr float edgeBlend = 0.08f;   // +6 dB/oct tilt amount
constexpr float attack = 0.3f;       // Envelope attack
constexpr float release = 0.998f;    // ~11ms @ 48kHz
constexpr float transBoost = 0.2f;   // Transient expansion amount

float diffL = out[0] - prevOutL;

// Envelope follower
float magL = std::abs(diffL);
envL = (magL > envL) ? magL * attack + envL * (1-attack) : envL * release;

// Tilt component (constant +6 dB/oct)
out[0] += diffL * edgeBlend;

// Transient expansion (envelope-gated)
out[0] += diffL * envL * transBoost;
```

### Known Limitations

1. **Diff boosts to Nyquist** - The first difference has maximum gain at highest frequencies. This boosts 12-20kHz zone where 8-bit quantization noise and hi-hat sizzle live. A future improvement would lowpass the diff through a one-pole at ~10-12kHz to focus the boost in the 2-8kHz presence zone.

2. **Release time** - At 0.998 (τ≈11ms @ 48kHz), the envelope barely decays between attacks on dense material. The transient component degenerates into a constant expander, duplicating the edge effect. Longer release (~0.9995, τ≈45ms) with a noise gate would be more selective.

3. **No diff clamping** - On extreme sample transitions, `diff * env * transBoost` can spike. Should add soft limiting.

4. **Metrics are circular** - We measure MaxSlew/HF Energy, but the punch effect directly inflates these metrics. The measurements don't prove accuracy, only that the effect is working.

### Proper Validation

The only valid reference is **line-out recording from real A1200** with:
- Loudness matched to 0.1 dB
- Blind A/B comparison
- Null test (subtract, check residual)

Comparing against WinUAE (which has its own compromises) doesn't establish ground truth.

### Measured Effect

Test module: tbl-tint_secondpart.mod

| Renderer | MaxSlew | Character |
|----------|---------|-----------|
| PWM (no punch) | 41.8k | Clean reference |
| PWM + Punch | 50.4k | +20% transient boost |
| BLEP | 21.8k | Softer |
| WinUAE | 20.3k | Softest |

The punch adds ~20% to transient slew rate. Whether this matches real hardware or exceeds it is **unknown without hardware measurement**.

## Rejected Approaches

### Analog Modeling (Slew, DAC, Saturation, Crosstalk, Noise)

Full analog chain modeling was implemented and tested:

1. **Slew rate limiting** - LF347 op-amp slew (~13 V/µs). Made transients muddy.
2. **DAC waveshaper** - Static nonlinearity with asymmetric distortion. Subtle effect, not worth complexity.
3. **Coupling HPF** - Two cascaded HPFs at 7Hz and 15Hz. Created phase tilt in bass - punchy but ringy.
4. **Output saturation** - Soft/hard clipping. Reduced dynamics without benefit.
5. **Channel crosstalk** - -46dB bleed with LP filter. Inaudible (and this is accurate - real Paula crosstalk is at this level).
6. **Noise floor** - -78dB pink noise. Inaudible.

### Why Not Crosstalk for Stereo "Glue"

The intuition that crosstalk would "glue" the hard L-R-R-L panning is incorrect. Real Paula's electrical crosstalk is ~-46dB - inaudible by design.

What actually made Amiga sound "spatial" was **air between speakers**. For headphone listening, the correct solution is **crossfeed** (see Future Work), not crosstalk.

### FIR Filter Variations

| Cutoff | Taps | Beta | Result |
|--------|------|------|--------|
| 0.48 | 127 | 2.5 | Original - muddy transients |
| 0.52 | 63 | 2.0 | Better HF, still smeared |
| 0.65 | 15 | 1.5 | Too much aliasing |
| CIC | - | - | **Best** - natural rolloff |

The FIR cut useful Paula harmonics. CIC's gentle -3.9dB rolloff at Nyquist preserves them.

## Phase Linearity

The punch enhancement uses only current and previous samples (first difference). This is **linear phase** - no group delay variation, no bass smearing.

Compare to HPF approach which introduced audible phase shift in the 50-200Hz range.

## Future Work

### Room Simulation (Implemented)

For headphone listening, hard L-R-R-L panning is fatiguing. Traditional crossfeed (bs2b, Meier) uses ~600Hz lowpass to simulate head shadow, but this removes high-frequency clarity.

The current implementation uses **room simulation** instead:
- 3ms delay (early reflection timing, not ITD)
- ~10kHz lowpass (air absorption, not head shadow)
- Levels from -15dB to -9dB

This preserves crystal clarity while reducing stereo fatigue.

| Mode | Linear | dB | Recommendation |
|------|--------|-----|----------------|
| Room -15dB | 0.178 | -15 | Subtle, safe for all material |
| Room -14dB | 0.20 | -14 | **Recommended** for most music |
| Room -13dB | 0.224 | -13 | Light, good for slower tracks |
| Room -12dB | 0.25 | -12 | Moderate, may color transients |
| Room -9dB | 0.35 | -9 | Strong, for ambient/pad-heavy music |

### Why Higher Levels Sound "Too Much"

The 3ms delay creates comb filtering with notches at 167 Hz, 500 Hz, 833 Hz, etc. At -12dB and above, these notches become audible as:
- Subtle "double hit" on transients (kicks, hats)
- Smearing on fast-changing material (techno, breakbeats)

On transient-heavy music, stick to -14dB or -15dB. The higher levels exist for ambient, chiptune, or slower tracker music where spatial width matters more than transient precision.

### Rejected Spatial Approaches

1. **Traditional crossfeed** (0.3ms ITD, 600Hz LP) - removed clarity immediately, even at -1dB
2. **Tilt filter crossfeed** (-4dB/oct shelf) - still colored the sound noticeably
3. **Early reflections** (discrete wall taps) - added complexity without improvement over simple room

### Punch Improvements

1. Lowpass the diff signal at ~10-12kHz before adding - focuses energy in presence zone (2-8kHz) instead of boosting noise/sizzle
2. Longer release (~45ms) with noise gate - more selective transient detection
3. Soft clamp on transient component - prevent spikes on extreme transitions

### Hardware Validation

Record line-out from real A1200, loudness-match, blind A/B. This is the only way to know if punch exceeds real hardware character.
