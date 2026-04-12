#pragma once
#include "DrumVoice.h"
#include "../../Sequencer/TrackState.h"
#include "../FX/FDNReverb.h"
#include "../FX/ReverbAlgos.h"
#include <array>
#include <vector>
#include <cmath>

// ═══════════════════════════════════════════════════════════════════
// Per-track FX chain: Dist → Redux → Chorus → Delay → Reverb → LP/HP → Pan
// Matches synth FX chain 1:1
// ═══════════════════════════════════════════════════════════════════

struct TrackFX
{
    double sampleRate = 44100.0;

    // Delay (L + R for ping-pong)
    std::vector<float> delayBuf, delayBufR;
    int delayWritePos = 0;
    float delayFBS = 0.0f, delayFBSR = 0.0f;
    float smoothDelSmp = 4410.0f;
    float dlyFilterL = 0.0f, dlyFilterR = 0.0f;

    // Chorus
    static constexpr int kChorusMax = 4096;
    static constexpr float kChorusBase = 0.007f;  // 7ms base
    static constexpr float kChorusDepth = 0.003f;  // 3ms max mod
    std::array<float, kChorusMax> chorusBufL {}, chorusBufR {};
    int chorusWr = 0;
    double chorusLfo = 0.0;

    // SVF filter states
    float lpIC1L = 0, lpIC2L = 0, lpIC1R = 0, lpIC2R = 0;
    float hpIC1L = 0, hpIC2L = 0, hpIC1R = 0, hpIC2R = 0;
    // 3-band EQ biquad states
    float eqLZ1L = 0, eqLZ2L = 0, eqLZ1R = 0, eqLZ2R = 0;
    float eqMZ1L = 0, eqMZ2L = 0, eqMZ1R = 0, eqMZ2R = 0;
    float eqHZ1L = 0, eqHZ2L = 0, eqHZ1R = 0, eqHZ2R = 0;

    // Redux
    float reduxHold = 0.0f;
    float reduxHoldR = 0.0f;
    int reduxCtr = 0;
    float proDistLP_L = 0.0f, proDistLP_R = 0.0f;  // tone filter state

    // OTT multiband compressor state (3 bands × L+R)
    float ottEnvLo = 0.0f, ottEnvMid = 0.0f, ottEnvHi = 0.0f;     // envelope followers
    float ottAutoGain = 1.0f;  // auto-gain compensation ratio
    float ottXoLoL = 0.0f, ottXoLoL2 = 0.0f, ottXoLoR = 0.0f, ottXoLoR2 = 0.0f; // LP crossover
    float ottXoHiL = 0.0f, ottXoHiL2 = 0.0f, ottXoHiR = 0.0f, ottXoHiR2 = 0.0f; // HP crossover

    // Reverb algo variants
    FDNReverb reverb;
    PlateReverb plateReverb;
    ShimmerReverb shimmerReverb;
    GalacticReverb galacticReverb;
    RoomReverb roomReverb;
    SpringReverb springReverb;
    NonlinReverb nonlinReverb;

    // Delay algo state
    double tapeWowPhase = 0.0;
    uint32_t bbdRng = 54321;
    struct DiffAP { float z = 0.0f; };
    std::array<DiffAP, 4> diffAP;

    // ── Phaser state (6-stage all-pass) ──
    static constexpr int kPhaserStages = 6;
    std::array<float, kPhaserStages> phApL {}, phApR {};  // all-pass states
    double phLfoPhase = 0.0;

    // ── Flanger state (modulated delay line) ──
    static constexpr int kFlangerBufSize = 4096;
    std::array<float, kFlangerBufSize> flBufL {}, flBufR {};
    int flWr = 0;
    float flFbL = 0.0f, flFbR = 0.0f;
    double flLfoPhase = 0.0;

    void prepare (double sr)
    {
        sampleRate = sr;
        delayBuf.assign (static_cast<size_t>(sr * 2.0) + 1, 0.0f);
        delayBufR.assign (static_cast<size_t>(sr * 2.0) + 1, 0.0f);
        delayWritePos = 0;
        delayFBS = delayFBSR = 0.0f;
        dlyFilterL = dlyFilterR = 0.0f;
        smoothDelSmp = 4410.0f;
        for (auto& s : chorusBufL) s = 0; for (auto& s : chorusBufR) s = 0;
        chorusWr = 0; chorusLfo = 0;
        lpIC1L = lpIC2L = lpIC1R = lpIC2R = hpIC1L = hpIC2L = hpIC1R = hpIC2R = 0;
        eqLZ1L = eqLZ2L = eqLZ1R = eqLZ2R = eqMZ1L = eqMZ2L = eqMZ1R = eqMZ2R = eqHZ1L = eqHZ2L = eqHZ1R = eqHZ2R = 0;
        reverb.prepare (sr);
        plateReverb.prepare (sr);
        shimmerReverb.prepare (sr);
        galacticReverb.prepare (sr);
        roomReverb.prepare (sr);
        springReverb.prepare (sr);
        nonlinReverb.prepare (sr);
        // Phaser + Flanger init
        for (auto& s : phApL) s = 0; for (auto& s : phApR) s = 0;
        phLfoPhase = 0;
        for (auto& s : flBufL) s = 0; for (auto& s : flBufR) s = 0;
        flWr = 0; flFbL = flFbR = 0; flLfoPhase = 0;
    }

