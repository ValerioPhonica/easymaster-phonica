#pragma once
#include "../Sequencer/StepData.h"
#include <array>
#include <vector>
#include <string>

// Full undo/redo — captures step sequences + sound params (as XML string)
class SeqUndoManager
{
public:
    static constexpr int kMaxUndo = 5;

    struct Snapshot
    {
        std::array<StepSequence, 10> drumSeqs;
        std::array<int, 10>         drumLens;
        std::array<StepSequence, 5> synthSeqs;
        std::array<int, 5>          synthLens;
        std::string                 paramXml;   // full sound param state (XML)
        bool valid = false;
    };

    template <typename DrumTracks, typename SynthTracks>
    void pushUndo (const DrumTracks& drums, const SynthTracks& synths,
                   const std::string& currentParamXml = "")
    {
        if (undoPos < static_cast<int>(history.size()) - 1)
            history.resize (static_cast<size_t>(undoPos + 1));

        Snapshot snap;
        for (int i = 0; i < 10; ++i)
        {
            snap.drumSeqs[static_cast<size_t>(i)] = drums[static_cast<size_t>(i)].seq;
            snap.drumLens[static_cast<size_t>(i)] = drums[static_cast<size_t>(i)].length;
        }
        for (int i = 0; i < 5; ++i)
        {
            snap.synthSeqs[static_cast<size_t>(i)] = synths[static_cast<size_t>(i)].seq;
            snap.synthLens[static_cast<size_t>(i)] = synths[static_cast<size_t>(i)].length;
        }
        snap.paramXml = currentParamXml;
        snap.valid = true;
        history.push_back (snap);

        if (history.size() > kMaxUndo + 1)
            history.erase (history.begin());

        undoPos = static_cast<int>(history.size()) - 1;
    }

    template <typename DrumTracks, typename SynthTracks>
    bool undo (DrumTracks& drums, SynthTracks& synths, const std::string& currentParamXml = "")
    {
        if (undoPos <= 0 || history.empty()) return false;

        if (undoPos == static_cast<int>(history.size()) - 1)
        {
            Snapshot current;
            for (int i = 0; i < 10; ++i)
            {
                current.drumSeqs[static_cast<size_t>(i)] = drums[static_cast<size_t>(i)].seq;
                current.drumLens[static_cast<size_t>(i)] = drums[static_cast<size_t>(i)].length;
            }
            for (int i = 0; i < 5; ++i)
            {
                current.synthSeqs[static_cast<size_t>(i)] = synths[static_cast<size_t>(i)].seq;
                current.synthLens[static_cast<size_t>(i)] = synths[static_cast<size_t>(i)].length;
            }
            current.paramXml = currentParamXml;
            current.valid = true;
            history.push_back (current);
        }

        undoPos--;
        restoreSnapshot (history[static_cast<size_t>(undoPos)], drums, synths);
        return true;
    }

    template <typename DrumTracks, typename SynthTracks>
    bool redo (DrumTracks& drums, SynthTracks& synths)
    {
        if (undoPos >= static_cast<int>(history.size()) - 1) return false;
        undoPos++;
        restoreSnapshot (history[static_cast<size_t>(undoPos)], drums, synths);
        return true;
    }

    bool canUndo() const { return undoPos > 0 && !history.empty(); }
    bool canRedo() const { return undoPos < static_cast<int>(history.size()) - 1; }

    // Get the param XML from the last restored snapshot (caller applies it)
    std::string getLastRestoredParamXml() const { return lastRestoredXml; }

private:
    std::vector<Snapshot> history;
    int undoPos = -1;
    std::string lastRestoredXml;

    template <typename DrumTracks, typename SynthTracks>
    void restoreSnapshot (const Snapshot& snap, DrumTracks& drums, SynthTracks& synths)
    {
        for (int i = 0; i < 10; ++i)
        {
            drums[static_cast<size_t>(i)].seq = snap.drumSeqs[static_cast<size_t>(i)];
            drums[static_cast<size_t>(i)].length = snap.drumLens[static_cast<size_t>(i)];
        }
        for (int i = 0; i < 5; ++i)
        {
            synths[static_cast<size_t>(i)].seq = snap.synthSeqs[static_cast<size_t>(i)];
            synths[static_cast<size_t>(i)].length = snap.synthLens[static_cast<size_t>(i)];
        }
        lastRestoredXml = snap.paramXml;
    }
};
