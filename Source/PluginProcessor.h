#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Sequencer/SequencerEngine.h"
#include "Audio/DrumEngine/DrumSynth.h"
#include "Audio/SynthEngine/SynthEngine.h"
#include "Audio/FX/DuckEngine.h"
#include "Audio/FX/LFOEngine.h"
#include "Audio/FX/ArpEngine.h"
#include "Audio/FX/MasterPultecEQ.h"
#include "State/GrooveBoxState.h"
#include <vector>
#include <array>

class GrooveBoxProcessor : public juce::AudioProcessor
{
public:
    GrooveBoxProcessor();
    ~GrooveBoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GrooveBox Phonica"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    GrooveBoxState state;
    SequencerEngine sequencer;

    int getDrumPlayStep (int trackIdx) const { return sequencer.getDrumPlayingStep (trackIdx); }

    // Per-track peak levels for UI meters (set in processBlock, decayed in editor timer)
    std::array<std::atomic<float>, 10> drumLevels;
    std::array<std::atomic<float>, 5> synthLevels;

    // ── Granular Resample (public for editor finalize access) ──
    std::vector<float> resampleBuf;

private:
    DrumSynth drumSynth;
    SynthEngine synthEngine;
    std::vector<SeqEvent> eventBuffer;
    double currentSampleRate = 44100.0;

    // Per-track audio buffers
    std::array<juce::AudioBuffer<float>, 10> trackBuffers;

    // Delay lines (one per track, but we use track 0 as master for now)
    std::array<std::vector<float>, 10> delayLines;
    std::array<int, 10> delayWritePos {};

    // Reverb
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;

    // Pre-allocated render buffer (avoid malloc in audio thread)
    juce::AudioBuffer<float> stereoBuffer;
    float masterCompEnv = 0.0f;  // compressor envelope
    float compScHpL = 0.0f, compScHpR = 0.0f; // sidechain HP filter state (1-pole)
    bool wasTransportPlaying = false; // for MSEG transport sync detection
    float masterLimGain = 1.0f;  // limiter gain
    float masterLimGRdB = 0.0f;  // actual limiter GR in dB (negative)
    float hqPrevL = 0.0f, hqPrevR = 0.0f; // HQ mode: previous sample for 2x interpolation
    // Metronome
    double metroBeatAcc = 0.0;       // accumulator: samples since last beat
    double metroClickPhase = 0.0;    // synthesis phase
    int    metroClickSamplesLeft = 0; // samples remaining in current click
    int    metroBeatCount = 0;        // 0-3: which beat in bar (0=downbeat)
    uint32_t metroRng = 54321;        // noise RNG for rimshot
    MasterPultecEQ masterPultecEQ;

    // Master creative FX state
    float gaterPhase = 0.0f;
    float gaterEnv = 0.0f;
    float gaterFiltL1 = 0.0f, gaterFiltL2 = 0.0f;
    float gaterFiltR1 = 0.0f, gaterFiltR2 = 0.0f;
    std::vector<float> mDelayBufL, mDelayBufR;
    int mDelayWritePos = 0;
    float mDelaySmoothSamples = 0.0f; // smoothed read position for click-free
    float mDelayFBStateL = 0.0f, mDelayFBStateR = 0.0f;
    float mDelayHPStateL = 0.0f, mDelayHPStateR = 0.0f;
    float mDelayLPStateL = 0.0f, mDelayLPStateR = 0.0f;
    // Delay algo state (identical to per-track)
    float mDlyFilterL = 0.0f, mDlyFilterR = 0.0f;  // damping LP
    double mTapeWowPhase = 0.0;
    uint32_t mBbdRng = 12345;
    struct { float z = 0.0f; } mDiffAP[4];
    std::vector<float> mDelayBufRPP; // second buffer for ping-pong R channel
    float djFilterStateL1 = 0.0f, djFilterStateL2 = 0.0f;
    float djFilterStateR1 = 0.0f, djFilterStateR2 = 0.0f;
    float djFilterWet = 0.0f;     // smoothed wet amount for crossfade
    float djFilterModeBlend = 0.0f; // 0=LP, 1=HP, smooth transition

    // Ducking engine
    DuckEngine duckEngine;
    std::array<LFOEngine, 10> drumLFOs;
    std::array<LFOEngine, 5> synthLFOs;
    std::array<std::array<MSEGEngine, 3>, 10> drumMSEGs;
    std::array<std::array<MSEGEngine, 3>, 5> synthMSEGs;
    std::array<ArpEngine, 5> synthArps;

    // ── Granular Resample System ──
    static constexpr int kResampleMaxSamples = 480000; // 10 sec @ 48kHz
    std::vector<float> drumTapBuf;           // per-block tap from drum engine
    std::vector<float> synthTapBuf;          // per-block tap from synth engine
    int resampleWritePos = 0;
    bool wasResampleActive = false;
    bool wasResamplePlaying = false;  // transport state for resample sync

    // Arp cache: store last svp/smpBuf/gate per track for arp note generation
    std::array<SynthVoiceParams, 5> arpCachedSvp;
    std::array<std::shared_ptr<juce::AudioBuffer<float>>, 5> arpCachedBuf;
    std::array<float, 5> arpCachedGate {};

    // Keyboard arp state (for piano keyboard → arp integration)
    struct KbArpState {
        bool active = false;
        int sampleCounter = 0;
        int stepIdx = 0;            // which arp step (0..numSteps-1)
        int noteIdx = 0;            // which note in chord list
        int dirMul = 1;             // 1=ascending, -1=descending
        std::vector<std::pair<int,int>> notes; // (noteIndex 0-11, octave)
        float velocity = 0.8f;
    };
    std::array<KbArpState, 5> kbArp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveBoxProcessor)
};
