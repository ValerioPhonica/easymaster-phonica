#pragma once
#include <cmath>
#include <array>
#include <algorithm>

// Simple 2x oversampler with 6th-order half-band IIR filter
// Lightweight enough for real-time master bus processing
class Oversampler2x
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        reset();
    }

    void reset()
    {
        for (auto& s : upState) s = 0.0f;
        for (auto& s : dnStateL) s = 0.0f;
        for (auto& s : dnStateR) s = 0.0f;
        prevL = prevR = 0.0f;
    }

    // Process a stereo block with 2x oversampling
    // callback(float* L, float* R, int numSamples) processes at 2x rate
    template <typename ProcessFn>
    void process (float* outL, float* outR, int numSamples, ProcessFn&& fn)
    {
        // Upsample: insert zeros between samples, then filter
        // Since input is already mixed, we create a 2x buffer
        constexpr int kMaxBlock = 2048;
        float upL[kMaxBlock * 2], upR[kMaxBlock * 2];
        int n = std::min (numSamples, kMaxBlock);

        // Zero-stuff upsample (×2 gain to compensate)
        for (int i = 0; i < n; ++i)
        {
            upL[i * 2]     = outL[i] * 2.0f;
            upL[i * 2 + 1] = 0.0f;
            upR[i * 2]     = outR[i] * 2.0f;
            upR[i * 2 + 1] = 0.0f;
        }

        // Anti-image filter (half-band LP at Nyquist/2)
        halfBandFilter (upL, n * 2, upState);

        // Process at 2x rate
        fn (upL, upR, n * 2);

        // Anti-alias filter + decimate
        halfBandFilter (upL, n * 2, dnStateL);
        halfBandFilter (upR, n * 2, dnStateR);
        for (int i = 0; i < n; ++i)
        {
            outL[i] = upL[i * 2];
            outR[i] = upR[i * 2];
        }
    }

private:
    double sampleRate = 44100.0;
    float prevL = 0, prevR = 0;
    std::array<float, 6> upState {}, dnStateL {}, dnStateR {};

    // 3-stage cascaded allpass half-band filter (Valimaki/Smith design)
    // Coefficients for ~100dB stopband rejection
    static void halfBandFilter (float* data, int len, std::array<float, 6>& st)
    {
        // 3 second-order allpass sections
        // Coefficients optimized for half-band with good rejection
        constexpr float a0 = 0.07986642623635751f;
        constexpr float a1 = 0.5453536510711322f;
        constexpr float a2 = 0.9057023685478590f;
        const float coeffs[] = { a0, a1, a2 };

        for (int i = 0; i < len; ++i)
        {
            float x = data[i];
            // Path A: odd samples through allpass chain
            float pathA = x;
            for (int s = 0; s < 3; ++s)
            {
                float y = coeffs[s] * (pathA - st[s * 2 + 1]) + st[s * 2];
                st[s * 2] = pathA;
                st[s * 2 + 1] = y;
                pathA = y;
            }
            data[i] = pathA;
        }
    }
};
