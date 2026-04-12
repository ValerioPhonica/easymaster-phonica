#pragma once
#include "SynthPart.h"
#include "../FX/FDNReverb.h"
#include "../FX/ReverbAlgos.h"
#include <array>
#include <vector>
#include <cmath>

// ═══════════════════════════════════════════════════════════════════
// SynthFX — Per-track FX chain for synth: Dist → Redux → Chorus →
//           Delay → Reverb → LP → HP → Pan → stereo out
// ═══════════════════════════════════════════════════════════════════
struct SynthFX
{
    double sampleRate = 44100.0;

    // Delay
    std::vector<float> delayBuf;
    std::vector<float> delayBufR; // for ping-pong
    int delayWritePos = 0;
    float delayFBState = 0.0f;
    float delayFBStateR = 0.0f;
    float dlyFilterL = 0.0f;   // feedback LP filter state L
    float dlyFilterR = 0.0f;   // feedback LP filter state R
    float smoothDelSmp = 4410.0f; // tape delay smooth target

    // SVF 12dB filter states
    float lpIC1L = 0.0f, lpIC2L = 0.0f, lpIC1R = 0.0f, lpIC2R = 0.0f;
    float hpIC1L = 0.0f, hpIC2L = 0.0f, hpIC1R = 0.0f, hpIC2R = 0.0f;

    // 3-band EQ biquad states (transposed direct form II)
    float eqLZ1L = 0, eqLZ2L = 0, eqLZ1R = 0, eqLZ2R = 0; // low shelf
    float eqMZ1L = 0, eqMZ2L = 0, eqMZ1R = 0, eqMZ2R = 0; // mid bell
    float eqHZ1L = 0, eqHZ2L = 0, eqHZ1R = 0, eqHZ2R = 0; // high shelf

    // Redux
    float reduxHold = 0.0f;
    int reduxCounter = 0;

    // FDN Reverb + algorithm variants
    FDNReverb reverb;
    PlateReverb plateReverb;
    ShimmerReverb shimmerReverb;
    GalacticReverb galacticReverb;
    RoomReverb roomReverb;
    SpringReverb springReverb;
    NonlinReverb nonlinReverb;

    // Delay algo state: tape wow/flutter, BBD noise/clock
    double tapeWowPhase = 0.0;
    float  tapeWowDepth = 0.0f;
    float  bbdClockJitter = 0.0f;
    float  bbdNoiseState = 0.0f;
    uint32_t bbdRng = 12345;

    // DIFFUSE delay algo state
    struct DiffAP { float z = 0.0f; };
    std::array<DiffAP, 4> diffAP;
    bool diffuseActive = false;

    // Stereo Chorus
    std::vector<float> chorusBufL, chorusBufR;
    int chorusWritePos = 0;
    double chorusLfoPhase = 0.0;
    static constexpr int kChorusMaxDelay = 2048;
    static constexpr float kChorusBaseDelay = 0.007f;
    static constexpr float kChorusMaxDepth = 0.004f;

    void prepare (double sr)
    {
        sampleRate = sr;
        delayBuf.assign (static_cast<size_t>(sr * 2.0) + 1, 0.0f);
        delayBufR.assign (static_cast<size_t>(sr * 2.0) + 1, 0.0f);
        delayWritePos = 0;
        delayFBState = delayFBStateR = 0.0f;
        dlyFilterL = dlyFilterR = 0.0f;
        lpIC1L = lpIC2L = lpIC1R = lpIC2R = hpIC1L = hpIC2L = hpIC1R = hpIC2R = 0.0f;
        eqLZ1L = eqLZ2L = eqLZ1R = eqLZ2R = eqMZ1L = eqMZ2L = eqMZ1R = eqMZ2R = eqHZ1L = eqHZ2L = eqHZ1R = eqHZ2R = 0.0f;
        reverb.prepare (sr);
        plateReverb.prepare (sr);
        shimmerReverb.prepare (sr);
        galacticReverb.prepare (sr);
        roomReverb.prepare (sr);
        springReverb.prepare (sr);
        nonlinReverb.prepare (sr);
        tapeWowPhase = 0.0; tapeWowDepth = 0.0f;
        bbdClockJitter = 0.0f; bbdNoiseState = 0.0f;

        chorusBufL.assign (kChorusMaxDelay, 0.0f);
        chorusBufR.assign (kChorusMaxDelay, 0.0f);
        chorusWritePos = 0;
        chorusLfoPhase = 0.0;
    }

    void silenceDelays()
    {
        std::fill (delayBuf.begin(), delayBuf.end(), 0.0f);
        std::fill (delayBufR.begin(), delayBufR.end(), 0.0f);
        delayFBState = delayFBStateR = 0.0f;
        dlyFilterL = dlyFilterR = 0.0f;
        std::fill (chorusBufL.begin(), chorusBufL.end(), 0.0f);
        std::fill (chorusBufR.begin(), chorusBufR.end(), 0.0f);
        reverb.reset();
        plateReverb.reset();
        shimmerReverb.reset();
        galacticReverb.reset();
        roomReverb.reset();
        springReverb.reset();
        nonlinReverb.reset();
        lpIC1L = lpIC2L = lpIC1R = lpIC2R = hpIC1L = hpIC2L = hpIC1R = hpIC2R = 0.0f;
        eqLZ1L = eqLZ2L = eqLZ1R = eqLZ2R = eqMZ1L = eqMZ2L = eqMZ1R = eqMZ2R = eqHZ1L = eqHZ2L = eqHZ1R = eqHZ2R = 0.0f;
    }

    void processBlock (float* monoIn, float* outL, float* outR, int numSamples,
                       const SynthTrackState& p, float bpm)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float x = monoIn[i];

            // (Distortion is applied in STEREO by renderBlock before mono sum)

            // ── Redux ──
            if (p.reduxBits < 15.5f || p.reduxRate > 0.01f)
            {
                if (p.reduxRate > 0.01f)
                {
                    int hold = std::max (1, static_cast<int>(p.reduxRate * 64.0f));
                    if (++reduxCounter >= hold) { reduxHold = x; reduxCounter = 0; }
                    x = reduxHold;
                }
                if (p.reduxBits < 15.5f)
                {
                    float lev = std::pow (2.0f, std::max (1.0f, p.reduxBits));
                    x = std::round (x * lev) / lev;
                }
            }

