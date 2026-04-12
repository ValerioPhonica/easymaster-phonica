#pragma once
#include "StepData.h"
#include "../Audio/FX/MSEGEngine.h"
#include "../Audio/FX/ArpEngine.h"
#include "../Audio/SynthEngine/WavetableVoice.h"
#include "../Audio/Analysis/SampleAnalysis.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <string>
#include <atomic>
#include <array>
#include <memory>
#include <vector>

enum class DrumType : uint8_t
{
    Kick, Snare, HiHatClosed, HiHatOpen,
    Clap, Tom, TomHi, Cowbell, Rimshot, Crash
};

inline const char* drumName (DrumType t)
{
    switch (t)
    {
        case DrumType::Kick:        return "KICK";
        case DrumType::Snare:       return "SNARE";
        case DrumType::HiHatClosed: return "HH CLS";
        case DrumType::HiHatOpen:   return "HH OPN";
        case DrumType::Clap:        return "CLAP";
        case DrumType::Tom:         return "TOM";
        case DrumType::TomHi:       return "TOM HI";
        case DrumType::Cowbell:     return "COWBEL";
        case DrumType::Rimshot:     return "RIM";
        case DrumType::Crash:       return "CRASH";
        default:                    return "???";
    }
}

inline const char* drumId (DrumType t)
{
    switch (t)
    {
        case DrumType::Kick:        return "kick";
        case DrumType::Snare:       return "snare";
        case DrumType::HiHatClosed: return "hhc";
        case DrumType::HiHatOpen:   return "hho";
        case DrumType::Clap:        return "clap";
        case DrumType::Tom:         return "tom";
        case DrumType::TomHi:       return "tomhi";
        case DrumType::Cowbell:     return "cowbell";
        case DrumType::Rimshot:     return "rimshot";
        case DrumType::Crash:       return "crash";
        default:                    return "kick";
    }
}

// ── Sub-model names per drum type ──
// Returns {name, count} for the available synthesis variants.
// subModel: 0=CLASSIC, 1=ACOUSTIC, 2=808, 3=909, 4=707, 5=606
struct SubModelInfo { const char* names[6]; int count; };

inline SubModelInfo getSubModels (DrumType t)
{
    (void) t;
    return {{"CLASSIC", "ACOUSTIC", "808", "909", "707", "606"}, 6};
}

enum class SynthModel : uint8_t
{
    Analog, FM, DWGS, Formant, Sampler, Wavetable, Granular
};

// ═══════════════════════════════════════
// ARPEGGIATOR (matches HTML arpState)
// ═══════════════════════════════════════
struct ArpSettings
{
    bool        on       = false;
    int         mode     = 0;       // 0=up, 1=down, 2=updown, 3=downup, 4=random, 5=order, 6=chord, 7=skip, 8=zigzag
    int         octaves  = 1;       // 1-4
    int         rate     = 1;       // index: 0=1/32, 1=1/16, 2=1/8, 3=1/4, 4=1/16T, 5=1/8T
    int         gate     = 50;      // 1-100 percent
    int         swing    = 0;       // 0-100
};

// ═══════════════════════════════════════
// LFO (matches HTML per-track LFO x3)
// ═══════════════════════════════════════
struct LFOSettings
{
    int         target   = 1;       // primary target (CUT default)
    int         shape    = 0;       // 0=sine, 1=tri, 2=saw, 3=square, 4=ramp, 5=s&h
    float       rate     = 2.0f;    // Hz
    float       depth    = 0.0f;    // -1 to +1 bipolar (primary depth)
    bool        sync     = false;
    float       syncDiv  = 7.0f;    // sync division index 0-14
    bool        retrig   = false;
    bool        hiRate   = false;   // false=LOW, true=HIGH
    float       fadeIn   = 0.0f;    // fade-in time (seconds in FREE, sync div index in SYNC)
    bool        fadeInSync = false;  // true = fade-in time synced to BPM
    // Extra modulation routes (Serum-style multi-target)
    std::array<ModRoute, 16> extraRoutes;
};

// ═══════════════════════════════════════
// DRUM TRACK STATE
// ═══════════════════════════════════════
struct DrumTrackState
{
    DrumType    type      = DrumType::Kick;
    int         subModel  = 0;        // synthesis variant per drum type (0=classic, 1=808, 2=909...)
    StepSequence seq;

    int         length    = 16;       // pattern length 1-128
    float       clockMul  = 1.0f;     // 0.5, 1, 2, 3
    bool        muted     = false;
    bool        solo      = false;
    float       volume    = 0.6f;     // 0-1
    int         swing     = 0;        // 0-100 (positive only, like HTML)
    int         playDir   = 0;        // 0=FWD, 1=REV, 2=PING, 3=RND, 4=ONE
    int         page      = 0;        // 0-3 (viewing page)
    float       fmMix     = 0.0f;     // 0=pure analog, 1=pure FM, 0-1=layered

    // Drum engine params (per-type defaults loaded at init)
    float       pitch     = 150.0f;   // Kick body start ~150Hz (Analog RYTM style)
    float       pitchDec  = 0.08f;   // pitch sweep speed
    float       decay     = 0.5f;
    float       click     = 0.3f;
    float       tune      = 0.0f;    // semitone offset (0 = neutral)
    float       tone      = 185.0f;
    float       toneDecay = 0.08f;
    float       noiseDecay= 0.18f;
    float       snap      = 0.8f;
    float       cutoff    = 9000.0f;
    float       freq      = 1500.0f;
    float       freq1     = 540.0f;
    float       freq2     = 845.0f;
    float       spread    = 0.025f;
    float       noise     = 0.6f;
    float       pitchEnd  = 50.0f;

    // FM drum engine params
    float       fmRatio   = 2.0f;
    float       fmDepth   = 80.0f;
    float       fmDecay   = 0.05f;
    float       fmNoise   = 0.0f;    // noise amount 0-1 for FM cymbals
    int         fmNoiseType = 0;      // 0=white 1=metal 2=hiss 3=crunch

