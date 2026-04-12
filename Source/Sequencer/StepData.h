#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <map>
#include <vector>

// Per-step trigger condition — matches HTML shouldTrigger exactly
enum class TrigCondition : uint8_t
{
    Always, P50, P25, P75, P12,
    OneOf2, TwoOf2,
    OneOf3, TwoOf3, ThreeOf3,
    OneOf4, TwoOf4, ThreeOf4, FourOf4,
    Fill, NotFill
};

// Note data for synth steps
struct NoteInfo
{
    uint8_t noteIndex = 0;  // 0-11 (C=0, C#=1 ... B=11)
    uint8_t octave    = 3;  // 0-8 (3 = C3)
};

// Per-step data — matches HTML makeSD() 1:1
struct StepData
{
    bool          active    = false;
    uint8_t       velocity  = 100;      // 1-127
    uint8_t       gate      = 100;      // 1-200 (percentage)
    uint8_t       ratchet   = 1;        // 1-4
    bool          triplet   = false;
    uint8_t       noteLen   = 1;        // note length multiplier 1-128
    bool          slide     = false;    // 303-style slide
    bool          trigless  = false;    // automation-only: apply p-locks without note trigger
    int8_t        nudge     = 0;        // -50 to +50 (% of step)
    int8_t        chordMode = -1;       // -1=track default, 0=OFF, 1-24=chord type
    int8_t        chordInversion = -1;  // -1=track default, 0=root, 1=1st, 2=2nd, 3=3rd
    int8_t        chordVoicing = -1;    // -1=track default, 0=close, 1=drop2, 2=spread, 3=open
    uint8_t       strum     = 0;        // 0-200: strum spread % (0=simultaneous)
    int8_t        sampleSlot = -1;      // -1=track default, 0-127=sample slot from folder (Octatrack-style)
    TrigCondition cond      = TrigCondition::Always;

    // For synth tracks: root note
    uint8_t       noteIndex = 0;  // 0-11 (C=0 ... B=11)
    uint8_t       octave    = 3;  // 0-8 (3 = C3)

    NoteInfo getNote() const { return { noteIndex, octave }; }

    // P-locks: parameter name → value override
    // (accessed from audio thread only at event dispatch, safe because
    //  UI writes are atomic-ish for floats and we don't resize the map mid-playback)
    std::map<std::string, float> plocks;

    // Chord: if >1 entry, this step plays a chord
    // Index 0 = root, rest = chord tones
    // Empty = single note from 'note' field
    std::vector<NoteInfo> chordNotes;

    void reset()
    {
        active = false;
        velocity = 100;
        gate = 100;
        ratchet = 1;
        triplet = false;
        noteLen = 1;
        slide = false;
        trigless = false;
        nudge = 0;
        chordMode = -1;
        chordInversion = -1;
        chordVoicing = -1;
        strum = 0;
        sampleSlot = -1;
        cond = TrigCondition::Always;
        noteIndex = 0;
        octave = 3;
        plocks.clear();
        chordNotes.clear();
    }
};

// 8 pages of 16 steps = 128 max steps
static constexpr int kStepsPerPage = 16;
static constexpr int kNumPages     = 8;
static constexpr int kMaxSteps     = kNumPages * kStepsPerPage; // 128

// Complete step sequence for one track
struct StepSequence
{
    std::array<StepData, kMaxSteps> steps;

    void reset()
    {
        for (auto& s : steps)
            s.reset();
    }
};
