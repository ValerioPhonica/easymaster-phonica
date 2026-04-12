#pragma once
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// PlateReverb — Dattorro-style plate reverb
//
// 4 input diffusers → 2 parallel tank branches with modulated allpass,
// different output taps for wide stereo. Classic 80s plate character.
// ═══════════════════════════════════════════════════════════════════
class PlateReverb
{
public:
    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);
        int maxLen = static_cast<int>(sr * 0.15) + 1;

        for (auto& ap : inputAP)  { ap.buf.assign (static_cast<size_t>(maxLen), 0.0f); ap.pos = 0; }
        for (auto& ap : tankAPL)  { ap.buf.assign (static_cast<size_t>(maxLen), 0.0f); ap.pos = 0; }
        for (auto& ap : tankAPR)  { ap.buf.assign (static_cast<size_t>(maxLen), 0.0f); ap.pos = 0; }
        for (auto& dl : tankDelL) { dl.buf.assign (static_cast<size_t>(maxLen), 0.0f); dl.pos = 0; }
        for (auto& dl : tankDelR) { dl.buf.assign (static_cast<size_t>(maxLen), 0.0f); dl.pos = 0; }

        dampL = dampR = 0.0f;
        smoothMix = 0.0f;
        lfoPhase = 0.0;

        // Input allpass lengths (prime-ish, scaled to SR)
        setLen (inputAP[0], 0.00457f); setLen (inputAP[1], 0.00683f);
        setLen (inputAP[2], 0.00981f); setLen (inputAP[3], 0.01327f);
        // Tank allpass lengths
        setLen (tankAPL[0], 0.02017f); setLen (tankAPL[1], 0.03163f);
        setLen (tankAPR[0], 0.02227f); setLen (tankAPR[1], 0.03491f);
        // Tank delay lengths
        setLen (tankDelL[0], 0.04957f); setLen (tankDelL[1], 0.06733f);
        setLen (tankDelR[0], 0.05381f); setLen (tankDelR[1], 0.07247f);
    }

    void reset()
    {
        for (auto& ap : inputAP)  std::fill (ap.buf.begin(), ap.buf.end(), 0.0f);
        for (auto& ap : tankAPL)  std::fill (ap.buf.begin(), ap.buf.end(), 0.0f);
        for (auto& ap : tankAPR)  std::fill (ap.buf.begin(), ap.buf.end(), 0.0f);
        for (auto& dl : tankDelL) std::fill (dl.buf.begin(), dl.buf.end(), 0.0f);
        for (auto& dl : tankDelR) std::fill (dl.buf.begin(), dl.buf.end(), 0.0f);
        dampL = dampR = 0.0f; smoothMix = 0.0f;
    }

    void processSample (float inL, float inR, float& outL, float& outR,
                        float size, float damp, float mix,
                        float /*preDelay*/ = 0.0f, float /*erLevel*/ = 0.0f)
    {
        // Smooth mix
        float targetMix = std::clamp (mix, 0.0f, 1.0f);
        smoothMix += (targetMix - smoothMix) * 0.001f;
        if (smoothMix < 0.001f) { outL = inL; outR = inR; return; }

        float decay = std::clamp (0.3f + size * 0.42f, 0.1f, 0.97f);
        float dampCoeff = 0.15f + damp * 0.7f;
        float inDiff = 0.75f;
        float tankDiff = 0.5f + size * 0.15f;

        // Input: mono sum → 4 allpass diffusers
        float sig = (inL + inR) * 0.5f;
        sig = processAP (inputAP[0], sig, inDiff);
        sig = processAP (inputAP[1], sig, inDiff);
        sig = processAP (inputAP[2], sig, inDiff);
        sig = processAP (inputAP[3], sig, inDiff);

        // Modulation
        lfoPhase += 0.7 / sampleRate;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        float lfo = std::sin (static_cast<float>(lfoPhase * 6.283185307)) * 0.0003f * sampleRate;

        // Tank L
        float tL = sig + readDel (tankDelR[1]) * decay;
        tL = processAP (tankAPL[0], tL, tankDiff);
        tL = processAPMod (tankAPL[1], tL, tankDiff, lfo);
        dampL += (tL - dampL) * (1.0f - dampCoeff);
        writeDel (tankDelL[0], dampL * decay);
        float tapL1 = readDel (tankDelL[0], 0.65f);
        writeDel (tankDelL[1], readDel (tankDelL[0]));

        // Tank R
        float tR = sig + readDel (tankDelL[1]) * decay;
        tR = processAP (tankAPR[0], tR, tankDiff);
        tR = processAPMod (tankAPR[1], tR, tankDiff, -lfo);
        dampR += (tR - dampR) * (1.0f - dampCoeff);
        writeDel (tankDelR[0], dampR * decay);
        float tapR1 = readDel (tankDelR[0], 0.65f);
        writeDel (tankDelR[1], readDel (tankDelR[0]));

        // Output taps (cross-channel for width)
        float wetL = (tapL1 + readDel (tankDelR[0], 0.4f)) * 0.5f;
        float wetR = (tapR1 + readDel (tankDelL[0], 0.4f)) * 0.5f;

        float w = smoothMix;
        outL = inL * (1.0f - w * 0.5f) + wetL * w;
        outR = inR * (1.0f - w * 0.5f) + wetR * w;
    }