    // ER-1 drum engine params (drumEngine = 3)
    int         er1Wave1    = 0;       // 0=sine 1=tri 2=saw 3=square
    float       er1Pitch1   = 200.0f;  // osc1 base pitch Hz
    float       er1PDec1    = 0.05f;   // osc1 pitch decay (seconds)
    int         er1Wave2    = 0;       // 0=sine 1=tri 2=saw 3=square
    float       er1Pitch2   = 300.0f;  // osc2 base pitch Hz
    float       er1PDec2    = 0.03f;   // osc2 pitch decay (seconds)
    float       er1Ring     = 0.0f;    // ring mod depth 0-1
    float       er1XMod     = 0.0f;    // cross mod (osc2→osc1 FM) depth 0-500
    float       er1Noise    = 0.0f;    // noise level 0-1
    float       er1NDec     = 0.08f;   // noise decay (seconds)
    float       er1Cut      = 8000.0f; // filter cutoff Hz (200-16000)
    float       er1Res      = 0.0f;    // filter resonance 0-1
    float       er1Decay    = 0.3f;    // amp decay (seconds)
    float       er1Drive    = 0.0f;    // drive amount 0-1

    // Per-track FX (matches HTML per-track chain)
    float       distAmt     = 0.0f;
    float       reduxBits   = 16.0f;  // bit depth 1-16 (16 = off)
    float       reduxRate   = 0.0f;   // sample rate reducer 0-1 (0 = off)
    float       chorusDepth = 0.0f;
    float       chorusRate  = 0.5f;
    float       chorusMix   = 0.0f;
    float       delayMix    = 0.0f;
    float       delayTime   = 0.25f;
    float       delayFB     = 0.3f;
    bool        delaySync   = false;
    float       delayBeats  = 1.0f;
    int         delayPP     = 0;       // 0=mono, 1=ping-pong
    float       delayDamp   = 0.3f;   // 0-1 feedback LP filter (0=bright, 1=dark)
    int         delayAlgo   = 0;       // 0=DIGITAL, 1=TAPE, 2=BBD, 3=DIFFUSE
    float       reverbMix   = 0.0f;
    float       reverbSize  = 1.5f;
    float       reverbDamp  = 0.5f;
    int         reverbAlgo  = 0;       // 0=FDN, 1=PLATE, 2=SHIMMER, 3=GALACTIC, 4=ROOM, 5=SPRING, 6=NONLIN

    // Drum filter
    float       drumCut     = 100.0f; // 0-100 (100 = off)
    float       drumRes     = 0.0f;   // 0-1
    // Filter AD envelope (for kick/tom analog filter sweep)
    float       drumFiltEnv = 0.0f;   // 0-100 envelope depth (adds to drumCut)
    float       drumFiltA   = 0.001f; // attack 0-0.1s
    float       drumFiltD   = 0.15f;  // decay 0.01-1.0s

    // FX filters
    float       fxLP        = 20000.0f; // lowpass cutoff Hz (20000 = off)
    float       fxHP        = 20.0f;    // highpass cutoff Hz (20 = off)
    // 3-band EQ (dB, 0 = flat)
    float       eqLow       = 0.0f;     // -12 to +12 dB (shelf ~200Hz)
    float       eqMid       = 0.0f;     // -12 to +12 dB (bell ~1kHz)
    float       eqHigh      = 0.0f;     // -12 to +12 dB (shelf ~5kHz)
    float       pan         = 0.0f;

    // Ducking — this track gets ducked when duckSrc triggers
    // duckSrc: -1=OFF, 0-9=drum track, 10-14=synth track
    int         duckSrc     = -1;
    float       duckDepth   = 0.8f;    // 0-1 how much gain reduction
    float       duckAtk     = 0.005f;  // seconds
    float       duckRel     = 0.15f;   // seconds

    // Pro Distortion module (TUBE/TAPE/XFMR/AMP/WSHP)
    int         proDistModel = 0;      // 0=TUBE, 1=TAPE, 2=XFMR, 3=AMP, 4=WSHP
    float       proDistDrive = 0.0f;   // 0-1 (0=bypass)
    float       proDistTone  = 0.5f;   // 0-1 (dark→bright)
    float       proDistMix   = 1.0f;   // 0-1 dry/wet
    float       proDistBias  = 0.0f;   // 0-1 asymmetry (even harmonics)

    // OTT-style multiband compressor
    float       ottDepth     = 0.0f;   // 0-1 (0=bypass)
    float       ottUpward    = 0.7f;   // 0-1 upward compression amount
    float       ottDownward  = 0.5f;   // 0-1 downward compression amount

    // ── PHASER ──
    float       phaserMix    = 0.0f;   // 0-1 (0=bypass)
    float       phaserRate   = 0.3f;   // Hz (0.05-5)
    float       phaserDepth  = 0.6f;   // 0-1
    float       phaserFB     = 0.5f;   // 0-0.95

    // ── FLANGER ──
    float       flangerMix   = 0.0f;   // 0-1 (0=bypass)
    float       flangerRate  = 0.2f;   // Hz (0.05-5)
    float       flangerDepth = 0.5f;   // 0-1
    float       flangerFB    = 0.4f;   // -0.95 to 0.95 (negative = jet effect)

    // 3 LFOs per drum track
    std::array<LFOSettings, 3> lfos;

    // 3 MSEGs per drum track
    std::array<MSEGData, 3> msegs;
    bool        msegRetrig = true;    // global: retrigger all MSEGs on note (false = free-run)

    // ── Velocity & Key tracking modulation routes ──
    std::array<ModRoute, 4> velRoutes;  // velocity → param with bipolar depth
    std::array<ModRoute, 4> keyRoutes;  // key (MIDI note) → param with bipolar depth
    float       lastVelocity = 0.8f;    // updated on each noteOn
    int         lastNote     = 60;      // updated on each noteOn (MIDI note number)

    // Drum engine mode: 0=ANA, 1=FM, 2=SAMPLE
    int         drumEngine  = 0;

