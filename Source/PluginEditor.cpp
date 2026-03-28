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

    // Set onChange FIRST, then set ID with dontSendNotification
    // This prevents loadInit() being called when the editor reopens
    presetSelector.onChange = [this]
    {
        auto name = presetSelector.getText();
        if (name == "INIT") processor.getPresetManager().loadInit();
        else processor.getPresetManager().loadPreset (name);
    };
    presetSelector.setSelectedId (1, juce::dontSendNotification);

    addAndMakeVisible (savePresetButton);
    savePresetButton.onClick = [this]
    {
        // Show Save As dialog
        auto dlg = std::make_shared<juce::AlertWindow> ("Save Preset", "Enter a name for your preset:", juce::MessageBoxIconType::NoIcon);
        dlg->addTextEditor ("name", presetSelector.getText() == "INIT" ? "" : presetSelector.getText(), "Preset Name");
        dlg->addButton ("Save", 1);
        dlg->addButton ("Cancel", 0);
        dlg->enterModalState (true, juce::ModalCallbackFunction::create ([this, dlg] (int result)
        {
            if (result == 1)
            {
                auto name = dlg->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                {
                    processor.getPresetManager().savePreset (name);
                    // Refresh preset list
                    presetSelector.clear (juce::dontSendNotification);
                    auto presets = processor.getPresetManager().getPresetList();
                    for (int i = 0; i < presets.size(); ++i)
                        presetSelector.addItem (presets[i], i + 1);
                    // Select the saved preset
                    for (int i = 0; i < presets.size(); ++i)
                    {
                        if (presets[i] == name)
                        { presetSelector.setSelectedId (i + 1, juce::dontSendNotification); break; }
                    }
                }
            }
        }), true);
    };

    addAndMakeVisible (initButton);
    initButton.onClick = [this]
    { processor.getPresetManager().loadInit(); presetSelector.setSelectedId (1, juce::dontSendNotification); };

    // ─── Global Bypass + Auto Match ─────────────────────
    addAndMakeVisible (globalBypassButton);
    globalBypassButton.setClickingTogglesState (true);
    globalBypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFFF4444));
    globalBypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), "Global_Bypass", globalBypassButton);

    addAndMakeVisible (autoMatchButton);
    autoMatchButton.setClickingTogglesState (true);
    autoMatchButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF44AA44));
    autoMatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), "Auto_Match", autoMatchButton);

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

    // ─── Reorder buttons ────────────────────────────────
    addAndMakeVisible (moveLeftBtn);
    moveLeftBtn.onClick = [this]
    {
        // Find current tab's position in reorderable range (1-7)
        int tabIdx = -1;
        for (int i = 0; i < tabButtons.size(); ++i)
            if (tabButtons[i]->getToggleState()) { tabIdx = i; break; }
        if (tabIdx < 2 || tabIdx > 7) return;  // Can't move INPUT, LIMITER, or first reorderable left
        int posA = tabIdx - 1;  // engine position (0-based in reorderable)
        int posB = posA - 1;
        processor.getEngine().swapStages (posA, posB);
        refreshTabLabels();
        // Stay on the same stage type
        showStage (tabIdx - 1);
        tabButtons[tabIdx - 1]->setToggleState (true, juce::dontSendNotification);
    };

    addAndMakeVisible (moveRightBtn);
    moveRightBtn.onClick = [this]
    {
        int tabIdx = -1;
        for (int i = 0; i < tabButtons.size(); ++i)
            if (tabButtons[i]->getToggleState()) { tabIdx = i; break; }
        if (tabIdx < 1 || tabIdx > 6) return;  // Can't move LIMITER or last reorderable right
        int posA = tabIdx - 1;
        int posB = posA + 1;
        processor.getEngine().swapStages (posA, posB);
        refreshTabLabels();
        showStage (tabIdx + 1);
        tabButtons[tabIdx + 1]->setToggleState (true, juce::dontSendNotification);
    };

    // ─── Per-stage bypass toggles (skip INPUT = index 0) ──
    juce::StringArray bypassParamIDs = {
        "", "S2_EQ_On", "S3_Comp_On", "S4_Sat_On",
        "S5_EQ2_On", "S6_Filter_On", "S6B_DynEQ_On", "S7_Clipper_On", "S7_Lim_On"
    };
    for (int i = 0; i < bypassParamIDs.size(); ++i)
    {
        auto* tog = new juce::ToggleButton ("ON");
        tog->setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFF44FF44));
        tog->setVisible (false);
        addAndMakeVisible (tog);
        stageBypassToggles.add (tog);

        if (bypassParamIDs[i].isNotEmpty())
        {
            auto* att = new juce::AudioProcessorValueTreeState::ButtonAttachment (
                processor.getAPVTS(), bypassParamIDs[i], *tog);
            bypassAttachments.add (att);
        }
    }

    // ─── Compressor auto-release toggle (via inline toggles) ──

    refreshTabLabels();

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

    auto addToggle = [&] (const juce::String& paramID, const juce::String& label, int stage)
    {
        auto* tog = new juce::ToggleButton (label);
        tog->setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFF44CC44));
        tog->setVisible (false);
        addAndMakeVisible (tog);
        inlineToggles.add (tog);

        auto* att = new juce::AudioProcessorValueTreeState::ButtonAttachment (apvts, paramID, *tog);
        inlineToggleAttachments.add (att);

        toggleStage.add (stage);
    };

    // ─── STAGE 0: INPUT ───────────────────────────────────
    addKnob ("S1_Input_Gain", "Gain", 0);
    addKnob ("S1_Input_Crossover", "Crossover", 999);    // hidden — widener moved to Imager
    addKnob ("S1_Input_Low_Width", "Low Width", 999);     // hidden
    addKnob ("S1_Input_High_Width", "High Width", 999);   // hidden
    addKnob ("S1_Input_Mid_Gain", "Mid", 0);
    addKnob ("S1_Input_Side_Gain", "Side", 0);
    addCombo ("S1_Input_Crossover_Mode", "Phase", 999);   // hidden

    // ─── STAGE 1: PULTEC EQ ──────────────────────────────
    // EQP-1A LOW: freq selector + boost + atten
    addCombo ("S2_EQ_LowBoost_Freq", "Low Freq", 1);
    addKnob ("S2_EQ_LowBoost_Gain", "Low Boost", 1);
    addKnob ("S2_EQ_LowAtten_Gain", "Low Atten", 1);
    // EQP-1A HIGH: freq selector + boost + atten + atten sel + bandwidth
    addCombo ("S2_EQ_HighBoost_Freq", "High Freq", 1);
    addKnob ("S2_EQ_HighBoost_Gain", "Hi Boost", 1);
    addKnob ("S2_EQ_HighAtten_Gain", "Hi Atten", 1);
    addCombo ("S2_EQ_HighAtten_Freq", "Atten Sel", 1);
    addKnob ("S2_EQ_HighAtten_BW", "Bandwidth", 1);
    // MEQ-5: 3 mid bands
    addKnob ("S2_EQ_LowMid_Freq", "LM Freq", 1);
    addKnob ("S2_EQ_LowMid_Gain", "LM Peak", 1);
    addKnob ("S2_EQ_MidDip_Freq", "Dip Freq", 1);
    addKnob ("S2_EQ_MidDip_Gain", "Dip", 1);
    addKnob ("S2_EQ_HighMid_Freq", "HM Freq", 1);
    addKnob ("S2_EQ_HighMid_Gain", "HM Peak", 1);

    // ─── STAGE 2: COMPRESSOR ─────────────────────────────
    addCombo ("S3_Comp_Model", "Model", 2);
    addKnob ("S3_Comp_Threshold", "Threshold", 2);
    addKnob ("S3_Comp_Ratio", "Ratio", 2);
    addKnob ("S3_Comp_Attack", "Attack", 2);
    addKnob ("S3_Comp_Release", "Release", 2);
    addToggle ("S3_Comp_AutoRelease", "Auto Release", 2);
    addKnob ("S3_Comp_Makeup", "Makeup", 2);
    addKnob ("S3_Comp_Mix", "Mix", 2);
    addKnob ("S3_Comp_SC_HP", "SC HP", 2);

    // ─── STAGE 3: SATURATION ─────────────────────────────
    addCombo ("S4_Sat_Mode", "Mode", kSatCommon);
    addCombo ("S4_Sat_Type", "Type", kSatSingle);
    addKnob ("S4_Sat_Drive", "Drive", kSatSingle);
    addKnob ("S4_Sat_Output", "Output", kSatSingle);
    addKnob ("S4_Sat_Blend", "Blend", kSatSingle);
    addKnob ("S4_Sat_Xover1", "X1 Freq", kSatMulti);
    addKnob ("S4_Sat_Xover2", "X2 Freq", kSatMulti);
    addKnob ("S4_Sat_Xover3", "X3 Freq", kSatMulti);
    addCombo ("S4_Sat_Xover_Mode", "XOver Phase", kSatMulti);
    // Per-band controls
    for (int b = 1; b <= 4; ++b)
    {
        auto p = "S4_Sat_B" + juce::String(b) + "_";
        auto lb = "B" + juce::String(b) + " ";
        addCombo (p + "Type", lb + "Type", kSatMulti);
        addKnob (p + "Drive", lb + "Drv", kSatMulti);
        addKnob (p + "Output", lb + "Out", kSatMulti);
        addKnob (p + "Blend", lb + "Bld", kSatMulti);
        addToggle (p + "Solo", lb + "Solo", kSatMulti);
        addToggle (p + "Mute", lb + "Mute", kSatMulti);
    }

    // ─── STAGE 4: OUTPUT EQ (5-band FabFilter style) ─────
    addKnob ("S5_EQ2_LowShelf_Freq", "LS Freq", 4);
    addKnob ("S5_EQ2_LowShelf_Gain", "LS Gain", 4);
    addKnob ("S5_EQ2_LowShelf_Q", "LS Q", 4);
    addKnob ("S5_EQ2_LowMid_Freq", "LM Freq", 4);
    addKnob ("S5_EQ2_LowMid_Gain", "LM Gain", 4);
    addKnob ("S5_EQ2_LowMid_Q", "LM Q", 4);
    addKnob ("S5_EQ2_Mid_Freq", "Mid Freq", 4);
    addKnob ("S5_EQ2_Mid_Gain", "Mid Gain", 4);
    addKnob ("S5_EQ2_Mid_Q", "Mid Q", 4);
    addKnob ("S5_EQ2_HighMid_Freq", "HM Freq", 4);
    addKnob ("S5_EQ2_HighMid_Gain", "HM Gain", 4);
    addKnob ("S5_EQ2_HighMid_Q", "HM Q", 4);
    addKnob ("S5_EQ2_HighShelf_Freq", "HS Freq", 4);
    addKnob ("S5_EQ2_HighShelf_Gain", "HS Gain", 4);
    addKnob ("S5_EQ2_HighShelf_Q", "HS Q", 4);

    // ─── STAGE 5: FILTER ─────────────────────────────────
    addToggle ("S6_HP_On", "HP On", 5);
    addKnob ("S6_HP_Freq", "HP Freq", 5);
    addCombo ("S6_HP_Slope", "HP Slope", 5);
    addToggle ("S6_LP_On", "LP On", 5);
    addKnob ("S6_LP_Freq", "LP Freq", 5);
    addCombo ("S6_LP_Slope", "LP Slope", 5);
    addCombo ("S6_Filter_Mode", "Phase", 5);

    // ─── STAGE 6: DYNAMIC RESONANCE ─────────────────────
    addKnob ("S6B_DynEQ_Depth", "Depth", 6);
    addKnob ("S6B_DynEQ_Sensitivity", "Selectivity", 6);
    addKnob ("S6B_DynEQ_Sharpness", "Sharpness", 6);
    addKnob ("S6B_DynEQ_Speed", "Speed", 6);
    addKnob ("S6B_DynEQ_LowFreq", "Low Freq", 6);
    addKnob ("S6B_DynEQ_HighFreq", "High Freq", 6);

    // ─── STAGE 7: CLIPPER ────────────────────────────────
    addKnob ("S7_Clipper_Ceiling", "Ceiling", 7);
    addCombo ("S7_Clipper_Style", "Style", 7);

    // ─── STAGE 8: LIMITER ────────────────────────────────
    addKnob ("S7_Lim_Input", "Input", 8);
    addKnob ("S7_Lim_Ceiling", "Ceiling", 8);
    addKnob ("S7_Lim_Release", "Release", 8);
    addToggle ("S7_Lim_AutoRelease", "Auto Release", 8);
    addKnob ("S7_Lim_Lookahead", "Lookahead", 8);
    addCombo ("S7_Lim_Style", "Style", 8);
    // Imager width knobs (visible on LIMITER tab, laid out in Imager section)
    addKnob ("IMG_B1_Width", "Low W", kImager);
    addKnob ("IMG_B2_Width", "L-M W", kImager);
    addKnob ("IMG_B3_Width", "H-M W", kImager);
    addKnob ("IMG_B4_Width", "High W", kImager);
    // Imager crossover knobs (hidden — controlled by drag on Imager display)
    addKnob ("IMG_Xover1", "IX1", 999);
    addKnob ("IMG_Xover2", "IX2", 999);
    addKnob ("IMG_Xover3", "IX3", 999);

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

