#include "GrooveBoxState.h"
#include "PresetManager.h"

GrooveBoxState::GrooveBoxState()
{
    initDrumDefaults();
    for (int i = 0; i < 5; ++i)
    {
        synthTracks[static_cast<size_t>(i)].partIndex = i;
        synthTracks[static_cast<size_t>(i)].model = SynthModel::Analog;
    }
}

void GrooveBoxState::initDrumDefaults()
{
    DrumType types[] = {
        DrumType::Kick, DrumType::Snare, DrumType::HiHatClosed,
        DrumType::HiHatOpen, DrumType::Clap, DrumType::Tom,
        DrumType::TomHi, DrumType::Cowbell, DrumType::Rimshot, DrumType::Crash
    };
    for (int i = 0; i < 10; ++i)
    {
        auto& t = drumTracks[static_cast<size_t>(i)];
        t.type = types[i]; t.length = 16; t.clockMul = 1.0f;
        t.muted = false; t.solo = false; t.volume = 0.6f;
        t.swing = 0; t.page = 0; t.fmMix = 0.0f; t.seq.reset();
        switch (t.type)
        {
            case DrumType::Kick:       t.pitch=50; t.tune=0.0f; t.pitchDec=0.05f; t.decay=0.3f; t.click=0.3f; t.fmRatio=2.0f; t.fmDepth=80; t.fmDecay=0.05f; t.drumCut=100; t.drumRes=0; t.drumFiltEnv=0; t.drumFiltA=0.001f; t.drumFiltD=0.15f; break;
            case DrumType::Snare:      t.tone=185; t.toneDecay=0.08f; t.noiseDecay=0.18f; t.snap=0.8f; t.fmRatio=1.4f; t.fmDepth=60; t.fmDecay=0.08f; break;
            case DrumType::HiHatClosed:t.cutoff=9000; t.decay=0.04f; t.fmRatio=7.2f; t.fmDepth=120; t.fmDecay=0.03f; break;
            case DrumType::HiHatOpen:  t.cutoff=7000; t.decay=0.35f; t.fmRatio=7.2f; t.fmDepth=80; t.fmDecay=0.15f; break;
            case DrumType::Clap:       t.freq=1500; t.decay=0.1f; t.spread=0.025f; t.fmRatio=3.5f; t.fmDepth=100; t.fmDecay=0.06f; break;
            case DrumType::Tom:        t.pitch=100; t.pitchEnd=65; t.pitchDec=0.04f; t.decay=0.35f; t.click=0.2f; t.fmRatio=1.5f; t.fmDepth=60; t.fmDecay=0.1f; break;
            case DrumType::TomHi:      t.pitch=180; t.pitchEnd=130; t.pitchDec=0.03f; t.decay=0.2f; t.click=0.25f; t.fmRatio=1.8f; t.fmDepth=70; t.fmDecay=0.06f; break;
            case DrumType::Cowbell:    t.freq1=540; t.freq2=845; t.decay=0.4f; t.fmRatio=2.8f; t.fmDepth=40; t.fmDecay=0.2f; break;
            case DrumType::Rimshot:    t.tone=400; t.decay=0.03f; t.noise=0.6f; t.fmRatio=5.0f; t.fmDepth=80; t.fmDecay=0.02f; break;
            case DrumType::Crash:      t.freq=5000; t.decay=1.2f; t.fmRatio=8.5f; t.fmDepth=200; t.fmDecay=0.5f; break;
        }
    }
}

bool GrooveBoxState::anySolo() const
{
    for (auto& t : drumTracks)  if (t.solo) return true;
    for (auto& t : synthTracks) if (t.solo) return true;
    return false;
}
bool GrooveBoxState::isEffectivelyMuted (int drumIdx) const
{
    if (drumIdx < 0 || drumIdx >= 10) return true;
    const auto& t = drumTracks[static_cast<size_t>(drumIdx)];
    return t.muted || (anySolo() && !t.solo);
}
bool GrooveBoxState::isSynthEffectivelyMuted (int synthIdx) const
{
    if (synthIdx < 0 || synthIdx >= 5) return true;
    const auto& t = synthTracks[static_cast<size_t>(synthIdx)];
    return t.muted || (anySolo() && !t.solo);
}