    // Sampler params (engine mode 2)
    float       smpStart    = 0.0f;      // 0-1 start position
    float       smpEnd      = 1.0f;      // 0-1 end position
    float       smpGain     = 1.0f;      // 0-2 level
    int         smpLoop     = 0;         // 0=one-shot, 1=loop
    int         smpPlayMode = 0;         // 0=ONE SHOT (play to end), 1=GATE (release on gate off)
    int         smpReverse  = 0;         // 0=forward, 1=reverse
    float       smpTune     = 0.0f;      // -24 to +24 semitones
    float       smpFine     = 0.0f;      // -1 to +1 (cents)
    float       smpCut      = 100.0f;    // 0-100 filter cutoff
    float       smpRes      = 0.0f;      // 0-1 filter resonance
    int         smpFType    = 0;         // 0=LP, 1=HP, 2=BP
    int         smpFModel   = 0;         // 0=CLN, 1=ACD, 2=DRT, 3=SEM, 4=ARP, 5=LQD
    int         smpFPoles   = 12;        // 6, 12, 24 dB/oct
    float       smpFiltEnv  = 0.0f;      // -100..+100 filter envelope amount
    float       smpFiltA = 0.001f, smpFiltD = 0.3f, smpFiltS = 0.0f, smpFiltR = 0.1f; // filter ADSR
    int         smpRootNote = 60;        // MIDI note of original pitch (C3 Ableton)
    // Amp envelope for sampler
    float       smpA = 0.001f, smpD = 0.3f, smpS = 1.0f, smpR = 0.1f;
    // Time stretch
    float       smpStretch  = 1.0f;      // 0.25-4.0 (1.0 = original speed)
    int         smpWarp     = 0;         // 0=off, 1=beats, 2=texture, 3=repitch, 4=beats2
    float       smpBPM      = 120.0f;    // original BPM of sample
    float       smpPlayPos  = 0.0f;     // current playback position (0-1, for GUI playhead)
    float       smpFileSR   = 44100.0f;  // sample rate of the loaded audio file
    int         smpBpmSync  = 1;         // 0=INT (manual stretch via STRC knob), 1=DAW (auto-follow host BPM)
    int         smpSyncMul  = 0;         // rate multiplier: -3..+3 → speed = 2^val (÷8 to ×8)
    int         smpBars     = 0;         // 0=auto from BPM, 1-64=user bar count for grid sync
    int         gridDiv     = 0;         // 0=auto, 1=1/1, 2=1/2, 4=1/4, 8=1/8, 16=1/16, 32=1/32
    // Sampler FM modulator
    float       smpFmAmt    = 0.0f;      // 0-1 FM depth
    float       smpFmRatio  = 2.0f;      // 0.5-16 freq ratio
    float       smpFmEnvA   = 0.001f;    // attack 0-1s
    float       smpFmEnvD   = 0.3f;      // decay 0-2s
    float       smpFmEnvS   = 0.0f;      // sustain 0-1

    std::shared_ptr<juce::AudioBuffer<float>> sampleData;
    juce::String samplePath;
    std::vector<WarpMarker> warpMarkers;  // per-transient timing correction markers

    // ── Sample slots (Octatrack-style: load folder, each step picks a sample) ──
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> sampleSlots; // up to 128
    std::vector<juce::String> sampleSlotNames; // filenames for display

    // ── Track linking (tracks in same group share mute/solo) ──
    int linkGroup = 0; // 0=none, 1-4=group A-D

    // ── Choke group (triggering one kills others in same group) ──
    int chokeGroup = 0; // 0=none, 1-8=group A-H

    // ── Pre-link snapshot (restored when unlinking) ──
    StepSequence preLinkedSeq;
    int preLinkedLength = 16;
    int preLinkedSwing = 0;
    float preLinkedClockMul = 1.0f;
    int preLinkedPlayDir = 0;
    float preLinkedVolume = 0.6f;

    // Reset engine params to defaults, preserving sequence/identity/FX
    void resetEngine()
    {
        DrumTrackState def;
        // Preserve identity & sequencer
        def.type = type; def.seq = seq; def.length = length;
        def.clockMul = clockMul; def.muted = muted; def.solo = solo;
        def.volume = volume; def.swing = swing; def.playDir = playDir; def.page = page;
        def.drumEngine = drumEngine;
        // Preserve sample data
        def.sampleData = sampleData; def.samplePath = samplePath;
        def.warpMarkers = warpMarkers;
        // Preserve LFOs & MSEGs
        def.lfos = lfos; def.msegs = msegs;
        *this = def;
        // Apply correct engine defaults for this drum type
        setTypeDefaults (type);
    }

    // Reset all LFOs and MSEGs to defaults
    void resetModulations()
    {
        for (auto& l : lfos) l = LFOSettings{};
        for (auto& m : msegs) m = MSEGData{};
        for (auto& vr : velRoutes) vr = {-1, 0.0f};
        for (auto& kr : keyRoutes) kr = {-1, 0.0f};
    }

    void loadDefaults()
    {
        setTypeDefaults (type);
    }

    // Switch drum type — resets engine params to new type's defaults
    // Preserves: seq, length, clockMul, mute, solo, volume, swing, playDir, page,
    //            drumEngine, FX, LFOs, MSEGs, sample data
    void setType (DrumType newType)
    {
        type = newType;
        setTypeDefaults (newType);
    }

private:
    void setTypeDefaults (DrumType t)
    {
        // Reset engine-specific params to defaults for this type
        pitch = 0; tune = 0; pitchDec = 0; decay = 0.2f; click = 0;
        tone = 0; toneDecay = 0; noiseDecay = 0; snap = 0;
        cutoff = 0; freq = 0; freq1 = 0; freq2 = 0;
        spread = 0; noise = 0; pitchEnd = 0;
        fmRatio = 1.0f; fmDepth = 0; fmDecay = 0.05f;
        fmMix = (drumEngine == 1) ? 1.0f : 0.0f;
        fmNoise = 0; fmNoiseType = 0;
        drumCut = 100; drumRes = 0; drumFiltEnv = 0;
        drumFiltA = 0.001f; drumFiltD = 0.15f;

        switch (t)
        {
            case DrumType::Kick:       pitch=50; tune=0.0f; pitchDec=0.05f; decay=0.3f; click=0.3f; fmRatio=2.0f; fmDepth=80; fmDecay=0.05f; break;
            case DrumType::Snare:      tone=185; toneDecay=0.08f; noiseDecay=0.18f; snap=0.8f; fmRatio=1.4f; fmDepth=60; fmDecay=0.08f; break;
            case DrumType::HiHatClosed:cutoff=9000; decay=0.04f; fmRatio=7.2f; fmDepth=120; fmDecay=0.03f; break;
            case DrumType::HiHatOpen:  cutoff=7000; decay=0.35f; fmRatio=7.2f; fmDepth=80; fmDecay=0.15f; break;
            case DrumType::Clap:       freq=1500; decay=0.1f; spread=0.025f; fmRatio=3.5f; fmDepth=100; fmDecay=0.06f; break;
            case DrumType::Tom:        pitch=100; pitchEnd=65; pitchDec=0.04f; decay=0.35f; click=0.2f; fmRatio=1.5f; fmDepth=60; fmDecay=0.1f; break;
            case DrumType::TomHi:      pitch=180; pitchEnd=130; pitchDec=0.03f; decay=0.2f; click=0.25f; fmRatio=1.8f; fmDepth=70; fmDecay=0.06f; break;
            case DrumType::Cowbell:    freq1=540; freq2=845; decay=0.4f; fmRatio=2.8f; fmDepth=40; fmDecay=0.2f; break;
            case DrumType::Rimshot:    tone=400; decay=0.03f; noise=0.6f; fmRatio=5.0f; fmDepth=80; fmDecay=0.02f; break;
            case DrumType::Crash:      freq=5000; decay=1.2f; fmRatio=8.5f; fmDepth=200; fmDecay=0.5f; break;
        }
    }
public:
};

