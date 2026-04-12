#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include "StepData.h"

// Scale definitions matching HTML ALL_SCALES exactly
namespace ScaleUtils
{
    static const char* const SCALE_NAMES[] = {
        "CHROM", "MAJOR", "MINOR", "PENTA", "BLUES",
        "DORIC", "PHRYG", "LYDIA", "MIXO", "LOCR",
        "HARM", "WHOLE", "DIM", "AUG"
    };
    static constexpr int NUM_SCALES = 14;

    static const char* const NOTE_NAMES[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    // Scale intervals (semitones from root)
    inline const std::vector<int>& getScale (int scaleType)
    {
        static const std::vector<int> scales[] = {
            {0,1,2,3,4,5,6,7,8,9,10,11}, // 0 chromatic
            {0,2,4,5,7,9,11},             // 1 major
            {0,2,3,5,7,8,10},             // 2 minor
            {0,2,4,7,9},                  // 3 pentatonic
            {0,3,5,6,7,10},               // 4 blues
            {0,2,3,5,7,9,10},             // 5 dorian
            {0,1,3,5,7,8,10},             // 6 phrygian
            {0,2,4,6,7,9,11},             // 7 lydian
            {0,2,4,5,7,9,10},             // 8 mixolydian
            {0,1,3,5,6,8,10},             // 9 locrian
            {0,2,3,5,7,8,11},             // 10 harmonic minor
            {0,2,4,6,8,10},               // 11 whole tone
            {0,2,3,5,6,8,9,11},           // 12 diminished
            {0,3,4,7,8,11}                // 13 augmented
        };
        if (scaleType < 0 || scaleType >= NUM_SCALES) return scales[0];
        return scales[scaleType];
    }

    // Quantize a note index (0-11) to the nearest note in the given scale
    // Returns the quantized note index (0-11)
    inline int quantizeNote (int noteIdx, int scaleRoot, int scaleType)
    {
        if (scaleType == 0) return noteIdx; // chromatic = no quantization

        const auto& scale = getScale (scaleType);
        int relToRoot = ((noteIdx - scaleRoot) % 12 + 12) % 12;

        // Find nearest scale degree
        int bestDeg = 0;
        int bestDist = 12;
        for (int deg : scale)
        {
            int dist = std::min (std::abs (deg - relToRoot), 12 - std::abs (deg - relToRoot));
            if (dist < bestDist)
            {
                bestDist = dist;
                bestDeg = deg;
            }
        }

        return ((scaleRoot + bestDeg) % 12 + 12) % 12;
    }

    // Check if a note is in the current scale
    inline bool isInScale (int noteIdx, int scaleRoot, int scaleType)
    {
        if (scaleType == 0) return true;
        const auto& scale = getScale (scaleType);
        int relToRoot = ((noteIdx - scaleRoot) % 12 + 12) % 12;
        for (int deg : scale)
            if (deg == relToRoot) return true;
        return false;
    }

    // ═══════════════════════════════════════
    // CHORD DEFINITIONS (12 types)
    // ═══════════════════════════════════════
    static const char* const CHORD_NAMES[] = {
        "OFF", "MAJ", "MIN", "7TH", "m7", "MAJ7", "DIM", "AUG",
        "SUS2", "SUS4", "ADD9", "m9", "PWR"
    };
    static constexpr int NUM_CHORDS = 13;

    // Returns intervals in semitones from root
    inline const std::vector<int>& getChordIntervals (int chordType)
    {
        static const std::vector<int> chords[] = {
            {},                    // 0 = OFF
            {0, 4, 7},            // 1 = Major triad
            {0, 3, 7},            // 2 = Minor triad
            {0, 4, 7, 10},        // 3 = Dominant 7th
            {0, 3, 7, 10},        // 4 = Minor 7th
            {0, 4, 7, 11},        // 5 = Major 7th
            {0, 3, 6},            // 6 = Diminished
            {0, 4, 8},            // 7 = Augmented
            {0, 2, 7},            // 8 = Sus2
            {0, 5, 7},            // 9 = Sus4
            {0, 4, 7, 14},        // 10 = Add9 (2nd octave)
            {0, 3, 7, 10, 14},    // 11 = Minor 9th
            {0, 7}                // 12 = Power chord (5th)
        };
        if (chordType < 0 || chordType >= NUM_CHORDS) return chords[0];
        return chords[chordType];
    }

    // Build chord notes from root, quantizing each to scale
    inline std::vector<NoteInfo> buildChord (int rootNoteIdx, int rootOctave, int chordType,
                                              int scaleRoot, int scaleType)
    {
        std::vector<NoteInfo> notes;
        if (chordType <= 0) return notes;

        const auto& intervals = getChordIntervals (chordType);
        for (int interval : intervals)
        {
            int absSemi = rootNoteIdx + interval;
            int noteIdx = absSemi % 12;
            int octaveOffset = absSemi / 12;

            // Quantize to scale
            int qNote = quantizeNote (noteIdx, scaleRoot, scaleType);
            notes.push_back ({ static_cast<uint8_t>(qNote), static_cast<uint8_t>(rootOctave + octaveOffset) });
        }
        return notes;
    }
}
