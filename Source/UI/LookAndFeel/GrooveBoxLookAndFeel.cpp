#include "GrooveBoxLookAndFeel.h"

GrooveBoxLookAndFeel::GrooveBoxLookAndFeel()
{
    monoFont = juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);
    sansFont = juce::Font (juce::Font::getDefaultSansSerifFontName(), 12.0f, juce::Font::plain);

    setColour (juce::ResizableWindow::backgroundColourId, Colours_GB::bg);
    setColour (juce::TextButton::buttonColourId, Colours_GB::panel2);
    setColour (juce::TextButton::textColourOffId, Colours_GB::text);
    setColour (juce::TextButton::buttonOnColourId, Colours_GB::accent);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff001820));
    setColour (juce::Label::textColourId, Colours_GB::text);
    setColour (juce::Slider::rotarySliderFillColourId, Colours_GB::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, Colours_GB::border);
    setColour (juce::ComboBox::backgroundColourId, Colours_GB::panel2);
    setColour (juce::ComboBox::outlineColourId, Colours_GB::border);
    setColour (juce::ComboBox::textColourId, Colours_GB::text);
    setColour (juce::ComboBox::arrowColourId, Colours_GB::accent);
    setColour (juce::PopupMenu::backgroundColourId, Colours_GB::panel2);
    setColour (juce::PopupMenu::textColourId, Colours_GB::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours_GB::accent.withAlpha (0.2f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours_GB::accentBright);
    setColour (juce::PopupMenu::headerTextColourId, Colours_GB::accent);
}

void GrooveBoxLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                              int x, int y, int width, int height,
                                              float sliderPos,
                                              float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto cx = bounds.getCentreX();
    auto cy = bounds.getCentreY();
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // === ARC TRACK (thin, dark) ===
    float arcR = radius - 0.5f;
    {
        juce::Path trackArc;
        trackArc.addCentredArc (cx, cy, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (Colours_GB::knobTrack);
        g.strokePath (trackArc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
    }

    // === ARC FILL - cyan with glow ===
    if (sliderPos > 0.005f)
    {
        juce::Path fillArc;
        fillArc.addCentredArc (cx, cy, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
        // Outer glow layer
        g.setColour (Colours_GB::accent.withAlpha (0.12f));
        g.strokePath (fillArc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        // Main arc
        g.setColour (Colours_GB::accent.withAlpha (0.9f));
        g.strokePath (fillArc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }

    // === BODY - modern metallic gradient ===
    float bodyR = radius - 5.0f;
    if (bodyR > 2.0f)
    {
        // Drop shadow
        g.setColour (juce::Colour (0xff060810));
        g.fillEllipse (cx - bodyR - 1.5f, cy - bodyR, (bodyR + 1.5f) * 2, (bodyR + 1.5f) * 2);
        // Body gradient - cool tones
        juce::ColourGradient bodyGrad (
            Colours_GB::knobHi, cx - bodyR * 0.5f, cy - bodyR * 0.6f,
            Colours_GB::knobBody, cx + bodyR * 0.5f, cy + bodyR * 0.6f, true);
        g.setGradientFill (bodyGrad);
        g.fillEllipse (cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);
        // Rim highlight
        g.setColour (juce::Colour (0x0effffff));
        g.drawEllipse (cx - bodyR + 0.5f, cy - bodyR + 0.5f, bodyR * 2 - 1, bodyR * 2 - 1, 0.5f);
    }

    // === INDICATOR LINE - dual layer for glow ===
    float lineInner = bodyR * 0.2f;
    float lineOuter = bodyR * 0.82f;
    float ia = angle - juce::MathConstants<float>::halfPi;
    float cosA = std::cos (ia), sinA = std::sin (ia);
    // Glow
    g.setColour (Colours_GB::accent.withAlpha (0.15f));
    g.drawLine (cx + lineInner * cosA, cy + lineInner * sinA,
                cx + lineOuter * cosA, cy + lineOuter * sinA, 3.5f);
    // Bright line
    g.setColour (Colours_GB::accent);
    g.drawLine (cx + lineInner * cosA, cy + lineInner * sinA,
                cx + lineOuter * cosA, cy + lineOuter * sinA, 1.5f);
}

void GrooveBoxLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                              int x, int y, int width, int height,
                                              float sliderPos, float, float,
                                              juce::Slider::SliderStyle style, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    auto trackCol = slider.findColour (juce::Slider::trackColourId);
    auto thumbCol = slider.findColour (juce::Slider::thumbColourId);
    bool useCustomCol = (thumbCol != juce::Colours::transparentBlack && thumbCol.getAlpha() > 10);
    auto fillCol = useCustomCol ? thumbCol : Colours_GB::accent;

    if (style == juce::Slider::LinearHorizontal)
    {
        float trackY = bounds.getCentreY();
        float trackH = 2.5f;
        // Track background
        g.setColour (Colours_GB::knobTrack);
        g.fillRoundedRectangle (bounds.getX(), trackY - trackH / 2, bounds.getWidth(), trackH, trackH / 2);
        // Filled portion
        g.setColour (fillCol.withAlpha (0.8f));
        float fillW = sliderPos - bounds.getX();
        g.fillRoundedRectangle (bounds.getX(), trackY - trackH / 2, fillW, trackH, trackH / 2);
        // Glow on fill
        g.setColour (fillCol.withAlpha (0.1f));
        g.fillRoundedRectangle (bounds.getX(), trackY - trackH - 2, fillW, trackH + 4, 2.0f);
        // Thumb
        float thumbW = 5.0f, thumbH = 10.0f;
        g.setColour (Colours_GB::panel3);
        g.fillRoundedRectangle (sliderPos - thumbW / 2, trackY - thumbH / 2, thumbW, thumbH, 2.0f);
        g.setColour (fillCol);
        g.drawRoundedRectangle (sliderPos - thumbW / 2, trackY - thumbH / 2, thumbW, thumbH, 2.0f, 1.0f);
    }
    else if (style == juce::Slider::LinearVertical)
    {
        float trackX = bounds.getCentreX();
        float trackW = 2.5f;
        // Track background
        g.setColour (Colours_GB::knobTrack);
        g.fillRoundedRectangle (trackX - trackW / 2, bounds.getY(), trackW, bounds.getHeight(), trackW / 2);
        // Filled portion (from bottom up)
        float fillH = bounds.getBottom() - sliderPos;
        g.setColour (fillCol.withAlpha (0.8f));
        g.fillRoundedRectangle (trackX - trackW / 2, sliderPos, trackW, fillH, trackW / 2);
        // Thumb
        float thumbW = 14.0f, thumbH = 5.0f;
        g.setColour (Colours_GB::panel3);
        g.fillRoundedRectangle (trackX - thumbW / 2, sliderPos - thumbH / 2, thumbW, thumbH, 2.0f);
        g.setColour (fillCol);
        g.drawRoundedRectangle (trackX - thumbW / 2, sliderPos - thumbH / 2, thumbW, thumbH, 2.0f, 1.0f);
    }
}

void GrooveBoxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                   const juce::Colour&, bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto base = button.findColour (button.getToggleState()
        ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    if (isDown) base = base.brighter (0.15f);
    else if (isHighlighted) base = base.brighter (0.06f);

    // Subtle top-to-bottom gradient
    juce::ColourGradient grad (base.brighter (0.04f), 0.0f, bounds.getY(),
                               base.darker (0.06f), 0.0f, bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bounds, 3.0f);

    // Border with accent on hover
    auto borderCol = button.getToggleState() ? base.brighter (0.15f) : Colours_GB::border;
    if (isHighlighted) borderCol = Colours_GB::accent.withAlpha (0.4f);
    g.setColour (borderCol);
    g.drawRoundedRectangle (bounds, 3.0f, 0.6f);

    // Subtle inner highlight at top edge
    if (button.getToggleState())
    {
        g.setColour (juce::Colour (0x10ffffff));
        g.drawLine (bounds.getX() + 4, bounds.getY() + 1, bounds.getRight() - 4, bounds.getY() + 1, 0.5f);
    }
}

void GrooveBoxLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setColour (button.findColour (button.getToggleState()
        ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));
    g.setFont (monoFont.withHeight (10.0f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

void GrooveBoxLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (monoFont.withHeight (label.getFont().getHeight()));
    g.drawText (label.getText(), label.getLocalBounds(), label.getJustificationType(), false);
}

void GrooveBoxLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                          int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0, 0, static_cast<float>(width), static_cast<float>(height));
    g.setColour (Colours_GB::panel2);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (isButtonDown ? Colours_GB::accent.withAlpha (0.5f) : Colours_GB::border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 0.6f);

    // Arrow
    float arrowX = static_cast<float>(width) - 14.0f;
    float arrowY = static_cast<float>(height) * 0.5f - 2.0f;
    juce::Path arrow;
    arrow.addTriangle (arrowX, arrowY, arrowX + 8.0f, arrowY, arrowX + 4.0f, arrowY + 5.0f);
    g.setColour (Colours_GB::accent.withAlpha (0.7f));
    g.fillPath (arrow);
}

juce::Font GrooveBoxLookAndFeel::getLabelFont (juce::Label& label)
{
    return monoFont.withHeight (label.getFont().getHeight());
}
