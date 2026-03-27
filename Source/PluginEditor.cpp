#include "PluginProcessor.h"

// ═══════════════════════════════════════════════════════════════
//  EASY MASTER EDITOR — Minimal working version
//  No fancy drag & drop, just working controls that don't crash
// ═══════════════════════════════════════════════════════════════

EasyMasterEditor::EasyMasterEditor (EasyMasterProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setSize (1200, 700);
    setResizable (true, true);
    setResizeLimits (900, 500, 1920, 1080);

    // ─── Preset bar ───────────────────────────────────────
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
    { auto n = presetSelector.getText(); if (n.isNotEmpty() && n != "INIT") processor.getPresetManager().savePreset (n); };

    addAndMakeVisible (initButton);
    initButton.onClick = [this]
    { processor.getPresetManager().loadInit(); presetSelector.setSelectedId (1); };

    addAndMakeVisible (lufsLabel);
    lufsLabel.setFont (juce::Font (16.0f, juce::Font::bold));
    lufsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    lufsLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (truePeakLabel);
    truePeakLabel.setFont (juce::Font (12.0f));
    truePeakLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFCCCCCC));
    truePeakLabel.setJustificationType (juce::Justification::centredRight);

    // ─── Stage selector tabs ──────────────────────────────
    juce::StringArray stageNames = { "INPUT", "PULTEC", "COMP", "SAT", "OUT EQ", "FILTER", "DYN RES", "CLIPPER", "LIMITER" };
    for (int i = 0; i < stageNames.size(); ++i)
    {
        auto* btn = new juce::TextButton (stageNames[i]);
        btn->setRadioGroupId (1001);
        btn->setClickingTogglesState (true);
        btn->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFE94560));
        btn->onClick = [this, i] { showStage (i); };
        addAndMakeVisible (btn);
        tabButtons.add (btn);
    }
    tabButtons[0]->setToggleState (true, juce::dontSendNotification);

    // ─── Create all sliders + attachments ─────────────────
    auto& apvts = processor.getAPVTS();

    // Helper lambdas
    auto addKnob = [&] (const juce::String& paramID, const juce::String& label, int stage) -> juce::Slider*
    {
        auto* s = new juce::Slider (paramID);
        s->setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        s->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFE94560));
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s->setVisible (false);
        addAndMakeVisible (s);
        allSliders.add (s);

        auto* lbl = new juce::Label ({}, label);
        lbl->setFont (juce::Font (10.0f));
        lbl->setColour (juce::Label::textColourId, juce::Colour (0xFFAAAAAA));
        lbl->setJustificationType (juce::Justification::centred);
        lbl->setVisible (false);
        addAndMakeVisible (lbl);
        allLabels.add (lbl);

        auto* att = new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, paramID, *s);
        allAttachments.add (att);

        stageForControl.add (stage);
        return s;
    };

    auto addCombo = [&] (const juce::String& paramID, const juce::String& label, int stage) -> juce::ComboBox*
    {
        auto* c = new juce::ComboBox (paramID);
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
            for (int i = 0; i < param->choices.size(); ++i)
                c->addItem (param->choices[i], i + 1);
        c->setVisible (false);
        addAndMakeVisible (c);
        allCombos.add (c);

        auto* lbl = new juce::Label ({}, label);
        lbl->setFont (juce::Font (10.0f));
        lbl->setColour (juce::Label::textColourId, juce::Colour (0xFFAAAAAA));
        lbl->setJustificationType (juce::Justification::centred);
        lbl->setVisible (false);
        addAndMakeVisible (lbl);
        comboLabels.add (lbl);

        auto* att = new juce::AudioProcessorValueTreeState::ComboBoxAttachment (apvts, paramID, *c);
        comboAttachments.add (att);

        comboStage.add (stage);
        return c;
    };

    // ─── STAGE 0: INPUT ───────────────────────────────────
    addKnob ("S1_Input_Gain", "Gain", 0);
    addKnob ("S1_Input_Crossover", "Crossover", 0);
    addKnob ("S1_Input_Low_Width", "Low Width", 0);
    addKnob ("S1_Input_High_Width", "High Width", 0);
    addKnob ("S1_Input_Mid_Gain", "Mid", 0);
    addKnob ("S1_Input_Side_Gain", "Side", 0);
    addCombo ("S1_Input_Crossover_Mode", "Phase", 0);

    // ─── STAGE 1: PULTEC EQ ──────────────────────────────
    addKnob ("S2_EQ_LowBoost_Freq", "LB Freq", 1);
    addKnob ("S2_EQ_LowBoost_Gain", "LB Gain", 1);
    addKnob ("S2_EQ_LowAtten_Freq", "LA Freq", 1);
    addKnob ("S2_EQ_LowAtten_Gain", "LA Gain", 1);
    addKnob ("S2_EQ_HighBoost_Freq", "HB Freq", 1);
    addKnob ("S2_EQ_HighBoost_Gain", "HB Gain", 1);
    addKnob ("S2_EQ_HighAtten_Freq", "HA Freq", 1);
    addKnob ("S2_EQ_HighAtten_BW", "HA BW", 1);
    addKnob ("S2_EQ_LowMid_Freq", "LM Freq", 1);
    addKnob ("S2_EQ_LowMid_Gain", "LM Gain", 1);
    addKnob ("S2_EQ_MidDip_Freq", "Dip Freq", 1);
    addKnob ("S2_EQ_MidDip_Gain", "Dip Gain", 1);
    addKnob ("S2_EQ_HighMid_Freq", "HM Freq", 1);
    addKnob ("S2_EQ_HighMid_Gain", "HM Gain", 1);

    // ─── STAGE 2: COMPRESSOR ─────────────────────────────
    addCombo ("S3_Comp_Model", "Model", 2);
    addKnob ("S3_Comp_Threshold", "Threshold", 2);
    addKnob ("S3_Comp_Ratio", "Ratio", 2);
    addKnob ("S3_Comp_Attack", "Attack", 2);
    addKnob ("S3_Comp_Release", "Release", 2);
    addKnob ("S3_Comp_Makeup", "Makeup", 2);
    addKnob ("S3_Comp_Mix", "Mix", 2);
    addKnob ("S3_Comp_SC_HP", "SC HP", 2);

    // ─── STAGE 3: SATURATION ─────────────────────────────
    addCombo ("S4_Sat_Mode", "Mode", 3);
    addCombo ("S4_Sat_Type", "Type", 3);
    addKnob ("S4_Sat_Drive", "Drive", 3);
    addKnob ("S4_Sat_Output", "Output", 3);
    addKnob ("S4_Sat_Blend", "Blend", 3);
    addKnob ("S4_Sat_Bits", "Bits", 3);

    // ─── STAGE 4: OUTPUT EQ ──────────────────────────────
    addKnob ("S5_EQ2_HighShelf_Freq", "HS Freq", 4);
    addKnob ("S5_EQ2_HighShelf_Gain", "HS Gain", 4);
    addKnob ("S5_EQ2_LowShelf_Freq", "LS Freq", 4);
    addKnob ("S5_EQ2_LowShelf_Gain", "LS Gain", 4);
    addKnob ("S5_EQ2_Mid_Freq", "Mid Freq", 4);
    addKnob ("S5_EQ2_Mid_Gain", "Mid Gain", 4);

    // ─── STAGE 5: FILTER ─────────────────────────────────
    addKnob ("S6_HP_Freq", "HP Freq", 5);
    addCombo ("S6_HP_Slope", "HP Slope", 5);
    addKnob ("S6_LP_Freq", "LP Freq", 5);
    addCombo ("S6_LP_Slope", "LP Slope", 5);
    addCombo ("S6_Filter_Mode", "Phase", 5);

    // ─── STAGE 6: DYNAMIC RESONANCE ─────────────────────
    addKnob ("S6B_DynEQ_Depth", "Depth", 6);
    addKnob ("S6B_DynEQ_Sensitivity", "Sensitivity", 6);

    // ─── STAGE 7: CLIPPER ────────────────────────────────
    addKnob ("S7_Clipper_Ceiling", "Ceiling", 7);
    addCombo ("S7_Clipper_Style", "Style", 7);

    // ─── STAGE 8: LIMITER ────────────────────────────────
    addKnob ("S7_Lim_Input", "Input", 8);
    addKnob ("S7_Lim_Ceiling", "Ceiling", 8);
    addKnob ("S7_Lim_Release", "Release", 8);
    addKnob ("S7_Lim_Lookahead", "Lookahead", 8);
    addCombo ("S7_Lim_Style", "Style", 8);

    // ─── Master output ───────────────────────────────────
    addAndMakeVisible (masterOutputSlider);
    masterOutputSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterOutputSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    masterOutputSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFE94560));
    masterOutputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "Master_Output_Gain", masterOutputSlider);

    // ─── Oversampling ────────────────────────────────────
    addAndMakeVisible (oversamplingCombo);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("Oversampling")))
        for (int i = 0; i < p->choices.size(); ++i)
            oversamplingCombo.addItem (p->choices[i], i + 1);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "Oversampling", oversamplingCombo);

    // Show first stage
    showStage (0);
    startTimerHz (30);
}

