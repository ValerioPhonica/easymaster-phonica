#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Colours.h"
#include "../../Sequencer/StepData.h"
#include <functional>
#include <atomic>
#include <set>

class PianoKeyboard : public juce::Component
{
public:
    PianoKeyboard()
    {
        setRepaintsOnMouseActivity (true);
    }

    // Callbacks
    std::function<void(int note, float velocity)> onNoteOn;
    std::function<void(int note)> onNoteOff;
    std::function<void()> onClose;

    // State
    int baseOctave = 4;
    float velocity = 0.8f;
    bool holdMode = false;
    bool scaleHighlight = true;
    bool recMode = false;
    bool stepRecMode = false;   // true = step-by-step (no play)
    int quantizeDiv = 4;        // 0=OFF, 1=1/4, 2=1/8, 4=1/16

    // Scale info (set from outside)
    int scaleRoot = 0;
    int scaleType = 0;

    // Step REC state
    int recStep = 0;
    int recLength = 16;

    // Sequencer playing step (for real-time rec display)
    std::atomic<int>* playingStepPtr = nullptr;

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        auto toolbar = bounds.removeFromTop (28);
        auto keyArea = bounds;

        // ── TOOLBAR ──
        g.setColour (Colours_GB::panel2);
        g.fillRect (toolbar);
        g.setColour (Colours_GB::border);
        g.drawLine (0, 28, static_cast<float>(getWidth()), 28, 0.5f);

