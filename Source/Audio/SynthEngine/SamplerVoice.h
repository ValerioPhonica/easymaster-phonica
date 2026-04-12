#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "AnalogVoice.h"
#include "../FX/TimeStretch.h"
#include "../FX/MultiModelFilter.h"
#include <cmath>
#include <algorithm>
#include <memory>

// ═══════════════════════════════════════════════════════════════════
// SamplerVoice — High-quality sample playback with pitch shifting
//
// ■ Cubic Hermite interpolation for clean pitch transposition
// ■ Start/End points, loop, reverse
// ■ ADSR amplitude envelope
// ■ SVF multimode filter (LP/HP/BP)
// ■ Per-note pitch from sequencer (chromatic playback)
// ═══════════════════════════════════════════════════════════════════
class SamplerVoice
{
public:
    void prepare (double sr) { sampleRate = sr; }

    // Set warp markers BEFORE calling noteOn — they'll be passed to TimeStretch
    void setWarpData (const std::vector<TimeStretch::WarpPt>& pts, float totalBeats)
    {
        pendingWarpPts = pts;
        pendingWarpBeats = totalBeats;
        // Apply immediately to already-playing voices (live warp editing) — BOTH channels
        if (!pts.empty() && totalBeats > 0.0f)
        {
            timeStretch.setWarpMarkers (pts, totalBeats);
            if (stretchStereo) timeStretchR.setWarpMarkers (pts, totalBeats);
        }
        else
        {
            timeStretch.clearWarpMarkers();
            if (stretchStereo) timeStretchR.clearWarpMarkers();
        }
    }

