#pragma once
#include <set>
#include <random>
#include <unordered_map>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Colours.h"
#include "StepGrid.h"
#include "RatchetRow.h"
#include "LEDMeter.h"
#include "KnobComponent.h"
#include "../../Sequencer/TrackState.h"
#include "../../Audio/Analysis/SampleAnalysis.h"
#include "EngineIcons.h"
#include "WaveformOverlay.h"
#include "WavetableEditor.h"
#include "../../Audio/FX/LFOEngine.h"
#include "MSEGEditor.h"
#include "ArpEditor.h"
#include "PianoKeyboard.h"
#include "../../Audio/FX/HarmonyGenerator.h"
#include "../../State/PresetManager.h"
#include <functional>
#include <random>

static const char* const SYNTH_NOTE_NAMES[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// Custom step button that shows note names for synth tracks
class SynthStepButton : public juce::Component
{
public:
    SynthStepButton (int idx) : index (idx) { setRepaintsOnMouseActivity (true); }

    int getIndex() const { return index; }

    void setStepData (StepData* data) { stepData = data; repaint(); }
    void setPlaying (bool p) { if (isPlaying != p) { isPlaying = p; repaint(); } }
    void setPlockModePtr (bool* ptr) { plockModePtr = ptr; }
    void setPlockSelected (bool sel) { plockSelected = sel; repaint(); }
    void setSampleSlotNames (const std::vector<juce::String>* names) { slotNames = names; }

    std::function<void(int)> onToggle;
    std::function<void(int)> onNoteChange;
    std::function<void(int)> onPlockSelect;
    std::function<void(int)> onPlockRecToggle;
    std::function<void(juce::Point<int>, int)> onPaintDrag;
    std::function<void()> onPaintEnd;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        bool active = stepData != nullptr && stepData->active;
        bool isTrigless = stepData != nullptr && stepData->trigless && active;

        // Background
        juce::Colour bg = Colours_GB::ledOff;
        if ((plockModePtr != nullptr && *plockModePtr) && !active)
            bg = Colours_GB::accentDim.withAlpha (0.3f); // slight tint in PLK mode
        if (isTrigless)
        {
            // Trigless: dark magenta/purple tint — distinctly different from normal steps
            bg = juce::Colour (0xff140620);
        }
        else if (active)
        {
            float nv = stepData->velocity / 127.0f;
            bg = Colours_GB::blueDim.interpolatedWith (Colours_GB::blue.withAlpha (0.6f), nv);
            if ((plockModePtr != nullptr && *plockModePtr))
                bg = bg.brighter (0.15f);
        }
        if (isPlaying && active)
            bg = isTrigless ? Colours_GB::purple : Colours_GB::blue.brighter (0.2f);
        else if (isPlaying)
            bg = Colours_GB::ledOff.brighter (0.1f);

        g.setColour (bg);
        g.fillRoundedRectangle (b, 3.0f);

        // Border
        auto bc = active ? (isTrigless ? Colours_GB::purple : Colours_GB::blue.withAlpha (0.6f)) : Colours_GB::border.withAlpha (0.5f);
        if (isPlaying) bc = Colours_GB::accentBright;
        g.setColour (bc);
        g.drawRoundedRectangle (b, 3.0f, isPlaying ? 1.5f : 0.8f);

        // P-lock selection highlight (strong visible outline + background tint)
        if (plockSelected && active)
        {
            bool blink = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
            g.setColour (blink ? juce::Colour (0x30ffcc00) : juce::Colour (0x2000ffdd));
            g.fillRoundedRectangle (b, 2.5f);
            g.setColour (blink ? juce::Colour (0xffffc800) : juce::Colour (0xff00ffd0));
            g.drawRoundedRectangle (b.reduced (0.5f), 2.5f, 2.5f);
        }

        // ── Trigless step: show p-lock dots but no note name ──
        if (isTrigless)
        {
            // "TL" label
            g.setColour (juce::Colour (0xff9060d0));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawText ("TL", b.toNearestInt(), juce::Justification::centred);

            // P-lock indicator
            if (stepData && !stepData->plocks.empty())
            {
                g.setColour (juce::Colour (0xffc080ff));
                g.fillEllipse (b.getX() + 1.5f, b.getBottom() - 5.5f, 4.0f, 4.0f);
            }
        }
        // ── Normal active step ──
        else if (active && stepData != nullptr)
        {
            int ni = stepData->noteIndex % 12;
            g.setColour (juce::Colour (0xff7ab8ff));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            auto noteStr = juce::String (SYNTH_NOTE_NAMES[ni]) + juce::String (stepData->octave);
            g.drawText (noteStr, b.toNearestInt(), juce::Justification::centred);

            // Velocity bar at bottom
            float velNorm = stepData->velocity / 127.0f;
            g.setColour (Colours_GB::blue);
            g.fillRoundedRectangle (b.getX(), b.getBottom() - 2.5f,
                                     b.getWidth() * velNorm, 2.5f, 1.0f);

            // Gate length bar at top (green / amber if > 100%)
            float gateNorm = std::min (1.0f, stepData->gate / 100.0f);
            auto gateCol = stepData->gate > 100 ? Colours_GB::accent : Colours_GB::green;
            g.setColour (gateCol.withAlpha (0.7f));
            g.fillRoundedRectangle (b.getX(), b.getY(),
                                     b.getWidth() * gateNorm, 2.5f, 1.0f);

            // Velocity text when dragging
            if (velDragMode)
            {
                g.setColour (juce::Colours::white);
                g.setFont (8.0f);
                g.drawText (juce::String (stepData->velocity),
                            b.toNearestInt(), juce::Justification::centred);
            }

            // Trig condition indicator (purple dot top-right)
            if (stepData->cond != TrigCondition::Always)
            {
                g.setColour (juce::Colour (0xffa040ff));
                g.fillEllipse (b.getRight() - 6.0f, b.getY() + 1.5f, 4.0f, 4.0f);
            }

            // Slide indicator (cyan dot top-left)
            if (stepData->slide)
            {
                g.setColour (Colours_GB::cyan);
                g.fillEllipse (b.getX() + 1.5f, b.getY() + 1.5f, 4.0f, 4.0f);
            }

            // P-lock indicator (amber dot bottom-left)
            if (!stepData->plocks.empty())
            {
                g.setColour (Colours_GB::accent);
                g.fillEllipse (b.getX() + 1.5f, b.getBottom() - 5.5f, 4.0f, 4.0f);
            }
            // Sample slot indicator (orange dot top-right)
            if (stepData->sampleSlot >= 0)
            {
                g.setColour (juce::Colour (0xffe07030));
                g.fillEllipse (b.getRight() - 5.5f, b.getY() + 1.5f, 3.5f, 3.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (stepData == nullptr) return;
        wasActive = stepData->active;
        hasDragged = false;
        paintMode = false;
        velDragMode = false;
        gateDragMode = false;
        rightClicked = false;
        plockClicked = false;

        if (e.mods.isRightButtonDown())
        {
            rightClicked = true;
            showContextMenu();
            return;
        }
        if (e.mods.isLeftButtonDown())
        {
            if ((plockModePtr != nullptr && *plockModePtr) && stepData->active)
            {
                plockClicked = true;
                if (onPlockSelect) onPlockSelect (index);
                return;
            }
            dragStartY = e.getScreenY();
            dragStartX = e.getScreenX();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if ((plockModePtr != nullptr && *plockModePtr)) return;

        int dy = -(e.getScreenY() - dragStartY);
        int rawDx = e.getDistanceFromDragStartX();
        int signedDx = e.getScreenX() - dragStartX;

        if (!paintMode && !velDragMode && !gateDragMode && e.getDistanceFromDragStart() > 6)
        {
            hasDragged = true;
            if (wasActive && std::abs (dy) > std::abs (rawDx) && stepData != nullptr)
            {
                velDragMode = true;
                startVel = stepData->velocity;
            }
            else if (wasActive && signedDx < -4 && stepData != nullptr)
            {
                gateDragMode = true;
                startGate = stepData->gate;
            }
            else if (signedDx > 4)
            {
                paintMode = true;
                if (!wasActive && stepData != nullptr)
                {
                    stepData->reset();
                    stepData->active = true;
                    stepData->noteIndex = 0;
                    stepData->octave = 3;
                    if (onToggle) onToggle (index);
                }
            }
        }

        if (velDragMode && stepData != nullptr)
        {
            int newVel = juce::jlimit (1, 127, startVel + static_cast<int>(dy * 1.5f));
            stepData->velocity = static_cast<uint8_t>(newVel);
            repaint();
        }

        if (gateDragMode && stepData != nullptr)
        {
            int newGate = juce::jlimit (5, 200, startGate + static_cast<int>(signedDx * 1.5f));
            stepData->gate = static_cast<uint8_t>(std::min (200, newGate));
            repaint();
        }

        if (paintMode)
        {
            paintVelocity = juce::jlimit (1, 127, 100 + static_cast<int>(dy * 1.5f));
            if (onPaintDrag)
                onPaintDrag (e.getEventRelativeTo (getParentComponent()).getPosition(), paintVelocity);
        }
    }

    void mouseUp (const juce::MouseEvent& ev) override
    {
        if (rightClicked) return;
        if (plockClicked) return;
        if ((plockModePtr != nullptr && *plockModePtr)) return;

        if (!hasDragged && stepData != nullptr)
        {
            // Shift+click: toggle trigless
            if (ev.mods.isShiftDown())
            {
                if (!stepData->active)
                {
                    // Empty step → create trigless
                    stepData->active = true;
                    stepData->trigless = true;
                }
                else if (!stepData->trigless)
                {
                    // Normal step → convert to trigless (keep plocks)
                    stepData->trigless = true;
                }
                else
                {
                    // Trigless → deactivate
                    stepData->trigless = false;
                    stepData->active = false;
                }
                if (onToggle) onToggle (index);
            }
            else
            {
                // Normal click: toggle on/off
                if (stepData->trigless && stepData->active)
                {
                    // Click on trigless → convert to normal active
                    stepData->trigless = false;
                }
                else if (!wasActive)
                {
                    stepData->reset();
                    stepData->active = true;
                    stepData->noteIndex = 0;
                    stepData->octave = 3;
                }
                else
                {
                    stepData->reset();
                }
                if (onToggle) onToggle (index);
            }
        }

        if (paintMode && onPaintEnd)
            onPaintEnd();

        paintMode = false;
        velDragMode = false;
        gateDragMode = false;
        repaint();
    }

    // Scroll wheel = change octave (guard against trackpad right-click generating scroll)
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        // Don't change notes — forward to parent for Viewport scrolling
        if (auto* p = getParentComponent())
            p->mouseWheelMove (e.getEventRelativeTo (p), w);
    }

    bool* plockModePtr = nullptr;
    int* trackChordMode = nullptr;
    int* trackChordInversion = nullptr;
    int* trackChordVoicing = nullptr;
    const std::vector<juce::String>* slotNames = nullptr;

private:
    int index;
    StepData* stepData = nullptr;
    bool isPlaying = false;
    bool plockSelected = false;
    int dragStartY = 0;
    int dragStartX = 0;
    int paintVelocity = 100;
    bool hasDragged = false;
    bool wasActive = false;
    bool paintMode = false;
    bool rightClicked = false;
    bool plockClicked = false;
    bool velDragMode = false;
    bool gateDragMode = false;
    int startVel = 100;
    int startGate = 100;
    static inline StepData s_synthStepClip;
    static inline bool s_synthStepClipValid = false;

    void showContextMenu()
    {
        juce::PopupMenu noteMenu;
        for (int n = 0; n < 12; ++n)
            noteMenu.addItem (100 + n, SYNTH_NOTE_NAMES[n], true, stepData && (stepData->noteIndex % 12) == n);

        juce::PopupMenu octMenu;
        for (int o = 0; o <= 8; ++o)
            octMenu.addItem (200 + o, juce::String (o), true, stepData && stepData->octave == o);

        juce::PopupMenu trigMenu;
        trigMenu.addItem (1,  "ALWAYS", true, stepData && stepData->cond == TrigCondition::Always);
        trigMenu.addSeparator();
        trigMenu.addItem (2,  "50%",    true, stepData && stepData->cond == TrigCondition::P50);
        trigMenu.addItem (3,  "75%",    true, stepData && stepData->cond == TrigCondition::P75);
        trigMenu.addItem (4,  "25%",    true, stepData && stepData->cond == TrigCondition::P25);
        trigMenu.addItem (5,  "12%",    true, stepData && stepData->cond == TrigCondition::P12);
        trigMenu.addSeparator();
        trigMenu.addItem (6,  "1of2", true, stepData && stepData->cond == TrigCondition::OneOf2);
        trigMenu.addItem (7,  "2of2", true, stepData && stepData->cond == TrigCondition::TwoOf2);
        trigMenu.addItem (8,  "1of3", true, stepData && stepData->cond == TrigCondition::OneOf3);
        trigMenu.addItem (9,  "2of3", true, stepData && stepData->cond == TrigCondition::TwoOf3);
        trigMenu.addItem (10, "3of3", true, stepData && stepData->cond == TrigCondition::ThreeOf3);
        trigMenu.addItem (11, "1of4", true, stepData && stepData->cond == TrigCondition::OneOf4);
        trigMenu.addItem (15, "FILL",    true, stepData && stepData->cond == TrigCondition::Fill);
        trigMenu.addItem (16, "NOT FILL",true, stepData && stepData->cond == TrigCondition::NotFill);

        juce::PopupMenu nudgeMenu;
        nudgeMenu.addItem (300, "<<< -50%", true, stepData && stepData->nudge == -50);
        nudgeMenu.addItem (301, "<< -25%",  true, stepData && stepData->nudge == -25);
        nudgeMenu.addItem (302, "< -10%",   true, stepData && stepData->nudge == -10);
        nudgeMenu.addItem (303, "CENTER 0",  true, stepData && stepData->nudge == 0);
        nudgeMenu.addItem (304, "> +10%",   true, stepData && stepData->nudge == 10);
        nudgeMenu.addItem (305, ">> +25%",  true, stepData && stepData->nudge == 25);
        nudgeMenu.addItem (306, ">>> +50%", true, stepData && stepData->nudge == 50);

        juce::PopupMenu lenMenu;
        lenMenu.addItem (400, "1 step",    true, stepData && stepData->noteLen == 1);
        lenMenu.addItem (401, "2 steps",   true, stepData && stepData->noteLen == 2);
        lenMenu.addItem (402, "3 steps",   true, stepData && stepData->noteLen == 3);
        lenMenu.addItem (403, "4 steps",   true, stepData && stepData->noteLen == 4);
        lenMenu.addItem (404, "6 steps",   true, stepData && stepData->noteLen == 6);
        lenMenu.addItem (405, "8 steps",   true, stepData && stepData->noteLen == 8);
        lenMenu.addItem (406, "12 steps",  true, stepData && stepData->noteLen == 12);
        lenMenu.addItem (407, "16 steps",  true, stepData && stepData->noteLen == 16);
        lenMenu.addItem (408, "24 steps",  true, stepData && stepData->noteLen == 24);
        lenMenu.addItem (409, "32 steps",  true, stepData && stepData->noteLen == 32);
        lenMenu.addItem (410, "48 steps",  true, stepData && stepData->noteLen == 48);
        lenMenu.addItem (411, "64 steps",  true, stepData && stepData->noteLen == 64);
        lenMenu.addItem (412, "96 steps",  true, stepData && stepData->noteLen == 96);
        lenMenu.addItem (413, "128 steps", true, stepData && stepData->noteLen == 128);

        juce::PopupMenu menu;
        menu.addSubMenu ("NOTE", noteMenu);
        menu.addSubMenu ("OCTAVE", octMenu);
        menu.addSeparator();
        menu.addSubMenu ("TRIG COND", trigMenu);
        menu.addSubMenu ("NUDGE", nudgeMenu);
        menu.addSubMenu ("NOTE LEN", lenMenu);
        menu.addSeparator();
        menu.addItem (700, stepData && stepData->slide ? "SLIDE: ON" : "SLIDE: OFF", true, stepData && stepData->slide);
        menu.addItem (710, stepData && stepData->trigless ? "TRIGLESS: ON" : "TRIGLESS: OFF", true, stepData && stepData->trigless);

        // Chord submenu (per-step) — 24 types + inversions + voicing
        juce::PopupMenu chordMenu;
        int curChord = stepData ? static_cast<int>(stepData->chordMode) : -1;
        chordMenu.addItem (900, "TRK", true, curChord < 0);
        for (int c = 0; c <= SequencerEngine::kNumChordTypes; ++c)
            chordMenu.addItem (901 + c, SequencerEngine::chordName (c), true, curChord == c);
        // Inversion submenu (per-step, -1=track default)
        juce::PopupMenu invMenu;
        static const char* invNames[] = {"ROOT","1st","2nd","3rd"};
        int curInv = stepData ? static_cast<int>(stepData->chordInversion) : -1;
        invMenu.addItem (950, "TRK", true, curInv < 0);
        for (int i = 0; i < 4; ++i)
            invMenu.addItem (951 + i, invNames[i], true, curInv == i);
        chordMenu.addSeparator();
        juce::String invLabel = (curInv >= 0) ? juce::String (invNames[std::clamp (curInv, 0, 3)]) : "TRK";
        chordMenu.addSubMenu ("INV: " + invLabel, invMenu);
        // Voicing submenu (per-step, -1=track default)
        juce::PopupMenu voiceMenu;
        static const char* voiceNames[] = {"CLOSE","DROP2","SPREAD","OPEN"};
        int curVce = stepData ? static_cast<int>(stepData->chordVoicing) : -1;
        voiceMenu.addItem (960, "TRK", true, curVce < 0);
        for (int v = 0; v < 4; ++v)
            voiceMenu.addItem (961 + v, voiceNames[v], true, curVce == v);
        juce::String vceLabel = (curVce >= 0) ? juce::String (voiceNames[std::clamp (curVce, 0, 3)]) : "TRK";
        chordMenu.addSubMenu ("VOICE: " + vceLabel, voiceMenu);
        // Strum submenu — 0-200% in steps of 10 (extended range for dramatic strums)
        juce::PopupMenu strumMenu;
        int curStrum = stepData ? static_cast<int>(stepData->strum) : 0;
        strumMenu.addItem (970, "OFF", true, curStrum == 0);
        for (int sv = 10; sv <= 200; sv += 10)
            strumMenu.addItem (970 + sv, juce::String (sv) + "%", true, curStrum == sv);
        chordMenu.addSubMenu ("STRUM: " + (curStrum > 0 ? juce::String (curStrum) + "%" : juce::String ("OFF")), strumMenu);
        menu.addSubMenu ("CHORD", chordMenu);

        // Copy/paste step
        menu.addSeparator();
        bool active = stepData && stepData->active;
        if (active) menu.addItem (800, "COPY STEP");
        menu.addItem (801, "PASTE STEP", s_synthStepClipValid);
        menu.addItem (850, "RANDOMIZE STEP");

        // ── Sample slot selector (Octatrack-style per-step sample) ──
        if (active && slotNames != nullptr && !slotNames->empty())
        {
            juce::PopupMenu slotMenu;
            slotMenu.addItem (1200, "DEFAULT", true, stepData->sampleSlot < 0);
            for (int si = 0; si < static_cast<int>(slotNames->size()); ++si)
                slotMenu.addItem (1201 + si, (*slotNames)[static_cast<size_t>(si)],
                                  true, stepData->sampleSlot == si);
            menu.addSubMenu ("SAMPLE SLOT" + juce::String (stepData->sampleSlot >= 0
                ? " [" + (*slotNames)[static_cast<size_t>(std::clamp (static_cast<int>(stepData->sampleSlot), 0,
                    static_cast<int>(slotNames->size()) - 1))] + "]" : ""), slotMenu);
        }

        std::vector<std::string> plockKeysInMenu;
        if (active && stepData && !stepData->plocks.empty())
        {
            juce::PopupMenu plockMenu;
            int plockIdx = 600;
            for (const auto& [key, val] : stepData->plocks)
            {
                plockMenu.addItem (plockIdx, juce::String ("x  ") + juce::String (key) + " = " + juce::String (val, 2));
                plockKeysInMenu.push_back (key);
                ++plockIdx;
            }
            plockMenu.addSeparator();
            plockMenu.addItem (699, "CLEAR ALL P-LOCKS");
            menu.addSubMenu ("P-LOCKS (" + juce::String (static_cast<int>(stepData->plocks.size())) + ")", plockMenu);
        }
        if (active)
        {
            menu.addSeparator();
            bool isRecording = plockModePtr != nullptr && *plockModePtr && plockSelected;
            menu.addItem (990, isRecording ? "STOP P-LOCK REC" : "P-LOCK REC");
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this, plockKeysInMenu = std::move(plockKeysInMenu)](int result) {
                if (result == 0 || stepData == nullptr) return;
                // Individual p-lock removal
                if (result >= 600 && result < 600 + static_cast<int>(plockKeysInMenu.size()))
                {
                    stepData->plocks.erase (plockKeysInMenu[static_cast<size_t>(result - 600)]);
                    repaint();
                    return;
                }
                // P-LOCK REC toggle
                if (result == 990)
                {
                    if (onPlockRecToggle) onPlockRecToggle (index);
                    repaint();
                    return;
                }
                if (result >= 100 && result < 112)
                {
                    stepData->noteIndex = static_cast<uint8_t>(result - 100);
                    if (onNoteChange) onNoteChange (index);
                }
                else if (result >= 200 && result <= 208)
                {
                    stepData->octave = static_cast<uint8_t>(result - 200);
                    if (onNoteChange) onNoteChange (index);
                }
                else if (result >= 1 && result <= 16)
                {
                    TrigCondition conds[] = {
                        TrigCondition::Always, TrigCondition::P50, TrigCondition::P75,
                        TrigCondition::P25, TrigCondition::P12,
                        TrigCondition::OneOf2, TrigCondition::TwoOf2,
                        TrigCondition::OneOf3, TrigCondition::TwoOf3, TrigCondition::ThreeOf3,
                        TrigCondition::OneOf4, TrigCondition::TwoOf4, TrigCondition::ThreeOf4, TrigCondition::FourOf4,
                        TrigCondition::Fill, TrigCondition::NotFill
                    };
                    if (result <= 16) stepData->cond = conds[result - 1];
                }
                else if (result >= 300 && result <= 306)
                {
                    int nudgeVals[] = { -50, -25, -10, 0, 10, 25, 50 };
                    stepData->nudge = static_cast<int8_t>(nudgeVals[result - 300]);
                }
                else if (result >= 400 && result <= 413)
                {
                    int lenVals[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128 };
                    stepData->noteLen = static_cast<uint8_t>(lenVals[result - 400]);
                }
                else if (result == 700) { stepData->slide = !stepData->slide; }
                else if (result == 710)
                {
                    stepData->trigless = !stepData->trigless;
                    if (stepData->trigless) stepData->active = true; // trigless implies active
                }
                else if (result == 900)
                {
                    stepData->chordMode = -1; // use track default
                }
                else if (result >= 901 && result <= 901 + SequencerEngine::kNumChordTypes)
                {
                    stepData->chordMode = static_cast<int8_t>(result - 901);
                }
                else if (result == 950)
                {
                    stepData->chordInversion = -1; // use track default
                }
                else if (result >= 951 && result <= 954)
                {
                    stepData->chordInversion = static_cast<int8_t>(result - 951);
                }
                else if (result == 960)
                {
                    stepData->chordVoicing = -1; // use track default
                }
                else if (result >= 961 && result <= 964)
                {
                    stepData->chordVoicing = static_cast<int8_t>(result - 961);
                }
                else if (result >= 970 && result <= 1170)
                {
                    stepData->strum = static_cast<uint8_t>(result - 970);
                }
                else if (result == 699) stepData->plocks.clear();
                // Sample slot selection
                else if (result == 1200) { stepData->sampleSlot = -1; }
                else if (result >= 1201 && result < 1201 + 128) { stepData->sampleSlot = result - 1201; }
                // Copy/paste step
                else if (result == 800)
                {
                    s_synthStepClip = *stepData;
                    s_synthStepClipValid = true;
                }
                else if (result == 801 && s_synthStepClipValid)
                {
                    *stepData = s_synthStepClip;
                    stepData->active = true;
                    if (onNoteChange) onNoteChange (index);
                }
                else if (result == 850)
                {
                    std::mt19937 rng { std::random_device{}() };
                    stepData->active = true;
                    stepData->noteIndex = static_cast<uint8_t>(std::uniform_int_distribution<int>(0, 11)(rng));
                    stepData->octave = static_cast<uint8_t>(std::uniform_int_distribution<int>(2, 6)(rng));
                    stepData->velocity = static_cast<uint8_t>(std::uniform_int_distribution<int>(40, 127)(rng));
                    stepData->gate = static_cast<uint8_t>(std::uniform_int_distribution<int>(30, 150)(rng));
                    stepData->ratchet = static_cast<uint8_t>(std::uniform_int_distribution<int>(1, 3)(rng));
                    int8_t nudges[] = { -25, -10, 0, 0, 0, 0, 10, 25 };
                    stepData->nudge = nudges[std::uniform_int_distribution<int>(0, 7)(rng)];
                    if (onNoteChange) onNoteChange (index);
                }
                repaint();
            });
    }
};

class SynthTrackRow : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    SynthTrackRow (int partIdx, SynthTrackState& trackState)
        : index (partIdx), track (trackState)
    {
        // Track label
        addAndMakeVisible (nameLabel);
        nameLabel.setText ("PT " + juce::String (partIdx + 1), juce::dontSendNotification);
        nameLabel.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 11.0f, juce::Font::bold));
        nameLabel.setColour (juce::Label::textColourId, Colours_GB::blue);
        nameLabel.setJustificationType (juce::Justification::centredRight);

        // Build synth step buttons (not using StepGrid — we need note display)
        for (int i = 0; i < kMaxSteps; ++i)
        {
            auto* btn = new SynthStepButton (i);
            btn->setStepData (&track.seq.steps[static_cast<size_t>(i)]);
            btn->setSampleSlotNames (&track.sampleSlotNames);
            btn->onToggle = [this](int) { fireBeforeStepEdit(); refreshSteps(); if (onStepSync) onStepSync(); };
            btn->onNoteChange = [this](int) { fireBeforeStepEdit(); refreshSteps(); };
            btn->trackChordMode = &track.chordMode;
            btn->trackChordInversion = &track.chordInversion;
            btn->trackChordVoicing = &track.chordVoicing;
            btn->onPlockSelect = [this](int stepIdx) {
                if (!plockMode) return;
                if (!engineOpen)
                {
                    engineOpen = true;
                    expandBtn.setToggleState (true, juce::dontSendNotification);
                    expandBtn.setButtonText ("-");
                    if (onExpandToggle) onExpandToggle();
                }
                if (plockTargetStep == stepIdx)
                {
                    plockMode = false;
                    plockTargetStep = -1;
                    plkBtn.setButtonText ("PLK");
                }
                else
                {
                    plockTargetStep = stepIdx;
                }
                updatePlockHighlight();
            };
            btn->onPlockRecToggle = [this](int stepIdx) {
                // Toggle p-lock recording on this step
                if (plockMode && plockTargetStep == stepIdx)
                {
                    // STOP recording
                    plockMode = false;
                    plockTargetStep = -1;
                }
                else
                {
                    // START recording on this step
                    plockMode = true;
                    plockTargetStep = stepIdx;
                    if (!engineOpen)
                    {
                        engineOpen = true;
                        expandBtn.setToggleState (true, juce::dontSendNotification);
                        expandBtn.setButtonText ("-");
                        if (onExpandToggle) onExpandToggle();
                    }
                }
                plkBtn.setButtonText (plockMode ? "PLK*" : "PLK");
                updatePlockHighlight();
            };
            btn->onPaintDrag = [this](juce::Point<int> pos, int vel) {
                for (auto* b : synthSteps)
                {
                    if (b->isVisible() && b->getBounds().contains (pos))
                    {
                        int si = b->getIndex();
                        auto& sd = track.seq.steps[static_cast<size_t>(si)];
                        if (!sd.active)
                        {
                            sd.active = true;
                            sd.noteIndex = 0;
                            sd.octave = 3;
                        }
                        sd.velocity = static_cast<uint8_t>(std::clamp (vel, 1, 127));
                        b->repaint();
                        break;
                    }
                }
            };
            btn->onPaintEnd = []() {};
            synthSteps.add (btn);
            addChildComponent (btn);
        }
        updateVisibleSteps();

        // Ratchet row
        addAndMakeVisible (ratchetRow);
        ratchetRow.setStepSequence (&track.seq);

        // Page buttons (8 pages for 128 steps)
        for (int p = 0; p < kNumPages; ++p)
        {
            auto* btn = new juce::TextButton (juce::String (p + 1));
            btn->onClick = [this, p]() {
                track.page = p;
                updateVisibleSteps();
                ratchetRow.setPage (p);
                manualPageOverride = true;
                updatePageButtons();
            };
            btn->addMouseListener (this, false); // forward right-click to row
            pageButtons.add (btn);
            addAndMakeVisible (btn);
        }
        updatePageButtons();

        // LEN — editable label
        addAndMakeVisible (lenCaption);
        lenCaption.setText ("LEN", juce::dontSendNotification);
        lenCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        lenCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        lenCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (lenLabel);
        lenLabel.setText (juce::String (track.length), juce::dontSendNotification);
        lenLabel.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        lenLabel.setColour (juce::Label::textColourId, Colours_GB::blue);
        lenLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black);
        lenLabel.setColour (juce::Label::outlineColourId, Colours_GB::border);
        lenLabel.setJustificationType (juce::Justification::centred);
        lenLabel.setEditable (true);
        lenLabel.onTextChange = [this]() {
            int v = lenLabel.getText().getIntValue();
            if (v >= 1 && v <= kMaxSteps) { track.length = v; lenLabel.setText (juce::String (v), juce::dontSendNotification); if (onLinkSync) onLinkSync(); }
            else lenLabel.setText (juce::String (track.length), juce::dontSendNotification);
        };

        // SWG label + slider
        addAndMakeVisible (swingCaption);
        swingCaption.setText ("SWG", juce::dontSendNotification);
        swingCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        swingCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        swingCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (swingSlider);
        swingSlider.setRange (-50, 50, 1);
        swingSlider.setValue (track.swing, juce::dontSendNotification);
        swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        swingSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        swingSlider.setColour (juce::Slider::thumbColourId, Colours_GB::cyan);
        swingSlider.setColour (juce::Slider::trackColourId, Colours_GB::border);
        swingSlider.onValueChange = [this]() {
            track.swing = static_cast<int>(swingSlider.getValue());
            if (onLinkSync) onLinkSync();
        };

        // SPD (clock multiplier)
        addAndMakeVisible (spdCaption);
        spdCaption.setText ("SPD", juce::dontSendNotification);
        spdCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        spdCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        spdCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (spdSelector);
        spdSelector.addItem ("1/8x", 1);
        spdSelector.addItem ("1/4x", 2);
        spdSelector.addItem ("1/3x", 3);
        spdSelector.addItem ("3/8x", 4);
        spdSelector.addItem ("1/2x", 5);
        spdSelector.addItem ("3/4x", 6);
        spdSelector.addItem ("x1", 7);
        spdSelector.addItem ("x3/2", 8);
        spdSelector.addItem ("x2", 9);
        spdSelector.addItem ("x3", 10);
        spdSelector.addItem ("x4", 11);
        spdSelector.addItem ("x6", 12);
        spdSelector.addItem ("x8", 13);
        spdSelector.setSelectedId (7, juce::dontSendNotification);
        spdSelector.onChange = [this]() {
            float vals[] = { 0.125f, 0.25f, 0.333f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f };
            int idx = spdSelector.getSelectedId() - 1;
            if (idx >= 0 && idx < 13) { track.clockMul = vals[idx]; if (onLinkSync) onLinkSync(); }
        };

        // RND (randomize) button
        addAndMakeVisible (randBtn);
        randBtn.setButtonText ("RND");
        randBtn.onClick = [this]() { showRandomizeMenu(); };

        // INIT (selective reset) button
        addAndMakeVisible (initBtn);
        initBtn.setButtonText ("INI");
        initBtn.onClick = [this]() { showInitMenu(); };

        // Copy track
        addAndMakeVisible (copyBtn);
        copyBtn.setButtonText ("CP");
        copyBtn.onClick = [this]() {
            s_synthClipboard = track.seq;
            s_synthClipboardValid = true;
        };

        // Paste track
        addAndMakeVisible (pasteBtn);
        pasteBtn.setButtonText ("PST");
        pasteBtn.onClick = [this]() {
            if (s_synthClipboardValid)
            {
                track.seq = s_synthClipboard;
                refreshSteps();
                ratchetRow.setStepSequence (&track.seq);
            }
        };

        // Link group button (OFF→A→B→C→D)
        addAndMakeVisible (linkBtn);
        linkBtn.setButtonText ("L");
        linkBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1e28));
        linkBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff606870));
        linkBtn.onClick = [this]() {
            int oldGroup = track.linkGroup;
            track.linkGroup = (track.linkGroup + 1) % 5;

            if (oldGroup == 0 && track.linkGroup > 0)
            {
                track.preLinkedSeq = track.seq;
                track.preLinkedLength = track.length;
                track.preLinkedSwing = track.swing;
                track.preLinkedClockMul = track.clockMul;
                track.preLinkedPlayDir = track.playDir;
                track.preLinkedVolume = track.volume;
            }
            else if (oldGroup > 0 && track.linkGroup == 0)
            {
                track.seq = track.preLinkedSeq;
                track.length = track.preLinkedLength;
                track.swing = track.preLinkedSwing;
                track.clockMul = track.preLinkedClockMul;
                track.playDir = track.preLinkedPlayDir;
                track.volume = track.preLinkedVolume;
                lenLabel.setText (juce::String (track.length), juce::dontSendNotification);
                swingSlider.setValue (track.swing, juce::dontSendNotification);
                volSlider.setValue (track.volume, juce::dontSendNotification);
                int spdId = clockMulToSpdId (track.clockMul);
                spdSelector.setSelectedId (spdId, juce::dontSendNotification);
                refreshSteps();
            }

            static const char* labels[] = {"L","A","B","C","D"};
            static const juce::Colour cols[] = {
                juce::Colour(0xff606870), juce::Colour(0xff40c8e0),
                juce::Colour(0xfff0a020), juce::Colour(0xff60d068),
                juce::Colour(0xffc060d0)
            };
            linkBtn.setButtonText (labels[track.linkGroup]);
            linkBtn.setColour (juce::TextButton::textColourOffId, cols[track.linkGroup]);
            linkBtn.setColour (juce::TextButton::buttonColourId,
                track.linkGroup > 0 ? cols[track.linkGroup].withAlpha(0.15f) : juce::Colour(0xff1a1e28));
            if (onLinkSync) onLinkSync();
            refreshSteps();
        };

        // Mute / Solo / Vol
        addAndMakeVisible (muteBtn);
        muteBtn.setButtonText ("M");
        muteBtn.setClickingTogglesState (true);
        muteBtn.onClick = [this]() { track.muted = muteBtn.getToggleState(); if (onLinkSync) onLinkSync(); };

        addAndMakeVisible (soloBtn);
        soloBtn.setButtonText ("S");
        soloBtn.setClickingTogglesState (true);
        soloBtn.onClick = [this]() { track.solo = soloBtn.getToggleState(); if (onLinkSync) onLinkSync(); };

        addAndMakeVisible (volCaption);
        volCaption.setText ("VOL", juce::dontSendNotification);
        volCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        volCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        volCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (volSlider);
        volSlider.setRange (0.0, 1.0, 0.01);
        volSlider.setValue (0.6, juce::dontSendNotification);
        volSlider.setSliderStyle (juce::Slider::LinearVertical);
        volSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volSlider.onValueChange = [this]() {
            track.volume = static_cast<float>(volSlider.getValue());
            if (onLinkSync) onLinkSync();
        };

        // Expand
        addAndMakeVisible (expandBtn);
        expandBtn.setVisible (false); // hidden — click on name label instead
        expandBtn.setClickingTogglesState (true);
        expandBtn.onClick = [this]() {
            engineOpen = expandBtn.getToggleState();
            expandBtn.setButtonText (engineOpen ? "-" : "+");
            if (onExpandToggle) onExpandToggle();
        };

        // Click on name label to toggle expand
        nameLabel.setInterceptsMouseClicks (true, false);
        nameLabel.addMouseListener (this, false);

        // PLK (parameter lock) button
        addAndMakeVisible (plkBtn);
        plkBtn.setButtonText ("PLK");
        plkBtn.onClick = [this]() {
            plockMode = !plockMode;
            plkBtn.setButtonText (plockMode ? "PLK*" : "PLK");
            if (!plockMode)
            {
                plockTargetStep = -1;
                updatePlockHighlight();
            }
        };

        // Pass plockMode pointer to all synth step buttons
        for (auto* btn : synthSteps)
            btn->setPlockModePtr (&plockMode);

        // ANA/FM model switch — vertical engine selector buttons
        addAndMakeVisible (fmBtn); fmBtn.setVisible (false); // keep hidden, replaced by engSelBtns
        {
            const SynthModel models[] = {SynthModel::Analog, SynthModel::FM, SynthModel::DWGS, SynthModel::Formant, SynthModel::Sampler, SynthModel::Wavetable, SynthModel::Granular};
            const char* labels[] = {"ANA", "FM", "DWG", "FMT", "SMP", "WT", "GRN"};
            screenMode = 0;
            for (int mi = 0; mi < 7; ++mi)
                if (track.model == models[mi]) { screenMode = mi; break; }
            for (int mi = 0; mi < 7; ++mi)
            {
                auto& btn = engSelBtns[static_cast<size_t>(mi)];
                btn.setButtonText (labels[mi]);
                btn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                addAndMakeVisible (btn);
                btn.onClick = [this, mi]() {
                    screenMode = mi;
                    const SynthModel mdls[] = {SynthModel::Analog, SynthModel::FM, SynthModel::DWGS, SynthModel::Formant, SynthModel::Sampler, SynthModel::Wavetable, SynthModel::Granular};
                    track.model = mdls[mi];
                    buildEngineKnobs();
                    bool isSmp = (screenMode == 4);
                    bool isWT  = (screenMode == 5);
                    bool isGrn = (screenMode == 6);
                    loadSampleBtn.setVisible (isSmp || isGrn);
                    smpPrevBtn.setVisible (isSmp || isGrn);
                    smpNextBtn.setVisible (isSmp || isGrn);
                    zoomInBtn.setVisible (isSmp);
                    zoomOutBtn.setVisible (isSmp);
                    resampleSrcBox.setVisible (isGrn);
                    resampleRecBtn.setVisible (isGrn);
                    resampleSyncBtn.setVisible (isGrn);
                    if (isWT && !track.wtData1)
                    {
                        track.wtData1 = std::make_shared<WavetableData>(WavetableData::createBasic());
                        track.wtData2 = std::make_shared<WavetableData>(WavetableData::createBasic());
                    }
                    wtLoadBtn1.setVisible (isWT);
                    wtLoadBtn2.setVisible (isWT);
                    wtPrevBtn.setVisible (isWT);
                    wtNextBtn.setVisible (isWT);
                    wtEditBtn.setVisible (isWT);
                    resized();
                    repaint();
                };
            }
        }

        buildEngineKnobs();

        // LOAD sample button (only visible in SMP mode)
        addAndMakeVisible (loadSampleBtn);
        loadSampleBtn.setButtonText ("LOAD");
        loadSampleBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2080a0));
        loadSampleBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        loadSampleBtn.onClick = [this]() {
            juce::PopupMenu loadMenu;
            loadMenu.addItem (1, "Load File");
            loadMenu.addItem (2, "Load Folder");
            loadMenu.addItem (4, "Load Slots Folder (P-Lock)");
            loadMenu.addSeparator();
            bool hasSample = (track.sampleData != nullptr && track.sampleData->getNumSamples() > 0);
            loadMenu.addItem (3, "Save Sample as WAV...", hasSample);
            int choice = loadMenu.show();
            if (choice == 1)
            {
                fileChooser = std::make_unique<juce::FileChooser> (
                    "Load Sample", juce::File(), "*.wav;*.aiff;*.aif;*.flac;*.mp3");
                auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
                fileChooser->launchAsync (flags, [this](const juce::FileChooser& fc) {
                    auto results = fc.getResults();
                    if (results.isEmpty()) return;
                    loadSampleFromFile (results.getFirst());
                });
            }
            else if (choice == 2)
            {
                fileChooser = std::make_unique<juce::FileChooser> (
                    "Select Sample Folder", juce::File());
                auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
                fileChooser->launchAsync (flags, [this](const juce::FileChooser& fc) {
                    auto results = fc.getResults();
                    if (results.isEmpty()) return;
                    auto dir = results.getFirst();
                    if (!dir.isDirectory()) return;
                    sampleFolder.clear();
                    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.wav;*.aiff;*.aif;*.flac;*.mp3"))
                        sampleFolder.add (entry.getFile());
                    struct FileSorter { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()); } };
                    FileSorter fs; sampleFolder.sort (fs);
                    if (!sampleFolder.isEmpty())
                    {
                        sampleFolderIdx = 0;
                        loadSampleFromFile (sampleFolder[0]);
                    }
                });
            }
            else if (choice == 3)
            {
                saveSampleToWav();
            }
            else if (choice == 4)
            {
                fileChooser = std::make_unique<juce::FileChooser> (
                    "Select Slots Folder", juce::File());
                auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
                fileChooser->launchAsync (flags, [this](const juce::FileChooser& fc) {
                    auto results = fc.getResults();
                    if (results.isEmpty()) return;
                    auto dir = results.getFirst();
                    if (dir.isDirectory())
                        loadSampleFolder (dir);
                });
            }
        };
        loadSampleBtn.setVisible (screenMode == 4 || screenMode == 6);

        // Prev/Next sample buttons
        addAndMakeVisible (smpPrevBtn);
        smpPrevBtn.setButtonText ("<");
        smpPrevBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        smpPrevBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff40b0b0));
        smpPrevBtn.onClick = [this]() { browseSample (-1); };
        smpPrevBtn.setVisible (screenMode == 4 || screenMode == 6);

        addAndMakeVisible (smpNextBtn);
        smpNextBtn.setButtonText (">");
        smpNextBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        smpNextBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff40b0b0));
        smpNextBtn.onClick = [this]() { browseSample (1); };
        smpNextBtn.setVisible (screenMode == 4 || screenMode == 6);

        // ── Resample controls (Granular only) ──
        addAndMakeVisible (resampleSrcBox);
        resampleSrcBox.addItem ("OFF", 1);
        for (int d = 0; d < 10; ++d) resampleSrcBox.addItem ("D" + juce::String (d + 1), d + 2);
        for (int s = 0; s < 5; ++s) resampleSrcBox.addItem ("S" + juce::String (s + 1), s + 12);
        resampleSrcBox.addItem ("MST", 17);
        resampleSrcBox.setSelectedId (1, juce::dontSendNotification);
        resampleSrcBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff252a35));
        resampleSrcBox.setColour (juce::ComboBox::textColourId, juce::Colour (0xffcc3030));
        resampleSrcBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff553030));
        resampleSrcBox.onChange = [this]() {
            if (resampleSrcPtr == nullptr) return;
            int sel = resampleSrcBox.getSelectedId();
            resampleSrcPtr->store (sel <= 1 ? -1 : sel - 2);  // OFF=-1, D1=0..D10=9, S1=10..S5=14, MST=15
        };
        resampleSrcBox.setVisible (screenMode == 6);

        addAndMakeVisible (resampleRecBtn);
        resampleRecBtn.setButtonText ("REC");
        resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc3030));
        resampleRecBtn.onClick = [this]() {
            if (resampleActivePtr == nullptr || resampleTargetPtr == nullptr) return;

            bool syncOn = (resampleTransSyncPtr != nullptr && resampleTransSyncPtr->load());

            if (syncOn)
            {
                // ── Transport sync mode: ARM / DISARM ──
                if (resampleArmedPtr == nullptr) return;
                bool wasArmed = resampleArmedPtr->load();
                if (!wasArmed)
                {
                    // Arm: set target + source, wait for transport
                    resampleTargetPtr->store (index);
                    resampleArmedPtr->store (true);
                    resampleRecBtn.setButtonText ("ARM");
                    resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffcc8800));
                    resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                }
                else
                {
                    // Disarm (and stop if recording)
                    resampleArmedPtr->store (false);
                    if (resampleActivePtr->load())
                        resampleActivePtr->store (false);
                    resampleRecBtn.setButtonText ("REC");
                    resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
                    resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc3030));
                }
            }
            else
            {
                // ── Immediate mode: START / STOP ──
                bool wasActive = resampleActivePtr->load();
                if (!wasActive)
                {
                    resampleTargetPtr->store (index);
                    resampleActivePtr->store (true);
                    resampleRecBtn.setButtonText ("STOP");
                    resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffcc3030));
                    resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                }
                else
                {
                    resampleActivePtr->store (false);
                    resampleRecBtn.setButtonText ("REC");
                    resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
                    resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc3030));
                }
            }
        };
        resampleRecBtn.setVisible (screenMode == 6);

        // ── SYNC toggle (transport sync for resample) ──
        addAndMakeVisible (resampleSyncBtn);
        resampleSyncBtn.setButtonText ("SYNC");
        resampleSyncBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        resampleSyncBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff808080));
        resampleSyncBtn.onClick = [this]() {
            if (resampleTransSyncPtr == nullptr) return;
            bool on = !resampleTransSyncPtr->load();
            resampleTransSyncPtr->store (on);
            // Visual toggle
            resampleSyncBtn.setColour (juce::TextButton::buttonColourId,
                on ? juce::Colour (0xff206030) : juce::Colour (0xff252a35));
            resampleSyncBtn.setColour (juce::TextButton::textColourOffId,
                on ? juce::Colour (0xff40ff80) : juce::Colour (0xff808080));
            // Reset arm/rec state when toggling mode
            if (resampleArmedPtr) resampleArmedPtr->store (false);
            if (resampleActivePtr && resampleActivePtr->load())
                resampleActivePtr->store (false);
            resampleRecBtn.setButtonText ("REC");
            resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
            resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc3030));
        };
        resampleSyncBtn.setVisible (screenMode == 6);

        addAndMakeVisible (zoomInBtn);
        zoomInBtn.setButtonText ("+");
        zoomInBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        zoomInBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff60c0c0));
        zoomInBtn.onClick = [this]() {
            wfZoom = std::min (16.0f, wfZoom * 1.5f);
            float vis = 1.0f / wfZoom;
            wfOffset = std::clamp (wfOffset, 0.0f, 1.0f - vis);
            repaint();
        };
        zoomInBtn.setVisible (screenMode == 4);

        addAndMakeVisible (zoomOutBtn);
        zoomOutBtn.setButtonText ("-");
        zoomOutBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        zoomOutBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff60c0c0));
        zoomOutBtn.onClick = [this]() {
            wfZoom = std::max (1.0f, wfZoom / 1.5f);
            float vis = 1.0f / wfZoom;
            wfOffset = std::clamp (wfOffset, 0.0f, 1.0f - vis);
            repaint();
        };
        zoomOutBtn.setVisible (screenMode == 4);

        // ── Wavetable buttons ──
        auto wtBtnCol = juce::Colour (0xff205868);
        addAndMakeVisible (wtLoadBtn1);
        wtLoadBtn1.setButtonText ("WT1");
        wtLoadBtn1.setColour (juce::TextButton::buttonColourId, wtBtnCol);
        wtLoadBtn1.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff50c0e0));
        wtLoadBtn1.onClick = [this]() {
            juce::PopupMenu menu;
            menu.addItem (1, "Select OSC 1");
            menu.addSeparator();
            menu.addItem (2, "Load WT File...");
            menu.addItem (3, "Load WT Folder...");
            int ch = menu.show();
            if (ch == 1) {
                wtLoadTarget = 0;
                if (wtEditorEmbed && track.wtData1) wtEditorEmbed->setData (*track.wtData1);
                wtLoadBtn1.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff405060));
                wtLoadBtn2.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
            } else if (ch == 2) { wtLoadTarget = 0; browseWavetableFile(); }
            else if (ch == 3) { wtLoadTarget = 0; browseWavetableFolderDialog(); }
            repaint();
        };
        wtLoadBtn1.setVisible (screenMode == 5);

        addAndMakeVisible (wtLoadBtn2);
        wtLoadBtn2.setButtonText ("WT2");
        wtLoadBtn2.setColour (juce::TextButton::buttonColourId, wtBtnCol);
        wtLoadBtn2.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff50c0e0));
        wtLoadBtn2.onClick = [this]() {
            juce::PopupMenu menu;
            menu.addItem (1, "Select OSC 2");
            menu.addSeparator();
            menu.addItem (2, "Load WT File...");
            menu.addItem (3, "Load WT Folder...");
            int ch = menu.show();
            if (ch == 1) {
                wtLoadTarget = 1;
                if (wtEditorEmbed && track.wtData2) wtEditorEmbed->setData (*track.wtData2);
                wtLoadBtn2.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff405060));
                wtLoadBtn1.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
            } else if (ch == 2) { wtLoadTarget = 1; browseWavetableFile(); }
            else if (ch == 3) { wtLoadTarget = 1; browseWavetableFolderDialog(); }
            repaint();
        };
        wtLoadBtn2.setVisible (screenMode == 5);

        addAndMakeVisible (wtPrevBtn);
        wtPrevBtn.setButtonText ("<");
        wtPrevBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        wtPrevBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff50c0e0));
        wtPrevBtn.onClick = [this]() { browseWavetableFolder (-1); };
        wtPrevBtn.setVisible (screenMode == 5);

        addAndMakeVisible (wtNextBtn);
        wtNextBtn.setButtonText (">");
        wtNextBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        wtNextBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff50c0e0));
        wtNextBtn.onClick = [this]() { browseWavetableFolder (1); };
        wtNextBtn.setVisible (screenMode == 5);

        addAndMakeVisible (wtEditBtn);
        wtEditBtn.setButtonText ("EDIT");
        wtEditBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff304050));
        wtEditBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff50c0e0));
        wtEditBtn.onClick = [this]() { openWavetableEditor(); };
        wtEditBtn.setVisible (screenMode == 5);

        // Tab buttons (ENG | FX | LFO)
        auto setupTab = [this](juce::TextButton& btn, int tabIdx) {
            addAndMakeVisible (btn);
            btn.setClickingTogglesState (false);
            btn.onClick = [this, tabIdx]() {
                currentTab = tabIdx;
                resized();
                repaint();
            };
        };
        setupTab (tabEng, 0);
        setupTab (tabFx, 1);
        setupTab (tabLfo, 2);
        setupTab (tabMseg, 3);
        setupTab (tabVoice, 4);

        // MSEG editor
        msegEditor = std::make_unique<MSEGEditor> (track.msegs[static_cast<size_t>(msegIdx)], msegIdx);
        addChildComponent (*msegEditor);

        // ARP button — opens step arpeggiator popup
        addAndMakeVisible (arpBtn);
        arpBtn.setButtonText ("ARP");
        arpBtn.setColour (juce::TextButton::buttonColourId, Colours_GB::panel3);
        arpBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        arpBtn.onClick = [this]() {
            auto* popup = new ArpEditor();
            popup->setData (track.arp);
            popup->setSize (500, 200);
            popup->onChanged = [this]() {
                // Update button appearance when arp state changes inside popup
                arpBtn.setButtonText (track.arp.enabled ? "ARP*" : "ARP");
                arpBtn.setColour (juce::TextButton::buttonColourId,
                    track.arp.enabled ? Colours_GB::accent : Colours_GB::panel3);
            };
            juce::CallOutBox::launchAsynchronously (
                std::unique_ptr<juce::Component> (popup),
                arpBtn.getScreenBounds(), nullptr);
        };

        // HARMONY button — chord progression generator
        addAndMakeVisible (harmBtn);
        harmBtn.setButtonText ("HARM");
        harmBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a4060));
        harmBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff60c0ff));
        harmBtn.onClick = [this]() { showHarmonyMenu(); };

        // KEYBOARD button — toggle piano keyboard
        addAndMakeVisible (kbBtn);
        kbBtn.setButtonText ("KB");
        kbBtn.setColour (juce::TextButton::buttonColourId, Colours_GB::panel3);
        kbBtn.setColour (juce::TextButton::textColourOffId, Colours_GB::text);
        kbBtn.onClick = [this]() {
            keyboardOpen = !keyboardOpen;
            kbBtn.setColour (juce::TextButton::buttonColourId,
                keyboardOpen ? Colours_GB::accent : Colours_GB::panel3);
            kbBtn.setColour (juce::TextButton::textColourOffId,
                keyboardOpen ? juce::Colour (0xff001820) : Colours_GB::text);
            resized();
            repaint();
        };

        // PianoKeyboard component
        pianoKeyboard.setVisible (false);
        addChildComponent (pianoKeyboard);
        pianoKeyboard.onNoteOn = [this](int midiNote, float vel) {
            // Write to atomic for audio thread pickup
            track.kbNoteOn.store (midiNote);
            track.kbVelocity.store (vel);
            track.kbNoteOff.store (false);

            // REC: write note to sequencer
            if (pianoKeyboard.recMode)
            {
                int noteIdx = midiNote % 12;
                int octave = midiNote / 12 - 2;

                // Auto-detect: sequencer playing → real-time rec, stopped → step rec
                int playStep = pianoKeyboard.playingStepPtr ? pianoKeyboard.playingStepPtr->load() : -1;
                bool isStepRec = (playStep < 0);
                pianoKeyboard.stepRecMode = isStepRec;

                int step = isStepRec ? pianoKeyboard.recStep : playStep;
                if (step >= 0 && step < track.length)
                {
                    auto& sd = track.seq.steps[static_cast<size_t>(step)];
                    sd.active = true;
                    sd.trigless = false;
                    sd.noteIndex = static_cast<uint8_t>(noteIdx);
                    sd.octave = static_cast<int8_t>(octave);
                    sd.velocity = static_cast<uint8_t>(std::clamp (static_cast<int>(vel * 127), 1, 127));

                    // Step REC: advance to next step
                    if (isStepRec)
                        pianoKeyboard.recStep = (pianoKeyboard.recStep + 1) % track.length;
                }
                // Refresh synth step data
                for (int si = 0; si < kMaxSteps; ++si)
                    synthSteps[si]->setStepData (&track.seq.steps[static_cast<size_t>(si)]);
                repaint();
            }
        };
        pianoKeyboard.onNoteOff = [this](int /*midiNote*/) {
            track.kbNoteOff.store (true);
        };
        pianoKeyboard.onClose = [this]() {
            keyboardOpen = false;
            kbBtn.setColour (juce::TextButton::buttonColourId, Colours_GB::panel3);
            kbBtn.setColour (juce::TextButton::textColourOffId, Colours_GB::text);
            pianoKeyboard.setVisible (false);
            resized();
            repaint();
        };

        // Per-track preset button
        addAndMakeVisible (pstBtn);
        pstBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2060a0));
        pstBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        pstBtn.onClick = [this]() {
            juce::PopupMenu menu;
            if (lastSavedPresetName.isNotEmpty())
                menu.addItem (1, "Save \"" + lastSavedPresetName + "\"");
            menu.addItem (3, "Save As...");
            menu.addSeparator();

            std::vector<juce::File> loadFiles;
            auto loadMenu = PresetManager::buildBrowseMenu (
                PresetManager::PresetType::Synth, "", 100, loadFiles);
            if (loadFiles.empty()) loadMenu.addItem (-1, "(no presets)", false);
            menu.addSubMenu ("Load Synth Preset", loadMenu);
            menu.addSeparator();

            std::vector<juce::File> delFiles;
            auto delMenu = PresetManager::buildBrowseMenu (
                PresetManager::PresetType::Synth, "", 2000, delFiles);
            if (!delFiles.empty())
                menu.addSubMenu ("Delete Preset", delMenu);
            menu.addSeparator();

            menu.addItem (2, "New Folder...");

            std::vector<juce::File> delFolders;
            auto folderDelMenu = PresetManager::buildFolderDeleteMenu (
                PresetManager::PresetType::Synth, 3000, delFolders);
            if (!delFolders.empty())
                menu.addSubMenu ("Delete Folder", folderDelMenu);

            int result = menu.show();
            if (result == 0) return;

            if (result == 1 && lastSavedPresetName.isNotEmpty())
            {
                PresetManager::saveSynthEngine (track, lastSavedPresetName, lastSavedPresetFolder);
            }
            else if (result == 3)
                PresetManager::showSaveDialog (PresetManager::PresetType::Synth,
                    lastSavedPresetName.isNotEmpty() ? lastSavedPresetName : "Synth " + juce::String(index + 1),
                    [this](juce::String name, juce::String folder) {
                        PresetManager::saveSynthEngine (track, name, folder);
                        lastSavedPresetName = name;
                        lastSavedPresetFolder = folder;
                    });
            else if (result == 2)
                PresetManager::showNewFolderDialog (PresetManager::PresetType::Synth);
            else if (result >= 100 && result < 2000)
            {
                int fi = result - 100;
                if (fi >= 0 && fi < static_cast<int>(loadFiles.size()))
                {
                    PresetManager::loadSynthEngine (track, loadFiles[static_cast<size_t>(fi)]);
                    lastSavedPresetName = loadFiles[static_cast<size_t>(fi)].getFileNameWithoutExtension();
                    auto parent = loadFiles[static_cast<size_t>(fi)].getParentDirectory();
                    auto typeDir = PresetManager::getTypeDir (PresetManager::PresetType::Synth);
                    lastSavedPresetFolder = (parent == typeDir) ? juce::String() : parent.getFileName();
                    buildEngineKnobs();
                    resized();
                    repaint();
                }
            }
            else if (result >= 2000 && result < 3000)
            {
                int di = result - 2000;
                if (di >= 0 && di < static_cast<int>(delFiles.size()))
                    PresetManager::deletePreset (delFiles[static_cast<size_t>(di)]);
            }
            else if (result >= 3000)
            {
                int fi2 = result - 3000;
                if (fi2 >= 0 && fi2 < static_cast<int>(delFolders.size()))
                    PresetManager::deleteFolder (delFolders[static_cast<size_t>(fi2)]);
            }
        };

        // LED Meter
        addAndMakeVisible (ledMeter);
    }

    void setLevel (float lv) { ledMeter.setLevel (lv); }

    void syncMuteSolo()
    {
        if (muteBtn.getToggleState() != track.muted)
            muteBtn.setToggleState (track.muted, juce::dontSendNotification);
        if (soloBtn.getToggleState() != track.solo)
            soloBtn.setToggleState (track.solo, juce::dontSendNotification);
        // Sync SPD selector from track clockMul (preset load)
        int spdId = clockMulToSpdId (track.clockMul);
        if (spdSelector.getSelectedId() != spdId)
            spdSelector.setSelectedId (spdId, juce::dontSendNotification);
    }

    static int clockMulToSpdId (float cm)
    {
        const float vals[] = { 0.125f, 0.25f, 0.333f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f };
        int best = 7; float bestD = 999.0f;
        for (int i = 0; i < 13; ++i) { float d = std::abs (cm - vals[i]); if (d < bestD) { bestD = d; best = i + 1; } }
        return best;
    }

    void setMsegPlayhead (float p0, float p1, float p2,
                          float a0 = 0, float a1 = 0, float a2 = 0,
                          const float crossVals[3][2] = nullptr)
    {
        if (msegEditor && msegEditor->isVisible())
        {
            float phases[3] = { p0, p1, p2 };
            float auxes[3] = { a0, a1, a2 };
            msegEditor->setPlayheadPosition (phases[msegIdx]);
            msegEditor->setAuxValue (auxes[msegIdx]);
            if (crossVals)
                msegEditor->setCrossValues (crossVals[msegIdx][0], crossVals[msegIdx][1]);
            msegEditor->repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Page button events — forwarded via addMouseListener
        for (int p = 0; p < pageButtons.size(); ++p)
        {
            if (e.eventComponent == pageButtons[p])
            {
                if (e.mods.isPopupMenu())
                    showPageContextMenu (p);
                return;
            }
        }

        // Convert to local coordinates (events from addMouseListener arrive
        // in the source component's space, not ours)
        auto localPos = e.getEventRelativeTo (this).getPosition();

        // Waveform drag — check if clicking on start/end handles (SMP mode)
        // or click-to-set grain position (GRN mode)
        if (currentTab == 0 && (screenMode == 4 || screenMode == 6) && !waveformBounds.isEmpty()
            && waveformBounds.contains (localPos))
        {
            float wfX = static_cast<float>(waveformBounds.getX());
            float wfW = static_cast<float>(waveformBounds.getWidth());
            float wfY = static_cast<float>(waveformBounds.getY());
            float wfHf = static_cast<float>(waveformBounds.getHeight());
            float visibleFrac = 1.0f / wfZoom;
            float viewStart = wfOffset;
            float viewEnd = viewStart + visibleFrac;
            float pixNorm = (static_cast<float>(localPos.x) - wfX) / wfW;
            float clickNorm = wfOffset + pixNorm * visibleFrac;

            // ── Right-click → marker context menu OR grid div popup ──
            if (screenMode == 4 && e.mods.isRightButtonDown() && track.smpWarp > 0)
            {
                static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                int hitM = track.warpMarkers.empty() ? -1 :
                    WaveformOverlay::hitTestMarker (static_cast<float>(localPos.x), static_cast<float>(localPos.y),
                        wfX, wfY, wfW, wfHf, viewStart, viewEnd, track.warpMarkers, totalBeats);
                if (hitM >= 0)
                {
                    WaveformOverlay::showMarkerMenu (this, hitM, track.warpMarkers, totalBeats, [this](){ repaint(); });
                    return;
                }
                if (WaveformOverlay::isInRuler (static_cast<float>(localPos.y), wfY))
                {
                    WaveformOverlay::showGridDivMenu (this, track.gridDiv, [this](int d){ track.gridDiv = d; repaint(); });
                    return;
                }
            }

            // ── Warp marker interaction (priority over start/end handles) ──
            if (screenMode == 4 && track.smpWarp > 0)
            {
                static const float barLUT2[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                float hitTotalBeats = barLUT2[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                int hitIdx = track.warpMarkers.empty() ? -1 :
                    WaveformOverlay::hitTestMarker (
                        static_cast<float>(localPos.x), static_cast<float>(localPos.y),
                        wfX, wfY, wfW, wfHf, viewStart, viewEnd, track.warpMarkers, hitTotalBeats);

                // Double-click on marker → REMOVE it
                if (hitIdx >= 0 && e.getNumberOfClicks() >= 2 && e.mods.isLeftButtonDown())
                {
                    if (hitIdx > 0 && hitIdx < static_cast<int>(track.warpMarkers.size()) - 1)
                        track.warpMarkers.erase (track.warpMarkers.begin() + hitIdx);
                    warpHoveredMarker = -1; repaint(); return;
                }

                if (hitIdx >= 0 && e.mods.isLeftButtonDown())
                { warpDraggedMarker = hitIdx; wfDragMode = 4; repaint(); return; }

                // Double-click on empty space → ADD marker
                if (e.getNumberOfClicks() >= 2 && e.mods.isLeftButtonDown() && track.smpBars > 0)
                {
                    // Calculate beatPos from CURRENT warp mapping (Ableton-style)
                    // Adding a marker should change NOTHING — it captures the exact current state
                    static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                    float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                    float beatPos = 0.0f;
                    auto& wm = track.warpMarkers;
                    if (wm.size() >= 2)
                    {
                        for (size_t m = 0; m + 1 < wm.size(); ++m)
                        {
                            if (clickNorm >= wm[m].samplePos && (clickNorm <= wm[m + 1].samplePos || m + 2 == wm.size()))
                            {
                                float sLen = wm[m + 1].samplePos - wm[m].samplePos;
                                float bLen = wm[m + 1].beatPos - wm[m].beatPos;
                                float frac = (sLen > 0.001f) ? (clickNorm - wm[m].samplePos) / sLen : 0.0f;
                                beatPos = wm[m].beatPos + frac * bLen;
                                break;
                            }
                        }
                    }
                    else
                    {
                        beatPos = clickNorm * totalBeats;
                    }
                    WarpMarker newM; newM.samplePos = std::clamp (clickNorm, 0.001f, 0.999f);
                    newM.beatPos = beatPos; newM.originalSamplePos = newM.samplePos; newM.isAuto = false;
                    track.warpMarkers.push_back (newM);
                    std::sort (track.warpMarkers.begin(), track.warpMarkers.end(),
                        [](const WarpMarker& a, const WarpMarker& b) { return a.samplePos < b.samplePos; });
                    repaint(); return;
                }
            }

            if (screenMode == 4)
            {
                // Sampler: drag start/end handles
                float distStart = std::abs (clickNorm - track.smpStart);
                float distEnd = std::abs (clickNorm - track.smpEnd);
                float threshold = 0.03f * wfZoom;
                wfDragMode = (distStart < distEnd) ? 1 : 2;
                if (distStart > threshold && distEnd > threshold) wfDragMode = 0;
            }
            else
            {
                // Granular: click to set grain position
                track.grainPos = std::clamp (clickNorm, 0.0f, 1.0f);
                wfDragMode = 3; // special mode for grain pos drag
                repaint();
            }
            return;
        }
        if (nameLabel.getBounds().contains (localPos))
        {
            if (e.mods.isPopupMenu())
            {
                showTrackContextMenu();
                return;
            }
            engineOpen = !engineOpen;
            expandBtn.setToggleState (engineOpen, juce::dontSendNotification);
            if (onExpandToggle) onExpandToggle();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (wfDragMode > 0 && !waveformBounds.isEmpty())
        {
            float wfX = static_cast<float>(waveformBounds.getX());
            float wfW = static_cast<float>(waveformBounds.getWidth());
            float visibleFrac = 1.0f / wfZoom;
            float pixNorm = std::clamp ((static_cast<float>(e.x) - wfX) / wfW, 0.0f, 1.0f);
            float norm = wfOffset + pixNorm * visibleFrac; // convert to full-range normalized
            norm = std::clamp (norm, 0.0f, 1.0f);
            if (wfDragMode == 1) { track.smpStart = std::min (norm, track.smpEnd - 0.01f); }
            else if (wfDragMode == 2) { track.smpEnd = std::max (norm, track.smpStart + 0.01f); }
            else if (wfDragMode == 3) { track.grainPos = norm; } // Granular: drag grain position
            else if (wfDragMode == 4 && warpDraggedMarker >= 0
                     && warpDraggedMarker < static_cast<int>(track.warpMarkers.size()))
            {
                auto& m = track.warpMarkers[static_cast<size_t>(warpDraggedMarker)];
                // Convert mouse position to beat position (Ableton-style: drag WHEN it plays, not WHAT plays)
                static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                float beatPos = norm * totalBeats;
                // Clamp between neighboring markers' beat positions
                float minB = 0.01f, maxB = totalBeats - 0.01f;
                if (warpDraggedMarker > 0)
                    minB = track.warpMarkers[static_cast<size_t>(warpDraggedMarker - 1)].beatPos + 0.01f;
                if (warpDraggedMarker < static_cast<int>(track.warpMarkers.size()) - 1)
                    maxB = track.warpMarkers[static_cast<size_t>(warpDraggedMarker + 1)].beatPos - 0.01f;
                m.beatPos = std::clamp (beatPos, minB, maxB);
                m.isAuto = false;
            }
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        wfDragMode = 0;
        warpDraggedMarker = -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (currentTab == 0 && screenMode == 4 && track.smpWarp > 0
            && !waveformBounds.isEmpty() && !track.warpMarkers.empty())
        {
            float wfX = static_cast<float>(waveformBounds.getX());
            float wfW = static_cast<float>(waveformBounds.getWidth());
            float wfY = static_cast<float>(waveformBounds.getY());
            float wfHf = static_cast<float>(waveformBounds.getHeight());
            float visibleFrac = 1.0f / wfZoom;
            int oldHover = warpHoveredMarker;
            warpHoveredMarker = WaveformOverlay::hitTestMarker (
                static_cast<float>(e.x), static_cast<float>(e.y),
                wfX, wfY, wfW, wfHf, wfOffset, wfOffset + visibleFrac, track.warpMarkers,
                (track.smpWarp > 0 && track.smpBars > 0) ? [&](){
                    static const float bl[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                    return bl[std::clamp(track.smpBars, 1, 8)] * 4.0f; }() : 0.0f);
            if (warpHoveredMarker != oldHover) repaint();
        }
        else if (warpHoveredMarker >= 0)
        {
            warpHoveredMarker = -1;
            repaint();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // Zoom waveform with scroll wheel when hovering over it
        if (currentTab == 0 && (screenMode == 4 || screenMode == 6) && !waveformBounds.isEmpty()
            && waveformBounds.contains (e.getPosition()))
        {
            float oldZoom = wfZoom;
            wfZoom *= (wheel.deltaY > 0) ? 1.25f : 0.8f;
            wfZoom = juce::jlimit (1.0f, 16.0f, wfZoom);
            // Keep zoom centered on mouse position
            float mouseNorm = (static_cast<float>(e.x) - waveformBounds.getX()) / waveformBounds.getWidth();
            float visibleBefore = 1.0f / oldZoom;
            float visibleAfter = 1.0f / wfZoom;
            wfOffset += (visibleBefore - visibleAfter) * mouseNorm;
            wfOffset = juce::jlimit (0.0f, 1.0f - visibleAfter, wfOffset);
            repaint();
        }
        else
        {
            // Forward to parent for Viewport scrolling
            if (auto* p = getParentComponent())
                p->mouseWheelMove (e.getEventRelativeTo (p), wheel);
        }
    }

    void showTrackContextMenu()
    {
        std::set<std::string> usedParams;
        int totalPlocks = 0;
        for (auto& s : track.seq.steps)
        {
            for (auto& kv : s.plocks)
            {
                usedParams.insert (kv.first);
                ++totalPlocks;
            }
        }

        juce::PopupMenu menu;
        menu.addSectionHeader ("MOTION");

        if (totalPlocks > 0)
        {
            menu.addItem (1, "Clear ALL Motion (" + juce::String (totalPlocks) + " locks)");
            menu.addSeparator();

            int itemId = 100;
            for (auto& pName : usedParams)
            {
                int count = 0;
                for (auto& s : track.seq.steps)
                    if (s.plocks.count (pName)) ++count;
                menu.addItem (itemId, "Clear \"" + juce::String (pName) + "\" (" + juce::String (count) + " steps)");
                ++itemId;
            }
        }
        else
        {
            menu.addItem (-1, "No motion recorded", false);
        }

        menu.addSeparator();
        menu.addSectionHeader ("PLAY DIR");
        {
            static const char* dirNames[] = { "FWD", "REV", "PING", "RND", "ONE" };
            for (int d = 0; d < 5; ++d)
                menu.addItem (200 + d, dirNames[d], true, track.playDir == d);
        }

        menu.addSeparator();
        menu.addSectionHeader ("CHOKE GROUP");
        {
            menu.addItem (400, "OFF", true, track.chokeGroup == 0);
            static const char* grpNames[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
            for (int g = 0; g < 8; ++g)
                menu.addItem (401 + g, grpNames[g], true, track.chokeGroup == g + 1);
        }

        menu.addSeparator();
        menu.addSectionHeader ("LINK GROUP");
        {
            menu.addItem (500, "OFF", true, track.linkGroup == 0);
            static const char* lnkNames[] = { "A", "B", "C", "D" };
            for (int g = 0; g < 4; ++g)
                menu.addItem (501 + g, lnkNames[g], true, track.linkGroup == g + 1);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameLabel),
            [this, usedParams] (int result)
            {
                if (result == 1)
                {
                    for (auto& s : track.seq.steps)
                        s.plocks.clear();
                }
                else if (result >= 200 && result < 205)
                {
                    track.playDir = result - 200;
                    if (onLinkSync) onLinkSync();
                }
                else if (result >= 400 && result <= 408)
                {
                    track.chokeGroup = result - 400;
                }
                else if (result >= 500 && result <= 504)
                {
                    track.linkGroup = result - 500;
                }
                else if (result >= 100)
                {
                    int idx = result - 100;
                    auto it = usedParams.begin();
                    std::advance (it, idx);
                    if (it != usedParams.end())
                    {
                        std::string key = *it;
                        for (auto& s : track.seq.steps)
                            s.plocks.erase (key);
                    }
                }
                refreshSteps();
                repaint();
            });
    }

    void showPageContextMenu (int srcPage)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("PAGE " + juce::String (srcPage + 1));

        for (int dst = 0; dst < kNumPages; ++dst)
        {
            if (dst == srcPage) continue;
            menu.addItem (10 + dst, "Copy -> Page " + juce::String (dst + 1));
        }
        menu.addItem (50, "Copy -> ALL pages");
        menu.addSeparator();
        menu.addItem (60, "Clear Page " + juce::String (srcPage + 1));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pageButtons[srcPage]),
            [this, srcPage](int result)
            {
                if (result == 0) return;

                if (result >= 10 && result < 10 + kNumPages)
                {
                    copyPage (srcPage, result - 10);
                }
                else if (result == 50)
                {
                    for (int d = 0; d < kNumPages; ++d)
                        if (d != srcPage) copyPage (srcPage, d);
                }
                else if (result == 60)
                {
                    int base = srcPage * kStepsPerPage;
                    for (int i = 0; i < kStepsPerPage; ++i)
                        track.seq.steps[static_cast<size_t>(base + i)].reset();
                }

                refreshSteps();
                ratchetRow.repaint();
                repaint();
            });
    }

    void copyPage (int srcPage, int dstPage)
    {
        int srcBase = srcPage * kStepsPerPage;
        int dstBase = dstPage * kStepsPerPage;
        for (int i = 0; i < kStepsPerPage; ++i)
            track.seq.steps[static_cast<size_t>(dstBase + i)] =
                track.seq.steps[static_cast<size_t>(srcBase + i)];
    }

    void setPlayingStep (int step)
    {
        if (step >= 0)
        {
            int newPage = step / kStepsPerPage;
            // Reset manual override when playback reaches the displayed page
            if (manualPageOverride && newPage == track.page)
                manualPageOverride = false;

            if (!manualPageOverride && newPage != lastAutoPage && newPage < kNumPages)
            {
                lastAutoPage = newPage;
                track.page = newPage;
                updateVisibleSteps();
                ratchetRow.setPage (newPage);
                updatePageButtons();
                resized();
            }
        }
        else if (step < 0)
        {
            manualPageOverride = false;
            lastAutoPage = -1;
        }
        for (int i = 0; i < kMaxSteps; ++i)
            synthSteps[i]->setPlaying (i == step);
    }

    bool isEngineOpen() const { return engineOpen; }
    int getDesiredHeight() const { return engineOpen ? 268 : 56; }

    std::function<void()> onExpandToggle;
    std::function<void()> onBeforeEdit;
    std::function<void()> onLinkSync;
    std::function<void()> onStepSync;

    // ── Load sample folder into sample slots ──
    void loadSampleFolder (const juce::File& folder)
    {
        if (!folder.isDirectory()) return;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        track.sampleSlots.clear();
        track.sampleSlotNames.clear();
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        files.sort();
        juce::File firstFile;
        for (const auto& f : files)
        {
            if (auto* reader = fm.createReaderFor (f))
            {
                auto buf = std::make_shared<juce::AudioBuffer<float>> (static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                reader->read (buf.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
                track.sampleSlots.push_back (buf);
                track.sampleSlotNames.push_back (f.getFileNameWithoutExtension());
                if (firstFile == juce::File()) firstFile = f;
                delete reader;
                if (track.sampleSlots.size() >= 128) break;
            }
        }
        // Load first sample as main track sample (waveform, GATE, stretch — everything)
        if (firstFile.existsAsFile())
            loadSampleFromFile (firstFile);
    }

    void rebuildKnobs() { buildEngineKnobs(); resized(); }

    void refreshLinkedSteps()
    {
        refreshSteps();
        lenLabel.setText (juce::String (track.length), juce::dontSendNotification);
        swingSlider.setValue (track.swing, juce::dontSendNotification);
        volSlider.setValue (track.volume, juce::dontSendNotification);
        int spdId = clockMulToSpdId (track.clockMul);
        spdSelector.setSelectedId (spdId, juce::dontSendNotification);
        muteBtn.setToggleState (track.muted, juce::dontSendNotification);
        soloBtn.setToggleState (track.solo, juce::dontSendNotification);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (2);

        // ── RIGHT STRIP: meter + fader + M/S — full collapsed-row height ──
        auto rightStrip = bounds.removeFromRight (46);
        auto meterFaderArea = rightStrip.removeFromTop (52); // mainRow(36) + ratchet(14) + gap(2)
        meterFaderArea.reduce (0, 2); // vertical padding

        ledMeter.setBounds (meterFaderArea.removeFromRight (10));
        meterFaderArea.removeFromRight (2);
        volSlider.setBounds (meterFaderArea.removeFromRight (14));
        meterFaderArea.removeFromRight (2);
        // M and S stacked vertically
        auto msArea = meterFaderArea.removeFromRight (17);
        muteBtn.setBounds (msArea.removeFromTop (msArea.getHeight() / 2).reduced (0, 1));
        soloBtn.setBounds (msArea.reduced (0, 1));

        // ── MAIN ROW (36px) ──
        auto mainRow = bounds.removeFromTop (36);

        // Left: label (with padding for engine icon)
        mainRow.removeFromLeft (10); // left margin for engine icon
        nameLabel.setBounds (mainRow.removeFromLeft (48).reduced (14, 6));
        mainRow.removeFromLeft (1);

        // Page buttons — LEFT of step grid (8 pages, compact)
        mainRow.removeFromLeft (4); // padding from name label
        for (int i = 0; i < pageButtons.size(); ++i)
            pageButtons[i]->setBounds (mainRow.getX() + i * 15,
                                        mainRow.getY() + 8,
                                        14, mainRow.getHeight() - 16);
        mainRow.removeFromLeft (kNumPages * 15 + 2);

        // Right controls — compact (no meter/fader/M/S — they're in the right strip)
        auto ctrlArea = mainRow.removeFromRight (280);

        expandBtn.setBounds (ctrlArea.removeFromRight (17).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        plkBtn.setBounds (ctrlArea.removeFromRight (22).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        linkBtn.setBounds (ctrlArea.removeFromRight (17).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        randBtn.setBounds (ctrlArea.removeFromRight (22).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        initBtn.setBounds (ctrlArea.removeFromRight (22).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        copyBtn.setBounds (ctrlArea.removeFromRight (18).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        pasteBtn.setBounds (ctrlArea.removeFromRight (20).reduced (0, 7));
        ctrlArea.removeFromRight (1);

        auto swgArea = ctrlArea.removeFromRight (32);
        swingCaption.setBounds (swgArea.removeFromTop (10));
        swingSlider.setBounds (swgArea.reduced (0, 3));
        ctrlArea.removeFromRight (1);

        auto spdArea = ctrlArea.removeFromRight (34);
        spdCaption.setBounds (spdArea.removeFromTop (10));
        spdSelector.setBounds (spdArea.reduced (0, 3));
        ctrlArea.removeFromRight (1);

        auto lenArea = ctrlArea.removeFromRight (28);
        lenCaption.setBounds (lenArea.removeFromTop (10));
        lenLabel.setBounds (lenArea.reduced (0, 1));

        // Step buttons — same positioning math as RatchetRow
        auto stepArea = mainRow;
        int numGroups = 4;
        int gapSize = 3;
        int totalGaps = (numGroups - 1) * gapSize;
        int stepW = std::max (1, (stepArea.getWidth() - totalGaps) / kStepsPerPage);
        int startStep = track.page * kStepsPerPage;
        for (int i = 0; i < kStepsPerPage; ++i)
        {
            int si = startStep + i;
            if (si < kMaxSteps)
            {
                int groupIdx = i / 4;
                int x = stepArea.getX() + i * stepW + groupIdx * gapSize;
                synthSteps[si]->setBounds (x, stepArea.getY() + 2,
                                            stepW - 1, stepArea.getHeight() - 4);
            }
        }

        // Ratchet
        auto ratchetArea = bounds.removeFromTop (14);
        ratchetArea = ratchetArea.withLeft (stepArea.getX()).withWidth (stepArea.getWidth());
        ratchetRow.setBounds (ratchetArea);

        // Engine panel — TABBED: [ENG] [FX] [LFO] [VOC] + knob area
        if (engineOpen && bounds.getHeight() > 10)
        {
            bounds.removeFromTop (6); // gap between steps/ratchet and engine panel
            auto engineArea = bounds.reduced (2, 0);

            // Left strip: tab buttons (stacked) + engine selector
            int tabW = 30;
            auto tabStrip = engineArea.removeFromLeft (tabW + 2);
            int tabH = (tabStrip.getHeight() - 10) / 9;
            tabEng.setVisible (true);
            tabFx.setVisible (true);
            tabLfo.setVisible (true);
            tabMseg.setVisible (true);
            tabVoice.setVisible (true);
            pstBtn.setVisible (true);
            arpBtn.setVisible (true);
            harmBtn.setVisible (true);
            kbBtn.setVisible (true);
            tabEng.setBounds (tabStrip.getX(), tabStrip.getY(), tabW, tabH);
            tabFx.setBounds (tabStrip.getX(), tabStrip.getY() + tabH + 1, tabW, tabH);
            tabLfo.setBounds (tabStrip.getX(), tabStrip.getY() + 2 * (tabH + 1), tabW, tabH);
            tabMseg.setBounds (tabStrip.getX(), tabStrip.getY() + 3 * (tabH + 1), tabW, tabH);
            tabVoice.setBounds (tabStrip.getX(), tabStrip.getY() + 4 * (tabH + 1), tabW, tabH);
            arpBtn.setBounds (tabStrip.getX(), tabStrip.getY() + 5 * (tabH + 1), tabW, tabH);
            harmBtn.setBounds (tabStrip.getX(), tabStrip.getY() + 6 * (tabH + 1), tabW, tabH);
            kbBtn.setBounds (tabStrip.getX(), tabStrip.getY() + 7 * (tabH + 1), tabW, tabH);
            pstBtn.setBounds (tabStrip.getX(), tabStrip.getY() + 8 * (tabH + 1), tabW, tabH);
            arpBtn.setColour (juce::TextButton::buttonColourId,
                track.arp.enabled ? Colours_GB::accent : Colours_GB::panel3);

            // ── KEYBOARD PANEL (takes bottom portion of engine area) ──
            if (keyboardOpen)
            {
                int kbH = std::min (160, engineArea.getHeight() / 2);
                auto kbArea = engineArea.removeFromBottom (kbH);
                pianoKeyboard.setBounds (kbArea);
                pianoKeyboard.recLength = track.length;
                pianoKeyboard.setVisible (true);
            }
            else
            {
                pianoKeyboard.setVisible (false);
            }

            // Highlight active tab — flat style with accent text
            auto inactiveCol = juce::Colour (0x01000000); // transparent
            auto activeTextCol = Colours_GB::accent;
            auto inactiveTextCol = juce::Colour (0xff707888);
            auto setTab = [&](juce::TextButton& btn, int tabIdx) {
                btn.setColour (juce::TextButton::buttonColourId, inactiveCol);
                btn.setColour (juce::TextButton::buttonOnColourId, inactiveCol);
                btn.setColour (juce::TextButton::textColourOffId, currentTab == tabIdx ? activeTextCol : inactiveTextCol);
            };
            setTab (tabEng, 0); setTab (tabFx, 1); setTab (tabLfo, 2);
            setTab (tabMseg, 3); setTab (tabVoice, 4);

            // Engine selector (ANA/FM/ELM/PLT) — only in ENG tab
            // ── RESET: Hide all controls before showing tab-specific ones ──
            fmBtn.setVisible (false);
            for (auto& btn : engSelBtns) btn.setVisible (false);
            loadSampleBtn.setVisible (false);
            smpPrevBtn.setVisible (false);
            smpNextBtn.setVisible (false);
            zoomInBtn.setVisible (false);
            zoomOutBtn.setVisible (false);
            for (auto* k : engineKnobs) k->setVisible (false);
            for (auto* k : msegKnobs) removeChildComponent (k);
            if (msegEditor) msegEditor->setVisible (false);
            if (msegTargetBtn) msegTargetBtn->setVisible (false); if (msegRetrigBtn) msegRetrigBtn->setVisible (false); for (auto& b : msegSelBtns) b.setVisible (false);

            if (currentTab == 0)
            {
                // Show vertical engine selector buttons
                int engBtnW = 32;
                int engBtnH = std::max (14, (engineArea.getHeight() - 6) / 7);
                int engX = engineArea.getX();
                int engY = engineArea.getY() + 1;
                juce::Colour engColors[] = {Colours_GB::accent, juce::Colour (0xff3060cc), Colours_GB::green, juce::Colour (0xffcc40cc), juce::Colour (0xff40b0b0), juce::Colour (0xff50c0e0), juce::Colour (0xffd06090)};
                for (int mi = 0; mi < 7; ++mi)
                {
                    auto& btn = engSelBtns[static_cast<size_t>(mi)];
                    btn.setBounds (engX, engY + mi * engBtnH, engBtnW, engBtnH - 1);
                    bool sel = (mi == screenMode);
                    btn.setColour (juce::TextButton::buttonColourId,
                                   sel ? engColors[mi].withAlpha (0.85f) : engColors[mi].withAlpha (0.3f));
                    btn.setColour (juce::TextButton::textColourOffId,
                                   sel ? juce::Colour (0xff001020) : juce::Colours::white.withAlpha (0.6f));
                    btn.setVisible (true);
                }
                engineArea.removeFromLeft (engBtnW + 2);
                fmBtn.setVisible (false);
            }
            else
            {
                fmBtn.setVisible (false);
                for (auto& btn : engSelBtns) btn.setVisible (false);
            }

            // Knob area — 2 rows for ALL knobs of current tab
            // In SMP/GRN mode (ENG tab), push knobs down to make room for waveform
            if (currentTab == 0 && (screenMode == 4 || screenMode == 6))
                engineArea.removeFromTop (static_cast<int>(engineArea.getHeight() * 0.42f));

            // LOAD sample button + prev/next — in ENG tab + SMP or GRN mode
            if (currentTab == 0 && (screenMode == 4 || screenMode == 6))
            {
                loadSampleBtn.setVisible (true);
                smpPrevBtn.setVisible (true);
                smpNextBtn.setVisible (true);
                int bY = engineArea.getY() - 18;
                int bX = engineArea.getX();
                smpPrevBtn.setBounds (bX, bY, 16, 16);
                smpNextBtn.setBounds (bX + 18, bY, 16, 16);
                loadSampleBtn.setBounds (bX + 36, bY, 42, 16);
                zoomOutBtn.setBounds (bX + 82, bY, 16, 16);
                zoomInBtn.setBounds (bX + 100, bY, 16, 16);
                zoomOutBtn.setVisible (screenMode == 4);
                zoomInBtn.setVisible (screenMode == 4);

                // ── Resample controls (Granular only) ──
                if (screenMode == 6)
                {
                    resampleSrcBox.setVisible (true);
                    resampleRecBtn.setVisible (true);
                    resampleSyncBtn.setVisible (true);
                    resampleSrcBox.setBounds (bX + 82, bY, 52, 16);
                    resampleRecBtn.setBounds (bX + 136, bY, 38, 16);
                    resampleSyncBtn.setBounds (bX + 176, bY, 38, 16);
                }
                else
                {
                    resampleSrcBox.setVisible (false);
                    resampleRecBtn.setVisible (false);
                    resampleSyncBtn.setVisible (false);
                }
            }
            else
            {
                loadSampleBtn.setVisible (false);
                smpPrevBtn.setVisible (false);
                smpNextBtn.setVisible (false);
                zoomInBtn.setVisible (false);
                zoomOutBtn.setVisible (false);
                resampleSrcBox.setVisible (false);
                resampleRecBtn.setVisible (false);
                resampleSyncBtn.setVisible (false);
            }

            // WT buttons — LOAD WT1, WT2, <, >, EDIT
            if (currentTab == 0 && screenMode == 5)
            {
                wtLoadBtn1.setVisible (true);
                wtLoadBtn2.setVisible (true);
                wtPrevBtn.setVisible (true);
                wtNextBtn.setVisible (true);
                wtEditBtn.setVisible (false); // no longer a popup — editor is embedded
                int bY = engineArea.getY() - 18;
                int bX = engineArea.getX();
                wtPrevBtn.setBounds (bX, bY, 16, 16);
                wtNextBtn.setBounds (bX + 18, bY, 16, 16);
                wtLoadBtn1.setBounds (bX + 36, bY, 30, 16);
                wtLoadBtn2.setBounds (bX + 68, bY, 30, 16);

                // ── Embedded WavetableEditor on the right side ──
                auto& wtRef = (wtLoadTarget == 0) ? track.wtData1 : track.wtData2;
                if (!wtRef) wtRef = std::make_shared<WavetableData>(WavetableData::createBasic());
                if (!wtEditorEmbed)
                {
                    wtEditorEmbed = std::make_unique<WavetableEditor> (*wtRef);
                    wtEditorEmbed->onChange = [this]() { repaint(); };
                    wtEditorEmbed->onFrameChange = [this](float pos) {
                        // When user navigates frames in the editor, sync the position knob
                        if (wtLoadTarget == 0)
                            track.wtPos1 = pos;
                        else
                            track.wtPos2 = pos;
                        // Update only the POS knob display — NOT rebuildKnobs (too heavy)
                        for (auto* k : engineKnobs)
                        {
                            if (k->getName() == "POS1" && wtLoadTarget == 0) { k->setValue (pos, false); break; }
                            if (k->getName() == "POS2" && wtLoadTarget == 1) { k->setValue (pos, false); break; }
                        }
                    };
                    addChildComponent (*wtEditorEmbed);
                }
                int editorW = std::min (490, engineArea.getWidth() * 56 / 100);
                auto editorArea = engineArea.removeFromRight (editorW);
                engineArea.removeFromRight (4); // gap
                wtEditorEmbed->setBounds (editorArea);
                wtEditorEmbed->setVisible (true);
            }
            else
            {
                wtLoadBtn1.setVisible (false);
                wtLoadBtn2.setVisible (false);
                wtPrevBtn.setVisible (false);
                wtNextBtn.setVisible (false);
                wtEditBtn.setVisible (false);
                if (wtEditorEmbed) wtEditorEmbed->setVisible (false);
            }

            // Reserve space for category labels at top of all tabs
            auto fullEngineArea = engineArea; // save for MSEG tab (uses full space)
            engineArea.removeFromTop (14);

            int rowH = (engineArea.getHeight() - 14) / 2; // 14px gap for row2 labels
            int numRows = 2;
            // LFO tab with extra routes → 3 rows (one LFO per row)
            if (currentTab == 2)
            {
                numRows = 3;
                rowH = (engineArea.getHeight() - 8) / 3;
            }
            auto row1 = engineArea.removeFromTop (rowH);
            if (numRows == 2)
                engineArea.removeFromTop (14); // gap for row2 category labels
            else
                engineArea.removeFromTop (4);
            auto row2 = (numRows == 2) ? engineArea : engineArea.removeFromTop (rowH);
            engineArea.removeFromTop (numRows == 3 ? 4 : 0);
            auto row3 = engineArea; // only used when numRows == 3

            // Determine which knobs to show
            int startK = 0, endK = 0;
            if (currentTab == 0) { startK = 0; endK = fxStartIdx; }                         // ENG
            else if (currentTab == 1) { startK = fxStartIdx; endK = lfoStartIdx; }           // FX
            else if (currentTab == 2) { startK = lfoStartIdx; endK = voiceStartIdx; }        // LFO
            else if (currentTab == 3) { startK = 0; endK = 0; }                              // MSEG (no knobs)
            else { startK = voiceStartIdx; endK = static_cast<int>(engineKnobs.size()); }     // VOICE

            // MSEG editor visibility
            if (msegEditor)
            {
                msegEditor->setVisible (currentTab == 3);
                if (currentTab == 3)
                {
                    auto msegArea = fullEngineArea;
                    // Control strip at bottom (38px for readable knobs)
                    auto ctrlRow = msegArea.removeFromBottom (38);
                    // MSEG editor fills EVERYTHING above
                    msegEditor->setBounds (msegArea);

                    // Control strip: [1][2][3] | TGT popup | DPTH | TIME | SYNC | LOOP | GX | GY
                    for (auto* k : msegKnobs) removeChildComponent (k);
                    msegKnobs.clear();

                    // MSEG 1/2/3 selector buttons
                    int cx = ctrlRow.getX();
                    for (int mi = 0; mi < 3; ++mi)
                    {
                        auto& btn = msegSelBtns[mi];
                        btn.setButtonText (juce::String (mi + 1));
                        btn.setColour (juce::TextButton::buttonColourId,
                            mi == msegIdx ? juce::Colour (0xff40d8e8) : juce::Colour (0xff252830));
                        btn.setColour (juce::TextButton::textColourOffId,
                            mi == msegIdx ? juce::Colours::black : juce::Colour (0xaaffffff));
                        btn.setBounds (cx, ctrlRow.getY(), 18, ctrlRow.getHeight());
                        btn.onClick = [this, mi]() {
                            msegIdx = mi;
                            if (msegEditor)
                            {
                                msegEditor->setData (track.msegs[static_cast<size_t>(msegIdx)]);
                                msegEditor->setMsegIndex (msegIdx);
                            }
                            resized();
                            repaint();
                        };
                        addAndMakeVisible (btn);
                        cx += 19;
                    }
                    cx += 2; // gap after selector

                    // Global MSEG retrigger toggle
                    if (!msegRetrigBtn)
                    {
                        msegRetrigBtn = std::make_unique<juce::TextButton> ("RTRIG");
                        msegRetrigBtn->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                        addChildComponent (*msegRetrigBtn);
                        msegRetrigBtn->onClick = [this]() {
                            track.msegRetrig = !track.msegRetrig;
                            msegRetrigBtn->setButtonText (track.msegRetrig ? "RTRIG" : "rtrig");
                            msegRetrigBtn->setColour (juce::TextButton::buttonColourId,
                                track.msegRetrig ? juce::Colour (0xff40d8e8) : juce::Colour (0xff8050c0));
                        };
                    }
                    msegRetrigBtn->setVisible (true);
                    msegRetrigBtn->setButtonText (track.msegRetrig ? "RTRIG" : "rtrig");
                    msegRetrigBtn->setColour (juce::TextButton::buttonColourId,
                        track.msegRetrig ? juce::Colour (0xff40d8e8) : juce::Colour (0xff8050c0));
                    msegRetrigBtn->setBounds (cx, ctrlRow.getY(), 36, ctrlRow.getHeight());
                    cx += 38;

                    auto addMK = [&](const juce::String& name, float minV, float maxV, float initV,
                                     std::function<void(float)> cb, std::function<juce::String(float)> fmt,
                                     juce::Colour accent = juce::Colour (0xff40d8e8))
                    {
                        auto* k = new KnobComponent (name, minV, maxV, initV, fmt);
                        k->onChange = cb; k->setAccentColour (accent);
                        addAndMakeVisible (k); msegKnobs.add (k);
                    };

                    juce::Colour colTarget  {0xffe09040u}; // amber — routing
                    juce::Colour colTime    {0xff60c070u}; // green — timing
                    juce::Colour colShape   {0xff9070c0u}; // purple — shape
                    juce::Colour colAux     {0xff40d8e8u}; // cyan — aux LFO
                    juce::Colour colRoute   {0xff6090ddu}; // blue — extra routes

                    // Target as popup button
                    if (!msegTargetBtn)
                    {
                        msegTargetBtn = std::make_unique<juce::TextButton> ("TGT");
                        msegTargetBtn->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252830));
                        addChildComponent (*msegTargetBtn);
                        msegTargetBtn->onClick = [this]() {
                            juce::PopupMenu m;
                            int curTgt = track.msegs[static_cast<size_t>(msegIdx)].target;
                            auto addSection = [&](const char* title, std::initializer_list<int> indices) {
                                m.addSeparator();
                                m.addSectionHeader (title);
                                for (int i : indices)
                                    m.addItem (i + 1, LFOEngine::getSynthTargetName (i), true, curTgt == i);
                            };
                            addSection ("FILTER",    {1, 2, 14, 56, 57, 58, 59});
                            addSection ("AMP",       {3, 15, 16, 17, 39});
                            addSection ("PITCH/PAN", {0, 4});
                            addSection ("OSC",       {9, 10, 11, 12, 13, 18, 54, 55});
                            addSection ("SEND",      {5, 6, 7, 8});
                            addSection ("FM",        {19, 60, 61, 20, 21, 22, 23, 24, 25, 26, 27});
                            addSection ("ELEMENTS",  {28, 29, 30, 31, 32, 33});
                            addSection ("PLAITS",    {34, 35, 36, 37, 38});
                            addSection ("FX",        {40,41,42,43,44,45,46,91,92});
                            addSection ("SAMPLER",   {47,48,49,50,51,52,53,89,90,93});
                            addSection ("WAVETABLE", {70, 71, 72, 73, 74, 75});
                            addSection ("GRANULAR", {76,77,78,79,80,81,82,83,84,85,86,87,88});
                            addSection ("X-MOD",     {62, 63, 64, 65, 66, 67});
                            addSection ("MSEG",      {68, 69});
                            m.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (*msegTargetBtn)
                                .withParentComponent (getTopLevelComponent()),
                                [this](int r) {
                                    if (r > 0) {
                                        track.msegs[static_cast<size_t>(msegIdx)].target = r - 1;
                                        msegTargetBtn->setButtonText (juce::String ("TGT: ") + LFOEngine::getSynthTargetName (r - 1));
                                    }
                                });
                        };
                    }
                    msegTargetBtn->setVisible (true);
                    msegTargetBtn->setButtonText (juce::String ("TGT: ") + LFOEngine::getSynthTargetName (std::clamp (track.msegs[static_cast<size_t>(msegIdx)].target, 0, LFOEngine::kNumSynthTargets - 1)));
                    msegTargetBtn->setBounds (cx, ctrlRow.getY(), 56, ctrlRow.getHeight());
                    cx += 58;

                    // ── TARGET (amber) ──
                    addMK ("DPTH", -1, 1, track.msegs[static_cast<size_t>(msegIdx)].depth, [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].depth = v; },
                        [](float v){ return juce::String(static_cast<int>(v*100))+"%"; }, colTarget);
                    // ── TIMING (green) ──
                    addMK ("TIME", 0.01f, 30.0f, track.msegs[static_cast<size_t>(msegIdx)].totalTime, [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].totalTime = v; },
                        [](float v){ return v<1?juce::String(static_cast<int>(v*1000))+"ms":juce::String(v,1)+"s"; }, colTime);
                    addMK ("SYNC", 0, 11, track.msegs[static_cast<size_t>(msegIdx)].tempoSync ? static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].syncDiv + 1) : 0.0f,
                        [this](float v){ int vi=static_cast<int>(v); track.msegs[static_cast<size_t>(msegIdx)].tempoSync=(vi>0); if(vi>0) track.msegs[static_cast<size_t>(msegIdx)].syncDiv=vi-1; },
                        [](float v){ int vi=static_cast<int>(v); if(vi==0)return juce::String("FREE"); static const char* sn[]={"1/32","1/16","1/8","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"}; return juce::String(sn[std::clamp(vi-1,0,10)]); }, colTime);
                    addMK ("TRNS", 0, 1, track.msegs[static_cast<size_t>(msegIdx)].transportSync ? 1.0f : 0.0f,
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].transportSync = (v > 0.5f); },
                        [](float v){ return v > 0.5f ? juce::String("ON") : juce::String("OFF"); }, colTime);
                    // ── SHAPE (purple) ──
                    addMK ("LOOP", 0, 3, static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].loopMode), [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].loopMode=static_cast<int>(v); },
                        [](float v){ const char*m[]={"ONE","LOOP","P.P","RND"};return juce::String(m[static_cast<int>(v)%4]); }, colShape);
                    addMK ("GX", 1, 32, static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].gridX), [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].gridX=static_cast<int>(v); if(msegEditor)msegEditor->repaint(); },
                        [](float v){ return juce::String(static_cast<int>(v)); }, colShape);
                    addMK ("GY", 1, 16, static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].gridY), [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].gridY=static_cast<int>(v); if(msegEditor)msegEditor->repaint(); },
                        [](float v){ return juce::String(static_cast<int>(v)); }, colShape);
                    // ── AUX LFO (cyan) ──
                    addMK ("A.RT", 0.05f, 20.0f, track.msegs[static_cast<size_t>(msegIdx)].auxRate,
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].auxRate = v; },
                        [](float v){ return v<1?juce::String(static_cast<int>(v*1000))+"ms":juce::String(v,1)+"Hz"; }, colAux);
                    addMK ("A.SH", 0, 4, static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].auxShape),
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].auxShape = static_cast<int>(v); },
                        [](float v){ const char* n[]={"SIN","TRI","SAW","SQR","S&H"}; return juce::String(n[std::clamp(static_cast<int>(v),0,4)]); }, colAux);
                    addMK ("A.SY", 0, 8, track.msegs[static_cast<size_t>(msegIdx)].auxSync ? static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].auxSyncDiv + 1) : 0.0f,
                        [this](float v){ int vi=static_cast<int>(v); track.msegs[static_cast<size_t>(msegIdx)].auxSync=(vi>0); if(vi>0) track.msegs[static_cast<size_t>(msegIdx)].auxSyncDiv=vi-1; },
                        [](float v){ int vi=static_cast<int>(v); if(vi==0)return juce::String("FREE"); static const char* sn[]={"1/32","1/16","1/8","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"}; return juce::String(sn[std::clamp(vi-1,0,10)]); }, colAux);

                    // ── FADE-IN (green, matches timing group) ──
                    addMK ("FDI", 0, 10, track.msegs[static_cast<size_t>(msegIdx)].fadeIn,
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].fadeIn = v; },
                        [this](float v){
                            if (v < 0.01f) return juce::String ("OFF");
                            auto& m = track.msegs[static_cast<size_t>(msegIdx)];
                            if (m.fadeInSync) {
                                static const char* sn[]={"1/32","1/16","1/8","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"};
                                return juce::String (sn[std::clamp (static_cast<int>(v), 0, 10)]);
                            }
                            if (v < 1.0f) return juce::String (static_cast<int>(v * 1000)) + "ms";
                            return juce::String (v, 1) + "s";
                        }, colTime);
                    addMK ("FDS", 0, 1, track.msegs[static_cast<size_t>(msegIdx)].fadeInSync ? 1.0f : 0.0f,
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].fadeInSync = (v > 0.5f); },
                        [](float v){ return v > 0.5f ? juce::String ("SYN") : juce::String ("FRE"); }, colTime);

                    // ── EXTRA ROUTES (blue) ──
                    for (int ri = 0; ri < 3; ++ri)
                    {
                        auto& route = track.msegs[static_cast<size_t>(msegIdx)].extraRoutes[static_cast<size_t>(ri)];
                        addMK ("D" + juce::String(ri+2), -1, static_cast<float>(LFOEngine::kNumSynthTargets-1),
                            static_cast<float>(route.target),
                            [this, ri](float v){ track.msegs[static_cast<size_t>(msegIdx)].extraRoutes[static_cast<size_t>(ri)].target=static_cast<int>(v); },
                            [](float v){ int t=static_cast<int>(v); return t<0?juce::String("OFF"):juce::String(LFOEngine::getSynthTargetName(t)); }, colRoute);
                        // Attach categorized popup
                        {
                            using PC = KnobComponent::PopupCategory;
                            std::vector<PC> cats;
                            auto mkCat = [](const juce::String& name, std::initializer_list<int> indices) {
                                PC c; c.name = name;
                                for (int i : indices) c.items.push_back ({i, LFOEngine::getSynthTargetName (i)});
                                return c;
                            };
                            { PC c; c.name = "---"; c.items.push_back ({-1, "OFF"}); cats.push_back (c); }
                            cats.push_back (mkCat ("FILTER",    {1, 2, 14, 56, 57, 58, 59}));
                            cats.push_back (mkCat ("AMP",       {3, 15, 16, 17, 39}));
                            cats.push_back (mkCat ("PITCH/PAN", {0, 4}));
                            cats.push_back (mkCat ("OSC",       {9, 10, 11, 12, 13, 18, 54, 55}));
                            cats.push_back (mkCat ("SEND",      {5, 6, 7, 8}));
                            cats.push_back (mkCat ("FM",        {19, 60, 61, 20, 21, 22, 23, 24, 25, 26, 27}));
                            cats.push_back (mkCat ("ELEMENTS",  {28, 29, 30, 31, 32, 33}));
                            cats.push_back (mkCat ("PLAITS",    {34, 35, 36, 37, 38}));
                            cats.push_back (mkCat ("FX",        {40,41,42,43,44,45,46,91,92,99}));
                            cats.push_back (mkCat ("EQ/OUT",    {96,97,98,94,95}));
                            cats.push_back (mkCat ("DUCK",      {124,125,126}));
                            cats.push_back (mkCat ("OTT",       {135,136,137}));
                            cats.push_back (mkCat ("SAT",       {138,139,140,141}));
                            cats.push_back (mkCat ("PHASER",    {142,143,144,145}));
                            cats.push_back (mkCat ("FLANGER",   {146,147,148,149}));
                            cats.push_back (mkCat ("SAMPLER",   {47,48,49,50,51,52,53,89,90,93}));
                            cats.push_back (mkCat ("WAVETABLE", {70, 71, 72, 73, 74, 75}));
                            cats.push_back (mkCat ("GRANULAR", {76,77,78,79,80,81,82,83,84,85,86,87,88}));
                            cats.push_back (mkCat ("X-MOD",     {62, 63, 64, 65, 66, 67}));
                            cats.push_back (mkCat ("MSEG",      {68, 69}));
                            msegKnobs.getLast()->setCategorizedPopup (cats);
                        }
                        addMK ("A" + juce::String(ri+2), -1, 1, route.depth,
                            [this, ri](float v){ track.msegs[static_cast<size_t>(msegIdx)].extraRoutes[static_cast<size_t>(ri)].depth=v; },
                            [](float v){ int p=static_cast<int>(v*100); return p==0?juce::String("OFF"):(p>0?"+":juce::String(""))+juce::String(p)+"%"; }, colRoute);
                    }

                    for (auto* k : msegKnobs)
                    {
                        k->setBounds (cx, ctrlRow.getY(), 48, ctrlRow.getHeight());
                        cx += 50;
                    }
                }
                else
                {
                    for (auto* k : msegKnobs) k->setVisible (false);
                    if (msegTargetBtn) msegTargetBtn->setVisible (false); if (msegRetrigBtn) msegRetrigBtn->setVisible (false); for (auto& b : msegSelBtns) b.setVisible (false);
                }
            }

            int numVisible = endK - startK;
            int knobsPerRow = std::max (1, (numVisible + 1) / 2);
            // LFO tab: force 15 knobs per row (one LFO per row, 3 rows)
            if (currentTab == 2) knobsPerRow = 14;
            // FX tab: DRIVE(3)+CHORUS(3)+DELAY(8)+OTT(3)=17 row1, REVERB(4)+EQ(3)+OUT(3)+DUCK(4)+SAT(5)=19 row2
            if (currentTab == 1 && numVisible >= 40) knobsPerRow = 21;
            else if (currentTab == 1 && numVisible >= 25) knobsPerRow = 17;
            // VOICE tab: all on one row if ≤ 7 knobs
            if (currentTab == 4 && numVisible <= 12) knobsPerRow = numVisible;
            // ENG tab FM: OSC(3)+OP2(3)+OP3(3)+OP4(4)+FB(1)=14 on row1, FILTER(6)+FLT_ENV(4)+AMP(4)=14 on row2
            if (currentTab == 0 && screenMode == 1 && numVisible >= 20) knobsPerRow = 13;
            // ENG tab ANA: OSC(6)+FILTER(6)+FLT_ENV(4)=16 on row1
            if (currentTab == 0 && screenMode == 0 && numVisible >= 29) knobsPerRow = 16;
            // ENG tab SMP: SAMPLE(6)+PITCH(3)+AMP(4)=13 on row1, FILTER(6)+FILT_ENV(4)+FM(4)+STRETCH(5)=19 wraps
            if (currentTab == 0 && screenMode == 4 && numVisible >= 24) knobsPerRow = 13;
            // ENG tab WT: OSC(4)+WARP(4)+UNISON(4)=12 on row1, FILTER(6)+F.ENV(4)+AMP(4)=14 on row2
            if (currentTab == 0 && screenMode == 5 && numVisible >= 20) knobsPerRow = 12;
            // ENG tab GRN: GRAIN(8)+SPREAD(8)+UNISON(3) on row1, FILTER+F.ENV+AMP on row2
            if (currentTab == 0 && screenMode == 6 && numVisible >= 16) knobsPerRow = 19;
            int gapW = (currentTab == 0 || currentTab == 1 || currentTab == 2) ? 4 : 0;
            int availW = row1.getWidth();
            int maxKnobSize = (currentTab == 0 && screenMode == 5) ? 44 : (currentTab == 2 ? 50 : 56);
            int row2Count = numVisible - knobsPerRow;
            int maxRowKnobs = std::max (knobsPerRow, row2Count);
            int knobW = (numVisible > 0) ? std::min (maxKnobSize, std::max (32, availW / maxRowKnobs)) : 50;

            // Build x positions with gaps at group boundaries
            std::vector<int> xPositions (static_cast<size_t>(numVisible), 0);
            int xAcc = 0;
            for (int ki = 0; ki < numVisible; ++ki)
            {
                int absIdx = startK + ki;
                // Check group break
                if (currentTab == 0 && ki > 0)
                {
                    for (int gs : engGroupStarts)
                        if (gs == absIdx) { xAcc += gapW; break; }
                }
                // LFO tab: gap every 15 knobs (between LFO1/LFO2/LFO3)
                if (currentTab == 2 && ki > 0 && (ki % 14 == 0))
                    xAcc += gapW;
                // FX tab: gaps at group boundaries (DRIVE|CHORUS|DELAY|REVERB|EQ|OUT|DUCK)
                if (currentTab == 1 && ki > 0 && (ki == 3 || ki == 6 || ki == 14 || ki == 17 || ki == 21 || ki == 24 || ki == 27 || ki == 31))
                    xAcc += gapW;
                xPositions[static_cast<size_t>(ki)] = xAcc;
                xAcc += knobW;
            }

            // Place knobs
            int ki = 0;
            for (int idx = 0; idx < static_cast<int>(engineKnobs.size()); ++idx)
            {
                auto* k = engineKnobs[idx];
                if (idx >= startK && idx < endK)
                {
                    k->setVisible (true);
                    int rowIdx = ki / knobsPerRow; // 0, 1, or 2+
                    int colIdx = ki % knobsPerRow;

                    if (currentTab == 2 && numRows == 3)
                    {
                        // LFO tab: 3 rows, 1 LFO per row, gap between main(8) and extra routes(6)
                        auto& targetRow = (rowIdx == 0) ? row1 : (rowIdx == 1) ? row2 : row3;
                        int xPos = colIdx * knobW;
                        if (colIdx >= 8) xPos += gapW; // gap before extra routes
                        k->setBounds (targetRow.getX() + xPos, targetRow.getY(), knobW, targetRow.getHeight());
                    }
                    else if (ki < knobsPerRow)
                    {
                        k->setBounds (row1.getX() + xPositions[static_cast<size_t>(ki)], row1.getY(), knobW, row1.getHeight());
                    }
                    else
                    {
                        // Row 2 for non-LFO tabs: rebuild x with gaps
                        int x2 = 0;
                        for (int j = knobsPerRow; j <= ki; ++j)
                        {
                            int ai = startK + j;
                            if (currentTab == 0 && j > knobsPerRow)
                            {
                                for (int gs : engGroupStarts)
                                    if (gs == ai) { x2 += gapW; break; }
                            }
                            if (j < ki) x2 += knobW;
                        }
                        k->setBounds (row2.getX() + x2, row2.getY(), knobW, row2.getHeight());
                    }
                    ki++;
                }
                else
                {
                    k->setVisible (false);
                }
            }
        }
        else
        {
            fmBtn.setVisible (false);
            for (auto& btn : engSelBtns) btn.setVisible (false);
            tabEng.setVisible (false);
            tabFx.setVisible (false);
            tabLfo.setVisible (false);
            tabMseg.setVisible (false);
            tabVoice.setVisible (false);
            pstBtn.setVisible (false);
            arpBtn.setVisible (false);
            harmBtn.setVisible (false);
            kbBtn.setVisible (false);
            pianoKeyboard.setVisible (false);
            if (msegEditor) msegEditor->setVisible (false);
            if (msegTargetBtn) msegTargetBtn->setVisible (false); if (msegRetrigBtn) msegRetrigBtn->setVisible (false); for (auto& b : msegSelBtns) b.setVisible (false);
            for (auto* k : msegKnobs) k->setVisible (false);
            for (auto* k : engineKnobs)
                k->setVisible (false);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Colours_GB::panel2);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (Colours_GB::border);
        g.drawRoundedRectangle (bounds, 4.0f, 0.5f);

        g.setColour (track.muted ? Colours_GB::red.withAlpha (0.5f) : Colours_GB::blue.withAlpha (0.7f));
        g.fillRoundedRectangle (bounds.getX(), bounds.getY() + 2, 3.0f, bounds.getHeight() - 4, 1.5f);

        // ── Synth engine icon + model name ──
        {
            auto labelBounds = nameLabel.getBounds().toFloat();
            // Icon to the left of the name text (in the 20px gap)
            auto iconArea = juce::Rectangle<float>(labelBounds.getX() - 18, labelBounds.getY(),
                                                    16.0f, labelBounds.getHeight());
            int modelIdx = 0;
            if (track.model == SynthModel::Analog) modelIdx = 0;
            else if (track.model == SynthModel::FM) modelIdx = 1;
            else if (track.model == SynthModel::DWGS) modelIdx = 2;
            else if (track.model == SynthModel::Formant) modelIdx = 3;
            else if (track.model == SynthModel::Sampler) modelIdx = 4;
            else if (track.model == SynthModel::Wavetable) modelIdx = 5;
            else if (track.model == SynthModel::Granular) modelIdx = 6;

            EngineIcons::drawSynthIcon (g, iconArea, modelIdx);

            // Model name badge below label
            auto badgeCol = EngineIcons::getSynthModelColour (modelIdx);
            auto badgeArea = juce::Rectangle<float>(labelBounds.getX() - 18, labelBounds.getBottom() + 1,
                                                     labelBounds.getWidth() + 18, 10.0f);
            g.setColour (badgeCol.withAlpha (0.15f));
            g.fillRoundedRectangle (badgeArea, 2.0f);
            g.setColour (badgeCol);
            g.drawRoundedRectangle (badgeArea, 2.0f, 0.6f);
            g.setFont (juce::Font (7.5f, juce::Font::bold));
            g.drawText (EngineIcons::getSynthModelName (modelIdx), badgeArea.toNearestInt(), juce::Justification::centred);
        }

        if (engineOpen)
        {
            g.setColour (Colours_GB::border.withAlpha (0.4f));
            g.drawLine (8, 52, bounds.getWidth() - 8, 52, 0.5f);

            // ── Active tab underline ──
            {
                juce::TextButton* tabs[] = { &tabEng, &tabFx, &tabLfo, &tabMseg, &tabVoice };
                auto& activeTab = *tabs[std::clamp (currentTab, 0, 4)];
                auto tb = activeTab.getBounds();
                g.setColour (Colours_GB::accent);
                g.fillRoundedRectangle (static_cast<float>(tb.getX() + 2), static_cast<float>(tb.getBottom() - 2),
                                        static_cast<float>(tb.getWidth() - 4), 2.0f, 1.0f);
            }

            // Engine selector button colors — all engines show their own color
            juce::Colour btnColors[] = {Colours_GB::accent, juce::Colour (0xff3060cc), Colours_GB::green, juce::Colour (0xffcc40cc), juce::Colour (0xff40b0b0), juce::Colour (0xff50c0e0), juce::Colour (0xffd06090)};
            for (int mi = 0; mi < 7; ++mi)
            {
                auto& btn = engSelBtns[static_cast<size_t>(mi)];
                btn.setColour (juce::TextButton::buttonColourId,
                               mi == screenMode ? btnColors[mi].withAlpha (0.85f) : btnColors[mi].withAlpha (0.3f));
                btn.setColour (juce::TextButton::textColourOffId,
                               mi == screenMode ? juce::Colour (0xff001020) : juce::Colours::white.withAlpha (0.6f));
            }
            // Draw selection marker ▶ on active engine button
            if (currentTab == 0)
            {
                auto& selBtn = engSelBtns[static_cast<size_t>(screenMode)];
                auto sb = selBtn.getBounds();
                float mx = static_cast<float>(sb.getRight()) - 4.0f;
                float my = static_cast<float>(sb.getCentreY());
                juce::Path tri;
                tri.addTriangle (static_cast<float>(sb.getX()) + 1, my - 3, static_cast<float>(sb.getX()) + 1, my + 3, static_cast<float>(sb.getX()) + 5, my);
                g.setColour (juce::Colours::white);
                g.fillPath (tri);
            }

            float panelTop = 53.0f;
            float panelH = bounds.getHeight() - panelTop - 4.0f;
            float panelX = 34.0f;
            float panelW = bounds.getWidth() - 40.0f;

            // Panel background — Serum 2 inspired dark with subtle color tint
            juce::Colour tabTints[] = {
                juce::Colour (0x0c0890a0),  // ENG — dark teal
                juce::Colour (0x0c20b060),  // FX — dark green
                juce::Colour (0x0c4060c0),  // LFO — dark indigo
                juce::Colour (0x0c3070a0)   // VOICE — dark steel
            };
            auto tint = tabTints[currentTab % 4];
            juce::ColourGradient panelGrad (
                juce::Colour (0xff0e1118).interpolatedWith (tint, 0.2f), panelX, panelTop,
                juce::Colour (0xff0a0c14).interpolatedWith (tint, 0.1f), panelX, panelTop + panelH, false);
            g.setGradientFill (panelGrad);
            g.fillRoundedRectangle (panelX, panelTop, panelW, panelH, 5.0f);

            // Panel border — subtle inner glow
            g.setColour (juce::Colour (0x0affffff));
            g.drawRoundedRectangle (panelX + 0.5f, panelTop + 0.5f, panelW - 1, panelH - 1, 4.0f, 0.4f);

            // ── Waveform display (SMP/GRN mode, ENG tab) ──
            if (currentTab == 0 && (screenMode == 4 || screenMode == 6) && track.sampleData && track.sampleData->getNumSamples() > 4)
            {
                auto* buf = track.sampleData.get();
                const float* src = buf->getReadPointer (0);
                int numSmp = buf->getNumSamples();

                // Start AFTER engine button (SMP) to avoid overlap
                float wfX = panelX + 36;  // past fmBtn
                float wfY = panelTop + 2;
                float wfW = panelW - 38;
                float wfH = panelH * 0.38f;
                waveformBounds = {static_cast<int>(wfX), static_cast<int>(wfY),
                                  static_cast<int>(wfW), static_cast<int>(wfH)};

                // Dark background with subtle gradient
                juce::ColourGradient wfBg (juce::Colour (0xff0a0a12), wfX, wfY,
                                           juce::Colour (0xff08080e), wfX, wfY + wfH, false);
                g.setGradientFill (wfBg);
                g.fillRoundedRectangle (wfX, wfY, wfW, wfH, 3.0f);

                // Center line
                float midY = wfY + wfH * 0.5f;
                g.setColour (juce::Colour (0x15ffffff));
                g.drawHorizontalLine (static_cast<int>(midY), wfX, wfX + wfW);

                // Peak waveform display with ZOOM support
                int pixW = static_cast<int>(wfW);
                float visibleFrac = 1.0f / wfZoom;
                float viewStart = wfOffset;
                float viewEnd = viewStart + visibleFrac;
                int smpStart_view = static_cast<int>(viewStart * numSmp);
                int smpEnd_view = std::min (numSmp - 1, static_cast<int>(viewEnd * numSmp));
                float samplesPerPx = static_cast<float>(smpEnd_view - smpStart_view) / static_cast<float>(pixW);

                // Warp mapping: when active, x-axis = beats, source mapped through markers
                bool hasWarp = (track.smpWarp > 0 && track.warpMarkers.size() >= 2 && track.smpBars > 0);
                float warpTotalBeats = 0.0f;
                if (hasWarp) {
                    static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                    warpTotalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                }

                // Lambda: map pixel to source sample index (handles warp)
                auto pixelToSample = [&](int px) -> int {
                    if (hasWarp && warpTotalBeats > 0.001f) {
                        float beatFrac = static_cast<float>(px) / std::max (1.0f, static_cast<float>(pixW));
                        float normPos = viewStart + (viewEnd - viewStart) * beatFrac;
                        float beatPos = normPos * warpTotalBeats;
                        float srcNorm = normPos; // fallback
                        auto& wm = track.warpMarkers;
                        for (size_t m = 0; m + 1 < wm.size(); ++m) {
                            float bpA = wm[m].beatPos, bpB = wm[m + 1].beatPos;
                            if (beatPos >= bpA && (beatPos <= bpB || m + 2 == wm.size())) {
                                float bLen = bpB - bpA, sLen = wm[m + 1].samplePos - wm[m].samplePos;
                                float frac = (bLen > 0.001f) ? std::clamp ((beatPos - bpA) / bLen, 0.0f, 1.0f) : 0.0f;
                                srcNorm = wm[m].samplePos + frac * sLen;
                                break;
                            }
                        }
                        return std::clamp (static_cast<int>(srcNorm * static_cast<float>(numSmp)), 0, numSmp - 1);
                    }
                    return std::clamp (smpStart_view + static_cast<int>(static_cast<float>(px) * samplesPerPx), 0, numSmp - 1);
                };

                juce::Path waveFill, waveLine;
                for (int px = 0; px < pixW; ++px)
                {
                    int s0 = pixelToSample (px);
                    int s1 = pixelToSample (px + 1);
                    if (s1 < s0) std::swap (s0, s1); // warp can reverse order
                    s0 = std::clamp (s0, 0, numSmp - 1);
                    s1 = std::clamp (s1, s0, numSmp - 1);
                    float mn = 0, mx = 0;
                    for (int s = s0; s <= s1; ++s)
                    { mn = std::min (mn, src[s]); mx = std::max (mx, src[s]); }
                    float x = wfX + static_cast<float>(px);
                    if (px == 0) { waveFill.startNewSubPath (x, midY - mx * wfH * 0.45f); waveLine.startNewSubPath (x, midY - (mx + mn) * 0.5f * wfH * 0.45f); }
                    else { waveLine.lineTo (x, midY - (mx + mn) * 0.5f * wfH * 0.45f); }
                    waveFill.lineTo (x, midY - mx * wfH * 0.45f);
                }
                for (int px = pixW - 1; px >= 0; --px)
                {
                    int s0 = pixelToSample (px);
                    int s1 = pixelToSample (px + 1);
                    if (s1 < s0) std::swap (s0, s1);
                    s0 = std::clamp (s0, 0, numSmp - 1);
                    s1 = std::clamp (s1, s0, numSmp - 1);
                    float mn = 0;
                    for (int s = s0; s <= s1; ++s) mn = std::min (mn, src[s]);
                    waveFill.lineTo (wfX + static_cast<float>(px), midY - mn * wfH * 0.45f);
                }
                waveFill.closeSubPath();

                // Fill with gradient — orange when warped, teal when normal
                juce::Colour wfBaseCol = hasWarp ? juce::Colour (0x25a06030) : juce::Colour (0x2530a0a0);
                juce::Colour wfBaseCol2 = hasWarp ? juce::Colour (0x08a06030) : juce::Colour (0x0830a0a0);
                juce::ColourGradient wfFillGrad (wfBaseCol, wfX, midY - wfH * 0.3f,
                                                  wfBaseCol2, wfX, midY + wfH * 0.3f, false);
                g.setGradientFill (wfFillGrad);
                g.fillPath (waveFill);
                g.setColour (hasWarp ? juce::Colour (0xddf0a040) : juce::Colour (0xdd40d8d8));
                g.strokePath (waveLine, juce::PathStrokeType (1.0f));
                // WARP indicator
                if (hasWarp)
                {
                    g.setColour (juce::Colour (0x80ff8040));
                    g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 9.0f, juce::Font::bold));
                    g.drawText ("WARPED", wfX + 3, wfY + 2, 50, 10, juce::Justification::left, false);
                }

                // Convert normalized positions to zoomed pixel positions
                auto normToX = [&](float n) { return wfX + ((n - viewStart) / visibleFrac) * wfW; };

                // Inverse warp: map source position (0-1) → output position (0-1)
                // When warp active, markers must follow the warped waveform
                auto srcToOutput = [&](float srcNorm) -> float {
                    if (!hasWarp || warpTotalBeats < 0.001f) return srcNorm;
                    auto& wm = track.warpMarkers;
                    for (size_t m = 0; m + 1 < wm.size(); ++m) {
                        float spA = wm[m].samplePos, spB = wm[m + 1].samplePos;
                        if (srcNorm >= spA && (srcNorm <= spB || m + 2 == wm.size())) {
                            float sLen = spB - spA;
                            float frac = (sLen > 0.001f) ? std::clamp ((srcNorm - spA) / sLen, 0.0f, 1.0f) : 0.0f;
                            float beatPos = wm[m].beatPos + frac * (wm[m + 1].beatPos - wm[m].beatPos);
                            return beatPos / warpTotalBeats;
                        }
                    }
                    return srcNorm; // fallback
                };

                float startX = normToX (srcToOutput (track.smpStart));
                float endX = normToX (srcToOutput (track.smpEnd));

                // Dim outside region
                if (startX > wfX) { g.setColour (juce::Colour (0x60000000)); g.fillRect (wfX, wfY, startX - wfX, wfH); }
                if (endX < wfX + wfW) { g.setColour (juce::Colour (0x60000000)); g.fillRect (endX, wfY, wfX + wfW - endX, wfH); }

                // Start handle — only if visible
                if (startX >= wfX - 5 && startX <= wfX + wfW + 5)
                {
                    g.setColour (juce::Colour (0xff50ff80));
                    g.fillRect (startX - 1, wfY, 2.0f, wfH);
                    g.fillRect (startX - 4, wfY, 9.0f, 9.0f);
                    g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 7.0f, juce::Font::bold));
                    g.setColour (juce::Colour (0xff000000));
                    g.drawText (track.smpLoop > 0 ? "L" : "S",
                                static_cast<int>(startX - 4), static_cast<int>(wfY), 9, 9, juce::Justification::centred);
                }

                // End handle — only if visible
                if (endX >= wfX - 5 && endX <= wfX + wfW + 5)
                {
                    g.setColour (juce::Colour (0xffff6060));
                    g.fillRect (endX - 1, wfY, 2.0f, wfH);
                    g.fillRect (endX - 4, wfY + wfH - 9, 9.0f, 9.0f);
                    g.setColour (juce::Colour (0xff000000));
                    g.drawText (track.smpLoop > 0 ? "L" : "E",
                                static_cast<int>(endX - 4), static_cast<int>(wfY + wfH - 9), 9, 9, juce::Justification::centred);
                }

                // Loop indicator
                if (track.smpLoop > 0 && startX < wfX + wfW && endX > wfX)
                {
                    float lx0 = std::max (startX, wfX), lx1 = std::min (endX, wfX + wfW);
                    g.setColour (juce::Colour (0x2550c0f0));
                    g.fillRect (lx0, wfY + wfH - 3, lx1 - lx0, 3.0f);
                }

                // ── Grid overlay (bars/beats) + warp markers ──
                bool gridActive = (track.smpWarp > 0 && track.smpBars > 0);
                if (gridActive)
                {
                    WaveformOverlay::drawGridOverlay (g, wfX, wfY, wfW, wfH,
                        viewStart, viewEnd, track.smpBPM, track.smpBars, track.smpWarp, track.gridDiv);
                    WaveformOverlay::drawWarpMarkers (g, wfX, wfY, wfW, wfH,
                        viewStart, viewEnd, track.warpMarkers,
                        warpHoveredMarker, warpDraggedMarker, warpTotalBeats);
                }

                // Sample name + zoom indicator (below ruler if grid active)
                float nameY = gridActive ? wfY + static_cast<float>(WaveformOverlay::kRulerH) + 1.0f
                                         : wfY + 1.0f;
                g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 7.5f, juce::Font::plain));
                g.setColour (juce::Colour (0xa0ffffff));
                auto smpName = juce::File (track.samplePath).getFileNameWithoutExtension();
                if (smpName.isNotEmpty())
                {
                    juce::String display = smpName;
                    if (!sampleFolder.isEmpty() && sampleFolderIdx >= 0)
                        display += "  [" + juce::String (sampleFolderIdx + 1) + "/" + juce::String (sampleFolder.size()) + "]";
                    g.drawText (display, static_cast<int>(wfX + 3), static_cast<int>(nameY),
                                static_cast<int>(wfW - 50), 10, juce::Justification::topLeft);
                }
                // Zoom level (top right)
                if (wfZoom > 1.05f)
                {
                    g.setColour (juce::Colour (0x80ffffff));
                    g.drawText (juce::String (wfZoom, 1) + "x",
                                static_cast<int>(wfX + wfW - 35), static_cast<int>(wfY + 1), 32, 10,
                                juce::Justification::topRight);
                }

                // Border — subtle
                g.setColour (juce::Colour (0x18ffffff));
                g.drawRoundedRectangle (wfX, wfY, wfW, wfH, 3.0f, 0.5f);

                // ── Sampler playhead (screenMode 4) ──
                if (screenMode == 4 && track.smpPlayPos >= 0.0f)
                {
                    float visibleFrac = 1.0f / wfZoom;
                    float mappedPos = srcToOutput (track.smpPlayPos);
                    float posNorm = (mappedPos - wfOffset) / visibleFrac;
                    if (posNorm >= 0.0f && posNorm <= 1.0f)
                    {
                        float px = wfX + posNorm * wfW;
                        g.setColour (juce::Colour (0xddffffff));
                        g.drawLine (px, wfY + 1, px, wfY + wfH - 1, 1.5f);
                        // Small triangle at top
                        juce::Path tri;
                        tri.addTriangle (px - 3, wfY, px + 3, wfY, px, wfY + 4);
                        g.fillPath (tri);
                    }
                }

                // ── Granular: grain position + active grains visualization ──
                if (screenMode == 6)
                {
                    float visibleFrac = 1.0f / wfZoom;

                    // Position indicator (playhead line)
                    float grainNorm = (track.grainPos - wfOffset) / visibleFrac;
                    if (grainNorm >= 0.0f && grainNorm <= 1.0f)
                    {
                        float gx = wfX + grainNorm * wfW;
                        g.setColour (juce::Colour (0xffd06090).withAlpha (0.6f));
                        g.drawLine (gx, wfY + 1, gx, wfY + wfH - 1, 1.0f);
                        juce::Path tri;
                        tri.addTriangle (gx - 3, wfY, gx + 3, wfY, gx, wfY + 4);
                        g.fillPath (tri);
                    }

                    // Active grains — colored by pan/pitch/direction
                    int numGrains = track.grainVisCount;
                    for (int gi = 0; gi < numGrains && gi < SynthTrackState::kMaxVisGrains; ++gi)
                    {
                        auto& gv = track.grainVisData[gi];
                        float gnorm = (gv.pos - wfOffset) / visibleFrac;
                        if (gnorm < -0.05f || gnorm > 1.05f) continue;

                        float gx = wfX + gnorm * wfW;
                        float yScatter = (static_cast<float>(gi % 5) - 2.0f) / 2.5f;
                        float gy = wfY + wfH * (0.5f + yScatter * 0.35f);
                        float radius = 2.0f + gv.amp * 3.5f;
                        float alpha = 0.4f + gv.amp * 0.6f;

                        // ── Color by pan position: L=blue, C=cyan, R=orange ──
                        float panVal = std::clamp (gv.pan, -1.0f, 1.0f); // -1=L, 0=C, +1=R
                        juce::Colour grainCol;
                        if (panVal < 0)
                        {
                            float t = -panVal; // 0=center, 1=hard left
                            grainCol = juce::Colour (
                                static_cast<uint8_t>(0x40 - t * 0x30),  // R: 64→16
                                static_cast<uint8_t>(0xe0 - t * 0x80),  // G: 224→96
                                static_cast<uint8_t>(0xff));             // B: 255 (stays blue)
                        }
                        else
                        {
                            float t = panVal; // 0=center, 1=hard right
                            grainCol = juce::Colour (
                                static_cast<uint8_t>(0x40 + t * 0xb0),  // R: 64→240 (orange)
                                static_cast<uint8_t>(0xe0 - t * 0x50),  // G: 224→144
                                static_cast<uint8_t>(0xff - t * 0xa0)); // B: 255→95
                        }

                        // ── Pitch tint: lower=larger glow, higher=tighter ──
                        float pitchFactor = std::clamp (gv.pitch, 0.25f, 4.0f);
                        float glowSize = radius * (2.5f + (1.0f / pitchFactor) * 1.5f);

                        // Glow (larger, transparent)
                        g.setColour (grainCol.withAlpha (alpha * 0.12f));
                        g.fillEllipse (gx - glowSize, gy - glowSize, glowSize * 2, glowSize * 2);

                        // Core dot
                        g.setColour (grainCol.withAlpha (alpha));
                        g.fillEllipse (gx - radius, gy - radius, radius * 2, radius * 2);

                        // Reverse grain: draw tiny arrow pointing left
                        if (gv.reverse)
                        {
                            g.setColour (juce::Colours::white.withAlpha (alpha * 0.7f));
                            juce::Path arrow;
                            arrow.addTriangle (gx + 3, gy - 2, gx + 3, gy + 2, gx - 2, gy);
                            g.fillPath (arrow);
                        }
                        else if (gv.amp > 0.3f)
                        {
                            // White center highlight for forward grains
                            g.setColour (juce::Colours::white.withAlpha (gv.amp * 0.5f));
                            g.fillEllipse (gx - 1.0f, gy - 1.0f, 2.0f, 2.0f);
                        }
                    }
                }
            }
            else if (currentTab == 0 && (screenMode == 4 || screenMode == 6))
            {
                // No sample loaded — show hint
                float wfX = panelX + 36, wfY = panelTop + 2, wfW = panelW - 38, wfH = panelH * 0.38f;
                g.setColour (juce::Colour (0xff0c0c0c));
                g.fillRoundedRectangle (wfX, wfY, wfW, wfH, 3.0f);
                g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 9.0f, juce::Font::plain));
                g.setColour (juce::Colour (0x50ffffff));
                g.drawText ("LOAD or drag & drop audio file here", static_cast<int>(wfX), static_cast<int>(wfY),
                            static_cast<int>(wfW), static_cast<int>(wfH), juce::Justification::centred);
            }

            // ── Group labels + separators — per-row, no cross-row lines ──
            auto paintLabels = [&](int startIdx, int endIdx,
                                   const std::vector<juce::String>& names,
                                   const std::vector<int>& sizes,
                                   juce::Colour txtCol = juce::Colour (0xaa9aa0b0),
                                   juce::Colour bgCol  = juce::Colour (0x10ffffff),
                                   const std::vector<juce::Colour>& perCatCols = {})
            {
                g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 7.0f, juce::Font::bold));
                int ki = 0;
                int prevRowY = -999;
                for (int fg = 0; fg < static_cast<int>(sizes.size()); ++fg)
                {
                    int absIdx = startIdx + ki;
                    if (absIdx >= endIdx || absIdx >= static_cast<int>(engineKnobs.size())) break;
                    if (!engineKnobs[absIdx]->isVisible()) { ki += sizes[static_cast<size_t>(fg)]; continue; }

                    int gx = engineKnobs[absIdx]->getX();
                    int ky = engineKnobs[absIdx]->getY();
                    int kh = engineKnobs[absIdx]->getHeight();
                    int ly = ky - 10;
                    bool sameRow = (std::abs (ky - prevRowY) < 20);

                    // Separator — only within same row
                    if (ki > 0 && sameRow)
                    {
                        g.setColour (juce::Colour (0x15ffffff));
                        g.drawVerticalLine (gx - 2, static_cast<float>(ly), static_cast<float>(ky + kh));
                    }

                    // Find last knob of this group ON SAME ROW
                    int lastK = std::min (absIdx + sizes[static_cast<size_t>(fg)] - 1,
                                          std::min (endIdx - 1, static_cast<int>(engineKnobs.size()) - 1));
                    while (lastK > absIdx && std::abs (engineKnobs[lastK]->getY() - ky) > 20)
                        --lastK;
                    int gw = std::max (30, engineKnobs[lastK]->getRight() - gx);

                    // Use per-category color if available
                    juce::Colour catCol = (fg < static_cast<int>(perCatCols.size())) ? perCatCols[static_cast<size_t>(fg)] : txtCol;
                    g.setColour (catCol.withAlpha (0.12f));
                    g.fillRoundedRectangle (static_cast<float>(gx), static_cast<float>(ly),
                                            static_cast<float>(gw), 9.0f, 3.0f);
                    g.setColour (catCol.withAlpha (0.85f));
                    if (fg < static_cast<int>(names.size()))
                        g.drawText (names[static_cast<size_t>(fg)], gx, ly, gw, 9,
                                    juce::Justification::centred, false);

                    prevRowY = ky;
                    ki += sizes[static_cast<size_t>(fg)];
                }
            };

            // ── ENG tab ──
            if (currentTab == 0 && !engGroupStarts.empty())
            {
                // Build sizes from engGroupStarts
                std::vector<int> engSizes;
                for (int gi = 0; gi < static_cast<int>(engGroupStarts.size()); ++gi)
                {
                    if (engGroupStarts[static_cast<size_t>(gi)] >= fxStartIdx) break;
                    int nextBound = (gi + 1 < static_cast<int>(engGroupStarts.size())
                                     && engGroupStarts[static_cast<size_t>(gi + 1)] < fxStartIdx)
                                    ? engGroupStarts[static_cast<size_t>(gi + 1)]
                                    : fxStartIdx;
                    engSizes.push_back (nextBound - engGroupStarts[static_cast<size_t>(gi)]);
                }
                paintLabels (engGroupStarts.empty() ? 0 : engGroupStarts[0], fxStartIdx,
                             engGroupNames, engSizes);
            }

            // ── FX tab ──
            if (currentTab == 1)
            {
                paintLabels (fxStartIdx, lfoStartIdx,
                             {"DRIVE","CHORUS","DELAY","OTT","PHASER","REVERB","EQ","OUT","DUCK","SAT","FLANGER"},
                             {3, 3, 8, 3, 4, 4, 3, 3, 4, 5, 4},
                             juce::Colour (0xaa9aa0b0), juce::Colour (0x10ffffff),
                             {juce::Colour(0xffc06040), juce::Colour(0xff50a0d0), juce::Colour(0xff60b080),
                              juce::Colour(0xfff0c040), juce::Colour(0xffd050e0), juce::Colour(0xff6070d8),
                              juce::Colour(0xff50d0b0), juce::Colour(0xff909098), juce::Colour(0xffe08020),
                              juce::Colour(0xffe04070), juce::Colour(0xffa0d040)});
            }

            // ── LFO tab ──
            if (currentTab == 2)
            {
                g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 7.5f, juce::Font::bold));
                static const juce::Colour lfoCols[] = {
                    juce::Colour (0xff50c0b0), juce::Colour (0xffc07090), juce::Colour (0xffc0a030)
                };
                for (int li = 0; li < 3; ++li)
                {
                    int bi = lfoStartIdx + li * 14;
                    if (bi >= static_cast<int>(engineKnobs.size()) || !engineKnobs[bi]->isVisible()) continue;
                    int lx = engineKnobs[bi]->getX();
                    int ky = engineKnobs[bi]->getY();
                    int kh = engineKnobs[bi]->getHeight();
                    int ly = ky - 10;
                    int lastIdx = std::min (bi + 13, static_cast<int>(engineKnobs.size()) - 1);
                    int lw = engineKnobs[lastIdx]->getRight() - lx;

                    // LFO label background
                    g.setColour (lfoCols[li].withAlpha (0.12f));
                    g.fillRoundedRectangle (static_cast<float>(lx), static_cast<float>(ly),
                                            static_cast<float>(lw), 9.0f, 3.0f);
                    g.setColour (lfoCols[li].withAlpha (0.85f));

                    // Show LFO label + live value indicator
                    float lfoVal = lfoDisplay[li];
                    auto& lfoSet = track.lfos[static_cast<size_t>(li)];
                    juce::String lfoLabel = "LFO " + juce::String (li + 1);
                    if (std::abs (lfoSet.depth) > 0.001f)
                        lfoLabel += "  " + LFOEngine::formatHz (lfoSet.rate) + "  d=" + juce::String (static_cast<int>(lfoSet.depth * 100)) + "%";
                    else
                        lfoLabel += "  [OFF]";
                    g.drawText (lfoLabel, lx, ly, lw, 9, juce::Justification::centred, false);

                    // Activity bar — animated indicator below label
                    float barY = static_cast<float>(ly + 9);
                    float barH = 2.0f;
                    float centerX = static_cast<float>(lx) + static_cast<float>(lw) * 0.5f;
                    float barLen = std::abs (lfoVal) * static_cast<float>(lw) * 0.4f;
                    if (barLen > 1.0f)
                    {
                        g.setColour (lfoCols[li].withAlpha (0.7f));
                        if (lfoVal > 0)
                            g.fillRect (centerX, barY, barLen, barH);
                        else
                            g.fillRect (centerX - barLen, barY, barLen, barH);
                    }
                    // Center dot
                    g.setColour (lfoCols[li].withAlpha (0.3f));
                    g.fillRect (centerX - 0.5f, barY, 1.0f, barH);
                }
            }

            // ── VOICE tab ──
            if (currentTab == 4)
            {
                paintLabels (voiceStartIdx, static_cast<int>(engineKnobs.size()),
                             {"VOICE","ARP","CHORD"}, {2, 4, 3});
            }
        }
    }

