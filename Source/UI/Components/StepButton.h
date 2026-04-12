#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Colours.h"
#include "../../Sequencer/StepData.h"
#include <random>

class StepButton : public juce::Component
{
public:
    StepButton (int stepIndex) : index (stepIndex)
    {
        setRepaintsOnMouseActivity (true);
    }

    void setStepData (StepData* data) { stepData = data; repaint(); }
    void setPlaying (bool p) { if (isCurrentlyPlaying != p) { isCurrentlyPlaying = p; repaint(); } }
    void setIsSynth (bool s) { isSynth = s; }
    int getIndex() const { return index; }

    std::function<void(int, bool)> onToggle;
    std::function<void(int, int)>  onVelChange;
    std::function<void(int, int)>  onGateChange;
    std::function<void(int, TrigCondition)> onTrigCondChange;
    std::function<void(int)>       onPlockSelect;
    std::function<void(int)>       onPlockRecToggle;
    std::function<void(juce::Point<int>, int)> onPaintDrag;
    std::function<void()>          onPaintEnd;

    void setPlockModePtr (bool* ptr) { plockModePtr = ptr; }
    void setPlockSelected (bool sel) { plockSelected = sel; repaint(); }
    void setSampleSlotNames (const std::vector<juce::String>* names) { slotNames = names; }
    std::function<void(int, int)> onSampleSlotChange; // (stepIdx, slotIdx) -1=default

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        bool active = stepData != nullptr && stepData->active;
        bool isTrigless = stepData != nullptr && stepData->trigless && active;
        bool hover = isMouseOver();

        // === BACKGROUND ===
        juce::Colour bgCol = Colours_GB::ledOff;
        if (isTrigless)
        {
            bgCol = juce::Colour (0xff160820);
        }
        else if ((plockModePtr != nullptr && *plockModePtr) && !active)
            bgCol = juce::Colour (0xff141820);
        if (active && !isTrigless)
        {
            float normVel = stepData != nullptr ? stepData->velocity / 127.0f : 0.8f;
            bgCol = isSynth ? juce::Colour (0xff0c2850) : Colours_GB::velColour (normVel);
            if ((plockModePtr != nullptr && *plockModePtr))
                bgCol = bgCol.brighter (0.12f);
        }
        if (isCurrentlyPlaying && active)
            bgCol = isTrigless ? juce::Colour (0xff7030b0) : (isSynth ? juce::Colour (0xff3090e0) : Colours_GB::ledPlay);
        else if (isCurrentlyPlaying)
            bgCol = Colours_GB::ledOff.brighter (0.12f);

