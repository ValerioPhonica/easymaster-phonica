#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../../Sequencer/TrackState.h"
#include "../FX/TimeStretch.h"
#include "../FX/MultiModelFilter.h"
#include <cmath>
#include <random>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// High-quality drum voice — multi-layer synthesis, SVF filters,
// polyBLEP anti-aliased oscillators, matching HTML GrooveBox quality
// ═══════════════════════════════════════════════════════════════════

class DrumVoice
{
public:
    DrumVoice() = default;

    void prepare (double sr, int /*blockSize*/)
    {
        sampleRate = sr;
        dt = 1.0 / sr;
        playing = false;

        // Pre-generate noise buffer (white noise)
        std::mt19937 gen { 42 };
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        noiseLen = static_cast<int>(sr * 2);
        noiseBuffer.resize (static_cast<size_t>(noiseLen));
        for (auto& s : noiseBuffer) s = dist (gen);
    }

    void trigger (DrumType type, float velocity, const DrumTrackState& params, float gateScale, float currentBPM = 120.0f, bool plocked = false)
    {
        drumType = type;
        vel = velocity;
        gate = gateScale;
        playing = true;
        hasPlocks = plocked;
        t = 0.0;
        noiseIdx = 0;

        // Reset all oscillator phases
        phase1 = 0.0; phase2 = 0.0; phase3 = 0.0;
        fmPhase1 = 0.0; fmPhase2 = 0.0; fmPhase3 = 0.0;
        for (auto& mp : metalPhases) mp = 0.0;
        // Reset filters
        svf1.reset(); svf2.reset(); svf3.reset(); svf4.reset(); svf5.reset();
        // Reset envelope
        ampEnv = 0.0f;
        smoothFmMix = p.fmMix;
        smoothDrumCut = p.drumCut;
        smoothDrumRes = p.drumRes;
        envA = 1.0f; envB = 1.0f; envC = 1.0f; envD = 1.0f;
        fadeIn = 0.0f;
        killFade = 0.0f;
        filtEnvVal = 0.0f;

        // ER-1 reset
        er1Phase1 = 0.0; er1Phase2 = 0.0;
        er1PEnv1 = 1.0f; er1PEnv2 = 1.0f;
        er1AEnv = 1.0f; er1NEnv = 1.0f;
        er1FiltLP = 0.0f; er1FiltBP = 0.0f;

        // Cache params
        p = params;

        // Sample playback init
        if (p.drumEngine == 2 && p.sampleData != nullptr)
        {
            smpBuf = p.sampleData;
            int totalSmp = smpBuf->getNumSamples();
            smpPlayRate = std::pow (2.0, (p.smpTune + p.smpFine) / 12.0);
            if (p.smpReverse > 0) smpPlayRate = -smpPlayRate;
            bool wasSmpPlaying = smpPlaying;
            smpPlaying = true;
            smpEndFade = 0;
            smpAmpEnv = 0.0f;
            smpAmpStage = 0; // attack
            smpFiltEnvVal = 0.0f;
            smpFiltEnvStage = 0; // attack
            smpGateTimer = 0.0;
            // GATE mode: calculate gate duration in seconds from gateScale + BPM
            if (p.smpPlayMode == 1 && currentBPM > 10.0f)
                smpGateDurSec = (gateScale * 60.0f / currentBPM / 4.0f);
            else
                smpGateDurSec = 0.0f; // 0 = one-shot (no gate cutoff)
            // Filter: soft reset if sample was playing (avoid resonance click)
            if (wasSmpPlaying) { smpFilter.softReset(); smpFilterR.softReset(); }
            else               { smpFilter.reset(); smpFilterR.reset(); }
            smpFmPhase = 0.0;
            smpFmEnvVal = 0.0f;
            smpFmEnvStage = 0;

            // Compute region — no snapToZero when time-stretching (need exact boundaries)
            const float* snapSrc = smpBuf->getReadPointer (0);
            int startSmp = static_cast<int>(p.smpStart * static_cast<float>(totalSmp));
            int endSmp   = static_cast<int>(p.smpEnd   * static_cast<float>(totalSmp));
            if (startSmp >= endSmp) { startSmp = 0; endSmp = totalSmp; }
            bool willStretch = (p.smpWarp > 0);
            if (!willStretch)
            {
                startSmp = smpSnapToZero (snapSrc, totalSmp, startSmp);
                endSmp   = smpSnapToZero (snapSrc, totalSmp, endSmp);
            }
            if (startSmp >= endSmp) endSmp = totalSmp;
            smpRegionStart = startSmp;
            smpRegionEnd   = endSmp;
            smpEndPos = static_cast<double>(endSmp);

            // Set initial position (reverse starts from end)
            if (smpPlayRate >= 0)
                smpReadPos = static_cast<double>(smpRegionStart);
            else
                smpReadPos = static_cast<double>(smpRegionEnd - 1);

            // TimeStretch: 0=OFF, 1=BEATS, 2=TEXTURE, 3=REPITCH
            // ═══ MATCH SamplerVoice logic exactly ═══
            float stretchFactor = p.smpStretch;
            float projectSR = static_cast<float>(1.0 / dt);

            if (p.smpWarp > 0 && currentBPM > 20.0f)
            {
                if (p.smpBars > 0 && p.smpBars <= 8)
                {
                    // BAR-BASED SYNC: stretch = targetDur * projectSR / N
                    static const float barLUT[] = { 0, 0.25f, 0.5f, 1, 2, 4, 8, 16, 32 };
                    float bars = barLUT[std::clamp (p.smpBars, 1, 8)];
                    // Use project BPM when synced, sample BPM knob when INTERNAL
                    float refBPM = (p.smpBpmSync == 1) ? currentBPM
                                 : (p.smpBPM > 20.0f ? p.smpBPM : currentBPM);
                    float targetDur = bars * 4.0f * 60.0f / refBPM;
                    int regionLen = smpRegionEnd - smpRegionStart;
                    if (regionLen > 64)
                        stretchFactor = targetDur * projectSR / static_cast<float>(regionLen);
                    // STRC knob acts as multiplier on top of bar-based stretch
                    stretchFactor *= p.smpStretch;
                }
                else if (p.smpBpmSync == 1 && p.smpBPM > 20.0f)
                {
                    stretchFactor = p.smpBPM / currentBPM;
                    if (p.smpFileSR > 100.0f && projectSR > 100.0f
                        && std::abs (p.smpFileSR - projectSR) > 10.0f)
                        stretchFactor *= projectSR / p.smpFileSR;
                }
            }
            // RATE multiplier always applied AFTER stretch calculation
            if (p.smpSyncMul != 0)
                stretchFactor /= std::pow (2.0f, static_cast<float>(p.smpSyncMul));

            smpUseStretch = (p.smpWarp > 0 && stretchFactor > 0.01f);
            smpDawSyncLoop = false;
            smpExpectedOutputLen = 0;
            smpStretchOutputCount = 0;
            if (smpUseStretch)
            {
                smpTimeStretch.prepare (projectSR);
                smpTimeStretch.setMode (p.smpWarp); // 1=BEATS, 2=TEXTURE, 3=REPITCH, 4=BEATS2
                const float* mono = smpBuf->getReadPointer (0);
                smpTimeStretch.setBuffer (mono, totalSmp);
                // All modes get pitchRatio — REPITCH combines it with stretch
                float wsolaPitch = static_cast<float>(std::abs (smpPlayRate));
                smpTimeStretch.setParams (stretchFactor, wsolaPitch);
                smpTimeStretch.setRegion (smpRegionStart, smpRegionEnd, p.smpLoop > 0);
                smpTimeStretch.setReversed (p.smpReverse > 0);

                // ── Stereo R channel stretch ──
                smpStereoStretch = (smpBuf->getNumChannels() > 1);
                if (smpStereoStretch)
                {
                    smpTimeStretchR.prepare (projectSR);
                    smpTimeStretchR.setMode (p.smpWarp);
                    smpTimeStretchR.setBuffer (smpBuf->getReadPointer (1), totalSmp);
                    smpTimeStretchR.setParams (stretchFactor, wsolaPitch);
                    smpTimeStretchR.setRegion (smpRegionStart, smpRegionEnd, p.smpLoop > 0);
                    smpTimeStretchR.setReversed (p.smpReverse > 0);
                }

                // ── Warp markers for per-segment variable stretch ──
                if (!p.warpMarkers.empty() && p.smpBars > 0)
                {
                    static const float barLUTw[] = { 0, 0.25f, 0.5f, 1, 2, 4, 8, 16, 32 };
                    float totalBeats = barLUTw[std::clamp (p.smpBars, 1, 8)] * 4.0f;
                    std::vector<TimeStretch::WarpPt> pts;
                    pts.reserve (p.warpMarkers.size());
                    for (const auto& wm : p.warpMarkers)
                        pts.push_back ({wm.samplePos, wm.beatPos});
                    smpTimeStretch.setWarpMarkers (pts, totalBeats);
                    if (smpStereoStretch) smpTimeStretchR.setWarpMarkers (pts, totalBeats);
                }
                else
                {
                    smpTimeStretch.clearWarpMarkers();
                    if (smpStereoStretch) smpTimeStretchR.clearWarpMarkers();
                }

                if (p.smpLoop > 0)
                {
                    smpDawSyncLoop = true;
                    if (p.smpBars > 0 && p.smpBars <= 8 && currentBPM > 20.0f)
                    {
                        // BARS: compute from bar duration for maximum precision
                        static const float barLUT2[] = { 0, 0.25f, 0.5f, 1, 2, 4, 8, 16, 32 };
                        float bars = barLUT2[p.smpBars];
                        double targetDurSec = static_cast<double>(bars) * 4.0 * 60.0 / static_cast<double>(currentBPM);
                        smpExpectedOutputLen = static_cast<int>(std::round (targetDurSec * static_cast<double>(projectSR)));
                    }
                    else
                    {
                        // BPM: output samples = input samples × stretchFactor
                        int regionLen = smpRegionEnd - smpRegionStart;
                        smpExpectedOutputLen = static_cast<int>(std::round (
                            static_cast<double>(regionLen) * static_cast<double>(stretchFactor)));
                    }
                }
            }
        }
        else
        {
            smpPlaying = false;
        }
    }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        if (!playing) return;

        // ═══ SAMPLE ENGINE MODE ═══
        if (p.drumEngine == 2 && smpPlaying && smpBuf != nullptr)
        {
            const float* smpDataL = smpBuf->getReadPointer (0);
            const float* smpDataR = (smpBuf->getNumChannels() > 1) ? smpBuf->getReadPointer (1) : smpDataL;
            int smpLen = smpBuf->getNumSamples();

            for (int i = 0; i < numSamples; ++i)
            {
                float sL = 0.0f, sR = 0.0f;
                float envDt = static_cast<float>(dt);

                // Filter AD envelope (reuse drumFiltA/D/Env — same knobs as synth engine)
                {
                    float fA = std::max (0.0001f, p.drumFiltA);
                    float fD = std::max (0.01f, p.drumFiltD);
                    double tSmp = static_cast<double>(i) * dt + t; // approximate time
                    if (t < fA)
                        filtEnvVal = std::clamp (static_cast<float>(t / fA), 0.0f, 1.0f);
                    else
                        filtEnvVal = std::exp (-(static_cast<float>(t) - fA) / fD);
                }

                if (smpUseStretch)
                {
                    // ── FM modulator with envelope (stretch path) ──
                    float fmMod = 0.0f;
                    if (p.smpFmAmt > 0.001f)
                    {
                        float fmEnv = smpRunFmEnv (envDt, p.smpFmEnvA, p.smpFmEnvD, p.smpFmEnvS);
                        float fmOsc = std::sin (static_cast<float>(smpFmPhase * 6.283185307));
                        fmMod = fmOsc * p.smpFmAmt * fmEnv * 4.0f;
                        double baseHz = 440.0 * std::pow (2.0, (p.smpRootNote - 69 + p.smpTune + p.smpFine) / 12.0);
                        smpFmPhase += baseHz * static_cast<double>(p.smpFmRatio) * dt;
                        if (smpFmPhase > 1.0) smpFmPhase -= std::floor (smpFmPhase);
                    }

                    smpTimeStretch.setPitchMod (fmMod);
                    if (smpStereoStretch) smpTimeStretchR.setPitchMod (fmMod);
                    sL = smpTimeStretch.getNextSample();
                    sR = smpStereoStretch ? smpTimeStretchR.getNextSample() : sL;
                    smpStretchOutputCount++;

                    // DAW sync: force-loop at exact bar boundary
                    if (smpDawSyncLoop && smpExpectedOutputLen > 0
                        && smpStretchOutputCount >= smpExpectedOutputLen)
                    {
                        smpTimeStretch.forceRestart();
                        if (smpStereoStretch) smpTimeStretchR.forceRestart();
                        smpStretchOutputCount = 0;
                    }
                    else if (smpTimeStretch.isFinished() && !p.smpLoop)
                    {
                        smpEndFade = 64; // anti-click: fade to zero over 64 samples
                    }
                }
                else
                {
                    // ── Boundary check with reverse support ──
                    if (smpPlayRate >= 0)
                    {
                        if (smpReadPos >= static_cast<double>(smpRegionEnd))
                        {
                            if (p.smpLoop)
                                smpReadPos = static_cast<double>(smpRegionStart) + std::fmod (smpReadPos - smpRegionEnd, static_cast<double>(smpRegionEnd - smpRegionStart));
                            else
                                { smpReadPos = static_cast<double>(smpRegionEnd - 1); smpEndFade = 64; }
                        }
                    }
                    else
                    {
                        if (smpReadPos < static_cast<double>(smpRegionStart))
                        {
                            if (p.smpLoop)
                                smpReadPos = static_cast<double>(smpRegionEnd - 1) - std::fmod (static_cast<double>(smpRegionStart) - smpReadPos, static_cast<double>(smpRegionEnd - smpRegionStart));
                            else
                                { smpReadPos = static_cast<double>(smpRegionStart); smpEndFade = 64; }
                        }
                    }

                    // ── TRUE LINEAR FM: modulates READ POSITION, not speed ──
                    double fmDisp = 0.0;
                    if (p.smpFmAmt > 0.001f)
                    {
                        float fmEnv = smpRunFmEnv (envDt, p.smpFmEnvA, p.smpFmEnvD, p.smpFmEnvS);
                        float fmOsc = std::sin (static_cast<float>(smpFmPhase * 6.283185307));
                        double baseHz = 440.0 * std::pow (2.0, (p.smpRootNote - 69 + p.smpTune + p.smpFine) / 12.0);
                        float modIndex = p.smpFmAmt * fmEnv * 8.0f;
                        float dispScale = static_cast<float>(1.0 / (dt * std::max (20.0, baseHz)));
                        fmDisp = static_cast<double>(fmOsc * modIndex * dispScale);
                        smpFmPhase += baseHz * static_cast<double>(p.smpFmRatio) * dt;
                        if (smpFmPhase > 1.0) smpFmPhase -= std::floor (smpFmPhase);
                    }

                    // Read at FM-displaced position — STEREO
                    double readPos = std::clamp (smpReadPos + fmDisp,
                        static_cast<double>(smpRegionStart), static_cast<double>(smpRegionEnd - 1));
                    sL = smpCubicInterp (smpDataL, smpLen, readPos);
                    sR = smpCubicInterp (smpDataR, smpLen, readPos);

                    // Position advances at constant speed (FM doesn't affect speed)
                    smpReadPos += smpPlayRate;
                }

                // ── Gate mode: trigger release when gate closes ──
                smpGateTimer += dt;
                if (smpGateDurSec > 0.0f && smpGateTimer >= smpGateDurSec && smpAmpStage < 3)
                {
                    smpAmpStage = 3; // release
                    if (smpFiltEnvStage < 3) smpFiltEnvStage = 3; // filter release too
                }

                // ── AMP ADSR envelope ──
                switch (smpAmpStage)
                {
                    case 0: // Attack
                        smpAmpEnv += envDt / std::max (0.0005f, p.smpA);
                        if (smpAmpEnv >= 1.0f) { smpAmpEnv = 1.0f; smpAmpStage = 1; }
                        break;
                    case 1: // Decay
                        smpAmpEnv -= (smpAmpEnv - p.smpS) * (1.0f - std::exp (-envDt / std::max (0.005f, p.smpD)));
                        if (smpAmpEnv <= p.smpS + 0.001f) { smpAmpEnv = p.smpS; smpAmpStage = 2; }
                        break;
                    case 2: // Sustain
                        smpAmpEnv = p.smpS;
                        break;
                    case 3: // Release
                        smpAmpEnv *= std::exp (-envDt / std::max (0.005f, p.smpR));
                        if (smpAmpEnv < 0.001f) { smpAmpEnv = 0; smpPlaying = false; playing = false; return; }
                        break;
                }

                // ── FILTER ADSR envelope ──
                switch (smpFiltEnvStage)
                {
                    case 0:
                        smpFiltEnvVal += envDt / std::max (0.0005f, p.smpFiltA);
                        if (smpFiltEnvVal >= 1.0f) { smpFiltEnvVal = 1.0f; smpFiltEnvStage = 1; }
                        break;
                    case 1:
                        smpFiltEnvVal -= (smpFiltEnvVal - p.smpFiltS) * (1.0f - std::exp (-envDt / std::max (0.005f, p.smpFiltD)));
                        if (smpFiltEnvVal <= p.smpFiltS + 0.001f) { smpFiltEnvVal = p.smpFiltS; smpFiltEnvStage = 2; }
                        break;
                    case 2:
                        smpFiltEnvVal = p.smpFiltS;
                        break;
                    case 3:
                        smpFiltEnvVal *= std::exp (-envDt / std::max (0.005f, p.smpFiltR));
                        if (smpFiltEnvVal < 0.001f) smpFiltEnvVal = 0.0f;
                        break;
                }

                float gain = p.smpGain * vel * smpAmpEnv;

                // ── Anti-click fadeout when sample ends ──
                if (smpEndFade > 0)
                {
                    gain *= static_cast<float>(smpEndFade) / 64.0f;
                    --smpEndFade;
                    if (smpEndFade == 0) { smpPlaying = false; playing = false; return; }
                }

                // ── Multi-model filter (CLN/ACD/DRT/SEM/ARP/LQD) + filter envelope ──
                float filtEnvMod = p.smpFiltEnv * smpFiltEnvVal;
                float effCut = std::min (100.0f, p.smpCut + filtEnvMod);
                if (effCut < 99.0f)
                {
                    float cutHz = 20.0f * std::pow (1000.0f, effCut / 100.0f);
                    cutHz = std::clamp (cutHz, 16.0f, std::min (18000.0f, static_cast<float>(sampleRate) * 0.45f));
                    sL = smpFilter.process (sL, cutHz, p.smpRes, p.smpFModel, p.smpFType, p.smpFPoles, static_cast<float>(sampleRate));
                    sR = smpFilterR.process (sR, cutHz, p.smpRes, p.smpFModel, p.smpFType, p.smpFPoles, static_cast<float>(sampleRate));
                }

                outL[i] += sL * gain;
                outR[i] += sR * gain;
            }
            return;
        }