        int tx = 8;
        auto drawLabel = [&](const juce::String& text, int w, juce::Colour col) {
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.setColour (col);
            g.drawText (text, tx, toolbar.getY() + 2, w, 24, juce::Justification::centredLeft);
            tx += w;
        };
        auto drawBtn = [&](const juce::String& text, int w, bool active, juce::Colour activeCol) {
            auto r = juce::Rectangle<float>(static_cast<float>(tx), toolbar.getY() + 4.0f,
                                             static_cast<float>(w), 20.0f);
            g.setColour (active ? activeCol : Colours_GB::panel3);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (active ? juce::Colour (0xff001820) : Colours_GB::text);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, active ? juce::Font::bold : juce::Font::plain));
            g.drawText (text, r.toNearestInt(), juce::Justification::centred);
            tx += w + 3;
        };

        drawLabel ("KEYBOARD", 62, Colours_GB::accent);
        tx += 2;

        // Octave
        drawBtn ("-", 14, false, Colours_GB::accent);
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
        g.setColour (Colours_GB::accentBright);
        g.drawText ("C" + juce::String (baseOctave), tx, toolbar.getY() + 2, 20, 24, juce::Justification::centred);
        tx += 22;
        drawBtn ("+", 14, false, Colours_GB::accent);
        tx += 4;

        // VEL
        drawLabel ("VEL", 22, Colours_GB::textDim);
        g.setColour (Colours_GB::accent);
        g.drawText (juce::String (static_cast<int>(velocity * 127)), tx, toolbar.getY() + 2, 24, 24, juce::Justification::centredLeft);
        tx += 26;

        // Separator
        g.setColour (Colours_GB::border);
        g.drawVerticalLine (tx, toolbar.getY() + 6.0f, toolbar.getY() + 22.0f);
        tx += 6;

        drawBtn ("HOLD", 30, holdMode, Colours_GB::accent);
        drawBtn ("SCALE", 34, scaleHighlight, Colours_GB::accent);

        // Separator
        g.setColour (Colours_GB::border);
        g.drawVerticalLine (tx, toolbar.getY() + 6.0f, toolbar.getY() + 22.0f);
        tx += 6;

        // REC
        if (recMode)
        {
            auto r = juce::Rectangle<float>(static_cast<float>(tx), toolbar.getY() + 4.0f, 30.0f, 20.0f);
            g.setColour (juce::Colour (0xffcc2020));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawText ("REC", r.toNearestInt(), juce::Justification::centred);
            tx += 33;
        }
        else
        {
            drawBtn ("REC", 28, false, juce::Colour (0xffcc2020));
        }

        // Quantize
        juce::String qStr = quantizeDiv == 0 ? "Q:OFF" : "Q:1/" + juce::String (quantizeDiv * 4);
        drawBtn (qStr, 38, quantizeDiv > 0, Colours_GB::accent);

        // Step counter (when rec)
        if (recMode)
        {
            int curStep = stepRecMode ? recStep : (playingStepPtr ? playingStepPtr->load() : 0);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
            g.setColour (Colours_GB::textDim);
            g.drawText ("STEP", tx, toolbar.getY() + 2, 28, 24, juce::Justification::centredLeft);
            tx += 28;
            g.setColour (juce::Colour (0xffff4040));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            g.drawText (juce::String (curStep + 1) + "/" + juce::String (recLength), tx, toolbar.getY() + 2, 30, 24, juce::Justification::centredLeft);
            tx += 34;

            drawBtn ("CLR", 24, false, Colours_GB::accent);
        }

        // Note display
        tx = getWidth() - 70;
        if (activeNote >= 0)
        {
            const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            int oct = activeNote / 12 - 2;
            int ni = activeNote % 12;
            auto r = juce::Rectangle<float>(static_cast<float>(tx), toolbar.getY() + 4.0f, 36.0f, 20.0f);
            g.setColour (Colours_GB::bg);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colours_GB::accent.withAlpha (0.4f));
            g.drawRoundedRectangle (r, 3.0f, 0.4f);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
            g.setColour (Colours_GB::accentBright);
            g.drawText (juce::String (names[ni]) + juce::String (oct), r.toNearestInt(), juce::Justification::centred);
        }

        // Close X
        tx = getWidth() - 28;
        auto xr = juce::Rectangle<float>(static_cast<float>(tx), toolbar.getY() + 4.0f, 22.0f, 20.0f);
        g.setColour (Colours_GB::panel3);
        g.fillRoundedRectangle (xr, 3.0f);
        g.setColour (Colours_GB::textDim);
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        g.drawText ("X", xr.toNearestInt(), juce::Justification::centred);

        // ── PIANO KEYS ──
        g.setColour (juce::Colour (0xff0a0c14));
        g.fillRect (keyArea);

        int numOctaves = 2;
        int numWhite = numOctaves * 7 + 1; // +1 for final C
        float whiteW = static_cast<float>(keyArea.getWidth()) / numWhite;
        float whiteH = static_cast<float>(keyArea.getHeight());
        float blackW = whiteW * 0.65f;
        float blackH = whiteH * 0.62f;
        float keyY = static_cast<float>(keyArea.getY());

        // Scale notes (for highlighting)
        static const int scales[][12] = {
            {1,1,1,1,1,1,1,1,1,1,1,1}, // chromatic
            {1,0,1,0,1,1,0,1,0,1,0,1}, // major
            {1,0,1,1,0,1,0,1,1,0,1,0}, // minor
            {1,0,1,0,1,0,0,1,0,1,0,0}, // penta
            {1,0,1,1,0,1,1,1,0,1,0,0}, // blues
            {1,0,1,1,0,1,0,1,0,1,1,0}, // dorian
            {1,1,0,1,0,1,0,1,1,0,1,0}, // phrygian
            {1,0,1,0,1,0,1,1,0,1,0,1}, // lydian
            {1,0,1,0,1,1,0,1,0,1,1,0}, // mixo
            {1,1,0,1,0,1,1,0,1,0,1,0}, // locr
            {1,0,1,1,0,1,0,1,1,0,0,1}, // harm min
            {1,0,1,0,1,0,1,0,1,0,1,0}, // whole
            {1,0,1,1,0,1,1,0,1,1,0,1}, // dim
            {1,0,0,1,1,0,0,1,1,0,0,1}, // aug
        };

        auto isInScale = [&](int midiNote) -> bool {
            if (!scaleHighlight || scaleType == 0) return false;
            int st = scaleType < 14 ? scaleType : 0;
            int rel = ((midiNote % 12) - scaleRoot + 12) % 12;
            return scales[st][rel] != 0;
        };

        auto isBlack = [](int noteInOct) { return noteInOct==1||noteInOct==3||noteInOct==6||noteInOct==8||noteInOct==10; };

        // Draw white keys
        int whiteIdx = 0;
        for (int oct = 0; oct <= numOctaves; ++oct)
        {
            int maxNote = (oct == numOctaves) ? 1 : 7;
            static const int whiteNotes[] = {0,2,4,5,7,9,11};
            for (int wn = 0; wn < maxNote; ++wn)
            {
                int noteInOct = whiteNotes[wn];
                int midiNote = (baseOctave + oct + 2) * 12 + noteInOct;
                float x = whiteIdx * whiteW;

                bool isActive = (midiNote == activeNote) || heldNotes.count (midiNote);
                bool inScale = isInScale (midiNote);

                // White key fill
                juce::Colour keyCol = isActive ? Colours_GB::accent.withAlpha (0.5f) : juce::Colour (0xffd0d4e0);
                g.setColour (keyCol);
                g.fillRoundedRectangle (x + 1, keyY, whiteW - 2, whiteH - 2, 2.0f);
                g.setColour (juce::Colour (0xff252a35));
                g.drawRoundedRectangle (x + 1, keyY, whiteW - 2, whiteH - 2, 2.0f, 0.3f);

                // Scale dot
                if (inScale)
                {
                    float dotY = keyY + whiteH - 14;
                    g.setColour (Colours_GB::accent.withAlpha (noteInOct == 0 ? 0.7f : 0.4f));
                    g.fillEllipse (x + whiteW / 2 - 3, dotY, 6, 6);
                }

                // Note name
                if (whiteH > 60)
                {
                    const char* names[] = {"C","D","E","F","G","A","B"};
                    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
                    g.setColour (isActive ? Colours_GB::accent : juce::Colour (0xff404858));
                    g.drawText (names[wn], static_cast<int>(x), static_cast<int>(keyY + whiteH - 14),
                                static_cast<int>(whiteW), 12, juce::Justification::centred);
                }

                // REC indicator (red bar at bottom of recorded step)
                if (recMode && isActive)
                {
                    g.setColour (juce::Colour (0xffff4040).withAlpha (0.7f));
                    g.fillRect (x + 2, keyY + whiteH - 5, whiteW - 4, 3.0f);
                }

                whiteKeyMidi[whiteIdx] = midiNote;
                whiteIdx++;
            }
        }

        // Draw black keys
        whiteIdx = 0;
        for (int oct = 0; oct < numOctaves; ++oct)
        {
            static const int blackPos[] = {0,1,3,4,5}; // which white key the black is after
            static const int blackNote[] = {1,3,6,8,10};
            for (int bn = 0; bn < 5; ++bn)
            {
                int whiteBase = oct * 7 + blackPos[bn];
                int midiNote = (baseOctave + oct + 2) * 12 + blackNote[bn];
                float x = (whiteBase + 1) * whiteW - blackW / 2;

                bool isActive = (midiNote == activeNote) || heldNotes.count (midiNote);

                juce::Colour keyCol = isActive ? Colours_GB::accent.withAlpha (0.7f) : juce::Colour (0xff0e1118);
                g.setColour (keyCol);
                g.fillRoundedRectangle (x, keyY, blackW, blackH, 2.0f);
                g.setColour (juce::Colour (0xff181c24));
                g.drawRoundedRectangle (x, keyY, blackW, blackH, 2.0f, 0.5f);
            }
        }

        // Store layout for hit testing
        numWhiteKeys = numWhite;
        this->whiteW = whiteW;
        this->whiteH = whiteH;
        this->blackW = blackW;
        this->blackH = blackH;
        this->keyY = keyY;
        this->numOctavesStored = numOctaves;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto pos = e.getPosition();

        // Toolbar hit testing
        if (pos.y < 28)
        {
            handleToolbarClick (pos.x);
            return;
        }

        int note = hitTestKey (pos);
        if (note >= 0) triggerNoteOn (note);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getPosition().y < 28) return;
        int note = hitTestKey (e.getPosition());
        if (note >= 0 && note != activeNote)
        {
            if (activeNote >= 0 && !holdMode) triggerNoteOff (activeNote);
            triggerNoteOn (note);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.getPosition().y < 28) return;
        if (!holdMode && activeNote >= 0)
        {
            triggerNoteOff (activeNote);
            activeNote = -1;
            repaint();
        }
    }