private:
    struct APBuf { std::vector<float> buf; int pos = 0; int len = 100; };
    struct DelBuf { std::vector<float> buf; int pos = 0; int len = 100; };

    std::array<APBuf, 4> inputAP;
    std::array<APBuf, 2> tankAPL, tankAPR;
    std::array<DelBuf, 2> tankDelL, tankDelR;
    float dampL = 0.0f, dampR = 0.0f, smoothMix = 0.0f;
    float sampleRate = 44100.0f;
    double lfoPhase = 0.0;

    void setLen (APBuf& ap, float sec) { ap.len = std::max (1, static_cast<int>(sec * sampleRate)); }
    void setLen (DelBuf& dl, float sec) { dl.len = std::max (1, static_cast<int>(sec * sampleRate)); }

    float processAP (APBuf& ap, float in, float coeff)
    {
        int readPos = (ap.pos - ap.len + static_cast<int>(ap.buf.size())) % static_cast<int>(ap.buf.size());
        float delayed = ap.buf[static_cast<size_t>(readPos)];
        float v = in - delayed * coeff;
        ap.buf[static_cast<size_t>(ap.pos)] = v;
        ap.pos = (ap.pos + 1) % static_cast<int>(ap.buf.size());
        return delayed + v * coeff;
    }

    float processAPMod (APBuf& ap, float in, float coeff, float modSamples)
    {
        float readF = static_cast<float>(ap.pos) - static_cast<float>(ap.len) + modSamples;
        while (readF < 0) readF += static_cast<float>(ap.buf.size());
        int idx0 = static_cast<int>(readF) % static_cast<int>(ap.buf.size());
        int idx1 = (idx0 + 1) % static_cast<int>(ap.buf.size());
        float frac = readF - std::floor (readF);
        float delayed = ap.buf[static_cast<size_t>(idx0)] * (1.0f - frac) + ap.buf[static_cast<size_t>(idx1)] * frac;
        float v = in - delayed * coeff;
        ap.buf[static_cast<size_t>(ap.pos)] = v;
        ap.pos = (ap.pos + 1) % static_cast<int>(ap.buf.size());
        return delayed + v * coeff;
    }

    void writeDel (DelBuf& dl, float v)
    {
        dl.buf[static_cast<size_t>(dl.pos)] = v;
        dl.pos = (dl.pos + 1) % static_cast<int>(dl.buf.size());
    }

    float readDel (DelBuf& dl, float fraction = 1.0f)
    {
        int offset = std::max (1, static_cast<int>(static_cast<float>(dl.len) * fraction));
        int idx = (dl.pos - offset + static_cast<int>(dl.buf.size())) % static_cast<int>(dl.buf.size());
        return dl.buf[static_cast<size_t>(idx)];
    }
};

