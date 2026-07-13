#pragma once

#include <cmath>
#include <numbers>

namespace mod {

struct OnePoleFilter {
    float tmpL = 0.0f, tmpR = 0.0f;
    float a0 = 0.0f, b1 = 0.0f;

    void setup(double audioRate, double cutoff) {
        if (cutoff >= audioRate / 2.0)
            cutoff = (audioRate / 2.0) - 1e-4;

        b1 = static_cast<float>(std::exp((-2.0 * std::numbers::pi) * cutoff / audioRate));
        a0 = 1.0f - b1;
    }

    void clear() { tmpL = tmpR = 0.0f; }

    void lowPassStereo(const float* in, float* out) {
        tmpL = (in[0] * a0) + (tmpL * b1);
        out[0] = tmpL;
        tmpR = (in[1] * a0) + (tmpR * b1);
        out[1] = tmpR;
    }

    void highPassStereo(const float* in, float* out) {
        tmpL = (in[0] * a0) + (tmpL * b1);
        out[0] = in[0] - tmpL;
        tmpR = (in[1] * a0) + (tmpR * b1);
        out[1] = in[1] - tmpR;
    }
};

struct TwoPoleFilter {
    float tmpL[4] = {0}, tmpR[4] = {0};
    float a1 = 0.0f, a2 = 0.0f, b1 = 0.0f, b2 = 0.0f;

    void setup(double audioRate, double cutoff, double qFactor) {
        if (cutoff >= audioRate / 2.0)
            cutoff = (audioRate / 2.0) - 1e-4;

        const double a = 1.0 / std::tan((std::numbers::pi * cutoff) / audioRate);
        const double r = 1.0 / qFactor;

        a1 = static_cast<float>(1.0 / (1.0 + r * a + a * a));
        a2 = static_cast<float>(2.0 * a1);
        b1 = static_cast<float>(2.0 * (1.0 - a * a) * a1);
        b2 = static_cast<float>((1.0 - r * a + a * a) * a1);
    }

    void clear() {
        for (int i = 0; i < 4; i++) tmpL[i] = tmpR[i] = 0.0f;
    }

    void lowPassStereo(const float* in, float* out) {
        float LOut = (in[0] * a1) + (tmpL[0] * a2) + (tmpL[1] * a1) - (tmpL[2] * b1) - (tmpL[3] * b2);
        float ROut = (in[1] * a1) + (tmpR[0] * a2) + (tmpR[1] * a1) - (tmpR[2] * b1) - (tmpR[3] * b2);

        tmpL[1] = tmpL[0]; tmpL[0] = in[0];
        tmpL[3] = tmpL[2]; tmpL[2] = LOut;

        tmpR[1] = tmpR[0]; tmpR[0] = in[1];
        tmpR[3] = tmpR[2]; tmpR[2] = ROut;

        out[0] = LOut;
        out[1] = ROut;
    }
};

} // namespace mod
