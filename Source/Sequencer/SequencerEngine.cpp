#include "SequencerEngine.h"
#include "ScaleUtils.h"
#include <cmath>
#include <algorithm>
#include <numeric>

SequencerEngine::SequencerEngine() {}

// ═══════════════════════════════════════
// CHORD BUILDER — inversions + voicing
// ═══════════════════════════════════════
std::vector<NoteInfo> SequencerEngine::buildChord (NoteInfo root, int chordType, int inversion, int voicing,
                                                     int scaleType, int scaleRoot)
{
    std::vector<NoteInfo> result;
    if (chordType < 1 || chordType > kNumChordTypes) return result;

    // Collect intervals (semitones from root, excluding root itself)
    std::vector<int> intervals;
    for (int i = 0; i < kMaxChordNotes; ++i)
    {
        int iv = chordTable[chordType][i];
        if (iv < 0) break;
        intervals.push_back (iv);
    }
    if (intervals.empty()) return result;

    // ── Apply inversion: rotate lowest notes up by 12 ──
    // 1st inv: move 1st interval up an octave (becomes highest)
    // 2nd inv: move 1st and 2nd intervals up
    // 3rd inv: move 1st, 2nd, and 3rd up (only for 4+ note chords)
    int invCount = std::clamp (inversion, 0, std::min (3, static_cast<int>(intervals.size()) - 1));
    for (int inv = 0; inv < invCount; ++inv)
    {
        if (!intervals.empty())
        {
            intervals.front() += 12;
            std::sort (intervals.begin(), intervals.end());
        }
    }

    // ── Apply voicing ──
    // 0 = close (default — all notes in tightest range)
    // 1 = drop-2 (2nd highest note dropped 1 octave)
    // 2 = spread (every other note raised 1 octave)
    // 3 = open (2nd note dropped 1 octave = wider spacing)
    if (voicing == 1 && intervals.size() >= 3) // Drop-2
    {
        // 2nd highest = intervals[size-2], drop it by 12
        intervals[intervals.size() - 2] -= 12;
        std::sort (intervals.begin(), intervals.end());
    }
    else if (voicing == 2) // Spread
    {
        for (size_t i = 1; i < intervals.size(); i += 2)
            intervals[i] += 12;
    }
    else if (voicing == 3 && intervals.size() >= 2) // Open
    {
        // Drop 1st interval down an octave
        intervals[0] -= 12;
        std::sort (intervals.begin(), intervals.end());
    }

    // Convert intervals to NoteInfo (relative to root)
    for (int iv : intervals)
    {
        int newNote = root.noteIndex + iv;
        int newOct = root.octave;
        while (newNote >= 12) { newNote -= 12; newOct++; }
        while (newNote < 0) { newNote += 12; newOct--; }
        newOct = std::clamp (newOct, 0, 8);
        // Scale quantize
        if (scaleType > 0)
            newNote = ScaleUtils::quantizeNote (newNote, scaleRoot, scaleType);
        result.push_back ({ static_cast<uint8_t>(newNote), static_cast<uint8_t>(newOct) });
    }
    return result;
}

void SequencerEngine::prepare (double sr)
{
    sampleRate = sr;
    reset();
}

void SequencerEngine::reset()
{
    globalSamplePos = 0.0;
    for (auto& c : drumClocks)  { c.nextSamplePos = 0; c.beatCounter = 0; c.currentStep = -1; c.logicalStep = 0; c.pingDir = 1; c.oneShotDone = false; }
    for (auto& c : synthClocks) { c.nextSamplePos = 0; c.beatCounter = 0; c.currentStep = -1; c.logicalStep = 0; c.pingDir = 1; c.oneShotDone = false; }
    masterFXClock = {};
    trigCycleCounters.clear();
    pendingDrumEvents.clear();
    pendingSynthEvents.clear();
    for (auto& as : arpStates)
    {
        as.pool.clear();
        as.idx = 0;
        as.nextTime = 0;
        as.openUntil = 0;
        as.swingCount = 0;
    }
}

