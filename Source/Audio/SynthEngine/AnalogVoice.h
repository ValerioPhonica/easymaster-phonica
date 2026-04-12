#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../../Sequencer/TrackState.h"
#include <cmath>
#include <algorithm>
#include <array>

// ═══════════════════════════════════════════════════════════════════
// SynthVoiceParams — unified param block passed at note trigger
// Used by both AnalogVoice and FM4OpVoice
// ═══════════════════════════════════════════════════════════════════
struct SynthVoiceParams
{
    // Oscillators
    int    w1 = 0;          // 0=saw, 1=square, 2=tri, 3=sine, 4=pwm
    int    w2 = 1;
    float  tune      = 0.0f;     // semitones
    float  detune    = -0.1f;    // semitones (osc2 vs osc1)
    float  mix2      = 0.4f;     // 0-1
    float  subLevel  = 0.3f;     // 0-1

    // Osc sync
    bool   oscSync   = false;
    float  syncRatio = 2.0f;     // slave freq multiplier

    // PWM
    float  pwm       = 0.5f;     // 0.05 - 0.95

    // Unison
    int    unison    = 1;        // 1-16
    float  uniSpread = 0.2f;     // total spread in semitones (detune)
    float  uniStereo = 0.0f;     // 0-1 stereo spread of unison voices

    // Filter
    int    fType     = 0;        // 0=LP, 1=HP, 2=BP
    int    fPoles    = 12;       // 6, 12, 24 dB/oct
    int    fModel    = 0;        // 0=CLEAN (SVF), 1=ACID (303), 2=DIRTY (driven SVF), 3=SEM (Oberheim), 4=ARP (2600), 5=LIQUID (Ripples/Jupiter)
    float  cut       = 75.0f;    // 0-100 (mapped exponentially to Hz)
    float  res       = 0.25f;    // 0-1
    float  fenv      = 0.4f;     // filter env depth

    // Filter ADSR
    float  fA = 0.01f, fD = 0.3f, fS = 0.5f, fR = 0.3f;

    // Amp ADSR
    float  aA = 0.02f, aD = 0.4f, aS = 0.7f, aR = 0.3f;

    float  volume = 0.6f;

    // Character: analog coloring (0=warm, 1=fold, 2=fractal)
    int    charType  = 0;
    float  charAmt   = 0.0f;

    // Linear FM modulator
    float  fmLinAmt   = 0.0f;     // 0-100 depth
    float  fmLinRatio = 2.0f;     // mod/carrier ratio
    float  fmLinDecay = 0.3f;     // mod envelope decay
    float  fmLinSustain = 0.0f;   // 0-1 sustain level
    int    fmLinSnap  = 1;        // 0=FREE (atonal), 1=INT (tonal)

    // FM 4-Op params (per-operator control)
    int    fmAlgo     = 0;       // 0-7
    float  cRatio = 1.0f;       // OP1 carrier ratio
    float  cLevel = 1.0f;       // OP1 output level
    float  r2 = 2.0f, l2 = 50.0f, dc2 = 0.3f;   // OP2: ratio, level, decay
    float  r3 = 3.0f, l3 = 30.0f, dc3 = 0.2f;   // OP3
    float  r4 = 4.0f, l4 = 20.0f, dc4 = 0.15f;  // OP4
    float  fmFeedback = 0;
    float  cA = 0.01f, cD = 0.5f, cS = 0.6f, cR = 0.4f; // carrier ADSR

    // Elements modal synthesis params
    float  elemBow = 0.0f, elemBlow = 0.0f, elemStrike = 0.8f;
    float  elemContour = 0.5f, elemMallet = 0.5f, elemFlow = 0.5f;
    float  elemGeometry = 0.25f, elemBright = 0.5f;
    float  elemDamping = 0.5f, elemPosition = 0.3f, elemSpace = 0.3f;
    float  elemPitch = 0.5f;

    // Plaits multi-model params
    int    plaitsModel = 0;         // 0-7
    float  plaitsHarmonics = 0.5f;
    float  plaitsTimbre = 0.5f;
    float  plaitsMorph = 0.0f;
    float  plaitsDecay = 0.5f;
    float  plaitsLpgColor = 0.5f;

    // Sampler params
    float  smpStart = 0.0f, smpEnd = 1.0f, smpGain = 1.0f;
    int    smpLoop = 0, smpReverse = 0, smpPlayMode = 0;
    float  smpTune = 0.0f, smpFine = 0.0f;
    float  smpA = 0.001f, smpD = 0.3f, smpS = 1.0f, smpR = 0.1f;
    float  smpCut = 100.0f, smpRes = 0.0f;
    int    smpFType = 0;
    int    smpFModel = 0;      // 0=CLN, 1=ACD, 2=DRT, 3=SEM, 4=ARP, 5=LQD
    int    smpFPoles = 12;     // 6, 12, 24 dB/oct
    int    smpRootNote = 60;
    float  smpStretch = 1.0f;    // 0.25-4.0
    int    smpWarp = 0;          // 0=off, 1=beats, 2=texture, 3=repitch
    float  smpBPM = 120.0f;
    float  smpFileSR = 44100.0f; // sample rate of loaded file
    int    smpBpmSync = 1;      // 0=INT, 1=DAW
    int    smpSyncMul = 0;     // -3..+3 rate multiplier
    int    smpBars = 0;        // 0=auto, 1-8 encoded bar count for grid sync
    // Sampler FM modulator
    float  smpFmAmt   = 0.0f;
    float  smpFmRatio = 2.0f;
    float  smpFmEnvA  = 0.001f;
    float  smpFmEnvD  = 0.3f;
    float  smpFmEnvS  = 0.0f;