void EasyMasterEditor::showStage (int tabIndex)
{
    // Map tab index to stage type using current order
    int stageType = stageTypeForTab[(size_t)tabIndex];
    currentStage = stageType;

    // Determine which sub-stages to show for SAT
    bool isSat = (stageType == kSatCommon);
    int satMode = 0;
    if (isSat)
        satMode = (int) processor.getAPVTS().getRawParameterValue ("S4_Sat_Mode")->load();

    auto shouldShow = [&](int controlStage) -> bool
    {
        if (controlStage == stageType) return true;
        if (isSat)
        {
            if (controlStage == kSatCommon) return true;
            if (controlStage == kSatSingle && satMode == 0) return true;
            if (controlStage == kSatMulti  && satMode == 1) return true;
        }
        // Show Imager width knobs on LIMITER tab
        if (stageType == 8 && controlStage == kImager) return true;
        return false;
    };

    for (int i = 0; i < allSliders.size(); ++i)
    {
        bool show = shouldShow (stageForControl[i]);
        allSliders[i]->setVisible (show);
        allLabels[i]->setVisible (show);
    }
    for (int i = 0; i < allCombos.size(); ++i)
    {
        bool show = shouldShow (comboStage[i]);
        allCombos[i]->setVisible (show);
        comboLabels[i]->setVisible (show);
    }

    // Show/hide per-stage bypass toggles (skip INPUT = stage 0)
    for (int i = 0; i < stageBypassToggles.size(); ++i)
    {
        // For SAT, bypass toggle is index 3
        if (isSat)
            stageBypassToggles[i]->setVisible (i == kSatCommon);
        else
            stageBypassToggles[i]->setVisible (i == stageType && stageType != 0);
    }

    // Show/hide inline toggles
    for (int i = 0; i < inlineToggles.size(); ++i)
    {
        bool show = shouldShow (toggleStage[i]);
        inlineToggles[i]->setVisible (show);
    }

    // Show/hide reorder buttons (only for reorderable stages, tabs 1-7)
    bool canReorder = (tabIndex >= 1 && tabIndex <= 7);
    moveLeftBtn.setVisible (canReorder && tabIndex > 1);
    moveRightBtn.setVisible (canReorder && tabIndex < 7);

    resized();
}

void EasyMasterEditor::updateSatModeVisibility()
{
    // Called when SAT Mode parameter changes — re-show the stage
    if (currentStage == kSatCommon)
    {
        // Find which tab is currently SAT
        for (int t = 0; t < 9; ++t)
        {
            if (stageTypeForTab[(size_t)t] == kSatCommon)
            {
                showStage (t);
                return;
            }
        }
    }
}

void EasyMasterEditor::refreshTabLabels()
{
    juce::StringArray allNames = { "INPUT", "PULTEC", "COMP", "SAT", "OUT EQ", "FILTER", "DYN RES", "CLIPPER", "LIMITER" };
    auto order = processor.getEngine().getStageOrder();

    // Tab 0 = INPUT (fixed), Tab 8 = LIMITER (fixed)
    stageTypeForTab[0] = 0;
    stageTypeForTab[8] = 8;

    // Tabs 1-7 map to reorderable stages via engine order
    // order[pos] = stage index in reorderableStages (0=PultecEQ, 1=Comp, etc.)
    // Stage type = order[pos] + 1 (because stage 0 is INPUT)
    for (int pos = 0; pos < 7; ++pos)
        stageTypeForTab[(size_t)(pos + 1)] = order[(size_t)pos] + 1;

    // Update tab button labels
    for (int i = 0; i < 9; ++i)
    {
        int st = stageTypeForTab[(size_t)i];
        tabButtons[i]->setButtonText (allNames[st]);
    }
}

void EasyMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF14142A));

    // Top bar with subtle gradient
    g.setColour (juce::Colour (0xFF16213E));
    g.fillRect (0, 0, getWidth(), 50);
    g.setColour (juce::Colour (0xFF1A2644));
    g.fillRect (0, 0, getWidth(), 25);

    // Logo
    g.setColour (juce::Colour (0xFFE94560));
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("EASY MASTER", 12, 8, 180, 28, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xFF777799));
    g.setFont (juce::Font (9.0f));
    g.drawText ("by Phonica School", 12, 34, 140, 12, juce::Justification::centredLeft);

    // Bottom bar
    g.setColour (juce::Colour (0xFF12122A));
    g.fillRect (0, getHeight() - 70, getWidth(), 70);
    g.setColour (juce::Colour (0xFF1A1A38));
    g.fillRect (0, getHeight() - 70, getWidth(), 1);

    // Panel area with glow border
    auto panelArea = getLocalBounds().withTop (95).withBottom (getHeight() - 70).reduced (8).toFloat();
    g.setColour (juce::Colour (0xFF111128));
    g.fillRoundedRectangle (panelArea, 10.0f);
    g.setColour (juce::Colour (0xFF2A2A55));
    g.drawRoundedRectangle (panelArea, 10.0f, 1.0f);

    // ─── Stage-specific GR meter (shown inside the panel area) ───
    if (tabButtons.size() > 0)
    {
        auto meterArea = panelArea.reduced (12.0f);
        float meterY = meterArea.getBottom() - 50.0f;
        float meterW = meterArea.getWidth();
        float meterX = meterArea.getX();

        // Saturation FFT spectrum display (stage 3)
        if (currentStage == kSatCommon)
        {
            auto* sat = dynamic_cast<SaturationStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::Saturation));
            if (sat)
            {
                // Background
                float fftX = meterX;
                float fftY = meterY - 35.0f;
                float fftW = meterW;
                float fftH = 85.0f;
                fftDisplayArea = { fftX, fftY, fftW, fftH };

                g.setColour (juce::Colour (0xFF0A0A18));
                g.fillRoundedRectangle (fftDisplayArea, 6.0f);

                g.setColour (juce::Colour (0xFF888888));
                g.setFont (juce::Font (10.0f));
                g.drawText ("SPECTRUM ANALYZER", fftX + 8.0f, fftY + 2.0f, 200.0f, 14.0f, juce::Justification::centredLeft);

                // Draw area for spectrum
                float specX = fftX + 8.0f;
                float specY = fftY + 16.0f;
                float specW = fftW - 16.0f;
                float specH = fftH - 22.0f;

                // Compute FFT magnitudes
                sat->computeFFTMagnitudes();
                auto& mags = sat->getMagnitudes();

                // Band colors for fill regions
                juce::Colour bandCols[] = {
                    juce::Colour (0xFF4488CC), juce::Colour (0xFF44CC88),
                    juce::Colour (0xFFCCAA44), juce::Colour (0xFFCC4444)
                };

                bool isMulti = (sat->getMode() == 1);

                // Get crossover freqs
                float x1f = processor.getAPVTS().getRawParameterValue ("S4_Sat_Xover1")->load();
                float x2f = processor.getAPVTS().getRawParameterValue ("S4_Sat_Xover2")->load();
                float x3f = processor.getAPVTS().getRawParameterValue ("S4_Sat_Xover3")->load();

                // Draw band color fills behind spectrum (multiband only)
                if (isMulti)
                {
                    float xoverPositions[] = {
                        freqToX (x1f, specX, specW),
                        freqToX (x2f, specX, specW),
                        freqToX (x3f, specX, specW)
                    };

                    float starts[] = { specX, xoverPositions[0], xoverPositions[1], xoverPositions[2] };
                    float ends[] = { xoverPositions[0], xoverPositions[1], xoverPositions[2], specX + specW };

                    for (int b = 0; b < 4; ++b)
                    {
                        g.setColour (bandCols[b].withAlpha (0.08f));
                        g.fillRect (starts[b], specY, ends[b] - starts[b], specH);
                    }
                }

                // Draw FFT spectrum curve
                float sr = (float) processor.getSampleRate();
                if (sr <= 0) sr = 44100.0f;
                int fftHalfSize = SaturationStage::fftSize / 2;

                juce::Path specPath;
                bool pathStarted = false;
                for (int i = 1; i < fftHalfSize; ++i)
                {
                    float freq = (float) i * sr / (float) SaturationStage::fftSize;
                    if (freq < 20.0f || freq > 20000.0f) continue;

                    float xPos = freqToX (freq, specX, specW);
                    float yPos = specY + specH - mags[(size_t) i] * specH;
                    yPos = juce::jlimit (specY, specY + specH, yPos);

                    if (! pathStarted)
                    {
                        specPath.startNewSubPath (xPos, yPos);
                        pathStarted = true;
                    }
                    else
                        specPath.lineTo (xPos, yPos);
                }

                // Fill under curve
                if (pathStarted)
                {
                    juce::Path fillPath = specPath;
                    fillPath.lineTo (specX + specW, specY + specH);
                    fillPath.lineTo (specX, specY + specH);
                    fillPath.closeSubPath();
                    g.setColour (juce::Colour (0xFFE94560).withAlpha (0.15f));
                    g.fillPath (fillPath);

                    // Spectrum line
                    g.setColour (juce::Colour (0xFFE94560).withAlpha (0.7f));
                    g.strokePath (specPath, juce::PathStrokeType (1.5f));
                }

                // Draw crossover lines (multiband only)
                if (isMulti)
                {
                    float xoverFreqs[] = { x1f, x2f, x3f };
                    for (int xo = 0; xo < 3; ++xo)
                    {
                        float xPos = freqToX (xoverFreqs[xo], specX, specW);
                        bool isDragging = (draggingXover == xo);

                        // Crossover line
                        g.setColour (isDragging ? juce::Colours::white : juce::Colour (0xFFCCCCCC));
                        g.drawLine (xPos, specY, xPos, specY + specH, isDragging ? 2.5f : 1.5f);

                        // Drag handle (triangle/circle)
                        g.setColour (isDragging ? juce::Colours::white : juce::Colour (0xFFAAAAAA));
                        g.fillEllipse (xPos - 4.0f, specY + specH - 2.0f, 8.0f, 8.0f);

                        // Frequency label
                        g.setFont (juce::Font (9.0f, juce::Font::bold));
                        auto fmtFreq = [](float f) { return f >= 1000.0f ? juce::String (f/1000.0f, 1) + "k" : juce::String ((int)f); };
                        g.drawText (fmtFreq (xoverFreqs[xo]), (int)(xPos - 16), (int)(specY - 12), 32, 12, juce::Justification::centred);
                    }
                }

                // Frequency axis labels
                g.setColour (juce::Colour (0xFF555555));
                g.setFont (juce::Font (7.0f));
                float freqLabels[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
                for (float f : freqLabels)
                {
                    float xPos = freqToX (f, specX, specW);
                    auto fmtFreq = [](float fr) { return fr >= 1000.0f ? juce::String (fr/1000.0f, 0) + "k" : juce::String ((int)fr); };
                    g.drawText (fmtFreq (f), (int)(xPos - 12), (int)(specY + specH + 1), 24, 10, juce::Justification::centred);
                }
            }
        }

        // Pultec EQ — curve display + FFT analyzer (stage 1)
        if (currentStage == 1)
        {
            auto* pultec = dynamic_cast<PultecEQStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::PultecEQ));
            if (pultec)
            {
                // Section headers
                g.setColour (juce::Colour (0xFFE94560));
                g.setFont (juce::Font (9.0f, juce::Font::bold));
                auto kArea = panelArea.reduced (12.0f);
                g.drawText ("EQP-1A", (int)(kArea.getX()), (int)(kArea.getY() + 28), 60, 12, juce::Justification::centredLeft);

                // Find where row 2 starts (after first 8 controls)
                float row2Y = kArea.getY() + 28 + (kArea.getHeight() - 55 - 28) * 0.5f;
                g.drawText ("MEQ-5", (int)(kArea.getX()), (int)(row2Y), 60, 12, juce::Justification::centredLeft);

                // EQ curve + FFT display area
                float dispX = meterX;
                float dispY = meterY - 60.0f;
                float dispW = meterW;
                float dispH = 110.0f;

                g.setColour (juce::Colour (0xFF0D0D1E));
                g.fillRoundedRectangle (dispX, dispY, dispW, dispH, 6.0f);
                g.setColour (juce::Colour (0xFF2A2A50));
                g.drawRoundedRectangle (dispX, dispY, dispW, dispH, 6.0f, 0.5f);

                float specX = dispX + 30.0f;
                float specY2 = dispY + 4.0f;
                float specW = dispW - 36.0f;
                float specH = dispH - 10.0f;

                // dB grid
                g.setColour (juce::Colour (0xFF1A1A35));
                float dbRange = 18.0f;
                for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
                {
                    float yy = specY2 + specH * 0.5f - (db / dbRange) * (specH * 0.5f);
                    g.drawHorizontalLine ((int)yy, specX, specX + specW);
                }
                // 0 dB line brighter
                g.setColour (juce::Colour (0xFF2A2A55));
                float zeroY = specY2 + specH * 0.5f;
                g.drawHorizontalLine ((int)zeroY, specX, specX + specW);

                // dB labels
                g.setColour (juce::Colour (0xFF555577));
                g.setFont (juce::Font (7.0f));
                for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
                {
                    float yy = specY2 + specH * 0.5f - (db / dbRange) * (specH * 0.5f);
                    g.drawText (juce::String ((int)db), (int)(dispX + 2), (int)(yy - 5), 26, 10, juce::Justification::centredRight);
                }

                // FFT spectrum (faded behind)
                pultec->computeFFTMagnitudes();
                auto& mags = pultec->getMagnitudes();
                float sr = (float) processor.getSampleRate();
                if (sr <= 0) sr = 44100.0f;
                int fftHalf = PultecEQStage::fftSize / 2;

                juce::Path fftPath;
                bool fftStarted = false;
                for (int i = 1; i < fftHalf; ++i)
                {
                    float freq = (float) i * sr / (float) PultecEQStage::fftSize;
                    if (freq < 20.0f || freq > 20000.0f) continue;
                    float xPos = freqToX (freq, specX, specW);
                    float yPos2 = specY2 + specH - mags[(size_t)i] * specH;
                    yPos2 = juce::jlimit (specY2, specY2 + specH, yPos2);
                    if (!fftStarted) { fftPath.startNewSubPath (xPos, yPos2); fftStarted = true; }
                    else fftPath.lineTo (xPos, yPos2);
                }
                if (fftStarted)
                {
                    juce::Path fftFill = fftPath;
                    fftFill.lineTo (specX + specW, specY2 + specH);
                    fftFill.lineTo (specX, specY2 + specH);
                    fftFill.closeSubPath();
                    g.setColour (juce::Colour (0xFF4488CC).withAlpha (0.08f));
                    g.fillPath (fftFill);
                    g.setColour (juce::Colour (0xFF4488CC).withAlpha (0.25f));
                    g.strokePath (fftPath, juce::PathStrokeType (1.0f));
                }

                // EQ curve (bright)
                juce::Path eqPath;
                bool eqStarted = false;
                for (float px = 0; px <= specW; px += 1.5f)
                {
                    float freq = std::pow (10.0f, std::log10 (20.0f) + (px / specW) * (std::log10 (20000.0f) - std::log10 (20.0f)));
                    double magDb = pultec->getMagnitudeAtFreq ((double) freq);
                    float yy = specY2 + specH * 0.5f - (float)(magDb / dbRange) * (specH * 0.5f);
                    yy = juce::jlimit (specY2, specY2 + specH, yy);
                    if (!eqStarted) { eqPath.startNewSubPath (specX + px, yy); eqStarted = true; }
                    else eqPath.lineTo (specX + px, yy);
                }
                if (eqStarted)
                {
                    // Fill above/below 0dB
                    juce::Path eqFill = eqPath;
                    eqFill.lineTo (specX + specW, zeroY);
                    eqFill.lineTo (specX, zeroY);
                    eqFill.closeSubPath();
                    g.setColour (juce::Colour (0xFFE94560).withAlpha (0.12f));
                    g.fillPath (eqFill);
                    // Curve line
                    g.setColour (juce::Colour (0xFFE94560).withAlpha (0.85f));
                    g.strokePath (eqPath, juce::PathStrokeType (2.0f));
                }

                // Freq axis
                g.setColour (juce::Colour (0xFF444466));
                g.setFont (juce::Font (7.0f));
                float fLabels2[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
                for (float f : fLabels2)
                {
                    float xPos = freqToX (f, specX, specW);
                    g.setColour (juce::Colour (0xFF1A1A35));
                    g.drawVerticalLine ((int)xPos, specY2, specY2 + specH);
                    g.setColour (juce::Colour (0xFF444466));
                    auto fmt = [](float fr) { return fr >= 1000.0f ? juce::String (fr/1000.0f, 0) + "k" : juce::String ((int)fr); };
                    g.drawText (fmt (f), (int)(xPos - 10), (int)(specY2 + specH + 1), 20, 10, juce::Justification::centred);
                }
            }
        }

        // Output EQ — FabFilter-style curve display (stage 4)
        if (currentStage == 4)
        {
            auto* outEQ = dynamic_cast<OutputEQStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::OutputEQ));
            if (outEQ)
            {
                // Display area — takes most of the bottom
                float dispX = meterX;
                float dispY = meterY - 55.0f;
                float dispW = meterW;
                float dispH = 105.0f;

                // Background with border
                g.setColour (juce::Colour (0xFF0D0D1E));
                g.fillRoundedRectangle (dispX, dispY, dispW, dispH, 8.0f);
                g.setColour (juce::Colour (0xFF2A2A50));
                g.drawRoundedRectangle (dispX, dispY, dispW, dispH, 8.0f, 0.5f);

                float specX = dispX + 30.0f;
                float specY2 = dispY + 6.0f;
                float specW = dispW - 36.0f;
                float specH = dispH - 14.0f;
                float dbRange = 18.0f;

                // Store for mouse dragging
                eqDisplayArea = { specX, specY2, specW, specH };

                // dB grid lines
                for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
                {
                    float yy = specY2 + specH * 0.5f - (db / dbRange) * (specH * 0.5f);
                    g.setColour (db == 0.0f ? juce::Colour (0xFF2A2A55) : juce::Colour (0xFF1A1A35));
                    g.drawHorizontalLine ((int)yy, specX, specX + specW);
                }

                // dB labels
                g.setColour (juce::Colour (0xFF555577));
                g.setFont (juce::Font (7.0f));
                for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
                {
                    float yy = specY2 + specH * 0.5f - (db / dbRange) * (specH * 0.5f);
                    g.drawText (juce::String ((int)db), (int)(dispX + 2), (int)(yy - 5), 26, 10, juce::Justification::centredRight);
                }

                // Freq grid + labels
                g.setFont (juce::Font (7.0f));
                float fLabels[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
                for (float f : fLabels)
                {
                    float xPos = freqToX (f, specX, specW);
                    g.setColour (juce::Colour (0xFF1A1A35));
                    g.drawVerticalLine ((int)xPos, specY2, specY2 + specH);
                    g.setColour (juce::Colour (0xFF444466));
                    auto fmt = [](float fr) { return fr >= 1000.0f ? juce::String (fr/1000.0f, 0) + "k" : juce::String ((int)fr); };
                    g.drawText (fmt (f), (int)(xPos - 10), (int)(specY2 + specH + 1), 20, 9, juce::Justification::centred);
                }

                // FFT spectrum (faded behind)
                outEQ->computeFFTMagnitudes();
                auto& mags = outEQ->getMagnitudes();
                float sr = (float) processor.getSampleRate();
                if (sr <= 0) sr = 44100.0f;
                int fftHalf = OutputEQStage::fftSize / 2;

                juce::Path fftPath;
                bool fftStarted = false;
                for (int i = 1; i < fftHalf; ++i)
                {
                    float freq = (float) i * sr / (float) OutputEQStage::fftSize;
                    if (freq < 20.0f || freq > 20000.0f) continue;
                    float xPos = freqToX (freq, specX, specW);
                    float yPos2 = specY2 + specH - mags[(size_t)i] * specH;
                    yPos2 = juce::jlimit (specY2, specY2 + specH, yPos2);
                    if (!fftStarted) { fftPath.startNewSubPath (xPos, yPos2); fftStarted = true; }
                    else fftPath.lineTo (xPos, yPos2);
                }
                if (fftStarted)
                {
                    juce::Path fftFill = fftPath;
                    fftFill.lineTo (specX + specW, specY2 + specH);
                    fftFill.lineTo (specX, specY2 + specH);
                    fftFill.closeSubPath();
                    g.setColour (juce::Colour (0xFF4488CC).withAlpha (0.06f));
                    g.fillPath (fftFill);
                    g.setColour (juce::Colour (0xFF4488CC).withAlpha (0.2f));
                    g.strokePath (fftPath, juce::PathStrokeType (1.0f));
                }

                // EQ curve (bright, FabFilter pink/orange)
                float zeroY = specY2 + specH * 0.5f;
                juce::Path eqPath;
                bool eqStarted = false;
                for (float px = 0; px <= specW; px += 1.0f)
                {
                    float freq = std::pow (10.0f, std::log10 (20.0f) + (px / specW) * (std::log10 (20000.0f) - std::log10 (20.0f)));
                    double magDb = outEQ->getMagnitudeAtFreq ((double) freq);
                    float yy = specY2 + specH * 0.5f - (float)(magDb / dbRange) * (specH * 0.5f);
                    yy = juce::jlimit (specY2, specY2 + specH, yy);
                    if (!eqStarted) { eqPath.startNewSubPath (specX + px, yy); eqStarted = true; }
                    else eqPath.lineTo (specX + px, yy);
                }
                if (eqStarted)
                {
                    // Fill between curve and 0dB
                    juce::Path eqFill = eqPath;
                    eqFill.lineTo (specX + specW, zeroY);
                    eqFill.lineTo (specX, zeroY);
                    eqFill.closeSubPath();
                    g.setColour (juce::Colour (0xFFE9A045).withAlpha (0.15f));
                    g.fillPath (eqFill);
                    // Curve line with glow
                    g.setColour (juce::Colour (0xFFE9A045).withAlpha (0.3f));
                    g.strokePath (eqPath, juce::PathStrokeType (4.0f));
                    g.setColour (juce::Colour (0xFFFFBB55).withAlpha (0.9f));
                    g.strokePath (eqPath, juce::PathStrokeType (2.0f));
                }

                // Band nodes (circles on the curve)
                juce::Colour nodeCols[] = {
                    juce::Colour (0xFF4488CC), juce::Colour (0xFF44CC88),
                    juce::Colour (0xFFE9A045), juce::Colour (0xFFCC6688),
                    juce::Colour (0xFFCC4444)
                };
                juce::String nodeLabels[] = { "LS", "LM", "M", "HM", "HS" };

                for (int b = 0; b < OutputEQStage::NUM_BANDS; ++b)
                {
                    auto bi = outEQ->getBandInfo (b);
                    float nodeX = freqToX (bi.freq, specX, specW);
                    double nodeMag = outEQ->getMagnitudeAtFreq ((double) bi.freq);
                    float nodeY = specY2 + specH * 0.5f - (float)(nodeMag / dbRange) * (specH * 0.5f);
                    nodeY = juce::jlimit (specY2 + 4.0f, specY2 + specH - 4.0f, nodeY);

                    float nodeR = (std::abs (bi.gain) > 0.5f) ? 7.0f : 5.0f;

                    // Glow
                    g.setColour (nodeCols[b].withAlpha (0.2f));
                    g.fillEllipse (nodeX - nodeR - 2, nodeY - nodeR - 2, (nodeR + 2) * 2, (nodeR + 2) * 2);
                    // Fill
                    g.setColour (nodeCols[b].withAlpha (0.8f));
                    g.fillEllipse (nodeX - nodeR, nodeY - nodeR, nodeR * 2, nodeR * 2);
                    // Border
                    g.setColour (juce::Colours::white.withAlpha (0.5f));
                    g.drawEllipse (nodeX - nodeR, nodeY - nodeR, nodeR * 2, nodeR * 2, 1.0f);
                    // Label
                    g.setColour (juce::Colours::white.withAlpha (0.7f));
                    g.setFont (juce::Font (7.0f, juce::Font::bold));
                    g.drawText (nodeLabels[b], (int)(nodeX - 10), (int)(nodeY - nodeR - 12), 20, 10, juce::Justification::centred);
                }
            }
        }

        // Compressor GR (stage 2)
        if (currentStage == 2)
        {
            auto* comp = processor.getEngine().getStage (ProcessingStage::StageID::Compressor);
            if (comp)
            {
                float gr = comp->getMeterData().gainReduction.load();
                float normalized = juce::jlimit (0.0f, 1.0f, -gr / 20.0f);

                // Background
                g.setColour (juce::Colour (0xFF0A0A18));
                g.fillRoundedRectangle (meterX, meterY, meterW, 40.0f, 6.0f);

                // Label
                g.setColour (juce::Colour (0xFF888888));
                g.setFont (juce::Font (10.0f));
                g.drawText ("GAIN REDUCTION", meterX + 8.0f, meterY + 2.0f, 150.0f, 14.0f, juce::Justification::centredLeft);

                // Meter bar
                auto barArea = juce::Rectangle<float> (meterX + 8.0f, meterY + 18.0f, meterW - 16.0f, 16.0f);
                g.setColour (juce::Colour (0xFF1A1A2E));
                g.fillRoundedRectangle (barArea, 3.0f);

                // GR fill — from right to left (reduction)
                float fillW = barArea.getWidth() * normalized;
                g.setColour (juce::Colour (0xFFFF6B6B));
                g.fillRoundedRectangle (barArea.getRight() - fillW, barArea.getY(), fillW, barArea.getHeight(), 3.0f);

                // dB scale marks
                g.setColour (juce::Colour (0xFF555555));
                g.setFont (juce::Font (8.0f));
                for (int db = 0; db >= -20; db -= 5)
                {
                    float xPos = barArea.getRight() - barArea.getWidth() * ((float)-db / 20.0f);
                    g.drawVerticalLine ((int)xPos, barArea.getY(), barArea.getY() + 3.0f);
                    g.drawText (juce::String (db), (int)xPos - 12, (int)barArea.getBottom(), 24, 10, juce::Justification::centred);
                }

                // Value readout
                g.setColour (juce::Colours::white);
                g.setFont (juce::Font (12.0f, juce::Font::bold));
                g.drawText (juce::String (gr, 1) + " dB", meterX + meterW - 120.0f, meterY + 2.0f, 110.0f, 14.0f, juce::Justification::centredRight);
            }
        }

        // Dynamic Resonance display (stage 6)
        if (currentStage == 6)
        {
            auto* dynRes = dynamic_cast<DynamicResonanceStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::DynamicResonance));
            if (dynRes)
            {
                // Draw spectral GR display
                g.setColour (juce::Colour (0xFF0A0A18));
                g.fillRoundedRectangle (meterX, meterY, meterW, 50.0f, 6.0f);

                g.setColour (juce::Colour (0xFF888888));
                g.setFont (juce::Font (10.0f));
                g.drawText ("RESONANCE REDUCTION", meterX + 8.0f, meterY + 2.0f, 200.0f, 14.0f, juce::Justification::centredLeft);

                // Draw 24 bars, one per band
                float barAreaX = meterX + 8.0f;
                float barAreaY = meterY + 16.0f;
                float barAreaW = meterW - 16.0f;
                float barAreaH = 30.0f;
                float bandW = barAreaW / (float)DynamicResonanceStage::NUM_BANDS;

                for (int b = 0; b < DynamicResonanceStage::NUM_BANDS; ++b)
                {
                    float grDb = dynRes->bandGR[(size_t)b].load (std::memory_order_relaxed);
                    float normalized = juce::jlimit (0.0f, 1.0f, -grDb / 18.0f);
                    float x = barAreaX + (float)b * bandW;

                    // Background bar
                    g.setColour (juce::Colour (0xFF1A1A2E));
                    g.fillRect (x + 1.0f, barAreaY, bandW - 2.0f, barAreaH);

                    // GR fill (from bottom up)
                    if (normalized > 0.01f)
                    {
                        float fillH = barAreaH * normalized;
                        // Color: green for light GR, orange/red for heavy
                        auto col = normalized < 0.3f ? juce::Colour (0xFF44CC44) :
                                   normalized < 0.6f ? juce::Colour (0xFFCCAA22) :
                                                       juce::Colour (0xFFE94560);
                        g.setColour (col);
                        g.fillRect (x + 1.0f, barAreaY + barAreaH - fillH, bandW - 2.0f, fillH);
                    }
                }

                // Frequency labels
                g.setColour (juce::Colour (0xFF555555));
                g.setFont (juce::Font (7.0f));
                int labelBands[] = { 0, 6, 12, 18, 23 };
                for (int lb : labelBands)
                {
                    float freq = dynRes->getBandFreq (lb);
                    juce::String freqStr = freq >= 1000.0f ?
                        juce::String (freq / 1000.0f, 1) + "k" :
                        juce::String ((int)freq);
                    float x = barAreaX + (float)lb * bandW;
                    g.drawText (freqStr, (int)x - 8, (int)(barAreaY + barAreaH + 1), 24, 10, juce::Justification::centred);
                }
            }
        }

        // Limiter — Insight-style metering panel (stage 8)
        if (currentStage == 8)
        {
            auto* om = processor.getEngine().getOutputMeter();
            auto* lim = processor.getEngine().getStage (ProcessingStage::StageID::Limiter);
            if (om && lim)
            {
                float fullX = meterArea.getX();
                float fullY = meterArea.getY() + 140.0f;
                float fullW = meterArea.getWidth();
                float fullH = meterArea.getBottom() - fullY;

                // Panel background with subtle border
                g.setColour (juce::Colour (0xFF0D0D1E));
                g.fillRoundedRectangle (fullX, fullY, fullW, fullH, 8.0f);
                g.setColour (juce::Colour (0xFF2A2A50));
                g.drawRoundedRectangle (fullX, fullY, fullW, fullH, 8.0f, 1.0f);

                // ════════════════════════════════════════════════
                // LEFT: LOUDNESS (5 bars)
                // ════════════════════════════════════════════════
                float lufsW = fullW * 0.18f;
                float lufsX = fullX + 10.0f;
                float lufsY = fullY + 22.0f;
                float lufsH = fullH - 42.0f;

                // Section background
                g.setColour (juce::Colour (0xFF0A0A16));
                g.fillRoundedRectangle (lufsX - 4, fullY + 4, lufsW + 4, fullH - 8, 6.0f);

                g.setColour (juce::Colour (0xFF666688));
                g.setFont (juce::Font (9.0f, juce::Font::bold));
                g.drawText ("LOUDNESS", (int)lufsX, (int)(fullY + 6), (int)lufsW, 12, juce::Justification::centred);

                float mom  = om->getMomentaryLUFS();
                float st   = om->getShortTermLUFS();
                float intg = om->getIntegratedLUFS();
                float tp   = om->getTruePeak();
                float gr   = lim->getMeterData().gainReduction.load();

                juce::String barLabels[] = { "M", "S", "I", "TP", "GR" };
                float barValues[] = { mom, st, intg, tp, gr };
                juce::Colour barColors[] = {
                    juce::Colour (0xFF44CC88), juce::Colour (0xFF4488CC),
                    juce::Colour (0xFFE94560), juce::Colour (0xFFCCAA44),
                    juce::Colour (0xFFFF6B6B)
                };

                float barW = (lufsW - 8.0f) / 5.0f;
                for (int b = 0; b < 5; ++b)
                {
                    float bx = lufsX + (float)b * barW + 1.0f;

                    // Bar track
                    g.setColour (juce::Colour (0xFF18182E));
                    g.fillRoundedRectangle (bx, lufsY, barW - 2.0f, lufsH, 3.0f);

                    float val = barValues[b];
                    float normalized;
                    if (b < 3) normalized = juce::jlimit (0.0f, 1.0f, (val + 60.0f) / 60.0f);
                    else if (b == 3) normalized = juce::jlimit (0.0f, 1.0f, (val + 60.0f) / 66.0f);
                    else normalized = juce::jlimit (0.0f, 1.0f, -val / 20.0f);

                    float fillH = lufsH * normalized;
                    auto col = barColors[b];
                    if (b == 3 && val > -0.3f) col = juce::Colour (0xFFFF2222);
                    else if (b == 3 && val > -1.0f) col = juce::Colour (0xFFFF8800);

                    // Gradient-like fill: brighter at top
                    g.setColour (col.withAlpha (0.5f));
                    g.fillRoundedRectangle (bx, lufsY + lufsH - fillH, barW - 2.0f, fillH, 3.0f);
                    g.setColour (col.withAlpha (0.85f));
                    float capH = std::min (fillH, 4.0f);
                    g.fillRoundedRectangle (bx, lufsY + lufsH - fillH, barW - 2.0f, capH, 3.0f);

                    // Label
                    g.setColour (juce::Colour (0xFF999999));
                    g.setFont (juce::Font (8.0f, juce::Font::bold));
                    g.drawText (barLabels[b], (int)bx, (int)(lufsY + lufsH + 2), (int)(barW - 2), 10, juce::Justification::centred);

                    // Value
                    g.setColour (juce::Colours::white.withAlpha (0.8f));
                    g.setFont (juce::Font (8.0f));
                    juce::String valStr = (val > -100.f) ? juce::String (val, 1) : "--";
                    g.drawText (valStr, (int)(bx - 4), (int)(lufsY - 13), (int)(barW + 6), 12, juce::Justification::centred);
                }

                // ════════════════════════════════════════════════
                // CENTER: SPECTRUM ANALYZER
                // ════════════════════════════════════════════════
                float specX = fullX + lufsW + 24.0f;
                float specW = fullW * 0.48f;
                float specY = fullY + 22.0f;
                float specH = fullH - 42.0f;

                // Section background
                g.setColour (juce::Colour (0xFF0A0A16));
                g.fillRoundedRectangle (specX - 4, fullY + 4, specW + 8, fullH - 8, 6.0f);

                g.setColour (juce::Colour (0xFF666688));
                g.setFont (juce::Font (9.0f, juce::Font::bold));
                g.drawText ("SPECTRUM", (int)specX, (int)(fullY + 6), (int)specW, 12, juce::Justification::centredLeft);

                // Spectrum area
                g.setColour (juce::Colour (0xFF0F0F22));
                g.fillRoundedRectangle (specX, specY, specW, specH, 4.0f);

                // Grid
                g.setColour (juce::Colour (0xFF1A1A35));
                float dbLines[] = { -48, -36, -24, -12 };
                for (float db : dbLines)
                {
                    float yy = specY + specH * (1.0f - (db + 60.0f) / 60.0f);
                    g.drawHorizontalLine ((int) yy, specX + 2, specX + specW - 2);
                }

                // FFT curve
                om->computeFFTMagnitudes();
                auto& mags = om->getMagnitudes();
                float sr = (float) processor.getSampleRate();
                if (sr <= 0) sr = 44100.0f;
                int fftHalf = OutputMeter::fftSize / 2;

                juce::Path specPath;
                bool started = false;
                for (int i = 1; i < fftHalf; ++i)
                {
                    float freq = (float) i * sr / (float) OutputMeter::fftSize;
                    if (freq < 20.0f || freq > 20000.0f) continue;
                    float xPos = freqToX (freq, specX, specW);
                    float yPos = specY + specH - mags[(size_t) i] * specH;
                    yPos = juce::jlimit (specY, specY + specH, yPos);
                    if (! started) { specPath.startNewSubPath (xPos, yPos); started = true; }
                    else specPath.lineTo (xPos, yPos);
                }

                if (started)
                {
                    juce::Path fillPath = specPath;
                    fillPath.lineTo (specX + specW, specY + specH);
                    fillPath.lineTo (specX, specY + specH);
                    fillPath.closeSubPath();
                    g.setColour (juce::Colour (0xFF4488CC).withAlpha (0.12f));
                    g.fillPath (fillPath);
                    g.setColour (juce::Colour (0xFF5599DD).withAlpha (0.65f));
                    g.strokePath (specPath, juce::PathStrokeType (1.5f));
                }

                // Freq axis
                g.setColour (juce::Colour (0xFF444466));
                g.setFont (juce::Font (7.0f));
                float fLabels[] = { 100, 500, 1000, 5000, 10000 };
                for (float f : fLabels)
                {
                    float xPos = freqToX (f, specX, specW);
                    auto fmt = [](float fr) { return fr >= 1000.0f ? juce::String (fr/1000.0f, 0) + "k" : juce::String ((int)fr); };
                    g.drawText (fmt (f), (int)(xPos - 10), (int)(specY + specH + 1), 20, 10, juce::Justification::centred);
                    g.setColour (juce::Colour (0xFF1A1A35));
                    g.drawVerticalLine ((int) xPos, specY + 2, specY + specH - 2);
                    g.setColour (juce::Colour (0xFF444466));
                }

                // ════════════════════════════════════════════════
                // RIGHT: STEREO IMAGER (4 band cards)
                // ════════════════════════════════════════════════
                float sterX = specX + specW + 16.0f;
                float sterW = fullW - (sterX - fullX) - 8.0f;
                float sterY = fullY + 22.0f;
                float sterH = fullH - 28.0f;

                // Section background
                g.setColour (juce::Colour (0xFF0A0A16));
                g.fillRoundedRectangle (sterX - 4, fullY + 4, sterW + 8, fullH - 8, 6.0f);

                g.setColour (juce::Colour (0xFF666688));
                g.setFont (juce::Font (9.0f, juce::Font::bold));
                g.drawText ("STEREO IMAGER", (int)sterX, (int)(fullY + 6), (int)sterW, 12, juce::Justification::centredLeft);

                // Global correlation (compact)
                float gCorrY = sterY;
                float gCorrH = 10.0f;
                float gCorr = om->getCorrelation();

                g.setColour (juce::Colour (0xFF18182E));
                g.fillRoundedRectangle (sterX, gCorrY, sterW, gCorrH, 3.0f);

                float gCenter = sterX + sterW * 0.5f;
                float gPos = gCenter + gCorr * (sterW * 0.5f);
                auto gCol = gCorr > 0.3f ? juce::Colour (0xFF44CC88) :
                            gCorr > 0.0f ? juce::Colour (0xFFCCAA44) : juce::Colour (0xFFE94560);
                float gFS = std::min (gCenter, gPos);
                g.setColour (gCol.withAlpha (0.65f));
                g.fillRoundedRectangle (gFS, gCorrY + 1, std::abs (gPos - gCenter), gCorrH - 2, 2.0f);
                g.setColour (juce::Colour (0xFF555577));
                g.drawVerticalLine ((int) gCenter, gCorrY, gCorrY + gCorrH);

                // ─── Band Cards ───
                juce::Colour bandCols[] = {
                    juce::Colour (0xFF4488CC), juce::Colour (0xFF44CC88),
                    juce::Colour (0xFFCCAA44), juce::Colour (0xFFCC4444)
                };
                juce::String bandNames[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };

                float cardsStartY = gCorrY + gCorrH + 6.0f;
                float cardsAvailH = (sterY + sterH) - cardsStartY;
                float cardGap = 4.0f;
                float xoverBadgeH = 14.0f;
                // 4 cards + 3 xover badges
                float totalGaps = 3.0f * (cardGap + xoverBadgeH + cardGap);
                float cardH = (cardsAvailH - totalGaps) / 4.0f;
                int activeSolo = om->getSoloedBand();

                // Store display area for crossover drag
                imagerDisplayArea = { sterX, cardsStartY, sterW, cardsAvailH };

                float xf0 = om->getImagerXover (0);
                float xf1 = om->getImagerXover (1);
                float xf2 = om->getImagerXover (2);
                auto fmtF2 = [](float f) { return f >= 1000.0f ? juce::String (f/1000.0f, 1) + "k" : juce::String ((int)f); };
                juce::String freqRanges[] = {
                    "20 - " + fmtF2 (xf0) + " Hz",
                    fmtF2 (xf0) + " - " + fmtF2 (xf1),
                    fmtF2 (xf1) + " - " + fmtF2 (xf2),
                    fmtF2 (xf2) + " - 20k"
                };
                float xoverFreqs[] = { xf0, xf1, xf2 };

                float yPos = cardsStartY;
                for (int b = 0; b < 4; ++b)
                {
                    auto& bs = om->getBandStereo (b);
                    auto cardRect = juce::Rectangle<float> (sterX, yPos, sterW, cardH);

                    // Card background
                    g.setColour (juce::Colour (0xFF12122A));
                    g.fillRoundedRectangle (cardRect, 5.0f);

                    // Colored accent bar on left
                    g.setColour (bandCols[b]);
                    g.fillRoundedRectangle (sterX, yPos + 2, 3.0f, cardH - 4, 2.0f);

                    // Band name + freq range
                    g.setColour (bandCols[b]);
                    g.setFont (juce::Font (9.0f, juce::Font::bold));
                    g.drawText (bandNames[b], (int)(sterX + 26), (int)yPos, 48, (int)(cardH * 0.45f), juce::Justification::centredLeft);
                    g.setColour (juce::Colour (0xFF777799));
                    g.setFont (juce::Font (7.0f));
                    g.drawText (freqRanges[b], (int)(sterX + 68), (int)yPos, 80, (int)(cardH * 0.45f), juce::Justification::centredLeft);

                    // Solo button — left side, next to accent bar
                    float soloBtnX = sterX + 6.0f;
                    float soloBtnY = yPos + 2.0f;
                    float soloBtnW = 18.0f;
                    float soloBtnH = std::min (cardH * 0.4f, 16.0f);
                    imgSoloBtnRects[(size_t)b] = { soloBtnX, soloBtnY, soloBtnW, soloBtnH };

                    bool isSoloed = (activeSolo == b);
                    g.setColour (isSoloed ? juce::Colour (0xFFFFCC00) : juce::Colour (0xFF2A2A48));
                    g.fillRoundedRectangle (soloBtnX, soloBtnY, soloBtnW, soloBtnH, 3.0f);
                    if (!isSoloed) { g.setColour (juce::Colour (0xFF3A3A58)); g.drawRoundedRectangle (soloBtnX, soloBtnY, soloBtnW, soloBtnH, 3.0f, 0.5f); }
                    g.setColour (isSoloed ? juce::Colour (0xFF000000) : juce::Colour (0xFF999999));
                    g.setFont (juce::Font (8.0f, juce::Font::bold));
                    g.drawText ("S", (int)soloBtnX, (int)soloBtnY, (int)soloBtnW, (int)soloBtnH, juce::Justification::centred);

                    // Correlation bar
                    float corrY = yPos + cardH * 0.48f;
                    float corrH = std::max (cardH * 0.22f, 8.0f);
                    float corrW = sterW - 78.0f;

                    g.setColour (juce::Colour (0xFF18182E));
                    g.fillRoundedRectangle (sterX + 6, corrY, corrW, corrH, 3.0f);

                    float bCorr = bs.correlation.load();
                    float bCenter = sterX + 6 + corrW * 0.5f;
                    float bPos2 = bCenter + bCorr * (corrW * 0.5f);
                    auto bCol = bCorr > 0.3f ? juce::Colour (0xFF44CC88) :
                                bCorr > 0.0f ? juce::Colour (0xFFCCAA44) : juce::Colour (0xFFE94560);
                    float bFS = std::min (bCenter, bPos2);
                    g.setColour (bCol.withAlpha (0.65f));
                    g.fillRoundedRectangle (bFS, corrY + 1, std::abs (bPos2 - bCenter), corrH - 2, 2.0f);
                    g.setColour (juce::Colour (0xFF444466));
                    g.drawVerticalLine ((int) bCenter, corrY, corrY + corrH);

                    // Corr value
                    g.setColour (juce::Colours::white.withAlpha (0.5f));
                    g.setFont (juce::Font (7.0f));
                    g.drawText (juce::String (bCorr, 2), (int)(sterX + corrW - 14), (int)corrY, 24, (int)corrH, juce::Justification::centredRight);

                    // Width bar
                    float widY = corrY + corrH + 2.0f;
                    float widH = corrH;

                    g.setColour (juce::Colour (0xFF18182E));
                    g.fillRoundedRectangle (sterX + 6, widY, corrW, widH, 3.0f);

                    float bWidth = bs.width.load();
                    float wFill = corrW * bWidth;
                    g.setColour (bandCols[b].withAlpha (0.4f));
                    g.fillRoundedRectangle (sterX + 6 + (corrW - wFill) * 0.5f, widY + 1, wFill, widH - 2, 2.0f);

                    g.setColour (juce::Colours::white.withAlpha (0.5f));
                    g.drawText (juce::String ((int)(bWidth * 200)) + "%", (int)(sterX + corrW - 14), (int)widY, 24, (int)widH, juce::Justification::centredRight);

                    yPos += cardH;

                    // ─── Crossover badge between bands ───
                    if (b < 3)
                    {
                        yPos += cardGap;
                        float badgeY = yPos;
                        bool isDragging = (draggingImgXover == b);

                        // Draggable line across full width
                        g.setColour (isDragging ? juce::Colour (0xFF6666AA) : juce::Colour (0xFF2A2A48));
                        g.drawHorizontalLine ((int)(badgeY + xoverBadgeH * 0.5f), sterX + 4, sterX + sterW - 4);

                        // Center pill badge
                        float pillW = 52.0f;
                        float pillX = sterX + (sterW - pillW) * 0.5f;
                        g.setColour (isDragging ? juce::Colour (0xFF4444AA) : juce::Colour (0xFF1E1E3A));
                        g.fillRoundedRectangle (pillX, badgeY, pillW, xoverBadgeH, 7.0f);
                        g.setColour (isDragging ? juce::Colour (0xFF8888DD) : juce::Colour (0xFF555577));
                        g.drawRoundedRectangle (pillX, badgeY, pillW, xoverBadgeH, 7.0f, 0.8f);
                        g.setColour (isDragging ? juce::Colours::white : juce::Colour (0xFFAAAACC));
                        g.setFont (juce::Font (8.0f, juce::Font::bold));
                        g.drawText (fmtF2 (xoverFreqs[b]) + " Hz", (int)pillX, (int)badgeY, (int)pillW, (int)xoverBadgeH, juce::Justification::centred);

                        // Drag grip dots
                        g.setColour (isDragging ? juce::Colour (0xFF8888DD) : juce::Colour (0xFF444466));
                        for (int d = 0; d < 3; ++d)
                        {
                            float dotX = pillX + 6.0f + (float)d * 4.0f;
                            g.fillEllipse (dotX, badgeY + 5.0f, 2.0f, 2.0f);
                            dotX = pillX + pillW - 14.0f + (float)d * 4.0f;
                            g.fillEllipse (dotX, badgeY + 5.0f, 2.0f, 2.0f);
                        }

                        yPos += xoverBadgeH + cardGap;
                    }
                }
            }
        }

        // ─── Bottom bar: always-visible Limiter GR ───
        auto* limiter = processor.getEngine().getStage (ProcessingStage::StageID::Limiter);
        if (limiter)
        {
            float gr = limiter->getMeterData().gainReduction.load();
            float normalized = juce::jlimit (0.0f, 1.0f, -gr / 20.0f);
            auto grArea = juce::Rectangle<float> (12.0f, (float)getHeight() - 46.0f, (float)getWidth() - 234.0f, 18.0f);
            g.setColour (juce::Colour (0xFF0D0D1E));
            g.fillRoundedRectangle (grArea, 4.0f);
            g.setColour (juce::Colour (0xFF2A2A50));
            g.drawRoundedRectangle (grArea, 4.0f, 0.5f);
            if (normalized > 0.001f)
            {
                g.setColour (juce::Colour (0xFFE94560).withAlpha (0.7f));
                g.fillRoundedRectangle (grArea.getX() + 1, grArea.getY() + 1, (grArea.getWidth() - 2) * normalized, grArea.getHeight() - 2, 3.0f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::Font (10.0f));
            g.drawText ("LIM GR: " + juce::String (gr, 1) + " dB", grArea, juce::Justification::centred);
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
    presetSelector.setBounds (topBar.removeFromLeft (160).reduced (8));
    savePresetButton.setBounds (topBar.removeFromLeft (55).reduced (6));
    initButton.setBounds (topBar.removeFromLeft (50).reduced (6));
    globalBypassButton.setBounds (topBar.removeFromLeft (70).reduced (5));
    autoMatchButton.setBounds (topBar.removeFromLeft (65).reduced (5));
    lufsLabel.setBounds (topBar.removeFromRight (130).reduced (4));
    truePeakLabel.setBounds (topBar.removeFromRight (120).reduced (4));

    // Tab strip + reorder buttons
    auto tabRow = area.removeFromTop (40).reduced (8, 4);
    // Reorder buttons on the sides
    if (moveLeftBtn.isVisible())
        moveLeftBtn.setBounds (tabRow.removeFromLeft (28));
    if (moveRightBtn.isVisible())
        moveRightBtn.setBounds (tabRow.removeFromRight (28));
    int tabW = tabRow.getWidth() / tabButtons.size();
    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setBounds (tabRow.getX() + i * tabW, tabRow.getY(), tabW - 2, tabRow.getHeight());

    // Bottom bar
    auto bottomBar = area.removeFromBottom (70);
    masterOutputSlider.setBounds (bottomBar.removeFromRight (100).reduced (4, 2));
    oversamplingCombo.setBounds (bottomBar.removeFromRight (80).reduced (4, 18));

    // Panel area — layout visible knobs in a grid
    auto panelArea = area.reduced (16, 8);

    // Per-stage bypass toggle at top-right of panel
    for (int i = 0; i < stageBypassToggles.size(); ++i)
    {
        if (stageBypassToggles[i]->isVisible())
            stageBypassToggles[i]->setBounds (panelArea.getRight() - 60, panelArea.getY(), 60, 24);
    }

    // Reserve space for bypass toggle and GR meter / FFT
    panelArea.removeFromTop (28);
    panelArea.removeFromBottom (55);  // space for GR meter / FFT when visible

    // ─── Special layout for SAT Multiband ───
    bool isSatMulti = (currentStage == kSatCommon)
        && ((int) processor.getAPVTS().getRawParameterValue ("S4_Sat_Mode")->load() == 1);

    if (isSatMulti)
    {
        layoutSatMultiband (panelArea);
        return;
    }

    // ─── Generic grid layout for all other stages ───
    // Helper: check if control belongs to currently visible stage
    auto isVisible = [&](int controlStage) -> bool
    {
        if (controlStage == kImager) return false; // positioned manually below
        if (controlStage == currentStage) return true;
        if (currentStage == kSatCommon)
        {
            if (controlStage == kSatCommon) return true;
            if (controlStage == kSatSingle) return true; // single mode if we got here
        }
        return false;
    };

    // Count visible controls for this stage
    int visibleKnobs = 0;
    for (int i = 0; i < allSliders.size(); ++i)
        if (isVisible (stageForControl[i])) visibleKnobs++;

    int visibleCombos = 0;
    for (int i = 0; i < allCombos.size(); ++i)
        if (isVisible (comboStage[i])) visibleCombos++;

    int visibleToggles = 0;
    for (int i = 0; i < inlineToggles.size(); ++i)
        if (isVisible (toggleStage[i])) visibleToggles++;

    int totalControls = visibleKnobs + visibleCombos + visibleToggles;
    if (totalControls == 0) return;

    int cols = juce::jmin (totalControls, 8);
    int rows = (totalControls + cols - 1) / cols;
    int cellW = panelArea.getWidth() / cols;
    int cellH = panelArea.getHeight() / juce::jmax (rows, 1);
    int knobH = juce::jmin (cellH - 20, 100);

    int col = 0, row = 0;

    // Layout combos first (Mode at top)
    for (int i = 0; i < allCombos.size(); ++i)
    {
        if (! isVisible (comboStage[i])) continue;
        int x = panelArea.getX() + col * cellW;
        int y = panelArea.getY() + row * cellH;
        comboLabels[i]->setBounds (x, y, cellW, 16);
        allCombos[i]->setBounds (x + 4, y + 20, cellW - 8, 28);
        col++;
        if (col >= cols) { col = 0; row++; }
    }

    // Layout knobs
    for (int i = 0; i < allSliders.size(); ++i)
    {
        if (! isVisible (stageForControl[i])) continue;
        int x = panelArea.getX() + col * cellW;
        int y = panelArea.getY() + row * cellH;
        allLabels[i]->setBounds (x, y, cellW, 16);
        allSliders[i]->setBounds (x + 4, y + 16, cellW - 8, knobH);
        col++;
        if (col >= cols) { col = 0; row++; }
    }

    // Layout inline toggles
    for (int i = 0; i < inlineToggles.size(); ++i)
    {
        if (! isVisible (toggleStage[i])) continue;
        int x = panelArea.getX() + col * cellW;
        int y = panelArea.getY() + row * cellH;
        inlineToggles[i]->setBounds (x + 4, y + 16, cellW - 8, 28);
        col++;
        if (col >= cols) { col = 0; row++; }
    }

    // ─── Position Imager width knobs in LIMITER tab ───
    if (currentStage == 8)
    {
        // Compute Imager section coordinates matching paint()
        auto pa = getLocalBounds().withTop (95).withBottom (getHeight() - 70).reduced (8).toFloat();
        auto ma = pa.reduced (12.0f);
        float fullX = ma.getX();
        float fullY = ma.getY() + 140.0f;
        float fullW = ma.getWidth();
        float fullH = ma.getBottom() - fullY;

        float specW = fullW * 0.48f;
        float lufsW = fullW * 0.18f;
        float specX = fullX + lufsW + 24.0f;
        float sterX = specX + specW + 16.0f;
        float sterW = fullW - (sterX - fullX) - 8.0f;
        float sterY = fullY + 22.0f;
        float sterH = fullH - 28.0f;

        float gCorrH = 10.0f;
        float cardsStartY = sterY + gCorrH + 6.0f;
        float cardsAvailH = (sterY + sterH) - cardsStartY;
        float cardGap = 4.0f;
        float xoverBadgeH = 14.0f;
        float totalGaps = 3.0f * (cardGap + xoverBadgeH + cardGap);
        float cardH = (cardsAvailH - totalGaps) / 4.0f;

        // Position 4 width knobs — right side of each band card
        int imgKnobIdx = 0;
        float yPos2 = cardsStartY;
        for (int i = 0; i < allSliders.size(); ++i)
        {
            if (stageForControl[i] != kImager) continue;
            if (imgKnobIdx >= 4) break;

            float knobX = sterX + sterW - 62.0f;
            float knobY = yPos2 + 2.0f;
            int knobSize = (int) std::min (cardH - 4.0f, 50.0f);

            allSliders[i]->setSliderStyle (juce::Slider::RotaryVerticalDrag);
            allSliders[i]->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 12);
            allSliders[i]->setBounds ((int) knobX, (int) knobY, 54, knobSize);
            allLabels[i]->setBounds ((int) knobX, (int)(knobY - 10), 54, 10);

            yPos2 += cardH;
            if (imgKnobIdx < 3) yPos2 += cardGap + xoverBadgeH + cardGap;
            imgKnobIdx++;
        }
    }
}

