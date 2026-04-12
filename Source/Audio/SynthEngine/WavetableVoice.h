#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../FX/MultiModelFilter.h"

// ═══════════════════════════════════════════════════════════════════
// WavetableVoice — Dual Wavetable Oscillator (Serum-style)
//
// • 2 independent WT oscillators with position morphing + warp modes
// • 2048-sample frames, up to 256 frames per table
// • 10-level mip-mapped anti-aliasing
// • Cubic Hermite interpolation (inter-sample AND inter-frame)
// • Up to 16-voice unison with detune + stereo spread
// • Sub oscillator (-1/-2 oct sine)
// • SVF filter with tanh soft-clip + ADSR
// • Amp ADSR + gate timing
// • Import .wav, 6 factory tables
// ═══════════════════════════════════════════════════════════════════

// ─── Wavetable Data Container ───────────────────────────────────
struct WavetableData
{
    static constexpr int kFrameSize = 2048;
    static constexpr int kMaxFrames = 256;
    static constexpr int kNumMipLevels = 10;

    std::vector<float> frames;
    int numFrames = 0;
    juce::String name = "Init";

    std::array<std::vector<float>, kNumMipLevels> mipLevels;
    bool mipValid = false;

    void clear()
    {
        frames.clear();
        numFrames = 0;
        mipValid = false;
        for (auto& ml : mipLevels) ml.clear();
    }

    bool isEmpty() const { return numFrames == 0 || frames.empty(); }

    const float* getFrame (int frameIdx) const
    {
        if (frameIdx < 0 || frameIdx >= numFrames || isEmpty()) return nullptr;
        return frames.data() + frameIdx * kFrameSize;
    }

    const float* getMipFrame (int frameIdx, int mipLevel) const
    {
        int ml = std::clamp (mipLevel, 0, kNumMipLevels - 1);
        if (!mipValid || mipLevels[static_cast<size_t>(ml)].empty())
            return getFrame (frameIdx);
        int idx = frameIdx * kFrameSize;
        if (idx + kFrameSize > static_cast<int>(mipLevels[static_cast<size_t>(ml)].size()))
            return getFrame (frameIdx);
        return mipLevels[static_cast<size_t>(ml)].data() + idx;
    }

    // Build mip-maps: each level progressively LP-filtered from previous
    void buildMipMaps()
    {
        if (isEmpty() || static_cast<int>(frames.size()) < numFrames * kFrameSize)
            { mipValid = false; return; }

        for (int level = 0; level < kNumMipLevels; ++level)
        {
            auto& ml = mipLevels[static_cast<size_t>(level)];
            ml.resize (static_cast<size_t>(numFrames * kFrameSize));

            if (level == 0)
            {
                std::copy (frames.begin(), frames.begin() + numFrames * kFrameSize, ml.begin());
            }
            else
            {
                // Windowed moving average with Hann window for cleaner rolloff
                const auto& prev = mipLevels[static_cast<size_t>(level - 1)];
                int halfW = 1 << (level - 1); // 1,2,4,8,...
                int width = halfW * 2 + 1;

                for (int f = 0; f < numFrames; ++f)
                {
                    int base = f * kFrameSize;
                    for (int s = 0; s < kFrameSize; ++s)
                    {
                        float sum = 0.0f, wSum = 0.0f;
                        for (int k = -halfW; k <= halfW; ++k)
                        {
                            int idx = (s + k + kFrameSize) % kFrameSize;
                            // Hann window weight
                            float w = 0.5f + 0.5f * std::cos (3.14159265f * static_cast<float>(k) / static_cast<float>(halfW + 1));
                            sum += prev[static_cast<size_t>(base + idx)] * w;
                            wSum += w;
                        }
                        ml[static_cast<size_t>(base + s)] = (wSum > 0.0f) ? sum / wSum : 0.0f;
                    }
                }
            }
        }
        mipValid = true;
    }