            // ── Stereo Chorus (enhanced width) ──
            float chorusL = x, chorusR = x;
            if (p.chorusMix > 0.01f)
            {
                chorusBufL[static_cast<size_t>(chorusWritePos)] = x;
                chorusBufR[static_cast<size_t>(chorusWritePos)] = x;

                float rate = std::max (0.1f, p.chorusRate);
                float depth = std::max (0.01f, p.chorusDepth);
                float lfoL = std::sin (static_cast<float>(chorusLfoPhase * 6.283185307));
                float lfoR = std::sin (static_cast<float>(chorusLfoPhase * 6.283185307 + 1.5707963)); // 90° offset
                chorusLfoPhase += static_cast<double>(rate) / sampleRate;
                if (chorusLfoPhase >= 1.0) chorusLfoPhase -= 1.0;

                float baseD = kChorusBaseDelay * static_cast<float>(sampleRate);
                float modD = kChorusMaxDepth * depth * static_cast<float>(sampleRate);

                // Different base offsets for L and R (more stereo width)
                float baseOffR = baseD * 1.12f;
                chorusL = chorusRead (chorusBufL, chorusWritePos, baseD + lfoL * modD);
                chorusR = chorusRead (chorusBufR, chorusWritePos, baseOffR + lfoR * modD);
                chorusWritePos = (chorusWritePos + 1) % kChorusMaxDelay;

                float wet = p.chorusMix;
                chorusL = x * (1.0f - wet * 0.5f) + chorusL * wet;
                chorusR = x * (1.0f - wet * 0.5f) + chorusR * wet;
            }

            // ── Delay (sync or free/tape, mono or ping-pong, with feedback filter) ──
            if (p.delayMix > 0.01f)
            {
                // Compute target delay time
                float targetDelSec;
                if (p.delaySync && bpm > 20.0f)
                    targetDelSec = (60.0f / bpm) * p.delayBeats;
                else
                    targetDelSec = 0.001f + p.delayTime * 1.999f; // 1ms to 2s in free mode

                float targetDelSmp = targetDelSec * sampleRate;
                int bufLen = static_cast<int>(delayBuf.size());
                targetDelSmp = std::clamp (targetDelSmp, 4.0f, static_cast<float>(bufLen) - 4.0f);

                // Tape delay: smoothed read position
                // Sync: 10ms (track BPM changes fast, minimal pitch artifact)
                // Free: 25ms (responsive knob feel, still smooth pitch glide)
                float smoothTime = p.delaySync ? 0.01f : 0.025f;
                float smoothCoeff = 1.0f - std::exp (-1.0f / (sampleRate * smoothTime));
                smoothDelSmp += (targetDelSmp - smoothDelSmp) * smoothCoeff;

                // ── Delay algo modifications ──
                float algoReadOffset = 0.0f;
                float algoFBSat = 1.0f;       // 1.0 = clean, < 1.0 = saturated
                float algoDampMul = 1.0f;      // multiplier on damp cutoff
                float algoNoise = 0.0f;

                if (p.delayAlgo == 1) // TAPE: wow/flutter + saturation
                {
                    // Wow: slow LFO (0.4Hz) modulating delay time ±3ms
                    tapeWowPhase += 0.4 / sampleRate;
                    if (tapeWowPhase >= 1.0) tapeWowPhase -= 1.0;
                    float wow = std::sin (static_cast<float>(tapeWowPhase * 6.283185307)) * sampleRate * 0.003f;
                    // Flutter: faster LFO (5Hz) modulating ±0.5ms
                    float flutter = std::sin (static_cast<float>(tapeWowPhase * 6.283185307 * 12.5)) * sampleRate * 0.0005f;
                    algoReadOffset = wow + flutter;
                    algoFBSat = 0.7f;  // soft tape saturation on feedback
                    algoDampMul = 0.6f; // darker repeats (tape head loss)
                }
                else if (p.delayAlgo == 2) // BBD: clock jitter + noise + dark
                {
                    // Clock jitter: random per-sample delay variation ±1ms
                    bbdRng ^= bbdRng << 13; bbdRng ^= bbdRng >> 17; bbdRng ^= bbdRng << 5;
                    float rnd = static_cast<float>(bbdRng & 0xFFFF) / 65535.0f - 0.5f;
                    algoReadOffset = rnd * sampleRate * 0.001f;
                    // BBD noise floor
                    bbdRng ^= bbdRng << 13; bbdRng ^= bbdRng >> 17; bbdRng ^= bbdRng << 5;
                    algoNoise = (static_cast<float>(bbdRng & 0xFFFF) / 65535.0f - 0.5f) * 0.002f;
                    algoDampMul = 0.3f; // very dark — BBD has strong HF rolloff
                }
                else if (p.delayAlgo == 3) // DIFFUSE: allpass-smeared, washed-out repeats
                {
                    algoDampMul = 0.5f; // moderately darker
                    diffuseActive = true;
                }

                // Cubic Hermite interpolation for click-free tape-quality delay
                float readPos = smoothDelSmp + algoReadOffset;
                readPos = std::clamp (readPos, 2.0f, static_cast<float>(bufLen) - 3.0f);
                int dInt = static_cast<int>(readPos);
                float dFrac = readPos - static_cast<float>(dInt);

                auto readSample = [&](std::vector<float>& buf, int offset) -> float {
                    int idx = (delayWritePos - offset + bufLen) % bufLen;
                    return buf[static_cast<size_t>(idx)];
                };

                // 4-point cubic Hermite on L buffer
                float sL0 = readSample (delayBuf, dInt - 1);
                float sL1 = readSample (delayBuf, dInt);
                float sL2 = readSample (delayBuf, dInt + 1);
                float sL3 = readSample (delayBuf, dInt + 2);
                float cL1 = 0.5f*(sL2-sL0), cL2 = sL0-2.5f*sL1+2*sL2-0.5f*sL3, cL3 = 0.5f*(sL3-sL0)+1.5f*(sL1-sL2);
                float delL = ((cL3*dFrac+cL2)*dFrac+cL1)*dFrac+sL1;

                float fb = std::min (0.92f, p.delayFB);
                float dm = p.delayMix;

                // Feedback LP filter: damp controls cutoff (0=20kHz bright, 1=800Hz dark)
                // algoDampMul makes TAPE/BBD darker
                float dampFreq = 20000.0f * std::pow (0.04f, p.delayDamp) * algoDampMul;
                dampFreq = std::clamp (dampFreq, 200.0f, 20000.0f);
                float dampCoeff = 1.0f - std::exp (-6.2831853f * dampFreq / sampleRate);

                // Tape/BBD saturation on the delayed signal
                if (algoFBSat < 0.99f)
                {
                    float drive = 1.0f + (1.0f - algoFBSat) * 3.0f;
                    delL = std::tanh (delL * drive) / drive;
                }
                delL += algoNoise;

                // DIFFUSE algo: 4-stage allpass cascade on read signal
                if (diffuseActive)
                {
                    for (int da = 0; da < 4; ++da)
                    {
                        float delayed = diffAP[da].z;
                        float v = delL - delayed * 0.6f;
                        diffAP[da].z = v;
                        delL = delayed + v * 0.6f;
                    }
                }
                diffuseActive = false; // reset for next sample

                if (p.delayPP > 0)
                {
                    // ── TRUE PING-PONG: L→R→L bouncing ──
                    float sR0 = readSample (delayBufR, dInt - 1);
                    float sR1 = readSample (delayBufR, dInt);
                    float sR2 = readSample (delayBufR, dInt + 1);
                    float sR3 = readSample (delayBufR, dInt + 2);
                    float cR1 = 0.5f*(sR2-sR0), cR2 = sR0-2.5f*sR1+2*sR2-0.5f*sR3, cR3 = 0.5f*(sR3-sR0)+1.5f*(sR1-sR2);
                    float delR = ((cR3*dFrac+cR2)*dFrac+cR1)*dFrac+sR1;

                    // Tape/BBD saturation on R channel too
                    if (algoFBSat < 0.99f)
                    {
                        float drive = 1.0f + (1.0f - algoFBSat) * 3.0f;
                        delR = std::tanh (delR * drive) / drive;
                    }
                    delR += algoNoise;

                    // Apply feedback filter to the feedback signal
                    float fbL = std::tanh (delL * fb);
                    float fbR = std::tanh (delR * fb);
                    dlyFilterL += (fbL - dlyFilterL) * dampCoeff;
                    dlyFilterR += (fbR - dlyFilterR) * dampCoeff;

                    // Ping-pong routing: input→L, L feedback→R, R feedback→L
                    float monoIn = (chorusL + chorusR) * 0.5f;
                    delayBuf[static_cast<size_t>(delayWritePos)]  = monoIn + dlyFilterR;  // L: input + filtered R feedback
                    delayBufR[static_cast<size_t>(delayWritePos)] = dlyFilterL;            // R: filtered L feedback only
                    delayWritePos = (delayWritePos + 1) % bufLen;

                    chorusL = chorusL * (1.0f - dm * 0.5f) + delL * dm;
                    chorusR = chorusR * (1.0f - dm * 0.5f) + delR * dm;
                }
                else
                {
                    // ── MONO/STEREO delay ──
                    float fbSig = std::tanh (delL * fb);
                    dlyFilterL += (fbSig - dlyFilterL) * dampCoeff;

                    float monoIn = (chorusL + chorusR) * 0.5f;
                    delayBuf[static_cast<size_t>(delayWritePos)] = monoIn + dlyFilterL;
                    delayWritePos = (delayWritePos + 1) % bufLen;

                    chorusL = chorusL * (1.0f - dm * 0.5f) + delL * dm;
                    chorusR = chorusR * (1.0f - dm * 0.5f) + delL * dm;
                }
            }

