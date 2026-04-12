#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

// ═══════════════════════════════════════════════════════════════════
// ArpEngine — Step Arpeggiator (Ableton Step Arp style)
//
// Per-track arp with up to 32 steps, each with:
//   - Velocity (1-127, drawn as bar height)
//   - Gate (5-200%, drawn as bar width)
//   - Assignable parameter (0-100, routed to any synth target)
//
// Global controls: division, octaves, direction, loop length,
// key retrigger, assignable target + depth.
// ═══════════════════════════════════════════════════════════════════

static constexpr int kArpMaxSteps = 32;

struct ArpStepData
{
    uint8_t velocity = 100;   // 1-127
    uint8_t gate     = 80;    // 5-200 (percentage of arp step duration)
    float   param    = 0.5f;  // 0.0 - 1.0 assignable param lane 1
    float   param2   = 0.5f;  // 0.0 - 1.0 assignable param lane 2
};

struct ArpData
{
    bool    enabled    = false;
    int     numSteps   = 8;      // 1-32 active steps
    int     division   = 3;      // 0=1/32, 1=1/16T, 2=1/16, 3=1/8, 4=1/8T, 5=1/4, 6=1/2, 7=1bar
    int     octaves    = 1;      // 1-4 octave range
    int     direction  = 0;      // 0=UP, 1=DN, 2=U/D, 3=D/U, 4=RND, 5=CHORD, 6=CONV, 7=DIV
    bool    keyRetrig  = true;   // reset arp position on new note
    int     loopLen    = 0;      // 0 = use numSteps, otherwise 1-32
    int     assignTarget = 1;    // LFO/MSEG target ID (1=CUT, etc.)
    float   assignDepth  = 0.0f; // -1 to +1
    int     assign2Target = -1;  // second param lane target
    float   assign2Depth  = 0.0f; // -1 to +1

    // Multi-target modulation routes (like LFO extraRoutes)
    struct ArpModRoute { int target = -1; float depth = 0.0f; };
    std::array<ArpModRoute, 16> extraRoutes;

    std::array<ArpStepData, kArpMaxSteps> steps;

    ArpData()
    {
        for (int i = 0; i < kArpMaxSteps; ++i)
        {
            steps[static_cast<size_t>(i)].velocity = 100;
            steps[static_cast<size_t>(i)].gate = 80;
            steps[static_cast<size_t>(i)].param = 0.5f;
            steps[static_cast<size_t>(i)].param2 = 0.5f;
        }
    }

    // Division → samples per arp step
    float getDivisionBeats() const
    {
        // Returns duration in beats (quarter notes)
        static const float divBeats[] = {
            0.125f,    // 0: 1/32
            0.1667f,   // 1: 1/16T (triplet)
            0.25f,     // 2: 1/16
            0.5f,      // 3: 1/8
            0.667f,    // 4: 1/8T
            1.0f,      // 5: 1/4
            2.0f,      // 6: 1/2
            4.0f       // 7: 1 bar (4/4)
        };
        return divBeats[std::clamp (division, 0, 7)];
    }

    static const char* getDivisionName (int div)
    {
        static const char* names[] = {
            "1/32", "1/16T", "1/16", "1/8", "1/8T", "1/4", "1/2", "1bar"
        };
        return names[std::clamp (div, 0, 7)];
    }

    static const char* getDirectionName (int dir)
    {
        static const char* names[] = { "UP", "DOWN", "UP-DN", "DN-UP", "RND", "CHORD", "CONV", "DIV" };
        return names[std::clamp (dir, 0, 7)];
    }

    int getEffectiveLen() const
    {
        return (loopLen > 0) ? std::clamp (loopLen, 1, kArpMaxSteps)
                             : std::clamp (numSteps, 1, kArpMaxSteps);
    }
};


