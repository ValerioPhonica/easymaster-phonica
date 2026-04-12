#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Sequencer/TrackState.h"
#include "SeqUndoManager.h"
#include "MacroEngine.h"
#include <array>

class PresetManager; // forward — full definition in PresetManager.h

// Central state for the entire groovebox
class GrooveBoxState
{
public:
    GrooveBoxState();

    // Drum tracks
    std::array<DrumTrackState, 10> drumTracks;

    // Synth tracks
    std::array<SynthTrackState, 5> synthTracks;

    // Transport
    std::atomic<float> bpm { 120.0f };
    std::atomic<int>   globalSwing { 0 };
    std::atomic<bool>  playing { false };
    std::atomic<bool>  externalClock { true };

    // Master
    std::atomic<float> masterVolume { 0.8f };
    std::atomic<int>   quality { 1 };          // 0=ECO (saves CPU), 1=STD (project SR), 2=HQ (2x oversample)

    // Metronome
    std::atomic<bool>  metronomeOn { false };
    std::atomic<float> metronomeVol { 0.6f };  // 0-1
    std::atomic<int>   metronomeSound { 0 };   // 0=click, 1=hi, 2=cowbell, 3=rim
    std::atomic<int>   metronomePreRoll { 0 }; // 0=off, 1=1bar, 2=2bars
    std::atomic<bool>  metronomeAutoRec { true }; // auto-enable on rec

    // Master Pultec EQ
    std::atomic<float> pultecLowBoost  { 2.0f };   // 0-10 dB (default: gentle 2dB bass boost)
    std::atomic<float> pultecLowAtten  { 0.5f };   // 0-10 dB (Pultec trick: slight atten tightens)
    std::atomic<float> pultecLowFreq   { 60.0f };  // 20-200 Hz
    std::atomic<float> pultecHighBoost { 1.5f };    // 0-10 dB (default: gentle air boost)
    std::atomic<float> pultecHighAtten { 0.0f };    // 0-10 dB
    std::atomic<float> pultecHighFreq  { 10000.0f };// 3k-16k Hz (boost freq)
    std::atomic<float> pultecHighBW    { 1.5f };    // Q 0.3-4.0
    std::atomic<float> pultecHiAttnFrq { 12000.0f };// 5k-20k Hz (atten freq, separate)

    // Master Compressor
    std::atomic<float> compThreshold { -12.0f };   // -40 to 0 dBFS
    std::atomic<float> compRatio     { 4.0f };     // 1-20
    std::atomic<float> compAttack    { 10.0f };    // 0.1-100 ms
    std::atomic<float> compRelease   { 100.0f };   // 10-1000 ms
    std::atomic<float> compMakeup    { 3.0f };     // 0-12 dB
    std::atomic<float> compKnee      { 6.0f };     // 0-12 dB
    std::atomic<float> compScHP      { 0.0f };     // Sidechain HP freq: 0=OFF, 20-500 Hz
    std::atomic<int>   compStyle     { 0 };         // 0=CLEAN, 1=WARM, 2=PUNCH, 3=GLUE

    // Master Limiter
    std::atomic<float> limCeiling    { -0.3f };    // -3 to 0 dBFS
    std::atomic<float> limRelease    { 50.0f };    // 10-500 ms
    std::atomic<float> limInputGain  { 0.0f };     // 0-12 dB input drive
    std::atomic<float> limAutoRel    { 0.0f };     // 0=off, 1=on (auto-release)

    // Master Gater/Slicer
    std::atomic<float> gaterMix    { 0.0f };       // 0-1 dry/wet
    std::atomic<float> gaterRate   { 3.0f };       // sync division index 0-7
    std::atomic<float> gaterDepth  { 1.0f };       // 0-1 gate depth
    std::atomic<float> gaterShape  { 0.0f };       // 0=square, 1=saw, 2=ramp, 3=tri
    std::atomic<float> gaterSmooth { 0.05f };      // 0-0.5 attack/release smoothing