        // If sampler mode but no sample loaded → silence (don't fall through to analog!)
        if (p.drumEngine == 2) return;

        // ═══ ER-1 ENGINE (Korg Electribe style) ═══
        if (p.drumEngine == 3)
        {
            float sr = static_cast<float>(sampleRate);
            float tuneRatio = std::pow (2.0f, p.tune / 12.0f);

            // Decay coefficients
            float pDecCoeff1 = std::exp (-1.0f / std::max (0.001f, p.er1PDec1 * sr));
            float pDecCoeff2 = std::exp (-1.0f / std::max (0.001f, p.er1PDec2 * sr));
            float aDecCoeff  = std::exp (-1.0f / std::max (0.001f, p.er1Decay * sr));
            float nDecCoeff  = std::exp (-1.0f / std::max (0.001f, p.er1NDec * sr));

            // SVF filter coefficients
            float cutHz = std::clamp (p.er1Cut, 20.0f, sr * 0.48f);
            float g = std::tan (3.14159265f * cutHz / sr);
            float k = 2.0f - 2.0f * std::clamp (p.er1Res, 0.0f, 0.98f);
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;
            float a3 = g * a2;

            for (int i = 0; i < numSamples; ++i)
            {
                // ── Pitch envelopes (exponential decay, 1→0) ──
                float pitchMod1 = p.er1Pitch1 + (p.er1Pitch1 * 4.0f) * er1PEnv1;
                float pitchMod2 = p.er1Pitch2 + (p.er1Pitch2 * 4.0f) * er1PEnv2;
                pitchMod1 *= tuneRatio;
                pitchMod2 *= tuneRatio;
                er1PEnv1 *= pDecCoeff1;
                er1PEnv2 *= pDecCoeff2;

                // ── OSC 2 (renders first — needed for cross-mod) ──
                float inc2 = static_cast<float>(pitchMod2 * dt);
                float osc2 = er1PolyBLEP (er1Phase2, inc2, p.er1Wave2);
                er1Phase2 += inc2;
                if (er1Phase2 >= 1.0) er1Phase2 -= 1.0;

                // ── OSC 1 (with optional cross-mod from OSC 2) ──
                float xmod = osc2 * p.er1XMod * static_cast<float>(dt);
                float inc1 = static_cast<float>(pitchMod1 * dt) + xmod;
                inc1 = std::clamp (inc1, 0.0f, 0.49f); // anti-alias safety
                float osc1 = er1PolyBLEP (er1Phase1, inc1, p.er1Wave1);
                er1Phase1 += inc1;
                if (er1Phase1 >= 1.0) er1Phase1 -= std::floor (er1Phase1);
                if (er1Phase1 < 0.0) er1Phase1 += 1.0;

                // ── Ring modulation ──
                float ringMod = osc1 * osc2;
                float oscMix = osc1 * (1.0f - p.er1Ring) + ringMod * p.er1Ring;

                // ── Noise ──
                er1NoiseRng ^= er1NoiseRng << 13; er1NoiseRng ^= er1NoiseRng >> 17; er1NoiseRng ^= er1NoiseRng << 5;
                float noise = (static_cast<float>(er1NoiseRng & 0xFFFF) / 32768.0f - 1.0f);
                float noiseSig = noise * p.er1Noise * er1NEnv;
                er1NEnv *= nDecCoeff;

                // ── Mix osc + noise ──
                float sig = oscMix + noiseSig;

                // ── SVF lowpass filter (Chamberlin topology) ──
                float v3 = sig - er1FiltLP;
                float v1 = a1 * er1FiltBP + a2 * v3;
                float v2 = er1FiltLP + a2 * er1FiltBP + a3 * v3;
                er1FiltBP = v1; er1FiltLP = v2;
                sig = v2; // LP output

                // ── Drive (soft tanh saturation) ──
                if (p.er1Drive > 0.01f)
                {
                    float drv = 1.0f + p.er1Drive * 8.0f;
                    sig = std::tanh (sig * drv) / drv;
                    sig *= 1.0f + p.er1Drive * 2.0f; // makeup gain
                }

                // ── Amp envelope (exponential decay) ──
                sig *= er1AEnv * vel;
                er1AEnv *= aDecCoeff;
                ampEnv = er1AEnv; // for voice lifetime tracking

                // ── Soft clip ──
                sig = softClip (sig);

                if (killFade > 0.0f)
                {
                    sig *= killFade;
                    killFade -= static_cast<float>(dt / 0.0005);
                    if (killFade <= 0.0f) { playing = false; break; }
                }

                outL[i] += sig; // volume applied post-FX in DrumSynth
                outR[i] += sig;
                t += dt;

                if ((er1AEnv < 0.0005f && t > 0.01) || t > 8.0)
                { playing = false; break; }
            }
            return;
        }

        // If ER-1 mode but somehow didn't enter block → silence
        if (p.drumEngine == 3) return;

        // ═══ SYNTH ENGINE (ANA/FM) ═══
        float fmSmoothCoeff = 1.0f - std::exp (-1.0f / (static_cast<float>(sampleRate) * 0.008f)); // ~8ms
        float filtSmoothCoeff = 1.0f - std::exp (-1.0f / (static_cast<float>(sampleRate) * 0.005f)); // 5ms

        for (int i = 0; i < numSamples; ++i)
        {
            // Per-sample filter smoothing (prevents SVF ringing at block boundaries)
            // NOTE: does NOT modify p.xxx — uses separate smooth members
            smoothDrumCut += (p.drumCut - smoothDrumCut) * filtSmoothCoeff;
            smoothDrumRes += (p.drumRes - smoothDrumRes) * filtSmoothCoeff;
            // ── Filter AD envelope (shared by ALL engines) ──
            {
                float fA = std::max (0.0001f, p.drumFiltA);
                float fD = std::max (0.01f, p.drumFiltD);
                if (static_cast<float>(t) < fA)
                    filtEnvVal = std::clamp (static_cast<float>(t / fA), 0.0f, 1.0f);
                else
                    filtEnvVal = std::exp (-(static_cast<float>(t) - fA) / fD);
            }

            float sample = 0.0f;
            float targetFmMix = (p.drumEngine == 2) ? 0.0f : p.fmMix; // force 0 in sampler mode

            // Per-sample smoothing of fmMix — prevents brusco transitions
            smoothFmMix += (targetFmMix - smoothFmMix) * fmSmoothCoeff;
            float fmMix = smoothFmMix;

            if (fmMix < 0.005f)
            {
                sample = renderAnalogDispatch();
            }
            else if (fmMix > 0.995f)
            {
                // Pure FM
                sample = renderFM();
            }
            else
            {
                // Layer: render both, equal-power crossfade
                float analogSample = renderAnalogDispatch();
                float anaEnv = ampEnv;
                float fmSample = renderFM();
                float fmEnvVal = ampEnv;
                ampEnv = std::max (anaEnv, fmEnvVal);
                float angle = fmMix * 1.5707963f;
                sample = analogSample * std::cos (angle) + fmSample * std::sin (angle);
            }

            // Soft clip to prevent harsh digital clipping
            sample = softClip (sample);

            // Apply kill fade if voice is being stolen
            if (killFade > 0.0f)
            {
                sample *= killFade;
                killFade -= static_cast<float>(dt / 0.0005); // 0.5ms fadeout
                if (killFade <= 0.0f) { playing = false; break; }
            }

            // Output at unity (volume applied post-FX in DrumSynth)
            outL[i] += sample;
            outR[i] += sample;
            t += dt;

            if ((ampEnv < 0.0005f && t > 0.01) || t > 8.0)
            {
                playing = false;
                break;
            }
        }
    }

    bool isPlaying() const { return playing; }

    // Rapid fadeout to prevent clicks on voice stealing
    void kill()
    {
        if (playing) killFade = 1.0f;  // will fade to 0 over ~0.5ms
    }

    // Immediate stop — used when retriggering the same drum
    void hardKill()
    {
        playing = false;
        killFade = 0.0f;
        ampEnv = 0.0f;
    }

    // Normalized sampler playback position (0-1) for GUI playhead
    float getSmpPlayPosition() const
    {
        if (!smpPlaying || smpBuf == nullptr) return -1.0f;
        return static_cast<float>(smpReadPos) / static_cast<float>(std::max (1, smpBuf->getNumSamples()));
    }

private:
    double sampleRate = 44100.0;
    double dt = 1.0 / 44100.0;
    DrumType drumType = DrumType::Kick;
    float vel = 0.0f;
    float gate = 1.0f;
    bool playing = false;
    bool hasPlocks = false;  // when true, skip live param updates (Elektron-style p-lock hold)
    double t = 0.0;

    double phase1 = 0.0, phase2 = 0.0, phase3 = 0.0;
    double fmPhase1 = 0.0, fmPhase2 = 0.0, fmPhase3 = 0.0; // independent FM oscillator phases
    double metalPhases[6] = {}; // independent phases for metallic partials
    float ampEnv = 0.0f;
    float smoothFmMix = 0.0f;
    float smoothDrumCut = 50.0f;
    float smoothDrumRes = 0.0f;
    // Stateful IIR envelopes — immune to parameter-change clicks
    float envA = 1.0f, envB = 1.0f, envC = 1.0f, envD = 1.0f;
    float fadeIn = 0.0f;
    float killFade = 0.0f;  // >0 means voice is being killed
    float filtEnvVal = 0.0f; // filter AD envelope value 0-1

    // ER-1 engine state
    double er1Phase1 = 0.0, er1Phase2 = 0.0;
    float er1PEnv1 = 1.0f, er1PEnv2 = 1.0f;  // pitch envelope (1→0)
    float er1AEnv = 1.0f;                       // amp envelope
    float er1NEnv = 1.0f;                       // noise envelope
    float er1FiltLP = 0.0f, er1FiltBP = 0.0f;  // SVF filter state
    uint32_t er1NoiseRng = 0x7F3A5C19u;         // noise LFSR

    DrumTrackState p; // cached params

