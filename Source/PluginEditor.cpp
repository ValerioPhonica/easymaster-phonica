#include "PluginProcessor.h"

// ═══════════════════════════════════════════════════════════════
//  EASY MASTER EDITOR — Minimal working version
//  No fancy drag & drop, just working controls that don't crash
// ═══════════════════════════════════════════════════════════════

EasyMasterEditor::EasyMasterEditor (EasyMasterProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&customLnF);
    setSize (1200, 850);
    setResizable (true, true);
    setResizeLimits (900, 650, 1920, 1200);

    // ─── Preset bar ───────────────────────────────────────
    addAndMakeVisible (presetSelector);
    presetSelector.setTextWhenNothingSelected ("-- Select Preset --");
    {
        auto presets = processor.getPresetManager().getPresetList();
        for (int i = 0; i < presets.size(); ++i)
        {
            if (presets[i] == "INIT") continue; // don't show INIT in preset list
            presetSelector.addItem (presets[i], i + 1);
        }
    }
    presetSelector.onChange = [this]
    {
        auto name = presetSelector.getText();
        if (name.isNotEmpty())
            processor.getPresetManager().loadPreset (name);
    };

    addAndMakeVisible (savePresetButton);
    savePresetButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1A3355));
    savePresetButton.onClick = [this]
    {
        auto dlg = std::make_shared<juce::AlertWindow> ("Save Preset", "Enter a name for your preset:", juce::MessageBoxIconType::NoIcon);
        dlg->addTextEditor ("name", presetSelector.getText().isEmpty() ? "" : presetSelector.getText(), "Preset Name");
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
                    {
                        if (presets[i] == "INIT") continue;
                        presetSelector.addItem (presets[i], i + 1);
                    }
                    for (int i = 0; i < presets.size(); ++i)
                    {
                        if (presets[i] == name)
                        { presetSelector.setSelectedId (i + 1, juce::dontSendNotification); break; }
                    }
                }
            }
        }), false);  // false = don't delete, shared_ptr handles lifetime
    };

    addAndMakeVisible (deletePresetButton);
    deletePresetButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF3A2222));
    deletePresetButton.onClick = [this]
    {
        auto name = presetSelector.getText();
        if (name.isEmpty()) return;
        if (juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
            "Delete Preset", "Delete \"" + name + "\"?", "Delete", "Cancel"))
        {
            processor.getPresetManager().deletePreset (name);
            presetSelector.clear (juce::dontSendNotification);
            auto presets = processor.getPresetManager().getPresetList();
            for (int i = 0; i < presets.size(); ++i)
            {
                if (presets[i] == "INIT") continue;
                presetSelector.addItem (presets[i], i + 1);
            }
            presetSelector.setSelectedId (0, juce::dontSendNotification);
            presetSelector.setText ("", juce::dontSendNotification);
        }
    };

    addAndMakeVisible (initButton);
    initButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF3A1E1E));
    initButton.onClick = [this]
    {
        processor.getPresetManager().loadInit();
        presetSelector.setSelectedId (0, juce::dontSendNotification);
        presetSelector.setText ("", juce::dontSendNotification);
    };

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

    // ─── Reference track controls ───
    addAndMakeVisible (loadRefButton);
    loadRefButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1A3350));
    loadRefButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Load Reference Track",
            juce::File{}, "*.wav;*.aiff;*.aif;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                processor.loadReferenceFile (file);
                refNameLabel.setText (processor.getRefFileName(), juce::dontSendNotification);
            }
        });
    };

    addAndMakeVisible (abButton);
    abButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF2A2240));
    abButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFE94560));
    abButton.setClickingTogglesState (true);
    abButton.onClick = [this]
    {
        processor.setABActive (abButton.getToggleState());
    };

    addAndMakeVisible (refNameLabel);
    refNameLabel.setFont (juce::Font (10.0f));
    refNameLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF889999));
    refNameLabel.setJustificationType (juce::Justification::centredLeft);

    // M/S EQ edit toggle
    addAndMakeVisible (eqMsToggle);
    eqMsToggle.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1A3350));
    eqMsToggle.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF55AACC));
    eqMsToggle.setVisible (false); // only shown in OutputEQ when M/S is active
    eqMsToggle.onClick = [this]
    {
        eqEditSide = !eqEditSide;
        eqMsToggle.setButtonText (eqEditSide ? "EDIT: SIDE" : "EDIT: MID");
        eqMsToggle.setToggleState (eqEditSide, juce::dontSendNotification);
        // Re-show stage to update knob visibility
        for (int t = 0; t < 9; ++t)
            if (stageTypeForTab[(size_t) t] == 4) { showStage (t); break; }
    };

    addAndMakeVisible (lufsLabel);
    lufsLabel.setFont (juce::Font (15.0f, juce::Font::bold));
    lufsLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF55DDEE));
    lufsLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (truePeakLabel);
    truePeakLabel.setFont (juce::Font (13.0f));
    truePeakLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFAABBCC));
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
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFE94560));
        s->setVisible (false);
        addAndMakeVisible (s);
        allSliders.add (s);

        auto* lbl = new juce::Label ({}, label.toUpperCase());
        lbl->setFont (juce::Font (9.0f));
        lbl->setColour (juce::Label::textColourId, juce::Colour (0xFF889AAB));
        lbl->setJustificationType (juce::Justification::centred);
        lbl->setInterceptsMouseClicks (false, false);
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

        auto* lbl = new juce::Label ({}, label.toUpperCase());
        lbl->setFont (juce::Font (9.0f));
        lbl->setColour (juce::Label::textColourId, juce::Colour (0xFF889AAB));
        lbl->setJustificationType (juce::Justification::centred);
        lbl->setInterceptsMouseClicks (false, false);
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
    addKnob ("S1_Input_Balance", "Balance", 0);
    addToggle ("S1_Input_DC", "DC Filter", 0);
    addToggle ("S1_Input_PhaseL", "Phase Inv L", 0);
    addToggle ("S1_Input_PhaseR", "Phase Inv R", 0);
    addToggle ("S1_Input_Mono", "Mono Check", 0);
    addCombo ("S1_Input_Crossover_Mode", "Phase", 999);   // hidden

    // ─── STAGE 1: PULTEC EQ ──────────────────────────────
    static constexpr int kPultecTop = 1;   // combo + EQP-1A on row 1
    static constexpr int kPultecMEQ = 15;  // MEQ-5 on row 2
    addCombo ("S2_EQ_MS", "Channel", kPultecTop);
    pultecComboStartIdx = allCombos.size(); // first swappable Pultec combo
    addCombo ("S2_EQ_LowBoost_Freq", "Low Freq", kPultecTop);
    addCombo ("S2_EQ_HighBoost_Freq", "High Freq", kPultecTop);
    addCombo ("S2_EQ_HighAtten_Freq", "Atten Sel", kPultecTop);
    pultecKnobStartIdx = allSliders.size(); // first Pultec knob
    addKnob ("S2_EQ_LowBoost_Gain", "Low Boost", kPultecTop);
    addKnob ("S2_EQ_LowAtten_Gain", "Low Atten", kPultecTop);
    addKnob ("S2_EQ_HighBoost_Gain", "Hi Boost", kPultecTop);
    addKnob ("S2_EQ_HighAtten_Gain", "Hi Atten", kPultecTop);
    addKnob ("S2_EQ_HighAtten_BW", "Bandwidth", kPultecTop);
    // MEQ-5
    addKnob ("S2_EQ_LowMid_Freq", "LM Freq", kPultecMEQ);
    addKnob ("S2_EQ_LowMid_Gain", "LM Peak", kPultecMEQ);
    addKnob ("S2_EQ_MidDip_Freq", "Dip Freq", kPultecMEQ);
    addKnob ("S2_EQ_MidDip_Gain", "Dip", kPultecMEQ);
    addKnob ("S2_EQ_HighMid_Freq", "HM Freq", kPultecMEQ);
    addKnob ("S2_EQ_HighMid_Gain", "HM Peak", kPultecMEQ);

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
    addCombo ("S4_Sat_MS", "Channel", kSatCommon);
    addCombo ("S4_Sat_Mode", "Mode", kSatCommon);
    satComboStartIdx = allCombos.size(); // first swappable SAT combo (Type)
    addCombo ("S4_Sat_Type", "Type", kSatSingle);
    satKnobStartIdx = allSliders.size(); // first swappable SAT knob (Drive)
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
    addCombo ("S5_EQ2_MS", "Channel", 4);
    eqKnobStartIdx = allSliders.size(); // remember where EQ knobs start
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
    addCombo ("S6_Filter_MS", "Channel", 5);
    addCombo ("S6_Filter_Mode", "Phase", 5);
    filterToggleStartIdx = inlineToggles.size();
    addToggle ("S6_HP_On", "HP On", 5);
    filterKnobStartIdx = allSliders.size();
    addKnob ("S6_HP_Freq", "HP Freq", 5);
    filterComboStartIdx = allCombos.size();
    addCombo ("S6_HP_Slope", "HP Slope", 5);
    addToggle ("S6_LP_On", "LP On", 5);
    addKnob ("S6_LP_Freq", "LP Freq", 5);
    addCombo ("S6_LP_Slope", "LP Slope", 5);

    // ─── STAGE 6: DYNAMIC RESONANCE ─────────────────────
    addCombo ("S6B_DynEQ_Mode", "Mode", 6);
    addKnob ("S6B_DynEQ_Depth", "Depth", 6);
    addKnob ("S6B_DynEQ_Sensitivity", "Selectivity", 6);
    addKnob ("S6B_DynEQ_Sharpness", "Sharpness", 6);
    addKnob ("S6B_DynEQ_Speed", "Speed", 6);
    addKnob ("S6B_DynEQ_LowFreq", "Low Freq", 6);
    addKnob ("S6B_DynEQ_HighFreq", "High Freq", 6);

    // ─── STAGE 7: CLIPPER ────────────────────────────────
    addCombo ("S7_Clipper_Style", "Mode", 7);
    addKnob ("S7_Clipper_Input", "Input", 7);
    addKnob ("S7_Clipper_Ceiling", "Ceiling", 7);
    addKnob ("S7_Clipper_Shape", "Shape", 7);
    addKnob ("S7_Clipper_Transient", "Transient", 7);
    addKnob ("S7_Clipper_Output", "Output", 7);
    addKnob ("S7_Clipper_Mix", "Mix", 7);

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
    masterOutputSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    masterOutputSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    masterOutputSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xFFAABBCC));
    masterOutputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "Master_Output_Gain", masterOutputSlider);

    // ─── Oversampling ────────────────────────────────────
    addAndMakeVisible (oversamplingCombo);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("Oversampling")))
        for (int i = 0; i < p->choices.size(); ++i)
            oversamplingCombo.addItem (p->choices[i], i + 1);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "Oversampling", oversamplingCombo);

    // ─── Dither ───────────────────────────────────────────
    addAndMakeVisible (ditherCombo);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("Dither_Mode")))
        for (int i = 0; i < p->choices.size(); ++i)
            ditherCombo.addItem (p->choices[i], i + 1);
    ditherAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "Dither_Mode", ditherCombo);

    // ─── Analyzer Speed ──────────────────────────────────
    addAndMakeVisible (analyzerSpeedCombo);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("Analyzer_Speed")))
        for (int i = 0; i < p->choices.size(); ++i)
            analyzerSpeedCombo.addItem (p->choices[i], i + 1);
    analyzerSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, "Analyzer_Speed", analyzerSpeedCombo);

    // ─── Show Ref Spectrum toggle ──────────────────────────
    addAndMakeVisible (showRefSpecToggle);
    showRefSpecToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (0xFF55DDEE));
    showRefSpecToggle.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFF55DDEE));
    showRefSpecAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "Show_Ref_Spectrum", showRefSpecToggle);

    // Show first stage
    showStage (0);
    startTimerHz (30);
}