    // ─── Import from audio buffer ───
    void importFromBuffer (const juce::AudioBuffer<float>& buffer, int framesPerTable = 0)
    {
        int totalSamples = buffer.getNumSamples();
        if (totalSamples < 1 || buffer.getNumChannels() < 1) return;
        const float* src = buffer.getReadPointer (0);

        if (framesPerTable <= 0)
        {
            if (totalSamples % kFrameSize == 0)
                framesPerTable = totalSamples / kFrameSize;
            else
                framesPerTable = std::max (1, totalSamples / kFrameSize);
        }

        framesPerTable = std::clamp (framesPerTable, 1, kMaxFrames);
        numFrames = framesPerTable;
        frames.assign (static_cast<size_t>(numFrames * kFrameSize), 0.0f);

        for (int f = 0; f < numFrames; ++f)
        {
            double srcStart = static_cast<double>(f) / numFrames * totalSamples;
            double srcEnd = static_cast<double>(f + 1) / numFrames * totalSamples;
            double srcLen = srcEnd - srcStart;
            if (srcLen < 1.0) srcLen = 1.0;
            int dstBase = f * kFrameSize;

            for (int s = 0; s < kFrameSize; ++s)
            {
                double srcPos = srcStart + static_cast<double>(s) / kFrameSize * srcLen;
                int idx = static_cast<int>(srcPos);
                float frac = static_cast<float>(srcPos - idx);
                idx = std::clamp (idx, 0, totalSamples - 1);
                int idx1 = std::min (idx + 1, totalSamples - 1);
                frames[static_cast<size_t>(dstBase + s)] = src[idx] * (1.0f - frac) + src[idx1] * frac;
            }
        }

        // Normalize each frame
        for (int f = 0; f < numFrames; ++f)
        {
            float peak = 0.0f;
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
                peak = std::max (peak, std::abs (frames[static_cast<size_t>(base + s)]));
            if (peak > 0.001f)
            {
                float gain = 1.0f / peak;
                for (int s = 0; s < kFrameSize; ++s)
                    frames[static_cast<size_t>(base + s)] *= gain;
            }
        }
        buildMipMaps();
    }