            // ── Reverb (algo-selectable) ──
            if (p.reverbMix > 0.01f)
            {
                float revL = 0.0f, revR = 0.0f;
                switch (p.reverbAlgo)
                {
                    case 1: // PLATE
                        plateReverb.processSample (chorusL, chorusR, revL, revR,
                            p.reverbSize, p.reverbDamp, p.reverbMix);
                        break;
                    case 2: // SHIMMER
                        shimmerReverb.processSample (chorusL, chorusR, revL, revR,
                            p.reverbSize, p.reverbDamp, p.reverbMix);
                        break;
                    case 3: // GALACTIC
                        galacticReverb.processSample (chorusL, chorusR, revL, revR,
                            p.reverbSize, p.reverbDamp, p.reverbMix);
                        break;
                    case 4: // ROOM
                        roomReverb.processSample (chorusL, chorusR, revL, revR,
                            p.reverbSize, p.reverbDamp, p.reverbMix);
                        break;
                    case 5: // SPRING
                        springReverb.processSample (chorusL, chorusR, revL, revR,
                            p.reverbSize, p.reverbDamp, p.reverbMix);
                        break;
                    case 6: // NONLIN (gated)
                        nonlinReverb.processSample (chorusL, chorusR, revL, revR,
                            p.reverbSize, p.reverbDamp, p.reverbMix);
                        break;
                    default: // 0 = FDN
                        reverb.processSample (chorusL, chorusR, revL, revR,
                            p.reverbSize, p.reverbDamp, p.reverbMix,
                            10.0f, 1.0f);
                        break;
                }
                chorusL = revL;
                chorusR = revR;
            }

            // ── LP SVF 12dB — true stereo (L+R independent) ──
            if (p.fxLP < 19000.0f)
            {
                float g = std::tan (3.14159f * p.fxLP / static_cast<float>(sampleRate));
                float k = 1.414f;
                float a1 = 1.0f / (1.0f + g * (g + k));
                float a2 = g * a1;
                float a3 = g * a2;
                // Left
                float v3L = chorusL - lpIC2L;
                float v1L = a1 * lpIC1L + a2 * v3L;
                float v2L = lpIC2L + a2 * lpIC1L + a3 * v3L;
                lpIC1L = 2.0f * v1L - lpIC1L;
                lpIC2L = 2.0f * v2L - lpIC2L;
                if (!std::isfinite (lpIC1L)) lpIC1L = 0.0f;
                if (!std::isfinite (lpIC2L)) lpIC2L = 0.0f;
                chorusL = v2L;
                // Right
                float v3R = chorusR - lpIC2R;
                float v1R = a1 * lpIC1R + a2 * v3R;
                float v2R = lpIC2R + a2 * lpIC1R + a3 * v3R;
                lpIC1R = 2.0f * v1R - lpIC1R;
                lpIC2R = 2.0f * v2R - lpIC2R;
                if (!std::isfinite (lpIC1R)) lpIC1R = 0.0f;
                if (!std::isfinite (lpIC2R)) lpIC2R = 0.0f;
                chorusR = v2R;
            }

            // ── HP SVF 12dB — true stereo (L+R independent) ──
            if (p.fxHP > 25.0f)
            {
                float g = std::tan (3.14159f * p.fxHP / static_cast<float>(sampleRate));
                float k = 1.414f;
                float a1 = 1.0f / (1.0f + g * (g + k));
                float a2 = g * a1;
                float a3 = g * a2;
                // Left
                float v3L = chorusL - hpIC2L;
                float v1L = a1 * hpIC1L + a2 * v3L;
                float v2L = hpIC2L + a2 * hpIC1L + a3 * v3L;
                hpIC1L = 2.0f * v1L - hpIC1L;
                hpIC2L = 2.0f * v2L - hpIC2L;
                if (!std::isfinite (hpIC1L)) hpIC1L = 0.0f;
                if (!std::isfinite (hpIC2L)) hpIC2L = 0.0f;
                chorusL = chorusL - k * v1L - v2L;
                // Right
                float v3R = chorusR - hpIC2R;
                float v1R = a1 * hpIC1R + a2 * v3R;
                float v2R = hpIC2R + a2 * hpIC1R + a3 * v3R;
                hpIC1R = 2.0f * v1R - hpIC1R;
                hpIC2R = 2.0f * v2R - hpIC2R;
                if (!std::isfinite (hpIC1R)) hpIC1R = 0.0f;
                if (!std::isfinite (hpIC2R)) hpIC2R = 0.0f;
                chorusR = chorusR - k * v1R - v2R;
            }