void SequencerEngine::setPlaying (bool shouldPlay)
{
    if (shouldPlay && !playing)
    {
        reset();
        playing = true;
    }
    else if (!shouldPlay)
    {
        playing = false;
    }
}

void SequencerEngine::stop()
{
    playing = false;
    for (auto& c : drumClocks)  { c.currentStep = -1; c.beatCounter = 0; c.nextSamplePos = 0; c.logicalStep = 0; c.pingDir = 1; c.oneShotDone = false; }
    for (auto& c : synthClocks) { c.currentStep = -1; c.beatCounter = 0; c.nextSamplePos = 0; c.logicalStep = 0; c.pingDir = 1; c.oneShotDone = false; }
    masterFXClock = {};
    pendingDrumEvents.clear();
    pendingSynthEvents.clear();
    globalSamplePos = 0.0;
    for (auto& as : arpStates) { as.pool.clear(); as.idx = 0; as.openUntil = 0; as.nextTime = 0; }
}

void SequencerEngine::setBPM (float newBPM)
{
    bpm = std::clamp (newBPM, 40.0f, 300.0f);
}

double SequencerEngine::stepDurationSamples (float clockMul) const
{
    // One step = 1/16 note = (60 / bpm / 4) seconds
    double stepSec = 60.0 / bpm / 4.0 / static_cast<double>(clockMul);
    return stepSec * sampleRate;
}

// ═══════════════════════════════════════
// TRIG CONDITIONS — matches HTML shouldTrigger() exactly
// ═══════════════════════════════════════
int SequencerEngine::makeTrigKey (int trackType, int trackIdx, int stepIdx) const
{
    // Pack into single int: trackType (0=drum, 1=synth) | trackIdx | stepIdx
    return (trackType << 16) | (trackIdx << 8) | stepIdx;
}

bool SequencerEngine::shouldTrigger (const StepData& step, int trackKey, int stepIdx)
{
    if (!step.active)
        return false;

    switch (step.cond)
    {
        case TrigCondition::Always: return true;
        case TrigCondition::Fill:   return fillMode;
        case TrigCondition::NotFill: return !fillMode;
        case TrigCondition::P50:    return (rng() % 100) < 50;
        case TrigCondition::P25:    return (rng() % 100) < 25;
        case TrigCondition::P75:    return (rng() % 100) < 75;
        case TrigCondition::P12:    return (rng() % 100) < 12;

        // n-of-m: per-step cycle counting (matches HTML trackLoopCount)
        case TrigCondition::OneOf2:
        case TrigCondition::TwoOf2:
        case TrigCondition::OneOf3:
        case TrigCondition::TwoOf3:
        case TrigCondition::ThreeOf3:
        case TrigCondition::OneOf4:
        case TrigCondition::TwoOf4:
        case TrigCondition::ThreeOf4:
        case TrigCondition::FourOf4:
        {
            int key = trackKey | (stepIdx & 0xFF);
            auto& counter = trigCycleCounters[key];

            int n = 1, m = 2;
            switch (step.cond)
            {
                case TrigCondition::OneOf2:    n = 1; m = 2; break;
                case TrigCondition::TwoOf2:    n = 2; m = 2; break;
                case TrigCondition::OneOf3:    n = 1; m = 3; break;
                case TrigCondition::TwoOf3:    n = 2; m = 3; break;
                case TrigCondition::ThreeOf3:  n = 3; m = 3; break;
                case TrigCondition::OneOf4:    n = 1; m = 4; break;
                case TrigCondition::TwoOf4:    n = 2; m = 4; break;
                case TrigCondition::ThreeOf4:  n = 3; m = 4; break;
                case TrigCondition::FourOf4:   n = 4; m = 4; break;
                default: break;
            }

            counter++;
            if (counter > m) counter = 1;
            return counter == n;
        }

        default: return true;
    }
}