    // ─── Factory tables ───
    static WavetableData createBasic()
    {
        WavetableData wt; wt.name = "Basic"; wt.numFrames = 8;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float morph = static_cast<float>(f) / (wt.numFrames - 1);
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float ph = static_cast<float>(s) / kFrameSize;
                float sine = std::sin (ph * 6.2831853f);
                float saw = 2.0f * ph - 1.0f;
                float sq = ph < 0.5f ? 1.0f : -1.0f;
                wt.frames[static_cast<size_t>(base + s)] =
                    morph < 0.5f ? sine * (1.0f - morph * 2.0f) + saw * morph * 2.0f
                                 : saw * (1.0f - (morph - 0.5f) * 2.0f) + sq * (morph - 0.5f) * 2.0f;
            }
        }
        wt.buildMipMaps(); return wt;
    }

    static WavetableData createPWM()
    {
        WavetableData wt; wt.name = "PWM"; wt.numFrames = 32;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float pw = 0.05f + 0.9f * static_cast<float>(f) / (wt.numFrames - 1);
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float ph = static_cast<float>(s) / kFrameSize;
                wt.frames[static_cast<size_t>(base + s)] = ph < pw ? 1.0f : -1.0f;
            }
        }
        wt.buildMipMaps(); return wt;
    }

    static WavetableData createFormant()
    {
        WavetableData wt; wt.name = "Formant"; wt.numFrames = 16;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        static const float vf[5][3] = {{730,1090,2440},{660,1720,2410},{270,2290,3010},{570,840,2410},{300,870,2240}};
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float vPos = static_cast<float>(f) / (wt.numFrames - 1) * 4.0f;
            int v0 = std::min (4, static_cast<int>(vPos));
            int v1 = std::min (4, v0 + 1);
            float vFr = vPos - v0;
            float f1 = vf[v0][0]*(1-vFr)+vf[v1][0]*vFr, f2 = vf[v0][1]*(1-vFr)+vf[v1][1]*vFr, f3 = vf[v0][2]*(1-vFr)+vf[v1][2]*vFr;
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float t = static_cast<float>(s) / kFrameSize * 6.2831853f;
                float sig = std::sin(t) + 0.5f*std::sin(t*f1/100) + 0.3f*std::sin(t*f2/100) + 0.2f*std::sin(t*f3/100);
                wt.frames[static_cast<size_t>(base + s)] = std::tanh (sig * 0.5f);
            }
        }
        wt.buildMipMaps(); return wt;
    }

    static WavetableData createMetallic()
    {
        WavetableData wt; wt.name = "Metallic"; wt.numFrames = 16;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        float ratios[] = {1,2.76f,5.4f,8.93f,13.34f,1.41f,3.89f};
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float m = static_cast<float>(f)/(wt.numFrames-1);
            float amps[] = {1,0.6f,0.4f,0.25f,0.15f,0.5f*m,0.3f*m};
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float ph = static_cast<float>(s)/kFrameSize*6.2831853f;
                float sig = 0;
                for (int h = 0; h < 7; ++h) sig += amps[h]*std::sin(ph*ratios[h]);
                wt.frames[static_cast<size_t>(base+s)] = std::tanh(sig*0.3f);
            }
        }
        wt.buildMipMaps(); return wt;
    }

    static WavetableData createDigital()
    {
        WavetableData wt; wt.name = "Digital"; wt.numFrames = 32;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float m = static_cast<float>(f)/(wt.numFrames-1);
            int harms = 2 + static_cast<int>(m*30);
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float ph = static_cast<float>(s)/kFrameSize;
                float sig = 0;
                for (int h = 1; h <= harms; ++h)
                    sig += (1.0f/(h*(1+m*2)))*std::sin((ph*h + m*0.3f*std::sin(ph*h*3))*6.2831853f);
                wt.frames[static_cast<size_t>(base+s)] = std::tanh(sig);
            }
        }
        wt.buildMipMaps(); return wt;
    }

    static WavetableData createSuperSaw()
    {
        WavetableData wt; wt.name = "SuperSaw"; wt.numFrames = 16;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float m = static_cast<float>(f)/(wt.numFrames-1);
            int ns = 1 + static_cast<int>(m*6);
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float ph = static_cast<float>(s)/kFrameSize;
                float sig = 0;
                for (int sw = 0; sw < ns; ++sw)
                {
                    float dt = (sw-ns/2)*m*0.02f;
                    float p = std::fmod(ph*(1+dt)+sw*0.137f, 1.0f);
                    sig += (2*p-1)/ns;
                }
                wt.frames[static_cast<size_t>(base+s)] = sig;
            }
        }
        wt.buildMipMaps(); return wt;
    }

    // ── Vocal Choir: morphing vowel formants with rich harmonics ──
    static WavetableData createVocalChoir()
    {
        WavetableData wt; wt.name = "Vocal Choir"; wt.numFrames = 32;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        // Formant frequencies for A-E-I-O-U with bandwidth
        static const float formants[5][3] = {{800,1200,2500},{350,2000,2800},{270,2300,3000},{450,800,2500},{325,700,2500}};
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float vPos = static_cast<float>(f) / (wt.numFrames - 1) * 4.0f;
            int v0 = std::min (4, static_cast<int>(vPos));
            int v1 = std::min (4, v0 + 1);
            float vFr = vPos - v0;
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float t = static_cast<float>(s) / kFrameSize * 6.2831853f;
                float sig = 0;
                // Glottal pulse (saw-like source)
                for (int h = 1; h <= 20; ++h)
                {
                    float freq = static_cast<float>(h);
                    // Apply formant envelope: boost near formant frequencies
                    float boost = 0.0f;
                    for (int fi = 0; fi < 3; ++fi)
                    {
                        float fc = formants[v0][fi]*(1-vFr) + formants[v1][fi]*vFr;
                        float dist = std::abs (freq * 100.0f - fc) / 200.0f;
                        boost += std::exp (-dist * dist);
                    }
                    sig += std::sin (t * freq) * (0.1f + boost * 0.5f) / freq;
                }
                wt.frames[static_cast<size_t>(base + s)] = std::tanh (sig);
            }
        }
        wt.buildMipMaps(); return wt;
    }

    // ── Pluck: decaying harmonics morphing from bright to dark ──
    static WavetableData createPluck()
    {
        WavetableData wt; wt.name = "Pluck"; wt.numFrames = 16;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float brightness = 1.0f - static_cast<float>(f) / (wt.numFrames - 1);
            int maxH = 2 + static_cast<int>(brightness * 30);
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float t = static_cast<float>(s) / kFrameSize * 6.2831853f;
                float sig = 0;
                for (int h = 1; h <= maxH; ++h)
                {
                    float decay = std::exp (-static_cast<float>(h - 1) * (1.0f - brightness) * 0.3f);
                    sig += std::sin (t * h) * decay / h;
                }
                wt.frames[static_cast<size_t>(base + s)] = std::tanh (sig * 0.6f);
            }
        }
        wt.buildMipMaps(); return wt;
    }

    // ── Spectral: pure additive with morphing harmonic content ──
    static WavetableData createSpectral()
    {
        WavetableData wt; wt.name = "Spectral"; wt.numFrames = 32;
        wt.frames.resize (static_cast<size_t>(wt.numFrames * kFrameSize), 0.0f);
        for (int f = 0; f < wt.numFrames; ++f)
        {
            float m = static_cast<float>(f) / (wt.numFrames - 1);
            int base = f * kFrameSize;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float t = static_cast<float>(s) / kFrameSize * 6.2831853f;
                float sig = 0;
                // Odd harmonics morph to even harmonics
                for (int h = 1; h <= 16; ++h)
                {
                    bool isOdd = (h % 2 == 1);
                    float amp = isOdd ? (1.0f - m * 0.8f) : (m * 0.8f);
                    sig += std::sin (t * h) * amp / (h * 0.7f);
                }
                wt.frames[static_cast<size_t>(base + s)] = std::tanh (sig * 0.4f);
            }
        }
        wt.buildMipMaps(); return wt;
    }

    // ═══ UTILITY: Spectral Morph between two frames ═══
    // Creates smooth interpolation in frequency domain (like Serum MORPH)
    static void spectralMorphFrames (WavetableData& wt, int frameA, int frameB, int outputFrame)
    {
        if (wt.isEmpty() || frameA >= wt.numFrames || frameB >= wt.numFrames || outputFrame >= wt.numFrames) return;
        const float* srcA = wt.getFrame (frameA);
        const float* srcB = wt.getFrame (frameB);
        if (!srcA || !srcB) return;

        int base = outputFrame * kFrameSize;

        // Simple DFT → interpolate magnitudes/phases → inverse DFT
        int numBins = kFrameSize / 2;
        std::vector<float> magA(static_cast<size_t>(numBins)), phA(static_cast<size_t>(numBins));
        std::vector<float> magB(static_cast<size_t>(numBins)), phB(static_cast<size_t>(numBins));

        // Forward DFT for both frames
        for (int h = 0; h < numBins; ++h)
        {
            float reA = 0, imA = 0, reB = 0, imB = 0;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float angle = 6.2831853f * (h + 1) * s / kFrameSize;
                float c = std::cos (angle), sn = std::sin (angle);
                reA += srcA[s] * c; imA += srcA[s] * sn;
                reB += srcB[s] * c; imB += srcB[s] * sn;
            }
            magA[static_cast<size_t>(h)] = std::sqrt (reA*reA + imA*imA);
            phA[static_cast<size_t>(h)] = std::atan2 (imA, reA);
            magB[static_cast<size_t>(h)] = std::sqrt (reB*reB + imB*imB);
            phB[static_cast<size_t>(h)] = std::atan2 (imB, reB);
        }

        // 50/50 morph
        std::fill (wt.frames.begin() + base, wt.frames.begin() + base + kFrameSize, 0.0f);
        for (int h = 0; h < numBins; ++h)
        {
            float mag = (magA[static_cast<size_t>(h)] + magB[static_cast<size_t>(h)]) * 0.5f;
            float ph = (phA[static_cast<size_t>(h)] + phB[static_cast<size_t>(h)]) * 0.5f;
            for (int s = 0; s < kFrameSize; ++s)
            {
                float angle = 6.2831853f * (h + 1) * s / kFrameSize;
                wt.frames[static_cast<size_t>(base + s)] += mag * std::cos (angle - ph) / (kFrameSize / 2);
            }
        }
    }
};