    void noteOn (int noteIdx, int octave, float vel, const SynthVoiceParams& p,
                 float gateDur, std::shared_ptr<juce::AudioBuffer<float>> buf,
                 float currentBPM = 120.0f)
    {
        if (buf == nullptr || buf->getNumSamples() < 4) return;

        bufferRef = buf;  // keep shared_ptr alive on audio thread
        buffer = buf.get();
        velocity = vel;
        params = p;
        gate = true;
        bool wasPlaying = playing;
        playing = true;
        hasPlocks = false;
        killFade = 0; // reset kill state on retrigger
        gateDuration = gateDur;
        gateTimer = 0.0f;

        // Pitch ratio: semitone difference from root note
        int semi = noteIdx + (octave + 2) * 12;
        float semiDiff = static_cast<float>(semi - p.smpRootNote) + p.smpTune + p.smpFine;
        pitchRatio = std::pow (2.0f, semiDiff / 12.0f);
        if (p.smpReverse > 0) pitchRatio = -pitchRatio;

        // Compute sample region
        int totalLen = buf->getNumSamples();
        const float* snapSrc = buf->getReadPointer (0);
        int startSmp = static_cast<int>(p.smpStart * static_cast<float>(totalLen));
        int endSmp   = static_cast<int>(p.smpEnd   * static_cast<float>(totalLen));
        if (startSmp >= endSmp) { startSmp = 0; endSmp = totalLen; }

        // ── CRITICAL: only snap to zero when NOT time-stretching ──
        // When stretching, we need EXACT boundaries for sample-accurate
        // loop timing. The TimeStretch engine handles crossfades internally.
        bool willStretch = (p.smpWarp > 0);
        if (!willStretch)
        {
            startSmp = snapToZero (snapSrc, totalLen, startSmp);
            endSmp   = snapToZero (snapSrc, totalLen, endSmp);
        }
        if (startSmp >= endSmp) endSmp = totalLen;
        regionStart = startSmp;
        regionEnd   = endSmp;

        // Set initial position
        if (pitchRatio >= 0)
            position = static_cast<double>(regionStart);
        else
            position = static_cast<double>(regionEnd - 1);

        // Reset envelope — preserve envVal on retrigger to avoid click
        envStage = 0;
        if (!wasPlaying) envVal = 0.0f;
        filtEnvStage = 0;
        if (!wasPlaying) filtEnvVal = 0.0f;

        // Reset FM modulator
        fmPhase = 0.0;
        fmEnvVal = 0.0f;
        fmEnvStage = 0;

        // Filter: soft reset on retrigger to avoid click, hard reset on fresh note
        if (wasPlaying) { filterL.softReset(); filterR.softReset(); }
        else            { filterL.reset();     filterR.reset();     }

        endFade = 0; // reset end-of-sample fade

        // ═══════════════════════════════════════════════════════
        // TIME STRETCH SETUP
        // ═══════════════════════════════════════════════════════
        // smpBpmSync = 0 (MANUAL): user controls ratio via smpStretch knob
        // smpBpmSync = 1 (AUTO):   compute ratio from smpBPM / currentBPM
        // smpBars > 0:             compute from bar count — most reliable!
        // Both modes work in INT and EXT clock (currentBPM always valid)
        // ═══════════════════════════════════════════════════════
        // TIME STRETCH — stretchFactor is output/input in RAW SAMPLE SPACE.
        // The TimeStretch engine reads N buffer samples, produces N*stretch
        // output samples at projectSR.  All SR conversions must account
        // for the fact that the buffer is stored at fileSR but played at
        // projectSR → the "apparent" duration of N samples = N/projectSR.
        // ═══════════════════════════════════════════════════════
        float stretchFactor = p.smpStretch; // manual stretch fallback
        float projectSR = static_cast<float>(sampleRate);

        if (p.smpWarp > 0 && currentBPM > 20.0f)
        {
            if (p.smpBars > 0)
            {
                // BAR-BASED SYNC: most reliable
                static const float barLUT[] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
                float bars = barLUT[std::clamp (p.smpBars, 1, 8)];
                // Use project BPM when synced, sample BPM knob when INTERNAL
                float refBPM = (p.smpBpmSync == 1) ? currentBPM
                             : (p.smpBPM > 20.0f ? p.smpBPM : currentBPM);
                float targetDur = bars * 4.0f * 60.0f / refBPM;
                int regionLen = regionEnd - regionStart;
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
        if (p.smpSyncMul != 0)
            stretchFactor /= std::pow (2.0f, static_cast<float>(p.smpSyncMul));
        useStretch = (p.smpWarp > 0 && stretchFactor > 0.01f);
        dawSyncLoop = false;
        expectedOutputLen = 0;
        stretchOutputCount = 0;
        if (useStretch)
        {
            timeStretch.prepare (projectSR);
            timeStretch.setMode (p.smpWarp); // 1=BEATS, 2=TEXTURE, 3=REPITCH, 4=BEATS2
            const float* mono = buf->getReadPointer (0);
            timeStretch.setBuffer (mono, buf->getNumSamples());
            // ALL modes get the note's pitchRatio:
            // BEATS/TEXTURE: pitchRatio controls grain read speed (pitch shift)
            // REPITCH: pitchRatio combined with stretch for tape-style speed
            // At root note, pitchRatio = 1.0 → no pitch change
            float wsolaPitch = std::abs (pitchRatio);
            timeStretch.setParams (stretchFactor, wsolaPitch);
            timeStretch.setRegion (regionStart, regionEnd, p.smpLoop > 0);
            timeStretch.setReversed (p.smpReverse > 0);

            // ── Stereo: setup R channel stretch ──
            stretchStereo = (buf->getNumChannels() > 1);
            if (stretchStereo)
            {
                timeStretchR.prepare (projectSR);
                timeStretchR.setMode (p.smpWarp);
                timeStretchR.setBuffer (buf->getReadPointer (1), buf->getNumSamples());
                timeStretchR.setParams (stretchFactor, wsolaPitch);
                timeStretchR.setRegion (regionStart, regionEnd, p.smpLoop > 0);
                timeStretchR.setReversed (p.smpReverse > 0);
            }

            // ── Pass warp markers for per-segment variable stretch ──
            if (!pendingWarpPts.empty() && pendingWarpBeats > 0.0f)
            {
                timeStretch.setWarpMarkers (pendingWarpPts, pendingWarpBeats);
                if (stretchStereo) timeStretchR.setWarpMarkers (pendingWarpPts, pendingWarpBeats);
            }
            else
            {
                timeStretch.clearWarpMarkers();
                if (stretchStereo) timeStretchR.clearWarpMarkers();
            }

            // DAW sync loop: exact output length for one full source pass
            if (p.smpLoop > 0)
            {
                dawSyncLoop = true;
                // expectedOutputLen = N * stretchFactor  (direct — no SR conversion
                // needed because stretchFactor already accounts for projectSR)
                if (p.smpBars > 0)
                {
                    // BARS mode: compute from bar duration for maximum precision
                    static const float barLUT2[] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
                    float bars = barLUT2[std::clamp (p.smpBars, 1, 8)];
                    double targetDurSec = static_cast<double>(bars) * 4.0 * 60.0 / static_cast<double>(currentBPM);
                    expectedOutputLen = static_cast<int>(std::round (targetDurSec * sampleRate));
                }
                else
                {
                    // BPM mode: output samples = input samples × stretchFactor
                    int regionLen = regionEnd - regionStart;
                    expectedOutputLen = static_cast<int>(std::round (
                        static_cast<double>(regionLen) * static_cast<double>(stretchFactor)));
                }
            }
        }
    }

    void noteOff()
    {
        gate = false;
        if (envStage < 3) envStage = 3; // amp release
        if (filtEnvStage < 3) filtEnvStage = 3; // filter release
    }

    bool isPlaying() const { return playing; }
    bool isKilling() const { return playing && killFade > 0; }
    bool isGateActive() const { return playing && envStage < 3; }
    void releaseGate() { gate = false; }
    // Normalized playback position (0-1) for GUI playhead
    float getPlayPosition() const
    {
        if (!playing || buffer == nullptr) return 0.0f;
        return static_cast<float>(position) / static_cast<float>(std::max (1, buffer->getNumSamples()));
    }
    void kill()
    {
        if (playing && envVal > 0.001f)
            killFade = 256;
        else
        { playing = false; gate = false; buffer = nullptr; bufferRef.reset(); }
    }
    void hardKill()
    {
        playing = false; gate = false; killFade = 0;
        envVal = 0.0f; buffer = nullptr; bufferRef.reset();
    }
    void updateParams (const SynthVoiceParams& p) { if (!hasPlocks) params = p; }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        if (!playing || buffer == nullptr) return;

        const float* srcL = buffer->getReadPointer (0);
        const float* srcR = (buffer->getNumChannels() > 1) ? buffer->getReadPointer (1) : srcL;
        int bufLen = buffer->getNumSamples();
        float dt = 1.0f / static_cast<float>(sampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            // Gate timing — only trigger release in GATE mode (smpPlayMode==1)
            // ONE SHOT mode (smpPlayMode==0): sample plays to end regardless of gate
            gateTimer += dt;
            if (params.smpPlayMode == 1 && gate && gateDuration > 0 && gateTimer >= gateDuration)
                noteOff();

            float smpL, smpR;

            if (useStretch)
            {
                // ── FM modulator with envelope ──
                float fmAmt = params.smpFmAmt;
                float fmRat = params.smpFmRatio;
                float fmMod = 0.0f;
                if (fmAmt > 0.001f)
                {
                    float fmEnv = runFmEnv (dt, params.smpFmEnvA, params.smpFmEnvD, params.smpFmEnvS);
                    float fmOsc = std::sin (static_cast<float>(fmPhase * 6.283185307));
                    fmMod = fmOsc * fmAmt * fmEnv * 4.0f;
                    double baseHz = 440.0 * std::pow (2.0, (params.smpRootNote - 69 + params.smpTune + params.smpFine) / 12.0);
                    fmPhase += baseHz * static_cast<double>(fmRat) / sampleRate;
                    if (fmPhase > 1.0) fmPhase -= std::floor (fmPhase);
                }

                // Apply FM as per-sample pitch modulation into WSOLA
                timeStretch.setPitchMod (fmMod);
                if (stretchStereo) timeStretchR.setPitchMod (fmMod);

                // TimeStretch engine — independent pitch/time + FM
                smpL = timeStretch.getNextSample();
                smpR = stretchStereo ? timeStretchR.getNextSample() : smpL;
                stretchOutputCount++;

                // ── DAW sync: force-loop at exact bar boundary ──
                if (dawSyncLoop && expectedOutputLen > 0
                    && stretchOutputCount >= expectedOutputLen)
                {
                    timeStretch.forceRestart();
                    if (stretchStereo) timeStretchR.forceRestart();
                    stretchOutputCount = 0;
                }
                // Non-looping: stop when source exhausted
                else if (timeStretch.isFinished() && params.smpLoop == 0)
                    { playing = false; return; }
            }
            else
            {
                // ── TRUE LINEAR FM: modulates READ POSITION, not speed ──
                // This creates classic DX7-style metallic/bell sidebands
                // instead of ring-mod-like speed wobble
                float fmAmt = params.smpFmAmt;
                float fmRat = params.smpFmRatio;
                double fmDispL = 0.0, fmDispR = 0.0;
                if (fmAmt > 0.001f)
                {
                    float fmEnv = runFmEnv (dt, params.smpFmEnvA, params.smpFmEnvD, params.smpFmEnvS);
                    float fmOscL = std::sin (static_cast<float>(fmPhase * 6.283185307));
                    float fmOscR = std::sin (static_cast<float>((fmPhase + 0.083) * 6.283185307)); // 30° stereo offset

                    // Modulation index: displacement in samples = index * (sr / modFreq)
                    double baseHz = 440.0 * std::pow (2.0, (params.smpRootNote - 69 + params.smpTune + params.smpFine) / 12.0);
                    float modIndex = fmAmt * fmEnv * 8.0f; // max ~8 carrier cycles displacement
                    float dispScale = static_cast<float>(sampleRate / std::max (20.0, baseHz));
                    fmDispL = static_cast<double>(fmOscL * modIndex * dispScale);
                    fmDispR = static_cast<double>(fmOscR * modIndex * dispScale);

                    fmPhase += baseHz * static_cast<double>(fmRat) / sampleRate;
                    if (fmPhase > 1.0) fmPhase -= std::floor (fmPhase);
                }

                // Read L and R at FM-displaced positions (true linear FM)
                double readL = position + fmDispL;
                double readR = position + fmDispR;
                // Clamp to region bounds
                readL = std::clamp (readL, static_cast<double>(regionStart), static_cast<double>(regionEnd - 1));
                readR = std::clamp (readR, static_cast<double>(regionStart), static_cast<double>(regionEnd - 1));
                smpL = cubicInterp (srcL, bufLen, readL);
                smpR = cubicInterp (srcR, bufLen, readR);

                // Position advances at constant speed (FM doesn't affect speed)
                position += static_cast<double>(pitchRatio);

                if (pitchRatio >= 0)
                {
                    if (position >= regionEnd)
                    {
                        if (params.smpLoop > 0)
                            position = static_cast<double>(regionStart) + std::fmod (position - regionEnd, static_cast<double>(regionEnd - regionStart));
                        else
                            { endFade = 64; position = static_cast<double>(regionEnd - 1); }
                    }
                }
                else
                {
                    if (position < regionStart)
                    {
                        if (params.smpLoop > 0)
                            position = static_cast<double>(regionEnd - 1) - std::fmod (static_cast<double>(regionStart) - position, static_cast<double>(regionEnd - regionStart));
                        else
                            { endFade = 64; position = static_cast<double>(regionStart); }
                    }
                }
            }

            // ADSR envelope
            float env = runADSR (dt);

            // Filter ADSR envelope
            float fEnv = runFilterADSR (dt);

            // Apply gain and velocity
            float gain = params.smpGain * velocity * env;
            smpL *= gain;
            smpR *= gain;

            // Multi-model filter — params already include LFO/MSEG modulation
            float liveCut = params.smpCut;
            float liveRes = params.smpRes;
            int   liveFTp = params.smpFType;
            int   liveFMd = params.smpFModel;
            int   liveFPo = params.smpFPoles;
            float liveFiltEnvAmt = params.smpFiltEnv;
            // Apply filter envelope modulation
            liveCut = std::clamp (liveCut + liveFiltEnvAmt * fEnv, 0.0f, 100.0f);
            if (liveCut < 99.5f)
            {
                float cutHz = 20.0f * std::pow (1000.0f, liveCut / 100.0f);
                cutHz = std::clamp (cutHz, 16.0f, std::min (18000.0f, static_cast<float>(sampleRate) * 0.45f));
                smpL = filterL.process (smpL, cutHz, liveRes, liveFMd, liveFTp, liveFPo,
                                         static_cast<float>(sampleRate));
                smpR = filterR.process (smpR, cutHz, liveRes, liveFMd, liveFTp, liveFPo,
                                         static_cast<float>(sampleRate));
            }

            if (killFade > 0)
            {
                float fadeGain = static_cast<float>(killFade) / 256.0f;
                smpL *= fadeGain;
                smpR *= fadeGain;
                --killFade;
                if (killFade == 0) { playing = false; gate = false; break; }
            }

            // End-of-sample fadeout (64 samples → anti-click)
            if (endFade > 0)
            {
                float fadeGain = static_cast<float>(endFade) / 64.0f;
                smpL *= fadeGain;
                smpR *= fadeGain;
                --endFade;
                if (endFade == 0) { playing = false; break; }
            }

            outL[i] += smpL;
            outR[i] += smpR;
        }
    }

    void setGlide (float, float) {} // Stub for SynthPart compatibility
    void setLiveState (const SynthTrackState* s) { if (!hasPlocks) liveState = s; }
    bool hasPlocks = false;
    void setPlocked() { hasPlocks = true; }
    const SynthTrackState* liveState = nullptr;

private:
    double sampleRate = 44100.0;
    const juce::AudioBuffer<float>* buffer = nullptr;
    std::shared_ptr<juce::AudioBuffer<float>> bufferRef;  // keeps buffer alive on audio thread
    SynthVoiceParams params;
    bool playing = false;
    int  killFade = 0;
    int  endFade = 0;   // anti-click: 64-sample fadeout at sample end
    bool gate = false;
    float velocity = 0;
    float gateDuration = 0;
    float gateTimer = 0;
    double position = 0;
    float pitchRatio = 1.0f;
    int regionStart = 0, regionEnd = 0;

    // Time stretch engine (used when smpWarp > 0)
    TimeStretch timeStretch;   // L (or mono)
    TimeStretch timeStretchR;  // R channel (only used when buffer is stereo)
    bool useStretch = false;
    bool stretchStereo = false; // true when stretching a stereo buffer

    // Pending warp data — set before noteOn, consumed during setup
    std::vector<TimeStretch::WarpPt> pendingWarpPts;
    float pendingWarpBeats = 0.0f;

    // ── DAW sync: force-loop at exact bar boundary ──
    bool dawSyncLoop = false;       // true when DAW sync + loop
    int  expectedOutputLen = 0;     // exact samples per loop cycle
    int  stretchOutputCount = 0;    // output samples produced this cycle

    // FM modulator for sampler
    double fmPhase = 0.0;
    float  fmEnvVal = 0.0f;
    int    fmEnvStage = 0; // 0=attack, 1=decay, 2=done

    float runFmEnv (float dt, float atk, float dcy, float sus = 0.0f)
    {
        switch (fmEnvStage)
        {
            case 0: // Attack
                fmEnvVal += dt / std::max (0.0003f, atk);
                if (fmEnvVal >= 1.0f) { fmEnvVal = 1.0f; fmEnvStage = 1; }
                break;
            case 1: // Decay → Sustain
            {
                float target = std::clamp (sus, 0.0f, 1.0f);
                fmEnvVal = target + (fmEnvVal - target) * std::exp (-dt / std::max (0.005f, dcy));
                if (fmEnvVal <= target + 0.001f) { fmEnvVal = target; fmEnvStage = 2; }
                break;
            }
            case 2: // Sustain hold
                fmEnvVal = std::clamp (sus, 0.0f, 1.0f);
                break;
            default: break;
        }
        return fmEnvVal;
    }

    // ADSR
    int envStage = 0; // 0=atk, 1=dec, 2=sus, 3=rel, 4=done
    float envVal = 0;

    float runADSR (float dt)
    {
        switch (envStage)
        {
            case 0: // Attack
                envVal += dt / std::max (0.0005f, params.smpA);
                if (envVal >= 1.0f) { envVal = 1.0f; envStage = 1; }
                break;
            case 1: // Decay
                envVal -= (envVal - params.smpS) * (1.0f - std::exp (-dt / std::max (0.005f, params.smpD)));
                if (envVal <= params.smpS + 0.001f) { envVal = params.smpS; envStage = 2; }
                break;
            case 2: // Sustain
                envVal = params.smpS;
                break;
            case 3: // Release
                envVal *= std::exp (-dt / std::max (0.005f, params.smpR));
                if (envVal < 0.001f) { envVal = 0; playing = false; envStage = 4; }
                break;
            default:
                break;
        }
        return envVal;
    }

    // Filter ADSR
    int filtEnvStage = 0;
    float filtEnvVal = 0;

    float runFilterADSR (float dt)
    {
        float fA = params.smpFiltA;
        float fD = params.smpFiltD;
        float fS = params.smpFiltS;
        float fR = params.smpFiltR;
        switch (filtEnvStage)
        {
            case 0:
                filtEnvVal += dt / std::max (0.0005f, fA);
                if (filtEnvVal >= 1.0f) { filtEnvVal = 1.0f; filtEnvStage = 1; }
                break;
            case 1:
                filtEnvVal -= (filtEnvVal - fS) * (1.0f - std::exp (-dt / std::max (0.005f, fD)));
                if (filtEnvVal <= fS + 0.001f) { filtEnvVal = fS; filtEnvStage = 2; }
                break;
            case 2:
                filtEnvVal = fS;
                break;
            case 3:
                filtEnvVal *= std::exp (-dt / std::max (0.005f, fR));
                if (filtEnvVal < 0.001f) filtEnvVal = 0.0f;
                break;
            default: break;
        }
        return filtEnvVal;
    }

    // Multi-model filter (6 models: CLN/ACD/DRT/SEM/ARP/LQD)
    MultiModelFilterCh filterL, filterR;

    // Snap to nearest zero crossing (within ±64 samples) to avoid clicks
    static int snapToZero (const float* data, int len, int pos)
    {
        int best = pos;
        float bestAbs = 999.0f;
        int range = 64;
        int lo = std::max (0, pos - range);
        int hi = std::min (len - 1, pos + range);
        for (int i = lo; i < hi; ++i)
        {
            // Look for sign change (true zero crossing)
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

    // Cubic Hermite interpolation
    static float cubicInterp (const float* data, int len, double pos)
    {
        int idx = static_cast<int>(pos);
        float frac = static_cast<float>(pos - idx);

        auto getSample = [&](int i) -> float {
            i = std::clamp (i, 0, len - 1);
            return data[i];
        };

        float y0 = getSample (idx - 1);
        float y1 = getSample (idx);
        float y2 = getSample (idx + 1);
        float y3 = getSample (idx + 2);

        // Hermite interpolation
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }
};