// ═══════════════════════════════════════
// SYNTH TRACK STATE
// ═══════════════════════════════════════
struct SynthTrackState
{
    // Custom copy assignment — needed because std::atomic deletes default operator=
    SynthTrackState& operator= (const SynthTrackState& o)
    {
        if (this == &o) return *this;
        partIndex = o.partIndex; model = o.model; seq = o.seq;
        length = o.length; clockMul = o.clockMul; muted = o.muted; solo = o.solo;
        volume = o.volume; swing = o.swing; playDir = o.playDir; page = o.page;
        mono = o.mono; glide = o.glide; glideType = o.glideType;
        w1 = o.w1; w2 = o.w2; tune = o.tune; detune = o.detune; mix2 = o.mix2; subLevel = o.subLevel;
        oscSync = o.oscSync; syncRatio = o.syncRatio; pwm = o.pwm;
        unison = o.unison; uniSpread = o.uniSpread; uniStereo = o.uniStereo;
        charType = o.charType; charAmt = o.charAmt;
        fmLinAmt = o.fmLinAmt; fmLinRatio = o.fmLinRatio; fmLinDecay = o.fmLinDecay;
        fmLinSustain = o.fmLinSustain; fmLinSnap = o.fmLinSnap;
        fType = o.fType; fPoles = o.fPoles; fModel = o.fModel;
        cut = o.cut; res = o.res; fenv = o.fenv;
        fA = o.fA; fD = o.fD; fS = o.fS; fR = o.fR;
        aA = o.aA; aD = o.aD; aS = o.aS; aR = o.aR;
        fmAlgo = o.fmAlgo; cRatio = o.cRatio; cLevel = o.cLevel;
        r2 = o.r2; l2 = o.l2; dc2 = o.dc2; r3 = o.r3; l3 = o.l3; dc3 = o.dc3;
        r4 = o.r4; l4 = o.l4; dc4 = o.dc4; fmFeedback = o.fmFeedback;
        cA = o.cA; cD = o.cD; cS = o.cS; cR = o.cR;
        elemBow = o.elemBow; elemBlow = o.elemBlow; elemStrike = o.elemStrike;
        elemContour = o.elemContour; elemMallet = o.elemMallet; elemFlow = o.elemFlow;
        elemGeometry = o.elemGeometry; elemBright = o.elemBright; elemDamping = o.elemDamping;
        elemPosition = o.elemPosition; elemSpace = o.elemSpace; elemPitch = o.elemPitch;
        plaitsModel = o.plaitsModel; plaitsHarmonics = o.plaitsHarmonics;
        plaitsTimbre = o.plaitsTimbre; plaitsMorph = o.plaitsMorph;
        plaitsDecay = o.plaitsDecay; plaitsLpgColor = o.plaitsLpgColor;
        sampleData = o.sampleData; samplePath = o.samplePath; warpMarkers = o.warpMarkers;
        smpStart = o.smpStart; smpEnd = o.smpEnd; smpGain = o.smpGain;
        smpLoop = o.smpLoop; smpReverse = o.smpReverse; smpPlayMode = o.smpPlayMode;
        smpTune = o.smpTune; smpFine = o.smpFine; smpRootNote = o.smpRootNote;
        smpA = o.smpA; smpD = o.smpD; smpS = o.smpS; smpR = o.smpR;
        smpCut = o.smpCut; smpRes = o.smpRes; smpFType = o.smpFType; smpFModel = o.smpFModel; smpFPoles = o.smpFPoles;
        smpFiltEnv = o.smpFiltEnv; smpFiltA = o.smpFiltA; smpFiltD = o.smpFiltD; smpFiltS = o.smpFiltS; smpFiltR = o.smpFiltR;
        smpFmAmt = o.smpFmAmt; smpFmRatio = o.smpFmRatio;
        smpFmEnvA = o.smpFmEnvA; smpFmEnvD = o.smpFmEnvD; smpFmEnvS = o.smpFmEnvS;
        smpStretch = o.smpStretch; smpWarp = o.smpWarp; smpBPM = o.smpBPM; smpFileSR = o.smpFileSR;
        smpBpmSync = o.smpBpmSync; smpSyncMul = o.smpSyncMul; smpBars = o.smpBars;
        gridDiv = o.gridDiv;
        wtData1 = o.wtData1; wtData2 = o.wtData2;
        wtPos1 = o.wtPos1; wtPos2 = o.wtPos2; wtMix = o.wtMix;
        wtWarp1 = o.wtWarp1; wtWarpAmt1 = o.wtWarpAmt1; wtWarp2 = o.wtWarp2; wtWarpAmt2 = o.wtWarpAmt2;
        wtSubLevel = o.wtSubLevel;
        grainPos = o.grainPos; grainSize = o.grainSize; grainDensity = o.grainDensity;
        grainSpray = o.grainSpray; grainPitch = o.grainPitch; grainPan = o.grainPan;
        grainShape = o.grainShape; grainDir = o.grainDir;
        grainTexture = o.grainTexture; grainFreeze = o.grainFreeze; grainScan = o.grainScan;
        grainMode = o.grainMode; grainTilt = o.grainTilt;
        grainUniVoices = o.grainUniVoices; grainUniDetune = o.grainUniDetune; grainUniStereo = o.grainUniStereo;
        grainQuantize = o.grainQuantize; grainFeedback = o.grainFeedback;
        grainFmAmt = o.grainFmAmt; grainFmRatio = o.grainFmRatio; grainFmDecay = o.grainFmDecay;
        grainFmSus = o.grainFmSus; grainFmSnap = o.grainFmSnap; grainFmSpread = o.grainFmSpread;
        distAmt = o.distAmt; chorusRate = o.chorusRate; chorusDepth = o.chorusDepth; chorusMix = o.chorusMix;
        delayMix = o.delayMix; delayTime = o.delayTime; delayFB = o.delayFB;
        delaySync = o.delaySync; delayBeats = o.delayBeats; delayPP = o.delayPP; delayAlgo = o.delayAlgo; delayDamp = o.delayDamp;
        reverbMix = o.reverbMix; reverbSize = o.reverbSize; reverbDamp = o.reverbDamp; reverbAlgo = o.reverbAlgo;
        reduxBits = o.reduxBits; reduxRate = o.reduxRate;
        fxLP = o.fxLP; fxHP = o.fxHP; eqLow = o.eqLow; eqMid = o.eqMid; eqHigh = o.eqHigh;
        pan = o.pan; duckSrc = o.duckSrc; duckDepth = o.duckDepth; duckAtk = o.duckAtk; duckRel = o.duckRel;
        ottDepth = o.ottDepth; ottUpward = o.ottUpward; ottDownward = o.ottDownward;
        phaserMix = o.phaserMix; phaserRate = o.phaserRate; phaserDepth = o.phaserDepth; phaserFB = o.phaserFB;
        flangerMix = o.flangerMix; flangerRate = o.flangerRate; flangerDepth = o.flangerDepth; flangerFB = o.flangerFB;
        proDistModel = o.proDistModel; proDistDrive = o.proDistDrive; proDistTone = o.proDistTone;
        proDistMix = o.proDistMix; proDistBias = o.proDistBias;
        chokeGroup = o.chokeGroup; linkGroup = o.linkGroup;
        sampleSlots = o.sampleSlots; sampleSlotNames = o.sampleSlotNames;
        chordMode = o.chordMode; chordInversion = o.chordInversion; chordVoicing = o.chordVoicing;
        lfos = o.lfos; msegs = o.msegs; msegRetrig = o.msegRetrig; arp = o.arp;
        // Atomics — copy values
        kbNoteOn.store (o.kbNoteOn.load()); kbVelocity.store (o.kbVelocity.load());
        kbNoteOff.store (o.kbNoteOff.load()); kbLastNote = o.kbLastNote;
        // Visualization (non-essential, benign race)
        smpPlayPos = o.smpPlayPos;
        grainVisCount = o.grainVisCount;
        for (int i = 0; i < kMaxVisGrains; ++i) grainVisData[i] = o.grainVisData[i];
        return *this;
    }
    SynthTrackState() = default;
    SynthTrackState (const SynthTrackState& o) { *this = o; } // use operator= above

