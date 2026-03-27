#include "PluginProcessor.h"

// ═══════════════════════════════════════════════════════════════
//  STAGE BLOCK COMPONENT — draggable + clickable
// ═══════════════════════════════════════════════════════════════

class StageBlock : public juce::Component
{
public:
    StageBlock (const juce::String& name, juce::Colour col, int index, bool fixed)
        : stageName (name), colour (col), stageIndex (index), isFixed (fixed) {}

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().reduced (2).toFloat();
        g.setColour (selected ? colour.withAlpha (0.6f) : colour.withAlpha (0.3f));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (selected ? colour.brighter (0.5f) : colour);
        g.drawRoundedRectangle (bounds, 6.0f, selected ? 2.5f : 1.5f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (stageName, getLocalBounds(), juce::Justification::centred);
        if (dragging)
        {
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.fillRoundedRectangle (bounds, 6.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (!isFixed) dragStartX = e.x;
        if (onClick) onClick (stageIndex);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isFixed) return;
        if (std::abs (e.x - dragStartX) > 10 && !dragging)
            dragging = true;
        if (dragging && onDrag)
            onDrag (stageIndex, e.getEventRelativeTo (getParentComponent()).x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && onDragEnd)
            onDragEnd (stageIndex);
        dragging = false;
    }

    std::function<void(int)> onClick;
    std::function<void(int, int)> onDrag;
    std::function<void(int)> onDragEnd;
    bool selected = false;
    int stageIndex;
    juce::String getStageName() const { return stageName; }

private:
    juce::String stageName;
    juce::Colour colour;
    bool isFixed;
    bool dragging = false;
    int dragStartX = 0;
};

// ═══════════════════════════════════════════════════════════════
//  KNOB HELPER — rotary knob attached to a parameter
// ═══════════════════════════════════════════════════════════════

class AttachedKnob : public juce::Component
{
public:
    AttachedKnob (juce::AudioProcessorValueTreeState& apvts,
                  const juce::String& paramID, const juce::String& label)
        : labelText (label)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFE94560));
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);

        nameLabel.setText (label, juce::dontSendNotification);
        nameLabel.setFont (juce::Font (10.0f));
        nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFAAAAAA));
        nameLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (nameLabel);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        nameLabel.setBounds (area.removeFromTop (14));
        slider.setBounds (area);
    }

private:
    juce::Slider slider;
    juce::Label nameLabel;
    juce::String labelText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ═══════════════════════════════════════════════════════════════
//  TOGGLE HELPER — on/off button attached to a parameter
// ═══════════════════════════════════════════════════════════════

class AttachedToggle : public juce::Component
{
public:
    AttachedToggle (juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID, const juce::String& label)
    {
        button.setButtonText (label);
        button.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        button.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFFE94560));
        addAndMakeVisible (button);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramID, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// ═══════════════════════════════════════════════════════════════
//  COMBO HELPER — choice attached to a parameter
// ═══════════════════════════════════════════════════════════════

class AttachedCombo : public juce::Component
{
public:
    AttachedCombo (juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& paramID, const juce::String& label)
    {
        nameLabel.setText (label, juce::dontSendNotification);
        nameLabel.setFont (juce::Font (10.0f));
        nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFAAAAAA));
        nameLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (nameLabel);

        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
        {
            auto choices = param->choices;
            for (int i = 0; i < choices.size(); ++i)
                combo.addItem (choices[i], i + 1);
        }
        addAndMakeVisible (combo);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramID, combo);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        nameLabel.setBounds (area.removeFromTop (14));
        combo.setBounds (area.reduced (2));
    }

private:
    juce::ComboBox combo;
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// ═══════════════════════════════════════════════════════════════
//  STAGE PANELS — one per stage with real controls
// ═══════════════════════════════════════════════════════════════