    // Wavetable params
    float  wtPos1     = 0.0f;
    float  wtPos2     = 0.0f;
    float  wtMix      = 0.0f;
    int    wtWarp1    = 0;
    float  wtWarpAmt1 = 0.0f;
    int    wtWarp2    = 0;
    float  wtWarpAmt2 = 0.0f;
    float  wtSubLevel = 0.0f;

    // Granular
    float  grainPos     = 0.5f;
    float  grainSize    = 80.0f;
    float  grainDensity = 10.0f;
    float  grainSpray   = 0.0f;
    float  grainPitch   = 0.0f;
    float  grainPan     = 0.0f;
    int    grainShape   = 0;
    int    grainDir     = 0;
    float  grainTexture = 0.0f;
    bool   grainFreeze  = false;
    float  grainScan    = 0.0f;
    float  grainFmAmt   = 0.0f;
    float  grainFmRatio = 2.0f;
    float  grainFmDecay = 0.3f;
    float  grainFmSus   = 0.0f;
    int    grainFmSnap  = 1;
    float  grainFmSpread = 0.0f;
    int    grainMode      = 0;      // 0=STRD, 1=CLOUD, 2=SCRUB, 3=GLTCH, 4=FLUX
    float  grainTilt      = 50.0f;  // 0-100 envelope peak shift
    int    grainUniVoices  = 1;     // 1-8 unison voices
    float  grainUniDetune  = 0.0f;  // 0-100 cents detune
    float  grainUniStereo  = 0.0f;  // 0-1 stereo spread
    int    grainQuantize   = 0;     // 0=OFF, 1=OCT, 2=5TH, 3=TRAD, 4=SCAL
    float  grainFeedback   = 0.0f;  // 0-1 output→input feedback

    // Sampler filter envelope
    float  smpFiltEnv = 0.0f;       // -100..+100 filter envelope amount
    float  smpFiltA = 0.001f, smpFiltD = 0.3f, smpFiltS = 0.0f, smpFiltR = 0.1f;
};

