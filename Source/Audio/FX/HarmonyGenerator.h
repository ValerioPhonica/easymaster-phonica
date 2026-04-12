#pragma once
#include "../../Sequencer/TrackState.h"
#include "../../Sequencer/StepData.h"
#include "../../Sequencer/SequencerEngine.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// HarmonyGenerator — Programs chord progressions into the sequencer
//
// Select key, scale, pattern → GENERATE writes notes + chords
// directly into SynthTrackState steps. Includes classical, jazz,
// EDM, and cinematic patterns with proper voice leading.
// ═══════════════════════════════════════════════════════════════════

namespace HarmonyGen
{

// ── Scale intervals (semitones from root) ──
static constexpr int kScaleIntervals[][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // 0 Major (Ionian)
    { 0, 2, 3, 5, 7, 8, 10 },   // 1 Natural Minor (Aeolian)
    { 0, 2, 3, 5, 7, 8, 11 },   // 2 Harmonic Minor
    { 0, 2, 3, 5, 7, 9, 11 },   // 3 Melodic Minor
    { 0, 2, 4, 7, 9, -1, -1 },  // 4 Pentatonic Major
    { 0, 3, 5, 7, 10, -1, -1 }, // 5 Pentatonic Minor
    { 0, 2, 4, 6, 7, 9, 10 },   // 6 Mixolydian
    { 0, 2, 3, 5, 7, 9, 10 },   // 7 Dorian
    { 0, 1, 3, 5, 7, 8, 10 },   // 8 Phrygian
    { 0, 2, 4, 6, 7, 9, 11 },   // 9 Lydian
};
static constexpr int kNumScales = 10;
static const char* scaleName (int idx)
{
    static const char* n[] = {"MAJ","MIN","H.MIN","M.MIN","PENT","PENTm","MIXO","DOR","PHRY","LYD"};
    return (idx >= 0 && idx < kNumScales) ? n[idx] : "?";
}

// ── Chord quality for each scale degree (auto-detected from scale) ──
// Returns chord type ID (1-24) matching SequencerEngine::chordTable
inline int chordQualityForDegree (int degree, int scaleIdx)
{
    // Get intervals of the triad built on this degree
    const int* sc = kScaleIntervals[scaleIdx];
    int scLen = 7;
    for (int i = 0; i < 7; ++i) if (sc[i] < 0) { scLen = i; break; }
    if (scLen < 5) return 1; // pentatonic: default to MAJ

    int root = sc[degree % scLen];
    int third = sc[(degree + 2) % scLen] + (((degree + 2) >= scLen) ? 12 : 0);
    int fifth = sc[(degree + 4) % scLen] + (((degree + 4) >= scLen) ? 12 : 0);

    int interval3 = (third - root + 12) % 12;
    int interval5 = (fifth - root + 12) % 12;

    // Detect triad quality from intervals
    if (interval3 == 4 && interval5 == 7) return 1;  // MAJ
    if (interval3 == 3 && interval5 == 7) return 2;  // MIN
    if (interval3 == 3 && interval5 == 6) return 6;  // DIM
    if (interval3 == 4 && interval5 == 8) return 7;  // AUG
    return 1; // fallback MAJ
}

// ── Progression patterns ──
// degree: scale degree (0-based) — used for diatonic chords
// semiOverride: if >= 0, use this exact semitone from root instead of scale lookup
//   (for modal interchange: bVII=10, bVI=8, bIII=3, #IV=6)
// steps: duration in sequencer steps (4 = 1 beat at 1/16)
// chordOverride: -1 = auto-detect from scale, >0 = forced chord type
struct ChordStep { int degree; int steps; int chordOverride; int semiOverride; };

struct Progression
{
    const char* name;
    const char* category;
    std::vector<ChordStep> chords;
};

// Returns all built-in progressions
inline std::vector<Progression> getProgressions()
{
    return {
        // ── POP / ROCK ──
        { "I-IV-V-I",         "POP",    {{ {0,4,-1,-1}, {3,4,-1,-1}, {4,4,-1,-1}, {0,4,-1,-1} }} },
        { "I-V-vi-IV",        "POP",    {{ {0,4,-1,-1}, {4,4,-1,-1}, {5,4,-1,-1}, {3,4,-1,-1} }} },
        { "vi-IV-I-V",        "POP",    {{ {5,4,-1,-1}, {3,4,-1,-1}, {0,4,-1,-1}, {4,4,-1,-1} }} },
        { "I-vi-IV-V",        "POP",    {{ {0,4,-1,-1}, {5,4,-1,-1}, {3,4,-1,-1}, {4,4,-1,-1} }} },
        { "I-IV-vi-V",        "POP",    {{ {0,4,-1,-1}, {3,4,-1,-1}, {5,4,-1,-1}, {4,4,-1,-1} }} },
        { "I-iii-IV-V",       "POP",    {{ {0,4,-1,-1}, {2,4,-1,-1}, {3,4,-1,-1}, {4,4,-1,-1} }} },
        { "I-V-IV-V",         "POP",    {{ {0,4,-1,-1}, {4,4,-1,-1}, {3,4,-1,-1}, {4,4,-1,-1} }} },
        { "IV-I-V-vi",        "POP",    {{ {3,4,-1,-1}, {0,4,-1,-1}, {4,4,-1,-1}, {5,4,-1,-1} }} },
        { "I-ii-IV-V",        "POP",    {{ {0,4,-1,-1}, {1,4,-1,-1}, {3,4,-1,-1}, {4,4,-1,-1} }} },
        { "I-V-vi-iii-IV",    "POP",    {{ {0,4,-1,-1}, {4,4,-1,-1}, {5,4,-1,-1}, {2,4,-1,-1}, {3,4,-1,-1} }} },

        // ── JAZZ ──
        { "ii-V-I",           "JAZZ",   {{ {1,4, 5,-1}, {4,4, 3,-1}, {0,4, 4,-1} }} },
        { "ii-V-I-vi",        "JAZZ",   {{ {1,4, 5,-1}, {4,4, 3,-1}, {0,4, 4,-1}, {5,4, 5,-1} }} },
        { "I-vi-ii-V",        "JAZZ",   {{ {0,4, 4,-1}, {5,4, 5,-1}, {1,4, 5,-1}, {4,4, 3,-1} }} },
        { "iii-vi-ii-V",      "JAZZ",   {{ {2,4, 5,-1}, {5,4, 5,-1}, {1,4, 5,-1}, {4,4, 3,-1} }} },
        { "I-IV-iii-vi-ii-V", "JAZZ",   {{ {0,2, 4,-1}, {3,2, 4,-1}, {2,2, 5,-1}, {5,2, 5,-1}, {1,4, 5,-1}, {4,4, 3,-1} }} },
        { "IMaj7-IVMaj7",     "JAZZ",   {{ {0,8, 4,-1}, {3,8, 4,-1} }} },
        { "ii-V-iii-vi",      "JAZZ",   {{ {1,4, 5,-1}, {4,4, 3,-1}, {2,4, 5,-1}, {5,4, 5,-1} }} },
        { "I-bVII-IV",        "JAZZ",   {{ {0,4, 3,-1}, {0,4, 3,10}, {3,4, 4,-1}, {0,4, 4,-1} }} },

        // ── R&B / NEO-SOUL ──
        { "IVMaj7-iii7-vi7",  "R&B",    {{ {3,4, 4,-1}, {2,4, 5,-1}, {5,4, 5,-1}, {0,4, 4,-1} }} },
        { "I-iii-IV-iv",      "R&B",    {{ {0,4, 4,-1}, {2,4, 5,-1}, {3,4, 4,-1}, {3,4, 2,-1} }} },
        { "vi-V-IV-I",        "R&B",    {{ {5,4, 5,-1}, {4,4, 3,-1}, {3,4, 4,-1}, {0,4, 4,-1} }} },
        { "ii7-V7-IMaj7-vi7", "R&B",    {{ {1,4, 5,-1}, {4,4, 3,-1}, {0,4, 4,-1}, {5,4, 5,-1} }} },
        { "I-V/VII-vi-IV",    "R&B",    {{ {0,4, 4,-1}, {4,4, 1,-1}, {5,4, 5,-1}, {3,4, 4,-1} }} },

        // ── EDM ──
        { "i-bVII-bVI-bVII",  "EDM",    {{ {0,4, 2,-1}, {0,4, 1,10}, {0,4, 1, 8}, {0,4, 1,10} }} },
        { "i-bVI-bVII-i",     "EDM",    {{ {0,4, 2,-1}, {0,4, 1, 8}, {0,4, 1,10}, {0,4, 2,-1} }} },
        { "i-iv-v-i",         "EDM",    {{ {0,4, 2,-1}, {3,4, 2,-1}, {4,4, 2,-1}, {0,4, 2,-1} }} },
        { "I-I-IV-IV",        "EDM",    {{ {0,4,-1,-1}, {0,4,-1,-1}, {3,4,-1,-1}, {3,4,-1,-1} }} },
        { "vi-IV-I-V",        "EDM",    {{ {5,4,-1,-1}, {3,4,-1,-1}, {0,4,-1,-1}, {4,4,-1,-1} }} },
        { "i-i-bVI-bVII",     "EDM",    {{ {0,4, 2,-1}, {0,4, 2,-1}, {0,4, 1, 8}, {0,4, 1,10} }} },
        { "i-bIII-bVII-IV",   "EDM",    {{ {0,4, 2,-1}, {0,4, 1, 3}, {0,4, 1,10}, {3,4, 1,-1} }} },
        { "IV-I-IV-V",        "EDM",    {{ {3,4,-1,-1}, {0,4,-1,-1}, {3,4,-1,-1}, {4,4,-1,-1} }} },

        // ── TRAP / HIP-HOP ──
        { "i-iv",             "TRAP",   {{ {0,8, 2,-1}, {3,8, 2,-1} }} },
        { "i-bVI-bVII",       "TRAP",   {{ {0,8, 2,-1}, {0,4, 1, 8}, {0,4, 1,10} }} },
        { "i-bVII-bVI-V",     "TRAP",   {{ {0,4, 2,-1}, {0,4, 1,10}, {0,4, 1, 8}, {4,4, 1,-1} }} },
        { "i-bVI-iv-v",       "TRAP",   {{ {0,4, 2,-1}, {0,4, 1, 8}, {3,4, 2,-1}, {4,4, 2,-1} }} },
        { "iv-bVI-bVII-i",    "TRAP",   {{ {3,4, 2,-1}, {0,4, 1, 8}, {0,4, 1,10}, {0,4, 2,-1} }} },

        // ── CINEMATIC ──
        { "i-bIII-bVII-IV",   "CINE",   {{ {0,4, 2,-1}, {0,4, 1, 3}, {0,4, 1,10}, {3,4, 1,-1} }} },
        { "i-V-bVI-IV",       "CINE",   {{ {0,4, 2,-1}, {4,4, 1,-1}, {0,4, 1, 8}, {3,4, 1,-1} }} },
        { "bVI-bVII-I",       "CINE",   {{ {0,8, 1, 8}, {0,4, 1,10}, {0,4, 1,-1} }} },
        { "i-bVI-bIII-bVII",  "CINE",   {{ {0,4, 2,-1}, {0,4, 1, 8}, {0,4, 1, 3}, {0,4, 1,10} }} },
        { "I-bVI-bIII-IV",    "CINE",   {{ {0,4, 1,-1}, {0,4, 1, 8}, {0,4, 1, 3}, {3,4, 1,-1} }} },
        { "i-iv-bVI-V",       "CINE",   {{ {0,4, 2,-1}, {3,4, 2,-1}, {0,4, 1, 8}, {4,4, 1,-1} }} },
        { "bVII-IV-bVI-I",    "CINE",   {{ {0,4, 1,10}, {3,4, 1,-1}, {0,4, 1, 8}, {0,4, 1,-1} }} },

        // ── LO-FI / AMBIENT ──
        { "IMaj7-IVMaj7 (sus)","LOFI",  {{ {0,8, 4,-1}, {3,8, 4,-1} }} },
        { "vi7-IV-I-iii",     "LOFI",   {{ {5,4, 5,-1}, {3,4,-1,-1}, {0,4,-1,-1}, {2,4,-1,-1} }} },
        { "ii7-IMaj7",        "LOFI",   {{ {1,8, 5,-1}, {0,8, 4,-1} }} },
        { "IVMaj7-V-vi7-I",   "LOFI",   {{ {3,4, 4,-1}, {4,4,-1,-1}, {5,4, 5,-1}, {0,4, 4,-1} }} },
        { "I-bVII-IV (modal)","LOFI",   {{ {0,4, 4,-1}, {0,4, 3,10}, {3,4, 4,-1}, {3,4, 4,-1} }} },

        // ── ADVANCED ──
        { "Backdoor bVII-IV-I","ADV",   {{ {0,4, 3,10}, {3,4,-1,-1}, {0,4, 4,-1} }} },
        { "Chromatic Med I-bVI-IV", "ADV", {{ {0,4, 4,-1}, {0,4, 1, 8}, {3,4,-1,-1}, {0,4, 4,-1} }} },
        { "Modal Int i-IV-i-bVII", "ADV", {{ {0,4, 2,-1}, {3,4, 1,-1}, {0,4, 2,-1}, {0,4, 1,10} }} },
        { "Circle 5ths vi-ii-V-I", "ADV", {{ {5,4, 5,-1}, {1,4, 5,-1}, {4,4, 3,-1}, {0,4, 4,-1} }} },
        { "Tritone Sub bII-V-I","ADV",   {{ {0,4, 3, 1}, {4,4, 3,-1}, {0,4, 4,-1}, {0,4, 4,-1} }} },
        { "Pedal I: I-IV/I-V/I","ADV",   {{ {0,4, 4,-1}, {3,4, 4,-1}, {0,4, 4,-1}, {4,4,-1,-1} }} },
    };
}

static constexpr int kNumProgressions = 54;

// ── Note name from index (0-11) ──
inline const char* noteName (int idx)
{
    static const char* n[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return n[idx % 12];
}

// ═══════════════════════════════════════
// GENERATE — writes progression into track
// ═══════════════════════════════════════
// ── Rhythm patterns for harmony generation ──────────────────────
// Each pattern is defined for a 16-step span; scaled to actual chord duration
struct RhythmPattern
{
    const char* name;
    const char* category;
    int numHits;
    int offsets[16];     // step offsets within the chord duration (for dur=16)
    int velocities[16];  // velocity per hit (0 = use default 100)
    int gates[16];       // gate per hit (0 = use default 90)
};

static constexpr int kNumRhythms = 32;

inline const RhythmPattern* getRhythms()
{
    static const RhythmPattern patterns[kNumRhythms] = {
        // ── BASIC ──
        {"-OFF-",        "BASIC",     1, {0},                          {100},                      {90}},
        {"Half",         "BASIC",     2, {0, 8},                       {100, 85},                  {90, 80}},
        {"Quarter",      "BASIC",     4, {0, 4, 8, 12},               {100, 80, 90, 75},          {90, 80, 85, 75}},
        {"Eighth",       "BASIC",     8, {0, 2, 4, 6, 8, 10, 12, 14}, {100,70,85,65,90,70,80,60}, {80,60,75,55,80,60,70,50}},
        {"Sixteenth",    "BASIC",    16, {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}, {100,50,70,50,90,50,65,50,95,50,70,50,85,50,60,50}, {70,40,55,40,65,40,50,40,70,40,55,40,60,40,45,40}},
        {"Dotted Q",     "BASIC",     3, {0, 6, 12},                  {100, 85, 80},              {90, 80, 75}},
        // ── LATIN ──
        {"Tresillo",     "LATIN",     3, {0, 6, 12},                  {100, 90, 85},              {90, 80, 75}},
        {"Habanera",     "LATIN",     4, {0, 6, 8, 12},               {100, 80, 95, 75},          {90, 70, 85, 70}},
        {"Montuno",      "LATIN",     5, {0, 3, 6, 10, 12},           {100, 70, 90, 75, 85},      {80, 60, 75, 60, 70}},
        {"Bossa Nova",   "LATIN",     5, {0, 3, 6, 8, 12},            {100, 75, 85, 90, 70},      {85, 65, 75, 80, 60}},
        {"Son Clave",    "LATIN",     5, {0, 6, 9, 12, 15},           {100, 85, 75, 90, 80},      {90, 75, 65, 80, 70}},
        {"Rumba Clave",  "LATIN",     5, {0, 6, 10, 12, 15},          {100, 85, 80, 90, 75},      {90, 75, 70, 80, 65}},
        {"Reggaeton",    "LATIN",     4, {0, 3, 8, 11},               {100, 80, 95, 85},          {85, 60, 90, 70}},
        {"Cumbia",       "LATIN",     4, {0, 4, 6, 12},               {100, 75, 90, 80},          {90, 65, 80, 70}},
        // ── GROOVE ──
        {"Offbeat",      "GROOVE",    4, {2, 6, 10, 14},              {85, 80, 90, 75},           {80, 70, 85, 65}},
        {"Synco 1",      "GROOVE",    4, {0, 3, 8, 11},               {100, 85, 95, 80},          {90, 75, 85, 70}},
        {"Synco 2",      "GROOVE",    5, {0, 3, 6, 10, 13},           {100, 75, 90, 80, 70},      {85, 60, 80, 70, 55}},
        {"Shuffle",      "GROOVE",    4, {0, 5, 8, 13},               {100, 75, 90, 70},          {90, 65, 80, 60}},
        {"Stab",         "GROOVE",    3, {0, 4, 14},                  {100, 95, 70},              {40, 35, 30}},
        {"Arp Feel",     "GROOVE",    6, {0, 3, 5, 8, 11, 13},       {100,70,80,90,65,75},       {75,55,65,75,50,60}},
        {"Push",         "GROOVE",    4, {1, 5, 9, 13},               {95, 80, 90, 75},           {85, 70, 80, 65}},
        // ── HOUSE / TECHNO ──
        {"4-Floor",      "HOUSE",     4, {0, 4, 8, 12},               {100, 90, 95, 85},          {95, 90, 95, 85}},
        {"Offbeat Hse",  "HOUSE",     4, {2, 6, 10, 14},              {90, 85, 95, 80},           {90, 85, 90, 80}},
        {"Deep Hse",     "HOUSE",     3, {0, 6, 10},                  {100, 80, 90},              {90, 70, 80}},
        {"Garage",       "HOUSE",     5, {0, 3, 6, 10, 14},           {100, 65, 85, 75, 70},      {80, 50, 70, 60, 55}},
        // ── TRAP / DRILL ──
        {"Trap 1",       "TRAP",      5, {0, 6, 7, 12, 14},           {100, 70, 90, 85, 60},      {80, 40, 70, 65, 35}},
        {"Trap 2",       "TRAP",      6, {0, 3, 6, 7, 10, 14},        {100,60,80,90,70,65},       {70,35,55,65,45,35}},
        {"Drill",        "TRAP",      4, {0, 5, 8, 13},               {100, 85, 95, 80},          {60, 45, 55, 40}},
        {"Hihat Roll",   "TRAP",      8, {0, 2, 4, 5, 8, 10, 12, 13},{100,60,80,90,95,55,75,85}, {50,30,40,45,50,25,35,40}},
        // ── BROKEN / DnB ──
        {"Broken",       "DnB",       5, {0, 4, 6, 10, 15},           {100, 80, 90, 75, 85},      {85, 65, 80, 60, 70}},
        {"2-Step",       "DnB",       3, {0, 5, 12},                  {100, 85, 90},              {90, 70, 85}},
        {"Jungle",       "DnB",       6, {0, 3, 6, 9, 12, 14},       {100,70,85,75,90,65},       {75,50,65,55,70,45}},
    };
    return patterns;
}

inline void generate (SynthTrackState& track, int keyRoot, int scaleIdx, int progressionIdx,
                      int startStep = 0, int octave = 3, bool clearFirst = true,
                      int noteDuration = 0, int rhythmIdx = 0)
{
    auto progs = getProgressions();
    if (progressionIdx < 0 || progressionIdx >= static_cast<int>(progs.size())) return;
    if (scaleIdx < 0 || scaleIdx >= kNumScales) return;

    const auto& prog = progs[static_cast<size_t>(progressionIdx)];
    const int* scale = kScaleIntervals[scaleIdx];
    int scLen = 7;
    for (int i = 0; i < 7; ++i) if (scale[i] < 0) { scLen = i; break; }

    if (clearFirst)
    {
        for (int s = 0; s < kMaxSteps; ++s)
            track.seq.steps[static_cast<size_t>(s)].reset();
    }

    int step = startStep;
    for (const auto& ch : prog.chords)
    {
        if (step >= kMaxSteps) break;

        // Use override duration if set, otherwise use progression's default
        int dur = (noteDuration > 0) ? noteDuration : ch.steps;

        // Get root note — use semiOverride for non-diatonic chords (bVII, bVI, bIII)
        int noteOffset;
        if (ch.semiOverride >= 0)
            noteOffset = ch.semiOverride; // exact semitone from key root
        else
        {
            int degree = ch.degree % scLen;
            noteOffset = scale[degree];
        }
        int noteIdx = (keyRoot + noteOffset) % 12;
        int noteOct = octave + (keyRoot + noteOffset) / 12;

        // Determine chord type
        int chordType = ch.chordOverride;
        if (chordType < 0)
        {
            if (ch.semiOverride >= 0)
                chordType = 1; // default MAJ for non-diatonic
            else
                chordType = chordQualityForDegree (ch.degree % scLen, scaleIdx);
        }

        // Write notes using rhythm pattern
        const auto* rhythms = getRhythms();
        int ri = std::clamp (rhythmIdx, 0, kNumRhythms - 1);
        const auto& rhy = rhythms[ri];

        for (int h = 0; h < rhy.numHits; ++h)
        {
            // Scale rhythm offset from 16-step grid to actual chord duration
            int hitOffset = (rhy.offsets[h] * dur + 8) / 16; // rounded
            int hitStep = step + hitOffset;
            if (hitStep >= kMaxSteps) break;
            if (hitStep < step + dur) // must be within chord span
            {
                auto& sd = track.seq.steps[static_cast<size_t>(hitStep)];
                sd.active = true;
                sd.noteIndex = static_cast<uint8_t>(noteIdx);
                sd.octave = static_cast<uint8_t>(std::clamp (noteOct, 0, 8));
                sd.chordMode = static_cast<int8_t>(chordType);
                sd.velocity = static_cast<uint8_t>(rhy.velocities[h] > 0 ? rhy.velocities[h] : 100);
                sd.gate = static_cast<uint8_t>(rhy.gates[h] > 0 ? rhy.gates[h] : 90);

                // Note length: distance to next hit or end of chord
                int nextHitOff = (h + 1 < rhy.numHits)
                    ? (rhy.offsets[h + 1] * dur + 8) / 16
                    : dur;
                sd.noteLen = static_cast<uint8_t>(std::clamp (nextHitOff - hitOffset, 1, 128));
            }
        }

        step += dur;
    }

    // Set track length to fit the progression
    track.length = std::clamp (step, 1, kMaxSteps);

    // Enable chord mode on track (the per-step chordMode overrides this)
    // But set track default to OFF so only generated steps have chords
    track.chordMode = 0;
}

} // namespace HarmonyGen