// ═══════════════════════════════════════
// ArpEngine — runtime arp processor
// ═══════════════════════════════════════
struct ArpNoteEvent
{
    int   noteIndex = 0;    // 0-11
    int   octave    = 3;    // 0-8
    float velocity  = 1.0f; // 0-1
    float gateScale = 1.0f; // multiplier for note gate
    float paramVal  = 0.5f; // assignable param value for this step (lane 1)
    float param2Val = 0.5f; // assignable param value for this step (lane 2)
};

class ArpEngine
{
public:
    void prepare (float sr) { sampleRate = sr; }

    // Feed held notes — returns true if notes actually changed
    bool setHeldNotes (const std::vector<std::pair<int,int>>& notes, const ArpData& data)
    {
        auto sorted = notes;
        std::sort (sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return (a.second * 12 + a.first) < (b.second * 12 + b.first);
        });
        bool changed = (sorted != heldNotes);
        heldNotes = sorted;
        buildNoteList (data);
        timeSinceLastTrigger = 0;
        return changed;
    }

    void noteOff()
    {
        heldNotes.clear();
        noteList.clear();
        totalNotes = 0;
        currentlyPlaying = false;
    }

    void retrigger (const ArpData& data)
    {
        if (data.keyRetrig)
        {
            arpStepIdx = 0;
            noteIdx = 0;
            pingDir = 1;
            fireNextTick = true;   // fire ONE note on next tick
            sampleCounter = 0.0;   // reset counter cleanly
        }
    }

    void reset()
    {
        arpStepIdx = 0;
        noteIdx = 0;
        pingDir = 1;
        sampleCounter = 0.0;
        gateRemaining = 0;
        timeSinceLastTrigger = 0;
        currentlyPlaying = false;
        fireNextTick = false;
        heldNotes.clear();
        noteList.clear();
        totalNotes = 0;
    }

    bool tick (int numSamples, float bpm, const ArpData& data)
    {
        if (!data.enabled || heldNotes.empty() || totalNotes <= 0)
        {
            currentlyPlaying = false;
            fireNextTick = false;
            return false;
        }

        // Auto-timeout: stop if no new trigger for one full pattern cycle
        timeSinceLastTrigger += numSamples;
        float beatsPerStep = data.getDivisionBeats();
        float samplesPerBeat = (sampleRate * 60.0f) / std::max (20.0f, bpm);
        double samplesPerArpStep = static_cast<double>(beatsPerStep * samplesPerBeat);
        if (samplesPerArpStep < 1.0) samplesPerArpStep = 1.0; // safety

        int patternSamples = static_cast<int>(samplesPerArpStep * data.getEffectiveLen()) + 4096;
        if (timeSinceLastTrigger > patternSamples)
        {
            noteOff();
            return false;
        }

        sampleCounter += numSamples;

        // Gate countdown
        if (gateRemaining > 0)
        {
            gateRemaining -= numSamples;
            if (gateRemaining <= 0)
                currentlyPlaying = false;
        }

        // Fire note: either immediate (fireNextTick) or when counter reaches step boundary
        bool shouldFire = fireNextTick || (sampleCounter >= samplesPerArpStep);

        if (shouldFire)
        {
            if (fireNextTick)
            {
                // Immediate fire: reset counter for proper timing from here
                sampleCounter = 0.0;
                fireNextTick = false;
            }
            else
            {
                // Normal step boundary: subtract one step
                sampleCounter -= samplesPerArpStep;
                // Prevent accumulated drift: clamp to avoid rapid catch-up
                if (sampleCounter > samplesPerArpStep)
                    sampleCounter = 0.0;
            }

            // Get current arp step data
            int stepLen = data.getEffectiveLen();
            int si = arpStepIdx % stepLen;
            const auto& stepData = data.steps[static_cast<size_t>(si)];

            // Get current note from expanded note list
            int ni = noteIdx % totalNotes;
            if (ni < 0) ni = 0;
            if (ni >= static_cast<int>(noteList.size())) ni = 0;

            lastEvent.noteIndex = noteList[static_cast<size_t>(ni)].first;
            lastEvent.octave    = noteList[static_cast<size_t>(ni)].second;
            lastEvent.velocity  = static_cast<float>(stepData.velocity) / 127.0f;
            lastEvent.gateScale = static_cast<float>(stepData.gate) / 100.0f;
            lastEvent.paramVal  = stepData.param;
            lastEvent.param2Val = stepData.param2;

            gateRemaining = static_cast<int>(samplesPerArpStep * lastEvent.gateScale);
            currentlyPlaying = true;

            // Advance to next note + step
            advanceNote (data);
            arpStepIdx++;

            return true;
        }
        return false;
    }

    ArpNoteEvent getLastEvent() const { return lastEvent; }
    bool isPlaying() const { return currentlyPlaying; }