private:
    int activeNote = -1;
    std::set<int> heldNotes;
    int whiteKeyMidi[32] = {};
    int numWhiteKeys = 15;
    float whiteW = 0, whiteH = 0, blackW = 0, blackH = 0, keyY = 0;
    int numOctavesStored = 2;

    void triggerNoteOn (int midiNote)
    {
        if (holdMode)
        {
            if (heldNotes.count (midiNote))
            {
                heldNotes.erase (midiNote);
                if (onNoteOff) onNoteOff (midiNote);
            }
            else
            {
                heldNotes.insert (midiNote);
                if (onNoteOn) onNoteOn (midiNote, velocity);
            }
        }
        else
        {
            activeNote = midiNote;
            if (onNoteOn) onNoteOn (midiNote, velocity);
        }
        repaint();
    }

    void triggerNoteOff (int midiNote)
    {
        if (onNoteOff) onNoteOff (midiNote);
        heldNotes.erase (midiNote);
    }

    int hitTestKey (juce::Point<int> pos) const
    {
        if (whiteW < 1 || pos.y < static_cast<int>(keyY)) return -1;
        float mx = static_cast<float>(pos.x);
        float my = static_cast<float>(pos.y);

        // Check black keys first (they're on top)
        for (int oct = 0; oct < numOctavesStored; ++oct)
        {
            static const int blackPos[] = {0,1,3,4,5};
            static const int blackNote[] = {1,3,6,8,10};
            for (int bn = 0; bn < 5; ++bn)
            {
                int whiteBase = oct * 7 + blackPos[bn];
                int midiNote = (baseOctave + oct + 2) * 12 + blackNote[bn];
                float bx = (whiteBase + 1) * whiteW - blackW / 2;
                if (mx >= bx && mx < bx + blackW && my >= keyY && my < keyY + blackH)
                    return midiNote;
            }
        }

        // White keys
        int whiteIdx = static_cast<int>(mx / whiteW);
        if (whiteIdx >= 0 && whiteIdx < numWhiteKeys)
            return whiteKeyMidi[whiteIdx];
        return -1;
    }

    void handleToolbarClick (int mx)
    {
        // Hit zones (approximate from paint layout)
        if (mx >= 72 && mx < 86)  { baseOctave = std::max (0, baseOctave - 1); repaint(); } // -
        if (mx >= 108 && mx < 122) { baseOctave = std::min (7, baseOctave + 1); repaint(); } // +

        // HOLD
        if (mx >= 210 && mx < 244) {
            holdMode = !holdMode;
            if (!holdMode) { for (auto n : heldNotes) if (onNoteOff) onNoteOff (n); heldNotes.clear(); activeNote = -1; }
            repaint();
        }
        // SCALE
        if (mx >= 247 && mx < 285) { scaleHighlight = !scaleHighlight; repaint(); }
        // REC
        if (mx >= 291 && mx < 325) { recMode = !recMode; if (recMode) recStep = 0; repaint(); }
        // Quantize
        if (mx >= 328 && mx < 370) {
            if (quantizeDiv == 4) quantizeDiv = 2;
            else if (quantizeDiv == 2) quantizeDiv = 1;
            else if (quantizeDiv == 1) quantizeDiv = 0;
            else quantizeDiv = 4;
            repaint();
        }
        // Close
        if (mx >= getWidth() - 30) { if (onClose) onClose(); }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoKeyboard)
};