    int         partIndex = 0;        // 0-4
    SynthModel  model     = SynthModel::Analog;
    StepSequence seq;

    int         length    = 16;
    float       clockMul  = 1.0f;
    bool        muted     = false;
    bool        solo      = false;
    float       volume    = 0.6f;
    int         swing     = 0;
    int         playDir   = 0;        // 0=FWD, 1=REV, 2=PING, 3=RND, 4=ONE
    int         page      = 0;
    bool        mono      = false;
    float       glide     = 0.0f;
    int         glideType = 0;        // 0=exp, 1=linear

    // Analog synth params
    int         w1        = 0;        // 0=saw, 1=square, 2=tri, 3=sine, 4=pwm
    int         w2        = 1;
    float       tune      = 0.0f;
    float       detune    = -0.1f;
    float       mix2      = 0.4f;
    float       subLevel  = 0.3f;
    bool        oscSync   = false;
    float       syncRatio = 2.0f;
    float       pwm       = 0.5f;
    int         unison    = 1;
    float       uniSpread = 0.2f;
    float       uniStereo = 0.0f;    // 0-1 stereo spread of unison voices

    // Character: analog coloring
    int         charType  = 0;        // 0=warm, 1=fold, 2=fractal
    float       charAmt   = 0.0f;     // 0-1 amount

    // Linear FM modulator (simple sine mod on oscillator pitch)
    float       fmLinAmt   = 0.0f;    // 0-100 modulation depth
    float       fmLinRatio = 2.0f;    // mod freq ratio to carrier
    float       fmLinDecay = 0.3f;    // mod envelope decay time
    float       fmLinSustain = 0.0f;  // 0-1 sustain level (0=full decay, 1=no decay)
    int         fmLinSnap  = 1;       // 0=continuous (atonal), 1=integer snap (tonal)

    // Filter
    int         fType     = 0;         // 0=LP, 1=HP, 2=BP
    int         fPoles    = 12;        // 6, 12, 24
    int         fModel    = 0;         // 0=CLEAN, 1=ACID(303), 2=DIRTY, 3=SEM, 4=ARP, 5=LIQUID
    float       cut       = 75.0f;
    float       res       = 0.25f;
    float       fenv      = 0.4f;
    float       fA        = 0.01f, fD = 0.3f, fS = 0.5f, fR = 0.3f;
    float       aA        = 0.02f, aD = 0.4f, aS = 0.7f, aR = 0.3f;

    // FM 4-Op params (per-operator control)
    int         fmAlgo    = 0;        // 0-7 algorithm
    float       cRatio    = 1.0f;     // OP1 carrier ratio
    float       cLevel    = 1.0f;     // OP1 carrier output level 0-1
    float       r2 = 2.0f;           // OP2 ratio
    float       l2 = 0.0f;           // OP2 mod level 0-100 (init=0: carrier only)
    float       dc2 = 0.3f;          // OP2 mod envelope decay
    float       r3 = 3.0f;           // OP3 ratio
    float       l3 = 0.0f;           // OP3 mod level (init=0)
    float       dc3 = 0.2f;          // OP3 decay
    float       r4 = 4.0f;           // OP4 ratio
    float       l4 = 0.0f;           // OP4 mod level (init=0)
    float       dc4 = 0.15f;         // OP4 decay
    float       fmFeedback = 0.0f;   // OP4 self-feedback 0-1
    // Carrier ADSR
    float       cA = 0.01f, cD = 0.5f, cS = 0.6f, cR = 0.4f;

