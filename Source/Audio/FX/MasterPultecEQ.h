#pragma once
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// MasterPultecEQ — Pultec EQP-1A style master EQ
//
// Classic passive EQ character: simultaneous boost + atten at low freq
// creates the famous "Pultec trick" dip-then-boost curve.
//
// Low section:  Shelf boost + shelf atten at same selectable freq
// High section: Bell boost + shelf atten at separate freqs
// ═══════════════════════════════════════════════════════════════════
class MasterPultecEQ
{
public:
    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);
        reset();
    }

    void reset()
    {
        for (auto& s : lowBoostState)  s = {0, 0};
        for (auto& s : lowAttenState)  s = {0, 0};
        for (auto& s : highBoostState) s = {0, 0};
        for (auto& s : highAttenState) s = {0, 0};
    }

    // Settings — all in range [0..1] except frequencies in Hz
    struct Settings
    {
        float lowBoostFreq  = 60.0f;   // 20-200 Hz
        float lowBoostAmt   = 0.0f;    // 0-10 dB
        float lowAttenAmt   = 0.0f;    // 0-10 dB (same freq as boost)
        float highBoostFreq = 8000.0f; // 3k-16k Hz
        float highBoostAmt  = 0.0f;    // 0-10 dB
        float highBoostBW   = 1.0f;    // Q: 0.5-3.0
        float highAttenFreq = 10000.0f;// 5k-20k Hz
        float highAttenAmt  = 0.0f;    // 0-10 dB
    };

    void process (float* left, float* right, int numSamples, const Settings& s)
    {
        if (sampleRate < 1.0f) return;

        // ── Low Boost (low shelf) ──
        if (s.lowBoostAmt > 0.05f)
        {
            float gain = std::pow (10.0f, s.lowBoostAmt / 20.0f);
            calcLowShelf (lowBoostCoeffs, s.lowBoostFreq, gain, 0.7f);
            applyBiquad (left, right, numSamples, lowBoostCoeffs, lowBoostState);
        }

        // ── Low Atten (low shelf, inverted) ──
        if (s.lowAttenAmt > 0.05f)
        {
            float gain = std::pow (10.0f, -s.lowAttenAmt / 20.0f);
            calcLowShelf (lowAttenCoeffs, s.lowBoostFreq, gain, 0.7f);
            applyBiquad (left, right, numSamples, lowAttenCoeffs, lowAttenState);
        }

        // ── High Boost (peaking bell) ──
        if (s.highBoostAmt > 0.05f)
        {
            float gain = std::pow (10.0f, s.highBoostAmt / 20.0f);
            float q = std::clamp (s.highBoostBW, 0.3f, 4.0f);
            calcPeakEQ (highBoostCoeffs, s.highBoostFreq, gain, q);
            applyBiquad (left, right, numSamples, highBoostCoeffs, highBoostState);
        }

        // ── High Atten (high shelf, inverted) ──
        if (s.highAttenAmt > 0.05f)
        {
            float gain = std::pow (10.0f, -s.highAttenAmt / 20.0f);
            calcHighShelf (highAttenCoeffs, s.highAttenFreq, gain, 0.7f);
            applyBiquad (left, right, numSamples, highAttenCoeffs, highAttenState);
        }
    }

private:
    float sampleRate = 44100.0f;

    struct Coeffs { float b0, b1, b2, a1, a2; };
    struct State  { float z1, z2; };

    Coeffs lowBoostCoeffs{}, lowAttenCoeffs{}, highBoostCoeffs{}, highAttenCoeffs{};
    State  lowBoostState[2]{}, lowAttenState[2]{}, highBoostState[2]{}, highAttenState[2]{};

    void calcLowShelf (Coeffs& c, float freq, float gain, float q)
    {
        float A  = std::sqrt (gain);
        float w0 = 2.0f * 3.14159265f * std::clamp (freq, 20.0f, sampleRate * 0.45f) / sampleRate;
        float cs = std::cos (w0), sn = std::sin (w0);
        float alpha = sn / (2.0f * q);
        float sqA = 2.0f * std::sqrt (A) * alpha;

        float a0 = (A + 1) + (A - 1) * cs + sqA;
        c.b0 = (A * ((A + 1) - (A - 1) * cs + sqA)) / a0;
        c.b1 = (2 * A * ((A - 1) - (A + 1) * cs))   / a0;
        c.b2 = (A * ((A + 1) - (A - 1) * cs - sqA)) / a0;
        c.a1 = (-2 * ((A - 1) + (A + 1) * cs))      / a0;
        c.a2 = ((A + 1) + (A - 1) * cs - sqA)       / a0;
    }

    void calcHighShelf (Coeffs& c, float freq, float gain, float q)
    {
        float A  = std::sqrt (gain);
        float w0 = 2.0f * 3.14159265f * std::clamp (freq, 20.0f, sampleRate * 0.45f) / sampleRate;
        float cs = std::cos (w0), sn = std::sin (w0);
        float alpha = sn / (2.0f * q);
        float sqA = 2.0f * std::sqrt (A) * alpha;

        float a0 = (A + 1) - (A - 1) * cs + sqA;
        c.b0 = (A * ((A + 1) + (A - 1) * cs + sqA)) / a0;
        c.b1 = (-2 * A * ((A - 1) + (A + 1) * cs))  / a0;
        c.b2 = (A * ((A + 1) + (A - 1) * cs - sqA)) / a0;
        c.a1 = (2 * ((A - 1) - (A + 1) * cs))       / a0;
        c.a2 = ((A + 1) - (A - 1) * cs - sqA)       / a0;
    }

    void calcPeakEQ (Coeffs& c, float freq, float gain, float q)
    {
        float A  = std::sqrt (gain);
        float w0 = 2.0f * 3.14159265f * std::clamp (freq, 20.0f, sampleRate * 0.45f) / sampleRate;
        float cs = std::cos (w0), sn = std::sin (w0);
        float alpha = sn / (2.0f * q);

        float a0 = 1.0f + alpha / A;
        c.b0 = (1.0f + alpha * A) / a0;
        c.b1 = (-2.0f * cs)       / a0;
        c.b2 = (1.0f - alpha * A) / a0;
        c.a1 = (-2.0f * cs)       / a0;
        c.a2 = (1.0f - alpha / A) / a0;
    }

    void applyBiquad (float* left, float* right, int n, const Coeffs& c, State* st)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* data = (ch == 0) ? left : right;
            float z1 = st[ch].z1, z2 = st[ch].z2;
            for (int i = 0; i < n; ++i)
            {
                float x = data[i];
                float y = c.b0 * x + z1;
                z1 = c.b1 * x - c.a1 * y + z2;
                z2 = c.b2 * x - c.a2 * y;
                data[i] = y;
            }
            st[ch].z1 = z1;
            st[ch].z2 = z2;
        }
    }
};