// ─── Warp Modes (Serum-style phase distortion) ──────────────────
enum class WarpMode { Off = 0, BendPlus, BendMinus, BendPM, Asym, FM, Quantize,
                      Mirror, Squeeze, Wrap, Sync, Saturate, COUNT };

inline float applyWarp (float phase, float amount, WarpMode mode)
{
    if (mode == WarpMode::Off || std::abs (amount) < 0.001f) return phase;
    float a = std::clamp (amount, 0.0f, 1.0f);
    switch (mode)
    {
        case WarpMode::BendPlus:   return std::pow (phase, 1.0f + a * 3.0f);
        case WarpMode::BendMinus:  return 1.0f - std::pow (1.0f - phase, 1.0f + a * 3.0f);
        case WarpMode::BendPM:     return 0.5f + (phase - 0.5f) * (1.0f - a * 0.9f);
        case WarpMode::Asym:
        {
            float x = phase * 2.0f - 1.0f;
            float curved = x + a * x * std::abs (x);
            return std::clamp ((curved + 1.0f) * 0.5f, 0.0f, 1.0f);
        }
        case WarpMode::FM:
        {
            float r = std::fmod (phase + a * 0.5f * std::sin (phase * 6.2831853f * 2.0f), 1.0f);
            return r < 0.0f ? r + 1.0f : r;
        }
        case WarpMode::Quantize:
        {
            int steps = 2 + static_cast<int>(a * 30);
            return std::round (phase * steps) / steps;
        }
        case WarpMode::Mirror:
        {
            // Fold waveform at center — creates even harmonics
            float stretched = phase * (1.0f + a * 2.0f);
            float folded = stretched;
            while (folded > 1.0f) folded = 2.0f - folded;
            while (folded < 0.0f) folded = -folded;
            return folded;
        }
        case WarpMode::Squeeze:
        {
            // Asymmetric compression — pushes energy to one side
            float x = phase;
            float bias = a * 0.4f;
            x = x * (1.0f + bias) / (x + bias + 0.001f);
            return std::clamp (x, 0.0f, 1.0f);
        }
        case WarpMode::Wrap:
        {
            // Phase wrapping — adds overtones progressively
            float stretched = phase * (1.0f + a * 4.0f);
            return stretched - std::floor (stretched);
        }
        case WarpMode::Sync:
        {
            // Hard sync simulation — multiplies phase, resets at boundaries
            float ratio = 1.0f + a * 3.0f; // 1x to 4x
            float syncPhase = std::fmod (phase * ratio, 1.0f);
            return syncPhase;
        }
        case WarpMode::Saturate:
        {
            // Soft saturation — warm overdrive, adds odd harmonics
            float x = (phase * 2.0f - 1.0f); // -1 to +1
            float drive = 1.0f + a * 8.0f;
            float sat = std::tanh (x * drive) / std::tanh (drive);
            return (sat + 1.0f) * 0.5f;
        }
        default: return phase;
    }
}