    // Elements (modal synthesis) params
    float       elemBow     = 0.0f;      // 0-1 bow exciter level
    float       elemBlow    = 0.0f;      // 0-1 blow exciter level
    float       elemStrike  = 0.8f;      // 0-1 strike exciter level
    float       elemContour = 0.5f;      // 0-1 exciter envelope shape
    float       elemMallet  = 0.5f;      // 0-1 mallet hardness
    float       elemFlow    = 0.5f;      // 0-1 blow turbulence
    float       elemGeometry = 0.25f;    // 0-1 modal ratios (harmonic→metallic→dissonant)
    float       elemBright  = 0.5f;      // 0-1 resonator brightness
    float       elemDamping = 0.5f;      // 0-1 resonator damping
    float       elemPosition = 0.3f;     // 0-1 excitation position
    float       elemSpace   = 0.3f;      // 0-1 built-in reverb
    float       elemPitch   = 0.5f;      // 0-1 global pitch (0=−24st, 0.5=0, 1=+24st)

    // Plaits (multi-model synthesis) params
    int         plaitsModel = 0;         // 0-7 synthesis model
    float       plaitsHarmonics = 0.5f;  // 0-1
    float       plaitsTimbre = 0.5f;     // 0-1
    float       plaitsMorph = 0.0f;      // 0-1
    float       plaitsDecay = 0.5f;      // 0-1
    float       plaitsLpgColor = 0.5f;   // 0-1

    // Formant params
    float       formV1 = 0, formV2 = 3, formMorph = 0.5f;
    float       formLfoRate = 0.5f, formLfoDepth = 0;

    // Sampler params
    float       smpStart    = 0.0f;      // 0-1 start position
    float       smpEnd      = 1.0f;      // 0-1 end position
    float       smpGain     = 1.0f;      // 0-2 level
    int         smpLoop     = 0;         // 0=one-shot, 1=loop
    int         smpPlayMode = 0;         // 0=ONE SHOT (play to end), 1=GATE (release on gate off)
    int         smpReverse  = 0;         // 0=forward, 1=reverse
    float       smpTune     = 0.0f;      // -24 to +24 semitones (relative to root)
    float       smpFine     = 0.0f;      // -1 to +1 (±100 cents)
    float       smpA = 0.001f, smpD = 0.3f, smpS = 1.0f, smpR = 0.1f; // ADSR
    float       smpCut      = 100.0f;    // 0-100 filter cutoff
    float       smpRes      = 0.0f;      // 0-1 filter resonance
    int         smpFType    = 0;         // 0=LP, 1=HP, 2=BP
    int         smpFModel   = 0;         // 0=CLN, 1=ACD, 2=DRT, 3=SEM, 4=ARP, 5=LQD
    int         smpFPoles   = 12;        // 6, 12, 24 dB/oct
    float       smpFiltEnv  = 0.0f;      // -100..+100 filter envelope amount
    float       smpFiltA = 0.001f, smpFiltD = 0.3f, smpFiltS = 0.0f, smpFiltR = 0.1f; // filter ADSR
    int         smpRootNote = 60;        // MIDI note of original pitch (C3 Ableton)
    // Time stretch
    float       smpStretch  = 1.0f;      // 0.25-4.0 (1.0 = original speed)
    int         smpWarp     = 0;         // 0=off, 1=beats, 2=texture, 3=repitch, 4=beats2
    float       smpBPM      = 120.0f;    // original BPM of sample
    float       smpPlayPos  = 0.0f;     // current playback position (0-1, for GUI playhead)
    float       smpFileSR   = 44100.0f;  // sample rate of the loaded audio file
    int         smpBpmSync  = 1;         // 0=INT (manual stretch via STRC knob), 1=DAW (auto-follow host BPM)
    int         smpSyncMul  = 0;         // rate multiplier: -3..+3 → speed = 2^val (÷8 to ×8)
    int         smpBars     = 0;         // 0=auto from BPM, 1-64=user bar count for grid sync
    int         gridDiv     = 0;         // 0=auto, 1=1/1, 2=1/2, 4=1/4, 8=1/8, 16=1/16, 32=1/32
    // Sampler FM modulator
    float       smpFmAmt    = 0.0f;      // 0-1 FM depth
    float       smpFmRatio  = 2.0f;      // 0.5-16 freq ratio
    float       smpFmEnvA   = 0.001f;    // attack 0-1s
    float       smpFmEnvD   = 0.3f;      // decay 0-2s
    float       smpFmEnvS   = 0.0f;      // sustain 0-1
    // Sample data (shared between audio and UI threads)
    std::shared_ptr<juce::AudioBuffer<float>> sampleData;
    juce::String samplePath; // file path for serialization
    std::vector<WarpMarker> warpMarkers;  // per-transient timing correction markers

    // ── Sample slots (Octatrack-style: load folder, each step picks a sample) ──
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> sampleSlots;
    std::vector<juce::String> sampleSlotNames;

    // ── Track linking ──
    int linkGroup = 0; // 0=none, 1-4=group A-D

    // ── Choke group (triggering one kills others in same group) ──
    int chokeGroup = 0; // 0=none, 1-8=group A-H

    // ── Pre-link snapshot (restored when unlinking) ──
    StepSequence preLinkedSeq;
    int preLinkedLength = 16;
    int preLinkedSwing = 0;
    float preLinkedClockMul = 1.0f;
    int preLinkedPlayDir = 0;
    float preLinkedVolume = 0.8f;

    // ── Wavetable engine params ──
    float       wtPos1      = 0.0f;      // 0-1 wavetable position osc1
    float       wtPos2      = 0.0f;      // 0-1 wavetable position osc2
    float       wtMix       = 0.0f;      // 0-1 osc1/osc2 blend
    int         wtWarp1     = 0;         // WarpMode enum (0=Off, 1-6)
    float       wtWarpAmt1  = 0.0f;      // 0-1 warp amount osc1
    int         wtWarp2     = 0;         // WarpMode enum
    float       wtWarpAmt2  = 0.0f;      // 0-1 warp amount osc2
    float       wtSubLevel  = 0.0f;      // 0-1 sub oscillator level
    std::shared_ptr<WavetableData> wtData1; // wavetable data osc1
    std::shared_ptr<WavetableData> wtData2; // wavetable data osc2

