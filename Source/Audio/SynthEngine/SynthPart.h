#pragma once
#include "AnalogVoice.h"
#include "FM4OpVoice.h"
#include "ElementsVoice.h"
#include "PlaitsVoice.h"
#include "SamplerVoice.h"
#include "GranularVoice.h"
#include "../../Sequencer/TrackState.h"
#include <array>

// ═══════════════════════════════════════════════════════════════════
// SynthPart — voice allocation with Mono/Legato/Glide support
//
// Poly mode: round-robin 8 voices
// Mono mode: single voice, new notes kill previous
// Legato mode: mono + no envelope re-trigger on overlapping notes
// Glide: pitch slides from previous note to new note
// ═══════════════════════════════════════════════════════════════════
class SynthPart
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& v : analogVoices)   v.prepare (sr);
        for (auto& v : fmVoices)       v.prepare (sr);
        for (auto& v : elementsVoices) v.prepare (sr);
        for (auto& v : plaitsVoices)   v.prepare (sr);
        for (auto& v : samplerVoices)  v.prepare (sr);
        for (auto& v : wtVoices)       v.prepare (sr);
        for (auto& v : granVoices)     v.prepare (sr);
    }

    // Set wavetable data for this part (called from SynthEngine per block)
    void setWTData (const WavetableData* d1, const WavetableData* d2) { wtDataPtr1 = d1; wtDataPtr2 = d2; }

    // Forward warp marker data to all sampler voices (call before noteOn)
    void setWarpData (const std::vector<TimeStretch::WarpPt>& pts, float totalBeats)
    {
        for (auto& sv : samplerVoices)
            sv.setWarpData (pts, totalBeats);
    }

    void setMonoGlide (bool monoOn, float glideTimeSec)
    {
        mono = monoOn;
        glideTime = glideTimeSec;
    }

    void noteOn (int noteIdx, int octave, float velocity, const SynthVoiceParams& params,
                 SynthModel model, float gateDuration = 0.2f,
                 std::shared_ptr<juce::AudioBuffer<float>> sampleBuf = nullptr,
                 float currentBPM = 120.0f)
    {
        int semi = noteIdx + (octave + 2) * 12;
        float newFreq = 440.0f * std::pow (2.0f, (semi - 69.0f + params.tune) / 12.0f);

        currentSampleBuf = sampleBuf; // store for legato re-triggers

        if (mono)
        {
            // 303-style mono behavior:
            // - SLIDE ON + voice still audible → LEGATO: glide pitch, don't retrigger envelope
            //   (works even if voice is in release — 303 ties notes across gaps)
            // - SLIDE OFF or no voice playing → RETRIGGER: fresh noteOn with envelope
            bool anyPlaying = isAnyVoicePlaying (model);
            bool doLegato = (glideTime > 0.001f && anyPlaying);

            if (doLegato)
            {
                // Legato: glide to new pitch, extend gate, no envelope retrigger
                float prevFreq = lastFreq;
                killAllExcept (model, monoVoiceIdx);
                legatoUpdate (model, monoVoiceIdx, noteIdx, octave, velocity, params, gateDuration, prevFreq);
            }
            else
            {
                // Fresh trigger: kill all, retrigger envelope from zero
                killAll();
                monoVoiceIdx = 0;
                // Mono retrigger: always glide from last freq to prevent click
                // If explicit glide is set → use full glide; otherwise → 2ms micro-glide
                float glideFrom = (anyPlaying && lastFreq > 1.0f) ? lastFreq : 0.0f;
                triggerVoice (model, 0, noteIdx, octave, velocity, params, gateDuration, glideFrom, sampleBuf, currentBPM);
            }

            lastFreq = newFreq;
            lastNote = noteIdx;
            lastOctave = octave;
        }
        else
        {
            int idx = findFreeVoice (model);
            triggerVoice (model, idx, noteIdx, octave, velocity, params, gateDuration, 0.0f, sampleBuf, currentBPM);
            lastFreq = newFreq;
            lastNote = noteIdx;
            lastOctave = octave;
        }
    }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        for (auto& v : analogVoices)
            if (v.isPlaying()) v.renderBlock (outL, outR, numSamples);
        for (auto& v : fmVoices)
            if (v.isPlaying()) v.renderBlock (outL, outR, numSamples);
        for (auto& v : elementsVoices)
            if (v.isPlaying()) v.renderBlock (outL, outR, numSamples);
        for (auto& v : plaitsVoices)
            if (v.isPlaying()) v.renderBlock (outL, outR, numSamples);
        for (auto& v : samplerVoices)
            if (v.isPlaying()) v.renderBlock (outL, outR, numSamples);
        for (auto& v : wtVoices)
            if (v.isPlaying()) v.renderBlock (outL, outR, numSamples);
        for (auto& v : granVoices)
            if (v.isPlaying()) v.renderBlock (outL, outR, numSamples);
    }

    // Push current (LFO-modulated) params to all active voices — called every block
    void updatePlayingVoices (const SynthVoiceParams& rawParams, SynthModel model,
                              const SynthTrackState* liveTrackState = nullptr)
    {
        // Pass params directly to voices — no intermediate smoothing
        // (AnalogVoice has its own per-sample smoothing for cut/res/vol,
        //  LFOEngine has adaptive output smoothing, block-rate knob changes are tiny)
        const auto& params = rawParams;
        // Kill voices of previous model when switching engines (prevents Elements reverb tail lingering etc.)
        if (model != lastActiveModel)
        {
            switch (lastActiveModel)
            {
                case SynthModel::Analog:   for (auto& v : analogVoices)   v.kill(); break;
                case SynthModel::FM:       for (auto& v : fmVoices)       v.kill(); break;
                case SynthModel::DWGS:     for (auto& v : elementsVoices) v.kill(); break;
                case SynthModel::Formant:  for (auto& v : plaitsVoices)   v.kill(); break;
                case SynthModel::Sampler:  for (auto& v : samplerVoices)  v.hardKill(); break;
                case SynthModel::Wavetable:for (auto& v : wtVoices)       v.kill(); break;
                case SynthModel::Granular: for (auto& v : granVoices)    v.kill(); break;
                default: break;
            }
        }
        lastActiveModel = model;

        for (auto& v : analogVoices)
        {
            if (v.isPlaying()) { v.updateParams (params); v.setLiveState (liveTrackState); }
        }
        for (auto& v : fmVoices)
            if (v.isPlaying()) v.updateParams (params);
        for (auto& v : samplerVoices)
            if (v.isPlaying()) { v.updateParams (params); v.setLiveState (liveTrackState); }

        // Wavetable: update data pointers + positions + warp from live state
        // CRITICAL: always update wavetable pointers on ALL playing voices —
        // the shared_ptr may have been replaced (e.g. prev/next browse)
        for (auto& v : wtVoices)
            if (v.isPlaying())
                v.setWavetables (wtDataPtr1, wtDataPtr2);

        if (liveTrackState != nullptr)
        {
            for (auto& v : wtVoices)
                if (v.isPlaying() && !v.hasPlocks)
                {
                    v.setPositions (liveTrackState->wtPos1, liveTrackState->wtPos2, liveTrackState->wtMix);
                    v.setWarp (static_cast<WarpMode>(liveTrackState->wtWarp1), liveTrackState->wtWarpAmt1,
                               static_cast<WarpMode>(liveTrackState->wtWarp2), liveTrackState->wtWarpAmt2);
                    v.updateLiveParams (liveTrackState->cut, liveTrackState->res, liveTrackState->fType,
                                        liveTrackState->fModel, liveTrackState->fPoles,
                                        liveTrackState->fenv,
                                        liveTrackState->fA, liveTrackState->fD, liveTrackState->fS, liveTrackState->fR,
                                        liveTrackState->aA, liveTrackState->aD, liveTrackState->aS, liveTrackState->aR,
                                        liveTrackState->volume, liveTrackState->wtSubLevel);
                }
        }

        // Elements needs param conversion
        ElementsParams ep;
        ep.bow = params.elemBow; ep.blow = params.elemBlow; ep.strike = params.elemStrike;
        ep.contour = params.elemContour; ep.mallet = params.elemMallet; ep.flow = params.elemFlow;
        ep.geometry = params.elemGeometry; ep.brightness = params.elemBright;
        ep.damping = params.elemDamping; ep.position = params.elemPosition;
        ep.space = params.elemSpace; ep.pitch = params.elemPitch;
        ep.volume = params.volume; ep.tune = params.tune;
        for (auto& v : elementsVoices)
            if (v.isPlaying()) v.updateParams (ep);

        // Plaits needs param conversion
        PlaitsParams pp;
        pp.model = params.plaitsModel; pp.harmonics = params.plaitsHarmonics;
        pp.timbre = params.plaitsTimbre; pp.morph = params.plaitsMorph;
        pp.decay = params.plaitsDecay; pp.lpgColor = params.plaitsLpgColor;
        pp.volume = params.volume; pp.tune = params.tune;
        pp.cut = params.cut; pp.res = params.res;
        pp.fType = params.fType; pp.fModel = params.fModel; pp.fPoles = params.fPoles;
        pp.fenv = params.fenv; pp.fA = params.fA; pp.fD = params.fD; pp.fS = params.fS; pp.fR = params.fR;
        for (auto& v : plaitsVoices)
            if (v.isPlaying()) v.updateParams (pp);

        // Granular: real-time param update for knob twists, LFO/MSEG, p-locks
        if (model == SynthModel::Granular)
        {
            GranularParams gp;
            gp.position = params.grainPos; gp.grainSize = params.grainSize;
            gp.density = params.grainDensity; gp.spray = params.grainSpray;
            gp.pitchSpread = params.grainPitch; gp.panSpread = params.grainPan;
            gp.shape = params.grainShape; gp.direction = params.grainDir;
            gp.texture = params.grainTexture; gp.freeze = params.grainFreeze;
            gp.scan = params.grainScan; gp.mode = params.grainMode;
            gp.tilt = params.grainTilt; gp.uniVoices = params.grainUniVoices;
            gp.uniDetune = params.grainUniDetune; gp.uniStereo = params.grainUniStereo; gp.quantize = params.grainQuantize;
            gp.feedback = params.grainFeedback;
            gp.fmAmt = params.grainFmAmt; gp.fmRatio = params.grainFmRatio;
            gp.fmDecay = params.grainFmDecay; gp.fmSustain = params.grainFmSus; gp.fmSnap = params.grainFmSnap;
            gp.fmSpread = params.grainFmSpread;
            gp.tune = params.tune; gp.volume = params.volume;
            gp.cut = params.cut; gp.res = params.res; gp.fType = params.fType;
            gp.fModel = params.fModel; gp.fPoles = params.fPoles;
            gp.fenv = params.fenv;
            gp.fA = params.fA; gp.fD = params.fD; gp.fS = params.fS; gp.fR = params.fR;
            gp.aA = params.aA; gp.aD = params.aD; gp.aS = params.aS; gp.aR = params.aR;
            for (auto& v : granVoices)
                if (v.isPlaying() && !v.hasPlocks)
                    v.updateParams (gp);
        }
    }

    void killAll()
    {
        for (auto& v : analogVoices)   v.kill();
        for (auto& v : fmVoices)       v.kill();
        for (auto& v : elementsVoices) v.kill();
        for (auto& v : plaitsVoices)   v.kill();
        for (auto& v : samplerVoices)  v.hardKill(); // immediate — prevents phasing on retrigger
        for (auto& v : wtVoices)       v.kill();
        for (auto& v : granVoices)     v.kill();
    }

    // Release gate on all active voices (triggers ADSR release, natural fade-out)
    void releaseAll()
    {
        for (auto& v : analogVoices)   v.releaseGate();
        for (auto& v : fmVoices)       v.releaseGate();
        for (auto& v : elementsVoices) v.releaseGate();
        for (auto& v : plaitsVoices)   v.releaseGate();
        for (auto& v : samplerVoices)  v.releaseGate();
        for (auto& v : wtVoices)       v.releaseGate();
        for (auto& v : granVoices)     v.releaseGate();
    }

    // Update grain visualization data for GUI (call after renderBlock)
    void updateGrainVis (SynthTrackState& st)
    {
        int total = 0;
        for (auto& v : granVoices)
        {
            if (v.isPlaying() && total < SynthTrackState::kMaxVisGrains)
            {
                GranularVoice::GrainVis buf[SynthTrackState::kMaxVisGrains];
                int n = v.getActiveGrainPositions (buf, SynthTrackState::kMaxVisGrains - total);
                for (int i = 0; i < n && total < SynthTrackState::kMaxVisGrains; ++i)
                    st.grainVisData[total++] = { buf[i].pos, buf[i].size, buf[i].amp,
                                                  buf[i].pan, buf[i].pitch, buf[i].reverse };
            }
        }
        st.grainVisCount = total;
    }

    // Update sampler playback position for GUI playhead
    void updateSmpPlayPos (SynthTrackState& st)
    {
        st.smpPlayPos = -1.0f; // no playhead by default
        for (auto& v : samplerVoices)
            if (v.isPlaying()) { st.smpPlayPos = v.getPlayPosition(); return; }
    }

    // Mark all currently playing voices as p-locked (won't be overwritten by live updates)
    void markPlocked()
    {
        for (auto& v : analogVoices)   if (v.isPlaying()) v.setPlocked();
        for (auto& v : fmVoices)       if (v.isPlaying()) v.setPlocked();
        for (auto& v : elementsVoices) if (v.isPlaying()) v.setPlocked();
        for (auto& v : plaitsVoices)   if (v.isPlaying()) v.setPlocked();
        for (auto& v : samplerVoices)  if (v.isPlaying()) v.setPlocked();
        for (auto& v : wtVoices)       if (v.isPlaying()) v.setPlocked();
        for (auto& v : granVoices)     if (v.isPlaying()) v.setPlocked();
    }