    float chorusRead (const std::array<float, kChorusMax>& buf, int wr, float delaySmp)
    {
        delaySmp = std::clamp (delaySmp, 1.0f, static_cast<float>(kChorusMax - 2));
        int d = static_cast<int>(delaySmp);
        float f = delaySmp - static_cast<float>(d);
        int r0 = (wr - d + kChorusMax) % kChorusMax;
        int r1 = (r0 - 1 + kChorusMax) % kChorusMax;
        return buf[r0] * (1.0f - f) + buf[r1] * f;
    }

    void processBlock (float* bufL, float* bufR, float* outL, float* outR, int numSamples,
                       const DrumTrackState& p, float bpm)
    {
        float sr = static_cast<float>(sampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            float xL = bufL[i];
            float xR = bufR[i];

            // ── Distortion ──
            if (p.distAmt > 0.01f)
            {
                float drive = 1.0f + p.distAmt * 8.0f;
                float tanhDrv = std::tanh (drive);
                xL = std::tanh (xL * drive) / tanhDrv;
                xR = std::tanh (xR * drive) / tanhDrv;
            }

            // ── Pro Distortion (TUBE/TAPE/XFMR/AMP/WSHP) ──
            if (p.proDistDrive > 0.01f)
            {
                float dryL = xL, dryR = xR;
                float drv = 1.0f + p.proDistDrive * 20.0f;
                float bias = p.proDistBias * 0.3f;

                switch (p.proDistModel)
                {
                    case 0: // TUBE — asymmetric soft clip, even harmonics
                    {
                        xL = std::tanh ((xL + bias) * drv) - std::tanh (bias * drv * 0.5f);
                        xR = std::tanh ((xR + bias) * drv) - std::tanh (bias * drv * 0.5f);
                        // 2nd harmonic warmth from asymmetry
                        xL += 0.1f * bias * xL * xL;
                        xR += 0.1f * bias * xR * xR;
                        break;
                    }
                    case 1: // TAPE — magnetic saturation + compression
                    {
                        float comp = 1.0f / (1.0f + std::abs (xL) * drv * 0.3f);
                        xL = std::tanh (xL * drv * 0.7f) * comp + xL * (1.0f - comp) * 0.3f;
                        comp = 1.0f / (1.0f + std::abs (xR) * drv * 0.3f);
                        xR = std::tanh (xR * drv * 0.7f) * comp + xR * (1.0f - comp) * 0.3f;
                        // Tape head bump (subtle low boost)
                        proDistLP_L += (xL - proDistLP_L) * 0.15f;
                        proDistLP_R += (xR - proDistLP_R) * 0.15f;
                        xL += proDistLP_L * 0.15f * p.proDistDrive;
                        xR += proDistLP_R * 0.15f * p.proDistDrive;
                        break;
                    }
                    case 2: // XFMR — transformer iron saturation, odd harmonics
                    {
                        float x3L = xL * xL * xL;
                        float x3R = xR * xR * xR;
                        xL = xL * drv - x3L * drv * 0.33f;
                        xR = xR * drv - x3R * drv * 0.33f;
                        xL = std::clamp (xL, -1.5f, 1.5f) * 0.67f;
                        xR = std::clamp (xR, -1.5f, 1.5f) * 0.67f;
                        break;
                    }
                    case 3: // AMP — guitar amp, hard asymmetric clip + tone
                    {
                        xL = (xL + bias) * drv;
                        xR = (xR + bias) * drv;
                        // Asymmetric hard clip (positive clips softer)
                        xL = xL > 0.0f ? std::tanh (xL) : std::max (-1.0f, xL * 0.8f + std::tanh (xL * 0.2f));
                        xR = xR > 0.0f ? std::tanh (xR) : std::max (-1.0f, xR * 0.8f + std::tanh (xR * 0.2f));
                        break;
                    }
                    case 4: // WSHP — Chebyshev waveshaper
                    {
                        float a = std::clamp (xL * drv, -1.0f, 1.0f);
                        float b = std::clamp (xR * drv, -1.0f, 1.0f);
                        // T3(x) = 4x³ - 3x blended with T2(x) = 2x² - 1
                        float t3L = 4.0f*a*a*a - 3.0f*a, t2L = 2.0f*a*a - 1.0f;
                        float t3R = 4.0f*b*b*b - 3.0f*b, t2R = 2.0f*b*b - 1.0f;
                        xL = a * 0.4f + t2L * bias * 0.3f + t3L * (1.0f - bias) * 0.3f;
                        xR = b * 0.4f + t2R * bias * 0.3f + t3R * (1.0f - bias) * 0.3f;
                        break;
                    }
                }
                // Tone control (LP filter: 0=dark, 1=bright)
                float toneCoeff = 0.05f + p.proDistTone * 0.9f;
                proDistLP_L += (xL - proDistLP_L) * toneCoeff;
                proDistLP_R += (xR - proDistLP_R) * toneCoeff;
                xL = proDistLP_L * (1.0f - p.proDistTone * 0.3f) + xL * p.proDistTone * 0.3f;
                xR = proDistLP_R * (1.0f - p.proDistTone * 0.3f) + xR * p.proDistTone * 0.3f;
                // Dry/wet
                float wet = p.proDistMix;
                xL = dryL * (1.0f - wet) + xL * wet;
                xR = dryR * (1.0f - wet) + xR * wet;
            }

            // ── Redux ──
            if (p.reduxBits < 15.5f || p.reduxRate > 0.01f)
            {
                if (p.reduxRate > 0.01f)
                {
                    int hold = std::max (1, static_cast<int>(p.reduxRate * 64.0f));
                    if (++reduxCtr >= hold) { reduxHold = xL; reduxHoldR = xR; reduxCtr = 0; }
                    xL = reduxHold;
                    xR = reduxHoldR;
                }
                if (p.reduxBits < 15.5f)
                {
                    float lev = std::pow (2.0f, std::max (1.0f, p.reduxBits));
                    xL = std::round (xL * lev) / lev;
                    xR = std::round (xR * lev) / lev;
                }
            }

            // ── Stereo Chorus ──
            float cL = xL, cR = xR;
            if (p.chorusMix > 0.01f)
            {
                chorusBufL[chorusWr] = xL;
                chorusBufR[chorusWr] = xR;
                float rate = std::max (0.1f, p.chorusRate);
                float depth = std::max (0.01f, p.chorusDepth);
                float lfoL = std::sin (static_cast<float>(chorusLfo * 6.2832));
                float lfoR = std::sin (static_cast<float>(chorusLfo * 6.2832 + 1.5708));
                chorusLfo += static_cast<double>(rate) / sampleRate;
                if (chorusLfo >= 1.0) chorusLfo -= 1.0;
                float baseD = kChorusBase * sr;
                float modD = kChorusDepth * depth * sr;
                float baseRD = baseD * 1.12f;
                cL = chorusRead (chorusBufL, chorusWr, baseD + lfoL * modD);
                cR = chorusRead (chorusBufR, chorusWr, baseRD + lfoR * modD);
                chorusWr = (chorusWr + 1) % kChorusMax;
                float wet = p.chorusMix;
                cL = xL * (1.0f - wet * 0.5f) + cL * wet;
                cR = xR * (1.0f - wet * 0.5f) + cR * wet;
            }

            // ── Delay (sync/free + mono/ping-pong + algo variants) ──
            if (p.delayMix > 0.01f)
            {
                float targetSec;
                if (p.delaySync && bpm > 20.0f)
                    targetSec = (60.0f / bpm) * p.delayBeats;
                else
                    targetSec = 0.001f + p.delayTime * 1.999f;

                float targetSmp = targetSec * static_cast<float>(sr);
                int bufLen = static_cast<int>(delayBuf.size());
                targetSmp = std::clamp (targetSmp, 4.0f, static_cast<float>(bufLen) - 4.0f);

                float smoothTime = p.delaySync ? 0.01f : 0.025f;
                float smoothCoeff = 1.0f - std::exp (-1.0f / (static_cast<float>(sr) * smoothTime));
                smoothDelSmp += (targetSmp - smoothDelSmp) * smoothCoeff;

                // ── Delay algo modifiers ──
                float algoOffset = 0.0f;
                float algoDampMul = 1.0f;
                bool doDiffuse = false;

                if (p.delayAlgo == 1) // TAPE
                {
                    tapeWowPhase += 0.4 / sampleRate;
                    if (tapeWowPhase >= 1.0) tapeWowPhase -= 1.0;
                    float wow = std::sin (static_cast<float>(tapeWowPhase * 6.283185307)) * sr * 0.003f;
                    float flutter = std::sin (static_cast<float>(tapeWowPhase * 6.283185307 * 12.5)) * sr * 0.0005f;
                    algoOffset = wow + flutter;
                    algoDampMul = 0.6f;
                }
                else if (p.delayAlgo == 2) // BBD
                {
                    bbdRng ^= bbdRng << 13; bbdRng ^= bbdRng >> 17; bbdRng ^= bbdRng << 5;
                    algoOffset = (static_cast<float>(bbdRng & 0xFFFF) / 65535.0f - 0.5f) * sr * 0.001f;
                    algoDampMul = 0.3f;
                }
                else if (p.delayAlgo == 3) // DIFFUSE
                {
                    algoDampMul = 0.5f;
                    doDiffuse = true;
                }

                float readP = smoothDelSmp + algoOffset;
                readP = std::clamp (readP, 2.0f, static_cast<float>(bufLen) - 3.0f);
                int dI = static_cast<int>(readP);
                float dF = readP - static_cast<float>(dI);
                float fb = std::min (0.92f, p.delayFB);
                float dm = p.delayMix;

                float dampFreq = 20000.0f * std::pow (0.04f, p.delayDamp) * algoDampMul;
                dampFreq = std::clamp (dampFreq, 200.0f, 20000.0f);
                float dampCoeff = 1.0f - std::exp (-6.2831853f * dampFreq / static_cast<float>(sr));

                auto readCubic = [&](std::vector<float>& buf, int offset) -> float {
                    auto rd = [&](int o) -> float {
                        return buf[static_cast<size_t>((delayWritePos - o + bufLen) % bufLen)];
                    };
                    float s0 = rd(offset-1), s1 = rd(offset), s2 = rd(offset+1), s3 = rd(offset+2);
                    float c1=0.5f*(s2-s0), c2=s0-2.5f*s1+2*s2-0.5f*s3, c3=0.5f*(s3-s0)+1.5f*(s1-s2);
                    return ((c3*dF+c2)*dF+c1)*dF+s1;
                };

                float dL = readCubic (delayBuf, dI);

                // TAPE saturation
                if (p.delayAlgo == 1)
                {
                    float drive = 2.5f;
                    dL = std::tanh (dL * drive) / drive;
                }

                // DIFFUSE: allpass cascade
                if (doDiffuse)
                {
                    for (int da = 0; da < 4; ++da)
                    {
                        float delayed = diffAP[da].z;
                        float v = dL - delayed * 0.6f;
                        diffAP[da].z = v;
                        dL = delayed + v * 0.6f;
                    }
                }

                if (p.delayPP > 0)
                {
                    float dR = readCubic (delayBufR, dI);
                    if (p.delayAlgo == 1) { float dr=2.5f; dR = std::tanh(dR*dr)/dr; }
                    float fbL = std::tanh (dL * fb);
                    float fbR = std::tanh (dR * fb);
                    dlyFilterL += (fbL - dlyFilterL) * dampCoeff;
                    dlyFilterR += (fbR - dlyFilterR) * dampCoeff;
                    float monoIn = (cL + cR) * 0.5f;
                    delayBuf[static_cast<size_t>(delayWritePos)]  = monoIn + dlyFilterR;
                    delayBufR[static_cast<size_t>(delayWritePos)] = dlyFilterL;
                    delayWritePos = (delayWritePos + 1) % bufLen;
                    cL = cL * (1.0f - dm * 0.5f) + dL * dm;
                    cR = cR * (1.0f - dm * 0.5f) + dR * dm;
                }
                else
                {
                    float fbSig = std::tanh (dL * fb);
                    dlyFilterL += (fbSig - dlyFilterL) * dampCoeff;
                    float monoIn = (cL + cR) * 0.5f;
                    delayBuf[static_cast<size_t>(delayWritePos)] = monoIn + dlyFilterL;
                    delayWritePos = (delayWritePos + 1) % bufLen;
                    cL = cL * (1.0f - dm*0.5f) + dL * dm;
                    cR = cR * (1.0f - dm*0.5f) + dL * dm;
                }
            }

            // ── Reverb (algo-selectable, identical to SynthEngine) ──
            float sL = cL, sR = cR;
            if (p.reverbMix > 0.01f)
            {
                switch (p.reverbAlgo)
                {
                    case 1: plateReverb.processSample (cL, cR, sL, sR, p.reverbSize, p.reverbDamp, p.reverbMix); break;
                    case 2: shimmerReverb.processSample (cL, cR, sL, sR, p.reverbSize, p.reverbDamp, p.reverbMix); break;
                    case 3: galacticReverb.processSample (cL, cR, sL, sR, p.reverbSize, p.reverbDamp, p.reverbMix); break;
                    case 4: roomReverb.processSample (cL, cR, sL, sR, p.reverbSize, p.reverbDamp, p.reverbMix); break;
                    case 5: springReverb.processSample (cL, cR, sL, sR, p.reverbSize, p.reverbDamp, p.reverbMix); break;
                    case 6: nonlinReverb.processSample (cL, cR, sL, sR, p.reverbSize, p.reverbDamp, p.reverbMix); break;
                    default: reverb.processSample (cL, cR, sL, sR, p.reverbSize, p.reverbDamp, p.reverbMix, 10.0f, 0.8f); break;
                }
            }

            // ── LP SVF 12dB — true stereo (L+R independent) ──
            if (p.fxLP < 19000.0f)
            {
                float g = std::tan (3.14159f * std::clamp (p.fxLP, 20.0f, sr*0.49f) / sr);
                float k = 1.414f;
                float a1 = 1.0f / (1.0f + g*(g+k));
                float a2 = g*a1, a3 = g*a2;
                // Left
                float v3L = sL - lpIC2L;
                float v1L = a1*lpIC1L + a2*v3L;
                float v2L = lpIC2L + a2*lpIC1L + a3*v3L;
                lpIC1L = std::clamp (2.0f*v1L-lpIC1L, -8.0f, 8.0f);
                lpIC2L = std::clamp (2.0f*v2L-lpIC2L, -8.0f, 8.0f);
                sL = v2L;
                // Right
                float v3R = sR - lpIC2R;
                float v1R = a1*lpIC1R + a2*v3R;
                float v2R = lpIC2R + a2*lpIC1R + a3*v3R;
                lpIC1R = std::clamp (2.0f*v1R-lpIC1R, -8.0f, 8.0f);
                lpIC2R = std::clamp (2.0f*v2R-lpIC2R, -8.0f, 8.0f);
                sR = v2R;
            }

            // ── HP SVF 12dB — true stereo (L+R independent) ──
            if (p.fxHP > 25.0f)
            {
                float g = std::tan (3.14159f * std::clamp (p.fxHP, 20.0f, sr*0.49f) / sr);
                float k = 1.414f;
                float a1 = 1.0f / (1.0f + g*(g+k));
                float a2 = g*a1, a3 = g*a2;
                // Left
                float v3L = sL - hpIC2L;
                float v1L = a1*hpIC1L + a2*v3L;
                float v2L = hpIC2L + a2*hpIC1L + a3*v3L;
                hpIC1L = std::clamp (2.0f*v1L-hpIC1L, -8.0f, 8.0f);
                hpIC2L = std::clamp (2.0f*v2L-hpIC2L, -8.0f, 8.0f);
                sL = sL - k*v1L - v2L;
                // Right
                float v3R = sR - hpIC2R;
                float v1R = a1*hpIC1R + a2*v3R;
                float v2R = hpIC2R + a2*hpIC1R + a3*v3R;
                hpIC1R = std::clamp (2.0f*v1R-hpIC1R, -8.0f, 8.0f);
                hpIC2R = std::clamp (2.0f*v2R-hpIC2R, -8.0f, 8.0f);
                sR = sR - k*v1R - v2R;
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
                if (std::abs (p.eqLow) > 0.1f) {
                    float A = std::pow (10.0f, p.eqLow / 40.0f);
                    float w0 = 6.2832f * 200.0f / sr; float cs = std::cos(w0); float sn = std::sin(w0);
                    float al = sn * 0.7071f; float sqA = 2.0f * std::sqrt(A) * al;
                    float a0 = (A+1)+(A-1)*cs+sqA; float ia0 = 1.0f/a0;
                    float b0=A*((A+1)-(A-1)*cs+sqA)*ia0, b1i=2*A*((A-1)-(A+1)*cs)*ia0, b2=A*((A+1)-(A-1)*cs-sqA)*ia0;
                    float ca1=-2*((A-1)+(A+1)*cs)*ia0, ca2=((A+1)+(A-1)*cs-sqA)*ia0;
                    sL = biquad(sL, b0,b1i,b2, ca1,ca2, eqLZ1L, eqLZ2L);
                    sR = biquad(sR, b0,b1i,b2, ca1,ca2, eqLZ1R, eqLZ2R);
                }
                if (std::abs (p.eqMid) > 0.1f) {
                    float A = std::pow (10.0f, p.eqMid / 40.0f);
                    float w0 = 6.2832f * 1000.0f / sr; float cs = std::cos(w0); float sn = std::sin(w0);
                    float al = sn / 1.4f;
                    float a0 = 1+al/A; float ia0 = 1.0f/a0;
                    float b0=(1+al*A)*ia0, b1i=-2*cs*ia0, b2=(1-al*A)*ia0;
                    float ca1=-2*cs*ia0, ca2=(1-al/A)*ia0;
                    sL = biquad(sL, b0,b1i,b2, ca1,ca2, eqMZ1L, eqMZ2L);
                    sR = biquad(sR, b0,b1i,b2, ca1,ca2, eqMZ1R, eqMZ2R);
                }
                if (std::abs (p.eqHigh) > 0.1f) {
                    float A = std::pow (10.0f, p.eqHigh / 40.0f);
                    float w0 = 6.2832f * 5000.0f / sr; float cs = std::cos(w0); float sn = std::sin(w0);
                    float al = sn * 0.7071f; float sqA = 2.0f * std::sqrt(A) * al;
                    float a0 = (A+1)-(A-1)*cs+sqA; float ia0 = 1.0f/a0;
                    float b0=A*((A+1)+(A-1)*cs+sqA)*ia0, b1i=-2*A*((A-1)+(A+1)*cs)*ia0, b2=A*((A+1)+(A-1)*cs-sqA)*ia0;
                    float ca1=2*((A-1)-(A+1)*cs)*ia0, ca2=((A+1)-(A-1)*cs-sqA)*ia0;
                    sL = biquad(sL, b0,b1i,b2, ca1,ca2, eqHZ1L, eqHZ2L);
                    sR = biquad(sR, b0,b1i,b2, ca1,ca2, eqHZ1R, eqHZ2R);
                }
            }

            // ── OTT multiband compressor (post all FX) ──
            if (p.ottDepth > 0.01f)
            {
                float dryL = sL, dryR = sR;
                float osr = static_cast<float>(sampleRate > 0 ? sampleRate : 48000);

                // ── Linkwitz-Riley 2nd-order crossover (250Hz / 2.5kHz) ──
                float loC = std::min (0.5f, 250.0f * 6.2832f / osr);
                float hiC = std::min (0.5f, 2500.0f * 6.2832f / osr);

                // Band splitting: 2-pole LP → lo, subtract → mid+hi, 2-pole LP → mid, subtract → hi
                ottXoLoL += (sL - ottXoLoL)*loC; ottXoLoL2 += (ottXoLoL - ottXoLoL2)*loC;
                float loL = ottXoLoL2; float mhL = sL - loL;
                ottXoHiL += (mhL - ottXoHiL)*hiC; ottXoHiL2 += (ottXoHiL - ottXoHiL2)*hiC;
                float midL = ottXoHiL2; float hiL = mhL - midL;
                ottXoLoR += (sR - ottXoLoR)*loC; ottXoLoR2 += (ottXoLoR - ottXoLoR2)*loC;
                float loR = ottXoLoR2; float mhR = sR - loR;
                ottXoHiR += (mhR - ottXoHiR)*hiC; ottXoHiR2 += (ottXoHiR - ottXoHiR2)*hiC;
                float midR = ottXoHiR2; float hiR = mhR - midR;

                // ── Per-band envelope followers (different timing per band) ──
                // Low: slow attack/release for smooth bass handling
                // Mid: medium speed
                // Hi: fast for transient detail
                float loAtk = std::min (0.5f, 1.0f / (osr * 0.010f));   // 10ms attack
                float loRel = std::exp (-1.0f / (osr * 0.150f));        // 150ms release
                float mdAtk = std::min (0.5f, 1.0f / (osr * 0.003f));   // 3ms attack
                float mdRel = std::exp (-1.0f / (osr * 0.060f));        // 60ms release
                float hiAtk = std::min (0.5f, 1.0f / (osr * 0.0005f));  // 0.5ms attack
                float hiRel = std::exp (-1.0f / (osr * 0.025f));        // 25ms release

                // Envelope update per band
                auto uE=[](float&e, float l, float atk, float rel){
                    if(l>e) e += (l-e)*atk; else e = e*rel + l*(1.0f-rel);
                    e = std::max(e, 1e-8f);
                };

                float envInLo = std::sqrt(loL*loL + loR*loR + 1e-12f);
                float envInMid = std::sqrt(midL*midL + midR*midR + 1e-12f);
                float envInHi = std::sqrt(hiL*hiL + hiR*hiR + 1e-12f);
                uE(ottEnvLo, envInLo, loAtk, loRel);
                uE(ottEnvMid, envInMid, mdAtk, mdRel);
                uE(ottEnvHi, envInHi, hiAtk, hiRel);

                // ── Compression gain with soft knee ──
                // Target: bring each band toward a reference level
                // Higher target = less volume loss
                float tgt = 0.22f;  // ~-13 dBFS RMS reference
                auto compGain = [&](float env) -> float {
                    float dB = 20.0f * std::log10(env / tgt + 1e-20f);
                    float gainDB = 0.0f;
                    if (dB > 0.0f) {
                        // Downward compression: reduce loud signals
                        // Soft knee with smooth transition
                        gainDB = -dB * p.ottDownward * 0.85f;
                    } else {
                        // Upward compression: boost quiet signals
                        gainDB = -dB * p.ottUpward * 0.7f;
                    }
                    // Clamp to prevent extreme gain (±24 dB)
                    gainDB = std::clamp(gainDB, -24.0f, 24.0f);
                    return std::pow(10.0f, gainDB / 20.0f);
                };

                float gL = compGain(ottEnvLo);
                float gM = compGain(ottEnvMid);
                float gH = compGain(ottEnvHi);

                // ── Recombine bands with compression ──
                float wetL = loL*gL + midL*gM + hiL*gH;
                float wetR = loR*gL + midR*gM + hiR*gH;

                // ── Auto-gain: match RMS of wet to dry ──
                float dryPow = dryL*dryL + dryR*dryR + 1e-12f;
                float wetPow = wetL*wetL + wetR*wetR + 1e-12f;
                float agRatio = std::sqrt(dryPow / wetPow);
                // Smooth the auto-gain ratio to avoid pumping
                float agSmooth = std::exp(-1.0f / (osr * 0.050f)); // 50ms
                ottAutoGain = ottAutoGain * agSmooth + agRatio * (1.0f - agSmooth);
                ottAutoGain = std::clamp(ottAutoGain, 0.25f, 4.0f);

                wetL *= ottAutoGain;
                wetR *= ottAutoGain;

                // ── Dry/wet blend via depth ──
                float d = p.ottDepth;
                sL = dryL * (1.0f - d) + wetL * d;
                sR = dryR * (1.0f - d) + wetR * d;
            }

            // ── PHASER: 6-stage analog-style all-pass chain ──
            if (p.phaserMix > 0.01f)
            {
                float phSr = static_cast<float>(sampleRate > 0 ? sampleRate : 48000);
                // LFO
                phLfoPhase += static_cast<double>(p.phaserRate) / static_cast<double>(phSr);
                if (phLfoPhase >= 1.0) phLfoPhase -= 1.0;
                float lfo = std::sin (static_cast<float>(phLfoPhase * 6.283185307));
                // Sweep 200Hz–4kHz exponential
                float modF = 200.0f * std::pow (20.0f, 0.5f + 0.5f * lfo * p.phaserDepth);
                modF = std::min (modF, phSr * 0.45f); // stay below Nyquist
                // All-pass coefficient: c = (tan(pi*fc/sr) - 1) / (tan(pi*fc/sr) + 1)
                float t = std::tan (3.14159265f * modF / phSr);
                float c = (t - 1.0f) / (t + 1.0f);
                c = std::clamp (c, -0.999f, 0.999f);
                // Feed input + feedback from last stage
                float inL = sL + phApL[kPhaserStages - 1] * p.phaserFB * 0.6f;
                float inR = sR + phApR[kPhaserStages - 1] * p.phaserFB * 0.6f;
                // Soft-clip feedback to prevent blowup
                inL = std::tanh (inL);
                inR = std::tanh (inR);
                // 6 cascaded 1st-order all-pass stages
                float apL = inL, apR = inR;
                for (int s = 0; s < kPhaserStages; ++s)
                {
                    // y[n] = c * (x[n] + y[n-1]) - x[n-1]  ... no
                    // Standard: out = c*in + state; state = in - c*out
                    float oL = c * apL + phApL[s];
                    phApL[s] = apL - c * oL;
                    apL = oL;
                    float oR = c * apR + phApR[s];
                    phApR[s] = apR - c * oR;
                    apR = oR;
                }
                float d = p.phaserMix;
                sL = sL * (1.0f - d * 0.5f) + apL * d * 0.5f;
                sR = sR * (1.0f - d * 0.5f) + apR * d * 0.5f;
            }

            // ── FLANGER: modulated short delay with feedback ──
            if (p.flangerMix > 0.01f)
            {
                float flSr = static_cast<float>(sampleRate > 0 ? sampleRate : 48000);
                // LFO: sine with slight triangle character
                flLfoPhase += static_cast<double>(p.flangerRate) / static_cast<double>(flSr);
                if (flLfoPhase >= 1.0) flLfoPhase -= 1.0;
                float lfo = std::sin (static_cast<float>(flLfoPhase * 6.283185307));
                // Delay range: 0.2ms - 7ms (classic flanger range)
                float minDly = 0.0002f * flSr; // ~9 samples at 44.1k
                float maxDly = 0.007f * flSr;   // ~309 samples at 44.1k
                float delaySmp = minDly + (maxDly - minDly) * (0.5f + 0.5f * lfo * p.flangerDepth);
                delaySmp = std::clamp (delaySmp, 1.0f, static_cast<float>(kFlangerBufSize - 2));
                // Write into buffer (with feedback)
                float fb = std::clamp (p.flangerFB, -0.95f, 0.95f);
                flBufL[flWr] = sL + flFbL * fb;
                flBufR[flWr] = sR + flFbR * fb;
                // Read with cubic Hermite interpolation
                int idxBase = flWr - static_cast<int>(delaySmp);
                float frac = delaySmp - std::floor (delaySmp);
                auto flRead = [&](const std::array<float, kFlangerBufSize>& buf, int base, float fr) -> float {
                    int i0 = (base - 1 + kFlangerBufSize) % kFlangerBufSize;
                    int i1 = (base     + kFlangerBufSize) % kFlangerBufSize;
                    int i2 = (base + 1 + kFlangerBufSize) % kFlangerBufSize;
                    int i3 = (base + 2 + kFlangerBufSize) % kFlangerBufSize;
                    float y0 = buf[i0], y1 = buf[i1], y2 = buf[i2], y3 = buf[i3];
                    float c0 = y1, c1 = 0.5f*(y2-y0), c2 = y0-2.5f*y1+2.0f*y2-0.5f*y3;
                    float c3 = 0.5f*(y3-y0)+1.5f*(y1-y2);
                    return ((c3*fr+c2)*fr+c1)*fr+c0;
                };
                float wetL = flRead (flBufL, idxBase, frac);
                float wetR = flRead (flBufR, idxBase, frac);
                flFbL = wetL; flFbR = wetR;  // store for feedback
                flWr = (flWr + 1) % kFlangerBufSize;
                // Mix with slight volume compensation
                float d = p.flangerMix;
                sL = sL * (1.0f - d * 0.3f) + wetL * d;
                sR = sR * (1.0f - d * 0.3f) + wetR * d;
            }

            outL[i] = sL;
            outR[i] = sR;
        }
    }
};