EasyMasterEditor::~EasyMasterEditor() { stopTimer(); setLookAndFeel (nullptr); }

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
        if (controlStage == kEQSide) return false;
        if (controlStage == stageType) return true;
        // Pultec: show both EQP-1A (stage 1) and MEQ-5 (kPultecMEQ)
        if (stageType == 1 && controlStage == kPultecMEQ) return true;
        if (isSat)
        {
            if (controlStage == kSatCommon) return true;
            if (controlStage == kSatSingle && satMode == 0) return true;
            if (controlStage == kSatMulti  && satMode == 1) return true;
        }
        if (stageType == 8 && controlStage == kImager) return true;
        return false;
    };

    // Hide M/S toggle (removed)
    eqMsToggle.setVisible (false);

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
    // ─── Background ───
    juce::ColourGradient bgGrad (juce::Colour (0xFF0E0E22), 0, 0,
                                 juce::Colour (0xFF161632), 0, (float) getHeight(), false);
    g.setGradientFill (bgGrad);
    g.fillAll();

    // ─── Top bar ───
    {
        juce::ColourGradient topGrad (juce::Colour (0xFF1A2644), 0, 0,
                                      juce::Colour (0xFF141E38), 0, 50, false);
        g.setGradientFill (topGrad);
        g.fillRect (0, 0, getWidth(), 50);
        // Subtle bottom line
        g.setColour (juce::Colour (0xFF2A3A5A));
        g.fillRect (0, 49, getWidth(), 1);
    }

    // Logo
    g.setColour (juce::Colour (0xFFE94560));
    g.setFont (juce::Font (24.0f, juce::Font::bold));
    g.drawText ("EASY MASTER", 14, 6, 180, 30, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xFF556688));
    g.setFont (juce::Font (9.0f));
    g.drawText ("by Phonica School", 14, 34, 140, 12, juce::Justification::centredLeft);

    // ─── Bottom bar ───
    {
        juce::ColourGradient botGrad (juce::Colour (0xFF141428), 0, (float)(getHeight() - 70),
                                      juce::Colour (0xFF0E0E20), 0, (float) getHeight(), false);
        g.setGradientFill (botGrad);
        g.fillRect (0, getHeight() - 70, getWidth(), 70);
        g.setColour (juce::Colour (0xFF2A2A48));
        g.fillRect (0, getHeight() - 70, getWidth(), 1);
    }

    // ─── Panel area ───
    auto panelArea = getLocalBounds().withTop (95).withBottom (getHeight() - 70).reduced (8).toFloat();
    {
        juce::ColourGradient panelGrad (juce::Colour (0xFF131330), panelArea.getX(), panelArea.getY(),
                                        juce::Colour (0xFF101028), panelArea.getX(), panelArea.getBottom(), false);
        g.setGradientFill (panelGrad);
        g.fillRoundedRectangle (panelArea, 8.0f);
        g.setColour (juce::Colour (0xFF2A2A55).withAlpha (0.6f));
        g.drawRoundedRectangle (panelArea, 8.0f, 1.0f);
    }

    // ─── Stage-specific GR meter (shown inside the panel area) ───
    if (tabButtons.size() > 0)
    {
        // ─── Draw stage section header ───
        juce::StringArray stageDisplayNames = { "INPUT", "PULTEC EQ", "COMPRESSOR",
            "SATURATION", "OUTPUT EQ", "FILTER", "DYNAMIC RESONANCE", "CLIPPER", "LIMITER" };

        if (currentStage >= 0 && currentStage < stageDisplayNames.size())
        {
            // Thin separator line under tabs
            g.setColour (juce::Colour (0xFF2A2A50));
            g.fillRect ((int) panelArea.getX() + 12, (int) panelArea.getY() + 2,
                        (int) panelArea.getWidth() - 24, 1);
        }

        auto meterArea = panelArea.reduced (12.0f);
        float meterY = meterArea.getBottom() - 50.0f;
        float meterW = meterArea.getWidth();
        float meterX = meterArea.getX();

        // ─── INPUT: Reference spectrum comparison (stage 0) ───
        if (currentStage == 0)
        {
            // Full-size spectrum + waveform display
            float dispX = meterArea.getX();
            float dispY = meterArea.getY() + 120.0f; // below knobs
            float totalH = meterArea.getHeight() - 130.0f;
            if (totalH < 150) totalH = 300;

            // Split: spectrum 58%, waveform 38%, gap 4%
            float dispW = meterArea.getWidth();
            float specDispH = totalH * 0.58f;
            float waveH = totalH * 0.38f;
            float dispH = specDispH;

            // Background
            g.setColour (juce::Colour (0xFF0A0A18));
            g.fillRoundedRectangle (dispX, dispY, dispW, dispH, 8.0f);
            g.setColour (juce::Colour (0xFF2A2A50));
            g.drawRoundedRectangle (dispX, dispY, dispW, dispH, 8.0f, 0.5f);

            g.setColour (juce::Colour (0xFF667788));
            g.setFont (juce::Font (9.0f));
            g.drawText ("SPECTRUM COMPARISON", dispX + 10, dispY + 4, 200, 12, juce::Justification::centredLeft);

            if (processor.hasReference())
            {
                g.setColour (juce::Colour (0xFF55DDEE));
                g.drawText ("REF: " + processor.getRefFileName(), dispX + dispW - 200, dispY + 4, 190, 12, juce::Justification::centredRight);
            }

            float specX = dispX + 34.0f;
            float specY2 = dispY + 20.0f;
            float specW = dispW - 40.0f;
            float specH = dispH - 40.0f;
            float dbMin = -60.0f, dbMax = 3.0f;
            float dbRange = dbMax - dbMin;

            // dB grid
            g.setColour (juce::Colour (0xFF1A1A35));
            for (float db = -50.0f; db <= 0.0f; db += 10.0f)
            {
                float yy = specY2 + specH * (1.0f - (db - dbMin) / dbRange);
                g.drawHorizontalLine ((int) yy, specX, specX + specW);
            }
            // 0 dB line brighter
            g.setColour (juce::Colour (0xFF2A2A50));
            float yZero = specY2 + specH * (1.0f - (0.0f - dbMin) / dbRange);
            g.drawHorizontalLine ((int) yZero, specX, specX + specW);

            // dB labels
            g.setColour (juce::Colour (0xFF445566));
            g.setFont (juce::Font (8.0f));
            for (float db : { -50.0f, -40.0f, -30.0f, -20.0f, -10.0f, 0.0f })
            {
                float yy = specY2 + specH * (1.0f - (db - dbMin) / dbRange);
                g.drawText (juce::String ((int) db), (int)(dispX + 2), (int)(yy - 5), 30, 10, juce::Justification::centredRight);
            }

            // Freq grid + labels
            float freqs[] = { 30, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
            g.setColour (juce::Colour (0xFF1A1A35));
            for (float f : freqs)
            {
                float xPos = specX + specW * (std::log10 (f / 20.0f) / std::log10 (20000.0f / 20.0f));
                g.drawVerticalLine ((int) xPos, specY2, specY2 + specH);
            }
            g.setColour (juce::Colour (0xFF445566));
            g.setFont (juce::Font (7.0f));
            for (float f : freqs)
            {
                float xPos = specX + specW * (std::log10 (f / 20.0f) / std::log10 (20000.0f / 20.0f));
                juce::String label = f >= 1000 ? juce::String ((int)(f / 1000)) + "k" : juce::String ((int) f);
                g.drawText (label, (int)(xPos - 12), (int)(specY2 + specH + 2), 24, 10, juce::Justification::centred);
            }

            // ─── Spectrum: use OutputMeter FFT (same as limiter display) ───
            auto* om = processor.getEngine().getOutputMeter();
            if (om)
            {
                om->computeFFTMagnitudes();
                auto& midMags  = om->getMidMagnitudes();
                auto& sideMags = om->getSideMagnitudes();
                float sr = (float) processor.getSampleRate();
                if (sr <= 0) sr = 48000.0f;
                int fftHalf = OutputMeter::fftSize / 2;

                // Draw spectrum curve (0..1 normalized, same as limiter)
                auto drawOMSpec = [&](const std::array<float, OutputMeter::fftSize / 2>& mags,
                                     juce::Colour strokeCol, juce::Colour fillCol, float strokeW)
                {
                    juce::Path path;
                    bool started = false;
                    for (int i = 1; i < fftHalf; ++i)
                    {
                        float freq = (float) i * sr / (float) OutputMeter::fftSize;
                        if (freq < 20.0f || freq > 20000.0f) continue;
                        float xPos = freqToX (freq, specX, specW);
                        float yPos = specY2 + specH - mags[(size_t) i] * specH;
                        yPos = juce::jlimit (specY2, specY2 + specH, yPos);
                        if (!started) { path.startNewSubPath (xPos, yPos); started = true; }
                        else path.lineTo (xPos, yPos);
                    }
                    if (started)
                    {
                        juce::Path fill = path;
                        fill.lineTo (specX + specW, specY2 + specH);
                        fill.lineTo (specX, specY2 + specH);
                        fill.closeSubPath();
                        g.setColour (fillCol);
                        g.fillPath (fill);
                        g.setColour (strokeCol);
                        g.strokePath (path, juce::PathStrokeType (strokeW));
                    }
                };

                // Master Mid (orange) + Side (brown)
                drawOMSpec (midMags,  juce::Colour (0xFFE9A045).withAlpha (0.85f), juce::Colour (0xFFE9A045).withAlpha (0.10f), 1.5f);
                drawOMSpec (sideMags, juce::Colour (0xFFBB7722).withAlpha (0.70f), juce::Colour (0xFFBB7722).withAlpha (0.06f), 1.0f);
            }

            // Reference spectrum — same direct bin-plotting as master
            bool showRef = processor.getAPVTS().getRawParameterValue ("Show_Ref_Spectrum")->load() > 0.5f;
            if (showRef && processor.hasReference() && processor.isRefSpectrumReady())
            {
                auto& refMidMags  = processor.getRefMidSpectrum();
                auto& refSideMags = processor.getRefSideSpectrum();
                float sr = (float) processor.getSampleRate();
                if (sr <= 0) sr = 48000.0f;
                int halfSize = EasyMasterProcessor::REF_FFT_HALF;

                // Same approach as master: direct bin plotting, dB→0..1 conversion
                auto drawRefDirect = [&](const std::array<float, EasyMasterProcessor::REF_FFT_HALF>& mags,
                                         juce::Colour strokeCol, juce::Colour fillCol, float strokeW)
                {
                    juce::Path path;
                    bool started = false;
                    float binHz = sr / (float) EasyMasterProcessor::REF_FFT_SIZE;
                    for (int i = 1; i < halfSize; ++i)
                    {
                        float freq = (float) i * binHz;
                        if (freq < 20.0f || freq > 20000.0f) continue;
                        float xPos = freqToX (freq, specX, specW);
                        // Convert dB to 0..1 (same scale as OutputMeter)
                        float norm = juce::jmap (juce::jlimit (-80.0f, 0.0f, mags[(size_t) i]), -80.0f, 0.0f, 0.0f, 1.0f);
                        float yPos = juce::jlimit (specY2, specY2 + specH, specY2 + specH - norm * specH);
                        if (!started) { path.startNewSubPath (xPos, yPos); started = true; }
                        else path.lineTo (xPos, yPos);
                    }
                    if (started)
                    {
                        juce::Path fill = path;
                        fill.lineTo (specX + specW, specY2 + specH);
                        fill.lineTo (specX, specY2 + specH);
                        fill.closeSubPath();
                        g.setColour (fillCol);
                        g.fillPath (fill);
                        g.setColour (strokeCol);
                        g.strokePath (path, juce::PathStrokeType (strokeW));
                    }
                };

                drawRefDirect (refMidMags,  juce::Colour (0xFF55DDEE).withAlpha (0.85f), juce::Colour (0xFF55DDEE).withAlpha (0.06f), 1.5f);
                drawRefDirect (refSideMags, juce::Colour (0xFF2299AA).withAlpha (0.70f), juce::Colour (0xFF2299AA).withAlpha (0.04f), 1.0f);
            }

            // Legend
            g.setFont (juce::Font (9.0f));
            float lx = specX;
            g.setColour (juce::Colour (0xFFE9A045));
            g.fillRect (lx, specY2 + 4, 12.0f, 2.0f);
            g.drawText ("M MID", (int)(lx + 14), (int)specY2, 40, 12, juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xFFBB7722));
            g.fillRect (lx + 58, specY2 + 4, 12.0f, 2.0f);
            g.drawText ("M SIDE", (int)(lx + 72), (int)specY2, 42, 12, juce::Justification::centredLeft);
            if (showRef && processor.hasReference())
            {
                g.setColour (juce::Colour (0xFF55DDEE));
                g.fillRect (lx + 120, specY2 + 4, 12.0f, 2.0f);
                g.drawText ("R MID", (int)(lx + 134), (int)specY2, 40, 12, juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xFF2299AA));
                g.fillRect (lx + 178, specY2 + 4, 12.0f, 2.0f);
                g.drawText ("R SIDE", (int)(lx + 192), (int)specY2, 42, 12, juce::Justification::centredLeft);
            }

            // Correlation meter
            auto* inp = dynamic_cast<InputStage*> (processor.getEngine().getStage (ProcessingStage::StageID::Input));
            if (inp)
            {
                float corr = inp->getCorrelation();
                float corrBarW = 120.0f;
                float corrBarX = dispX + dispW - corrBarW - 10;
                float corrBarY = dispY + dispH - 14;

                g.setColour (juce::Colour (0xFF1A1A35));
                g.fillRoundedRectangle (corrBarX, corrBarY, corrBarW, 8, 3);

                float corrNorm = (corr + 1.0f) * 0.5f; // -1..1 → 0..1
                auto corrCol = corr > 0.5f ? juce::Colour (0xFF44CC88) :
                               corr > 0.0f ? juce::Colour (0xFFCCAA22) :
                                             juce::Colour (0xFFE94560);
                g.setColour (corrCol);
                g.fillRoundedRectangle (corrBarX, corrBarY, corrBarW * corrNorm, 8, 3);

                g.setColour (juce::Colour (0xFF667788));
                g.setFont (juce::Font (8.0f));
                g.drawText ("CORR: " + juce::String (corr, 2), corrBarX - 55, corrBarY - 1, 52, 10, juce::Justification::centredRight);
            }

            // ═══════════════════════════════════════════════════
            // REFERENCE WAVEFORM DISPLAY (below spectrum)
            // ═══════════════════════════════════════════════════
            float waveY = dispY + specDispH + totalH * 0.04f;
            float waveX = dispX;
            float waveW = dispW;
            waveformDisplayArea = { waveX, waveY, waveW, waveH };

            // Background
            g.setColour (juce::Colour (0xFF0A0A18));
            g.fillRoundedRectangle (waveX, waveY, waveW, waveH, 6.0f);
            g.setColour (juce::Colour (0xFF2A2A50));
            g.drawRoundedRectangle (waveX, waveY, waveW, waveH, 6.0f, 0.5f);

            if (processor.hasReference())
            {
                auto& peaks = processor.getRefWaveformPeaks();
                float dur = processor.getRefDurationSeconds();
                float playNorm = processor.getRefPlayPositionNorm();

                float wfX = waveX + 6.0f;
                float wfW = waveW - 12.0f;
                float wfY = waveY + 16.0f;
                float wfH = waveH - 28.0f;
                float midY = wfY + wfH * 0.5f;

                // Title
                g.setColour (juce::Colour (0xFF667788));
                g.setFont (juce::Font (9.0f, juce::Font::bold));
                g.drawText ("REFERENCE", (int)(waveX + 8), (int)(waveY + 2), 80, 12, juce::Justification::centredLeft);

                // File name
                g.setColour (juce::Colour (0xFF55DDEE));
                g.setFont (juce::Font (9.0f));
                g.drawText (processor.getRefFileName(), (int)(waveX + 90), (int)(waveY + 2), (int)(waveW - 100), 12, juce::Justification::centredLeft);

                // Draw waveform
                g.setColour (juce::Colour (0xFF55DDEE).withAlpha (0.15f));
                for (int p = 0; p < EasyMasterProcessor::WAVEFORM_POINTS; ++p)
                {
                    float xPos = wfX + wfW * (float) p / (float) EasyMasterProcessor::WAVEFORM_POINTS;
                    float barW = juce::jmax (1.0f, wfW / (float) EasyMasterProcessor::WAVEFORM_POINTS);
                    float h = peaks[(size_t) p] * wfH * 0.5f;
                    g.fillRect (xPos, midY - h, barW, h * 2.0f);
                }
                // Waveform outline
                {
                    juce::Path topPath, bottomPath;
                    for (int p = 0; p < EasyMasterProcessor::WAVEFORM_POINTS; ++p)
                    {
                        float xPos = wfX + wfW * (float) p / (float) EasyMasterProcessor::WAVEFORM_POINTS;
                        float h = peaks[(size_t) p] * wfH * 0.5f;
                        if (p == 0) { topPath.startNewSubPath (xPos, midY - h); bottomPath.startNewSubPath (xPos, midY + h); }
                        else { topPath.lineTo (xPos, midY - h); bottomPath.lineTo (xPos, midY + h); }
                    }
                    g.setColour (juce::Colour (0xFF55DDEE).withAlpha (0.5f));
                    g.strokePath (topPath, juce::PathStrokeType (0.8f));
                    g.strokePath (bottomPath, juce::PathStrokeType (0.8f));
                }

                // Centerline
                g.setColour (juce::Colour (0xFF2A2A50));
                g.drawHorizontalLine ((int) midY, wfX, wfX + wfW);

                // Timeline
                g.setColour (juce::Colour (0xFF445566));
                g.setFont (juce::Font (7.0f));
                if (dur > 0)
                {
                    // Time markers every 30s or 10s depending on duration
                    float interval = dur > 120 ? 30.0f : dur > 30 ? 10.0f : 5.0f;
                    for (float t = 0; t <= dur; t += interval)
                    {
                        float xPos = wfX + wfW * (t / dur);
                        g.drawVerticalLine ((int) xPos, wfY + wfH, wfY + wfH + 3);
                        int mins = (int)(t / 60.0f);
                        int secs = (int) t % 60;
                        g.drawText (juce::String (mins) + ":" + juce::String (secs).paddedLeft ('0', 2),
                                    (int)(xPos - 14), (int)(wfY + wfH + 2), 28, 10, juce::Justification::centred);
                    }
                }

                // Playhead
                float playX = wfX + wfW * playNorm;
                g.setColour (juce::Colour (0xFFFFFFFF).withAlpha (0.8f));
                g.drawVerticalLine ((int) playX, wfY, wfY + wfH);
                // Small triangle at top
                juce::Path tri;
                tri.addTriangle (playX - 4, wfY, playX + 4, wfY, playX, wfY + 6);
                g.setColour (juce::Colour (0xFFFFFFFF));
                g.fillPath (tri);
            }
            else
            {
                // No reference loaded
                g.setColour (juce::Colour (0xFF445566));
                g.setFont (juce::Font (11.0f));
                g.drawText ("Load a reference track with LOAD REF to compare", waveX, waveY, waveW, waveH, juce::Justification::centred);
            }
        }

        // Saturation FFT spectrum display (stage 3)
        if (currentStage == kSatCommon)
        {
            auto* sat = dynamic_cast<SaturationStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::Saturation));
            if (sat)
            {
                // Background
                float fftX = meterX;
                float fftY = meterY - 200.0f;
                float fftW = meterW;
                float fftH = 250.0f;
                fftDisplayArea = { fftX, fftY, fftW, fftH };

                g.setColour (juce::Colour (0xFF0A0A18));
                g.fillRoundedRectangle (fftDisplayArea, 6.0f);

                g.setColour (juce::Colour (0xFF888888));
                g.setFont (juce::Font (10.0f));
                g.drawText ("SPECTRUM ANALYZER", fftX + 8.0f, fftY + 2.0f, 200.0f, 14.0f, juce::Justification::centredLeft);

                // M/S mode indicator
                int satMs = (int) processor.getAPVTS().getRawParameterValue ("S4_Sat_MS")->load();
                juce::String satMsLabel = (satMs > 0) ? "M/S" : "STEREO";
                juce::Colour satMsCol = (satMs > 0) ? juce::Colour (0xFFE9A045) : juce::Colour (0xFF888888);
                g.setColour (satMsCol);
                g.setFont (juce::Font (10.0f, juce::Font::bold));
                g.drawText (satMsLabel, fftX + fftW - 70, fftY + 2.0f, 60, 14, juce::Justification::centredRight);

                // Draw area for spectrum
                float specX = fftX + 8.0f;
                float specY = fftY + 16.0f;
                float specW = fftW - 16.0f;
                float specH = fftH - 22.0f;

                // Compute FFT magnitudes from OutputMeter (post-processing)
                auto* omSat = processor.getEngine().getOutputMeter();
                if (omSat) omSat->computeFFTMagnitudes();

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

                // Draw FFT spectrum (Mid + Side from OutputMeter)
                if (omSat)
                {
                    auto& midM = omSat->getMidMagnitudes();
                    auto& sideM = omSat->getSideMagnitudes();
                    float sr = (float) processor.getSampleRate();
                    if (sr <= 0) sr = 44100.0f;
                    int fftHalfSize = OutputMeter::fftSize / 2;

                    auto drawOMCurve = [&](const std::array<float, OutputMeter::fftSize/2>& m,
                                          juce::Colour strokeC, juce::Colour fillC, float sw) {
                        juce::Path sp; bool st = false;
                        for (int i = 1; i < fftHalfSize; ++i) {
                            float freq = (float)i * sr / (float)OutputMeter::fftSize;
                            if (freq < 20.0f || freq > 20000.0f) continue;
                            float xP = freqToX(freq, specX, specW);
                            float yP = juce::jlimit(specY, specY+specH, specY + specH - m[(size_t)i] * specH);
                            if (!st) { sp.startNewSubPath(xP, yP); st = true; } else sp.lineTo(xP, yP);
                        }
                        if (st) {
                            juce::Path fp = sp; fp.lineTo(specX+specW, specY+specH); fp.lineTo(specX, specY+specH); fp.closeSubPath();
                            g.setColour(fillC); g.fillPath(fp);
                            g.setColour(strokeC); g.strokePath(sp, juce::PathStrokeType(sw));
                        }
                    };
                    drawOMCurve(midM,  juce::Colour(0xFFE9A045).withAlpha(0.85f), juce::Colour(0xFFE9A045).withAlpha(0.10f), 1.5f);
                    drawOMCurve(sideM, juce::Colour(0xFFBB7722).withAlpha(0.65f), juce::Colour(0xFFBB7722).withAlpha(0.05f), 1.0f);
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
                // Section headers in the control area
                auto ctrlArea = getLocalBounds().withTop (95).withBottom (getHeight() - 70).reduced (8).toFloat();
                ctrlArea = ctrlArea.reduced (12.0f);
                float rowH = (ctrlArea.getHeight() - 200.0f - 28.0f) / 2.0f; // minus display and bypass

                g.setColour (juce::Colour (0xFFE94560));
                g.setFont (juce::Font (10.0f, juce::Font::bold));
                g.drawText ("EQP-1A", (int)(ctrlArea.getX() + 4), (int)(ctrlArea.getY() + 30), 60, 14, juce::Justification::centredLeft);

                // ─── Separator between EQP-1A and MEQ-5 ───
                float divY = ctrlArea.getY() + 22 + rowH;
                // Gradient line with glow
                {
                    juce::ColourGradient grad (juce::Colour (0x00E94560), ctrlArea.getX() + 4, divY,
                                               juce::Colour (0x00E94560), ctrlArea.getRight() - 4, divY, false);
                    grad.addColour (0.05, juce::Colour (0x44E94560));
                    grad.addColour (0.3,  juce::Colour (0xAAE94560));
                    grad.addColour (0.7,  juce::Colour (0xAAE94560));
                    grad.addColour (0.95, juce::Colour (0x44E94560));
                    g.setGradientFill (grad);
                    g.fillRect (ctrlArea.getX() + 4, divY, ctrlArea.getWidth() - 8, 1.0f);
                    // Subtle glow above/below
                    g.setColour (juce::Colour (0x18E94560));
                    g.fillRect (ctrlArea.getX() + 20, divY - 1.0f, ctrlArea.getWidth() - 40, 3.0f);
                }

                g.setColour (juce::Colour (0xFFE94560));
                g.setFont (juce::Font (10.0f, juce::Font::bold));
                g.drawText ("MEQ-5", (int)(ctrlArea.getX() + 4), (int)(divY + 4), 60, 14, juce::Justification::centredLeft);

                // EQ curve + FFT display area
                float dispX = meterX;
                float dispY = meterY - 200.0f;
                float dispW = meterW;
                float dispH = 250.0f;

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

                // FFT spectrum (Mid/Side from OutputMeter — post-processing output)
                {
                    auto* omPul = processor.getEngine().getOutputMeter();
                    if (omPul) {
                        omPul->computeFFTMagnitudes();
                        auto& midM = omPul->getMidMagnitudes();
                        auto& sideM = omPul->getSideMagnitudes();
                        float sr = (float) processor.getSampleRate();
                        if (sr <= 0) sr = 44100.0f;
                        int fftHalf = OutputMeter::fftSize / 2;
                        auto drawOMC = [&](const std::array<float, OutputMeter::fftSize/2>& m,
                                           juce::Colour sc, juce::Colour fc, float sw) {
                            juce::Path sp; bool st = false;
                            for (int i = 1; i < fftHalf; ++i) {
                                float freq = (float)i * sr / (float)OutputMeter::fftSize;
                                if (freq < 20.0f || freq > 20000.0f) continue;
                                float xP = freqToX(freq, specX, specW);
                                float yP = juce::jlimit(specY2, specY2+specH, specY2 + specH - m[(size_t)i] * specH);
                                if (!st) { sp.startNewSubPath(xP, yP); st = true; } else sp.lineTo(xP, yP);
                            }
                            if (st) {
                                juce::Path fp = sp; fp.lineTo(specX+specW, specY2+specH); fp.lineTo(specX, specY2+specH); fp.closeSubPath();
                                g.setColour(fc); g.fillPath(fp); g.setColour(sc); g.strokePath(sp, juce::PathStrokeType(sw));
                            }
                        };
                        drawOMC(midM,  juce::Colour(0xFF4488CC).withAlpha(0.3f), juce::Colour(0xFF4488CC).withAlpha(0.06f), 1.0f);
                        drawOMC(sideM, juce::Colour(0xFF2266AA).withAlpha(0.2f), juce::Colour(0xFF2266AA).withAlpha(0.03f), 0.7f);
                    }
                }

                // EQ curve — color based on M/S mode
                int pultecMs = (int) processor.getAPVTS().getRawParameterValue ("S2_EQ_MS")->load();

                if (pultecMs > 0)
                {
                    // M/S mode: draw both Mid (orange) and Side (cyan) curves
                    g.setColour (juce::Colour (0xFFE9A045));
                    g.setFont (juce::Font (10.0f, juce::Font::bold));
                    g.drawText ("M/S", (int)(dispX + dispW - 65), (int)(dispY + 5), 55, 12, juce::Justification::centredRight);

                    // Mid curve (orange)
                    juce::Path midPath;
                    bool midStarted = false;
                    for (float px = 0; px <= specW; px += 1.5f)
                    {
                        float freq = std::pow (10.0f, std::log10 (20.0f) + (px / specW) * (std::log10 (20000.0f) - std::log10 (20.0f)));
                        double magDb = pultec->getMagnitudeAtFreqMid ((double) freq);
                        float yy = specY2 + specH * 0.5f - (float)(magDb / dbRange) * (specH * 0.5f);
                        yy = juce::jlimit (specY2, specY2 + specH, yy);
                        if (!midStarted) { midPath.startNewSubPath (specX + px, yy); midStarted = true; }
                        else midPath.lineTo (specX + px, yy);
                    }
                    if (midStarted)
                    {
                        juce::Path midFill = midPath;
                        midFill.lineTo (specX + specW, zeroY); midFill.lineTo (specX, zeroY); midFill.closeSubPath();
                        g.setColour (juce::Colour (0xFFE9A045).withAlpha (0.08f)); g.fillPath (midFill);
                        g.setColour (juce::Colour (0xFFE9A045).withAlpha (0.85f)); g.strokePath (midPath, juce::PathStrokeType (2.0f));
                    }
                    // Side curve (cyan)
                    juce::Path sidePath;
                    bool sideStarted = false;
                    for (float px = 0; px <= specW; px += 1.5f)
                    {
                        float freq = std::pow (10.0f, std::log10 (20.0f) + (px / specW) * (std::log10 (20000.0f) - std::log10 (20.0f)));
                        double magDb = pultec->getMagnitudeAtFreqSide ((double) freq);
                        float yy = specY2 + specH * 0.5f - (float)(magDb / dbRange) * (specH * 0.5f);
                        yy = juce::jlimit (specY2, specY2 + specH, yy);
                        if (!sideStarted) { sidePath.startNewSubPath (specX + px, yy); sideStarted = true; }
                        else sidePath.lineTo (specX + px, yy);
                    }
                    if (sideStarted)
                    {
                        juce::Path sideFill = sidePath;
                        sideFill.lineTo (specX + specW, zeroY); sideFill.lineTo (specX, zeroY); sideFill.closeSubPath();
                        g.setColour (juce::Colour (0xFF44DDCC).withAlpha (0.08f)); g.fillPath (sideFill);
                        g.setColour (juce::Colour (0xFF44DDCC).withAlpha (0.85f)); g.strokePath (sidePath, juce::PathStrokeType (2.0f));
                    }
                }
                else
                {
                    // Stereo mode: single curve
                    juce::Colour pCurveCol (0xFFE9A045);
                    g.setColour (pCurveCol);
                    g.setFont (juce::Font (10.0f, juce::Font::bold));
                    g.drawText ("STEREO", (int)(dispX + dispW - 65), (int)(dispY + 5), 55, 12, juce::Justification::centredRight);

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
                        juce::Path eqFill = eqPath;
                        eqFill.lineTo (specX + specW, zeroY);
                        eqFill.lineTo (specX, zeroY);
                        eqFill.closeSubPath();
                        g.setColour (pCurveCol.withAlpha (0.12f));
                        g.fillPath (eqFill);
                        g.setColour (pCurveCol.withAlpha (0.85f));
                        g.strokePath (eqPath, juce::PathStrokeType (2.0f));
                    }
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
                float dispY = meterY - 200.0f;
                float dispW = meterW;
                float dispH = 250.0f;

                // Background with border
                g.setColour (juce::Colour (0xFF0D0D1E));
                g.fillRoundedRectangle (dispX, dispY, dispW, dispH, 8.0f);
                g.setColour (juce::Colour (0xFF2A2A50));
                g.drawRoundedRectangle (dispX, dispY, dispW, dispH, 8.0f, 0.5f);

                float specX = dispX + 30.0f;
                float specY2 = dispY + 6.0f;
                float specW = dispW - 36.0f;
                float specH = dispH - 14.0f;
                float dbRange = 24.0f;

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

                // FFT spectrum (Mid/Side from OutputMeter)
                {
                    auto* omEQ = processor.getEngine().getOutputMeter();
                    if (omEQ) {
                        omEQ->computeFFTMagnitudes();
                        auto& midM = omEQ->getMidMagnitudes();
                        auto& sideM = omEQ->getSideMagnitudes();
                        float sr = (float) processor.getSampleRate();
                        if (sr <= 0) sr = 44100.0f;
                        int fftHalf = OutputMeter::fftSize / 2;
                        auto drawOMEQ = [&](const std::array<float, OutputMeter::fftSize/2>& m,
                                            juce::Colour sc, juce::Colour fc, float sw) {
                            juce::Path sp; bool st = false;
                            for (int i = 1; i < fftHalf; ++i) {
                                float freq = (float)i * sr / (float)OutputMeter::fftSize;
                                if (freq < 20.0f || freq > 20000.0f) continue;
                                float xP = freqToX(freq, specX, specW);
                                float yP = juce::jlimit(specY2, specY2+specH, specY2 + specH - m[(size_t)i] * specH);
                                if (!st) { sp.startNewSubPath(xP, yP); st = true; } else sp.lineTo(xP, yP);
                            }
                            if (st) {
                                juce::Path fp = sp; fp.lineTo(specX+specW, specY2+specH); fp.lineTo(specX, specY2+specH); fp.closeSubPath();
                                g.setColour(fc); g.fillPath(fp); g.setColour(sc); g.strokePath(sp, juce::PathStrokeType(sw));
                            }
                        };
                        drawOMEQ(midM,  juce::Colour(0xFF4488CC).withAlpha(0.3f), juce::Colour(0xFF4488CC).withAlpha(0.06f), 1.0f);
                        drawOMEQ(sideM, juce::Colour(0xFF2266AA).withAlpha(0.2f), juce::Colour(0xFF2266AA).withAlpha(0.03f), 0.7f);
                    }
                }

                // ─── 3 EQ curves: Stereo (yellow), Mid (red), Side (green) ───
                int eqMsMode = (int) processor.getAPVTS().getRawParameterValue ("S5_EQ2_MS")->load();
                float zeroY = specY2 + specH * 0.5f;

                struct CurveSet { juce::Colour col; juce::Colour bright; juce::String label; int mode; };
                CurveSet curves[3] = {
                    { juce::Colour (0xFFE9A045), juce::Colour (0xFFFFBB55), "STEREO", 0 },
                    { juce::Colour (0xFFFF5555), juce::Colour (0xFFFF8888), "MID", 1 },
                    { juce::Colour (0xFF55DD77), juce::Colour (0xFF88FFAA), "SIDE", 2 }
                };

                // Draw all 3 curves — inactive ones faded, active one bright
                for (int c = 0; c < 3; ++c)
                {
                    bool isActive = (c == eqMsMode);
                    float alpha = isActive ? 1.0f : 0.25f;

                    juce::Path eqPath;
                    bool eqStarted = false;
                    for (float px = 0; px <= specW; px += 1.0f)
                    {
                        float freq = std::pow (10.0f, std::log10 (20.0f) + (px / specW) * (std::log10 (20000.0f) - std::log10 (20.0f)));
                        double magDb = (c == 0) ? outEQ->getMagnitudeAtFreq ((double) freq) :
                                       (c == 1) ? outEQ->getMagnitudeAtFreqMid ((double) freq) :
                                                   outEQ->getMagnitudeAtFreqSide ((double) freq);
                        float yy = specY2 + specH * 0.5f - (float)(magDb / dbRange) * (specH * 0.5f);
                        yy = juce::jlimit (specY2, specY2 + specH, yy);
                        if (!eqStarted) { eqPath.startNewSubPath (specX + px, yy); eqStarted = true; }
                        else eqPath.lineTo (specX + px, yy);
                    }
                    if (eqStarted)
                    {
                        if (isActive)
                        {
                            juce::Path eqFill = eqPath;
                            eqFill.lineTo (specX + specW, zeroY);
                            eqFill.lineTo (specX, zeroY);
                            eqFill.closeSubPath();
                            g.setColour (curves[c].col.withAlpha (0.10f * alpha));
                            g.fillPath (eqFill);
                        }
                        g.setColour (curves[c].col.withAlpha (0.3f * alpha));
                        g.strokePath (eqPath, juce::PathStrokeType (isActive ? 3.0f : 1.5f));
                        if (isActive)
                        {
                            g.setColour (curves[c].bright.withAlpha (0.9f));
                            g.strokePath (eqPath, juce::PathStrokeType (1.5f));
                        }
                    }
                }

                // Mode indicator top-right
                g.setColour (curves[eqMsMode].col);
                g.setFont (juce::Font (10.0f, juce::Font::bold));
                g.drawText (curves[eqMsMode].label, (int)(dispX + dispW - 65), (int)(dispY + 5), 55, 12, juce::Justification::centredRight);

                // Legend: small colored lines for all 3 sets
                float legX = specX + 4;
                for (int c = 0; c < 3; ++c)
                {
                    g.setColour (curves[c].col.withAlpha (c == eqMsMode ? 0.9f : 0.3f));
                    g.fillRect (legX, specY2 + 4, 10.0f, 2.0f);
                    g.setFont (juce::Font (7.0f));
                    g.drawText (curves[c].label.substring (0, 1), (int)(legX + 12), (int)(specY2 + 0), 10, 10, juce::Justification::centredLeft);
                    legX += 26;
                }

                // Band nodes — only for the ACTIVE set
                juce::Colour nodeCol = curves[eqMsMode].col;
                for (int b = 0; b < OutputEQStage::NUM_BANDS; ++b)
                {
                    auto bi = (eqMsMode == 1) ? outEQ->getBandInfoMid (b) :
                              (eqMsMode == 2) ? outEQ->getBandInfoSide (b) :
                                                outEQ->getBandInfo (b);
                    double nodeMag = (eqMsMode == 1) ? outEQ->getMagnitudeAtFreqMid ((double) bi.freq) :
                                     (eqMsMode == 2) ? outEQ->getMagnitudeAtFreqSide ((double) bi.freq) :
                                                       outEQ->getMagnitudeAtFreq ((double) bi.freq);
                    float nodeX = freqToX (bi.freq, specX, specW);
                    float nodeY = specY2 + specH * 0.5f - (float)(nodeMag / dbRange) * (specH * 0.5f);
                    nodeY = juce::jlimit (specY2 + 4.0f, specY2 + specH - 4.0f, nodeY);
                    float nodeR = (std::abs (bi.gain) > 0.5f) ? 7.0f : 5.0f;

                    g.setColour (nodeCol.withAlpha (0.2f));
                    g.fillEllipse (nodeX - nodeR - 2, nodeY - nodeR - 2, (nodeR + 2) * 2, (nodeR + 2) * 2);
                    g.setColour (nodeCol.withAlpha (0.8f));
                    g.fillEllipse (nodeX - nodeR, nodeY - nodeR, nodeR * 2, nodeR * 2);
                    g.setColour (juce::Colours::white.withAlpha (0.5f));
                    g.drawEllipse (nodeX - nodeR, nodeY - nodeR, nodeR * 2, nodeR * 2, 1.0f);

                    juce::String nodeLabels[] = { "LS", "LM", "M", "HM", "HS" };
                    g.setColour (juce::Colours::white.withAlpha (0.7f));
                    g.setFont (juce::Font (7.0f, juce::Font::bold));
                    g.drawText (nodeLabels[b], (int)(nodeX - 12), (int)(nodeY - nodeR - 12), 24, 10, juce::Justification::centred);
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

        // ─── FILTER DISPLAY (stage 5) — interactive HP/LP curve ───
        if (currentStage == 5)
        {
            float dispX = meterX, dispY = meterY - 200.0f, dispW = meterW, dispH = 250.0f;
            filterDisplayArea = { dispX, dispY, dispW, dispH };

            g.setColour (juce::Colour (0xFF0D0D1E));
            g.fillRoundedRectangle (dispX, dispY, dispW, dispH, 6.0f);
            g.setColour (juce::Colour (0xFF2A2A50));
            g.drawRoundedRectangle (dispX, dispY, dispW, dispH, 6.0f, 0.5f);

            float specX = dispX + 30.0f, specY2 = dispY + 4.0f;
            float specW = dispW - 36.0f, specH = dispH - 18.0f;
            float dbRange = 36.0f;
            float zeroY = specY2 + specH * 0.5f;

            // dB grid
            g.setColour (juce::Colour (0xFF1A1A35));
            for (float db : { -24.0f, -12.0f, -6.0f, 6.0f }) {
                float yy = zeroY - (db / dbRange) * (specH * 0.5f);
                g.drawHorizontalLine ((int) yy, specX, specX + specW);
            }
            g.setColour (juce::Colour (0xFF2A2A50));
            g.drawHorizontalLine ((int) zeroY, specX, specX + specW);

            // dB labels
            g.setColour (juce::Colour (0xFF444466)); g.setFont (juce::Font (8.0f));
            for (float db : { -24.0f, -12.0f, 0.0f, 6.0f }) {
                float yy = zeroY - (db / dbRange) * (specH * 0.5f);
                g.drawText (juce::String ((int)db), (int)(dispX+2), (int)(yy-5), 26, 10, juce::Justification::centredRight);
            }

            // Freq grid
            float fLabs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
            g.setColour (juce::Colour (0xFF1A1A35));
            for (float f : fLabs) g.drawVerticalLine ((int) freqToX(f,specX,specW), specY2, specY2+specH);
            g.setColour (juce::Colour (0xFF444466)); g.setFont (juce::Font (7.0f));
            for (float f : fLabs) {
                float xp = freqToX(f,specX,specW);
                auto fmt = [](float fr){return fr>=1000?juce::String(fr/1000,0)+"k":juce::String((int)fr);};
                g.drawText (fmt(f),(int)(xp-12),(int)(specY2+specH+1),24,10,juce::Justification::centred);
            }

            // M/S + phase labels
            int fltMs = (int) processor.getAPVTS().getRawParameterValue("S6_Filter_MS")->load();
            bool isLin = (int) processor.getAPVTS().getRawParameterValue("S6_Filter_Mode")->load() == 1;
            g.setColour (juce::Colour (0xFFE9A045)); g.setFont (juce::Font (10.0f, juce::Font::bold));
            g.drawText (fltMs>0?"M/S":"STEREO",(int)(dispX+dispW-110),(int)(dispY+4),45,12,juce::Justification::centredRight);
            g.setColour (juce::Colour (0xFF66AADD));
            bool effectiveLinear = isLin && (fltMs == 0); // linear phase only in stereo
            g.drawText (effectiveLinear?"LINEAR":"MIN",(int)(dispX+dispW-60),(int)(dispY+4),50,12,juce::Justification::centredRight);

            // ─── FFT Spectrum analyzer (Mid/Side from OutputMeter) ───
            {
                auto* omFlt = processor.getEngine().getOutputMeter();
                if (omFlt) {
                    omFlt->computeFFTMagnitudes();
                    auto& midM = omFlt->getMidMagnitudes();
                    auto& sideM = omFlt->getSideMagnitudes();
                    float sr = (float) processor.getSampleRate();
                    if (sr <= 0) sr = 48000.0f;
                    int fftHalf = OutputMeter::fftSize / 2;
                    auto drawOMFlt = [&](const std::array<float, OutputMeter::fftSize/2>& m,
                                        juce::Colour sc, juce::Colour fc, float sw) {
                        juce::Path sp; bool st = false;
                        for (int i = 1; i < fftHalf; ++i) {
                            float freq = (float)i * sr / (float)OutputMeter::fftSize;
                            if (freq < 20.0f || freq > 20000.0f) continue;
                            float xp = freqToX(freq, specX, specW);
                            float yp = juce::jlimit(specY2, specY2+specH, specY2 + specH - m[(size_t)i] * specH);
                            if (!st) { sp.startNewSubPath(xp, yp); st = true; } else sp.lineTo(xp, yp);
                        }
                        if (st) {
                            juce::Path fp = sp; fp.lineTo(specX+specW, specY2+specH); fp.lineTo(specX, specY2+specH); fp.closeSubPath();
                            g.setColour(fc); g.fillPath(fp); g.setColour(sc); g.strokePath(sp, juce::PathStrokeType(sw));
                        }
                    };
                    drawOMFlt(midM,  juce::Colour(0xFF4488CC).withAlpha(0.3f), juce::Colour(0xFF4488CC).withAlpha(0.06f), 1.0f);
                    drawOMFlt(sideM, juce::Colour(0xFF2266AA).withAlpha(0.2f), juce::Colour(0xFF2266AA).withAlpha(0.03f), 0.7f);
                }
            }

            // Compute IIR magnitude at freq
            auto computeFltMag = [&](float freq, bool hpAct, float hpF, int hpSl, bool lpAct, float lpF, int lpSl) -> double {
                auto* fs = dynamic_cast<FilterStage*>(processor.getEngine().getStage(ProcessingStage::StageID::Filter));
                double sr = fs ? fs->getSampleRate() : 48000; if(sr<=0) return 0;
                double mag = 1.0;
                static const int sm[]={1,1,2,2,4};
                if(hpAct) { int ns=sm[juce::jlimit(0,4,hpSl)]; for(int s=0;s<ns;++s) {
                    auto c=(hpSl==0&&s==0)?juce::dsp::IIR::Coefficients<double>::makeFirstOrderHighPass(sr,(double)hpF)
                                          :juce::dsp::IIR::Coefficients<double>::makeHighPass(sr,(double)hpF,0.707);
                    mag*=c->getMagnitudeForFrequency((double)freq,sr); }}
                if(lpAct) { int ns=sm[juce::jlimit(0,4,lpSl)]; for(int s=0;s<ns;++s) {
                    auto c=(lpSl==0&&s==0)?juce::dsp::IIR::Coefficients<double>::makeFirstOrderLowPass(sr,(double)lpF)
                                          :juce::dsp::IIR::Coefficients<double>::makeLowPass(sr,(double)lpF,0.707);
                    mag*=c->getMagnitudeForFrequency((double)freq,sr); }}
                return juce::Decibels::gainToDecibels(mag,-60.0);
            };

            // Draw curve lambda
            auto drawFltCurve = [&](juce::Colour col, bool hpA, float hpF, int hpSl, bool lpA, float lpF, int lpSl) {
                juce::Path p; bool st=false;
                for(float px=0;px<=specW;px+=1.5f){
                    float freq=std::pow(10.0f,std::log10(20.0f)+(px/specW)*(std::log10(20000.0f)-std::log10(20.0f)));
                    double mDb=computeFltMag(freq,hpA,hpF,hpSl,lpA,lpF,lpSl);
                    float yy=juce::jlimit(specY2,specY2+specH,zeroY-(float)(mDb/dbRange)*(specH*0.5f));
                    if(!st){p.startNewSubPath(specX+px,yy);st=true;}else p.lineTo(specX+px,yy);
                }
                if(st){ juce::Path fp=p; fp.lineTo(specX+specW,zeroY); fp.lineTo(specX,zeroY); fp.closeSubPath();
                    g.setColour(col.withAlpha(0.1f)); g.fillPath(fp);
                    g.setColour(col.withAlpha(0.85f)); g.strokePath(p,juce::PathStrokeType(2.0f)); }
            };

            // Draw node lambda
            auto drawFltNode = [&](juce::Colour col, float freq, double mDb, juce::String lbl, bool diamond) {
                float nx=freqToX(freq,specX,specW);
                float ny=juce::jlimit(specY2,specY2+specH,zeroY-(float)(mDb/dbRange)*(specH*0.5f));
                float r=6;
                if(diamond){ juce::Path d; d.addTriangle(nx,ny-r,nx+r,ny,nx,ny+r); d.addTriangle(nx,ny-r,nx-r,ny,nx,ny+r);
                    g.setColour(col); g.fillPath(d); }
                else{ g.setColour(col.darker(0.3f)); g.fillEllipse(nx-r,ny-r,r*2,r*2);
                    g.setColour(col); g.drawEllipse(nx-r,ny-r,r*2,r*2,1.5f); }
                g.setColour(juce::Colours::white.withAlpha(0.8f)); g.setFont(juce::Font(8.0f,juce::Font::bold));
                g.drawText(lbl,(int)(nx-25),(int)(ny-r-14),50,10,juce::Justification::centred);
            };

            static const juce::String slLabels[]={"6","12","18","24","48"};

            if (fltMs > 0) {
                bool mHP=processor.getAPVTS().getRawParameterValue("S6_HP_M_On")->load()>0.5f;
                float mHPF=processor.getAPVTS().getRawParameterValue("S6_HP_M_Freq")->load();
                int mHPS=(int)processor.getAPVTS().getRawParameterValue("S6_HP_M_Slope")->load();
                bool mLP=processor.getAPVTS().getRawParameterValue("S6_LP_M_On")->load()>0.5f;
                float mLPF=processor.getAPVTS().getRawParameterValue("S6_LP_M_Freq")->load();
                int mLPS=(int)processor.getAPVTS().getRawParameterValue("S6_LP_M_Slope")->load();
                bool sHP=processor.getAPVTS().getRawParameterValue("S6_HP_S_On")->load()>0.5f;
                float sHPF=processor.getAPVTS().getRawParameterValue("S6_HP_S_Freq")->load();
                int sHPS=(int)processor.getAPVTS().getRawParameterValue("S6_HP_S_Slope")->load();
                bool sLP=processor.getAPVTS().getRawParameterValue("S6_LP_S_On")->load()>0.5f;
                float sLPF=processor.getAPVTS().getRawParameterValue("S6_LP_S_Freq")->load();
                int sLPS=(int)processor.getAPVTS().getRawParameterValue("S6_LP_S_Slope")->load();
                juce::Colour midC(0xFFE9A045), sideC(0xFF44DDCC);
                drawFltCurve(midC,mHP,mHPF,mHPS,mLP,mLPF,mLPS);
                drawFltCurve(sideC,sHP,sHPF,sHPS,sLP,sLPF,sLPS);
                if(mHP) drawFltNode(midC,mHPF,computeFltMag(mHPF,true,mHPF,mHPS,false,0,0),"M:HP "+slLabels[juce::jlimit(0,4,mHPS)],false);
                if(mLP) drawFltNode(midC,mLPF,computeFltMag(mLPF,false,0,0,true,mLPF,mLPS),"M:LP "+slLabels[juce::jlimit(0,4,mLPS)],false);
                if(sHP) drawFltNode(sideC,sHPF,computeFltMag(sHPF,true,sHPF,sHPS,false,0,0),"S:HP "+slLabels[juce::jlimit(0,4,sHPS)],true);
                if(sLP) drawFltNode(sideC,sLPF,computeFltMag(sLPF,false,0,0,true,sLPF,sLPS),"S:LP "+slLabels[juce::jlimit(0,4,sLPS)],true);
            } else {
                bool hpA=processor.getAPVTS().getRawParameterValue("S6_HP_On")->load()>0.5f;
                float hpF=processor.getAPVTS().getRawParameterValue("S6_HP_Freq")->load();
                int hpSl=(int)processor.getAPVTS().getRawParameterValue("S6_HP_Slope")->load();
                bool lpA=processor.getAPVTS().getRawParameterValue("S6_LP_On")->load()>0.5f;
                float lpF=processor.getAPVTS().getRawParameterValue("S6_LP_Freq")->load();
                int lpSl=(int)processor.getAPVTS().getRawParameterValue("S6_LP_Slope")->load();
                drawFltCurve(juce::Colour(0xFFE9A045),hpA,hpF,hpSl,lpA,lpF,lpSl);
                if(hpA) drawFltNode(juce::Colour(0xFF55CC77),hpF,computeFltMag(hpF,true,hpF,hpSl,false,0,0),"HP "+slLabels[juce::jlimit(0,4,hpSl)],false);
                if(lpA) drawFltNode(juce::Colour(0xFFCC5577),lpF,computeFltMag(lpF,false,0,0,true,lpF,lpSl),"LP "+slLabels[juce::jlimit(0,4,lpSl)],false);
                if(!hpA && !lpA) {
                    g.setColour(juce::Colour(0xFF555577)); g.setFont(juce::Font(11.0f));
                    g.drawText("Double-click to create HP or LP filter", filterDisplayArea, juce::Justification::centred);
                }
            }
        }

        // Dynamic Resonance display (stage 6)
        if (currentStage == 6)
        {
            auto* dynRes = dynamic_cast<DynamicResonanceStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::DynamicResonance));
            if (dynRes)
            {
                // Big spectrum-style display
                float dispX = meterX;
                float dispY = meterY - 200.0f;
                float dispW = meterW;
                float dispH = 250.0f;

                g.setColour (juce::Colour (0xFF0A0A18));
                g.fillRoundedRectangle (dispX, dispY, dispW, dispH, 8.0f);
                g.setColour (juce::Colour (0xFF2A2A50));
                g.drawRoundedRectangle (dispX, dispY, dispW, dispH, 8.0f, 0.5f);

                g.setColour (juce::Colour (0xFF667788));
                g.setFont (juce::Font (9.0f));
                g.drawText ("RESONANCE SUPPRESSOR", dispX + 10, dispY + 4, 200, 12, juce::Justification::centredLeft);

                // Mode display
                int mode = (int) processor.getAPVTS().getRawParameterValue ("S6B_DynEQ_Mode")->load();
                g.setColour (mode == 0 ? juce::Colour (0xFF44CC88) : juce::Colour (0xFFE94560));
                g.drawText (mode == 0 ? "SOFT" : "HARD", dispX + dispW - 70, dispY + 4, 60, 12, juce::Justification::centredRight);

                // Band bars area
                float barX = dispX + 12.0f;
                float barY = dispY + 22.0f;
                float barW = dispW - 24.0f;
                float barH = dispH - 40.0f;
                float bandW = barW / (float) DynamicResonanceStage::NUM_BANDS;

                // dB grid lines — always use 18dB scale for consistent display
                g.setColour (juce::Colour (0xFF1A1A35));
                float displayMaxDb = 18.0f;
                for (float db = -3.0f; db >= -displayMaxDb; db -= 3.0f)
                {
                    float yLine = barY + barH * (-db / displayMaxDb);
                    g.drawHorizontalLine ((int) yLine, barX, barX + barW);
                }
                // 0 dB line
                g.setColour (juce::Colour (0xFF2A2A55));
                g.drawHorizontalLine ((int) barY, barX, barX + barW);

                // Draw bars
                for (int b = 0; b < DynamicResonanceStage::NUM_BANDS; ++b)
                {
                    float grDb = dynRes->bandGR[(size_t) b].load (std::memory_order_relaxed);
                    float normalized = juce::jlimit (0.0f, 1.0f, -grDb / displayMaxDb);
                    float x = barX + (float) b * bandW;

                    // Background
                    g.setColour (juce::Colour (0xFF141430));
                    g.fillRect (x + 0.5f, barY, bandW - 1.0f, barH);

                    // GR fill (from top down)
                    if (normalized > 0.005f)
                    {
                        float fillH = barH * normalized;
                        auto col = normalized < 0.2f ? juce::Colour (0xFF33AA66) :
                                   normalized < 0.5f ? juce::Colour (0xFFBB9922) :
                                                       juce::Colour (0xFFE94560);
                        g.setColour (col.withAlpha (0.8f));
                        g.fillRect (x + 0.5f, barY, bandW - 1.0f, fillH);
                    }
                }

                // Frequency labels
                g.setColour (juce::Colour (0xFF556677));
                g.setFont (juce::Font (8.0f));
                int labelBands[] = { 0, 4, 8, 12, 16, 20, 24, 28, 31 };
                for (int lb : labelBands)
                {
                    if (lb >= DynamicResonanceStage::NUM_BANDS) continue;
                    float freq = dynRes->getBandFreq (lb);
                    juce::String freqStr = freq >= 1000.0f ?
                        juce::String (freq / 1000.0f, 1) + "k" :
                        juce::String ((int) freq);
                    float x = barX + (float) lb * bandW;
                    g.drawText (freqStr, (int)(x - 10), (int)(barY + barH + 2), 24, 10, juce::Justification::centred);
                }

                // dB scale on left
                g.setColour (juce::Colour (0xFF556677));
                g.setFont (juce::Font (8.0f));
                g.drawText ("0", (int)(dispX + 1), (int) barY - 5, 12, 10, juce::Justification::centredLeft);
                g.drawText (juce::String (-(int) displayMaxDb), (int)(dispX + 1), (int)(barY + barH - 5), 16, 10, juce::Justification::centredLeft);
            }
        }

        // ─── Clipper waveform + peak metering (stage 7) ───
        if (currentStage == 7)
        {
            auto* clip = dynamic_cast<ClipperStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::Clipper));
            if (clip)
            {
                float wfX = meterX;
                float wfY = meterY - 200.0f;
                float wfW = meterW;
                float wfH = 250.0f;

                // Background
                g.setColour (juce::Colour (0xFF0A0A18));
                g.fillRoundedRectangle (wfX, wfY, wfW, wfH, 8.0f);
                g.setColour (juce::Colour (0xFF2A2A50));
                g.drawRoundedRectangle (wfX, wfY, wfW, wfH, 8.0f, 0.5f);

                // Ceiling line
                float ceilDb = processor.getAPVTS().getRawParameterValue ("S7_Clipper_Ceiling")->load();
                float ceilNorm = juce::jmap (ceilDb, -12.0f, 0.0f, 0.0f, 1.0f);
                float ceilYPos = wfY + wfH * 0.5f - ceilNorm * wfH * 0.4f;
                float ceilYNeg = wfY + wfH * 0.5f + ceilNorm * wfH * 0.4f;
                g.setColour (juce::Colour (0xFFFF4444).withAlpha (0.5f));
                g.drawHorizontalLine ((int) ceilYPos, wfX + 4, wfX + wfW - 4);
                g.drawHorizontalLine ((int) ceilYNeg, wfX + 4, wfX + wfW - 4);

                // Zero line
                g.setColour (juce::Colour (0xFF333355));
                g.drawHorizontalLine ((int)(wfY + wfH * 0.5f), wfX + 4, wfX + wfW - 4);

                // Waveform: input (gray) and output (cyan)
                int bufSize = ClipperStage::WAVE_BUF_SIZE;
                int wp = clip->getWaveWritePos();
                float drawW = wfW - 8.0f;
                int samplesToShow = juce::jmin (bufSize, (int) drawW * 2);

                // Input waveform
                juce::Path inputPath;
                bool started = false;
                for (int i = 0; i < (int) drawW; ++i)
                {
                    int sampleIdx = (wp - samplesToShow + (int)(i * samplesToShow / drawW) + bufSize) % bufSize;
                    float sample = clip->getWaveIn (sampleIdx);
                    float yy = wfY + wfH * 0.5f - sample * wfH * 0.4f;
                    yy = juce::jlimit (wfY + 2, wfY + wfH - 2, yy);
                    if (!started) { inputPath.startNewSubPath (wfX + 4 + i, yy); started = true; }
                    else inputPath.lineTo (wfX + 4 + i, yy);
                }
                g.setColour (juce::Colour (0xFF555577));
                g.strokePath (inputPath, juce::PathStrokeType (1.0f));

                // Output waveform
                juce::Path outputPath;
                started = false;
                for (int i = 0; i < (int) drawW; ++i)
                {
                    int sampleIdx = (wp - samplesToShow + (int)(i * samplesToShow / drawW) + bufSize) % bufSize;
                    float sample = clip->getWaveOut (sampleIdx);
                    float yy = wfY + wfH * 0.5f - sample * wfH * 0.4f;
                    yy = juce::jlimit (wfY + 2, wfY + wfH - 2, yy);
                    if (!started) { outputPath.startNewSubPath (wfX + 4 + i, yy); started = true; }
                    else outputPath.lineTo (wfX + 4 + i, yy);
                }
                g.setColour (juce::Colour (0xFF55CCEE));
                g.strokePath (outputPath, juce::PathStrokeType (1.2f));

                // Labels
                g.setColour (juce::Colour (0xFF888888));
                g.setFont (juce::Font (9.0f));
                g.drawText ("INPUT", wfX + 8, wfY + 3, 50, 12, juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xFF55CCEE));
                g.drawText ("OUTPUT", wfX + 60, wfY + 3, 50, 12, juce::Justification::centredLeft);

                // Peak meter: input peak + clip amount
                float inPeak = clip->getInputPeakDb();
                float clipAmt = clip->getClipAmountDb();
                g.setColour (juce::Colours::white);
                g.setFont (juce::Font (11.0f, juce::Font::bold));
                g.drawText ("IN: " + juce::String (inPeak, 1) + " dB", wfX + wfW - 220, wfY + 3, 100, 14, juce::Justification::centredRight);
                g.setColour (clipAmt < -0.5f ? juce::Colour (0xFFFF6B6B) : juce::Colour (0xFF88CC88));
                g.drawText ("CLIP: " + juce::String (clipAmt, 1) + " dB", wfX + wfW - 110, wfY + 3, 100, 14, juce::Justification::centredRight);
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

                // FFT curve (Mid + Side)
                om->computeFFTMagnitudes();
                float sr = (float) processor.getSampleRate();
                if (sr <= 0) sr = 44100.0f;
                int fftHalf = OutputMeter::fftSize / 2;

                auto drawLimCurve = [&](const std::array<float, OutputMeter::fftSize/2>& m,
                                       juce::Colour sc, juce::Colour fc, float sw) {
                    juce::Path sp; bool st = false;
                    for (int i = 1; i < fftHalf; ++i) {
                        float freq = (float)i * sr / (float)OutputMeter::fftSize;
                        if (freq < 20.0f || freq > 20000.0f) continue;
                        float xP = freqToX(freq, specX, specW);
                        float yP = juce::jlimit(specY, specY+specH, specY + specH - m[(size_t)i] * specH);
                        if (!st) { sp.startNewSubPath(xP, yP); st = true; } else sp.lineTo(xP, yP);
                    }
                    if (st) {
                        juce::Path fp = sp; fp.lineTo(specX+specW, specY+specH); fp.lineTo(specX, specY+specH); fp.closeSubPath();
                        g.setColour(fc); g.fillPath(fp); g.setColour(sc); g.strokePath(sp, juce::PathStrokeType(sw));
                    }
                };
                drawLimCurve(om->getMidMagnitudes(),  juce::Colour(0xFF5599DD).withAlpha(0.75f), juce::Colour(0xFF4488CC).withAlpha(0.12f), 1.5f);
                drawLimCurve(om->getSideMagnitudes(), juce::Colour(0xFF3366AA).withAlpha(0.50f), juce::Colour(0xFF2255AA).withAlpha(0.05f), 0.8f);

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

    // Labels above controls
    g.setColour (juce::Colour (0xFF667788));
    g.setFont (juce::Font (9.0f));
    auto msBounds = masterOutputSlider.getBounds();
    g.drawText ("MASTER", msBounds.getX(), msBounds.getY() - 14, msBounds.getWidth(), 12, juce::Justification::centred);
    auto osBounds = oversamplingCombo.getBounds();
    g.drawText ("OS", osBounds.getX(), osBounds.getY() - 14, osBounds.getWidth(), 12, juce::Justification::centred);
    auto dtBounds = ditherCombo.getBounds();
    g.drawText ("DITHER", dtBounds.getX(), dtBounds.getY() - 14, dtBounds.getWidth(), 12, juce::Justification::centred);
    auto azBounds = analyzerSpeedCombo.getBounds();
    g.drawText ("ANALYZER", azBounds.getX(), azBounds.getY() - 14, azBounds.getWidth(), 12, juce::Justification::centred);
}