class InputPanel : public juce::Component
{
public:
    InputPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff   = std::make_unique<AttachedToggle> (a, "S1_Input_On", "ON"); addAndMakeVisible (onOff  .get());
        gain    = std::make_unique<AttachedKnob> (a, "S1_Input_Gain", "Gain"); addAndMakeVisible (gain   .get());
        xover   = std::make_unique<AttachedKnob> (a, "S1_Input_Crossover", "Crossover"); addAndMakeVisible (xover  .get());
        lowW    = std::make_unique<AttachedKnob> (a, "S1_Input_Low_Width", "Low Width"); addAndMakeVisible (lowW   .get());
        highW   = std::make_unique<AttachedKnob> (a, "S1_Input_High_Width", "High Width"); addAndMakeVisible (highW  .get());
        midG    = std::make_unique<AttachedKnob> (a, "S1_Input_Mid_Gain", "Mid"); addAndMakeVisible (midG   .get());
        sideG   = std::make_unique<AttachedKnob> (a, "S1_Input_Side_Gain", "Side"); addAndMakeVisible (sideG  .get());
        xMode   = std::make_unique<AttachedCombo> (a, "S1_Input_Crossover_Mode", "Phase"); addAndMakeVisible (xMode  .get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 8;
        onOff->setBounds (area.removeFromLeft (kw).withHeight (30));
        gain->setBounds (area.removeFromLeft (kw));
        xover->setBounds (area.removeFromLeft (kw));
        lowW->setBounds (area.removeFromLeft (kw));
        highW->setBounds (area.removeFromLeft (kw));
        midG->setBounds (area.removeFromLeft (kw));
        sideG->setBounds (area.removeFromLeft (kw));
        xMode->setBounds (area.removeFromLeft (kw).withHeight (40));
    }
private:
    std::unique_ptr<AttachedToggle> onOff;
    std::unique_ptr<AttachedKnob> gain, xover, lowW, highW, midG, sideG;
    std::unique_ptr<AttachedCombo> xMode;
};

class PultecPanel : public juce::Component
{
public:
    PultecPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff = std::make_unique<AttachedToggle> (a, "S2_EQ_On", "ON"); addAndMakeVisible (onOff.get());
        lbF = std::make_unique<AttachedKnob> (a, "S2_EQ_LowBoost_Freq", "LB Freq"); addAndMakeVisible (lbF.get());
        lbG = std::make_unique<AttachedKnob> (a, "S2_EQ_LowBoost_Gain", "LB Gain"); addAndMakeVisible (lbG.get());
        laF = std::make_unique<AttachedKnob> (a, "S2_EQ_LowAtten_Freq", "LA Freq"); addAndMakeVisible (laF.get());
        laG = std::make_unique<AttachedKnob> (a, "S2_EQ_LowAtten_Gain", "LA Gain"); addAndMakeVisible (laG.get());
        hbF = std::make_unique<AttachedKnob> (a, "S2_EQ_HighBoost_Freq", "HB Freq"); addAndMakeVisible (hbF.get());
        hbG = std::make_unique<AttachedKnob> (a, "S2_EQ_HighBoost_Gain", "HB Gain"); addAndMakeVisible (hbG.get());
        haF = std::make_unique<AttachedKnob> (a, "S2_EQ_HighAtten_Freq", "HA Freq"); addAndMakeVisible (haF.get());
        haBW = std::make_unique<AttachedKnob> (a, "S2_EQ_HighAtten_BW", "HA BW"); addAndMakeVisible (haBW.get());
        lmF = std::make_unique<AttachedKnob> (a, "S2_EQ_LowMid_Freq", "LM Freq"); addAndMakeVisible (lmF.get());
        lmG = std::make_unique<AttachedKnob> (a, "S2_EQ_LowMid_Gain", "LM Gain"); addAndMakeVisible (lmG.get());
        mdF = std::make_unique<AttachedKnob> (a, "S2_EQ_MidDip_Freq", "Dip Freq"); addAndMakeVisible (mdF.get());
        mdG = std::make_unique<AttachedKnob> (a, "S2_EQ_MidDip_Gain", "Dip Gain"); addAndMakeVisible (mdG.get());
        hmF = std::make_unique<AttachedKnob> (a, "S2_EQ_HighMid_Freq", "HM Freq"); addAndMakeVisible (hmF.get());
        hmG = std::make_unique<AttachedKnob> (a, "S2_EQ_HighMid_Gain", "HM Gain"); addAndMakeVisible (hmG.get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 8;
        int h = area.getHeight() / 2;
        // Row 1: EQP-1A
        auto row1 = area.removeFromTop (h);
        onOff->setBounds (row1.removeFromLeft (kw).withHeight (30));
        lbF->setBounds (row1.removeFromLeft (kw));
        lbG->setBounds (row1.removeFromLeft (kw));
        laF->setBounds (row1.removeFromLeft (kw));
        laG->setBounds (row1.removeFromLeft (kw));
        hbF->setBounds (row1.removeFromLeft (kw));
        hbG->setBounds (row1.removeFromLeft (kw));
        haF->setBounds (row1.removeFromLeft (kw));
        // Row 2: MEQ-5
        auto row2 = area;
        haBW->setBounds (row2.removeFromLeft (kw));
        lmF->setBounds (row2.removeFromLeft (kw));
        lmG->setBounds (row2.removeFromLeft (kw));
        mdF->setBounds (row2.removeFromLeft (kw));
        mdG->setBounds (row2.removeFromLeft (kw));
        hmF->setBounds (row2.removeFromLeft (kw));
        hmG->setBounds (row2.removeFromLeft (kw));
    }
private:
    std::unique_ptr<AttachedToggle> onOff;
    std::unique_ptr<AttachedKnob> lbF,lbG,laF,laG,hbF,hbG,haF,haBW,lmF,lmG,mdF,mdG,hmF,hmG;
};