// ─── Wavetable Oscillator ──────────────────────────────────────
class WavetableOsc
{
public:
    void reset() { phase = 0.0; }
    void setPhase (double p) { phase = std::fmod (p, 1.0); if (phase < 0) phase += 1.0; }

    float getSample (const WavetableData& wt, double freq, float position,
                     int mipLevel, double sampleRate,
                     WarpMode warpMode = WarpMode::Off, float warpAmount = 0.0f)
    {
        if (wt.isEmpty()) return 0.0f;

        position = std::clamp (position, 0.0f, 1.0f);

        // Apply warp to reading phase
        float readPhase = static_cast<float>(phase);
        readPhase = applyWarp (readPhase, warpAmount, warpMode);

        // Frame interpolation (cubic between 4 frames)
        float framePos = position * static_cast<float>(std::max (1, wt.numFrames) - 1);
        int baseFrame = static_cast<int>(std::floor (framePos));
        float frameFrac = framePos - static_cast<float>(baseFrame);

        int f0 = std::clamp (baseFrame - 1, 0, wt.numFrames - 1);
        int f1 = std::clamp (baseFrame,     0, wt.numFrames - 1);
        int f2 = std::clamp (baseFrame + 1, 0, wt.numFrames - 1);
        int f3 = std::clamp (baseFrame + 2, 0, wt.numFrames - 1);

        float s0 = readFrame (wt, f0, mipLevel, readPhase);
        float s1 = readFrame (wt, f1, mipLevel, readPhase);
        float s2 = readFrame (wt, f2, mipLevel, readPhase);
        float s3 = readFrame (wt, f3, mipLevel, readPhase);

        // Cubic Hermite between frames
        float c1 = 0.5f * (s2 - s0);
        float c2 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
        float c3 = 0.5f * (s3 - s0) + 1.5f * (s1 - s2);
        float result = ((c3 * frameFrac + c2) * frameFrac + c1) * frameFrac + s1;

        // Advance phase
        phase += freq / sampleRate;
        phase -= std::floor (phase);

        return result;
    }