// ═══════════════════════════════════════════════════════════════════
// ShimmerReverb — FDN with pitch-shifted feedback (+12st)
//
// Ethereal, rising reverb tails. Uses a simple granular pitch shifter
// in the feedback loop. Great for pads, ambient, cinematic.
// ═══════════════════════════════════════════════════════════════════
class ShimmerReverb
{
public:
    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);
        int maxLen = static_cast<int>(sr * 0.25) + 1;

        for (int i = 0; i < kN; ++i)
        {
            lines[i].buf.assign (static_cast<size_t>(maxLen), 0.0f);
            lines[i].pos = 0;
            lines[i].damp = 0.0f;
        }

        // Pitch shifter buffer (granular, ~100ms window)
        int psLen = static_cast<int>(sr * 0.1) + 1;
        psBuf.assign (static_cast<size_t>(psLen), 0.0f);
        psWritePos = 0;
        psPhase = 0.0;

        smoothMix = 0.0f;
    }

    void reset()
    {
        for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.damp = 0.0f; }
        std::fill (psBuf.begin(), psBuf.end(), 0.0f);
        smoothMix = 0.0f;
    }

    void processSample (float inL, float inR, float& outL, float& outR,
                        float size, float damp, float mix,
                        float /*preDelay*/ = 0.0f, float /*erLevel*/ = 0.0f)
    {
        float targetMix = std::clamp (mix, 0.0f, 1.0f);
        smoothMix += (targetMix - smoothMix) * 0.001f;
        if (smoothMix < 0.001f) { outL = inL; outR = inR; return; }

        float decay = std::clamp (0.4f + size * 0.38f, 0.2f, 0.95f);
        float dampCoeff = 0.1f + damp * 0.75f;
        float shimmerAmt = std::clamp (size * 0.5f, 0.0f, 0.6f); // more size → more shimmer

        float input = (inL + inR) * 0.5f;

        // Read from delay lines
        float sumL = 0.0f, sumR = 0.0f;
        static constexpr float delays[kN] = {0.0297f, 0.0371f, 0.0411f, 0.0461f,
                                              0.0537f, 0.0601f, 0.0667f, 0.0733f};
        for (int i = 0; i < kN; ++i)
        {
            int len = std::max (1, static_cast<int>(delays[i] * sampleRate * (0.6f + size * 0.6f)));
            int rp = (lines[i].pos - len + static_cast<int>(lines[i].buf.size())) % static_cast<int>(lines[i].buf.size());
            float s = lines[i].buf[static_cast<size_t>(rp)];
            if (i < kN / 2) sumL += s; else sumR += s;
        }
        sumL *= 0.25f; sumR *= 0.25f;

        // Pitch shift the feedback (+12 semitones = 2× speed)
        float fbMono = (sumL + sumR) * 0.5f;
        psBuf[static_cast<size_t>(psWritePos)] = fbMono;
        int psBufLen = static_cast<int>(psBuf.size());

        // Two overlapping grains reading at 2× speed
        float shifted = 0.0f;
        float grainLen = sampleRate * 0.05f; // 50ms grain
        for (int g = 0; g < 2; ++g)
        {
            double ph = psPhase + g * 0.5;
            if (ph >= 1.0) ph -= 1.0;
            float readOffset = static_cast<float>(ph * grainLen);
            float readF = static_cast<float>(psWritePos) - readOffset;
            if (readF < 0) readF += static_cast<float>(psBufLen);
            int idx0 = static_cast<int>(readF) % psBufLen;
            int idx1 = (idx0 + 1) % psBufLen;
            float frac = readF - std::floor (readF);
            float s = psBuf[static_cast<size_t>(idx0)] * (1.0f - frac) + psBuf[static_cast<size_t>(idx1)] * frac;
            // Hann window
            float w = 0.5f - 0.5f * std::cos (static_cast<float>(ph * 6.283185307));
            shifted += s * w;
        }
        psPhase += 2.0 / static_cast<double>(grainLen); // 2× = +12st
        if (psPhase >= 1.0) psPhase -= 1.0;
        psWritePos = (psWritePos + 1) % psBufLen;

        // Write back to lines: input + decayed feedback + shimmer
        float fb = fbMono * decay * (1.0f - shimmerAmt) + shifted * decay * shimmerAmt;

        // Hadamard-like mixing
        float hadamard[kN];
        for (int i = 0; i < kN; ++i)
        {
            float sign = ((i & 1) ? -1.0f : 1.0f) * ((i & 2) ? -0.7f : 0.7f);
            hadamard[i] = input + fb * sign;
        }

        for (int i = 0; i < kN; ++i)
        {
            lines[i].damp += (hadamard[i] - lines[i].damp) * (1.0f - dampCoeff);
            lines[i].buf[static_cast<size_t>(lines[i].pos)] = std::tanh (lines[i].damp);
            lines[i].pos = (lines[i].pos + 1) % static_cast<int>(lines[i].buf.size());
        }

        float w = smoothMix;
        outL = inL * (1.0f - w * 0.5f) + sumL * w;
        outR = inR * (1.0f - w * 0.5f) + sumR * w;
    }