class CompPanel : public juce::Component
{
public:
    CompPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff = std::make_unique<AttachedToggle> (a, "S3_Comp_On", "ON"); addAndMakeVisible (onOff.get());
        model = std::make_unique<AttachedCombo> (a, "S3_Comp_Model", "Model"); addAndMakeVisible (model.get());
        thresh = std::make_unique<AttachedKnob> (a, "S3_Comp_Threshold", "Threshold"); addAndMakeVisible (thresh.get());
        ratio = std::make_unique<AttachedKnob> (a, "S3_Comp_Ratio", "Ratio"); addAndMakeVisible (ratio.get());
        attack = std::make_unique<AttachedKnob> (a, "S3_Comp_Attack", "Attack"); addAndMakeVisible (attack.get());
        release = std::make_unique<AttachedKnob> (a, "S3_Comp_Release", "Release"); addAndMakeVisible (release.get());
        autoRel = std::make_unique<AttachedToggle> (a, "S3_Comp_AutoRelease", "Auto Rel"); addAndMakeVisible (autoRel.get());
        makeup = std::make_unique<AttachedKnob> (a, "S3_Comp_Makeup", "Makeup"); addAndMakeVisible (makeup.get());
        mix = std::make_unique<AttachedKnob> (a, "S3_Comp_Mix", "Mix"); addAndMakeVisible (mix.get());
        scHp = std::make_unique<AttachedKnob> (a, "S3_Comp_SC_HP", "SC HP"); addAndMakeVisible (scHp.get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 10;
        onOff->setBounds (area.removeFromLeft (kw).withHeight (30));
        model->setBounds (area.removeFromLeft (kw).withHeight (40));
        thresh->setBounds (area.removeFromLeft (kw));
        ratio->setBounds (area.removeFromLeft (kw));
        attack->setBounds (area.removeFromLeft (kw));
        release->setBounds (area.removeFromLeft (kw));
        autoRel->setBounds (area.removeFromLeft (kw).withHeight (30));
        makeup->setBounds (area.removeFromLeft (kw));
        mix->setBounds (area.removeFromLeft (kw));
        scHp->setBounds (area.removeFromLeft (kw));
    }
private:
    std::unique_ptr<AttachedToggle> onOff, autoRel;
    std::unique_ptr<AttachedCombo> model;
    std::unique_ptr<AttachedKnob> thresh, ratio, attack, release, makeup, mix, scHp;
};