    // Master Delay
    std::atomic<float> mDelayMix   { 0.0f };       // 0-1
    std::atomic<float> mDelayTime  { 0.375f };     // seconds (or beats)
    std::atomic<float> mDelayFB    { 0.4f };       // 0-0.9
    std::atomic<float> mDelaySync  { 1.0f };       // 0=free, 1=sync
    std::atomic<float> mDelayBeats { 1.0f };       // beats (1/16=0.25, 1/8=0.5, 1/4=1, etc)
    std::atomic<float> mDelayHP    { 200.0f };     // (legacy, unused)
    std::atomic<float> mDelayLP    { 8000.0f };    // (legacy, unused)
    std::atomic<float> mDelayPP    { 0.0f };       // 0=mono, 1=ping-pong
    std::atomic<float> mDelayAlgo  { 0.0f };       // 0=DIG, 1=TAPE, 2=BBD, 3=DIFFUSE
    std::atomic<float> mDelayDamp  { 0.3f };       // 0-1 feedback damping

    // Master DJ Filter (HP/LP sweep)
    std::atomic<float> djFilterFreq  { 0.5f };     // 0-1 (0=full LP, 0.5=bypass, 1=full HP)
    std::atomic<float> djFilterRes   { 0.3f };     // 0-1

    // Master FX Step Sequencer (MX-1 style)
    StepSequence masterFXSeq;                       // up to 128 steps with p-locks
    std::atomic<int>   masterFXLength { 16 };       // 1-128
    std::atomic<int>   masterFXStep   { -1 };       // current step for UI display

    // Metering
    std::atomic<float> peakL { 0.0f };
    std::atomic<float> peakR { 0.0f };
    std::atomic<float> rmsL { 0.0f };
    std::atomic<float> rmsR { 0.0f };
    std::atomic<float> compGR { 0.0f };            // compressor gain reduction in dB (positive = reducing)
    std::atomic<float> limGR  { 0.0f };            // limiter gain reduction in dB
    std::atomic<float> vuCalibration { -18.0f };   // dBFS that equals 0 VU (-24 to -4)

    // Fill mode
    std::atomic<bool>  fillMode { false };

    // ── Motion Recording (Korg-style) ──
    std::atomic<bool>  motionRec { false };     // true = recording
    std::atomic<int>   motionRecMode { 0 };     // 0=STEP, 1=SMOOTH
    // Current playing step per track (updated by audio thread, read by GUI for motion rec)
    std::array<std::atomic<int>, 10> drumCurrentStep {};
    std::array<std::atomic<int>, 5>  synthCurrentStep {};

    // LFO readback (updated from processBlock for GUI indicators)
    std::array<std::array<std::atomic<float>, 3>, 5> synthLfoValues {};  // 5 tracks × 3 LFOs
    std::array<std::array<std::atomic<float>, 3>, 10> drumLfoValues {};  // 10 tracks × 3 LFOs
    std::array<std::array<std::atomic<float>, 3>, 5> synthMsegPhase {};   // for GUI playhead
    std::array<std::array<std::atomic<float>, 3>, 5> synthMsegAux {};     // aux LFO value for GUI
    // Cross-MSEG values for GUI visualization [track][mseg][source 0 or 1]
    std::array<std::array<std::array<std::atomic<float>, 2>, 3>, 5> synthMsegCross {};
    std::array<std::atomic<float>, 5> synthMsegValue {};
    std::array<std::array<std::atomic<float>, 3>, 10> drumMsegPhase {};
    std::array<std::atomic<float>, 10> drumMsegValue {};

    // ── Granular Resample ──
    std::atomic<int>   resampleSrc    { -1 };    // -1=OFF, 0-9=drum track, 10-14=synth track, 15=master
    std::atomic<bool>  resampleActive { false };  // true = recording
    std::atomic<int>   resampleTarget { 0 };      // which synth track (0-4) receives the sample
    std::atomic<int>   resampleLength { 0 };      // number of samples recorded so far
    std::atomic<bool>  resampleReady  { false };   // flag: recording just finished, finalize on GUI thread
    std::atomic<bool>  resampleTransportSync { false }; // true = recording follows transport play/stop
    std::atomic<bool>  resampleArmed  { false };   // true = waiting for transport to start recording