    // ── Granular engine params ──
    int         grainMode   = 0;         // 0=STRD, 1=CLOUD, 2=SCRUB, 3=GLTCH, 4=FLUX
    float       grainPos    = 0.5f;      // 0-1 playback position in sample
    float       grainSize   = 80.0f;     // grain size in ms (5-500)
    float       grainDensity= 10.0f;     // grains per second (1-100)
    float       grainSpray  = 0.0f;      // 0-1 position randomization
    float       grainPitch  = 0.0f;      // -24..+24 semitones random spread
    float       grainPan    = 0.0f;      // 0-1 stereo spread
    int         grainShape  = 0;         // 0=Hann, 1=Tri, 2=Rect, 3=Tukey, 4=Gauss, 5=Saw
    int         grainDir    = 0;         // 0=FWD, 1=REV, 2=RND
    float       grainTilt   = 50.0f;     // 0-100 envelope peak shift (50=center)
    float       grainMix    = 1.0f;      // 0-1 wet/dry
    bool        grainFreeze = false;     // lock position
    float       grainTexture= 0.0f;      // 0-1 spectral texture (formant shift)
    float       grainScan   = 0.0f;      // -1..+1 auto scan speed (Granulator II style)
    int         grainUniVoices = 1;      // 1-8 unison voices
    float       grainUniDetune = 0.0f;   // 0-100 detune cents between unison voices
    float       grainUniStereo = 0.0f;   // 0-1 stereo spread of unison voices
    int         grainQuantize  = 0;      // 0=OFF, 1=OCT, 2=5TH, 3=TRAD, 4=SCAL
    float       grainFeedback  = 0.0f;   // 0-1 output→input feedback
    // Granular FM modulator
    float       grainFmAmt    = 0.0f;   // 0-100 depth
    float       grainFmRatio  = 2.0f;   // mod/carrier ratio
    float       grainFmDecay  = 0.3f;   // mod envelope decay (sec)
    float       grainFmSus    = 0.0f;   // 0-1 sustain level
    int         grainFmSnap   = 1;      // 0=FREE, 1=INT
    float       grainFmSpread = 0.0f;   // 0-1 per-grain FM depth variation
    // Grain visualization readback (written by audio thread, read by GUI — benign race)
    static constexpr int kMaxVisGrains = 16;
    struct GrainVis { float pos = 0; float size = 0; float amp = 0; float pan = 0; float pitch = 1; bool reverse = false; };
    GrainVis grainVisData[kMaxVisGrains] {};
    int grainVisCount = 0;

    // Per-track FX
    float       distAmt     = 0.0f;
    float       reduxBits   = 16.0f;
    float       reduxRate   = 0.0f;
    float       chorusDepth = 0.0f;
    float       chorusRate  = 0.5f;
    float       chorusMix   = 0.0f;
    float       delayMix    = 0.0f;
    float       delayTime   = 0.25f;
    float       delayFB     = 0.3f;
    bool        delaySync   = false;
    float       delayBeats  = 1.0f;
    int         delayPP     = 0;       // 0=mono, 1=ping-pong
    float       delayDamp   = 0.3f;   // 0-1 feedback LP filter (0=bright, 1=dark)
    int         delayAlgo   = 0;       // 0=DIGITAL, 1=TAPE, 2=BBD, 3=DIFFUSE
    float       reverbMix   = 0.0f;
    float       reverbSize  = 1.5f;
    float       reverbDamp  = 0.5f;
    int         reverbAlgo  = 0;       // 0=FDN, 1=PLATE, 2=SHIMMER, 3=GALACTIC, 4=ROOM, 5=SPRING, 6=NONLIN

    // FX filters
    float       fxLP        = 20000.0f;
    float       fxHP        = 20.0f;
    // 3-band EQ (dB, 0 = flat)
    float       eqLow       = 0.0f;     // -12 to +12 dB (shelf ~200Hz)
    float       eqMid       = 0.0f;     // -12 to +12 dB (bell ~1kHz)
    float       eqHigh      = 0.0f;     // -12 to +12 dB (shelf ~5kHz)
    float       pan         = 0.0f;     // -1=L, 0=C, 1=R

    // Ducking — this track gets ducked when duckSrc triggers
    int         duckSrc     = -1;       // -1=OFF, 0-9=drum, 10-14=synth
    float       duckDepth   = 0.8f;
    float       duckAtk     = 0.005f;
    float       duckRel     = 0.15f;

    // Pro Distortion module (TUBE/TAPE/XFMR/AMP/WSHP)
    int         proDistModel = 0;
    float       proDistDrive = 0.0f;
    float       proDistTone  = 0.5f;
    float       proDistMix   = 1.0f;
    float       proDistBias  = 0.0f;

    // OTT-style multiband compressor
    float       ottDepth     = 0.0f;
    float       ottUpward    = 0.7f;
    float       ottDownward  = 0.5f;

    // ── PHASER ──
    float       phaserMix    = 0.0f;
    float       phaserRate   = 0.3f;
    float       phaserDepth  = 0.6f;
    float       phaserFB     = 0.5f;

    // ── FLANGER ──
    float       flangerMix   = 0.0f;
    float       flangerRate  = 0.2f;
    float       flangerDepth = 0.5f;
    float       flangerFB    = 0.4f;

    // Arpeggiator
    // Chord mode (0=OFF, 1-24 = chord types, quantized to scale)
    int         chordMode = 0;
    int         chordInversion = 0;  // 0=root, 1=1st inv, 2=2nd inv, 3=3rd inv
    int         chordVoicing = 0;    // 0=close, 1=drop2, 2=spread, 3=open

    // 3 LFOs per synth track (matches HTML)
    std::array<LFOSettings, 3> lfos;

    // 3 MSEGs per synth track
    std::array<MSEGData, 3> msegs;
    bool        msegRetrig = true;    // global: retrigger all MSEGs on note (false = free-run)

    // ── Velocity & Key tracking modulation routes ──
    std::array<ModRoute, 4> velRoutes;  // velocity → param with bipolar depth
    std::array<ModRoute, 4> keyRoutes;  // key (MIDI note) → param with bipolar depth
    float       lastVelocity = 0.8f;
    int         lastNote     = 60;

    // Step Arpeggiator (Ableton Step Arp style)
    ArpData     arp;

    // ── Keyboard preview (UI → audio thread, polled each block) ──
    std::atomic<int>   kbNoteOn   { -1 };    // -1=off, 0-127=MIDI note
    std::atomic<float> kbVelocity { 0.8f };
    std::atomic<bool>  kbNoteOff  { false };
    int   kbLastNote = -1;    // audio thread only, no need for atomic