class SatPanel : public juce::Component
{
public:
    SatPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff = std::make_unique<AttachedToggle> (a, "S4_Sat_On", "ON"); addAndMakeVisible (onOff.get());
        mode = std::make_unique<AttachedCombo> (a, "S4_Sat_Mode", "Mode"); addAndMakeVisible (mode.get());
        type = std::make_unique<AttachedCombo> (a, "S4_Sat_Type", "Type"); addAndMakeVisible (type.get());
        drive = std::make_unique<AttachedKnob> (a, "S4_Sat_Drive", "Drive"); addAndMakeVisible (drive.get());
        output = std::make_unique<AttachedKnob> (a, "S4_Sat_Output", "Output"); addAndMakeVisible (output.get());
        blend = std::make_unique<AttachedKnob> (a, "S4_Sat_Blend", "Blend"); addAndMakeVisible (blend.get());
        bits = std::make_unique<AttachedKnob> (a, "S4_Sat_Bits", "Bits"); addAndMakeVisible (bits.get());
        xo1 = std::make_unique<AttachedKnob> (a, "S4_Sat_Xover1", "X1"); addAndMakeVisible (xo1.get());
        xo2 = std::make_unique<AttachedKnob> (a, "S4_Sat_Xover2", "X2"); addAndMakeVisible (xo2.get());
        xo3 = std::make_unique<AttachedKnob> (a, "S4_Sat_Xover3", "X3"); addAndMakeVisible (xo3.get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 10;
        onOff->setBounds (area.removeFromLeft (kw).withHeight (30));
        mode->setBounds (area.removeFromLeft (kw).withHeight (40));
        type->setBounds (area.removeFromLeft (kw).withHeight (40));
        drive->setBounds (area.removeFromLeft (kw));
        output->setBounds (area.removeFromLeft (kw));
        blend->setBounds (area.removeFromLeft (kw));
        bits->setBounds (area.removeFromLeft (kw));
        xo1->setBounds (area.removeFromLeft (kw));
        xo2->setBounds (area.removeFromLeft (kw));
        xo3->setBounds (area.removeFromLeft (kw));
    }
private:
    std::unique_ptr<AttachedToggle> onOff;
    std::unique_ptr<AttachedCombo> mode, type;
    std::unique_ptr<AttachedKnob> drive, output, blend, bits, xo1, xo2, xo3;
};

class OutEQPanel : public juce::Component
{
public:
    OutEQPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff = std::make_unique<AttachedToggle> (a, "S5_EQ2_On", "ON"); addAndMakeVisible (onOff.get());
        hsF = std::make_unique<AttachedKnob> (a, "S5_EQ2_HighShelf_Freq", "HS Freq"); addAndMakeVisible (hsF.get());
        hsG = std::make_unique<AttachedKnob> (a, "S5_EQ2_HighShelf_Gain", "HS Gain"); addAndMakeVisible (hsG.get());
        lsF = std::make_unique<AttachedKnob> (a, "S5_EQ2_LowShelf_Freq", "LS Freq"); addAndMakeVisible (lsF.get());
        lsG = std::make_unique<AttachedKnob> (a, "S5_EQ2_LowShelf_Gain", "LS Gain"); addAndMakeVisible (lsG.get());
        midF = std::make_unique<AttachedKnob> (a, "S5_EQ2_Mid_Freq", "Mid Freq"); addAndMakeVisible (midF.get());
        midG = std::make_unique<AttachedKnob> (a, "S5_EQ2_Mid_Gain", "Mid Gain"); addAndMakeVisible (midG.get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 7;
        onOff->setBounds (area.removeFromLeft (kw).withHeight (30));
        hsF->setBounds (area.removeFromLeft (kw));
        hsG->setBounds (area.removeFromLeft (kw));
        lsF->setBounds (area.removeFromLeft (kw));
        lsG->setBounds (area.removeFromLeft (kw));
        midF->setBounds (area.removeFromLeft (kw));
        midG->setBounds (area.removeFromLeft (kw));
    }
private:
    std::unique_ptr<AttachedToggle> onOff;
    std::unique_ptr<AttachedKnob> hsF, hsG, lsF, lsG, midF, midG;
};