            // ── 3-band EQ (low shelf 200Hz, mid bell 1kHz, high shelf 5kHz) ──
            if (std::abs (p.eqLow) > 0.1f || std::abs (p.eqMid) > 0.1f || std::abs (p.eqHigh) > 0.1f)
            {
                auto biquad = [](float x, float b0, float b1, float b2, float a1, float a2, float& z1, float& z2) {
                    float y = b0 * x + z1;
                    z1 = b1 * x - a1 * y + z2;
                    z2 = b2 * x - a2 * y;
                    return y;
                };
                float sr = static_cast<float>(sampleRate);
                // Low shelf 200Hz
                if (std::abs (p.eqLow) > 0.1f) {
                    float A = std::pow (10.0f, p.eqLow / 40.0f);
                    float w0 = 6.2832f * 200.0f / sr; float cs = std::cos(w0); float sn = std::sin(w0);
                    float al = sn * 0.7071f; float sqA = 2.0f * std::sqrt(A) * al;
                    float a0 = (A+1)+(A-1)*cs+sqA; float ia0 = 1.0f/a0;
                    float b0=A*((A+1)-(A-1)*cs+sqA)*ia0, b1=2*A*((A-1)-(A+1)*cs)*ia0, b2=A*((A+1)-(A-1)*cs-sqA)*ia0;
                    float ca1=-2*((A-1)+(A+1)*cs)*ia0, ca2=((A+1)+(A-1)*cs-sqA)*ia0;
                    chorusL = biquad(chorusL, b0,b1,b2, ca1,ca2, eqLZ1L, eqLZ2L);
                    chorusR = biquad(chorusR, b0,b1,b2, ca1,ca2, eqLZ1R, eqLZ2R);
                }
                // Mid bell 1kHz Q=0.7
                if (std::abs (p.eqMid) > 0.1f) {
                    float A = std::pow (10.0f, p.eqMid / 40.0f);
                    float w0 = 6.2832f * 1000.0f / sr; float cs = std::cos(w0); float sn = std::sin(w0);
                    float al = sn / 1.4f; // Q=0.7
                    float a0 = 1+al/A; float ia0 = 1.0f/a0;
                    float b0=(1+al*A)*ia0, b1=-2*cs*ia0, b2=(1-al*A)*ia0;
                    float ca1=-2*cs*ia0, ca2=(1-al/A)*ia0;
                    chorusL = biquad(chorusL, b0,b1,b2, ca1,ca2, eqMZ1L, eqMZ2L);
                    chorusR = biquad(chorusR, b0,b1,b2, ca1,ca2, eqMZ1R, eqMZ2R);
                }
                // High shelf 5kHz
                if (std::abs (p.eqHigh) > 0.1f) {
                    float A = std::pow (10.0f, p.eqHigh / 40.0f);
                    float w0 = 6.2832f * 5000.0f / sr; float cs = std::cos(w0); float sn = std::sin(w0);
                    float al = sn * 0.7071f; float sqA = 2.0f * std::sqrt(A) * al;
                    float a0 = (A+1)-(A-1)*cs+sqA; float ia0 = 1.0f/a0;
                    float b0=A*((A+1)+(A-1)*cs+sqA)*ia0, b1=-2*A*((A-1)+(A+1)*cs)*ia0, b2=A*((A+1)+(A-1)*cs-sqA)*ia0;
                    float ca1=2*((A-1)-(A+1)*cs)*ia0, ca2=((A+1)-(A-1)*cs-sqA)*ia0;
                    chorusL = biquad(chorusL, b0,b1,b2, ca1,ca2, eqHZ1L, eqHZ2L);
                    chorusR = biquad(chorusR, b0,b1,b2, ca1,ca2, eqHZ1R, eqHZ2R);
                }
            }

            outL[i] = chorusL;
            outR[i] = chorusR;
        }
    }

private:
    float chorusRead (const std::vector<float>& buf, int writeP, float delaySamples)
    {
        float readF = static_cast<float>(writeP) - delaySamples;
        if (readF < 0.0f) readF += static_cast<float>(kChorusMaxDelay);
        int r0 = static_cast<int>(readF) % kChorusMaxDelay;
        int r1 = (r0 + 1) % kChorusMaxDelay;
        float frac = readF - std::floor (readF);
        return buf[static_cast<size_t>(r0)] * (1.0f - frac) + buf[static_cast<size_t>(r1)] * frac;
    }
};

