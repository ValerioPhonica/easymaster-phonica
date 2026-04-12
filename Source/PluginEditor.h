#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/LookAndFeel/GrooveBoxLookAndFeel.h"
#include "UI/Components/HeaderBar.h"
#include "UI/Components/DrumTrackRow.h"
#include "UI/Components/SynthTrackRow.h"
#include "UI/Components/MasterFXRow.h"

// Inner component that holds all the tracks — lives inside a Viewport
class MainContent : public juce::Component
{
public:
    MainContent (GrooveBoxProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;

    HeaderBar headerBar; // Created here, but reparented to editor for fixed positioning
    std::function<void()> onPresetLoaded; // called after preset load/init to refresh MasterFXRow
    juce::String lastGlobalPresetName, lastGlobalPresetFolder;
    juce::OwnedArray<DrumTrackRow> drumRows;
    juce::OwnedArray<SynthTrackRow> synthRows;
    juce::Label drumSectionLabel;
    juce::Label synthSectionLabel;

    GrooveBoxProcessor& processorRef;

    int getDesiredHeight() const;
};

class GrooveBoxEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit GrooveBoxEditor (GrooveBoxProcessor&);
    ~GrooveBoxEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    GrooveBoxProcessor& processorRef;
    GrooveBoxLookAndFeel lookAndFeel;

    juce::Viewport viewport;
    MainContent content;
    MasterFXRow masterFX;

    // MIX OUT meter extension — drawn below HeaderBar on right side
    juce::Rectangle<int> meterExtBounds;
    float meterPeakL = 0, meterPeakR = 0, meterRmsL = 0, meterRmsR = 0;
    float meterCompGR = 0, meterLimGR = 0;

    // MIX volume knob (lives in PluginEditor, positioned in meter area)
    juce::Slider mixKnob;

    // Zoom
    float zoomFactor = 1.0f;
    static constexpr int baseWidth = 1300;
    static constexpr int baseHeight = 750;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveBoxEditor)
};