// ═══════════════════════════════════════
// Step serialization helpers
// ═══════════════════════════════════════
static void saveSteps (juce::XmlElement& trackEl, const StepSequence& seq, bool isSynth)
{
    for (int s = 0; s < kMaxSteps; ++s)
    {
        const auto& step = seq.steps[static_cast<size_t>(s)];
        bool hasPlocks = !step.plocks.empty();
        bool hasData = step.active || step.velocity != 100 || step.gate != 100
                     || step.ratchet != 1 || step.slide || step.trigless || step.nudge != 0
                     || step.chordMode != -1 || step.strum > 0 || hasPlocks;
        if (isSynth) hasData = hasData || step.noteIndex != 0 || step.octave != 3;
        if (!hasData) continue;

        auto* stepEl = trackEl.createNewChildElement ("Step");
        stepEl->setAttribute ("idx", s);
        stepEl->setAttribute ("on", (int) step.active);
        stepEl->setAttribute ("vel", (int) step.velocity);
        stepEl->setAttribute ("gate", (int) step.gate);
        stepEl->setAttribute ("ratch", (int) step.ratchet);
        stepEl->setAttribute ("trip", (int) step.triplet);
        stepEl->setAttribute ("slide", (int) step.slide);
        stepEl->setAttribute ("trigless", (int) step.trigless);
        stepEl->setAttribute ("nudge", (int) step.nudge);
        stepEl->setAttribute ("noteLen", (int) step.noteLen);
        stepEl->setAttribute ("cond", (int) step.cond);
        stepEl->setAttribute ("chord", (int) step.chordMode);
        if (step.strum > 0) stepEl->setAttribute ("strum", (int) step.strum);
        if (isSynth)
        {
            stepEl->setAttribute ("note", (int) step.noteIndex);
            stepEl->setAttribute ("oct", (int) step.octave);
        }
        if (hasPlocks)
        {
            for (auto& [key, val] : step.plocks)
            {
                auto* pl = stepEl->createNewChildElement ("PL");
                pl->setAttribute ("k", juce::String (key));
                pl->setAttribute ("v", (double) val);
            }
        }
    }
}

static void loadSteps (const juce::XmlElement& trackEl, StepSequence& seq, bool isSynth)
{
    seq.reset();
    for (auto* stepEl = trackEl.getChildByName ("Step"); stepEl;
         stepEl = stepEl->getNextElementWithTagName ("Step"))
    {
        int si = stepEl->getIntAttribute ("idx", -1);
        if (si < 0 || si >= kMaxSteps) continue;
        auto& step = seq.steps[static_cast<size_t>(si)];
        step.active   = stepEl->getBoolAttribute ("on", false);
        step.velocity = (uint8_t) stepEl->getIntAttribute ("vel", 100);
        step.gate     = (uint8_t) stepEl->getIntAttribute ("gate", 100);
        step.ratchet  = (uint8_t) stepEl->getIntAttribute ("ratch", 1);
        step.triplet  = stepEl->getBoolAttribute ("trip", false);
        step.slide    = stepEl->getBoolAttribute ("slide", false);
        step.trigless  = stepEl->getBoolAttribute ("trigless", false);
        step.nudge    = (int8_t) stepEl->getIntAttribute ("nudge", 0);
        step.noteLen  = (uint8_t) stepEl->getIntAttribute ("noteLen", 1);
        step.cond     = (TrigCondition) stepEl->getIntAttribute ("cond", 0);
        step.chordMode= (int8_t) stepEl->getIntAttribute ("chord", -1);
        step.strum    = (uint8_t) stepEl->getIntAttribute ("strum", 0);
        if (isSynth)
        {
            step.noteIndex = (uint8_t) stepEl->getIntAttribute ("note", 0);
            step.octave    = (uint8_t) stepEl->getIntAttribute ("oct", 3);
        }
        for (auto* pl = stepEl->getChildByName ("PL"); pl;
             pl = pl->getNextElementWithTagName ("PL"))
        {
            auto k = pl->getStringAttribute ("k").toStdString();
            float v = (float) pl->getDoubleAttribute ("v", 0.0);
            if (!k.empty()) step.plocks[k] = v;
        }
    }
}