// ═══════════════════════════════════════════════════════════════════
// SynthEngine — 5 parts with per-track FX, chorus, pan → stereo
// ═══════════════════════════════════════════════════════════════════
class SynthEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& p : parts) p.prepare (sampleRate);
        for (auto& fx : trackFX) fx.prepare (sampleRate);
        maxBlock = static_cast<int>(sampleRate); // 1 sec max
        trackBufL.resize (static_cast<size_t>(maxBlock), 0.0f);
        trackBufR.resize (static_cast<size_t>(maxBlock), 0.0f);
        monoFXIn.resize (static_cast<size_t>(maxBlock), 0.0f);
        fxBufL.resize (static_cast<size_t>(maxBlock), 0.0f);
        fxBufR.resize (static_cast<size_t>(maxBlock), 0.0f);
    }

    void setTrackStates (SynthTrackState* states) { trackStates = states; }
    void setBPM (float bpm) { currentBPM = bpm; }
    void setGlobalAnySolo (bool s) { globalAnySolo = s; }

    // ── Resample tap: select which synth track to record (post-FX mono) ──
    void setResampleTap (int trackIdx, float* tapBuf, int tapBufLen)
    {
        resampleTapTrack = trackIdx;
        resampleTapBuf = tapBuf;
        resampleTapLen = tapBufLen;
    }
    void clearResampleTap() { resampleTapTrack = -1; resampleTapBuf = nullptr; resampleTapLen = 0; }

    void setMonoGlide (int partIndex, bool monoOn, float glideTimeSec)
    {
        if (partIndex >= 0 && partIndex < 5)
            parts[static_cast<size_t>(partIndex)].setMonoGlide (monoOn, glideTimeSec);
    }

    void killAll()
    {
        for (auto& p : parts) p.killAll();
        for (auto& fx : trackFX) fx.silenceDelays();
        for (auto& v : plockFXValid) v = false;
    }

    void killPart (int partIndex)
    {
        if (partIndex >= 0 && partIndex < 5)
            parts[static_cast<size_t>(partIndex)].killAll();
    }

    // Release all active voices in a part (trigger ADSR release, don't kill)
    void releaseVoices (int partIndex)
    {
        if (partIndex >= 0 && partIndex < 5)
            parts[static_cast<size_t>(partIndex)].releaseAll();
    }

    void noteOn (int partIndex, int noteIdx, int octave, float velocity,
                 const SynthVoiceParams& params, SynthModel model,
                 float gateDuration = 0.2f,
                 std::shared_ptr<juce::AudioBuffer<float>> sampleBuf = nullptr)
    {
        if (partIndex >= 0 && partIndex < 5)
            parts[static_cast<size_t>(partIndex)].noteOn (noteIdx, octave, velocity,
                                                           params, model, gateDuration, sampleBuf, currentBPM);
    }

    // Set warp marker data for a part's sampler voices (call before noteOn)
    void setWarpData (int partIndex, const std::vector<TimeStretch::WarpPt>& pts, float totalBeats)
    {
        if (partIndex >= 0 && partIndex < 5)
            parts[static_cast<size_t>(partIndex)].setWarpData (pts, totalBeats);
    }

    // Mark all active voices on a track as p-locked (Elektron-style: trigger-time params held)
    void markPlocked (int partIndex)
    {
        if (partIndex >= 0 && partIndex < 5)
            parts[static_cast<size_t>(partIndex)].markPlocked();
    }

    void setPlockFX (int trackIndex, const SynthTrackState& plocked, bool hasPlocks)
    {
        if (trackIndex >= 0 && trackIndex < 5)
        {
            if (hasPlocks)
            {
                plockFX[static_cast<size_t>(trackIndex)] = plocked;
                plockFXValid[static_cast<size_t>(trackIndex)] = true;
            }
            else
            {
                plockFXValid[static_cast<size_t>(trackIndex)] = false;
            }
        }
    }

    void renderBlock (float* outputL, float* outputR, int numSamples,
                      const float* duckGains = nullptr)
    {
        if (trackStates == nullptr) return;

        for (int trk = 0; trk < 5; ++trk)
        {
            auto& st = trackStates[trk];
            auto& fxParams = plockFXValid[static_cast<size_t>(trk)]
                ? plockFX[static_cast<size_t>(trk)]
                : trackStates[trk];

            // ── Push LFO-modulated params to all active voices ──
            {
                SynthVoiceParams svp;
                svp.w1 = st.w1; svp.w2 = st.w2;
                svp.tune = st.tune; svp.detune = st.detune;
                svp.mix2 = st.mix2; svp.subLevel = st.subLevel;
                svp.oscSync = st.oscSync; svp.syncRatio = st.syncRatio;
                svp.pwm = st.pwm; svp.unison = st.unison;
                svp.uniSpread = st.uniSpread; svp.uniStereo = st.uniStereo;
                svp.charType = st.charType; svp.charAmt = st.charAmt;
                svp.fmLinAmt = st.fmLinAmt; svp.fmLinRatio = st.fmLinRatio;
                svp.fmLinDecay = st.fmLinDecay; svp.fmLinSustain = st.fmLinSustain; svp.fmLinSnap = st.fmLinSnap;
                svp.fType = st.fType; svp.fPoles = st.fPoles; svp.fModel = st.fModel;
                svp.cut = st.cut; svp.res = st.res; svp.fenv = st.fenv;
                svp.fA = st.fA; svp.fD = st.fD; svp.fS = st.fS; svp.fR = st.fR;
                svp.aA = st.aA; svp.aD = st.aD; svp.aS = st.aS; svp.aR = st.aR;
                svp.volume = 1.0f;  // render at unity — real volume applied post-FX
                svp.fmAlgo = st.fmAlgo;
                svp.cRatio = st.cRatio; svp.cLevel = st.cLevel;
                svp.r2 = st.r2; svp.l2 = st.l2; svp.dc2 = st.dc2;
                svp.r3 = st.r3; svp.l3 = st.l3; svp.dc3 = st.dc3;
                svp.r4 = st.r4; svp.l4 = st.l4; svp.dc4 = st.dc4;
                svp.fmFeedback = st.fmFeedback;
                svp.cA = st.cA; svp.cD = st.cD; svp.cS = st.cS; svp.cR = st.cR;
                svp.elemBow = st.elemBow; svp.elemBlow = st.elemBlow;
                svp.elemStrike = st.elemStrike; svp.elemContour = st.elemContour;
                svp.elemMallet = st.elemMallet; svp.elemFlow = st.elemFlow;
                svp.elemGeometry = st.elemGeometry; svp.elemBright = st.elemBright;
                svp.elemDamping = st.elemDamping; svp.elemPosition = st.elemPosition;
                svp.elemSpace = st.elemSpace; svp.elemPitch = st.elemPitch;
                svp.plaitsModel = st.plaitsModel; svp.plaitsHarmonics = st.plaitsHarmonics;
                svp.plaitsTimbre = st.plaitsTimbre; svp.plaitsMorph = st.plaitsMorph;
                svp.plaitsDecay = st.plaitsDecay; svp.plaitsLpgColor = st.plaitsLpgColor;
                // Sampler — ALL params must be copied (otherwise defaults override knob values!)
                svp.smpStart = st.smpStart; svp.smpEnd = st.smpEnd; svp.smpGain = st.smpGain;
                svp.smpLoop = st.smpLoop; svp.smpReverse = st.smpReverse; svp.smpPlayMode = st.smpPlayMode;
                svp.smpTune = st.smpTune; svp.smpFine = st.smpFine; svp.smpRootNote = st.smpRootNote;
                svp.smpA = st.smpA; svp.smpD = st.smpD; svp.smpS = st.smpS; svp.smpR = st.smpR;
                svp.smpCut = st.smpCut; svp.smpRes = st.smpRes; svp.smpFType = st.smpFType;
                svp.smpFModel = st.smpFModel; svp.smpFPoles = st.smpFPoles;
                svp.smpFiltEnv = st.smpFiltEnv; svp.smpFiltA = st.smpFiltA; svp.smpFiltD = st.smpFiltD; svp.smpFiltS = st.smpFiltS; svp.smpFiltR = st.smpFiltR;
                svp.smpFmAmt = st.smpFmAmt; svp.smpFmRatio = st.smpFmRatio;
                svp.smpFmEnvA = st.smpFmEnvA; svp.smpFmEnvD = st.smpFmEnvD; svp.smpFmEnvS = st.smpFmEnvS;
                svp.smpStretch = st.smpStretch; svp.smpWarp = st.smpWarp;
                svp.smpBPM = st.smpBPM; svp.smpFileSR = st.smpFileSR; svp.smpBpmSync = st.smpBpmSync; svp.smpSyncMul = st.smpSyncMul; svp.smpBars = st.smpBars;
                // Wavetable params
                svp.wtPos1 = st.wtPos1; svp.wtPos2 = st.wtPos2; svp.wtMix = st.wtMix;
                svp.wtWarp1 = st.wtWarp1; svp.wtWarpAmt1 = st.wtWarpAmt1;
                svp.wtWarp2 = st.wtWarp2; svp.wtWarpAmt2 = st.wtWarpAmt2;
                svp.wtSubLevel = st.wtSubLevel;
                // Granular params
                svp.grainPos = st.grainPos; svp.grainSize = st.grainSize;
                svp.grainDensity = st.grainDensity; svp.grainSpray = st.grainSpray;
                svp.grainPitch = st.grainPitch; svp.grainPan = st.grainPan;
                svp.grainShape = st.grainShape; svp.grainDir = st.grainDir;
                svp.grainTexture = st.grainTexture; svp.grainFreeze = st.grainFreeze;
                svp.grainScan = st.grainScan;
                svp.grainMode = st.grainMode; svp.grainTilt = st.grainTilt;
                svp.grainUniVoices = st.grainUniVoices; svp.grainUniDetune = st.grainUniDetune; svp.grainUniStereo = st.grainUniStereo;
                svp.grainQuantize = st.grainQuantize; svp.grainFeedback = st.grainFeedback;
                svp.grainFmAmt = st.grainFmAmt; svp.grainFmRatio = st.grainFmRatio;
                svp.grainFmDecay = st.grainFmDecay; svp.grainFmSus = st.grainFmSus;
                svp.grainFmSnap = st.grainFmSnap;
                svp.grainFmSpread = st.grainFmSpread;
                // Auto-init wavetable data if model is WT but data is missing
                if (st.model == SynthModel::Wavetable)
                {
                    if (!st.wtData1)
                        st.wtData1 = std::make_shared<WavetableData>(WavetableData::createBasic());
                    if (!st.wtData2)
                        st.wtData2 = std::make_shared<WavetableData>(WavetableData::createBasic());
                }
                // Pass wavetable data pointers to part
                parts[static_cast<size_t>(trk)].setWTData (
                    st.wtData1 ? st.wtData1.get() : nullptr,
                    st.wtData2 ? st.wtData2.get() : nullptr);
                parts[static_cast<size_t>(trk)].updatePlayingVoices (svp, st.model, &st);
            }

            // Render voices to stereo track buffers
            std::fill (trackBufL.begin(), trackBufL.begin() + numSamples, 0.0f);
            std::fill (trackBufR.begin(), trackBufR.begin() + numSamples, 0.0f);
            parts[static_cast<size_t>(trk)].renderBlock (trackBufL.data(), trackBufR.data(), numSamples);

            // Update grain visualization data for GUI
            if (st.model == SynthModel::Granular)
                parts[static_cast<size_t>(trk)].updateGrainVis (trackStates[trk]);
            if (st.model == SynthModel::Sampler)
                parts[static_cast<size_t>(trk)].updateSmpPlayPos (trackStates[trk]);

            // Check for signal
            bool hasSignal = false;
            for (int i = 0; i < numSamples; ++i)
                if (std::abs (trackBufL[static_cast<size_t>(i)]) > 0.00001f ||
                    std::abs (trackBufR[static_cast<size_t>(i)]) > 0.00001f)
                { hasSignal = true; break; }

            bool hasTail = (fxParams.delayMix > 0.01f || fxParams.reverbMix > 0.01f ||
                           fxParams.chorusMix > 0.01f);

            if (!hasSignal && !hasTail) continue;

            // ── Stereo distortion (pre-FX, preserves stereo image) ──
            if (fxParams.distAmt > 0.01f)
            {
                float drive = 1.0f + fxParams.distAmt * 8.0f;
                float tanhD = std::tanh (drive);
                for (int i = 0; i < numSamples; ++i)
                {
                    trackBufL[static_cast<size_t>(i)] = std::tanh (trackBufL[static_cast<size_t>(i)] * drive) / tanhD;
                    trackBufR[static_cast<size_t>(i)] = std::tanh (trackBufR[static_cast<size_t>(i)] * drive) / tanhD;
                }
            }

            // ── Pro Distortion (TUBE/TAPE/XFMR/AMP/WSHP) ──
            if (fxParams.proDistDrive > 0.01f)
            {
                float drv = 1.0f + fxParams.proDistDrive * 20.0f;
                float bias = fxParams.proDistBias * 0.3f;
                auto& pdL = proDistLP_L[static_cast<size_t>(trk)];
                auto& pdR = proDistLP_R[static_cast<size_t>(trk)];
                for (int i = 0; i < numSamples; ++i)
                {
                    float dryL = trackBufL[static_cast<size_t>(i)];
                    float dryR = trackBufR[static_cast<size_t>(i)];
                    float xL = dryL, xR = dryR;
                    switch (fxParams.proDistModel)
                    {
                        case 0: // TUBE
                            xL = std::tanh((xL+bias)*drv) - std::tanh(bias*drv*0.5f);
                            xR = std::tanh((xR+bias)*drv) - std::tanh(bias*drv*0.5f);
                            xL += 0.1f*bias*xL*xL; xR += 0.1f*bias*xR*xR;
                            break;
                        case 1: // TAPE
                        { float cL=1.0f/(1.0f+std::abs(xL)*drv*0.3f); xL=std::tanh(xL*drv*0.7f)*cL+xL*(1.0f-cL)*0.3f;
                          float cR=1.0f/(1.0f+std::abs(xR)*drv*0.3f); xR=std::tanh(xR*drv*0.7f)*cR+xR*(1.0f-cR)*0.3f;
                          pdL+=(xL-pdL)*0.15f; pdR+=(xR-pdR)*0.15f;
                          xL+=pdL*0.15f*fxParams.proDistDrive; xR+=pdR*0.15f*fxParams.proDistDrive; break; }
                        case 2: // XFMR
                        { float x3L=xL*xL*xL, x3R=xR*xR*xR;
                          xL=xL*drv-x3L*drv*0.33f; xR=xR*drv-x3R*drv*0.33f;
                          xL=std::clamp(xL,-1.5f,1.5f)*0.67f; xR=std::clamp(xR,-1.5f,1.5f)*0.67f; break; }
                        case 3: // AMP
                          xL=(xL+bias)*drv; xR=(xR+bias)*drv;
                          xL=xL>0?std::tanh(xL):std::max(-1.0f,xL*0.8f+std::tanh(xL*0.2f));
                          xR=xR>0?std::tanh(xR):std::max(-1.0f,xR*0.8f+std::tanh(xR*0.2f)); break;
                        case 4: // WSHP
                        { float a=std::clamp(xL*drv,-1.0f,1.0f), b=std::clamp(xR*drv,-1.0f,1.0f);
                          xL=a*0.4f+(2*a*a-1)*bias*0.3f+(4*a*a*a-3*a)*(1-bias)*0.3f;
                          xR=b*0.4f+(2*b*b-1)*bias*0.3f+(4*b*b*b-3*b)*(1-bias)*0.3f; break; }
                    }
                    float tc=0.05f+fxParams.proDistTone*0.9f;
                    pdL+=(xL-pdL)*tc; pdR+=(xR-pdR)*tc;
                    xL=pdL*(1.0f-fxParams.proDistTone*0.3f)+xL*fxParams.proDistTone*0.3f;
                    xR=pdR*(1.0f-fxParams.proDistTone*0.3f)+xR*fxParams.proDistTone*0.3f;
                    float wet=fxParams.proDistMix;
                    trackBufL[static_cast<size_t>(i)]=dryL*(1-wet)+xL*wet;
                    trackBufR[static_cast<size_t>(i)]=dryR*(1-wet)+xR*wet;
                }
            }

            // FX: mono sum as input → stereo output
            std::fill (monoFXIn.begin(), monoFXIn.begin() + numSamples, 0.0f);
            for (int i = 0; i < numSamples; ++i)
                monoFXIn[static_cast<size_t>(i)] = (trackBufL[static_cast<size_t>(i)] + trackBufR[static_cast<size_t>(i)]) * 0.5f;

            std::fill (fxBufL.begin(), fxBufL.begin() + numSamples, 0.0f);
            std::fill (fxBufR.begin(), fxBufR.begin() + numSamples, 0.0f);
            trackFX[static_cast<size_t>(trk)].processBlock (monoFXIn.data(),
                fxBufL.data(), fxBufR.data(), numSamples, fxParams, currentBPM);

            // Blend: keep unison stereo image for dry, use FX stereo for wet
            // FX output already contains dry+wet mixed, so add back stereo diff
            for (int i = 0; i < numSamples; ++i)
            {
                float mono = monoFXIn[static_cast<size_t>(i)];
                float stereoL = trackBufL[static_cast<size_t>(i)] - mono;
                float stereoR = trackBufR[static_cast<size_t>(i)] - mono;
                fxBufL[static_cast<size_t>(i)] += stereoL;
                fxBufR[static_cast<size_t>(i)] += stereoR;
            }

            // ── OTT multiband compressor (post all FX) ──
            if (fxParams.ottDepth > 0.01f)
            {
                float srf = static_cast<float>(sr);
                float loC = std::min (0.5f, 250.0f * 6.2832f / srf);
                float hiC = std::min (0.5f, 2500.0f * 6.2832f / srf);

                // Per-band timing
                float loAtk = std::min (0.5f, 1.0f / (srf * 0.010f));
                float loRel = std::exp (-1.0f / (srf * 0.150f));
                float mdAtk = std::min (0.5f, 1.0f / (srf * 0.003f));
                float mdRel = std::exp (-1.0f / (srf * 0.060f));
                float hiAtk = std::min (0.5f, 1.0f / (srf * 0.0005f));
                float hiRel = std::exp (-1.0f / (srf * 0.025f));
                float agSmooth = std::exp (-1.0f / (srf * 0.050f));

                auto& ott = ottState[static_cast<size_t>(trk)];
                for (int i = 0; i < numSamples; ++i)
                {
                    float xL = fxBufL[static_cast<size_t>(i)];
                    float xR = fxBufR[static_cast<size_t>(i)];
                    float dryL = xL, dryR = xR;

                    // Band split
                    ott.xoLoL+=(xL-ott.xoLoL)*loC; ott.xoLoL2+=(ott.xoLoL-ott.xoLoL2)*loC;
                    float loL=ott.xoLoL2; float mhL=xL-loL;
                    ott.xoHiL+=(mhL-ott.xoHiL)*hiC; ott.xoHiL2+=(ott.xoHiL-ott.xoHiL2)*hiC;
                    float midL=ott.xoHiL2; float hiL=mhL-midL;
                    ott.xoLoR+=(xR-ott.xoLoR)*loC; ott.xoLoR2+=(ott.xoLoR-ott.xoLoR2)*loC;
                    float loR=ott.xoLoR2; float mhR=xR-loR;
                    ott.xoHiR+=(mhR-ott.xoHiR)*hiC; ott.xoHiR2+=(ott.xoHiR-ott.xoHiR2)*hiC;
                    float midR=ott.xoHiR2; float hiR=mhR-midR;

                    // Per-band envelope followers
                    auto uE=[](float&e, float l, float atk, float rel){
                        if(l>e) e += (l-e)*atk; else e = e*rel + l*(1.0f-rel);
                        e = std::max(e, 1e-8f);
                    };
                    uE(ott.envLo, std::sqrt(loL*loL+loR*loR+1e-12f), loAtk, loRel);
                    uE(ott.envMid, std::sqrt(midL*midL+midR*midR+1e-12f), mdAtk, mdRel);
                    uE(ott.envHi, std::sqrt(hiL*hiL+hiR*hiR+1e-12f), hiAtk, hiRel);

                    // dB-domain soft compression with per-band gain
                    float tgt = 0.22f;
                    auto compGain = [&](float env) -> float {
                        float dB = 20.0f * std::log10(env / tgt + 1e-20f);
                        float gainDB = 0.0f;
                        if (dB > 0.0f)
                            gainDB = -dB * fxParams.ottDownward * 0.85f;
                        else
                            gainDB = -dB * fxParams.ottUpward * 0.7f;
                        gainDB = std::clamp(gainDB, -24.0f, 24.0f);
                        return std::pow(10.0f, gainDB / 20.0f);
                    };

                    float gLo = compGain(ott.envLo);
                    float gMd = compGain(ott.envMid);
                    float gHi = compGain(ott.envHi);

                    float wetL = loL*gLo + midL*gMd + hiL*gHi;
                    float wetR = loR*gLo + midR*gMd + hiR*gHi;

                    // Auto-gain: match wet RMS to dry RMS
                    float dryPow = dryL*dryL + dryR*dryR + 1e-12f;
                    float wetPow = wetL*wetL + wetR*wetR + 1e-12f;
                    float agRatio = std::sqrt(dryPow / wetPow);
                    ott.autoGain = ott.autoGain * agSmooth + agRatio * (1.0f - agSmooth);
                    ott.autoGain = std::clamp(ott.autoGain, 0.25f, 4.0f);
                    wetL *= ott.autoGain;
                    wetR *= ott.autoGain;

                    float d = fxParams.ottDepth;
                    fxBufL[static_cast<size_t>(i)] = dryL*(1.0f-d) + wetL*d;
                    fxBufR[static_cast<size_t>(i)] = dryR*(1.0f-d) + wetR*d;
                }
            }

            // ── PHASER: 6-stage analog-style all-pass chain ──
            if (fxParams.phaserMix > 0.01f)
            {
                float phSr = static_cast<float>(sr);
                auto& ph = phState[static_cast<size_t>(trk)];
                for (int i = 0; i < numSamples; ++i)
                {
                    float xL = fxBufL[static_cast<size_t>(i)];
                    float xR = fxBufR[static_cast<size_t>(i)];
                    ph.lfoPhase += static_cast<double>(fxParams.phaserRate) / static_cast<double>(phSr);
                    if (ph.lfoPhase >= 1.0) ph.lfoPhase -= 1.0;
                    float lfo = std::sin (static_cast<float>(ph.lfoPhase * 6.283185307));
                    float modF = 200.0f * std::pow (20.0f, 0.5f + 0.5f * lfo * fxParams.phaserDepth);
                    modF = std::min (modF, phSr * 0.45f);
                    float t = std::tan (3.14159265f * modF / phSr);
                    float c = std::clamp ((t - 1.0f) / (t + 1.0f), -0.999f, 0.999f);
                    // Feed input + feedback
                    float inL = std::tanh (xL + ph.apL[5] * fxParams.phaserFB * 0.6f);
                    float inR = std::tanh (xR + ph.apR[5] * fxParams.phaserFB * 0.6f);
                    float apL = inL, apR = inR;
                    for (int s = 0; s < 6; ++s) {
                        float oL = c * apL + ph.apL[s];
                        ph.apL[s] = apL - c * oL;
                        apL = oL;
                        float oR = c * apR + ph.apR[s];
                        ph.apR[s] = apR - c * oR;
                        apR = oR;
                    }
                    float d = fxParams.phaserMix;
                    fxBufL[static_cast<size_t>(i)] = xL * (1.0f - d*0.5f) + apL * d * 0.5f;
                    fxBufR[static_cast<size_t>(i)] = xR * (1.0f - d*0.5f) + apR * d * 0.5f;
                }
            }

            // ── FLANGER: modulated short delay with feedback ──
            if (fxParams.flangerMix > 0.01f)
            {
                float flSr = static_cast<float>(sr);
                auto& fl = flState[static_cast<size_t>(trk)];
                for (int i = 0; i < numSamples; ++i)
                {
                    float xL = fxBufL[static_cast<size_t>(i)];
                    float xR = fxBufR[static_cast<size_t>(i)];
                    fl.lfoPhase += static_cast<double>(fxParams.flangerRate) / static_cast<double>(flSr);
                    if (fl.lfoPhase >= 1.0) fl.lfoPhase -= 1.0;
                    float lfo = std::sin (static_cast<float>(fl.lfoPhase * 6.283185307));
                    float minD = 0.0002f * flSr, maxD = 0.007f * flSr;
                    float dSmp = minD + (maxD-minD) * (0.5f + 0.5f * lfo * fxParams.flangerDepth);
                    dSmp = std::clamp (dSmp, 1.0f, 4094.0f);
                    float fb = std::clamp (fxParams.flangerFB, -0.95f, 0.95f);
                    fl.bufL[fl.wr] = xL + fl.fbL * fb;
                    fl.bufR[fl.wr] = xR + fl.fbR * fb;
                    int base = fl.wr - static_cast<int>(dSmp);
                    float frac = dSmp - std::floor(dSmp);
                    auto flRd = [&](const std::array<float,4096>& buf, int b, float f) {
                        int i0=(b-1+4096)%4096, i1=(b+4096)%4096, i2=(b+1+4096)%4096, i3=(b+2+4096)%4096;
                        float y0=buf[i0],y1=buf[i1],y2=buf[i2],y3=buf[i3];
                        float c0=y1,c1=0.5f*(y2-y0),c2=y0-2.5f*y1+2.0f*y2-0.5f*y3,c3=0.5f*(y3-y0)+1.5f*(y1-y2);
                        return ((c3*f+c2)*f+c1)*f+c0;
                    };
                    float wL = flRd(fl.bufL, base, frac);
                    float wR = flRd(fl.bufR, base, frac);
                    fl.fbL = wL; fl.fbR = wR;
                    fl.wr = (fl.wr + 1) % 4096;
                    float d = fxParams.flangerMix;
                    fxBufL[static_cast<size_t>(i)] = xL*(1.0f-d*0.3f) + wL*d;
                    fxBufR[static_cast<size_t>(i)] = xR*(1.0f-d*0.3f) + wR*d;
                }
            }

            // ── Resample tap: capture post-FX mono for this track ──
            if (resampleTapBuf != nullptr && resampleTapTrack == trk)
            {
                for (int i = 0; i < std::min (numSamples, resampleTapLen); ++i)
                    resampleTapBuf[i] = (fxBufL[static_cast<size_t>(i)] + fxBufR[static_cast<size_t>(i)]) * 0.5f;
            }

            // ── Track volume (post-FX, pre-pan) — smooth to prevent clicks ──
            float targetVol = fxParams.volume;
            float volSc = std::exp (-1.0f / (static_cast<float>(sr) * 0.005f));
            for (int i = 0; i < numSamples; ++i)
            {
                smoothVol[trk] = targetVol + (smoothVol[trk] - targetVol) * volSc;
                fxBufL[static_cast<size_t>(i)] *= smoothVol[trk];
                fxBufR[static_cast<size_t>(i)] *= smoothVol[trk];
            }

            // Apply pan + ducking with smoothing
            float pan = std::clamp (fxParams.pan, -1.0f, 1.0f);
            float angle = (pan + 1.0f) * 0.25f * 3.14159f;
            float targetL = std::cos (angle);
            float targetR = std::sin (angle);

            float dg = (duckGains != nullptr) ? duckGains[trk] : 1.0f;

            // Apply mute/solo
            if (trackStates != nullptr)
            {
                if (trackStates[trk].muted || (globalAnySolo && !trackStates[trk].solo))
                    dg = 0.0f;
            }

            targetL *= dg;
            targetR *= dg;

            // Smooth pan transitions (avoid clicks)
            float smoothCoeff = std::exp (-1.0f / (static_cast<float>(sr) * 0.005f)); // 5ms
            for (int i = 0; i < numSamples; ++i)
            {
                smoothPanL[trk] = targetL + (smoothPanL[trk] - targetL) * smoothCoeff;
                smoothPanR[trk] = targetR + (smoothPanR[trk] - targetR) * smoothCoeff;
                // Console 8 encode: sin() saturation per-channel before summing
                float chL = fxBufL[static_cast<size_t>(i)] * smoothPanL[trk];
                float chR = fxBufR[static_cast<size_t>(i)] * smoothPanR[trk];
                outputL[i] += consoleEncode (chL);
                outputR[i] += consoleEncode (chR);
            }
        }
    }