void EasyMasterEditor::resized()
{
    // Guard: don't layout until constructor is done
    if (tabButtons.size() == 0)
        return;

    auto area = getLocalBounds();

    // Top bar
    auto topBar = area.removeFromTop (50);
    topBar.removeFromLeft (210); // logo area (EASY MASTER + subtitle)
    presetSelector.setBounds (topBar.removeFromLeft (130).reduced (4, 8));
    savePresetButton.setBounds (topBar.removeFromLeft (50).reduced (2, 10));
    deletePresetButton.setBounds (topBar.removeFromLeft (60).reduced (2, 10));
    globalBypassButton.setBounds (topBar.removeFromLeft (66).reduced (2, 10));
    autoMatchButton.setBounds (topBar.removeFromLeft (90).reduced (2, 10));
    loadRefButton.setBounds (topBar.removeFromLeft (72).reduced (2, 10));
    abButton.setBounds (topBar.removeFromLeft (40).reduced (2, 10));
    refNameLabel.setBounds (topBar.removeFromLeft (80).reduced (2, 12));
    // Right side: RESET | TP | LUFS
    initButton.setBounds (topBar.removeFromRight (56).reduced (2, 10));
    lufsLabel.setBounds (topBar.removeFromRight (100).reduced (2));
    truePeakLabel.setBounds (topBar.removeFromRight (100).reduced (2));

    // Tab strip + reorder buttons
    auto tabRow = area.removeFromTop (36).reduced (8, 2);
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
    masterOutputSlider.setBounds (bottomBar.removeFromRight (60).reduced (2, 8));
    oversamplingCombo.setBounds (bottomBar.removeFromRight (70).reduced (4, 20));
    ditherCombo.setBounds (bottomBar.removeFromRight (70).reduced (4, 20));
    analyzerSpeedCombo.setBounds (bottomBar.removeFromRight (70).reduced (4, 20));
    showRefSpecToggle.setBounds (bottomBar.removeFromRight (50).reduced (2, 18));

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
    panelArea.removeFromBottom (200);  // space for GR meter / FFT / waveform displays

    // ─── Special layout for Pultec ───
    if (currentStage == 1)
    {
        // EQP-1A row: combos + knobs for stage kPultecTop (=1)
        // MEQ-5 row: knobs for stage kPultecMEQ (=15)
        auto row1 = panelArea.removeFromTop ((int)(panelArea.getHeight() * 0.48f));
        auto row2 = panelArea.withTrimmedTop (18); // extra space for separator + MEQ-5 label

        // Section headers
        // (painted in paint(), not here)

        // Row 1: EQP-1A — collect combos and knobs
        {
            int nCombos = 0, nKnobs = 0;
            for (int i = 0; i < allCombos.size(); ++i)
                if (comboStage[i] == 1) nCombos++;
            for (int i = 0; i < allSliders.size(); ++i)
                if (stageForControl[i] == 1) nKnobs++;
            int total = nCombos + nKnobs;
            if (total > 0)
            {
                int cellW = row1.getWidth() / juce::jmax (total, 1);
                int knobSz = juce::jlimit (40, 70, juce::jmin (cellW - 16, row1.getHeight() - 22));
                int col = 0;
                for (int i = 0; i < allCombos.size(); ++i)
                {
                    if (comboStage[i] != 1) continue;
                    int x = row1.getX() + col * cellW;
                    comboLabels[i]->setBounds (x, row1.getY(), cellW, 14);
                    allCombos[i]->setBounds (x + 8, row1.getY() + 16, cellW - 16, 24);
                    col++;
                }
                for (int i = 0; i < allSliders.size(); ++i)
                {
                    if (stageForControl[i] != 1) continue;
                    int x = row1.getX() + col * cellW;
                    allLabels[i]->setBounds (x, row1.getY(), cellW, 14);
                    allSliders[i]->setBounds (x + (cellW - knobSz) / 2, row1.getY() + 16, knobSz, knobSz);
                    col++;
                }
            }
        }

        // Row 2: MEQ-5
        {
            int nKnobs = 0;
            for (int i = 0; i < allSliders.size(); ++i)
                if (stageForControl[i] == kPultecMEQ) nKnobs++;
            if (nKnobs > 0)
            {
                int cellW = row2.getWidth() / nKnobs;
                int knobSz = juce::jlimit (40, 70, juce::jmin (cellW - 16, row2.getHeight() - 22));
                int col = 0;
                for (int i = 0; i < allSliders.size(); ++i)
                {
                    if (stageForControl[i] != kPultecMEQ) continue;
                    int x = row2.getX() + col * cellW;
                    allLabels[i]->setBounds (x, row2.getY(), cellW, 14);
                    allSliders[i]->setBounds (x + (cellW - knobSz) / 2, row2.getY() + 16, knobSz, knobSz);
                    col++;
                }
            }
        }
        return;
    }

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
        if (controlStage == kImager) return false;
        if (controlStage == kEQSide) return false;
        if (controlStage == kPultecMEQ) return false; // handled by custom layout
        if (controlStage == currentStage) return true;
        if (currentStage == kSatCommon)
        {
            if (controlStage == kSatCommon) return true;
            if (controlStage == kSatSingle) return true;
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

    // Enforce minimum cell height to prevent label overlap
    int minCellH = 110;
    int cols = juce::jmin (totalControls, 8);
    int rows = (totalControls + cols - 1) / cols;

    // If cells would be too small, use fewer columns
    while (rows > 1 && (panelArea.getHeight() / rows) < minCellH && cols > 4)
    {
        cols--;
        rows = (totalControls + cols - 1) / cols;
    }

    int cellW = panelArea.getWidth() / cols;
    int cellH = panelArea.getHeight() / juce::jmax (rows, 1);
    cellH = juce::jmax (cellH, minCellH);

    // Knob size: square, fits inside cell with room for label (14px) + gap (4px)
    int knobSize = juce::jmin (cellW - 16, cellH - 22);
    knobSize = juce::jlimit (40, 80, knobSize);

    int col = 0, row = 0;

    // Layout combos first
    for (int i = 0; i < allCombos.size(); ++i)
    {
        if (! isVisible (comboStage[i])) continue;
        int x = panelArea.getX() + col * cellW;
        int y = panelArea.getY() + row * cellH;
        comboLabels[i]->setBounds (x, y, cellW, 14);
        allCombos[i]->setBounds (x + 8, y + 16, cellW - 16, 24);
        col++;
        if (col >= cols) { col = 0; row++; }
    }

    // Layout knobs — label above, knob centered below
    for (int i = 0; i < allSliders.size(); ++i)
    {
        if (! isVisible (stageForControl[i])) continue;
        int x = panelArea.getX() + col * cellW;
        int y = panelArea.getY() + row * cellH;
        allLabels[i]->setBounds (x, y, cellW, 14);
        allSliders[i]->setBounds (x + (cellW - knobSize) / 2, y + 16, knobSize, knobSize);
        col++;
        if (col >= cols) { col = 0; row++; }
    }

    // Layout inline toggles
    for (int i = 0; i < inlineToggles.size(); ++i)
    {
        if (! isVisible (toggleStage[i])) continue;
        int x = panelArea.getX() + col * cellW;
        int y = panelArea.getY() + row * cellH;
        inlineToggles[i]->setBounds (x + 8, y + 16, cellW - 16, 24);
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
            allSliders[i]->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
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
    // ─── Top row: Mode combo + XOver Phase combo ───
    auto topRow = panelArea.removeFromTop (50);
    for (int i = 0; i < allCombos.size(); ++i)
    {
        if (comboStage[i] == kSatCommon && allCombos[i]->isVisible())
        {
            comboLabels[i]->setBounds (topRow.getX(), topRow.getY(), 80, 16);
            allCombos[i]->setBounds (topRow.getX() + 4, topRow.getY() + 18, 130, 28);
        }
    }
    // Position XOver Phase combo (it's a kSatMulti combo but NOT a per-band one)
    for (int i = 0; i < allCombos.size(); ++i)
    {
        if (comboStage[i] == kSatMulti && !allCombos[i]->getName().contains ("_B"))
        {
            comboLabels[i]->setBounds (topRow.getX() + 150, topRow.getY(), 100, 16);
            allCombos[i]->setBounds (topRow.getX() + 154, topRow.getY() + 18, 140, 28);
            allCombos[i]->setVisible (true);
            comboLabels[i]->setVisible (true);
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

void EasyMasterEditor::updateEQKnobAttachments (int mode)
{
    if (eqKnobStartIdx < 0) return;

    // 15 EQ knobs: 5 bands × (Freq, Gain, Q)
    juce::String stereoIDs[] = {
        "S5_EQ2_LowShelf_Freq", "S5_EQ2_LowShelf_Gain", "S5_EQ2_LowShelf_Q",
        "S5_EQ2_LowMid_Freq", "S5_EQ2_LowMid_Gain", "S5_EQ2_LowMid_Q",
        "S5_EQ2_Mid_Freq", "S5_EQ2_Mid_Gain", "S5_EQ2_Mid_Q",
        "S5_EQ2_HighMid_Freq", "S5_EQ2_HighMid_Gain", "S5_EQ2_HighMid_Q",
        "S5_EQ2_HighShelf_Freq", "S5_EQ2_HighShelf_Gain", "S5_EQ2_HighShelf_Q"
    };
    juce::String midIDs[] = {
        "S5_EQ2_M_LS_Freq", "S5_EQ2_M_LS_Gain", "S5_EQ2_M_LS_Q",
        "S5_EQ2_M_LM_Freq", "S5_EQ2_M_LM_Gain", "S5_EQ2_M_LM_Q",
        "S5_EQ2_M_Mid_Freq", "S5_EQ2_M_Mid_Gain", "S5_EQ2_M_Mid_Q",
        "S5_EQ2_M_HM_Freq", "S5_EQ2_M_HM_Gain", "S5_EQ2_M_HM_Q",
        "S5_EQ2_M_HS_Freq", "S5_EQ2_M_HS_Gain", "S5_EQ2_M_HS_Q"
    };
    juce::String sideIDs[] = {
        "S5_EQ2_S_LS_Freq", "S5_EQ2_S_LS_Gain", "S5_EQ2_S_LS_Q",
        "S5_EQ2_S_LM_Freq", "S5_EQ2_S_LM_Gain", "S5_EQ2_S_LM_Q",
        "S5_EQ2_S_Mid_Freq", "S5_EQ2_S_Mid_Gain", "S5_EQ2_S_Mid_Q",
        "S5_EQ2_S_HM_Freq", "S5_EQ2_S_HM_Gain", "S5_EQ2_S_HM_Q",
        "S5_EQ2_S_HS_Freq", "S5_EQ2_S_HS_Gain", "S5_EQ2_S_HS_Q"
    };

    auto& ids = (mode == 1) ? midIDs : (mode == 2) ? sideIDs : stereoIDs;
    auto& apvts = processor.getAPVTS();

    for (int i = 0; i < 15; ++i)
    {
        int idx = eqKnobStartIdx + i;
        if (idx < allAttachments.size())
        {
            // Destroy old attachment, create new one
            allAttachments.set (idx, nullptr);
            allAttachments.set (idx, new juce::AudioProcessorValueTreeState::SliderAttachment (
                apvts, ids[i], *allSliders[idx]));
        }
    }
}

void EasyMasterEditor::updatePultecKnobAttachments (int mode)
{
    if (pultecKnobStartIdx < 0 || pultecComboStartIdx < 0) return;
    auto& apvts = processor.getAPVTS();

    // 3 combos: LowBoost_Freq, HighBoost_Freq, HighAtten_Freq
    juce::String stereoComboIDs[] = { "S2_EQ_LowBoost_Freq", "S2_EQ_HighBoost_Freq", "S2_EQ_HighAtten_Freq" };
    juce::String midComboIDs[]    = { "S2_EQ_M_LowBoost_Freq", "S2_EQ_M_HighBoost_Freq", "S2_EQ_M_HighAtten_Freq" };
    juce::String sideComboIDs[]   = { "S2_EQ_S_LowBoost_Freq", "S2_EQ_S_HighBoost_Freq", "S2_EQ_S_HighAtten_Freq" };
    auto& cids = (mode == 1) ? midComboIDs : (mode == 2) ? sideComboIDs : stereoComboIDs;
    for (int i = 0; i < 3; ++i)
    {
        int idx = pultecComboStartIdx + i;
        if (idx < comboAttachments.size())
        {
            comboAttachments.set (idx, nullptr);
            comboAttachments.set (idx, new juce::AudioProcessorValueTreeState::ComboBoxAttachment (
                apvts, cids[i], *allCombos[idx]));
        }
    }

    // 11 knobs: LowBoost_Gain, LowAtten_Gain, HighBoost_Gain, HighAtten_Gain, HighAtten_BW,
    //           LowMid_Freq, LowMid_Gain, MidDip_Freq, MidDip_Gain, HighMid_Freq, HighMid_Gain
    juce::String stereoKnobIDs[] = {
        "S2_EQ_LowBoost_Gain", "S2_EQ_LowAtten_Gain",
        "S2_EQ_HighBoost_Gain", "S2_EQ_HighAtten_Gain", "S2_EQ_HighAtten_BW",
        "S2_EQ_LowMid_Freq", "S2_EQ_LowMid_Gain",
        "S2_EQ_MidDip_Freq", "S2_EQ_MidDip_Gain",
        "S2_EQ_HighMid_Freq", "S2_EQ_HighMid_Gain"
    };
    juce::String midKnobIDs[] = {
        "S2_EQ_M_LowBoost_Gain", "S2_EQ_M_LowAtten_Gain",
        "S2_EQ_M_HighBoost_Gain", "S2_EQ_M_HighAtten_Gain", "S2_EQ_M_HighAtten_BW",
        "S2_EQ_M_LowMid_Freq", "S2_EQ_M_LowMid_Gain",
        "S2_EQ_M_MidDip_Freq", "S2_EQ_M_MidDip_Gain",
        "S2_EQ_M_HighMid_Freq", "S2_EQ_M_HighMid_Gain"
    };
    juce::String sideKnobIDs[] = {
        "S2_EQ_S_LowBoost_Gain", "S2_EQ_S_LowAtten_Gain",
        "S2_EQ_S_HighBoost_Gain", "S2_EQ_S_HighAtten_Gain", "S2_EQ_S_HighAtten_BW",
        "S2_EQ_S_LowMid_Freq", "S2_EQ_S_LowMid_Gain",
        "S2_EQ_S_MidDip_Freq", "S2_EQ_S_MidDip_Gain",
        "S2_EQ_S_HighMid_Freq", "S2_EQ_S_HighMid_Gain"
    };
    auto& kids = (mode == 1) ? midKnobIDs : (mode == 2) ? sideKnobIDs : stereoKnobIDs;
    for (int i = 0; i < 11; ++i)
    {
        int idx = pultecKnobStartIdx + i;
        if (idx < allAttachments.size())
        {
            allAttachments.set (idx, nullptr);
            allAttachments.set (idx, new juce::AudioProcessorValueTreeState::SliderAttachment (
                apvts, kids[i], *allSliders[idx]));
        }
    }
}

void EasyMasterEditor::updateSatKnobAttachments (int mode)
{
    if (satKnobStartIdx < 0 || satComboStartIdx < 0) return;
    auto& apvts = processor.getAPVTS();

    // 1 combo: Type
    juce::String stereoComboID = "S4_Sat_Type";
    juce::String midComboID    = "S4_Sat_M_Type";
    juce::String sideComboID   = "S4_Sat_S_Type";
    auto& cid = (mode == 1) ? midComboID : (mode == 2) ? sideComboID : stereoComboID;
    if (satComboStartIdx < comboAttachments.size())
    {
        comboAttachments.set (satComboStartIdx, nullptr);
        comboAttachments.set (satComboStartIdx, new juce::AudioProcessorValueTreeState::ComboBoxAttachment (
            apvts, cid, *allCombos[satComboStartIdx]));
    }

    // 3 knobs: Drive, Output, Blend
    juce::String stereoKnobIDs[] = { "S4_Sat_Drive", "S4_Sat_Output", "S4_Sat_Blend" };
    juce::String midKnobIDs[]    = { "S4_Sat_M_Drive", "S4_Sat_M_Output", "S4_Sat_M_Blend" };
    juce::String sideKnobIDs[]   = { "S4_Sat_S_Drive", "S4_Sat_S_Output", "S4_Sat_S_Blend" };
    auto& kids = (mode == 1) ? midKnobIDs : (mode == 2) ? sideKnobIDs : stereoKnobIDs;
    for (int i = 0; i < 3; ++i)
    {
        int idx = satKnobStartIdx + i;
        if (idx < allAttachments.size())
        {
            allAttachments.set (idx, nullptr);
            allAttachments.set (idx, new juce::AudioProcessorValueTreeState::SliderAttachment (
                apvts, kids[i], *allSliders[idx]));
        }
    }
}

void EasyMasterEditor::updateFilterKnobAttachments (int mode)
{
    if (filterKnobStartIdx < 0 || filterComboStartIdx < 0 || filterToggleStartIdx < 0) return;
    auto& apvts = processor.getAPVTS();

    // 2 toggles: HP On, LP On
    juce::String stereoTogIDs[] = { "S6_HP_On", "S6_LP_On" };
    juce::String midTogIDs[]    = { "S6_HP_M_On", "S6_LP_M_On" };
    juce::String sideTogIDs[]   = { "S6_HP_S_On", "S6_LP_S_On" };
    auto& tids = (mode == 1) ? midTogIDs : (mode == 2) ? sideTogIDs : stereoTogIDs;
    for (int i = 0; i < 2; ++i)
    {
        int idx = filterToggleStartIdx + i;
        if (idx < inlineToggleAttachments.size())
        {
            inlineToggleAttachments.set (idx, nullptr);
            inlineToggleAttachments.set (idx, new juce::AudioProcessorValueTreeState::ButtonAttachment (
                apvts, tids[i], *inlineToggles[idx]));
        }
    }

    // 2 knobs: HP Freq, LP Freq
    juce::String stereoKnobIDs[] = { "S6_HP_Freq", "S6_LP_Freq" };
    juce::String midKnobIDs[]    = { "S6_HP_M_Freq", "S6_LP_M_Freq" };
    juce::String sideKnobIDs[]   = { "S6_HP_S_Freq", "S6_LP_S_Freq" };
    auto& kids = (mode == 1) ? midKnobIDs : (mode == 2) ? sideKnobIDs : stereoKnobIDs;
    for (int i = 0; i < 2; ++i)
    {
        int idx = filterKnobStartIdx + i;
        if (idx < allAttachments.size())
        {
            allAttachments.set (idx, nullptr);
            allAttachments.set (idx, new juce::AudioProcessorValueTreeState::SliderAttachment (
                apvts, kids[i], *allSliders[idx]));
        }
    }

    // 2 combos: HP Slope, LP Slope
    juce::String stereoComboIDs[] = { "S6_HP_Slope", "S6_LP_Slope" };
    juce::String midComboIDs[]    = { "S6_HP_M_Slope", "S6_LP_M_Slope" };
    juce::String sideComboIDs[]   = { "S6_HP_S_Slope", "S6_LP_S_Slope" };
    auto& cids = (mode == 1) ? midComboIDs : (mode == 2) ? sideComboIDs : stereoComboIDs;
    for (int i = 0; i < 2; ++i)
    {
        int idx = filterComboStartIdx + i;
        if (idx < comboAttachments.size())
        {
            comboAttachments.set (idx, nullptr);
            comboAttachments.set (idx, new juce::AudioProcessorValueTreeState::ComboBoxAttachment (
                apvts, cids[i], *allCombos[idx]));
        }
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

    // Update A/B button state
    abButton.setEnabled (processor.hasReference());
    if (processor.isABActive())
        abButton.setButtonText ("B (REF)");
    else
        abButton.setButtonText ("A/B");

    // Check if EQ Channel combo changed — swap knob attachments
    if (currentStage == 4)
    {
        int eqMs = (int) processor.getAPVTS().getRawParameterValue ("S5_EQ2_MS")->load();
        if (eqMs != lastEqMsMode)
        {
            lastEqMsMode = eqMs;
            updateEQKnobAttachments (eqMs);
        }
    }

    // Check if Pultec Channel combo changed — swap knob attachments
    if (currentStage == 1 || currentStage == kPultecMEQ)
    {
        int pMs = (int) processor.getAPVTS().getRawParameterValue ("S2_EQ_MS")->load();
        if (pMs != lastPultecMsMode)
        {
            lastPultecMsMode = pMs;
            updatePultecKnobAttachments (pMs);
        }
    }

    // Check if SAT Channel combo changed — swap knob attachments
    if (currentStage == kSatCommon || currentStage == kSatSingle)
    {
        int sMs = (int) processor.getAPVTS().getRawParameterValue ("S4_Sat_MS")->load();
        if (sMs != lastSatMsMode)
        {
            lastSatMsMode = sMs;
            updateSatKnobAttachments (sMs);
        }
    }

    // Check if Filter Channel combo changed — swap knob attachments
    if (currentStage == 5)
    {
        int fMs = (int) processor.getAPVTS().getRawParameterValue ("S6_Filter_MS")->load();
        if (fMs != lastFilterMsMode)
        {
            lastFilterMsMode = fMs;
            updateFilterKnobAttachments (fMs);
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
    // ─── Reference waveform click-to-seek (INPUT stage) ───
    if (currentStage == 0 && processor.hasReference() && waveformDisplayArea.getWidth() > 0)
    {
        auto pos = e.position;
        float wfX = waveformDisplayArea.getX() + 6.0f;
        float wfW = waveformDisplayArea.getWidth() - 12.0f;
        if (pos.y >= waveformDisplayArea.getY() && pos.y <= waveformDisplayArea.getBottom()
            && pos.x >= wfX && pos.x <= wfX + wfW)
        {
            float normPos = (pos.x - wfX) / wfW;
            processor.setRefPlayPosition (juce::jlimit (0.0f, 1.0f, normPos));
            repaint();
            return;
        }
    }

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
                int eqMs = (int) processor.getAPVTS().getRawParameterValue ("S5_EQ2_MS")->load();
                draggingEQNode = -1;
                float bestDist = 20.0f;
                for (int b = 0; b < OutputEQStage::NUM_BANDS; ++b)
                {
                    auto bi = (eqMs == 1) ? outEQ->getBandInfoMid (b) :
                              (eqMs == 2) ? outEQ->getBandInfoSide (b) :
                                            outEQ->getBandInfo (b);
                    double mag = (eqMs == 1) ? outEQ->getMagnitudeAtFreqMid ((double) bi.freq) :
                                 (eqMs == 2) ? outEQ->getMagnitudeAtFreqSide ((double) bi.freq) :
                                               outEQ->getMagnitudeAtFreq ((double) bi.freq);
                    float nodeX = freqToX (bi.freq, eqDisplayArea.getX(), eqDisplayArea.getWidth());
                    float nodeY = eqDisplayArea.getY() + eqDisplayArea.getHeight() * 0.5f
                                  - (float)(mag / eqDbRange) * (eqDisplayArea.getHeight() * 0.5f);
                    float dist = std::sqrt ((pos.x - nodeX) * (pos.x - nodeX) + (pos.y - nodeY) * (pos.y - nodeY));
                    if (dist < bestDist) { bestDist = dist; draggingEQNode = b; }
                }
                if (draggingEQNode >= 0) return;
            }
        }
    }

    // ─── Filter node dragging (stage 5) ───
    if (currentStage == 5)
    {
        auto pos = e.position;
        if (filterDisplayArea.getWidth() > 0 && filterDisplayArea.expanded (10).contains (pos))
        {
            float specX = filterDisplayArea.getX() + 30.0f;
            float specW = filterDisplayArea.getWidth() - 36.0f;
            int fltMs = (int) processor.getAPVTS().getRawParameterValue ("S6_Filter_MS")->load();

            juce::String hpFrID = (fltMs==1)?"S6_HP_M_Freq":(fltMs==2)?"S6_HP_S_Freq":"S6_HP_Freq";
            juce::String lpFrID = (fltMs==1)?"S6_LP_M_Freq":(fltMs==2)?"S6_LP_S_Freq":"S6_LP_Freq";
            juce::String hpOnID = (fltMs==1)?"S6_HP_M_On":(fltMs==2)?"S6_HP_S_On":"S6_HP_On";
            juce::String lpOnID = (fltMs==1)?"S6_LP_M_On":(fltMs==2)?"S6_LP_S_On":"S6_LP_On";

            draggingFilterNode = -1;
            float bestDist = 25.0f;

            bool hpActive = processor.getAPVTS().getRawParameterValue(hpOnID)->load() > 0.5f;
            bool lpActive = processor.getAPVTS().getRawParameterValue(lpOnID)->load() > 0.5f;

            // Check distance to existing nodes
            if (hpActive) {
                float hpF = processor.getAPVTS().getRawParameterValue(hpFrID)->load();
                float dist = std::abs(pos.x - freqToX(hpF, specX, specW));
                if (dist < bestDist) { bestDist = dist; draggingFilterNode = 0; }
            }
            if (lpActive) {
                float lpF = processor.getAPVTS().getRawParameterValue(lpFrID)->load();
                float dist = std::abs(pos.x - freqToX(lpF, specX, specW));
                if (dist < bestDist) { bestDist = dist; draggingFilterNode = 1; }
            }

            // If no node found, double-click enables nearest filter at click position
            if (draggingFilterNode < 0 && e.getNumberOfClicks() >= 2)
            {
                float clickFreq = xToFreq(pos.x, specX, specW);
                float midFreq = xToFreq(specX + specW * 0.5f, specX, specW);

                if (clickFreq < midFreq) {
                    // Left half → enable HP
                    float freq = juce::jlimit(20.0f, 2000.0f, clickFreq);
                    if (auto* pOn = processor.getAPVTS().getParameter(hpOnID))
                        pOn->setValueNotifyingHost(1.0f);
                    if (auto* pF = processor.getAPVTS().getParameter(hpFrID))
                        pF->setValueNotifyingHost(pF->getNormalisableRange().convertTo0to1(freq));
                    draggingFilterNode = 0;
                } else {
                    // Right half → enable LP
                    float freq = juce::jlimit(200.0f, 20000.0f, clickFreq);
                    if (auto* pOn = processor.getAPVTS().getParameter(lpOnID))
                        pOn->setValueNotifyingHost(1.0f);
                    if (auto* pF = processor.getAPVTS().getParameter(lpFrID))
                        pF->setValueNotifyingHost(pF->getNormalisableRange().convertTo0to1(freq));
                    draggingFilterNode = 1;
                }
            }

            if (draggingFilterNode >= 0) return;
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
    // ─── Reference waveform drag-to-seek ───
    if (currentStage == 0 && processor.hasReference() && waveformDisplayArea.getWidth() > 0)
    {
        float wfX = waveformDisplayArea.getX() + 6.0f;
        float wfW = waveformDisplayArea.getWidth() - 12.0f;
        if (e.position.y >= waveformDisplayArea.getY() && e.position.y <= waveformDisplayArea.getBottom())
        {
            float normPos = (e.position.x - wfX) / wfW;
            processor.setRefPlayPosition (juce::jlimit (0.0f, 1.0f, normPos));
            repaint();
            return;
        }
    }

    // ─── Filter node drag (stage 5) ───
    if (draggingFilterNode >= 0)
    {
        if (filterDisplayArea.getWidth() <= 0) return;
        float specX = filterDisplayArea.getX() + 30.0f;
        float specW = filterDisplayArea.getWidth() - 36.0f;
        float freq = xToFreq (e.position.x, specX, specW);

        int fltMs = (int) processor.getAPVTS().getRawParameterValue ("S6_Filter_MS")->load();
        juce::String paramID;
        if (draggingFilterNode == 0) // HP
        {
            freq = juce::jlimit (20.0f, 2000.0f, freq);
            paramID = (fltMs == 1) ? "S6_HP_M_Freq" : (fltMs == 2) ? "S6_HP_S_Freq" : "S6_HP_Freq";
        }
        else // LP
        {
            freq = juce::jlimit (200.0f, 20000.0f, freq);
            paramID = (fltMs == 1) ? "S6_LP_M_Freq" : (fltMs == 2) ? "S6_LP_S_Freq" : "S6_LP_Freq";
        }
        if (auto* p = processor.getAPVTS().getParameter (paramID))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (freq));
        repaint();
        return;
    }

    // ─── Output EQ node drag ───
    if (draggingEQNode >= 0)
    {
        if (eqDisplayArea.getWidth() <= 0) return;
        float sx = eqDisplayArea.getX(), sw = eqDisplayArea.getWidth();
        float sy = eqDisplayArea.getY(), sh = eqDisplayArea.getHeight();

        int eqMs = (int) processor.getAPVTS().getRawParameterValue ("S5_EQ2_MS")->load();

        juce::String freqIDs[3][5] = {
            { "S5_EQ2_LowShelf_Freq", "S5_EQ2_LowMid_Freq", "S5_EQ2_Mid_Freq", "S5_EQ2_HighMid_Freq", "S5_EQ2_HighShelf_Freq" },
            { "S5_EQ2_M_LS_Freq", "S5_EQ2_M_LM_Freq", "S5_EQ2_M_Mid_Freq", "S5_EQ2_M_HM_Freq", "S5_EQ2_M_HS_Freq" },
            { "S5_EQ2_S_LS_Freq", "S5_EQ2_S_LM_Freq", "S5_EQ2_S_Mid_Freq", "S5_EQ2_S_HM_Freq", "S5_EQ2_S_HS_Freq" }
        };
        juce::String gainIDs[3][5] = {
            { "S5_EQ2_LowShelf_Gain", "S5_EQ2_LowMid_Gain", "S5_EQ2_Mid_Gain", "S5_EQ2_HighMid_Gain", "S5_EQ2_HighShelf_Gain" },
            { "S5_EQ2_M_LS_Gain", "S5_EQ2_M_LM_Gain", "S5_EQ2_M_Mid_Gain", "S5_EQ2_M_HM_Gain", "S5_EQ2_M_HS_Gain" },
            { "S5_EQ2_S_LS_Gain", "S5_EQ2_S_LM_Gain", "S5_EQ2_S_Mid_Gain", "S5_EQ2_S_HM_Gain", "S5_EQ2_S_HS_Gain" }
        };
        juce::String qIDs[3][5] = {
            { "S5_EQ2_LowShelf_Q", "S5_EQ2_LowMid_Q", "S5_EQ2_Mid_Q", "S5_EQ2_HighMid_Q", "S5_EQ2_HighShelf_Q" },
            { "S5_EQ2_M_LS_Q", "S5_EQ2_M_LM_Q", "S5_EQ2_M_Mid_Q", "S5_EQ2_M_HM_Q", "S5_EQ2_M_HS_Q" },
            { "S5_EQ2_S_LS_Q", "S5_EQ2_S_LM_Q", "S5_EQ2_S_Mid_Q", "S5_EQ2_S_HM_Q", "S5_EQ2_S_HS_Q" }
        };

        int set = juce::jlimit (0, 2, eqMs);

        if (e.mods.isAltDown())
        {
            float normY = 1.0f - (e.position.y - sy) / sh;
            float newQ = juce::jmap (normY, 0.0f, 1.0f, 0.1f, 10.0f);
            newQ = juce::jlimit (0.1f, 10.0f, newQ);
            if (auto* qp = processor.getAPVTS().getParameter (qIDs[set][draggingEQNode]))
                qp->setValueNotifyingHost (qp->convertTo0to1 (newQ));
        }
        else
        {
            float newFreq = xToFreq (e.position.x, sx, sw);
            float normY = 1.0f - (e.position.y - sy) / sh;
            float newGain = (normY - 0.5f) * 2.0f * eqDbRange;
            newGain = juce::jlimit (-12.0f, 12.0f, newGain);

            if (auto* fp = processor.getAPVTS().getParameter (freqIDs[set][draggingEQNode]))
                fp->setValueNotifyingHost (fp->convertTo0to1 (newFreq));
            if (auto* gp = processor.getAPVTS().getParameter (gainIDs[set][draggingEQNode]))
                gp->setValueNotifyingHost (gp->convertTo0to1 (newGain));
        }

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
    draggingFilterNode = -1;
    repaint();
}

void EasyMasterEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // ─── Filter: scroll wheel changes slope of nearest node ───
    if (currentStage == 5 && filterDisplayArea.getWidth() > 0)
    {
        auto pos = e.position;
        if (filterDisplayArea.expanded (20).contains (pos))
        {
            float specX = filterDisplayArea.getX() + 30.0f;
            float specW = filterDisplayArea.getWidth() - 36.0f;
            int fltMs = (int) processor.getAPVTS().getRawParameterValue ("S6_Filter_MS")->load();

            // Find nearest node
            juce::String hpOnID = (fltMs==1)?"S6_HP_M_On":(fltMs==2)?"S6_HP_S_On":"S6_HP_On";
            juce::String lpOnID = (fltMs==1)?"S6_LP_M_On":(fltMs==2)?"S6_LP_S_On":"S6_LP_On";
            juce::String hpFID = (fltMs==1)?"S6_HP_M_Freq":(fltMs==2)?"S6_HP_S_Freq":"S6_HP_Freq";
            juce::String lpFID = (fltMs==1)?"S6_LP_M_Freq":(fltMs==2)?"S6_LP_S_Freq":"S6_LP_Freq";
            juce::String hpSlID = (fltMs==1)?"S6_HP_M_Slope":(fltMs==2)?"S6_HP_S_Slope":"S6_HP_Slope";
            juce::String lpSlID = (fltMs==1)?"S6_LP_M_Slope":(fltMs==2)?"S6_LP_S_Slope":"S6_LP_Slope";

            int nearest = -1; float bestD = 30.0f;
            if (processor.getAPVTS().getRawParameterValue(hpOnID)->load() > 0.5f) {
                float xp = freqToX(processor.getAPVTS().getRawParameterValue(hpFID)->load(), specX, specW);
                float d = std::abs(pos.x - xp); if (d < bestD) { bestD = d; nearest = 0; }
            }
            if (processor.getAPVTS().getRawParameterValue(lpOnID)->load() > 0.5f) {
                float xp = freqToX(processor.getAPVTS().getRawParameterValue(lpFID)->load(), specX, specW);
                float d = std::abs(pos.x - xp); if (d < bestD) { bestD = d; nearest = 1; }
            }

            if (nearest >= 0) {
                juce::String slopeID = (nearest == 0) ? hpSlID : lpSlID;
                int curSlope = (int) processor.getAPVTS().getRawParameterValue(slopeID)->load();
                int newSlope = curSlope + (wheel.deltaY > 0 ? 1 : -1);
                newSlope = juce::jlimit(0, 4, newSlope);
                if (auto* p = processor.getAPVTS().getParameter(slopeID))
                    p->setValueNotifyingHost ((float) newSlope / 4.0f);
                repaint();
                return;
            }
        }
    }

    // ─── Output EQ: scroll wheel changes Q of nearest node ───
    if (currentStage == 4 && eqDisplayArea.getWidth() > 0)
    {
        auto pos = e.position;
        if (eqDisplayArea.expanded (20).contains (pos))
        {
            auto* outEQ = dynamic_cast<OutputEQStage*> (
                processor.getEngine().getStage (ProcessingStage::StageID::OutputEQ));
            if (outEQ)
            {
                int eqMs = (int) processor.getAPVTS().getRawParameterValue ("S5_EQ2_MS")->load();

                // Find nearest node from the ACTIVE set
                int nearest = -1;
                float bestDist = 30.0f;
                for (int b = 0; b < OutputEQStage::NUM_BANDS; ++b)
                {
                    auto bi = (eqMs == 1) ? outEQ->getBandInfoMid (b) :
                              (eqMs == 2) ? outEQ->getBandInfoSide (b) :
                                            outEQ->getBandInfo (b);
                    double mag = (eqMs == 1) ? outEQ->getMagnitudeAtFreqMid ((double) bi.freq) :
                                 (eqMs == 2) ? outEQ->getMagnitudeAtFreqSide ((double) bi.freq) :
                                               outEQ->getMagnitudeAtFreq ((double) bi.freq);
                    float nodeX = freqToX (bi.freq, eqDisplayArea.getX(), eqDisplayArea.getWidth());
                    float nodeY = eqDisplayArea.getY() + eqDisplayArea.getHeight() * 0.5f
                                  - (float)(mag / eqDbRange) * (eqDisplayArea.getHeight() * 0.5f);
                    float dist = std::sqrt ((pos.x - nodeX) * (pos.x - nodeX) + (pos.y - nodeY) * (pos.y - nodeY));
                    if (dist < bestDist) { bestDist = dist; nearest = b; }
                }

                if (nearest >= 0)
                {
                    juce::String qIDs[3][5] = {
                        { "S5_EQ2_LowShelf_Q", "S5_EQ2_LowMid_Q", "S5_EQ2_Mid_Q", "S5_EQ2_HighMid_Q", "S5_EQ2_HighShelf_Q" },
                        { "S5_EQ2_M_LS_Q", "S5_EQ2_M_LM_Q", "S5_EQ2_M_Mid_Q", "S5_EQ2_M_HM_Q", "S5_EQ2_M_HS_Q" },
                        { "S5_EQ2_S_LS_Q", "S5_EQ2_S_LM_Q", "S5_EQ2_S_Mid_Q", "S5_EQ2_S_HM_Q", "S5_EQ2_S_HS_Q" }
                    };
                    int set = juce::jlimit (0, 2, eqMs);
                    auto& qID = qIDs[set][nearest];
                    if (auto* qp = processor.getAPVTS().getParameter (qID))
                    {
                        float curQ = processor.getAPVTS().getRawParameterValue (qID)->load();
                        float delta = wheel.deltaY * 0.5f;
                        float newQ = juce::jlimit (0.1f, 10.0f, curQ + delta);
                        qp->setValueNotifyingHost (qp->convertTo0to1 (newQ));
                        repaint();
                    }
                }
            }
            return;
        }
    }
    // Default: pass to parent
    Component::mouseWheelMove (e, wheel);
}