// ═══════════════════════════════════════
// FULL SERIALIZATION — uses PresetManager helpers for ALL params
// ═══════════════════════════════════════
void GrooveBoxState::saveToXml (juce::XmlElement& xml) const
{
    xml.setAttribute ("bpm", (double) bpm.load());
    xml.setAttribute ("swing", globalSwing.load());
    xml.setAttribute ("masterVol", (double) masterVolume.load());
    xml.setAttribute ("quality", quality.load());
    xml.setAttribute ("compStyle", compStyle.load());
    xml.setAttribute ("scaleRoot", scaleRoot.load());
    xml.setAttribute ("scaleType", scaleType.load());

    // ── Master Pultec EQ ──
    xml.setAttribute ("pultLB",  (double) pultecLowBoost.load());
    xml.setAttribute ("pultLA",  (double) pultecLowAtten.load());
    xml.setAttribute ("pultLF",  (double) pultecLowFreq.load());
    xml.setAttribute ("pultHB",  (double) pultecHighBoost.load());
    xml.setAttribute ("pultHA",  (double) pultecHighAtten.load());
    xml.setAttribute ("pultHF",  (double) pultecHighFreq.load());
    xml.setAttribute ("pultHBW", (double) pultecHighBW.load());
    xml.setAttribute ("pultHAF", (double) pultecHiAttnFrq.load());

    // ── Master Compressor ──
    xml.setAttribute ("compThr", (double) compThreshold.load());
    xml.setAttribute ("compRat", (double) compRatio.load());
    xml.setAttribute ("compAtk", (double) compAttack.load());
    xml.setAttribute ("compRel", (double) compRelease.load());
    xml.setAttribute ("compMkp", (double) compMakeup.load());
    xml.setAttribute ("compKne", (double) compKnee.load());
    xml.setAttribute ("compSHP", (double) compScHP.load());

    // ── Master Limiter ──
    xml.setAttribute ("limCeil", (double) limCeiling.load());
    xml.setAttribute ("limRel",  (double) limRelease.load());
    xml.setAttribute ("limDrv",  (double) limInputGain.load());
    xml.setAttribute ("limAuto", (double) limAutoRel.load());

    // ── Master Gater ──
    xml.setAttribute ("gatMix",  (double) gaterMix.load());
    xml.setAttribute ("gatRate", (double) gaterRate.load());
    xml.setAttribute ("gatDep",  (double) gaterDepth.load());
    xml.setAttribute ("gatShp",  (double) gaterShape.load());
    xml.setAttribute ("gatSmo",  (double) gaterSmooth.load());

    // ── Master Delay ──
    xml.setAttribute ("mdMix",   (double) mDelayMix.load());
    xml.setAttribute ("mdTime",  (double) mDelayTime.load());
    xml.setAttribute ("mdFB",    (double) mDelayFB.load());
    xml.setAttribute ("mdSync",  (double) mDelaySync.load());
    xml.setAttribute ("mdBeats", (double) mDelayBeats.load());
    xml.setAttribute ("mdHP",    (double) mDelayHP.load());
    xml.setAttribute ("mdLP",    (double) mDelayLP.load());
    xml.setAttribute ("mdPP",    (double) mDelayPP.load());
    xml.setAttribute ("mdAlgo",  (double) mDelayAlgo.load());
    xml.setAttribute ("mdDamp",  (double) mDelayDamp.load());

    // ── DJ Filter ──
    xml.setAttribute ("djFreq",  (double) djFilterFreq.load());
    xml.setAttribute ("djRes",   (double) djFilterRes.load());
    xml.setAttribute ("mfxLen",  masterFXLength.load());

    for (int i = 0; i < 10; ++i)
    {
        const auto& dt = drumTracks[static_cast<size_t>(i)];
        auto* trackEl = xml.createNewChildElement ("DrumTrack");
        trackEl->setAttribute ("index", i);
        trackEl->setAttribute ("type", static_cast<int>(dt.type));
        trackEl->setAttribute ("length", dt.length);
        trackEl->setAttribute ("clockMul", (double) dt.clockMul);
        trackEl->setAttribute ("muted", (int) dt.muted);
        trackEl->setAttribute ("solo", (int) dt.solo);
        trackEl->setAttribute ("swing", dt.swing);
        trackEl->setAttribute ("playDir", dt.playDir);
        trackEl->setAttribute ("msegRetrig", (int) dt.msegRetrig);
        PresetManager::saveDrumParams (*trackEl, dt);
        saveSteps (*trackEl, dt.seq, false);
    }

    for (int i = 0; i < 5; ++i)
    {
        const auto& st = synthTracks[static_cast<size_t>(i)];
        auto* trackEl = xml.createNewChildElement ("SynthTrack");
        trackEl->setAttribute ("index", i);
        trackEl->setAttribute ("length", st.length);
        trackEl->setAttribute ("clockMul", (double) st.clockMul);
        trackEl->setAttribute ("muted", (int) st.muted);
        trackEl->setAttribute ("solo", (int) st.solo);
        trackEl->setAttribute ("swing", st.swing);
        trackEl->setAttribute ("playDir", st.playDir);
        trackEl->setAttribute ("msegRetrig", (int) st.msegRetrig);
        // Arp data
        trackEl->setAttribute ("arpEnabled", (int) st.arp.enabled);
        trackEl->setAttribute ("arpSteps", st.arp.numSteps);
        trackEl->setAttribute ("arpDiv", st.arp.division);
        trackEl->setAttribute ("arpOct", st.arp.octaves);
        trackEl->setAttribute ("arpDir", st.arp.direction);
        trackEl->setAttribute ("arpRetrig", (int) st.arp.keyRetrig);
        trackEl->setAttribute ("arpLoop", st.arp.loopLen);
        trackEl->setAttribute ("arpTgt", st.arp.assignTarget);
        trackEl->setAttribute ("arpDepth", (double) st.arp.assignDepth);
        for (int ri = 0; ri < 16; ++ri)
        {
            if (st.arp.extraRoutes[static_cast<size_t>(ri)].target >= 0)
            {
                auto* rel = trackEl->createNewChildElement ("ArpRoute");
                rel->setAttribute ("idx", ri);
                rel->setAttribute ("tgt", st.arp.extraRoutes[static_cast<size_t>(ri)].target);
                rel->setAttribute ("dep", (double) st.arp.extraRoutes[static_cast<size_t>(ri)].depth);
            }
        }
        {
            auto* arpEl = trackEl->createNewChildElement ("ArpSteps");
            for (int s = 0; s < st.arp.numSteps; ++s)
            {
                auto* stepEl = arpEl->createNewChildElement ("S");
                stepEl->setAttribute ("v", st.arp.steps[static_cast<size_t>(s)].velocity);
                stepEl->setAttribute ("g", st.arp.steps[static_cast<size_t>(s)].gate);
                stepEl->setAttribute ("p", (double) st.arp.steps[static_cast<size_t>(s)].param);
            }
        }
        PresetManager::saveSynthParams (*trackEl, st);
        saveSteps (*trackEl, st.seq, true);
    }

    // ── Macros ──
    macroEngine.saveToXml (xml);
}