private:
    static constexpr int kN = 8;
    struct Line { std::vector<float> buf; int pos = 0; float damp = 0.0f; };
    std::array<Line, kN> lines;

    std::vector<float> psBuf;
    int psWritePos = 0;
    double psPhase = 0.0;

    float sampleRate = 44100.0f;
    float smoothMix = 0.0f;
};

// ═══════════════════════════════════════════════════════════════════
// GalacticReverb — Infinite ambient reverb (Airwindows Galactic style)
//
// Very long FDN (12 lines, up to 1.5s each) with subtle pitch drift
// in the feedback. When size is high, the tail becomes near-infinite.
// Perfect for ambient, drone, cinematic pads.
// ═══════════════════════════════════════════════════════════════════
class GalacticReverb
{
public:
    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);
        int maxLen = static_cast<int>(sr * 1.5) + 1;

        for (int i = 0; i < kN; ++i)
        {
            lines[i].buf.assign (static_cast<size_t>(maxLen), 0.0f);
            lines[i].pos = 0;
            lines[i].dampLP = 0.0f;
            lines[i].lfoPhase = static_cast<float>(i) / static_cast<float>(kN);
        }

        // 4 input allpass diffusers
        int apMax = static_cast<int>(sr * 0.06) + 1;
        for (auto& ap : inputAP) { ap.buf.assign (static_cast<size_t>(apMax), 0.0f); ap.pos = 0; }
        setAPLen (inputAP[0], 0.0087f); setAPLen (inputAP[1], 0.0131f);
        setAPLen (inputAP[2], 0.0179f); setAPLen (inputAP[3], 0.0229f);

        smoothMix = 0.0f;
    }

    void reset()
    {
        for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.dampLP = 0.0f; }
        for (auto& ap : inputAP) std::fill (ap.buf.begin(), ap.buf.end(), 0.0f);
        smoothMix = 0.0f;
    }

    void processSample (float inL, float inR, float& outL, float& outR,
                        float size, float damp, float mix,
                        float /*preDelay*/ = 0.0f, float /*width*/ = 0.0f)
    {
        float targetMix = std::clamp (mix, 0.0f, 1.0f);
        smoothMix += (targetMix - smoothMix) * 0.001f;
        if (smoothMix < 0.001f) { outL = inL; outR = inR; return; }

        float sizeC = std::clamp (size, 0.05f, 4.0f);
        // Near-infinite decay when size is high
        float decay = std::clamp (0.6f + sizeC * 0.18f, 0.3f, 0.998f);
        float dampCoeff = 0.05f + damp * 0.55f;
        float drift = 0.0002f * sampleRate; // subtle pitch drift

        // Input diffusion
        float sig = (inL + inR) * 0.35f;
        sig = processAP (inputAP[0], sig, 0.6f);
        sig = processAP (inputAP[1], sig, 0.6f);
        sig = processAP (inputAP[2], sig, 0.6f);
        sig = processAP (inputAP[3], sig, 0.6f);

        // Read + decorrelation
        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < kN; ++i)
        {
            // Very long delay times scaled by size
            float baseT = baseTimes[i];
            float targetLen = baseT * sampleRate * (0.4f + sizeC * 0.8f);
            targetLen = std::clamp (targetLen, 4.0f, static_cast<float>(lines[i].buf.size()) - 4.0f);
            lines[i].smoothLen += (targetLen - lines[i].smoothLen) * 0.0003f;

            // Pitch drift modulation (each line has different rate)
            lines[i].lfoPhase += (0.03f + static_cast<float>(i) * 0.007f) / sampleRate;
            if (lines[i].lfoPhase >= 1.0f) lines[i].lfoPhase -= 1.0f;
            float lfoMod = std::sin (lines[i].lfoPhase * 6.2832f) * drift;

            float totalD = std::max (2.0f, lines[i].smoothLen + lfoMod);
            totalD = std::min (totalD, static_cast<float>(lines[i].buf.size()) - 2.0f);

            int d0 = static_cast<int>(totalD);
            float frac = totalD - static_cast<float>(d0);
            int rp0 = (lines[i].pos - d0 + static_cast<int>(lines[i].buf.size())) % static_cast<int>(lines[i].buf.size());
            int rp1 = (rp0 - 1 + static_cast<int>(lines[i].buf.size())) % static_cast<int>(lines[i].buf.size());
            float s = lines[i].buf[static_cast<size_t>(rp0)] * (1.0f - frac)
                     + lines[i].buf[static_cast<size_t>(rp1)] * frac;

            if (i < kN / 2) sumL += s; else sumR += s;
        }
        sumL /= static_cast<float>(kN / 2);
        sumR /= static_cast<float>(kN / 2);

        // Householder-style feedback mixing
        float fbL = sumL * decay;
        float fbR = sumR * decay;
        float cross = 0.35f;
        float mixedL = fbL * (1.0f - cross) + fbR * cross;
        float mixedR = fbR * (1.0f - cross) + fbL * cross;

        // Write back: input + mixed feedback with damping
        for (int i = 0; i < kN; ++i)
        {
            float fb = (i < kN / 2) ? mixedL : mixedR;
            // Alternate signs for density
            float sign = ((i & 1) ? -1.0f : 1.0f);
            float wet = sig + fb * sign;

            // LP damping
            lines[i].dampLP += (wet - lines[i].dampLP) * (1.0f - dampCoeff);
            if (std::abs (lines[i].dampLP) < 1e-18f) lines[i].dampLP = 0.0f;
            if (!std::isfinite (lines[i].dampLP)) lines[i].dampLP = 0.0f;

            lines[i].buf[static_cast<size_t>(lines[i].pos)] = std::tanh (lines[i].dampLP);
            lines[i].pos = (lines[i].pos + 1) % static_cast<int>(lines[i].buf.size());
        }

        float w = smoothMix;
        outL = inL + sumL * w * 1.3f;
        outR = inR + sumR * w * 1.3f;

        if (!std::isfinite (outL)) outL = inL;
        if (!std::isfinite (outR)) outR = inR;
    }