void EasyMasterEditor::layoutSatMultiband (juce::Rectangle<int> panelArea)
{
    // ─── Top row: Mode combo ───
    auto topRow = panelArea.removeFromTop (50);
    for (int i = 0; i < allCombos.size(); ++i)
    {
        if (comboStage[i] == kSatCommon && allCombos[i]->isVisible())
        {
            comboLabels[i]->setBounds (topRow.getX(), topRow.getY(), 80, 16);
            allCombos[i]->setBounds (topRow.getX() + 4, topRow.getY() + 18, 130, 28);
        }
    }

    // ─── Xover knobs row (hidden — driven by FFT drag, but keep accessible) ───
    // Hide xover knobs (they'll be controlled by dragging on FFT display)
    for (int i = 0; i < allSliders.size(); ++i)
    {
        if (stageForControl[i] == kSatMulti && allSliders[i]->isVisible())
        {
            auto name = allSliders[i]->getName();
            if (name.contains ("Xover"))
            {
                allSliders[i]->setVisible (false);
                allLabels[i]->setVisible (false);
            }
        }
    }

    // ─── 4 band boxes ───
    panelArea.removeFromTop (4);
    auto bandsArea = panelArea;
    int bandBoxW = bandsArea.getWidth() / 4;
    int bandBoxH = bandsArea.getHeight();

    juce::Colour bandCols[] = {
        juce::Colour (0xFF4488CC), juce::Colour (0xFF44CC88),
        juce::Colour (0xFFCCAA44), juce::Colour (0xFFCC4444)
    };
    juce::String bandNames[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };

    // Find per-band controls: for band b (1-4), multiband combos are at indices
    // relative to their creation order. Band combos: indices 1,2,3,4 in multiband combos
    // Band knobs: groups of 3 (Drive, Out, Blend) per band
    // Band toggles: groups of 2 (Solo, Mute) per band

    // Collect multiband controls by band
    struct BandControls {
        int comboIdx = -1;   // Type combo
        int knobDrive = -1, knobOut = -1, knobBlend = -1;
        int togSolo = -1, togMute = -1;
    };
    std::array<BandControls, 4> bc;

    // Find per-band combos (B1 Type, B2 Type, etc.)
    int bandComboCount = 0;
    for (int i = 0; i < allCombos.size(); ++i)
    {
        if (comboStage[i] != kSatMulti) continue;
        if (allCombos[i]->getName().contains ("_B"))
        {
            int bIdx = bandComboCount; // 0-3
            if (bIdx < 4) bc[(size_t)bIdx].comboIdx = i;
            bandComboCount++;
        }
    }

    // Find per-band knobs (3 per band: Drive, Out, Blend)
    int bandKnobCount = 0;
    for (int i = 0; i < allSliders.size(); ++i)
    {
        if (stageForControl[i] != kSatMulti) continue;
        auto name = allSliders[i]->getName();
        if (name.contains ("_B"))
        {
            int bIdx = bandKnobCount / 3;
            int kIdx = bandKnobCount % 3;
            if (bIdx < 4)
            {
                if (kIdx == 0) bc[(size_t)bIdx].knobDrive = i;
                else if (kIdx == 1) bc[(size_t)bIdx].knobOut = i;
                else bc[(size_t)bIdx].knobBlend = i;
            }
            bandKnobCount++;
        }
    }

    // Find per-band toggles (2 per band: Solo, Mute)
    int bandTogCount = 0;
    for (int i = 0; i < inlineToggles.size(); ++i)
    {
        if (toggleStage[i] != kSatMulti) continue;
        int bIdx = bandTogCount / 2;
        int tIdx = bandTogCount % 2;
        if (bIdx < 4)
        {
            if (tIdx == 0) bc[(size_t)bIdx].togSolo = i;
            else bc[(size_t)bIdx].togMute = i;
        }
        bandTogCount++;
    }

    // Layout each band box
    for (int b = 0; b < 4; ++b)
    {
        auto box = juce::Rectangle<int> (
            bandsArea.getX() + b * bandBoxW + 3,
            bandsArea.getY(),
            bandBoxW - 6,
            bandBoxH);

        int y = box.getY();

        // Type combo at top of box
        if (bc[b].comboIdx >= 0)
        {
            int ci = bc[b].comboIdx;
            comboLabels[ci]->setBounds (box.getX(), y, box.getWidth(), 14);
            allCombos[ci]->setBounds (box.getX() + 2, y + 14, box.getWidth() - 4, 26);
        }
        y += 44;

        // 3 knobs in a row
        int knobW = box.getWidth() / 3;
        int knobH = juce::jmin (box.getHeight() - 100, 90);
        int knobs[] = { bc[b].knobDrive, bc[b].knobOut, bc[b].knobBlend };
        for (int k = 0; k < 3; ++k)
        {
            int ki = knobs[k];
            if (ki >= 0)
            {
                int kx = box.getX() + k * knobW;
                allLabels[ki]->setBounds (kx, y, knobW, 14);
                allSliders[ki]->setBounds (kx + 2, y + 14, knobW - 4, knobH);
            }
        }
        y += knobH + 18;

        // Solo/Mute toggles
        int halfW = box.getWidth() / 2;
        if (bc[b].togSolo >= 0)
            inlineToggles[bc[b].togSolo]->setBounds (box.getX(), y, halfW, 24);
        if (bc[b].togMute >= 0)
            inlineToggles[bc[b].togMute]->setBounds (box.getX() + halfW, y, halfW, 24);
    }
}