private:
    float sampleRate = 44100.0f;
    double sampleCounter = 0.0;
    int gateRemaining = 0;
    int timeSinceLastTrigger = 0;
    bool currentlyPlaying = false;
    bool fireNextTick = false;

    std::vector<std::pair<int,int>> heldNotes;
    std::vector<std::pair<int,int>> noteList; // expanded with octaves
    int totalNotes = 0;

    int arpStepIdx = 0;
    int noteIdx = 0;
    int pingDir = 1;
    int convIdx = 0;
    int divIdx = 0;

    ArpNoteEvent lastEvent;

    // Build note list with octave expansion upfront
    void buildNoteList (const ArpData& data)
    {
        noteList.clear();
        if (heldNotes.empty()) { totalNotes = 0; return; }

        int numOctaves = std::max (1, data.octaves);
        int numBase = static_cast<int>(heldNotes.size());

        noteList.reserve (static_cast<size_t>(numBase * numOctaves));
        for (int oct = 0; oct < numOctaves; ++oct)
        {
            for (int n = 0; n < numBase; ++n)
            {
                noteList.push_back ({
                    heldNotes[static_cast<size_t>(n)].first,
                    heldNotes[static_cast<size_t>(n)].second + oct
                });
            }
        }
        totalNotes = static_cast<int>(noteList.size());
    }

    void advanceNote (const ArpData& data)
    {
        if (totalNotes <= 0) return;

        switch (data.direction)
        {
            case 0: // UP
                noteIdx = (noteIdx + 1) % totalNotes;
                break;
            case 1: // DOWN
                noteIdx--;
                if (noteIdx < 0) noteIdx = totalNotes - 1;
                break;
            case 2: // UP-DOWN
                noteIdx += pingDir;
                if (noteIdx >= totalNotes - 1) { noteIdx = totalNotes - 1; pingDir = -1; }
                else if (noteIdx <= 0) { noteIdx = 0; pingDir = 1; }
                break;
            case 3: // DOWN-UP
                noteIdx += pingDir;
                if (noteIdx <= 0) { noteIdx = 0; pingDir = 1; }
                else if (noteIdx >= totalNotes - 1) { noteIdx = totalNotes - 1; pingDir = -1; }
                break;
            case 4: // RANDOM
                noteIdx = std::rand() % totalNotes;
                break;
            case 5: // CHORD — noteIdx stays at 0, all notes fire together
                noteIdx = 0;
                break;
            case 6: // CONVERGE — outside-in
            {
                // Pattern: 0, last, 1, last-1, 2, last-2, ...
                int half = (totalNotes + 1) / 2;
                int ci = convIdx % (totalNotes);
                int pair = ci / 2;
                noteIdx = (ci % 2 == 0) ? pair : (totalNotes - 1 - pair);
                noteIdx = std::clamp (noteIdx, 0, totalNotes - 1);
                convIdx++;
                break;
            }
            case 7: // DIVERGE — inside-out
            {
                int mid = totalNotes / 2;
                int di = divIdx % totalNotes;
                int d = (di + 1) / 2;
                noteIdx = (di % 2 == 0) ? (mid + d) : (mid - d);
                noteIdx = std::clamp (noteIdx, 0, totalNotes - 1);
                divIdx++;
                break;
            }
        }
    }
};
