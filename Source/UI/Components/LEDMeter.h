#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Colours.h"

class LEDMeter : public juce::Component
{
public:
    LEDMeter (int /*numLeds*/ = 12, bool /*horizontal*/ = false) {}

    void setLevel (float newLevel)
    {
        float clamped = juce::jlimit (0.0f, 1.0f, newLevel);
        if (clamped > peakLevel) { peakLevel = clamped; peakHold = 30; }
        level = clamped;
        if (peakHold > 0) --peakHold;
        else peakLevel = std::max (0.0f, peakLevel - 0.015f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff080a10));
        g.fillRoundedRectangle (b, 2.0f);

        float segH = 2.0f, segGap = 1.0f;
        float bx = b.getX() + 1.0f, bw = b.getWidth() - 2.0f;
        float by = b.getY() + 1.0f, bh = b.getHeight() - 2.0f;
        int n = static_cast<int>(bh / (segH + segGap));

        for (int i = 0; i < n; ++i)
        {
            float norm = static_cast<float>(i) / static_cast<float>(n);
            float sy = by + bh - static_cast<float>(i) * (segH + segGap) - segH;
            if (sy < by) break;
            juce::Colour c = (norm > 0.88f) ? juce::Colour (0xffdd3030)
                           : (norm > 0.70f) ? juce::Colour (0xffc0a020)
                                            : Colours_GB::accent.withAlpha (0.85f);
            g.setColour ((norm < level) ? c.withAlpha (0.9f) : juce::Colour (0xff0e1018));
            g.fillRect (bx, sy, bw, segH);
        }
        // Peak hold line
        if (peakLevel > 0.02f)
        {
            float py = by + bh * (1.0f - peakLevel);
            juce::Colour pc = (peakLevel > 0.88f) ? juce::Colour (0xffff4040)
                            : (peakLevel > 0.70f) ? juce::Colour (0xffe0c030)
                                                  : Colours_GB::accentBright;
            g.setColour (pc);
            g.fillRect (bx, std::max (by, py), bw, 1.5f);
        }
    }

private:
    float level = 0.0f, peakLevel = 0.0f;
    int peakHold = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LEDMeter)
};