private:
    int index;
    SynthTrackState& track;
    bool engineOpen = false;
    int screenMode = 0;  // 0=ANA, 1=FM, 2=DWG, 3=FMT, 4=SMP, 5=WT
    int currentTab = 0;  // 0=ENG, 1=FX, 2=LFO, 3=MSEG, 4=VOICE
    int msegIdx = 0;     // 0-2: which MSEG is shown/edited
    bool manualPageOverride = false;
    juce::String lastSavedPresetName;
    juce::String lastSavedPresetFolder;
    int  lastAutoPage = -1;
    bool plockMode = false;
    int  plockTargetStep = -1;
    int  fxStartIdx = 0;
    int  lfoStartIdx = 0;
    int  voiceStartIdx = 0;
    std::vector<int> engGroupStarts;
    std::vector<juce::String> engGroupNames;
    juce::Colour currentAccent { 0xffe0a020 }; // default amber

    juce::Label nameLabel, lenCaption, swingCaption, volCaption, spdCaption;
    juce::OwnedArray<SynthStepButton> synthSteps;
    RatchetRow ratchetRow;
    juce::OwnedArray<juce::TextButton> pageButtons;
    juce::Slider swingSlider, volSlider;
    juce::Label lenLabel;
    juce::ComboBox spdSelector;
    juce::TextButton muteBtn, soloBtn, expandBtn, plkBtn, randBtn, initBtn, copyBtn, pasteBtn, linkBtn;
    juce::TextButton fmBtn; // kept for backward compat, hidden
    std::array<juce::TextButton, 7> engSelBtns; // vertical engine selector: ANA FM DWG FMT SMP WT GRN
    juce::TextButton loadSampleBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<MSEGEditor> msegEditor;
    std::unique_ptr<WavetableEditor> wtEditorEmbed;
    std::unique_ptr<juce::TextButton> msegTargetBtn;
    std::unique_ptr<juce::TextButton> msegRetrigBtn;
    juce::OwnedArray<KnobComponent> msegKnobs;
    juce::TextButton msegSelBtns[3];
    juce::TextButton tabEng {"ENG"}, tabFx {"FX"}, tabLfo {"LFO"}, tabMseg {"MSEG"}, tabVoice {"VOC"}, pstBtn {"PST"};
    juce::TextButton arpBtn {"ARP"};
    juce::TextButton harmBtn {"HARM"};
    juce::TextButton kbBtn {"KB"};
    PianoKeyboard pianoKeyboard;
    bool keyboardOpen = false;
    int harmKey = 0, harmScale = 0, harmOctave = 3, harmNoteDur = 0, harmRhythm = 0, harmStrum = 0;
    juce::OwnedArray<KnobComponent> engineKnobs;
    LEDMeter ledMeter { 10, false };

    static inline StepSequence s_synthClipboard;
    static inline bool s_synthClipboardValid = false;

    void updatePlockHighlight()
    {
        for (int i = 0; i < kMaxSteps; ++i)
            synthSteps[i]->setPlockSelected (i == plockTargetStep);
    }

    void showInitMenu()
    {
        if (onBeforeEdit) onBeforeEdit();
        juce::PopupMenu menu;
        menu.addItem (10, "RESET ENGINE (sound params)");
        menu.addItem (11, "INIT MODULATIONS (LFO + MSEG + MACRO)");
        menu.addSeparator();
        menu.addItem (12, "RESET MACROS (this track)");
        menu.addItem (2, "RESET STEPS ONLY");
        menu.addItem (3, "RESET VELOCITIES (to 100)");
        menu.addItem (4, "RESET GATES (to 100%)");
        menu.addItem (5, "RESET RATCHETS (to 1x)");
        menu.addItem (6, "RESET P-LOCKS");
        menu.addItem (7, "RESET TRIG CONDITIONS");
        menu.addItem (8, "RESET NUDGE");
        menu.addItem (9, "RESET NOTES (to C3)");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&initBtn),
            [this](int result) {
                if (result == 0) return;
                if (result == 10) { track.resetEngine(); rebuildKnobs(); }
                else if (result == 11) {
                    track.resetModulations();
                    if (macroEnginePtr != nullptr)
                        for (auto& mk : macroEnginePtr->macros)
                            mk.assignments.erase (std::remove_if (mk.assignments.begin(), mk.assignments.end(),
                                [this](const MacroAssignment& a){ return a.trackType == 0 && a.trackIndex == index; }), mk.assignments.end());
                    rebuildKnobs();
                }
                else if (result == 12) {
                    if (macroEnginePtr != nullptr)
                        for (auto& mk : macroEnginePtr->macros)
                            mk.assignments.erase (std::remove_if (mk.assignments.begin(), mk.assignments.end(),
                                [this](const MacroAssignment& a){ return a.trackType == 0 && a.trackIndex == index; }), mk.assignments.end());
                }
                else {
                    if (result == 1) { track.resetEngine(); track.resetModulations(); }
                    for (int si = 0; si < kMaxSteps; ++si)
                    {
                        auto& step = track.seq.steps[static_cast<size_t>(si)];
                        if (result == 1) step.reset();
                        else if (result == 2) step.reset();
                        else if (result == 3) step.velocity = 100;
                        else if (result == 4) step.gate = 100;
                        else if (result == 5) { step.ratchet = 1; step.triplet = false; }
                        else if (result == 6) step.plocks.clear();
                        else if (result == 7) step.cond = TrigCondition::Always;
                        else if (result == 8) step.nudge = 0;
                        else if (result == 9) { step.noteIndex = 0; step.octave = 3; }
                    }
                }
                rebuildKnobs();
                refreshSteps();
                ratchetRow.setStepSequence (&track.seq);
            });
    }

    void showRandomizeMenu()
    {
        if (onBeforeEdit) onBeforeEdit();
        // ── Randomize Panel (CallOutBox with category sliders) ──
        struct RndPanel : public juce::Component
        {
            struct Row { juce::String name; float pct = 0.0f; juce::Rectangle<float> sliderRect; };
            std::vector<Row> rows = {
                {"TRIGGERS", 50}, {"VELOCITY", 0}, {"NOTES", 0},
                {"GATE/LEN", 0}, {"ENGINE", 0}, {"FX SENDS", 0}
            };
            std::function<void(float,float,float,float,float,float)> onApply;
            std::function<void()> onClear;
            int dragRow = -1;

            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colour (0xff14161c));
                g.setColour (juce::Colour (0xff404555));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6, 1);
                auto f = juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold);
                g.setFont (f);
                float y = 8;
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    float x = 8, w = static_cast<float>(getWidth()) - 16;
                    // Label
                    g.setColour (juce::Colour (0xaaffffff));
                    g.drawText (rows[i].name, static_cast<int>(x), static_cast<int>(y), 70, 16, juce::Justification::left);
                    // Slider track
                    float sx = x + 74, sw = w - 74 - 38;
                    rows[i].sliderRect = { sx, y + 3, sw, 10 };
                    g.setColour (juce::Colour (0xff2a2d38));
                    g.fillRoundedRectangle (rows[i].sliderRect, 4);
                    // Fill
                    float fillW = sw * rows[i].pct / 100.0f;
                    g.setColour (Colours_GB::accent.withAlpha (0.8f));
                    g.fillRoundedRectangle (sx, y + 3, fillW, 10, 4);
                    // Value
                    g.setColour (juce::Colours::white);
                    g.drawText (juce::String (static_cast<int>(rows[i].pct)) + "%",
                                static_cast<int>(sx + sw + 4), static_cast<int>(y), 34, 16, juce::Justification::left);
                    y += 22;
                }
                // Buttons
                float btnY = y + 6;
                g.setColour (juce::Colour (0xffe8a030));
                g.fillRoundedRectangle (8, btnY, 80, 20, 4);
                g.setColour (juce::Colours::black);
                g.drawText ("RANDOMIZE", 8, static_cast<int>(btnY), 80, 20, juce::Justification::centred);
                g.setColour (juce::Colour (0xff505868));
                g.fillRoundedRectangle (96, btnY, 50, 20, 4);
                g.setColour (juce::Colours::white);
                g.drawText ("CLEAR", 96, static_cast<int>(btnY), 50, 20, juce::Justification::centred);
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                float btnY = 8 + rows.size() * 22 + 6;
                if (e.y >= btnY && e.y < btnY + 20)
                {
                    if (e.x < 90 && onApply)
                        onApply (rows[0].pct, rows[1].pct, rows[2].pct, rows[3].pct, rows[4].pct, rows[5].pct);
                    else if (e.x >= 90 && onClear)
                        onClear();
                    return;
                }
                for (size_t i = 0; i < rows.size(); ++i)
                    if (rows[i].sliderRect.expanded (0, 4).contains (static_cast<float>(e.x), static_cast<float>(e.y)))
                        { dragRow = static_cast<int>(i); updateSlider (e.x); break; }
            }
            void mouseDrag (const juce::MouseEvent& e) override { updateSlider (e.x); }
            void mouseUp (const juce::MouseEvent&) override { dragRow = -1; }

            void updateSlider (int mx)
            {
                if (dragRow < 0 || dragRow >= static_cast<int>(rows.size())) return;
                auto& r = rows[static_cast<size_t>(dragRow)];
                float pct = (static_cast<float>(mx) - r.sliderRect.getX()) / r.sliderRect.getWidth() * 100.0f;
                r.pct = std::clamp (pct, 0.0f, 100.0f);
                repaint();
            }
        };

        auto* panel = new RndPanel();
        panel->setSize (200, static_cast<int>(8 + 6 * 22 + 6 + 20 + 8));
        panel->onApply = [this](float trig, float vel, float note, float gate, float eng, float fx) {
            applyRandomize (trig, vel, note, gate, eng, fx);
        };
        panel->onClear = [this]() {
            for (int si = 0; si < track.length; ++si)
                track.seq.steps[static_cast<size_t>(si)].reset();
            refreshSteps();
        };
        juce::CallOutBox::launchAsynchronously (
            std::unique_ptr<juce::Component> (panel),
            randBtn.getScreenBounds(), nullptr);
    }

    void applyRandomize (float trigPct, float velPct, float notePct, float gatePct, float engPct, float fxPct)
    {
        std::mt19937 rng { std::random_device{}() };
        std::uniform_real_distribution<float> prob (0.0f, 1.0f);

        // ── Step-level randomization ──
        for (int si = 0; si < track.length; ++si)
        {
            auto& step = track.seq.steps[static_cast<size_t>(si)];

            // TRIGGERS: pct = probability of step being active
            if (trigPct > 0.1f)
                step.active = prob (rng) < (trigPct / 100.0f);

            if (!step.active) continue;

            // VELOCITY: ±pct of range (0-127)
            if (velPct > 0.1f)
            {
                float dev = velPct / 100.0f * 127.0f;
                float base = static_cast<float>(step.velocity);
                float rndV = base + (prob (rng) * 2.0f - 1.0f) * dev;
                step.velocity = static_cast<uint8_t>(std::clamp (rndV, 20.0f, 127.0f));
            }

            // NOTES: ±pct of range (0-11)
            if (notePct > 0.1f)
            {
                float dev = notePct / 100.0f * 11.0f;
                float base = static_cast<float>(step.noteIndex);
                float rndN = base + (prob (rng) * 2.0f - 1.0f) * dev;
                step.noteIndex = static_cast<uint8_t>(std::clamp (static_cast<int>(rndN), 0, 11));
            }

            // GATE: ±pct of range (10-200)
            if (gatePct > 0.1f)
            {
                float dev = gatePct / 100.0f * 190.0f;
                float base = static_cast<float>(step.gate);
                float rndG = base + (prob (rng) * 2.0f - 1.0f) * dev;
                step.gate = static_cast<uint8_t>(std::clamp (static_cast<int>(rndG), 10, 200));
            }
        }

        // ── Engine knobs: ±pct of each knob's range ──
        if (engPct > 0.1f)
        {
            int startK = 0, endK = fxStartIdx;
            for (int ki = startK; ki < endK && ki < static_cast<int>(engineKnobs.size()); ++ki)
            {
                auto* k = engineKnobs[ki];
                if (!k->isVisible()) continue;
                float range = k->getMaxValue() - k->getMinValue();
                float dev = engPct / 100.0f * range;
                float newVal = k->getValue() + (prob (rng) * 2.0f - 1.0f) * dev;
                newVal = std::clamp (newVal, k->getMinValue(), k->getMaxValue());
                k->setValue (newVal);
            }
        }

        // ── FX knobs: ±pct of each knob's range ──
        if (fxPct > 0.1f)
        {
            int startK = fxStartIdx, endK = lfoStartIdx;
            for (int ki = startK; ki < endK && ki < static_cast<int>(engineKnobs.size()); ++ki)
            {
                auto* k = engineKnobs[ki];
                if (!k->isVisible()) continue;
                float range = k->getMaxValue() - k->getMinValue();
                float dev = fxPct / 100.0f * range;
                float newVal = k->getValue() + (prob (rng) * 2.0f - 1.0f) * dev;
                newVal = std::clamp (newVal, k->getMinValue(), k->getMaxValue());
                k->setValue (newVal);
            }
        }

        refreshSteps();
    }

    void updatePageButtons()
    {
        for (int i = 0; i < pageButtons.size(); ++i)
            pageButtons[i]->setToggleState (i == track.page, juce::dontSendNotification);
    }

    void updateVisibleSteps()
    {
        int startStep = track.page * kStepsPerPage;
        for (int i = 0; i < kMaxSteps; ++i)
            synthSteps[i]->setVisible (i >= startStep && i < startStep + kStepsPerPage);
        resized();
    }

    void showHarmonyMenu()
    {
        auto progs = HarmonyGen::getProgressions();
        juce::PopupMenu menu;
        juce::PopupMenu keyMenu;
        for (int k = 0; k < 12; ++k)
            keyMenu.addItem (2000 + k, HarmonyGen::noteName (k), true, harmKey == k);
        menu.addSubMenu ("KEY: " + juce::String (HarmonyGen::noteName (harmKey)), keyMenu);
        juce::PopupMenu scaleMenu;
        for (int s = 0; s < HarmonyGen::kNumScales; ++s)
            scaleMenu.addItem (2100 + s, HarmonyGen::scaleName (s), true, harmScale == s);
        menu.addSubMenu ("SCALE: " + juce::String (HarmonyGen::scaleName (harmScale)), scaleMenu);
        juce::PopupMenu octMenu;
        for (int o = 1; o <= 5; ++o)
            octMenu.addItem (2200 + o, juce::String (o), true, harmOctave == o);
        menu.addSubMenu ("OCT: " + juce::String (harmOctave), octMenu);
        // Note duration submenu
        juce::PopupMenu lenMenu;
        const int lenValues[] = {0, 1, 2, 4, 8, 16, 32};
        const char* lenLabels[] = {"AUTO", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS"};
        for (int i = 0; i < 7; ++i)
            lenMenu.addItem (2400 + i, lenLabels[i], true, harmNoteDur == lenValues[i]);
        juce::String curLen = (harmNoteDur == 0) ? "AUTO" :
                              (harmNoteDur == 1) ? "1/16" :
                              (harmNoteDur == 2) ? "1/8" :
                              (harmNoteDur == 4) ? "1/4" :
                              (harmNoteDur == 8) ? "1/2" :
                              (harmNoteDur == 16) ? "1 BAR" : "2 BARS";
        menu.addSubMenu ("LEN: " + curLen, lenMenu);
        // Rhythm submenu
        juce::PopupMenu rhyMenu;
        auto* rhythms = HarmonyGen::getRhythms();
        juce::String lastRhyCat;
        for (int r = 0; r < HarmonyGen::kNumRhythms; ++r)
        {
            juce::String cat (rhythms[r].category);
            if (cat != lastRhyCat) { if (lastRhyCat.isNotEmpty()) rhyMenu.addSeparator();
                lastRhyCat = cat; rhyMenu.addSectionHeader (cat); }
            rhyMenu.addItem (2500 + r, rhythms[r].name, true, harmRhythm == r);
        }
        menu.addSubMenu ("RHYTHM: " + juce::String (rhythms[std::clamp (harmRhythm, 0, HarmonyGen::kNumRhythms - 1)].name), rhyMenu);
        // Strum submenu — free percentage every 5%
        juce::PopupMenu strumHMenu;
        for (int sv = 0; sv <= 100; sv += 5)
            strumHMenu.addItem (2600 + sv / 5, sv == 0 ? juce::String ("OFF") : juce::String (sv) + "%", true, harmStrum == sv);
        menu.addSubMenu ("STRUM: " + (harmStrum > 0 ? juce::String (harmStrum) + "%" : juce::String ("OFF")), strumHMenu);
        menu.addSeparator();
        juce::String lastCat;
        for (int p = 0; p < static_cast<int>(progs.size()); ++p)
        {
            juce::String cat (progs[static_cast<size_t>(p)].category);
            if (cat != lastCat) { if (lastCat.isNotEmpty()) menu.addSeparator(); lastCat = cat;
                menu.addSectionHeader (cat); }
            menu.addItem (2300 + p, progs[static_cast<size_t>(p)].name);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&harmBtn),
            [this, progs](int result) {
                if (result >= 2000 && result < 2012)
                {
                    harmKey = result - 2000;
                    juce::Timer::callAfterDelay (50, [this]() { showHarmonyMenu(); });
                }
                else if (result >= 2100 && result < 2200)
                {
                    harmScale = result - 2100;
                    juce::Timer::callAfterDelay (50, [this]() { showHarmonyMenu(); });
                }
                else if (result >= 2200 && result < 2210)
                {
                    harmOctave = result - 2200;
                    juce::Timer::callAfterDelay (50, [this]() { showHarmonyMenu(); });
                }
                else if (result >= 2400 && result < 2407)
                {
                    const int lv[] = {0, 1, 2, 4, 8, 16, 32};
                    harmNoteDur = lv[result - 2400];
                    juce::Timer::callAfterDelay (50, [this]() { showHarmonyMenu(); });
                }
                else if (result >= 2500 && result < 2500 + HarmonyGen::kNumRhythms)
                {
                    harmRhythm = result - 2500;
                    juce::Timer::callAfterDelay (50, [this]() { showHarmonyMenu(); });
                }
                else if (result >= 2600 && result <= 2620)
                {
                    harmStrum = (result - 2600) * 5;
                    juce::Timer::callAfterDelay (50, [this]() { showHarmonyMenu(); });
                }
                else if (result >= 2300 && result < 2300 + static_cast<int>(progs.size()))
                {
                    if (onBeforeEdit) onBeforeEdit();
                    HarmonyGen::generate (track, harmKey, harmScale, result - 2300, 0, harmOctave, true, harmNoteDur, harmRhythm);
                    // Apply strum to all generated chord steps
                    if (harmStrum > 0)
                        for (auto& step : track.seq.steps)
                            if (step.active && (step.chordMode > 0 || !step.chordNotes.empty()))
                                step.strum = static_cast<uint8_t>(harmStrum);
                    refreshSteps();
                    repaint();
                }
            });
    }

    void refreshSteps()
    {
        for (int i = 0; i < kMaxSteps; ++i)
            synthSteps[i]->repaint();
    }

    void buildEngineKnobs()
    {
        for (auto* k : engineKnobs) removeChildComponent (k);
        engineKnobs.clear();
        engGroupStarts.clear();
        engGroupNames.clear();

        if (screenMode == 0)
        {
            // ══ ANALOG SCREEN — grouped with separators ══
            // ── OSC ──
            currentAccent = juce::Colour (0xff50c0b0); // cyan
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("OSC");
            addKnob ("W1", 0, 4, static_cast<float>(track.w1), [this](float v){ track.w1 = static_cast<int>(v); },
                     [](float v){
                         int w = static_cast<int>(v);
                         const char* names[] = {"SAW","SQR","TRI","SIN","PWM"};
                         return juce::String (w < 5 ? names[w] : "?");
                     }, "w1");
            addKnob ("W2", 0, 4, static_cast<float>(track.w2), [this](float v){ track.w2 = static_cast<int>(v); },
                     [](float v){
                         int w = static_cast<int>(v);
                         const char* names[] = {"SAW","SQR","TRI","SIN","PWM"};
                         return juce::String (w < 5 ? names[w] : "?");
                     }, "w2");
            addKnob ("MIX", 0, 1, track.mix2, [this](float v){ track.mix2 = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "mix2");
            addKnob ("DET", -12, 12, track.detune, [this](float v){ track.detune = v; },
                     [](float v){ return juce::String (v, 1) + "st"; }, "detune");
            addKnob ("SUB", 0, 1, track.subLevel, [this](float v){ track.subLevel = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "subLevel");
            addKnob ("PWM", 0.05f, 0.95f, track.pwm, [this](float v){ track.pwm = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "pwm");
            // ── FILTER ──
            currentAccent = juce::Colour (0xffe09040); // orange
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FILTER");
            addKnob ("FLT", 0, 5, static_cast<float>(track.fModel), [this](float v){ track.fModel = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"CLN","ACD","DRT","SEM","ARP","LQD"};
                         int m = static_cast<int>(v);
                         return juce::String (m < 6 ? n[m] : "?");
                     }, "fModel");
            addKnob ("CUT", 0, 100, track.cut, [this](float v){ track.cut = v; },
                     [](float v){ return juce::String (static_cast<int>(v)) + "%"; }, "cut");
            addKnob ("RES", 0, 1, track.res, [this](float v){ track.res = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "res");
            addKnob ("TYP", 0, 3, static_cast<float>(track.fType), [this](float v){ track.fType = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"LP","HP","BP","NTC"};
                         return juce::String (n[static_cast<int>(v) % 4]);
                     }, "fType");
            addKnob ("dB", 0, 2, static_cast<float>((track.fPoles == 6) ? 0 : (track.fPoles == 12) ? 1 : 2),
                     [this](float v){
                         int p = static_cast<int>(v);
                         track.fPoles = (p == 0) ? 6 : (p == 1) ? 12 : 24;
                     },
                     [](float v){
                         int p = static_cast<int>(v);
                         return juce::String ((p == 0) ? "6dB" : (p == 1) ? "12dB" : "24dB");
                     }, "fPoles");
            addKnob ("ENV", 0, 1, track.fenv, [this](float v){ track.fenv = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fenv");
            // ── FILTER ENVELOPE ──
            currentAccent = juce::Colour (0xffd08050); // warm orange
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FLT ENV");
            addKnob ("A", 0.001f, 2.0f, track.fA, [this](float v){ track.fA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fA");
            addKnob ("D", 0.01f, 2.0f, track.fD, [this](float v){ track.fD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fD");
            addKnob ("S", 0, 1, track.fS, [this](float v){ track.fS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fS");
            addKnob ("R", 0.01f, 3.0f, track.fR, [this](float v){ track.fR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fR");
            // ── AMP ENVELOPE ──
            currentAccent = juce::Colour (0xff70c050); // green
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("AMP");
            addKnob ("A", 0.001f, 2.0f, track.aA, [this](float v){ track.aA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aA");
            addKnob ("D", 0.01f, 2.0f, track.aD, [this](float v){ track.aD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aD");
            addKnob ("S", 0, 1, track.aS, [this](float v){ track.aS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "aS");
            addKnob ("R", 0.01f, 3.0f, track.aR, [this](float v){ track.aR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aR");
            // ── UNISON ──
            currentAccent = juce::Colour (0xff6090c0); // steel blue
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("UNI");
            addKnob ("UNI", 1, 16, static_cast<float>(track.unison), [this](float v){ track.unison = static_cast<int>(v); },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "unison");
            addKnob ("SPR", 0, 1, track.uniSpread, [this](float v){ track.uniSpread = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "uniSpread");
            addKnob ("STR", 0, 1, track.uniStereo, [this](float v){ track.uniStereo = v; },
                     [](float v){ return v < 0.02f ? juce::String ("MONO") : juce::String (static_cast<int>(v * 100)) + "%"; }, "uniStereo");
            // ── CHARACTER ──
            currentAccent = juce::Colour (0xffc07090); // pink
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("CHAR");
            addKnob ("STYLE", 0, 2, static_cast<float>(track.charType), [this](float v){ track.charType = static_cast<int>(v); },
                     [](float v){
                         int t = static_cast<int>(v);
                         const char* names[] = {"WRM","FLD","FRC"};
                         return juce::String (t < 3 ? names[t] : "?");
                     }, "charType");
            addKnob ("CH.D", 0, 1, track.charAmt, [this](float v){ track.charAmt = v; },
                     [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "charAmt");
            // ── LINEAR FM ──
            currentAccent = juce::Colour (0xffc0a030); // gold
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FM");
            addKnob ("FM", 0, 100, track.fmLinAmt, [this](float v){ track.fmLinAmt = v; },
                     [](float v){ return v < 0.5f ? juce::String ("OFF") : juce::String (static_cast<int>(v)); }, "fmLinAmt");
            addKnob ("F.R", 0.25f, 16, track.fmLinRatio, [this](float v){ track.fmLinRatio = v; },
                     [this](float v){
                         if (track.fmLinSnap > 0)
                             return juce::String (static_cast<int>(std::round (v)));
                         return juce::String (v, 2);
                     }, "fmLinRatio");
            addKnob ("SNP", 0, 1, static_cast<float>(track.fmLinSnap), [this](float v){ track.fmLinSnap = static_cast<int>(v); },
                     [](float v){ return static_cast<int>(v) > 0 ? juce::String ("INT") : juce::String ("FRE"); }, "fmLinSnap");
            addKnob ("FDC", 0.01f, 2.0f, track.fmLinDecay, [this](float v){ track.fmLinDecay = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fmLinDecay");
            addKnob ("FSS", 0, 1, track.fmLinSustain, [this](float v){ track.fmLinSustain = v; },
                     [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "fmLinSustain");
        }
        else if (screenMode == 1)
        {
            // ══ FM SCREEN — Per-operator control ══
            // Algorithm selector
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("OSC");
            currentAccent = juce::Colour (0xff50c0b0); // cyan
            addKnob ("ALGO", 0, 7, static_cast<float>(track.fmAlgo), [this](float v){ track.fmAlgo = static_cast<int>(v); },
                     [](float v){
                         const char* names[] = {"SER","2>1","2PR","BRN","CH+","3>1","ORG","ADD"};
                         int a = static_cast<int>(v);
                         return juce::String (a < 8 ? names[a] : "?");
                     }, "fmAlgo");
            // Visual FM algorithm selector popup (Operator-style colored squares)
            engineKnobs.getLast()->onClickPopup = [this]() {
                struct FMAlgoPopup : public juce::Component
                {
                    int currentAlgo = 0;
                    std::function<void(int)> onSelect;

                    FMAlgoPopup (int cur) : currentAlgo (cur) { setSize (420, 130); }

                    void paint (juce::Graphics& g) override
                    {
                        g.fillAll (juce::Colour (0xff1a1d28));
                        const juce::Colour opCols[] = {
                            juce::Colour (0xffe8a030), // OP1 amber/carrier
                            juce::Colour (0xff40c070), // OP2 green
                            juce::Colour (0xff40a0e0), // OP3 blue
                            juce::Colour (0xffc060b0)  // OP4 pink
                        };
                        // 8 algorithms: connections[algo][op] = which op it modulates (-1=output)
                        // Layout: {op4→dest, op3→dest, op2→dest, op1→dest} where dest is op index or -1
                        // Carriers (→output) drawn at bottom, modulators above
                        struct Algo { const char* label; int conn[4]; }; // conn[i] = what OP(i+1) feeds into (0-based, -1=out)
                        const Algo algos[] = {
                            {"SER",  { -1,  0,  1,  2 }}, // 1←2←3←4
                            {"2>1",  { -1,  0,  0,  2 }}, // 1←2, 1←4←3... wait, let me reconsider

                            // Let me define by who modulates whom:
                            // conn[i] = index of op that THIS op modulates. -1 = audio output
                            // OP index: 0=OP1, 1=OP2, 2=OP3, 3=OP4
                            // Algo 0: 4→3→2→1 : conn = {-1, 0, 1, 2}
                            // Algo 1: [3→2]+[4]→1 : conn = {-1, 0, 1, 0}
                            // Algo 2: [4→3]+[2→1] : conn = {-1, 0, -1, 2}
                            // Algo 3: [4→2]+[3]→1 : conn = {-1, 0, 0, 1}
                            // Algo 4: [4→3→1]+[2] : conn = {-1, -1, 0, 2}
                            // Algo 5: [4+3+2]→1 : conn = {-1, 0, 0, 0}
                            // Algo 6: [4→3]+[2]+[1] : conn = {-1, -1, -1, 2}
                            // Algo 7: all carriers : conn = {-1, -1, -1, -1}
                        };
                        // Simplified: draw each algo as positioned boxes
                        int cellW = getWidth() / 4;
                        int cellH = getHeight() / 2;

                        for (int a = 0; a < 8; ++a)
                        {
                            int col = a % 4, row = a / 4;
                            int cx = col * cellW, cy = row * cellH;
                            auto cell = juce::Rectangle<int> (cx + 2, cy + 2, cellW - 4, cellH - 4);

                            // Highlight current
                            if (a == currentAlgo)
                            {
                                g.setColour (juce::Colour (0x40ffffff));
                                g.fillRoundedRectangle (cell.toFloat(), 4.0f);
                                g.setColour (juce::Colour (0xffe8a030));
                                g.drawRoundedRectangle (cell.toFloat(), 4.0f, 1.5f);
                            }

                            // Draw OP boxes and connections per algorithm
                            int bsz = 14; // box size
                            int gap = 4;
                            int startX = cx + (cellW - 4 * bsz - 3 * gap) / 2;
                            int midY = cy + cellH / 2;

                            // Position each OP box — carriers at bottom, modulators at top
                            // Define Y positions: modulator row = midY-12, carrier row = midY+4
                            int modRow = midY - 10, carRow = midY + 6;

                            // Per-algorithm layout (x, y for each OP 1-4)
                            struct OPPos { int x, y; bool carrier; };
                            OPPos ops[4];

                            // Default: all in a row
                            for (int o = 0; o < 4; ++o)
                                ops[o] = { startX + o * (bsz + gap), midY - bsz/2, false };

                            // Customize per algorithm
                            switch (a)
                            {
                                case 0: // 4→3→2→1 serial chain
                                    ops[0] = { startX + 3*(bsz+gap), carRow, true };
                                    ops[1] = { startX + 2*(bsz+gap), modRow, false };
                                    ops[2] = { startX + 1*(bsz+gap), modRow, false };
                                    ops[3] = { startX + 0*(bsz+gap), modRow, false };
                                    break;
                                case 1: // [3→2]+[4]→1
                                    ops[0] = { startX + 2*(bsz+gap), carRow, true };
                                    ops[1] = { startX + 1*(bsz+gap), modRow, false };
                                    ops[2] = { startX + 0*(bsz+gap), modRow, false };
                                    ops[3] = { startX + 3*(bsz+gap), modRow, false };
                                    break;
                                case 2: // [4→3]+[2→1] two pairs
                                    ops[0] = { startX + 1*(bsz+gap), carRow, true };
                                    ops[1] = { startX + 0*(bsz+gap), modRow, false };
                                    ops[2] = { startX + 3*(bsz+gap), carRow, true };
                                    ops[3] = { startX + 2*(bsz+gap), modRow, false };
                                    break;
                                case 3: // [4→2]+[3]→1 branch
                                    ops[0] = { startX + 2*(bsz+gap), carRow, true };
                                    ops[1] = { startX + 1*(bsz+gap), modRow, false };
                                    ops[2] = { startX + 3*(bsz+gap), modRow, false };
                                    ops[3] = { startX + 0*(bsz+gap), modRow, false };
                                    break;
                                case 4: // [4→3→1]+[2]
                                    ops[0] = { startX + 2*(bsz+gap), carRow, true };
                                    ops[1] = { startX + 3*(bsz+gap), carRow, true };
                                    ops[2] = { startX + 1*(bsz+gap), modRow, false };
                                    ops[3] = { startX + 0*(bsz+gap), modRow, false };
                                    break;
                                case 5: // [4+3+2]→1 all mod carrier
                                    ops[0] = { startX + 1*(bsz+gap) + bsz/2, carRow, true };
                                    ops[1] = { startX + 0*(bsz+gap), modRow, false };
                                    ops[2] = { startX + 1*(bsz+gap), modRow, false };
                                    ops[3] = { startX + 2*(bsz+gap), modRow, false };
                                    break;
                                case 6: // [4→3]+[2]+[1] organ
                                    ops[0] = { startX + 0*(bsz+gap), carRow, true };
                                    ops[1] = { startX + 1*(bsz+gap), carRow, true };
                                    ops[2] = { startX + 3*(bsz+gap), carRow, true };
                                    ops[3] = { startX + 2*(bsz+gap), modRow, false };
                                    break;
                                case 7: // all carriers additive
                                    ops[0] = { startX + 0*(bsz+gap), carRow, true };
                                    ops[1] = { startX + 1*(bsz+gap), carRow, true };
                                    ops[2] = { startX + 2*(bsz+gap), carRow, true };
                                    ops[3] = { startX + 3*(bsz+gap), carRow, true };
                                    break;
                            }

                            // Draw connections (arrows from modulator to target)
                            g.setColour (juce::Colour (0x80ffffff));
                            auto drawArrow = [&](int fromOp, int toOp) {
                                int fx = ops[fromOp].x + bsz/2, fy = ops[fromOp].y + bsz;
                                int tx = ops[toOp].x + bsz/2, ty = ops[toOp].y;
                                g.drawLine (static_cast<float>(fx), static_cast<float>(fy),
                                           static_cast<float>(tx), static_cast<float>(ty), 1.0f);
                            };
                            switch (a)
                            {
                                case 0: drawArrow(3,2); drawArrow(2,1); drawArrow(1,0); break;
                                case 1: drawArrow(2,1); drawArrow(1,0); drawArrow(3,0); break;
                                case 2: drawArrow(3,2); drawArrow(1,0); break;
                                case 3: drawArrow(3,1); drawArrow(2,0); drawArrow(1,0); break;
                                case 4: drawArrow(3,2); drawArrow(2,0); break;
                                case 5: drawArrow(1,0); drawArrow(2,0); drawArrow(3,0); break;
                                case 6: drawArrow(3,2); break;
                                case 7: break; // no connections
                            }

                            // Draw OP boxes
                            for (int o = 0; o < 4; ++o)
                            {
                                auto r = juce::Rectangle<int> (ops[o].x, ops[o].y, bsz, bsz).toFloat();
                                g.setColour (opCols[o].withAlpha (ops[o].carrier ? 1.0f : 0.7f));
                                g.fillRoundedRectangle (r, 2.0f);
                                g.setColour (juce::Colours::black);
                                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
                                g.drawText (juce::String (o + 1), r.toNearestInt(), juce::Justification::centred);
                            }

                            // Draw output indicator (small triangle) under carriers
                            g.setColour (juce::Colour (0x60ffffff));
                            for (int o = 0; o < 4; ++o)
                            {
                                if (ops[o].carrier)
                                {
                                    float tx = static_cast<float>(ops[o].x + bsz/2);
                                    float ty = static_cast<float>(ops[o].y + bsz + 2);
                                    juce::Path tri;
                                    tri.addTriangle (tx - 3, ty, tx + 3, ty, tx, ty + 4);
                                    g.fillPath (tri);
                                }
                            }

                            // Algo label
                            g.setColour (juce::Colour (0xaaffffff));
                            g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 8.0f, juce::Font::plain));
                            const char* labels[] = {"Serial","2>1","Pairs","Branch","Chain+","3>1","Organ","Add"};
                            g.drawText (juce::String ("A") + juce::String (a + 1) + " " + labels[a],
                                       cx + 2, cy + cellH - 14, cellW - 4, 12, juce::Justification::centred);
                        }
                    }

                    void mouseDown (const juce::MouseEvent& e) override
                    {
                        int col = e.getPosition().x / (getWidth() / 4);
                        int row = e.getPosition().y / (getHeight() / 2);
                        int algo = std::clamp (row * 4 + col, 0, 7);
                        if (onSelect) onSelect (algo);
                        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
                            cb->dismiss();
                    }
                };

                auto* popup = new FMAlgoPopup (track.fmAlgo);
                popup->onSelect = [this](int algo) {
                    track.fmAlgo = algo;
                    // Update the ALGO knob display
                    for (auto* k : engineKnobs)
                        if (k->getName() == "ALGO") { k->setValue (static_cast<float>(algo), true); break; }
                };
                // Find the ALGO knob to position the popup
                KnobComponent* algoKnob = nullptr;
                for (auto* k : engineKnobs)
                    if (k->getName() == "ALGO") { algoKnob = k; break; }
                if (!algoKnob) return;
                juce::CallOutBox::launchAsynchronously (
                    std::unique_ptr<juce::Component> (popup),
                    algoKnob->getScreenBounds(), nullptr);
            };
            // OP1 (Carrier)
            currentAccent = juce::Colour (0xffe09040); // orange
            addKnob ("C.RT", 0.25f, 16, track.cRatio, [this](float v){ track.cRatio = v; },
                     [](float v){ return juce::String (v, 2); }, "cRatio");
            addKnob ("C.LV", 0, 1, track.cLevel, [this](float v){ track.cLevel = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "cLevel");
            // OP2
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("OP2");
            currentAccent = juce::Colour (0xff60b080); // green
            addKnob ("R2", 0.25f, 16, track.r2, [this](float v){ track.r2 = v; },
                     [](float v){ return juce::String (v, 2); }, "r2");
            addKnob ("L2", 0, 100, track.l2, [this](float v){ track.l2 = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "l2");
            addKnob ("DC2", 0.005f, 3.0f, track.dc2, [this](float v){ track.dc2 = v; },
                     [](float v){ return v < 1.0f ? juce::String (static_cast<int>(v * 1000)) + "ms" : juce::String (v, 1) + "s"; }, "dc2");
            // OP3
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("OP3");
            currentAccent = juce::Colour (0xff6090c0); // blue
            addKnob ("R3", 0.25f, 16, track.r3, [this](float v){ track.r3 = v; },
                     [](float v){ return juce::String (v, 2); }, "r3");
            addKnob ("L3", 0, 100, track.l3, [this](float v){ track.l3 = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "l3");
            addKnob ("DC3", 0.005f, 3.0f, track.dc3, [this](float v){ track.dc3 = v; },
                     [](float v){ return v < 1.0f ? juce::String (static_cast<int>(v * 1000)) + "ms" : juce::String (v, 1) + "s"; }, "dc3");
            // OP4
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("OP4");
            currentAccent = juce::Colour (0xffc07090); // pink
            addKnob ("R4", 0.25f, 16, track.r4, [this](float v){ track.r4 = v; },
                     [](float v){ return juce::String (v, 2); }, "r4");
            addKnob ("L4", 0, 100, track.l4, [this](float v){ track.l4 = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "l4");
            addKnob ("DC4", 0.005f, 3.0f, track.dc4, [this](float v){ track.dc4 = v; },
                     [](float v){ return v < 1.0f ? juce::String (static_cast<int>(v * 1000)) + "ms" : juce::String (v, 1) + "s"; }, "dc4");
            addKnob ("FB", 0, 1, track.fmFeedback, [this](float v){ track.fmFeedback = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fmFeedback");
            // Filter
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FILTER");
            currentAccent = juce::Colour (0xffe09040); // orange
            addKnob ("FLT", 0, 5, static_cast<float>(track.fModel), [this](float v){ track.fModel = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"CLN","ACD","DRT","SEM","ARP","LQD"};
                         int m = static_cast<int>(v);
                         return juce::String (m < 6 ? n[m] : "?");
                     }, "fModel");
            addKnob ("CUT", 0, 100, track.cut, [this](float v){ track.cut = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "cut");
            addKnob ("RES", 0, 1, track.res, [this](float v){ track.res = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "res");
            addKnob ("TYP", 0, 3, static_cast<float>(track.fType), [this](float v){ track.fType = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"LP","HP","BP","NTC"};
                         return juce::String (n[static_cast<int>(v) % 4]);
                     }, "fType");
            addKnob ("dB", 0, 2, static_cast<float>((track.fPoles == 6) ? 0 : (track.fPoles == 12) ? 1 : 2),
                     [this](float v){
                         int p = static_cast<int>(v);
                         track.fPoles = (p == 0) ? 6 : (p == 1) ? 12 : 24;
                     },
                     [](float v){
                         int p = static_cast<int>(v);
                         return juce::String ((p == 0) ? "6dB" : (p == 1) ? "12dB" : "24dB");
                     }, "fPoles");
            addKnob ("FEV", 0, 1, track.fenv, [this](float v){ track.fenv = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fenv");
            // ── FILTER ENVELOPE ──
            currentAccent = juce::Colour (0xffd08050); // warm orange (same as Analog)
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FLT ENV");
            addKnob ("A", 0.001f, 2.0f, track.fA, [this](float v){ track.fA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fA");
            addKnob ("D", 0.01f, 2.0f, track.fD, [this](float v){ track.fD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fD");
            addKnob ("S", 0, 1, track.fS, [this](float v){ track.fS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fS");
            addKnob ("R", 0.01f, 3.0f, track.fR, [this](float v){ track.fR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fR");
            // Carrier ADSR
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("AMP");
            currentAccent = juce::Colour (0xff70c050); // green
            addKnob ("A", 0.001f, 2.0f, track.cA, [this](float v){ track.cA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "cA");
            addKnob ("D", 0.01f, 2.0f, track.cD, [this](float v){ track.cD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "cD");
            addKnob ("S", 0, 1, track.cS, [this](float v){ track.cS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "cS");
            addKnob ("R", 0.01f, 3.0f, track.cR, [this](float v){ track.cR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "cR");
        }
        else if (screenMode == 2)
        {
            // ══ ELEMENTS SCREEN — Modal synthesis ══
            // ── EXCITER ──
            currentAccent = juce::Colour (0xffe09040); // orange
            addKnob ("BOW", 0, 1, track.elemBow, [this](float v){ track.elemBow = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemBow");
            addKnob ("BLW", 0, 1, track.elemBlow, [this](float v){ track.elemBlow = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemBlow");
            addKnob ("STK", 0, 1, track.elemStrike, [this](float v){ track.elemStrike = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemStrike");
            addKnob ("CNT", 0, 1, track.elemContour, [this](float v){ track.elemContour = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemContour");
            addKnob ("MLT", 0, 1, track.elemMallet, [this](float v){ track.elemMallet = v; },
                     [](float v){ return v < 0.33f ? juce::String ("SFT") : v < 0.66f ? juce::String ("MED") : juce::String ("HRD"); }, "elemMallet");
            addKnob ("FLW", 0, 1, track.elemFlow, [this](float v){ track.elemFlow = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemFlow");
            // ── RESONATOR ──
            currentAccent = juce::Colour (0xff50c0b0); // cyan
            addKnob ("GEO", 0, 1, track.elemGeometry, [this](float v){ track.elemGeometry = v; },
                     [](float v){
                         if (v < 0.33f) return juce::String ("STR");
                         if (v < 0.66f) return juce::String ("MTL");
                         return juce::String ("DSN");
                     }, "elemGeometry");
            addKnob ("BRT", 0, 1, track.elemBright, [this](float v){ track.elemBright = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemBright");
            addKnob ("DMP", 0, 1, track.elemDamping, [this](float v){ track.elemDamping = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemDamping");
            addKnob ("POS", 0, 1, track.elemPosition, [this](float v){ track.elemPosition = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemPosition");
            // ── SPACE ──
            currentAccent = juce::Colour (0xff6070d8); // indigo
            addKnob ("SPC", 0, 1, track.elemSpace, [this](float v){ track.elemSpace = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "elemSpace");
            addKnob ("PTCH", 0, 1, track.elemPitch, [this](float v){ track.elemPitch = v; },
                     [](float v){
                         int st = static_cast<int>((v - 0.5f) * 48.0f);
                         if (st == 0) return juce::String ("0");
                         return juce::String (st > 0 ? "+" : "") + juce::String (st);
                     }, "elemPitch");
        }
        else if (screenMode == 3)
        {
            // ══ PLAITS SCREEN — Multi-model synthesis ══
            currentAccent = juce::Colour (0xff50c0b0); // cyan - model
            addKnob ("MDL", 0, 15, static_cast<float>(track.plaitsModel), [this](float v){ track.plaitsModel = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"VA","WSH","FM2","GRN","ADD","WTB","STR","NOS",
                                            "MOD","CHD","SPH","PHS","HRM","RSN","PRT","BSS"};
                         int m = static_cast<int>(v);
                         return juce::String (m < 16 ? n[m] : "?");
                     }, "plaitsModel");
            currentAccent = juce::Colour (0xffe09040); // orange - sound
            addKnob ("HARM", 0, 1, track.plaitsHarmonics, [this](float v){ track.plaitsHarmonics = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "plaitsHarmonics");
            addKnob ("TMBR", 0, 1, track.plaitsTimbre, [this](float v){ track.plaitsTimbre = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "plaitsTimbre");
            addKnob ("MRPH", 0, 1, track.plaitsMorph, [this](float v){ track.plaitsMorph = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "plaitsMorph");
            currentAccent = juce::Colour (0xff70c050); // green - envelope
            addKnob ("DCY", 0, 1, track.plaitsDecay, [this](float v){ track.plaitsDecay = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "plaitsDecay");
            addKnob ("LPG", 0, 1, track.plaitsLpgColor, [this](float v){ track.plaitsLpgColor = v; },
                     [](float v){
                         if (v < 0.33f) return juce::String ("VCA");
                         if (v < 0.66f) return juce::String ("MIX");
                         return juce::String ("VCF");
                     }, "plaitsLpgColor");
            // ── FILTER ──
            currentAccent = juce::Colour (0xffd06070); // red-pink
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FILTER");
            addKnob ("FLT", 0, 5, static_cast<float>(track.fModel), [this](float v){ track.fModel = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"CLN","ACD","DRT","SEM","ARP","LQD"}; return juce::String (n[static_cast<int>(v) % 6]); }, "fModel");
            addKnob ("CUT", 0, 100, track.cut, [this](float v){ track.cut = v; },
                     [](float v){ float hz = 20.0f * std::pow (1000.0f, v / 100.0f); return (hz >= 1000.0f) ? juce::String (hz / 1000.0f, 1) + "k" : juce::String (static_cast<int>(hz)); }, "cut");
            addKnob ("RES", 0, 1, track.res, [this](float v){ track.res = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "res");
            addKnob ("TYP", 0, 2, static_cast<float>(track.fType), [this](float v){ track.fType = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"LP","HP","BP"}; return juce::String (n[static_cast<int>(v) % 3]); }, "fType");
            addKnob ("POL", 0, 2, static_cast<float>(track.fPoles == 6 ? 0 : (track.fPoles == 12 ? 1 : 2)),
                     [this](float v){ const int p[] = {6, 12, 24}; track.fPoles = p[static_cast<int>(v) % 3]; },
                     [](float v){ const char* n[] = {"6dB","12dB","24dB"}; return juce::String (n[static_cast<int>(v) % 3]); }, "fPoles");
            // ── FILTER ENVELOPE ──
            currentAccent = juce::Colour (0xffb06090); // magenta
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("F.ENV");
            addKnob ("ENV", 0, 100, track.fenv, [this](float v){ track.fenv = v; },
                     [](float v){ return v < 0.5f ? juce::String ("OFF") : juce::String (static_cast<int>(v)); }, "fenv");
            addKnob ("F.A", 0.001f, 2.0f, track.fA, [this](float v){ track.fA = v; },
                     [](float v){ return (v < 0.1f) ? juce::String (static_cast<int>(v * 1000)) + "ms" : juce::String (v, 2) + "s"; }, "fA");
            addKnob ("F.D", 0.001f, 2.0f, track.fD, [this](float v){ track.fD = v; },
                     [](float v){ return (v < 0.1f) ? juce::String (static_cast<int>(v * 1000)) + "ms" : juce::String (v, 2) + "s"; }, "fD");
            addKnob ("F.S", 0, 1, track.fS, [this](float v){ track.fS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fS");
            addKnob ("F.R", 0.001f, 4.0f, track.fR, [this](float v){ track.fR = v; },
                     [](float v){ return (v < 0.1f) ? juce::String (static_cast<int>(v * 1000)) + "ms" : juce::String (v, 2) + "s"; }, "fR");
        }
        else if (screenMode == 4)
        {
            // ══ SAMPLER SCREEN — Sample playback ══
            // ── SAMPLE ──
            currentAccent = juce::Colour (0xff40b0b0); // teal
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("SAMPLE");
            addKnob ("STRT", 0, 1, track.smpStart, [this](float v){ track.smpStart = v; repaint(); },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpStart");
            addKnob ("END", 0, 1, track.smpEnd, [this](float v){ track.smpEnd = v; repaint(); },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpEnd");
            addKnob ("GAIN", 0, 2, track.smpGain, [this](float v){ track.smpGain = v; },
                     [](float v){ return juce::String (v, 1); }, "smpGain");
            addKnob ("LOOP", 0, 1, static_cast<float>(track.smpLoop), [this](float v){ track.smpLoop = static_cast<int>(v); repaint(); },
                     [](float v){ return static_cast<int>(v) > 0 ? juce::String ("ON") : juce::String ("OFF"); }, "smpLoop");
            addKnob ("PLAY", 0, 1, static_cast<float>(track.smpPlayMode), [this](float v){ track.smpPlayMode = static_cast<int>(v); },
                     [](float v){ return static_cast<int>(v) > 0 ? juce::String ("GATE") : juce::String ("1SH"); }, "smpPlayMode");
            addKnob ("REV", 0, 1, static_cast<float>(track.smpReverse), [this](float v){ track.smpReverse = static_cast<int>(v); },
                     [](float v){ return static_cast<int>(v) > 0 ? juce::String ("ON") : juce::String ("OFF"); }, "smpReverse");
            // ── PITCH ──
            currentAccent = juce::Colour (0xff50c0b0); // cyan
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("PITCH");
            addKnob ("TUNE", -24, 24, track.smpTune, [this](float v){ track.smpTune = v; },
                     [](float v){ int s = static_cast<int>(v); return (s >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (s) + "st"; }, "smpTune");
            addKnob ("FINE", -1, 1, track.smpFine, [this](float v){ track.smpFine = v; },
                     [](float v){ int c = static_cast<int>(v * 100); return (c >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (c) + "ct"; }, "smpFine");
            addKnob ("ROOT", 0, 127, static_cast<float>(track.smpRootNote), [this](float v){ track.smpRootNote = static_cast<int>(v); },
                     [](float v){
                         int n = static_cast<int>(v);
                         const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                         return juce::String (names[n % 12]) + juce::String (n / 12 - 2);
                     }, "smpRootNote");
            // ── AMP ENVELOPE ──
            currentAccent = juce::Colour (0xff70c050); // green
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("AMP");
            addKnob ("A", 0.001f, 2.0f, track.smpA, [this](float v){ track.smpA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpA");
            addKnob ("D", 0.01f, 2.0f, track.smpD, [this](float v){ track.smpD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpD");
            addKnob ("S", 0, 1, track.smpS, [this](float v){ track.smpS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpS");
            addKnob ("R", 0.01f, 3.0f, track.smpR, [this](float v){ track.smpR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpR");
            // ── FILTER ──
            currentAccent = juce::Colour (0xffe09040); // orange
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FILTER");
            addKnob ("FLT", 0, 5, static_cast<float>(track.smpFModel), [this](float v){ track.smpFModel = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"CLN","ACD","DRT","SEM","ARP","LQD"};
                         int m = static_cast<int>(v);
                         return juce::String (m < 6 ? n[m] : "?");
                     }, "smpFModel");
            addKnob ("CUT", 0, 100, track.smpCut, [this](float v){ track.smpCut = v; },
                     [](float v){ return juce::String (static_cast<int>(v)) + "%"; }, "smpCut");
            addKnob ("RES", 0, 1, track.smpRes, [this](float v){ track.smpRes = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpRes");
            addKnob ("TYP", 0, 3, static_cast<float>(track.smpFType), [this](float v){ track.smpFType = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"LP","HP","BP","NTC"};
                         return juce::String (n[static_cast<int>(v) % 4]);
                     }, "smpFType");
            addKnob ("dB", 0, 2, static_cast<float>((track.smpFPoles == 6) ? 0 : (track.smpFPoles == 12) ? 1 : 2),
                     [this](float v){
                         int p = static_cast<int>(v);
                         track.smpFPoles = (p == 0) ? 6 : (p == 1) ? 12 : 24;
                     },
                     [](float v){
                         int p = static_cast<int>(v);
                         return juce::String ((p == 0) ? "6dB" : (p == 1) ? "12dB" : "24dB");
                     }, "smpFPoles");
            addKnob ("ENV", -100, 100, track.smpFiltEnv, [this](float v){ track.smpFiltEnv = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "smpFiltEnv");
            // ── FILT ENV ──
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FILT ENV");
            addKnob ("A", 0.001f, 2.0f, track.smpFiltA, [this](float v){ track.smpFiltA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFiltA");
            addKnob ("D", 0.01f, 2.0f, track.smpFiltD, [this](float v){ track.smpFiltD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFiltD");
            addKnob ("S", 0, 1, track.smpFiltS, [this](float v){ track.smpFiltS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpFiltS");
            addKnob ("R", 0.01f, 3.0f, track.smpFiltR, [this](float v){ track.smpFiltR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFiltR");
            currentAccent = juce::Colour (0xff6090dd); // blue
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FM MOD");
            addKnob ("FM", 0, 1, track.smpFmAmt, [this](float v){ track.smpFmAmt = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpFmAmt");
            addKnob ("RAT", 0.5f, 16, track.smpFmRatio, [this](float v){ track.smpFmRatio = v; },
                     [](float v){ return juce::String (v, 1) + "x"; }, "smpFmRatio");
            addKnob ("F.A", 0.001f, 1.0f, track.smpFmEnvA, [this](float v){ track.smpFmEnvA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFmEnvA");
            addKnob ("F.D", 0.01f, 2.0f, track.smpFmEnvD, [this](float v){ track.smpFmEnvD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFmEnvD");
            addKnob ("F.S", 0, 1, track.smpFmEnvS, [this](float v){ track.smpFmEnvS = v; },
                     [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "smpFmEnvS");
            // ── STRETCH ──
            currentAccent = juce::Colour (0xffc0a030); // gold
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("STRETCH");
            addKnob ("STRC", 0.25f, 4.0f, track.smpStretch, [this](float v){ track.smpStretch = v; },
                     [](float v){ return juce::String (v, 2) + "x"; }, "smpStretch");
            addKnob ("WARP", 0, 4, static_cast<float>(track.smpWarp), [this](float v){ track.smpWarp = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"OFF","BEAT","TEXR","RPTC","BTS2"};
                         return juce::String (n[static_cast<int>(v) % 5]);
                     }, "smpWarp");
            addKnob ("BPM", 40, 220, track.smpBPM, [this](float v){ track.smpBPM = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "smpBPM");
            addKnob ("SYNC", 0, 1, static_cast<float>(track.smpBpmSync), [this](float v){ track.smpBpmSync = static_cast<int>(v); },
                     [](float v){ return static_cast<int>(v) == 0 ? juce::String ("INT") : juce::String ("DAW"); }, "smpBpmSync");
            addKnob ("RATE", -3, 3, static_cast<float>(track.smpSyncMul), [this](float v){ track.smpSyncMul = static_cast<int>(v); },
                     [](float v){
                         int iv = static_cast<int>(v);
                         if (iv == 0) return juce::String ("1x");
                         if (iv > 0) return juce::String ("x") + juce::String (1 << iv);
                         return juce::String ("/") + juce::String (1 << (-iv));
                     }, "smpSyncMul");
            addKnob ("BARS", 0, 8, static_cast<float>(track.smpBars), [this](float v){
                         track.smpBars = std::clamp (static_cast<int>(v + 0.5f), 0, 8);
                     },
                     [](float v){
                         int b = static_cast<int>(v + 0.5f);
                         const char* n[] = {"AUTO","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"};
                         return juce::String (n[std::clamp (b, 0, 8)]);
                     }, "smpBars");
        }
        else if (screenMode == 5)
        {
            // ══ WAVETABLE ENGINE ══
            // ── OSC ──
            currentAccent = juce::Colour (0xff50c0e0); // cyan (WT color)
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("OSC");
            addKnob ("POS1", 0, 1, track.wtPos1, [this](float v){ track.wtPos1 = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "wtPos1");
            addKnob ("POS2", 0, 1, track.wtPos2, [this](float v){ track.wtPos2 = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "wtPos2");
            addKnob ("MIX", 0, 1, track.wtMix, [this](float v){ track.wtMix = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "wtMix");
            addKnob ("DET", -12, 12, track.detune, [this](float v){ track.detune = v; },
                     [](float v){ return juce::String (v, 1) + "st"; }, "detune");
            // ── WARP ──
            currentAccent = juce::Colour (0xff8060d0); // purple
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("WARP");
            addKnob ("WRP1", 0, 11, static_cast<float>(track.wtWarp1), [this](float v){ track.wtWarp1 = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"OFF","BND+","BND-","B+-","ASYM","FM","QNTZ","MIR","SQZ","WRP","SYN","SAT"};
                         return juce::String (n[static_cast<int>(v) % 12]);
                     }, "wtWarp1");
            addKnob ("AMT1", 0, 1, track.wtWarpAmt1, [this](float v){ track.wtWarpAmt1 = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "wtWarpAmt1");
            addKnob ("WRP2", 0, 11, static_cast<float>(track.wtWarp2), [this](float v){ track.wtWarp2 = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"OFF","BND+","BND-","B+-","ASYM","FM","QNTZ","MIR","SQZ","WRP","SYN","SAT"};
                         return juce::String (n[static_cast<int>(v) % 12]);
                     }, "wtWarp2");
            addKnob ("AMT2", 0, 1, track.wtWarpAmt2, [this](float v){ track.wtWarpAmt2 = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "wtWarpAmt2");
            // ── UNISON ──
            currentAccent = juce::Colour (0xff40b0b0); // teal
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("UNISON");
            addKnob ("UNI", 1, 16, static_cast<float>(track.unison), [this](float v){ track.unison = static_cast<int>(v); },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "unison");
            addKnob ("SPRD", 0, 1, track.uniSpread, [this](float v){ track.uniSpread = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "uniSpread");
            addKnob ("STER", 0, 1, track.uniStereo, [this](float v){ track.uniStereo = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "uniStereo");
            addKnob ("SUB", 0, 1, track.wtSubLevel, [this](float v){ track.wtSubLevel = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "wtSubLevel");
            // ── FILTER ──
            currentAccent = juce::Colour (0xffe09040); // orange
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FILTER");
            addKnob ("FLT", 0, 5, static_cast<float>(track.fModel), [this](float v){ track.fModel = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"CLN","ACD","DRT","SEM","ARP","LQD"};
                         int m = static_cast<int>(v);
                         return juce::String (m < 6 ? n[m] : "?");
                     }, "fModel");
            addKnob ("CUT", 0, 100, track.cut, [this](float v){ track.cut = v; },
                     [](float v){ return juce::String (static_cast<int>(v)) + "%"; }, "cut");
            addKnob ("RES", 0, 1, track.res, [this](float v){ track.res = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "res");
            addKnob ("TYP", 0, 3, static_cast<float>(track.fType), [this](float v){ track.fType = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"LP","HP","BP","NTC"};
                         return juce::String (n[static_cast<int>(v) % 4]);
                     }, "fType");
            addKnob ("dB", 0, 2, static_cast<float>((track.fPoles == 6) ? 0 : (track.fPoles == 12) ? 1 : 2),
                     [this](float v){
                         int p = static_cast<int>(v);
                         track.fPoles = (p == 0) ? 6 : (p == 1) ? 12 : 24;
                     },
                     [](float v){
                         int p = static_cast<int>(v);
                         return juce::String ((p == 0) ? "6dB" : (p == 1) ? "12dB" : "24dB");
                     }, "fPoles");
            addKnob ("ENV", 0, 100, track.fenv, [this](float v){ track.fenv = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "fenv");
            // ── FILTER ENVELOPE ──
            currentAccent = juce::Colour (0xffd08030); // dark orange
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("F.ENV");
            addKnob ("F.A", 0.001f, 2.0f, track.fA, [this](float v){ track.fA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fA");
            addKnob ("F.D", 0.01f, 2.0f, track.fD, [this](float v){ track.fD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fD");
            addKnob ("F.S", 0, 1, track.fS, [this](float v){ track.fS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fS");
            addKnob ("F.R", 0.01f, 3.0f, track.fR, [this](float v){ track.fR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fR");
            // ── AMP ENVELOPE ──
            currentAccent = juce::Colour (0xff70c050); // green
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("AMP");
            addKnob ("A", 0.001f, 2.0f, track.aA, [this](float v){ track.aA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aA");
            addKnob ("D", 0.01f, 2.0f, track.aD, [this](float v){ track.aD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aD");
            addKnob ("S", 0, 1, track.aS, [this](float v){ track.aS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "aS");
            addKnob ("R", 0.01f, 3.0f, track.aR, [this](float v){ track.aR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aR");
        }
        else if (screenMode == 6) // ═══ GRANULAR ═══
        {
            // ── GRAIN ──
            currentAccent = juce::Colour (0xffd06090); // rose
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("GRAIN");
            addKnob ("MODE", 0, 4, static_cast<float>(track.grainMode), [this](float v){ track.grainMode = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"STRD","CLOD","SCRB","GLTC","FLUX"}; return juce::String (n[static_cast<int>(v) % 5]); }, "grainMode");
            addKnob ("POS", 0, 1, track.grainPos, [this](float v){ track.grainPos = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "grainPos");
            addKnob ("SIZE", 5, 500, track.grainSize, [this](float v){ track.grainSize = v; },
                     [](float v){ return v < 100 ? juce::String (static_cast<int>(v)) + "ms" : juce::String (v/1000,2) + "s"; }, "grainSize");
            addKnob ("DENS", 1, 100, track.grainDensity, [this](float v){ track.grainDensity = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "grainDensity");
            addKnob ("SPRY", 0, 1, track.grainSpray, [this](float v){ track.grainSpray = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "grainSpray");
            addKnob ("SCAN", -1, 1, track.grainScan, [this](float v){ track.grainScan = v; },
                     [](float v){ return v < -0.01f ? juce::String (static_cast<int>(v * 100)) + "%" :
                                         v > 0.01f ? juce::String ("+" + juce::String (static_cast<int>(v * 100))) + "%" : juce::String ("OFF"); }, "grainScan");
            addKnob ("TEXR", 0, 1, track.grainTexture, [this](float v){ track.grainTexture = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "grainTexture");
            addKnob ("FDBK", 0, 1, track.grainFeedback, [this](float v){ track.grainFeedback = v; },
                     [](float v){ return v < 0.01f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "grainFeedback");
            // ── SPREAD ──
            currentAccent = juce::Colour (0xffe07080); // lighter rose
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("SPREAD");
            addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                     [](float v){ return juce::String (v, 1) + "st"; }, "tune");
            addKnob ("PTCH", -24, 24, track.grainPitch, [this](float v){ track.grainPitch = v; },
                     [](float v){ return juce::String (v, 1) + "st"; }, "grainPitch");
            addKnob ("PAN", 0, 1, track.grainPan, [this](float v){ track.grainPan = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "grainPan");
            addKnob ("SHPE", 0, 5, static_cast<float>(track.grainShape), [this](float v){ track.grainShape = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"HANN","TRI","RECT","TUKY","GAUS","SAW"}; return juce::String (n[static_cast<int>(v) % 6]); }, "grainShape");
            addKnob ("TILT", 0, 100, track.grainTilt, [this](float v){ track.grainTilt = v; },
                     [](float v){ return v < 25 ? juce::String ("PERC") : v > 75 ? juce::String ("SWEL") : juce::String (static_cast<int>(v)) + "%"; }, "grainTilt");
            addKnob ("DIR", 0, 2, static_cast<float>(track.grainDir), [this](float v){ track.grainDir = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"FWD","REV","RND"}; return juce::String (n[static_cast<int>(v) % 3]); }, "grainDir");
            addKnob ("QUNT", 0, 4, static_cast<float>(track.grainQuantize), [this](float v){ track.grainQuantize = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"OFF","OCT","5TH","TRAD","SCAL"}; return juce::String (n[static_cast<int>(v) % 5]); }, "grainQuantize");
            addKnob ("FREZ", 0, 1, track.grainFreeze ? 1.0f : 0.0f, [this](float v){ track.grainFreeze = (v > 0.5f); },
                     [](float v){ return v > 0.5f ? juce::String ("ON") : juce::String ("OFF"); }, "grainFreeze");
            // ── UNISON ──
            currentAccent = juce::Colour (0xff9070d0); // purple
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("UNISON");
            addKnob ("UNI", 1, 8, static_cast<float>(track.grainUniVoices), [this](float v){ track.grainUniVoices = static_cast<int>(v); },
                     [](float v){ int iv = static_cast<int>(v); return iv <= 1 ? juce::String ("OFF") : juce::String (iv); }, "grainUniVoices");
            addKnob ("DETN", 0, 100, track.grainUniDetune, [this](float v){ track.grainUniDetune = v; },
                     [](float v){ return juce::String (static_cast<int>(v)) + "ct"; }, "grainUniDetune");
            addKnob ("STR", 0, 1, track.grainUniStereo, [this](float v){ track.grainUniStereo = v; },
                     [](float v){ return v < 0.01f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "grainUniStereo");
            // ── FM MODULATOR ──
            currentAccent = juce::Colour (0xff60a0e0); // blue
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FM");
            addKnob ("FM", 0, 100, track.grainFmAmt, [this](float v){ track.grainFmAmt = v; },
                     [](float v){ return v < 0.5f ? juce::String ("OFF") : juce::String (static_cast<int>(v)); }, "grainFmAmt");
            addKnob ("RAT", 0.5f, 16, track.grainFmRatio, [this](float v){ track.grainFmRatio = v; },
                     [](float v){ return juce::String (v, 2); }, "grainFmRatio");
            addKnob ("DCY", 0.01f, 3.0f, track.grainFmDecay, [this](float v){ track.grainFmDecay = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "grainFmDecay");
            addKnob ("SUS", 0, 1, track.grainFmSus, [this](float v){ track.grainFmSus = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "grainFmSus");
            addKnob ("SNP", 0, 1, static_cast<float>(track.grainFmSnap), [this](float v){ track.grainFmSnap = static_cast<int>(v); },
                     [](float v){ return v > 0.5f ? juce::String ("INT") : juce::String ("FREE"); }, "grainFmSnap");
            addKnob ("FMSP", 0, 1, track.grainFmSpread, [this](float v){ track.grainFmSpread = v; },
                     [](float v){ return v < 0.01f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "grainFmSpread");
            // ── FILTER ──
            currentAccent = juce::Colour (0xffe09040);
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("FILTER");
            addKnob ("FLT", 0, 5, static_cast<float>(track.fModel), [this](float v){ track.fModel = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"CLN","ACD","DRT","SEM","ARP","LQD"};
                         int m = static_cast<int>(v);
                         return juce::String (m < 6 ? n[m] : "?");
                     }, "fModel");
            addKnob ("CUT", 0, 100, track.cut, [this](float v){ track.cut = v; },
                     [](float v){ return juce::String (static_cast<int>(v)) + "%"; }, "cut");
            addKnob ("RES", 0, 1, track.res, [this](float v){ track.res = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "res");
            addKnob ("TYP", 0, 3, static_cast<float>(track.fType), [this](float v){ track.fType = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"LP","HP","BP","NTC"}; return juce::String (n[static_cast<int>(v) % 4]); }, "fType");
            addKnob ("dB", 0, 2, static_cast<float>((track.fPoles == 6) ? 0 : (track.fPoles == 12) ? 1 : 2),
                     [this](float v){
                         int p = static_cast<int>(v);
                         track.fPoles = (p == 0) ? 6 : (p == 1) ? 12 : 24;
                     },
                     [](float v){
                         int p = static_cast<int>(v);
                         return juce::String ((p == 0) ? "6dB" : (p == 1) ? "12dB" : "24dB");
                     }, "fPoles");
            addKnob ("ENV", 0, 100, track.fenv, [this](float v){ track.fenv = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "fenv");
            // ── F.ENV ──
            currentAccent = juce::Colour (0xffd08030);
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("F.ENV");
            addKnob ("F.A", 0.001f, 2.0f, track.fA, [this](float v){ track.fA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fA");
            addKnob ("F.D", 0.01f, 2.0f, track.fD, [this](float v){ track.fD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fD");
            addKnob ("F.S", 0, 1, track.fS, [this](float v){ track.fS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fS");
            addKnob ("F.R", 0.01f, 3.0f, track.fR, [this](float v){ track.fR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fR");
            // ── AMP ──
            currentAccent = juce::Colour (0xff70c050);
            engGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            engGroupNames.push_back ("AMP");
            addKnob ("A", 0.001f, 2.0f, track.aA, [this](float v){ track.aA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aA");
            addKnob ("D", 0.01f, 2.0f, track.aD, [this](float v){ track.aD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aD");
            addKnob ("S", 0, 1, track.aS, [this](float v){ track.aS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "aS");
            addKnob ("R", 0.01f, 3.0f, track.aR, [this](float v){ track.aR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "aR");
            addKnob ("VOL", 0, 1, track.volume, [this](float v){ track.volume = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "volume");
        }

        // ══ FX — grouped with colors ══
        fxStartIdx = static_cast<int>(engineKnobs.size());
        // ── DRIVE ──
        currentAccent = juce::Colour (0xffc06040); // warm red
        addKnob ("DST", 0, 1, track.distAmt, [this](float v){ track.distAmt = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "distAmt");
        addKnob ("BIT", 1, 16, track.reduxBits, [this](float v){ track.reduxBits = v; },
                 [](float v){ return v >= 15.5f ? juce::String ("OFF") : juce::String (static_cast<int>(v)); }, "reduxBits");
        addKnob ("S.R", 0, 1, track.reduxRate, [this](float v){ track.reduxRate = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "reduxRate");
        // ── CHORUS ──
        currentAccent = juce::Colour (0xff50a0d0); // light blue
        addKnob ("CHO", 0, 1, track.chorusMix, [this](float v){ track.chorusMix = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "chorusMix");
        addKnob ("C.R", 0.1f, 5, track.chorusRate, [this](float v){ track.chorusRate = v; },
                 [](float v){ return juce::String (v, 1) + "Hz"; }, "chorusRate");
        addKnob ("C.D", 0, 1, track.chorusDepth, [this](float v){ track.chorusDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "chorusDepth");
        // ── DELAY ──
        currentAccent = juce::Colour (0xff60b080); // teal
        addKnob ("D.AL", 0, 3, static_cast<float>(track.delayAlgo), [this](float v){ track.delayAlgo = static_cast<int>(v); },
                 [](float v){ const char* n[] = {"DIG","TAPE","BBD","DIFF"}; return juce::String (n[static_cast<int>(v) % 4]); }, "delayAlgo");
        addKnob ("DLY", 0, 1, track.delayMix, [this](float v){ track.delayMix = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "delayMix");
        addKnob ("D.S", 0, 1, track.delaySync ? 1.0f : 0.0f,
                 [this](float v){ track.delaySync = (v > 0.5f); },
                 [](float v){ return v > 0.5f ? juce::String ("SYN") : juce::String ("FRE"); }, "delaySync");
        addKnob ("D.B", 0.125f, 4.0f, track.delayBeats, [this](float v){
                     // Snap to nearest musical division
                     const float divs[] = {0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
                     float best = divs[0]; float bestDist = 999.0f;
                     for (float d : divs) { float dist = std::abs(v - d); if (dist < bestDist) { bestDist = dist; best = d; } }
                     track.delayBeats = best;
                 },
                 [](float v){
                     if (v <= 0.17f)  return juce::String ("1/32");
                     if (v <= 0.31f)  return juce::String ("1/16");
                     if (v <= 0.43f)  return juce::String ("D.16");
                     if (v <= 0.62f)  return juce::String ("1/8");
                     if (v <= 0.87f)  return juce::String ("D.8");
                     if (v <= 1.25f)  return juce::String ("1/4");
                     if (v <= 1.75f)  return juce::String ("D.4");
                     if (v <= 2.5f)   return juce::String ("1/2");
                     if (v <= 3.5f)   return juce::String ("D.2");
                     return juce::String ("1bar");
                 }, "delayBeats");
        addKnob ("D.TM", 0.001f, 2.0f, track.delayTime, [this](float v){ track.delayTime = v; },
                 [](float v){ return v < 1.0f ? juce::String (static_cast<int>(v * 1000)) + "ms"
                                               : juce::String (v, 1) + "s"; }, "delayTime");
        addKnob ("D.FB", 0, 0.9f, track.delayFB, [this](float v){ track.delayFB = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "delayFB");
        addKnob ("D.P", 0, 1, static_cast<float>(track.delayPP), [this](float v){ track.delayPP = static_cast<int>(v); },
                 [](float v){ return static_cast<int>(v) > 0 ? juce::String ("P.P") : juce::String ("MNO"); }, "delayPP");
        addKnob ("D.LP", 0, 1, track.delayDamp, [this](float v){ track.delayDamp = v; },
                 [](float v){
                     if (v < 0.1f) return juce::String ("BRT");
                     if (v < 0.4f) return juce::String ("AIR");
                     if (v < 0.7f) return juce::String ("WRM");
                     return juce::String ("DRK");
                 }, "delayDamp");
        // ── OTT ──
        currentAccent = juce::Colour (0xfff0c040); // gold/yellow
        addKnob ("OTT", 0, 1, track.ottDepth, [this](float v){ track.ottDepth = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "ottDepth");
        addKnob ("UP", 0, 1, track.ottUpward, [this](float v){ track.ottUpward = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "ottUpward");
        addKnob ("DN", 0, 1, track.ottDownward, [this](float v){ track.ottDownward = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "ottDownward");
        // ── PHASER ──
        currentAccent = juce::Colour (0xffd050e0); // magenta
        addKnob ("PHS", 0, 1, track.phaserMix, [this](float v){ track.phaserMix = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "phaserMix");
        addKnob ("PH.R", 0.05f, 5.0f, track.phaserRate, [this](float v){ track.phaserRate = v; },
                 [](float v){ return juce::String (v, 1) + "Hz"; }, "phaserRate");
        addKnob ("PH.D", 0, 1, track.phaserDepth, [this](float v){ track.phaserDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "phaserDepth");
        addKnob ("PH.F", 0, 0.95f, track.phaserFB, [this](float v){ track.phaserFB = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "phaserFB");
        // ── REVERB ──
        currentAccent = juce::Colour (0xff6070d8); // indigo
        addKnob ("R.AL", 0, 6, static_cast<float>(track.reverbAlgo), [this](float v){ track.reverbAlgo = static_cast<int>(v); },
                 [](float v){ const char* n[] = {"FDN","PLTE","SHIM","GLXY","ROOM","SPRG","GATE"}; return juce::String (n[static_cast<int>(v) % 7]); }, "reverbAlgo");
        addKnob ("REV", 0, 1, track.reverbMix, [this](float v){ track.reverbMix = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "reverbMix");
        addKnob ("R.S", 0.05f, 4.0f, track.reverbSize, [this](float v){ track.reverbSize = v; },
                 [](float v){
                     if (v < 0.3f) return juce::String ("TINY");
                     if (v < 0.8f) return juce::String ("ROOM");
                     if (v < 1.5f) return juce::String ("HALL");
                     if (v < 2.5f) return juce::String ("CATH");
                     return juce::String ("INF");
                 }, "reverbSize");
        addKnob ("R.DM", 0, 1, track.reverbDamp, [this](float v){ track.reverbDamp = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "reverbDamp");
        // ── EQ ──
        currentAccent = juce::Colour (0xff50d0b0); // mint
        addKnob ("LO", -12, 12, track.eqLow, [this](float v){ track.eqLow = v; },
                 [](float v){ int d = static_cast<int>(v); return (d >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (d); }, "eqLow");
        addKnob ("MD", -12, 12, track.eqMid, [this](float v){ track.eqMid = v; },
                 [](float v){ int d = static_cast<int>(v); return (d >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (d); }, "eqMid");
        addKnob ("HI", -12, 12, track.eqHigh, [this](float v){ track.eqHigh = v; },
                 [](float v){ int d = static_cast<int>(v); return (d >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (d); }, "eqHigh");
        // ── OUTPUT ──
        currentAccent = juce::Colour (0xff909098); // grey
        addKnob ("LP", 200, 20000, track.fxLP, [this](float v){ track.fxLP = v; },
                 [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "k" : juce::String (static_cast<int>(v)); }, "fxLP");
        addKnob ("HP", 20, 5000, track.fxHP, [this](float v){ track.fxHP = v; },
                 [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "k" : juce::String (static_cast<int>(v)); }, "fxHP");
        addKnob ("PAN", -1, 1, track.pan, [this](float v){ track.pan = v; },
                 [](float v){
                     if (v < -0.05f) return juce::String ("L") + juce::String (static_cast<int>(-v * 50));
                     if (v > 0.05f) return juce::String ("R") + juce::String (static_cast<int>(v * 50));
                     return juce::String ("C");
                 }, "pan");
        // ── DUCK ──
        currentAccent = juce::Colour (0xffe08020); // orange
        addKnob ("D.S", -1, 14, static_cast<float>(track.duckSrc), [this](float v){ track.duckSrc = static_cast<int>(v); },
                 [](float v){
                     int s = static_cast<int>(v);
                     if (s < 0) return juce::String ("OFF");
                     if (s < 10) { const char* n[] = {"KK","SN","HC","HO","CR","CB","RM","TM","TH","CP"}; return juce::String (n[s]); }
                     return juce::String ("S") + juce::String (s - 9);
                 }, "duckSrc");
        addKnob ("D.D", 0, 1, track.duckDepth, [this](float v){ track.duckDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "duckDepth");
        addKnob ("D.A", 0.001f, 0.1f, track.duckAtk, [this](float v){ track.duckAtk = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "duckAtk");
        addKnob ("D.R", 0.01f, 1.0f, track.duckRel, [this](float v){ track.duckRel = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "duckRel");
        // ── SAT ──
        currentAccent = juce::Colour (0xffe04070); // rose
        addKnob ("TYP", 0, 4, static_cast<float>(track.proDistModel), [this](float v){ track.proDistModel = static_cast<int>(v + 0.5f); },
                 [](float v){ const char* n[]={"TUBE","TAPE","XFMR","AMP","WSHP"}; return juce::String(n[std::clamp(static_cast<int>(v+0.5f),0,4)]); }, "proDistModel");
        addKnob ("DRV", 0, 1, track.proDistDrive, [this](float v){ track.proDistDrive = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "proDistDrive");
        addKnob ("TON", 0, 1, track.proDistTone, [this](float v){ track.proDistTone = v; },
                 [](float v){ return v < 0.2f ? juce::String ("DRK") : (v > 0.8f ? juce::String ("BRT") : juce::String (static_cast<int>(v*100)) + "%"); }, "proDistTone");
        addKnob ("MIX", 0, 1, track.proDistMix, [this](float v){ track.proDistMix = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "proDistMix");
        addKnob ("BIA", 0, 1, track.proDistBias, [this](float v){ track.proDistBias = v; },
                 [](float v){ return v < 0.02f ? juce::String ("SYM") : juce::String (static_cast<int>(v * 100)) + "%"; }, "proDistBias");
        // ── FLANGER ──
        currentAccent = juce::Colour (0xffa0d040); // yellow-green
        addKnob ("FLG", 0, 1, track.flangerMix, [this](float v){ track.flangerMix = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "flangerMix");
        addKnob ("FL.R", 0.05f, 5.0f, track.flangerRate, [this](float v){ track.flangerRate = v; },
                 [](float v){ return juce::String (v, 1) + "Hz"; }, "flangerRate");
        addKnob ("FL.D", 0, 1, track.flangerDepth, [this](float v){ track.flangerDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "flangerDepth");
        addKnob ("FL.F", -0.95f, 0.95f, track.flangerFB, [this](float v){ track.flangerFB = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "flangerFB");

        // ══ LFO (3 per track) ══
        lfoStartIdx = static_cast<int>(engineKnobs.size());
        static const juce::Colour lfoColors[] = {
            juce::Colour (0xff50c0b0),  // LFO1 cyan
            juce::Colour (0xffc07090),  // LFO2 pink
            juce::Colour (0xffc0a030)   // LFO3 gold
        };
        for (int li = 0; li < 3; ++li)
        {
            currentAccent = lfoColors[li];
            auto& lfo = track.lfos[static_cast<size_t>(li)];

            addKnob ("SYN", 0, 1, lfo.sync ? 1.0f : 0.0f,
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].sync = (v > 0.5f); },
                     [](float v){ return v > 0.5f ? juce::String ("SYN") : juce::String ("FRE"); },
                     "lfo" + juce::String(li) + "Sync");
            addKnob ("RNG", 0, 1, lfo.hiRate ? 1.0f : 0.0f,
                     [this, li](float v){
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         bool newHi = (v > 0.5f);
                         if (newHi != l.hiRate) {
                             // Convert knob position from old range to new range
                             float knobPos = l.hiRate ? LFOEngine::hzToKnobHi (l.rate)
                                                      : LFOEngine::hzToKnob (l.rate);
                             l.hiRate = newHi;
                             l.rate = newHi ? LFOEngine::knobToHzHi (knobPos)
                                            : LFOEngine::knobToHz (knobPos);
                         }
                     },
                     [this, li](float v){
                         if (track.lfos[static_cast<size_t>(li)].sync) return juce::String ("---");
                         return v > 0.5f ? juce::String ("HI") : juce::String ("LO");
                     },
                     "lfo" + juce::String(li) + "HiRate");
            addKnob ("RATE", 0, 17, lfo.sync ? lfo.syncDiv
                         : (lfo.hiRate ? LFOEngine::hzToKnobHi (lfo.rate) : LFOEngine::hzToKnob (lfo.rate)),
                     [this, li](float v){
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         // ALWAYS write both so value is never lost
                         l.syncDiv = v;
                         l.rate = l.hiRate ? LFOEngine::knobToHzHi (v) : LFOEngine::knobToHz (v);
                     },
                     [this, li](float v){
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         if (l.sync)
                             return juce::String (LFOEngine::getDivName (static_cast<int>(v)));
                         float hz = l.hiRate ? LFOEngine::knobToHzHi (v) : LFOEngine::knobToHz (v);
                         return LFOEngine::formatHz (hz);
                     },
                     "lfo" + juce::String(li) + "Rate");
            addKnob ("AMT", -1, 1, lfo.depth,
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].depth = v; },
                     [](float v){
                         int pct = static_cast<int>(v * 100);
                         if (pct == 0) return juce::String ("OFF");
                         return (pct > 0 ? juce::String ("+") : juce::String ("")) + juce::String (pct) + "%";
                     },
                     "lfo" + juce::String(li) + "Depth");
            addKnob ("WAVE", 0, 5, static_cast<float>(lfo.shape),
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].shape = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"SIN","TRI","SAW","SQR","RMP","S&H"};
                         return juce::String (n[std::clamp (static_cast<int>(v), 0, 5)]);
                     }, "lfo" + juce::String(li) + "Shape");
            addKnob ("DEST", 0, static_cast<float>(LFOEngine::kNumSynthTargets - 1),
                     static_cast<float>(lfo.target),
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].target = static_cast<int>(v); },
                     [](float v){
                         return juce::String (LFOEngine::getSynthTargetName (static_cast<int>(v)));
                     }, "lfo" + juce::String(li) + "Target");
            // Categorized popup menu for target selection
            {
                using PC = KnobComponent::PopupCategory;
                std::vector<PC> cats;
                auto mkCat = [](const juce::String& name, std::initializer_list<int> indices) {
                    PC c; c.name = name;
                    for (int i : indices)
                        c.items.push_back ({i, LFOEngine::getSynthTargetName (i)});
                    return c;
                };
                cats.push_back (mkCat ("FILTER",    {1, 2, 14, 56, 57, 58, 59})); // CUT, RES, FEV, F.A, F.D, F.S, F.R
                cats.push_back (mkCat ("AMP",       {3, 15, 16, 17, 39})); // VOL, ATK, DCY, SUS, REL
                cats.push_back (mkCat ("PITCH/PAN", {0, 4}));              // PCH, PAN
                cats.push_back (mkCat ("OSC",       {9, 10, 11, 12, 13, 18, 54, 55})); // PWM,MIX,DET,SUB,SPR,CHR,SYR,UST
                cats.push_back (mkCat ("SEND",      {5, 6, 7, 8}));        // DLY, DST, CHO, REV
                cats.push_back (mkCat ("FM",        {19, 60, 61, 20, 21, 22, 23, 24, 25, 26, 27})); // LFM,FLR,FLD,C.R..FB
                cats.push_back (mkCat ("ELEMENTS",  {28, 29, 30, 31, 32, 33}));
                cats.push_back (mkCat ("PLAITS",    {34, 35, 36, 37, 38}));
                cats.push_back (mkCat ("FX",        {40,41,42,43,44,45,46,91,92,99}));
                cats.push_back (mkCat ("EQ/OUT",    {96,97,98,94,95}));
                cats.push_back (mkCat ("DUCK",      {124,125,126}));
                            cats.push_back (mkCat ("OTT",       {135,136,137}));
                            cats.push_back (mkCat ("SAT",       {138,139,140,141}));
                            cats.push_back (mkCat ("PHASER",    {142,143,144,145}));
                            cats.push_back (mkCat ("FLANGER",   {146,147,148,149}));
                cats.push_back (mkCat ("SAMPLER",   {47,48,49,50,51,52,53,89,90,93}));
                cats.push_back (mkCat ("WAVETABLE", {70, 71, 72, 73, 74, 75}));
                            cats.push_back (mkCat ("GRANULAR", {76,77,78,79,80,81,82,83,84,85,86,87,88}));
                cats.push_back (mkCat ("X-MOD",     {62, 63, 64, 65, 66, 67}));
                cats.push_back (mkCat ("MSEG",      {68, 69}));
                engineKnobs.getLast()->setCategorizedPopup (cats);
            }
            addKnob ("RTG", 0, 1, lfo.retrig ? 1.0f : 0.0f,
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].retrig = (v > 0.5f); },
                     [](float v){ return v > 0.5f ? juce::String ("ON") : juce::String ("OFF"); },
                     "lfo" + juce::String(li) + "Retrig");
            // ── Fade-in (green = timing) — sync follows LFO sync automatically ──
            juce::Colour savedAccent = currentAccent;
            currentAccent = juce::Colour (0xff60c070); // green
            addKnob ("FDI", 0, 10, lfo.fadeIn,
                     [this, li](float v){
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         l.fadeIn = v;
                         l.fadeInSync = l.sync; // auto-follow LFO sync
                     },
                     [this, li](float v){
                         if (v < 0.01f) return juce::String ("OFF");
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         if (l.sync) {
                             static const char* sn[]={"1/32","1/16","1/8","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"};
                             return juce::String (sn[std::clamp (static_cast<int>(v), 0, 10)]);
                         }
                         if (v < 1.0f) return juce::String (static_cast<int>(v * 1000)) + "ms";
                         return juce::String (v, 1) + "s";
                     }, "lfo" + juce::String(li) + "FadeIn");

            // ── Extra modulation routes (blue = routing) ──
            currentAccent = juce::Colour (0xff6090dd);
            for (int ri = 0; ri < 3; ++ri)
            {
                auto& route = lfo.extraRoutes[static_cast<size_t>(ri)];
                juce::String rLabel = "D" + juce::String (ri + 2);
                juce::String aLabel = "A" + juce::String (ri + 2);
                // Extra DEST knob: -1 = OFF, 0..N = target
                addKnob (rLabel, -1, static_cast<float>(LFOEngine::kNumSynthTargets - 1),
                         static_cast<float>(route.target),
                         [this, li, ri](float v){ track.lfos[static_cast<size_t>(li)].extraRoutes[static_cast<size_t>(ri)].target = static_cast<int>(v); },
                         [](float v){
                             int t = static_cast<int>(v);
                             if (t < 0) return juce::String ("OFF");
                             return juce::String (LFOEngine::getSynthTargetName (t));
                         }, "lfo" + juce::String(li) + "ExDst" + juce::String(ri));
                // Attach categorized popup (same categories + OFF at top)
                {
                    using PC = KnobComponent::PopupCategory;
                    std::vector<PC> cats;
                    auto mkCat = [](const juce::String& name, std::initializer_list<int> indices) {
                        PC c; c.name = name;
                        for (int i : indices)
                            c.items.push_back ({i, LFOEngine::getSynthTargetName (i)});
                        return c;
                    };
                    // OFF option as first category
                    { PC c; c.name = "---"; c.items.push_back ({-1, "OFF"}); cats.push_back (c); }
                    cats.push_back (mkCat ("FILTER",    {1, 2, 14, 56, 57, 58, 59}));
                    cats.push_back (mkCat ("AMP",       {3, 15, 16, 17, 39}));
                    cats.push_back (mkCat ("PITCH/PAN", {0, 4}));
                    cats.push_back (mkCat ("OSC",       {9, 10, 11, 12, 13, 18, 54, 55}));
                    cats.push_back (mkCat ("SEND",      {5, 6, 7, 8}));
                    cats.push_back (mkCat ("FM",        {19, 60, 61, 20, 21, 22, 23, 24, 25, 26, 27}));
                    cats.push_back (mkCat ("ELEMENTS",  {28, 29, 30, 31, 32, 33}));
                    cats.push_back (mkCat ("PLAITS",    {34, 35, 36, 37, 38}));
                    cats.push_back (mkCat ("FX",        {40,41,42,43,44,45,46,91,92,99}));
                    cats.push_back (mkCat ("EQ/OUT",    {96,97,98,94,95}));
                    cats.push_back (mkCat ("DUCK",      {124,125,126}));
                            cats.push_back (mkCat ("OTT",       {135,136,137}));
                            cats.push_back (mkCat ("SAT",       {138,139,140,141}));
                            cats.push_back (mkCat ("PHASER",    {142,143,144,145}));
                            cats.push_back (mkCat ("FLANGER",   {146,147,148,149}));
                    cats.push_back (mkCat ("SAMPLER",   {47,48,49,50,51,52,53,89,90,93}));
                    cats.push_back (mkCat ("WAVETABLE", {70, 71, 72, 73, 74, 75}));
                            cats.push_back (mkCat ("GRANULAR", {76,77,78,79,80,81,82,83,84,85,86,87,88}));
                    cats.push_back (mkCat ("X-MOD",     {62, 63, 64, 65, 66, 67}));
                    cats.push_back (mkCat ("MSEG",      {68, 69}));
                    engineKnobs.getLast()->setCategorizedPopup (cats);
                }
                // Extra AMT knob
                addKnob (aLabel, -1, 1, route.depth,
                         [this, li, ri](float v){ track.lfos[static_cast<size_t>(li)].extraRoutes[static_cast<size_t>(ri)].depth = v; },
                         [](float v){
                             int pct = static_cast<int>(v * 100);
                             if (pct == 0) return juce::String ("OFF");
                             return (pct > 0 ? juce::String ("+") : juce::String ("")) + juce::String (pct) + "%";
                         }, "lfo" + juce::String(li) + "ExAmt" + juce::String(ri));
            }
        }

        // ══ VOICE — Mono, Glide, Arp, Chord (global across engines) ══
        voiceStartIdx = static_cast<int>(engineKnobs.size());
        // ── MONO / GLIDE ──
        currentAccent = juce::Colour (0xffa0c060); // lime green
        addKnob ("MONO", 0, 1, track.mono ? 1.0f : 0.0f,
                 [this](float v){ track.mono = (v > 0.5f); },
                 [](float v){ return v > 0.5f ? juce::String ("ON") : juce::String ("OFF"); }, "mono");
        addKnob ("GLDE", 0, 1, track.glide,
                 [this](float v){ track.glide = v; },
                 [](float v){ return v < 0.01f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 1000)) + "ms"; }, "glide");
        // ── ARPEGGIATOR (basic controls — full editor via ARP popup button) ──
        currentAccent = juce::Colour (0xffc0a030); // gold
        addKnob ("ARP", 0, 1, track.arp.enabled ? 1.0f : 0.0f,
                 [this](float v){ track.arp.enabled = (v > 0.5f); },
                 [](float v){ return v > 0.5f ? juce::String ("ON") : juce::String ("OFF"); }, "arpOn");
        addKnob ("A.MD", 0, 7, static_cast<float>(track.arp.direction),
                 [this](float v){ track.arp.direction = static_cast<int>(v); },
                 [](float v){
                     const char* n[] = {"UP","DN","U/D","D/U","RND","CHD","CNV","DIV"};
                     int m = static_cast<int>(v);
                     return juce::String (m < 8 ? n[m] : "?");
                 }, "arpDir");
        addKnob ("A.RT", 0, 7, static_cast<float>(track.arp.division),
                 [this](float v){ track.arp.division = static_cast<int>(v); },
                 [](float v){
                     return juce::String (ArpData::getDivisionName (static_cast<int>(v)));
                 }, "arpDiv");
        addKnob ("A.OC", 1, 4, static_cast<float>(track.arp.octaves),
                 [this](float v){ track.arp.octaves = static_cast<int>(v); },
                 [](float v){ return juce::String (static_cast<int>(v)) + "oct"; }, "arpOctaves");
        // ── CHORD (type + inversion + voicing) — TRACK-LEVEL, not p-lockable ──
        // Per-step chord override is done via step right-click menu
        currentAccent = juce::Colour (0xff60a0c0); // sky blue
        addKnob ("CHRD", 0, static_cast<float>(SequencerEngine::kNumChordTypes), static_cast<float>(track.chordMode),
                 [this](float v){ track.chordMode = static_cast<int>(v); },
                 [](float v){ return juce::String (SequencerEngine::chordName (static_cast<int>(v))); }, "chordMode");
        engineKnobs.getLast()->setupPlock (nullptr, nullptr, nullptr, ""); // disable p-lock
        addKnob ("INV", 0, 3, static_cast<float>(track.chordInversion),
                 [this](float v){ track.chordInversion = static_cast<int>(v); },
                 [](float v){
                     const char* n[] = {"ROOT","1st","2nd","3rd"};
                     return juce::String (n[std::clamp (static_cast<int>(v), 0, 3)]);
                 }, "chordInversion");
        engineKnobs.getLast()->setupPlock (nullptr, nullptr, nullptr, ""); // disable p-lock
        addKnob ("VCE", 0, 3, static_cast<float>(track.chordVoicing),
                 [this](float v){ track.chordVoicing = static_cast<int>(v); },
                 [](float v){
                     const char* n[] = {"CLS","DRP2","SPRD","OPEN"};
                     return juce::String (n[std::clamp (static_cast<int>(v), 0, 3)]);
                 }, "chordVoicing");
        engineKnobs.getLast()->setupPlock (nullptr, nullptr, nullptr, ""); // disable p-lock
    }

    void addKnob (const juce::String& name, float minVal, float maxVal, float initVal,
                  std::function<void(float)> onChange,
                  std::function<juce::String(float)> formatter = nullptr,
                  const juce::String& plockKeyOverride = "")
    {
        auto* knob = new KnobComponent (name, minVal, maxVal, initVal, formatter);
        knob->onChange = onChange;
        knob->setAccentColour (currentAccent);
        std::string pk = plockKeyOverride.isNotEmpty()
            ? plockKeyOverride.toStdString()
            : name.toLowerCase().toStdString();
        knob->setupPlock (&plockMode, &plockTargetStep, &track.seq, pk);
        if (motionRecPtr != nullptr && motionStepPtr != nullptr)
            knob->setupMotionRec (motionRecPtr, motionStepPtr, motionRecModePtr);
        if (macroEnginePtr != nullptr)
            knob->setupMacro (macroEnginePtr, 0, index);
        knob->onPlockWritten = [this]() { refreshSteps(); };

        // ── Modulation assignment: map paramKey → LFO/MSEG target ID ──
        knob->modTargetId = paramKeyToModTarget (pk);
        if (knob->modTargetId >= 0)
        {
            int tgtId = knob->modTargetId;
            float kMin = minVal, kMax = maxVal;
            knob->onModRightClick = [this, tgtId, pk, kMin, kMax]()
            {
                showModAssignPopup (tgtId, pk, kMin, kMax);
            };
        }

        knob->setVisible (false);
        engineKnobs.add (knob);
        addChildComponent (knob);
    }

    // ── paramKey → LFO/MSEG target ID mapping ──
    static int paramKeyToModTarget (const std::string& key)
    {
        // Synth modulation targets 0-99 (from LFOEngine.h kNumSynthTargets)
        static const std::unordered_map<std::string, int> map = {
            {"tune",0}, {"cut",1}, {"res",2}, {"volume",3}, {"pan",4},
            {"delayMix",5}, {"distAmt",6}, {"chorusMix",7}, {"reverbMix",8},
            {"pwm",9}, {"mix2",10}, {"detune",11}, {"subLevel",12}, {"uniSpread",13},
            {"fenv",14}, {"aA",15}, {"aD",16}, {"aS",17}, {"charAmt",18}, {"fmLinAmt",19},
            {"cRatio",20}, {"r2",21}, {"l2",22}, {"r3",23}, {"l3",24}, {"r4",25}, {"l4",26}, {"fmFeedback",27},
            {"elemBow",28}, {"elemBlow",29}, {"elemStrike",30}, {"elemGeometry",31}, {"elemBright",32}, {"elemSpace",33},
            {"plaitsHarmonics",34}, {"plaitsTimbre",35}, {"plaitsMorph",36}, {"plaitsDecay",37}, {"plaitsLpgColor",38},
            {"aR",39},
            {"chorusRate",40}, {"chorusDepth",41}, {"delayTime",42}, {"delayFB",43},
            {"reverbSize",44}, {"reduxBits",45}, {"reduxRate",46},
            {"smpCut",47}, {"smpRes",48}, {"smpGain",49}, {"smpStart",50}, {"smpEnd",51}, {"smpTune",52}, {"smpFine",53},
            {"syncRatio",54}, {"uniStereo",55}, {"fA",56}, {"fD",57}, {"fS",58}, {"fR",59},
            {"fmLinRatio",60}, {"fmLinDecay",61},
            {"wtPos1",70}, {"wtPos2",71}, {"wtMix",72}, {"wtWarpAmt1",73}, {"wtWarpAmt2",74}, {"wtSubLevel",75},
            {"grainPos",76}, {"grainSize",77}, {"grainDensity",78}, {"grainSpray",79},
            {"grainPitch",80}, {"grainPan",81}, {"grainScan",82},
            {"grainTexture",83}, {"grainFmAmt",84}, {"grainFmRatio",85}, {"grainFmDecay",86}, {"grainFmSus",87}, {"grainFmSpread",88},
            {"smpFmAmt",89}, {"smpFmRatio",90},
            {"delayDamp",91}, {"reverbDamp",92}, {"smpFiltEnv",93},
            {"fxLP",94}, {"fxHP",95}, {"eqLow",96}, {"eqMid",97}, {"eqHigh",98}, {"delayBeats",99},
            // Extended targets 100-134
            {"grainFeedback",100}, {"grainTilt",101}, {"grainUniDetune",102}, {"grainUniStereo",103}, {"grainUniVoices",104},
            {"elemContour",105}, {"elemDamping",106}, {"elemFlow",107}, {"elemMallet",108}, {"elemPitch",109}, {"elemPosition",110},
            {"fmLinSustain",111}, {"glide",112},
            {"smpA",113}, {"smpD",114}, {"smpS",115}, {"smpR",116},
            {"smpFiltA",117}, {"smpFiltD",118}, {"smpFiltS",119}, {"smpFiltR",120},
            {"smpFmEnvA",121}, {"smpFmEnvD",122}, {"smpFmEnvS",123},
            {"duckDepth",124}, {"duckAtk",125}, {"duckRel",126},
            {"dc2",127}, {"dc3",128}, {"dc4",129},
            {"cA",130}, {"cD",131}, {"cS",132}, {"cR",133}, {"cLevel",134},
            // OTT + ProDist (135-141)
            {"ottDepth",135}, {"ottUpward",136}, {"ottDownward",137},
            {"proDistDrive",138}, {"proDistTone",139}, {"proDistMix",140}, {"proDistBias",141},
            // Phaser + Flanger (142-149)
            {"phaserMix",142}, {"phaserRate",143}, {"phaserDepth",144}, {"phaserFB",145},
            {"flangerMix",146}, {"flangerRate",147}, {"flangerDepth",148}, {"flangerFB",149},
        };
        auto it = map.find (key);
        return (it != map.end()) ? it->second : -1;
    }

    // ── Right-click modulation assignment popup ──
    void showModAssignPopup (int targetId, const std::string& paramKey, float minVal, float maxVal)
    {
        juce::PopupMenu menu;
        juce::String targetName = LFOEngine::getSynthTargetName (targetId);
        menu.addSectionHeader ("Modulate: " + targetName);

        // ── LFO 1/2/3 ──
        for (int li = 0; li < 3; ++li)
        {
            float curDepth = 0.0f;
            bool isMainTarget = (track.lfos[static_cast<size_t>(li)].target == targetId);
            if (isMainTarget) curDepth = track.lfos[static_cast<size_t>(li)].depth;
            for (auto& r : track.lfos[static_cast<size_t>(li)].extraRoutes)
                if (r.target == targetId) curDepth = r.depth;

            juce::String lfoLabel = "LFO " + juce::String (li + 1);
            if (std::abs (curDepth) > 0.001f)
                lfoLabel += "  [" + juce::String (static_cast<int>(curDepth * 100)) + "%]";
            menu.addItem (100 + li, lfoLabel);
        }

        menu.addSeparator();

        // ── MSEG 1/2/3 ──
        for (int mi = 0; mi < 3; ++mi)
        {
            float curDepth = 0.0f;
            bool isMainTarget = (track.msegs[static_cast<size_t>(mi)].target == targetId);
            if (isMainTarget) curDepth = track.msegs[static_cast<size_t>(mi)].depth;
            for (auto& r : track.msegs[static_cast<size_t>(mi)].extraRoutes)
                if (r.target == targetId) curDepth = r.depth;

            juce::String msegLabel = "MSEG " + juce::String (mi + 1);
            if (std::abs (curDepth) > 0.001f)
                msegLabel += "  [" + juce::String (static_cast<int>(curDepth * 100)) + "%]";
            menu.addItem (200 + mi, msegLabel);
        }

        menu.addSeparator();

        // ── MACRO 1/2/3/4 ──
        if (macroEnginePtr != nullptr)
        {
            for (int mi = 0; mi < 4; ++mi)
            {
                float curDepth = 0.0f;
                for (auto& a : macroEnginePtr->macros[static_cast<size_t>(mi)].assignments)
                    if (a.trackType == 0 && a.trackIndex == index && a.paramKey == paramKey)
                        curDepth = a.depth;

                juce::String macroLabel = "MACRO " + juce::String (mi + 1);
                if (std::abs (curDepth) > 0.001f)
                    macroLabel += "  [" + juce::String (static_cast<int>(curDepth * 100)) + "%]";
                menu.addItem (300 + mi, macroLabel);
            }
        }

        menu.addSeparator();

        // ── VEL / KEY tracking ──
        {
            float velD = 0.0f;
            for (auto& vr : track.velRoutes) if (vr.target == targetId) velD = vr.depth;
            juce::String velLabel = "VELOCITY";
            if (std::abs (velD) > 0.001f) velLabel += "  [" + juce::String (static_cast<int>(velD * 100)) + "%]";
            menu.addItem (400, velLabel);

            float keyD = 0.0f;
            for (auto& kr : track.keyRoutes) if (kr.target == targetId) keyD = kr.depth;
            juce::String keyLabel = "KEY TRACK";
            if (std::abs (keyD) > 0.001f) keyLabel += "  [" + juce::String (static_cast<int>(keyD * 100)) + "%]";
            menu.addItem (401, keyLabel);
        }

        int result = menu.show();
        if (result == 0) return;

        // ── Determine source type and index ──
        int sourceType = -1; // 0=LFO, 1=MSEG, 2=MACRO
        int sourceIdx = 0;
        float currentDepth = 0.0f;

        if (result >= 100 && result < 200)
        {
            sourceType = 0; sourceIdx = result - 100;
            if (track.lfos[static_cast<size_t>(sourceIdx)].target == targetId)
                currentDepth = track.lfos[static_cast<size_t>(sourceIdx)].depth;
            for (auto& r : track.lfos[static_cast<size_t>(sourceIdx)].extraRoutes)
                if (r.target == targetId) currentDepth = r.depth;
        }
        else if (result >= 200 && result < 300)
        {
            sourceType = 1; sourceIdx = result - 200;
            if (track.msegs[static_cast<size_t>(sourceIdx)].target == targetId)
                currentDepth = track.msegs[static_cast<size_t>(sourceIdx)].depth;
            for (auto& r : track.msegs[static_cast<size_t>(sourceIdx)].extraRoutes)
                if (r.target == targetId) currentDepth = r.depth;
        }
        else if (result >= 300 && result < 400)
        {
            sourceType = 2; sourceIdx = result - 300;
            if (macroEnginePtr != nullptr)
                for (auto& a : macroEnginePtr->macros[static_cast<size_t>(sourceIdx)].assignments)
                    if (a.trackType == 0 && a.trackIndex == index && a.paramKey == paramKey)
                        currentDepth = a.depth;
        }
        else if (result == 400)
        {
            sourceType = 3; sourceIdx = 0; // VEL
            for (auto& vr : track.velRoutes) if (vr.target == targetId) currentDepth = vr.depth;
        }
        else if (result == 401)
        {
            sourceType = 4; sourceIdx = 0; // KEY
            for (auto& kr : track.keyRoutes) if (kr.target == targetId) currentDepth = kr.depth;
        }

        if (sourceType < 0) return;

        // ── Show depth slider dialog ──
        static const char* srcNames[] = { "LFO", "MSEG", "MACRO", "VEL", "KEY" };
        juce::String title = juce::String (srcNames[sourceType]) + (sourceType < 3 ? (" " + juce::String (sourceIdx + 1)) : juce::String())
                             + " → " + targetName;

        auto ae = std::make_shared<juce::AlertWindow> (title, "Depth (-100% to +100%):", juce::MessageBoxIconType::NoIcon);
        ae->addTextEditor ("depth", juce::String (static_cast<int>(currentDepth * 100)), "Depth %:");
        ae->addButton ("Apply", 1);
        ae->addButton ("Remove", 2);
        ae->addButton ("Cancel", 0);
        ae->setAlwaysOnTop (true);

        int sType = sourceType, sIdx = sourceIdx, tId = targetId;
        std::string pKey = paramKey;
        float kMin = minVal, kMax = maxVal;

        ae->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, ae, sType, sIdx, tId, pKey, kMin, kMax](int r)
            {
                if (r == 1) // Apply
                {
                    float depth = ae->getTextEditorContents ("depth").getFloatValue() / 100.0f;
                    depth = juce::jlimit (-1.0f, 1.0f, depth);
                    applyModDepth (sType, sIdx, tId, pKey, depth, kMin, kMax);
                }
                else if (r == 2) // Remove
                {
                    applyModDepth (sType, sIdx, tId, pKey, 0.0f, kMin, kMax);
                }
            }), false);
    }

    // Apply modulation depth from dialog
    void applyModDepth (int sourceType, int sourceIdx, int targetId,
                        const std::string& paramKey, float depth, float minVal, float maxVal)
    {
        if (sourceType == 0) // LFO
            assignLfoRoute (sourceIdx, targetId, depth);
        else if (sourceType == 1) // MSEG
            assignMsegRoute (sourceIdx, targetId, depth);
        else if (sourceType == 2 && macroEnginePtr != nullptr) // MACRO
        {
            if (std::abs (depth) < 0.001f)
            {
                auto& assignments = macroEnginePtr->macros[static_cast<size_t>(sourceIdx)].assignments;
                assignments.erase (
                    std::remove_if (assignments.begin(), assignments.end(),
                        [&](const MacroAssignment& a) { return a.trackType == 0 && a.trackIndex == index && a.paramKey == paramKey; }),
                    assignments.end());
            }
            else
            {
                float curVal = 0.0f;
                for (auto* k : engineKnobs)
                    if (k->modTargetId == paramKeyToModTarget (paramKey))
                    { curVal = k->getValue(); break; }
                macroEnginePtr->macros[static_cast<size_t>(sourceIdx)].addAssignment (0, index, paramKey, curVal, minVal, maxVal, depth);
            }
        }
        else if (sourceType == 3) // VEL
        {
            for (auto& vr : track.velRoutes)
                if (vr.target == targetId) { vr.depth = depth; if (std::abs(depth) < 0.001f) vr.target = -1; repaint(); return; }
            if (std::abs(depth) > 0.001f)
                for (auto& vr : track.velRoutes)
                    if (vr.target < 0) { vr.target = targetId; vr.depth = depth; break; }
        }
        else if (sourceType == 4) // KEY
        {
            for (auto& kr : track.keyRoutes)
                if (kr.target == targetId) { kr.depth = depth; if (std::abs(depth) < 0.001f) kr.target = -1; repaint(); return; }
            if (std::abs(depth) > 0.001f)
                for (auto& kr : track.keyRoutes)
                    if (kr.target < 0) { kr.target = targetId; kr.depth = depth; break; }
        }
        repaint();
    }

    // Assign/update/remove LFO extra route
    void assignLfoRoute (int lfoIdx, int targetId, float depth)
    {
        auto& lfo = track.lfos[static_cast<size_t>(lfoIdx)];
        // Check if it's the main target
        if (lfo.target == targetId)
        {
            lfo.depth = depth;
            return;
        }
        // Find existing route or empty slot
        for (auto& r : lfo.extraRoutes)
        {
            if (r.target == targetId)
            {
                if (std::abs (depth) < 0.01f)
                    r = { -1, 0.0f }; // remove
                else
                    r.depth = depth;
                return;
            }
        }
        // Not found — add to empty slot
        if (std::abs (depth) > 0.01f)
        {
            for (auto& r : lfo.extraRoutes)
            {
                if (r.target < 0)
                {
                    r.target = targetId;
                    r.depth = depth;
                    return;
                }
            }
        }
    }

    // Assign/update/remove MSEG extra route
    void assignMsegRoute (int msegIdx, int targetId, float depth)
    {
        auto& mseg = track.msegs[static_cast<size_t>(msegIdx)];
        if (mseg.target == targetId)
        {
            mseg.depth = depth;
            return;
        }
        for (auto& r : mseg.extraRoutes)
        {
            if (r.target == targetId)
            {
                if (std::abs (depth) < 0.01f)
                    r = { -1, 0.0f };
                else
                    r.depth = depth;
                return;
            }
        }
        if (std::abs (depth) > 0.01f)
        {
            for (auto& r : mseg.extraRoutes)
            {
                if (r.target < 0)
                {
                    r.target = targetId;
                    r.depth = depth;
                    return;
                }
            }
        }
    }

    // ── Waveform interaction ──
    juce::Rectangle<int> waveformBounds;
    int wfDragMode = 0; // 0=none, 1=start, 2=end, 4=warp marker drag
    float wfZoom = 1.0f;   // 1.0 = full view, up to 16x

    // Warp marker interaction state
    int warpHoveredMarker = -1;
    int warpDraggedMarker = -1;

    // Motion rec pointers (set from editor)
    std::atomic<bool>* motionRecPtr = nullptr;
    std::atomic<int>*  motionStepPtr = nullptr;
    std::atomic<int>*  motionRecModePtr = nullptr;
    MacroEngine*       macroEnginePtr = nullptr;
public:
    void setMotionRecPointers (std::atomic<bool>* recPtr, std::atomic<int>* stepPtr,
                               std::atomic<int>* modePtr = nullptr)
    {
        motionRecPtr = recPtr;
        motionStepPtr = stepPtr;
        motionRecModePtr = modePtr;
        // Wire to all existing knobs
        for (auto* k : engineKnobs)
            k->setupMotionRec (recPtr, stepPtr, modePtr);
        // Wire playing step to keyboard for real-time REC
        pianoKeyboard.playingStepPtr = stepPtr;
    }

    void setMacroEngine (MacroEngine* engine)
    {
        macroEnginePtr = engine;
        for (auto* k : engineKnobs)
            k->setupMacro (engine, 0, index);
    }

    void setResamplePointers (std::atomic<int>* srcPtr, std::atomic<bool>* activePtr,
                              std::atomic<int>* targetPtr, std::atomic<int>* lengthPtr,
                              std::atomic<bool>* transSyncPtr = nullptr,
                              std::atomic<bool>* armedPtr = nullptr,
                              double sr = 44100.0)
    {
        resampleSrcPtr      = srcPtr;
        resampleActivePtr   = activePtr;
        resampleTargetPtr   = targetPtr;
        resampleLengthPtr   = lengthPtr;
        resampleTransSyncPtr = transSyncPtr;
        resampleArmedPtr    = armedPtr;
        sampleRateForSave   = sr;
    }

    // Called from editor timer — sync REC button visual on auto-stop or transport-triggered recording
    void syncResampleBtn()
    {
        // ── Granular waveform animation: repaint when grains are active ──
        if (screenMode == 6 && currentTab == 0 && engineOpen && track.grainVisCount > 0)
            repaint();

        // ── Wavetable: real-time position + warp display ──
        if (screenMode == 5 && currentTab == 0 && engineOpen && wtEditorEmbed)
        {
            float pos = (wtLoadTarget == 0) ? track.wtPos1 : track.wtPos2;
            wtEditorEmbed->setDisplayPosition (pos);
            int wm = (wtLoadTarget == 0) ? track.wtWarp1 : track.wtWarp2;
            float wa = (wtLoadTarget == 0) ? track.wtWarpAmt1 : track.wtWarpAmt2;
            wtEditorEmbed->setDisplayWarp (wm, wa);
        }
        if (resampleActivePtr == nullptr) return;
        bool active = resampleActivePtr->load();
        bool armed = (resampleArmedPtr != nullptr) ? resampleArmedPtr->load() : false;
        bool syncOn = (resampleTransSyncPtr != nullptr) ? resampleTransSyncPtr->load() : false;

        juce::String curText = resampleRecBtn.getButtonText();

        if (syncOn)
        {
            // Transport sync mode
            if (active && curText != "STOP")
            {
                // Transport started recording → show STOP
                resampleRecBtn.setButtonText ("STOP");
                resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffcc3030));
                resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            }
            else if (!active && armed && curText != "ARM")
            {
                // Still armed, not recording
                resampleRecBtn.setButtonText ("ARM");
                resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffcc8800));
                resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            }
            else if (!active && !armed && curText != "REC")
            {
                // Disarmed (recording finished)
                resampleRecBtn.setButtonText ("REC");
                resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
                resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc3030));
            }
        }
        else
        {
            // Immediate mode — sync on auto-stop
            if (!active && curText == "STOP")
            {
                resampleRecBtn.setButtonText ("REC");
                resampleRecBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
                resampleRecBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc3030));
            }
        }
    }

    // LFO activity readback (set from editor timer)
public:
    float lfoDisplay[3] = {0, 0, 0};
    void setLfoValues (float v0, float v1, float v2) { lfoDisplay[0] = v0; lfoDisplay[1] = v1; lfoDisplay[2] = v2; if (currentTab == 2) repaint(); }
private:
    float wfOffset = 0.0f; // 0..1 normalized scroll offset

    // Debounced undo capture for step editing
    double lastStepUndoTime = 0.0;
    void fireBeforeStepEdit()
    {
        if (!onBeforeEdit) return;
        double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastStepUndoTime > 1500.0)
        {
            onBeforeEdit();
            lastStepUndoTime = now;
        }
    }

    // ── Sample folder browsing ──
    juce::Array<juce::File> sampleFolder;
    int sampleFolderIdx = -1;
    juce::TextButton smpPrevBtn, smpNextBtn;
    juce::TextButton zoomInBtn, zoomOutBtn;

    // ── Resample controls (Granular only) ──
    juce::ComboBox resampleSrcBox;
    juce::TextButton resampleRecBtn;
    juce::TextButton resampleSyncBtn;   // transport sync toggle
    std::atomic<int>*  resampleSrcPtr    = nullptr;
    std::atomic<bool>* resampleActivePtr = nullptr;
    std::atomic<int>*  resampleTargetPtr = nullptr;
    std::atomic<int>*  resampleLengthPtr = nullptr;
    std::atomic<bool>* resampleTransSyncPtr = nullptr;
    std::atomic<bool>* resampleArmedPtr  = nullptr;
    double sampleRateForSave = 44100.0;

    // Wavetable buttons
    juce::TextButton wtLoadBtn1, wtLoadBtn2, wtPrevBtn, wtNextBtn, wtEditBtn;
    juce::Array<juce::File> wtFolder;
    int wtFolderIdx = -1;
    int wtLoadTarget = 0; // 0=osc1, 1=osc2

    void loadSampleAudio (const juce::File& file)
    {
        juce::AudioFormatManager fmtMgr;
        fmtMgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fmtMgr.createReaderFor (file));
        if (reader != nullptr)
        {
            auto buf = std::make_shared<juce::AudioBuffer<float>> (
                static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
            reader->read (buf.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
            track.sampleData = buf;
            track.samplePath = file.getFullPathName();
            track.smpFileSR = static_cast<float>(reader->sampleRate);

            // ── CRITICAL: reset sample region on new load ──
            track.smpStart = 0.0f;
            track.smpEnd = 1.0f;
            track.smpRootNote = 60; // C3 (Ableton convention)
            track.smpStretch = 1.0f;
            track.smpSyncMul = 0;
            track.smpPlayMode = 1; // default GATE (not one-shot)

            // ── BPM + bar detection + auto warp ──
            auto analysis = SampleAnalysis::analyze (*buf, reader->sampleRate);
            if (analysis.isLoop && analysis.bpm > 0.0f)
            {
                track.smpBPM = analysis.bpm;
                track.smpBpmSync = 1;
                track.smpBars = analysis.barIndex;
                track.smpLoop = 1; // auto-enable loop for detected loops
                if (track.smpWarp == 0)
                    track.smpWarp = 1; // auto-enable BEATS mode
                // 2 markers only: start + end → uniform stretching (Ableton-style)
                // Sample sounds identical, just time-stretched to project BPM
                {
                    static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                    float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                    track.warpMarkers.clear();
                    WarpMarker startM; startM.samplePos = 0.0f; startM.beatPos = 0.0f;
                    startM.originalSamplePos = 0.0f; startM.isAuto = true;
                    WarpMarker endM; endM.samplePos = 1.0f; endM.beatPos = totalBeats;
                    endM.originalSamplePos = 1.0f; endM.isAuto = true;
                    track.warpMarkers.push_back (startM);
                    track.warpMarkers.push_back (endM);
                }
            }
            else
            {
                // One-shot: detect root note for pitched samples
                track.smpBpmSync = 0;
                track.smpLoop = 0;
                track.warpMarkers.clear();
                if (analysis.rootNote >= 0)
                    track.smpRootNote = analysis.rootNote;
            }

            // Refresh ALL knobs to reflect new values
            for (auto* k : engineKnobs)
            {
                if (k->getName() == "WARP")      k->setValue (static_cast<float>(track.smpWarp), false);
                else if (k->getName() == "BPM")   k->setValue (track.smpBPM, false);
                else if (k->getName() == "ROOT")  k->setValue (static_cast<float>(track.smpRootNote), false);
                else if (k->getName() == "SYNC")  k->setValue (static_cast<float>(track.smpBpmSync), false);
                else if (k->getName() == "STRC")  k->setValue (track.smpStretch, false);
                else if (k->getName() == "BARS")  k->setValue (static_cast<float>(track.smpBars), false);
                else if (k->getName() == "LOOP")  k->setValue (static_cast<float>(track.smpLoop), false);
                else if (k->getName() == "PLAY")  k->setValue (static_cast<float>(track.smpPlayMode), false);
                else if (k->getName() == "STRT")  k->setValue (track.smpStart, false);
                else if (k->getName() == "END")   k->setValue (track.smpEnd, false);
            }

            repaint();
        }
    }

    void loadSampleFromFile (const juce::File& file)
    {
        loadSampleAudio (file);
        if (track.sampleData)
        {
            // Build folder list from this file's directory
            auto dir = file.getParentDirectory();
            sampleFolder.clear();
            for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.wav;*.aiff;*.aif;*.flac;*.mp3"))
                sampleFolder.add (entry.getFile());
            struct FileSorter {
                static int compareElements (const juce::File& a, const juce::File& b)
                { return a.getFileName().compareIgnoreCase (b.getFileName()); }
            };
            FileSorter fs;
            sampleFolder.sort (fs);
            sampleFolderIdx = sampleFolder.indexOf (file);
        }
    }

    void browseSample (int delta)
    {
        if (sampleFolder.isEmpty()) return;
        sampleFolderIdx = (sampleFolderIdx + delta + sampleFolder.size()) % sampleFolder.size();
        loadSampleAudio (sampleFolder[sampleFolderIdx]);
    }

    void saveSampleToWav()
    {
        if (track.sampleData == nullptr || track.sampleData->getNumSamples() == 0) return;

        // Default filename based on source
        juce::String defaultName = "resampled";
        if (track.samplePath.isNotEmpty() && track.samplePath != "[resampled]")
            defaultName = juce::File (track.samplePath).getFileNameWithoutExtension();
        else
            defaultName = "GB_Resample_S" + juce::String (index + 1);

        fileChooser = std::make_unique<juce::FileChooser> (
            "Save Sample as WAV", juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile (defaultName + ".wav"), "*.wav");
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync (flags, [this](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result == juce::File()) return;

            // Ensure .wav extension
            juce::File outFile = result;
            if (outFile.getFileExtension().toLowerCase() != ".wav")
                outFile = outFile.withFileExtension (".wav");

            // Delete existing file
            if (outFile.existsAsFile())
                outFile.deleteFile();

            double sr = sampleRateForSave > 0 ? sampleRateForSave : 44100.0;
            int numChannels = track.sampleData->getNumChannels();
            int numSamples = track.sampleData->getNumSamples();

            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wavFormat.createWriterFor (
                    new juce::FileOutputStream (outFile),
                    sr,
                    static_cast<unsigned int>(numChannels),
                    24, // 24-bit WAV
                    {}, 0));

            if (writer != nullptr)
            {
                writer->writeFromAudioSampleBuffer (*track.sampleData, 0, numSamples);
                writer.reset(); // flush

                // Update samplePath to the saved file
                track.samplePath = outFile.getFullPathName();

                // Build folder list from saved location for PREV/NEXT navigation
                auto dir = outFile.getParentDirectory();
                sampleFolder.clear();
                for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.wav;*.aiff;*.aif;*.flac;*.mp3"))
                    sampleFolder.add (entry.getFile());
                struct FileSorter { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()); } };
                FileSorter fs; sampleFolder.sort (fs);
                sampleFolderIdx = sampleFolder.indexOf (outFile);

                repaint();
            }
        });
    }

    // ── FileDragAndDropTarget ──
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        // Accept audio files in Sampler (4), Wavetable (5), and Granular (6) modes
        if ((screenMode != 4 && screenMode != 5 && screenMode != 6) || currentTab != 0) return false;
        for (auto& f : files)
        {
            auto ext = juce::File (f).getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".mp3")
                return true;
        }
        return false;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { repaint(); }
    void fileDragExit (const juce::StringArray&) override { repaint(); }

    void filesDropped (const juce::StringArray& files, int x, int) override
    {
        for (auto& f : files)
        {
            juce::File file (f);
            auto ext = file.getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".mp3")
            {
                if (screenMode == 5)
                {
                    // In WT mode: left half → osc1, right half → osc2
                    wtLoadTarget = (x > getWidth() / 2) ? 1 : 0;
                    loadWavetableFromFile (file);
                }
                else  // Sampler (4) and Granular (6) share sample loading
                    loadSampleFromFile (file);
                return;
            }
        }
    }

    // ═══ WAVETABLE LOADING ═══
    void browseWavetableFile()
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Load Wavetable",
            juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
                loadWavetableFromFile (file);
        });
    }

    void loadWavetableFromFile (const juce::File& file)
    {
        if (!file.existsAsFile()) return;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
        if (!rd) return;

        // Limit to ~10 seconds at 96kHz (prevent huge memory allocation)
        int maxSamples = static_cast<int>(std::min (static_cast<long long>(rd->lengthInSamples), 960000LL));
        juce::AudioBuffer<float> buf (1, maxSamples);
        rd->read (&buf, 0, maxSamples, 0, true, false);

        auto wt = std::make_shared<WavetableData>();
        wt->importFromBuffer (buf);
        wt->name = file.getFileNameWithoutExtension();

        if (wtLoadTarget == 0)
            track.wtData1 = wt;
        else
            track.wtData2 = wt;

        // Build folder list
        auto dir = file.getParentDirectory();
        wtFolder.clear();
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.wav;*.aiff;*.aif;*.flac"))
            wtFolder.add (entry.getFile());
        struct FS { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()); } };
        FS fs; wtFolder.sort (fs);
        wtFolderIdx = wtFolder.indexOf (file);

        repaint();
        // Update embedded editor with new data
        if (wtEditorEmbed)
        {
            auto& wtRef = (wtLoadTarget == 0) ? track.wtData1 : track.wtData2;
            if (wtRef) wtEditorEmbed->setData (*wtRef);
        }
    }

    void browseWavetableFolder (int delta)
    {
        if (wtFolder.isEmpty()) return;
        wtFolderIdx = (wtFolderIdx + delta + wtFolder.size()) % wtFolder.size();
        loadWavetableFromFile (wtFolder[wtFolderIdx]);
    }

    void browseWavetableFolderDialog()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Select Wavetable Folder", juce::File());
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
        fileChooser->launchAsync (flags, [this](const juce::FileChooser& fc) {
            auto results = fc.getResults();
            if (results.isEmpty()) return;
            auto dir = results.getFirst();
            if (!dir.isDirectory()) return;
            wtFolder.clear();
            for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.wav;*.aiff;*.aif;*.flac"))
                wtFolder.add (entry.getFile());
            struct FS { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()); } };
            FS fs; wtFolder.sort (fs);
            if (!wtFolder.isEmpty()) { wtFolderIdx = 0; loadWavetableFromFile (wtFolder[0]); }
        });
    }

    void openWavetableEditor()
    {
        auto& wtRef = (wtLoadTarget == 0) ? track.wtData1 : track.wtData2;
        if (!wtRef) wtRef = std::make_shared<WavetableData>(WavetableData::createBasic());

        auto* editor = new WavetableEditor (*wtRef);
        editor->onChange = [this]() { repaint(); };
        editor->setSize (600, 360);

        juce::CallOutBox::launchAsynchronously (
            std::unique_ptr<juce::Component> (editor),
            wtEditBtn.getScreenBounds(), nullptr);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthTrackRow)
};