private:
    static constexpr int kN = 12;
    struct Line {
        std::vector<float> buf; int pos = 0;
        float dampLP = 0.0f, lfoPhase = 0.0f, smoothLen = 0.0f;
    };
    std::array<Line, kN> lines;

    struct APBuf { std::vector<float> buf; int pos = 0; int len = 100; };
    std::array<APBuf, 4> inputAP;

    // Very long, prime-adjacent delay times (seconds) for maximum diffusion
    std::array<float, kN> baseTimes {{ 0.1297f, 0.1523f, 0.1733f, 0.1979f,
                                        0.2213f, 0.2477f, 0.2719f, 0.3001f,
                                        0.3259f, 0.3571f, 0.3863f, 0.4177f }};
    float sampleRate = 44100.0f;
    float smoothMix = 0.0f;

    void setAPLen (APBuf& ap, float sec) { ap.len = std::max (1, static_cast<int>(sec * sampleRate)); }

    float processAP (APBuf& ap, float in, float coeff)
    {
        int readPos = (ap.pos - ap.len + static_cast<int>(ap.buf.size())) % static_cast<int>(ap.buf.size());
        float delayed = ap.buf[static_cast<size_t>(readPos)];
        float v = in - delayed * coeff;
        ap.buf[static_cast<size_t>(ap.pos)] = v;
        ap.pos = (ap.pos + 1) % static_cast<int>(ap.buf.size());
        return delayed + v * coeff;
    }
};