class FilterPanel : public juce::Component
{
public:
    FilterPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff = std::make_unique<AttachedToggle> (a, "S6_Filter_On", "ON"); addAndMakeVisible (onOff.get());
        hpOn = std::make_unique<AttachedToggle> (a, "S6_HP_On", "HP"); addAndMakeVisible (hpOn.get());
        hpF = std::make_unique<AttachedKnob> (a, "S6_HP_Freq", "HP Freq"); addAndMakeVisible (hpF.get());
        hpS = std::make_unique<AttachedCombo> (a, "S6_HP_Slope", "HP Slope"); addAndMakeVisible (hpS.get());
        lpOn = std::make_unique<AttachedToggle> (a, "S6_LP_On", "LP"); addAndMakeVisible (lpOn.get());
        lpF = std::make_unique<AttachedKnob> (a, "S6_LP_Freq", "LP Freq"); addAndMakeVisible (lpF.get());
        lpS = std::make_unique<AttachedCombo> (a, "S6_LP_Slope", "LP Slope"); addAndMakeVisible (lpS.get());
        fMode = std::make_unique<AttachedCombo> (a, "S6_Filter_Mode", "Phase"); addAndMakeVisible (fMode.get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 8;
        onOff->setBounds (area.removeFromLeft (kw).withHeight (30));
        hpOn->setBounds (area.removeFromLeft (kw).withHeight (30));
        hpF->setBounds (area.removeFromLeft (kw));
        hpS->setBounds (area.removeFromLeft (kw).withHeight (40));
        lpOn->setBounds (area.removeFromLeft (kw).withHeight (30));
        lpF->setBounds (area.removeFromLeft (kw));
        lpS->setBounds (area.removeFromLeft (kw).withHeight (40));
        fMode->setBounds (area.removeFromLeft (kw).withHeight (40));
    }
private:
    std::unique_ptr<AttachedToggle> onOff, hpOn, lpOn;
    std::unique_ptr<AttachedKnob> hpF, lpF;
    std::unique_ptr<AttachedCombo> hpS, lpS, fMode;
};

class DynResPanel : public juce::Component
{
public:
    DynResPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff = std::make_unique<AttachedToggle> (a, "S6B_DynEQ_On", "ON"); addAndMakeVisible (onOff.get());
        depth = std::make_unique<AttachedKnob> (a, "S6B_DynEQ_Depth", "Depth"); addAndMakeVisible (depth.get());
        sens = std::make_unique<AttachedKnob> (a, "S6B_DynEQ_Sensitivity", "Sensitivity"); addAndMakeVisible (sens.get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 3;
        onOff->setBounds (area.removeFromLeft (kw).withHeight (30));
        depth->setBounds (area.removeFromLeft (kw));
        sens->setBounds (area.removeFromLeft (kw));
    }
private:
    std::unique_ptr<AttachedToggle> onOff;
    std::unique_ptr<AttachedKnob> depth, sens;
};

class ClipperPanel : public juce::Component
{
public:
    ClipperPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff = std::make_unique<AttachedToggle> (a, "S7_Clipper_On", "ON"); addAndMakeVisible (onOff.get());
        ceil = std::make_unique<AttachedKnob> (a, "S7_Clipper_Ceiling", "Ceiling"); addAndMakeVisible (ceil.get());
        style = std::make_unique<AttachedCombo> (a, "S7_Clipper_Style", "Style"); addAndMakeVisible (style.get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 3;
        onOff->setBounds (area.removeFromLeft (kw).withHeight (30));
        ceil->setBounds (area.removeFromLeft (kw));
        style->setBounds (area.removeFromLeft (kw).withHeight (40));
    }
private:
    std::unique_ptr<AttachedToggle> onOff;
    std::unique_ptr<AttachedKnob> ceil;
    std::unique_ptr<AttachedCombo> style;
};