    double getPhase() const { return phase; }

private:
    double phase = 0.0;

    float readFrame (const WavetableData& wt, int frameIdx, int mipLevel, float readPhase) const
    {
        const float* data = wt.getMipFrame (frameIdx, mipLevel);
        if (data == nullptr) return 0.0f;

        // Ensure phase is in [0,1) — safety net for any warp edge cases
        if (readPhase < 0.0f) readPhase += 1.0f;
        if (readPhase >= 1.0f) readPhase -= 1.0f;

        double samplePos = static_cast<double>(readPhase) * WavetableData::kFrameSize;
        int idx = static_cast<int>(samplePos);
        float frac = static_cast<float>(samplePos - idx);

        int N = WavetableData::kFrameSize;
        // Safe wrapping modulo (handles negative idx from float precision)
        int i0 = ((idx - 1) % N + N) % N;
        int i1 = (idx % N + N) % N;
        int i2 = ((idx + 1) % N + N) % N;
        int i3 = ((idx + 2) % N + N) % N;

        float y0 = data[i0], y1 = data[i1], y2 = data[i2], y3 = data[i3];
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + y1;
    }
};

// ═════════════════════════════════════════════════════════════════
// WavetableVoice — Dual WT + Sub + Filter + Envelopes
// ═════════════════════════════════════════════════════════════════
class WavetableVoice
{
public:
    static constexpr int kMaxUnison = 16;

    void prepare (double sr) { sampleRate = sr; }

    void noteOn (int noteIdx, int octave, float vel,
                 const WavetableData* wt1, const WavetableData* wt2,
                 float wtPos1, float wtPos2, float oscMix,
                 float detuneSemi, int unisonCount, float uniSpread, float uniStereo,
                 float filterCut, float filterRes, int filterType,
                 int filterModel, int filterPoles,
                 float fEnv, float fA, float fD, float fS, float fR,
                 float aA, float aD, float aS, float aR,
                 float gateSeconds,
                 WarpMode warp1 = WarpMode::Off, float warpAmt1 = 0.0f,
                 WarpMode warp2 = WarpMode::Off, float warpAmt2 = 0.0f,
                 float subLevel = 0.0f, int subOct = -1)
    {
        wavetable1 = wt1;
        wavetable2 = wt2;
        pos1 = wtPos1; pos2 = wtPos2;
        targetPos1 = wtPos1; targetPos2 = wtPos2;
        mix = oscMix; targetMix = oscMix;
        warpMode1 = warp1; warpAmount1 = warpAmt1;
        warpMode2 = warp2; warpAmount2 = warpAmt2;
        subLvl = subLevel; subOctave = subOct;

        velocity = vel;
        bool wasPlaying = playing;
        playing = true; gate = true; hasPlocks = false;
        gateDuration = gateSeconds; gateTimer = 0.0f;

        int midi = noteIdx + (octave + 2) * 12;
        baseFreq = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        osc2Detune = detuneSemi;

        fCut = filterCut; fRes = filterRes; fType = filterType;
        fModel = filterModel; fPoles = filterPoles;
        filtEnvDepth = fEnv;
        filtA = fA; filtD = fD; filtS = fS; filtR = fR;
        ampA = aA; ampD = aD; ampS = aS; ampR = aR;

        if (!wasPlaying) { ampEnvVal = 0.0f; }
        ampEnvStage = 0;
        filtEnvVal = 0.0f; filtEnvStage = 0;
        subPhase = 0.0;

        // Random phase per note trigger (Serum-style)
        auto& rng = juce::Random::getSystemRandom();

        numUnison = std::clamp (unisonCount, 1, kMaxUnison);
        for (int u = 0; u < numUnison; ++u)
        {
            uniVoices1[u].osc.reset();
            uniVoices2[u].osc.reset();

            if (numUnison == 1)
            {
                uniVoices1[u].detuneAmount = 0.0f;
                uniVoices1[u].panPosition = 0.0f;
            }
            else
            {
                float spread = static_cast<float>(u) / (numUnison - 1) * 2.0f - 1.0f;
                uniVoices1[u].detuneAmount = spread * uniSpread;
                uniVoices1[u].panPosition = spread * uniStereo;
            }
            uniVoices2[u] = uniVoices1[u];

            double rp = rng.nextDouble();
            uniVoices1[u].osc.setPhase (rp);
            uniVoices2[u].osc.setPhase (std::fmod (rp + 0.5, 1.0));
        }

        filterL.reset(); filterR.reset();
        killFade = 0;
    }