// ═══════════════════════════════════════════════════════════════════
// RoomReverb — Small room with distinct early reflections
// Tapped delay line for ER + short 4-line FDN tail.
// ═══════════════════════════════════════════════════════════════════
class RoomReverb
{
public:
    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);
        int maxLen = static_cast<int>(sr * 0.15) + 1;
        erBufL.assign (static_cast<size_t>(maxLen), 0.0f);
        erBufR.assign (static_cast<size_t>(maxLen), 0.0f);
        erWritePos = 0;
        for (int i = 0; i < kN; ++i) {
            lines[i].buf.assign (static_cast<size_t>(maxLen), 0.0f);
            lines[i].pos = 0; lines[i].damp = 0.0f;
        }
        smoothMix = 0.0f;
    }
    void reset()
    {
        std::fill (erBufL.begin(), erBufL.end(), 0.0f);
        std::fill (erBufR.begin(), erBufR.end(), 0.0f);
        for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.damp = 0.0f; }
        smoothMix = 0.0f;
    }
    void processSample (float inL, float inR, float& outL, float& outR,
                        float size, float damp, float mix, float = 0, float = 0)
    {
        float targetMix = std::clamp (mix, 0.0f, 1.0f);
        smoothMix += (targetMix - smoothMix) * 0.001f;
        if (smoothMix < 0.001f) { outL = inL; outR = inR; return; }
        int bufSz = static_cast<int>(erBufL.size());
        float sizeC = std::clamp (size, 0.0f, 1.0f);
        float decay = 0.2f + sizeC * 0.45f;
        float dampC = 0.1f + damp * 0.6f;
        erBufL[static_cast<size_t>(erWritePos)] = inL;
        erBufR[static_cast<size_t>(erWritePos)] = inR;
        // 6 ER taps
        float erL = 0, erR = 0;
        static const float erT[] = {0.0073f,0.0113f,0.0197f,0.0277f,0.0371f,0.0449f};
        static const float erG[] = {0.85f,0.72f,0.60f,0.48f,0.38f,0.30f};
        for (int t = 0; t < 6; ++t) {
            int d = std::clamp (static_cast<int>(erT[t]*sampleRate*(0.5f+sizeC)), 1, bufSz-1);
            int rp = (erWritePos - d + bufSz) % bufSz;
            float g = erG[t] * (0.6f + sizeC * 0.4f);
            if (t&1) { erL += erBufR[static_cast<size_t>(rp)]*g; erR += erBufL[static_cast<size_t>(rp)]*g; }
            else     { erL += erBufL[static_cast<size_t>(rp)]*g; erR += erBufR[static_cast<size_t>(rp)]*g; }
        }
        erWritePos = (erWritePos + 1) % bufSz;
        // Short FDN tail
        static const float fdnT[] = {0.0397f,0.0467f,0.0541f,0.0631f};
        float sumL=0, sumR=0;
        for (int i = 0; i < kN; ++i) {
            int dLen = std::clamp (static_cast<int>(fdnT[i]*sampleRate*(0.5f+sizeC)), 4, static_cast<int>(lines[i].buf.size())-1);
            int rp = (lines[i].pos - dLen + static_cast<int>(lines[i].buf.size())) % static_cast<int>(lines[i].buf.size());
            float s = lines[i].buf[static_cast<size_t>(rp)];
            if (i<2) sumL += s; else sumR += s;
        }
        sumL *= 0.5f; sumR *= 0.5f;
        float fbM = (sumL+sumR)*0.5f*decay;
        float inp = (inL+inR)*0.3f;
        for (int i = 0; i < kN; ++i) {
            float sign = (i&1) ? -1.0f : 1.0f;
            float wet = inp + fbM * sign;
            lines[i].damp += (wet - lines[i].damp) * (1.0f - dampC);
            lines[i].buf[static_cast<size_t>(lines[i].pos)] = std::tanh(lines[i].damp);
            lines[i].pos = (lines[i].pos + 1) % static_cast<int>(lines[i].buf.size());
        }
        float w = smoothMix;
        outL = inL*(1.0f-w*0.5f) + (erL + sumL*0.4f)*w;
        outR = inR*(1.0f-w*0.5f) + (erR + sumR*0.4f)*w;
    }