void EasyMasterEditor::timerCallback()
{
    auto* om = processor.getEngine().getOutputMeter();
    if (om)
    {
        float lufs = om->getShortTermLUFS();
        float tp = om->getTruePeak();
        lufsLabel.setText (lufs > -100.f ? juce::String (lufs, 1) + " LUFS" : "--.-- LUFS", juce::dontSendNotification);
        truePeakLabel.setText (tp > -100.f ? "TP: " + juce::String (tp, 1) + " dB" : "TP: --.-- dB", juce::dontSendNotification);
    }

    // Check if SAT Mode changed (for auto-updating visibility)
    if (currentStage == kSatCommon)
    {
        int currentMode = (int) processor.getAPVTS().getRawParameterValue ("S4_Sat_Mode")->load();
        static int lastMode = -1;
        if (currentMode != lastMode)
        {
            lastMode = currentMode;
            updateSatModeVisibility();
        }
    }

    repaint();  // for meters + FFT
}

// ─── Frequency ↔ X position helpers (log scale, 20 Hz – 20 kHz) ───

float EasyMasterEditor::freqToX (float freq, float x, float w) const
{
    float logMin = std::log10 (20.0f);
    float logMax = std::log10 (20000.0f);
    float logF = std::log10 (juce::jlimit (20.0f, 20000.0f, freq));
    return x + w * (logF - logMin) / (logMax - logMin);
}

