#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Colours.h"
#include "../../Sequencer/StepData.h"

class RatchetRow : public juce::Component
{
public:
    RatchetRow() = default;

    void setStepSequence (StepSequence* seq) { sequence = seq; repaint(); }
    void setPage (int p) { page = p; repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (sequence == nullptr) return;

        auto bounds = getLocalBounds();
        int totalW = bounds.getWidth();
        int numGroups = 4;
        int gapSize = 3;
        int totalGaps = (numGroups - 1) * gapSize;
        int btnW = (totalW - totalGaps) / kStepsPerPage;
        int startStep = page * kStepsPerPage;

        for (int i = 0; i < kStepsPerPage; ++i)
        {
            int si = startStep + i;
            if (si >= kMaxSteps) break;

            int groupIdx = i / 4;
            int x = bounds.getX() + i * btnW + groupIdx * gapSize;
            auto btnBounds = juce::Rectangle<float> (static_cast<float>(x), static_cast<float>(bounds.getY()),
                                                       static_cast<float>(btnW - 1), static_cast<float>(bounds.getHeight()));

            auto& step = sequence->steps[static_cast<size_t>(si)];
            int ratch = step.ratchet;
            bool trip = step.triplet;

            // Background
            juce::Colour bg (0xff0d0d0d);
            juce::Colour textCol (0xff2a2a2a);
            juce::Colour borderCol (0xff1a1a1a);

            if (ratch == 1 && step.active)      { bg = juce::Colour (0xff111111); textCol = juce::Colour (0xff444444); borderCol = juce::Colour (0xff333333); }
            else if (ratch == 2) { bg = juce::Colour (0xff001833); textCol = Colours_GB::blue; borderCol = juce::Colour (0xff0a4a8a); }
            else if (ratch == 3) { bg = juce::Colour (0xff2a1500); textCol = Colours_GB::accent; borderCol = juce::Colour (0xff7a4800); }
            else if (ratch == 4) { bg = juce::Colour (0xff2a0800); textCol = Colours_GB::red; borderCol = juce::Colour (0xff7a1810); }

            if (trip)
            {
                borderCol = Colours_GB::cyan;
                textCol = Colours_GB::cyan;
            }

            g.setColour (bg);
            g.fillRoundedRectangle (btnBounds, 2.0f);
            g.setColour (borderCol);
            g.drawRoundedRectangle (btnBounds, 2.0f, 0.5f);

            // Text
            g.setColour (textCol);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
            juce::String label = trip ? juce::String (ratch) + "T" : juce::String (ratch);
            g.drawText (label, btnBounds.toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (sequence == nullptr) return;

        int si = getStepAtPosition (e.getPosition().x);
        if (si < 0 || si >= kMaxSteps) return;

        auto& step = sequence->steps[static_cast<size_t>(si)];

        if (e.mods.isRightButtonDown())
        {
            // Right click = toggle triplet
            step.triplet = !step.triplet;
        }
        else
        {
            // Left click = cycle ratchet 1→2→3→4→1
            step.ratchet = static_cast<uint8_t>((step.ratchet % 4) + 1);
        }
        repaint();
    }

private:
    StepSequence* sequence = nullptr;
    int page = 0;

    int getStepAtPosition (int mouseX) const
    {
        auto bounds = getLocalBounds();
        int totalW = bounds.getWidth();
        int numGroups = 4;
        int gapSize = 3;
        int totalGaps = (numGroups - 1) * gapSize;
        int btnW = (totalW - totalGaps) / kStepsPerPage;

        for (int i = 0; i < kStepsPerPage; ++i)
        {
            int groupIdx = i / 4;
            int x = bounds.getX() + i * btnW + groupIdx * gapSize;
            if (mouseX >= x && mouseX < x + btnW)
                return page * kStepsPerPage + i;
        }
        return -1;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RatchetRow)
};
