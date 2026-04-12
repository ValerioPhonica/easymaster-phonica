#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Colours.h"

class GrooveBoxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GrooveBoxLookAndFeel();

    // Slider (used for knobs and sliders)
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    // Button
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;

    // Label
    void drawLabel (juce::Graphics&, juce::Label&) override;

    // ComboBox
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    // Font
    juce::Font getLabelFont (juce::Label&) override;

private:
    juce::Font monoFont;
    juce::Font sansFont;
};