private:
    // Console 8 (Airwindows-style): sin() encode for analog summing character
    static inline float consoleEncode (float x)
    {
        return (std::abs (x) < 1.5707963f) ? std::sin (x) : ((x > 0.0f) ? 1.0f : -1.0f);
    }
    double sr = 44100.0;
    int maxBlock = 512;
    float currentBPM = 120.0f;
    bool globalAnySolo = false;
    SynthTrackState* trackStates = nullptr;
    std::array<SynthPart, 5> parts;
    std::array<SynthFX, 5> trackFX;
    std::array<SynthTrackState, 5> plockFX;
    std::array<bool, 5> plockFXValid {};
    std::vector<float> trackBufL, trackBufR, monoFXIn, fxBufL, fxBufR;
    std::array<float, 5> proDistLP_L {{}};
    std::array<float, 5> proDistLP_R {{}};
    std::array<float, 5> smoothPanL {{ 0.707f, 0.707f, 0.707f, 0.707f, 0.707f }};
    std::array<float, 5> smoothPanR {{ 0.707f, 0.707f, 0.707f, 0.707f, 0.707f }};
    std::array<float, 5> smoothVol {{ 0.8f, 0.8f, 0.8f, 0.8f, 0.8f }};  // post-FX volume smoothing

    // OTT multiband compressor state per track
    struct OTTState {
        float envLo=0,envMid=0,envHi=0;
        float xoLoL=0,xoLoL2=0,xoLoR=0,xoLoR2=0;
        float xoHiL=0,xoHiL2=0,xoHiR=0,xoHiR2=0;
        float autoGain=1.0f;
    };
    std::array<OTTState, 5> ottState {};

    struct PhaserState {
        std::array<float, 6> apL {}, apR {};
        double lfoPhase = 0.0;
    };
    std::array<PhaserState, 5> phState {};

    struct FlangerState {
        std::array<float, 4096> bufL {}, bufR {};
        int wr = 0;
        float fbL = 0.0f, fbR = 0.0f;
        double lfoPhase = 0.0;
    };
    std::array<FlangerState, 5> flState {};

    // Resample tap state
    int resampleTapTrack = -1;
    float* resampleTapBuf = nullptr;
    int resampleTapLen = 0;
};