private:
    static constexpr int kN = 4;
    struct Line { std::vector<float> buf; int pos=0; float damp=0; };
    std::array<Line,kN> lines;
    std::vector<float> erBufL, erBufR;
    int erWritePos = 0;
    float sampleRate = 44100.0f, smoothMix = 0.0f;
};

// ═══════════════════════════════════════════════════════════════════
// SpringReverb — 8-stage allpass cascade with feedback
// Chirpy, metallic, classic dub/surf character.
// ═══════════════════════════════════════════════════════════════════
class SpringReverb
{
public:
    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);
        int maxLen = static_cast<int>(sr * 0.12) + 1;
        for (int i = 0; i < kN; ++i) {
            aps[i].buf.assign (static_cast<size_t>(maxLen), 0.0f); aps[i].pos = 0;
        }
        float baseLens[] = {0.0347f,0.0281f,0.0223f,0.0179f,0.0143f,0.0109f,0.0083f,0.0061f};
        for (int i = 0; i < kN; ++i)
            aps[i].len = std::max(4, static_cast<int>(baseLens[i]*sampleRate));
        fbDelL.assign(static_cast<size_t>(maxLen),0); fbDelR.assign(static_cast<size_t>(maxLen),0);
        fbPos=0; dampSt=0; smoothMix=0;
    }
    void reset()
    {
        for (auto& ap : aps) std::fill(ap.buf.begin(),ap.buf.end(),0.0f);
        std::fill(fbDelL.begin(),fbDelL.end(),0.0f); std::fill(fbDelR.begin(),fbDelR.end(),0.0f);
        dampSt=0; smoothMix=0;
    }
    void processSample (float inL, float inR, float& outL, float& outR,
                        float size, float damp, float mix, float=0, float=0)
    {
        float tM = std::clamp(mix,0.0f,1.0f);
        smoothMix += (tM - smoothMix)*0.001f;
        if (smoothMix < 0.001f) { outL=inL; outR=inR; return; }
        float sC = std::clamp(size,0.0f,1.0f);
        float decay = 0.35f + sC*0.52f;
        float dC = 0.3f + damp*0.5f;
        int fbLen = std::clamp(static_cast<int>(0.037f*sampleRate*(0.5f+sC)),4,static_cast<int>(fbDelL.size())-1);
        int rp = (fbPos - fbLen + static_cast<int>(fbDelL.size())) % static_cast<int>(fbDelL.size());
        float fbL = fbDelL[static_cast<size_t>(rp)]*decay;
        float fbR = fbDelR[static_cast<size_t>(rp)]*decay;
        float sig = (inL+inR)*0.45f + (fbL+fbR)*0.5f;
        dampSt += (sig - dampSt)*(1.0f - dC);
        sig = dampSt;
        for (int i = 0; i < kN; ++i) {
            float coeff = 0.55f + sC*0.15f;
            int rdP = (aps[i].pos - aps[i].len + static_cast<int>(aps[i].buf.size())) % static_cast<int>(aps[i].buf.size());
            float delayed = aps[i].buf[static_cast<size_t>(rdP)];
            float v = sig - delayed*coeff;
            aps[i].buf[static_cast<size_t>(aps[i].pos)] = v;
            aps[i].pos = (aps[i].pos+1) % static_cast<int>(aps[i].buf.size());
            sig = delayed + v*coeff;
        }
        float wetL = sig*0.7f + fbL*0.3f;
        float wetR = sig*0.7f - fbR*0.3f;
        fbDelL[static_cast<size_t>(fbPos)] = std::tanh(sig*0.95f);
        fbDelR[static_cast<size_t>(fbPos)] = std::tanh(sig*-0.95f);
        fbPos = (fbPos+1) % static_cast<int>(fbDelL.size());
        float w = smoothMix;
        outL = inL*(1.0f-w*0.5f) + wetL*w;
        outR = inR*(1.0f-w*0.5f) + wetR*w;
    }