    // Scale quantization
    std::atomic<int>   scaleRoot { 0 };    // 0=C, 1=C#, ... 11=B
    std::atomic<int>   scaleType { 0 };    // 0=chromatic, 1=major, 2=minor, etc.

    // Clipboard for copy/paste
    StepData    clipboardStep;
    bool        clipboardStepValid = false;
    StepSequence clipboardTrack;
    bool        clipboardTrackValid = false;

    // Serialization
    void saveToXml (juce::XmlElement& xml) const;
    void loadFromXml (const juce::XmlElement& xml);

    // Check if any track has solo
    bool anySolo() const;
    bool isEffectivelyMuted (int drumIdx) const;
    bool isSynthEffectivelyMuted (int synthIdx) const;

    // Undo/Redo (5 levels)
    SeqUndoManager undoManager;
    
    // Macro system (4 macro knobs)
    MacroEngine macroEngine;
    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo()   { return undoManager.canUndo(); }
    bool canRedo()   { return undoManager.canRedo(); }

    // ── Factory reset — restores entire plugin to fresh state ──
    void initAll();

    // ── Master chain preset save/load ──
    juce::String saveMasterChainToXml() const
    {
        juce::XmlElement root ("MasterChain");
        root.setAttribute ("pultecLowBoost",  (double) pultecLowBoost.load());
        root.setAttribute ("pultecLowAtten",  (double) pultecLowAtten.load());
        root.setAttribute ("pultecLowFreq",   (double) pultecLowFreq.load());
        root.setAttribute ("pultecHighBoost",  (double) pultecHighBoost.load());
        root.setAttribute ("pultecHighAtten",  (double) pultecHighAtten.load());
        root.setAttribute ("pultecHighFreq",   (double) pultecHighFreq.load());
        root.setAttribute ("pultecHighBW",     (double) pultecHighBW.load());
        root.setAttribute ("pultecHiAttnFrq",  (double) pultecHiAttnFrq.load());
        root.setAttribute ("compThreshold",    (double) compThreshold.load());
        root.setAttribute ("compRatio",        (double) compRatio.load());
        root.setAttribute ("compAttack",       (double) compAttack.load());
        root.setAttribute ("compRelease",      (double) compRelease.load());
        root.setAttribute ("compMakeup",       (double) compMakeup.load());
        root.setAttribute ("compKnee",         (double) compKnee.load());
        root.setAttribute ("compScHP",         (double) compScHP.load());
        root.setAttribute ("compStyle",        compStyle.load());
        root.setAttribute ("limCeiling",       (double) limCeiling.load());
        root.setAttribute ("limRelease",       (double) limRelease.load());
        return root.toString();
    }

    void loadMasterChainFromXml (const juce::String& xmlStr)
    {
        auto parsed = juce::parseXML (xmlStr);
        if (parsed == nullptr) return;
        pultecLowBoost.store  ((float) parsed->getDoubleAttribute ("pultecLowBoost",  pultecLowBoost.load()));
        pultecLowAtten.store  ((float) parsed->getDoubleAttribute ("pultecLowAtten",  pultecLowAtten.load()));
        pultecLowFreq.store   ((float) parsed->getDoubleAttribute ("pultecLowFreq",   pultecLowFreq.load()));
        pultecHighBoost.store ((float) parsed->getDoubleAttribute ("pultecHighBoost", pultecHighBoost.load()));
        pultecHighAtten.store ((float) parsed->getDoubleAttribute ("pultecHighAtten", pultecHighAtten.load()));
        pultecHighFreq.store  ((float) parsed->getDoubleAttribute ("pultecHighFreq",  pultecHighFreq.load()));
        pultecHighBW.store    ((float) parsed->getDoubleAttribute ("pultecHighBW",    pultecHighBW.load()));
        pultecHiAttnFrq.store ((float) parsed->getDoubleAttribute ("pultecHiAttnFrq", pultecHiAttnFrq.load()));
        compThreshold.store   ((float) parsed->getDoubleAttribute ("compThreshold",   compThreshold.load()));
        compRatio.store       ((float) parsed->getDoubleAttribute ("compRatio",       compRatio.load()));
        compAttack.store      ((float) parsed->getDoubleAttribute ("compAttack",      compAttack.load()));
        compRelease.store     ((float) parsed->getDoubleAttribute ("compRelease",     compRelease.load()));
        compMakeup.store      ((float) parsed->getDoubleAttribute ("compMakeup",      compMakeup.load()));
        compKnee.store        ((float) parsed->getDoubleAttribute ("compKnee",        compKnee.load()));
        compScHP.store        ((float) parsed->getDoubleAttribute ("compScHP",        compScHP.load()));
        compStyle.store       (parsed->getIntAttribute ("compStyle", compStyle.load()));
        limCeiling.store      ((float) parsed->getDoubleAttribute ("limCeiling",      limCeiling.load()));
        limRelease.store      ((float) parsed->getDoubleAttribute ("limRelease",      limRelease.load()));
    }