private:
    double sampleRate = 44100.0;
    bool mono = false;
    float glideTime = 0.0f;
    float lastFreq = 440.0f;
    int lastNote = 0, lastOctave = 3;
    int monoVoiceIdx = 0;
    std::shared_ptr<juce::AudioBuffer<float>> currentSampleBuf;

    static constexpr int kMaxVoices = 8;
    std::array<AnalogVoice, kMaxVoices>   analogVoices;
    std::array<FM4OpVoice, kMaxVoices>    fmVoices;
    std::array<ElementsVoice, kMaxVoices> elementsVoices;
    std::array<PlaitsVoice, kMaxVoices>   plaitsVoices;
    std::array<SamplerVoice, kMaxVoices>  samplerVoices;
    std::array<WavetableVoice, kMaxVoices> wtVoices;
    std::array<GranularVoice, kMaxVoices> granVoices;
    int analogStealIdx = 0, fmStealIdx = 0, elementsStealIdx = 0, plaitsStealIdx = 0, samplerStealIdx = 0, wtStealIdx = 0, granStealIdx = 0;
    SynthModel lastActiveModel = SynthModel::Analog;
    const WavetableData* wtDataPtr1 = nullptr;
    const WavetableData* wtDataPtr2 = nullptr;

    bool isAnyVoicePlaying (SynthModel model)
    {
        if (model == SynthModel::FM)
            { for (auto& v : fmVoices) if (v.isPlaying()) return true; }
        else if (model == SynthModel::DWGS)
            { for (auto& v : elementsVoices) if (v.isPlaying()) return true; }
        else if (model == SynthModel::Formant)
            { for (auto& v : plaitsVoices) if (v.isPlaying()) return true; }
        else if (model == SynthModel::Sampler)
            { for (auto& v : samplerVoices) if (v.isPlaying()) return true; }
        else if (model == SynthModel::Wavetable)
            { for (auto& v : wtVoices) if (v.isPlaying()) return true; }
        else if (model == SynthModel::Granular)
            { for (auto& v : granVoices) if (v.isPlaying()) return true; }
        else
            { for (auto& v : analogVoices) if (v.isPlaying()) return true; }
        return false;
    }

    // True only when a voice has its gate OPEN (A/D/S stages, not in Release)
    // Used for mono legato: gate active = legato glide, gate released = retrigger
    bool isAnyGateActive (SynthModel model)
    {
        if (model == SynthModel::FM)
            { for (auto& v : fmVoices) if (v.isGateActive()) return true; }
        else if (model == SynthModel::DWGS)
            { for (auto& v : elementsVoices) if (v.isGateActive()) return true; }
        else if (model == SynthModel::Formant)
            { for (auto& v : plaitsVoices) if (v.isGateActive()) return true; }
        else if (model == SynthModel::Sampler)
            { for (auto& v : samplerVoices) if (v.isGateActive()) return true; }
        else if (model == SynthModel::Wavetable)
            { for (auto& v : wtVoices) if (v.isGateActive()) return true; }
        else if (model == SynthModel::Granular)
            { for (auto& v : granVoices) if (v.isGateActive()) return true; }
        else
            { for (auto& v : analogVoices) if (v.isGateActive()) return true; }
        return false;
    }

    void killAllExcept (SynthModel model, int keepIdx)
    {
        if (model == SynthModel::FM)
            { for (int i = 0; i < kMaxVoices; ++i) if (i != keepIdx) fmVoices[i].kill(); }
        else if (model == SynthModel::DWGS)
            { for (int i = 0; i < kMaxVoices; ++i) if (i != keepIdx) elementsVoices[i].kill(); }
        else if (model == SynthModel::Formant)
            { for (int i = 0; i < kMaxVoices; ++i) if (i != keepIdx) plaitsVoices[i].kill(); }
        else if (model == SynthModel::Sampler)
            { for (int i = 0; i < kMaxVoices; ++i) if (i != keepIdx) samplerVoices[i].kill(); }
        else if (model == SynthModel::Wavetable)
            { for (int i = 0; i < kMaxVoices; ++i) if (i != keepIdx) wtVoices[i].kill(); }
        else if (model == SynthModel::Granular)
            { for (int i = 0; i < kMaxVoices; ++i) if (i != keepIdx) granVoices[i].kill(); }
        else
            { for (int i = 0; i < kMaxVoices; ++i) if (i != keepIdx) analogVoices[i].kill(); }
    }

    void triggerVoice (SynthModel model, int idx, int noteIdx, int octave, float velocity,
                       const SynthVoiceParams& params, float gateDuration, float glideFromFreq,
                       std::shared_ptr<juce::AudioBuffer<float>> sampleBuf = nullptr,
                       float currentBPM = 120.0f)
    {
        if (model == SynthModel::Sampler)
        {
            samplerVoices[idx].noteOn (noteIdx, octave, velocity, params, gateDuration, sampleBuf, currentBPM);
            return;
        }
        else if (model == SynthModel::Wavetable)
        {
            wtVoices[idx].noteOn (noteIdx, octave, velocity,
                wtDataPtr1, wtDataPtr2,
                params.wtPos1, params.wtPos2, params.wtMix,
                params.detune,
                std::max (1, params.unison), params.uniSpread, params.uniStereo,
                params.cut, params.res, params.fType,
                params.fModel, params.fPoles,
                params.fenv, params.fA, params.fD, params.fS, params.fR,
                params.aA, params.aD, params.aS, params.aR,
                gateDuration,
                static_cast<WarpMode>(params.wtWarp1), params.wtWarpAmt1,
                static_cast<WarpMode>(params.wtWarp2), params.wtWarpAmt2,
                params.wtSubLevel);
            return;
        }
        else if (model == SynthModel::Granular)
        {
            GranularParams gp;
            gp.position = params.grainPos; gp.grainSize = params.grainSize;
            gp.density = params.grainDensity; gp.spray = params.grainSpray;
            gp.pitchSpread = params.grainPitch; gp.panSpread = params.grainPan;
            gp.shape = params.grainShape; gp.direction = params.grainDir;
            gp.texture = params.grainTexture; gp.freeze = params.grainFreeze;
            gp.scan = params.grainScan; gp.mode = params.grainMode;
            gp.tilt = params.grainTilt; gp.uniVoices = params.grainUniVoices;
            gp.uniDetune = params.grainUniDetune; gp.uniStereo = params.grainUniStereo; gp.quantize = params.grainQuantize;
            gp.feedback = params.grainFeedback;
            gp.fmAmt = params.grainFmAmt; gp.fmRatio = params.grainFmRatio;
            gp.fmDecay = params.grainFmDecay; gp.fmSustain = params.grainFmSus; gp.fmSnap = params.grainFmSnap;
            gp.fmSpread = params.grainFmSpread;
            gp.tune = params.tune; gp.volume = params.volume;
            gp.cut = params.cut; gp.res = params.res; gp.fType = params.fType;
            gp.fModel = params.fModel; gp.fPoles = params.fPoles;
            gp.fenv = params.fenv;
            gp.fA = params.fA; gp.fD = params.fD; gp.fS = params.fS; gp.fR = params.fR;
            gp.aA = params.aA; gp.aD = params.aD; gp.aS = params.aS; gp.aR = params.aR;
            granVoices[idx].noteOn (noteIdx, octave, velocity, gp, gateDuration, sampleBuf);
            return;
        }
        else if (model == SynthModel::FM)
        {
            fmVoices[idx].noteOn (noteIdx, octave, velocity, params, gateDuration);
            if (glideFromFreq > 1.0f) fmVoices[idx].setGlide (glideFromFreq, std::max (0.002f, glideTime));
        }
        else if (model == SynthModel::DWGS)
        {
            ElementsParams ep;
            ep.bow = params.elemBow; ep.blow = params.elemBlow; ep.strike = params.elemStrike;
            ep.contour = params.elemContour; ep.mallet = params.elemMallet; ep.flow = params.elemFlow;
            ep.geometry = params.elemGeometry; ep.brightness = params.elemBright;
            ep.damping = params.elemDamping; ep.position = params.elemPosition;
            ep.space = params.elemSpace; ep.pitch = params.elemPitch;
            ep.volume = params.volume; ep.tune = params.tune;
            elementsVoices[idx].noteOn (noteIdx, octave, velocity, ep, gateDuration);
        }
        else if (model == SynthModel::Formant)
        {
            PlaitsParams pp;
            pp.model = params.plaitsModel; pp.harmonics = params.plaitsHarmonics;
            pp.timbre = params.plaitsTimbre; pp.morph = params.plaitsMorph;
            pp.decay = params.plaitsDecay; pp.lpgColor = params.plaitsLpgColor;
            pp.volume = params.volume; pp.tune = params.tune;
            pp.cut = params.cut; pp.res = params.res;
            pp.fType = params.fType; pp.fModel = params.fModel; pp.fPoles = params.fPoles;
            pp.fenv = params.fenv; pp.fA = params.fA; pp.fD = params.fD; pp.fS = params.fS; pp.fR = params.fR;
            plaitsVoices[idx].noteOn (noteIdx, octave, velocity, pp, gateDuration);
        }
        else
        {
            analogVoices[idx].noteOn (noteIdx, octave, velocity, params, gateDuration);
            if (glideFromFreq > 1.0f) analogVoices[idx].setGlide (glideFromFreq, std::max (0.002f, glideTime));
        }
    }

    // Legato: update freq + params without re-triggering envelope
    void legatoUpdate (SynthModel model, int idx, int noteIdx, int octave, float velocity,
                       const SynthVoiceParams& params, float gateDuration, float fromFreq)
    {
        int semi = noteIdx + (octave + 2) * 12;
        float targetFreq = 440.0f * std::pow (2.0f, (semi - 69.0f + params.tune) / 12.0f);

        if (model == SynthModel::Analog)
        {
            auto& v = analogVoices[idx];
            if (v.isPlaying())
            {
                v.setGlideTarget (targetFreq, glideTime);
                v.updateGate (gateDuration);
                v.updateParams (params); // update filter etc without re-trigger
            }
            else
                triggerVoice (model, idx, noteIdx, octave, velocity, params, gateDuration, fromFreq, currentSampleBuf);
        }
        else if (model == SynthModel::FM)
        {
            auto& v = fmVoices[idx];
            if (v.isPlaying())
            {
                v.setGlideTarget (targetFreq, glideTime);
                v.updateGate (gateDuration);
            }
            else
                triggerVoice (model, idx, noteIdx, octave, velocity, params, gateDuration, fromFreq, currentSampleBuf);
        }
        else
        {
            // Elements/Plaits: re-trigger but with glide time as fade
            triggerVoice (model, idx, noteIdx, octave, velocity, params, gateDuration, 0.0f, currentSampleBuf);
        }
    }

    int findFreeVoice (SynthModel model)
    {
        if (model == SynthModel::FM) return findFree (fmVoices, fmStealIdx);
        if (model == SynthModel::DWGS) return findFree (elementsVoices, elementsStealIdx);
        if (model == SynthModel::Formant) return findFree (plaitsVoices, plaitsStealIdx);
        if (model == SynthModel::Sampler) return findFree (samplerVoices, samplerStealIdx);
        if (model == SynthModel::Wavetable) return findFree (wtVoices, wtStealIdx);
        if (model == SynthModel::Granular) return findFree (granVoices, granStealIdx);
        return findFree (analogVoices, analogStealIdx);
    }

    template <typename VoiceArray>
    int findFree (VoiceArray& voices, int& stealIdx)
    {
        // 1st pass: find a completely free voice
        for (int i = 0; i < kMaxVoices; ++i)
            if (!voices[static_cast<size_t>(i)].isPlaying()) return i;
        // 2nd pass: prefer a voice that's already fading out (kill in progress)
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices[static_cast<size_t>(i)].isKilling()) return i;
        // 3rd: round-robin steal
        int r = stealIdx;
        stealIdx = (stealIdx + 1) % kMaxVoices;
        return r;
    }
};