float EasyMasterEditor::xToFreq (float xPos, float x, float w) const
{
    float logMin = std::log10 (20.0f);
    float logMax = std::log10 (20000.0f);
    float norm = (xPos - x) / w;
    norm = juce::jlimit (0.0f, 1.0f, norm);
    return std::pow (10.0f, logMin + norm * (logMax - logMin));
}

// ─── Mouse handlers for draggable crossover lines ───

void EasyMasterEditor::mouseDown (const juce::MouseEvent& e)
{
    // ─── Imager solo buttons (LIMITER stage) ───
    if (currentStage == 8)
    {
        auto* om = processor.getEngine().getOutputMeter();
        if (om)
        {
            auto pos = e.position;

            // Check solo buttons
            for (int b = 0; b < 4; ++b)
            {
                if (imgSoloBtnRects[(size_t)b].contains (pos))
                {
                    int current = om->getSoloedBand();
                    om->setSoloedBand (current == b ? -1 : b);
                    repaint();
                    return;
                }
            }

            // Check imager crossover badges (between band cards)
            if (imagerDisplayArea.getWidth() > 0)
            {
                float cardsAvailH = imagerDisplayArea.getHeight();
                float cardGap = 4.0f;
                float xoverBadgeH = 14.0f;
                float totalGaps = 3.0f * (cardGap + xoverBadgeH + cardGap);
                float cardH = (cardsAvailH - totalGaps) / 4.0f;

                draggingImgXover = -1;
                float yCheck = imagerDisplayArea.getY();
                for (int xo = 0; xo < 3; ++xo)
                {
                    yCheck += cardH + cardGap;
                    float badgeCenterY = yCheck + xoverBadgeH * 0.5f;
                    if (std::abs (pos.y - badgeCenterY) < 12.0f &&
                        pos.x >= imagerDisplayArea.getX() &&
                        pos.x <= imagerDisplayArea.getRight())
                    {
                        draggingImgXover = xo;
                        return;
                    }
                    yCheck += xoverBadgeH + cardGap;
                }
            }
        }
    }

    // ─── Output EQ node dragging (stage 4) ───
    if (currentStage == 4)
    {
        auto pos = e.position;
        if (eqDisplayArea.getWidth() > 0 && eqDisplayArea.expanded (10).contains (pos))
        {
            auto* outEQ = dynamic_cast<OutputEQStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::OutputEQ));
            if (outEQ)
            {
                draggingEQNode = -1;
                float bestDist = 20.0f;
                for (int b = 0; b < OutputEQStage::NUM_BANDS; ++b)
                {
                    auto bi = outEQ->getBandInfo (b);
                    float nodeX = freqToX (bi.freq, eqDisplayArea.getX(), eqDisplayArea.getWidth());
                    double nodeMag = outEQ->getMagnitudeAtFreq ((double) bi.freq);
                    float nodeY = eqDisplayArea.getY() + eqDisplayArea.getHeight() * 0.5f
                                  - (float)(nodeMag / eqDbRange) * (eqDisplayArea.getHeight() * 0.5f);
                    float dist = std::sqrt ((pos.x - nodeX) * (pos.x - nodeX) + (pos.y - nodeY) * (pos.y - nodeY));
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        draggingEQNode = b;
                    }
                }
                if (draggingEQNode >= 0) return;
            }
        }
    }

    // ─── SAT crossover dragging ───
    if (currentStage == kSatCommon)
    {
        int satMode = (int) processor.getAPVTS().getRawParameterValue ("S4_Sat_Mode")->load();
        if (satMode == 1)
        {
            auto pos = e.position;
            if (fftDisplayArea.contains (pos))
            {
                float specX = fftDisplayArea.getX() + 8.0f;
                float specW = fftDisplayArea.getWidth() - 16.0f;

                juce::String xoverParams[] = { "S4_Sat_Xover1", "S4_Sat_Xover2", "S4_Sat_Xover3" };
                float bestDist = 12.0f;
                draggingXover = -1;

                for (int xo = 0; xo < 3; ++xo)
                {
                    float freq = processor.getAPVTS().getRawParameterValue (xoverParams[xo])->load();
                    float xPos = freqToX (freq, specX, specW);
                    float dist = std::abs (pos.x - xPos);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        draggingXover = xo;
                    }
                }
            }
        }
    }
}