EasyMasterEditor::~EasyMasterEditor() { stopTimer(); }

void EasyMasterEditor::showStage (int stage)
{
    currentStage = stage;

    for (int i = 0; i < allSliders.size(); ++i)
    {
        bool show = (stageForControl[i] == stage);
        allSliders[i]->setVisible (show);
        allLabels[i]->setVisible (show);
    }
    for (int i = 0; i < allCombos.size(); ++i)
    {
        bool show = (comboStage[i] == stage);
        allCombos[i]->setVisible (show);
        comboLabels[i]->setVisible (show);
    }
    resized();
}

void EasyMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF1A1A2E));

    // Top bar
    g.setColour (juce::Colour (0xFF16213E));
    g.fillRect (0, 0, getWidth(), 50);

    g.setColour (juce::Colour (0xFFE94560));
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("EASY MASTER", 12, 8, 180, 28, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xFF888888));
    g.setFont (juce::Font (9.0f));
    g.drawText ("by Phonica School", 12, 34, 140, 12, juce::Justification::centredLeft);

    // Bottom bar
    g.setColour (juce::Colour (0xFF16213E));
    g.fillRect (0, getHeight() - 70, getWidth(), 70);

    // Panel area
    auto panelArea = getLocalBounds().withTop (95).withBottom (getHeight() - 70).reduced (8).toFloat();
    g.setColour (juce::Colour (0xFF151530));
    g.fillRoundedRectangle (panelArea, 8.0f);

    // GR meter
    if (tabButtons.size() > 0)
    {
        auto* limiter = processor.getEngine().getStage (ProcessingStage::StageID::Limiter);
        if (limiter)
        {
        float gr = limiter->getMeterData().gainReduction.load();
        float normalized = juce::jlimit (0.0f, 1.0f, -gr / 20.0f);
        auto grArea = juce::Rectangle<float> (10.0f, (float)getHeight() - 45.0f, (float)getWidth() - 230.0f, 18.0f);
        g.setColour (juce::Colour (0xFF111122));
        g.fillRoundedRectangle (grArea, 3.0f);
        g.setColour (juce::Colour (0xFFE94560));
        g.fillRoundedRectangle (grArea.getX(), grArea.getY(), grArea.getWidth() * normalized, grArea.getHeight(), 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (10.0f));
        g.drawText ("GR: " + juce::String (gr, 1) + " dB", grArea, juce::Justification::centred);
        }
    }

    // Labels
    g.setColour (juce::Colour (0xFFAAAAAA));
    g.setFont (juce::Font (10.0f));
    g.drawText ("MASTER", (float)getWidth() - 110.0f, (float)getHeight() - 68.0f, 100.0f, 14.0f, juce::Justification::centred);
    g.drawText ("OS", (float)getWidth() - 210.0f, (float)getHeight() - 68.0f, 80.0f, 14.0f, juce::Justification::centred);
}