class LimiterPanel : public juce::Component
{
public:
    LimiterPanel (juce::AudioProcessorValueTreeState& a)
    {
        onOff = std::make_unique<AttachedToggle> (a, "S7_Lim_On", "ON"); addAndMakeVisible (onOff.get());
        input = std::make_unique<AttachedKnob> (a, "S7_Lim_Input", "Input"); addAndMakeVisible (input.get());
        ceil = std::make_unique<AttachedKnob> (a, "S7_Lim_Ceiling", "Ceiling"); addAndMakeVisible (ceil.get());
        rel = std::make_unique<AttachedKnob> (a, "S7_Lim_Release", "Release"); addAndMakeVisible (rel.get());
        autoR = std::make_unique<AttachedToggle> (a, "S7_Lim_AutoRelease", "Auto Rel"); addAndMakeVisible (autoR.get());
        look = std::make_unique<AttachedKnob> (a, "S7_Lim_Lookahead", "Lookahead"); addAndMakeVisible (look.get());
        style = std::make_unique<AttachedCombo> (a, "S7_Lim_Style", "Style"); addAndMakeVisible (style.get());
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        int kw = area.getWidth() / 7;
        onOff->setBounds (area.removeFromLeft (kw).withHeight (30));
        input->setBounds (area.removeFromLeft (kw));
        ceil->setBounds (area.removeFromLeft (kw));
        rel->setBounds (area.removeFromLeft (kw));
        autoR->setBounds (area.removeFromLeft (kw).withHeight (30));
        look->setBounds (area.removeFromLeft (kw));
        style->setBounds (area.removeFromLeft (kw).withHeight (40));
    }
private:
    std::unique_ptr<AttachedToggle> onOff, autoR;
    std::unique_ptr<AttachedKnob> input, ceil, rel, look;
    std::unique_ptr<AttachedCombo> style;
};

// ═══════════════════════════════════════════════════════════════
//  GR METER COMPONENT
// ═══════════════════════════════════════════════════════════════

class GRMeter : public juce::Component
{
public:
    void setGR (float db) { grDb = db; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xFF111122));
        g.fillRoundedRectangle (area, 3.0f);
        float normalized = juce::jlimit (0.0f, 1.0f, -grDb / 20.0f);
        g.setColour (juce::Colour (0xFFE94560));
        g.fillRoundedRectangle (area.getX(), area.getY(), area.getWidth() * normalized, area.getHeight(), 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (10.0f));
        g.drawText ("GR: " + juce::String (grDb, 1) + " dB", area, juce::Justification::centred);
    }
private:
    float grDb = 0.0f;
};

// ═══════════════════════════════════════════════════════════════
//  MAIN EDITOR
// ═══════════════════════════════════════════════════════════════