// ═══════════════════════════════════════════════════════════════════
// AnalogVoice PRO — Professional subtractive synth voice
//
// DSP chain: PolyBLEP Osc1 + PolyBLEP Osc2 + Sub → SVF Filter → Amp
//
// ■ PolyBLEP anti-aliased saw, square, triangle, sine, PWM
// ■ Hard oscillator sync (slave resets on master zero-crossing)
// ■ Unison detune 1–7 voices with symmetric spread
// ■ Sub oscillator (sine, -1 oct)
// ■ SVF multimode filter (LP/HP/BP), cascadable 6/12/24 dB/oct
//   Topology-Preserving Transform — unconditionally stable
// ■ Exponential ADSR envelopes for amplitude and filter
// ■ Analog-style soft clipping
// ═══════════════════════════════════════════════════════════════════
class AnalogVoice
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        dtSec = 1.0 / sr;
        paramSmooth = 1.0f - std::exp (-1.0f / (static_cast<float>(sr) * 0.002f)); // 2ms τ
        reset();
    }

    void reset()
    {
        for (auto& p : uniPhase1) p = 0.0;
        for (auto& p : uniPhase2) p = 0.0;
        phaseSub = 0.0;
        masterPhase = 0.0;
        fmLinPhase = 0.0;
        ampEnvVal = 0.0f;
        filtEnvVal = 0.0f;
        ampStage = filtStage = 0;
        svfL1 = {}; svfL2 = {}; svfR1 = {}; svfR2 = {};
        ladderL.reset(); ladderR.reset();
        semL.reset(); semR.reset();
        arpL.reset(); arpR.reset();
        liquidL.reset(); liquidR.reset();
        playing = false;
        samplesPlayed = gateSamples = 0;
    }

    void noteOn (int noteIdx, int octave, float velocity, const SynthVoiceParams& p,
                 float gateDuration = 0.2f)
    {
        bool wasPlaying = playing && ampEnvVal > 0.001f;

        params = p;
        // Only reset smooth params on FRESH trigger — on retrigger, let paramSmooth
        // in renderBlock gradually move to new targets (avoids filter/volume click)
        if (!wasPlaying)
        {
            smoothCut = p.cut; smoothRes = p.res; smoothVol = p.volume; smoothFenv = p.fenv;
            smoothCharAmt = p.charAmt;
        }
        vel = std::clamp (velocity, 0.0f, 1.0f);
        baseFreq = noteToHz (noteIdx, octave) * std::pow (2.0f, params.tune / 12.0f);
        baseFreq = std::clamp (baseFreq, 8.0f, 20000.0f);

        if (wasPlaying)
        {
            // Retrigger: micro-glide (2ms) to avoid pitch discontinuity click
            // currentFreq stays at old value, glide smooths to new baseFreq
            glideCoeff = std::exp (-1.0f / (static_cast<float>(sampleRate) * 0.002f));
        }
        else
        {
            currentFreq = baseFreq;
            glideCoeff = 0.0f;
        }

        playing = true;
        hasPlocks = false;  // reset on new note — p-lock set separately after noteOn
        ampStage = filtStage = 0;
        killFadeSamples = 0;

        // Anti-click on retrigger: keep existing envelope value, let attack ramp from there
        if (!wasPlaying)
        {
            ampEnvVal = 0.0f;
            filtEnvVal = 0.0f;
        }
        // else: ampEnvVal/filtEnvVal stay at their current value — attack ramps from there

        samplesPlayed = 0;
        gateSamples = std::max (64, static_cast<int>(gateDuration * sampleRate));

        // Randomize unison start phases only on FRESH trigger (not retrigger)
        // Retrigger keeps phases continuous → no discontinuity → no click
        if (!wasPlaying)
        {
            auto& rng = juce::Random::getSystemRandom();
            for (int u = 0; u < kMaxUnison; ++u)
            {
                uniPhase1[u] = rng.nextDouble();
                uniPhase2[u] = rng.nextDouble();
            }
            phaseSub = 0.0;
            masterPhase = 0.0;
            fmLinPhase = 0.0;
        }

        // Gentle filter state reset — less aggressive on retrigger to avoid click
        float resetMul = wasPlaying ? 0.3f : 0.01f;
        svfL1.ic1eq *= resetMul; svfL1.ic2eq *= resetMul;
        svfR1.ic1eq *= resetMul; svfR1.ic2eq *= resetMul;
        svfL2.ic1eq *= resetMul; svfL2.ic2eq *= resetMul;
        svfR2.ic1eq *= resetMul; svfR2.ic2eq *= resetMul;
        // Ladder/SEM/ARP/Liquid: only soft reset on retrigger
        if (!wasPlaying)
        {
            ladderL.reset(); ladderR.reset();
            semL.reset(); semR.reset();
            arpL.reset(); arpR.reset();
            liquidL.reset(); liquidR.reset();
        }
    }

    bool isPlaying() const { return playing; }
    bool isKilling() const { return playing && killFadeSamples > 0; }
    bool isGateActive() const { return playing && ampStage < 3; }
    void releaseGate() { if (playing && ampStage < 3) ampStage = filtStage = 3; }
    void kill()
    {
        // Quick anti-click fade instead of instant zero
        if (ampEnvVal > 0.001f)
            killFadeSamples = 256; // ~6ms at 44.1kHz — longer fade for smoother loop transitions
        else
        {
            playing = false;
            ampEnvVal = filtEnvVal = 0.0f;
        }
    }

    // Glide: set starting freq for portamento on new note
    void setGlide (float fromFreq, float gTime)
    {
        glideFromFreq = fromFreq;
        glideCoeff = (gTime > 0.001f) ? std::exp (-1.0f / (static_cast<float>(sampleRate) * gTime)) : 0.0f;
        currentFreq = fromFreq;
    }

    // Legato glide: change target freq without re-triggering envelope
    void setGlideTarget (float targetFreq, float gTime)
    {
        baseFreq = std::clamp (targetFreq, 8.0f, 20000.0f);
        glideCoeff = (gTime > 0.001f) ? std::exp (-1.0f / (static_cast<float>(sampleRate) * gTime)) : 0.0f;
    }

    // Update params without re-triggering envelope (for legato)
    void updateParams (const SynthVoiceParams& newParams)
    {
        if (hasPlocks) return;  // p-locked voice keeps trigger-time params
        params = newParams;
    }

    // Live state pointer for real-time LFO modulation (bypasses struct copy)
    void setLiveState (const SynthTrackState* s) { if (!hasPlocks) liveState = s; }
    const SynthTrackState* liveState = nullptr;
    float smoothCut = 75.0f, smoothRes = 0.25f, smoothVol = 0.6f, smoothFenv = 0.4f;
    float smoothCharAmt = 0.0f;
    bool hasPlocks = false;  // when true, skip live param updates (Elektron-style)
    void setPlocked() { hasPlocks = true; }

    // Extend gate without re-triggering envelope from zero
    // If voice is in release (ampStage == 3), reopen to sustain
    void updateGate (float gateDuration)
    {
        int newGate = std::max (64, static_cast<int>(gateDuration * sampleRate));
        if (newGate > gateSamples - samplesPlayed)
            gateSamples = samplesPlayed + newGate;
        // Reopen gate if in release → jump back to sustain stage
        if (ampStage >= 3)  ampStage = 2;  // sustain
        if (filtStage >= 3) filtStage = 2;
    }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        if (!playing) return;

        // Hard timeout: 10 seconds max per voice
        if (samplesPlayed > static_cast<int>(sampleRate * 10.0)) { kill(); return; }

        // ── Glide: 303/Moog-style portamento in log-frequency space ──
        // Constant rate in semitones/sec — sounds musical regardless of interval
        if (glideCoeff > 0.0001f && std::abs (currentFreq - baseFreq) > 0.1f)
        {
            float logCurrent = std::log2 (currentFreq);
            float logTarget = std::log2 (baseFreq);
            float alpha = std::pow (glideCoeff, static_cast<float>(numSamples));
            float logNew = logTarget + (logCurrent - logTarget) * alpha;
            currentFreq = std::pow (2.0f, logNew);
        }
        else
        {
            currentFreq = baseFreq;
            glideCoeff = 0.0f;
        }
        float renderFreq = currentFreq;

        const int nUni = std::clamp (params.unison, 1, kMaxUnison);
        const float uniGain = 1.0f / std::sqrt (static_cast<float>(nUni));
        const float osc2ratio = std::pow (2.0f, params.detune / 12.0f);
        const float subFreq = renderFreq * 0.5f;
        const float fdt = static_cast<float>(dtSec);
        const bool doSync = params.oscSync && params.syncRatio > 1.01f;
        const float stereoW = params.uniStereo; // 0=mono, 1=full stereo

        // Pre-calc per-voice pan gains
        std::array<float, kMaxUnison> voicePanL {}, voicePanR {};
        for (int u = 0; u < nUni; ++u)
        {
            float pan = 0.0f; // center
            if (nUni > 1)
            {
                float t = static_cast<float>(u) / static_cast<float>(nUni - 1);
                pan = (t - 0.5f) * 2.0f * stereoW; // -stereoW to +stereoW
            }
            float angle = (pan + 1.0f) * 0.25f * 3.14159f;
            voicePanL[u] = std::cos (angle);
            voicePanR[u] = std::sin (angle);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (ampStage < 3 && samplesPlayed >= gateSamples)
                ampStage = filtStage = 3;

            // ── Linear FM modulator (phase modulation) ──
            float fmPhaseMod = 0.0f;
            if (params.fmLinAmt > 0.01f)
            {
                float ratio = params.fmLinRatio;
                if (params.fmLinSnap > 0)
                    ratio = std::round (ratio);
                ratio = std::max (0.25f, ratio);
                float modFreq = renderFreq * ratio;
                float modSig = std::sin (static_cast<float>(fmLinPhase * 6.283185307));
                fmLinPhase += static_cast<double>(modFreq) * dtSec;
                if (fmLinPhase >= 1.0) fmLinPhase -= 1.0;

                // Decay→Sustain envelope: decays from 1.0 to sustain level
                float sus = std::clamp (params.fmLinSustain, 0.0f, 1.0f);
                float rawDecay = std::exp (-static_cast<float>(samplesPlayed) * fdt
                    / std::max (0.01f, params.fmLinDecay));
                float fmEnv = sus + (1.0f - sus) * rawDecay;

                // Smooth amount to prevent clicks on knob changes
                float targetAmt = params.fmLinAmt / 100.0f;
                fmLinAmtSmooth += (targetAmt - fmLinAmtSmooth) * 0.002f; // ~20ms smooth

                // Phase modulation depth: smoothed amount × envelope × 0.8 max
                fmPhaseMod = modSig * fmLinAmtSmooth * 0.8f * fmEnv;
            }
            else
            {
                fmLinAmtSmooth *= 0.99f; // fade out smoothly when turned off
                if (fmLinAmtSmooth > 0.001f)
                {
                    float modSig = std::sin (static_cast<float>(fmLinPhase * 6.283185307));
                    fmPhaseMod = modSig * fmLinAmtSmooth * 0.8f;
                    float ratio = params.fmLinRatio;
                    if (params.fmLinSnap > 0) ratio = std::round (ratio);
                    fmLinPhase += static_cast<double>(renderFreq * std::max (0.25f, ratio)) * dtSec;
                    if (fmLinPhase >= 1.0) fmLinPhase -= 1.0;
                }
            }

            // ── Unison oscillator stack → stereo ──
            float oscSumL = 0.0f, oscSumR = 0.0f;

            for (int u = 0; u < nUni; ++u)
            {
                float uniDet = 0.0f;
                if (nUni > 1)
                {
                    float t = static_cast<float>(u) / static_cast<float>(nUni - 1);
                    uniDet = (t - 0.5f) * params.uniSpread;
                }

                float f1 = renderFreq * std::pow (2.0f, uniDet / 12.0f);
                float f2 = f1 * osc2ratio;

                bool syncNow = false;
                if (doSync && u == 0)
                {
                    double mInc = static_cast<double>(renderFreq) * dtSec;
                    masterPhase += mInc;
                    if (masterPhase >= 1.0)
                    {
                        masterPhase -= std::floor (masterPhase);
                        syncNow = true;
                    }
                }
                if (syncNow)
                {
                    uniPhase1[u] = masterPhase * static_cast<double>(params.syncRatio);
                    uniPhase1[u] -= std::floor (uniPhase1[u]);
                }

                float oscF1 = doSync ? (f1 * params.syncRatio) : f1;
                double inc1 = static_cast<double>(oscF1) * dtSec;
                double inc2 = static_cast<double>(f2) * dtSec;

                double pm1 = uniPhase1[u] + static_cast<double>(fmPhaseMod);
                pm1 -= std::floor (pm1); // always 0-1
                double pm2 = uniPhase2[u] + static_cast<double>(fmPhaseMod * 0.5f);
                pm2 -= std::floor (pm2);
                float o1 = polyBlepOsc (pm1, inc1, params.w1, params.pwm);
                float o2 = polyBlepOsc (pm2, inc2, params.w2, params.pwm);
                float voiceSig = (o1 + o2 * params.mix2) * uniGain;

                // Pan this voice
                oscSumL += voiceSig * voicePanL[u];
                oscSumR += voiceSig * voicePanR[u];

                uniPhase1[u] += inc1;
                if (uniPhase1[u] >= 1.0) uniPhase1[u] -= 1.0;
                uniPhase2[u] += inc2;
                if (uniPhase2[u] >= 1.0) uniPhase2[u] -= 1.0;
            }

            // Sub oscillator (mono, center)
            float sub = std::sin (static_cast<float>(phaseSub * 6.283185307179586));
            phaseSub += static_cast<double>(subFreq) * dtSec;
            if (phaseSub >= 1.0) phaseSub -= 1.0;
            float subSig = sub * params.subLevel;
            oscSumL += subSig * 0.5f;
            oscSumR += subSig * 0.5f;

            // Headroom
            oscSumL *= 0.45f;
            oscSumR *= 0.45f;

            // ── Filter — true stereo with model selection ──
            // ── FILTER — modulated values from params (LFO/MSEG/ARP applied via SVP) ──
            filtEnvVal = runADSR (filtEnvVal, filtStage, params.fA, params.fD, params.fS, params.fR, fdt);
            float targetCut = params.cut;
            float targetRes = params.res;
            float targetFenv = params.fenv;
            float targetVol = params.volume;
            // Per-sample smoothing (τ = 2ms, cached in prepare())
            smoothCut  += (targetCut  - smoothCut)  * paramSmooth;
            smoothRes  += (targetRes  - smoothRes)  * paramSmooth;
            smoothFenv += (targetFenv - smoothFenv) * paramSmooth;
            smoothVol  += (targetVol  - smoothVol)  * paramSmooth;
            float cutHz = 20.0f * std::pow (2.0f, (smoothCut * 0.01f) * 10.0f);
            // Envelope modulation in OCTAVE space (like real analog synths)
            // fenv=0.5 → 3.5 octaves, fenv=1.0 → 7 octaves of sweep
            float envOctaves = smoothFenv * filtEnvVal * 7.0f;
            float modCut = cutHz * std::pow (2.0f, envOctaves);
            modCut = std::clamp (modCut, 16.0f, std::min (18000.0f, static_cast<float>(sampleRate) * 0.45f));
            float Q = 0.5f + smoothRes * 18.0f;

            float sL, sR;

            if (params.fModel == 1)
            {
                // ACID — TB-303 diode ladder (4-pole LP, self-oscillating)
                sL = runLadder303 (oscSumL, modCut, smoothRes, ladderL);
                sR = runLadder303 (oscSumR, modCut, smoothRes, ladderR);
            }
            else if (params.fModel == 3)
            {
                // SEM — Oberheim-style 2-pole SVF with continuous morph
                // fType: 0=LP, 1=HP, 2=BP, 3=Notch
                sL = runSEM (oscSumL, modCut, smoothRes, params.fType, semL);
                sR = runSEM (oscSumR, modCut, smoothRes, params.fType, semR);
            }
            else if (params.fModel == 4)
            {
                // ARP — ARP 2600 aggressive 4-pole ladder
                sL = runARP2600 (oscSumL, modCut, smoothRes, arpL);
                sR = runARP2600 (oscSumR, modCut, smoothRes, arpR);
            }
            else if (params.fModel == 5)
            {
                // LIQUID — Ripples/Jupiter VCA ladder, resonance-compensated
                sL = runLiquid (oscSumL, modCut, smoothRes, params.fType, params.fPoles, liquidL);
                sR = runLiquid (oscSumR, modCut, smoothRes, params.fType, params.fPoles, liquidR);
            }
            else
            {
                // CLEAN (0) or DIRTY (2)
                float driveIn = (params.fModel == 2) ? 1.5f : 1.0f;
                float inL = oscSumL * driveIn;
                float inR = oscSumR * driveIn;

                // First SVF pass (always runs = 12dB/oct minimum)
                sL = runSVF (inL, modCut, Q, params.fType, svfL1);
                sR = runSVF (inR, modCut, Q, params.fType, svfR1);

                if (params.fPoles >= 24)
                {
                    // 24dB/oct — cascade second SVF
                    sL = runSVF (sL, modCut, Q * 0.6f, params.fType, svfL2);
                    sR = runSVF (sR, modCut, Q * 0.6f, params.fType, svfR2);
                }
                else if (params.fPoles <= 6)
                {
                    // 6dB/oct — blend filtered with slightly less filtered
                    float s6L = runSVF (inL, modCut * 2.0f, Q * 0.3f, params.fType, svfL2);
                    float s6R = runSVF (inR, modCut * 2.0f, Q * 0.3f, params.fType, svfR2);
                    sL = s6L * 0.5f + sL * 0.5f;
                    sR = s6R * 0.5f + sR * 0.5f;
                }

                // DIRTY: soft saturation after filter
                if (params.fModel == 2)
                {
                    sL = std::tanh (sL * 1.2f);
                    sR = std::tanh (sR * 1.2f);
                }
            }

            // ── Character (smoothed to prevent clicks) ──
            smoothCharAmt += (params.charAmt - smoothCharAmt) * paramSmooth;
            if (smoothCharAmt > 0.01f)
            {
                sL = applyCharacter (sL, params.charType, smoothCharAmt);
                sR = applyCharacter (sR, params.charType, smoothCharAmt);
            }

            // ── Amp envelope ──
            ampEnvVal = runADSR (ampEnvVal, ampStage, params.aA, params.aD, params.aS, params.aR, fdt);

            // Anti-click kill fade
            float killGain = 1.0f;
            if (killFadeSamples > 0)
            {
                killGain = static_cast<float>(killFadeSamples) / 256.0f;
                --killFadeSamples;
                if (killFadeSamples == 0)
                {
                    playing = false;
                    break;
                }
            }

            sL = tanhClip (sL * ampEnvVal * vel * smoothVol * killGain);
            sR = tanhClip (sR * ampEnvVal * vel * smoothVol * killGain);

            outL[i] += sL;
            outR[i] += sR;
            ++samplesPlayed;

            // Periodic state sanitization (every 256 samples)
            if ((samplesPlayed & 255) == 0)
            {
                auto flush = [](float& v) { if (!std::isfinite(v) || std::abs(v) < 1e-18f) v = 0.0f; };
                flush (svfL1.ic1eq); flush (svfL1.ic2eq);
                flush (svfR1.ic1eq); flush (svfR1.ic2eq);
                flush (svfL2.ic1eq); flush (svfL2.ic2eq);
                flush (svfR2.ic1eq); flush (svfR2.ic2eq);
                flush (ladderL.s1); flush (ladderL.s2); flush (ladderL.s3); flush (ladderL.s4);
                flush (ladderR.s1); flush (ladderR.s2); flush (ladderR.s3); flush (ladderR.s4);
                flush (semL.lp); flush (semL.bp); flush (semL.hp);
                flush (semR.lp); flush (semR.bp); flush (semR.hp);
                flush (arpL.s1); flush (arpL.s2); flush (arpL.s3); flush (arpL.s4);
                flush (arpR.s1); flush (arpR.s2); flush (arpR.s3); flush (arpR.s4);
                flush (ampEnvVal); flush (filtEnvVal);
            }

            if (ampStage == 3 && ampEnvVal < 0.0001f)
            {
                playing = false;
                break;
            }

            // Hard timeout: max 10 seconds per voice
            if (samplesPlayed > static_cast<int>(sampleRate * 10.0))
            {
                kill();
                break;
            }
        }
    }

