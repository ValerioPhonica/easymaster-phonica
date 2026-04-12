#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Colours.h"
#include "../../State/GrooveBoxState.h"

class HeaderBar : public juce::Component
{
public:
    HeaderBar()
    {
        // Logo — custom painted in paint(), labels hidden
        addAndMakeVisible (logoLabel);
        logoLabel.setText ("", juce::dontSendNotification);
        logoLabel.setVisible (false);

        addAndMakeVisible (subtitleLabel);
        subtitleLabel.setText ("", juce::dontSendNotification);
        subtitleLabel.setVisible (false);

        // Status
        addAndMakeVisible (statusLabel);
        statusLabel.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
        statusLabel.setColour (juce::Label::textColourId, Colours_GB::accent);
        statusLabel.setColour (juce::Label::backgroundColourId, Colours_GB::bg);
        statusLabel.setJustificationType (juce::Justification::centred);
        statusLabel.setText ("STOP", juce::dontSendNotification);

        // BPM
        addAndMakeVisible (bpmLabel);
        bpmLabel.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
        bpmLabel.setColour (juce::Label::textColourId, Colours_GB::accentBright);
        bpmLabel.setColour (juce::Label::backgroundColourId, Colours_GB::bg);
        bpmLabel.setJustificationType (juce::Justification::centred);
        bpmLabel.setText ("120", juce::dontSendNotification);
        bpmLabel.setEditable (true);
        bpmLabel.onTextChange = [this]() {
            int newBpm = bpmLabel.getText().getIntValue();
            if (newBpm >= 40 && newBpm <= 300 && onBPMChange)
                onBPMChange (static_cast<float>(newBpm));
        };

        addAndMakeVisible (bpmCaption);
        bpmCaption.setText ("BPM", juce::dontSendNotification);
        bpmCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
        bpmCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);

        // Transport buttons
        addAndMakeVisible (playBtn);
        playBtn.setButtonText ("> PLAY");
        playBtn.setClickingTogglesState (true);
        playBtn.onClick = [this]() {
            bool nowPlaying = playBtn.getToggleState();
            playBtn.setButtonText (nowPlaying ? "II PAUSE" : "> PLAY");
            statusLabel.setText (nowPlaying ? "PLAY" : "PAUSE", juce::dontSendNotification);
            if (onPlayToggle) onPlayToggle (nowPlaying);
        };

        addAndMakeVisible (stopBtn);
        stopBtn.setButtonText ("[] STOP");
        stopBtn.onClick = [this]() {
            playBtn.setToggleState (false, juce::dontSendNotification);
            playBtn.setButtonText ("> PLAY");
            statusLabel.setText ("STOP", juce::dontSendNotification);
            if (onStop) onStop();
        };

        // Tap tempo
        addAndMakeVisible (tapBtn);
        tapBtn.setButtonText ("TAP");
        tapBtn.onClick = [this]() { handleTap(); };

        // INT/EXT clock toggle
        addAndMakeVisible (clockBtn);
        clockBtn.setButtonText ("EXT");
        clockBtn.setClickingTogglesState (true);
        clockBtn.setToggleState (true, juce::dontSendNotification);
        clockBtn.onClick = [this]() {
            bool ext = clockBtn.getToggleState();
            clockBtn.setButtonText (ext ? "EXT" : "INT");
            if (onClockModeChange) onClockModeChange (ext);
        };

        addAndMakeVisible (clockCaption);
        clockCaption.setText ("CLK", juce::dontSendNotification);
        clockCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        clockCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        clockCaption.setJustificationType (juce::Justification::centred);