EasyMasterEditor::EasyMasterEditor (EasyMasterProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setSize (1200, 700);
    setResizable (true, true);
    setResizeLimits (1000, 600, 1920, 1080);

    // ─── Top bar ──────────────────────────────────────────
    addAndMakeVisible (presetSelector);
    auto presets = processor.getPresetManager().getPresetList();
    for (int i = 0; i < presets.size(); ++i)
        presetSelector.addItem (presets[i], i + 1);
    presetSelector.setSelectedId (1);
    presetSelector.onChange = [this]
    {
        auto name = presetSelector.getText();
        if (name == "INIT") processor.getPresetManager().loadInit();
        else processor.getPresetManager().loadPreset (name);
    };

    addAndMakeVisible (savePresetButton);
    savePresetButton.onClick = [this]
    {
        auto name = presetSelector.getText();
        if (name.isNotEmpty() && name != "INIT")
            processor.getPresetManager().savePreset (name);
    };

    addAndMakeVisible (initButton);
    initButton.onClick = [this]
    { processor.getPresetManager().loadInit(); presetSelector.setSelectedId (1); };

    addAndMakeVisible (lufsLabel);
    lufsLabel.setFont (juce::Font (16.0f, juce::Font::bold));
    lufsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    lufsLabel.setJustificationType (juce::Justification::centredRight);
    lufsLabel.setText ("--.-- LUFS", juce::dontSendNotification);

    addAndMakeVisible (truePeakLabel);
    truePeakLabel.setFont (juce::Font (12.0f));
    truePeakLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFCCCCCC));
    truePeakLabel.setJustificationType (juce::Justification::centredRight);
    truePeakLabel.setText ("TP: --.-- dB", juce::dontSendNotification);

    // ─── Stage blocks ─────────────────────────────────────
    juce::StringArray names = { "INPUT", "PULTEC", "COMP", "SAT", "OUT EQ", "FILTER", "DYN RES", "CLIPPER", "LIMITER" };
    juce::Colour cols[] = {
        juce::Colour(0xFF2E86AB), juce::Colour(0xFF4ECDC4), juce::Colour(0xFFFF6B6B),
        juce::Colour(0xFFFFA07A), juce::Colour(0xFF95E1D3), juce::Colour(0xFF7B68EE),
        juce::Colour(0xFFDDA0DD), juce::Colour(0xFFFF4757), juce::Colour(0xFFE94560)
    };

    for (int i = 0; i < 9; ++i)
    {
        bool fixed = (i == 0 || i == 8);
        auto* block = new StageBlock (names[i], cols[i], i, fixed);
        block->onClick = [this] (int idx) { selectStage (idx); };
        block->onDrag = [this] (int idx, int mouseX) { handleDrag (idx, mouseX); };
        block->onDragEnd = [this] (int) { rebuildStageOrder(); };
        addAndMakeVisible (block);
        stageBlocks.add (block);
    }

    // ─── Stage panels ─────────────────────────────────────
    auto& a = processor.getAPVTS();
    stagePanels.add (new InputPanel (a));
    stagePanels.add (new PultecPanel (a));
    stagePanels.add (new CompPanel (a));
    stagePanels.add (new SatPanel (a));
    stagePanels.add (new OutEQPanel (a));
    stagePanels.add (new FilterPanel (a));
    stagePanels.add (new DynResPanel (a));
    stagePanels.add (new ClipperPanel (a));
    stagePanels.add (new LimiterPanel (a));

    for (auto* panel : stagePanels)
    {
        addChildComponent (panel);  // hidden initially
    }

    // ─── Bottom bar ───────────────────────────────────────
    addAndMakeVisible (masterOutputSlider);
    masterOutputSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterOutputSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    masterOutputSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFE94560));
    masterOutputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), "Master_Output_Gain", masterOutputSlider);

    grMeter = std::make_unique<GRMeter>();
    addAndMakeVisible (grMeter.get());

    // Select first stage
    selectStage (0);

    startTimerHz (30);
}

EasyMasterEditor::~EasyMasterEditor() { stopTimer(); }

void EasyMasterEditor::selectStage (int index)
{
    selectedStage = index;
    for (int i = 0; i < stageBlocks.size(); ++i)
        stageBlocks[i]->selected = (i == index);
    for (int i = 0; i < stagePanels.size(); ++i)
        stagePanels[i]->setVisible (i == index);
    repaint();
    resized();
}

void EasyMasterEditor::handleDrag (int stageIdx, int mouseX)
{
    if (stageIdx == 0 || stageIdx == 8) return; // fixed
    // Find which position we're dragging over
    for (int i = 1; i < 8; ++i)
    {
        if (i == stageIdx) continue;
        auto bounds = stageBlocks[i]->getBounds();
        if (mouseX > bounds.getX() && mouseX < bounds.getRight())
        {
            // Swap blocks visually
            stageBlocks.swap (stageIdx, i);
            stageBlocks[stageIdx]->stageIndex = stageIdx;
            stageBlocks[i]->stageIndex = i;
            // Update engine
            processor.getEngine().swapStages (stageIdx - 1, i - 1);
            resized();
            break;
        }
    }
}