private:
    static constexpr int kMaxUnison = 16;

    double sampleRate = 44100.0;
    double dtSec = 1.0 / 44100.0;
    float  paramSmooth = 0.02f;   // cached 2ms smoothing coefficient
    SynthVoiceParams params;
    float baseFreq = 440.0f;
    float vel = 0.8f;
    bool playing = false;
    float glideFromFreq = 440.0f;
    float glideCoeff = 0.0f;
    float currentFreq = 440.0f;

    // Oscillator phases
    std::array<double, kMaxUnison> uniPhase1 {}, uniPhase2 {};
    double phaseSub = 0.0, masterPhase = 0.0;
    double fmLinPhase = 0.0; // linear FM modulator phase
    float  fmLinAmtSmooth = 0.0f; // smoothed FM amount (anti-click)

    // Envelopes
    float ampEnvVal = 0.0f, filtEnvVal = 0.0f;
    int ampStage = 0, filtStage = 0;
    int killFadeSamples = 0;
    int samplesPlayed = 0, gateSamples = 0;

    // SVF filter states — stereo (L/R) × 2 (for 24dB cascade)
    struct SVFState { float ic1eq = 0.0f, ic2eq = 0.0f; };
    SVFState svfL1, svfL2, svfR1, svfR2;

    // 303-style ladder filter state (4-pole diode ladder)
    struct LadderState {
        float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
        float prevFeedback = 0.0f;
        void reset() { s1 = s2 = s3 = s4 = prevFeedback = 0.0f; }
    };
    LadderState ladderL, ladderR;

    // SEM (Oberheim) state — 2-pole SVF with continuous morphing
    struct SEMState {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        void reset() { lp = bp = hp = 0.0f; }
    };
    SEMState semL, semR;

    // ARP 2600 ladder state — aggressive 4-pole with different saturation
    struct ARPState {
        float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
        void reset() { s1 = s2 = s3 = s4 = 0.0f; }
    };
    ARPState arpL, arpR;

    // LIQUID — Mutable Ripples-inspired VCA ladder (V2164 + OTA character)
    struct LiquidState {
        float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
        void reset() { s1 = s2 = s3 = s4 = 0.0f; }
    };
    LiquidState liquidL, liquidR;

    // ── Helpers ──
    static float noteToHz (int noteIdx, int octave)
    {
        int semi = noteIdx + (octave + 2) * 12;
        return 440.0f * std::pow (2.0f, (semi - 69.0f) / 12.0f);
    }

    // ═══════════════════════════════════════════════
    // PolyBLEP — removes aliasing at waveform discontinuities
    // Polynomial correction applied at the transition point
    // ═══════════════════════════════════════════════
    static inline float polyBlep (double t, double inc)
    {
        if (inc < 1e-12) return 0.0f;
        if (t < inc)
        {
            double x = t / inc;
            return static_cast<float>(x + x - x * x - 1.0);
        }
        if (t > 1.0 - inc)
        {
            double x = (t - 1.0) / inc;
            return static_cast<float>(x * x + x + x + 1.0);
        }
        return 0.0f;
    }

    static float polyBlepOsc (double phase, double inc, int wave, float pwmW)
    {
        float p = static_cast<float>(phase);
        switch (wave)
        {
            case 0: // SAW — PolyBLEP corrected
            {
                float v = 2.0f * p - 1.0f;
                v -= polyBlep (phase, inc);
                return v;
            }
            case 1: // SQUARE — PolyBLEP at both transitions
            {
                float v = (p < 0.5f) ? 1.0f : -1.0f;
                v += polyBlep (phase, inc);
                v -= polyBlep (std::fmod (phase + 0.5, 1.0), inc);
                return v;
            }
            case 2: // TRIANGLE — naive (already nearly bandlimited)
                return 4.0f * std::abs (p - 0.5f) - 1.0f;
            case 3: // SINE
                return std::sin (p * 6.283185307f);
            case 4: // PWM — variable pulse width, PolyBLEP
            {
                float w = std::clamp (pwmW, 0.05f, 0.95f);
                float v = (p < w) ? 1.0f : -1.0f;
                v += polyBlep (phase, inc);
                v -= polyBlep (std::fmod (phase + static_cast<double>(1.0f - w), 1.0), inc);
                return v;
            }
            default: return 0.0f;
        }
    }

    // ═══════════════════════════════════════════════
    // SVF — Cytomic TPT (Topology-Preserving Transform)
    // Unconditionally stable, zero-delay feedback
    // ═══════════════════════════════════════════════
    float runSVF (float in, float cutHz, float Q, int type, SVFState& s)
    {
        float g = std::tan (3.14159265f * std::clamp (cutHz, 16.0f, static_cast<float>(sampleRate) * 0.49f)
                            / static_cast<float>(sampleRate));
        float k = 1.0f / std::max (0.5f, Q);
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;

        float v3 = in - s.ic2eq;
        float v1 = a1 * s.ic1eq + a2 * v3;
        float v2 = s.ic2eq + a2 * s.ic1eq + a3 * v3;

        s.ic1eq = 2.0f * v1 - s.ic1eq;
        s.ic2eq = 2.0f * v2 - s.ic2eq;

        // Hard clamp to prevent runaway
        s.ic1eq = std::clamp (s.ic1eq, -8.0f, 8.0f);
        s.ic2eq = std::clamp (s.ic2eq, -8.0f, 8.0f);
        if (!std::isfinite (s.ic1eq)) s.ic1eq = 0.0f;
        if (!std::isfinite (s.ic2eq)) s.ic2eq = 0.0f;

        switch (type)
        {
            case 0:  return v2;                   // LP
            case 1:  return in - k * v1 - v2;    // HP
            case 2:  return v1;                   // BP
            case 3:  return in - k * v1;          // Notch (LP + HP)
            default: return v2;
        }
    }

    // ═══════════════════════════════════════════════
    // TB-303 Diode Ladder — Stilson/Smith
    // 4 cascaded one-pole LP stages with nonlinear
    // feedback. Self-oscillation at high reso.
    // Resonance NARROWS and CUTS, never opens.
    // ═══════════════════════════════════════════════
    float runLadder303 (float in, float cutHz, float reso, LadderState& s)
    {
        float sr = static_cast<float>(sampleRate);
        cutHz = std::clamp (cutHz, 20.0f, sr * 0.45f);

        // Cutoff coefficient — standard Stilson/Smith
        float fc = cutHz / sr;
        float f = 2.0f * std::sin (3.14159265f * fc);
        f = std::clamp (f, 0.0001f, 0.99f);

        // Resonance LOWERS effective cutoff — 303 characteristic
        // At max reso, cutoff drops ~15%
        float fEff = f * (1.0f - reso * 0.15f);

        // Feedback: 4.0 = self-oscillation threshold
        float fb = reso * 3.95f;

        // Feedback with tanh saturation (prevents runaway)
        float fbSig = std::tanh (s.s4 * 1.1f);

        // Input (no drive multiplication — stays clean until saturated by fb)
        // ── Resonance compensation: boost input to match other filter volumes ──
        float comp = 1.0f + reso * 1.2f;
        float u = in * comp - fb * fbSig;

        // Diode-style saturation per stage: asymmetric, subtle
        // Positive signal clips gently, negative clips harder (diode forward/reverse)
        auto diode = [](float x) -> float {
            if (x >= 0.0f) return x / (1.0f + 0.5f * x);   // gentle positive
            return std::tanh (x * 1.2f);                     // harder negative
        };

        // 4 cascaded one-pole LP stages
        s.s1 += fEff * (diode (u)    - s.s1);
        s.s2 += fEff * (diode (s.s1) - s.s2);
        s.s3 += fEff * (diode (s.s2) - s.s3);
        s.s4 += fEff * (diode (s.s3) - s.s4);

        float out = s.s4;

        // Stability
        if (!std::isfinite (out)) { s.reset(); return 0.0f; }
        return std::clamp (out, -5.0f, 5.0f);
    }

    // ═══════════════════════════════════════════════
    // SEM — Oberheim SEM-style 2-pole State Variable
    // Warm, round character. Continuous LP/BP/HP/Notch.
    // Lower resonance ceiling than ladder — musical, not screamy.
    // Soft clipping in the integrators (tube-like warmth).
    // ═══════════════════════════════════════════════
    float runSEM (float in, float cutHz, float reso, int type, SEMState& s)
    {
        float sr = static_cast<float>(sampleRate);
        float fc = std::clamp (cutHz, 16.0f, sr * 0.49f);

        // SEM coefficient — bilinear warped
        float f = 2.0f * std::sin (3.14159265f * fc / sr);
        f = std::clamp (f, 0.0001f, 0.99f);

        // SEM resonance — gentler than ladder, max ~3.2
        float q = 1.0f - reso * 0.95f;
        q = std::max (0.05f, q);
        float damp = q * 2.0f;

        // ── Two-integrator SVF loop ──
        s.hp = in - s.lp - damp * s.bp;
        // Soft clip the BP integrator (SEM warmth)
        s.bp += f * std::tanh (s.hp * 0.8f);
        s.lp += f * s.bp;

        // Clamp for stability
        s.lp = std::clamp (s.lp, -8.0f, 8.0f);
        s.bp = std::clamp (s.bp, -8.0f, 8.0f);
        if (!std::isfinite (s.lp)) { s.reset(); return 0.0f; }

        switch (type)
        {
            case 0:  return s.lp;                    // LP — warm, round
            case 1:  return s.hp;                    // HP
            case 2:  return s.bp;                    // BP — vocal, resonant
            case 3:  return s.lp + s.hp;             // Notch (SEM specialty)
            default: return s.lp;
        }
    }

    // ═══════════════════════════════════════════════
    // ARP 2600 — Aggressive 4-pole ladder filter
    // Harder clipping than 303, more "buzzy" resonance.
    // Asymmetric soft-clipping gives odd harmonics.
    // Self-oscillates into a pure sine at high reso.
    // ═══════════════════════════════════════════════
    float runARP2600 (float in, float cutHz, float reso, ARPState& s)
    {
        float sr = static_cast<float>(sampleRate);
        float fc = std::clamp (cutHz / sr, 0.0001f, 0.497f);

        // ARP coefficient — slightly different curve from 303
        float g = 1.0f - std::exp (-2.0f * 3.14159265f * fc);

        // ARP resonance — higher ceiling, more aggressive (4.1x → screaming)
        float fb = reso * 4.1f;

        // ARP feedback: asymmetric clipping (adds odd harmonics)
        float fbSig = s.s4 * fb;
        fbSig = (fbSig > 0.0f) ? std::tanh (fbSig * 2.0f) : std::tanh (fbSig * 1.5f);

        // Hard input drive — ARP style
        float input = in * 1.2f - fbSig;
        input = std::clamp (input, -4.0f, 4.0f);

        // 4 cascaded poles with per-stage hard clipping
        s.s1 += g * (input - s.s1);
        s.s1 = std::clamp (s.s1, -5.0f, 5.0f);

        s.s2 += g * (s.s1 - s.s2);
        s.s2 = std::clamp (s.s2, -5.0f, 5.0f);

        s.s3 += g * (s.s2 - s.s3);
        s.s3 = std::clamp (s.s3, -5.0f, 5.0f);

        s.s4 += g * (s.s3 - s.s4);
        s.s4 = std::clamp (s.s4, -5.0f, 5.0f);

        if (!std::isfinite (s.s4)) { s.reset(); return 0.0f; }

        return s.s4;
    }

    // ═══════════════════════════════════════════════
    // LIQUID — Mutable Ripples / Roland Jupiter-inspired
    // V2164 VCA ladder with OTA feedback saturation.
    //
    // Character: round, liquid resonance. Clean self-oscillation.
    // No loudness drop at high resonance (Roland-style compensation).
    // tanh() per-stage models VCA transfer function.
    // OTA soft-limit in feedback → pure sine self-osc.
    // Multimode from ladder taps: LP4, LP2 (s2), BP (s2-s4), HP (in-LP4).
    // ═══════════════════════════════════════════════
    float runLiquid (float in, float cutHz, float reso, int type, int poles, LiquidState& s)
    {
        float sr = static_cast<float>(sampleRate);
        cutHz = std::clamp (cutHz, 16.0f, sr * 0.45f);

        // ── Bilinear-warped integrator gain (accurate at high cutoff) ──
        float g = std::tan (3.14159265f * cutHz / sr);
        float G = g / (1.0f + g);

        // ── Resonance: wide musical sweep, clean self-oscillation ──
        // Jupiter/Ripples character: resonance "sings" with a pure sine.
        // Lower feedback gain = wider sweep before oscillation.
        float fb = reso * 3.8f;

        // ── Resonance compensation (Ripples-style: resonance adds, never subtracts) ──
        float comp = 1.0f + reso * reso * 2.0f;  // quadratic: subtle at low reso, strong at high

        // ── Feedback: OTA soft-limit for clean sine self-oscillation ──
        // Gentler than 303 diode — models LM13700 VCA in feedback path
        float fbSig = s.s4 / (1.0f + std::abs (s.s4 * 0.3f));  // rational soft clip — transparent at low signal

        float u = in * comp - fb * fbSig;

        // ── 4 cascaded VCA stages (V2164/Jupiter character) ──
        // KEY DIFFERENCE from 303: VERY gentle nonlinearity.
        // VCA saturation is symmetric, barely audible at normal levels.
        // This gives the "liquid" transparency — signal passes almost unchanged
        // until pushed hard, then smoothly compresses. No grit, no bite.
        auto vcaSat = [](float x) -> float {
            // Cubic soft-clip: linear up to ±1.0, then gentle compression
            // Much more transparent than tanh — the "liquid" character
            if (x > 1.3f) return 1.0f;
            if (x < -1.3f) return -1.0f;
            return x - (x * x * x) * 0.1f;
        };

        s.s1 += G * (vcaSat (u)    - s.s1);
        s.s2 += G * (vcaSat (s.s1) - s.s2);
        s.s3 += G * (vcaSat (s.s2) - s.s3);
        s.s4 += G * (vcaSat (s.s3) - s.s4);

        // Stability
        if (!std::isfinite (s.s4)) { s.reset(); return 0.0f; }

        // ── Multimode output from ladder taps ──
        float lp4 = s.s4;
        float lp2 = s.s2;
        float bp  = (s.s2 - s.s4) * 2.0f;  // bandpass from tap difference
        float hp  = u / std::max (1.0f, comp) - s.s4;  // HP = uncompensated input - LP4

        switch (type)
        {
            case 0:  return (poles >= 24) ? lp4 : lp2;  // LP
            case 1:  return hp;                          // HP
            case 2:  return bp;                          // BP
            default: return lp4;
        }
    }

    // ═══════════════════════════════════════════════
    static float runADSR (float val, int& stage, float a, float d, float s, float r, float fdt)
    {
        a = std::max (0.001f, a);
        d = std::max (0.001f, d);
        r = std::max (0.001f, r);

        switch (stage)
        {
            case 0: // Attack — linear ramp to 1.0
                val += fdt / a;
                if (val >= 1.0f) { val = 1.0f; stage = 1; }
                break;
            case 1: // Decay — exponential toward sustain
            {
                float tgt = std::clamp (s, 0.0f, 1.0f);
                val = tgt + (val - tgt) * std::exp (-fdt / (d * 0.35f));
                if (std::abs (val - tgt) < 0.001f) { val = tgt; stage = 2; }
                break;
            }
            case 2: // Sustain — hold
                val = std::clamp (s, 0.0f, 1.0f);
                break;
            case 3: // Release — exponential to zero
                val *= std::exp (-fdt / (r * 0.35f));
                break;
        }
        return val;
    }

    // ── Warm tanh soft clip ──
    static float tanhClip (float x)
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // ═══════════════════════════════════════════════
    // Character — type-based waveshaper
    //   Type 0 (WARM): tube-like asymmetric saturation
    //   Type 1 (FOLD): triangle-fold harmonics
    //   Type 2 (FRAC): iterative fractal fold
    // Amount controls intensity 0-1
    // ═══════════════════════════════════════════════
    static float applyCharacter (float x, int type, float amount)
    {
        float gain = 1.0f + amount * 4.0f;
        float sig = x * gain;

        switch (type)
        {
            case 0: // WARM — tube saturation with even harmonics
            {
                float drive = 1.0f + amount * 5.0f;
                sig = std::tanh (sig * drive) / std::max (0.1f, std::tanh (drive));
                // Slight asymmetry for even harmonics
                sig += sig * sig * amount * 0.1f;
                break;
            }
            case 1: // FOLD — wave folding
            {
                float folds = 1.0f + amount * 4.0f;
                sig *= folds;
                sig = std::asin (std::sin (sig * 1.5707963f)) * 0.6366f;
                break;
            }
            case 2: // FRACTAL — iterative fold with feedback
            {
                int iters = 2 + static_cast<int>(amount * 4.0f);
                for (int j = 0; j < iters; ++j)
                {
                    sig = std::sin (sig * 1.5707963f);
                    sig *= 1.0f + amount * 0.3f;
                }
                break;
            }
        }

        // Dry/wet crossfade
        return x * (1.0f - amount) + sig * amount;
    }
};