// ═══════════════════════════════════════════════════════════════════
// DrumSynth — 4 voices per track, per-track FX, mix to output
// ═══════════════════════════════════════════════════════════════════

class DrumSynth
{
public:
    DrumSynth() = default;

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        maxBlock = blockSize;
        for (auto& trackVoices : voices)
            for (auto& voice : trackVoices)
                voice.prepare (sampleRate, blockSize);
        for (auto& fx : trackFX) fx.prepare (sampleRate);
        trackBuf.assign (static_cast<size_t>(blockSize), 0.0f);
        trackBufR.assign (static_cast<size_t>(blockSize), 0.0f);
        fxBufL.assign (static_cast<size_t>(blockSize), 0.0f);
        fxBufR.assign (static_cast<size_t>(blockSize), 0.0f);
        for (auto& v : smoothPanL) v = 0.707f;
        for (auto& v : smoothPanR) v = 0.707f;
        for (auto& v : smoothVol) v = 0.8f;
    }

    void setTrackStates (DrumTrackState* states) { trackStates = states; }
    void setBPM (float b) { currentBPM = b; }
    void setGlobalAnySolo (bool s) { globalAnySolo = s; }

    // ── Resample tap: select which track to record (post-FX mono) ──
    void setResampleTap (int trackIdx, float* tapBuf, int tapBufLen)
    {
        resampleTapTrack = trackIdx;
        resampleTapBuf = tapBuf;
        resampleTapLen = tapBufLen;
    }
    void clearResampleTap() { resampleTapTrack = -1; resampleTapBuf = nullptr; resampleTapLen = 0; }

    void killAll()
    {
        for (auto& trackVoices : voices)
            for (auto& v : trackVoices) v.kill();
        for (auto& fx : trackFX)
        {
            std::fill (fx.delayBuf.begin(), fx.delayBuf.end(), 0.0f);
            std::fill (fx.delayBufR.begin(), fx.delayBufR.end(), 0.0f);
            fx.delayFBS = fx.delayFBSR = 0.0f;
            std::fill (fx.chorusBufL.begin(), fx.chorusBufL.end(), 0.0f);
            std::fill (fx.chorusBufR.begin(), fx.chorusBufR.end(), 0.0f);
            fx.reverb.reset();
            fx.plateReverb.reset();
            fx.shimmerReverb.reset();
            fx.galacticReverb.reset();
            fx.roomReverb.reset();
            fx.springReverb.reset();
            fx.nonlinReverb.reset();
            fx.lpIC1L = fx.lpIC2L = fx.lpIC1R = fx.lpIC2R = fx.hpIC1L = fx.hpIC2L = fx.hpIC1R = fx.hpIC2R = 0.0f;
            fx.eqLZ1L = fx.eqLZ2L = fx.eqLZ1R = fx.eqLZ2R = fx.eqMZ1L = fx.eqMZ2L = fx.eqMZ1R = fx.eqMZ2R = fx.eqHZ1L = fx.eqHZ2L = fx.eqHZ1R = fx.eqHZ2R = 0.0f;
        }
        for (auto& v : plockFXValid) v = false;
    }

    void killTrack (int trackIdx)
    {
        if (trackIdx >= 0 && trackIdx < 10)
            for (auto& v : voices[static_cast<size_t>(trackIdx)]) v.kill();
    }

    void trigger (int trackIndex, float velocity, const DrumTrackState& params, float gateScale, bool plocked = false)
    {
        if (trackIndex < 0 || trackIndex >= 10) return;

        // Only freeze FX params when the voice is actually p-locked
        // Otherwise FX read from live trackStates (responds to knob/LFO/trigless in real-time)
        if (plocked)
        {
            plockFX[static_cast<size_t>(trackIndex)] = params;
            plockFXValid[static_cast<size_t>(trackIndex)] = true;
        }
        else
        {
            plockFXValid[static_cast<size_t>(trackIndex)] = false;
        }
        auto& trackVoices = voices[static_cast<size_t>(trackIndex)];
        // Hard-kill all previous voices (no fade = no double-trigger)
        for (auto& voice : trackVoices) voice.hardKill();
        // Always use voice 0 (no round-robin needed for drums)
        trackVoices[0].trigger (params.type, velocity, params, gateScale, currentBPM, plocked);
    }

    void renderBlock (float* outputL, float* outputR, int numSamples,
                      const float* duckGains = nullptr)
    {
        for (int trk = 0; trk < 10; ++trk)
        {
            auto& tv = voices[static_cast<size_t>(trk)];
            auto& fxParams = plockFXValid[static_cast<size_t>(trk)]
                ? plockFX[static_cast<size_t>(trk)]
                : trackStates[trk];

            bool anyPlaying = false;
            for (auto& v : tv) if (v.isPlaying()) { anyPlaying = true; break; }
            bool hasTail = (fxParams.delayMix > 0.01f || fxParams.reverbMix > 0.01f);
            if (!anyPlaying && !hasTail) continue;

            std::fill (trackBuf.begin(), trackBuf.begin() + numSamples, 0.0f);
            std::fill (trackBufR.begin(), trackBufR.begin() + numSamples, 0.0f);
            // Update live params from track state (so knob tweaks respond in real-time)
            for (auto& v : tv)
                if (v.isPlaying()) v.updateLiveParams (trackStates[trk]);
            for (auto& v : tv) if (v.isPlaying()) v.renderBlock (trackBuf.data(), trackBufR.data(), numSamples);

            // Update sampler playback position for GUI
            if (trackStates != nullptr && trackStates[trk].drumEngine == 2)
            {
                trackStates[trk].smpPlayPos = -1.0f;
                for (auto& v : tv)
                {
                    float pp = v.getSmpPlayPosition();
                    if (pp >= 0.0f) { trackStates[trk].smpPlayPos = pp; break; }
                }
            }

            std::fill (fxBufL.begin(), fxBufL.begin() + numSamples, 0.0f);
            std::fill (fxBufR.begin(), fxBufR.begin() + numSamples, 0.0f);
            trackFX[static_cast<size_t>(trk)].processBlock (trackBuf.data(), trackBufR.data(),
                fxBufL.data(), fxBufR.data(), numSamples, fxParams, currentBPM);

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

            targetL *= dg; targetR *= dg;

            float sc = std::exp (-1.0f / (static_cast<float>(sr) * 0.005f));
            for (int i = 0; i < numSamples; ++i)
            {
                smoothPanL[trk] = targetL + (smoothPanL[trk] - targetL) * sc;
                smoothPanR[trk] = targetR + (smoothPanR[trk] - targetR) * sc;
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
    static constexpr int kVoicesPerTrack = 4;
    double sr = 44100.0;
    int maxBlock = 512;
    std::array<std::array<DrumVoice, kVoicesPerTrack>, 10> voices;
    std::array<float, 10> smoothPanL {}, smoothPanR {};
    std::array<float, 10> smoothVol {};  // post-FX volume smoothing per track
    std::array<int, 10> nextVoiceIdx {};
    std::array<TrackFX, 10> trackFX;
    DrumTrackState* trackStates = nullptr;
    std::array<DrumTrackState, 10> plockFX;
    std::array<bool, 10> plockFXValid {};
    float currentBPM = 120.0f;
    bool globalAnySolo = false;
    std::vector<float> trackBuf, trackBufR, fxBufL, fxBufR;

    // Resample tap state
    int resampleTapTrack = -1;
    float* resampleTapBuf = nullptr;
    int resampleTapLen = 0;
};