    // Reset engine params to defaults, preserving sequence/identity
    void resetEngine()
    {
        // Reset sound params to defaults — preserve identity/seq/samples
        // Can't use *this = def because of std::atomic members
        w1 = 0; w2 = 1; tune = 0.0f; detune = -0.1f; mix2 = 0.4f; subLevel = 0.3f;
        oscSync = false; syncRatio = 2.0f; pwm = 0.5f; unison = 1; uniSpread = 0.2f; uniStereo = 0.0f;
        charType = 0; charAmt = 0.0f;
        fmLinAmt = 0.0f; fmLinRatio = 2.0f; fmLinDecay = 0.3f; fmLinSustain = 0.0f; fmLinSnap = 1;
        fType = 0; fPoles = 12; fModel = 0; cut = 75.0f; res = 0.25f; fenv = 0.4f;
        fA = 0.01f; fD = 0.3f; fS = 0.5f; fR = 0.3f;
        aA = 0.02f; aD = 0.4f; aS = 0.7f; aR = 0.3f;
        mono = false; glide = 0.0f; glideType = 0;
        fmAlgo = 0; cRatio = 1.0f; cLevel = 1.0f;
        r2 = 2.0f; l2 = 0.0f; dc2 = 0.3f; r3 = 3.0f; l3 = 0.0f; dc3 = 0.2f;
        r4 = 4.0f; l4 = 0.0f; dc4 = 0.15f; fmFeedback = 0.0f;
        cA = 0.01f; cD = 0.5f; cS = 0.6f; cR = 0.4f;
        elemBow = 0.0f; elemBlow = 0.0f; elemStrike = 0.8f; elemContour = 0.5f;
        elemMallet = 0.5f; elemFlow = 0.5f; elemGeometry = 0.25f; elemBright = 0.5f;
        elemDamping = 0.5f; elemPosition = 0.3f; elemSpace = 0.3f; elemPitch = 0.5f;
        plaitsModel = 0; plaitsHarmonics = 0.5f; plaitsTimbre = 0.5f; plaitsMorph = 0.0f;
        plaitsDecay = 0.5f; plaitsLpgColor = 0.5f;
        distAmt = 0.0f; chorusRate = 1.5f; chorusDepth = 0.4f; chorusMix = 0.0f;
        delayMix = 0.0f; delayTime = 0.3f; delayFB = 0.3f; delaySync = 0; delayBeats = 0.5f;
        delayPP = 0; delayAlgo = 0; delayDamp = 0.3f;
        reverbMix = 0.0f; reverbSize = 0.5f; reverbDamp = 0.3f; reverbAlgo = 0;
        reduxBits = 16.0f; reduxRate = 0.0f;
        fxLP = 20000.0f; fxHP = 20.0f; eqLow = 0.0f; eqMid = 0.0f; eqHigh = 0.0f;
        pan = 0.0f; duckSrc = -1; duckDepth = 0.8f; duckAtk = 0.005f; duckRel = 0.15f;
        ottDepth = 0.0f; ottUpward = 0.7f; ottDownward = 0.5f;
        phaserMix = 0.0f; phaserRate = 0.3f; phaserDepth = 0.6f; phaserFB = 0.5f;
        flangerMix = 0.0f; flangerRate = 0.2f; flangerDepth = 0.5f; flangerFB = 0.4f;
        proDistModel = 0; proDistDrive = 0.0f; proDistTone = 0.5f; proDistMix = 1.0f; proDistBias = 0.0f;
        chordMode = 0; chordInversion = 0; chordVoicing = 0;
        // Reset keyboard state
        kbNoteOn.store (-1); kbVelocity.store (0.8f); kbNoteOff.store (false); kbLastNote = -1;
        // Reset sampler params to defaults
        smpStart = 0.0f; smpEnd = 1.0f; smpGain = 1.0f; smpLoop = 0; smpReverse = false; smpPlayMode = 0;
        smpTune = 0.0f; smpFine = 0.0f; smpRootNote = 60;
        smpA = 0.001f; smpD = 0.1f; smpS = 1.0f; smpR = 0.1f;
        smpCut = 100.0f; smpRes = 0.0f; smpFType = 0; smpFModel = 0; smpFPoles = 12;
        smpFiltEnv = 0.0f; smpFiltA = 0.01f; smpFiltD = 0.3f; smpFiltS = 0.5f; smpFiltR = 0.3f;
        smpFmAmt = 0.0f; smpFmRatio = 2.0f; smpFmEnvA = 0.01f; smpFmEnvD = 0.3f; smpFmEnvS = 0.0f;
        smpStretch = 0; smpWarp = 0.0f; smpBPM = 120.0f; smpFileSR = 44100.0f;
        smpBpmSync = false; smpSyncMul = 1.0f; smpBars = 1;
        // Reset WT params
        wtPos1 = 0.0f; wtPos2 = 0.0f; wtMix = 0.0f;
        wtWarp1 = 0; wtWarpAmt1 = 0.0f; wtWarp2 = 0; wtWarpAmt2 = 0.0f; wtSubLevel = 0.0f;
        // Reset granular params
        grainPos = 0.5f; grainSize = 0.1f; grainDensity = 0.5f; grainSpray = 0.1f;
        grainPitch = 0.0f; grainPan = 0.0f; grainShape = 0; grainDir = 0;
        grainTexture = 0.0f; grainFreeze = false; grainScan = 0.0f;
        grainMode = 0; grainTilt = 0.5f; grainUniVoices = 1; grainUniDetune = 0.0f; grainUniStereo = 0.0f;
        grainQuantize = 0; grainFeedback = 0.0f;
        grainFmAmt = 0.0f; grainFmRatio = 2.0f; grainFmDecay = 0.3f; grainFmSus = 0.0f;
        grainFmSnap = 1; grainFmSpread = 0.0f;
        // Reset arp
        arp = ArpData{};
        // Reset vel/key tracking routes
        for (auto& vr : velRoutes) vr = {-1, 0.0f};
        for (auto& kr : keyRoutes) kr = {-1, 0.0f};
    }

    // Reset all LFOs and MSEGs to defaults
    void resetModulations()
    {
        for (auto& l : lfos) l = LFOSettings{};
        for (auto& m : msegs) m = MSEGData{};
        for (auto& vr : velRoutes) vr = {-1, 0.0f};
        for (auto& kr : keyRoutes) kr = {-1, 0.0f};
    }
};