// ═══════════════════════════════════════
// EMIT PENDING EVENTS (ratchets/swung hits from previous buffer)
// ═══════════════════════════════════════
void SequencerEngine::emitPendingEvents (std::vector<PendingEvent>& pending,
                                          int numSamples, std::vector<SeqEvent>& events)
{
    double bufStart = globalSamplePos;
    double bufEnd   = globalSamplePos + numSamples;

    auto it = pending.begin();
    while (it != pending.end())
    {
        if (it->absoluteSamplePos < bufEnd)
        {
            int offset = static_cast<int>(it->absoluteSamplePos - bufStart);
            offset = std::clamp (offset, 0, numSamples - 1);
            it->event.sampleOffset = offset;
            events.push_back (it->event);
            it = pending.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

// ═══════════════════════════════════════
// MAIN PROCESS BLOCK
// ═══════════════════════════════════════
void SequencerEngine::processBlock (int numSamples,
                                     std::vector<SeqEvent>& events,
                                     const std::array<DrumTrackState, 10>& drums,
                                     const std::array<SynthTrackState, 5>& synths)
{
    if (!playing)
        return;

    events.clear();

    // First emit any pending events from previous buffers
    emitPendingEvents (pendingDrumEvents, numSamples, events);
    emitPendingEvents (pendingSynthEvents, numSamples, events);

    // Process each track
    for (int i = 0; i < 10; ++i)
        processDrumTrack (i, drums[static_cast<size_t>(i)], numSamples, events);

    for (int i = 0; i < 5; ++i)
    {
        processSynthTrack (i, synths[static_cast<size_t>(i)], numSamples, events);
        processArpeggiator (i, synths[static_cast<size_t>(i)], numSamples, events);
    }

    globalSamplePos += numSamples;

    // Advance master FX sequencer clock (1/16th note grid, no swing)
    {
        double sd = stepDurationSamples (1.0f);
        double bufEnd = globalSamplePos;
        while (masterFXClock.nextSamplePos < bufEnd)
        {
            int step = masterFXClock.beatCounter % std::max (1, masterFXLength);
            masterFXClock.currentStep = step;
            masterFXClock.beatCounter++;
            masterFXClock.nextSamplePos += sd;
        }
    }

    // Sort events by sample offset
    std::sort (events.begin(), events.end(),
               [](const SeqEvent& a, const SeqEvent& b) { return a.sampleOffset < b.sampleOffset; });
}

// ═══════════════════════════════════════
// DRUM TRACK — fixed timing, swing, ratchet
// ═══════════════════════════════════════
void SequencerEngine::processDrumTrack (int trackIdx, const DrumTrackState& track,
                                         int numSamples, std::vector<SeqEvent>& events)
{
    auto& clock = drumClocks[static_cast<size_t>(trackIdx)];
    double sd = stepDurationSamples (track.clockMul);
    double bufEnd = globalSamplePos + numSamples;

    while (clock.nextSamplePos < bufEnd)
    {
        // Initialize direction on first beat
        if (clock.beatCounter == 0)
            initLogicalStep (clock, track.playDir, track.length);

        int step = getAndAdvanceStep (clock, track.playDir, track.length);
        if (step < 0) { clock.nextSamplePos += sd; clock.beatCounter++; break; }  // ONE mode done

        const StepData stepData = track.seq.steps[static_cast<size_t>(step)]; // SNAPSHOT copy

        // ── SWING: offset odd beats (bidirectional: negative = early, positive = late) ──
        double swingAmt = std::clamp ((track.swing + globalSwing) / 100.0, -1.0, 1.0);
        double swingOffset = (clock.beatCounter % 2 == 1) ? sd * swingAmt * 0.67 : 0.0;

        // ── NUDGE: per-step timing offset ──
        double nudgeOffset = sd * (stepData.nudge / 100.0);

        // The ACTUAL absolute time this step should fire
        double stepAbsTime = clock.nextSamplePos + swingOffset + nudgeOffset;

        // Update UI step indicator
        clock.currentStep = step;

        int trigKey = makeTrigKey (0, trackIdx, step);

        if (shouldTrigger (stepData, trigKey, step))
        {
            int ratchetCount = std::max (1, static_cast<int>(stepData.ratchet));
            double activeDur = stepData.triplet ? sd * (2.0 / 3.0) : sd;
            double sliceDur  = activeDur / ratchetCount;

            for (int ri = 0; ri < ratchetCount; ++ri)
            {
                // Ratchet velocity: first hit = full, subsequent hits decay
                float vel = (ri == 0)
                    ? (stepData.velocity / 127.0f) * track.volume
                    : std::max (0.15f, ((stepData.velocity - ri * 18.0f) / 127.0f)) * track.volume;

                float gateScale = (stepData.gate / 100.0f) * stepData.noteLen;

                double eventAbsTime = stepAbsTime + ri * sliceDur;

                SeqEvent ev;
                ev.type = SeqEvent::DrumTrigger;
                ev.trackIndex = trackIdx;
                ev.stepIndex = step;
                ev.velocity = std::clamp (vel, 0.0f, 1.0f);
                ev.gateScale = gateScale;
                ev.note = {};
                ev.ratchetIndex = ri;
                ev.slide = false;
                ev.trigless = stepData.trigless;
                ev.sampleSlot = stepData.sampleSlot;
                ev.capturePlocks (stepData.plocks.empty() ? nullptr : &stepData.plocks);

                // Is this event within the current buffer?
                if (eventAbsTime >= globalSamplePos && eventAbsTime < bufEnd)
                {
                    int offset = static_cast<int>(eventAbsTime - globalSamplePos);
                    ev.sampleOffset = std::clamp (offset, 0, numSamples - 1);
                    events.push_back (ev);
                }
                else if (eventAbsTime >= bufEnd)
                {
                    // Deferred to next buffer
                    pendingDrumEvents.push_back ({ ev, eventAbsTime });
                }
                // If eventAbsTime < globalSamplePos, it's in the past — fire at 0
                else
                {
                    ev.sampleOffset = 0;
                    events.push_back (ev);
                }
            }
        }

        clock.beatCounter++;
        clock.nextSamplePos += sd;
    }
}

// ═══════════════════════════════════════
// SYNTH TRACK — fixed timing, swing, ratchet, slide, chords
// ═══════════════════════════════════════
void SequencerEngine::processSynthTrack (int trackIdx, const SynthTrackState& track,
                                          int numSamples, std::vector<SeqEvent>& events)
{
    auto& clock = synthClocks[static_cast<size_t>(trackIdx)];
    double sd = stepDurationSamples (track.clockMul);
    double bufEnd = globalSamplePos + numSamples;

    while (clock.nextSamplePos < bufEnd)
    {
        // Initialize direction on first beat
        if (clock.beatCounter == 0)
            initLogicalStep (clock, track.playDir, track.length);

        int step = getAndAdvanceStep (clock, track.playDir, track.length);
        if (step < 0) { clock.nextSamplePos += sd; clock.beatCounter++; break; }  // ONE mode done

        const StepData stepData = track.seq.steps[static_cast<size_t>(step)]; // SNAPSHOT copy

        double swingAmt = std::clamp ((track.swing + globalSwing) / 100.0, -1.0, 1.0);
        double swingOffset = (clock.beatCounter % 2 == 1) ? sd * swingAmt * 0.67 : 0.0;
        double nudgeOffset = sd * (stepData.nudge / 100.0);
        double stepAbsTime = clock.nextSamplePos + swingOffset + nudgeOffset;

        clock.currentStep = step;

        int trigKey = makeTrigKey (1, trackIdx, step);

        if (shouldTrigger (stepData, trigKey, step))
        {
            // If arp is ON, we don't emit synth events here — the arpeggiator handles it
            if (track.arp.enabled)
            {
                // Open the arp gate for this step
                auto& as = arpStates[static_cast<size_t>(trackIdx)];
                double activeDur = stepData.triplet ? sd * (2.0 / 3.0) : sd;
                double gateDur = activeDur * stepData.noteLen * (stepData.gate / 100.0f);

                as.openUntil = stepAbsTime + gateDur;
                as.vel = (stepData.velocity / 127.0f) * track.volume;
                as.capturePlocks (stepData.plocks.empty() ? nullptr : &stepData.plocks);

                // Build note pool from this step
                buildArpPool (trackIdx, track);
                as.idx = 0;
                if (as.nextTime < stepAbsTime)
                {
                    as.nextTime = stepAbsTime;
                    as.swingCount = 0;
                }
            }
            else
            {
                // Normal (non-arp) synth event
                int ratchetCount = std::max (1, static_cast<int>(stepData.ratchet));
                double activeDur = stepData.triplet ? sd * (2.0 / 3.0) : sd;
                double sliceDur  = activeDur / ratchetCount;

                // 303-style: if NEXT step has slide, extend THIS step's gate to overlap
                // After getAndAdvanceStep, logicalStep already points to the upcoming step
                int nextStep = std::clamp (clock.logicalStep, 0, std::max (1, track.length) - 1);
                const StepData& nextStepData = track.seq.steps[static_cast<size_t>(nextStep)];
                bool nextHasSlide = nextStepData.active && nextStepData.slide;

                for (int ri = 0; ri < ratchetCount; ++ri)
                {
                    float vel = (ri == 0)
                        ? (stepData.velocity / 127.0f) * track.volume
                        : std::max (0.15f, ((stepData.velocity - ri * 18.0f) / 127.0f)) * track.volume;

                    float gateScale = (stepData.gate / 100.0f) * stepData.noteLen;
                    // Extend gate if next step slides into this one
                    if (nextHasSlide && ri == ratchetCount - 1)
                        gateScale = std::max (gateScale, 1.2f); // 120% = overlap into next step

                    double eventAbsTime = stepAbsTime + ri * sliceDur;

                    SeqEvent ev;
                    ev.type = SeqEvent::SynthNoteOn;
                    ev.trackIndex = trackIdx;
                    ev.stepIndex = step;
                    ev.velocity = std::clamp (vel, 0.0f, 1.0f);
                    ev.gateScale = gateScale;
                    ev.note = stepData.getNote();
                    // Quantize root note to scale
                    if (scaleType > 0)
                        ev.note.noteIndex = static_cast<uint8_t>(ScaleUtils::quantizeNote (
                            ev.note.noteIndex, scaleRoot, scaleType));
                    ev.ratchetIndex = ri;
                    ev.slide = stepData.slide;
                    ev.trigless = stepData.trigless;
                    ev.sampleSlot = stepData.sampleSlot;
                    ev.capturePlocks (stepData.plocks.empty() ? nullptr : &stepData.plocks);

                    // Auto-chord: per-step chordMode (-1=track default, 0=OFF, 1-24=type)
                    int effectiveChord = (stepData.chordMode >= 0) ? stepData.chordMode : track.chordMode;
                    int effectiveInv = (stepData.chordInversion >= 0) ? stepData.chordInversion : track.chordInversion;
                    int effectiveVce = (stepData.chordVoicing >= 0) ? stepData.chordVoicing : track.chordVoicing;
                    if (effectiveChord > 0 && stepData.chordNotes.empty())
                    {
                        auto chordNotes = buildChord (ev.note, effectiveChord,
                            effectiveInv, effectiveVce, scaleType, scaleRoot);
                        for (const auto& cn : chordNotes)
                            ev.chordNotes.push_back (cn);
                    }

                    // If step has manual chord notes, include them (quantized)
                    if (!stepData.chordNotes.empty())
                    {
                        ev.chordNotes = stepData.chordNotes;
                        if (scaleType > 0)
                        {
                            for (auto& cn : ev.chordNotes)
                                cn.noteIndex = static_cast<uint8_t>(ScaleUtils::quantizeNote (
                                    cn.noteIndex, scaleRoot, scaleType));
                        }
                    }

                    if (eventAbsTime >= globalSamplePos && eventAbsTime < bufEnd)
                    {
                        int offset = static_cast<int>(eventAbsTime - globalSamplePos);
                        ev.sampleOffset = std::clamp (offset, 0, numSamples - 1);

                        // ── STRUM: spread chord notes over time ──
                        if (stepData.strum > 0 && !ev.chordNotes.empty())
                        {
                            float strumPct = static_cast<float>(stepData.strum) / 100.0f;
                            double stepDurSmp = sd; // samples per step
                            double strumSpread = stepDurSmp * strumPct; // max = full step at 100%
                            int numNotes = static_cast<int>(ev.chordNotes.size());
                            auto strumNotes = ev.chordNotes;
                            ev.chordNotes.clear(); // root fires alone

                            for (int si = 0; si < numNotes; ++si)
                            {
                                double noteDelay = strumSpread * static_cast<double>(si + 1) / static_cast<double>(numNotes);
                                double strumAbsTime = eventAbsTime + noteDelay;

                                SeqEvent sev;
                                sev.type = SeqEvent::SynthNoteOn;
                                sev.trackIndex = ev.trackIndex;
                                sev.stepIndex = ev.stepIndex;
                                sev.velocity = ev.velocity;
                                sev.gateScale = ev.gateScale;
                                sev.note = strumNotes[static_cast<size_t>(si)];
                                sev.slide = ev.slide;
                                sev.arpStepIndex = ev.arpStepIndex;
                                sev.capturePlocks (stepData.plocks.empty() ? nullptr : &stepData.plocks);

                                if (strumAbsTime >= globalSamplePos && strumAbsTime < bufEnd)
                                {
                                    sev.sampleOffset = std::clamp (static_cast<int>(strumAbsTime - globalSamplePos), 0, numSamples - 1);
                                    events.push_back (sev);
                                }
                                else if (strumAbsTime >= bufEnd)
                                {
                                    pendingSynthEvents.push_back ({ sev, strumAbsTime });
                                }
                            }
                        }

                        events.push_back (ev);
                    }
                    else if (eventAbsTime >= bufEnd)
                    {
                        pendingSynthEvents.push_back ({ ev, eventAbsTime });
                    }
                    else
                    {
                        ev.sampleOffset = 0;
                        events.push_back (ev);
                    }
                }
            }
        }

        clock.beatCounter++;
        clock.nextSamplePos += sd;
    }
}

// ═══════════════════════════════════════
// ARPEGGIATOR ENGINE (matches HTML exactly)
// ═══════════════════════════════════════
void SequencerEngine::buildArpPool (int trackIdx, const SynthTrackState& track)
{
    auto& as = arpStates[static_cast<size_t>(trackIdx)];
    std::vector<NoteInfo> rawPool;

    // Build from current step's notes/chords
    int step = synthClocks[static_cast<size_t>(trackIdx)].currentStep;
    if (step < 0) step = 0;
    const StepData sd = track.seq.steps[static_cast<size_t>(step)]; // SNAPSHOT copy

    // Always include root note (quantized)
    NoteInfo rootNote = sd.getNote();
    if (scaleType > 0)
        rootNote.noteIndex = static_cast<uint8_t>(ScaleUtils::quantizeNote (
            rootNote.noteIndex, scaleRoot, scaleType));
    rawPool.push_back (rootNote);

    // Manual per-step chords (quantized)
    if (!sd.chordNotes.empty())
    {
        for (auto cn : sd.chordNotes)
        {
            if (scaleType > 0)
                cn.noteIndex = static_cast<uint8_t>(ScaleUtils::quantizeNote (
                    cn.noteIndex, scaleRoot, scaleType));
            rawPool.push_back (cn);
        }
    }

    // Auto-chord from chordMode (per-step with track fallback)
    int arpChord = (sd.chordMode >= 0) ? sd.chordMode : track.chordMode;
    int arpInv = (sd.chordInversion >= 0) ? sd.chordInversion : track.chordInversion;
    int arpVce = (sd.chordVoicing >= 0) ? sd.chordVoicing : track.chordVoicing;
    if (arpChord > 0 && sd.chordNotes.empty())
    {
        auto chordNotes = buildChord (rootNote, arpChord,
            arpInv, arpVce, scaleType, scaleRoot);
        for (const auto& cn : chordNotes)
            rawPool.push_back (cn);
    }

    as.pool = sortArpPool (rawPool, track.arp.direction, std::max (1, track.arp.octaves));
}

std::vector<NoteInfo> SequencerEngine::sortArpPool (const std::vector<NoteInfo>& pool, int mode, int octaves)
{
    if (pool.empty())
        return { { 0, 3 } }; // fallback C3

    // Deduplicate
    std::vector<NoteInfo> unique;
    for (const auto& n : pool)
    {
        bool found = false;
        for (const auto& u : unique)
            if (u.noteIndex == n.noteIndex && u.octave == n.octave) { found = true; break; }
        if (!found) unique.push_back (n);
    }

    // Sort by pitch
    std::sort (unique.begin(), unique.end(), [](const NoteInfo& a, const NoteInfo& b) {
        return (a.octave * 12 + a.noteIndex) < (b.octave * 12 + b.noteIndex);
    });

    // Expand octaves
    std::vector<NoteInfo> expanded;
    for (int o = 0; o < octaves; ++o)
        for (const auto& n : unique)
            expanded.push_back ({ n.noteIndex, static_cast<uint8_t>(n.octave + o) });

    // Apply mode
    switch (mode)
    {
        case 0: // up
            return expanded;
        case 1: // down
            std::reverse (expanded.begin(), expanded.end());
            return expanded;
        case 2: // updown
        {
            auto down = expanded;
            if (down.size() > 2)
                down = std::vector<NoteInfo>(expanded.rbegin() + 1, expanded.rend() - 1);
            else
                down.clear();
            auto result = expanded;
            result.insert (result.end(), down.begin(), down.end());
            return result;
        }
        case 3: // downup
        {
            auto rev = expanded;
            std::reverse (rev.begin(), rev.end());
            auto up = rev;
            if (up.size() > 2)
                up = std::vector<NoteInfo>(rev.rbegin() + 1, rev.rend() - 1);
            else
                up.clear();
            auto result = rev;
            result.insert (result.end(), up.begin(), up.end());
            return result;
        }
        case 4: // random
            std::shuffle (expanded.begin(), expanded.end(), rng);
            return expanded;
        case 5: // CHORD — return base notes only (no octave expansion), all fire simultaneously
        {
            return unique; // just the unique notes, arp tick fires all at once
        }
        case 6: // CONVERGE — outside-in: low, high, low+1, high-1, ...
        {
            std::vector<NoteInfo> conv;
            int lo = 0, hi = static_cast<int>(expanded.size()) - 1;
            while (lo <= hi)
            {
                conv.push_back (expanded[static_cast<size_t>(lo++)]);
                if (lo <= hi) conv.push_back (expanded[static_cast<size_t>(hi--)]);
            }
            return conv;
        }
        case 7: // DIVERGE — inside-out: mid, mid+1, mid-1, mid+2, mid-2, ...
        {
            std::vector<NoteInfo> div;
            int mid = static_cast<int>(expanded.size()) / 2;
            div.push_back (expanded[static_cast<size_t>(mid)]);
            for (int d = 1; d <= mid; ++d)
            {
                if (mid + d < static_cast<int>(expanded.size()))
                    div.push_back (expanded[static_cast<size_t>(mid + d)]);
                if (mid - d >= 0)
                    div.push_back (expanded[static_cast<size_t>(mid - d)]);
            }
            return div;
        }
        default:
            return expanded;
    }
}

void SequencerEngine::processArpeggiator (int trackIdx, const SynthTrackState& track,
                                           int numSamples, std::vector<SeqEvent>& events)
{
    if (!track.arp.enabled || track.muted)
    {
        // Clear arp state when disabled to prevent stuck notes
        auto& as = arpStates[static_cast<size_t>(trackIdx)];
        as.pool.clear();
        as.openUntil = 0;
        return;
    }

    auto& as = arpStates[static_cast<size_t>(trackIdx)];
    if (as.pool.empty()) return;

    double bufEnd = globalSamplePos + numSamples;

    // Don't play if gate is closed — also clear pool to prevent lingering
    if (as.openUntil <= globalSamplePos)
    {
        as.pool.clear();
        return;
    }

    double beatDur = 60.0 / bpm * sampleRate; // one beat in samples
    int rateIdx = std::clamp (track.arp.division, 0, 7);
    double rateDur = beatDur * kArpRateBeats[rateIdx];

    if (as.nextTime < globalSamplePos - rateDur)
    {
        as.nextTime = globalSamplePos;
        as.swingCount = 0;
    }

    double arpGateEnd = std::min (bufEnd, as.openUntil);

    while (as.nextTime < arpGateEnd)
    {
        // Arp swing (use track swing)
        double arpSwing = track.swing / 100.0;
        double arpSwingOffset = (as.swingCount % 2 == 1) ? rateDur * arpSwing * 0.5 : 0.0;

        double eventTime = as.nextTime + arpSwingOffset;

        if (eventTime >= globalSamplePos && eventTime < bufEnd)
        {
            const auto& noteObj = as.pool[static_cast<size_t>(as.idx % as.pool.size())];

            // Read gate from arp step data (not hardcoded!)
            int arpStep = as.idx % track.arp.getEffectiveLen();
            float gatePct = std::max (5.0f, static_cast<float>(track.arp.steps[static_cast<size_t>(arpStep)].gate)) / 100.0f;
            float gateDurSec = static_cast<float>(rateDur / sampleRate) * std::min (gatePct, 0.95f);

            SeqEvent ev;
            ev.type = SeqEvent::SynthNoteOn;
            ev.trackIndex = trackIdx;
            ev.stepIndex = synthClocks[static_cast<size_t>(trackIdx)].currentStep;
            // Read velocity from arp step data
            ev.velocity = std::clamp (static_cast<float>(track.arp.steps[static_cast<size_t>(arpStep)].velocity) / 127.0f * as.vel, 0.0f, 1.0f);
            ev.gateScale = std::max (0.02f, gateDurSec / (60.0f / bpm / 4.0f)); // relative to step dur
            ev.note = noteObj;
            ev.ratchetIndex = 0;
            ev.arpStepIndex = arpStep;  // pass arp step to PluginProcessor
            // Arp legato: when gate >= 75% and mono is on, tie notes
            ev.slide = (track.mono && gatePct >= 0.75f);

            // CHORD mode (direction 5): fire ALL pool notes as chord
            if (track.arp.direction == 5 && as.pool.size() > 1)
            {
                if (track.mono)
                {
                    // Mono + chord = single root note (no chord voices)
                }
                else
                {
                    for (size_t ci = 0; ci < as.pool.size(); ++ci)
                    {
                        if (as.pool[ci].noteIndex != noteObj.noteIndex ||
                            as.pool[ci].octave != noteObj.octave)
                            ev.chordNotes.push_back (as.pool[ci]);
                    }
                }
            }

            ev.capturePlocks (as.plocks);

            int offset = static_cast<int>(eventTime - globalSamplePos);
            ev.sampleOffset = std::clamp (offset, 0, numSamples - 1);
            events.push_back (ev);

            as.idx++;
        }

        as.nextTime += rateDur;
        as.swingCount++;
    }
}

// ═══════════════════════════════════════
// UI QUERIES
// ═══════════════════════════════════════
int SequencerEngine::getDrumPlayingStep (int trackIdx) const
{
    if (trackIdx >= 0 && trackIdx < 10)
        return drumClocks[static_cast<size_t>(trackIdx)].currentStep;
    return -1;
}

int SequencerEngine::getSynthPlayingStep (int trackIdx) const
{
    if (trackIdx >= 0 && trackIdx < 5)
        return synthClocks[static_cast<size_t>(trackIdx)].currentStep;
    return -1;
}
