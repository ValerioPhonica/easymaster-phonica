#pragma once
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// FDNReverb — 8-line Feedback Delay Network (v2)
//
// Fixes: smooth enable (no clicks), better stereo, brighter tail,
//        size range rebalanced (tight room → big hall)
// ═══════════════════════════════════════════════════════════════════
class FDNReverb
{
public:
    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);

        int maxPre = static_cast<int>(sr * 0.1) + 1;
        preDelayBuf.assign (static_cast<size_t>(maxPre), 0.0f);
        preDelayWritePos = 0;

        int maxER = static_cast<int>(sr * 0.08) + 1;
        erBuf.assign (static_cast<size_t>(maxER), 0.0f);
        erWritePos = 0;

        // FDN lines: max 300ms each
        int maxLen = static_cast<int>(sr * 0.3) + 1;
        for (int i = 0; i < kN; ++i)
        {
            lines[i].buf.assign (static_cast<size_t>(maxLen), 0.0f);
            lines[i].writePos = 0;
            lines[i].dampLP = 0.0f;
            lines[i].dampHP = 0.0f;
            lines[i].lfoPhase = static_cast<float>(i) / static_cast<float>(kN);
            lines[i].smoothLen = baseDelays[i] * sampleRate;
        }

        smoothMix = 0.0f;
        erTapTimes = {{ 0.0043f, 0.0097f, 0.0135f, 0.0189f, 0.0247f, 0.0319f }};
        erTapGains = {{ 0.80f, 0.68f, 0.56f, 0.44f, 0.34f, 0.26f }};
    }

    void reset()
    {
        for (auto& f : preDelayBuf) f = 0.0f;
        for (auto& f : erBuf) f = 0.0f;
        for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.dampLP = l.dampHP = 0.0f; }
        smoothMix = 0.0f;
    }

    void processSample (float inL, float inR, float& outL, float& outR,
                        float size, float damping, float mix,
                        float preDelayMs = 10.0f, float width = 1.0f)
    {
        // Smooth mix to prevent clicks when enabling/disabling
        smoothMix += (mix - smoothMix) * 0.0005f;  // ~40ms ramp
        if (smoothMix < 0.0005f) { outL = inL; outR = inR; return; }

        // Mono sum for reverb input (reverb tank is mono-in, stereo-out)
        float monoIn = (inL + inR) * 0.5f;

        // ── Pre-delay ──
        int preDelaySmp = std::clamp (static_cast<int>(preDelayMs * 0.001f * sampleRate), 0,
                                       static_cast<int>(preDelayBuf.size()) - 1);
        preDelayBuf[static_cast<size_t>(preDelayWritePos)] = monoIn;
        int preRP = preDelayWritePos - preDelaySmp;
        if (preRP < 0) preRP += static_cast<int>(preDelayBuf.size());
        float preDelayed = preDelayBuf[static_cast<size_t>(preRP)];
        preDelayWritePos = (preDelayWritePos + 1) % static_cast<int>(preDelayBuf.size());

        // ── Early Reflections ──
        erBuf[static_cast<size_t>(erWritePos)] = preDelayed;
        float erL = 0.0f, erR = 0.0f;
        float sizeClamp = std::clamp (size, 0.05f, 4.0f);
        for (int t = 0; t < kER; ++t)
        {
            int tapSmp = static_cast<int>(erTapTimes[t] * sampleRate * std::max (0.2f, sizeClamp * 0.5f));
            tapSmp = std::clamp (tapSmp, 0, static_cast<int>(erBuf.size()) - 1);
            int rp = erWritePos - tapSmp;
            if (rp < 0) rp += static_cast<int>(erBuf.size());
            float tap = erBuf[static_cast<size_t>(rp)] * erTapGains[t];
            if (t % 2 == 0) { erL += tap; erR += tap * (1.0f - width * 0.6f); }
            else            { erR += tap; erL += tap * (1.0f - width * 0.6f); }
        }
        erWritePos = (erWritePos + 1) % static_cast<int>(erBuf.size());

        // ── FDN Read ──
        std::array<float, kN> delOut {};
        for (int i = 0; i < kN; ++i)
        {
            // Size mapping: quadratic for better range at short settings
            // size 0.1 → tiny room, 1.0 → medium hall, 3.0 → cathedral
            float sizeScale = sizeClamp * sizeClamp * 0.4f + sizeClamp * 0.3f;
            float targetLen = baseDelays[i] * sampleRate * std::clamp (sizeScale, 0.05f, 3.5f);
            // Slower smoothing = no clicks when size changes
            lines[i].smoothLen += (targetLen - lines[i].smoothLen) * 0.001f;
            float lfoMod = std::sin (lines[i].lfoPhase * 6.2832f) * 8.0f;
            float totalD = std::max (1.0f, std::min (lines[i].smoothLen + lfoMod,
                static_cast<float>(lines[i].buf.size()) - 2.0f));

            // Linear interpolation
            int d0 = static_cast<int>(totalD);
            float frac = totalD - static_cast<float>(d0);
            int rp0 = (lines[i].writePos - d0 + static_cast<int>(lines[i].buf.size())) % static_cast<int>(lines[i].buf.size());
            int rp1 = (rp0 - 1 + static_cast<int>(lines[i].buf.size())) % static_cast<int>(lines[i].buf.size());
            delOut[i] = lines[i].buf[static_cast<size_t>(rp0)] * (1.0f - frac)
                      + lines[i].buf[static_cast<size_t>(rp1)] * frac;

            // LFO advance
            lines[i].lfoPhase += (0.25f + static_cast<float>(i) * 0.06f) / sampleRate;
            if (lines[i].lfoPhase >= 1.0f) lines[i].lfoPhase -= 1.0f;
        }

        // ── Hadamard mixing ──
        std::array<float, kN> mixed = delOut;
        hadamard8 (mixed);

        // ── Feedback with LP + HP damping ──
        // Decay maps to feedback gain — wider range
        float rt60 = 0.1f + sizeClamp * 2.0f; // 0.1s to 8s+ reverb time
        float decayGain = std::pow (0.001f, 1.0f / (rt60 * sampleRate * 0.05f));
        decayGain = std::clamp (decayGain, 0.0f, 0.997f);

        float lpCoeff = 0.08f + damping * 0.82f; // LP damping
        float hpCoeff = 0.003f + (1.0f - damping) * 0.015f; // HP to keep mid-highs

        for (int i = 0; i < kN; ++i)
        {
            float fb = mixed[i] * decayGain + preDelayed * 0.06f;

            // LP damping (high freqs decay faster = natural)
            lines[i].dampLP = fb + lpCoeff * (lines[i].dampLP - fb);
            // HP to prevent low-end buildup and keep definition
            lines[i].dampHP += hpCoeff * (lines[i].dampLP - lines[i].dampHP);
            float dampedSig = lines[i].dampLP - lines[i].dampHP;

            // Flush denormals (prevents clicks when reverb tail fades)
            if (std::abs (lines[i].dampLP) < 1e-18f) lines[i].dampLP = 0.0f;
            if (std::abs (lines[i].dampHP) < 1e-18f) lines[i].dampHP = 0.0f;

            // NaN/Inf protection (catches filter blowup from extreme params)
            if (! std::isfinite (lines[i].dampLP)) lines[i].dampLP = 0.0f;
            if (! std::isfinite (lines[i].dampHP)) lines[i].dampHP = 0.0f;
            if (! std::isfinite (dampedSig)) dampedSig = 0.0f;

            // Soft limit
            dampedSig = std::tanh (dampedSig);

            lines[i].buf[static_cast<size_t>(lines[i].writePos)] = dampedSig;
            lines[i].writePos = (lines[i].writePos + 1) % static_cast<int>(lines[i].buf.size());
        }

        // ── Stereo output: decorrelated pairs ──
        float lateL = 0.0f, lateR = 0.0f;
        // Use different line combinations for better stereo image
        lateL = (delOut[0] + delOut[2] - delOut[4] + delOut[6]) * 0.25f;
        lateR = (delOut[1] - delOut[3] + delOut[5] + delOut[7]) * 0.25f;

        // Width control
        float mid = (lateL + lateR) * 0.5f;
        float side = (lateL - lateR) * 0.5f;
        side *= (0.5f + width * 1.5f); // boost side for more stereo
        lateL = mid + side;
        lateR = mid - side;

        // Combine ER + late
        float wetL = erL * 0.4f + lateL * 1.2f;
        float wetR = erR * 0.4f + lateR * 1.2f;

        // Additive send-style mix: dry stays at 100%, wet is added on top
        // This prevents the volume drop that equal-power crossfade causes
        outL = inL + wetL * smoothMix;
        outR = inR + wetR * smoothMix;

        // Safety: prevent NaN/inf from propagating
        if (!std::isfinite (outL)) outL = inL;
        if (!std::isfinite (outR)) outR = inR;
    }