void EasyMasterEditor::mouseDrag (const juce::MouseEvent& e)
{
    // ─── Output EQ node drag ───
    if (draggingEQNode >= 0)
    {
        if (eqDisplayArea.getWidth() <= 0) return;
        float sx = eqDisplayArea.getX(), sw = eqDisplayArea.getWidth();
        float sy = eqDisplayArea.getY(), sh = eqDisplayArea.getHeight();

        // X → frequency (log scale)
        float newFreq = xToFreq (e.position.x, sx, sw);

        // Y → gain (linear)
        float normY = 1.0f - (e.position.y - sy) / sh; // 0=bottom, 1=top
        float newGain = (normY - 0.5f) * 2.0f * eqDbRange;
        newGain = juce::jlimit (-12.0f, 12.0f, newGain);

        // Band param IDs
        juce::String freqIDs[] = { "S5_EQ2_LowShelf_Freq", "S5_EQ2_LowMid_Freq", "S5_EQ2_Mid_Freq", "S5_EQ2_HighMid_Freq", "S5_EQ2_HighShelf_Freq" };
        juce::String gainIDs[] = { "S5_EQ2_LowShelf_Gain", "S5_EQ2_LowMid_Gain", "S5_EQ2_Mid_Gain", "S5_EQ2_HighMid_Gain", "S5_EQ2_HighShelf_Gain" };

        if (auto* fp = processor.getAPVTS().getParameter (freqIDs[draggingEQNode]))
            fp->setValueNotifyingHost (fp->convertTo0to1 (newFreq));
        if (auto* gp = processor.getAPVTS().getParameter (gainIDs[draggingEQNode]))
            gp->setValueNotifyingHost (gp->convertTo0to1 (newGain));

        repaint();
        return;
    }

    // ─── Imager crossover drag (horizontal = freq change) ───
    if (draggingImgXover >= 0)
    {
        // Map horizontal position to frequency (log scale across the imager width)
        float normX = (e.position.x - imagerDisplayArea.getX()) / imagerDisplayArea.getWidth();
        normX = juce::jlimit (0.0f, 1.0f, normX);

        float logMin = std::log10 (20.0f), logMax = std::log10 (20000.0f);
        float newFreq = std::pow (10.0f, logMin + normX * (logMax - logMin));

        juce::String imgXoverParams[] = { "IMG_Xover1", "IMG_Xover2", "IMG_Xover3" };

        // Get all current freqs
        float freqs[3];
        for (int i = 0; i < 3; ++i)
            freqs[i] = processor.getAPVTS().getRawParameterValue (imgXoverParams[i])->load();

        freqs[draggingImgXover] = newFreq;

        // Enforce ordering with min spacing
        float minSpacing = 50.0f;
        if (draggingImgXover == 0)
            freqs[0] = juce::jlimit (20.0f, freqs[1] - minSpacing, freqs[0]);
        else if (draggingImgXover == 1)
            freqs[1] = juce::jlimit (freqs[0] + minSpacing, freqs[2] - minSpacing, freqs[1]);
        else
            freqs[2] = juce::jlimit (freqs[1] + minSpacing, 16000.0f, freqs[2]);

        if (auto* param = processor.getAPVTS().getParameter (imgXoverParams[draggingImgXover]))
            param->setValueNotifyingHost (param->convertTo0to1 (freqs[draggingImgXover]));

        repaint();
        return;
    }

    // ─── SAT crossover drag ───
    if (draggingXover >= 0)
    {
        float specX = fftDisplayArea.getX() + 8.0f;
        float specW = fftDisplayArea.getWidth() - 16.0f;
        float newFreq = xToFreq (e.position.x, specX, specW);

        juce::String xoverParams[] = { "S4_Sat_Xover1", "S4_Sat_Xover2", "S4_Sat_Xover3" };

        float freqs[3];
        for (int i = 0; i < 3; ++i)
            freqs[i] = processor.getAPVTS().getRawParameterValue (xoverParams[i])->load();

        freqs[draggingXover] = newFreq;

        float minSpacing = 50.0f;
        if (draggingXover == 0)
            freqs[0] = juce::jlimit (20.0f, freqs[1] - minSpacing, freqs[0]);
        else if (draggingXover == 1)
            freqs[1] = juce::jlimit (freqs[0] + minSpacing, freqs[2] - minSpacing, freqs[1]);
        else
            freqs[2] = juce::jlimit (freqs[1] + minSpacing, 16000.0f, freqs[2]);

        if (auto* param = processor.getAPVTS().getParameter (xoverParams[draggingXover]))
            param->setValueNotifyingHost (param->convertTo0to1 (freqs[draggingXover]));

        repaint();
    }
}

void EasyMasterEditor::mouseUp (const juce::MouseEvent&)
{
    draggingXover = -1;
    draggingImgXover = -1;
    draggingEQNode = -1;
    repaint();
}