void GrooveBoxState::loadFromXml (const juce::XmlElement& xml)
{
    bpm.store ((float) xml.getDoubleAttribute ("bpm", 120.0));
    globalSwing.store (xml.getIntAttribute ("swing", 0));
    masterVolume.store ((float) xml.getDoubleAttribute ("masterVol", 0.8));
    quality.store (xml.getIntAttribute ("quality", 1));
    compStyle.store (xml.getIntAttribute ("compStyle", 0));
    scaleRoot.store (xml.getIntAttribute ("scaleRoot", 0));
    scaleType.store (xml.getIntAttribute ("scaleType", 0));

    // ── Master Pultec EQ ──
    pultecLowBoost.store  ((float) xml.getDoubleAttribute ("pultLB",  pultecLowBoost.load()));
    pultecLowAtten.store  ((float) xml.getDoubleAttribute ("pultLA",  pultecLowAtten.load()));
    pultecLowFreq.store   ((float) xml.getDoubleAttribute ("pultLF",  pultecLowFreq.load()));
    pultecHighBoost.store ((float) xml.getDoubleAttribute ("pultHB",  pultecHighBoost.load()));
    pultecHighAtten.store ((float) xml.getDoubleAttribute ("pultHA",  pultecHighAtten.load()));
    pultecHighFreq.store  ((float) xml.getDoubleAttribute ("pultHF",  pultecHighFreq.load()));
    pultecHighBW.store    ((float) xml.getDoubleAttribute ("pultHBW", pultecHighBW.load()));
    pultecHiAttnFrq.store ((float) xml.getDoubleAttribute ("pultHAF", pultecHiAttnFrq.load()));

    // ── Master Compressor ──
    compThreshold.store ((float) xml.getDoubleAttribute ("compThr", compThreshold.load()));
    compRatio.store     ((float) xml.getDoubleAttribute ("compRat", compRatio.load()));
    compAttack.store    ((float) xml.getDoubleAttribute ("compAtk", compAttack.load()));
    compRelease.store   ((float) xml.getDoubleAttribute ("compRel", compRelease.load()));
    compMakeup.store    ((float) xml.getDoubleAttribute ("compMkp", compMakeup.load()));
    compKnee.store      ((float) xml.getDoubleAttribute ("compKne", compKnee.load()));
    compScHP.store      ((float) xml.getDoubleAttribute ("compSHP", compScHP.load()));

    // ── Master Limiter ──
    limCeiling.store   ((float) xml.getDoubleAttribute ("limCeil", limCeiling.load()));
    limRelease.store   ((float) xml.getDoubleAttribute ("limRel",  limRelease.load()));
    limInputGain.store ((float) xml.getDoubleAttribute ("limDrv",  limInputGain.load()));
    limAutoRel.store   ((float) xml.getDoubleAttribute ("limAuto", limAutoRel.load()));

    // ── Master Gater ──
    gaterMix.store    ((float) xml.getDoubleAttribute ("gatMix",  gaterMix.load()));
    gaterRate.store   ((float) xml.getDoubleAttribute ("gatRate", gaterRate.load()));
    gaterDepth.store  ((float) xml.getDoubleAttribute ("gatDep",  gaterDepth.load()));
    gaterShape.store  ((float) xml.getDoubleAttribute ("gatShp",  gaterShape.load()));
    gaterSmooth.store ((float) xml.getDoubleAttribute ("gatSmo",  gaterSmooth.load()));

    // ── Master Delay ──
    mDelayMix.store   ((float) xml.getDoubleAttribute ("mdMix",   mDelayMix.load()));
    mDelayTime.store  ((float) xml.getDoubleAttribute ("mdTime",  mDelayTime.load()));
    mDelayFB.store    ((float) xml.getDoubleAttribute ("mdFB",    mDelayFB.load()));
    mDelaySync.store  ((float) xml.getDoubleAttribute ("mdSync",  mDelaySync.load()));
    mDelayBeats.store ((float) xml.getDoubleAttribute ("mdBeats", mDelayBeats.load()));
    mDelayHP.store    ((float) xml.getDoubleAttribute ("mdHP",    mDelayHP.load()));
    mDelayLP.store    ((float) xml.getDoubleAttribute ("mdLP",    mDelayLP.load()));
    mDelayPP.store    ((float) xml.getDoubleAttribute ("mdPP",    mDelayPP.load()));
    mDelayAlgo.store  ((float) xml.getDoubleAttribute ("mdAlgo",  mDelayAlgo.load()));
    mDelayDamp.store  ((float) xml.getDoubleAttribute ("mdDamp",  mDelayDamp.load()));

    // ── DJ Filter ──
    djFilterFreq.store ((float) xml.getDoubleAttribute ("djFreq", djFilterFreq.load()));
    djFilterRes.store  ((float) xml.getDoubleAttribute ("djRes",  djFilterRes.load()));
    masterFXLength.store (xml.getIntAttribute ("mfxLen", masterFXLength.load()));

    for (auto* trackEl = xml.getChildByName ("DrumTrack"); trackEl;
         trackEl = trackEl->getNextElementWithTagName ("DrumTrack"))
    {
        int idx = trackEl->getIntAttribute ("index", -1);
        if (idx < 0 || idx >= 10) continue;
        auto& t = drumTracks[static_cast<size_t>(idx)];
        t.type = (DrumType) trackEl->getIntAttribute ("type", (int) t.type);
        t.length = trackEl->getIntAttribute ("length", 16);
        t.clockMul = (float) trackEl->getDoubleAttribute ("clockMul", 1.0);
        t.muted = trackEl->getBoolAttribute ("muted", false);
        t.solo = trackEl->getBoolAttribute ("solo", false);
        t.swing = trackEl->getIntAttribute ("swing", 0);
        t.playDir = trackEl->getIntAttribute ("playDir", 0);
        t.msegRetrig = trackEl->getBoolAttribute ("msegRetrig", true);
        PresetManager::loadDrumParams (*trackEl, t);
        loadSteps (*trackEl, t.seq, false);
    }

    for (auto* trackEl = xml.getChildByName ("SynthTrack"); trackEl;
         trackEl = trackEl->getNextElementWithTagName ("SynthTrack"))
    {
        int idx = trackEl->getIntAttribute ("index", -1);
        if (idx < 0 || idx >= 5) continue;
        auto& t = synthTracks[static_cast<size_t>(idx)];
        t.length = trackEl->getIntAttribute ("length", 16);
        t.clockMul = (float) trackEl->getDoubleAttribute ("clockMul", 1.0);
        t.muted = trackEl->getBoolAttribute ("muted", false);
        t.solo = trackEl->getBoolAttribute ("solo", false);
        t.swing = trackEl->getIntAttribute ("swing", 0);
        t.playDir = trackEl->getIntAttribute ("playDir", 0);
        t.msegRetrig = trackEl->getBoolAttribute ("msegRetrig", true);
        // Arp data
        t.arp.enabled = trackEl->getBoolAttribute ("arpEnabled", false);
        t.arp.numSteps = trackEl->getIntAttribute ("arpSteps", 8);
        t.arp.division = trackEl->getIntAttribute ("arpDiv", 3);
        t.arp.octaves = trackEl->getIntAttribute ("arpOct", 1);
        t.arp.direction = trackEl->getIntAttribute ("arpDir", 0);
        t.arp.keyRetrig = trackEl->getBoolAttribute ("arpRetrig", true);
        t.arp.loopLen = trackEl->getIntAttribute ("arpLoop", 0);
        t.arp.assignTarget = trackEl->getIntAttribute ("arpTgt", 1);
        t.arp.assignDepth = (float) trackEl->getDoubleAttribute ("arpDepth", 0.0);
        for (auto* rel = trackEl->getChildByName ("ArpRoute"); rel; rel = rel->getNextElementWithTagName ("ArpRoute"))
        {
            int ri = rel->getIntAttribute ("idx", -1);
            if (ri >= 0 && ri < 16)
            {
                t.arp.extraRoutes[static_cast<size_t>(ri)].target = rel->getIntAttribute ("tgt", -1);
                t.arp.extraRoutes[static_cast<size_t>(ri)].depth = (float) rel->getDoubleAttribute ("dep", 0.0);
            }
        }
        if (auto* arpEl = trackEl->getChildByName ("ArpSteps"))
        {
            int s = 0;
            for (auto* stepEl = arpEl->getChildByName ("S"); stepEl && s < kArpMaxSteps;
                 stepEl = stepEl->getNextElementWithTagName ("S"), ++s)
            {
                t.arp.steps[static_cast<size_t>(s)].velocity = static_cast<uint8_t>(stepEl->getIntAttribute ("v", 100));
                t.arp.steps[static_cast<size_t>(s)].gate = static_cast<uint8_t>(stepEl->getIntAttribute ("g", 80));
                t.arp.steps[static_cast<size_t>(s)].param = (float) stepEl->getDoubleAttribute ("p", 0.5);
            }
        }
        PresetManager::loadSynthParams (*trackEl, t);
        loadSteps (*trackEl, t.seq, true);
    }

    // ── Macros ──
    macroEngine.loadFromXml (xml);
}

