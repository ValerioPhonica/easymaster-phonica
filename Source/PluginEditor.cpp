#include "PluginProcessor.h"

EasyMasterEditor::EasyMasterEditor (EasyMasterProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setSize (1200, 700);
    setResizable (true, true);
    setResizeLimits (900, 500, 1920, 1080);

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
    lufsLabel.setFont (juce::Font (18.0f));
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setText ("--.-- LUFS", juce::dontSendNotification);

    addAndMakeVisible (truePeakLabel);
    truePeakLabel.setFont (juce::Font (14.0f));
    truePeakLabel.setJustificationType (juce::Justification::centred);
    truePeakLabel.setText ("TP: --.-- dB", juce::dontSendNotification);

    addAndMakeVisible (masterOutputSlider);
    masterOutputSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterOutputSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    masterOutputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), "Master_Output_Gain", masterOutputSlider);

    startTimerHz (30);
}

EasyMasterEditor::~EasyMasterEditor() { stopTimer(); }

void EasyMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF1A1A2E));

    g.setColour (juce::Colour (0xFF16213E));
    g.fillRect (0, 0, getWidth(), 50);
    g.fillRect (0, getHeight() - 60, getWidth(), 60);

    g.setColour (juce::Colour (0xFFE94560));
    g.setFont (juce::Font (24.0f));
    g.drawText ("EASY MASTER", 10, 10, 200, 30, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xFF999999));
    g.setFont (juce::Font (10.0f));
    g.drawText ("by Phonica School", 10, 35, 200, 15, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xFF222244));
    g.fillRoundedRectangle (10.0f, 60.0f, (float) getWidth() - 20.0f, (float) getHeight() - 130.0f, 8.0f);

    auto stageArea = getLocalBounds().reduced (20, 80).withTrimmedBottom (50);
    auto order = processor.getEngine().getStageOrder();

    int totalStages = 9;
    float stageW = (float)stageArea.getWidth() / (float)totalStages;
    float stageH = (float)stageArea.getHeight() * 0.6f;
    float stageY = (float)stageArea.getY() + ((float)stageArea.getHeight() - stageH) * 0.5f;

    juce::StringArray stageNames = { "INPUT" };
    juce::StringArray reorderNames = { "PULTEC EQ", "COMP", "SAT", "OUT EQ", "FILTER", "DYN RES", "CLIPPER" };
    for (int i = 0; i < 7; ++i) stageNames.add (reorderNames[order[(size_t)i]]);
    stageNames.add ("LIMITER");

    juce::Colour stageColors[] = {
        juce::Colour(0xFF2E86AB), juce::Colour(0xFF4ECDC4), juce::Colour(0xFFFF6B6B),
        juce::Colour(0xFFFFA07A), juce::Colour(0xFF95E1D3), juce::Colour(0xFF7B68EE),
        juce::Colour(0xFFDDA0DD), juce::Colour(0xFFFF4757), juce::Colour(0xFFE94560)
    };

    g.setFont (juce::Font (11.0f));
    for (int i = 0; i < totalStages; ++i)
    {
        float x = (float)stageArea.getX() + (float)i * stageW + 3.0f;
        auto col = stageColors[i];
        g.setColour (col.withAlpha (0.3f));
        g.fillRoundedRectangle (x, stageY, stageW - 6.0f, stageH, 6.0f);
        g.setColour (col);
        g.drawRoundedRectangle (x, stageY, stageW - 6.0f, stageH, 6.0f, 1.5f);
        g.setColour (juce::Colours::white);
        g.drawText (stageNames[i], (int)x, (int)(stageY + stageH * 0.4f), (int)(stageW - 6.0f), 20,
                    juce::Justification::centred);

        if (i < totalStages - 1)
        {
            float arrowX = x + stageW - 3.0f;
            float arrowY = stageY + stageH * 0.5f;
            g.setColour (juce::Colour (0xFF555577));
            g.drawArrow (juce::Line<float>(arrowX - 4.0f, arrowY, arrowX + 4.0f, arrowY), 1.5f, 6.0f, 6.0f);
        }
    }

    g.setColour (juce::Colour (0xFF777799));
    g.setFont (juce::Font (10.0f));
    g.drawText ("[ Drag stages to reorder - Input & Limiter are fixed ]",
                stageArea.getX(), (int)(stageY + stageH + 10.0f), stageArea.getWidth(), 20,
                juce::Justification::centred);
}

void EasyMasterEditor::resized()
{
    auto area = getLocalBounds();
    auto topBar = area.removeFromTop (50);
    topBar.removeFromLeft (220);
    presetSelector.setBounds (topBar.removeFromLeft (200).reduced (8));
    savePresetButton.setBounds (topBar.removeFromLeft (60).reduced (8));
    initButton.setBounds (topBar.removeFromLeft (60).reduced (8));
    lufsLabel.setBounds (topBar.removeFromRight (120).reduced (4));
    truePeakLabel.setBounds (topBar.removeFromRight (120).reduced (4));
    auto bottomBar = area.removeFromBottom (60);
    masterOutputSlider.setBounds (bottomBar.removeFromRight (100).reduced (4));
}

void EasyMasterEditor::timerCallback()
{
    float lufs = processor.getEngine().getLUFS();
    float tp = processor.getEngine().getTruePeak();
    lufsLabel.setText (lufs > -100.f ? juce::String (lufs, 1) + " LUFS" : "--.-- LUFS", juce::dontSendNotification);
    truePeakLabel.setText (tp > -100.f ? "TP: " + juce::String (tp, 1) + " dB" : "TP: --.-- dB", juce::dontSendNotification);
}