void EasyMasterEditor::rebuildStageOrder()
{
    resized();
    repaint();
}

void EasyMasterEditor::paint (juce::Graphics& g)
{
    // Background
    g.fillAll (juce::Colour (0xFF1A1A2E));

    // Top bar
    g.setColour (juce::Colour (0xFF16213E));
    g.fillRect (0, 0, getWidth(), 50);

    // Title
    g.setColour (juce::Colour (0xFFE94560));
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("EASY MASTER", 12, 8, 180, 28, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xFF888888));
    g.setFont (juce::Font (9.0f));
    g.drawText ("by Phonica School", 12, 34, 140, 12, juce::Justification::centredLeft);

    // Bottom bar
    g.setColour (juce::Colour (0xFF16213E));
    g.fillRect (0, getHeight() - 70, getWidth(), 70);

    // Panel background
    auto panelArea = getLocalBounds().withTop (130).withBottom (getHeight() - 70);
    g.setColour (juce::Colour (0xFF151530));
    g.fillRoundedRectangle (panelArea.toFloat().reduced (8), 8.0f);

    // Stage label
    if (selectedStage >= 0 && selectedStage < stageBlocks.size())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::Font (13.0f));
        g.drawText ("Stage: " + stageBlocks[selectedStage]->getStageName(),
                     panelArea.getX() + 16, panelArea.getY() + 4, 200, 20,
                     juce::Justification::centredLeft);
    }

    // Bottom labels
    g.setColour (juce::Colour (0xFFAAAAAA));
    g.setFont (juce::Font (10.0f));
    g.drawText ("MASTER OUTPUT", getWidth() - 110, getHeight() - 68, 100, 14, juce::Justification::centred);
    g.drawText ("GAIN REDUCTION", 10, getHeight() - 68, 200, 14, juce::Justification::centredLeft);
}

void EasyMasterEditor::resized()
{
    auto area = getLocalBounds();

    // Top bar
    auto topBar = area.removeFromTop (50);
    topBar.removeFromLeft (170);
    presetSelector.setBounds (topBar.removeFromLeft (180).reduced (8));
    savePresetButton.setBounds (topBar.removeFromLeft (55).reduced (8));
    initButton.setBounds (topBar.removeFromLeft (55).reduced (8));
    lufsLabel.setBounds (topBar.removeFromRight (130).reduced (4));
    truePeakLabel.setBounds (topBar.removeFromRight (120).reduced (4));

    // Stage blocks strip
    auto stripArea = area.removeFromTop (80).reduced (8, 4);
    int blockW = stripArea.getWidth() / 9;
    for (int i = 0; i < stageBlocks.size(); ++i)
        stageBlocks[i]->setBounds (stripArea.getX() + i * blockW, stripArea.getY(), blockW, stripArea.getHeight());

    // Bottom bar
    auto bottomBar = area.removeFromBottom (70);
    masterOutputSlider.setBounds (bottomBar.removeFromRight (100).reduced (4, 2));
    grMeter->setBounds (bottomBar.withTrimmedLeft (10).withTrimmedRight (10).withSizeKeepingCentre (bottomBar.getWidth() - 130, 22));

    // Panel area
    auto panelArea = area.reduced (8).withTrimmedTop (4);
    for (auto* panel : stagePanels)
        panel->setBounds (panelArea);
}

void EasyMasterEditor::timerCallback()
{
    float lufs = processor.getEngine().getLUFS();
    float tp = processor.getEngine().getTruePeak();
    lufsLabel.setText (lufs > -100.f ? juce::String (lufs, 1) + " LUFS" : "--.-- LUFS", juce::dontSendNotification);
    truePeakLabel.setText (tp > -100.f ? "TP: " + juce::String (tp, 1) + " dB" : "TP: --.-- dB", juce::dontSendNotification);

    // Update GR meter from limiter
    auto* limiter = processor.getEngine().getStage (ProcessingStage::StageID::Limiter);
    if (limiter)
        grMeter->setGR (limiter->getMeterData().gainReduction.load());
}