    void noteOff()
    {
        gate = false;
        if (ampEnvStage < 3) ampEnvStage = 3;
        if (filtEnvStage < 3) filtEnvStage = 3;
    }

    void kill() { if (playing) killFade = 256; }
    bool isPlaying() const { return playing; }
    bool isGateActive() const { return playing && ampEnvStage < 3; }
    void releaseGate() { if (playing && ampEnvStage < 3) ampEnvStage = 3; }
    bool isKilling() const { return playing && killFade > 0; }

    void setPositions (float p1, float p2, float m) { targetPos1 = p1; targetPos2 = p2; targetMix = m; }
    void setWarp (WarpMode m1, float a1, WarpMode m2, float a2) { warpMode1=m1; warpAmount1=a1; warpMode2=m2; warpAmount2=a2; }

    // Live parameter update: allows LFO/MSEG/ARP modulation to reach playing voices
    void updateLiveParams (float cut, float res, int type, int model, int poles, float fenv,
                           float fa, float fd, float fs, float fr,
                           float aa, float ad, float as, float ar,
                           float vol, float sub)
    {
        fCut = cut; fRes = res; fType = type; fModel = model; fPoles = poles;
        filtEnvDepth = fenv;
        filtA = fa; filtD = fd; filtS = fs; filtR = fr;
        ampA = aa; ampD = ad; ampS = as; ampR = ar;
        subLvl = sub;
        // Volume: smooth toward new value to prevent clicks
        // (don't set velocity directly — it's per-note from sequencer)
    }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        if (!playing) return;
        float dt = 1.0f / static_cast<float>(sampleRate);
        float sr = static_cast<float>(sampleRate);

        // ── Smooth position transitions to prevent clicks ──
        // 5ms smoothing time constant — fast enough for modulation, no clicks
        float posSmooth = std::exp (-1.0f / (sr * 0.005f));

        // Pre-compute per-block constants
        int mipLevel = std::clamp (static_cast<int>(std::log2 (std::max (20.0, baseFreq) / 20.0)), 0, 9);
        float invUni = 1.0f / static_cast<float>(numUnison);
        double subFreq = baseFreq * std::pow (2.0, static_cast<double>(subOctave));

        double uniFreqs1[kMaxUnison], uniFreqs2[kMaxUnison];
        for (int u = 0; u < numUnison; ++u)
        {
            uniFreqs1[u] = baseFreq * std::pow (2.0, uniVoices1[u].detuneAmount / 12.0);
            uniFreqs2[u] = baseFreq * std::pow (2.0, (uniVoices2[u].detuneAmount + osc2Detune) / 12.0);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            // Smooth position/mix to prevent clicks on knob movement
            pos1 += (targetPos1 - pos1) * (1.0f - posSmooth);
            pos2 += (targetPos2 - pos2) * (1.0f - posSmooth);
            mix  += (targetMix  - mix)  * (1.0f - posSmooth);

            gateTimer += dt;
            if (gate && gateDuration > 0 && gateTimer >= gateDuration)
                noteOff();

            float ampEnv = runADSR (ampEnvVal, ampEnvStage, ampA, ampD, ampS, ampR, dt);
            float filtEnv = runADSR (filtEnvVal, filtEnvStage, filtA, filtD, filtS, filtR, dt);

            if (ampEnv < 0.0001f && ampEnvStage >= 4) { playing = false; return; }

            // Render unison oscillators
            float sigL = 0.0f, sigR = 0.0f;
            for (int u = 0; u < numUnison; ++u)
            {
                float pan = uniVoices1[u].panPosition;
                float s1 = 0.0f, s2 = 0.0f;

                if (wavetable1 != nullptr)
                    s1 = uniVoices1[u].osc.getSample (*wavetable1, uniFreqs1[u], pos1, mipLevel, sampleRate, warpMode1, warpAmount1);
                if (wavetable2 != nullptr)
                    s2 = uniVoices2[u].osc.getSample (*wavetable2, uniFreqs2[u], pos2, mipLevel, sampleRate, warpMode2, warpAmount2);

                float sig = s1 * (1.0f - mix) + s2 * mix;

                float gainL = std::cos ((pan + 1.0f) * 0.25f * 3.14159265f);
                float gainR = std::sin ((pan + 1.0f) * 0.25f * 3.14159265f);
                sigL += sig * gainL * invUni;
                sigR += sig * gainR * invUni;
            }

            // Sub oscillator (mono, centered)
            if (subLvl > 0.001f)
            {
                float sub = std::sin (static_cast<float>(subPhase * 6.283185307));
                subPhase += subFreq / sampleRate;
                if (subPhase >= 1.0) subPhase -= 1.0;
                sigL += sub * subLvl;
                sigR += sub * subLvl;
            }

            // Multi-model filter (CLN/ACD/DRT/SEM/ARP/LQD)
            float cutHz = 20.0f * std::pow (1000.0f, std::clamp (fCut + filtEnvDepth * filtEnv * 100.0f, 0.0f, 100.0f) / 100.0f);
            cutHz = std::clamp (cutHz, 16.0f, std::min (18000.0f, sr * 0.45f));
            sigL = filterL.process (sigL, cutHz, fRes, fModel, fType, fPoles, sr);
            sigR = filterR.process (sigR, cutHz, fRes, fModel, fType, fPoles, sr);

            float finalGain = ampEnv * velocity;

            if (killFade > 0)
            {
                finalGain *= static_cast<float>(killFade) / 256.0f;
                --killFade;
                if (killFade == 0) { playing = false; return; }
            }

            outL[i] += sigL * finalGain;
            outR[i] += sigR * finalGain;
        }
    }

    void setPlocked() { hasPlocks = true; }
    bool hasPlocks = false;