    // ── Link channels: propagate sequencer params to linked tracks ──
    void propagateLink (bool isDrum, int trackIdx)
    {
        if (isDrum)
        {
            int grp = drumTracks[static_cast<size_t>(trackIdx)].linkGroup;
            if (grp <= 0) return;
            auto& src = drumTracks[static_cast<size_t>(trackIdx)];
            for (int i = 0; i < 10; ++i)
                if (i != trackIdx && drumTracks[static_cast<size_t>(i)].linkGroup == grp)
                {
                    auto& dst = drumTracks[static_cast<size_t>(i)];
                    dst.muted = src.muted; dst.solo = src.solo;
                    dst.length = src.length; dst.swing = src.swing;
                    dst.clockMul = src.clockMul; dst.playDir = src.playDir;
                    dst.volume = src.volume;
                }
        }
        else
        {
            int grp = synthTracks[static_cast<size_t>(trackIdx)].linkGroup;
            if (grp <= 0) return;
            auto& src = synthTracks[static_cast<size_t>(trackIdx)];
            for (int i = 0; i < 5; ++i)
                if (i != trackIdx && synthTracks[static_cast<size_t>(i)].linkGroup == grp)
                {
                    auto& dst = synthTracks[static_cast<size_t>(i)];
                    dst.muted = src.muted; dst.solo = src.solo;
                    dst.length = src.length; dst.swing = src.swing;
                    dst.clockMul = src.clockMul; dst.playDir = src.playDir;
                    dst.volume = src.volume;
                }
        }
    }

    // ── Sync step sequence to all linked tracks (called after step edits) ──
    void propagateLinkSteps (bool isDrum, int trackIdx)
    {
        if (isDrum)
        {
            int grp = drumTracks[static_cast<size_t>(trackIdx)].linkGroup;
            if (grp <= 0) return;
            auto& srcSeq = drumTracks[static_cast<size_t>(trackIdx)].seq;
            int srcLen = drumTracks[static_cast<size_t>(trackIdx)].length;
            for (int i = 0; i < 10; ++i)
                if (i != trackIdx && drumTracks[static_cast<size_t>(i)].linkGroup == grp)
                {
                    drumTracks[static_cast<size_t>(i)].seq = srcSeq;
                    drumTracks[static_cast<size_t>(i)].length = srcLen;
                }
        }
        else
        {
            int grp = synthTracks[static_cast<size_t>(trackIdx)].linkGroup;
            if (grp <= 0) return;
            auto& srcSeq = synthTracks[static_cast<size_t>(trackIdx)].seq;
            int srcLen = synthTracks[static_cast<size_t>(trackIdx)].length;
            for (int i = 0; i < 5; ++i)
                if (i != trackIdx && synthTracks[static_cast<size_t>(i)].linkGroup == grp)
                {
                    synthTracks[static_cast<size_t>(i)].seq = srcSeq;
                    synthTracks[static_cast<size_t>(i)].length = srcLen;
                }
        }
    }

private:
    void initDrumDefaults();
};