void EasyMasterEditor::resized()
{
    // Guard: don't layout until constructor is done
    if (tabButtons.size() == 0)
        return;

    auto area = getLocalBounds();

    // Top bar
    auto topBar = area.removeFromTop (50);
    topBar.removeFromLeft (170);
    presetSelector.setBounds (topBar.removeFromLeft (180).reduced (8));
    savePresetButton.setBounds (topBar.removeFromLeft (55).reduced (8));
    initButton.setBounds (topBar.removeFromLeft (55).reduced (8));
    lufsLabel.setBounds (topBar.removeFromRight (130).reduced (4));
    truePeakLabel.setBounds (topBar.removeFromRight (120).reduced (4));

    // Tab strip
    auto tabStrip = area.removeFromTop (40).reduced (8, 4);
    int tabW = tabStrip.getWidth() / tabButtons.size();
    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setBounds (tabStrip.getX() + i * tabW, tabStrip.getY(), tabW - 2, tabStrip.getHeight());

    // Bottom bar
    auto bottomBar = area.removeFromBottom (70);
    masterOutputSlider.setBounds (bottomBar.removeFromRight (100).reduced (4, 2));
    oversamplingCombo.setBounds (bottomBar.removeFromRight (80).reduced (4, 18));

    // Panel area — layout visible knobs in a grid
    auto panelArea = area.reduced (16, 8);

    // Count visible sliders for this stage
    int visibleKnobs = 0;
    for (int i = 0; i < allSliders.size(); ++i)
        if (stageForControl[i] == currentStage) visibleKnobs++;

    int visibleCombos = 0;
    for (int i = 0; i < allCombos.size(); ++i)
        if (comboStage[i] == currentStage) visibleCombos++;

    int totalControls = visibleKnobs + visibleCombos;
    if (totalControls == 0) return;

    int cols = juce::jmin (totalControls, 8);
    int rows = (totalControls + cols - 1) / cols;
    int cellW = panelArea.getWidth() / cols;
    int cellH = panelArea.getHeight() / juce::jmax (rows, 1);
    int knobH = juce::jmin (cellH - 20, 100);

    int col = 0, row = 0;

    // Layout knobs
    for (int i = 0; i < allSliders.size(); ++i)
    {
        if (stageForControl[i] != currentStage) continue;
        int x = panelArea.getX() + col * cellW;
        int y = panelArea.getY() + row * cellH;
        allLabels[i]->setBounds (x, y, cellW, 16);
        allSliders[i]->setBounds (x + 4, y + 16, cellW - 8, knobH);
        col++;
        if (col >= cols) { col = 0; row++; }
    }

    // Layout combos
    for (int i = 0; i < allCombos.size(); ++i)
    {
        if (comboStage[i] != currentStage) continue;
        int x = panelArea.getX() + col * cellW;
        int y = panelArea.getY() + row * cellH;
        comboLabels[i]->setBounds (x, y, cellW, 16);
        allCombos[i]->setBounds (x + 4, y + 20, cellW - 8, 28);
        col++;
        if (col >= cols) { col = 0; row++; }
    }
}

void EasyMasterEditor::timerCallback()
{
    float lufs = processor.getEngine().getLUFS();
    float tp = processor.getEngine().getTruePeak();
    lufsLabel.setText (lufs > -100.f ? juce::String (lufs, 1) + " LUFS" : "--.-- LUFS", juce::dontSendNotification);
    truePeakLabel.setText (tp > -100.f ? "TP: " + juce::String (tp, 1) + " dB" : "TP: --.-- dB", juce::dontSendNotification);
    repaint();  // for GR meter
}