        // ── GLOBAL SWING ──
        addAndMakeVisible (swingCaption);
        swingCaption.setText ("SWING", juce::dontSendNotification);
        swingCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        swingCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        swingCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (swingSlider);
        swingSlider.setRange (-50, 50, 1);
        swingSlider.setValue (0, juce::dontSendNotification);
        swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 26, 14);
        swingSlider.setColour (juce::Slider::textBoxTextColourId, Colours_GB::accent);
        swingSlider.setColour (juce::Slider::textBoxBackgroundColourId, Colours_GB::bg);
        swingSlider.setColour (juce::Slider::textBoxOutlineColourId, Colours_GB::border);
        swingSlider.setColour (juce::Slider::thumbColourId, Colours_GB::accent);
        swingSlider.setColour (juce::Slider::trackColourId, Colours_GB::knobTrack);
        swingSlider.onValueChange = [this]() {
            if (onSwingChange)
                onSwingChange (static_cast<int>(swingSlider.getValue()));
        };

        // ── FILL BUTTON ──
        addAndMakeVisible (fillBtn);
        fillBtn.setButtonText ("FILL");
        fillBtn.setClickingTogglesState (true);
        fillBtn.onClick = [this]() {
            bool active = fillBtn.getToggleState();
            fillBtn.setButtonText (active ? "FILL *" : "FILL");
            if (onFillToggle) onFillToggle (active);
        };

        // ── SCALE ROOT ──
        addAndMakeVisible (rootCaption);
        rootCaption.setText ("ROOT", juce::dontSendNotification);
        rootCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        rootCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        rootCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (rootSelector);
        const char* notes[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        for (int i = 0; i < 12; ++i)
            rootSelector.addItem (notes[i], i + 1);
        rootSelector.setSelectedId (1, juce::dontSendNotification);
        rootSelector.onChange = [this]() {
            if (onScaleRootChange) onScaleRootChange (rootSelector.getSelectedId() - 1);
        };

        // ── SCALE TYPE ──
        addAndMakeVisible (scaleCaption);
        scaleCaption.setText ("SCALE", juce::dontSendNotification);
        scaleCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        scaleCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        scaleCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (scaleSelector);
        const char* scaleNames[] = {"CHROM","MAJOR","MINOR","PENTA","BLUES","DORIC","PHRYG","LYDIA","MIXO","LOCR","HARM","WHOLE","DIM","AUG"};
        for (int i = 0; i < 14; ++i)
            scaleSelector.addItem (scaleNames[i], i + 1);
        scaleSelector.setSelectedId (1, juce::dontSendNotification);
        scaleSelector.onChange = [this]() {
            if (onScaleTypeChange) onScaleTypeChange (scaleSelector.getSelectedId() - 1);
        };

        // ── QUANTIZE ALL button ──
        addAndMakeVisible (quantizeBtn);
        quantizeBtn.setButtonText ("Q-ALL");
        quantizeBtn.onClick = [this]() {
            if (onQuantizeAll) onQuantizeAll();
        };

        // ── GLOBAL INIT button ──
        addAndMakeVisible (globalInitBtn);
        globalInitBtn.setButtonText ("INIT");
        globalInitBtn.onClick = [this]() {
            if (onGlobalInit) onGlobalInit();
        };

        // ── GLOBAL RANDOM button ──
        addAndMakeVisible (globalRndBtn);
        globalRndBtn.setButtonText ("RND");
        globalRndBtn.onClick = [this]() {
            if (onGlobalRandom) onGlobalRandom();
        };

        // ── UNDO / REDO buttons (custom-drawn circular arrows) ──
        addAndMakeVisible (undoBtn);
        undoBtn.setButtonText ("");
        undoBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0x01000000));
        undoBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0x01000000));
        undoBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        undoBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
        undoBtn.onClick = [this]() {
            if (motionRecState) { motionRecState->undo(); if (onUndoRedo) onUndoRedo(); }
            repaint();
        };
        addAndMakeVisible (redoBtn);
        redoBtn.setButtonText ("");
        redoBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0x01000000));
        redoBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0x01000000));
        redoBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        redoBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
        redoBtn.onClick = [this]() {
            if (motionRecState) { motionRecState->redo(); if (onUndoRedo) onUndoRedo(); }
            repaint();
        };

        // ── PRESET button ──
        addAndMakeVisible (presetBtn);
        presetBtn.setButtonText ("PRESET");
        presetBtn.setColour (juce::TextButton::buttonColourId, Colours_GB::accentMid);
        presetBtn.onClick = [this]() {
            if (onPresetClick) onPresetClick();
        };

        // ── Motion Rec button ──
        addAndMakeVisible (motBtn);
        motBtn.setButtonText ("REC");
        motBtn.setColour (juce::TextButton::buttonColourId, Colours_GB::panel3);
        motBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        motBtn.onClick = [this]() {
            if (motionRecState == nullptr) return;
            bool wasRec = motionRecState->motionRec.load();
            if (wasRec)
            {
                // Stop recording
                motionRecState->motionRec.store (false);
                motBtn.setButtonText ("REC");
                motBtn.setColour (juce::TextButton::buttonColourId, Colours_GB::panel3);
                motBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            }
            else
            {
                // Start recording — show mode menu first
                juce::PopupMenu menu;
                menu.addItem (1, "STEP (quantized to steps)", true, motionRecState->motionRecMode.load() == 0);
                menu.addItem (2, "SMOOTH (interpolated)", true, motionRecState->motionRecMode.load() == 1);
                menu.showMenuAsync (juce::PopupMenu::Options()
                    .withParentComponent (getTopLevelComponent())
                    .withTargetComponent (motBtn),
                    [this](int r) {
                        if (r == 0 || motionRecState == nullptr) return;
                        motionRecState->motionRecMode.store (r - 1);
                        motionRecState->motionRec.store (true);
                        motBtn.setButtonText (r == 1 ? "REC*S" : "REC*M");
                        motBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffcc2020));
                        motBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                    });
            }
        };

        // ── Metronome button ──
        addAndMakeVisible (metroBtn);
        metroBtn.setButtonText ("MET");
        metroBtn.setColour (juce::TextButton::buttonColourId, Colours_GB::panel3);
        metroBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        metroBtn.onClick = [this]() {
            if (!motionRecState) return;
            bool on = !motionRecState->metronomeOn.load();
            motionRecState->metronomeOn.store (on);
            metroBtn.setColour (juce::TextButton::buttonColourId,
                on ? Colours_GB::accent : Colours_GB::panel3);
        };
        // Right-click: settings popup (sound, pre-roll, auto-rec)
        metroBtn.addMouseListener (this, false);

        // ── Global SOLO indicator ──
        addAndMakeVisible (soloIndicator);
        soloIndicator.setButtonText ("SOLO");
        soloIndicator.setColour (juce::TextButton::buttonColourId, Colours_GB::accent);
        soloIndicator.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff001820));
        soloIndicator.setVisible (false); // hidden by default
        soloIndicator.onClick = [this]() {
            if (onClearAllSolo) onClearAllSolo();
        };

        // Master volume
        addAndMakeVisible (masterCaption);
        masterCaption.setText ("MIX", juce::dontSendNotification);
        masterCaption.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 8.0f, juce::Font::bold));
        masterCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        masterCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (masterSlider);
        masterSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        masterSlider.setRange (0.0, 1.5, 0.01);
        masterSlider.setValue (0.8, juce::dontSendNotification);
        masterSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        masterSlider.setColour (juce::Slider::thumbColourId, Colours_GB::accent);
        masterSlider.setColour (juce::Slider::trackColourId, Colours_GB::knobTrack);
        masterSlider.onValueChange = [this]() {
            if (onMasterVolumeChange) onMasterVolumeChange (static_cast<float>(masterSlider.getValue()));
        };
    }

    void setBPMDisplay (float bpm)
    {
        if (!bpmLabel.isBeingEdited())
            bpmLabel.setText (juce::String (static_cast<int>(bpm)), juce::dontSendNotification);
    }

    void setMasterVolume (float vol)
    {
        masterSlider.setValue (static_cast<double>(vol), juce::dontSendNotification);
    }

    void setSwingDisplay (int swing)
    {
        swingSlider.setValue (static_cast<double>(swing), juce::dontSendNotification);
    }

    void setMotionRecState (GrooveBoxState* s) { motionRecState = s; }

    void setGlobalSoloActive (bool active)
    {
        if (soloIndicator.isVisible() != active)
        {
            soloIndicator.setVisible (active);
            repaint();
        }
    }

    std::function<void()> onClearAllSolo;
    std::function<void(float)> onZoomChange;

    void setStatusText (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
    }

    void setPlayState (bool playing)
    {
        playBtn.setToggleState (playing, juce::dontSendNotification);
        playBtn.setButtonText (playing ? "II PAUSE" : "> PLAY");
        statusLabel.setText (playing ? "PLAY" : "STOP", juce::dontSendNotification);
    }

    std::function<void(bool)>   onPlayToggle;
    std::function<void()>       onStop;
    std::function<void(float)>  onBPMChange;
    std::function<void(bool)>   onClockModeChange;
    std::function<void(int)>    onSwingChange;
    std::function<void(bool)>   onFillToggle;
    std::function<void(int)>    onScaleRootChange;
    std::function<void(int)>    onScaleTypeChange;
    std::function<void()>       onQuantizeAll;
    std::function<void()>       onGlobalInit;
    std::function<void()>       onGlobalRandom;
    std::function<void()>       onUndoRedo;
    std::function<void()>       onPresetClick;
    std::function<void(float)>  onMasterVolumeChange;

    void setPeakLevels (float l, float r, float rl = 0, float rr = 0) { peakL = l; peakR = r; rmsL = rl; rmsR = rr; repaint(); }
    void setGRLevels (float cGR, float lGR) { compGR = cGR; limGR = lGR; }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (4);

        // ═══ No right panel — full width for transport/controls ═══
        // (MIX knob + meters are in PluginEditor's MasterFX overlay)
        masterSlider.setVisible (false);
        masterCaption.setVisible (false);
        mixOutPanel = {}; volDbArea = {};

        // ═══ ROW 1: Transport + controls (top 42px) ═══
        auto topRow = bounds.removeFromTop (42);

        // Logo area — saved for custom paint
        auto left = topRow.removeFromLeft (110);
        logoArea = left;

        // Status/BPM
        topRow.removeFromLeft (4);
        auto display = topRow.removeFromLeft (70);
        auto displayTop = display.removeFromTop (display.getHeight() / 2);
        statusLabel.setBounds (displayTop.reduced (1));
        bpmLabel.setBounds (display.reduced (1));
        bpmCaption.setBounds (topRow.removeFromLeft (18).withSizeKeepingCentre (18, 14));

        // Transport
        topRow.removeFromLeft (6);
        playBtn.setBounds (topRow.removeFromLeft (52).reduced (1, 5));
        stopBtn.setBounds (topRow.removeFromLeft (42).reduced (1, 5));
        tapBtn.setBounds (topRow.removeFromLeft (30).reduced (1, 5));

        // Clock
        topRow.removeFromLeft (6);
        auto clockArea = topRow.removeFromLeft (28);
        clockCaption.setBounds (clockArea.removeFromTop (12));
        clockBtn.setBounds (clockArea.reduced (1, 1));

        // Swing
        topRow.removeFromLeft (4);
        auto swingArea = topRow.removeFromLeft (70);
        swingCaption.setBounds (swingArea.removeFromTop (12));
        swingSlider.setBounds (swingArea.reduced (0, 1));

        // Fill
        topRow.removeFromLeft (6);
        fillBtn.setBounds (topRow.removeFromLeft (32).reduced (1, 5));

        // Scale Root + Type
        topRow.removeFromLeft (6);
        auto rootArea = topRow.removeFromLeft (30);
        rootCaption.setBounds (rootArea.removeFromTop (12));
        rootSelector.setBounds (rootArea.reduced (0, 1));

        topRow.removeFromLeft (2);
        auto scaleArea = topRow.removeFromLeft (44);
        scaleCaption.setBounds (scaleArea.removeFromTop (12));
        scaleSelector.setBounds (scaleArea.reduced (0, 1));

        // Tools
        topRow.removeFromLeft (6);
        quantizeBtn.setBounds (topRow.removeFromLeft (28).reduced (1, 5));
        topRow.removeFromLeft (2);
        globalInitBtn.setBounds (topRow.removeFromLeft (26).reduced (1, 5));
        topRow.removeFromLeft (2);
        globalRndBtn.setBounds (topRow.removeFromLeft (26).reduced (1, 5));
        topRow.removeFromLeft (4);
        undoBtn.setBounds (topRow.removeFromLeft (24).reduced (1, 5));
        topRow.removeFromLeft (2);
        redoBtn.setBounds (topRow.removeFromLeft (24).reduced (1, 5));
        // Store bounds for custom painting
        undoBtnArea = undoBtn.getBounds();
        redoBtnArea = redoBtn.getBounds();
        // Update undo/redo active state
        if (motionRecState)
        {
            canUndoNow = motionRecState->canUndo();
            canRedoNow = motionRecState->canRedo();
        }
        topRow.removeFromLeft (6);
        presetBtn.setBounds (topRow.removeFromLeft (40).reduced (1, 5));
        topRow.removeFromLeft (2);
        motBtn.setBounds (topRow.removeFromLeft (38).reduced (1, 5));
        topRow.removeFromLeft (2);
        metroBtn.setBounds (topRow.removeFromLeft (30).reduced (1, 5));
        // Refresh metro button color on layout
        if (motionRecState)
            metroBtn.setColour (juce::TextButton::buttonColourId,
                motionRecState->metronomeOn.load() ? Colours_GB::accent : Colours_GB::panel3);

        // Global SOLO indicator (yellow, visible only when any track is soloed)
        topRow.removeFromLeft (2);
        soloIndicator.setBounds (topRow.removeFromLeft (36).reduced (1, 5));

        // ── MACRO KNOBS (4 custom-drawn knobs before meter) ──
        topRow.removeFromLeft (6);
        macroArea = topRow.removeFromLeft (152); // 4 × 38px
        for (int i = 0; i < 4; ++i)
            macroKnobBounds[i] = { macroArea.getX() + i * 38, macroArea.getY(), 37, macroArea.getHeight() };

        // ═══ ROW 2: now free — master FX strip uses full width ═══
        bounds.removeFromTop (2);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Colours_GB::panel);
        g.fillAll();

        // ── CUSTOM LOGO: MUSIC BOX ──
        if (logoArea.getWidth() > 10)
        {
            float lx = static_cast<float>(logoArea.getX());
            float ly = static_cast<float>(logoArea.getY()) + 4;

            // "MUSIC" — light weight, accent
            g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 13.0f, juce::Font::plain));
            g.setColour (Colours_GB::accent);
            g.drawText ("MUSIC", static_cast<int>(lx), static_cast<int>(ly), 52, 16, juce::Justification::centredLeft);

            // "BOX" — bold, bright
            g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 13.0f, juce::Font::bold));
            g.setColour (Colours_GB::accentBright);
            g.drawText ("BOX", static_cast<int>(lx + 50), static_cast<int>(ly), 30, 16, juce::Justification::centredLeft);

            // Accent underline
            g.setColour (Colours_GB::accent.withAlpha (0.4f));
            g.fillRect (lx, ly + 18, 80.0f, 1.0f);

            // "by Phonica" + zoom %
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
            g.setColour (Colours_GB::textDark);
            g.drawText ("by Phonica", static_cast<int>(lx), static_cast<int>(ly + 20), 56, 10, juce::Justification::centredLeft);

            // Zoom percentage (clickable)
            juce::String zoomStr = juce::String (static_cast<int>(currentZoom * 100)) + "%";
            g.setColour (Colours_GB::accent.withAlpha (0.7f));
            g.drawText (zoomStr, static_cast<int>(lx + 58), static_cast<int>(ly + 20), 30, 10, juce::Justification::centredLeft);
            zoomClickArea = { static_cast<int>(lx + 56), static_cast<int>(ly + 18), 34, 14 };
        }

        // Bottom border
        g.setColour (Colours_GB::border);
        g.drawLine (0, static_cast<float>(getHeight() - 1),
                    static_cast<float>(getWidth()), static_cast<float>(getHeight() - 1), 1.0f);

        // Separator line between transport row and master row
        float sepY = 46.0f;
        g.setColour (juce::Colour (0x20ffffff));
        g.drawLine (8.0f, sepY, static_cast<float>(getWidth() - 8), sepY, 0.5f);

        // Master row background — subtle dark strip
        g.setColour (juce::Colour (0x0cffffff));
        g.fillRect (4.0f, sepY + 2, static_cast<float>(getWidth() - 8), static_cast<float>(getHeight()) - sepY - 6);

        // ── GROUP SEPARATORS (vertical lines between sections) ──
        {
            g.setColour (juce::Colour (0x18ffffff));
            float y1 = 8.0f, y2 = 40.0f;

            // Section micro-labels
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 6.0f, juce::Font::plain));
            g.setColour (juce::Colour (0x30ffffff));

            // After BPM area
            if (bpmLabel.isVisible())
            {
                float sx = static_cast<float>(bpmCaption.getRight()) + 2;
                g.setColour (juce::Colour (0x18ffffff));
                g.drawLine (sx, y1, sx, y2, 0.5f);
            }
            // After TAP (transport group)
            if (tapBtn.isVisible())
            {
                float sx = static_cast<float>(tapBtn.getRight()) + 2;
                g.setColour (juce::Colour (0x18ffffff));
                g.drawLine (sx, y1, sx, y2, 0.5f);
            }
            // After FILL (timing group)
            if (fillBtn.isVisible())
            {
                float sx = static_cast<float>(fillBtn.getRight()) + 2;
                g.setColour (juce::Colour (0x18ffffff));
                g.drawLine (sx, y1, sx, y2, 0.5f);
            }
            // After Scale selector
            if (scaleSelector.isVisible())
            {
                float sx = static_cast<float>(scaleSelector.getRight()) + 1;
                g.setColour (juce::Colour (0x18ffffff));
                g.drawLine (sx, y1, sx, y2, 0.5f);
            }
            // After Redo (tools group)
            if (redoBtn.isVisible())
            {
                float sx = static_cast<float>(redoBtn.getRight()) + 1;
                g.setColour (juce::Colour (0x18ffffff));
                g.drawLine (sx, y1, sx, y2, 0.5f);
            }
            // After MET/SOLO (session group)
            if (metroBtn.isVisible())
            {
                float sx = static_cast<float>(soloIndicator.isVisible() ? soloIndicator.getRight() : metroBtn.getRight()) + 3;
                g.setColour (juce::Colour (0x18ffffff));
                g.drawLine (sx, y1, sx, y2, 0.5f);
            }
        }

        // ── MACRO KNOBS (4 custom arc knobs) ──
        if (motionRecState != nullptr)
        {
            // Macro section background
            if (macroArea.getWidth() > 10)
            {
                g.setColour (juce::Colour (0x0cffffff));
                g.fillRoundedRectangle (macroArea.toFloat().expanded (2, 0), 3.0f);
                // "MACROS" label
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 6.5f, juce::Font::plain));
                g.setColour (Colours_GB::textDark);
                g.drawText ("MACROS", macroArea.getX(), macroArea.getY() - 1, macroArea.getWidth(), 8, juce::Justification::centred);
            }
            // Refresh undo/redo state every paint
            canUndoNow = motionRecState->canUndo();
            canRedoNow = motionRecState->canRedo();

            // ── UNDO / REDO icons — simple clean arrows ──
            auto drawUndoRedo = [&](juce::Rectangle<int> area, bool isUndo, bool active, bool pressed)
            {
                float x = static_cast<float>(area.getX());
                float y = static_cast<float>(area.getY());
                float w = static_cast<float>(area.getWidth());
                float h = static_cast<float>(area.getHeight());

                juce::Colour col = active ? Colours_GB::accent : Colours_GB::textDark;
                if (pressed && active) col = col.brighter (0.4f);
                g.setColour (col);

                // Normalized coords (0-1), then scale to area
                // Classic undo arrow: curved line going right-to-left with arrowhead
                float mx = x + w * 0.5f, my = y + h * 0.5f;
                float s = std::min (w, h) * 0.42f;

                juce::Path curve;
                if (isUndo)
                {
                    // Arc: starts right, curves up and around to left
                    curve.startNewSubPath (mx + s * 0.5f, my + s * 0.3f);
                    curve.cubicTo (mx + s * 0.5f, my - s * 0.6f,
                                   mx - s * 0.5f, my - s * 0.6f,
                                   mx - s * 0.5f, my + s * 0.1f);
                    g.strokePath (curve, juce::PathStrokeType (active ? 1.8f : 1.2f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    // Arrowhead pointing down-left
                    juce::Path arrow;
                    float ax = mx - s * 0.5f, ay = my + s * 0.1f;
                    arrow.startNewSubPath (ax, ay);
                    arrow.lineTo (ax + s * 0.28f, ay - s * 0.22f);
                    arrow.lineTo (ax + s * 0.12f, ay + s * 0.3f);
                    arrow.closeSubPath();
                    g.fillPath (arrow);
                }
                else
                {
                    // Mirror for redo
                    curve.startNewSubPath (mx - s * 0.5f, my + s * 0.3f);
                    curve.cubicTo (mx - s * 0.5f, my - s * 0.6f,
                                   mx + s * 0.5f, my - s * 0.6f,
                                   mx + s * 0.5f, my + s * 0.1f);
                    g.strokePath (curve, juce::PathStrokeType (active ? 1.8f : 1.2f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    juce::Path arrow;
                    float ax = mx + s * 0.5f, ay = my + s * 0.1f;
                    arrow.startNewSubPath (ax, ay);
                    arrow.lineTo (ax - s * 0.28f, ay - s * 0.22f);
                    arrow.lineTo (ax - s * 0.12f, ay + s * 0.3f);
                    arrow.closeSubPath();
                    g.fillPath (arrow);
                }
            };

            drawUndoRedo (undoBtnArea, true,  canUndoNow, undoBtn.isDown());
            drawUndoRedo (redoBtnArea, false, canRedoNow, redoBtn.isDown());

            static const juce::Colour macroColors[] = {
                juce::Colour (0xff40c8e0), // cyan
                juce::Colour (0xffe040c0), // magenta
                juce::Colour (0xffe0c040), // gold
                juce::Colour (0xff50e070)  // green
            };
            auto& me = motionRecState->macroEngine;

            for (int i = 0; i < 4; ++i)
            {
                auto r = macroKnobBounds[i].toFloat();
                auto& mk = me.macros[static_cast<size_t>(i)];
                auto col = macroColors[i];
                float cx = r.getCentreX(), cy = r.getY() + 18.0f;
                float radius = 13.0f;

                // Background circle
                g.setColour (juce::Colour (0xff0a0c14));
                g.fillEllipse (cx - radius, cy - radius, radius * 2, radius * 2);

                // Glow ring when armed
                if (mk.armed)
                {
                    g.setColour (col.withAlpha (0.3f));
                    g.drawEllipse (cx - radius - 2, cy - radius - 2, (radius + 2) * 2, (radius + 2) * 2, 2.0f);
                }

                // Arc track (dark)
                float startAngle = juce::MathConstants<float>::pi * 0.75f;
                float endAngle   = juce::MathConstants<float>::pi * 2.25f;
                float arcRadius = radius - 2.0f;
                juce::Path trackArc;
                trackArc.addCentredArc (cx, cy, arcRadius, arcRadius, 0, startAngle, endAngle, true);
                g.setColour (Colours_GB::border.withAlpha (0.5f));
                g.strokePath (trackArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // Arc fill (value)
                float valAngle = startAngle + mk.value * (endAngle - startAngle);
                if (mk.value > 0.01f)
                {
                    juce::Path fillArc;
                    fillArc.addCentredArc (cx, cy, arcRadius, arcRadius, 0, startAngle, valAngle, true);
                    g.setColour (col.withAlpha (0.85f));
                    g.strokePath (fillArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }

                // Pointer dot
                float dotAngle = valAngle;
                float dotX = cx + std::cos (dotAngle) * (arcRadius + 0.5f);
                float dotY = cy + std::sin (dotAngle) * (arcRadius + 0.5f);
                g.setColour (juce::Colours::white);
                g.fillEllipse (dotX - 2, dotY - 2, 4, 4);

                // Label — show depth % when in edit mode
                bool isDepthEdit = (depthEditMacro == i && depthEditAssignment >= 0
                    && depthEditAssignment < static_cast<int>(mk.assignments.size()));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
                if (isDepthEdit)
                {
                    int dpct = static_cast<int>(std::round (mk.assignments[static_cast<size_t>(depthEditAssignment)].depth * 100.0f));
                    juce::String depthLabel = (dpct >= 0 ? "+" : "") + juce::String (dpct);
                    g.setColour (juce::Colours::white);
                    g.drawText (depthLabel, static_cast<int>(r.getX()), static_cast<int>(r.getBottom() - 10),
                                static_cast<int>(r.getWidth()), 10, juce::Justification::centred);
                    // Edit mode glow ring (white pulsing)
                    g.setColour (juce::Colours::white.withAlpha (0.25f));
                    g.drawEllipse (cx - radius - 2, cy - radius - 2, (radius + 2) * 2, (radius + 2) * 2, 2.0f);
                }
                else
                {
                    g.setColour (mk.armed ? col : col.withAlpha (0.6f));
                    g.drawText ("M" + juce::String (i + 1), static_cast<int>(r.getX()), static_cast<int>(r.getBottom() - 10),
                                static_cast<int>(r.getWidth()), 10, juce::Justification::centred);
                }

                // Assignment count badge
                int numAssign = static_cast<int>(mk.assignments.size());
                if (numAssign > 0)
                {
                    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 6.0f, juce::Font::bold));
                    g.setColour (col.withAlpha (0.5f));
                    g.drawText (juce::String (numAssign), static_cast<int>(cx + radius - 4), static_cast<int>(cy - radius - 2), 10, 8, juce::Justification::centred);
                }
            }
        }

        // (No MIX OUT panel — knob + meters in PluginEditor)
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // ── Zoom click (on percentage text in logo) ──
        if (zoomClickArea.contains (e.getPosition()))
        {
            juce::PopupMenu menu;
            menu.addSectionHeader ("ZOOM");
            static const int pcts[] = { 75, 88, 100, 125, 150 };
            for (int i = 0; i < 5; ++i)
            {
                bool isCurrent = std::abs (zoomLevels[i] - currentZoom) < 0.01f;
                menu.addItem (200 + i, juce::String (pcts[i]) + "%", true, isCurrent);
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                localAreaToGlobal (zoomClickArea)),
                [this](int result) {
                    if (result >= 200 && result <= 204)
                    {
                        currentZoom = zoomLevels[result - 200];
                        repaint();
                        if (onZoomChange) onZoomChange (currentZoom);
                    }
                });
            return;
        }

        // Right-click on MET button → settings popup
        if (e.originalComponent == &metroBtn && (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) && motionRecState != nullptr)
        {
            juce::PopupMenu menu;
            int curSound = motionRecState->metronomeSound.load();
            int curPreRoll = motionRecState->metronomePreRoll.load();
            bool autoRec = motionRecState->metronomeAutoRec.load();

            // Sound type
            menu.addSectionHeader ("SOUND");
            menu.addItem (10, "Click",   true, curSound == 0);
            menu.addItem (11, "Hi",      true, curSound == 1);
            menu.addItem (12, "Cowbell", true, curSound == 2);
            menu.addItem (13, "Rimshot", true, curSound == 3);

            // Pre-roll
            menu.addSeparator();
            menu.addSectionHeader ("PRE-ROLL");
            menu.addItem (20, "Off",     true, curPreRoll == 0);
            menu.addItem (21, "1 Bar",   true, curPreRoll == 1);
            menu.addItem (22, "2 Bars",  true, curPreRoll == 2);

            // Volume
            menu.addSeparator();
            menu.addSectionHeader ("VOLUME");
            float curVol = motionRecState->metronomeVol.load();
            for (int v = 100; v >= 10; v -= 10)
                menu.addItem (100 + v, juce::String (v) + "%", true,
                    static_cast<int>(curVol * 100.0f + 0.5f) == v);

            // Auto-rec
            menu.addSeparator();
            menu.addItem (30, juce::String ("Auto-REC: ") + (autoRec ? "ON" : "OFF"), true, autoRec);

            menu.showMenuAsync (juce::PopupMenu::Options()
                .withTargetComponent (metroBtn),
                [this](int r) {
                    if (!motionRecState) return;
                    if (r >= 10 && r <= 13) motionRecState->metronomeSound.store (r - 10);
                    else if (r >= 20 && r <= 22) motionRecState->metronomePreRoll.store (r - 20);
                    else if (r == 30) motionRecState->metronomeAutoRec.store (!motionRecState->metronomeAutoRec.load());
                    else if (r >= 110 && r <= 200) motionRecState->metronomeVol.store (static_cast<float>(r - 100) / 100.0f);
                });
            return;
        }

        // ── MACRO KNOB clicks ──
        if (motionRecState != nullptr)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (macroKnobBounds[i].contains (e.getPosition()))
                {
                    auto& me = motionRecState->macroEngine;

                    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
                    {
                        // Right-click → assignment menu + depth edit selection
                        auto& mk = me.macros[static_cast<size_t>(i)];
                        juce::PopupMenu menu;
                        menu.addSectionHeader ("MACRO " + juce::String (i + 1));
                        if (mk.assignments.empty())
                        {
                            menu.addItem (-1, "(no assignments)", false);
                            menu.addItem (-1, "Arm + click knob to assign", false);
                            menu.addItem (-1, "Alt+click = negative depth", false);
                        }
                        else
                        {
                            // Each assignment: click to enter depth edit mode
                            for (int a = 0; a < static_cast<int>(mk.assignments.size()); ++a)
                            {
                                auto& asgn = mk.assignments[static_cast<size_t>(a)];
                                const char* typeNames[] = {"SYN","DRM","MFX"};
                                int depthPct = static_cast<int>(std::round (asgn.depth * 100.0f));
                                juce::String depthStr = (depthPct >= 0 ? "+" : "") + juce::String (depthPct) + "%";
                                juce::String label = juce::String (typeNames[asgn.trackType % 3])
                                    + juce::String (asgn.trackIndex + 1) + " "
                                    + juce::String (asgn.paramKey)
                                    + "  " + depthStr;
                                bool isBeingEdited = (depthEditMacro == i && depthEditAssignment == a);
                                menu.addItem (200 + a, "EDIT: " + label, true, isBeingEdited);
                            }
                            menu.addSeparator();
                            for (int a = 0; a < static_cast<int>(mk.assignments.size()); ++a)
                            {
                                auto& asgn = mk.assignments[static_cast<size_t>(a)];
                                const char* typeNames[] = {"SYN","DRM","MFX"};
                                juce::String label = juce::String (typeNames[asgn.trackType % 3])
                                    + juce::String (asgn.trackIndex + 1) + " "
                                    + juce::String (asgn.paramKey);
                                menu.addItem (100 + a, "DEL: " + label);
                            }
                            menu.addSeparator();
                            menu.addItem (1, "CLEAR ALL");
                        }
                        menu.showMenuAsync (juce::PopupMenu::Options()
                            .withParentComponent (getTopLevelComponent()),
                            [this, i](int r) {
                                if (r == 0 || motionRecState == nullptr) return;
                                auto& mk2 = motionRecState->macroEngine.macros[static_cast<size_t>(i)];
                                if (r == 1) { mk2.clear(); depthEditMacro = -1; depthEditAssignment = -1; }
                                else if (r >= 200)
                                {
                                    // Enter depth edit mode for this assignment
                                    int assignIdx = r - 200;
                                    if (assignIdx >= 0 && assignIdx < static_cast<int>(mk2.assignments.size()))
                                    {
                                        depthEditMacro = i;
                                        depthEditAssignment = assignIdx;
                                    }
                                }
                                else if (r >= 100)
                                {
                                    int assignIdx = r - 100;
                                    mk2.removeAssignment (assignIdx);
                                    // Fix depth edit index if needed
                                    if (depthEditMacro == i)
                                    {
                                        if (depthEditAssignment == assignIdx)
                                            { depthEditMacro = -1; depthEditAssignment = -1; }
                                        else if (depthEditAssignment > assignIdx)
                                            depthEditAssignment--;
                                    }
                                }
                                repaint();
                            });
                    }
                    else
                    {
                        // Left-click behavior depends on mode:
                        if (depthEditMacro == i)
                        {
                            // In depth edit mode for this macro → start depth drag
                            macroDragIndex = i;
                            macroDragStartY = static_cast<float>(e.getPosition().getY());
                            auto& mk2 = me.macros[static_cast<size_t>(i)];
                            if (depthEditAssignment >= 0 && depthEditAssignment < static_cast<int>(mk2.assignments.size()))
                                depthEditStartVal = mk2.assignments[static_cast<size_t>(depthEditAssignment)].depth;
                            else
                                depthEditStartVal = 0.0f;
                        }
                        else
                        {
                            // Exit any other depth edit
                            depthEditMacro = -1;
                            depthEditAssignment = -1;
                            // Normal: arm/disarm + value drag
                            me.armMacro (i);
                            macroDragIndex = i;
                            macroDragStartY = static_cast<float>(e.getPosition().getY());
                            macroDragStartVal = me.macros[static_cast<size_t>(i)].value;
                        }
                        repaint();
                    }
                    return;
                }
            }
        }

    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (macroDragIndex >= 0 && motionRecState != nullptr)
        {
            float dy = macroDragStartY - static_cast<float>(e.getPosition().getY());

            if (depthEditMacro == macroDragIndex && depthEditAssignment >= 0)
            {
                // DEPTH EDIT MODE: drag adjusts assignment depth continuously
                auto& mk = motionRecState->macroEngine.macros[static_cast<size_t>(macroDragIndex)];
                if (depthEditAssignment < static_cast<int>(mk.assignments.size()))
                {
                    float newDepth = depthEditStartVal + dy * 0.008f;
                    mk.assignments[static_cast<size_t>(depthEditAssignment)].depth = std::clamp (newDepth, -1.0f, 1.0f);
                }
            }
            else
            {
                // NORMAL MODE: drag adjusts macro value
                float newVal = macroDragStartVal + dy * 0.005f;
                motionRecState->macroEngine.macros[static_cast<size_t>(macroDragIndex)].value = std::clamp (newVal, 0.0f, 1.0f);
            }
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        // If we were in depth edit and didn't drag much, exit depth edit
        if (depthEditMacro >= 0 && macroDragIndex == depthEditMacro)
        {
            float dy = std::abs (macroDragStartY - static_cast<float>(e.getPosition().getY()));
            if (dy < 3.0f)
            {
                // Click without drag → exit depth edit mode
                depthEditMacro = -1;
                depthEditAssignment = -1;
                repaint();
            }
        }
        macroDragIndex = -1;
    }