// ═══════════════════════════════════════
// UNDO / REDO with full param serialization
// ═══════════════════════════════════════

void GrooveBoxState::pushUndo()
{
    juce::XmlElement root ("UndoState");
    for (int i = 0; i < 10; ++i)
    {
        auto* el = root.createNewChildElement ("D" + juce::String (i));
        PresetManager::saveDrumParams (*el, drumTracks[static_cast<size_t>(i)]);
    }
    for (int i = 0; i < 5; ++i)
    {
        auto* el = root.createNewChildElement ("S" + juce::String (i));
        PresetManager::saveSynthParams (*el, synthTracks[static_cast<size_t>(i)]);
    }
    undoManager.pushUndo (drumTracks, synthTracks, root.toString().toStdString());
}

bool GrooveBoxState::undo()
{
    juce::XmlElement cur ("UndoState");
    for (int i = 0; i < 10; ++i)
    { auto* el = cur.createNewChildElement ("D" + juce::String (i)); PresetManager::saveDrumParams (*el, drumTracks[static_cast<size_t>(i)]); }
    for (int i = 0; i < 5; ++i)
    { auto* el = cur.createNewChildElement ("S" + juce::String (i)); PresetManager::saveSynthParams (*el, synthTracks[static_cast<size_t>(i)]); }

    if (!undoManager.undo (drumTracks, synthTracks, cur.toString().toStdString()))
        return false;

    auto xml = undoManager.getLastRestoredParamXml();
    if (!xml.empty())
    {
        auto parsed = juce::parseXML (juce::String (xml));
        if (parsed != nullptr)
        {
            for (int i = 0; i < 10; ++i)
                if (auto* el = parsed->getChildByName ("D" + juce::String (i)))
                    PresetManager::loadDrumParams (*el, drumTracks[static_cast<size_t>(i)]);
            for (int i = 0; i < 5; ++i)
                if (auto* el = parsed->getChildByName ("S" + juce::String (i)))
                    PresetManager::loadSynthParams (*el, synthTracks[static_cast<size_t>(i)]);
        }
    }
    return true;
}