        // Subtle gradient for depth
        juce::ColourGradient bgGrad (bgCol.brighter (0.04f), bounds.getX(), bounds.getY(),
                                     bgCol.darker (0.03f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (bgGrad);
        g.fillRoundedRectangle (bounds, 2.5f);

        // === BORDER (thinner, more defined) ===
        juce::Colour borderCol = Colours_GB::border.withAlpha (0.5f);
        if (active)
            borderCol = isSynth ? Colours_GB::blue.withAlpha (0.6f) : Colours_GB::accent.withAlpha (0.6f);
        if (isCurrentlyPlaying)
        {
            borderCol = Colours_GB::accentBright;
            // Outer glow
            g.setColour (Colours_GB::accentBright.withAlpha (0.12f));
            g.drawRoundedRectangle (bounds.expanded (1), 3.5f, 1.5f);
        }
        if (hover && !active)
            borderCol = borderCol.brighter (0.3f);

        g.setColour (borderCol);
        g.drawRoundedRectangle (bounds, 2.5f, 0.6f);

        // Trigless visual
        if (isTrigless)
        {
            g.setColour (Colours_GB::purple.withAlpha (0.6f));
            g.drawRoundedRectangle (bounds.reduced (1.0f), 2.0f, 1.2f);

            if (stepData && !stepData->plocks.empty())
            {
                g.setColour (Colours_GB::purple.brighter (0.3f));
                g.fillEllipse (bounds.getX() + 1.5f, bounds.getBottom() - 5.5f, 3.5f, 3.5f);
            }
            // Sample slot indicator (orange dot top-right)
            if (stepData && stepData->sampleSlot >= 0)
            {
                g.setColour (juce::Colour (0xffe07030));
                g.fillEllipse (bounds.getRight() - 5.5f, bounds.getY() + 1.5f, 3.5f, 3.5f);
            }
        }

        // P-lock selection highlight (strong visible outline + background tint)
        if (plockSelected && active)
        {
            // Background tint - bright so it stands out
            bool blink = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
            g.setColour (blink ? juce::Colour (0x30ffcc00) : juce::Colour (0x2000ffdd));
            g.fillRoundedRectangle (bounds, 2.5f);
            // Thick contrasting outline
            g.setColour (blink ? juce::Colour (0xffffc800) : juce::Colour (0xff00ffd0));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 2.5f, 2.5f);
        }

        if (active && stepData != nullptr)
        {
            // Velocity bar at bottom
            float velNorm = stepData->velocity / 127.0f;
            float barH = 2.5f;
            auto velCol = isSynth ? Colours_GB::blue : Colours_GB::accent;
            g.setColour (velCol.withAlpha (0.85f));
            g.fillRoundedRectangle (bounds.getX() + 1, bounds.getBottom() - barH - 0.5f,
                                     (bounds.getWidth() - 2) * velNorm, barH, 1.0f);

            // Gate bar at top
            float gateNorm = std::min (1.0f, stepData->gate / 100.0f);
            auto gateCol = stepData->gate > 100 ? Colours_GB::amber : Colours_GB::green;
            g.setColour (gateCol.withAlpha (0.6f));
            g.fillRoundedRectangle (bounds.getX() + 1, bounds.getY() + 0.5f,
                                     (bounds.getWidth() - 2) * gateNorm, 2.0f, 1.0f);

            // Trig condition (small dot top-right)
            if (stepData->cond != TrigCondition::Always)
            {
                g.setColour (Colours_GB::purple.brighter (0.2f));
                g.fillEllipse (bounds.getRight() - 5.5f, bounds.getY() + 2.0f, 3.5f, 3.5f);
            }

            // Slide (dot top-left)
            if (stepData->slide)
            {
                g.setColour (Colours_GB::cyan);
                g.fillEllipse (bounds.getX() + 2.0f, bounds.getY() + 2.0f, 3.5f, 3.5f);
            }

            // P-lock (dot bottom-left)
            if (!stepData->plocks.empty())
            {
                g.setColour (Colours_GB::accent);
                g.fillEllipse (bounds.getX() + 2.0f, bounds.getBottom() - 5.5f, 3.5f, 3.5f);
            }

            // Velocity text when dragging
            if ((paintMode || velDragMode) && stepData->active)
            {
                g.setColour (juce::Colours::white);
                g.setFont (8.0f);
                g.drawText (juce::String (stepData->velocity),
                            getLocalBounds(), juce::Justification::centred);
            }
            else if (gateDragMode && stepData->active)
            {
                g.setColour (Colours_GB::green);
                g.setFont (8.0f);
                g.drawText (juce::String (stepData->gate) + "%",
                            getLocalBounds(), juce::Justification::centred);
            }
        }

        // Hover highlight overlay
        if (hover && !isCurrentlyPlaying)
        {
            g.setColour (juce::Colour (0x08ffffff));
            g.fillRoundedRectangle (bounds, 2.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        wasActive = stepData != nullptr && stepData->active;
        hasDragged = false;
        paintMode = false;
        velDragMode = false;
        gateDragMode = false;
        plockClicked = false;

        if (e.mods.isRightButtonDown())
        {
            hasDragged = true;
            showStepMenu();
            return;
        }
        if (e.mods.isLeftButtonDown())
        {
            // Shift+click: toggle TRIGLESS (automation step — p-locks without note)
            if (e.mods.isShiftDown() && stepData != nullptr)
            {
                hasDragged = true; // prevent normal toggle in mouseUp
                if (!stepData->active)
                {
                    // Empty step -> create trigless step
                    stepData->active = true;
                    stepData->trigless = true;
                }
                else if (!stepData->trigless)
                {
                    // Active normal step -> convert to trigless
                    stepData->trigless = true;
                }
                else
                {
                    // Trigless step -> deactivate completely
                    stepData->active = false;
                    stepData->trigless = false;
                }
                repaint();
                return;
            }

            bool inPlockMode = plockModePtr != nullptr && *plockModePtr;
            if (inPlockMode && stepData != nullptr && stepData->active)
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
        bool inPlockMode = plockModePtr != nullptr && *plockModePtr;
        if (inPlockMode) return;

        int dy = -(e.getScreenY() - dragStartY);
        int rawDx = e.getDistanceFromDragStartX();
        int signedDx = e.getScreenX() - dragStartX;

        if (!paintMode && !velDragMode && !gateDragMode && e.getDistanceFromDragStart() > 6)
        {
            hasDragged = true;
            if (wasActive && std::abs (dy) > std::abs (rawDx))
            {
                // Vertical dominant -> velocity
                velDragMode = true;
                startVel = (stepData != nullptr) ? stepData->velocity : 100;
            }
            else if (wasActive && signedDx < -4)
            {
                // Drag LEFT on active step -> gate length
                gateDragMode = true;
                startGate = (stepData != nullptr) ? stepData->gate : 100;
            }
            else if (signedDx > 4)
            {
                // Drag RIGHT -> paint mode
                paintMode = true;
                if (!wasActive && onToggle)
                    onToggle (index, true);
            }
        }

        if (velDragMode && stepData != nullptr)
        {
            int newVel = juce::jlimit (1, 127, startVel + static_cast<int>(dy * 1.5f));
            if (onVelChange) onVelChange (index, newVel);
            repaint();
        }

        if (gateDragMode && stepData != nullptr)
        {
            int newGate = juce::jlimit (5, 200, startGate + static_cast<int>(signedDx * 1.5f));
            if (onGateChange) onGateChange (index, newGate);
            repaint();
        }

        if (paintMode)
        {
            paintVelocity = juce::jlimit (1, 127, 100 + static_cast<int>(dy * 1.5f));
            if (onPaintDrag)
                onPaintDrag (e.getEventRelativeTo (getParentComponent()).getPosition(), paintVelocity);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (plockClicked) return;
        bool inPlockMode = plockModePtr != nullptr && *plockModePtr;
        if (inPlockMode) return;

        if (!hasDragged)
        {
            // Single click on trigless -> convert to normal active step
            if (stepData != nullptr && stepData->trigless && stepData->active)
            {
                stepData->trigless = false;
                repaint();
            }
            else
            {
                // Normal toggle on/off
                if (onToggle) onToggle (index, !wasActive);
                // Clear trigless when deactivating
                if (stepData != nullptr && !stepData->active)
                    stepData->trigless = false;
            }
        }

        if (paintMode && onPaintEnd)
            onPaintEnd();

        paintMode = false;
        velDragMode = false;
        gateDragMode = false;
        repaint();
    }

private:
    int index;
    StepData* stepData = nullptr;
    bool isCurrentlyPlaying = false;
    bool isSynth = false;
    bool hasDragged = false;
    bool wasActive = false;
    bool paintMode = false;
    bool velDragMode = false;
    bool gateDragMode = false;
    bool plockSelected = false;
    bool plockClicked = false;
    bool* plockModePtr = nullptr;
    const std::vector<juce::String>* slotNames = nullptr;
    int paintVelocity = 100;
    int startVel = 100;
    int startGate = 100;
    int dragStartY = 0;
    int dragStartX = 0;
    static inline StepData s_stepClipboard;
    static inline bool s_stepClipboardValid = false;

    void showStepMenu()
    {
        if (stepData == nullptr) return;
        bool active = stepData->active;

        // TRIG CONDITION submenu
        juce::PopupMenu trigMenu;
        trigMenu.addItem (1,  "ALWAYS",  true, stepData->cond == TrigCondition::Always);
        trigMenu.addSeparator();
        trigMenu.addItem (2,  "50%",     true, stepData->cond == TrigCondition::P50);
        trigMenu.addItem (3,  "75%",     true, stepData->cond == TrigCondition::P75);
        trigMenu.addItem (4,  "25%",     true, stepData->cond == TrigCondition::P25);
        trigMenu.addItem (5,  "12%",     true, stepData->cond == TrigCondition::P12);
        trigMenu.addSeparator();
        trigMenu.addItem (6,  "1 of 2",  true, stepData->cond == TrigCondition::OneOf2);
        trigMenu.addItem (7,  "2 of 2",  true, stepData->cond == TrigCondition::TwoOf2);
        trigMenu.addItem (8,  "1 of 3",  true, stepData->cond == TrigCondition::OneOf3);
        trigMenu.addItem (9,  "2 of 3",  true, stepData->cond == TrigCondition::TwoOf3);
        trigMenu.addItem (10, "3 of 3",  true, stepData->cond == TrigCondition::ThreeOf3);
        trigMenu.addItem (11, "1 of 4",  true, stepData->cond == TrigCondition::OneOf4);
        trigMenu.addItem (12, "2 of 4",  true, stepData->cond == TrigCondition::TwoOf4);
        trigMenu.addItem (13, "3 of 4",  true, stepData->cond == TrigCondition::ThreeOf4);
        trigMenu.addItem (14, "4 of 4",  true, stepData->cond == TrigCondition::FourOf4);
        trigMenu.addSeparator();
        trigMenu.addItem (15, "FILL",    true, stepData->cond == TrigCondition::Fill);
        trigMenu.addItem (16, "NOT FILL",true, stepData->cond == TrigCondition::NotFill);

        // NUDGE submenu
        juce::PopupMenu nudgeMenu;
        nudgeMenu.addItem (300, "<<< -50%", true, stepData->nudge == -50);
        nudgeMenu.addItem (301, "<< -25%",  true, stepData->nudge == -25);
        nudgeMenu.addItem (302, "< -10%",   true, stepData->nudge == -10);
        nudgeMenu.addItem (303, "CENTER 0",  true, stepData->nudge == 0);
        nudgeMenu.addItem (304, "> +10%",   true, stepData->nudge == 10);
        nudgeMenu.addItem (305, ">> +25%",  true, stepData->nudge == 25);
        nudgeMenu.addItem (306, ">>> +50%", true, stepData->nudge == 50);

        // NOTE LENGTH submenu
        juce::PopupMenu lenMenu;
        lenMenu.addItem (400, "1 step",    true, stepData->noteLen == 1);
        lenMenu.addItem (401, "2 steps",   true, stepData->noteLen == 2);
        lenMenu.addItem (402, "3 steps",   true, stepData->noteLen == 3);
        lenMenu.addItem (403, "4 steps",   true, stepData->noteLen == 4);
        lenMenu.addItem (404, "6 steps",   true, stepData->noteLen == 6);
        lenMenu.addItem (405, "8 steps",   true, stepData->noteLen == 8);
        lenMenu.addItem (406, "12 steps",  true, stepData->noteLen == 12);
        lenMenu.addItem (407, "16 steps",  true, stepData->noteLen == 16);
        lenMenu.addItem (408, "24 steps",  true, stepData->noteLen == 24);
        lenMenu.addItem (409, "32 steps",  true, stepData->noteLen == 32);
        lenMenu.addItem (410, "48 steps",  true, stepData->noteLen == 48);
        lenMenu.addItem (411, "64 steps",  true, stepData->noteLen == 64);
        lenMenu.addItem (412, "96 steps",  true, stepData->noteLen == 96);
        lenMenu.addItem (413, "128 steps", true, stepData->noteLen == 128);

        juce::PopupMenu menu;
        if (active) menu.addSubMenu ("TRIG COND", trigMenu);
        if (active) menu.addSubMenu ("NUDGE", nudgeMenu);
        if (active) menu.addSubMenu ("NOTE LEN", lenMenu);
        menu.addSeparator();
        if (active) menu.addItem (800, "COPY STEP");
        menu.addItem (801, "PASTE STEP", s_stepClipboardValid);
        menu.addItem (850, "RANDOMIZE STEP");

        // ── Sample slot selector (Octatrack-style per-step sample) ──
        if (active && slotNames != nullptr && !slotNames->empty())
        {
            juce::PopupMenu slotMenu;
            slotMenu.addItem (700, "DEFAULT", true, stepData->sampleSlot < 0);
            for (int si = 0; si < static_cast<int>(slotNames->size()); ++si)
                slotMenu.addItem (701 + si, (*slotNames)[static_cast<size_t>(si)],
                                  true, stepData->sampleSlot == si);
            menu.addSubMenu ("SAMPLE SLOT" + juce::String (stepData->sampleSlot >= 0
                ? " [" + (*slotNames)[static_cast<size_t>(std::clamp (static_cast<int>(stepData->sampleSlot), 0,
                    static_cast<int>(slotNames->size()) - 1))] + "]" : ""), slotMenu);
        }

        std::vector<std::string> plockKeysInMenu;
        if (active && !stepData->plocks.empty())
        {
            juce::PopupMenu plockMenu;
            int plockIdx = 600;
            for (const auto& [key, val] : stepData->plocks)
            {
                juce::String displayName (key);
                juce::String valStr = juce::String (val, 2);
                plockMenu.addItem (plockIdx, juce::String ("x  ") + displayName + " = " + valStr);
                plockKeysInMenu.push_back (key);
                ++plockIdx;
            }
            plockMenu.addSeparator();
            plockMenu.addItem (599, "CLEAR ALL P-LOCKS");
            menu.addSubMenu ("P-LOCKS (" + juce::String (static_cast<int>(stepData->plocks.size())) + ")", plockMenu);
        }
        if (active)
        {
            menu.addSeparator();
            bool isRecording = plockModePtr != nullptr && *plockModePtr
                && plockSelected;
            menu.addItem (900, isRecording ? "STOP P-LOCK REC" : "P-LOCK REC");
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this, plockKeysInMenu = std::move(plockKeysInMenu)](int result) {
                if (result == 0 || stepData == nullptr) return;

                if (result >= 600 && result < 600 + static_cast<int>(plockKeysInMenu.size()))
                {
                    auto& key = plockKeysInMenu[static_cast<size_t>(result - 600)];
                    stepData->plocks.erase (key);
                    repaint();
                    return;
                }

                // ── Sample slot selection ──
                if (result == 700)
                {
                    stepData->sampleSlot = -1;
                    if (onSampleSlotChange) onSampleSlotChange (index, -1);
                    repaint(); return;
                }
                if (result >= 701 && result < 701 + 128)
                {
                    stepData->sampleSlot = result - 701;
                    if (onSampleSlotChange) onSampleSlotChange (index, result - 701);
                    repaint(); return;
                }

                if (result >= 1 && result <= 16)
                {
                    TrigCondition conds[] = {
                        TrigCondition::Always, TrigCondition::P50, TrigCondition::P75,
                        TrigCondition::P25, TrigCondition::P12,
                        TrigCondition::OneOf2, TrigCondition::TwoOf2,
                        TrigCondition::OneOf3, TrigCondition::TwoOf3, TrigCondition::ThreeOf3,
                        TrigCondition::OneOf4, TrigCondition::TwoOf4, TrigCondition::ThreeOf4, TrigCondition::FourOf4,
                        TrigCondition::Fill, TrigCondition::NotFill
                    };
                    stepData->cond = conds[result - 1];
                    if (onTrigCondChange) onTrigCondChange (index, stepData->cond);
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
                else if (result == 599) stepData->plocks.clear();
                else if (result == 900)
                {
                    if (onPlockRecToggle) onPlockRecToggle (index);
                }
                else if (result == 800)
                {
                    s_stepClipboard = *stepData;
                    s_stepClipboardValid = true;
                }
                else if (result == 801 && s_stepClipboardValid)
                {
                    *stepData = s_stepClipboard;
                    stepData->active = true;
                    if (onToggle) onToggle (index, true);
                }
                else if (result == 850)
                {
                    std::mt19937 rng { std::random_device{}() };
                    stepData->active = true;
                    stepData->velocity = static_cast<uint8_t>(std::uniform_int_distribution<int>(40, 127)(rng));
                    stepData->gate = static_cast<uint8_t>(std::uniform_int_distribution<int>(30, 150)(rng));
                    stepData->ratchet = static_cast<uint8_t>(std::uniform_int_distribution<int>(1, 4)(rng));
                    int8_t nudges[] = { -50, -25, -10, 0, 0, 0, 0, 10, 25, 50 };
                    stepData->nudge = nudges[std::uniform_int_distribution<int>(0, 9)(rng)];
                    if (onToggle) onToggle (index, true);
                }
                repaint();
            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepButton)
};
