#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

GrooveBoxProcessor::GrooveBoxProcessor()
    : AudioProcessor (BusesProperties()
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    eventBuffer.reserve (256);
    for (auto& lv : drumLevels) lv.store (0.0f);
    for (auto& lv : synthLevels) lv.store (0.0f);
}

GrooveBoxProcessor::~GrooveBoxProcessor() {}

// ── Push warp markers from track state to synth engine ──
static void pushSynthWarpData (SynthEngine& engine, int trackIdx, const SynthTrackState& st)
{
    if (st.warpMarkers.empty() || st.smpBars <= 0 || st.smpWarp == 0)
    {
        engine.setWarpData (trackIdx, {}, 0.0f);
        return;
    }
    static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
    float totalBeats = barLUT[std::clamp (st.smpBars, 1, 8)] * 4.0f;
    // Use thread_local to avoid heap allocation per call
    thread_local std::vector<TimeStretch::WarpPt> pts;
    pts.clear();
    pts.reserve (st.warpMarkers.size());
    for (const auto& wm : st.warpMarkers)
        pts.push_back ({wm.samplePos, wm.beatPos});
    engine.setWarpData (trackIdx, pts, totalBeats);
}

// ── Apply arp step modulation directly to SynthVoiceParams ──
// Same target IDs as LFOEngine::applyModToSynth, but modifies svp for per-note control
static void applyArpModToSvp (SynthVoiceParams& p, int tgt, float v)
{
    if (std::abs (v) < 0.0001f) return;
    switch (tgt)
    {
        case 0:  p.tune      += v * 2.0f;    break;
        case 1:  p.cut       += v * 40.0f;   break;
        case 2:  p.res       += v * 0.3f;    break;
        case 3:  p.volume    *= 1.0f + v * 0.8f; break;
        // case 4: pan — not in svp, skip
        // case 5-8: FX sends — not in svp, skip
        case 9:  p.pwm       += v * 0.3f;    break;
        case 10: p.mix2      += v * 0.5f;    break;
        case 11: p.detune    += v * 6.0f;    break;
        case 12: p.subLevel  += v * 0.5f;    break;
        case 13: p.uniSpread += v * 0.5f;    break;
        case 14: p.fenv      += v * 0.5f;    break;
        case 15: p.aA        *= std::pow (4.0f, v); break;
        case 16: p.aD        *= std::pow (4.0f, v); break;
        case 17: p.aS        += v * 0.5f;    break;
        case 18: p.charAmt   += v * 0.5f;    break;
        case 19: p.fmLinAmt  += v * 50.0f;   break;
        case 20: p.cRatio    *= std::pow (2.0f, v); break;
        case 21: p.r2        *= std::pow (2.0f, v); break;
        case 22: p.l2        += v * 50.0f;   break;
        case 23: p.r3        *= std::pow (2.0f, v); break;
        case 24: p.l3        += v * 50.0f;   break;
        case 25: p.r4        *= std::pow (2.0f, v); break;
        case 26: p.l4        += v * 50.0f;   break;
        case 27: p.fmFeedback += v * 0.5f;   break;
        case 28: p.elemBow    += v * 0.5f;   break;
        case 29: p.elemBlow   += v * 0.5f;   break;
        case 30: p.elemStrike += v * 0.5f;   break;
        case 31: p.elemGeometry += v * 0.3f;  break;
        case 32: p.elemBright += v * 0.5f;    break;
        case 33: p.elemSpace  += v * 0.5f;    break;
        case 34: p.plaitsHarmonics += v * 0.5f; break;
        case 35: p.plaitsTimbre += v * 0.5f;  break;
        case 36: p.plaitsMorph += v * 0.5f;   break;
        case 37: p.plaitsDecay += v * 0.5f;   break;
        case 38: p.plaitsLpgColor += v * 0.5f; break;
        case 39: p.aR        *= std::pow (4.0f, v); break;
        // case 40-46: FX params — not per-voice, skip
        case 47: p.smpCut     += v * 40.0f;    break;
        case 48: p.smpRes     += v * 0.3f;     break;
        case 49: p.smpGain    *= 1.0f + v * 0.8f; break;
        case 50: p.smpStart   += v * 0.3f;     break;
        case 51: p.smpEnd     += v * 0.3f;     break;
        case 52: p.smpTune    += v * 12.0f;    break;
        case 53: p.smpFine    += v * 50.0f;    break;
        // Extended targets
        case 54: p.syncRatio  += v * 2.0f;     break;
        case 55: p.uniStereo += v * 0.5f;      break;
        case 56: p.fA        *= std::pow (4.0f, v); break;
        case 57: p.fD        *= std::pow (4.0f, v); break;
        case 58: p.fS        += v * 0.5f;      break;
        case 59: p.fR        *= std::pow (4.0f, v); break;
        case 60: p.fmLinRatio *= std::pow (2.0f, v); break;
        case 61: p.fmLinDecay *= std::pow (4.0f, v); break;
        // Wavetable targets (same IDs as LFO)
        case 70: p.wtPos1     += v * 0.5f;     break;
        case 71: p.wtPos2     += v * 0.5f;     break;
        case 72: p.wtMix      += v * 0.5f;     break;
        case 73: p.wtWarpAmt1 += v * 0.5f;     break;
        case 74: p.wtWarpAmt2 += v * 0.5f;     break;
        case 75: p.wtSubLevel += v * 0.5f;     break;
        // Granular targets
        case 76: p.grainPos     += v * 0.5f;  break;
        case 77: p.grainSize    += v * 50.0f;  break;
        case 78: p.grainDensity += v * 10.0f;  break;
        case 79: p.grainSpray   += v * 50.0f;  break;
        case 80: p.grainPitch   += v * 12.0f;  break;
        case 81: p.grainPan     += v * 0.5f;   break;
        case 82: p.grainScan    += v * 50.0f;  break;
        // Granular FM targets (83-88)
        case 83: p.grainTexture += v * 0.5f;  break;
        case 84: p.grainFmAmt  += v * 50.0f;  break;
        case 85: p.grainFmRatio += v * 4.0f;  break;
        case 86: p.grainFmDecay += v * 1.0f;  break;
        case 87: p.grainFmSus  += v * 0.5f;   break;
        case 88: p.grainFmSpread += v * 0.5f; break;
        // Sampler FM (89-90)
        case 89: p.smpFmAmt    += v * 50.0f;  break;
        case 90: p.smpFmRatio  += v * 4.0f;   break;
        case 93: p.smpFiltEnv  += v * 50.0f;  break;
        default: break;
    }
}

// Arp modulation for track-state targets (pan, FX sends, FX params)
// These can't go into svp because they're track-level, not per-voice
// Also updates synthSave so LFO restore doesn't wipe arp FX modulation
static void applyArpModToTrack (SynthTrackState& st, int tgt, float v)
{
    if (std::abs (v) < 0.0001f) return;
    switch (tgt)
    {
        case 4:  st.pan       += v * 0.5f;     break;
        case 5:  st.delayMix  += v * 0.5f;     break;
        case 6:  st.distAmt   += v * 50.0f;    break;
        case 7:  st.chorusMix += v * 0.5f;     break;
        case 8:  st.reverbMix += v * 0.5f;     break;
        case 40: st.chorusRate += v * 2.0f;    break;
        case 41: st.chorusDepth += v * 0.5f;   break;
        case 42: st.delayTime  += v * 200.0f;  break;
        case 43: st.delayFB    += v * 0.3f;    break;
        case 44: st.reverbSize += v * 0.5f;    break;
        case 45: st.reduxBits  += v * -8.0f;   break; // negative = more crush
        case 46: st.reduxRate  += v * 0.5f;    break;
        case 91: st.delayDamp  += v * 0.5f;    break;
        case 92: st.reverbDamp += v * 0.5f;    break;
        case 94: st.fxLP      *= std::pow (2.0f, v * 2.0f); break;
        case 95: st.fxHP      *= std::pow (2.0f, v * 2.0f); break;
        case 96: st.eqLow     += v * 12.0f;    break;
        case 97: st.eqMid     += v * 12.0f;    break;
        case 98: st.eqHigh    += v * 12.0f;    break;
        case 99: st.delayBeats *= std::pow (2.0f, v); break;
        case 100: st.grainFeedback += v * 0.5f;  break;
        case 101: st.grainTilt     += v * 50.0f;  break;
        case 102: st.grainUniDetune += v * 50.0f; break;
        case 103: st.grainUniStereo += v * 0.5f;  break;
        case 104: st.grainUniVoices += static_cast<int>(v * 4); break;
        case 105: st.elemContour   += v * 0.5f;   break;
        case 106: st.elemDamping   += v * 0.5f;   break;
        case 107: st.elemFlow      += v * 0.5f;   break;
        case 108: st.elemMallet    += v * 0.5f;   break;
        case 109: st.elemPitch     += v * 0.5f;   break;
        case 110: st.elemPosition  += v * 0.5f;   break;
        case 111: st.fmLinSustain  += v * 0.5f;   break;
        case 112: st.glide         += v * 0.5f;   break;
        case 113: st.smpA          *= std::pow (4.0f, v); break;
        case 114: st.smpD          *= std::pow (4.0f, v); break;
        case 115: st.smpS          += v * 0.5f;   break;
        case 116: st.smpR          *= std::pow (4.0f, v); break;
        case 117: st.smpFiltA      *= std::pow (4.0f, v); break;
        case 118: st.smpFiltD      *= std::pow (4.0f, v); break;
        case 119: st.smpFiltS      += v * 0.5f;   break;
        case 120: st.smpFiltR      *= std::pow (4.0f, v); break;
        case 121: st.smpFmEnvA     *= std::pow (4.0f, v); break;
        case 122: st.smpFmEnvD     *= std::pow (4.0f, v); break;
        case 123: st.smpFmEnvS     += v * 0.5f;   break;
        case 124: st.duckDepth     += v * 0.5f;   break;
        case 125: st.duckAtk       *= std::pow (4.0f, v); break;
        case 126: st.duckRel       *= std::pow (4.0f, v); break;
        case 127: st.dc2           *= std::pow (4.0f, v); break;
        case 128: st.dc3           *= std::pow (4.0f, v); break;
        case 129: st.dc4           *= std::pow (4.0f, v); break;
        case 130: st.cA            *= std::pow (4.0f, v); break;
        case 131: st.cD            *= std::pow (4.0f, v); break;
        case 132: st.cS            += v * 0.5f;   break;
        case 133: st.cR            *= std::pow (4.0f, v); break;
        case 134: st.cLevel        += v * 50.0f;  break;
        default: break;
    }
}

// Same as above but writes to SynthLFOSave so values survive LFO restore
template <typename SaveT>
static void applyArpModToSave (SaveT& ss, int tgt, float v)
{
    if (std::abs (v) < 0.0001f) return;
    switch (tgt)
    {
        case 4:  ss.pan       += v * 0.5f;     break;
        case 5:  ss.delayMix  += v * 0.5f;     break;
        case 6:  ss.distAmt   += v * 50.0f;    break;
        case 7:  ss.chorusMix += v * 0.5f;     break;
        case 8:  ss.reverbMix += v * 0.5f;     break;
        case 40: ss.chorusRate += v * 2.0f;    break;
        case 41: ss.chorusDepth += v * 0.5f;   break;
        case 42: ss.delayTime  += v * 200.0f;  break;
        case 43: ss.delayFB    += v * 0.3f;    break;
        case 44: ss.reverbSize += v * 0.5f;    break;
        case 45: ss.reduxBits  += v * -8.0f;   break; // negative = more crush
        case 46: ss.reduxRate  += v * 0.5f;    break;
        case 91: ss.delayDamp  += v * 0.5f;    break;
        case 92: ss.reverbDamp += v * 0.5f;    break;
        case 94: ss.fxLP      *= std::pow (2.0f, v * 2.0f); break;
        case 95: ss.fxHP      *= std::pow (2.0f, v * 2.0f); break;
        case 96: ss.eqLow     += v * 12.0f;    break;
        case 97: ss.eqMid     += v * 12.0f;    break;
        case 98: ss.eqHigh    += v * 12.0f;    break;
        case 99: ss.delayBeats *= std::pow (2.0f, v); break;
        case 100: ss.grainFeedback += v * 0.5f;  break;
        case 101: ss.grainTilt     += v * 50.0f;  break;
        case 102: ss.grainUniDetune += v * 50.0f; break;
        case 103: ss.grainUniStereo += v * 0.5f;  break;
        case 104: ss.grainUniVoices += static_cast<int>(v * 4); break;
        case 105: ss.elemContour   += v * 0.5f;   break;
        case 106: ss.elemDamping   += v * 0.5f;   break;
        case 107: ss.elemFlow      += v * 0.5f;   break;
        case 108: ss.elemMallet    += v * 0.5f;   break;
        case 109: ss.elemPitch     += v * 0.5f;   break;
        case 110: ss.elemPosition  += v * 0.5f;   break;
        case 111: ss.fmLinSustain  += v * 0.5f;   break;
        case 112: ss.glide         += v * 0.5f;   break;
        case 113: ss.smpA          *= std::pow (4.0f, v); break;
        case 114: ss.smpD          *= std::pow (4.0f, v); break;
        case 115: ss.smpS          += v * 0.5f;   break;
        case 116: ss.smpR          *= std::pow (4.0f, v); break;
        case 117: ss.smpFiltA      *= std::pow (4.0f, v); break;
        case 118: ss.smpFiltD      *= std::pow (4.0f, v); break;
        case 119: ss.smpFiltS      += v * 0.5f;   break;
        case 120: ss.smpFiltR      *= std::pow (4.0f, v); break;
        case 121: ss.smpFmEnvA     *= std::pow (4.0f, v); break;
        case 122: ss.smpFmEnvD     *= std::pow (4.0f, v); break;
        case 123: ss.smpFmEnvS     += v * 0.5f;   break;
        case 124: ss.duckDepth     += v * 0.5f;   break;
        case 125: ss.duckAtk       *= std::pow (4.0f, v); break;
        case 126: ss.duckRel       *= std::pow (4.0f, v); break;
        case 127: ss.dc2           *= std::pow (4.0f, v); break;
        case 128: ss.dc3           *= std::pow (4.0f, v); break;
        case 129: ss.dc4           *= std::pow (4.0f, v); break;
        case 130: ss.cA            *= std::pow (4.0f, v); break;
        case 131: ss.cD            *= std::pow (4.0f, v); break;
        case 132: ss.cS            += v * 0.5f;   break;
        case 133: ss.cR            *= std::pow (4.0f, v); break;
        case 134: ss.cLevel        += v * 50.0f;  break;
        default: break;
    }
}

void GrooveBoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    sequencer.prepare (sampleRate);
    drumSynth.prepare (sampleRate, samplesPerBlock);
    drumSynth.setTrackStates (state.drumTracks.data());
    synthEngine.prepare (sampleRate);
    synthEngine.setTrackStates (state.synthTracks.data());

    // Per-track buffers
    for (int i = 0; i < 10; ++i)
        trackBuffers[i].setSize (1, samplesPerBlock);

    // Per-track delay lines (max 2 sec)
    int maxDelaySamples = static_cast<int> (sampleRate * 2.0);
    for (int i = 0; i < 10; ++i)
    {
        delayLines[i].assign (static_cast<size_t> (maxDelaySamples), 0.0f);
        delayWritePos[i] = 0;
    }

    // Reverb
    reverbParams.roomSize = 0.6f;
    reverbParams.damping = 0.5f;
    reverbParams.wetLevel = 0.0f;
    reverbParams.dryLevel = 1.0f;
    reverbParams.width = 1.0f;
    reverb.setParameters (reverbParams);
    reverb.setSampleRate (sampleRate);

    // Pre-allocate stereo render buffer (NEVER malloc in audio thread!)
    stereoBuffer.setSize (2, samplesPerBlock);

    // Ducking engine
    duckEngine.prepare (sampleRate);
    for (auto& lfo : drumLFOs) lfo.prepare (sampleRate);
    for (auto& lfo : synthLFOs) lfo.prepare (sampleRate);
    for (auto& arp : synthArps) arp.prepare (static_cast<float>(sampleRate));

    // Master Pultec EQ
    masterPultecEQ.prepare (sampleRate);

    // Master delay buffer (max 2 sec)
    int mDelayMax = static_cast<int>(sampleRate * 2.0);
    mDelayBufL.assign (static_cast<size_t>(mDelayMax), 0.0f);
    mDelayBufR.assign (static_cast<size_t>(mDelayMax), 0.0f);
    mDelayWritePos = 0;

    // ── Resample buffer (10 sec mono) + tap buffers ──
    int resampleMax = static_cast<int>(sampleRate * 10.0);
    resampleBuf.assign (static_cast<size_t>(resampleMax), 0.0f);
    drumTapBuf.assign (static_cast<size_t>(samplesPerBlock), 0.0f);
    synthTapBuf.assign (static_cast<size_t>(samplesPerBlock), 0.0f);
    resampleWritePos = 0;
    wasResampleActive = false;
    wasResamplePlaying = false;
}

void GrooveBoxProcessor::releaseResources()
{
    reverb.reset();
}

// Waveshaper distortion (matches HTML makeDistCurve)
static float distort (float input, float amount)
{
    if (amount < 0.01f) return input;
    float k = amount * 300.0f;
    float pi = juce::MathConstants<float>::pi;
    return (pi + k) * input / (pi + k * std::abs (input));
}

// Console 8 bus decode (Airwindows-style): asin() inverse of per-channel sin() encode
// The clamp at ±1 creates natural bus saturation when multiple channels sum hot
static inline float consoleDecode (float x)
{
    return std::asin (std::clamp (x, -1.0f, 1.0f));
}

void GrooveBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = 0; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    int numSamples = buffer.getNumSamples();

    // ═══════════════════════════════════════
    // HOST TRANSPORT SYNC (only in EXT mode)
    // ═══════════════════════════════════════
    if (state.externalClock.load())
    {
        if (auto* playHead = getPlayHead())
        {
            auto posInfo = playHead->getPosition();
            if (posInfo.hasValue())
            {
                if (posInfo->getBpm().hasValue())
                {
                    float hostBPM = static_cast<float> (*posInfo->getBpm());
                    if (hostBPM >= 40.0f && hostBPM <= 300.0f)
                        state.bpm.store (hostBPM);
                }
                if (posInfo->getIsPlaying())
                {
                    if (! sequencer.isPlaying())
                    {
                        state.playing.store (true);
                        sequencer.setPlaying (true);
                    }
                }
                else
                {
                    if (sequencer.isPlaying())
                    {
                        state.playing.store (false);
                        sequencer.stop();
                        synthEngine.killAll();
                        drumSynth.killAll();
                        for (auto& arp : synthArps) arp.reset();
                    }
                }
            }
        }
    }

    // Sync sequencer state
    sequencer.setBPM (state.bpm.load());
    drumSynth.setBPM (state.bpm.load());
    synthEngine.setBPM (state.bpm.load());
    bool globalSolo = state.anySolo();
    drumSynth.setGlobalAnySolo (globalSolo);
    synthEngine.setGlobalAnySolo (globalSolo);
    sequencer.setGlobalSwing (state.globalSwing.load());
    sequencer.setFillMode (state.fillMode.load());
    sequencer.setScale (state.scaleRoot.load(), state.scaleType.load());

    if (state.playing.load() && !sequencer.isPlaying())
        sequencer.setPlaying (true);
    else if (!state.playing.load() && sequencer.isPlaying())
    {
        sequencer.stop();
        synthEngine.killAll();
        drumSynth.killAll();
        for (auto& arp : synthArps) arp.reset();
    }

    // ── MSEG Transport Sync: reset MSEGs on transport start ──
    {
        bool nowPlaying = sequencer.isPlaying();
        if (nowPlaying && !wasTransportPlaying)
        {
            // Transport just started → reset MSEGs with transportSync enabled
            for (int t = 0; t < 10; ++t)
                for (int mi = 0; mi < 3; ++mi)
                    drumMSEGs[static_cast<size_t>(t)][mi].transportReset();
            for (int t = 0; t < 5; ++t)
                for (int mi = 0; mi < 3; ++mi)
                    synthMSEGs[static_cast<size_t>(t)][mi].transportReset();
            // Reset metronome — trigger first click immediately
            // acc = samplesPerBeat so first check triggers exactly once
            // count = 3 so first +1%4 = 0 (downbeat)
            {
                float resetBpm = std::max (40.0f, state.bpm.load());
                metroBeatAcc = currentSampleRate * 60.0 / static_cast<double>(resetBpm);
                metroBeatCount = 3;
                metroClickSamplesLeft = 0;
                metroClickPhase = 0.0;
            }
        }
        wasTransportPlaying = nowPlaying;
    }

    // ═══════════════════════════════════════
    // SEQUENCER → EVENTS
    // ═══════════════════════════════════════
    sequencer.processBlock (numSamples, eventBuffer, state.drumTracks, state.synthTracks);

    // Update current step positions for Motion Rec + GUI playhead
    for (int t = 0; t < 10; ++t)
        state.drumCurrentStep[static_cast<size_t>(t)].store (sequencer.getDrumCurrentStep (t));
    for (int t = 0; t < 5; ++t)
        state.synthCurrentStep[static_cast<size_t>(t)].store (sequencer.getSynthCurrentStep (t));

    // ── KEYBOARD PREVIEW: pick up note-on/off from UI thread ──
    for (int t = 0; t < 5; ++t)
    {
        auto& st = state.synthTracks[static_cast<size_t>(t)];
        auto& ka = kbArp[static_cast<size_t>(t)];
        int noteOn = st.kbNoteOn.load();

        // ── New note pressed ──
        if (noteOn >= 0 && noteOn != st.kbLastNote)
        {
            int noteIdx = noteOn % 12;
            int octave = noteOn / 12 - 2;
            float vel = st.kbVelocity.load();

            // Build voice params
            SynthVoiceParams params;
            params.tune = st.tune; params.cut = st.cut; params.res = st.res;
            params.fenv = st.fenv;
            params.fA = st.fA; params.fD = st.fD; params.fS = st.fS; params.fR = st.fR;
            params.aA = st.aA; params.aD = st.aD; params.aS = st.aS; params.aR = st.aR;
            params.fType = st.fType; params.fPoles = st.fPoles; params.fModel = st.fModel;
            params.w1 = st.w1; params.w2 = st.w2; params.mix2 = st.mix2;
            params.pwm = st.pwm; params.subLevel = st.subLevel; params.volume = st.volume;

            // Build note list: root + chord notes
            std::vector<std::pair<int,int>> notes;
            notes.push_back ({noteIdx, octave});
            if (st.chordMode > 0 && !st.mono)
            {
                NoteInfo root;
                root.noteIndex = static_cast<uint8_t>(noteIdx);
                root.octave = static_cast<uint8_t>(octave);
                auto chordNotes = SequencerEngine::buildChord (
                    root, st.chordMode, st.chordInversion, st.chordVoicing,
                    sequencer.getScaleType(), sequencer.getScaleRoot());
                for (const auto& cn : chordNotes)
                    notes.push_back ({cn.noteIndex, cn.octave});
            }

            // Expand with octave range (ARP octaves setting)
            if (st.arp.enabled && st.arp.octaves > 1)
            {
                auto base = notes;
                for (int oc = 1; oc < st.arp.octaves; ++oc)
                    for (auto& n : base)
                        notes.push_back ({n.first, n.second + oc});
            }

            if (st.arp.enabled)
            {
                // ── ARP MODE: set up arp cycling ──
                ka.active = true;
                ka.notes = notes;
                ka.velocity = vel;
                ka.sampleCounter = 0;
                ka.stepIdx = 0;
                ka.noteIdx = 0;
                ka.dirMul = 1;

                // Sort by pitch for directional arp
                std::sort (ka.notes.begin(), ka.notes.end(),
                    [](const std::pair<int,int>& a, const std::pair<int,int>& b) {
                        return (a.second * 12 + a.first) < (b.second * 12 + b.first);
                    });
                if (st.arp.direction == 1) // DOWN
                    std::reverse (ka.notes.begin(), ka.notes.end());

                // Trigger first note immediately
                synthEngine.setMonoGlide (t, true, 0.005f); // mono glide for smooth arp transitions
                float stepBeats = st.arp.getDivisionBeats();
                float stepSec = (stepBeats * 60.0f) / std::max (20.0f, state.bpm.load());
                int gate100 = st.arp.steps[0].gate;
                float gateTime = stepSec * static_cast<float>(gate100) / 100.0f;
                auto& fn = ka.notes[0];
                synthEngine.noteOn (t, fn.first, fn.second, vel, params, st.model,
                                    std::max (0.02f, gateTime), st.sampleData);
            }
            else
            {
                // ── DIRECT MODE: sustain while held ──
                ka.active = false;
                float effectiveGlide = st.mono ? (st.glide * 0.5f) : 0.0f;
                synthEngine.setMonoGlide (t, st.mono, effectiveGlide);

                // Release previous keyboard notes before new ones (prevents voice pile-up)
                if (st.kbLastNote >= 0)
                    synthEngine.releaseVoices (t);

                // Trigger all notes (root + chord)
                for (auto& n : notes)
                    synthEngine.noteOn (t, n.first, n.second, vel, params, st.model,
                                        999.0f, st.sampleData);
            }

            st.kbLastNote = noteOn;
        }

        // ── ARP tick: advance to next note when step time elapses ──
        if (ka.active && st.kbLastNote >= 0)
        {
            ka.sampleCounter += numSamples;
            float stepBeats = st.arp.getDivisionBeats();
            float stepSec = (stepBeats * 60.0f) / std::max (20.0f, state.bpm.load());
            int stepSamples = std::max (64, static_cast<int>(stepSec * static_cast<float>(currentSampleRate)));

            while (ka.sampleCounter >= stepSamples)
            {
                ka.sampleCounter -= stepSamples;
                ka.stepIdx = (ka.stepIdx + 1) % std::max (1, st.arp.numSteps);

                // Advance note index based on direction
                int numNotes = static_cast<int>(ka.notes.size());
                if (numNotes > 0)
                {
                    int dir = st.arp.direction;
                    if (dir == 0 || dir == 1) // UP or DOWN (already sorted)
                        ka.noteIdx = (ka.noteIdx + 1) % numNotes;
                    else if (dir == 2 || dir == 3) // U/D or D/U
                    {
                        ka.noteIdx += ka.dirMul;
                        if (ka.noteIdx >= numNotes - 1) { ka.noteIdx = numNotes - 1; ka.dirMul = -1; }
                        if (ka.noteIdx <= 0) { ka.noteIdx = 0; ka.dirMul = 1; }
                    }
                    else if (dir == 4) // RND
                        ka.noteIdx = (ka.noteIdx + 3) % numNotes; // pseudo-random skip
                    else if (dir == 5) // CHORD — all at once
                        ka.noteIdx = 0; // will trigger all

                    // Per-step gate and velocity
                    auto& step = st.arp.steps[static_cast<size_t>(ka.stepIdx)];
                    float gateTime = stepSec * static_cast<float>(step.gate) / 100.0f;
                    float stepVel = ka.velocity * static_cast<float>(step.velocity) / 100.0f;
                    float minGate = 0.02f; // 20ms minimum to avoid clicks

                    // NO releaseVoices here — mono legato handles smooth transitions
                    // The gate timer handles note-off timing (staccato when gate < 100%)

                    // Build params fresh
                    SynthVoiceParams p;
                    p.tune = st.tune; p.cut = st.cut; p.res = st.res; p.fenv = st.fenv;
                    p.fA = st.fA; p.fD = st.fD; p.fS = st.fS; p.fR = st.fR;
                    p.aA = st.aA; p.aD = st.aD; p.aS = st.aS; p.aR = st.aR;
                    p.fType = st.fType; p.fPoles = st.fPoles; p.fModel = st.fModel;
                    p.w1 = st.w1; p.w2 = st.w2; p.mix2 = st.mix2;
                    p.pwm = st.pwm; p.subLevel = st.subLevel; p.volume = st.volume;

                    // Mono glide for smooth pitch transitions
                    synthEngine.setMonoGlide (t, true, 0.005f);

                    if (dir == 5 && numNotes > 1) // CHORD direction → all notes
                    {
                        for (auto& n : ka.notes)
                            synthEngine.noteOn (t, n.first, n.second, stepVel, p, st.model,
                                                std::max (minGate, gateTime), st.sampleData);
                    }
                    else // normal: single note
                    {
                        auto& n = ka.notes[static_cast<size_t>(ka.noteIdx)];
                        synthEngine.noteOn (t, n.first, n.second, stepVel, p, st.model,
                                            std::max (minGate, gateTime), st.sampleData);
                    }
                }
            }
        }

        // ── Note-off: release gate / stop arp ──
        if (st.kbNoteOff.load() && st.kbLastNote >= 0)
        {
            if (ka.active)
            {
                ka.active = false;
                ka.notes.clear();
            }
            synthEngine.releaseVoices (t);
            st.kbLastNote = -1;
            st.kbNoteOn.store (-1);
            st.kbNoteOff.store (false);
        }
    }

    // ═══════════════════════════════════════
    // LFO MODULATION (tick + apply to track states)
    // ═══════════════════════════════════════
    float currentBPM = state.bpm.load();

    // Save original values that LFOs will modulate, then apply
    struct DrumLFOSave { float pitch, decay, tone, volume, pan, delayMix, distAmt, click, drumCut, drumRes, drumFiltEnv, drumFiltA, drumFiltD, pitchDec, fmMix, fmRatio, fmDepth, fmDecay, fmNoise,
        chorusRate, chorusDepth, chorusMix, delayTime, delayFB, delayBeats, reverbSize, reverbMix, reduxBits, reduxRate,
        smpCut, smpRes, smpFiltEnv, smpFiltA, smpFiltD, smpFiltS, smpFiltR, smpTune, smpFine, smpGain, smpStart, smpEnd, smpFmAmt, smpFmRatio, smpFmEnvA, smpFmEnvD, smpFmEnvS, fxLP, fxHP, eqLow, eqMid, eqHigh, delayDamp, reverbDamp,
        duckDepth, duckAtk, duckRel, snap,
        smpA, smpD, smpS, smpR,
        er1Pitch1, er1Pitch2, er1PDec1, er1PDec2, er1Ring, er1XMod, er1Noise, er1NDec, er1Cut, er1Res, er1Decay, er1Drive;
        int delaySync, delayPP, delayAlgo, reverbAlgo, fmNoiseType, duckSrc, subModel, smpFType, smpFModel, smpFPoles; };
    struct SynthLFOSave { float tune, cut, res, volume, pan, delayMix, distAmt, chorusMix, reverbMix, pwm, mix2, detune, subLevel, uniSpread, fenv, aA, aD, aS, aR, charAmt, fmLinAmt, cRatio, r2, l2, r3, l3, r4, l4, fmFeedback, elemBow, elemBlow, elemStrike, elemGeometry, elemBright, elemSpace, plaitsHarmonics, plaitsTimbre, plaitsMorph, plaitsDecay, plaitsLpgColor,
        chorusRate, chorusDepth, delayTime, delayFB, delayBeats, reverbSize, reduxBits, reduxRate,
        smpCut, smpRes, smpFiltEnv, smpFiltA, smpFiltD, smpFiltS, smpFiltR, smpGain, smpStart, smpEnd, smpTune, smpFine, smpFmAmt, smpFmRatio, smpFmEnvA, smpFmEnvD, smpFmEnvS,
        syncRatio, uniStereo, fA, fD, fS, fR, fmLinRatio, fmLinDecay, fmLinSustain, fxLP, fxHP, eqLow, eqMid, eqHigh,
        wtPos1, wtPos2, wtMix, wtWarpAmt1, wtWarpAmt2, wtSubLevel,
        grainPos, grainSize, grainDensity, grainSpray, grainPitch, grainPan, grainTexture, grainScan,
        grainFmAmt, grainFmRatio, grainFmDecay, grainFmSus, delayDamp, grainFmSpread, reverbDamp,
        grainTilt, grainUniDetune, grainUniStereo, grainFeedback,
        elemContour, elemDamping, elemFlow, elemMallet, elemPitch, elemPosition,
        glide, smpA, smpD, smpS, smpR, duckDepth, duckAtk, duckRel,
        dc2, dc3, dc4, cA, cD, cS, cR, cLevel;
        int grainShape, grainDir, grainFmSnap, grainMode, grainUniVoices, grainQuantize; bool grainFreeze;
        int delayPP, delayAlgo, reverbAlgo, smpFType, smpFModel, smpFPoles; bool delaySync; };
    std::array<DrumLFOSave, 10> drumSave;
    std::array<SynthLFOSave, 5> synthSave;

    // P-lock clean state: tracks which params are "global" vs "p-locked"
    // When a p-lock step ends, the next block restores from clean
    static std::array<SynthLFOSave, 5> synthClean;
    static std::array<SynthLFOSave, 5> synthGlobal; // base state (no plock/trigless)
    static std::array<DrumLFOSave, 10> drumClean;
    static std::array<DrumLFOSave, 10> drumGlobal; // base state (no plock/trigless)
    static float smoothMasterVol;
    if (smoothMasterVol < 0.0001f && state.masterVolume.load() > 0.01f)
        smoothMasterVol = state.masterVolume.load(); // init on first block
    static std::array<bool, 5>  synthHadPlock {};
    static std::array<bool, 10> drumHadPlock {};

    for (int t = 0; t < 10; ++t)
    {
        auto& dt = state.drumTracks[static_cast<size_t>(t)];

        // ── P-lock cleanup for drums ──
        if (drumHadPlock[static_cast<size_t>(t)])
        {
            auto& cl = drumClean[static_cast<size_t>(t)];
            dt.pitch = cl.pitch; dt.decay = cl.decay; dt.tone = cl.tone; dt.volume = cl.volume; dt.pan = cl.pan;
            dt.delayMix = cl.delayMix; dt.distAmt = cl.distAmt; dt.click = cl.click;
            dt.drumCut = cl.drumCut; dt.drumRes = cl.drumRes; dt.drumFiltEnv = cl.drumFiltEnv;
            dt.drumFiltA = cl.drumFiltA; dt.drumFiltD = cl.drumFiltD; dt.pitchDec = cl.pitchDec;
            dt.fmMix = cl.fmMix; dt.fmRatio = cl.fmRatio; dt.fmDepth = cl.fmDepth; dt.fmDecay = cl.fmDecay; dt.fmNoise = cl.fmNoise;
            dt.chorusRate = cl.chorusRate; dt.chorusDepth = cl.chorusDepth; dt.chorusMix = cl.chorusMix;
            dt.delayTime = cl.delayTime; dt.delayFB = cl.delayFB; dt.delayBeats = cl.delayBeats;
            dt.reverbSize = cl.reverbSize; dt.reverbMix = cl.reverbMix;
            dt.reduxBits = cl.reduxBits; dt.reduxRate = cl.reduxRate;
            dt.smpCut = cl.smpCut; dt.smpRes = cl.smpRes;
            dt.smpFType = cl.smpFType; dt.smpFModel = cl.smpFModel; dt.smpFPoles = cl.smpFPoles;
            dt.smpFiltEnv = cl.smpFiltEnv; dt.smpFiltA = cl.smpFiltA; dt.smpFiltD = cl.smpFiltD; dt.smpFiltS = cl.smpFiltS; dt.smpFiltR = cl.smpFiltR;
            dt.smpTune = cl.smpTune; dt.smpFine = cl.smpFine;
            dt.smpGain = cl.smpGain; dt.smpStart = cl.smpStart; dt.smpEnd = cl.smpEnd;
            dt.smpFmAmt = cl.smpFmAmt; dt.smpFmRatio = cl.smpFmRatio;
            dt.smpFmEnvA = cl.smpFmEnvA; dt.smpFmEnvD = cl.smpFmEnvD; dt.smpFmEnvS = cl.smpFmEnvS;
            dt.fxLP = cl.fxLP; dt.fxHP = cl.fxHP; dt.eqLow = cl.eqLow; dt.eqMid = cl.eqMid; dt.eqHigh = cl.eqHigh;
            dt.delayDamp = cl.delayDamp; dt.reverbDamp = cl.reverbDamp;
            dt.delaySync = cl.delaySync; dt.delayPP = cl.delayPP;
            dt.delayAlgo = cl.delayAlgo; dt.reverbAlgo = cl.reverbAlgo;
            dt.fmNoiseType = cl.fmNoiseType; dt.duckSrc = cl.duckSrc;
            dt.duckDepth = cl.duckDepth; dt.duckAtk = cl.duckAtk; dt.duckRel = cl.duckRel;
            dt.er1Pitch1 = cl.er1Pitch1; dt.er1Pitch2 = cl.er1Pitch2;
            dt.er1PDec1 = cl.er1PDec1; dt.er1PDec2 = cl.er1PDec2;
            dt.er1Ring = cl.er1Ring; dt.er1XMod = cl.er1XMod;
            dt.er1Noise = cl.er1Noise; dt.er1NDec = cl.er1NDec;
            dt.er1Cut = cl.er1Cut; dt.er1Res = cl.er1Res;
            dt.er1Decay = cl.er1Decay; dt.er1Drive = cl.er1Drive;
            dt.snap = cl.snap; dt.subModel = cl.subModel;
            // DON'T reset drumHadPlock here — keep restoring until a new event clears it
        }

        drumSave[t] = { dt.pitch, dt.decay, dt.tone, dt.volume, dt.pan, dt.delayMix, dt.distAmt, dt.click, dt.drumCut, dt.drumRes, dt.drumFiltEnv, dt.drumFiltA, dt.drumFiltD, dt.pitchDec, dt.fmMix, dt.fmRatio, dt.fmDepth, dt.fmDecay, dt.fmNoise,
            dt.chorusRate, dt.chorusDepth, dt.chorusMix, dt.delayTime, dt.delayFB, dt.delayBeats, dt.reverbSize, dt.reverbMix, dt.reduxBits, dt.reduxRate,
            dt.smpCut, dt.smpRes, dt.smpFiltEnv, dt.smpFiltA, dt.smpFiltD, dt.smpFiltS, dt.smpFiltR, dt.smpTune, dt.smpFine, dt.smpGain, dt.smpStart, dt.smpEnd, dt.smpFmAmt, dt.smpFmRatio, dt.smpFmEnvA, dt.smpFmEnvD, dt.smpFmEnvS, dt.fxLP, dt.fxHP, dt.eqLow, dt.eqMid, dt.eqHigh, dt.delayDamp, dt.reverbDamp,
            dt.duckDepth, dt.duckAtk, dt.duckRel, dt.snap,
            dt.smpA, dt.smpD, dt.smpS, dt.smpR,
            dt.er1Pitch1, dt.er1Pitch2, dt.er1PDec1, dt.er1PDec2, dt.er1Ring, dt.er1XMod, dt.er1Noise, dt.er1NDec, dt.er1Cut, dt.er1Res, dt.er1Decay, dt.er1Drive,
            dt.delaySync ? 1 : 0, dt.delayPP, dt.delayAlgo, dt.reverbAlgo, dt.fmNoiseType, dt.duckSrc, dt.subModel, dt.smpFType, dt.smpFModel, dt.smpFPoles };
        drumClean[static_cast<size_t>(t)] = drumSave[static_cast<size_t>(t)];
        // Save global base state when no plock/trigless is active
        if (!drumHadPlock[static_cast<size_t>(t)])
            drumGlobal[static_cast<size_t>(t)] = drumSave[static_cast<size_t>(t)];
        drumLFOs[t].tick (dt.lfos, numSamples, currentBPM, 30);
        drumLFOs[t].applyToDrum (dt);
        // Reset external mod inputs (MSEG→LFO fills these below, take effect next block)
        for (int li = 0; li < 3; ++li)
        {
            state.drumLfoValues[static_cast<size_t>(t)][static_cast<size_t>(li)].store (drumLFOs[t].getValue (li));
            drumLFOs[t].extRateMod[li] = 0.0f;
            drumLFOs[t].extDepthMod[li] = 0.0f;
        }

        // LFO → MSEG cross-mod: if any LFO targets M.RT(33) or M.DP(34), feed into all 3 MSEGs
        for (int mi = 0; mi < 3; ++mi)
        {
            drumMSEGs[t][mi].extRateMod = 0.0f;
            drumMSEGs[t][mi].extDepthMod = 0.0f;
        }
        for (int li = 0; li < 3; ++li)
        {
            float lv = drumLFOs[t].getValue (li);
            if (std::abs (lv) < 0.0001f) continue;
            int ltgt = dt.lfos[static_cast<size_t>(li)].target;
            if (ltgt == 36) for (int mi = 0; mi < 3; ++mi) drumMSEGs[t][mi].extRateMod  += lv * 5.0f;
            if (ltgt == 37) for (int mi = 0; mi < 3; ++mi) drumMSEGs[t][mi].extDepthMod += lv * 0.5f;
        }

        // 3 MSEGs per drum track — each with independent target/depth
        for (int mi = 0; mi < 3; ++mi)
        {
            auto& mseg = dt.msegs[static_cast<size_t>(mi)];
            drumMSEGs[t][mi].setData (&mseg);
            if (mseg.numPoints >= 2)
            {
                float msegRaw = drumMSEGs[t][mi].tick (numSamples, currentBPM);
                float msegShape = (msegRaw - 0.5f) * 2.0f; // bipolar -1..+1
                float msegEffDepth = mseg.depth + drumMSEGs[t][mi].extDepthMod;
                if (std::abs (msegEffDepth) > 0.001f)
                {
                    float msegBipolar = msegShape * msegEffDepth;
                    int mtgt = mseg.target;
                    if (mtgt >= 27 && mtgt <= 32)
                    {
                        int cm = mtgt - 27;
                        int lfoIdx = cm / 2;
                        if (cm % 2 == 0) drumLFOs[t].extRateMod[lfoIdx]  += msegBipolar * 5.0f;
                        else             drumLFOs[t].extDepthMod[lfoIdx] += msegBipolar * 0.5f;
                    }
                    else
                        LFOEngine::applyModToDrum (dt, mtgt, msegBipolar);
                }
                // Extra routes (Serum-style multi-target)
                for (auto& route : mseg.extraRoutes)
                {
                    if (route.target >= 0 && std::abs (route.depth) > 0.0001f)
                        LFOEngine::applyModToDrum (dt, route.target, msegShape * route.depth);
                }
                state.drumMsegPhase[static_cast<size_t>(t)][static_cast<size_t>(mi)].store (drumMSEGs[t][mi].getPhase());
            }
        }
        // Cross-MSEG feeding: each MSEG receives the other 2's output values
        float dMsegOuts[3] = {};
        for (int mi = 0; mi < 3; ++mi)
            dMsegOuts[mi] = drumMSEGs[t][mi].getValue() * 2.0f - 1.0f; // bipolar
        drumMSEGs[t][0].crossMsegValue1 = dMsegOuts[1]; drumMSEGs[t][0].crossMsegValue2 = dMsegOuts[2];
        drumMSEGs[t][1].crossMsegValue1 = dMsegOuts[0]; drumMSEGs[t][1].crossMsegValue2 = dMsegOuts[2];
        drumMSEGs[t][2].crossMsegValue1 = dMsegOuts[0]; drumMSEGs[t][2].crossMsegValue2 = dMsegOuts[1];

        // ── Velocity & Key tracking modulation ──
        for (const auto& vr : dt.velRoutes)
            if (vr.target >= 0 && std::abs (vr.depth) > 0.001f)
                LFOEngine::applyModToDrum (dt, vr.target, dt.lastVelocity * vr.depth);
        for (const auto& kr : dt.keyRoutes)
            if (kr.target >= 0 && std::abs (kr.depth) > 0.001f)
                LFOEngine::applyModToDrum (dt, kr.target, ((static_cast<float>(dt.lastNote) - 60.0f) / 60.0f) * kr.depth);
    }
    for (int t = 0; t < 5; ++t)
    {
        auto& st = state.synthTracks[static_cast<size_t>(t)];

        // ── P-lock cleanup: if previous block had a p-lock, restore params to clean state ──
        // This prevents p-lock values from "leaking" to subsequent non-p-locked steps
        if (synthHadPlock[static_cast<size_t>(t)])
        {
            auto& cl = synthClean[static_cast<size_t>(t)];
            st.tune = cl.tune; st.cut = cl.cut; st.res = cl.res; st.volume = cl.volume; st.pan = cl.pan;
            st.delayMix = cl.delayMix; st.distAmt = cl.distAmt; st.chorusMix = cl.chorusMix; st.reverbMix = cl.reverbMix;
            st.pwm = cl.pwm; st.mix2 = cl.mix2; st.detune = cl.detune; st.subLevel = cl.subLevel;
            st.uniSpread = cl.uniSpread; st.fenv = cl.fenv;
            st.aA = cl.aA; st.aD = cl.aD; st.aS = cl.aS; st.aR = cl.aR;
            st.charAmt = cl.charAmt; st.fmLinAmt = cl.fmLinAmt;
            st.cRatio = cl.cRatio; st.r2 = cl.r2; st.l2 = cl.l2; st.r3 = cl.r3; st.l3 = cl.l3; st.r4 = cl.r4; st.l4 = cl.l4;
            st.fmFeedback = cl.fmFeedback;
            st.elemBow = cl.elemBow; st.elemBlow = cl.elemBlow; st.elemStrike = cl.elemStrike;
            st.elemGeometry = cl.elemGeometry; st.elemBright = cl.elemBright; st.elemSpace = cl.elemSpace;
            st.plaitsHarmonics = cl.plaitsHarmonics; st.plaitsTimbre = cl.plaitsTimbre; st.plaitsMorph = cl.plaitsMorph;
            st.plaitsDecay = cl.plaitsDecay; st.plaitsLpgColor = cl.plaitsLpgColor;
            st.chorusRate = cl.chorusRate; st.chorusDepth = cl.chorusDepth;
            st.delayTime = cl.delayTime; st.delayFB = cl.delayFB; st.delayBeats = cl.delayBeats;
            st.reverbSize = cl.reverbSize; st.reduxBits = cl.reduxBits; st.reduxRate = cl.reduxRate;
            st.smpCut = cl.smpCut; st.smpRes = cl.smpRes;
            st.smpFType = cl.smpFType; st.smpFModel = cl.smpFModel; st.smpFPoles = cl.smpFPoles;
            st.smpFiltEnv = cl.smpFiltEnv; st.smpFiltA = cl.smpFiltA; st.smpFiltD = cl.smpFiltD; st.smpFiltS = cl.smpFiltS; st.smpFiltR = cl.smpFiltR;
            st.smpGain = cl.smpGain;
            st.smpTune = cl.smpTune; st.smpFine = cl.smpFine;
            st.smpStart = cl.smpStart; st.smpEnd = cl.smpEnd;
            st.smpFmAmt = cl.smpFmAmt; st.smpFmRatio = cl.smpFmRatio;
            st.smpFmEnvA = cl.smpFmEnvA; st.smpFmEnvD = cl.smpFmEnvD; st.smpFmEnvS = cl.smpFmEnvS;
            st.fA = cl.fA; st.fD = cl.fD; st.fS = cl.fS; st.fR = cl.fR;
            st.syncRatio = cl.syncRatio; st.uniStereo = cl.uniStereo;
            st.fmLinRatio = cl.fmLinRatio; st.fmLinDecay = cl.fmLinDecay; st.fmLinSustain = cl.fmLinSustain;
            st.fxLP = cl.fxLP; st.fxHP = cl.fxHP; st.eqLow = cl.eqLow; st.eqMid = cl.eqMid; st.eqHigh = cl.eqHigh;
            st.delayDamp = cl.delayDamp; st.reverbDamp = cl.reverbDamp;
            st.delaySync = cl.delaySync; st.delayPP = cl.delayPP;
            st.delayAlgo = cl.delayAlgo; st.reverbAlgo = cl.reverbAlgo;
            st.wtPos1 = cl.wtPos1; st.wtPos2 = cl.wtPos2; st.wtMix = cl.wtMix;
            st.wtWarpAmt1 = cl.wtWarpAmt1; st.wtWarpAmt2 = cl.wtWarpAmt2; st.wtSubLevel = cl.wtSubLevel;
            st.grainPos = cl.grainPos; st.grainSize = cl.grainSize; st.grainDensity = cl.grainDensity;
            st.grainSpray = cl.grainSpray; st.grainPitch = cl.grainPitch; st.grainPan = cl.grainPan;
            st.grainTexture = cl.grainTexture; st.grainScan = cl.grainScan;
            st.grainFmAmt = cl.grainFmAmt; st.grainFmRatio = cl.grainFmRatio;
            st.grainFmDecay = cl.grainFmDecay; st.grainFmSus = cl.grainFmSus;
            st.grainFmSpread = cl.grainFmSpread;
            st.grainShape = cl.grainShape; st.grainDir = cl.grainDir;
            st.grainFmSnap = cl.grainFmSnap; st.grainFreeze = cl.grainFreeze;
            st.grainMode = cl.grainMode; st.grainTilt = cl.grainTilt;
            st.grainUniVoices = cl.grainUniVoices; st.grainUniDetune = cl.grainUniDetune; st.grainUniStereo = cl.grainUniStereo;
            st.grainQuantize = cl.grainQuantize; st.grainFeedback = cl.grainFeedback;
            // DON'T reset synthHadPlock here — keep restoring until a new event clears it
        }

        synthSave[t] = { st.tune, st.cut, st.res, st.volume, st.pan, st.delayMix, st.distAmt, st.chorusMix, st.reverbMix, st.pwm, st.mix2, st.detune, st.subLevel, st.uniSpread, st.fenv, st.aA, st.aD, st.aS, st.aR, st.charAmt, st.fmLinAmt, st.cRatio, st.r2, st.l2, st.r3, st.l3, st.r4, st.l4, st.fmFeedback, st.elemBow, st.elemBlow, st.elemStrike, st.elemGeometry, st.elemBright, st.elemSpace, st.plaitsHarmonics, st.plaitsTimbre, st.plaitsMorph, st.plaitsDecay, st.plaitsLpgColor,
            st.chorusRate, st.chorusDepth, st.delayTime, st.delayFB, st.delayBeats, st.reverbSize, st.reduxBits, st.reduxRate,
            st.smpCut, st.smpRes, st.smpFiltEnv, st.smpFiltA, st.smpFiltD, st.smpFiltS, st.smpFiltR, st.smpGain, st.smpStart, st.smpEnd, st.smpTune, st.smpFine, st.smpFmAmt, st.smpFmRatio, st.smpFmEnvA, st.smpFmEnvD, st.smpFmEnvS,
            st.syncRatio, st.uniStereo, st.fA, st.fD, st.fS, st.fR, st.fmLinRatio, st.fmLinDecay, st.fmLinSustain, st.fxLP, st.fxHP, st.eqLow, st.eqMid, st.eqHigh,
            st.wtPos1, st.wtPos2, st.wtMix, st.wtWarpAmt1, st.wtWarpAmt2, st.wtSubLevel,
            st.grainPos, st.grainSize, st.grainDensity, st.grainSpray, st.grainPitch, st.grainPan, st.grainTexture, st.grainScan,
            st.grainFmAmt, st.grainFmRatio, st.grainFmDecay, st.grainFmSus, st.delayDamp, st.grainFmSpread, st.reverbDamp,
            st.grainTilt, st.grainUniDetune, st.grainUniStereo, st.grainFeedback,
            st.elemContour, st.elemDamping, st.elemFlow, st.elemMallet, st.elemPitch, st.elemPosition,
            st.glide, st.smpA, st.smpD, st.smpS, st.smpR, st.duckDepth, st.duckAtk, st.duckRel,
            st.dc2, st.dc3, st.dc4, st.cA, st.cD, st.cS, st.cR, st.cLevel,
            st.grainShape, st.grainDir, st.grainFmSnap, st.grainMode, st.grainUniVoices, st.grainQuantize, st.grainFreeze,
            st.delayPP, st.delayAlgo, st.reverbAlgo, st.smpFType, st.smpFModel, st.smpFPoles, st.delaySync };
        // Update clean state (only when not p-locked — the save has clean values)
        synthClean[static_cast<size_t>(t)] = synthSave[static_cast<size_t>(t)];
        // Save global base state when no plock/trigless is active
        if (!synthHadPlock[static_cast<size_t>(t)])
            synthGlobal[static_cast<size_t>(t)] = synthSave[static_cast<size_t>(t)];
        synthLFOs[t].tick (st.lfos, numSamples, currentBPM, 62);
        synthLFOs[t].applyToSynth (st);
        // ── Live warp marker update (so dragging markers updates playback in real-time) ──
        if ((st.model == SynthModel::Sampler || st.model == SynthModel::Wavetable || st.model == SynthModel::Granular)
            && st.smpWarp > 0 && st.smpBars > 0 && !st.warpMarkers.empty())
            pushSynthWarpData (synthEngine, t, st);
        // Reset external mod inputs (MSEG→LFO fills these below, take effect next block)
        for (int li = 0; li < 3; ++li)
        {
            state.synthLfoValues[static_cast<size_t>(t)][static_cast<size_t>(li)].store (synthLFOs[t].getValue (li));
            synthLFOs[t].extRateMod[li] = 0.0f;
            synthLFOs[t].extDepthMod[li] = 0.0f;
        }

        // LFO → MSEG cross-mod: if any LFO targets M.RT(68) or M.DP(69), feed into all 3 MSEGs
        for (int mi = 0; mi < 3; ++mi)
        {
            synthMSEGs[t][mi].extRateMod = 0.0f;
            synthMSEGs[t][mi].extDepthMod = 0.0f;
        }
        for (int li = 0; li < 3; ++li)
        {
            float lv = synthLFOs[t].getValue (li);
            if (std::abs (lv) < 0.0001f) continue;
            int ltgt = st.lfos[static_cast<size_t>(li)].target;
            if (ltgt == 68) for (int mi = 0; mi < 3; ++mi) synthMSEGs[t][mi].extRateMod  += lv * 5.0f;
            if (ltgt == 69) for (int mi = 0; mi < 3; ++mi) synthMSEGs[t][mi].extDepthMod += lv * 0.5f;
        }

        // 3 MSEGs per synth track — each with independent target/depth
        for (int mi = 0; mi < 3; ++mi)
        {
            auto& mseg = st.msegs[static_cast<size_t>(mi)];
            synthMSEGs[t][mi].setData (&mseg);
            if (mseg.numPoints >= 2)
            {
                float msegRaw = synthMSEGs[t][mi].tick (numSamples, currentBPM);
                float msegShape = (msegRaw - 0.5f) * 2.0f; // bipolar -1..+1
                float msegEffDepth = mseg.depth + synthMSEGs[t][mi].extDepthMod;
                if (std::abs (msegEffDepth) > 0.001f)
                {
                    float msegBipolar = msegShape * msegEffDepth;
                    int mtgt = mseg.target;
                    if (mtgt >= 62 && mtgt <= 67)
                    {
                        int cm = mtgt - 62;
                        int lfoIdx = cm / 2;
                        if (cm % 2 == 0) synthLFOs[t].extRateMod[lfoIdx]  += msegBipolar * 5.0f;
                        else             synthLFOs[t].extDepthMod[lfoIdx] += msegBipolar * 0.5f;
                    }
                    else
                        LFOEngine::applyModToSynth (st, mtgt, msegBipolar);
                }
                // Extra routes (Serum-style multi-target)
                for (auto& route : mseg.extraRoutes)
                {
                    if (route.target >= 0 && std::abs (route.depth) > 0.0001f)
                        LFOEngine::applyModToSynth (st, route.target, msegShape * route.depth);
                }
                state.synthMsegPhase[static_cast<size_t>(t)][static_cast<size_t>(mi)].store (synthMSEGs[t][mi].getPhase());
                state.synthMsegAux[static_cast<size_t>(t)][static_cast<size_t>(mi)].store (synthMSEGs[t][mi].getAuxValue());
            }
        }
        // Cross-MSEG feeding
        float sMsegOuts[3] = {};
        for (int mi = 0; mi < 3; ++mi)
            sMsegOuts[mi] = synthMSEGs[t][mi].getValue() * 2.0f - 1.0f;
        synthMSEGs[t][0].crossMsegValue1 = sMsegOuts[1]; synthMSEGs[t][0].crossMsegValue2 = sMsegOuts[2];
        synthMSEGs[t][1].crossMsegValue1 = sMsegOuts[0]; synthMSEGs[t][1].crossMsegValue2 = sMsegOuts[2];
        synthMSEGs[t][2].crossMsegValue1 = sMsegOuts[0]; synthMSEGs[t][2].crossMsegValue2 = sMsegOuts[1];
        // Store for GUI visualization
        state.synthMsegCross[static_cast<size_t>(t)][0][0].store (sMsegOuts[1]);
        state.synthMsegCross[static_cast<size_t>(t)][0][1].store (sMsegOuts[2]);
        state.synthMsegCross[static_cast<size_t>(t)][1][0].store (sMsegOuts[0]);
        state.synthMsegCross[static_cast<size_t>(t)][1][1].store (sMsegOuts[2]);
        state.synthMsegCross[static_cast<size_t>(t)][2][0].store (sMsegOuts[0]);
        state.synthMsegCross[static_cast<size_t>(t)][2][1].store (sMsegOuts[1]);

        // ── Velocity & Key tracking modulation ──
        for (const auto& vr : st.velRoutes)
            if (vr.target >= 0 && std::abs (vr.depth) > 0.001f)
                LFOEngine::applyModToSynth (st, vr.target, st.lastVelocity * vr.depth);
        for (const auto& kr : st.keyRoutes)
            if (kr.target >= 0 && std::abs (kr.depth) > 0.001f)
                LFOEngine::applyModToSynth (st, kr.target, ((static_cast<float>(st.lastNote) - 60.0f) / 60.0f) * kr.depth);
    }

    // ═══════════════════════════════════════
    // MACRO MODULATION (4 macros → track params)
    // Additive: delta = macroValue * depth * range
    // When macro = 0, delta = 0, knob works freely
    // ═══════════════════════════════════════
    for (int mi = 0; mi < MacroEngine::kNumMacros; ++mi)
    {
        auto& mk = state.macroEngine.macros[static_cast<size_t>(mi)];
        if (mk.assignments.empty() || std::abs (mk.value) < 0.0001f) continue;

        for (const auto& a : mk.assignments)
        {
            float delta = mk.value * a.depth * (a.maxVal - a.minVal);
            if (std::abs (delta) < 0.0001f) continue;

            if (a.trackType == 0 && a.trackIndex >= 0 && a.trackIndex < 5)
            {
                auto& st = state.synthTracks[static_cast<size_t>(a.trackIndex)];
                // Additive — only write to st, NOT to synthSave (ss)
                if      (a.paramKey == "mix2")       st.mix2 += delta;
                else if (a.paramKey == "detune")     st.detune += delta;
                else if (a.paramKey == "sub")        st.subLevel += delta;
                else if (a.paramKey == "pwm")        st.pwm += delta;
                else if (a.paramKey == "tune")       st.tune += delta;
                else if (a.paramKey == "cut")        st.cut += delta;
                else if (a.paramKey == "res")        st.res += delta;
                else if (a.paramKey == "fenv")       st.fenv += delta;
                else if (a.paramKey == "fa")         st.fA += delta;
                else if (a.paramKey == "fd")         st.fD += delta;
                else if (a.paramKey == "fs")         st.fS += delta;
                else if (a.paramKey == "fr")         st.fR += delta;
                else if (a.paramKey == "aa")         st.aA += delta;
                else if (a.paramKey == "ad")         st.aD += delta;
                else if (a.paramKey == "as")         st.aS += delta;
                else if (a.paramKey == "ar")         st.aR += delta;
                else if (a.paramKey == "volume")     st.volume += delta;
                else if (a.paramKey == "pan")        st.pan += delta;
                else if (a.paramKey == "spr")        st.uniSpread += delta;
                else if (a.paramKey == "str")        st.uniStereo += delta;
                else if (a.paramKey == "ch.d")       st.charAmt += delta;
                else if (a.paramKey == "fm")         st.fmLinAmt += delta;
                else if (a.paramKey == "f.r")        st.fmLinRatio += delta;
                else if (a.paramKey == "fdc")        st.fmLinDecay += delta;
                else if (a.paramKey == "fss")        st.fmLinSustain += delta;
                else if (a.paramKey == "dst")        st.distAmt += delta;
                else if (a.paramKey == "bit")        st.reduxBits += delta;
                else if (a.paramKey == "s.r")        st.reduxRate += delta;
                else if (a.paramKey == "chr")        st.chorusMix += delta;
                else if (a.paramKey == "c.rt")       st.chorusRate += delta;
                else if (a.paramKey == "c.dp")       st.chorusDepth += delta;
                else if (a.paramKey == "dly")        st.delayMix += delta;
                else if (a.paramKey == "d.tm")       st.delayTime += delta;
                else if (a.paramKey == "d.fb")       st.delayFB += delta;
                else if (a.paramKey == "rev")        st.reverbMix += delta;
                else if (a.paramKey == "r.sz")       st.reverbSize += delta;
                else if (a.paramKey == "lp")         st.fxLP += delta;
                else if (a.paramKey == "hp")         st.fxHP += delta;
                else if (a.paramKey == "eqlo")       st.eqLow += delta;
                else if (a.paramKey == "eqmd")       st.eqMid += delta;
                else if (a.paramKey == "eqhi")       st.eqHigh += delta;
                else if (a.paramKey == "smpStart")   st.smpStart += delta;
                else if (a.paramKey == "smpEnd")     st.smpEnd += delta;
                else if (a.paramKey == "smpGain")    st.smpGain += delta;
                else if (a.paramKey == "smpCut")     st.smpCut += delta;
                else if (a.paramKey == "smpRes")     st.smpRes += delta;
                else if (a.paramKey == "smpTune")    st.smpTune += delta;
                else if (a.paramKey == "smpFine")    st.smpFine += delta;
                else if (a.paramKey == "smpFmAmt")   st.smpFmAmt += delta;
                else if (a.paramKey == "smpFmRatio") st.smpFmRatio += delta;
                else if (a.paramKey == "wtPos1")     st.wtPos1 += delta;
                else if (a.paramKey == "wtPos2")     st.wtPos2 += delta;
                else if (a.paramKey == "wtMix")      st.wtMix += delta;
                else if (a.paramKey == "grainPos")   st.grainPos += delta;
                else if (a.paramKey == "grainSize")  st.grainSize += delta;
                else if (a.paramKey == "grainPitch") st.grainPitch += delta;
                else if (a.paramKey == "grainDensity") st.grainDensity += delta;
                else if (a.paramKey == "grainSpray") st.grainSpray += delta;
                else if (a.paramKey == "grainPan")   st.grainPan += delta;
                else if (a.paramKey == "grainTexture") st.grainTexture += delta;
                else if (a.paramKey == "grainScan")  st.grainScan += delta;
                // Legacy keys
                else if (a.paramKey == "delayMix")   st.delayMix += delta;
                else if (a.paramKey == "reverbMix")  st.reverbMix += delta;
                else if (a.paramKey == "distAmt")    st.distAmt += delta;
                else if (a.paramKey == "charAmt")    st.charAmt += delta;
                else if (a.paramKey == "chorusMix")  st.chorusMix += delta;
                else if (a.paramKey == "fmLinAmt")   st.fmLinAmt += delta;
                else if (a.paramKey == "aA")         st.aA += delta;
                else if (a.paramKey == "aD")         st.aD += delta;
                else if (a.paramKey == "aS")         st.aS += delta;
                else if (a.paramKey == "aR")         st.aR += delta;
                // Knob paramKey aliases (match actual knob paramKey strings)
                else if (a.paramKey == "fxLP")       st.fxLP += delta;
                else if (a.paramKey == "fxHP")       st.fxHP += delta;
                else if (a.paramKey == "eqLow")      st.eqLow += delta;
                else if (a.paramKey == "eqMid")      st.eqMid += delta;
                else if (a.paramKey == "eqHigh")     st.eqHigh += delta;
                else if (a.paramKey == "reduxBits")  st.reduxBits += delta;
                else if (a.paramKey == "reduxRate")  st.reduxRate += delta;
                else if (a.paramKey == "chorusRate") st.chorusRate += delta;
                else if (a.paramKey == "chorusDepth")st.chorusDepth += delta;
                else if (a.paramKey == "delayTime")  st.delayTime += delta;
                else if (a.paramKey == "delayFB")    st.delayFB += delta;
                else if (a.paramKey == "delayDamp")  st.delayDamp += delta;
                else if (a.paramKey == "delayBeats") st.delayBeats += delta;
                else if (a.paramKey == "reverbSize") st.reverbSize += delta;
                else if (a.paramKey == "reverbDamp") st.reverbDamp += delta;
                else if (a.paramKey == "uniSpread")  st.uniSpread += delta;
                else if (a.paramKey == "uniStereo")  st.uniStereo += delta;
                // OTT
                else if (a.paramKey == "ottDepth")   st.ottDepth += delta;
                else if (a.paramKey == "ottUpward")  st.ottUpward += delta;
                else if (a.paramKey == "ottDownward")st.ottDownward += delta;
                // ProDist
                else if (a.paramKey == "proDistDrive") st.proDistDrive += delta;
                else if (a.paramKey == "proDistTone")  st.proDistTone += delta;
                else if (a.paramKey == "proDistMix")   st.proDistMix += delta;
                else if (a.paramKey == "proDistBias")  st.proDistBias += delta;
                // Duck
                else if (a.paramKey == "duckDepth")  st.duckDepth += delta;
                else if (a.paramKey == "duckAtk")    st.duckAtk += delta;
                else if (a.paramKey == "duckRel")    st.duckRel += delta;
                // Phaser
                else if (a.paramKey == "phaserMix")  st.phaserMix += delta;
                else if (a.paramKey == "phaserRate") st.phaserRate += delta;
                else if (a.paramKey == "phaserDepth")st.phaserDepth += delta;
                else if (a.paramKey == "phaserFB")   st.phaserFB += delta;
                // Flanger
                else if (a.paramKey == "flangerMix") st.flangerMix += delta;
                else if (a.paramKey == "flangerRate")st.flangerRate += delta;
                else if (a.paramKey == "flangerDepth")st.flangerDepth += delta;
                else if (a.paramKey == "flangerFB")  st.flangerFB += delta;
            }
            else if (a.trackType == 1 && a.trackIndex >= 0 && a.trackIndex < 10)
            {
                auto& dt = state.drumTracks[static_cast<size_t>(a.trackIndex)];
                // Additive — only write to dt, NOT to drumSave (ds)
                if      (a.paramKey == "pitch")      dt.pitch += delta;
                else if (a.paramKey == "decay")      dt.decay += delta;
                else if (a.paramKey == "tone")       dt.tone += delta;
                else if (a.paramKey == "click")      dt.click += delta;
                else if (a.paramKey == "p.dec")      dt.pitchDec += delta;
                else if (a.paramKey == "tune")       dt.tune += delta;
                else if (a.paramKey == "fmMix")      dt.fmMix += delta;
                else if (a.paramKey == "fm.rt")      dt.fmRatio += delta;
                else if (a.paramKey == "fm.dp")      dt.fmDepth += delta;
                else if (a.paramKey == "fm.dc")      dt.fmDecay += delta;
                else if (a.paramKey == "fmDepth")    dt.fmDepth += delta;
                else if (a.paramKey == "drumCut")    dt.drumCut += delta;
                else if (a.paramKey == "drumRes")    dt.drumRes += delta;
                else if (a.paramKey == "volume")     dt.volume += delta;
                else if (a.paramKey == "pan")        dt.pan += delta;
                else if (a.paramKey == "dst")        dt.distAmt += delta;
                else if (a.paramKey == "distAmt")    dt.distAmt += delta;
                else if (a.paramKey == "bit")        dt.reduxBits += delta;
                else if (a.paramKey == "s.r")        dt.reduxRate += delta;
                else if (a.paramKey == "chr")        dt.chorusMix += delta;
                else if (a.paramKey == "c.rt")       dt.chorusRate += delta;
                else if (a.paramKey == "c.dp")       dt.chorusDepth += delta;
                else if (a.paramKey == "dly")        dt.delayMix += delta;
                else if (a.paramKey == "delayMix")   dt.delayMix += delta;
                else if (a.paramKey == "d.tm")       dt.delayTime += delta;
                else if (a.paramKey == "d.fb")       dt.delayFB += delta;
                else if (a.paramKey == "rev")        dt.reverbMix += delta;
                else if (a.paramKey == "reverbMix")  dt.reverbMix += delta;
                else if (a.paramKey == "r.sz")       dt.reverbSize += delta;
                else if (a.paramKey == "lp")         dt.fxLP += delta;
                else if (a.paramKey == "hp")         dt.fxHP += delta;
                else if (a.paramKey == "eqlo")       dt.eqLow += delta;
                else if (a.paramKey == "eqmd")       dt.eqMid += delta;
                else if (a.paramKey == "eqhi")       dt.eqHigh += delta;
                else if (a.paramKey == "smpCut")     dt.smpCut += delta;
                else if (a.paramKey == "smpRes")     dt.smpRes += delta;
                else if (a.paramKey == "smpTune")    dt.smpTune += delta;
                else if (a.paramKey == "smpFine")    dt.smpFine += delta;
                else if (a.paramKey == "smpGain")    dt.smpGain += delta;
                else if (a.paramKey == "smpStart")   dt.smpStart += delta;
                else if (a.paramKey == "smpEnd")     dt.smpEnd += delta;
                else if (a.paramKey == "smpFmAmt")   dt.smpFmAmt += delta;
                else if (a.paramKey == "smpFmRatio") dt.smpFmRatio += delta;
                // Knob paramKey aliases (match actual knob paramKey strings)
                else if (a.paramKey == "fxLP")       dt.fxLP += delta;
                else if (a.paramKey == "fxHP")       dt.fxHP += delta;
                else if (a.paramKey == "eqLow")      dt.eqLow += delta;
                else if (a.paramKey == "eqMid")      dt.eqMid += delta;
                else if (a.paramKey == "eqHigh")     dt.eqHigh += delta;
                else if (a.paramKey == "reduxBits")  dt.reduxBits += delta;
                else if (a.paramKey == "reduxRate")  dt.reduxRate += delta;
                else if (a.paramKey == "chorusMix")  dt.chorusMix += delta;
                else if (a.paramKey == "chorusRate") dt.chorusRate += delta;
                else if (a.paramKey == "chorusDepth")dt.chorusDepth += delta;
                else if (a.paramKey == "delayTime")  dt.delayTime += delta;
                else if (a.paramKey == "delayFB")    dt.delayFB += delta;
                else if (a.paramKey == "delayDamp")  dt.delayDamp += delta;
                else if (a.paramKey == "delayBeats") dt.delayBeats += delta;
                else if (a.paramKey == "reverbSize") dt.reverbSize += delta;
                else if (a.paramKey == "reverbDamp") dt.reverbDamp += delta;
                // OTT
                else if (a.paramKey == "ottDepth")   dt.ottDepth += delta;
                else if (a.paramKey == "ottUpward")  dt.ottUpward += delta;
                else if (a.paramKey == "ottDownward")dt.ottDownward += delta;
                // ProDist
                else if (a.paramKey == "proDistDrive") dt.proDistDrive += delta;
                else if (a.paramKey == "proDistTone")  dt.proDistTone += delta;
                else if (a.paramKey == "proDistMix")   dt.proDistMix += delta;
                else if (a.paramKey == "proDistBias")  dt.proDistBias += delta;
                // Duck
                else if (a.paramKey == "duckDepth")  dt.duckDepth += delta;
                else if (a.paramKey == "duckAtk")    dt.duckAtk += delta;
                else if (a.paramKey == "duckRel")    dt.duckRel += delta;
                // Phaser
                else if (a.paramKey == "phaserMix")  dt.phaserMix += delta;
                else if (a.paramKey == "phaserRate") dt.phaserRate += delta;
                else if (a.paramKey == "phaserDepth")dt.phaserDepth += delta;
                else if (a.paramKey == "phaserFB")   dt.phaserFB += delta;
                // Flanger
                else if (a.paramKey == "flangerMix") dt.flangerMix += delta;
                else if (a.paramKey == "flangerRate")dt.flangerRate += delta;
                else if (a.paramKey == "flangerDepth")dt.flangerDepth += delta;
                else if (a.paramKey == "flangerFB")  dt.flangerFB += delta;
            }
        }
    }

    // ═══════════════════════════════════════
    // SUB-BLOCK RENDERING (stereo for pan support)
    // ═══════════════════════════════════════
    // Use pre-allocated buffer (no malloc in audio thread!)
    stereoBuffer.setSize (2, numSamples, false, false, true); // keep memory, no realloc if fits
    stereoBuffer.clear();

    // Clear resample tap buffers
    std::fill (drumTapBuf.begin(), drumTapBuf.begin() + numSamples, 0.0f);
    std::fill (synthTapBuf.begin(), synthTapBuf.begin() + numSamples, 0.0f);

    int eventIdx = 0;
    int currentSample = 0;

    while (currentSample < numSamples)
    {
        int nextEventSample = numSamples;
        if (eventIdx < static_cast<int>(eventBuffer.size()))
            nextEventSample = std::min (numSamples,
                eventBuffer[static_cast<size_t>(eventIdx)].sampleOffset);

        int samplesToRender = nextEventSample - currentSample;
        if (samplesToRender > 0)
        {
            // Process ducking envelope for this sub-block
            std::array<int, 15> dSrc {};
            std::array<float, 15> dDepth {}, dAtk {}, dRel {};
            for (int t = 0; t < 10; ++t)
            {
                dSrc[t] = state.drumTracks[static_cast<size_t>(t)].duckSrc;
                dDepth[t] = state.drumTracks[static_cast<size_t>(t)].duckDepth;
                dAtk[t] = state.drumTracks[static_cast<size_t>(t)].duckAtk;
                dRel[t] = state.drumTracks[static_cast<size_t>(t)].duckRel;
            }
            for (int t = 0; t < 5; ++t)
            {
                dSrc[10 + t] = state.synthTracks[static_cast<size_t>(t)].duckSrc;
                dDepth[10 + t] = state.synthTracks[static_cast<size_t>(t)].duckDepth;
                dAtk[10 + t] = state.synthTracks[static_cast<size_t>(t)].duckAtk;
                dRel[10 + t] = state.synthTracks[static_cast<size_t>(t)].duckRel;
            }
            for (int s = 0; s < samplesToRender; ++s)
                duckEngine.processSample (dSrc.data(), dDepth.data(), dAtk.data(), dRel.data(), 15);

            // Get current duck gains
            std::array<float, 10> drumDuckGains {};
            std::array<float, 5> synthDuckGains {};
            for (int t = 0; t < 10; ++t) drumDuckGains[t] = duckEngine.getGain (t);
            for (int t = 0; t < 5; ++t) synthDuckGains[t] = duckEngine.getGain (10 + t);

            float* renderL = stereoBuffer.getWritePointer (0) + currentSample;
            float* renderR = stereoBuffer.getWritePointer (1) + currentSample;

            // ── Wire resample taps before render ──
            {
                int src = state.resampleSrc.load();
                if (src >= 0 && src <= 9)
                {
                    drumSynth.setResampleTap (src, drumTapBuf.data() + currentSample, samplesToRender);
                    synthEngine.clearResampleTap();
                }
                else if (src >= 10 && src <= 14)
                {
                    synthEngine.setResampleTap (src - 10, synthTapBuf.data() + currentSample, samplesToRender);
                    drumSynth.clearResampleTap();
                }
                else
                {
                    drumSynth.clearResampleTap();
                    synthEngine.clearResampleTap();
                }
            }

            drumSynth.renderBlock (renderL, renderR, samplesToRender, drumDuckGains.data());
            synthEngine.renderBlock (renderL, renderR, samplesToRender, synthDuckGains.data());

            // ── Console 8 decode: asin() on master bus (Airwindows-style analog summing) ──
            for (int i = 0; i < samplesToRender; ++i)
            {
                renderL[i] = consoleDecode (renderL[i]);
                renderR[i] = consoleDecode (renderR[i]);
            }
        }

        while (eventIdx < static_cast<int>(eventBuffer.size()) &&
               eventBuffer[static_cast<size_t>(eventIdx)].sampleOffset <= nextEventSample)
        {
            const auto& ev = eventBuffer[static_cast<size_t>(eventIdx)];

            if (ev.type == SeqEvent::DrumTrigger)
            {
                if (! state.isEffectivelyMuted (ev.trackIndex))
                {
                    // Restore to global base before building drumParams
                    // (prevents previous step's trigless values from sticking)
                    if (drumHadPlock[static_cast<size_t>(ev.trackIndex)] && !ev.trigless)
                    {
                        auto& gl = drumGlobal[static_cast<size_t>(ev.trackIndex)];
                        auto& dtr = state.drumTracks[static_cast<size_t>(ev.trackIndex)];
                        // Restore ALL params from global base (identical to clean restore)
                        dtr.pitch = gl.pitch; dtr.decay = gl.decay; dtr.tone = gl.tone;
                        dtr.volume = gl.volume; dtr.pan = gl.pan; dtr.click = gl.click;
                        dtr.pitchDec = gl.pitchDec; dtr.snap = gl.snap;
                        dtr.drumCut = gl.drumCut; dtr.drumRes = gl.drumRes;
                        dtr.drumFiltEnv = gl.drumFiltEnv; dtr.drumFiltA = gl.drumFiltA; dtr.drumFiltD = gl.drumFiltD;
                        dtr.fmMix = gl.fmMix; dtr.fmRatio = gl.fmRatio; dtr.fmDepth = gl.fmDepth;
                        dtr.fmDecay = gl.fmDecay; dtr.fmNoise = gl.fmNoise;
                        dtr.distAmt = gl.distAmt;
                        dtr.chorusMix = gl.chorusMix; dtr.chorusRate = gl.chorusRate; dtr.chorusDepth = gl.chorusDepth;
                        dtr.delayMix = gl.delayMix; dtr.delayTime = gl.delayTime; dtr.delayFB = gl.delayFB;
                        dtr.delayBeats = gl.delayBeats; dtr.delayDamp = gl.delayDamp;
                        dtr.delaySync = gl.delaySync; dtr.delayPP = gl.delayPP; dtr.delayAlgo = gl.delayAlgo;
                        dtr.reverbMix = gl.reverbMix; dtr.reverbSize = gl.reverbSize;
                        dtr.reverbDamp = gl.reverbDamp; dtr.reverbAlgo = gl.reverbAlgo;
                        dtr.reduxBits = gl.reduxBits; dtr.reduxRate = gl.reduxRate;
                        dtr.smpCut = gl.smpCut; dtr.smpRes = gl.smpRes;
                        dtr.smpFType = gl.smpFType; dtr.smpFModel = gl.smpFModel; dtr.smpFPoles = gl.smpFPoles;
                        dtr.smpFiltEnv = gl.smpFiltEnv; dtr.smpFiltA = gl.smpFiltA; dtr.smpFiltD = gl.smpFiltD; dtr.smpFiltS = gl.smpFiltS; dtr.smpFiltR = gl.smpFiltR;
                        dtr.smpTune = gl.smpTune;
                        dtr.smpFine = gl.smpFine; dtr.smpGain = gl.smpGain;
                        dtr.smpStart = gl.smpStart; dtr.smpEnd = gl.smpEnd;
                        dtr.smpFmAmt = gl.smpFmAmt; dtr.smpFmRatio = gl.smpFmRatio;
                        dtr.smpFmEnvA = gl.smpFmEnvA; dtr.smpFmEnvD = gl.smpFmEnvD; dtr.smpFmEnvS = gl.smpFmEnvS;
                        dtr.fxLP = gl.fxLP; dtr.fxHP = gl.fxHP;
                        dtr.eqLow = gl.eqLow; dtr.eqMid = gl.eqMid; dtr.eqHigh = gl.eqHigh;
                        dtr.fmNoiseType = gl.fmNoiseType; dtr.duckSrc = gl.duckSrc;
                        dtr.duckDepth = gl.duckDepth; dtr.duckAtk = gl.duckAtk; dtr.duckRel = gl.duckRel;
                        dtr.er1Pitch1 = gl.er1Pitch1; dtr.er1Pitch2 = gl.er1Pitch2;
                        dtr.er1PDec1 = gl.er1PDec1; dtr.er1PDec2 = gl.er1PDec2;
                        dtr.er1Ring = gl.er1Ring; dtr.er1XMod = gl.er1XMod;
                        dtr.er1Noise = gl.er1Noise; dtr.er1NDec = gl.er1NDec;
                        dtr.er1Cut = gl.er1Cut; dtr.er1Res = gl.er1Res;
                        dtr.er1Decay = gl.er1Decay; dtr.er1Drive = gl.er1Drive;
                        dtr.subModel = gl.subModel;
                        drumHadPlock[static_cast<size_t>(ev.trackIndex)] = false;
                        drumClean[static_cast<size_t>(ev.trackIndex)] = gl;
                        drumSave[static_cast<size_t>(ev.trackIndex)] = gl;
                    }

                    auto drumParams = state.drumTracks[static_cast<size_t>(ev.trackIndex)];

                    // ── Per-step sample slot (Octatrack-style) ──
                    if (ev.sampleSlot >= 0)
                    {
                        auto& slots = state.drumTracks[static_cast<size_t>(ev.trackIndex)].sampleSlots;
                        int si = static_cast<int>(ev.sampleSlot);
                        if (si < static_cast<int>(slots.size()) && slots[si] != nullptr)
                            drumParams.sampleData = slots[si];
                    }

                    // Apply P-locks
                    if (ev.plocks != nullptr)
                    {
                        for (const auto& [key, val] : *ev.plocks)
                        {
                            if      (key == "pitch")     drumParams.pitch = val;
                            else if (key == "pitchDec")  drumParams.pitchDec = val;
                            else if (key == "decay")     drumParams.decay = val;
                            else if (key == "click")     drumParams.click = val;
                            else if (key == "tune")      drumParams.tune = val;
                            else if (key == "tone")      drumParams.tone = val;
                            else if (key == "toneDecay")  drumParams.toneDecay = val;
                            else if (key == "noiseDecay") drumParams.noiseDecay = val;
                            else if (key == "snap")      drumParams.snap = val;
                            else if (key == "cutoff")    drumParams.cutoff = val;
                            else if (key == "freq")      drumParams.freq = val;
                            else if (key == "freq1")     drumParams.freq1 = val;
                            else if (key == "freq2")     drumParams.freq2 = val;
                            else if (key == "spread")    drumParams.spread = val;
                            else if (key == "noise")     drumParams.noise = val;
                            else if (key == "pitchEnd")  drumParams.pitchEnd = val;
                            else if (key == "distAmt")   drumParams.distAmt = val;
                            else if (key == "reduxBits") drumParams.reduxBits = val;
                            else if (key == "reduxRate") drumParams.reduxRate = val;
                            else if (key == "delayMix")  drumParams.delayMix = val;
                            else if (key == "delayTime") drumParams.delayTime = val;
                            else if (key == "delayFB")   drumParams.delayFB = val;
                            else if (key == "reverbMix") drumParams.reverbMix = val;
                            else if (key == "reverbSize")drumParams.reverbSize = val;
                            else if (key == "reverbDamp") drumParams.reverbDamp = val;
                            else if (key == "reverbAlgo") drumParams.reverbAlgo = static_cast<int>(val);
                            else if (key == "delayDamp")  drumParams.delayDamp = val;
                            else if (key == "delayAlgo")  drumParams.delayAlgo = static_cast<int>(val);
                            // ER-1 engine params
                            else if (key == "er1Wave1")   drumParams.er1Wave1 = static_cast<int>(val);
                            else if (key == "er1Pitch1")  drumParams.er1Pitch1 = val;
                            else if (key == "er1PDec1")   drumParams.er1PDec1 = val;
                            else if (key == "er1Wave2")   drumParams.er1Wave2 = static_cast<int>(val);
                            else if (key == "er1Pitch2")  drumParams.er1Pitch2 = val;
                            else if (key == "er1PDec2")   drumParams.er1PDec2 = val;
                            else if (key == "er1Ring")    drumParams.er1Ring = val;
                            else if (key == "er1XMod")    drumParams.er1XMod = val;
                            else if (key == "er1Noise")   drumParams.er1Noise = val;
                            else if (key == "er1NDec")    drumParams.er1NDec = val;
                            else if (key == "er1Cut")     drumParams.er1Cut = val;
                            else if (key == "er1Res")     drumParams.er1Res = val;
                            else if (key == "er1Decay")   drumParams.er1Decay = val;
                            else if (key == "er1Drive")   drumParams.er1Drive = val;
                            else if (key == "subModel")   drumParams.subModel = static_cast<int>(val);
                            else if (key == "drumCut")   drumParams.drumCut = val;
                            else if (key == "drumRes")   drumParams.drumRes = val;
                            else if (key == "drumFiltEnv") drumParams.drumFiltEnv = val;
                            else if (key == "drumFiltA") drumParams.drumFiltA = val;
                            else if (key == "drumFiltD") drumParams.drumFiltD = val;
                            else if (key == "fmMix")     drumParams.fmMix = val;
                            else if (key == "fmRatio")   drumParams.fmRatio = val;
                            else if (key == "fmDepth")   drumParams.fmDepth = val;
                            else if (key == "fmDecay")   drumParams.fmDecay = val;
                            else if (key == "fmNoise")   drumParams.fmNoise = val;
                            else if (key == "fmNoiseType") drumParams.fmNoiseType = static_cast<int>(val + 0.5f);
                            else if (key == "fxLP")      drumParams.fxLP = val;
                            else if (key == "fxHP")      drumParams.fxHP = val;
                            else if (key == "eqLow")     drumParams.eqLow = val;
                            else if (key == "eqMid")     drumParams.eqMid = val;
                            else if (key == "eqHigh")    drumParams.eqHigh = val;
                            else if (key == "delayBeats")drumParams.delayBeats = val;
                            else if (key == "pan")       drumParams.pan = val;
                            // LFO plocks (modify actual track state for next block)
                            else if (key == "lfo0Rate")   { float hz = LFOEngine::knobToHz(val); drumParams.lfos[0].rate = hz; }
                            else if (key == "lfo0Depth")  drumParams.lfos[0].depth = val;
                            else if (key == "lfo0Shape")  drumParams.lfos[0].shape = static_cast<int>(val);
                            else if (key == "lfo0Target") drumParams.lfos[0].target = static_cast<int>(val);
                            else if (key == "lfo0Sync")   drumParams.lfos[0].sync = (val > 0.5f);
                            else if (key == "lfo1Rate")   { float hz = LFOEngine::knobToHz(val); drumParams.lfos[1].rate = hz; }
                            else if (key == "lfo1Depth")  drumParams.lfos[1].depth = val;
                            else if (key == "lfo1Shape")  drumParams.lfos[1].shape = static_cast<int>(val);
                            else if (key == "lfo1Target") drumParams.lfos[1].target = static_cast<int>(val);
                            else if (key == "lfo1Sync")   drumParams.lfos[1].sync = (val > 0.5f);
                            else if (key == "lfo2Rate")   { float hz = LFOEngine::knobToHz(val); drumParams.lfos[2].rate = hz; }
                            else if (key == "lfo2Depth")  drumParams.lfos[2].depth = val;
                            else if (key == "lfo2Shape")  drumParams.lfos[2].shape = static_cast<int>(val);
                            else if (key == "lfo2Target") drumParams.lfos[2].target = static_cast<int>(val);
                            else if (key == "lfo2Sync")   drumParams.lfos[2].sync = (val > 0.5f);
                            // Sampler p-locks
                            else if (key == "smpStart")    drumParams.smpStart = val;
                            else if (key == "smpEnd")      drumParams.smpEnd = val;
                            else if (key == "smpGain")     drumParams.smpGain = val;
                            else if (key == "smpLoop")     drumParams.smpLoop = static_cast<int>(val);
                            else if (key == "smpPlayMode") drumParams.smpPlayMode = static_cast<int>(val);
                            else if (key == "smpReverse")  drumParams.smpReverse = static_cast<int>(val);
                            else if (key == "smpTune")     drumParams.smpTune = val;
                            else if (key == "smpFine")     drumParams.smpFine = val;
                            else if (key == "smpCut")      drumParams.smpCut = val;
                            else if (key == "smpRes")      drumParams.smpRes = val;
                            else if (key == "smpFType")    drumParams.smpFType = static_cast<int>(val);
                            else if (key == "smpFModel")   drumParams.smpFModel = static_cast<int>(val);
                            else if (key == "smpFPoles")   drumParams.smpFPoles = static_cast<int>(val);
                            else if (key == "smpFiltEnv")  drumParams.smpFiltEnv = val;
                            else if (key == "smpFiltA")    drumParams.smpFiltA = val;
                            else if (key == "smpFiltD")    drumParams.smpFiltD = val;
                            else if (key == "smpFiltS")    drumParams.smpFiltS = val;
                            else if (key == "smpFiltR")    drumParams.smpFiltR = val;
                            else if (key == "smpRootNote") drumParams.smpRootNote = static_cast<int>(val);
                            else if (key == "smpA")        drumParams.smpA = val;
                            else if (key == "smpD")        drumParams.smpD = val;
                            else if (key == "smpS")        drumParams.smpS = val;
                            else if (key == "smpR")        drumParams.smpR = val;
                            else if (key == "smpFmAmt")    drumParams.smpFmAmt = val;
                            else if (key == "smpFmRatio")  drumParams.smpFmRatio = val;
                            else if (key == "smpFmEnvA")   drumParams.smpFmEnvA = val;
                            else if (key == "smpFmEnvD")   drumParams.smpFmEnvD = val;
                            else if (key == "smpFmEnvS")   drumParams.smpFmEnvS = val;
                            else if (key == "smpStretch")  drumParams.smpStretch = val;
                            else if (key == "smpWarp")     drumParams.smpWarp = static_cast<int>(val);
                            else if (key == "smpBPM")      drumParams.smpBPM = val;
                            else if (key == "smpBpmSync")  drumParams.smpBpmSync = static_cast<int>(val);
                            else if (key == "smpSyncMul")  drumParams.smpSyncMul = static_cast<int>(val);
                            // Drum FX plocks → track state
                            else if (key == "chorusRate")   drumParams.chorusRate = val;
                            else if (key == "chorusDepth")  drumParams.chorusDepth = val;
                            else if (key == "chorusMix")    drumParams.chorusMix = val;
                            else if (key == "delaySync")    drumParams.delaySync = (val > 0.5f);
                            else if (key == "delayPP")      drumParams.delayPP = static_cast<int>(val);
                            else if (key == "volume")       drumParams.volume = val;
                            // Ducking
                            else if (key == "duckSrc")      drumParams.duckSrc = static_cast<int>(val);
                            else if (key == "duckDepth")    drumParams.duckDepth = val;
                            else if (key == "duckAtk")      drumParams.duckAtk = val;
                            else if (key == "duckRel")      drumParams.duckRel = val;
                        }
                    }

                    bool drumPlocked = (ev.plocks != nullptr && !ev.plocks->empty());

                    if (ev.trigless)
                    {
                        // ── TRIGLESS TRIG: apply p-locks to LIVE track state, no note trigger ──
                        // CRITICAL: also update drumSave so LFO restore doesn't overwrite!
                        if (drumPlocked)
                        {
                            auto& dt = state.drumTracks[static_cast<size_t>(ev.trackIndex)];
                            auto& ds = drumSave[static_cast<size_t>(ev.trackIndex)];
                            for (const auto& [key, val] : *ev.plocks)
                            {
                                if      (key == "pitch")      { dt.pitch = val; ds.pitch = val; }
                                else if (key == "decay")      { dt.decay = val; ds.decay = val; }
                                else if (key == "tone")       { dt.tone = val; ds.tone = val; }
                                else if (key == "volume")     { dt.volume = val; ds.volume = val; }
                                else if (key == "pan")        { dt.pan = val; ds.pan = val; }
                                else if (key == "click")      { dt.click = val; ds.click = val; }
                                else if (key == "pitchDec")   { dt.pitchDec = val; ds.pitchDec = val; }
                                else if (key == "pitchEnd")   dt.pitchEnd = val;
                                else if (key == "tune")       dt.tune = val;
                                else if (key == "toneDecay")  dt.toneDecay = val;
                                else if (key == "noiseDecay") dt.noiseDecay = val;
                                else if (key == "snap")       { dt.snap = val; ds.snap = val; }
                                else if (key == "cutoff")     dt.cutoff = val;
                                else if (key == "freq")       dt.freq = val;
                                else if (key == "freq1")      dt.freq1 = val;
                                else if (key == "freq2")      dt.freq2 = val;
                                else if (key == "spread")     dt.spread = val;
                                else if (key == "noise")      dt.noise = val;
                                else if (key == "drumCut")    { dt.drumCut = val; ds.drumCut = val; }
                                else if (key == "drumRes")    { dt.drumRes = val; ds.drumRes = val; }
                                else if (key == "drumFiltEnv") { dt.drumFiltEnv = val; ds.drumFiltEnv = val; }
                                else if (key == "drumFiltA")  { dt.drumFiltA = val; ds.drumFiltA = val; }
                                else if (key == "drumFiltD")  { dt.drumFiltD = val; ds.drumFiltD = val; }
                                else if (key == "distAmt")    { dt.distAmt = val; ds.distAmt = val; }
                                else if (key == "reduxBits")  { dt.reduxBits = val; ds.reduxBits = val; }
                                else if (key == "reduxRate")  { dt.reduxRate = val; ds.reduxRate = val; }
                                else if (key == "chorusMix")  { dt.chorusMix = val; ds.chorusMix = val; }
                                else if (key == "chorusRate") { dt.chorusRate = val; ds.chorusRate = val; }
                                else if (key == "chorusDepth") { dt.chorusDepth = val; ds.chorusDepth = val; }
                                else if (key == "delayMix")   { dt.delayMix = val; ds.delayMix = val; }
                                else if (key == "delayTime")  { dt.delayTime = val; ds.delayTime = val; }
                                else if (key == "delayFB")    { dt.delayFB = val; ds.delayFB = val; }
                                else if (key == "delayBeats") { dt.delayBeats = val; ds.delayBeats = val; }
                                else if (key == "delayDamp")  { dt.delayDamp = val; ds.delayDamp = val; }
                                else if (key == "reverbMix")  { dt.reverbMix = val; ds.reverbMix = val; }
                                else if (key == "reverbSize") { dt.reverbSize = val; ds.reverbSize = val; }
                                else if (key == "reverbDamp") { dt.reverbDamp = val; ds.reverbDamp = val; }
                                else if (key == "fxLP")       { dt.fxLP = val; ds.fxLP = val; }
                                else if (key == "fxHP")       { dt.fxHP = val; ds.fxHP = val; }
                                else if (key == "eqLow")      { dt.eqLow = val; ds.eqLow = val; }
                                else if (key == "eqMid")      { dt.eqMid = val; ds.eqMid = val; }
                                else if (key == "eqHigh")     { dt.eqHigh = val; ds.eqHigh = val; }
                                // Sampler params (all in DrumLFOSave)
                                else if (key == "smpCut")      { dt.smpCut = val; ds.smpCut = val; }
                                else if (key == "smpRes")      { dt.smpRes = val; ds.smpRes = val; }
                                else if (key == "smpFiltEnv")  { dt.smpFiltEnv = val; ds.smpFiltEnv = val; }
                                else if (key == "smpFiltA")    { dt.smpFiltA = val; ds.smpFiltA = val; }
                                else if (key == "smpFiltD")    { dt.smpFiltD = val; ds.smpFiltD = val; }
                                else if (key == "smpFiltS")    { dt.smpFiltS = val; ds.smpFiltS = val; }
                                else if (key == "smpFiltR")    { dt.smpFiltR = val; ds.smpFiltR = val; }
                                else if (key == "smpTune")     { dt.smpTune = val; ds.smpTune = val; }
                                else if (key == "smpFine")     { dt.smpFine = val; ds.smpFine = val; }
                                else if (key == "smpGain")     { dt.smpGain = val; ds.smpGain = val; }
                                else if (key == "smpStart")    { dt.smpStart = val; ds.smpStart = val; }
                                else if (key == "smpEnd")      { dt.smpEnd = val; ds.smpEnd = val; }
                                else if (key == "smpFmAmt")    { dt.smpFmAmt = val; ds.smpFmAmt = val; }
                                else if (key == "smpFmRatio")  { dt.smpFmRatio = val; ds.smpFmRatio = val; }
                                else if (key == "smpFmEnvA")   { dt.smpFmEnvA = val; ds.smpFmEnvA = val; }
                                else if (key == "smpFmEnvD")   { dt.smpFmEnvD = val; ds.smpFmEnvD = val; }
                                else if (key == "smpFmEnvS")   { dt.smpFmEnvS = val; ds.smpFmEnvS = val; }
                                // FM drum params (all in DrumLFOSave)
                                else if (key == "fmMix")       { dt.fmMix = val; ds.fmMix = val; }
                                else if (key == "fmRatio")     { dt.fmRatio = val; ds.fmRatio = val; }
                                else if (key == "fmDepth")     { dt.fmDepth = val; ds.fmDepth = val; }
                                else if (key == "fmDecay")     { dt.fmDecay = val; ds.fmDecay = val; }
                                else if (key == "fmNoise")     { dt.fmNoise = val; ds.fmNoise = val; }
                                // Int fields in DrumLFOSave
                                else if (key == "delaySync")   { dt.delaySync = static_cast<int>(val); ds.delaySync = static_cast<int>(val); }
                                else if (key == "delayPP")     { dt.delayPP = static_cast<int>(val); ds.delayPP = static_cast<int>(val); }
                                else if (key == "delayAlgo")   { dt.delayAlgo = static_cast<int>(val); ds.delayAlgo = static_cast<int>(val); }
                                else if (key == "reverbAlgo")  { dt.reverbAlgo = static_cast<int>(val); ds.reverbAlgo = static_cast<int>(val); }
                                // More sampler trigless params
                                else if (key == "smpLoop")     { dt.smpLoop = static_cast<int>(val); }
                                else if (key == "smpPlayMode") { dt.smpPlayMode = static_cast<int>(val); }
                                else if (key == "smpReverse")  { dt.smpReverse = static_cast<int>(val); }
                                else if (key == "smpRootNote") { dt.smpRootNote = static_cast<int>(val); }
                                else if (key == "smpA")        { dt.smpA = val; }
                                else if (key == "smpD")        { dt.smpD = val; }
                                else if (key == "smpS")        { dt.smpS = val; }
                                else if (key == "smpR")        { dt.smpR = val; }
                                else if (key == "smpFType")    { dt.smpFType = static_cast<int>(val); }
                                else if (key == "smpFModel")   { dt.smpFModel = static_cast<int>(val); }
                                else if (key == "smpFPoles")   { dt.smpFPoles = static_cast<int>(val); }
                                else if (key == "smpWarp")     { dt.smpWarp = static_cast<int>(val); }
                                else if (key == "smpBPM")      { dt.smpBPM = val; }
                                else if (key == "smpStretch")  { dt.smpStretch = val; }
                                else if (key == "smpBars")     { dt.smpBars = static_cast<int>(val); }
                                else if (key == "smpBpmSync")  { dt.smpBpmSync = static_cast<int>(val); }
                                else if (key == "smpSyncMul")  { dt.smpSyncMul = static_cast<int>(val); }
                                // FM noise type
                                else if (key == "fmNoiseType") { dt.fmNoiseType = static_cast<int>(val); ds.fmNoiseType = static_cast<int>(val); }
                                // Ducking
                                else if (key == "duckSrc")     { dt.duckSrc = static_cast<int>(val); ds.duckSrc = static_cast<int>(val); }
                                else if (key == "duckDepth")   { dt.duckDepth = val; ds.duckDepth = val; }
                                else if (key == "duckAtk")     { dt.duckAtk = val; ds.duckAtk = val; }
                                else if (key == "duckRel")     { dt.duckRel = val; ds.duckRel = val; }
                                // ER-1 engine params (float params in DrumLFOSave need ds update!)
                                else if (key == "er1Wave1")    { dt.er1Wave1 = static_cast<int>(val); }
                                else if (key == "er1Pitch1")   { dt.er1Pitch1 = val; ds.er1Pitch1 = val; }
                                else if (key == "er1PDec1")    { dt.er1PDec1 = val; ds.er1PDec1 = val; }
                                else if (key == "er1Wave2")    { dt.er1Wave2 = static_cast<int>(val); }
                                else if (key == "er1Pitch2")   { dt.er1Pitch2 = val; ds.er1Pitch2 = val; }
                                else if (key == "er1PDec2")    { dt.er1PDec2 = val; ds.er1PDec2 = val; }
                                else if (key == "er1Ring")     { dt.er1Ring = val; ds.er1Ring = val; }
                                else if (key == "er1XMod")     { dt.er1XMod = val; ds.er1XMod = val; }
                                else if (key == "er1Noise")    { dt.er1Noise = val; ds.er1Noise = val; }
                                else if (key == "er1NDec")     { dt.er1NDec = val; ds.er1NDec = val; }
                                else if (key == "er1Cut")      { dt.er1Cut = val; ds.er1Cut = val; }
                                else if (key == "er1Res")      { dt.er1Res = val; ds.er1Res = val; }
                                else if (key == "er1Decay")    { dt.er1Decay = val; ds.er1Decay = val; }
                                else if (key == "er1Drive")    { dt.er1Drive = val; ds.er1Drive = val; }
                                else if (key == "subModel")    { dt.subModel = static_cast<int>(val); ds.subModel = static_cast<int>(val); }
                            }
                            drumHadPlock[static_cast<size_t>(ev.trackIndex)] = true;
                            // Update drumClean so trigless persists across blocks
                            // (drumGlobal keeps the ORIGINAL values for next-step restore)
                            drumClean[static_cast<size_t>(ev.trackIndex)] = drumSave[static_cast<size_t>(ev.trackIndex)];
                        }
                    }
                    else
                    {
                        // ── Cross-type choke group: kill drum+synth tracks with same group ──
                        {
                            int grp = state.drumTracks[static_cast<size_t>(ev.trackIndex)].chokeGroup;
                            if (grp > 0)
                            {
                                for (int i = 0; i < 10; ++i)
                                    if (i != ev.trackIndex && state.drumTracks[static_cast<size_t>(i)].chokeGroup == grp)
                                        drumSynth.killTrack (i);
                                for (int i = 0; i < 5; ++i)
                                    if (state.synthTracks[static_cast<size_t>(i)].chokeGroup == grp)
                                        synthEngine.killPart (i);
                            }
                        }
                        drumSynth.trigger (ev.trackIndex, ev.velocity, drumParams, ev.gateScale, drumPlocked);
                        state.drumTracks[static_cast<size_t>(ev.trackIndex)].lastVelocity = ev.velocity;
                        duckEngine.triggerTrack (ev.trackIndex);
                        drumLFOs[static_cast<size_t>(ev.trackIndex)].retrigger (
                            state.drumTracks[static_cast<size_t>(ev.trackIndex)].lfos);
                        if (state.drumTracks[static_cast<size_t>(ev.trackIndex)].msegRetrig)
                        {
                            for (int mi = 0; mi < 3; ++mi)
                                drumMSEGs[static_cast<size_t>(ev.trackIndex)][mi].trigger();
                        }
                        drumLevels[static_cast<size_t>(ev.trackIndex)].store (
                            std::max (drumLevels[static_cast<size_t>(ev.trackIndex)].load(), ev.velocity));
                    }
                }
            }
            else if (ev.type == SeqEvent::SynthNoteOn)
            {
                if (! state.isSynthEffectivelyMuted (ev.trackIndex))
                {
                    // Track peak level for meter
                    synthLevels[static_cast<size_t>(ev.trackIndex)].store (
                        std::max (synthLevels[static_cast<size_t>(ev.trackIndex)].load(),
                                  ev.velocity * state.synthTracks[static_cast<size_t>(ev.trackIndex)].volume));

                    auto& st = state.synthTracks[static_cast<size_t>(ev.trackIndex)];

                    SynthVoiceParams svp;
                    // Oscillators
                    svp.w1 = st.w1;
                    svp.w2 = st.w2;
                    svp.tune = st.tune;
                    svp.detune = st.detune;
                    svp.mix2 = st.mix2;
                    svp.subLevel = st.subLevel;
                    svp.oscSync = st.oscSync;
                    svp.syncRatio = st.syncRatio;
                    svp.pwm = st.pwm;
                    svp.unison = st.unison;
                    svp.uniSpread = st.uniSpread;
                    svp.uniStereo = st.uniStereo;
                    svp.charType = st.charType;
                    svp.charAmt = st.charAmt;
                    svp.fmLinAmt = st.fmLinAmt;
                    svp.fmLinRatio = st.fmLinRatio;
                    svp.fmLinDecay = st.fmLinDecay;
                    svp.fmLinSustain = st.fmLinSustain;
                    svp.fmLinSnap = st.fmLinSnap;
                    // Filter
                    svp.fType = st.fType;
                    svp.fPoles = st.fPoles;
                    svp.fModel = st.fModel;
                    svp.cut = st.cut;
                    svp.res = st.res;
                    svp.fenv = st.fenv;
                    svp.fA = st.fA; svp.fD = st.fD; svp.fS = st.fS; svp.fR = st.fR;
                    // Amp
                    svp.aA = st.aA; svp.aD = st.aD; svp.aS = st.aS; svp.aR = st.aR;
                    svp.volume = st.volume;
                    // FM params (per-operator)
                    svp.fmAlgo = st.fmAlgo;
                    svp.cRatio = st.cRatio; svp.cLevel = st.cLevel;
                    svp.r2 = st.r2; svp.l2 = st.l2; svp.dc2 = st.dc2;
                    svp.r3 = st.r3; svp.l3 = st.l3; svp.dc3 = st.dc3;
                    svp.r4 = st.r4; svp.l4 = st.l4; svp.dc4 = st.dc4;
                    svp.fmFeedback = st.fmFeedback;
                    svp.cA = st.cA; svp.cD = st.cD; svp.cS = st.cS; svp.cR = st.cR;

                    // Elements modal synthesis
                    svp.elemBow = st.elemBow; svp.elemBlow = st.elemBlow;
                    svp.elemStrike = st.elemStrike; svp.elemContour = st.elemContour;
                    svp.elemMallet = st.elemMallet; svp.elemFlow = st.elemFlow;
                    svp.elemGeometry = st.elemGeometry; svp.elemBright = st.elemBright;
                    svp.elemDamping = st.elemDamping; svp.elemPosition = st.elemPosition;
                    svp.elemSpace = st.elemSpace;
                    svp.elemPitch = st.elemPitch;

                    // Plaits multi-model
                    svp.plaitsModel = st.plaitsModel;
                    svp.plaitsHarmonics = st.plaitsHarmonics;
                    svp.plaitsTimbre = st.plaitsTimbre;
                    svp.plaitsMorph = st.plaitsMorph;
                    svp.plaitsDecay = st.plaitsDecay;
                    svp.plaitsLpgColor = st.plaitsLpgColor;

                    // Sampler
                    svp.smpStart = st.smpStart; svp.smpEnd = st.smpEnd;
                    svp.smpGain = st.smpGain; svp.smpLoop = st.smpLoop;
                    svp.smpReverse = st.smpReverse; svp.smpPlayMode = st.smpPlayMode;
                    svp.smpTune = st.smpTune; svp.smpFine = st.smpFine;
                    svp.smpA = st.smpA; svp.smpD = st.smpD;
                    svp.smpS = st.smpS; svp.smpR = st.smpR;
                    svp.smpCut = st.smpCut; svp.smpRes = st.smpRes;
                    svp.smpFType = st.smpFType; svp.smpFModel = st.smpFModel; svp.smpFPoles = st.smpFPoles;
                    svp.smpRootNote = st.smpRootNote;
                    svp.smpStretch = st.smpStretch; svp.smpWarp = st.smpWarp; svp.smpBPM = st.smpBPM; svp.smpFileSR = st.smpFileSR; svp.smpBpmSync = st.smpBpmSync; svp.smpSyncMul = st.smpSyncMul; svp.smpBars = st.smpBars;
                    svp.smpFmAmt = st.smpFmAmt; svp.smpFmRatio = st.smpFmRatio;
                    svp.smpFmEnvA = st.smpFmEnvA; svp.smpFmEnvD = st.smpFmEnvD; svp.smpFmEnvS = st.smpFmEnvS;

                    // Wavetable
                    svp.wtPos1 = st.wtPos1; svp.wtPos2 = st.wtPos2; svp.wtMix = st.wtMix;
                    svp.wtWarpAmt1 = st.wtWarpAmt1; svp.wtWarpAmt2 = st.wtWarpAmt2;
                    svp.wtWarp1 = st.wtWarp1; svp.wtWarp2 = st.wtWarp2;
                    svp.wtSubLevel = st.wtSubLevel;

                    // Granular
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

                    // Get sample buffer pointer for sampler engine
                    std::shared_ptr<juce::AudioBuffer<float>> smpBuf;
                    if ((st.model == SynthModel::Sampler || st.model == SynthModel::Granular) && st.sampleData)
                        smpBuf = st.sampleData;

                    // ── Per-step sample slot (Octatrack-style) ──
                    if (ev.sampleSlot >= 0)
                    {
                        auto& slots = state.synthTracks[static_cast<size_t>(ev.trackIndex)].sampleSlots;
                        int si = static_cast<int>(ev.sampleSlot);
                        if (si < static_cast<int>(slots.size()) && slots[si] != nullptr)
                            smpBuf = slots[si];
                    }

                    // Apply P-locks if present
                    if (ev.plocks != nullptr)
                    {
                        for (const auto& [key, val] : *ev.plocks)
                        {
                            // Analog osc
                            if      (key == "w1")       svp.w1 = static_cast<int>(val);
                            else if (key == "w2")       svp.w2 = static_cast<int>(val);
                            else if (key == "detune")   svp.detune = val;
                            else if (key == "mix2")     svp.mix2 = val;
                            else if (key == "subLevel") svp.subLevel = val;
                            else if (key == "pwm")      svp.pwm = val;
                            else if (key == "unison")   svp.unison = static_cast<int>(val);
                            else if (key == "uniSpread")svp.uniSpread = val;
                            else if (key == "uniStereo")svp.uniStereo = val;
                            // Filter
                            else if (key == "cut")      svp.cut = val;
                            else if (key == "res")      svp.res = val;
                            else if (key == "fenv")     svp.fenv = val;
                            else if (key == "fType")    svp.fType = static_cast<int>(val);
                            else if (key == "fPoles")   svp.fPoles = static_cast<int>(val);
                            else if (key == "fModel")   svp.fModel = static_cast<int>(val);
                            // Filter ADSR
                            else if (key == "fA")       svp.fA = val;
                            else if (key == "fD")       svp.fD = val;
                            else if (key == "fS")       svp.fS = val;
                            else if (key == "fR")       svp.fR = val;
                            // Amp ADSR
                            else if (key == "aA")       svp.aA = val;
                            else if (key == "aD")       svp.aD = val;
                            else if (key == "aS")       svp.aS = val;
                            else if (key == "aR")       svp.aR = val;
                            // FM
                            else if (key == "fmAlgo")   svp.fmAlgo = static_cast<int>(val);
                            else if (key == "cRatio")   svp.cRatio = val;
                            else if (key == "cLevel")   svp.cLevel = val;
                            else if (key == "r2")       svp.r2 = val;
                            else if (key == "l2")       svp.l2 = val;
                            else if (key == "dc2")      svp.dc2 = val;
                            else if (key == "r3")       svp.r3 = val;
                            else if (key == "l3")       svp.l3 = val;
                            else if (key == "dc3")      svp.dc3 = val;
                            else if (key == "r4")       svp.r4 = val;
                            else if (key == "l4")       svp.l4 = val;
                            else if (key == "dc4")      svp.dc4 = val;
                            else if (key == "fmFeedback") svp.fmFeedback = val;
                            else if (key == "cA")       svp.cA = val;
                            else if (key == "cD")       svp.cD = val;
                            else if (key == "cS")       svp.cS = val;
                            else if (key == "cR")       svp.cR = val;
                            // Character (analog)
                            else if (key == "charType") svp.charType = static_cast<int>(val);
                            else if (key == "charAmt")  svp.charAmt = val;
                            else if (key == "fmLinAmt") svp.fmLinAmt = val;
                            else if (key == "fmLinRatio") svp.fmLinRatio = val;
                            else if (key == "fmLinDecay") svp.fmLinDecay = val;
                            else if (key == "fmLinSustain") svp.fmLinSustain = val;
                            else if (key == "fmLinSnap") svp.fmLinSnap = static_cast<int>(val);
                            // Elements modal
                            else if (key == "elemBow")     svp.elemBow = val;
                            else if (key == "elemBlow")    svp.elemBlow = val;
                            else if (key == "elemStrike")  svp.elemStrike = val;
                            else if (key == "elemContour") svp.elemContour = val;
                            else if (key == "elemMallet")  svp.elemMallet = val;
                            else if (key == "elemFlow")    svp.elemFlow = val;
                            else if (key == "elemGeometry") svp.elemGeometry = val;
                            else if (key == "elemBright")  svp.elemBright = val;
                            else if (key == "elemDamping") svp.elemDamping = val;
                            else if (key == "elemPosition") svp.elemPosition = val;
                            else if (key == "elemSpace")   svp.elemSpace = val;
                            else if (key == "elemPitch")   svp.elemPitch = val;
                            // Plaits
                            else if (key == "plaitsModel")     svp.plaitsModel = static_cast<int>(val);
                            else if (key == "plaitsHarmonics") svp.plaitsHarmonics = val;
                            else if (key == "plaitsTimbre")    svp.plaitsTimbre = val;
                            else if (key == "plaitsMorph")     svp.plaitsMorph = val;
                            else if (key == "plaitsDecay")     svp.plaitsDecay = val;
                            else if (key == "plaitsLpgColor")  svp.plaitsLpgColor = val;
                            // Sampler
                            else if (key == "smpStart")    svp.smpStart = val;
                            else if (key == "smpEnd")      svp.smpEnd = val;
                            else if (key == "smpGain")     svp.smpGain = val;
                            else if (key == "smpLoop")     svp.smpLoop = static_cast<int>(val);
                            else if (key == "smpPlayMode") svp.smpPlayMode = static_cast<int>(val);
                            else if (key == "smpReverse")  svp.smpReverse = static_cast<int>(val);
                            else if (key == "smpTune")     svp.smpTune = val;
                            else if (key == "smpFine")     svp.smpFine = val;
                            else if (key == "smpA")        svp.smpA = val;
                            else if (key == "smpD")        svp.smpD = val;
                            else if (key == "smpS")        svp.smpS = val;
                            else if (key == "smpR")        svp.smpR = val;
                            else if (key == "smpCut")      svp.smpCut = val;
                            else if (key == "smpRes")      svp.smpRes = val;
                            else if (key == "smpFType")    svp.smpFType = static_cast<int>(val);
                            else if (key == "smpFModel")   svp.smpFModel = static_cast<int>(val);
                            else if (key == "smpFPoles")   svp.smpFPoles = static_cast<int>(val);
                            else if (key == "smpFiltEnv")  svp.smpFiltEnv = val;
                            else if (key == "smpFiltA")    svp.smpFiltA = val;
                            else if (key == "smpFiltD")    svp.smpFiltD = val;
                            else if (key == "smpFiltS")    svp.smpFiltS = val;
                            else if (key == "smpFiltR")    svp.smpFiltR = val;
                            else if (key == "smpRootNote") svp.smpRootNote = static_cast<int>(val);
                            else if (key == "smpFmAmt")    svp.smpFmAmt = val;
                            else if (key == "smpFmRatio")  svp.smpFmRatio = val;
                            else if (key == "smpFmEnvA")   svp.smpFmEnvA = val;
                            else if (key == "smpFmEnvD")   svp.smpFmEnvD = val;
                            else if (key == "smpFmEnvS")   svp.smpFmEnvS = val;
                            else if (key == "smpStretch")  svp.smpStretch = val;
                            else if (key == "smpWarp")     svp.smpWarp = static_cast<int>(val);
                            else if (key == "smpBPM")      svp.smpBPM = val;
                            else if (key == "smpBpmSync")  svp.smpBpmSync = static_cast<int>(val);
                            else if (key == "smpSyncMul")  svp.smpSyncMul = static_cast<int>(val);
                            else if (key == "smpBars")     svp.smpBars = static_cast<int>(val);
                            else if (key == "smpFileSR")   svp.smpFileSR = val;
                            // Granular
                            else if (key == "grainPos")     svp.grainPos = val;
                            else if (key == "grainSize")    svp.grainSize = val;
                            else if (key == "grainDensity") svp.grainDensity = val;
                            else if (key == "grainSpray")   svp.grainSpray = val;
                            else if (key == "grainPitch")   svp.grainPitch = val;
                            else if (key == "grainPan")     svp.grainPan = val;
                            else if (key == "grainShape")   svp.grainShape = static_cast<int>(val);
                            else if (key == "grainDir")     svp.grainDir = static_cast<int>(val);
                            else if (key == "grainTexture") svp.grainTexture = val;
                            else if (key == "grainFreeze")  svp.grainFreeze = (val > 0.5f);
                            else if (key == "grainScan")    svp.grainScan = val;
                            else if (key == "grainFmAmt")   svp.grainFmAmt = val;
                            else if (key == "grainFmRatio") svp.grainFmRatio = val;
                            else if (key == "grainFmDecay") svp.grainFmDecay = val;
                            else if (key == "grainFmSus")   svp.grainFmSus = val;
                            else if (key == "grainFmSnap")  svp.grainFmSnap = static_cast<int>(val);
                            else if (key == "grainFmSpread") svp.grainFmSpread = val;
                            else if (key == "grainMode")    svp.grainMode = static_cast<int>(val);
                            else if (key == "grainTilt")    svp.grainTilt = val;
                            else if (key == "grainUniVoices") svp.grainUniVoices = static_cast<int>(val);
                            else if (key == "grainUniDetune") svp.grainUniDetune = val;
                            else if (key == "grainUniStereo") svp.grainUniStereo = val;
                            else if (key == "grainQuantize") svp.grainQuantize = static_cast<int>(val);
                            else if (key == "grainFeedback") svp.grainFeedback = val;
                            // Wavetable
                            else if (key == "wtPos1")       svp.wtPos1 = val;
                            else if (key == "wtPos2")       svp.wtPos2 = val;
                            else if (key == "wtMix")        svp.wtMix = val;
                            else if (key == "wtWarpAmt1")   svp.wtWarpAmt1 = val;
                            else if (key == "wtWarpAmt2")   svp.wtWarpAmt2 = val;
                            else if (key == "wtWarp1")      svp.wtWarp1 = static_cast<int>(val);
                            else if (key == "wtWarp2")      svp.wtWarp2 = static_cast<int>(val);
                            else if (key == "wtSubLevel")   svp.wtSubLevel = val;
                            // ── FX plocks: modify track state + flag for restore ──
                            // (synthHadPlock ensures clean restore next block)
                            else if (key == "distAmt")     { st.distAmt = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "reduxBits")   { st.reduxBits = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "reduxRate")   { st.reduxRate = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "chorusMix")   { st.chorusMix = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "chorusRate")  { st.chorusRate = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "chorusDepth") { st.chorusDepth = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "delayMix")    { st.delayMix = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "delayTime")   { st.delayTime = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "delayFB")     { st.delayFB = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "delayBeats")  { st.delayBeats = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "delayDamp")   { st.delayDamp = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "delaySync")   { st.delaySync = (val > 0.5f); synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "delayPP")     { st.delayPP = static_cast<int>(val); synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "delayAlgo")   { st.delayAlgo = static_cast<int>(val); synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "reverbMix")   { st.reverbMix = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "reverbSize")  { st.reverbSize = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "reverbDamp")  { st.reverbDamp = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "reverbAlgo")  { st.reverbAlgo = static_cast<int>(val); synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "fxLP")        { st.fxLP = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "fxHP")        { st.fxHP = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "eqLow")       { st.eqLow = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "eqMid")       { st.eqMid = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "eqHigh")      { st.eqHigh = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "pan")         { st.pan = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            else if (key == "volume")      { st.volume = val; synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true; }
                            // Ducking
                            else if (key == "duckSrc")     { st.duckSrc = static_cast<int>(val); }
                            else if (key == "duckDepth")   { st.duckDepth = val; }
                            else if (key == "duckAtk")     { st.duckAtk = val; }
                            else if (key == "duckRel")     { st.duckRel = val; }
                        }
                    }

                    // Gate duration from BPM
                    float stepSec = 60.0f / state.bpm.load() / 4.0f;
                    float gateDur = stepSec * ev.gateScale;
                    gateDur = std::max (0.02f, std::min (60.0f, gateDur));

                    // 303-style: MONO + SLIDE = legato glide, MONO + no slide = retrigger
                    // MONO = single voice only, chords play root only
                    bool effectiveMono = st.mono;
                    float effectiveGlide = 0.0f;
                    if (effectiveMono && ev.slide)
                        effectiveGlide = std::max (0.06f, st.glide); // min 60ms for 303 feel
                    synthEngine.setMonoGlide (ev.trackIndex, effectiveMono, effectiveGlide);

                    // In poly mode: DON'T kill old voices — let them fade via release envelope
                    // 8 voices per engine provide enough headroom for natural overlap
                    // Voice stealing (findFree) handles the rare case of all 8 busy
                    // In mono mode: SynthPart handles voice management internally

                    // ── ARP INTERCEPT: sequencer already fires notes with correct velocity/gate ──
                    // We just need to apply param lane modulation using the sequencer's arp step index
                    if (st.arp.enabled && !ev.trigless && ev.arpStepIndex >= 0)
                    {
                        int asi = ev.arpStepIndex % st.arp.getEffectiveLen();
                        const auto& arpStep = st.arp.steps[static_cast<size_t>(asi)];

                        // Apply arp param lane 1 modulation
                        float modBase = (arpStep.param - 0.5f) * 2.0f;
                        if (std::abs (st.arp.assignDepth) > 0.01f && st.arp.assignTarget >= 0)
                        {
                            float mv = modBase * st.arp.assignDepth;
                            applyArpModToSvp (svp, st.arp.assignTarget, mv);
                            applyArpModToTrack (st, st.arp.assignTarget, mv);
                            applyArpModToSave (synthSave[static_cast<size_t>(ev.trackIndex)], st.arp.assignTarget, mv);
                        }
                        for (int ri = 0; ri < 16; ++ri)
                        {
                            auto& route = st.arp.extraRoutes[static_cast<size_t>(ri)];
                            if (route.target >= 0 && std::abs (route.depth) > 0.01f)
                            {
                                float mv = modBase * route.depth;
                                applyArpModToSvp (svp, route.target, mv);
                                applyArpModToTrack (st, route.target, mv);
                                applyArpModToSave (synthSave[static_cast<size_t>(ev.trackIndex)], route.target, mv);
                            }
                        }
                        // Param lane 2
                        float modBase2 = (arpStep.param2 - 0.5f) * 2.0f;
                        if (std::abs (st.arp.assign2Depth) > 0.01f && st.arp.assign2Target >= 0)
                        {
                            float mv2 = modBase2 * st.arp.assign2Depth;
                            applyArpModToSvp (svp, st.arp.assign2Target, mv2);
                            applyArpModToTrack (st, st.arp.assign2Target, mv2);
                            applyArpModToSave (synthSave[static_cast<size_t>(ev.trackIndex)], st.arp.assign2Target, mv2);
                        }

                        // Retrigger LFO/MSEG/duck
                        duckEngine.triggerTrack (10 + ev.trackIndex);
                        synthLFOs[static_cast<size_t>(ev.trackIndex)].retrigger (st.lfos);
                        if (st.msegRetrig)
                            for (int mi = 0; mi < 3; ++mi)
                                synthMSEGs[static_cast<size_t>(ev.trackIndex)][mi].trigger();

                        // 303-style slide
                        bool arpSlide = (ev.gateScale >= 1.0f);
                        float effectiveGlide = 0.0f;
                        if (st.mono && arpSlide && st.glide > 0.001f)
                            effectiveGlide = std::max (0.04f, st.glide);
                        synthEngine.setMonoGlide (ev.trackIndex, st.mono, effectiveGlide);

                        // Gate duration: when slide (gate>=100%), extend well past the next step
                        // so the voice stays alive for legato transition (303-style tied notes)
                        float stepDur = (60.0f / std::max (20.0f, state.bpm.load())) / 4.0f;
                        float arpGateDur = arpSlide ? (stepDur * 4.0f)  // 4x step = guaranteed overlap
                                                    : (ev.gateScale * stepDur);
                        pushSynthWarpData (synthEngine, ev.trackIndex, st);
                        // Kill previous voice on this track before new trigger
                        // (prevents overlapping notes from sequencer steps)
                        // Only preserve old voice for mono+slide (303 legato)
                        if (!arpSlide || !st.mono)
                            synthEngine.killPart (ev.trackIndex);
                        synthEngine.noteOn (ev.trackIndex, ev.note.noteIndex, ev.note.octave,
                                            ev.velocity, svp, st.model, arpGateDur, smpBuf);
                        synthEngine.markPlocked (ev.trackIndex);
                    }
                    else if (st.arp.enabled && !ev.trigless)
                    {
                        std::vector<std::pair<int,int>> arpNotes;
                        arpNotes.push_back ({static_cast<int>(ev.note.noteIndex), static_cast<int>(ev.note.octave)});
                        for (const auto& cn : ev.chordNotes)
                            arpNotes.push_back ({static_cast<int>(cn.noteIndex), static_cast<int>(cn.octave)});
                        bool notesChanged = synthArps[static_cast<size_t>(ev.trackIndex)].setHeldNotes (arpNotes, st.arp);
                        if (notesChanged)
                            synthArps[static_cast<size_t>(ev.trackIndex)].retrigger (st.arp);

                        // Cache svp + smpBuf for arp to reuse when firing notes
                        arpCachedSvp[static_cast<size_t>(ev.trackIndex)] = svp;
                        arpCachedBuf[static_cast<size_t>(ev.trackIndex)] = smpBuf;
                        arpCachedGate[static_cast<size_t>(ev.trackIndex)] = gateDur;

                        // Still retrigger LFO/MSEG/duck
                        duckEngine.triggerTrack (10 + ev.trackIndex);
                        synthLFOs[static_cast<size_t>(ev.trackIndex)].retrigger (st.lfos);
                        if (st.msegRetrig)
                            for (int mi = 0; mi < 3; ++mi)
                                synthMSEGs[static_cast<size_t>(ev.trackIndex)][mi].trigger();
                    }
                    else if (ev.trigless)
                    {
                        // ── TRIGLESS TRIG: apply p-locks to LIVE track state, no note trigger ──
                        // CRITICAL: also update synthSave so LFO restore doesn't overwrite!
                        if (ev.plocks != nullptr && !ev.plocks->empty())
                        {
                            auto& ss = synthSave[static_cast<size_t>(ev.trackIndex)];
                            for (const auto& [key, val] : *ev.plocks)
                            {
                                // Oscillator
                                if      (key == "w1")        st.w1 = static_cast<int>(val);
                                else if (key == "w2")        st.w2 = static_cast<int>(val);
                                else if (key == "tune")      { st.tune = val; ss.tune = val; }
                                else if (key == "detune")    { st.detune = val; ss.detune = val; }
                                else if (key == "mix2")      { st.mix2 = val; ss.mix2 = val; }
                                else if (key == "subLevel")  { st.subLevel = val; ss.subLevel = val; }
                                else if (key == "oscSync")   st.oscSync = static_cast<int>(val);
                                else if (key == "syncRatio") { st.syncRatio = val; ss.syncRatio = val; }
                                else if (key == "pwm")       { st.pwm = val; ss.pwm = val; }
                                else if (key == "unison")    st.unison = static_cast<int>(val);
                                else if (key == "uniSpread") { st.uniSpread = val; ss.uniSpread = val; }
                                else if (key == "uniStereo") { st.uniStereo = val; ss.uniStereo = val; }
                                // Filter
                                else if (key == "cut")       { st.cut = val; ss.cut = val; }
                                else if (key == "res")       { st.res = val; ss.res = val; }
                                else if (key == "fenv")      { st.fenv = val; ss.fenv = val; }
                                else if (key == "fType")     st.fType = static_cast<int>(val);
                                else if (key == "fPoles")    st.fPoles = static_cast<int>(val);
                                else if (key == "fModel")    st.fModel = static_cast<int>(val);
                                else if (key == "fA")        { st.fA = val; ss.fA = val; }
                                else if (key == "fD")        { st.fD = val; ss.fD = val; }
                                else if (key == "fS")        { st.fS = val; ss.fS = val; }
                                else if (key == "fR")        { st.fR = val; ss.fR = val; }
                                // Amp
                                else if (key == "volume")    { st.volume = val; ss.volume = val; }
                                else if (key == "aA")        { st.aA = val; ss.aA = val; }
                                else if (key == "aD")        { st.aD = val; ss.aD = val; }
                                else if (key == "aS")        { st.aS = val; ss.aS = val; }
                                else if (key == "aR")        { st.aR = val; ss.aR = val; }
                                else if (key == "pan")       { st.pan = val; ss.pan = val; }
                                // Character + FM
                                else if (key == "charType")  st.charType = static_cast<int>(val);
                                else if (key == "charAmt")   { st.charAmt = val; ss.charAmt = val; }
                                else if (key == "fmLinAmt")  { st.fmLinAmt = val; ss.fmLinAmt = val; }
                                else if (key == "fmLinRatio") { st.fmLinRatio = val; ss.fmLinRatio = val; }
                                else if (key == "fmLinDecay") { st.fmLinDecay = val; ss.fmLinDecay = val; }
                                else if (key == "fmLinSustain") { st.fmLinSustain = val; ss.fmLinSustain = val; }
                                else if (key == "fmLinSnap") st.fmLinSnap = static_cast<int>(val);
                                // FM 4-op
                                else if (key == "fmAlgo")    st.fmAlgo = static_cast<int>(val);
                                else if (key == "cRatio")    { st.cRatio = val; ss.cRatio = val; }
                                else if (key == "cLevel")    st.cLevel = val;
                                else if (key == "r2")        { st.r2 = val; ss.r2 = val; }
                                else if (key == "l2")        { st.l2 = val; ss.l2 = val; }
                                else if (key == "dc2")       st.dc2 = val;
                                else if (key == "r3")        { st.r3 = val; ss.r3 = val; }
                                else if (key == "l3")        { st.l3 = val; ss.l3 = val; }
                                else if (key == "dc3")       st.dc3 = val;
                                else if (key == "r4")        { st.r4 = val; ss.r4 = val; }
                                else if (key == "l4")        { st.l4 = val; ss.l4 = val; }
                                else if (key == "dc4")       st.dc4 = val;
                                else if (key == "fmFeedback") { st.fmFeedback = val; ss.fmFeedback = val; }
                                else if (key == "cA")        st.cA = val;
                                else if (key == "cD")        st.cD = val;
                                else if (key == "cS")        st.cS = val;
                                else if (key == "cR")        st.cR = val;
                                // Elements
                                else if (key == "elemBow")     { st.elemBow = val; ss.elemBow = val; }
                                else if (key == "elemBlow")    { st.elemBlow = val; ss.elemBlow = val; }
                                else if (key == "elemStrike")  { st.elemStrike = val; ss.elemStrike = val; }
                                else if (key == "elemContour") st.elemContour = val;
                                else if (key == "elemMallet")  st.elemMallet = val;
                                else if (key == "elemFlow")    st.elemFlow = val;
                                else if (key == "elemGeometry") { st.elemGeometry = val; ss.elemGeometry = val; }
                                else if (key == "elemBright")  { st.elemBright = val; ss.elemBright = val; }
                                else if (key == "elemDamping")  st.elemDamping = val;
                                else if (key == "elemPosition") st.elemPosition = val;
                                else if (key == "elemSpace")   { st.elemSpace = val; ss.elemSpace = val; }
                                else if (key == "elemPitch")   st.elemPitch = val;
                                // Plaits
                                else if (key == "plaitsModel")     st.plaitsModel = static_cast<int>(val);
                                else if (key == "plaitsHarmonics") { st.plaitsHarmonics = val; ss.plaitsHarmonics = val; }
                                else if (key == "plaitsTimbre")    { st.plaitsTimbre = val; ss.plaitsTimbre = val; }
                                else if (key == "plaitsMorph")     { st.plaitsMorph = val; ss.plaitsMorph = val; }
                                else if (key == "plaitsDecay")     { st.plaitsDecay = val; ss.plaitsDecay = val; }
                                else if (key == "plaitsLpgColor")  { st.plaitsLpgColor = val; ss.plaitsLpgColor = val; }
                                // Sampler
                                else if (key == "smpStart")    { st.smpStart = val; ss.smpStart = val; }
                                else if (key == "smpEnd")      { st.smpEnd = val; ss.smpEnd = val; }
                                else if (key == "smpGain")     { st.smpGain = val; ss.smpGain = val; }
                                else if (key == "smpTune")     { st.smpTune = val; ss.smpTune = val; }
                                else if (key == "smpFine")     { st.smpFine = val; ss.smpFine = val; }
                                else if (key == "smpCut")      { st.smpCut = val; ss.smpCut = val; }
                                else if (key == "smpRes")      { st.smpRes = val; ss.smpRes = val; }
                                else if (key == "smpFType")    { st.smpFType = static_cast<int>(val); ss.smpFType = static_cast<int>(val); }
                                else if (key == "smpFModel")   { st.smpFModel = static_cast<int>(val); ss.smpFModel = static_cast<int>(val); }
                                else if (key == "smpFPoles")   { st.smpFPoles = static_cast<int>(val); ss.smpFPoles = static_cast<int>(val); }
                                else if (key == "smpFiltEnv")  { st.smpFiltEnv = val; ss.smpFiltEnv = val; }
                                else if (key == "smpFiltA")    { st.smpFiltA = val; ss.smpFiltA = val; }
                                else if (key == "smpFiltD")    { st.smpFiltD = val; ss.smpFiltD = val; }
                                else if (key == "smpFiltS")    { st.smpFiltS = val; ss.smpFiltS = val; }
                                else if (key == "smpFiltR")    { st.smpFiltR = val; ss.smpFiltR = val; }
                                else if (key == "smpFmAmt")    { st.smpFmAmt = val; ss.smpFmAmt = val; }
                                else if (key == "smpFmRatio")  { st.smpFmRatio = val; ss.smpFmRatio = val; }
                                else if (key == "smpFmEnvA")   { st.smpFmEnvA = val; ss.smpFmEnvA = val; }
                                else if (key == "smpFmEnvD")   { st.smpFmEnvD = val; ss.smpFmEnvD = val; }
                                else if (key == "smpFmEnvS")   { st.smpFmEnvS = val; ss.smpFmEnvS = val; }
                                // Wavetable
                                else if (key == "wtPos1")      { st.wtPos1 = val; ss.wtPos1 = val; }
                                else if (key == "wtPos2")      { st.wtPos2 = val; ss.wtPos2 = val; }
                                else if (key == "wtMix")       { st.wtMix = val; ss.wtMix = val; }
                                else if (key == "wtWarpAmt1")  { st.wtWarpAmt1 = val; ss.wtWarpAmt1 = val; }
                                else if (key == "wtWarpAmt2")  { st.wtWarpAmt2 = val; ss.wtWarpAmt2 = val; }
                                else if (key == "wtSubLevel")  { st.wtSubLevel = val; ss.wtSubLevel = val; }
                                // Granular
                                else if (key == "grainPos")     { st.grainPos = val; ss.grainPos = val; }
                                else if (key == "grainSize")    { st.grainSize = val; ss.grainSize = val; }
                                else if (key == "grainDensity") { st.grainDensity = val; ss.grainDensity = val; }
                                else if (key == "grainSpray")   { st.grainSpray = val; ss.grainSpray = val; }
                                else if (key == "grainPitch")   { st.grainPitch = val; ss.grainPitch = val; }
                                else if (key == "grainPan")     { st.grainPan = val; ss.grainPan = val; }
                                else if (key == "grainShape")   { st.grainShape = static_cast<int>(val); ss.grainShape = static_cast<int>(val); }
                                else if (key == "grainDir")     { st.grainDir = static_cast<int>(val); ss.grainDir = static_cast<int>(val); }
                                else if (key == "grainTexture") { st.grainTexture = val; ss.grainTexture = val; }
                                else if (key == "grainFreeze")  { st.grainFreeze = (val > 0.5f); ss.grainFreeze = (val > 0.5f); }
                                else if (key == "grainScan")    { st.grainScan = val; ss.grainScan = val; }
                                else if (key == "grainFmAmt")   { st.grainFmAmt = val; ss.grainFmAmt = val; }
                                else if (key == "grainFmRatio") { st.grainFmRatio = val; ss.grainFmRatio = val; }
                                else if (key == "grainFmDecay") { st.grainFmDecay = val; ss.grainFmDecay = val; }
                                else if (key == "grainFmSus")   { st.grainFmSus = val; ss.grainFmSus = val; }
                                else if (key == "grainFmSpread") { st.grainFmSpread = val; ss.grainFmSpread = val; }
                                else if (key == "grainFmSnap")  { st.grainFmSnap = static_cast<int>(val); ss.grainFmSnap = static_cast<int>(val); }
                                else if (key == "grainMode")    { st.grainMode = static_cast<int>(val); ss.grainMode = static_cast<int>(val); }
                                else if (key == "grainTilt")    { st.grainTilt = val; ss.grainTilt = val; }
                                else if (key == "grainUniVoices") { st.grainUniVoices = static_cast<int>(val); ss.grainUniVoices = static_cast<int>(val); }
                                else if (key == "grainUniDetune") { st.grainUniDetune = val; ss.grainUniDetune = val; }
                                else if (key == "grainQuantize") { st.grainQuantize = static_cast<int>(val); ss.grainQuantize = static_cast<int>(val); }
                                else if (key == "grainFeedback") { st.grainFeedback = val; ss.grainFeedback = val; }
                                // Wavetable warp type (write to track state only — not in save struct)
                                else if (key == "wtWarp1")     { st.wtWarp1 = static_cast<int>(val); }
                                else if (key == "wtWarp2")     { st.wtWarp2 = static_cast<int>(val); }
                                // FX
                                else if (key == "distAmt")   { st.distAmt = val; ss.distAmt = val; }
                                else if (key == "delayMix")  { st.delayMix = val; ss.delayMix = val; }
                                else if (key == "delayFB")   { st.delayFB = val; ss.delayFB = val; }
                                else if (key == "delayBeats") { st.delayBeats = val; ss.delayBeats = val; }
                                else if (key == "delayTime") { st.delayTime = val; ss.delayTime = val; }
                                else if (key == "delayDamp") { st.delayDamp = val; ss.delayDamp = val; }
                                else if (key == "reverbMix") { st.reverbMix = val; ss.reverbMix = val; }
                                else if (key == "reverbSize") { st.reverbSize = val; ss.reverbSize = val; }
                                else if (key == "reverbDamp") { st.reverbDamp = val; ss.reverbDamp = val; }
                                else if (key == "chorusMix") { st.chorusMix = val; ss.chorusMix = val; }
                                else if (key == "chorusRate") { st.chorusRate = val; ss.chorusRate = val; }
                                else if (key == "chorusDepth") { st.chorusDepth = val; ss.chorusDepth = val; }
                                else if (key == "reduxBits") { st.reduxBits = val; ss.reduxBits = val; }
                                else if (key == "reduxRate") { st.reduxRate = val; ss.reduxRate = val; }
                                else if (key == "fxLP")      { st.fxLP = val; ss.fxLP = val; }
                                else if (key == "fxHP")      { st.fxHP = val; ss.fxHP = val; }
                                else if (key == "eqLow")     { st.eqLow = val; ss.eqLow = val; }
                                else if (key == "eqMid")     { st.eqMid = val; ss.eqMid = val; }
                                else if (key == "eqHigh")    { st.eqHigh = val; ss.eqHigh = val; }
                                // Int/bool fields in SynthLFOSave
                                else if (key == "delaySync")  { st.delaySync = static_cast<int>(val); ss.delaySync = (val > 0.5f); }
                                else if (key == "delayPP")    { st.delayPP = static_cast<int>(val); ss.delayPP = static_cast<int>(val); }
                                else if (key == "delayAlgo")  { st.delayAlgo = static_cast<int>(val); ss.delayAlgo = static_cast<int>(val); }
                                else if (key == "reverbAlgo") { st.reverbAlgo = static_cast<int>(val); ss.reverbAlgo = static_cast<int>(val); }
                            }
                            synthHadPlock[static_cast<size_t>(ev.trackIndex)] = true;
                            // Update synthClean so trigless persists across blocks
                            synthClean[static_cast<size_t>(ev.trackIndex)] = synthSave[static_cast<size_t>(ev.trackIndex)];
                        }
                    }
                    else
                    {
                    // Restore to global base before triggering (prevents sticky trigless)
                    if (synthHadPlock[static_cast<size_t>(ev.trackIndex)])
                    {
                        auto& gl = synthGlobal[static_cast<size_t>(ev.trackIndex)];
                        st.tune = gl.tune; st.cut = gl.cut; st.res = gl.res; st.volume = gl.volume; st.pan = gl.pan;
                        st.delayMix = gl.delayMix; st.distAmt = gl.distAmt; st.chorusMix = gl.chorusMix; st.reverbMix = gl.reverbMix;
                        st.pwm = gl.pwm; st.mix2 = gl.mix2; st.detune = gl.detune; st.subLevel = gl.subLevel;
                        st.uniSpread = gl.uniSpread; st.uniStereo = gl.uniStereo; st.fenv = gl.fenv;
                        st.aA = gl.aA; st.aD = gl.aD; st.aS = gl.aS; st.aR = gl.aR;
                        st.charAmt = gl.charAmt; st.fmLinAmt = gl.fmLinAmt;
                        st.fmLinRatio = gl.fmLinRatio; st.fmLinDecay = gl.fmLinDecay; st.fmLinSustain = gl.fmLinSustain;
                        st.cRatio = gl.cRatio; st.r2 = gl.r2; st.l2 = gl.l2; st.r3 = gl.r3; st.l3 = gl.l3; st.r4 = gl.r4; st.l4 = gl.l4;
                        st.fmFeedback = gl.fmFeedback; st.syncRatio = gl.syncRatio;
                        st.fA = gl.fA; st.fD = gl.fD; st.fS = gl.fS; st.fR = gl.fR;
                        st.elemBow = gl.elemBow; st.elemBlow = gl.elemBlow; st.elemStrike = gl.elemStrike;
                        st.elemGeometry = gl.elemGeometry; st.elemBright = gl.elemBright; st.elemSpace = gl.elemSpace;
                        st.plaitsHarmonics = gl.plaitsHarmonics; st.plaitsTimbre = gl.plaitsTimbre; st.plaitsMorph = gl.plaitsMorph;
                        st.plaitsDecay = gl.plaitsDecay; st.plaitsLpgColor = gl.plaitsLpgColor;
                        st.chorusRate = gl.chorusRate; st.chorusDepth = gl.chorusDepth;
                        st.delayTime = gl.delayTime; st.delayFB = gl.delayFB; st.delayBeats = gl.delayBeats;
                        st.delayDamp = gl.delayDamp; st.delaySync = gl.delaySync; st.delayPP = gl.delayPP; st.delayAlgo = gl.delayAlgo;
                        st.reverbSize = gl.reverbSize; st.reverbDamp = gl.reverbDamp; st.reverbAlgo = gl.reverbAlgo;
                        st.reduxBits = gl.reduxBits; st.reduxRate = gl.reduxRate;
                        st.smpCut = gl.smpCut; st.smpRes = gl.smpRes;
                        st.smpFType = gl.smpFType; st.smpFModel = gl.smpFModel; st.smpFPoles = gl.smpFPoles;
                        st.smpFiltEnv = gl.smpFiltEnv; st.smpFiltA = gl.smpFiltA; st.smpFiltD = gl.smpFiltD; st.smpFiltS = gl.smpFiltS; st.smpFiltR = gl.smpFiltR;
                        st.smpGain = gl.smpGain;
                        st.smpTune = gl.smpTune; st.smpFine = gl.smpFine;
                        st.smpStart = gl.smpStart; st.smpEnd = gl.smpEnd;
                        st.smpFmAmt = gl.smpFmAmt; st.smpFmRatio = gl.smpFmRatio;
                        st.smpFmEnvA = gl.smpFmEnvA; st.smpFmEnvD = gl.smpFmEnvD; st.smpFmEnvS = gl.smpFmEnvS;
                        st.fxLP = gl.fxLP; st.fxHP = gl.fxHP; st.eqLow = gl.eqLow; st.eqMid = gl.eqMid; st.eqHigh = gl.eqHigh;
                        st.wtPos1 = gl.wtPos1; st.wtPos2 = gl.wtPos2; st.wtMix = gl.wtMix;
                        st.wtWarpAmt1 = gl.wtWarpAmt1; st.wtWarpAmt2 = gl.wtWarpAmt2; st.wtSubLevel = gl.wtSubLevel;
                        st.grainPos = gl.grainPos; st.grainSize = gl.grainSize; st.grainDensity = gl.grainDensity;
                        st.grainSpray = gl.grainSpray; st.grainPitch = gl.grainPitch; st.grainPan = gl.grainPan;
                        st.grainTexture = gl.grainTexture; st.grainScan = gl.grainScan;
                        st.grainFmAmt = gl.grainFmAmt; st.grainFmRatio = gl.grainFmRatio;
                        st.grainFmDecay = gl.grainFmDecay; st.grainFmSus = gl.grainFmSus;
                        st.grainFmSpread = gl.grainFmSpread;
                        st.grainShape = gl.grainShape; st.grainDir = gl.grainDir;
                        st.grainFmSnap = gl.grainFmSnap; st.grainFreeze = gl.grainFreeze;
                        st.grainMode = gl.grainMode; st.grainTilt = gl.grainTilt;
                        st.grainUniVoices = gl.grainUniVoices; st.grainUniDetune = gl.grainUniDetune;
                        st.grainQuantize = gl.grainQuantize; st.grainFeedback = gl.grainFeedback;
                        synthHadPlock[static_cast<size_t>(ev.trackIndex)] = false;
                        synthClean[static_cast<size_t>(ev.trackIndex)] = gl;
                        synthSave[static_cast<size_t>(ev.trackIndex)] = gl;
                    }
                    // Play chord or single note (mono = root only)
                    // ── Cross-type choke group: kill drum+synth tracks with same group ──
                    {
                        int grp = st.chokeGroup;
                        if (grp > 0)
                        {
                            for (int i = 0; i < 10; ++i)
                                if (state.drumTracks[static_cast<size_t>(i)].chokeGroup == grp)
                                    drumSynth.killTrack (i);
                            for (int i = 0; i < 5; ++i)
                                if (i != ev.trackIndex && state.synthTracks[static_cast<size_t>(i)].chokeGroup == grp)
                                    synthEngine.killPart (i);
                        }
                    }
                    pushSynthWarpData (synthEngine, ev.trackIndex, st);
                    // Kill previous voice before new sequencer trigger
                    // Only preserve old voice for mono+slide (303 legato)
                    if (ev.gateScale < 1.0f || !st.mono)
                        synthEngine.killPart (ev.trackIndex);
                    if (!ev.chordNotes.empty() && !st.mono)
                    {
                        // Play root note first
                        synthEngine.noteOn (ev.trackIndex, ev.note.noteIndex, ev.note.octave,
                                            ev.velocity, svp, st.model, gateDur, smpBuf);
                        // Then chord notes
                        for (const auto& cn : ev.chordNotes)
                            synthEngine.noteOn (ev.trackIndex, cn.noteIndex, cn.octave,
                                                ev.velocity, svp, st.model, gateDur, smpBuf);
                    }
                    else
                    {
                        synthEngine.noteOn (ev.trackIndex, ev.note.noteIndex, ev.note.octave,
                                            ev.velocity, svp, st.model, gateDur, smpBuf);
                    }

                    // Mark voices as p-locked AFTER noteOn so they keep trigger-time params
                    if (ev.plocks != nullptr && !ev.plocks->empty())
                        synthEngine.markPlocked (ev.trackIndex);

                    st.lastVelocity = ev.velocity;
                    st.lastNote = static_cast<int>(ev.note.noteIndex) + static_cast<int>(ev.note.octave) * 12;

                    duckEngine.triggerTrack (10 + ev.trackIndex);
                    synthLFOs[static_cast<size_t>(ev.trackIndex)].retrigger (st.lfos);
                    // Retrigger all 3 MSEGs on note (resets phase to 0)
                    if (st.msegRetrig)
                    {
                        for (int mi = 0; mi < 3; ++mi)
                            synthMSEGs[static_cast<size_t>(ev.trackIndex)][mi].trigger();
                    }

                    // P-lock FX: copy ONLY FX params (not the entire state with seq/plocks!)
                    SynthTrackState synthFxSnap;
                    synthFxSnap.distAmt = st.distAmt;
                    synthFxSnap.reduxBits = st.reduxBits;
                    synthFxSnap.reduxRate = st.reduxRate;
                    synthFxSnap.chorusMix = st.chorusMix;
                    synthFxSnap.chorusRate = st.chorusRate;
                    synthFxSnap.chorusDepth = st.chorusDepth;
                    synthFxSnap.delayMix = st.delayMix;
                    synthFxSnap.delayTime = st.delayTime;
                    synthFxSnap.delayBeats = st.delayBeats;
                    synthFxSnap.delayFB = st.delayFB;
                    synthFxSnap.delaySync = st.delaySync;
                    synthFxSnap.delayPP = st.delayPP;
                    synthFxSnap.reverbMix = st.reverbMix;
                    synthFxSnap.reverbSize = st.reverbSize;
                    synthFxSnap.reverbDamp = st.reverbDamp;
                    synthFxSnap.reverbAlgo = st.reverbAlgo;
                    synthFxSnap.delayAlgo = st.delayAlgo;
                    synthFxSnap.delayDamp = st.delayDamp;
                    synthFxSnap.fxLP = st.fxLP;
                    synthFxSnap.fxHP = st.fxHP;
                    synthFxSnap.eqLow = st.eqLow;
                    synthFxSnap.eqMid = st.eqMid;
                    synthFxSnap.eqHigh = st.eqHigh;
                    synthFxSnap.pan = st.pan;
                    synthFxSnap.volume = st.volume;
                    if (ev.plocks != nullptr)
                    {
                        for (const auto& [key, val] : *ev.plocks)
                        {
                            if      (key == "distAmt")    synthFxSnap.distAmt = val;
                            else if (key == "reduxBits")  synthFxSnap.reduxBits = val;
                            else if (key == "reduxRate")  synthFxSnap.reduxRate = val;
                            else if (key == "chorusMix")  synthFxSnap.chorusMix = val;
                            else if (key == "chorusRate") synthFxSnap.chorusRate = val;
                            else if (key == "chorusDepth")synthFxSnap.chorusDepth = val;
                            else if (key == "delayMix")   synthFxSnap.delayMix = val;
                            else if (key == "delayBeats") synthFxSnap.delayBeats = val;
                            else if (key == "delayFB")    synthFxSnap.delayFB = val;
                            else if (key == "reverbMix")  synthFxSnap.reverbMix = val;
                            else if (key == "reverbSize") synthFxSnap.reverbSize = val;
                            else if (key == "fxLP")       synthFxSnap.fxLP = val;
                            else if (key == "fxHP")       synthFxSnap.fxHP = val;
                            else if (key == "eqLow")      synthFxSnap.eqLow = val;
                            else if (key == "eqMid")      synthFxSnap.eqMid = val;
                            else if (key == "eqHigh")     synthFxSnap.eqHigh = val;
                            else if (key == "pan")        synthFxSnap.pan = val;
                            else if (key == "delaySync")  synthFxSnap.delaySync = (val > 0.5f);
                            else if (key == "delayPP")    synthFxSnap.delayPP = static_cast<int>(val);
                            else if (key == "delayTime")  synthFxSnap.delayTime = val;
                            else if (key == "duckSrc")    synthFxSnap.duckSrc = static_cast<int>(val);
                            else if (key == "duckDepth")  synthFxSnap.duckDepth = val;
                            else if (key == "duckAtk")    synthFxSnap.duckAtk = val;
                            else if (key == "duckRel")    synthFxSnap.duckRel = val;
                        }
                    }
                    synthEngine.setPlockFX (ev.trackIndex, synthFxSnap, ev.plocks != nullptr && !ev.plocks->empty());
                    } // end else (non-arp path)
                }
            }
            eventIdx++;
        }

        currentSample = nextEventSample;
        if (currentSample < numSamples && samplesToRender == 0)
            currentSample++;
    }

    // ═══════════════════════════════════════
    // ARP ENGINE TICK — generates notes from held chords at arp clock rate
    // ═══════════════════════════════════════
    float arpBPM = state.bpm.load();
    for (int t = 0; t < 5; ++t)
    {
        auto& st = state.synthTracks[static_cast<size_t>(t)];
        if (!st.arp.enabled) continue;
        if (state.isSynthEffectivelyMuted (t)) continue;

        auto& arp = synthArps[static_cast<size_t>(t)];
        if (arp.tick (numSamples, arpBPM, st.arp))
        {
            auto arpEv = arp.getLastEvent();

            // ── Build FRESH svp from CURRENT track state (NOT cached!) ──
            // This is the root cause fix: cached svp was stale, knob changes had no effect
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
            svp.volume = st.volume;
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
            svp.smpStart = st.smpStart; svp.smpEnd = st.smpEnd;
            svp.smpGain = st.smpGain; svp.smpLoop = st.smpLoop;
            svp.smpReverse = st.smpReverse; svp.smpPlayMode = st.smpPlayMode;
            svp.smpTune = st.smpTune; svp.smpFine = st.smpFine;
            svp.smpA = st.smpA; svp.smpD = st.smpD; svp.smpS = st.smpS; svp.smpR = st.smpR;
            svp.smpCut = st.smpCut; svp.smpRes = st.smpRes;
            svp.smpFType = st.smpFType; svp.smpFModel = st.smpFModel; svp.smpFPoles = st.smpFPoles;
            svp.smpRootNote = st.smpRootNote;
            svp.smpStretch = st.smpStretch; svp.smpWarp = st.smpWarp;
            svp.smpBPM = st.smpBPM; svp.smpFileSR = st.smpFileSR; svp.smpBpmSync = st.smpBpmSync; svp.smpSyncMul = st.smpSyncMul; svp.smpBars = st.smpBars;
            svp.smpFmAmt = st.smpFmAmt; svp.smpFmRatio = st.smpFmRatio;
            svp.smpFmEnvA = st.smpFmEnvA; svp.smpFmEnvD = st.smpFmEnvD; svp.smpFmEnvS = st.smpFmEnvS;
            svp.wtPos1 = st.wtPos1; svp.wtPos2 = st.wtPos2; svp.wtMix = st.wtMix;
            svp.wtWarpAmt1 = st.wtWarpAmt1; svp.wtWarpAmt2 = st.wtWarpAmt2;
            svp.wtWarp1 = st.wtWarp1; svp.wtWarp2 = st.wtWarp2;
            svp.wtSubLevel = st.wtSubLevel;
            svp.grainPos = st.grainPos; svp.grainSize = st.grainSize;
            svp.grainDensity = st.grainDensity; svp.grainSpray = st.grainSpray;
            svp.grainPitch = st.grainPitch; svp.grainPan = st.grainPan;
            svp.grainShape = st.grainShape; svp.grainDir = st.grainDir;
            svp.grainTexture = st.grainTexture; svp.grainFreeze = st.grainFreeze;
            svp.grainScan = st.grainScan;
            svp.grainMode = st.grainMode; svp.grainTilt = st.grainTilt;
            svp.grainUniVoices = st.grainUniVoices; svp.grainUniDetune = st.grainUniDetune;
            svp.grainQuantize = st.grainQuantize; svp.grainFeedback = st.grainFeedback;
            svp.grainFmAmt = st.grainFmAmt; svp.grainFmRatio = st.grainFmRatio;
            svp.grainFmDecay = st.grainFmDecay; svp.grainFmSus = st.grainFmSus;
            svp.grainFmSnap = st.grainFmSnap;
            svp.grainFmSpread = st.grainFmSpread;

            auto smpBuf = arpCachedBuf[static_cast<size_t>(t)]; // sample buffer can stay cached

            // Gate = arp step duration * arp step gate %
            float arpStepSec = (60.0f / std::max (20.0f, arpBPM)) * st.arp.getDivisionBeats();

            // ── 303-style ARP: slide when gate >= 100% and mono+glide active ──
            bool arpSlide = (arpEv.gateScale >= 1.0f);
            // When slide: extend gate well past next step for legato overlap
            float gateDur = arpSlide ? (arpStepSec * 4.0f) : (arpStepSec * arpEv.gateScale);
            float effectiveGlide = 0.0f;
            if (st.mono && arpSlide && st.glide > 0.001f)
                effectiveGlide = std::max (0.04f, st.glide);
            synthEngine.setMonoGlide (t, st.mono, effectiveGlide);

            // Apply arp step assignable param as modulation DIRECTLY to svp fields
            // ── Param Lane 1 ──
            float modBase = (arpEv.paramVal - 0.5f) * 2.0f; // -1 to +1
            if (std::abs (st.arp.assignDepth) > 0.01f && st.arp.assignTarget >= 0)
            {
                float mv = modBase * st.arp.assignDepth;
                applyArpModToSvp (svp, st.arp.assignTarget, mv);
                applyArpModToTrack (st, st.arp.assignTarget, mv);
                applyArpModToSave (synthSave[static_cast<size_t>(t)], st.arp.assignTarget, mv);
            }
            for (int ri = 0; ri < 16; ++ri)
            {
                auto& route = st.arp.extraRoutes[static_cast<size_t>(ri)];
                if (route.target >= 0 && std::abs (route.depth) > 0.01f)
                {
                    float mv = modBase * route.depth;
                    applyArpModToSvp (svp, route.target, mv);
                    applyArpModToTrack (st, route.target, mv);
                    applyArpModToSave (synthSave[static_cast<size_t>(t)], route.target, mv);
                }
            }
            // ── Param Lane 2 (independent values, independent target) ──
            float modBase2 = (arpEv.param2Val - 0.5f) * 2.0f;
            if (std::abs (st.arp.assign2Depth) > 0.01f && st.arp.assign2Target >= 0)
            {
                float mv2 = modBase2 * st.arp.assign2Depth;
                applyArpModToSvp (svp, st.arp.assign2Target, mv2);
                applyArpModToTrack (st, st.arp.assign2Target, mv2);
                applyArpModToSave (synthSave[static_cast<size_t>(t)], st.arp.assign2Target, mv2);
            }

            pushSynthWarpData (synthEngine, t, st);
            synthEngine.noteOn (t, arpEv.noteIndex, arpEv.octave,
                                arpEv.velocity, svp, st.model, gateDur, smpBuf);
            // Mark plocked: arp per-step modulation (velocity, param) persists until next step.
            // This is safe because svp is built FRESH from current track state each tick,
            // so knob changes apply at the next arp step (Elektron-style behavior).
            synthEngine.markPlocked (t);
        }
    }

    if (currentSample < numSamples)
    {
        int remaining = numSamples - currentSample;

        // Process ducking for remaining samples
        std::array<int, 15> dSrc {};
        std::array<float, 15> dDepth {}, dAtk {}, dRel {};
        for (int t = 0; t < 10; ++t)
        {
            dSrc[t] = state.drumTracks[static_cast<size_t>(t)].duckSrc;
            dDepth[t] = state.drumTracks[static_cast<size_t>(t)].duckDepth;
            dAtk[t] = state.drumTracks[static_cast<size_t>(t)].duckAtk;
            dRel[t] = state.drumTracks[static_cast<size_t>(t)].duckRel;
        }
        for (int t = 0; t < 5; ++t)
        {
            dSrc[10 + t] = state.synthTracks[static_cast<size_t>(t)].duckSrc;
            dDepth[10 + t] = state.synthTracks[static_cast<size_t>(t)].duckDepth;
            dAtk[10 + t] = state.synthTracks[static_cast<size_t>(t)].duckAtk;
            dRel[10 + t] = state.synthTracks[static_cast<size_t>(t)].duckRel;
        }
        for (int s = 0; s < remaining; ++s)
            duckEngine.processSample (dSrc.data(), dDepth.data(), dAtk.data(), dRel.data(), 15);

        std::array<float, 10> drumDuckGains {};
        std::array<float, 5> synthDuckGains {};
        for (int t = 0; t < 10; ++t) drumDuckGains[t] = duckEngine.getGain (t);
        for (int t = 0; t < 5; ++t) synthDuckGains[t] = duckEngine.getGain (10 + t);

        float* renderL = stereoBuffer.getWritePointer (0) + currentSample;
        float* renderR = stereoBuffer.getWritePointer (1) + currentSample;

        // ── Wire resample taps before render ──
        {
            int src = state.resampleSrc.load();
            if (src >= 0 && src <= 9)
            {
                drumSynth.setResampleTap (src, drumTapBuf.data() + currentSample, remaining);
                synthEngine.clearResampleTap();
            }
            else if (src >= 10 && src <= 14)
            {
                synthEngine.setResampleTap (src - 10, synthTapBuf.data() + currentSample, remaining);
                drumSynth.clearResampleTap();
            }
            else
            {
                drumSynth.clearResampleTap();
                synthEngine.clearResampleTap();
            }
        }

        drumSynth.renderBlock (renderL, renderR, remaining, drumDuckGains.data());
        synthEngine.renderBlock (renderL, renderR, remaining, synthDuckGains.data());

        // ── Console 8 decode (remaining samples) ──
        for (int i = 0; i < remaining; ++i)
        {
            renderL[i] = consoleDecode (renderL[i]);
            renderR[i] = consoleDecode (renderR[i]);
        }
    }

    // ═══════════════════════════════════════
    // RESTORE LFO-modulated values
    // ═══════════════════════════════════════
    for (int t = 0; t < 10; ++t)
    {
        auto& dt = state.drumTracks[static_cast<size_t>(t)];
        dt.pitch = drumSave[t].pitch; dt.decay = drumSave[t].decay;
        dt.tone = drumSave[t].tone; dt.volume = drumSave[t].volume;
        dt.pan = drumSave[t].pan; dt.delayMix = drumSave[t].delayMix;
        dt.distAmt = drumSave[t].distAmt; dt.click = drumSave[t].click;
        dt.drumCut = drumSave[t].drumCut; dt.drumRes = drumSave[t].drumRes;
        dt.drumFiltEnv = drumSave[t].drumFiltEnv; dt.drumFiltA = drumSave[t].drumFiltA; dt.drumFiltD = drumSave[t].drumFiltD;
        dt.pitchDec = drumSave[t].pitchDec;
        dt.fmMix = drumSave[t].fmMix; dt.fmRatio = drumSave[t].fmRatio;
        dt.fmDepth = drumSave[t].fmDepth; dt.fmDecay = drumSave[t].fmDecay;
        dt.fmNoise = drumSave[t].fmNoise;
        dt.chorusRate = drumSave[t].chorusRate; dt.chorusDepth = drumSave[t].chorusDepth;
        dt.chorusMix = drumSave[t].chorusMix;
        dt.delayTime = drumSave[t].delayTime; dt.delayFB = drumSave[t].delayFB;
        dt.delayBeats = drumSave[t].delayBeats;
        dt.delaySync = drumSave[t].delaySync != 0; dt.delayPP = drumSave[t].delayPP;
        dt.delayDamp = drumSave[t].delayDamp;
        dt.reverbDamp = drumSave[t].reverbDamp;
        dt.reverbSize = drumSave[t].reverbSize; dt.reverbMix = drumSave[t].reverbMix;
        dt.reduxBits = drumSave[t].reduxBits; dt.reduxRate = drumSave[t].reduxRate;
        dt.smpCut = drumSave[t].smpCut; dt.smpRes = drumSave[t].smpRes;
        dt.smpFType = drumSave[t].smpFType; dt.smpFModel = drumSave[t].smpFModel; dt.smpFPoles = drumSave[t].smpFPoles;
        dt.smpTune = drumSave[t].smpTune; dt.smpFine = drumSave[t].smpFine;
        dt.smpGain = drumSave[t].smpGain; dt.smpStart = drumSave[t].smpStart; dt.smpEnd = drumSave[t].smpEnd;
        dt.smpFmAmt = drumSave[t].smpFmAmt; dt.smpFmRatio = drumSave[t].smpFmRatio;
        dt.smpFmEnvA = drumSave[t].smpFmEnvA; dt.smpFmEnvD = drumSave[t].smpFmEnvD; dt.smpFmEnvS = drumSave[t].smpFmEnvS;
        dt.fxLP = drumSave[t].fxLP; dt.fxHP = drumSave[t].fxHP;
        dt.eqLow = drumSave[t].eqLow; dt.eqMid = drumSave[t].eqMid; dt.eqHigh = drumSave[t].eqHigh;
        dt.delayAlgo = drumSave[t].delayAlgo; dt.reverbAlgo = drumSave[t].reverbAlgo;
        dt.fmNoiseType = drumSave[t].fmNoiseType; dt.duckSrc = drumSave[t].duckSrc;
        dt.duckDepth = drumSave[t].duckDepth; dt.duckAtk = drumSave[t].duckAtk; dt.duckRel = drumSave[t].duckRel;
        dt.smpA = drumSave[t].smpA; dt.smpD = drumSave[t].smpD; dt.smpS = drumSave[t].smpS; dt.smpR = drumSave[t].smpR;
        dt.er1Pitch1 = drumSave[t].er1Pitch1; dt.er1Pitch2 = drumSave[t].er1Pitch2;
        dt.er1PDec1 = drumSave[t].er1PDec1; dt.er1PDec2 = drumSave[t].er1PDec2;
        dt.er1Ring = drumSave[t].er1Ring; dt.er1XMod = drumSave[t].er1XMod;
        dt.er1Noise = drumSave[t].er1Noise; dt.er1NDec = drumSave[t].er1NDec;
        dt.er1Cut = drumSave[t].er1Cut; dt.er1Res = drumSave[t].er1Res;
        dt.er1Decay = drumSave[t].er1Decay; dt.er1Drive = drumSave[t].er1Drive;
        dt.snap = drumSave[t].snap; dt.subModel = drumSave[t].subModel;
    }
    for (int t = 0; t < 5; ++t)
    {
        auto& st = state.synthTracks[static_cast<size_t>(t)];
        st.tune = synthSave[t].tune; st.cut = synthSave[t].cut;
        st.res = synthSave[t].res; st.volume = synthSave[t].volume;
        st.pan = synthSave[t].pan; st.delayMix = synthSave[t].delayMix;
        st.distAmt = synthSave[t].distAmt; st.chorusMix = synthSave[t].chorusMix;
        st.reverbMix = synthSave[t].reverbMix; st.pwm = synthSave[t].pwm;
        st.mix2 = synthSave[t].mix2; st.detune = synthSave[t].detune;
        st.subLevel = synthSave[t].subLevel; st.uniSpread = synthSave[t].uniSpread;
        st.fenv = synthSave[t].fenv;
        st.aA = synthSave[t].aA; st.aD = synthSave[t].aD;
        st.aS = synthSave[t].aS; st.aR = synthSave[t].aR;
        st.charAmt = synthSave[t].charAmt; st.fmLinAmt = synthSave[t].fmLinAmt;
        st.cRatio = synthSave[t].cRatio;
        st.r2 = synthSave[t].r2; st.l2 = synthSave[t].l2;
        st.r3 = synthSave[t].r3; st.l3 = synthSave[t].l3;
        st.r4 = synthSave[t].r4; st.l4 = synthSave[t].l4;
        st.fmFeedback = synthSave[t].fmFeedback;
        st.elemBow = synthSave[t].elemBow; st.elemBlow = synthSave[t].elemBlow;
        st.elemStrike = synthSave[t].elemStrike; st.elemGeometry = synthSave[t].elemGeometry;
        st.elemBright = synthSave[t].elemBright; st.elemSpace = synthSave[t].elemSpace;
        st.plaitsHarmonics = synthSave[t].plaitsHarmonics; st.plaitsTimbre = synthSave[t].plaitsTimbre;
        st.plaitsMorph = synthSave[t].plaitsMorph; st.plaitsDecay = synthSave[t].plaitsDecay;
        st.plaitsLpgColor = synthSave[t].plaitsLpgColor;
        st.chorusRate = synthSave[t].chorusRate; st.chorusDepth = synthSave[t].chorusDepth;
        st.delayTime = synthSave[t].delayTime; st.delayFB = synthSave[t].delayFB;
        st.delayBeats = synthSave[t].delayBeats;
        st.delaySync = synthSave[t].delaySync; st.delayPP = synthSave[t].delayPP;
        st.delayDamp = synthSave[t].delayDamp;
        st.reverbSize = synthSave[t].reverbSize;
        st.reduxBits = synthSave[t].reduxBits; st.reduxRate = synthSave[t].reduxRate;
        st.smpCut = synthSave[t].smpCut; st.smpRes = synthSave[t].smpRes;
        st.smpFType = synthSave[t].smpFType; st.smpFModel = synthSave[t].smpFModel; st.smpFPoles = synthSave[t].smpFPoles;
        st.smpGain = synthSave[t].smpGain; st.smpStart = synthSave[t].smpStart;
        st.smpEnd = synthSave[t].smpEnd; st.smpTune = synthSave[t].smpTune;
        st.smpFine = synthSave[t].smpFine;
        st.smpFmAmt = synthSave[t].smpFmAmt; st.smpFmRatio = synthSave[t].smpFmRatio;
        st.smpFmEnvA = synthSave[t].smpFmEnvA; st.smpFmEnvD = synthSave[t].smpFmEnvD; st.smpFmEnvS = synthSave[t].smpFmEnvS;
        st.syncRatio = synthSave[t].syncRatio; st.uniStereo = synthSave[t].uniStereo;
        st.fA = synthSave[t].fA; st.fD = synthSave[t].fD;
        st.fS = synthSave[t].fS; st.fR = synthSave[t].fR;
        st.fmLinRatio = synthSave[t].fmLinRatio; st.fmLinDecay = synthSave[t].fmLinDecay; st.fmLinSustain = synthSave[t].fmLinSustain;
        st.fxLP = synthSave[t].fxLP; st.fxHP = synthSave[t].fxHP;
        st.eqLow = synthSave[t].eqLow; st.eqMid = synthSave[t].eqMid; st.eqHigh = synthSave[t].eqHigh;
        st.wtPos1 = synthSave[t].wtPos1; st.wtPos2 = synthSave[t].wtPos2;
        st.wtMix = synthSave[t].wtMix; st.wtWarpAmt1 = synthSave[t].wtWarpAmt1;
        st.wtWarpAmt2 = synthSave[t].wtWarpAmt2; st.wtSubLevel = synthSave[t].wtSubLevel;
        st.grainPos = synthSave[t].grainPos; st.grainSize = synthSave[t].grainSize;
        st.grainDensity = synthSave[t].grainDensity; st.grainSpray = synthSave[t].grainSpray;
        st.grainPitch = synthSave[t].grainPitch; st.grainPan = synthSave[t].grainPan;
        st.grainTexture = synthSave[t].grainTexture; st.grainScan = synthSave[t].grainScan;
        st.grainShape = synthSave[t].grainShape; st.grainDir = synthSave[t].grainDir;
        st.grainFreeze = synthSave[t].grainFreeze;
        st.grainMode = synthSave[t].grainMode; st.grainTilt = synthSave[t].grainTilt;
        st.grainUniVoices = synthSave[t].grainUniVoices; st.grainUniDetune = synthSave[t].grainUniDetune;
        st.grainQuantize = synthSave[t].grainQuantize; st.grainFeedback = synthSave[t].grainFeedback;
        st.grainFmAmt = synthSave[t].grainFmAmt; st.grainFmRatio = synthSave[t].grainFmRatio;
        st.grainFmDecay = synthSave[t].grainFmDecay; st.grainFmSus = synthSave[t].grainFmSus;
        st.grainFmSnap = synthSave[t].grainFmSnap;
        st.grainFmSpread = synthSave[t].grainFmSpread;
        st.grainUniStereo = synthSave[t].grainUniStereo;
        st.elemContour = synthSave[t].elemContour; st.elemDamping = synthSave[t].elemDamping;
        st.elemFlow = synthSave[t].elemFlow; st.elemMallet = synthSave[t].elemMallet;
        st.elemPitch = synthSave[t].elemPitch; st.elemPosition = synthSave[t].elemPosition;
        st.glide = synthSave[t].glide;
        st.smpA = synthSave[t].smpA; st.smpD = synthSave[t].smpD;
        st.smpS = synthSave[t].smpS; st.smpR = synthSave[t].smpR;
        st.duckDepth = synthSave[t].duckDepth; st.duckAtk = synthSave[t].duckAtk; st.duckRel = synthSave[t].duckRel;
        st.dc2 = synthSave[t].dc2; st.dc3 = synthSave[t].dc3; st.dc4 = synthSave[t].dc4;
        st.cA = synthSave[t].cA; st.cD = synthSave[t].cD; st.cS = synthSave[t].cS; st.cR = synthSave[t].cR;
        st.cLevel = synthSave[t].cLevel;
        st.delayDamp = synthSave[t].delayDamp;
        st.reverbDamp = synthSave[t].reverbDamp;
        st.delayAlgo = synthSave[t].delayAlgo;
        st.reverbAlgo = synthSave[t].reverbAlgo;
    }

    // ═══════════════════════════════════════
    // MASTER OUTPUT (stereo) — with per-sample smoothing to prevent clicks
    // ═══════════════════════════════════════
    {
        float targetVol = state.masterVolume.load();
        auto* bufL = stereoBuffer.getWritePointer (0);
        auto* bufR = stereoBuffer.getWritePointer (1);
        int ns = stereoBuffer.getNumSamples();
        // ~5ms smoothing at 44.1kHz
        float smoothCoeff = 1.0f - std::exp (-1.0f / (static_cast<float>(getSampleRate()) * 0.005f));
        for (int i = 0; i < ns; ++i)
        {
            smoothMasterVol += (targetVol - smoothMasterVol) * smoothCoeff;
            bufL[i] *= smoothMasterVol;
            bufR[i] *= smoothMasterVol;
        }
    }

    // ══════════════════════════════════════════
    // RESAMPLE RECORDING — capture audio to buffer
    // ══════════════════════════════════════════
    {
        // ── Transport sync: auto-start/stop recording with transport ──
        if (state.resampleTransportSync.load() && state.resampleArmed.load())
        {
            bool transportNow = sequencer.isPlaying();
            // Transport just started → begin recording
            if (transportNow && !wasResamplePlaying)
            {
                state.resampleActive.store (true);
            }
            // Transport just stopped → stop recording
            if (!transportNow && wasResamplePlaying && state.resampleActive.load())
            {
                state.resampleActive.store (false);
                state.resampleArmed.store (false);  // disarm after capture
            }
            wasResamplePlaying = transportNow;
        }
        else
        {
            wasResamplePlaying = sequencer.isPlaying();
        }

        bool active = state.resampleActive.load();
        int src = state.resampleSrc.load();

        // Detect start: was inactive, now active → reset write position
        if (active && !wasResampleActive)
        {
            resampleWritePos = 0;
            state.resampleLength.store (0);
        }

        if (active && src >= 0)
        {
            int maxLen = static_cast<int>(resampleBuf.size());
            int toWrite = std::min (numSamples, maxLen - resampleWritePos);

            if (toWrite > 0)
            {
                if (src >= 0 && src <= 9)
                {
                    // Drum track — data already in drumTapBuf
                    for (int i = 0; i < toWrite; ++i)
                        resampleBuf[static_cast<size_t>(resampleWritePos + i)] = drumTapBuf[static_cast<size_t>(i)];
                }
                else if (src >= 10 && src <= 14)
                {
                    // Synth track — data in synthTapBuf
                    for (int i = 0; i < toWrite; ++i)
                        resampleBuf[static_cast<size_t>(resampleWritePos + i)] = synthTapBuf[static_cast<size_t>(i)];
                }
                else if (src == 15)
                {
                    // Master bus — stereo→mono from stereoBuffer
                    const float* mstL = stereoBuffer.getReadPointer (0);
                    const float* mstR = stereoBuffer.getReadPointer (1);
                    for (int i = 0; i < toWrite; ++i)
                        resampleBuf[static_cast<size_t>(resampleWritePos + i)] = (mstL[i] + mstR[i]) * 0.5f;
                }

                resampleWritePos += toWrite;
                state.resampleLength.store (resampleWritePos);
            }

            // Auto-stop when buffer is full
            if (resampleWritePos >= maxLen)
            {
                state.resampleActive.store (false);
                state.resampleArmed.store (false);
                state.resampleReady.store (true);
            }
        }

        // Detect manual stop: was active, now inactive → trigger finalize
        if (wasResampleActive && !active && !state.resampleReady.load())
        {
            if (resampleWritePos > 64)
                state.resampleReady.store (true);
        }

        wasResampleActive = active;
    }

    // ══════════════════════════════════════════
    // MASTER FX SEQUENCER — apply p-locks from current step
    // ══════════════════════════════════════════
    sequencer.setMasterFXLength (state.masterFXLength.load());
    int mfxStep = sequencer.getMasterFXPlayingStep();
    state.masterFXStep.store (mfxStep);

    // Save default master FX values BEFORE p-lock override
    struct MasterFXSave {
        float gaterMix, gaterRate, gaterDepth, gaterShape, gaterSmooth;
        float mDelayMix, mDelaySync, mDelayBeats, mDelayTime, mDelayFB, mDelayPP, mDelayHP, mDelayLP, mDelayAlgo, mDelayDamp;
        float djFilterFreq, djFilterRes;
    } mfxSave;
    mfxSave.gaterMix = state.gaterMix.load();
    mfxSave.gaterRate = state.gaterRate.load();
    mfxSave.gaterDepth = state.gaterDepth.load();
    mfxSave.gaterShape = state.gaterShape.load();
    mfxSave.gaterSmooth = state.gaterSmooth.load();
    mfxSave.mDelayMix = state.mDelayMix.load();
    mfxSave.mDelaySync = state.mDelaySync.load();
    mfxSave.mDelayBeats = state.mDelayBeats.load();
    mfxSave.mDelayTime = state.mDelayTime.load();
    mfxSave.mDelayFB = state.mDelayFB.load();
    mfxSave.mDelayPP = state.mDelayPP.load();
    mfxSave.mDelayHP = state.mDelayHP.load();
    mfxSave.mDelayLP = state.mDelayLP.load();
    mfxSave.mDelayAlgo = state.mDelayAlgo.load();
    mfxSave.mDelayDamp = state.mDelayDamp.load();
    mfxSave.djFilterFreq = state.djFilterFreq.load();
    mfxSave.djFilterRes = state.djFilterRes.load();

    bool mfxActive = false;
    if (mfxStep >= 0 && mfxStep < kMaxSteps)
    {
        const auto& step = state.masterFXSeq.steps[static_cast<size_t>(mfxStep)];
        mfxActive = step.active;
        if (mfxActive)
        {
            // Apply p-locks: override state atomics with per-step values
            for (const auto& [key, val] : step.plocks)
            {
                if (key == "gaterMix")    state.gaterMix.store (val);
                else if (key == "gaterRate")   state.gaterRate.store (val);
                else if (key == "gaterDepth")  state.gaterDepth.store (val);
                else if (key == "gaterShape")  state.gaterShape.store (val);
                else if (key == "gaterSmooth") state.gaterSmooth.store (val);
                else if (key == "mDelayMix")   state.mDelayMix.store (val);
                else if (key == "mDelaySync")  state.mDelaySync.store (val);
                else if (key == "mDelayBeats") state.mDelayBeats.store (val);
                else if (key == "mDelayTime")  state.mDelayTime.store (val);
                else if (key == "mDelayFB")    state.mDelayFB.store (val);
                else if (key == "mDelayPP")    state.mDelayPP.store (val);
                else if (key == "mDelayHP")    state.mDelayHP.store (val);
                else if (key == "mDelayLP")    state.mDelayLP.store (val);
                else if (key == "mDelayAlgo")  state.mDelayAlgo.store (val);
                else if (key == "mDelayDamp")  state.mDelayDamp.store (val);
                else if (key == "djFilterFreq") state.djFilterFreq.store (val);
                else if (key == "djFilterRes")  state.djFilterRes.store (val);
            }
        }
    }

    // ══════════════════════════════════════════
    // CREATIVE FX (before mastering chain)
    // ══════════════════════════════════════════

    // ── Gater/Slicer with envelope-following filter ──
    {
        float gMix = state.gaterMix.load();
        if (gMix > 0.01f)
        {
            float depth = std::clamp (state.gaterDepth.load(), 0.0f, 1.0f);
            float smooth = std::clamp (state.gaterSmooth.load(), 0.005f, 0.5f); // min 5ms to prevent distortion
            int shape = static_cast<int>(state.gaterShape.load());
            static const float divBeats[] = {0.0625f, 0.125f, 0.1875f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f};
            int divIdx = std::clamp (static_cast<int>(state.gaterRate.load()), 0, 7);
            float hz = (currentBPM > 20.0f) ? (currentBPM / 60.0f) / divBeats[divIdx] : 2.0f;
            float advance = hz / static_cast<float>(currentSampleRate);
            float aCoeff = std::exp (-1.0f / (static_cast<float>(currentSampleRate) * smooth));

            float* gL = stereoBuffer.getWritePointer (0);
            float* gR = stereoBuffer.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                gaterPhase += advance;
                if (gaterPhase >= 1.0f) gaterPhase -= 1.0f;
                float p = gaterPhase;
                float gateVal = 1.0f;
                switch (shape) {
                    case 0: gateVal = (p < 0.5f) ? 1.0f : 1.0f - depth; break;
                    case 1: gateVal = 1.0f - depth * p; break;
                    case 2: gateVal = 1.0f - depth * (1.0f - p); break;
                    case 3: gateVal = 1.0f - depth * (2.0f * std::abs(p - 0.5f)); break;
                    case 4: // filter sweep — LP tracks gate envelope
                    {
                        gateVal = 1.0f; // no volume gating
                        float envFollow = (p < 0.5f) ? 1.0f : 1.0f - (p - 0.5f) * 2.0f * depth;
                        float cutHz = 200.0f + envFollow * 18000.0f;
                        float fc = std::clamp (cutHz / static_cast<float>(currentSampleRate), 0.001f, 0.49f);
                        float g = std::tan (3.14159f * fc);
                        float k = 2.0f; // moderate resonance
                        float a1 = 1.0f / (1.0f + g * (g + k));
                        float a2 = g * a1;
                        float a3 = g * a2;
                        // L
                        float v3L = gL[i] - gaterFiltL2;
                        float v1L = a1 * gaterFiltL1 + a2 * v3L;
                        float v2L = gaterFiltL2 + a2 * gaterFiltL1 + a3 * v3L;
                        gaterFiltL1 = 2.0f * v1L - gaterFiltL1;
                        gaterFiltL2 = 2.0f * v2L - gaterFiltL2;
                        if (std::abs (gaterFiltL1) < 1e-15f) gaterFiltL1 = 0.0f;
                        if (std::abs (gaterFiltL2) < 1e-15f) gaterFiltL2 = 0.0f;
                        gL[i] = gL[i] * (1.0f - gMix) + v2L * gMix;
                        // R
                        float v3R = gR[i] - gaterFiltR2;
                        float v1R = a1 * gaterFiltR1 + a2 * v3R;
                        float v2R = gaterFiltR2 + a2 * gaterFiltR1 + a3 * v3R;
                        gaterFiltR1 = 2.0f * v1R - gaterFiltR1;
                        gaterFiltR2 = 2.0f * v2R - gaterFiltR2;
                        if (std::abs (gaterFiltR1) < 1e-15f) gaterFiltR1 = 0.0f;
                        if (std::abs (gaterFiltR2) < 1e-15f) gaterFiltR2 = 0.0f;
                        gR[i] = gR[i] * (1.0f - gMix) + v2R * gMix;
                        continue; // skip normal gating below
                    }
                }
                gaterEnv = gateVal + (gaterEnv - gateVal) * aCoeff;
                float wet = gaterEnv;
                float g = 1.0f * (1.0f - gMix) + wet * gMix;
                gL[i] *= g;
                gR[i] *= g;
                // Soft clip to prevent distortion on hot master bus
                if (std::abs (gL[i]) > 1.0f) gL[i] = std::tanh (gL[i]);
                if (std::abs (gR[i]) > 1.0f) gR[i] = std::tanh (gR[i]);
            }
        }
    }

    // ── DJ Filter (HP/LP morph) — click-free with separate LP+HP states ──
    {
        float freq = state.djFilterFreq.load();
        float dist = std::abs (freq - 0.5f); // 0 at center, 0.5 at extremes

        // Target wet: 0 at center, ramps to 1.0 over 0.05-0.15 range
        float targetWet = std::clamp ((dist - 0.03f) / 0.10f, 0.0f, 1.0f);

        // Smooth wet transition (10ms — prevents click on return to center)
        float wetCoeff = std::exp (-1.0f / (static_cast<float>(currentSampleRate) * 0.010f));
        
        bool isHP = freq > 0.5f;

        if (targetWet > 0.001f || djFilterWet > 0.001f)
        {
            float res = state.djFilterRes.load();
            float cutHz;
            if (!isHP) {
                // LP mode: freq 0→0.5 maps to 200Hz→20kHz
                float norm = std::clamp (freq * 2.0f, 0.0f, 1.0f);
                cutHz = 200.0f * std::pow (100.0f, norm);
            } else {
                // HP mode: freq 0.5→1 maps to 20Hz→5kHz
                float norm = std::clamp ((freq - 0.5f) * 2.0f, 0.0f, 1.0f);
                cutHz = 20.0f * std::pow (250.0f, norm);
            }
            float w0 = 2.0f * 3.14159265f * std::clamp (cutHz, 20.0f, static_cast<float>(currentSampleRate) * 0.45f) / static_cast<float>(currentSampleRate);
            float Q = 0.5f + res * 8.0f;
            float alpha = std::sin (w0) / (2.0f * Q);
            float cs = std::cos (w0);

            float b0, b1, b2, a1f, a2f;
            if (isHP) {
                float a0 = 1.0f + alpha; b0 = (1.0f + cs) * 0.5f / a0; b1 = -(1.0f + cs) / a0;
                b2 = b0; a1f = -2.0f * cs / a0; a2f = (1.0f - alpha) / a0;
            } else {
                float a0 = 1.0f + alpha; b0 = (1.0f - cs) * 0.5f / a0; b1 = (1.0f - cs) / a0;
                b2 = b0; a1f = -2.0f * cs / a0; a2f = (1.0f - alpha) / a0;
            }

            // Smooth mode transition (LP↔HP) — crossfade instead of state reset
            float targetModeBlend = isHP ? 1.0f : 0.0f;
            float modeCoeff = std::exp (-1.0f / (static_cast<float>(currentSampleRate) * 0.008f)); // 8ms

            float* fL = stereoBuffer.getWritePointer (0);
            float* fR = stereoBuffer.getWritePointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                djFilterWet = targetWet + (djFilterWet - targetWet) * wetCoeff;
                djFilterModeBlend = targetModeBlend + (djFilterModeBlend - targetModeBlend) * modeCoeff;

                float xL = fL[i];
                float yL = b0 * xL + djFilterStateL1;
                djFilterStateL1 = b1 * xL - a1f * yL + djFilterStateL2;
                djFilterStateL2 = b2 * xL - a2f * yL;

                float xR = fR[i];
                float yR = b0 * xR + djFilterStateR1;
                djFilterStateR1 = b1 * xR - a1f * yR + djFilterStateR2;
                djFilterStateR2 = b2 * xR - a2f * yR;

                // Stability clamp on filter states
                if (!std::isfinite(djFilterStateL1) || std::abs(djFilterStateL1) > 1e6f) djFilterStateL1 = 0.0f;
                if (!std::isfinite(djFilterStateL2) || std::abs(djFilterStateL2) > 1e6f) djFilterStateL2 = 0.0f;
                if (!std::isfinite(djFilterStateR1) || std::abs(djFilterStateR1) > 1e6f) djFilterStateR1 = 0.0f;
                if (!std::isfinite(djFilterStateR2) || std::abs(djFilterStateR2) > 1e6f) djFilterStateR2 = 0.0f;

                // Crossfade dry→wet
                fL[i] = xL + (yL - xL) * djFilterWet;
                fR[i] = xR + (yR - xR) * djFilterWet;
            }

            // When fade-out completes, zero filter states for clean re-engage
            if (djFilterWet < 0.001f)
            {
                djFilterStateL1 = djFilterStateL2 = 0.0f;
                djFilterStateR1 = djFilterStateR2 = 0.0f;
                djFilterWet = 0.0f;
            }
        }
    }

    // ── Master Delay (IDENTICAL DSP to per-track delay) ──
    {
        float dMix = state.mDelayMix.load();
        if (dMix > 0.01f && !mDelayBufL.empty())
        {
            float sr = static_cast<float>(currentSampleRate);
            int delayAlgo = static_cast<int>(state.mDelayAlgo.load());
            float delayDamp = state.mDelayDamp.load();
            bool pp = state.mDelayPP.load() > 0.5f;

            float targetSec;
            bool syncOn = state.mDelaySync.load() > 0.5f;
            if (syncOn && currentBPM > 20.0f)
                targetSec = (60.0f / currentBPM) * state.mDelayBeats.load();
            else
                targetSec = 0.001f + state.mDelayTime.load() * 1.999f;

            float targetSmp = targetSec * sr;
            int bufLen = static_cast<int>(mDelayBufL.size());
            targetSmp = std::clamp (targetSmp, 4.0f, static_cast<float>(bufLen) - 4.0f);

            float smoothTime = syncOn ? 0.01f : 0.025f;
            float smoothCoeff = 1.0f - std::exp (-1.0f / (sr * smoothTime));
            if (mDelaySmoothSamples < 1.0f) mDelaySmoothSamples = targetSmp;

            // ── Delay algo modifiers (identical to per-track) ──
            float fb = std::min (0.92f, state.mDelayFB.load());
            float dm = dMix;

            float dampFreq = 20000.0f * std::pow (0.04f, delayDamp);
            dampFreq = std::clamp (dampFreq, 200.0f, 20000.0f);
            float dampCoeff = 1.0f - std::exp (-6.2831853f * dampFreq / sr);

            float* dL = stereoBuffer.getWritePointer (0);
            float* dR = stereoBuffer.getWritePointer (1);

            auto readCubic = [&](std::vector<float>& buf, int offset, float frac) -> float {
                auto rd = [&](int o) -> float {
                    return buf[static_cast<size_t>((mDelayWritePos - o + bufLen) % bufLen)];
                };
                float s0 = rd(offset-1), s1 = rd(offset), s2 = rd(offset+1), s3 = rd(offset+2);
                float c1=0.5f*(s2-s0), c2=s0-2.5f*s1+2*s2-0.5f*s3, c3=0.5f*(s3-s0)+1.5f*(s1-s2);
                return ((c3*frac+c2)*frac+c1)*frac+s1;
            };

            for (int i = 0; i < numSamples; ++i)
            {
                mDelaySmoothSamples += (targetSmp - mDelaySmoothSamples) * smoothCoeff;

                // ── Algo offset (tape wow/flutter, BBD noise) ──
                float algoOffset = 0.0f;
                float algoDampMul = 1.0f;
                bool doDiffuse = false;

                if (delayAlgo == 1) // TAPE
                {
                    mTapeWowPhase += 0.4 / static_cast<double>(currentSampleRate);
                    if (mTapeWowPhase >= 1.0) mTapeWowPhase -= 1.0;
                    float wow = std::sin (static_cast<float>(mTapeWowPhase * 6.283185307)) * sr * 0.003f;
                    float flutter = std::sin (static_cast<float>(mTapeWowPhase * 6.283185307 * 12.5)) * sr * 0.0005f;
                    algoOffset = wow + flutter;
                    algoDampMul = 0.6f;
                }
                else if (delayAlgo == 2) // BBD
                {
                    mBbdRng ^= mBbdRng << 13; mBbdRng ^= mBbdRng >> 17; mBbdRng ^= mBbdRng << 5;
                    algoOffset = (static_cast<float>(mBbdRng & 0xFFFF) / 65535.0f - 0.5f) * sr * 0.001f;
                    algoDampMul = 0.3f;
                }
                else if (delayAlgo == 3) // DIFFUSE
                {
                    algoDampMul = 0.5f;
                    doDiffuse = true;
                }

                float readP = mDelaySmoothSamples + algoOffset;
                readP = std::clamp (readP, 2.0f, static_cast<float>(bufLen) - 3.0f);
                int dI = static_cast<int>(readP);
                float dF = readP - static_cast<float>(dI);

                float effDampFreq = 20000.0f * std::pow (0.04f, delayDamp) * algoDampMul;
                effDampFreq = std::clamp (effDampFreq, 200.0f, 20000.0f);
                float effDampCoeff = 1.0f - std::exp (-6.2831853f * effDampFreq / sr);

                float delL = readCubic (mDelayBufL, dI, dF);

                // TAPE saturation
                if (delayAlgo == 1)
                {
                    float drive = 2.5f;
                    delL = std::tanh (delL * drive) / drive;
                }

                // DIFFUSE: allpass cascade
                if (doDiffuse)
                {
                    for (int da = 0; da < 4; ++da)
                    {
                        float delayed = mDiffAP[da].z;
                        float v = delL - delayed * 0.6f;
                        mDiffAP[da].z = v;
                        delL = delayed + v * 0.6f;
                    }
                }

                float cL = dL[i], cR = dR[i];

                if (pp)
                {
                    float delR = readCubic (mDelayBufR, dI, dF);
                    if (delayAlgo == 1) { float dr=2.5f; delR = std::tanh(delR*dr)/dr; }
                    float fbL = std::tanh (delL * fb);
                    float fbR = std::tanh (delR * fb);
                    mDlyFilterL += (fbL - mDlyFilterL) * effDampCoeff;
                    mDlyFilterR += (fbR - mDlyFilterR) * effDampCoeff;
                    float monoIn = (cL + cR) * 0.5f;
                    mDelayBufL[static_cast<size_t>(mDelayWritePos)] = monoIn + mDlyFilterR;
                    mDelayBufR[static_cast<size_t>(mDelayWritePos)] = mDlyFilterL;
                    mDelayWritePos = (mDelayWritePos + 1) % bufLen;
                    dL[i] = cL * (1.0f - dm * 0.5f) + delL * dm;
                    dR[i] = cR * (1.0f - dm * 0.5f) + delR * dm;
                }
                else
                {
                    float fbSig = std::tanh (delL * fb);
                    mDlyFilterL += (fbSig - mDlyFilterL) * effDampCoeff;
                    float monoIn = (cL + cR) * 0.5f;
                    mDelayBufL[static_cast<size_t>(mDelayWritePos)] = monoIn + mDlyFilterL;
                    mDelayWritePos = (mDelayWritePos + 1) % bufLen;
                    dL[i] = cL * (1.0f - dm*0.5f) + delL * dm;
                    dR[i] = cR * (1.0f - dm*0.5f) + delL * dm;
                }
            }
        }
    }

    // ── Restore master FX defaults after p-lock application ──
    state.gaterMix.store (mfxSave.gaterMix);
    state.gaterRate.store (mfxSave.gaterRate);
    state.gaterDepth.store (mfxSave.gaterDepth);
    state.gaterShape.store (mfxSave.gaterShape);
    state.gaterSmooth.store (mfxSave.gaterSmooth);
    state.mDelayMix.store (mfxSave.mDelayMix);
    state.mDelaySync.store (mfxSave.mDelaySync);
    state.mDelayBeats.store (mfxSave.mDelayBeats);
    state.mDelayTime.store (mfxSave.mDelayTime);
    state.mDelayFB.store (mfxSave.mDelayFB);
    state.mDelayPP.store (mfxSave.mDelayPP);
    state.mDelayHP.store (mfxSave.mDelayHP);
    state.mDelayLP.store (mfxSave.mDelayLP);
    state.mDelayAlgo.store (mfxSave.mDelayAlgo);
    state.mDelayDamp.store (mfxSave.mDelayDamp);
    state.djFilterFreq.store (mfxSave.djFilterFreq);
    state.djFilterRes.store (mfxSave.djFilterRes);

    // ══════════════════════════════════════════
    // MASTERING CHAIN
    // ══════════════════════════════════════════
    int qualityMode = state.quality.load();

    // ── Master Pultec EQ (before compression) — skip in ECO mode ──
    if (qualityMode >= 1)
    {
        MasterPultecEQ::Settings peq;
        peq.lowBoostFreq  = state.pultecLowFreq.load();
        peq.lowBoostAmt   = state.pultecLowBoost.load();
        peq.lowAttenAmt   = state.pultecLowAtten.load();
        peq.highBoostFreq = state.pultecHighFreq.load();
        peq.highBoostAmt  = state.pultecHighBoost.load();
        peq.highBoostBW   = state.pultecHighBW.load();
        peq.highAttenFreq = state.pultecHiAttnFrq.load();
        peq.highAttenAmt  = state.pultecHighAtten.load();
        masterPultecEQ.process (stereoBuffer.getWritePointer (0),
                                stereoBuffer.getWritePointer (1),
                                numSamples, peq);
    }

    // ── Master Compressor — skip in ECO mode ──
    if (qualityMode >= 1)
    {
        float threshold = state.compThreshold.load();
        float ratio = state.compRatio.load();
        float knee = state.compKnee.load();
        float atkMs = state.compAttack.load();
        float relMs = state.compRelease.load();

        // ── Compressor style modifiers ──
        int compStyle = state.compStyle.load();
        float satAmount = 0.0f; // post-GR saturation (WARM style)
        switch (compStyle)
        {
            case 1: // WARM — slower attack, wider knee, gentle saturation
                atkMs = std::max (atkMs, 5.0f) * 1.5f;
                knee = std::max (knee, 6.0f);
                satAmount = 0.15f;
                break;
            case 2: // PUNCH — fast attack, fast release, hard knee
                atkMs = std::min (atkMs, 2.0f);
                relMs = std::min (relMs, 80.0f);
                knee = std::min (knee, 2.0f);
                break;
            case 3: // GLUE — medium attack, slow release, wide knee (SSL-style)
                atkMs = std::clamp (atkMs, 10.0f, 30.0f);
                relMs = std::max (relMs, 300.0f);
                knee = std::max (knee, 8.0f);
                ratio = std::min (ratio, 6.0f);
                break;
            default: break; // CLEAN — no modifications
        }

        float attackCoeff  = std::exp (-1.0f / (currentSampleRate * atkMs * 0.001f));
        float releaseCoeff = std::exp (-1.0f / (currentSampleRate * relMs * 0.001f));
        float makeupGain = std::pow (10.0f, state.compMakeup.load() / 20.0f);

        // Sidechain HP filter: LP state tracks low freq, HP = input - LP
        float scHpFreq = state.compScHP.load();
        bool useScHP = (scHpFreq > 15.0f);
        float scLpCoeff = 1.0f;
        if (useScHP)
        {
            float wc = 2.0f * 3.14159265f * scHpFreq / static_cast<float>(currentSampleRate);
            scLpCoeff = std::clamp (wc / (1.0f + wc), 0.0f, 1.0f); // LP coefficient
        }

        float* cL = stereoBuffer.getWritePointer (0);
        float* cR = stereoBuffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            // Sidechain detection signal (optionally HP filtered to ignore bass)
            float scL = cL[i];
            float scR = cR[i];
            if (useScHP)
            {
                compScHpL += (cL[i] - compScHpL) * scLpCoeff; // LP tracks lows
                compScHpR += (cR[i] - compScHpR) * scLpCoeff;
                scL = cL[i] - compScHpL; // HP = input minus lows
                scR = cR[i] - compScHpR;
            }

            float inputLevel = std::max (std::abs (scL), std::abs (scR));
            float inputDb = (inputLevel > 1e-6f) ? 20.0f * std::log10 (inputLevel) : -96.0f;

            // Soft knee
            float overDb = inputDb - threshold;
            float gainReduction = 0.0f;
            if (overDb > knee * 0.5f)
                gainReduction = overDb * (1.0f - 1.0f / ratio);
            else if (overDb > -knee * 0.5f)
            {
                float x = overDb + knee * 0.5f;
                gainReduction = x * x / (2.0f * knee) * (1.0f - 1.0f / ratio);
            }

            // Envelope follower
            float target = gainReduction;
            if (target > masterCompEnv)
                masterCompEnv = attackCoeff * masterCompEnv + (1.0f - attackCoeff) * target;
            else
                masterCompEnv = releaseCoeff * masterCompEnv + (1.0f - releaseCoeff) * target;

            float gain = std::pow (10.0f, -masterCompEnv / 20.0f) * makeupGain;
            cL[i] *= gain;
            cR[i] *= gain;
            // WARM style: gentle tube-like saturation
            if (satAmount > 0.001f)
            {
                cL[i] = cL[i] * (1.0f - satAmount) + std::tanh (cL[i] * 1.2f) * satAmount;
                cR[i] = cR[i] * (1.0f - satAmount) + std::tanh (cR[i] * 1.2f) * satAmount;
            }
        }
    }

    // ── Master Limiter (brickwall) ──
    {
        // Track peak BEFORE limiter chain for accurate GR metering
        float preLimPeak = 0.0f;
        {
            const float* pL = stereoBuffer.getReadPointer (0);
            const float* pR = stereoBuffer.getReadPointer (1);
            for (int i = 0; i < numSamples; ++i)
                preLimPeak = std::max (preLimPeak, std::max (std::abs (pL[i]), std::abs (pR[i])));
        }

        // Input gain drive
        float limDrive = std::pow (10.0f, state.limInputGain.load() / 20.0f);
        if (limDrive > 1.001f)
        {
            stereoBuffer.applyGain (limDrive);
            preLimPeak *= limDrive;
        }

        // ── HQ MODE: 2x oversampled soft-clip above threshold ──
        // ONLY processes samples that actually need clipping.
        // Clean signals pass through completely untouched (no filtering).
        if (qualityMode >= 2)
        {
            float* hqL = stereoBuffer.getWritePointer (0);
            float* hqR = stereoBuffer.getWritePointer (1);
            const float hqT = 0.9f; // threshold: ~-0.9 dBFS
            for (int i = 0; i < numSamples; ++i)
            {
                float absL = std::abs (hqL[i]);
                float absR = std::abs (hqR[i]);
                float absPrevL = std::abs (hqPrevL);
                float absPrevR = std::abs (hqPrevR);

                // Only engage oversampled soft-clip when signal is hot
                if (absL > hqT || absR > hqT || absPrevL > hqT || absPrevR > hqT)
                {
                    // Upsample: interpolate midpoint
                    float midL = (hqL[i] + hqPrevL) * 0.5f;
                    float midR = (hqR[i] + hqPrevR) * 0.5f;

                    // Gain-matched cubic soft-clip
                    auto sc = [hqT](float x) -> float {
                        float ax = std::abs (x);
                        if (ax <= hqT) return x;
                        float over = ax - hqT;
                        float range = 1.0f - hqT;
                        float norm = over / range;
                        float shaped = range * norm / (1.0f + norm);
                        return (x >= 0.0f) ? (hqT + shaped) : -(hqT + shaped);
                    };

                    midL = sc (midL);
                    midR = sc (midR);
                    hqL[i] = sc (hqL[i]);
                    hqR[i] = sc (hqR[i]);

                    // Downsample (average)
                    hqL[i] = (hqL[i] + midL) * 0.5f;
                    hqR[i] = (hqR[i] + midR) * 0.5f;
                }
                // Clean signals: no processing at all

                hqPrevL = hqL[i];
                hqPrevR = hqR[i];
            }
        }

        float ceiling = std::pow (10.0f, state.limCeiling.load() / 20.0f);
        float limitAttack  = std::exp (-1.0f / (currentSampleRate * 0.0005f));
        // Auto-release: faster release when GR is heavy
        float relMs = state.limRelease.load();
        bool autoRel = state.limAutoRel.load() > 0.5f;
        float limitRelease = std::exp (-1.0f / (currentSampleRate * relMs * 0.001f));

        float* lL = stereoBuffer.getWritePointer (0);
        float* lR = stereoBuffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            float pk = std::max (std::abs (lL[i]), std::abs (lR[i]));
            float targetGain = (pk > ceiling) ? ceiling / pk : 1.0f;
            if (targetGain < masterLimGain)
                masterLimGain = limitAttack * masterLimGain + (1.0f - limitAttack) * targetGain;
            else
            {
                float rel = limitRelease;
                if (autoRel && masterLimGain < 0.7f) // heavy GR → faster release
                    rel = std::exp (-1.0f / (currentSampleRate * std::max (5.0f, relMs * 0.3f) * 0.001f));
                masterLimGain = rel * masterLimGain + (1.0f - rel) * targetGain;
            }
            lL[i] *= masterLimGain;
            lR[i] *= masterLimGain;
            // Hard clip safety — nothing escapes above ceiling
            lL[i] = std::clamp (lL[i], -ceiling, ceiling);
            lR[i] = std::clamp (lR[i], -ceiling, ceiling);
        }

        // Track peak AFTER limiter chain for accurate GR
        float postLimPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            postLimPeak = std::max (postLimPeak, std::max (std::abs (lL[i]), std::abs (lR[i])));
        // Actual GR = difference between pre and post peaks (in dB)
        if (preLimPeak > 0.001f && postLimPeak < preLimPeak)
            masterLimGRdB = 20.0f * std::log10 (postLimPeak / preLimPeak);
        else
            masterLimGRdB = 0.0f;
    }

    // ═══════════════════════════════════════
    // METRONOME — synthesized click on beats
    // ═══════════════════════════════════════
    if (state.metronomeOn.load() && state.playing.load())
    {
        float clickBpm = state.bpm.load();
        double samplesPerBeat = currentSampleRate * 60.0 / static_cast<double>(clickBpm);
        float vol = state.metronomeVol.load();
        int snd = state.metronomeSound.load();
        float* mOutL = stereoBuffer.getWritePointer (0);
        float* mOutR = stereoBuffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            // Check if we're at a beat boundary
            if (metroBeatAcc >= samplesPerBeat)
            {
                metroBeatAcc -= samplesPerBeat;
                metroClickPhase = 0.0;
                metroClickSamplesLeft = static_cast<int>(currentSampleRate * 0.015); // 15ms click
                metroBeatCount = (metroBeatCount + 1) % 4;
            }
            metroBeatAcc += 1.0;

            // Synthesize click
            if (metroClickSamplesLeft > 0)
            {
                float env = static_cast<float>(metroClickSamplesLeft) / static_cast<float>(currentSampleRate * 0.015);
                env = env * env; // exponential decay
                float clickSample = 0.0f;
                bool isDownbeat = (metroBeatCount == 0);
                float freq = isDownbeat ? 1200.0f : 800.0f; // downbeat higher

                switch (snd)
                {
                    case 0: // Classic click (sine burst)
                        clickSample = std::sin (static_cast<float>(metroClickPhase * 6.2831853)) * env;
                        break;
                    case 1: // Hi click (higher freq)
                        freq = isDownbeat ? 2400.0f : 1600.0f;
                        clickSample = std::sin (static_cast<float>(metroClickPhase * 6.2831853)) * env;
                        break;
                    case 2: // Cowbell (two detuned sines)
                    {
                        freq = isDownbeat ? 800.0f : 540.0f;
                        float f2 = freq * 1.504f;
                        clickSample = (std::sin (static_cast<float>(metroClickPhase * 6.2831853))
                                     + 0.6f * std::sin (static_cast<float>(metroClickPhase * 6.2831853 * f2 / freq))) * env * 0.6f;
                        break;
                    }
                    case 3: // Rimshot (noise + sine)
                    {
                        float noise = (static_cast<float>(metroRng & 0xFFFF) / 32768.0f - 1.0f);
                        metroRng = metroRng * 1664525u + 1013904223u;
                        clickSample = (0.5f * std::sin (static_cast<float>(metroClickPhase * 6.2831853))
                                     + 0.5f * noise) * env;
                        break;
                    }
                }
                metroClickPhase += static_cast<double>(freq) / currentSampleRate;
                clickSample *= vol * (isDownbeat ? 0.5f : 0.35f);
                mOutL[i] += clickSample;
                mOutR[i] += clickSample;
                --metroClickSamplesLeft;
            }
        }
    }

    // Peak + RMS tracking for meter (post-limiter)
    float pkL = 0.0f, pkR = 0.0f;
    float sumSqL = 0.0f, sumSqR = 0.0f;
    const float* mL = stereoBuffer.getReadPointer (0);
    const float* mR = stereoBuffer.getReadPointer (1);
    for (int i = 0; i < numSamples; ++i) {
        pkL = std::max (pkL, std::abs (mL[i]));
        pkR = std::max (pkR, std::abs (mR[i]));
        sumSqL += mL[i] * mL[i];
        sumSqR += mR[i] * mR[i];
    }
    float blockRmsL = std::sqrt (sumSqL / std::max (1, numSamples));
    float blockRmsR = std::sqrt (sumSqR / std::max (1, numSamples));
    state.peakL.store (std::max (state.peakL.load() * 0.95f, pkL));
    state.peakR.store (std::max (state.peakR.load() * 0.95f, pkR));
    // RMS: slower decay for smooth display
    state.rmsL.store (std::max (state.rmsL.load() * 0.92f, blockRmsL));
    state.rmsR.store (std::max (state.rmsR.load() * 0.92f, blockRmsR));

    // GR metering (smooth decay)
    state.compGR.store (std::max (state.compGR.load() * 0.92f, masterCompEnv));
    float limGRdB = -masterLimGRdB; // convert negative dB to positive for display
    state.limGR.store (std::max (state.limGR.load() * 0.92f, limGRdB));

    buffer.copyFrom (0, 0, stereoBuffer, 0, 0, numSamples);
    if (totalNumOutputChannels > 1)
        buffer.copyFrom (1, 0, stereoBuffer, 1, 0, numSamples);
    else
        buffer.addFrom (0, 0, stereoBuffer, 1, 0, numSamples, 0.5f);
}

juce::AudioProcessorEditor* GrooveBoxProcessor::createEditor()
{
    return new GrooveBoxEditor (*this);
}

void GrooveBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = std::make_unique<juce::XmlElement> ("GrooveBoxPhonica");
    state.saveToXml (*xml);
    copyXmlToBinary (*xml, destData);
}

void GrooveBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName ("GrooveBoxPhonica"))
        state.loadFromXml (*xml);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveBoxProcessor();
}