private:
    juce::Label logoLabel, subtitleLabel, statusLabel, bpmLabel, bpmCaption, clockCaption, swingCaption, rootCaption, scaleCaption, masterCaption;
    juce::TextButton playBtn, stopBtn, tapBtn, clockBtn, fillBtn, quantizeBtn, globalInitBtn, globalRndBtn, presetBtn, motBtn, metroBtn, soloIndicator, undoBtn, redoBtn;
    juce::Rectangle<int> zoomClickArea;
    float currentZoom = 1.0f;
    static constexpr float zoomLevels[5] = { 0.75f, 0.875f, 1.0f, 1.25f, 1.5f };
    GrooveBoxState* motionRecState = nullptr;
    juce::Slider swingSlider, masterSlider;
    juce::ComboBox rootSelector, scaleSelector;
    juce::Rectangle<int> vuArea;
    juce::Rectangle<int> compGRArea, limGRArea;
    juce::Rectangle<int> macroArea;
    juce::Rectangle<int> mixOutPanel, volDbArea;
    juce::Rectangle<int> undoBtnArea, redoBtnArea;
    juce::Rectangle<int> logoArea;
    bool canUndoNow = false, canRedoNow = false;
    std::array<juce::Rectangle<int>, 4> macroKnobBounds;
    int macroDragIndex = -1;
    float macroDragStartY = 0;
    float macroDragStartVal = 0;

    // Depth edit mode: drag macro knob to continuously adjust an assignment's depth
    int depthEditMacro = -1;       // which macro is being depth-edited (-1 = none)
    int depthEditAssignment = -1;  // which assignment index
    float depthEditStartVal = 0.0f;
    float compGR = 0.0f, limGR = 0.0f;
    float peakL = 0.0f, peakR = 0.0f;
    float rmsL = 0.0f, rmsR = 0.0f;

    std::vector<double> tapTimes;

    void handleTap()
    {
        double now = juce::Time::getMillisecondCounterHiRes();
        tapTimes.push_back (now);
        if (tapTimes.size() > 8)
            tapTimes.erase (tapTimes.begin());

        if (tapTimes.size() > 1)
        {
            double sum = 0;
            for (size_t i = 1; i < tapTimes.size(); ++i)
                sum += tapTimes[i] - tapTimes[i - 1];
            double avg = sum / static_cast<double>(tapTimes.size() - 1);
            float newBpm = static_cast<float>(60000.0 / avg);
            newBpm = juce::jlimit (40.0f, 300.0f, std::round (newBpm));
            setBPMDisplay (newBpm);
            if (onBPMChange) onBPMChange (newBpm);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};
