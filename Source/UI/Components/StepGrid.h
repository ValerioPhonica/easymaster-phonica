#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StepButton.h"
#include "../../Sequencer/StepData.h"

class StepGrid : public juce::Component
{
public:
    StepGrid()
    {
        for (int i = 0; i < kMaxSteps; ++i)
        {
            auto* btn = new StepButton (i);
            btn->onToggle = [this](int idx, bool state) {
                fireBeforeEdit();
                if (onStepToggle) onStepToggle (idx, state);
            };
            btn->onVelChange = [this](int idx, int newVel) {
                fireBeforeEdit();
                if (onVelChange) onVelChange (idx, newVel);
            };
            btn->onGateChange = [this](int idx, int newGate) {
                fireBeforeEdit();
                if (onGateChange) onGateChange (idx, newGate);
            };
            btn->onTrigCondChange = [this](int idx, TrigCondition cond) {
                fireBeforeEdit();
                if (onTrigCondChange) onTrigCondChange (idx, cond);
            };
            btn->onPlockSelect = [this](int idx) {
                if (onPlockSelect) onPlockSelect (idx);
            };
            btn->onPlockRecToggle = [this](int idx) {
                if (onPlockRecToggle) onPlockRecToggle (idx);
            };
            btn->onPaintDrag = [this](juce::Point<int> pos, int vel) {
                for (auto* b : stepButtons)
                {
                    if (b->isVisible() && b->getBounds().contains (pos))
                    {
                        int stepIdx = b->getIndex();
                        if (onStepToggle) onStepToggle (stepIdx, true);
                        if (onVelChange) onVelChange (stepIdx, vel);
                        break;
                    }
                }
            };
            btn->onPaintEnd = []() {};
            stepButtons.add (btn);
            addChildComponent (btn);
        }
        updateVisibleSteps();
    }

    void setStepSequence (StepSequence* seq) { sequence = seq; updateStepData(); }
    void setPage (int p) { page = p; updateVisibleSteps(); }
    void setIsSynth (bool s) { for (auto* b : stepButtons) b->setIsSynth (s); }
    void setPlayingStep (int step) { currentPlayStep = step; updatePlayStates(); }

    void setPlockModePtr (bool* ptr)
    {
        for (auto* b : stepButtons) b->setPlockModePtr (ptr);
    }

    void setPlockStep (int stepIdx)
    {
        for (int i = 0; i < kMaxSteps; ++i)
            stepButtons[i]->setPlockSelected (i == stepIdx);
    }

    void setSampleSlotNames (const std::vector<juce::String>* names)
    {
        for (auto* b : stepButtons) b->setSampleSlotNames (names);
    }

    std::function<void(int, bool)> onStepToggle;
    std::function<void(int, int)>  onVelChange;
    std::function<void(int, int)>  onGateChange;
    std::function<void(int, TrigCondition)> onTrigCondChange;
    std::function<void(int)>       onPlockSelect;
    std::function<void(int)>       onPlockRecToggle;
    std::function<void()>          onBeforeStepEdit; // called once before edits begin (for undo)

    void resized() override
    {
        updateVisibleSteps();
    }

private:
    juce::OwnedArray<StepButton> stepButtons;
    StepSequence* sequence = nullptr;
    int page = 0;
    int currentPlayStep = -1;
    double lastUndoPushTime = 0.0;

    // Debounced undo capture: groups rapid edits within 1.5s into one undo state
    void fireBeforeEdit()
    {
        if (!onBeforeStepEdit) return;
        double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastUndoPushTime > 1500.0)
        {
            onBeforeStepEdit();
            lastUndoPushTime = now;
        }
    }

    void updateVisibleSteps()
    {
        int startStep = page * kStepsPerPage;
        auto bounds = getLocalBounds();
        if (bounds.getWidth() <= 0) return;

        int totalW = bounds.getWidth();
        int numGroups = 4;
        int gapSize = 3;
        int totalGaps = (numGroups - 1) * gapSize;
        int stepW = (totalW - totalGaps) / kStepsPerPage;

        for (int i = 0; i < kMaxSteps; ++i)
        {
            bool visible = (i >= startStep && i < startStep + kStepsPerPage);
            stepButtons[i]->setVisible (visible);

            if (visible)
            {
                int localIdx = i - startStep;
                int groupIdx = localIdx / 4;
                int x = localIdx * stepW + groupIdx * gapSize;
                stepButtons[i]->setBounds (x, 0, stepW - 1, bounds.getHeight());
            }
        }
        updateStepData();
    }

    void updateStepData()
    {
        if (sequence != nullptr)
        {
            for (size_t i = 0; i < kMaxSteps; ++i)
                stepButtons[static_cast<int>(i)]->setStepData (&sequence->steps[i]);
        }
        repaint();
    }

    void updatePlayStates()
    {
        for (int i = 0; i < kMaxSteps; ++i)
            stepButtons[i]->setPlaying (i == currentPlayStep);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};