public:
    // Update real-time params from live track state (called every block while playing)
    void updateLiveParams (const DrumTrackState& live)
    {
        // P-locked voices keep their trigger-time params (Elektron behavior)
        if (hasPlocks) return;

        // ═══ ALL engine params ═══
        auto sm = [](float& cur, float tgt) { cur += (tgt - cur) * 0.5f; };
        // Pitch → direct (instant response for percussion)
        p.pitch     = live.pitch;
        // Decay/tone/click → smoothed (prevents expDecay discontinuities)
        sm (p.decay, live.decay);
        sm (p.tone, live.tone);
        sm (p.click, live.click);
        p.pitchDec  = live.pitchDec;
        p.pitchEnd  = live.pitchEnd;
        // Continuous mix/FX params → smoothed
        sm (p.volume, live.volume);
        sm (p.pan, live.pan);
        sm (p.snap, live.snap);
        // Envelope-controlling params → DIRECT (no smoothing)
        p.tune      = live.tune;
        p.toneDecay = live.toneDecay;
        p.noiseDecay = live.noiseDecay;
        sm (p.cutoff, live.cutoff);
        sm (p.freq, live.freq);
        sm (p.freq1, live.freq1);
        sm (p.freq2, live.freq2);
        sm (p.spread, live.spread);
        sm (p.noise, live.noise);

        // ═══ FM drum engine — decay/envelope params direct, mix smoothed ═══
        sm (p.fmMix, live.fmMix);
        sm (p.fmRatio, live.fmRatio);
        sm (p.fmDepth, live.fmDepth);
        p.fmDecay   = live.fmDecay;
        sm (p.fmNoise, live.fmNoise);
        p.fmNoiseType = live.fmNoiseType;

        // ═══ ER-1 engine — decay/envelope params direct ═══
        sm (p.er1Pitch1, live.er1Pitch1);
        sm (p.er1Pitch2, live.er1Pitch2);
        p.er1PDec1   = live.er1PDec1;
        p.er1PDec2   = live.er1PDec2;
        sm (p.er1Ring, live.er1Ring);
        sm (p.er1XMod, live.er1XMod);
        sm (p.er1Noise, live.er1Noise);
        p.er1NDec    = live.er1NDec;
        sm (p.er1Cut, live.er1Cut);
        sm (p.er1Res, live.er1Res);
        p.er1Decay   = live.er1Decay;
        sm (p.er1Drive, live.er1Drive);

        // ═══ Sampler engine ═══
        sm (p.smpCut, live.smpCut);
        sm (p.smpRes, live.smpRes);
        p.smpFType   = live.smpFType;
        p.smpFModel  = live.smpFModel;
        p.smpFPoles  = live.smpFPoles;
        sm (p.smpFiltEnv, live.smpFiltEnv);
        // ADSR/envelope timing → direct (no smoothing)
        p.smpFiltA   = live.smpFiltA;
        p.smpFiltD   = live.smpFiltD;
        p.smpFiltS   = live.smpFiltS;
        p.smpFiltR   = live.smpFiltR;
        sm (p.smpGain, live.smpGain);
        sm (p.smpTune, live.smpTune);
        sm (p.smpFine, live.smpFine);
        sm (p.smpStart, live.smpStart);
        sm (p.smpEnd, live.smpEnd);
        sm (p.smpFmAmt, live.smpFmAmt);
        sm (p.smpFmRatio, live.smpFmRatio);
        p.smpFmEnvA  = live.smpFmEnvA;
        p.smpFmEnvD  = live.smpFmEnvD;
        p.smpFmEnvS  = live.smpFmEnvS;

        // ═══ Drum filter ═══
        sm (p.drumCut, live.drumCut);
        sm (p.drumRes, live.drumRes);
        sm (p.drumFiltEnv, live.drumFiltEnv);
        // Filter envelope timing → direct
        p.drumFiltA  = live.drumFiltA;
        p.drumFiltD  = live.drumFiltD;

        // ═══ FX ═══
        sm (p.distAmt, live.distAmt);
        sm (p.reduxBits, live.reduxBits);
        sm (p.reduxRate, live.reduxRate);
        sm (p.chorusMix, live.chorusMix);
        sm (p.chorusRate, live.chorusRate);
        sm (p.chorusDepth, live.chorusDepth);
        sm (p.delayMix, live.delayMix);
        sm (p.delayTime, live.delayTime);
        sm (p.delayFB, live.delayFB);
        sm (p.delayBeats, live.delayBeats);
        sm (p.delayDamp, live.delayDamp);
        p.delaySync  = live.delaySync;  // discrete
        p.delayPP    = live.delayPP;    // discrete
        p.delayAlgo  = live.delayAlgo;  // discrete
        sm (p.reverbMix, live.reverbMix);
        sm (p.reverbSize, live.reverbSize);
        sm (p.reverbDamp, live.reverbDamp);
        p.reverbAlgo = live.reverbAlgo; // discrete
        sm (p.fxLP, live.fxLP);
        sm (p.fxHP, live.fxHP);
        sm (p.eqLow, live.eqLow);
        sm (p.eqMid, live.eqMid);
        sm (p.eqHigh, live.eqHigh);

        // ═══ Live warp marker update ═══
        if (smpUseStretch && p.smpWarp > 0 && p.smpBars > 0)
        {
            bool changed = (live.warpMarkers.size() != p.warpMarkers.size());
            if (!changed && !live.warpMarkers.empty() && !p.warpMarkers.empty())
                changed = (std::abs (live.warpMarkers[1 < live.warpMarkers.size() ? 1 : 0].samplePos
                           - p.warpMarkers[1 < p.warpMarkers.size() ? 1 : 0].samplePos) > 0.0001f);
            if (changed)
            {
                p.warpMarkers = live.warpMarkers;
                if (!p.warpMarkers.empty())
                {
                    static const float barLUTw[] = { 0, 0.25f, 0.5f, 1, 2, 4, 8, 16, 32 };
                    float totalBeats = barLUTw[std::clamp (p.smpBars, 1, 8)] * 4.0f;
                    std::vector<TimeStretch::WarpPt> pts;
                    pts.reserve (p.warpMarkers.size());
                    for (const auto& wm : p.warpMarkers)
                        pts.push_back ({wm.samplePos, wm.beatPos});
                    smpTimeStretch.setWarpMarkers (pts, totalBeats);
                    if (smpStereoStretch) smpTimeStretchR.setWarpMarkers (pts, totalBeats);
                }
                else
                {
                    smpTimeStretch.clearWarpMarkers();
                    if (smpStereoStretch) smpTimeStretchR.clearWarpMarkers();
                }
            }
        }
    }

    // Sample playback state
    std::shared_ptr<juce::AudioBuffer<float>> smpBuf;
    double smpReadPos = 0.0;
    double smpEndPos = 0.0;
    double smpPlayRate = 1.0;
    bool smpPlaying = false;
    int  smpEndFade = 0;    // anti-click: 64-sample fadeout when sample ends
    float smpAmpEnv = 0.0f;
    int smpAmpStage = 0;
    float smpFiltEnvVal = 0.0f;
    int smpFiltEnvStage = 0;
    // Gate mode tracking for sampler
    float smpGateDurSec = 0.0f;  // gate duration in seconds (0 = one-shot)
    double smpGateTimer = 0.0;   // elapsed time since trigger
    MultiModelFilterCh smpFilter;   // L channel filter
    MultiModelFilterCh smpFilterR;  // R channel filter (stereo samples)
    // FM modulator for sampler
    double smpFmPhase = 0.0;
    float  smpFmEnvVal = 0.0f;
    int    smpFmEnvStage = 0;
    TimeStretch smpTimeStretch;
    TimeStretch smpTimeStretchR;  // R channel for stereo samples
    bool smpStereoStretch = false;
    bool smpUseStretch = false;
    // Region bounds (for reverse + loop)
    int smpRegionStart = 0;
    int smpRegionEnd = 0;
    // DAW sync bar-boundary tracking
    bool smpDawSyncLoop = false;
    int  smpExpectedOutputLen = 0;
    int  smpStretchOutputCount = 0;

    // Noise
    std::vector<float> noiseBuffer;
    int noiseLen = 0;
    int noiseIdx = 0;

    // ─── SVF 2-pole filter (12dB/oct, TPT) ──────────────────────
    struct SVF {
        double ic1eq = 0.0, ic2eq = 0.0;
        void reset() { ic1eq = ic2eq = 0.0; }

        // Returns: low, band, high
        struct Result { float low, band, high; };

        Result process (float input, float cutoffHz, float Q, double sr)
        {
            double g = std::tan (juce::MathConstants<double>::pi * std::min ((double)cutoffHz, sr * 0.49) / sr);
            double k = 1.0 / std::max (0.5, (double)Q);
            double a1 = 1.0 / (1.0 + g * (g + k));
            double a2 = g * a1;
            double a3 = g * a2;

            // Soft-clip state to prevent resonance from exploding
            ic1eq = std::tanh (ic1eq * 0.5) * 2.0;
            ic2eq = std::tanh (ic2eq * 0.5) * 2.0;

            double v3 = input - ic2eq;
            double v1 = a1 * ic1eq + a2 * v3;
            double v2 = ic2eq + a2 * ic1eq + a3 * v3;
            ic1eq = 2.0 * v1 - ic1eq;
            ic2eq = 2.0 * v2 - ic2eq;

            return { static_cast<float>(v2), static_cast<float>(v1),
                     static_cast<float>(input - k * v1 - v2) };
        }

        float lowpass (float input, float cutoffHz, float Q, double sr)
        { return process (input, cutoffHz, Q, sr).low; }

        float highpass (float input, float cutoffHz, float Q, double sr)
        { return process (input, cutoffHz, Q, sr).high; }

        float bandpass (float input, float cutoffHz, float Q, double sr)
        { return process (input, cutoffHz, Q, sr).band; }
    };

    SVF svf1, svf2, svf3, svf4, svf5;  // svf5 = kick/tom analog filter with envelope

    // ─── Helpers ─────────────────────────────────────────────────
    float noise()
    {
        if (noiseBuffer.empty()) return 0.0f;
        return noiseBuffer[static_cast<size_t>(noiseIdx++ % noiseLen)];
    }

    // PolyBLEP for anti-aliased oscillators
    static double polyblep (double phase, double phaseInc)
    {
        double dt2 = phaseInc;
        if (dt2 <= 0.0) return 0.0;
        if (phase < dt2) { double t2 = phase / dt2; return t2 + t2 - t2 * t2 - 1.0; }
        if (phase > 1.0 - dt2) { double t2 = (phase - 1.0) / dt2; return t2 * t2 + t2 + t2 + 1.0; }
        return 0.0;
    }

    float sine (double& ph, double freq)
    {
        ph += freq * dt;
        ph -= std::floor (ph);
        return static_cast<float>(std::sin (ph * 2.0 * juce::MathConstants<double>::pi));
    }

    float triangle (double& ph, double freq)
    {
        double inc = freq * dt;
        ph += inc;
        ph -= std::floor (ph);
        // Naive triangle + polyBLEP correction
        float raw = static_cast<float>(4.0 * std::abs (ph - 0.5) - 1.0);
        return raw;
    }

    float square (double& ph, double freq)
    {
        double inc = freq * dt;
        ph += inc;
        ph -= std::floor (ph);
        float raw = ph < 0.5 ? 1.0f : -1.0f;
        raw += static_cast<float>(polyblep (ph, inc));
        raw -= static_cast<float>(polyblep (std::fmod (ph + 0.5, 1.0), inc));
        return raw;
    }

    static float expDecay (double time, float decayTime)
    {
        return std::exp (static_cast<float>(-time / std::max (0.001, (double)decayTime)));
    }

    // Stateful IIR decay: changes RATE not LEVEL when decayTime changes → no clicks
    static float iirDecay (float& state, float decayTime, double dtSec)
    {
        state *= std::exp (static_cast<float>(-dtSec / std::max (0.001, static_cast<double>(decayTime))));
        return state;
    }

    static float smpCubicInterp (const float* data, int len, double pos)
    {
        int i1 = static_cast<int>(pos);
        float f = static_cast<float>(pos - i1);
        int i0 = std::max (0, i1 - 1);
        i1 = std::clamp (i1, 0, len - 1);
        int i2 = std::min (i1 + 1, len - 1);
        int i3 = std::min (i1 + 2, len - 1);
        float y0 = data[i0], y1 = data[i1], y2 = data[i2], y3 = data[i3];
        float a = y1;
        float b = 0.5f * (y2 - y0);
        float c = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float d = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return a + f * (b + f * (c + f * d));
    }

    // FM envelope for sampler (attack → decay → done) — identical to SamplerVoice
    float smpRunFmEnv (float envDt, float atk, float dcy, float sus = 0.0f)
    {
        switch (smpFmEnvStage)
        {
            case 0: // Attack
                smpFmEnvVal += envDt / std::max (0.0003f, atk);
                if (smpFmEnvVal >= 1.0f) { smpFmEnvVal = 1.0f; smpFmEnvStage = 1; }
                break;
            case 1: // Decay → Sustain
            {
                float target = std::clamp (sus, 0.0f, 1.0f);
                smpFmEnvVal = target + (smpFmEnvVal - target) * std::exp (-envDt / std::max (0.005f, dcy));
                if (smpFmEnvVal <= target + 0.001f) { smpFmEnvVal = target; smpFmEnvStage = 2; }
                break;
            }
            case 2: // Sustain hold
                smpFmEnvVal = std::clamp (sus, 0.0f, 1.0f);
                break;
            default: break;
        }
        return smpFmEnvVal;
    }

    // Snap-to-zero-crossing for click-free start/end points — identical to SamplerVoice
    static int smpSnapToZero (const float* data, int len, int pos)
    {
        int best = pos;
        float bestAbs = 999.0f;
        int range = 64;
        int lo = std::max (0, pos - range);
        int hi = std::min (len - 1, pos + range);
        for (int i = lo; i < hi; ++i)
        {
            if (i > 0 && data[i - 1] * data[i] <= 0.0f)
            {
                float a = std::abs (data[i - 1]);
                float b = std::abs (data[i]);
                int pick = (a < b) ? i - 1 : i;
                float val = std::min (a, b);
                if (val < bestAbs) { bestAbs = val; best = pick; }
            }
        }
        return best;
    }

    // ── ER-1 PolyBLEP oscillator: sine/tri/saw/square, anti-aliased ──
    static float er1PolyBLEP (double phase, float inc, int wave)
    {
        float p = static_cast<float>(phase);
        float dt = std::abs (inc);

        // PolyBLEP correction at discontinuities
        auto polyblep = [](float t, float dt) -> float {
            if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
            if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
            return 0.0f;
        };

        switch (wave)
        {
            case 0: // SINE
                return std::sin (p * 6.283185307f);

            case 1: // TRIANGLE (integrated square with PolyBLEP)
            {
                float sq = (p < 0.5f) ? 1.0f : -1.0f;
                sq += polyblep (p, dt);
                sq -= polyblep (static_cast<float>(std::fmod (p + 0.5, 1.0)), dt);
                // Leaky integrator for triangle from square
                // Approximate: direct formula
                float tri = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
                return tri;
            }

            case 2: // SAW (naive - PolyBLEP corrected)
            {
                float saw = 2.0f * p - 1.0f;
                saw -= polyblep (p, dt);
                return saw;
            }

            case 3: // SQUARE (PolyBLEP corrected)
            {
                float sq = (p < 0.5f) ? 1.0f : -1.0f;
                sq += polyblep (p, dt);
                sq -= polyblep (static_cast<float>(std::fmod (p + 0.5, 1.0)), dt);
                return sq * 0.9f; // slight attenuation to match other waveform levels
            }

            default: return std::sin (p * 6.283185307f);
        }
    }

    static float softClip (float x)
    {
        if (x > 1.0f) return 1.0f - 2.0f / (1.0f + std::exp (2.0f * x));
        if (x < -1.0f) return -1.0f + 2.0f / (1.0f + std::exp (-2.0f * x));
        return x;
    }

    // ═══ DISPATCH: routes to drum type render function ═══
    float renderAnalogDispatch()
    {
        switch (drumType)
        {
            case DrumType::Kick:        return renderKick();
            case DrumType::Snare:       return renderSnare();
            case DrumType::HiHatClosed: return renderHiHatClosed();
            case DrumType::HiHatOpen:   return renderHiHatOpen();
            case DrumType::Clap:        return renderClap();
            case DrumType::Tom:
            case DrumType::TomHi:       return renderTom();
            case DrumType::Cowbell:     return renderCowbell();
            case DrumType::Rimshot:     return renderRimshot();
            case DrumType::Crash:       return renderCrash();
            default: return 0.0f;
        }
    }


    // ═══════════════════════════════════════════════════════════════════
    // ACOUSTIC DRUM MODELS — Physics-based modal synthesis
    // Bessel function frequency ratios for circular membrane:
    // (0,1)=1.000 (1,1)=1.593 (2,1)=2.136 (0,2)=2.296 (3,1)=2.653 (1,2)=2.918
    // Each mode has independent decay (higher modes decay faster)
    // Shell resonance via SVF, beater/stick noise, tension modulation
    // Sources: U.Illinois Physics 406, Edinburgh NESS, McGill CCRMA
    // ═══════════════════════════════════════════════════════════════════

    float renderKickAcoustic()
    {
        // Acoustic kick: 22" drum, fundamental 50-80Hz, beater impact + membrane + shell
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float fund = std::max (30.0f, p.pitch * 0.4f * tuneR); // fundamental ~60Hz
        float tf = static_cast<float>(t);
        float decT = std::max (0.1f, p.decay * 2.0f) * gate;

        // Tension modulation: pitch glide on attack (nonlinear membrane behavior)
        float pitchGlide = 1.0f + 0.5f * std::exp (-tf / std::max (0.005f, p.pitchDec * 0.03f));

        // Modal synthesis: 5 Bessel modes with correct frequency ratios
        float f0 = fund * pitchGlide;
        float m0 = sine (phase1, f0);                          // (0,1) fundamental
        float m1 = sine (phase2, f0 * 1.593f) * 0.35f;         // (1,1) pitch-defining
        float m2 = sine (phase3, f0 * 2.136f) * 0.15f;         // (2,1) timbre
        float m3 = sine (fmPhase1, f0 * 2.296f) * 0.08f;       // (0,2) thump
        float m4 = sine (fmPhase2, f0 * 2.653f) * 0.04f;       // (3,1) ring

        // Each mode decays at different rate (higher = faster)
        float e0 = std::exp (-tf / decT);
        float e1 = std::exp (-tf / (decT * 0.6f));
        float e2 = std::exp (-tf / (decT * 0.35f));
        float e3 = std::exp (-tf / (decT * 0.25f));
        float e4 = std::exp (-tf / (decT * 0.15f));

        float membrane = m0 * e0 + m1 * e1 + m2 * e2 + m3 * e3 + m4 * e4;

        // Beater impact: broadband noise burst (3-5kHz, 2-5ms)
        float beater = 0.0f;
        if (tf < 0.005f) {
            noiseIdx = noiseIdx * 1664525u + 1013904223u;
            float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
            beater = svf3.bandpass (n, 4000.0f, 0.8f, sampleRate) * p.click * (1.0f - tf / 0.005f) * 4.0f;
        }

        // Shell resonance: SVF bandpass at 200-400Hz (wood shell warmth)
        float shellFreq = 250.0f + p.tone * 200.0f;
        float shell = svf4.bandpass (membrane, shellFreq, 1.5f, sampleRate) * 0.3f;

        float sample = (membrane + shell + beater) * vel;
        ampEnv = e0;
        return sample;
    }

    float renderSnareAcoustic()
    {
        // Acoustic snare: 14" drum, ~170-200Hz, dual membrane + snare wires
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float fund = std::max (100.0f, p.pitch * tuneR); // ~170-200Hz
        float tf = static_cast<float>(t);
        float decT = std::max (0.05f, (p.noiseDecay > 0.01f ? p.noiseDecay : p.decay * 0.8f)) * gate;

        // Tension modulation on attack
        float pitchGlide = 1.0f + 0.3f * std::exp (-tf / 0.003f);

        // Batter head modes (top membrane, struck)
        float f0 = fund * pitchGlide;
        float batter0 = sine (phase1, f0);                     // (0,1)
        float batter1 = sine (phase2, f0 * 1.593f) * 0.3f;     // (1,1)
        float batter2 = sine (phase3, f0 * 2.136f) * 0.12f;    // (2,1)

        // Snare-side membrane (coupled, slightly different tuning)
        float snareSide = sine (fmPhase1, f0 * 1.05f) * 0.15f; // slightly detuned

        float e0 = std::exp (-tf / (decT * 0.4f));
        float e1 = std::exp (-tf / (decT * 0.25f));
        float e2 = std::exp (-tf / (decT * 0.15f));

        // TONE: balance between membrane body and brightness
        float toneMix = std::clamp (p.tone / 500.0f, 0.0f, 1.0f);
        float membraneSum = batter0 * e0 * (1.0f - toneMix * 0.3f)
                           + batter1 * e1 * (0.5f + toneMix * 0.5f)
                           + batter2 * e2 * toneMix
                           + snareSide * e1 * 0.5f;

        // Snare wires: nonlinear interaction with membrane
        // Wire buzz = noise modulated by membrane amplitude (contact/no-contact)
        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
        float memAmp = std::abs (batter0 * e0);
        // Nonlinear contact: wires only buzz when membrane pushes them
        float wireContact = std::clamp (memAmp * 3.0f, 0.0f, 1.0f);
        float wireBuzz = n * wireContact;
        // Bandpass: snare wires resonate around 2-5kHz
        float wiresFilt = svf1.bandpass (wireBuzz, 3000.0f + p.tone * 2000.0f, 0.6f, sampleRate) * 2.0f;
        float wireEnv = std::exp (-tf / (decT * 0.7f));

        // Stick impact noise (very short, bright)
        float stick = 0.0f;
        if (tf < 0.003f) {
            stick = n * p.click * (1.0f - tf / 0.003f) * 2.0f;
        }

        // SNAP: snare wire amount (like SNAPPY knob)
        float snap = std::clamp (p.snap, 0.0f, 1.0f);
        float sample = (membraneSum * 0.5f + wiresFilt * wireEnv * snap + stick) * vel;
        ampEnv = std::max (e0, wireEnv * snap);
        return sample;
    }

    float renderHiHatClosedAcoustic()
    {
        // Acoustic closed hi-hat: bronze cymbal pair, damped, inharmonic modes
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.008f, std::min (p.decay * 0.15f, 0.12f)) * gate;

        // Inharmonic metal plate modes (NOT Bessel — cymbals are plates, not membranes)
        // Plate modes follow ~n² pattern, very inharmonic
        float baseF = 320.0f * tuneR;
        float s = sine (phase1, baseF * 2.01f) * 0.3f    // mode 1
                + sine (phase2, baseF * 3.73f) * 0.25f    // mode 2 (strongly inharmonic)
                + sine (phase3, baseF * 5.19f) * 0.18f     // mode 3
                + sine (fmPhase1, baseF * 7.41f) * 0.12f   // mode 4
                + sine (fmPhase2, baseF * 11.03f) * 0.08f; // mode 5 (high shimmer)

        // Ring modulation between modes (creates extra inharmonic content)
        float ringMod = sine (phase1, baseF * 2.01f) * sine (phase2, baseF * 3.73f) * 0.15f;

        // Broadband noise (bronze sizzle)
        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f);

        // Mix: modes + ring mod + noise, filtered
        float raw = s + ringMod + n * 0.15f;
        // TONE: filter brightness
        float filt = svf1.bandpass (raw, std::max(2000.0f, p.cutoff), 1.2f, sampleRate);

        float env = std::exp (-tf / decT);
        // Stick tip noise
        float tip = 0.0f;
        if (tf < 0.002f) tip = n * p.click * (1.0f - tf / 0.002f) * 1.5f;

        ampEnv = env;
        return (filt + tip) * env * vel * 2.0f;
    }

    float renderHiHatOpenAcoustic()
    {
        // Same cymbal model but UNDAMPED — much longer decay, more shimmer
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.15f, p.decay * 1.5f) * gate;

        float baseF = 320.0f * tuneR;
        float s = sine (phase1, baseF * 2.01f) * 0.25f
                + sine (phase2, baseF * 3.73f) * 0.22f
                + sine (phase3, baseF * 5.19f) * 0.18f
                + sine (fmPhase1, baseF * 7.41f) * 0.14f
                + sine (fmPhase2, baseF * 11.03f) * 0.1f
                + sine (fmPhase3, baseF * 14.8f) * 0.06f; // extra high shimmer

        float ringMod = sine (phase1, baseF * 2.01f) * sine (phase3, baseF * 5.19f) * 0.12f;

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f);

        float raw = s + ringMod + n * 0.12f;
        float filt = svf1.bandpass (raw, std::max(2000.0f, p.cutoff * 0.8f), 0.9f, sampleRate);

        // Two-stage decay: bright attack fades, warm sustain lingers
        float envFast = std::exp (-tf / (decT * 0.2f));
        float envSlow = std::exp (-tf / decT);
        float env = envFast * 0.3f + envSlow * 0.7f;

        ampEnv = env;
        return filt * env * vel * 2.0f;
    }

    float renderClapAcoustic()
    {
        // Acoustic hand clap: multiple hands striking with micro-delays + room
        float tf = static_cast<float>(t);
        float decT = std::max (0.05f, p.decay * 0.6f) * gate;

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;

        // 4 individual "hand strikes" with staggered timing (realistic)
        // Each burst slightly different in amplitude (human variation)
        float burst = 0.0f;
        float offs[] = { 0.0f, 0.012f, 0.022f, 0.030f };
        float amps[] = { 0.7f, 0.9f, 0.8f, 1.0f }; // slightly different strengths
        for (int b = 0; b < 4; ++b) {
            float bt = tf - offs[b];
            if (bt >= 0.0f && bt < 0.008f)
                burst += amps[b] * (1.0f - bt / 0.008f);
        }

        // Room reflection (short reverb tail — the "space" of the clap)
        float tailStart = 0.038f;
        float tail = (tf > tailStart) ? std::exp (-(tf - tailStart) / decT) * 0.5f : 0.0f;

        // Bandpass: cupped hands filter around 1-2kHz
        float bpFreq = (p.freq > 100.0f) ? p.freq : (1000.0f + p.tone / 500.0f * 800.0f);
        float bp = svf1.bandpass (n * (burst + tail), bpFreq, 0.5f + p.snap * 0.6f, sampleRate) * 3.0f;

        // Skin-on-skin adds some tonal content
        float skinTone = sine (phase1, 400.0f + p.tone * 300.0f) * burst * 0.1f;

        ampEnv = std::max (burst, tail);
        return (bp + skinTone) * vel;
    }

    float renderTomAcoustic()
    {
        // Acoustic tom: membrane modal synthesis like kick but higher, shallower shell
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float fund = std::max (60.0f, p.pitch * 0.8f * tuneR); // 80-250Hz depending on size
        float tf = static_cast<float>(t);
        float pdec = std::max (0.005f, p.pitchDec * 0.02f);
        float decT = std::max (0.08f, p.decay * 1.2f) * gate;

        // Tension modulation (pitch glide)
        float pitchGlide = 1.0f + 0.4f * std::exp (-tf / pdec);
        float f0 = fund * pitchGlide;

        // 4 Bessel modes
        float m0 = sine (phase1, f0);                      // (0,1)
        float m1 = sine (phase2, f0 * 1.593f) * 0.3f;      // (1,1)
        float m2 = sine (phase3, f0 * 2.136f) * 0.1f;       // (2,1)
        float m3 = sine (fmPhase1, f0 * 2.296f) * 0.05f;    // (0,2)

        float e0 = std::exp (-tf / decT);
        float e1 = std::exp (-tf / (decT * 0.5f));
        float e2 = std::exp (-tf / (decT * 0.3f));
        float e3 = std::exp (-tf / (decT * 0.2f));

        float membrane = m0 * e0 + m1 * e1 + m2 * e2 + m3 * e3;

        // Stick impact
        float stick = 0.0f;
        if (tf < 0.003f) {
            noiseIdx = noiseIdx * 1664525u + 1013904223u;
            stick = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f)
                    * p.click * (1.0f - tf / 0.003f) * 2.0f;
        }

        // Shell resonance (smaller shell = higher resonance than kick)
        float shellFreq = 300.0f + p.tone * 300.0f;
        float shell = svf4.bandpass (membrane, shellFreq, 1.8f, sampleRate) * 0.25f;

        float sample = (membrane + shell + stick) * vel;
        ampEnv = e0;
        return sample;
    }

    float renderCowbellAcoustic()
    {
        // Acoustic cowbell: metal shell with two main resonant modes + overtones
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.05f, p.decay * 0.6f) * gate;

        // Two main modes (metal box resonance ~540Hz + ~800Hz)
        float f1 = (p.freq1 > 10.0f ? p.freq1 : 545.0f) * tuneR, f2 = 815.0f * tuneR;
        float osc1 = sine (phase1, f1);
        float osc2 = sine (phase2, f2);

        // Metallic overtones from interaction
        float overtone = sine (phase3, (f1 + f2) * 0.5f) * 0.1f; // sum tone
        float ring = osc1 * osc2 * 0.2f; // ring mod = metallic

        // Bandpass shapes the metallic character
        float bpFreq = 750.0f + p.tone * 300.0f;
        float raw = osc1 * 0.4f + osc2 * 0.5f + overtone + ring;
        float filt = svf1.bandpass (raw, bpFreq, 3.5f, sampleRate);

        float env = std::exp (-tf / decT);
        // Strong attack emphasis (metallic strike)
        float atk = (tf < 0.002f) ? (1.0f + 3.0f * (1.0f - tf / 0.002f)) : 1.0f;

        ampEnv = env;
        return filt * env * atk * vel * 2.0f;
    }

    float renderRimshotAcoustic()
    {
        // Acoustic rimshot: stick hits both head AND rim — very bright, short
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.01f, p.decay * 0.15f) * gate;

        // Rim vibration: high-frequency metal ring
        float rim1 = sine (phase1, 1200.0f * tuneR) * 0.4f;
        float rim2 = sine (phase2, 1800.0f * tuneR) * 0.25f;
        float rim3 = sine (phase3, 2700.0f * tuneR) * 0.15f;

        // Head vibration (lower, from membrane contact)
        float head = sine (fmPhase1, 350.0f * tuneR) * 0.2f;

        // Saturating interaction (rim + head = bright crack)
        float mix = std::tanh ((rim1 + rim2 + rim3 + head) * (1.5f + p.click));

        // Sharp noise transient (stick-on-metal)
        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f);
        float impact = (tf < 0.002f) ? n * (1.0f - tf / 0.002f) * 3.0f : 0.0f;

        // HPF removes low mud, emphasizes crack
        float hpCut = std::max(200.0f, p.tone);
        float raw = (mix + impact) * std::exp (-tf / decT) * vel;
        float out = svf3.highpass (raw, hpCut, 0.5f, sampleRate);

        ampEnv = std::exp (-tf / decT);
        return out;
    }

    float renderCrashAcoustic()
    {
        // Acoustic crash cymbal: dense inharmonic plate modes + noise wash
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.5f, p.decay * 4.0f) * gate;

        // Cymbal: very inharmonic plate modes (n² spacing)
        float baseF = 220.0f * tuneR;
        float s = sine (phase1, baseF * 2.01f) * 0.2f
                + sine (phase2, baseF * 3.01f) * 0.18f
                + sine (phase3, baseF * 4.59f) * 0.15f
                + sine (fmPhase1, baseF * 6.27f) * 0.12f
                + sine (fmPhase2, baseF * 8.53f) * 0.08f
                + sine (fmPhase3, baseF * 11.7f) * 0.05f;

        // Ring modulation (metallic complexity)
        float ring1 = sine (phase1, baseF * 2.01f) * sine (phase3, baseF * 4.59f) * 0.08f;
        float ring2 = sine (phase2, baseF * 3.01f) * sine (fmPhase1, baseF * 6.27f) * 0.05f;

        // Broadband noise (cymbal wash)
        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f);

        float raw = s + ring1 + ring2 + n * 0.15f;
        // TONE: brightness control
        float filt = svf1.bandpass (raw, std::max(2000.0f, p.cutoff > 100.0f ? p.cutoff : 6000.0f), 0.6f, sampleRate);

        // Three-stage decay: bright attack → mid shimmer → warm sustain
        float envAttack = std::exp (-tf / (decT * 0.05f));
        float envMid    = std::exp (-tf / (decT * 0.3f));
        float envSustain = std::exp (-tf / decT);
        float env = envAttack * 0.2f + envMid * 0.3f + envSustain * 0.5f;

        // Stick impact
        float impact = 0.0f;
        if (tf < 0.003f) impact = n * p.click * (1.0f - tf / 0.003f) * 2.0f;

        ampEnv = env;
        return (filt + impact) * env * vel * 1.5f;
    }

    // ═══════════════════════════════════════════════════════════════════
    // TR-808 — Circuit-accurate synthesis based on Werner et al. (2014)
    // Bridged-T oscillators, Schmitt trigger metallic noise, asymmetric BPFs
    // All params mapped: PITCH/DECAY/TONE/CLICK/SNAP/P.DEC/TUNE
    // ═══════════════════════════════════════════════════════════════════

    float render808Kick()
    {
        // 808 BD: Bridged-T bandpass filter self-oscillating at ~49Hz
        // 6ms pitch sweep to ~130Hz, continuous pitch drop through decay
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float bodyFreq = std::max (20.0f, 49.4f * tuneR * (0.5f + p.pitch / 300.0f));
        float tf = static_cast<float>(t);
        float decT = 0.05f + p.decay * 0.75f; // 50-800ms range (authentic)
        decT *= gate;

        // Pitch envelope: 6ms sweep to 130Hz (circuit-accurate)
        float pitchEnvTime = std::max (0.002f, p.pitchDec * 0.015f); // ~6ms default
        float pitchEnv = std::exp (-tf / pitchEnvTime);
        // Voltage leakage: subtle continuous pitch drop (authentic 808 behavior)
        float leakage = 1.0f - tf * 0.8f / std::max (0.1f, decT);
        leakage = std::max (0.97f, leakage);
        float freq = bodyFreq * leakage + bodyFreq * 1.6f * pitchEnv;

        // Self-oscillating bridged-T: sine with slight waveform distortion
        float body = sine (phase1, freq);
        body = std::tanh (body * (1.3f + p.click * 0.8f)); // warm saturation

        // Trigger click (1ms pulse — authentic)
        float click = 0.0f;
        if (tf < 0.001f) {
            noiseIdx = noiseIdx * 1664525u + 1013904223u;
            click = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f)
                    * p.click * (1.0f - tf / 0.001f) * 3.0f;
        }

        // Decay envelope (feedback-controlled, like bridged-T Q)
        float env = std::exp (-tf / decT);

        // Tone: output lowpass filter (authentic — BD has TONE knob = LP cutoff)
        float raw = (body + click) * env * vel;
        float lpCut = 80.0f + p.tone * 8000.0f; // TONE controls LP cutoff
        float out = svf4.lowpass (raw, lpCut, 0.5f, sampleRate);

        ampEnv = env;
        return out;
    }

    float render808Snare()
    {
        // 808 SD: Two bridged-T oscillators (177Hz + 315Hz) + filtered noise
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.03f, (p.noiseDecay > 0.01f ? p.noiseDecay : p.decay * 0.5f)) * gate;

        // Low oscillator: 177Hz (bridged-T, self-damping ~60ms)
        float loFreq = 177.0f * tuneR;
        float lo = sine (phase1, loFreq);
        float loEnv = std::exp (-tf / std::max(0.01f, p.toneDecay > 0.01f ? p.toneDecay : 0.06f)); // self-damping

        // High oscillator: 315Hz
        float hiFreq = 315.0f * tuneR;
        float hi = sine (phase2, hiFreq);
        float hiEnv = std::exp (-tf / 0.045f); // slightly faster decay

        // TONE: balance between low and high (authentic — SD has TONE knob)
        float toneMix = std::clamp (p.tone / 500.0f, 0.0f, 1.0f);
        float toneBody = lo * loEnv * (1.0f - toneMix * 0.5f) + hi * hiEnv * (0.5f + toneMix * 0.5f);

        // Trigger click (1ms pulse mixed into oscillators)
        if (tf < 0.001f) {
            toneBody += (1.0f - tf / 0.001f) * p.click * 1.5f;
        }

        // Noise: white noise → BPF ~2500Hz (snare wires)
        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
        float nFilt = svf1.bandpass (n, 2500.0f, 0.7f, sampleRate) * 2.5f;
        float nEnv = std::exp (-tf / (decT * 0.8f));

        // SNAP: noise level (authentic — SD has SNAPPY knob)
        float snap = std::clamp (p.snap, 0.0f, 1.0f);
        float sample = (toneBody * 0.6f + nFilt * nEnv * snap) * vel;
        ampEnv = std::max (loEnv, nEnv * snap);
        return sample;
    }

    float render808HHClosed()
    {
        // 808 CH: 6 Schmitt trigger square oscillators → 2 BPFs → gating
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.008f, p.decay * 0.12f) * gate;

        // Authentic oscillator frequencies (HD14584 Schmitt trigger)
        float freqs[] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR) / sampleRate;
        }
        s *= 0.16f;

        // Two authentic BPFs: 7100Hz (shimmer) + 3440Hz (body)
        float bp1 = svf1.bandpass (s, std::max(2000.0f, p.cutoff), 1.5f, sampleRate);
        float bp2 = svf2.bandpass (s, std::max(1000.0f, p.cutoff * 0.48f), 1.2f, sampleRate);
        float mix = bp1 * 0.6f + bp2 * 0.4f;

        // Gating: soft limiting (authentic VCA distortion)
        mix = std::tanh (mix * 3.0f);

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return mix * env * vel;
    }

    float render808HHOpen()
    {
        // Same as closed but longer decay
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.06f, p.decay * 0.5f) * gate;

        float freqs[] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR) / sampleRate;
        }
        s *= 0.16f;

        float bp1 = svf1.bandpass (s, std::max(2000.0f, p.cutoff), 1.5f, sampleRate);
        float bp2 = svf2.bandpass (s, std::max(1000.0f, p.cutoff * 0.48f), 1.2f, sampleRate);
        float mix = std::tanh ((bp1 * 0.6f + bp2 * 0.4f) * 3.0f);

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return mix * env * vel;
    }

    float render808Clap()
    {
        // 808 HC: Noise → BPF ~1000Hz, 4 pre-echo bursts + reverb tail
        float tf = static_cast<float>(t);
        float decT = std::max (0.05f, p.decay * 0.5f) * gate;

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;

        // 4 authentic pre-echo bursts: 0, 14ms, 26ms, 36ms
        float burst = 0.0f;
        float offs[] = { 0.0f, 0.014f, 0.026f, 0.036f };
        for (int b = 0; b < 4; ++b) {
            float bt = tf - offs[b];
            if (bt >= 0.0f && bt < 0.005f)
                burst += (1.0f - bt / 0.005f) * 0.8f;
        }

        // Reverb tail (separate VCA in original)
        float tail = (tf > 0.04f) ? std::exp (-(tf - 0.04f) / decT) * 0.6f : 0.0f;

        // BPF at ~1000Hz (authentic, TONE adjusts)
        float bpFreq = (p.freq > 100.0f) ? p.freq : (800.0f + p.tone / 500.0f * 600.0f);
        float bp = svf1.bandpass (n * (burst + tail), bpFreq, 0.6f + p.snap * 0.8f, sampleRate) * 3.0f;

        ampEnv = std::max (burst, tail);
        return bp * vel;
    }

    float render808Tom()
    {
        // 808 Tom: Bridged-T oscillator with pitch sweep, TUNE controls pitch
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        // Default: Low~100Hz, Mid~160Hz, Hi~250Hz — PITCH knob sets this
        float bodyFreq = std::max (40.0f, p.pitch * 0.65f * tuneR);
        float pdec = std::max (0.005f, p.pitchDec * 0.5f);
        float decT = std::max (0.06f, p.decay * 0.8f) * gate;

        // Bridged-T: pitch sweep + self-damping decay
        float sweep = bodyFreq + bodyFreq * 1.8f * std::exp (-tf / pdec);
        float body = sine (phase1, sweep);

        // Click transient
        float ck = 0.0f;
        if (tf < 0.002f) {
            noiseIdx = noiseIdx * 1664525u + 1013904223u;
            ck = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f)
                 * p.click * (1.0f - tf / 0.002f) * 1.5f;
        }

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return (body + ck) * env * vel;
    }

    float render808Cowbell()
    {
        // 808 CB: Two Schmitt trigger squares (540Hz + 800Hz) → BPF ~850Hz Q~4.25
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.03f, p.decay * 0.3f) * gate;

        // Two square oscillators at authentic frequencies
        float sq1 = (std::fmod (phase1, 1.0) < 0.5 ? 1.0f : -1.0f) * 0.5f;
        float sq2 = (std::fmod (phase2, 1.0) < 0.5 ? 1.0f : -1.0f) * 0.6f; // higher one louder
        phase1 += static_cast<double>(p.freq1 > 10.0f ? p.freq1 : 540.0f) * static_cast<double>(tuneR) / sampleRate;
        phase2 += static_cast<double>(p.freq2 > 10.0f ? p.freq2 : 800.0f) * static_cast<double>(tuneR) / sampleRate;

        // Asymmetric BPF: centre ~850Hz, Q~4.25 (authentic)
        float bpFreq = 850.0f + p.tone * 400.0f; // TONE adjusts BPF
        float mix = svf1.bandpass (sq1 + sq2, bpFreq, 4.25f, sampleRate);

        // Percussive envelope with strong attack emphasis
        float env = std::exp (-tf / decT);
        float atk = (tf < 0.002f) ? (1.0f + 2.0f * (1.0f - tf / 0.002f)) : 1.0f;

        ampEnv = env;
        return mix * env * atk * vel * 2.0f;
    }

    float render808Rimshot()
    {
        // 808 RS: Two bridged-T oscillators (1667Hz + 455Hz) → saturating VCA → HPF
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.005f, p.decay * 0.05f) * gate; // very short! ~10ms

        // Authentic bridged-T frequencies
        float t1 = sine (phase1, 1667.0f * tuneR);
        float t2 = sine (phase2, 455.0f * tuneR);
        float mix = t1 * 0.5f + t2 * 0.4f;

        // Saturating VCA (adds harmonics — authentic)
        mix = std::tanh (mix * (2.0f + p.click * 2.0f));

        // 10ms AD envelope
        float env = std::exp (-tf / decT);
        float atk = (tf < 0.001f) ? (1.0f + 3.0f * (1.0f - tf / 0.001f)) : 1.0f;

        // HPF on output (removes low frequencies, emphasizes snap)
        float hpCut = std::max(200.0f, p.tone);
        float raw = mix * env * atk * vel;
        float out = svf3.highpass (raw, hpCut, 0.5f, sampleRate);

        ampEnv = env;
        return out;
    }

    float render808Cymbal()
    {
        // 808 CY: Same 6 oscillators as HH, both BPFs mixed, longer decay
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.3f, p.decay * 2.5f) * gate;

        float freqs[] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR) / sampleRate;
        }
        s *= 0.16f;

        // Both BPFs for full cymbal spectrum (HH uses mostly BP1)
        float bp1 = svf1.bandpass (s, std::max(2000.0f, p.cutoff > 100.0f ? p.cutoff : 7100.0f), 1.2f, sampleRate);
        float bp2 = svf2.bandpass (s, 3440.0f, 1.0f, sampleRate);
        float mix = std::tanh ((bp1 * 0.5f + bp2 * 0.5f) * 2.5f);

        // Two-stage decay (fast shimmer + slow body)
        float envF = std::exp (-tf / (decT * 0.15f));
        float envS = std::exp (-tf / decT);
        float env = envF * 0.3f + envS * 0.7f;

        ampEnv = env;
        return mix * env * vel;
    }

    // ═══════════════════════════════════════════════════════════════════
    // TR-909 — Circuit-accurate: triangle→saturation→LP, 6-bit sample hats
    // Bridged-T tones, dual triangle oscillators, punchier envelopes
    // ═══════════════════════════════════════════════════════════════════

    float render909Kick()
    {
        // 909 BD: Triangle → tanh saturation → LP filter (creates sine from hexagonal)
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float bodyFreq = std::max (30.0f, 165.0f * tuneR * (0.3f + p.pitch / 220.0f)); // ~E3 default
        float tf = static_cast<float>(t);
        float pdec = std::max (0.002f, p.pitchDec * 0.02f); // faster sweep than 808
        float decT = std::max (0.06f, p.decay * 1.5f) * gate;

        // Pitch envelope (slightly longer than 808 for "chest punch")
        float pitchEnv = std::exp (-tf / pdec);
        float freq = bodyFreq + bodyFreq * 4.0f * pitchEnv;

        // Triangle oscillator (NOT sine — authentic 909)
        float triPhase = static_cast<float>(std::fmod (phase1, 1.0));
        float tri = (triPhase < 0.5f) ? (4.0f * triPhase - 1.0f) : (3.0f - 4.0f * triPhase);
        phase1 += static_cast<double>(freq) / sampleRate;

        // Saturation → creates "hexagonal wave" (authentic 909 behavior)
        float hexagonal = std::tanh (tri * 2.5f);

        // Lowpass filter tracks oscillator freq (extracts sine from hexagonal)
        float body = svf4.lowpass (hexagonal, freq * 1.8f, 0.3f, sampleRate);

        // Noise click burst (authentic 909 ATTACK)
        float click = 0.0f;
        if (tf < 0.002f) {
            noiseIdx = noiseIdx * 1664525u + 1013904223u;
            click = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f)
                    * p.click * (1.0f - tf / 0.002f) * 2.5f;
        }

        float env = std::exp (-tf / decT);

        // TONE: output LP filter
        float toneLP = 100.0f + p.tone * 6000.0f;
        float raw = (body + click) * env * vel;
        ampEnv = env;
        return svf3.lowpass (raw, toneLP, 0.4f, sampleRate);
    }

    float render909Snare()
    {
        // 909 SD: Two TRIANGLE oscillators (180Hz + 330Hz) + noise LP+HP chain
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.04f, (p.noiseDecay > 0.01f ? p.noiseDecay : p.decay * 0.6f)) * gate;

        // Triangle oscillator 1: 180Hz (with pitch envelope for punch)
        float f1 = 180.0f * tuneR + 60.0f * std::exp (-tf / 0.004f);
        float tp1 = static_cast<float>(std::fmod (phase1, 1.0));
        float tri1 = (tp1 < 0.5f) ? (4.0f * tp1 - 1.0f) : (3.0f - 4.0f * tp1);
        phase1 += static_cast<double>(f1) / sampleRate;
        float env1 = std::exp (-tf / (decT * 0.4f)); // slightly longer for low osc

        // Triangle oscillator 2: 330Hz
        float f2 = 330.0f * tuneR;
        float tp2 = static_cast<float>(std::fmod (phase2, 1.0));
        float tri2 = (tp2 < 0.5f) ? (4.0f * tp2 - 1.0f) : (3.0f - 4.0f * tp2);
        phase2 += static_cast<double>(f2) / sampleRate;
        float env2 = std::exp (-tf / (decT * 0.3f));

        // TONE: balance between oscillators (authentic)
        float toneMix = std::clamp (p.tone / 500.0f, 0.0f, 1.0f);
        float toneBody = tri1 * env1 * (1.0f - toneMix * 0.4f) * 0.5f
                        + tri2 * env2 * (0.6f + toneMix * 0.4f) * 0.4f;

        // Noise → LP+HP chain (bandpass effect, authentic 909)
        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
        // HP at ~800Hz, LP at ~6000Hz → bandpass character
        float nFilt = svf1.bandpass (n, 4000.0f + p.tone * 2000.0f, 0.6f, sampleRate) * 2.5f;
        float nEnv = std::exp (-tf / (decT * 0.6f));

        // SNAP: noise level (authentic SNAPPY knob)
        float snap = std::clamp (p.snap, 0.0f, 1.0f);
        ampEnv = std::max (env1, nEnv * snap);
        return (toneBody + nFilt * nEnv * snap) * vel;
    }

    float render909HHClosed()
    {
        // 909 CH: Originally 6-bit samples, but synthesis approximation
        // Uses brighter tuning than 808, sharper transient
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.006f, p.decay * 0.08f) * gate;

        // Different metallic frequencies from 808 (brighter)
        float freqs[] = { 245.0f, 306.0f, 365.0f, 442.0f, 588.0f, 735.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR) / sampleRate;
        }
        s *= 0.16f;

        // Brighter filtering than 808 (more digital character)
        float bp = svf1.bandpass (s, std::max(3000.0f, p.cutoff * 0.9f), 2.0f, sampleRate);
        bp = std::tanh (bp * 3.5f); // harder gating

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return bp * env * vel;
    }

    float render909HHOpen()
    {
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.05f, p.decay * 0.45f) * gate;

        float freqs[] = { 245.0f, 306.0f, 365.0f, 442.0f, 588.0f, 735.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR) / sampleRate;
        }
        s *= 0.16f;

        float bp = svf1.bandpass (s, std::max(3000.0f, p.cutoff * 0.9f), 1.5f, sampleRate);
        bp = std::tanh (bp * 3.0f);

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return bp * env * vel;
    }

    float render909Clap()
    {
        // 909 HC: Tighter bursts than 808, brighter noise filtering
        float tf = static_cast<float>(t);
        float decT = std::max (0.03f, p.decay * 0.4f) * gate;

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;

        // Tighter burst timing than 808
        float burst = 0.0f;
        float offs[] = { 0.0f, 0.010f, 0.018f, 0.024f };
        for (int b = 0; b < 4; ++b) {
            float bt = tf - offs[b];
            if (bt >= 0.0f && bt < 0.004f)
                burst += (1.0f - bt / 0.004f) * 0.9f;
        }
        float tail = (tf > 0.028f) ? std::exp (-(tf - 0.028f) / decT) * 0.6f : 0.0f;

        // Higher BPF than 808 (~1500Hz), TONE adjusts
        float bpFreq = (p.freq > 100.0f) ? p.freq : (1200.0f + p.tone / 500.0f * 800.0f);
        float bp = svf1.bandpass (n * (burst + tail), bpFreq, 0.7f + p.snap * 0.6f, sampleRate) * 3.0f;

        ampEnv = std::max (burst, tail);
        return bp * vel;
    }

    float render909Tom()
    {
        // 909 Tom: 3 triangle oscillators, ratio 1:1.5:2.77 (authentic)
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float bodyFreq = std::max (50.0f, p.pitch * 0.8f * tuneR);
        float pdec = std::max (0.003f, p.pitchDec * 0.3f);
        float decT = std::max (0.05f, p.decay * 0.7f) * gate;

        // Three triangle oscillators at ratio 1:1.5:2.77
        float f1 = bodyFreq + bodyFreq * 1.5f * std::exp (-tf / pdec);
        float f2 = f1 * 1.5f;
        float f3 = f1 * 2.77f;

        float tp1 = static_cast<float>(std::fmod (phase1, 1.0));
        float tp2 = static_cast<float>(std::fmod (phase2, 1.0));
        float tp3 = static_cast<float>(std::fmod (phase3, 1.0));
        float tri1 = (tp1 < 0.5f ? 4.0f * tp1 - 1.0f : 3.0f - 4.0f * tp1);
        float tri2 = (tp2 < 0.5f ? 4.0f * tp2 - 1.0f : 3.0f - 4.0f * tp2) * 0.5f;
        float tri3 = (tp3 < 0.5f ? 4.0f * tp3 - 1.0f : 3.0f - 4.0f * tp3) * 0.25f;
        phase1 += static_cast<double>(f1) / sampleRate;
        phase2 += static_cast<double>(f2) / sampleRate;
        phase3 += static_cast<double>(f3) / sampleRate;

        // Each oscillator has its own decay (lower = longer, authentic)
        float e1 = std::exp (-tf / decT);
        float e2 = std::exp (-tf / (decT * 0.7f));
        float e3 = std::exp (-tf / (decT * 0.4f));

        // Click
        float ck = 0.0f;
        if (tf < 0.002f) {
            noiseIdx = noiseIdx * 1664525u + 1013904223u;
            ck = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f)
                 * p.click * (1.0f - tf / 0.002f) * 1.5f;
        }

        float body = tri1 * e1 + tri2 * e2 + tri3 * e3 + ck;
        ampEnv = e1;
        return body * vel;
    }

    float render909Rimshot()
    {
        // 909 RS: Three bridged-T tones, short envelope, bright
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.008f, p.decay * 0.08f) * gate;

        float t1 = sine (phase1, 500.0f * tuneR) * 0.4f;
        float t2 = sine (phase2, 680.0f * tuneR) * 0.3f;
        float t3 = sine (phase3, 1100.0f * tuneR) * 0.2f;

        // Saturating VCA
        float mix = std::tanh ((t1 + t2 + t3) * (2.0f + p.click));

        // Noise click
        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f);
        float nBP = svf1.bandpass (n, 5000.0f, 1.2f, sampleRate) * 0.3f;

        float env = std::exp (-tf / decT);
        float atk = (tf < 0.001f) ? 2.5f : 1.0f;

        ampEnv = env;
        return (mix + nBP) * env * atk * vel;
    }

    float render909Cymbal()
    {
        // 909 CY: Different metallic frequencies, brighter than 808
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.4f, p.decay * 3.0f) * gate;

        float freqs[] = { 295.0f, 405.0f, 530.0f, 680.0f, 870.0f, 1050.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR * 1.3f) / sampleRate;
        }
        s *= 0.16f;

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f) * 0.2f;
        float filt = svf1.bandpass (s + n, std::max(2000.0f, p.cutoff > 100.0f ? p.cutoff : 8000.0f), 0.8f, sampleRate);
        filt = std::tanh (filt * 2.5f);

        float envF = std::exp (-tf / (decT * 0.12f));
        float envS = std::exp (-tf / decT);
        ampEnv = envF * 0.3f + envS * 0.7f;
        return filt * ampEnv * vel;
    }

    // ═══════════════════════════════════════════════════════════════════
    // TR-707 — PCM character approximation: clean, bright, "plastic"
    // Original was sample-based, we approximate the sonic character
    // ═══════════════════════════════════════════════════════════════════

    float render707Kick()
    {
        // 707: tighter, higher fundamental, less sub than 808/909
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float bodyFreq = 65.0f * tuneR * (0.5f + p.pitch / 300.0f);
        float tf = static_cast<float>(t);
        float pdec = std::max (0.002f, p.pitchDec * 0.01f);
        float decT = std::max (0.04f, p.decay * 0.8f) * gate;

        float sweep = bodyFreq + bodyFreq * 2.0f * std::exp (-tf / pdec);
        float body = sine (phase1, sweep);
        body = std::clamp (body * 1.5f, -1.0f, 1.0f); // clean clip

        float ck = 0.0f;
        if (tf < 0.002f) { noiseIdx = noiseIdx * 1664525u + 1013904223u;
            ck = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f)
                 * p.click * (1.0f - tf / 0.002f) * 2.0f; }

        float env = std::exp (-tf / decT);

        // Clean LP filter (digital character)
        float raw = (body * 0.7f + ck) * env * vel;
        float lpCut = 120.0f + p.tone * 4000.0f;
        ampEnv = env;
        return svf4.lowpass (raw, lpCut, 0.3f, sampleRate);
    }

    float render707Snare()
    {
        // 707: brighter, cleaner than 808 snare
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.03f, (p.noiseDecay > 0.01f ? p.noiseDecay : p.decay * 0.45f)) * gate;

        float body = sine (phase1, 220.0f * tuneR);
        float bodyEnv = std::exp (-tf / (decT * 0.25f));

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
        float nFilt = svf1.bandpass (n, 5500.0f + p.tone * 2000.0f, 0.5f, sampleRate) * 2.0f;
        float nEnv = std::exp (-tf / (decT * 0.55f));

        float snap = std::clamp (p.snap, 0.0f, 1.0f);
        float toneMix = std::clamp (p.tone / 500.0f, 0.0f, 1.0f);
        ampEnv = std::max (bodyEnv, nEnv * snap);
        return (body * bodyEnv * (1.0f - toneMix * 0.3f) * 0.4f + nFilt * nEnv * snap) * vel;
    }

    float render707HHClosed()
    {
        // 707: digital noise character, very clean
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.005f, p.decay * 0.07f) * gate;

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
        float filt = svf1.bandpass (n, std::max(3000.0f, p.cutoff) * tuneR, 2.2f, sampleRate);

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return filt * env * vel * 2.0f;
    }

    float render707HHOpen()
    {
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.04f, p.decay * 0.35f) * gate;

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
        float filt = svf1.bandpass (n, std::max(3000.0f, p.cutoff) * tuneR, 1.6f, sampleRate);

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return filt * env * vel * 2.0f;
    }

    float render707Tom()
    {
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float bodyFreq = std::max (60.0f, p.pitch * 0.85f * tuneR);
        float pdec = std::max (0.003f, p.pitchDec * 0.3f);
        float decT = std::max (0.04f, p.decay * 0.6f) * gate;
        float tf = static_cast<float>(t);

        float sweep = bodyFreq + bodyFreq * 1.0f * std::exp (-tf / pdec);
        float body = sine (phase1, sweep);

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return body * env * vel;
    }

    float render707Cymbal()
    {
        // 707: digital noise cymbal, brighter and cleaner
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.25f, p.decay * 2.0f) * gate;

        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
        float metal = sine (phase1, 800.0f * tuneR) * 0.12f + sine (phase2, 1200.0f * tuneR) * 0.08f;
        float filt = svf1.bandpass (n * 0.6f + metal, 7000.0f + p.tone * 3000.0f, 0.7f, sampleRate);

        float envF = std::exp (-tf / (decT * 0.12f));
        float envS = std::exp (-tf / decT);
        ampEnv = envF * 0.3f + envS * 0.7f;
        return filt * ampEnv * vel * 1.8f;
    }

    // ═══════════════════════════════════════════════════════════════════
    // TR-606 — Same Schmitt trigger architecture as 808 but thinner
    // Different oscillator frequencies, less saturation, "garage" character
    // ═══════════════════════════════════════════════════════════════════

    float render606Kick()
    {
        // 606 BD: Same bridged-T as 808 but thinner, less body
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float bodyFreq = std::max (25.0f, 52.0f * tuneR * (0.5f + p.pitch / 300.0f));
        float tf = static_cast<float>(t);
        float pdec = std::max (0.003f, p.pitchDec * 0.012f);
        float decT = std::max (0.04f, p.decay * 0.7f) * gate; // shorter than 808

        float pitchEnv = std::exp (-tf / pdec);
        float freq = bodyFreq + bodyFreq * 1.2f * pitchEnv; // less sweep than 808

        float body = sine (phase1, freq);
        body = std::tanh (body * 1.1f); // less saturation than 808

        float ck = 0.0f;
        if (tf < 0.001f) { noiseIdx = noiseIdx * 1664525u + 1013904223u;
            ck = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f)
                 * p.click * (1.0f - tf / 0.001f) * 1.5f; }

        float env = std::exp (-tf / decT);

        // LP tone filter
        float raw = (body * 0.5f + ck) * env * vel;
        float lpCut = 60.0f + p.tone * 5000.0f;

        ampEnv = env;
        return svf4.lowpass (raw, lpCut, 0.4f, sampleRate);
    }

    float render606Snare()
    {
        // 606 SD: Similar to 808 but snappier, thinner tone
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.02f, (p.noiseDecay > 0.01f ? p.noiseDecay : p.decay * 0.4f)) * gate;

        // Bridged-T tones (similar to 808 but slightly different tuning)
        float lo = sine (phase1, 190.0f * tuneR);
        float loEnv = std::exp (-tf / 0.04f);
        float hi = sine (phase2, 340.0f * tuneR);
        float hiEnv = std::exp (-tf / 0.03f);

        float toneMix = std::clamp (p.tone / 500.0f, 0.0f, 1.0f);
        float toneBody = lo * loEnv * (1.0f - toneMix * 0.4f) * 0.4f
                        + hi * hiEnv * (0.5f + toneMix * 0.5f) * 0.35f;

        // Noise (less filtered than 808 — thinner)
        noiseIdx = noiseIdx * 1664525u + 1013904223u;
        float n = static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f;
        float nFilt = svf1.bandpass (n, 3000.0f, 0.5f, sampleRate) * 1.8f;
        float nEnv = std::exp (-tf / (decT * 0.7f));

        float snap = std::clamp (p.snap, 0.0f, 1.0f);
        ampEnv = std::max (loEnv, nEnv * snap);
        return (toneBody + nFilt * nEnv * snap) * vel;
    }

    float render606HHClosed()
    {
        // 606 CH: Same Schmitt trigger architecture as 808, different frequencies
        // BPFs at same centre freqs as 808: 7100Hz + 3440Hz
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.004f, p.decay * 0.06f) * gate;

        // 606 oscillator frequencies (different R/C values from 808)
        float freqs[] = { 188.0f, 278.0f, 345.0f, 490.0f, 562.0f, 710.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR) / sampleRate;
        }
        s *= 0.16f;

        // Same BPF centres as 808 but slightly different Q
        float bp1 = svf1.bandpass (s, std::max(2000.0f, p.cutoff), 1.8f, sampleRate);
        float bp2 = svf2.bandpass (s, std::max(1000.0f, p.cutoff * 0.48f), 1.4f, sampleRate);
        float mix = std::tanh ((bp1 * 0.55f + bp2 * 0.45f) * 2.5f);

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return mix * env * vel;
    }

    float render606HHOpen()
    {
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.035f, p.decay * 0.3f) * gate;

        float freqs[] = { 188.0f, 278.0f, 345.0f, 490.0f, 562.0f, 710.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR) / sampleRate;
        }
        s *= 0.16f;

        float bp1 = svf1.bandpass (s, std::max(2000.0f, p.cutoff), 1.5f, sampleRate);
        float bp2 = svf2.bandpass (s, std::max(1000.0f, p.cutoff * 0.48f), 1.2f, sampleRate);
        float mix = std::tanh ((bp1 * 0.55f + bp2 * 0.45f) * 2.5f);

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return mix * env * vel;
    }

    float render606Tom()
    {
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float bodyFreq = std::max (50.0f, p.pitch * 0.7f * tuneR);
        float pdec = std::max (0.004f, p.pitchDec * 0.35f);
        float decT = std::max (0.03f, p.decay * 0.5f) * gate;
        float tf = static_cast<float>(t);

        float sweep = bodyFreq + bodyFreq * 1.0f * std::exp (-tf / pdec);
        float body = sine (phase1, sweep);

        float ck = 0.0f;
        if (tf < 0.002f) { noiseIdx = noiseIdx * 1664525u + 1013904223u;
            ck = (static_cast<float>(noiseIdx & 0xFFFF) / 32768.0f - 1.0f)
                 * p.click * (1.0f - tf / 0.002f) * 1.0f; }

        float env = std::exp (-tf / decT);
        ampEnv = env;
        return (body * 0.7f + ck) * env * vel;
    }

    float render606Cymbal()
    {
        // 606 CY: Same 6 oscillators, both BPFs, HPF on output
        float tuneR = std::pow (2.0f, p.tune / 12.0f);
        float tf = static_cast<float>(t);
        float decT = std::max (0.2f, p.decay * 1.5f) * gate;

        float freqs[] = { 188.0f, 278.0f, 345.0f, 490.0f, 562.0f, 710.0f };
        float s = 0.0f;
        double* ph[] = { &phase1, &phase2, &phase3, &fmPhase1, &fmPhase2, &fmPhase3 };
        for (int i = 0; i < 6; ++i) {
            s += (std::fmod (*ph[i], 1.0) < 0.5 ? 1.0f : -1.0f);
            *ph[i] += static_cast<double>(freqs[i] * tuneR) / sampleRate;
        }
        s *= 0.16f;

        float bp1 = svf1.bandpass (s, 7100.0f, 1.0f, sampleRate);
        float bp2 = svf2.bandpass (s, 3440.0f, 0.9f, sampleRate);
        float mix = std::tanh ((bp1 * 0.5f + bp2 * 0.5f) * 2.0f);

        float envF = std::exp (-tf / (decT * 0.1f));
        float envS = std::exp (-tf / decT);
        ampEnv = envF * 0.3f + envS * 0.7f;
        return mix * ampEnv * vel;
    }

    // ═══ CLASSIC ANALOG MODELS (original) ═══

    float renderKick()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);

        // ── PITCH: knob controls BODY frequency (what you hear) ──
        // Sweep starts at 4x body freq and descends to body
        float bodyFreq   = std::max (10.0f, p.pitch * tuneRatio);
        float sweepStart = bodyFreq * 4.0f; // sweep from 4x above body
        float pitchDec   = std::max (0.005f, p.pitchDec);
        float decayTime  = std::max (0.01f, p.decay) * gate;

        // ── Exponential pitch ramp: sweepStart → bodyFreq ──
        float currentFreq = (t < pitchDec)
            ? sweepStart * std::pow (bodyFreq / sweepStart, static_cast<float>(t / pitchDec))
            : bodyFreq;

        // ── Sub: octave below body, slower ramp ──
        float subDec   = pitchDec * 1.5f;
        float subStart = sweepStart * 0.5f;
        float subEnd   = bodyFreq * 0.5f;
        float subFreq  = (t < subDec)
            ? subStart * std::pow (subEnd / subStart, static_cast<float>(t / subDec))
            : subEnd;

        // ── Body: sine → gentle saturation (warm, not crushed) ──
        float body = sine (phase1, currentFreq);
        body = std::tanh (body * 1.4f);

        // ── Sub: clean sine ──
        float sub = sine (phase2, subFreq);

        // ── 0.2ms fade-in ──
        fadeIn = std::min (1.0f, fadeIn + static_cast<float>(dt / 0.0002));

        // ── PUNCH: 1.3x → 1.0x over 2ms ──
        float punchBoost = 1.0f;
        if (t < 0.002)
            punchBoost = 1.0f + 0.3f * static_cast<float>(1.0 - t / 0.002);

        // ── Envelopes (stateful IIR — no clicks when decay changes) ──
        float bodyEnv = vel * iirDecay (envA, decayTime, dt) * fadeIn * punchBoost;
        float subEnv  = vel * 0.6f * iirDecay (envB, decayTime * 1.3f, dt) * fadeIn;

        ampEnv = std::max (bodyEnv, subEnv);
        float out = body * bodyEnv + sub * subEnv;

        // ── Click: fast pitch-sweep impulse ──
        if (p.click > 0.01f)
        {
            float clickStart = std::min (currentFreq * 20.0f, 10000.0f);
            float clickEnd   = currentFreq * 3.0f;
            float clickSweep = 0.0005f + p.click * 0.002f;
            float clickFreq;
            if (t < clickSweep)
            {
                float r = static_cast<float>(t / clickSweep);
                clickFreq = clickStart + (clickEnd - clickStart) * r * r;
            }
            else
                clickFreq = clickEnd;
            float clickSig = sine (phase3, clickFreq);
            float clickEnv = vel * p.click * 1.2f * expDecay (t, 0.0008f + p.click * 0.002f);
            out += clickSig * clickEnv;
        }

        // ── Analog filter with AD envelope ──
        {
            // filtEnvVal already computed at top of main loop

            float baseCut = 20.0f * std::pow (2.0f, (smoothDrumCut / 100.0f) * 13.0f);
            float envCut  = 20.0f * std::pow (2.0f, (std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal) / 100.0f) * 13.0f);
            float finalCut = std::max (baseCut, envCut);
            finalCut = std::min (finalCut, static_cast<float>(sampleRate) * 0.49f);
            float filterQ = 0.707f + smoothDrumRes * 4.0f;

            if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
                out = svf5.lowpass (out, finalCut, filterQ, sampleRate);
        }

        return out;
    }

    // ─── SNARE ───────────────────────────────────────────────────
    // 3 layers: Triangle body + Dual noise (snare wire) + Crack transient
    float renderSnare()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);
        float toneFreq = std::max (50.0f, p.tone) * tuneRatio;
        float toneDecayTime = std::max (0.01f, p.toneDecay) * gate;
        float noiseDecayTime = std::max (0.01f, p.noiseDecay) * gate;
        float snapAmt = std::max (0.0f, p.snap);

        // ── Triangle body with 2nd harmonic for fullness ──
        float toneSig = triangle (phase1, toneFreq);
        float tone2nd = sine (phase2, toneFreq * 2.0) * 0.15f;
        toneSig = std::tanh ((toneSig + tone2nd) * 1.5f) * 0.8f;
        float toneEnv = vel * 0.45f * expDecay (t, toneDecayTime);

        // ── Dual noise: bandpass rattle + highpass air ──
        float n = noise();
        float noiseBP = svf1.bandpass (n, 1200.0f * tuneRatio, 1.2f, sampleRate) * 3.0f;
        float noiseHP = svf2.highpass (n, 2500.0f, 0.7f, sampleRate) * 0.4f;
        float noiseEnv = vel * snapAmt * expDecay (t, noiseDecayTime);
        float noiseSig = (noiseBP + noiseHP) * noiseEnv;

        // ── Crack transient ──
        float crackEnv = vel * 0.6f * expDecay (t, 0.003f);
        float crackSig = noise() * crackEnv;
        crackSig = svf3.highpass (crackSig, 400.0f, 0.7f, sampleRate);

        ampEnv = std::max (toneEnv, std::max (noiseEnv, crackEnv));
        float out = toneSig * toneEnv + noiseSig + crackSig;

        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ─── HI-HAT CLOSED ────────────────────────────────────────────
    // Ring modulation between square oscillators → highpass → short envelope
    // Uses 3 pairs of square waves multiplied together for metallic sidebands
    float renderHiHatClosed()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);
        float cutHz = std::max (4000.0f, p.cutoff);
        float decayTime = std::min (std::max (0.005f, p.decay), 0.12f) * gate;

        // 3 pairs of square waves at inharmonic ratios (TR-808 style)
        float f1 = 317.0f * tuneRatio, f2 = 443.0f * tuneRatio;
        float f3 = 566.0f * tuneRatio, f4 = 752.0f * tuneRatio;
        float f5 = 892.0f * tuneRatio, f6 = 1154.0f * tuneRatio;

        float sq1 = square (phase1, f1);
        float sq2 = square (phase2, f2);
        // Ring mod pairs using metalPhases for extra oscillators
        metalPhases[0] += f3 * dt; metalPhases[0] -= std::floor (metalPhases[0]);
        metalPhases[1] += f4 * dt; metalPhases[1] -= std::floor (metalPhases[1]);
        metalPhases[2] += f5 * dt; metalPhases[2] -= std::floor (metalPhases[2]);
        metalPhases[3] += f6 * dt; metalPhases[3] -= std::floor (metalPhases[3]);
        float sq3 = metalPhases[0] < 0.5 ? 1.0f : -1.0f;
        float sq4 = metalPhases[1] < 0.5 ? 1.0f : -1.0f;
        float sq5 = metalPhases[2] < 0.5 ? 1.0f : -1.0f;
        float sq6 = metalPhases[3] < 0.5 ? 1.0f : -1.0f;

        // Ring modulation creates sum/difference frequencies = metallic!
        float ring1 = sq1 * sq2 * 0.4f;
        float ring2 = sq3 * sq4 * 0.35f;
        float ring3 = sq5 * sq6 * 0.25f;
        float metallic = ring1 + ring2 + ring3;

        // Highpass — closed hihat is bright and tight
        float filtered = svf1.highpass (metallic, cutHz, 1.5f, sampleRate);
        // Second highpass for extra brightness
        filtered = svf2.highpass (filtered, cutHz * 0.7f, 0.7f, sampleRate);

        ampEnv = vel * 0.7f * expDecay (t, decayTime);

        float out = filtered * ampEnv;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ─── HI-HAT OPEN ────────────────────────────────────────────
    // Same metallic engine but: longer decay, more low content, noise wash
    float renderHiHatOpen()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);
        float cutHz = std::max (3000.0f, p.cutoff * 0.7f); // lower cutoff than closed
        float decayTime = std::max (0.05f, p.decay) * gate;

        // Same 3 pairs but slightly different ratios for distinct character
        float f1 = 296.0f * tuneRatio, f2 = 415.0f * tuneRatio;
        float f3 = 531.0f * tuneRatio, f4 = 698.0f * tuneRatio;
        float f5 = 835.0f * tuneRatio, f6 = 1083.0f * tuneRatio;

        float sq1 = square (phase1, f1);
        float sq2 = square (phase2, f2);
        metalPhases[0] += f3 * dt; metalPhases[0] -= std::floor (metalPhases[0]);
        metalPhases[1] += f4 * dt; metalPhases[1] -= std::floor (metalPhases[1]);
        metalPhases[2] += f5 * dt; metalPhases[2] -= std::floor (metalPhases[2]);
        metalPhases[3] += f6 * dt; metalPhases[3] -= std::floor (metalPhases[3]);
        float sq3 = metalPhases[0] < 0.5 ? 1.0f : -1.0f;
        float sq4 = metalPhases[1] < 0.5 ? 1.0f : -1.0f;
        float sq5 = metalPhases[2] < 0.5 ? 1.0f : -1.0f;
        float sq6 = metalPhases[3] < 0.5 ? 1.0f : -1.0f;

        float ring1 = sq1 * sq2 * 0.4f;
        float ring2 = sq3 * sq4 * 0.35f;
        float ring3 = sq5 * sq6 * 0.25f;
        float metallic = ring1 + ring2 + ring3;

        // Softer filtering — lets more low-mid through for "open" character
        float filtered = svf1.highpass (metallic, cutHz, 1.0f, sampleRate);

        // Noise wash for shimmer on the tail
        float n = noise() * 0.15f;
        n = svf2.highpass (n, 5000.0f, 0.5f, sampleRate);
        float noiseEnv = vel * 0.3f * expDecay (t, decayTime * 0.8f);
        filtered += n * noiseEnv;

        ampEnv = vel * 0.7f * expDecay (t, decayTime);

        float out = filtered * ampEnv;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ─── CLAP ────────────────────────────────────────────────────
    // 3 staggered noise bursts → bandpass → reverb-like tail
    float renderClap()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);
        float spreadTime = std::max (0.005f, p.spread);
        float decayTime = std::max (0.01f, p.decay) * gate;
        float freqBP = std::max (200.0f, p.freq) * tuneRatio;

        float totalSig = 0.0f;
        float offsets[] = { 0.0f, spreadTime * 0.3f, spreadTime * 0.65f, spreadTime };

        for (auto off : offsets)
        {
            float localT = static_cast<float>(t) - off;
            if (localT < 0.0f) continue;
            float env = vel * 0.5f * expDecay (localT, decayTime);
            totalSig += noise() * env;
        }

        // Resonant bandpass
        totalSig = svf1.bandpass (totalSig, freqBP, 1.5f, sampleRate) * 3.0f;
        // Add filtered tail for body
        float tailNoise = noise() * vel * 0.15f * expDecay (t, decayTime * 1.5f);
        tailNoise = svf2.bandpass (tailNoise, freqBP * 0.8f, 0.8f, sampleRate) * 2.0f;
        totalSig += tailNoise;

        ampEnv = vel * expDecay (t, decayTime + spreadTime);

        float out = totalSig;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ─── TOM ─────────────────────────────────────────────────────
    // 808-style analog tom: stable pitched sine with slight initial bend
    // Tom Low = lower register, Tom Hi = upper register
    // Character: clean, punchy, PITCHED — NOT a kick sweep!
    float renderTom()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);

        // ── Base pitch: TomHi is pitched ~7 semitones above TomLow ──
        float hiOffset = (drumType == DrumType::TomHi) ? std::pow (2.0f, 7.0f / 12.0f) : 1.0f;
        float targetFreq = std::clamp (p.pitch, 40.0f, 600.0f) * tuneRatio * hiOffset;

        // ── Tiny initial pitch bend: 808 toms have a subtle 10-30% overshoot ──
        // Decays very fast (~3-8ms) — just enough for the "tok" attack
        float pitchBendAmt = 1.0f + std::clamp (p.tone * 0.003f, 0.05f, 0.35f);
        float pitchBendTime = 0.003f + p.pitchDec * 0.3f; // 3-45ms
        float bodyFreq;
        if (t < pitchBendTime)
        {
            float ratio = static_cast<float>(t / pitchBendTime);
            bodyFreq = targetFreq * (pitchBendAmt + (1.0f - pitchBendAmt) * ratio * ratio);
        }
        else
        {
            bodyFreq = targetFreq;
        }

        // ── Body: clean sine (808 bridged-T oscillator) ──
        float body = sine (phase1, bodyFreq);

        // ── Tone shaping: add harmonics based on tone knob ──
        // Low tone = pure sine (deep, clean). High tone = more overtones (bright, woody)
        float toneAmt = std::clamp (p.tone * 0.01f, 0.0f, 1.0f);
        if (toneAmt > 0.1f)
        {
            // 2nd harmonic for warmth (phase2), 3rd via waveshaping (no extra phase needed)
            float h2 = sine (phase2, bodyFreq * 2.0f) * toneAmt * 0.2f;
            float h3 = (4.0f * body * body * body - 3.0f * body) * toneAmt * 0.06f; // Chebyshev T3
            body = body * (1.0f - toneAmt * 0.15f) + h2 + h3;
        }

        // Gentle saturation — much lighter than kick
        body = body / (1.0f + std::abs (body) * 0.15f);

        // ── Click transient (808 toms have a sharp woody attack) ──
        float clickSig = 0.0f;
        if (p.click > 0.01f)
        {
            float clickFreq = bodyFreq * 3.5f;
            float clickDecay = 0.001f + p.click * 0.002f;
            clickSig = sine (phase3, clickFreq) * vel * p.click * 0.6f * expDecay (t, clickDecay);
        }

        // ── 0.3ms fade-in ──
        fadeIn = std::min (1.0f, fadeIn + static_cast<float>(dt / 0.0003));

        // ── Amp envelope: tight punch, no long kick tail ──
        float decayTime = std::max (0.02f, p.decay) * gate;
        // Initial punch: shorter than kick, more snappy
        float punch = 1.0f;
        if (t < 0.002)
            punch = 1.0f + 0.25f * static_cast<float>(1.0 - t / 0.002);
        ampEnv = vel * 0.85f * expDecay (t, decayTime) * fadeIn * punch;

        float out = body * ampEnv + clickSig;

        // ── Drum filter ──
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ─── COWBELL ──────────────────────────────────────────────────
    // TR-808 style: two sine oscillators through lowpass
    float renderCowbell()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);
        float f1 = std::max (50.0f, p.freq1) * tuneRatio;
        float f2 = std::max (50.0f, p.freq2) * tuneRatio;
        float decayTime = std::max (0.01f, p.decay) * gate;

        // Sine oscillators (808 cowbell uses pure tones, not square)
        float osc1 = sine (phase1, f1);
        float osc2 = sine (phase2, f2);

        // Mix with slight saturation for edge
        float mixed = (osc1 * 0.5f + osc2 * 0.5f);
        mixed = std::tanh (mixed * 2.0f) * 0.7f;

        // Lowpass to shape tone — higher than before for more presence
        mixed = svf1.lowpass (mixed, 2500.0f * tuneRatio, 0.8f, sampleRate);

        // Two-stage envelope: sharp 2ms transient + sustain
        float hitEnv = vel * 0.5f * expDecay (t, 0.002f);
        float susEnv = vel * 0.65f * expDecay (t, decayTime);
        ampEnv = hitEnv + susEnv;

        float out = mixed * ampEnv;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ─── RIMSHOT ─────────────────────────────────────────────────
    // Sharp tone burst + filtered noise snap
    float renderRimshot()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);
        float toneFreq = std::max (100.0f, p.tone) * tuneRatio;
        float decayTime = std::max (0.005f, p.decay) * gate;

        // Tone: two sine harmonics for body
        float tone1 = sine (phase1, toneFreq);
        float tone2 = sine (phase2, toneFreq * 1.5f) * 0.3f; // fifth harmonic
        float toneSig = std::tanh ((tone1 + tone2) * 2.0f) * 0.6f;
        float toneEnv = vel * 0.5f * expDecay (t, 0.015f);

        // Noise: bandpassed for woody character
        float n = noise();
        float noiseFilt = svf1.bandpass (n, 1800.0f, 1.0f, sampleRate) * 2.5f;
        float noiseEnv = vel * p.noise * expDecay (t, decayTime);

        // Sharp initial crack
        float crackEnv = vel * 0.8f * expDecay (t, 0.001f);
        float crackSig = noise() * crackEnv;
        crackSig = svf2.highpass (crackSig, 1000.0f, 0.7f, sampleRate);

        ampEnv = std::max (toneEnv, std::max (noiseEnv, crackEnv));

        float out = toneSig * toneEnv + noiseFilt * noiseEnv + crackSig;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ─── CRASH ───────────────────────────────────────────────────
    // Ring mod metallic engine (like open hihat) + heavy noise wash, longer decay
    float renderCrash()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);
        float freqBP = std::max (500.0f, p.freq) * tuneRatio;
        float decayTime = std::max (0.05f, p.decay) * gate;

        // Ring mod pairs at lower frequencies than hihat → fuller crash
        float f1 = 205.0f * tuneRatio, f2 = 338.0f * tuneRatio;
        float f3 = 487.0f * tuneRatio, f4 = 621.0f * tuneRatio;
        float f5 = 758.0f * tuneRatio, f6 = 943.0f * tuneRatio;

        float sq1 = square (phase1, f1);
        float sq2 = square (phase2, f2);
        metalPhases[0] += f3 * dt; metalPhases[0] -= std::floor (metalPhases[0]);
        metalPhases[1] += f4 * dt; metalPhases[1] -= std::floor (metalPhases[1]);
        metalPhases[2] += f5 * dt; metalPhases[2] -= std::floor (metalPhases[2]);
        metalPhases[3] += f6 * dt; metalPhases[3] -= std::floor (metalPhases[3]);
        float sq3 = metalPhases[0] < 0.5 ? 1.0f : -1.0f;
        float sq4 = metalPhases[1] < 0.5 ? 1.0f : -1.0f;
        float sq5 = metalPhases[2] < 0.5 ? 1.0f : -1.0f;
        float sq6 = metalPhases[3] < 0.5 ? 1.0f : -1.0f;

        float ring1 = sq1 * sq2 * 0.35f;
        float ring2 = sq3 * sq4 * 0.3f;
        float ring3 = sq5 * sq6 * 0.25f;
        float metallic = ring1 + ring2 + ring3;

        // Wide bandpass — lets through a broad spectrum
        float filtered = svf1.bandpass (metallic, freqBP, 0.3f, sampleRate) * 4.0f;
        // Add highpassed component for air
        float bright = svf2.highpass (metallic, freqBP * 1.5f, 0.5f, sampleRate) * 0.5f;
        filtered += bright;

        // Heavy noise wash for the long shimmer tail
        float n = noise() * 0.25f;
        n = svf3.highpass (n, 2000.0f, 0.5f, sampleRate);
        float noiseEnv = vel * 0.4f * expDecay (t, decayTime * 0.9f);
        filtered += n * noiseEnv;

        // Two-stage envelope: strong initial hit + long wash
        float hitEnv = vel * 0.7f * expDecay (t, 0.015f);
        float washEnv = vel * 0.5f * expDecay (t, decayTime);
        ampEnv = hitEnv + washEnv;

        float out = filtered * ampEnv;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ─── FM NOISE GENERATOR ──────────────────────────────────────
    // 4 noise types for FM cymbals:
    // 0 = WHT (white, filtered) — classic, clean shimmer
    // 1 = MTL (metallic) — noise × ring mod with inharmonic freq
    // 2 = HSS (hiss) — very high freq only, pure sizzle
    // 3 = CRN (crunch) — bit-crushed noise, lo-fi digital

    float generateTypedNoise (int noiseType, float tuneRatio)
    {
        float n = noise();
        switch (noiseType)
        {
            case 0: // White → bandpass around 6kHz
                return svf2.bandpass (n, 6000.0f * tuneRatio, 0.4f, sampleRate);
            case 1: // Metallic → noise ring-modulated with inharmonic sine
            {
                float metalFreq = 587.0f * tuneRatio;
                float metalOsc = static_cast<float>(std::sin (metalPhases[4] * 2.0 * juce::MathConstants<double>::pi));
                metalPhases[4] += metalFreq * dt;
                metalPhases[4] -= std::floor (metalPhases[4]);
                return n * metalOsc; // ring mod = multiply
            }
            case 2: // Hiss → steep highpass at 10kHz
                return svf2.highpass (n, 10000.0f, 1.5f, sampleRate);
            case 3: // Crunch → bit-crushed
            {
                float crushed = std::round (n * 8.0f) / 8.0f; // 3-bit crush
                return svf2.highpass (crushed, 3000.0f, 0.5f, sampleRate);
            }
            default:
                return n;
        }
    }

    // ─── FM DRUM ENGINE ──────────────────────────────────────────
    // Uses dedicated fmPhase1/2/3 so it can layer with analog engine
    // Cymbals: 3 independent carrier/mod pairs for dense metallic spectra

    float fmSine (double& ph, double freq)
    {
        ph += freq * dt;
        ph -= std::floor (ph);
        return static_cast<float>(std::sin (ph * 2.0 * juce::MathConstants<double>::pi));
    }

    float renderFM()
    {
        float tuneRatio = std::pow (2.0f, p.tune / 12.0f);

        switch (drumType)
        {
            case DrumType::HiHatClosed: return renderFMHiHatClosed (tuneRatio);
            case DrumType::HiHatOpen:   return renderFMHiHatOpen (tuneRatio);
            case DrumType::Crash:       return renderFMCrash (tuneRatio);
            case DrumType::Cowbell:     return renderFMCowbell (tuneRatio);
            default:                    return renderFMGeneric (tuneRatio);
        }
    }

    // ── FM HI-HAT CLOSED: 3 self-feedback operators, tight and bright ──
    float renderFMHiHatClosed (float tuneRatio)
    {
        float decayTime = std::min (std::max (0.005f, p.decay), 0.12f) * gate;
        float ratio = p.fmRatio;
        float fbAmount = p.fmDepth * 0.007f;
        float fbEnv = fbAmount * expDecay (t, std::max (0.001f, p.fmDecay));

        static const float baseFreqs[] = { 317.0f, 489.0f, 753.0f };
        static const float ratioMuls[] = { 1.0f, 1.347f, 1.891f };

        float sum = 0.0f;
        double* phases[] = { &fmPhase1, &fmPhase2, &fmPhase3 };

        for (int j = 0; j < 3; ++j)
        {
            float freq = baseFreqs[j] * tuneRatio * (1.0f + (ratio - 1.0f) * ratioMuls[j] * 0.3f);
            double ph = *phases[j];
            float fb = static_cast<float>(std::sin (ph * 2.0 * juce::MathConstants<double>::pi));
            float osc = static_cast<float>(std::sin ((ph + static_cast<double>(fb * fbEnv)) * 2.0 * juce::MathConstants<double>::pi));
            *phases[j] += freq * dt;
            *phases[j] -= std::floor (*phases[j]);
            sum += osc * (1.0f / 3.0f);
        }

        sum = svf1.highpass (sum, 7000.0f, 1.2f, sampleRate);

        // Fixed noise texture (was already perfect — don't use controllable noise here)
        float n = noise() * 0.06f;
        n = svf2.highpass (n, 9000.0f, 0.7f, sampleRate);
        sum += n * expDecay (t, decayTime * 0.5f);

        float fmFadeIn = std::min (1.0f, static_cast<float>(t / 0.0002));
        ampEnv = vel * 0.7f * expDecay (t, decayTime) * fmFadeIn;

        float out = sum * ampEnv;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ── FM HI-HAT OPEN: feedback FM + controllable noise ──
    float renderFMHiHatOpen (float tuneRatio)
    {
        float decayTime = std::max (0.1f, p.decay) * gate;
        float ratio = p.fmRatio;
        float fbAmount = p.fmDepth * 0.006f;
        float fbEnv = fbAmount * expDecay (t, std::max (0.001f, p.fmDecay) * 2.0f);
        float noiseAmt = p.fmNoise;

        static const float baseFreqs[] = { 245.0f, 397.0f, 643.0f };
        static const float ratioMuls[] = { 1.0f, 1.31f, 1.62f };

        float sum = 0.0f;
        double* phases[] = { &fmPhase1, &fmPhase2, &fmPhase3 };

        for (int j = 0; j < 3; ++j)
        {
            float freq = baseFreqs[j] * tuneRatio * (1.0f + (ratio - 1.0f) * ratioMuls[j] * 0.3f);
            double ph = *phases[j];
            float fb = static_cast<float>(std::sin (ph * 2.0 * juce::MathConstants<double>::pi));
            float osc = static_cast<float>(std::sin ((ph + static_cast<double>(fb * fbEnv)) * 2.0 * juce::MathConstants<double>::pi));
            *phases[j] += freq * dt;
            *phases[j] -= std::floor (*phases[j]);
            sum += osc * (1.0f / 3.0f);
        }

        sum = svf1.highpass (sum, 2500.0f, 0.7f, sampleRate);

        // Noise layer — amount and type controllable
        float noiseLevel = 0.3f + noiseAmt * 0.7f; // base 30% + knob adds up to 100%
        float n = generateTypedNoise (p.fmNoiseType, tuneRatio);
        float noiseEnv = vel * noiseLevel * expDecay (t, decayTime * 0.85f);
        sum = sum * (1.0f - noiseLevel * 0.5f) + n * noiseEnv;

        float fmFadeIn = std::min (1.0f, static_cast<float>(t / 0.0002));
        ampEnv = vel * 0.65f * expDecay (t, decayTime) * fmFadeIn;

        float out = sum * ampEnv;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ── FM CRASH: feedback FM + controllable noise wash ──
    float renderFMCrash (float tuneRatio)
    {
        float decayTime = std::max (0.15f, p.decay) * gate;
        float ratio = p.fmRatio;
        float fbAmount = p.fmDepth * 0.006f;
        float fbEnv = fbAmount * expDecay (t, std::max (0.001f, p.fmDecay) * 2.5f);
        float noiseAmt = p.fmNoise;

        static const float baseFreqs[] = { 195.0f, 322.0f, 510.0f };
        static const float ratioMuls[] = { 1.0f, 1.41f, 1.73f };

        float sum = 0.0f;
        double* phases[] = { &fmPhase1, &fmPhase2, &fmPhase3 };

        for (int j = 0; j < 3; ++j)
        {
            float freq = baseFreqs[j] * tuneRatio * (1.0f + (ratio - 1.0f) * ratioMuls[j] * 0.3f);
            double ph = *phases[j];
            float fb = static_cast<float>(std::sin (ph * 2.0 * juce::MathConstants<double>::pi));
            float osc = static_cast<float>(std::sin ((ph + static_cast<double>(fb * fbEnv)) * 2.0 * juce::MathConstants<double>::pi));
            *phases[j] += freq * dt;
            *phases[j] -= std::floor (*phases[j]);
            sum += osc * (1.0f / 3.0f);
        }

        sum = svf1.highpass (sum, 1500.0f, 0.4f, sampleRate);

        // Noise — heavier default for crash, plus controllable amount
        float noiseLevel = 0.4f + noiseAmt * 0.6f;
        float n = generateTypedNoise (p.fmNoiseType, tuneRatio);
        float noiseEnv = vel * noiseLevel * expDecay (t, decayTime * 0.9f);
        sum = sum * (1.0f - noiseLevel * 0.4f) + n * noiseEnv;

        float fmFadeIn = std::min (1.0f, static_cast<float>(t / 0.0002));
        float hitEnv = vel * 0.7f * expDecay (t, 0.015f);
        float washEnv = vel * 0.5f * expDecay (t, decayTime);
        ampEnv = (hitEnv + washEnv) * fmFadeIn;

        float out = sum * ampEnv;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ── FM COWBELL: 2 feedback operators, RATIO scales frequencies ──
    float renderFMCowbell (float tuneRatio)
    {
        float decayTime = std::max (0.01f, p.decay) * gate;
        float ratio = p.fmRatio;
        float fbAmount = p.fmDepth * 0.004f; // less feedback than hihat
        float fbEnv = fbAmount * expDecay (t, std::max (0.001f, p.fmDecay));

        // RATIO directly scales the base frequencies
        float f1 = std::max (50.0f, p.freq1) * tuneRatio * ratio;
        float f2 = std::max (50.0f, p.freq2) * tuneRatio * ratio;

        // Op 1 with self-feedback
        double ph1 = fmPhase1;
        float fb1 = static_cast<float>(std::sin (ph1 * 2.0 * juce::MathConstants<double>::pi));
        float osc1 = static_cast<float>(std::sin ((ph1 + static_cast<double>(fb1 * fbEnv)) * 2.0 * juce::MathConstants<double>::pi));
        fmPhase1 += f1 * dt;
        fmPhase1 -= std::floor (fmPhase1);

        // Op 2 with self-feedback
        double ph2 = fmPhase2;
        float fb2 = static_cast<float>(std::sin (ph2 * 2.0 * juce::MathConstants<double>::pi));
        float osc2 = static_cast<float>(std::sin ((ph2 + static_cast<double>(fb2 * fbEnv)) * 2.0 * juce::MathConstants<double>::pi));
        fmPhase2 += f2 * dt;
        fmPhase2 -= std::floor (fmPhase2);

        float mixed = (osc1 + osc2) * 0.4f;
        mixed = svf1.lowpass (mixed, 2500.0f * tuneRatio, 0.8f, sampleRate);

        // Noise layer (controllable)
        if (p.fmNoise > 0.01f)
        {
            float n = generateTypedNoise (p.fmNoiseType, tuneRatio);
            float noiseEnv = vel * p.fmNoise * 0.4f * expDecay (t, decayTime * 0.5f);
            mixed += n * noiseEnv;
        }

        float fmFadeIn = std::min (1.0f, static_cast<float>(t / 0.0002));
        float hitEnv = vel * 0.5f * expDecay (t, 0.002f);
        float susEnv = vel * 0.65f * expDecay (t, decayTime);
        ampEnv = (hitEnv + susEnv) * fmFadeIn;

        float out = mixed * ampEnv;
        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }

    // ── FM GENERIC: single mod→carrier for kick/snare/tom/clap/rimshot ──
    float renderFMGeneric (float tuneRatio)
    {
        float baseFreq, carDecay;
        bool useNoise = false;
        bool usePitchEnv = false;

        switch (drumType)
        {
            case DrumType::Kick:
                baseFreq = std::max (20.0f, p.pitch) * tuneRatio;
                carDecay = std::max (0.01f, p.decay);
                usePitchEnv = true;
                break;
            case DrumType::Snare:
                baseFreq = std::max (50.0f, p.tone) * tuneRatio;
                carDecay = std::max (0.01f, p.noiseDecay);
                useNoise = true;
                break;
            case DrumType::Clap:
                baseFreq = 180.0f * tuneRatio;
                carDecay = std::max (0.01f, p.decay);
                useNoise = true;
                break;
            case DrumType::Tom:
            case DrumType::TomHi:
            {
                float hiOff = (drumType == DrumType::TomHi) ? std::pow (2.0f, 7.0f / 12.0f) : 1.0f;
                baseFreq = std::max (30.0f, p.pitch) * tuneRatio * hiOff;
                carDecay = std::max (0.01f, p.decay);
                usePitchEnv = false; // toms use subtle bend, not kick sweep
                break;
            }
            case DrumType::Rimshot:
                baseFreq = std::max (100.0f, p.tone) * tuneRatio;
                carDecay = std::max (0.005f, p.decay);
                useNoise = true;
                break;
            default:
                baseFreq = 100.0f * tuneRatio;
                carDecay = std::max (0.01f, p.decay);
                break;
        }

        carDecay *= gate;
        float modDecayVal = std::max (0.001f, p.fmDecay);

        // Carrier frequency
        float carFreq;
        if (usePitchEnv)
        {
            float pitchDec = std::max (0.005f, p.pitchDec);
            float pitchStart = baseFreq * 2.0f;
            float pitchEndF = std::max (20.0f, baseFreq * 0.4f);
            carFreq = (t < pitchDec)
                ? pitchStart * std::pow (pitchEndF / pitchStart, static_cast<float>(t / pitchDec))
                : pitchEndF;
        }
        else
            carFreq = baseFreq;

        // Single modulator → carrier
        float modFreq = baseFreq * p.fmRatio;
        float modSig = fmSine (fmPhase2, modFreq);
        float modAmt = p.fmDepth * baseFreq * 0.01f * expDecay (t, modDecayVal);

        float modulatedFreq = std::max (1.0f, carFreq + modSig * modAmt);
        float out = fmSine (fmPhase1, modulatedFreq);

        float fmFadeIn = std::min (1.0f, static_cast<float>(t / 0.0002));
        ampEnv = vel * expDecay (t, carDecay) * fmFadeIn;
        out *= ampEnv;

        // Noise layer (built-in for snare/clap/rimshot + controllable via fmNoise for all)
        if (useNoise)
        {
            float n = noise();
            n = svf2.bandpass (n, baseFreq * 4.0f, 0.8f, sampleRate) * 2.5f;
            float noiseEnv = vel * 0.5f * expDecay (t, carDecay * 0.7f);
            out += n * noiseEnv;
            ampEnv = std::max (ampEnv, noiseEnv);
        }
        if (p.fmNoise > 0.01f && !useNoise)
        {
            float n = generateTypedNoise (p.fmNoiseType, tuneRatio);
            float noiseEnv = vel * p.fmNoise * 0.4f * expDecay (t, carDecay * 0.7f);
            out += n * noiseEnv;
            ampEnv = std::max (ampEnv, noiseEnv);
        }

        if (smoothDrumCut < 99.0f || p.drumFiltEnv > 0.5f)
        {
            float effCut = std::min (100.0f, smoothDrumCut + p.drumFiltEnv * filtEnvVal);
            float fFreq = 20.0f * std::pow (2.0f, (effCut / 100.0f) * 13.0f);
            out = svf4.lowpass (out, fFreq, 0.707f + smoothDrumRes * 4.0f, sampleRate);
        }
        return out;
    }
};