private:
    static constexpr int kN = 8;
    struct APBuf { std::vector<float> buf; int pos=0; int len=100; };
    std::array<APBuf,kN> aps;
    std::vector<float> fbDelL, fbDelR;
    int fbPos=0; float dampSt=0, sampleRate=44100.0f, smoothMix=0;
};

// ═══════════════════════════════════════════════════════════════════
// NonlinReverb — Gated/nonlinear reverb (80s Phil Collins style)
// FDN with amplitude-gated decay. Size = gate time.
// ═══════════════════════════════════════════════════════════════════
class NonlinReverb
{
public:
    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);
        int maxLen = static_cast<int>(sr * 0.5) + 1;
        for (int i = 0; i < kN; ++i) {
            lines[i].buf.assign(static_cast<size_t>(maxLen),0); lines[i].pos=0; lines[i].damp=0;
        }
        gateEnv=0; inputEnv=0; smoothMix=0;
    }
    void reset()
    {
        for (auto& l : lines) { std::fill(l.buf.begin(),l.buf.end(),0.0f); l.damp=0; }
        gateEnv=0; inputEnv=0; smoothMix=0;
    }
    void processSample (float inL, float inR, float& outL, float& outR,
                        float size, float damp, float mix, float=0, float=0)
    {
        float tM = std::clamp(mix,0.0f,1.0f);
        smoothMix += (tM-smoothMix)*0.001f;
        if (smoothMix < 0.001f) { outL=inL; outR=inR; return; }
        float sC = std::clamp(size,0.0f,1.0f);
        float gateTime = 0.1f + sC*0.5f;
        float gateDecay = std::exp(-1.0f/(sampleRate*gateTime));
        float dampC = 0.1f + damp*0.4f;
        float inputLevel = std::abs(inL)+std::abs(inR);
        if (inputLevel > inputEnv) inputEnv = inputLevel;
        else inputEnv *= 0.9995f;
        if (inputEnv > 0.01f) gateEnv = 1.0f;
        gateEnv *= gateDecay;
        static const float fdnT[] = {0.0293f,0.0371f,0.0433f,0.0521f,0.0617f,0.0709f};
        float sumL=0, sumR=0;
        for (int i = 0; i < kN; ++i) {
            int dLen = std::clamp(static_cast<int>(fdnT[i]*sampleRate),4,static_cast<int>(lines[i].buf.size())-1);
            int rp = (lines[i].pos-dLen+static_cast<int>(lines[i].buf.size())) % static_cast<int>(lines[i].buf.size());
            float s = lines[i].buf[static_cast<size_t>(rp)];
            if (i<3) sumL += s; else sumR += s;
        }
        sumL /= 3.0f; sumR /= 3.0f;
        float inp = (inL+inR)*0.4f;
        float decay = 0.85f * gateEnv;
        float fbM = (sumL+sumR)*0.5f*decay;
        for (int i = 0; i < kN; ++i) {
            float sign = (i&1) ? -1.0f : 1.0f;
            float wet = inp + fbM*sign;
            lines[i].damp += (wet-lines[i].damp)*(1.0f-dampC);
            lines[i].buf[static_cast<size_t>(lines[i].pos)] = std::tanh(lines[i].damp*1.2f);
            lines[i].pos = (lines[i].pos+1) % static_cast<int>(lines[i].buf.size());
        }
        float w = smoothMix;
        outL = inL*(1.0f-w*0.5f) + sumL*w*gateEnv;
        outR = inR*(1.0f-w*0.5f) + sumR*w*gateEnv;
    }
private:
    static constexpr int kN = 6;
    struct Line { std::vector<float> buf; int pos=0; float damp=0; };
    std::array<Line,kN> lines;
    float gateEnv=0, inputEnv=0, sampleRate=44100.0f, smoothMix=0;
};