private:
    double sampleRate = 44100.0;
    const WavetableData* wavetable1 = nullptr;
    const WavetableData* wavetable2 = nullptr;

public:
    void setWavetables (const WavetableData* wt1, const WavetableData* wt2)
    {
        wavetable1 = wt1;
        wavetable2 = wt2;
    }
    float pos1 = 0.0f, pos2 = 0.0f, mix = 0.0f;
    float targetPos1 = 0.0f, targetPos2 = 0.0f, targetMix = 0.0f;
    WarpMode warpMode1 = WarpMode::Off, warpMode2 = WarpMode::Off;
    float warpAmount1 = 0.0f, warpAmount2 = 0.0f;
    double baseFreq = 440.0;
    float osc2Detune = 0.0f;
    float velocity = 0.0f;
    bool playing = false, gate = false;
    float gateDuration = 0.0f, gateTimer = 0.0f;
    int killFade = 0;

    // Sub oscillator
    float subLvl = 0.0f;
    int subOctave = -1;
    double subPhase = 0.0;

    // Unison
    struct UnisonVoice { WavetableOsc osc; float detuneAmount = 0; float panPosition = 0; };
    int numUnison = 1;
    std::array<UnisonVoice, kMaxUnison> uniVoices1, uniVoices2;

    // Filter (multi-model: CLN/ACD/DRT/SEM/ARP/LQD)
    MultiModelFilterCh filterL, filterR;
    float fCut = 100.0f, fRes = 0.0f;
    int fType = 0, fModel = 0, fPoles = 12;
    float filtEnvDepth = 0.0f;
    float filtA = 0.01f, filtD = 0.3f, filtS = 0.5f, filtR = 0.3f;
    float filtEnvVal = 0.0f; int filtEnvStage = 0;

    float ampA = 0.01f, ampD = 0.3f, ampS = 0.7f, ampR = 0.3f;
    float ampEnvVal = 0.0f; int ampEnvStage = 0;

    float runADSR (float& val, int& stage, float a, float d, float s, float r, float dtSec)
    {
        switch (stage)
        {
            case 0: val += dtSec / std::max (0.0005f, a);
                    if (val >= 1.0f) { val = 1.0f; stage = 1; } break;
            case 1: val += (s - val) * (1.0f - std::exp (-dtSec / std::max (0.005f, d)));
                    if (std::abs (val - s) < 0.001f) { val = s; stage = 2; } break;
            case 2: val = s; break;
            case 3: val *= std::exp (-dtSec / std::max (0.005f, r));
                    if (val < 0.0001f) { val = 0.0f; stage = 4; } break;
            default: val = 0.0f; break;
        }
        return val;
    }
};