private:
    static constexpr int kN = 8;
    static constexpr int kER = 6;
    float sampleRate = 44100.0f;
    float smoothMix = 0.0f;

    std::vector<float> preDelayBuf;
    int preDelayWritePos = 0;
    std::vector<float> erBuf;
    int erWritePos = 0;
    std::array<float, kER> erTapTimes {}, erTapGains {};

    // Prime-adjacent delay times (in seconds)
    std::array<float, kN> baseDelays {{ 0.0297f, 0.0337f, 0.0389f, 0.0431f,
                                         0.0479f, 0.0523f, 0.0571f, 0.0619f }};

    struct DelayLine {
        std::vector<float> buf;
        int writePos = 0;
        float dampLP = 0.0f, dampHP = 0.0f;
        float lfoPhase = 0.0f, smoothLen = 0.0f;
    };
    std::array<DelayLine, kN> lines;

    static void hadamard8 (std::array<float, 8>& x)
    {
        for (int i = 0; i < 8; i += 2) { float a=x[i], b=x[i+1]; x[i]=a+b; x[i+1]=a-b; }
        for (int i = 0; i < 8; i += 4) { float a=x[i], b=x[i+2], c=x[i+1], d=x[i+3]; x[i]=a+b; x[i+2]=a-b; x[i+1]=c+d; x[i+3]=c-d; }
        for (int i = 0; i < 4; ++i) { float a=x[i], b=x[i+4]; x[i]=a+b; x[i+4]=a-b; }
        constexpr float n = 0.35355339f;
        for (auto& v : x) v *= n;
    }
};