bool GrooveBoxState::redo()
{
    if (!undoManager.redo (drumTracks, synthTracks))
        return false;
    auto xml = undoManager.getLastRestoredParamXml();
    if (!xml.empty())
    {
        auto parsed = juce::parseXML (juce::String (xml));
        if (parsed != nullptr)
        {
            for (int i = 0; i < 10; ++i)
                if (auto* el = parsed->getChildByName ("D" + juce::String (i)))
                    PresetManager::loadDrumParams (*el, drumTracks[static_cast<size_t>(i)]);
            for (int i = 0; i < 5; ++i)
                if (auto* el = parsed->getChildByName ("S" + juce::String (i)))
                    PresetManager::loadSynthParams (*el, synthTracks[static_cast<size_t>(i)]);
        }
    }
    return true;
}

// ═══════════════════════════════════════
// INIT ALL — Factory Reset
// ═══════════════════════════════════════

void GrooveBoxState::initAll()
{
    // Reset all drum tracks — fresh objects + type defaults
    {
        DrumType types[] = {
            DrumType::Kick, DrumType::Snare, DrumType::HiHatClosed,
            DrumType::HiHatOpen, DrumType::Clap, DrumType::Tom,
            DrumType::TomHi, DrumType::Cowbell, DrumType::Rimshot, DrumType::Crash
        };
        for (int i = 0; i < 10; ++i)
        {
            drumTracks[static_cast<size_t>(i)] = DrumTrackState();
            auto& t = drumTracks[static_cast<size_t>(i)];
            t.setType (types[i]);
            t.length = 16; t.clockMul = 1.0f;
            t.volume = 0.6f; t.seq.reset();
        }
    }

    // Reset all synth tracks — fresh objects
    for (int i = 0; i < 5; ++i)
    {
        synthTracks[static_cast<size_t>(i)] = SynthTrackState();
        synthTracks[static_cast<size_t>(i)].partIndex = i;
        synthTracks[static_cast<size_t>(i)].model = SynthModel::Analog;
        synthTracks[static_cast<size_t>(i)].seq.reset();
    }

    // Reset master EQ (Pultec)
    pultecLowBoost.store (2.0f); pultecLowAtten.store (0.5f); pultecLowFreq.store (60.0f);
    pultecHighBoost.store (1.5f); pultecHighAtten.store (0.0f); pultecHighFreq.store (10000.0f);
    pultecHighBW.store (1.5f); pultecHiAttnFrq.store (12000.0f);

    // Reset compressor
    compThreshold.store (-12.0f); compRatio.store (4.0f); compAttack.store (10.0f);
    compRelease.store (100.0f); compMakeup.store (3.0f); compKnee.store (6.0f);
    compScHP.store (0.0f); compStyle.store (0);

    // Reset limiter
    limCeiling.store (-0.3f); limRelease.store (50.0f);
    limInputGain.store (0.0f); limAutoRel.store (0.0f);

    // Reset master delay
    mDelayMix.store (0.0f); mDelayTime.store (0.375f); mDelayFB.store (0.4f);
    mDelaySync.store (1.0f); mDelayBeats.store (1.0f);
    mDelayHP.store (200.0f); mDelayLP.store (8000.0f);
    mDelayPP.store (0.0f); mDelayAlgo.store (0.0f); mDelayDamp.store (0.3f);

    // Reset gater
    gaterMix.store (0.0f); gaterRate.store (3.0f);
    gaterDepth.store (1.0f); gaterShape.store (0.0f); gaterSmooth.store (0.05f);

    // Reset DJ filter
    djFilterFreq.store (0.5f); djFilterRes.store (0.3f);

    // Reset master volume + BPM
    masterVolume.store (0.8f);
    globalSwing.store (0);

    // Reset macros
    for (auto& m : macroEngine.macros) { m.value = 0.0f; m.assignments.clear(); }
}
