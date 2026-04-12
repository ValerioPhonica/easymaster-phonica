#pragma once
#include <set>
#include <unordered_map>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Colours.h"
#include "StepGrid.h"
#include "RatchetRow.h"
#include "LEDMeter.h"
#include "KnobComponent.h"
#include "../../Sequencer/TrackState.h"
#include "../../Audio/Analysis/SampleAnalysis.h"
#include "EngineIcons.h"
#include "WaveformOverlay.h"
#include "../../Audio/FX/LFOEngine.h"
#include "MSEGEditor.h"
#include "../../State/PresetManager.h"
#include <functional>
#include <random>

class DrumTrackRow : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    DrumTrackRow (int trackIndex, DrumTrackState& trackState)
        : index (trackIndex), track (trackState)
    {
        // Track label
        addAndMakeVisible (nameLabel);
        nameLabel.setText (drumName (track.type), juce::dontSendNotification);
        nameLabel.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 10.0f, juce::Font::bold));
        nameLabel.setColour (juce::Label::textColourId, Colours_GB::accent);
        nameLabel.setJustificationType (juce::Justification::centred);

        // Step grid
        addAndMakeVisible (stepGrid);
        stepGrid.setStepSequence (&track.seq);
        stepGrid.setPage (track.page);
        stepGrid.setSampleSlotNames (&track.sampleSlotNames);

        stepGrid.onStepToggle = [this](int stepIdx, bool state) {
            auto& step = track.seq.steps[static_cast<size_t>(stepIdx)];
            if (state)
            {
                step.reset();
                step.active = true;
            }
            else
            {
                step.reset();
            }
            stepGrid.setStepSequence (&track.seq);
            if (onStepSync) onStepSync();
        };
        stepGrid.onVelChange = [this](int stepIdx, int newVel) {
            track.seq.steps[static_cast<size_t>(stepIdx)].velocity = static_cast<uint8_t>(newVel);
            stepGrid.setStepSequence (&track.seq);
        };
        stepGrid.onGateChange = [this](int stepIdx, int newGate) {
            track.seq.steps[static_cast<size_t>(stepIdx)].gate = static_cast<uint8_t>(newGate);
            stepGrid.setStepSequence (&track.seq);
        };
        stepGrid.onTrigCondChange = [this](int stepIdx, TrigCondition cond) {
            track.seq.steps[static_cast<size_t>(stepIdx)].cond = cond;
            stepGrid.setStepSequence (&track.seq);
        };

        // Undo capture: push undo state before step edits
        stepGrid.onBeforeStepEdit = [this]() { if (onBeforeEdit) onBeforeEdit(); };

        // Ratchet row
        addAndMakeVisible (ratchetRow);
        ratchetRow.setStepSequence (&track.seq);
        ratchetRow.setPage (track.page);

        // Page buttons — 8 pages (128 steps)
        for (int p = 0; p < kNumPages; ++p)
        {
            auto* btn = new juce::TextButton (juce::String (p + 1));
            btn->setClickingTogglesState (false);
            btn->onClick = [this, p]() {
                track.page = p;
                stepGrid.setPage (p);
                ratchetRow.setPage (p);
                manualPageOverride = true; // User took control — don't auto-follow
                updatePageButtons();
            };
            btn->addMouseListener (this, false); // forward right-click to row
            pageButtons.add (btn);
            addAndMakeVisible (btn);
        }
        updatePageButtons();

        // LEN — editable label with popup menu
        addAndMakeVisible (lenCaption);
        lenCaption.setText ("LEN", juce::dontSendNotification);
        lenCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        lenCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        lenCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (lenLabel);
        lenLabel.setText (juce::String (track.length), juce::dontSendNotification);
        lenLabel.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        lenLabel.setColour (juce::Label::textColourId, Colours_GB::accent);
        lenLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black);
        lenLabel.setColour (juce::Label::outlineColourId, Colours_GB::border);
        lenLabel.setJustificationType (juce::Justification::centred);
        lenLabel.setEditable (true);
        lenLabel.onTextChange = [this]() {
            int v = lenLabel.getText().getIntValue();
            if (v >= 1 && v <= kMaxSteps) { track.length = v; lenLabel.setText (juce::String (v), juce::dontSendNotification); if (onLinkSync) onLinkSync(); }
            else lenLabel.setText (juce::String (track.length), juce::dontSendNotification);
        };

        // SWG label + slider — bidirectional
        addAndMakeVisible (swingCaption);
        swingCaption.setText ("SWG", juce::dontSendNotification);
        swingCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        swingCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        swingCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (swingSlider);
        swingSlider.setRange (-50, 50, 1);
        swingSlider.setValue (track.swing, juce::dontSendNotification);
        swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        swingSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        swingSlider.setColour (juce::Slider::thumbColourId, Colours_GB::cyan);
        swingSlider.setColour (juce::Slider::trackColourId, Colours_GB::border);
        swingSlider.onValueChange = [this]() {
            track.swing = static_cast<int>(swingSlider.getValue());
            if (onLinkSync) onLinkSync();
        };

        // SPD (clock multiplier)
        addAndMakeVisible (spdCaption);
        spdCaption.setText ("SPD", juce::dontSendNotification);
        spdCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        spdCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        spdCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (spdSelector);
        spdSelector.addItem ("1/8x", 1);
        spdSelector.addItem ("1/4x", 2);
        spdSelector.addItem ("1/3x", 3);
        spdSelector.addItem ("3/8x", 4);
        spdSelector.addItem ("1/2x", 5);
        spdSelector.addItem ("3/4x", 6);
        spdSelector.addItem ("x1", 7);
        spdSelector.addItem ("x3/2", 8);
        spdSelector.addItem ("x2", 9);
        spdSelector.addItem ("x3", 10);
        spdSelector.addItem ("x4", 11);
        spdSelector.addItem ("x6", 12);
        spdSelector.addItem ("x8", 13);
        spdSelector.setSelectedId (7, juce::dontSendNotification); // default x1
        spdSelector.onChange = [this]() {
            float vals[] = { 0.125f, 0.25f, 0.333f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f };
            int idx = spdSelector.getSelectedId() - 1;
            if (idx >= 0 && idx < 13) { track.clockMul = vals[idx]; if (onLinkSync) onLinkSync(); }
        };

        // RND (randomize) button
        addAndMakeVisible (randBtn);
        randBtn.setButtonText ("RND");
        randBtn.onClick = [this]() { showRandomizeMenu(); };

        // INIT (selective reset) button
        addAndMakeVisible (initBtn);
        initBtn.setButtonText ("INI");
        initBtn.onClick = [this]() { showInitMenu(); };

        // Copy track
        addAndMakeVisible (copyBtn);
        copyBtn.setButtonText ("CP");
        copyBtn.onClick = [this]() {
            s_clipboard = track.seq;
            s_clipboardValid = true;
        };

        // Paste track
        addAndMakeVisible (pasteBtn);
        pasteBtn.setButtonText ("PST");
        pasteBtn.onClick = [this]() {
            if (s_clipboardValid)
            {
                track.seq = s_clipboard;
                stepGrid.setStepSequence (&track.seq);
                ratchetRow.setStepSequence (&track.seq);
            }
        };

        // Link group button (OFF→A→B→C→D)
        addAndMakeVisible (linkBtn);
        linkBtn.setButtonText ("L");
        linkBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1e28));
        linkBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff606870));
        linkBtn.onClick = [this]() {
            int oldGroup = track.linkGroup;
            track.linkGroup = (track.linkGroup + 1) % 5; // 0=OFF,1=A,2=B,3=C,4=D

            if (oldGroup == 0 && track.linkGroup > 0)
            {
                // Activating link — save snapshot of ALL synced params
                track.preLinkedSeq = track.seq;
                track.preLinkedLength = track.length;
                track.preLinkedSwing = track.swing;
                track.preLinkedClockMul = track.clockMul;
                track.preLinkedPlayDir = track.playDir;
                track.preLinkedVolume = track.volume;
            }
            else if (oldGroup > 0 && track.linkGroup == 0)
            {
                // Deactivating link — restore ALL synced params
                track.seq = track.preLinkedSeq;
                track.length = track.preLinkedLength;
                track.swing = track.preLinkedSwing;
                track.clockMul = track.preLinkedClockMul;
                track.playDir = track.preLinkedPlayDir;
                track.volume = track.preLinkedVolume;
                // Refresh UI to match restored values
                lenLabel.setText (juce::String (track.length), juce::dontSendNotification);
                swingSlider.setValue (track.swing, juce::dontSendNotification);
                volSlider.setValue (track.volume, juce::dontSendNotification);
                int spdId = clockMulToSpdId (track.clockMul);
                spdSelector.setSelectedId (spdId, juce::dontSendNotification);
                stepGrid.setStepSequence (&track.seq);
            }

            static const char* labels[] = {"L","A","B","C","D"};
            static const juce::Colour cols[] = {
                juce::Colour(0xff606870), juce::Colour(0xff40c8e0),
                juce::Colour(0xfff0a020), juce::Colour(0xff60d068),
                juce::Colour(0xffc060d0)
            };
            linkBtn.setButtonText (labels[track.linkGroup]);
            linkBtn.setColour (juce::TextButton::textColourOffId, cols[track.linkGroup]);
            linkBtn.setColour (juce::TextButton::buttonColourId,
                track.linkGroup > 0 ? cols[track.linkGroup].withAlpha(0.15f) : juce::Colour(0xff1a1e28));
            if (onLinkSync) onLinkSync();
            stepGrid.repaint();
        };

        // VOL label + slider
        addAndMakeVisible (volCaption);
        volCaption.setText ("VOL", juce::dontSendNotification);
        volCaption.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        volCaption.setColour (juce::Label::textColourId, Colours_GB::textDim);
        volCaption.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (volSlider);
        volSlider.setRange (0.0, 1.0, 0.01);
        volSlider.setValue (static_cast<double>(track.volume), juce::dontSendNotification);
        volSlider.setSliderStyle (juce::Slider::LinearVertical);
        volSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volSlider.onValueChange = [this]() {
            track.volume = static_cast<float>(volSlider.getValue());
            if (onLinkSync) onLinkSync();
        };

        // Mute
        addAndMakeVisible (muteBtn);
        muteBtn.setButtonText ("M");
        muteBtn.setClickingTogglesState (true);
        muteBtn.onClick = [this]() { track.muted = muteBtn.getToggleState(); if (onLinkSync) onLinkSync(); };

        // Solo
        addAndMakeVisible (soloBtn);
        soloBtn.setButtonText ("S");
        soloBtn.setClickingTogglesState (true);
        soloBtn.onClick = [this]() { track.solo = soloBtn.getToggleState(); if (onLinkSync) onLinkSync(); };

        // Expand
        addAndMakeVisible (expandBtn);
        expandBtn.setVisible (false); // hidden — click on name label instead
        expandBtn.setClickingTogglesState (true);
        expandBtn.onClick = [this]() {
            engineOpen = expandBtn.getToggleState();
            expandBtn.setButtonText (engineOpen ? "-" : "+");
            if (onExpandToggle) onExpandToggle();
        };

        nameLabel.setInterceptsMouseClicks (true, false);
        nameLabel.addMouseListener (this, false);

        // PLK (parameter lock) button
        addAndMakeVisible (plkBtn);
        plkBtn.setButtonText ("PLK");
        plkBtn.onClick = [this]() {
            plockMode = !plockMode;
            plkBtn.setButtonText (plockMode ? "PLK*" : "PLK");
            if (!plockMode)
            {
                plockTargetStep = -1;
                stepGrid.setPlockStep (-1);
            }
        };

        // Pass plockMode pointer directly to step buttons
        stepGrid.setPlockModePtr (&plockMode);

        // FM mode toggle button (shown in engine panel)
        addChildComponent (fmBtn);
        {
            const char* labels[] = {"ANA", "FM", "SMP", "ER1"};
            fmBtn.setButtonText (labels[std::clamp (track.drumEngine, 0, 3)]);
            fmScreen = (track.drumEngine == 1);
        }
        fmBtn.onClick = [this]() {
            track.drumEngine = (track.drumEngine + 1) % 4;
            const char* labels[] = {"ANA", "FM", "SMP", "ER1"};
            fmBtn.setButtonText (labels[std::clamp (track.drumEngine, 0, 3)]);
            fmScreen = (track.drumEngine == 1);
            if (track.drumEngine == 0) track.fmMix = 0.0f;
            else if (track.drumEngine == 1) track.fmMix = 1.0f;
            buildEngineKnobs();
            resized();
            repaint();
        };

        // Sample LOAD button (only in SMP mode)
        addChildComponent (smpLoadBtn);
        smpLoadBtn.setButtonText ("LOAD");
        smpLoadBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2080a0));
        smpLoadBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        smpLoadBtn.onClick = [this]() {
            juce::PopupMenu loadMenu;
            loadMenu.addItem (1, "Load File");
            loadMenu.addItem (2, "Load Folder");
            loadMenu.addSeparator();
            loadMenu.addItem (3, "Load Slots Folder (P-Lock)");
            int choice = loadMenu.show();
            if (choice == 1)
            {
                auto chooser = std::make_shared<juce::FileChooser> ("Load Sample",
                    track.samplePath.isNotEmpty() ? juce::File (track.samplePath).getParentDirectory()
                        : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                    "*.wav;*.aif;*.aiff;*.mp3;*.flac");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this, chooser](const juce::FileChooser& fc) {
                        auto file = fc.getResult();
                        if (file.existsAsFile())
                            loadSampleFromFile (file);
                    });
            }
            else if (choice == 2)
            {
                auto chooser = std::make_shared<juce::FileChooser> ("Select Sample Folder",
                    track.samplePath.isNotEmpty() ? juce::File (track.samplePath).getParentDirectory()
                        : juce::File::getSpecialLocation (juce::File::userHomeDirectory));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                    [this, chooser](const juce::FileChooser& fc) {
                        auto dir = fc.getResult();
                        if (!dir.isDirectory()) return;
                        sampleFolder.clear();
                        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.wav;*.aiff;*.aif;*.flac;*.mp3"))
                            sampleFolder.add (entry.getFile());
                        struct FileSorter { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()); } };
                        FileSorter fs; sampleFolder.sort (fs);
                        sampleFolderIdx = 0;
                        if (!sampleFolder.isEmpty())
                            loadSampleFromFile (sampleFolder[0]);
                    });
            }
            else if (choice == 3)
            {
                auto chooser = std::make_shared<juce::FileChooser> ("Select Slots Folder",
                    juce::File::getSpecialLocation (juce::File::userHomeDirectory));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                    [this, chooser](const juce::FileChooser& fc) {
                        auto dir = fc.getResult();
                        if (dir.isDirectory())
                            loadSampleFolder (dir);
                    });
            }
        };

        // Zoom buttons for sampler waveform
        addChildComponent (smpZoomInBtn);
        smpZoomInBtn.setButtonText ("+");
        smpZoomInBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        smpZoomInBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff60c0c0));
        smpZoomInBtn.onClick = [this]() {
            wfZoom = std::min (16.0f, wfZoom * 1.5f);
            float vis = 1.0f / wfZoom;
            wfOffset = std::clamp (wfOffset, 0.0f, 1.0f - vis);
            repaint();
        };
        addChildComponent (smpZoomOutBtn);
        smpZoomOutBtn.setButtonText ("-");
        smpZoomOutBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        smpZoomOutBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff60c0c0));
        smpZoomOutBtn.onClick = [this]() {
            wfZoom = std::max (1.0f, wfZoom / 1.5f);
            float vis = 1.0f / wfZoom;
            wfOffset = std::clamp (wfOffset, 0.0f, 1.0f - vis);
            repaint();
        };

        // Prev/Next sample buttons
        addChildComponent (smpPrevBtn);
        smpPrevBtn.setButtonText ("<");
        smpPrevBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        addChildComponent (smpNextBtn);
        smpNextBtn.setButtonText (">");
        smpNextBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252a35));
        smpPrevBtn.onClick = [this]() {
            if (!sampleFolder.isEmpty())
            {
                // Folder mode: browse through loaded folder
                if (sampleFolderIdx > 0)
                    loadSampleFromFile (sampleFolder[--sampleFolderIdx]);
            }
            else
            {
                // Single file mode: browse sibling files in same directory
                if (track.samplePath.isEmpty()) return;
                juce::File cur (track.samplePath);
                auto dir = cur.getParentDirectory();
                auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.mp3;*.flac");
                files.sort();
                int idx = files.indexOf (cur);
                if (idx > 0) loadSampleFromFile (files[idx - 1]);
            }
        };
        smpNextBtn.onClick = [this]() {
            if (!sampleFolder.isEmpty())
            {
                // Folder mode
                if (sampleFolderIdx < sampleFolder.size() - 1)
                    loadSampleFromFile (sampleFolder[++sampleFolderIdx]);
            }
            else
            {
                // Single file mode
                if (track.samplePath.isEmpty()) return;
                juce::File cur (track.samplePath);
                auto dir = cur.getParentDirectory();
                auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.mp3;*.flac");
                files.sort();
                int idx = files.indexOf (cur);
                if (idx >= 0 && idx < files.size() - 1) loadSampleFromFile (files[idx + 1]);
            }
        };

        // Mix knob: blends the OTHER engine in. Always visible in engine panel.
        addChildComponent (mixKnob);
        mixKnob.onChange = [this](float v) {
            // On ANA screen: v blends FM in → fmMix = v
            // On FM screen: v blends ANA in → fmMix = 1-v
            if (!fmScreen)
                track.fmMix = v;
            else
                track.fmMix = 1.0f - v;
        };
        mixKnob.setupPlock (&plockMode, &plockTargetStep, &track.seq, "fmMix");
        mixKnob.onPlockWritten = [this]() { stepGrid.repaint(); };

        // Wire plock step selection from grid
        stepGrid.onPlockSelect = [this](int stepIdx) {
            if (!plockMode) return;
            if (!engineOpen)
            {
                engineOpen = true;
                expandBtn.setToggleState (true, juce::dontSendNotification);
                expandBtn.setButtonText ("-");
                if (onExpandToggle) onExpandToggle();
            }
            if (plockTargetStep == stepIdx)
            {
                // Clicking same step → exit plock mode
                plockMode = false;
                plockTargetStep = -1;
                plkBtn.setButtonText ("PLK");
            }
            else
            {
                plockTargetStep = stepIdx;
            }
            stepGrid.setPlockStep (plockTargetStep);
        };

        stepGrid.onPlockRecToggle = [this](int stepIdx) {
            if (plockMode && plockTargetStep == stepIdx)
            {
                plockMode = false;
                plockTargetStep = -1;
            }
            else
            {
                plockMode = true;
                plockTargetStep = stepIdx;
                if (!engineOpen)
                {
                    engineOpen = true;
                    expandBtn.setToggleState (true, juce::dontSendNotification);
                    expandBtn.setButtonText ("-");
                    if (onExpandToggle) onExpandToggle();
                }
            }
            plkBtn.setButtonText (plockMode ? "PLK*" : "PLK");
            stepGrid.setPlockStep (plockTargetStep);
        };

        buildEngineKnobs();

        // Tab buttons
        auto setupTab = [this](juce::TextButton& btn, int tabIdx) {
            addAndMakeVisible (btn);
            btn.setClickingTogglesState (false);
            btn.onClick = [this, tabIdx]() {
                currentTab = tabIdx;
                resized();
                repaint();
            };
        };
        setupTab (tabEng, 0);
        setupTab (tabFx, 1);
        setupTab (tabLfo, 2);
        setupTab (tabMseg, 3);

        // MSEG editor
        msegEditor = std::make_unique<MSEGEditor> (track.msegs[static_cast<size_t>(msegIdx)]);
        addChildComponent (*msegEditor);

        // Per-track preset button
        addAndMakeVisible (pstBtn);
        pstBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2060a0));
        pstBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        pstBtn.onClick = [this]() {
            juce::PopupMenu menu;
            if (lastSavedPresetName.isNotEmpty())
                menu.addItem (1, "Save \"" + lastSavedPresetName + "\"");
            menu.addItem (3, "Save As...");
            menu.addSeparator();

            std::vector<juce::File> loadFiles;
            auto loadMenu = PresetManager::buildBrowseMenu (
                PresetManager::PresetType::Drum, "", 100, loadFiles);
            if (loadFiles.empty()) loadMenu.addItem (-1, "(no presets)", false);
            menu.addSubMenu ("Load Drum Preset", loadMenu);
            menu.addSeparator();

            std::vector<juce::File> delFiles;
            auto delMenu = PresetManager::buildBrowseMenu (
                PresetManager::PresetType::Drum, "", 2000, delFiles);
            if (!delFiles.empty())
                menu.addSubMenu ("Delete Preset", delMenu);
            menu.addSeparator();

            menu.addItem (2, "New Folder...");

            std::vector<juce::File> delFolders;
            auto folderDelMenu = PresetManager::buildFolderDeleteMenu (
                PresetManager::PresetType::Drum, 3000, delFolders);
            if (!delFolders.empty())
                menu.addSubMenu ("Delete Folder", folderDelMenu);

            int result = menu.show();
            if (result == 0) return;

            if (result == 1 && lastSavedPresetName.isNotEmpty())
            {
                // Direct overwrite — no dialog
                PresetManager::saveDrumEngine (track, lastSavedPresetName, lastSavedPresetFolder);
            }
            else if (result == 3)
                PresetManager::showSaveDialog (PresetManager::PresetType::Drum,
                    lastSavedPresetName.isNotEmpty() ? lastSavedPresetName : drumId (track.type),
                    [this](juce::String name, juce::String folder) {
                        PresetManager::saveDrumEngine (track, name, folder);
                        lastSavedPresetName = name;
                        lastSavedPresetFolder = folder;
                    });
            else if (result == 2)
                PresetManager::showNewFolderDialog (PresetManager::PresetType::Drum);
            else if (result >= 100 && result < 2000)
            {
                int fi = result - 100;
                if (fi >= 0 && fi < static_cast<int>(loadFiles.size()))
                {
                    PresetManager::loadDrumEngine (track, loadFiles[static_cast<size_t>(fi)]);
                    lastSavedPresetName = loadFiles[static_cast<size_t>(fi)].getFileNameWithoutExtension();
                    auto parent = loadFiles[static_cast<size_t>(fi)].getParentDirectory();
                    auto typeDir = PresetManager::getTypeDir (PresetManager::PresetType::Drum);
                    lastSavedPresetFolder = (parent == typeDir) ? juce::String() : parent.getFileName();
                    buildEngineKnobs();
                    resized();
                    repaint();
                }
            }
            else if (result >= 2000 && result < 3000)
            {
                int di = result - 2000;
                if (di >= 0 && di < static_cast<int>(delFiles.size()))
                    PresetManager::deletePreset (delFiles[static_cast<size_t>(di)]);
            }
            else if (result >= 3000)
            {
                int fi2 = result - 3000;
                if (fi2 >= 0 && fi2 < static_cast<int>(delFolders.size()))
                    PresetManager::deleteFolder (delFolders[static_cast<size_t>(fi2)]);
            }
        };

        // LED Meter
        addAndMakeVisible (ledMeter);
    }

    void setLevel (float lv) { ledMeter.setLevel (lv); }

    void syncMuteSolo()
    {
        if (muteBtn.getToggleState() != track.muted)
            muteBtn.setToggleState (track.muted, juce::dontSendNotification);
        if (soloBtn.getToggleState() != track.solo)
            soloBtn.setToggleState (track.solo, juce::dontSendNotification);
        // Sync SPD selector from track clockMul (preset load)
        int spdId = clockMulToSpdId (track.clockMul);
        if (spdSelector.getSelectedId() != spdId)
            spdSelector.setSelectedId (spdId, juce::dontSendNotification);
    }

    static int clockMulToSpdId (float cm)
    {
        const float vals[] = { 0.125f, 0.25f, 0.333f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f };
        int best = 7; float bestD = 999.0f;
        for (int i = 0; i < 13; ++i) { float d = std::abs (cm - vals[i]); if (d < bestD) { bestD = d; best = i + 1; } }
        return best;
    }

    void setMsegPlayhead (float p0, float p1, float p2)
    {
        if (msegEditor && msegEditor->isVisible())
        {
            float phases[3] = { p0, p1, p2 };
            msegEditor->setPlayheadPosition (phases[msegIdx]);
            msegEditor->repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Page button events — forwarded via addMouseListener
        for (int p = 0; p < pageButtons.size(); ++p)
        {
            if (e.eventComponent == pageButtons[p])
            {
                if (e.mods.isPopupMenu())
                    showPageContextMenu (p);
                return;
            }
        }

        // Convert to local coordinates (events from addMouseListener arrive
        // in the source component's space, not ours)
        auto localPos = e.getEventRelativeTo (this).getPosition();

        // Waveform start/end drag (sampler mode)
        if (track.drumEngine == 2 && currentTab == 0 && !waveformBounds.isEmpty()
            && waveformBounds.contains (localPos))
        {
            float wfX = static_cast<float>(waveformBounds.getX());
            float wfW = static_cast<float>(waveformBounds.getWidth());
            float wfY = static_cast<float>(waveformBounds.getY());
            float wfHf = static_cast<float>(waveformBounds.getHeight());
            float vis = 1.0f / wfZoom;
            float viewStart = wfOffset;
            float viewEnd = viewStart + vis;
            float normClick = (static_cast<float>(localPos.x) - wfX) / wfW * vis + wfOffset;

            // ── Right-click on ruler → grid div popup OR marker context menu ──
            if (e.mods.isRightButtonDown() && track.smpWarp > 0)
            {
                static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                int hitM = track.warpMarkers.empty() ? -1 :
                    WaveformOverlay::hitTestMarker (static_cast<float>(localPos.x), static_cast<float>(localPos.y),
                        wfX, wfY, wfW, wfHf, viewStart, viewEnd, track.warpMarkers, totalBeats);
                if (hitM >= 0)
                {
                    WaveformOverlay::showMarkerMenu (this, hitM, track.warpMarkers, totalBeats, [this](){ repaint(); });
                    return;
                }
                if (WaveformOverlay::isInRuler (static_cast<float>(localPos.y), wfY))
                {
                    WaveformOverlay::showGridDivMenu (this, track.gridDiv, [this](int d){ track.gridDiv = d; repaint(); });
                    return;
                }
            }

            // ── Warp marker interaction (priority) ──
            if (track.smpWarp > 0)
            {
                static const float barLUT2[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                float hitTotalBeats = barLUT2[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                int hitIdx = track.warpMarkers.empty() ? -1 :
                    WaveformOverlay::hitTestMarker (
                        static_cast<float>(localPos.x), static_cast<float>(localPos.y),
                        wfX, wfY, wfW, wfHf, viewStart, viewEnd, track.warpMarkers, hitTotalBeats);

                // Double-click on marker → REMOVE it
                if (hitIdx >= 0 && e.getNumberOfClicks() >= 2 && e.mods.isLeftButtonDown())
                {
                    if (hitIdx > 0 && hitIdx < static_cast<int>(track.warpMarkers.size()) - 1)
                        track.warpMarkers.erase (track.warpMarkers.begin() + hitIdx);
                    warpHoveredMarker = -1; repaint(); return;
                }

                if (hitIdx >= 0 && e.mods.isLeftButtonDown())
                { warpDraggedMarker = hitIdx; wfDragMode = 4; repaint(); return; }

                // Double-click on empty space → ADD marker
                if (e.getNumberOfClicks() >= 2 && e.mods.isLeftButtonDown() && track.smpBars > 0)
                {
                    // Calculate beatPos from CURRENT warp mapping (Ableton-style)
                    // Adding a marker should change NOTHING — it captures the exact current state
                    static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                    float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                    float beatPos = 0.0f;
                    auto& wm = track.warpMarkers;
                    if (wm.size() >= 2)
                    {
                        for (size_t m = 0; m + 1 < wm.size(); ++m)
                        {
                            if (normClick >= wm[m].samplePos && (normClick <= wm[m + 1].samplePos || m + 2 == wm.size()))
                            {
                                float sLen = wm[m + 1].samplePos - wm[m].samplePos;
                                float bLen = wm[m + 1].beatPos - wm[m].beatPos;
                                float frac = (sLen > 0.001f) ? (normClick - wm[m].samplePos) / sLen : 0.0f;
                                beatPos = wm[m].beatPos + frac * bLen;
                                break;
                            }
                        }
                    }
                    else
                    {
                        beatPos = normClick * totalBeats;
                    }
                    WarpMarker newM; newM.samplePos = std::clamp (normClick, 0.001f, 0.999f);
                    newM.beatPos = beatPos; newM.originalSamplePos = newM.samplePos; newM.isAuto = false;
                    track.warpMarkers.push_back (newM);
                    std::sort (track.warpMarkers.begin(), track.warpMarkers.end(),
                        [](const WarpMarker& a, const WarpMarker& b) { return a.samplePos < b.samplePos; });
                    repaint(); return;
                }
            }

            float distStart = std::abs (normClick - track.smpStart);
            float distEnd = std::abs (normClick - track.smpEnd);
            float threshold = 0.02f;
            wfDragMode = (distStart < distEnd) ? 1 : 2;
            if (distStart > threshold && distEnd > threshold) wfDragMode = 0;
            return;
        }
        if (nameLabel.getBounds().contains (localPos))
        {
            if (e.mods.isPopupMenu())
            {
                showTrackContextMenu();
                return;
            }
            engineOpen = !engineOpen;
            expandBtn.setToggleState (engineOpen, juce::dontSendNotification);
            if (onExpandToggle) onExpandToggle();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (wfDragMode > 0 && !waveformBounds.isEmpty())
        {
            float wfX = static_cast<float>(waveformBounds.getX());
            float wfW = static_cast<float>(waveformBounds.getWidth());
            float vis = 1.0f / wfZoom;
            float normPos = std::clamp ((static_cast<float>(e.x) - wfX) / wfW * vis + wfOffset, 0.0f, 1.0f);
            if (wfDragMode == 1) track.smpStart = std::min (normPos, track.smpEnd - 0.01f);
            else if (wfDragMode == 2) track.smpEnd = std::max (normPos, track.smpStart + 0.01f);
            else if (wfDragMode == 4 && warpDraggedMarker >= 0
                     && warpDraggedMarker < static_cast<int>(track.warpMarkers.size()))
            {
                auto& m = track.warpMarkers[static_cast<size_t>(warpDraggedMarker)];
                // Convert mouse position to beat position (Ableton-style: drag WHEN it plays, not WHAT plays)
                static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                float beatPos = normPos * totalBeats;
                // Clamp between neighboring markers' beat positions
                float minB = 0.01f, maxB = totalBeats - 0.01f;
                if (warpDraggedMarker > 0)
                    minB = track.warpMarkers[static_cast<size_t>(warpDraggedMarker - 1)].beatPos + 0.01f;
                if (warpDraggedMarker < static_cast<int>(track.warpMarkers.size()) - 1)
                    maxB = track.warpMarkers[static_cast<size_t>(warpDraggedMarker + 1)].beatPos - 0.01f;
                m.beatPos = std::clamp (beatPos, minB, maxB);
                m.isAuto = false;
            }
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        wfDragMode = 0;
        warpDraggedMarker = -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (track.drumEngine == 2 && currentTab == 0 && track.smpWarp > 0
            && !waveformBounds.isEmpty() && !track.warpMarkers.empty())
        {
            float wfX = static_cast<float>(waveformBounds.getX());
            float wfW = static_cast<float>(waveformBounds.getWidth());
            float wfY = static_cast<float>(waveformBounds.getY());
            float wfHf = static_cast<float>(waveformBounds.getHeight());
            float vis = 1.0f / wfZoom;
            int oldHover = warpHoveredMarker;
            warpHoveredMarker = WaveformOverlay::hitTestMarker (
                static_cast<float>(e.x), static_cast<float>(e.y),
                wfX, wfY, wfW, wfHf, wfOffset, wfOffset + vis, track.warpMarkers,
                (track.smpWarp > 0 && track.smpBars > 0) ? [&](){
                    static const float bl[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                    return bl[std::clamp(track.smpBars, 1, 8)] * 4.0f; }() : 0.0f);
            if (warpHoveredMarker != oldHover) repaint();
        }
        else if (warpHoveredMarker >= 0)
        {
            warpHoveredMarker = -1;
            repaint();
        }
    }

    void showTrackContextMenu()
    {
        // Collect which parameters have p-locks across all steps
        std::set<std::string> usedParams;
        int totalPlocks = 0;
        for (auto& s : track.seq.steps)
        {
            for (auto& kv : s.plocks)
            {
                usedParams.insert (kv.first);
                ++totalPlocks;
            }
        }

        juce::PopupMenu menu;
        menu.addSectionHeader ("MOTION");

        if (totalPlocks > 0)
        {
            menu.addItem (1, "Clear ALL Motion (" + juce::String (totalPlocks) + " locks)");
            menu.addSeparator();

            int itemId = 100;
            for (auto& pName : usedParams)
            {
                // Count how many steps have this param
                int count = 0;
                for (auto& s : track.seq.steps)
                    if (s.plocks.count (pName)) ++count;
                menu.addItem (itemId, "Clear \"" + juce::String (pName) + "\" (" + juce::String (count) + " steps)");
                ++itemId;
            }
        }
        else
        {
            menu.addItem (-1, "No motion recorded", false);
        }

        menu.addSeparator();
        menu.addSectionHeader ("PLAY DIR");
        {
            static const char* dirNames[] = { "FWD", "REV", "PING", "RND", "ONE" };
            for (int d = 0; d < 5; ++d)
                menu.addItem (200 + d, dirNames[d], true, track.playDir == d);
        }

        menu.addSeparator();
        menu.addSectionHeader ("CHOKE GROUP");
        {
            menu.addItem (400, "OFF", true, track.chokeGroup == 0);
            static const char* grpNames[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
            for (int g = 0; g < 8; ++g)
                menu.addItem (401 + g, grpNames[g], true, track.chokeGroup == g + 1);
        }

        menu.addSeparator();
        menu.addSectionHeader ("LINK GROUP");
        {
            menu.addItem (500, "OFF", true, track.linkGroup == 0);
            static const char* lnkNames[] = { "A", "B", "C", "D" };
            for (int g = 0; g < 4; ++g)
                menu.addItem (501 + g, lnkNames[g], true, track.linkGroup == g + 1);
        }

        menu.addSeparator();
        menu.addSectionHeader ("DRUM TYPE");
        {
            static const DrumType allTypes[] = {
                DrumType::Kick, DrumType::Snare, DrumType::HiHatClosed,
                DrumType::HiHatOpen, DrumType::Clap, DrumType::Tom,
                DrumType::TomHi, DrumType::Cowbell, DrumType::Rimshot, DrumType::Crash
            };
            for (int d = 0; d < 10; ++d)
                menu.addItem (300 + d, drumName (allTypes[d]), true, track.type == allTypes[d]);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameLabel),
            [this, usedParams] (int result)
            {
                if (result == 1)
                {
                    for (auto& s : track.seq.steps)
                        s.plocks.clear();
                }
                else if (result >= 300 && result < 310)
                {
                    static const DrumType allTypes[] = {
                        DrumType::Kick, DrumType::Snare, DrumType::HiHatClosed,
                        DrumType::HiHatOpen, DrumType::Clap, DrumType::Tom,
                        DrumType::TomHi, DrumType::Cowbell, DrumType::Rimshot, DrumType::Crash
                    };
                    DrumType newType = allTypes[result - 300];
                    if (newType != track.type)
                    {
                        track.setType (newType);
                        nameLabel.setText (drumName (track.type), juce::dontSendNotification);
                        buildEngineKnobs();
                        resized();
                    }
                }
                else if (result >= 200 && result < 205)
                {
                    track.playDir = result - 200;
                    if (onLinkSync) onLinkSync();
                }
                else if (result >= 400 && result <= 408)
                {
                    track.chokeGroup = result - 400;
                }
                else if (result >= 500 && result <= 504)
                {
                    track.linkGroup = result - 500;
                }
                else if (result >= 100)
                {
                    int idx = result - 100;
                    auto it = usedParams.begin();
                    std::advance (it, idx);
                    if (it != usedParams.end())
                    {
                        std::string key = *it;
                        for (auto& s : track.seq.steps)
                            s.plocks.erase (key);
                    }
                }
                stepGrid.repaint();
                repaint();
            });
    }

    void showPageContextMenu (int srcPage)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("PAGE " + juce::String (srcPage + 1));

        // Copy to each other page
        for (int dst = 0; dst < kNumPages; ++dst)
        {
            if (dst == srcPage) continue;
            menu.addItem (10 + dst, "Copy -> Page " + juce::String (dst + 1));
        }
        menu.addItem (50, "Copy -> ALL pages");
        menu.addSeparator();
        menu.addItem (60, "Clear Page " + juce::String (srcPage + 1));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pageButtons[srcPage]),
            [this, srcPage](int result)
            {
                if (result == 0) return;

                if (result >= 10 && result < 10 + kNumPages)
                {
                    copyPage (srcPage, result - 10);
                }
                else if (result == 50)
                {
                    for (int d = 0; d < kNumPages; ++d)
                        if (d != srcPage) copyPage (srcPage, d);
                }
                else if (result == 60)
                {
                    int base = srcPage * kStepsPerPage;
                    for (int i = 0; i < kStepsPerPage; ++i)
                        track.seq.steps[static_cast<size_t>(base + i)].reset();
                }

                stepGrid.repaint();
                ratchetRow.repaint();
                repaint();
            });
    }

    void copyPage (int srcPage, int dstPage)
    {
        int srcBase = srcPage * kStepsPerPage;
        int dstBase = dstPage * kStepsPerPage;
        for (int i = 0; i < kStepsPerPage; ++i)
            track.seq.steps[static_cast<size_t>(dstBase + i)] =
                track.seq.steps[static_cast<size_t>(srcBase + i)];
    }

    void setPlayingStep (int step)
    {
        if (step >= 0)
        {
            int newPage = step / kStepsPerPage;
            // Reset manual override when playback reaches the displayed page
            if (manualPageOverride && newPage == track.page)
                manualPageOverride = false;

            if (!manualPageOverride && newPage != lastAutoPage && newPage < kNumPages)
            {
                lastAutoPage = newPage;
                track.page = newPage;
                stepGrid.setPage (newPage);
                ratchetRow.setPage (newPage);
                updatePageButtons();
            }
        }
        else
        {
            // Transport stopped — re-enable auto-follow
            manualPageOverride = false;
            lastAutoPage = -1;
        }
        stepGrid.setPlayingStep (step);
    }
    bool isEngineOpen() const { return engineOpen; }
    int getDesiredHeight() const { return engineOpen ? 260 : 56; }

    std::function<void()> onExpandToggle;
    std::function<void()> onBeforeEdit; // called before destructive actions for undo
    std::function<void()> onLinkSync;   // called after mute/solo change for link propagation
    std::function<void()> onStepSync;   // called after step edit for linked step propagation

    // ── Load sample folder into sample slots ──
    void loadSampleFolder (const juce::File& folder)
    {
        if (!folder.isDirectory()) return;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        track.sampleSlots.clear();
        track.sampleSlotNames.clear();
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        files.sort();
        juce::File firstFile;
        for (const auto& f : files)
        {
            if (auto* reader = fm.createReaderFor (f))
            {
                auto buf = std::make_shared<juce::AudioBuffer<float>> (static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                reader->read (buf.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
                track.sampleSlots.push_back (buf);
                track.sampleSlotNames.push_back (f.getFileNameWithoutExtension());
                if (firstFile == juce::File()) firstFile = f;
                delete reader;
                if (track.sampleSlots.size() >= 128) break;
            }
        }
        // Load first sample as main track sample (waveform, GATE, stretch — everything)
        if (firstFile.existsAsFile())
            loadSampleFromFile (firstFile);
    }

    void rebuildKnobs() { buildEngineKnobs(); resized(); }

    // Refresh step grid + length/swing/SPD after link sync changed our data
    void refreshLinkedSteps()
    {
        stepGrid.setStepSequence (&track.seq);
        lenLabel.setText (juce::String (track.length), juce::dontSendNotification);
        swingSlider.setValue (track.swing, juce::dontSendNotification);
        volSlider.setValue (track.volume, juce::dontSendNotification);
        int spdId = clockMulToSpdId (track.clockMul);
        spdSelector.setSelectedId (spdId, juce::dontSendNotification);
        muteBtn.setToggleState (track.muted, juce::dontSendNotification);
        soloBtn.setToggleState (track.solo, juce::dontSendNotification);
        stepGrid.repaint();
    }

    void loadSampleFromFile (const juce::File& file)
    {
        if (!file.existsAsFile()) return;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
        if (rd)
        {
            auto buf = std::make_shared<juce::AudioBuffer<float>> (
                static_cast<int>(rd->numChannels), static_cast<int>(rd->lengthInSamples));
            rd->read (buf.get(), 0, static_cast<int>(rd->lengthInSamples), 0, true, true);
            track.sampleData = buf;
            track.samplePath = file.getFullPathName();
            track.smpFileSR = static_cast<float>(rd->sampleRate);

            // ── CRITICAL: reset sample region on new load ──
            track.smpStart = 0.0f;
            track.smpEnd = 1.0f;
            track.smpRootNote = 60; // C3 (Ableton convention)
            track.smpStretch = 1.0f;
            track.smpSyncMul = 0;
            track.smpPlayMode = 1; // default GATE (not one-shot)

            // ── BPM + bar detection + auto warp ──
            auto analysis = SampleAnalysis::analyze (*buf, rd->sampleRate);
            if (analysis.isLoop && analysis.bpm > 0.0f)
            {
                track.smpBPM = analysis.bpm;
                track.smpBpmSync = 1;
                track.smpBars = analysis.barIndex;
                track.smpLoop = 1; // auto-enable loop for detected loops
                if (track.smpWarp == 0)
                    track.smpWarp = 1; // auto-enable BEATS mode
                // 2 markers only: start + end → uniform stretching (Ableton-style)
                // Sample sounds identical, just time-stretched to project BPM
                {
                    static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                    float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                    track.warpMarkers.clear();
                    WarpMarker startM; startM.samplePos = 0.0f; startM.beatPos = 0.0f;
                    startM.originalSamplePos = 0.0f; startM.isAuto = true;
                    WarpMarker endM; endM.samplePos = 1.0f; endM.beatPos = totalBeats;
                    endM.originalSamplePos = 1.0f; endM.isAuto = true;
                    track.warpMarkers.push_back (startM);
                    track.warpMarkers.push_back (endM);
                }
            }
            else
            {
                track.smpBpmSync = 0;
                track.smpLoop = 0;
                track.warpMarkers.clear();
                if (analysis.rootNote >= 0)
                    track.smpRootNote = analysis.rootNote;
            }

            // Refresh ALL knobs
            for (auto* k : engineKnobs)
            {
                if (k->getName() == "WARP")      k->setValue (static_cast<float>(track.smpWarp), false);
                else if (k->getName() == "BPM")   k->setValue (track.smpBPM, false);
                else if (k->getName() == "ROOT")  k->setValue (static_cast<float>(track.smpRootNote), false);
                else if (k->getName() == "SYNC")  k->setValue (static_cast<float>(track.smpBpmSync), false);
                else if (k->getName() == "BARS")  k->setValue (static_cast<float>(track.smpBars), false);
                else if (k->getName() == "LOOP")  k->setValue (static_cast<float>(track.smpLoop), false);
                else if (k->getName() == "PLAY")  k->setValue (static_cast<float>(track.smpPlayMode), false);
                else if (k->getName() == "STRT")  k->setValue (track.smpStart, false);
                else if (k->getName() == "END")   k->setValue (track.smpEnd, false);
            }

            repaint();
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (2);

        // ── RIGHT STRIP: meter + fader + M/S — full collapsed-row height ──
        auto rightStrip = bounds.removeFromRight (46);
        auto meterFaderArea = rightStrip.removeFromTop (52); // mainRow(36) + ratchet(14) + gap(2)
        meterFaderArea.reduce (0, 2); // vertical padding

        ledMeter.setBounds (meterFaderArea.removeFromRight (10));
        meterFaderArea.removeFromRight (2);
        volSlider.setBounds (meterFaderArea.removeFromRight (14));
        meterFaderArea.removeFromRight (2);
        // M and S stacked vertically
        auto msArea = meterFaderArea.removeFromRight (17);
        muteBtn.setBounds (msArea.removeFromTop (msArea.getHeight() / 2).reduced (0, 1));
        soloBtn.setBounds (msArea.reduced (0, 1));

        // ── MAIN ROW (36px) ──
        auto mainRow = bounds.removeFromTop (36);

        // Left: label (with padding for engine icon at -18px from label)
        mainRow.removeFromLeft (10); // left margin for engine icon
        nameLabel.setBounds (mainRow.removeFromLeft (56).reduced (16, 6));
        mainRow.removeFromLeft (1);

        // Page buttons — LEFT of step grid (8 pages, compact)
        mainRow.removeFromLeft (4); // padding from name label
        for (int i = 0; i < pageButtons.size(); ++i)
        {
            pageButtons[i]->setBounds (mainRow.getX() + i * 15,
                                        mainRow.getY() + 8,
                                        14, mainRow.getHeight() - 16);
        }
        mainRow.removeFromLeft (kNumPages * 15 + 2);

        // Right controls (no meter/fader/M/S — they're in the right strip now)
        auto ctrlArea = mainRow.removeFromRight (280);

        expandBtn.setBounds (ctrlArea.removeFromRight (17).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        plkBtn.setBounds (ctrlArea.removeFromRight (22).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        linkBtn.setBounds (ctrlArea.removeFromRight (17).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        randBtn.setBounds (ctrlArea.removeFromRight (22).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        initBtn.setBounds (ctrlArea.removeFromRight (22).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        copyBtn.setBounds (ctrlArea.removeFromRight (18).reduced (0, 7));
        ctrlArea.removeFromRight (1);
        pasteBtn.setBounds (ctrlArea.removeFromRight (20).reduced (0, 7));
        ctrlArea.removeFromRight (1);

        auto swgArea = ctrlArea.removeFromRight (32);
        swingCaption.setBounds (swgArea.removeFromTop (10));
        swingSlider.setBounds (swgArea.reduced (0, 3));
        ctrlArea.removeFromRight (1);

        auto spdArea = ctrlArea.removeFromRight (32);
        spdCaption.setBounds (spdArea.removeFromTop (10));
        spdSelector.setBounds (spdArea.reduced (0, 3));
        ctrlArea.removeFromRight (1);

        auto lenArea = ctrlArea.removeFromRight (26);
        lenCaption.setBounds (lenArea.removeFromTop (10));
        lenLabel.setBounds (lenArea.reduced (0, 1));

        // Step grid takes remaining center space
        stepGrid.setBounds (mainRow.reduced (0, 2));

        // Ratchet row (below grid)
        auto ratchetArea = bounds.removeFromTop (14);
        ratchetArea = ratchetArea.withLeft (stepGrid.getX()).withWidth (stepGrid.getWidth());
        ratchetRow.setBounds (ratchetArea);

        // Engine panel — TABBED: [ENG] [FX] [LFO]
        if (engineOpen && bounds.getHeight() > 10)
        {
            auto engineArea = bounds.reduced (4, 2);
            enginePanelY = engineArea.getY();

            // Tab strip (left side, stacked vertically)
            int tabW = 28;
            auto tabStrip = engineArea.removeFromLeft (tabW + 2);
            int tabH = (tabStrip.getHeight() - 8) / 5;
            tabEng.setVisible (true);
            tabFx.setVisible (true);
            tabLfo.setVisible (true);
            tabMseg.setVisible (true);
            pstBtn.setVisible (true);
            tabEng.setBounds (tabStrip.getX(), tabStrip.getY(), tabW, tabH);
            tabFx.setBounds (tabStrip.getX(), tabStrip.getY() + tabH + 1, tabW, tabH);
            tabLfo.setBounds (tabStrip.getX(), tabStrip.getY() + 2 * (tabH + 1), tabW, tabH);
            tabMseg.setBounds (tabStrip.getX(), tabStrip.getY() + 3 * (tabH + 1), tabW, tabH);
            pstBtn.setBounds (tabStrip.getX(), tabStrip.getY() + 4 * (tabH + 1), tabW, tabH);

            auto inactiveCol = juce::Colour (0x01000000);
            auto activeTextCol = Colours_GB::accent;
            auto inactiveTextCol = juce::Colour (0xff707888);
            auto setTab = [&](juce::TextButton& btn, int tabIdx) {
                btn.setColour (juce::TextButton::buttonColourId, inactiveCol);
                btn.setColour (juce::TextButton::buttonOnColourId, inactiveCol);
                btn.setColour (juce::TextButton::textColourOffId, currentTab == tabIdx ? activeTextCol : inactiveTextCol);
            };
            setTab (tabEng, 0); setTab (tabFx, 1); setTab (tabLfo, 2); setTab (tabMseg, 3);

            // ENG tab: fmBtn + mixKnob + engine knobs
            // FX tab: FX knobs in 2 rows
            int fxStart = numAnalogKnobs + numFmKnobs + numSmpKnobs + numER1Knobs;

            // ── RESET: Hide all controls before showing tab-specific ones ──
            fmBtn.setVisible (false);
            smpLoadBtn.setVisible (false);
            smpZoomInBtn.setVisible (false);
            smpZoomOutBtn.setVisible (false);
            smpPrevBtn.setVisible (false);
            smpNextBtn.setVisible (false);
            mixKnob.setVisible (false);
            for (auto* k : engineKnobs) k->setVisible (false);
            for (auto* k : msegKnobs) removeChildComponent (k);
            if (msegEditor) msegEditor->setVisible (false);
            if (msegTargetBtn) msegTargetBtn->setVisible (false); if (msegRetrigBtn) msegRetrigBtn->setVisible (false); for (auto& b : msegSelBtns) b.setVisible (false);

            if (currentTab == 0)
            {
                fmBtn.setVisible (true);
                fmBtn.setBounds (engineArea.getX(), engineArea.getY() + 2, 32, 14);
                engineArea.removeFromTop (14); // space for category labels
                int x = engineArea.getX() + 36;

                // LOAD button for sampler — positioned after waveform like synth
                smpLoadBtn.setVisible (track.drumEngine == 2);
                smpZoomInBtn.setVisible (track.drumEngine == 2);
                smpZoomOutBtn.setVisible (track.drumEngine == 2);
                smpPrevBtn.setVisible (track.drumEngine == 2);
                smpNextBtn.setVisible (track.drumEngine == 2);
                if (track.drumEngine == 2)
                {
                    mixKnob.setVisible (false);
                }
                else
                {
                    mixKnob.setVisible (true);
                    mixKnob.setBounds (x, engineArea.getY(), 50, engineArea.getHeight());
                    x += 51;
                }

                int knobW = 50;

                bool showFM = fmScreen;
                int startK, endK;
                if (track.drumEngine == 2) {
                    startK = numAnalogKnobs + numFmKnobs;
                    endK = numAnalogKnobs + numFmKnobs + numSmpKnobs + numER1Knobs;
                    mixKnob.setVisible (false);

                    // ── SAMPLER LAYOUT (identical to synth sampler) ──
                    // Waveform takes top 42% of engine area
                    engineArea.removeFromTop (static_cast<int>(engineArea.getHeight() * 0.42f));

                    // Buttons between waveform and knobs (same as synth: < > LOAD - +)
                    {
                        int bY = engineArea.getY() - 18;
                        int bX = engineArea.getX();
                        smpPrevBtn.setBounds (bX, bY, 16, 16);
                        smpNextBtn.setBounds (bX + 18, bY, 16, 16);
                        smpLoadBtn.setBounds (bX + 36, bY, 42, 16);
                        smpZoomOutBtn.setBounds (bX + 82, bY, 16, 16);
                        smpZoomInBtn.setBounds (bX + 100, bY, 16, 16);
                    }

                    // Category labels + 2-row knob layout with group gaps (same as synth)
                    engineArea.removeFromTop (14); // row1 label space
                    int smpRowH = (engineArea.getHeight() - 14) / 2;
                    auto smpRow1 = engineArea.removeFromTop (smpRowH);
                    engineArea.removeFromTop (14); // row2 label space
                    auto smpRow2 = engineArea;

                    int numVisible = endK - startK;
                    // SAMPLE(6)+PITCH(3)+AMP(4)=13 on row1, FILTER(6)+FILT_ENV(4)+FM(4)+STRETCH(5)=19 wraps
                    int knobsRow1 = (numVisible >= 24) ? 13 : std::min (numVisible, std::max (1, smpRow1.getWidth() / knobW));
                    int gapW = 4;

                    // Build x positions with group gaps
                    auto buildXPos = [&](int rowStart, int rowCount, int baseIdx) {
                        std::vector<int> xp (static_cast<size_t>(rowCount), 0);
                        int xAcc = 0;
                        for (int ri = 0; ri < rowCount; ++ri)
                        {
                            int absIdx = baseIdx + ri;
                            if (ri > 0)
                            {
                                for (int gs : smpGroupStarts)
                                    if (gs == absIdx) { xAcc += gapW; break; }
                            }
                            xp[static_cast<size_t>(ri)] = xAcc;
                            xAcc += knobW;
                        }
                        return xp;
                    };

                    auto xRow1 = buildXPos (0, knobsRow1, startK);
                    auto xRow2 = buildXPos (0, numVisible - knobsRow1, startK + knobsRow1);

                    int ki = 0;
                    for (int idx = 0; idx < static_cast<int>(engineKnobs.size()); ++idx)
                    {
                        if (idx >= startK && idx < endK)
                        {
                            engineKnobs[idx]->setVisible (true);
                            if (ki < knobsRow1)
                                engineKnobs[idx]->setBounds (smpRow1.getX() + xRow1[static_cast<size_t>(ki)], smpRow1.getY(), knobW, smpRow1.getHeight());
                            else
                            {
                                int ki2 = ki - knobsRow1;
                                engineKnobs[idx]->setBounds (smpRow2.getX() + xRow2[static_cast<size_t>(ki2)], smpRow2.getY(), knobW, smpRow2.getHeight());
                            }
                            ki++;
                        }
                        else
                        {
                            engineKnobs[idx]->setVisible (false);
                        }
                    }
                } else if (track.drumEngine == 3) {
                    // ER-1 layout: no waveform, no mix knob, just ER-1 knobs
                    startK = numAnalogKnobs + numFmKnobs + numSmpKnobs;
                    endK = startK + numER1Knobs;
                    mixKnob.setVisible (false);

                    for (int ki = 0; ki < static_cast<int>(engineKnobs.size()); ++ki)
                    {
                        if (ki >= startK && ki < endK)
                        {
                            engineKnobs[ki]->setVisible (true);
                            engineKnobs[ki]->setBounds (x, engineArea.getY(), knobW, engineArea.getHeight());
                            x += knobW + 1;
                        }
                        else
                        {
                            engineKnobs[ki]->setVisible (false);
                        }
                    }
                } else {
                    startK = showFM ? numAnalogKnobs : 0;
                    endK = showFM ? numAnalogKnobs + numFmKnobs : numAnalogKnobs;

                    for (int ki = 0; ki < static_cast<int>(engineKnobs.size()); ++ki)
                    {
                        if (ki >= startK && ki < endK)
                        {
                            engineKnobs[ki]->setVisible (true);
                            engineKnobs[ki]->setBounds (x, engineArea.getY(), knobW, engineArea.getHeight());
                            x += knobW + 1;
                        }
                        else
                        {
                            engineKnobs[ki]->setVisible (false);
                        }
                    }
                }
            }
            else if (currentTab == 1)
            {
                fmBtn.setVisible (false);
                smpLoadBtn.setVisible (false);
                smpZoomInBtn.setVisible (false);
                smpZoomOutBtn.setVisible (false);
                smpPrevBtn.setVisible (false);
                smpNextBtn.setVisible (false);
                mixKnob.setVisible (false);

                engineArea.removeFromTop (14); // space for category labels
                int numFxKnobs = lfoStartIdx - fxStart;
                int knobsPerRow = std::max (1, (numFxKnobs + 1) / 2);
                // Row1: DRIVE(3)+CHORUS(3)+DELAY(8)+OTT(3)+PHASER(4)=21
                // Row2: REVERB(4)+EQ(3)+OUT(3)+DUCK(4)+SAT(5)+FLANGER(4)=23
                if (numFxKnobs >= 40) knobsPerRow = 21;
                else if (numFxKnobs >= 28) knobsPerRow = 17;
                else if (numFxKnobs == 25) knobsPerRow = 13;
                int row2Count = numFxKnobs - knobsPerRow;
                int maxRowKnobs = std::max (knobsPerRow, row2Count);
                int gapW = 4;
                int knobW = std::min (55, std::max (32, (engineArea.getWidth() - gapW * 5) / maxRowKnobs));
                int rowH = (engineArea.getHeight() - 14) / 2;
                auto row1 = engineArea.removeFromTop (rowH);
                engineArea.removeFromTop (14);
                auto row2 = engineArea;

                // Build x positions with gaps at group boundaries
                std::vector<int> xPos (static_cast<size_t>(numFxKnobs), 0);
                int xAcc = 0;
                for (int ki = 0; ki < numFxKnobs; ++ki)
                {
                    if (ki > 0 && (ki == 3 || ki == 6 || ki == 14 || ki == 17 || ki == 21 || ki == 24 || ki == 27 || ki == 31))
                        xAcc += gapW;
                    xPos[static_cast<size_t>(ki)] = xAcc;
                    xAcc += knobW;
                }

                int ki = 0;
                for (int idx = 0; idx < static_cast<int>(engineKnobs.size()); ++idx)
                {
                    if (idx >= fxStart && idx < lfoStartIdx)
                    {
                        engineKnobs[idx]->setVisible (true);
                        int x = (ki < knobsPerRow)
                            ? row1.getX() + xPos[static_cast<size_t>(ki)]
                            : row2.getX() + xPos[static_cast<size_t>(ki)] - xPos[static_cast<size_t>(knobsPerRow)];
                        auto& row = (ki < knobsPerRow) ? row1 : row2;
                        engineKnobs[idx]->setBounds (x, row.getY(), knobW, row.getHeight());
                        ki++;
                    }
                    else
                    {
                        engineKnobs[idx]->setVisible (false);
                    }
                }
            }
            else if (currentTab == 2)
            {
                // LFO tab
                fmBtn.setVisible (false);
                smpLoadBtn.setVisible (false);
                smpZoomInBtn.setVisible (false);
                smpZoomOutBtn.setVisible (false);
                smpPrevBtn.setVisible (false);
                smpNextBtn.setVisible (false);
                mixKnob.setVisible (false);

                engineArea.removeFromTop (14); // space for LFO labels
                int numLfoKnobs = static_cast<int>(engineKnobs.size()) - lfoStartIdx;
                int knobsPerRow = 14; // 8 main + 6 extra routes per LFO
                int gapW = 4;
                int knobW = std::min (50, std::max (32, (engineArea.getWidth() - gapW) / knobsPerRow));
                // 3 rows — one LFO per row
                int rowH = (engineArea.getHeight() - 8) / 3;
                auto row1 = engineArea.removeFromTop (rowH);
                engineArea.removeFromTop (4);
                auto row2 = engineArea.removeFromTop (rowH);
                engineArea.removeFromTop (4);
                auto row3 = engineArea;

                int ki = 0;
                for (int idx = 0; idx < static_cast<int>(engineKnobs.size()); ++idx)
                {
                    if (idx >= lfoStartIdx)
                    {
                        engineKnobs[idx]->setVisible (true);
                        int rowIdx = ki / knobsPerRow;
                        int colIdx = ki % knobsPerRow;
                        auto& targetRow = (rowIdx == 0) ? row1 : (rowIdx == 1) ? row2 : row3;
                        int xPos = colIdx * knobW;
                        if (colIdx >= 8) xPos += gapW; // gap between main and extra routes
                        engineKnobs[idx]->setBounds (targetRow.getX() + xPos, targetRow.getY(), knobW, targetRow.getHeight());
                        ki++;
                    }
                    else
                    {
                        engineKnobs[idx]->setVisible (false);
                    }
                }
            }
            else if (currentTab == 3)
            {
                // MSEG tab
                fmBtn.setVisible (false);
                smpLoadBtn.setVisible (false);
                smpZoomInBtn.setVisible (false);
                smpZoomOutBtn.setVisible (false);
                smpPrevBtn.setVisible (false);
                smpNextBtn.setVisible (false);
                mixKnob.setVisible (false);
                for (auto* k : engineKnobs) k->setVisible (false);

                if (msegEditor)
                {
                    msegEditor->setVisible (true);
                    auto msegArea = engineArea;
                    auto ctrlRow = msegArea.removeFromBottom (38);
                    msegEditor->setBounds (msegArea);

                    for (auto* k : msegKnobs) removeChildComponent (k);
                    msegKnobs.clear();

                    // MSEG 1/2/3 selector buttons
                    int cx = ctrlRow.getX();
                    for (int mi = 0; mi < 3; ++mi)
                    {
                        auto& btn = msegSelBtns[mi];
                        btn.setButtonText (juce::String (mi + 1));
                        btn.setColour (juce::TextButton::buttonColourId,
                            mi == msegIdx ? juce::Colour (0xff40d8e8) : juce::Colour (0xff252830));
                        btn.setColour (juce::TextButton::textColourOffId,
                            mi == msegIdx ? juce::Colours::black : juce::Colour (0xaaffffff));
                        btn.setBounds (cx, ctrlRow.getY(), 18, ctrlRow.getHeight());
                        btn.onClick = [this, mi]() {
                            msegIdx = mi;
                            if (msegEditor)
                                msegEditor->setData (track.msegs[static_cast<size_t>(msegIdx)]);
                            resized();
                            repaint();
                        };
                        addAndMakeVisible (btn);
                        cx += 19;
                    }
                    cx += 2; // gap after selector

                    // Global MSEG retrigger toggle
                    if (!msegRetrigBtn)
                    {
                        msegRetrigBtn = std::make_unique<juce::TextButton> ("RTRIG");
                        msegRetrigBtn->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                        addChildComponent (*msegRetrigBtn);
                        msegRetrigBtn->onClick = [this]() {
                            track.msegRetrig = !track.msegRetrig;
                            msegRetrigBtn->setButtonText (track.msegRetrig ? "RTRIG" : "rtrig");
                            msegRetrigBtn->setColour (juce::TextButton::buttonColourId,
                                track.msegRetrig ? juce::Colour (0xff40d8e8) : juce::Colour (0xff8050c0));
                        };
                    }
                    msegRetrigBtn->setVisible (true);
                    msegRetrigBtn->setButtonText (track.msegRetrig ? "RTRIG" : "rtrig");
                    msegRetrigBtn->setColour (juce::TextButton::buttonColourId,
                        track.msegRetrig ? juce::Colour (0xff40d8e8) : juce::Colour (0xff8050c0));
                    msegRetrigBtn->setBounds (cx, ctrlRow.getY(), 36, ctrlRow.getHeight());
                    cx += 38;

                    auto addMK = [&](const juce::String& name, float minV, float maxV, float initV,
                                     std::function<void(float)> cb, std::function<juce::String(float)> fmt)
                    {
                        auto* k = new KnobComponent (name, minV, maxV, initV, fmt);
                        k->onChange = cb; k->setAccentColour (juce::Colour (0xff40d8e8));
                        addAndMakeVisible (k); msegKnobs.add (k);
                    };
                    // Target as popup button — uses full LFO drum target list
                    if (!msegTargetBtn)
                    {
                        msegTargetBtn = std::make_unique<juce::TextButton> ("TGT");
                        msegTargetBtn->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252830));
                        addChildComponent (*msegTargetBtn);
                        msegTargetBtn->onClick = [this]() {
                            int nt = LFOEngine::kNumDrumTargets;
                            juce::PopupMenu m;
                            auto addSection = [&](const char* title, int from, int to) {
                                m.addSeparator();
                                m.addSectionHeader (title);
                                for (int i = from; i <= to && i < nt; ++i)
                                    m.addItem (i + 1, LFOEngine::getDrumTargetName (i), true, track.msegs[static_cast<size_t>(msegIdx)].target == i);
                            };
                            addSection ("Drum",     0, 10);
                            addSection ("FM",       11, 15);
                            addSection ("FX Mod",   16, 22);
                            addSection ("Sampler",  23, 26);
                            addSection ("Smp Ext",  49, 54);
                            addSection ("Filter Env", 27, 29);
                            addSection ("LFO X-Mod",30, 35);
                            addSection ("MSEG",     36, 37);
                            m.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (*msegTargetBtn)
                                .withParentComponent (getTopLevelComponent()),
                                [this](int r) {
                                    if (r > 0) {
                                        track.msegs[static_cast<size_t>(msegIdx)].target = r - 1;
                                        msegTargetBtn->setButtonText (juce::String ("TGT:") + LFOEngine::getDrumTargetName (r - 1));
                                    }
                                });
                        };
                    }
                    msegTargetBtn->setVisible (true);
                    msegTargetBtn->setButtonText (juce::String ("TGT:") + LFOEngine::getDrumTargetName (std::clamp (track.msegs[static_cast<size_t>(msegIdx)].target, 0, LFOEngine::kNumDrumTargets - 1)));
                    msegTargetBtn->setBounds (cx, ctrlRow.getY(), 52, ctrlRow.getHeight());
                    cx += 54;

                    addMK ("DPTH", -1, 1, track.msegs[static_cast<size_t>(msegIdx)].depth, [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].depth = v; },
                        [](float v){ return juce::String(static_cast<int>(v*100))+"%"; });
                    addMK ("TIME", 0.01f, 30.0f, track.msegs[static_cast<size_t>(msegIdx)].totalTime, [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].totalTime = v; },
                        [](float v){ return v<1?juce::String(static_cast<int>(v*1000))+"ms":juce::String(v,1)+"s"; });
                    addMK ("SYNC", 0, 11, track.msegs[static_cast<size_t>(msegIdx)].tempoSync ? static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].syncDiv + 1) : 0.0f,
                        [this](float v){ int vi=static_cast<int>(v); track.msegs[static_cast<size_t>(msegIdx)].tempoSync=(vi>0); if(vi>0) track.msegs[static_cast<size_t>(msegIdx)].syncDiv=vi-1; },
                        [](float v){ int vi=static_cast<int>(v); if(vi==0)return juce::String("FREE"); static const char* sn[]={"1/32","1/16","1/8","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"}; return juce::String(sn[std::clamp(vi-1,0,10)]); });
                    addMK ("LOOP", 0, 3, static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].loopMode), [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].loopMode=static_cast<int>(v); },
                        [](float v){ const char*m[]={"ONE","LOOP","P.P","RND"};return juce::String(m[static_cast<int>(v)%4]); });
                    addMK ("TRNS", 0, 1, track.msegs[static_cast<size_t>(msegIdx)].transportSync ? 1.0f : 0.0f,
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].transportSync = (v > 0.5f); },
                        [](float v){ return v > 0.5f ? juce::String("ON") : juce::String("OFF"); });
                    addMK ("GX", 1, 32, static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].gridX), [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].gridX=static_cast<int>(v); if(msegEditor)msegEditor->repaint(); },
                        [](float v){ return juce::String(static_cast<int>(v)); });
                    addMK ("GY", 1, 16, static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].gridY), [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].gridY=static_cast<int>(v); if(msegEditor)msegEditor->repaint(); },
                        [](float v){ return juce::String(static_cast<int>(v)); });

                    // ── Aux LFO for per-point modulation ──
                    addMK ("A.RT", 0.05f, 20.0f, track.msegs[static_cast<size_t>(msegIdx)].auxRate,
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].auxRate = v; },
                        [](float v){ return v<1?juce::String(static_cast<int>(v*1000))+"ms":juce::String(v,1)+"Hz"; });
                    addMK ("A.SH", 0, 4, static_cast<float>(track.msegs[static_cast<size_t>(msegIdx)].auxShape),
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].auxShape = static_cast<int>(v); },
                        [](float v){ const char* n[]={"SIN","TRI","SAW","SQR","S&H"}; return juce::String(n[std::clamp(static_cast<int>(v),0,4)]); });

                    // ── FADE-IN ──
                    addMK ("FDI", 0, 10, track.msegs[static_cast<size_t>(msegIdx)].fadeIn,
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].fadeIn = v; },
                        [this](float v){
                            if (v < 0.01f) return juce::String ("OFF");
                            auto& m = track.msegs[static_cast<size_t>(msegIdx)];
                            if (m.fadeInSync) {
                                static const char* sn[]={"1/32","1/16","1/8","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"};
                                return juce::String (sn[std::clamp (static_cast<int>(v), 0, 10)]);
                            }
                            if (v < 1.0f) return juce::String (static_cast<int>(v * 1000)) + "ms";
                            return juce::String (v, 1) + "s";
                        });
                    addMK ("FDS", 0, 1, track.msegs[static_cast<size_t>(msegIdx)].fadeInSync ? 1.0f : 0.0f,
                        [this](float v){ track.msegs[static_cast<size_t>(msegIdx)].fadeInSync = (v > 0.5f); },
                        [](float v){ return v > 0.5f ? juce::String ("SYN") : juce::String ("FRE"); });

                    // ── Extra modulation routes for MSEG ──
                    for (int ri = 0; ri < 3; ++ri)
                    {
                        auto& route = track.msegs[static_cast<size_t>(msegIdx)].extraRoutes[static_cast<size_t>(ri)];
                        addMK ("D" + juce::String(ri+2), -1, static_cast<float>(LFOEngine::kNumDrumTargets-1),
                            static_cast<float>(route.target),
                            [this, ri](float v){ track.msegs[static_cast<size_t>(msegIdx)].extraRoutes[static_cast<size_t>(ri)].target=static_cast<int>(v); },
                            [](float v){ int t=static_cast<int>(v); return t<0?juce::String("OFF"):juce::String(LFOEngine::getDrumTargetName(t)); });
                        {
                            using PC = KnobComponent::PopupCategory;
                            std::vector<PC> cats;
                            auto mkCat = [](const juce::String& name, std::initializer_list<int> indices) {
                                PC c; c.name = name;
                                for (int i : indices) c.items.push_back ({i, LFOEngine::getDrumTargetName (i)});
                                return c;
                            };
                            { PC c; c.name = "---"; c.items.push_back ({-1, "OFF"}); cats.push_back (c); }
                            cats.push_back (mkCat ("PITCH/AMP", {0, 1, 3, 7, 10}));
                            cats.push_back (mkCat ("FILTER",    {2, 8, 9, 27, 28, 29}));
                            cats.push_back (mkCat ("SEND",      {4, 5, 6}));
                            cats.push_back (mkCat ("FM",        {11, 12, 13, 14, 15}));
                            cats.push_back (mkCat ("FX",        {16, 17, 18, 19, 20, 21, 22, 55, 57, 58, 59}));
                            cats.push_back (mkCat ("EQ/OUT",    {60, 61, 62, 63, 64}));
                            cats.push_back (mkCat ("DUCK",      {65, 66, 67}));
                            cats.push_back (mkCat ("OTT",       {79,80,81}));
                            cats.push_back (mkCat ("SAT",       {82,83,84,85}));
                            cats.push_back (mkCat ("PHASER",    {86,87,88,89}));
                            cats.push_back (mkCat ("FLANGER",   {90,91,92,93}));
                            cats.push_back (mkCat ("SAMPLER",   {23, 24, 25, 26, 49, 50, 51, 52, 53, 54}));
                            cats.push_back (mkCat ("X-MOD",     {30, 31, 32, 33, 34, 35}));
                            msegKnobs.getLast()->setCategorizedPopup (cats);
                        }
                        addMK ("A" + juce::String(ri+2), -1, 1, route.depth,
                            [this, ri](float v){ track.msegs[static_cast<size_t>(msegIdx)].extraRoutes[static_cast<size_t>(ri)].depth=v; },
                            [](float v){ int p=static_cast<int>(v*100); return p==0?juce::String("OFF"):(p>0?"+":juce::String(""))+juce::String(p)+"%"; });
                    }
                    for (auto* k : msegKnobs) { k->setBounds (cx, ctrlRow.getY(), 46, ctrlRow.getHeight()); cx += 48; }
                }
            }

            // Hide MSEG editor when not on MSEG tab
            if (currentTab != 3 && msegEditor)
            {
                msegEditor->setVisible (false);
                if (msegTargetBtn) msegTargetBtn->setVisible (false); if (msegRetrigBtn) msegRetrigBtn->setVisible (false); for (auto& b : msegSelBtns) b.setVisible (false);
                for (auto* k : msegKnobs) k->setVisible (false);
            }
        }
        else
        {
            fmBtn.setVisible (false);
                smpLoadBtn.setVisible (false);
                smpZoomInBtn.setVisible (false);
                smpZoomOutBtn.setVisible (false);
                smpPrevBtn.setVisible (false);
                smpNextBtn.setVisible (false);
            mixKnob.setVisible (false);
            tabEng.setVisible (false);
            tabFx.setVisible (false);
            tabLfo.setVisible (false);
            tabMseg.setVisible (false);
            pstBtn.setVisible (false);
            if (msegEditor) msegEditor->setVisible (false);
            if (msegTargetBtn) msegTargetBtn->setVisible (false); if (msegRetrigBtn) msegRetrigBtn->setVisible (false); for (auto& b : msegSelBtns) b.setVisible (false);
            for (auto* k : msegKnobs) k->setVisible (false);
            for (auto* k : engineKnobs)
                k->setVisible (false);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Colours_GB::panel2);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (Colours_GB::border);
        g.drawRoundedRectangle (bounds, 4.0f, 0.5f);

        // Left accent bar
        g.setColour (track.muted ? Colours_GB::red.withAlpha (0.6f) : Colours_GB::accentMid);
        g.fillRoundedRectangle (bounds.getX(), bounds.getY() + 2, 3.0f, bounds.getHeight() - 4, 1.5f);

        // ── Drum type icon + engine badge ──
        {
            auto labelBounds = nameLabel.getBounds().toFloat();
            // Icon to the left of the name text
            auto iconArea = juce::Rectangle<float>(labelBounds.getX() - 18, labelBounds.getY(),
                                                    16.0f, labelBounds.getHeight());
            int drumTypeIdx = 0;
            switch (track.type) {
                case DrumType::Kick: drumTypeIdx = 0; break;
                case DrumType::Snare: drumTypeIdx = 1; break;
                case DrumType::HiHatClosed: drumTypeIdx = 2; break;
                case DrumType::HiHatOpen: drumTypeIdx = 3; break;
                case DrumType::Clap: drumTypeIdx = 4; break;
                case DrumType::Tom: drumTypeIdx = 5; break;
                case DrumType::TomHi: drumTypeIdx = 6; break;
                case DrumType::Cowbell: drumTypeIdx = 7; break;
                case DrumType::Rimshot: drumTypeIdx = 8; break;
                case DrumType::Crash: drumTypeIdx = 9; break;
            }
            EngineIcons::drawDrumIcon (g, iconArea, drumTypeIdx, track.drumEngine);

            // Engine badge below name
            auto badgeArea = juce::Rectangle<float>(labelBounds.getX() - 18, labelBounds.getBottom() + 1,
                                                     labelBounds.getWidth() + 18, 10.0f);
            EngineIcons::drawEngineBadge (g, badgeArea, track.drumEngine);
        }

        if (engineOpen)
        {
            g.setColour (Colours_GB::border.withAlpha (0.4f));
            g.drawLine (8, 52, bounds.getWidth() - 8, 52, 0.5f);

            // ── Active tab underline ──
            {
                juce::TextButton* tabs[] = { &tabEng, &tabFx, &tabLfo, &tabMseg };
                auto& activeTab = *tabs[std::clamp (currentTab, 0, 3)];
                auto tb = activeTab.getBounds();
                g.setColour (Colours_GB::accent);
                g.fillRoundedRectangle (static_cast<float>(tb.getX() + 2), static_cast<float>(tb.getBottom() - 2),
                                        static_cast<float>(tb.getWidth() - 4), 2.0f, 1.0f);
            }

            float panelY = static_cast<float>(enginePanelY);
            float panelH = bounds.getBottom() - panelY - 4;
            float panelX = 34.0f;
            float panelW = bounds.getWidth() - 40.0f;

            // Panel background — modern dark
            juce::Colour tabTints[] = {
                juce::Colour (0x0800a0c0),  // ENG — warm
                juce::Colour (0x0820c060),  // FX — green
                juce::Colour (0x084060b0)   // LFO — purple
            };
            auto tint = tabTints[currentTab % 3];
            juce::ColourGradient panelGrad (
                juce::Colour (0xff0e1118).interpolatedWith (tint, 0.15f), panelX, panelY,
                juce::Colour (0xff0a0c14).interpolatedWith (tint, 0.08f), panelX, panelY + panelH, false);
            g.setGradientFill (panelGrad);
            g.fillRoundedRectangle (panelX, panelY, panelW, panelH, 4.0f);

            // Panel border — subtle
            g.setColour (juce::Colour (0x0cffffff));
            g.drawRoundedRectangle (panelX, panelY, panelW, panelH, 4.0f, 0.4f);

            // ANA/FM/SMP button color
            if (currentTab == 0)
            {
                juce::Colour btnColor;
                if (track.drumEngine == 2) btnColor = juce::Colour (0xff40b0b0);       // teal
                else if (track.drumEngine == 1) btnColor = juce::Colour (0xff3060cc);   // blue
                else btnColor = Colours_GB::accent;                                        // amber
                fmBtn.setColour (juce::TextButton::buttonColourId, btnColor.withAlpha (0.8f));
                fmBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff001020));

                // Waveform display for sampler (42% of panel, same as synth)
                if (track.drumEngine == 2 && track.sampleData != nullptr)
                {
                    int wfX = static_cast<int>(panelX) + 2;
                    int wfW = static_cast<int>(panelW) - 4;
                    int wfY = static_cast<int>(panelY) + 2;
                    int wfH = static_cast<int>(panelH * 0.42f);
                    waveformBounds = { wfX, wfY, wfW, wfH };
                    g.setColour (juce::Colour (0x20ffffff));
                    g.fillRect (wfX, wfY, wfW, wfH);
                    const float* wfData = track.sampleData->getReadPointer (0);
                    int wfLen = track.sampleData->getNumSamples();
                    // Zoom window
                    float vis = 1.0f / wfZoom;
                    float viewStart = wfOffset;
                    float viewEnd = viewStart + vis;
                    int vStart = static_cast<int>(viewStart * wfLen);
                    int vEnd = static_cast<int>(viewEnd * wfLen);
                    juce::Path wfPath;
                    bool hasWarp = (track.smpWarp > 0 && track.warpMarkers.size() >= 2 && track.smpBars > 0);
                    for (int px = 0; px < wfW; ++px)
                    {
                        int si;
                        if (hasWarp)
                        {
                            // WARPED display: x-axis = output time (beats), waveform warped through markers
                            // Each pixel maps to a beat position, then through markers to source sample
                            static const float barLUT[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                            float totalBeats = barLUT[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                            // Pixel → beat position (linear in output time)
                            float beatFrac = static_cast<float>(px) / std::max (1.0f, static_cast<float>(wfW));
                            // Apply view zoom: only show the visible portion
                            float normPos = viewStart + (viewEnd - viewStart) * beatFrac;
                            float beatPos = normPos * totalBeats;

                            // Find segment in warp markers and interpolate source position
                            float srcNorm = normPos; // fallback
                            auto& wm = track.warpMarkers;
                            for (size_t m = 0; m + 1 < wm.size(); ++m)
                            {
                                float bpA = wm[m].beatPos;
                                float bpB = wm[m + 1].beatPos;
                                if (beatPos >= bpA && (beatPos <= bpB || m + 2 == wm.size()))
                                {
                                    float bLen = bpB - bpA;
                                    float sLen = wm[m + 1].samplePos - wm[m].samplePos;
                                    float frac = (bLen > 0.001f) ? std::clamp ((beatPos - bpA) / bLen, 0.0f, 1.0f) : 0.0f;
                                    srcNorm = wm[m].samplePos + frac * sLen;
                                    break;
                                }
                            }
                            si = static_cast<int>(std::clamp (srcNorm, 0.0f, 1.0f) * static_cast<float>(wfLen));
                        }
                        else
                        {
                            si = vStart + (vEnd - vStart) * px / std::max (1, wfW);
                        }
                        si = std::clamp (si, 0, wfLen - 1);
                        float s = wfData[si];
                        float py = wfY + wfH * 0.5f - s * wfH * 0.45f;
                        if (px == 0) wfPath.startNewSubPath (static_cast<float>(wfX + px), py);
                        else wfPath.lineTo (static_cast<float>(wfX + px), py);
                    }
                    // Waveform color: teal when normal, orange when warped (visual feedback)
                    g.setColour (hasWarp ? juce::Colour (0xffff8040) : juce::Colour (0xff40b0b0));
                    g.strokePath (wfPath, juce::PathStrokeType (0.8f));
                    // WARP indicator text (confirms warped display is active)
                    if (hasWarp)
                    {
                        g.setColour (juce::Colour (0x80ff8040));
                        g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 8.0f, juce::Font::bold));
                        g.drawText ("WARP", wfX + 2, wfY + 1, 30, 10, juce::Justification::left, false);
                    }
                    // Start/End markers — use inverse warp mapping when active
                    float warpTotalBeats = 0.0f;
                    if (hasWarp) {
                        static const float barLUT2[] = {0,0.25f,0.5f,1,2,4,8,16,32};
                        warpTotalBeats = barLUT2[std::clamp (track.smpBars, 1, 8)] * 4.0f;
                    }
                    auto srcToOutput = [&](float srcNorm) -> float {
                        if (!hasWarp || warpTotalBeats < 0.001f) return srcNorm;
                        auto& wm = track.warpMarkers;
                        for (size_t m = 0; m + 1 < wm.size(); ++m) {
                            float spA = wm[m].samplePos, spB = wm[m + 1].samplePos;
                            if (srcNorm >= spA && (srcNorm <= spB || m + 2 == wm.size())) {
                                float sLen = spB - spA;
                                float frac = (sLen > 0.001f) ? std::clamp ((srcNorm - spA) / sLen, 0.0f, 1.0f) : 0.0f;
                                float beatPos = wm[m].beatPos + frac * (wm[m + 1].beatPos - wm[m].beatPos);
                                return beatPos / warpTotalBeats;
                            }
                        }
                        return srcNorm;
                    };
                    auto drawMarker = [&](float normPos, juce::Colour c) {
                        float mapped = srcToOutput (normPos);
                        float mp = (mapped - viewStart) / vis;
                        if (mp >= 0.0f && mp <= 1.0f) {
                            float mx = wfX + mp * wfW;
                            g.setColour (c);
                            g.drawVerticalLine (static_cast<int>(mx), static_cast<float>(wfY), static_cast<float>(wfY + wfH));
                        }
                    };
                    drawMarker (track.smpStart, juce::Colour (0xff40ff40));
                    drawMarker (track.smpEnd, juce::Colour (0xffff4040));

                    // ── Grid overlay (bars/beats) + warp markers ──
                    if (track.smpWarp > 0 && track.smpBars > 0)
                    {
                        WaveformOverlay::drawGridOverlay (g,
                            static_cast<float>(wfX), static_cast<float>(wfY),
                            static_cast<float>(wfW), static_cast<float>(wfH),
                            viewStart, viewStart + vis, track.smpBPM, track.smpBars, track.smpWarp, track.gridDiv);
                        WaveformOverlay::drawWarpMarkers (g,
                            static_cast<float>(wfX), static_cast<float>(wfY),
                            static_cast<float>(wfW), static_cast<float>(wfH),
                            viewStart, viewStart + vis, track.warpMarkers,
                            warpHoveredMarker, warpDraggedMarker, warpTotalBeats);
                    }

                    // ── Sampler playhead ──
                    if (track.smpPlayPos >= 0.0f)
                    {
                        float mappedPos = srcToOutput (track.smpPlayPos);
                        float posNorm = (mappedPos - viewStart) / vis;
                        if (posNorm >= 0.0f && posNorm <= 1.0f)
                        {
                            float px = static_cast<float>(wfX) + posNorm * static_cast<float>(wfW);
                            g.setColour (juce::Colour (0xddffffff));
                            g.drawLine (px, static_cast<float>(wfY + 1), px, static_cast<float>(wfY + wfH - 1), 1.5f);
                            juce::Path tri;
                            tri.addTriangle (px - 3, static_cast<float>(wfY), px + 3, static_cast<float>(wfY), px, static_cast<float>(wfY + 4));
                            g.fillPath (tri);
                        }
                    }
                }

                // ── Sampler group labels (identical to synth) ──
                if (!smpGroupStarts.empty())
                {
                    g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 7.0f, juce::Font::bold));
                    int smpStart = numAnalogKnobs + numFmKnobs;
                    int smpEnd = smpStart + numSmpKnobs;

                    // Build group sizes
                    std::vector<int> smpSizes;
                    for (int gi = 0; gi < static_cast<int>(smpGroupStarts.size()); ++gi)
                    {
                        if (smpGroupStarts[static_cast<size_t>(gi)] >= smpEnd) break;
                        int nextBound = (gi + 1 < static_cast<int>(smpGroupStarts.size())
                                         && smpGroupStarts[static_cast<size_t>(gi + 1)] < smpEnd)
                                        ? smpGroupStarts[static_cast<size_t>(gi + 1)]
                                        : smpEnd;
                        smpSizes.push_back (nextBound - smpGroupStarts[static_cast<size_t>(gi)]);
                    }

                    int ki = 0; int prevRowY = -999;
                    for (int fg = 0; fg < static_cast<int>(smpSizes.size()); ++fg)
                    {
                        int absIdx = smpStart + ki;
                        if (absIdx >= static_cast<int>(engineKnobs.size()) || !engineKnobs[absIdx]->isVisible())
                        { ki += smpSizes[static_cast<size_t>(fg)]; continue; }

                        int gx = engineKnobs[absIdx]->getX();
                        int ky = engineKnobs[absIdx]->getY();
                        int kh = engineKnobs[absIdx]->getHeight();
                        int ly = ky - 10;
                        bool sameRow = (std::abs (ky - prevRowY) < 20);

                        if (ki > 0 && sameRow)
                        {
                            g.setColour (juce::Colour (0x15ffffff));
                            g.drawVerticalLine (gx - 2, static_cast<float>(ly), static_cast<float>(ky + kh));
                        }

                        int lastK = std::min (absIdx + smpSizes[static_cast<size_t>(fg)] - 1,
                                              std::min (smpEnd - 1, static_cast<int>(engineKnobs.size()) - 1));
                        while (lastK > absIdx && std::abs (engineKnobs[lastK]->getY() - ky) > 20) --lastK;
                        int gw = std::max (30, engineKnobs[lastK]->getRight() - gx);

                        g.setColour (juce::Colour (0x18ffffff));
                        g.fillRoundedRectangle (static_cast<float>(gx), static_cast<float>(ly),
                                                static_cast<float>(gw), 9.0f, 3.0f);
                        g.setColour (juce::Colour (0x90ffffff));
                        if (fg < static_cast<int>(smpGroupNames.size()))
                            g.drawText (smpGroupNames[static_cast<size_t>(fg)], gx, ly, gw, 9,
                                        juce::Justification::centred, false);

                        prevRowY = ky;
                        ki += smpSizes[static_cast<size_t>(fg)];
                    }
                }
            }

            // ── FX tab group labels — per-row, no cross-row lines ──
            if (currentTab == 1)
            {
                g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 7.0f, juce::Font::bold));
                int fxStart = numAnalogKnobs + numFmKnobs + numSmpKnobs + numER1Knobs;
                static const char* fxLabels[] = {"DRIVE","CHORUS","DELAY","OTT","PHASER","REVERB","EQ","OUT","DUCK","SAT","FLANGER"};
                static const int fxGroupSizes[] = {3, 3, 8, 3, 4, 4, 3, 3, 4, 5, 4};
                static const juce::Colour fxLabelCols[] = {
                    juce::Colour(0xffc06040), juce::Colour(0xff50a0d0), juce::Colour(0xff60b080),
                    juce::Colour(0xfff0c040), juce::Colour(0xffd050e0), juce::Colour(0xff6070d8),
                    juce::Colour(0xff50d0b0), juce::Colour(0xff909098), juce::Colour(0xffe08020),
                    juce::Colour(0xffe04070), juce::Colour(0xffa0d040)
                };
                int ki = 0; int prevRowY = -999;
                for (int fg = 0; fg < 11; ++fg)
                {
                    int absIdx = fxStart + ki;
                    if (absIdx < static_cast<int>(engineKnobs.size()) && engineKnobs[absIdx]->isVisible())
                    {
                        int gx = engineKnobs[absIdx]->getX();
                        int ky = engineKnobs[absIdx]->getY();
                        int kh = engineKnobs[absIdx]->getHeight();
                        int ly = ky - 10;
                        bool sameRow = (std::abs (ky - prevRowY) < 20);
                        // Find last knob on same row
                        int lastK = std::min (absIdx + fxGroupSizes[fg] - 1, static_cast<int>(engineKnobs.size()) - 1);
                        while (lastK > absIdx && std::abs (engineKnobs[lastK]->getY() - ky) > 20) --lastK;
                        int gw = std::max (30, engineKnobs[lastK]->getRight() - gx);

                        if (ki > 0 && sameRow)
                        {
                            g.setColour (juce::Colour (0x15ffffff));
                            g.drawVerticalLine (gx - 2, static_cast<float>(ly), static_cast<float>(ky + kh));
                        }
                        g.setColour (fxLabelCols[fg].withAlpha (0.12f));
                        g.fillRoundedRectangle (static_cast<float>(gx), static_cast<float>(ly),
                                                static_cast<float>(gw), 9.0f, 3.0f);
                        g.setColour (fxLabelCols[fg].withAlpha (0.85f));
                        g.drawText (fxLabels[fg], gx, ly, gw, 9, juce::Justification::centred, false);
                        prevRowY = ky;
                    }
                    ki += fxGroupSizes[fg];
                }
            }

            // ── LFO tab labels — per-row ──
            if (currentTab == 2)
            {
                g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 7.5f, juce::Font::bold));
                static const juce::Colour lfoCols[] = {
                    juce::Colour (0xff50c0b0), juce::Colour (0xffc07090), juce::Colour (0xffc0a030)
                };
                for (int li = 0; li < 3; ++li)
                {
                    int bi = lfoStartIdx + li * 14;
                    if (bi >= static_cast<int>(engineKnobs.size()) || !engineKnobs[bi]->isVisible()) continue;
                    int lx = engineKnobs[bi]->getX();
                    int ky = engineKnobs[bi]->getY();
                    int ly = ky - 10;
                    int lastIdx = std::min (bi + 13, static_cast<int>(engineKnobs.size()) - 1);
                    int lw = engineKnobs[lastIdx]->getRight() - lx;

                    g.setColour (lfoCols[li].withAlpha (0.12f));
                    g.fillRoundedRectangle (static_cast<float>(lx), static_cast<float>(ly),
                                            static_cast<float>(lw), 9.0f, 3.0f);
                    g.setColour (lfoCols[li].withAlpha (0.85f));
                    g.drawText ("LFO " + juce::String (li + 1), lx, ly, lw, 9,
                                juce::Justification::centred, false);
                }
            }
        }
    }

private:
    int index;
    DrumTrackState& track;
    bool engineOpen = false;
    bool fmScreen = false;
    int currentTab = 0; // 0=ENG, 1=FX, 2=LFO, 3=MSEG
    int msegIdx = 0;   // 0-2: which MSEG is shown/edited
    bool manualPageOverride = false;
    juce::String lastSavedPresetName;
    juce::String lastSavedPresetFolder;
    int  lastAutoPage = -1;
    bool plockMode = false;
    int  plockTargetStep = -1;

    juce::Label nameLabel;
    juce::Label lenCaption, swingCaption, volCaption, spdCaption;
    StepGrid stepGrid;
    RatchetRow ratchetRow;
    juce::OwnedArray<juce::TextButton> pageButtons;
    juce::TextButton muteBtn, soloBtn, expandBtn, plkBtn, fmBtn, randBtn, initBtn, copyBtn, pasteBtn, linkBtn;
    juce::TextButton smpLoadBtn, smpZoomInBtn, smpZoomOutBtn, smpPrevBtn, smpNextBtn;
    float wfZoom = 1.0f, wfOffset = 0.0f;
    juce::Rectangle<int> waveformBounds;
    int wfDragMode = 0; // 0=none, 1=start, 2=end, 4=warp marker drag
    int warpHoveredMarker = -1;
    int warpDraggedMarker = -1;
    juce::Array<juce::File> sampleFolder;
    int sampleFolderIdx = -1;
    juce::TextButton tabEng {"ENG"}, tabFx {"FX"}, tabLfo {"LFO"}, tabMseg {"MSEG"}, pstBtn {"PST"};
    std::unique_ptr<MSEGEditor> msegEditor;
    std::unique_ptr<juce::TextButton> msegTargetBtn;
    std::unique_ptr<juce::TextButton> msegRetrigBtn;
    juce::OwnedArray<KnobComponent> msegKnobs;
    juce::TextButton msegSelBtns[3];
    KnobComponent mixKnob { "MIX", 0.0f, 1.0f, 0.0f,
        [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; } };
    juce::Slider volSlider, swingSlider;
    juce::Label lenLabel;
    juce::ComboBox spdSelector;
    juce::OwnedArray<KnobComponent> engineKnobs;
    int numAnalogKnobs = 0;
    int numFmKnobs = 0;
    int numSmpKnobs = 0;
    int numER1Knobs = 0;
    int lfoStartIdx = 0;
    int fxStartIdx = 0;
    std::vector<int> smpGroupStarts;
    std::vector<juce::String> smpGroupNames;
    int enginePanelY = 0; // for paint
    LEDMeter ledMeter { 10, false };

    static inline StepSequence s_clipboard;
    static inline bool s_clipboardValid = false;

    void showInitMenu()
    {
        if (onBeforeEdit) onBeforeEdit();
        juce::PopupMenu menu;
        menu.addItem (9, "RESET ENGINE (sound params)");
        menu.addItem (10, "INIT MODULATIONS (LFO + MSEG + MACRO)");
        menu.addItem (11, "RESET MACROS (this track)");
        menu.addSeparator();
        menu.addItem (2, "RESET STEPS ONLY");
        menu.addItem (3, "RESET VELOCITIES (to 100)");
        menu.addItem (4, "RESET GATES (to 100%)");
        menu.addItem (5, "RESET RATCHETS (to 1x)");
        menu.addItem (6, "RESET P-LOCKS");
        menu.addItem (7, "RESET TRIG CONDITIONS");
        menu.addItem (8, "RESET NUDGE");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&initBtn),
            [this](int result) {
                if (result == 0) return;
                if (result == 9) { track.resetEngine(); rebuildKnobs(); }
                else if (result == 10) {
                    track.resetModulations();
                    if (macroEnginePtr != nullptr)
                        for (auto& mk : macroEnginePtr->macros)
                            mk.assignments.erase (std::remove_if (mk.assignments.begin(), mk.assignments.end(),
                                [this](const MacroAssignment& a){ return a.trackType == 1 && a.trackIndex == index; }), mk.assignments.end());
                    rebuildKnobs();
                }
                else if (result == 11) {
                    if (macroEnginePtr != nullptr)
                        for (auto& mk : macroEnginePtr->macros)
                            mk.assignments.erase (std::remove_if (mk.assignments.begin(), mk.assignments.end(),
                                [this](const MacroAssignment& a){ return a.trackType == 1 && a.trackIndex == index; }), mk.assignments.end());
                }
                else {
                    if (result == 1) { track.resetEngine(); track.resetModulations(); }
                    for (int si = 0; si < kMaxSteps; ++si)
                    {
                        auto& step = track.seq.steps[static_cast<size_t>(si)];
                        if (result == 1) step.reset();
                        else if (result == 2) step.reset();
                        else if (result == 3) step.velocity = 100;
                        else if (result == 4) step.gate = 100;
                        else if (result == 5) { step.ratchet = 1; step.triplet = false; }
                        else if (result == 6) step.plocks.clear();
                        else if (result == 7) step.cond = TrigCondition::Always;
                        else if (result == 8) step.nudge = 0;
                    }
                }
                rebuildKnobs();
                stepGrid.setStepSequence (&track.seq);
                ratchetRow.setStepSequence (&track.seq);
            });
    }

    void showRandomizeMenu()
    {
        if (onBeforeEdit) onBeforeEdit();
        struct RndPanel : public juce::Component
        {
            struct Row { juce::String name; float pct = 0.0f; juce::Rectangle<float> sliderRect; };
            std::vector<Row> rows = {
                {"TRIGGERS", 50}, {"VELOCITY", 0}, {"GATE", 0},
                {"RATCHET", 0}, {"ENGINE", 0}, {"FX SENDS", 0}
            };
            std::function<void(float,float,float,float,float,float)> onApply;
            std::function<void()> onClear;
            int dragRow = -1;

            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colour (0xff14161c));
                g.setColour (juce::Colour (0xff404555));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6, 1);
                auto f = juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold);
                g.setFont (f);
                float y = 8;
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    float x = 8, w = static_cast<float>(getWidth()) - 16;
                    g.setColour (juce::Colour (0xaaffffff));
                    g.drawText (rows[i].name, static_cast<int>(x), static_cast<int>(y), 70, 16, juce::Justification::left);
                    float sx = x + 74, sw = w - 74 - 38;
                    rows[i].sliderRect = { sx, y + 3, sw, 10 };
                    g.setColour (juce::Colour (0xff2a2d38));
                    g.fillRoundedRectangle (rows[i].sliderRect, 4);
                    float fillW = sw * rows[i].pct / 100.0f;
                    g.setColour (Colours_GB::accent.withAlpha (0.8f));
                    g.fillRoundedRectangle (sx, y + 3, fillW, 10, 4);
                    g.setColour (juce::Colours::white);
                    g.drawText (juce::String (static_cast<int>(rows[i].pct)) + "%",
                                static_cast<int>(sx + sw + 4), static_cast<int>(y), 34, 16, juce::Justification::left);
                    y += 22;
                }
                float btnY = y + 6;
                g.setColour (juce::Colour (0xffe8a030));
                g.fillRoundedRectangle (8, btnY, 80, 20, 4);
                g.setColour (juce::Colours::black);
                g.drawText ("RANDOMIZE", 8, static_cast<int>(btnY), 80, 20, juce::Justification::centred);
                g.setColour (juce::Colour (0xff505868));
                g.fillRoundedRectangle (96, btnY, 50, 20, 4);
                g.setColour (juce::Colours::white);
                g.drawText ("CLEAR", 96, static_cast<int>(btnY), 50, 20, juce::Justification::centred);
            }
            void mouseDown (const juce::MouseEvent& e) override
            {
                float btnY = 8 + rows.size() * 22 + 6;
                if (e.y >= btnY && e.y < btnY + 20)
                {
                    if (e.x < 90 && onApply) onApply (rows[0].pct, rows[1].pct, rows[2].pct, rows[3].pct, rows[4].pct, rows[5].pct);
                    else if (e.x >= 90 && onClear) onClear();
                    return;
                }
                for (size_t i = 0; i < rows.size(); ++i)
                    if (rows[i].sliderRect.expanded (0, 4).contains (static_cast<float>(e.x), static_cast<float>(e.y)))
                        { dragRow = static_cast<int>(i); updateSlider (e.x); break; }
            }
            void mouseDrag (const juce::MouseEvent& e) override { updateSlider (e.x); }
            void mouseUp (const juce::MouseEvent&) override { dragRow = -1; }
            void updateSlider (int mx)
            {
                if (dragRow < 0 || dragRow >= static_cast<int>(rows.size())) return;
                auto& r = rows[static_cast<size_t>(dragRow)];
                r.pct = std::clamp ((static_cast<float>(mx) - r.sliderRect.getX()) / r.sliderRect.getWidth() * 100.0f, 0.0f, 100.0f);
                repaint();
            }
        };

        auto* panel = new RndPanel();
        panel->setSize (200, static_cast<int>(8 + 6 * 22 + 6 + 20 + 8));
        panel->onApply = [this](float trig, float vel, float gate, float ratch, float eng, float fx) {
            std::mt19937 rng { std::random_device{}() };
            std::uniform_real_distribution<float> prob (0.0f, 1.0f);
            for (int si = 0; si < track.length; ++si)
            {
                auto& step = track.seq.steps[static_cast<size_t>(si)];
                if (trig > 0.1f) step.active = prob (rng) < (trig / 100.0f);
                if (!step.active) continue;
                if (vel > 0.1f) { float d = vel/100*127; step.velocity = static_cast<uint8_t>(std::clamp (step.velocity + (prob(rng)*2-1)*d, 20.0f, 127.0f)); }
                if (gate > 0.1f) { float d = gate/100*190; step.gate = static_cast<uint8_t>(std::clamp (static_cast<int>(step.gate + (prob(rng)*2-1)*d), 10, 200)); }
                if (ratch > 0.1f) { int r = 1 + static_cast<int>(prob(rng) * ratch / 25.0f); step.ratchet = static_cast<uint8_t>(std::clamp(r, 1, 4)); }
            }
            // Engine knobs (0 to fxStartIdx)
            if (eng > 0.1f)
            {
                for (int ki = 0; ki < fxStartIdx && ki < static_cast<int>(engineKnobs.size()); ++ki)
                {
                    auto* k = engineKnobs[ki]; if (!k->isVisible()) continue;
                    float range = k->getMaxValue() - k->getMinValue();
                    float nv = k->getValue() + (prob(rng)*2-1) * eng/100 * range;
                    k->setValue (std::clamp (nv, k->getMinValue(), k->getMaxValue()));
                }
            }
            // FX knobs (fxStartIdx to lfoStartIdx)
            if (fx > 0.1f)
            {
                for (int ki = fxStartIdx; ki < lfoStartIdx && ki < static_cast<int>(engineKnobs.size()); ++ki)
                {
                    auto* k = engineKnobs[ki]; if (!k->isVisible()) continue;
                    float range = k->getMaxValue() - k->getMinValue();
                    float nv = k->getValue() + (prob(rng)*2-1) * fx/100 * range;
                    k->setValue (std::clamp (nv, k->getMinValue(), k->getMaxValue()));
                }
            }
            stepGrid.setStepSequence (&track.seq);
            ratchetRow.setStepSequence (&track.seq);
        };
        panel->onClear = [this]() {
            for (int si = 0; si < track.length; ++si) track.seq.steps[static_cast<size_t>(si)].reset();
            stepGrid.setStepSequence (&track.seq); ratchetRow.setStepSequence (&track.seq);
        };
        juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (panel), randBtn.getScreenBounds(), nullptr);
    } // 6 LEDs, vertical

    void updatePageButtons()
    {
        for (int i = 0; i < pageButtons.size(); ++i)
            pageButtons[i]->setToggleState (i == track.page, juce::dontSendNotification);
    }

    void buildEngineKnobs()
    {
        // CRITICAL: clear old knobs before rebuilding!
        for (auto* k : engineKnobs) removeChildComponent (k);
        engineKnobs.clear();
        numAnalogKnobs = 0;
        numFmKnobs = 0;
        numSmpKnobs = 0;
        numER1Knobs = 0;

        // Engine-specific knobs get amber color (drum identity)
        currentAccent = Colours_GB::accent;

        switch (track.type)
        {
            case DrumType::Kick:
                addKnob ("PITCH", 20, 300, track.pitch, [this](float v){ track.pitch = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "Hz"; }, "pitch");
                addKnob ("DECAY", 0.01f, 2.0f, track.decay, [this](float v){ track.decay = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "decay");
                addKnob ("CLICK", 0, 1, track.click, [this](float v){ track.click = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "click");
                addKnob ("P.DEC", 0.005f, 0.3f, track.pitchDec, [this](float v){ track.pitchDec = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "pitchDec");
                addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");
                break;
            case DrumType::Snare:
                addKnob ("TONE", 50, 500, track.tone, [this](float v){ track.tone = v; }, nullptr, "tone");
                addKnob ("T.DEC", 0.01f, 0.5f, track.toneDecay, [this](float v){ track.toneDecay = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "toneDecay");
                addKnob ("N.DEC", 0.01f, 0.5f, track.noiseDecay, [this](float v){ track.noiseDecay = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "noiseDecay");
                addKnob ("SNAP", 0, 1, track.snap, [this](float v){ track.snap = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "snap");
                addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");
                break;
            case DrumType::HiHatClosed:
            case DrumType::HiHatOpen:
                addKnob ("CUT", 1000, 16000, track.cutoff, [this](float v){ track.cutoff = v; },
                         [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "k" : juce::String (static_cast<int>(v)); }, "cutoff");
                addKnob ("DECAY", 0.005f, 1.0f, track.decay, [this](float v){ track.decay = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "decay");
                addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");
                break;
            case DrumType::Clap:
                addKnob ("FREQ", 500, 5000, track.freq, [this](float v){ track.freq = v; }, nullptr, "freq");
                addKnob ("DECAY", 0.01f, 0.5f, track.decay, [this](float v){ track.decay = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "decay");
                addKnob ("SPRD", 0.005f, 0.1f, track.spread, [this](float v){ track.spread = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "spread");
                addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");
                break;
            case DrumType::Tom:
            case DrumType::TomHi:
                addKnob ("PITCH", 40, 500, track.pitch, [this](float v){ track.pitch = v; }, nullptr, "pitch");
                addKnob ("TONE", 0, 100, track.tone, [this](float v){ track.tone = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "%"; }, "tone");
                addKnob ("P.DEC", 0.005f, 0.15f, track.pitchDec, [this](float v){ track.pitchDec = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "pitchDec");
                addKnob ("DECAY", 0.05f, 1.5f, track.decay, [this](float v){ track.decay = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "decay");
                addKnob ("CLICK", 0, 1, track.click, [this](float v){ track.click = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "click");
                addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");
                break;
            case DrumType::Cowbell:
                addKnob ("FRQ1", 200, 1000, track.freq1, [this](float v){ track.freq1 = v; }, nullptr, "freq1");
                addKnob ("FRQ2", 400, 1200, track.freq2, [this](float v){ track.freq2 = v; }, nullptr, "freq2");
                addKnob ("DECAY", 0.05f, 1.0f, track.decay, [this](float v){ track.decay = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "decay");
                addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");
                break;
            case DrumType::Rimshot:
                addKnob ("TONE", 100, 1000, track.tone, [this](float v){ track.tone = v; }, nullptr, "tone");
                addKnob ("DECAY", 0.005f, 0.15f, track.decay, [this](float v){ track.decay = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "decay");
                addKnob ("NOISE", 0, 1, track.noise, [this](float v){ track.noise = v; },
                         [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "noise");
                addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");
                break;
            case DrumType::Crash:
                addKnob ("FREQ", 1000, 12000, track.freq, [this](float v){ track.freq = v; },
                         [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "k" : juce::String (static_cast<int>(v)); }, "freq");
                addKnob ("DECAY", 0.2f, 3.0f, track.decay, [this](float v){ track.decay = v; },
                         [](float v){ return juce::String (v, 1) + "s"; }, "decay");
                addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                         [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");
                break;
        }

        // ── FILTER (shared by ALL drum engines) ──
        currentAccent = juce::Colour (0xffe09040); // orange (same as synth/sampler filter)
        addKnob ("CUT", 0, 100, track.drumCut, [this](float v){ track.drumCut = v; },
                 [](float v){ return juce::String (static_cast<int>(v)); }, "drumCut");
        addKnob ("RES", 0, 1, track.drumRes, [this](float v){ track.drumRes = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "drumRes");
        addKnob ("F.ENV", 0, 100, track.drumFiltEnv, [this](float v){ track.drumFiltEnv = v; },
                 [](float v){ return juce::String (static_cast<int>(v)); }, "drumFiltEnv");
        addKnob ("F.ATK", 0.0001f, 0.1f, track.drumFiltA, [this](float v){ track.drumFiltA = v; },
                 [](float v){ return juce::String (v * 1000.0f, 1) + "ms"; }, "drumFiltA");
        addKnob ("F.DEC", 0.01f, 1.0f, track.drumFiltD, [this](float v){ track.drumFiltD = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "drumFiltD");

        numAnalogKnobs = static_cast<int>(engineKnobs.size());

        // FM knobs (without mix - mix is standalone)
        addKnob ("RATIO", 0.5f, 12.0f, track.fmRatio, [this](float v){ track.fmRatio = v; },
                 [](float v){ return juce::String (v, 1); }, "fmRatio");
        addKnob ("DEPTH", 0, 500, track.fmDepth, [this](float v){ track.fmDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v)); }, "fmDepth");
        addKnob ("FM.DC", 0.001f, 1.0f, track.fmDecay, [this](float v){ track.fmDecay = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "fmDecay");
        addKnob ("NOISE", 0, 1, track.fmNoise, [this](float v){ track.fmNoise = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "fmNoise");
        addKnob ("N.TYP", 0, 3, static_cast<float>(track.fmNoiseType),
                 [this](float v){ track.fmNoiseType = static_cast<int>(v + 0.5f); },
                 [](float v){
                     int t = static_cast<int>(v + 0.5f);
                     if (t == 0) return juce::String ("WHT");
                     if (t == 1) return juce::String ("MTL");
                     if (t == 2) return juce::String ("HSS");
                     return juce::String ("CRN");
                 }, "fmNoiseType");

        numFmKnobs = static_cast<int>(engineKnobs.size()) - numAnalogKnobs;

        // ══ SAMPLER ENGINE KNOBS (drumEngine == 2) ══
        numSmpKnobs = 0;
        smpGroupStarts.clear();
        smpGroupNames.clear();
        if (track.drumEngine == 2)
        {
            // ── SAMPLE ──
            currentAccent = juce::Colour (0xff40b0b0); // teal
            smpGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            smpGroupNames.push_back ("SAMPLE");
            addKnob ("STRT", 0, 1, track.smpStart, [this](float v){ track.smpStart = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpStart");
            addKnob ("END", 0, 1, track.smpEnd, [this](float v){ track.smpEnd = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpEnd");
            addKnob ("GAIN", 0, 2, track.smpGain, [this](float v){ track.smpGain = v; },
                     [](float v){ return juce::String (v, 1); }, "smpGain");
            addKnob ("LOOP", 0, 1, static_cast<float>(track.smpLoop), [this](float v){ track.smpLoop = static_cast<int>(v); },
                     [](float v){ return static_cast<int>(v) > 0 ? juce::String ("ON") : juce::String ("OFF"); }, "smpLoop");
            addKnob ("PLAY", 0, 1, static_cast<float>(track.smpPlayMode), [this](float v){ track.smpPlayMode = static_cast<int>(v); },
                     [](float v){ return static_cast<int>(v) > 0 ? juce::String ("GATE") : juce::String ("1SH"); }, "smpPlayMode");
            addKnob ("REV", 0, 1, static_cast<float>(track.smpReverse), [this](float v){ track.smpReverse = static_cast<int>(v); },
                     [](float v){ return static_cast<int>(v) > 0 ? juce::String ("ON") : juce::String ("OFF"); }, "smpReverse");
            // ── PITCH ──
            currentAccent = juce::Colour (0xff50c0b0); // cyan
            smpGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            smpGroupNames.push_back ("PITCH");
            addKnob ("TUNE", -24, 24, track.smpTune, [this](float v){ track.smpTune = v; },
                     [](float v){ int s = static_cast<int>(v); return (s >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (s) + "st"; }, "smpTune");
            addKnob ("FINE", -1, 1, track.smpFine, [this](float v){ track.smpFine = v; },
                     [](float v){ int c = static_cast<int>(v * 100); return (c >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (c) + "ct"; }, "smpFine");
            addKnob ("ROOT", 0, 127, static_cast<float>(track.smpRootNote), [this](float v){ track.smpRootNote = static_cast<int>(v); },
                     [](float v){
                         int n = static_cast<int>(v);
                         const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                         return juce::String (names[n % 12]) + juce::String (n / 12 - 2);
                     }, "smpRootNote");
            // ── AMP ENVELOPE ──
            currentAccent = juce::Colour (0xff70c050); // green
            smpGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            smpGroupNames.push_back ("AMP");
            addKnob ("A", 0.001f, 2.0f, track.smpA, [this](float v){ track.smpA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpA");
            addKnob ("D", 0.01f, 2.0f, track.smpD, [this](float v){ track.smpD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpD");
            addKnob ("S", 0, 1, track.smpS, [this](float v){ track.smpS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpS");
            addKnob ("R", 0.01f, 3.0f, track.smpR, [this](float v){ track.smpR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpR");
            // ── FILTER ──
            currentAccent = juce::Colour (0xffe09040); // orange
            smpGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            smpGroupNames.push_back ("FILTER");
            addKnob ("FLT", 0, 5, static_cast<float>(track.smpFModel), [this](float v){ track.smpFModel = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"CLN","ACD","DRT","SEM","ARP","LQD"};
                         int m = static_cast<int>(v);
                         return juce::String (m < 6 ? n[m] : "?");
                     }, "smpFModel");
            addKnob ("CUT", 0, 100, track.smpCut, [this](float v){ track.smpCut = v; },
                     [](float v){ return juce::String (static_cast<int>(v)) + "%"; }, "smpCut");
            addKnob ("RES", 0, 1, track.smpRes, [this](float v){ track.smpRes = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpRes");
            addKnob ("TYP", 0, 3, static_cast<float>(track.smpFType), [this](float v){ track.smpFType = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"LP","HP","BP","NTC"};
                         return juce::String (n[static_cast<int>(v) % 4]);
                     }, "smpFType");
            addKnob ("dB", 0, 2, static_cast<float>((track.smpFPoles == 6) ? 0 : (track.smpFPoles == 12) ? 1 : 2),
                     [this](float v){
                         int p = static_cast<int>(v);
                         track.smpFPoles = (p == 0) ? 6 : (p == 1) ? 12 : 24;
                     },
                     [](float v){
                         int p = static_cast<int>(v);
                         return juce::String ((p == 0) ? "6dB" : (p == 1) ? "12dB" : "24dB");
                     }, "smpFPoles");
            addKnob ("ENV", -100, 100, track.smpFiltEnv, [this](float v){ track.smpFiltEnv = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "smpFiltEnv");
            // ── FILT ENV ──
            smpGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            smpGroupNames.push_back ("FILT ENV");
            addKnob ("A", 0.001f, 2.0f, track.smpFiltA, [this](float v){ track.smpFiltA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFiltA");
            addKnob ("D", 0.01f, 2.0f, track.smpFiltD, [this](float v){ track.smpFiltD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFiltD");
            addKnob ("S", 0, 1, track.smpFiltS, [this](float v){ track.smpFiltS = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpFiltS");
            addKnob ("R", 0.01f, 3.0f, track.smpFiltR, [this](float v){ track.smpFiltR = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFiltR");
            // ── FM MODULATOR ──
            currentAccent = juce::Colour (0xff6090dd); // blue
            smpGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            smpGroupNames.push_back ("FM MOD");
            addKnob ("FM", 0, 1, track.smpFmAmt, [this](float v){ track.smpFmAmt = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "smpFmAmt");
            addKnob ("RAT", 0.5f, 16, track.smpFmRatio, [this](float v){ track.smpFmRatio = v; },
                     [](float v){ return juce::String (v, 1) + "x"; }, "smpFmRatio");
            addKnob ("F.A", 0.001f, 1.0f, track.smpFmEnvA, [this](float v){ track.smpFmEnvA = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFmEnvA");
            addKnob ("F.D", 0.01f, 2.0f, track.smpFmEnvD, [this](float v){ track.smpFmEnvD = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "smpFmEnvD");
            addKnob ("F.S", 0, 1, track.smpFmEnvS, [this](float v){ track.smpFmEnvS = v; },
                     [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "smpFmEnvS");
            // ── STRETCH ──
            currentAccent = juce::Colour (0xffc0a030); // gold
            smpGroupStarts.push_back (static_cast<int>(engineKnobs.size()));
            smpGroupNames.push_back ("STRETCH");
            addKnob ("STRC", 0.25f, 4.0f, track.smpStretch, [this](float v){ track.smpStretch = v; },
                     [](float v){ return juce::String (v, 2) + "x"; }, "smpStretch");
            addKnob ("WARP", 0, 4, static_cast<float>(track.smpWarp), [this](float v){ track.smpWarp = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"OFF","BEAT","TEXR","RPTC","BTS2"};
                         return juce::String (n[static_cast<int>(v) % 5]);
                     }, "smpWarp");
            addKnob ("BPM", 40, 220, track.smpBPM, [this](float v){ track.smpBPM = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "smpBPM");
            addKnob ("SYNC", 0, 1, static_cast<float>(track.smpBpmSync), [this](float v){ track.smpBpmSync = static_cast<int>(v); },
                     [](float v){ return static_cast<int>(v) == 0 ? juce::String ("INT") : juce::String ("DAW"); }, "smpBpmSync");
            addKnob ("RATE", -3, 3, static_cast<float>(track.smpSyncMul), [this](float v){ track.smpSyncMul = static_cast<int>(v); },
                     [](float v){
                         int iv = static_cast<int>(v);
                         if (iv == 0) return juce::String ("1x");
                         if (iv > 0) return juce::String ("x") + juce::String (1 << iv);
                         return juce::String ("/") + juce::String (1 << (-iv));
                     }, "smpSyncMul");
            addKnob ("BARS", 0, 8, static_cast<float>(track.smpBars), [this](float v){ track.smpBars = std::clamp (static_cast<int>(v + 0.5f), 0, 8); },
                     [](float v){
                         int b = static_cast<int>(v + 0.5f);
                         const char* n[] = {"AUTO","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"};
                         return juce::String (n[std::clamp (b, 0, 8)]);
                     }, "smpBars");
            numSmpKnobs = static_cast<int>(engineKnobs.size()) - numAnalogKnobs - numFmKnobs;
        }

        // ══ ER-1 ENGINE KNOBS (drumEngine == 3) ══
        numER1Knobs = 0;
        if (track.drumEngine == 3)
        {
            // ── OSC 1 ──
            currentAccent = Colours_GB::accent;
            addKnob ("W.1", 0, 3, static_cast<float>(track.er1Wave1), [this](float v){ track.er1Wave1 = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"SIN","TRI","SAW","SQR"}; return juce::String (n[static_cast<int>(v) % 4]); }, "er1Wave1");
            addKnob ("P.1", 20, 2000, track.er1Pitch1, [this](float v){ track.er1Pitch1 = v; },
                     [](float v){ return v < 1000 ? juce::String (static_cast<int>(v)) + "Hz" : juce::String (v/1000,1) + "k"; }, "er1Pitch1");
            addKnob ("PD1", 0.002f, 0.5f, track.er1PDec1, [this](float v){ track.er1PDec1 = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "er1PDec1");
            // ── OSC 2 ──
            currentAccent = juce::Colour (0xff40b0c0); // cyan
            addKnob ("W.2", 0, 3, static_cast<float>(track.er1Wave2), [this](float v){ track.er1Wave2 = static_cast<int>(v); },
                     [](float v){ const char* n[] = {"SIN","TRI","SAW","SQR"}; return juce::String (n[static_cast<int>(v) % 4]); }, "er1Wave2");
            addKnob ("P.2", 20, 2000, track.er1Pitch2, [this](float v){ track.er1Pitch2 = v; },
                     [](float v){ return v < 1000 ? juce::String (static_cast<int>(v)) + "Hz" : juce::String (v/1000,1) + "k"; }, "er1Pitch2");
            addKnob ("PD2", 0.002f, 0.5f, track.er1PDec2, [this](float v){ track.er1PDec2 = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "er1PDec2");
            // ── MOD ──
            currentAccent = juce::Colour (0xffc060a0); // magenta/pink
            addKnob ("RING", 0, 1, track.er1Ring, [this](float v){ track.er1Ring = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "er1Ring");
            addKnob ("XMOD", 0, 500, track.er1XMod, [this](float v){ track.er1XMod = v; },
                     [](float v){ return juce::String (static_cast<int>(v)); }, "er1XMod");
            // ── NOISE ──
            currentAccent = juce::Colour (0xff909090); // gray
            addKnob ("NZ", 0, 1, track.er1Noise, [this](float v){ track.er1Noise = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "er1Noise");
            addKnob ("N.DC", 0.005f, 0.5f, track.er1NDec, [this](float v){ track.er1NDec = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "er1NDec");
            // ── FILTER ──
            currentAccent = juce::Colour (0xffe09040); // orange
            addKnob ("CUT", 200, 16000, track.er1Cut, [this](float v){ track.er1Cut = v; },
                     [](float v){ return v >= 1000 ? juce::String (v/1000,1) + "k" : juce::String (static_cast<int>(v)); }, "er1Cut");
            addKnob ("RES", 0, 0.98f, track.er1Res, [this](float v){ track.er1Res = v; },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "er1Res");
            // ── AMP ──
            currentAccent = juce::Colour (0xff70c050); // green
            addKnob ("DEC", 0.01f, 3.0f, track.er1Decay, [this](float v){ track.er1Decay = v; },
                     [](float v){ return v < 1.0f ? juce::String (static_cast<int>(v * 1000)) + "ms" : juce::String (v, 1) + "s"; }, "er1Decay");
            addKnob ("DRV", 0, 1, track.er1Drive, [this](float v){ track.er1Drive = v; },
                     [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "er1Drive");
            addKnob ("TUNE", -24, 24, track.tune, [this](float v){ track.tune = v; },
                     [](float v){ return juce::String (static_cast<int>(v)) + "st"; }, "tune");

            numER1Knobs = static_cast<int>(engineKnobs.size()) - numAnalogKnobs - numFmKnobs - numSmpKnobs;
        }

        // FX knobs
        fxStartIdx = static_cast<int>(engineKnobs.size());
        // ── DRIVE ──
        currentAccent = juce::Colour (0xffc06040); // warm red (same as synth)
        addKnob ("DST", 0, 1, track.distAmt, [this](float v){ track.distAmt = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "distAmt");
        addKnob ("BIT", 1, 16, track.reduxBits, [this](float v){ track.reduxBits = v; },
                 [](float v){ return v >= 15.5f ? juce::String ("OFF") : juce::String (static_cast<int>(v)); }, "reduxBits");
        addKnob ("S.R", 0, 1, track.reduxRate, [this](float v){ track.reduxRate = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "reduxRate");
        // ── CHORUS ──
        currentAccent = juce::Colour (0xff50a0d0); // light blue
        addKnob ("CHO", 0, 1, track.chorusMix, [this](float v){ track.chorusMix = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "chorusMix");
        addKnob ("C.R", 0.1f, 5, track.chorusRate, [this](float v){ track.chorusRate = v; },
                 [](float v){ return juce::String (v, 1) + "Hz"; }, "chorusRate");
        addKnob ("C.D", 0, 1, track.chorusDepth, [this](float v){ track.chorusDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "chorusDepth");
        // ── DELAY ── (order matches SynthTrackRow: algo first)
        currentAccent = juce::Colour (0xff60b080); // teal
        addKnob ("D.AL", 0, 3, static_cast<float>(track.delayAlgo), [this](float v){ track.delayAlgo = static_cast<int>(v); },
                 [](float v){ const char* n[] = {"DIG","TAPE","BBD","DIFF"}; return juce::String (n[static_cast<int>(v) % 4]); }, "delayAlgo");
        addKnob ("DLY", 0, 1, track.delayMix, [this](float v){ track.delayMix = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "delayMix");
        addKnob ("D.S", 0, 1, track.delaySync ? 1.0f : 0.0f,
                 [this](float v){ track.delaySync = (v > 0.5f); },
                 [](float v){ return v > 0.5f ? juce::String ("SYN") : juce::String ("FRE"); }, "delaySync");
        addKnob ("D.B", 0.125f, 4.0f, track.delayBeats, [this](float v){
                     const float divs[] = {0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
                     float best = divs[0]; float bestDist = 999.0f;
                     for (float d : divs) { float dist = std::abs(v - d); if (dist < bestDist) { bestDist = dist; best = d; } }
                     track.delayBeats = best;
                 },
                 [](float v){
                     if (v <= 0.17f)  return juce::String ("1/32");
                     if (v <= 0.31f)  return juce::String ("1/16");
                     if (v <= 0.43f)  return juce::String ("D.16");
                     if (v <= 0.62f)  return juce::String ("1/8");
                     if (v <= 0.87f)  return juce::String ("D.8");
                     if (v <= 1.25f)  return juce::String ("1/4");
                     if (v <= 1.75f)  return juce::String ("D.4");
                     if (v <= 2.5f)   return juce::String ("1/2");
                     if (v <= 3.5f)   return juce::String ("D.2");
                     return juce::String ("1bar");
                 }, "delayBeats");
        addKnob ("D.TM", 0.001f, 2.0f, track.delayTime, [this](float v){ track.delayTime = v; },
                 [](float v){ return v < 1.0f ? juce::String (static_cast<int>(v * 1000)) + "ms"
                                               : juce::String (v, 1) + "s"; }, "delayTime");
        addKnob ("D.FB", 0, 0.9f, track.delayFB, [this](float v){ track.delayFB = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "delayFB");
        addKnob ("D.P", 0, 1, static_cast<float>(track.delayPP), [this](float v){ track.delayPP = static_cast<int>(v); },
                 [](float v){ return static_cast<int>(v) > 0 ? juce::String ("P.P") : juce::String ("MNO"); }, "delayPP");
        addKnob ("D.LP", 0, 1, track.delayDamp, [this](float v){ track.delayDamp = v; },
                 [](float v){
                     if (v < 0.1f) return juce::String ("BRT");
                     if (v < 0.4f) return juce::String ("AIR");
                     if (v < 0.7f) return juce::String ("WRM");
                     return juce::String ("DRK");
                 }, "delayDamp");
        // ── OTT ──
        currentAccent = juce::Colour (0xfff0c040); // gold/yellow
        addKnob ("OTT", 0, 1, track.ottDepth, [this](float v){ track.ottDepth = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "ottDepth");
        addKnob ("UP", 0, 1, track.ottUpward, [this](float v){ track.ottUpward = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "ottUpward");
        addKnob ("DN", 0, 1, track.ottDownward, [this](float v){ track.ottDownward = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "ottDownward");
        // ── PHASER ──
        currentAccent = juce::Colour (0xffd050e0); // magenta
        addKnob ("PHS", 0, 1, track.phaserMix, [this](float v){ track.phaserMix = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "phaserMix");
        addKnob ("PH.R", 0.05f, 5.0f, track.phaserRate, [this](float v){ track.phaserRate = v; },
                 [](float v){ return juce::String (v, 1) + "Hz"; }, "phaserRate");
        addKnob ("PH.D", 0, 1, track.phaserDepth, [this](float v){ track.phaserDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "phaserDepth");
        addKnob ("PH.F", 0, 0.95f, track.phaserFB, [this](float v){ track.phaserFB = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "phaserFB");
        // ── REVERB ── (order matches SynthTrackRow: algo first)
        currentAccent = juce::Colour (0xff6070d8); // indigo
        addKnob ("R.AL", 0, 6, static_cast<float>(track.reverbAlgo), [this](float v){ track.reverbAlgo = static_cast<int>(v); },
                 [](float v){ const char* n[] = {"FDN","PLTE","SHIM","GLXY","ROOM","SPRG","GATE"}; return juce::String (n[static_cast<int>(v) % 7]); }, "reverbAlgo");
        addKnob ("REV", 0, 1, track.reverbMix, [this](float v){ track.reverbMix = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "reverbMix");
        addKnob ("R.S", 0.05f, 4.0f, track.reverbSize, [this](float v){ track.reverbSize = v; },
                 [](float v){
                     if (v < 0.3f) return juce::String ("TINY");
                     if (v < 0.8f) return juce::String ("ROOM");
                     if (v < 1.5f) return juce::String ("HALL");
                     if (v < 2.5f) return juce::String ("CATH");
                     return juce::String ("INF");
                 }, "reverbSize");
        addKnob ("R.DM", 0, 1, track.reverbDamp, [this](float v){ track.reverbDamp = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "reverbDamp");
        // ── EQ ──
        currentAccent = juce::Colour (0xff50d0b0); // mint
        addKnob ("LO", -12, 12, track.eqLow, [this](float v){ track.eqLow = v; },
                 [](float v){ int d = static_cast<int>(v); return (d >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (d); }, "eqLow");
        addKnob ("MD", -12, 12, track.eqMid, [this](float v){ track.eqMid = v; },
                 [](float v){ int d = static_cast<int>(v); return (d >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (d); }, "eqMid");
        addKnob ("HI", -12, 12, track.eqHigh, [this](float v){ track.eqHigh = v; },
                 [](float v){ int d = static_cast<int>(v); return (d >= 0 ? juce::String ("+") : juce::String ("")) + juce::String (d); }, "eqHigh");
        // ── OUTPUT ──
        currentAccent = juce::Colour (0xff909098); // grey
        addKnob ("LP", 200, 20000, track.fxLP, [this](float v){ track.fxLP = v; },
                 [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "k" : juce::String (static_cast<int>(v)); }, "fxLP");
        addKnob ("HP", 20, 5000, track.fxHP, [this](float v){ track.fxHP = v; },
                 [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "k" : juce::String (static_cast<int>(v)); }, "fxHP");
        addKnob ("PAN", -1, 1, track.pan, [this](float v){ track.pan = v; },
                 [](float v){
                     if (v < -0.05f) return juce::String ("L") + juce::String (static_cast<int>(-v * 50));
                     if (v > 0.05f) return juce::String ("R") + juce::String (static_cast<int>(v * 50));
                     return juce::String ("C");
                 }, "pan");
        // Ducking
        // ── DUCK ──
        currentAccent = juce::Colour (0xffe08020); // orange
        addKnob ("D.S", -1, 14, static_cast<float>(track.duckSrc), [this](float v){ track.duckSrc = static_cast<int>(v); },
                 [](float v){
                     int s = static_cast<int>(v);
                     if (s < 0) return juce::String ("OFF");
                     if (s < 10) { const char* n[] = {"KK","SN","HC","HO","CR","CB","RM","TM","TH","CP"}; return juce::String (n[s]); }
                     return juce::String ("S") + juce::String (s - 9);
                 }, "duckSrc");
        addKnob ("D.D", 0, 1, track.duckDepth, [this](float v){ track.duckDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "duckDepth");
        addKnob ("D.A", 0.001f, 0.1f, track.duckAtk, [this](float v){ track.duckAtk = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "duckAtk");
        addKnob ("D.R", 0.01f, 1.0f, track.duckRel, [this](float v){ track.duckRel = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; }, "duckRel");
        // ── SAT ──
        currentAccent = juce::Colour (0xffe04070); // rose
        addKnob ("TYP", 0, 4, static_cast<float>(track.proDistModel), [this](float v){ track.proDistModel = static_cast<int>(v + 0.5f); },
                 [](float v){ const char* n[]={"TUBE","TAPE","XFMR","AMP","WSHP"}; return juce::String(n[std::clamp(static_cast<int>(v+0.5f),0,4)]); }, "proDistModel");
        addKnob ("DRV", 0, 1, track.proDistDrive, [this](float v){ track.proDistDrive = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "proDistDrive");
        addKnob ("TON", 0, 1, track.proDistTone, [this](float v){ track.proDistTone = v; },
                 [](float v){ return v < 0.2f ? juce::String ("DRK") : (v > 0.8f ? juce::String ("BRT") : juce::String (static_cast<int>(v*100)) + "%"); }, "proDistTone");
        addKnob ("MIX", 0, 1, track.proDistMix, [this](float v){ track.proDistMix = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "proDistMix");
        addKnob ("BIA", 0, 1, track.proDistBias, [this](float v){ track.proDistBias = v; },
                 [](float v){ return v < 0.02f ? juce::String ("SYM") : juce::String (static_cast<int>(v * 100)) + "%"; }, "proDistBias");
        // ── FLANGER ──
        currentAccent = juce::Colour (0xffa0d040); // yellow-green
        addKnob ("FLG", 0, 1, track.flangerMix, [this](float v){ track.flangerMix = v; },
                 [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; }, "flangerMix");
        addKnob ("FL.R", 0.05f, 5.0f, track.flangerRate, [this](float v){ track.flangerRate = v; },
                 [](float v){ return juce::String (v, 1) + "Hz"; }, "flangerRate");
        addKnob ("FL.D", 0, 1, track.flangerDepth, [this](float v){ track.flangerDepth = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "flangerDepth");
        addKnob ("FL.F", -0.95f, 0.95f, track.flangerFB, [this](float v){ track.flangerFB = v; },
                 [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; }, "flangerFB");


        // ══ LFO (3 per drum track) ══
        lfoStartIdx = static_cast<int>(engineKnobs.size());
        static const juce::Colour drumLfoColors[] = {
            juce::Colour (0xff50c0b0),  // LFO1 cyan
            juce::Colour (0xffc07090),  // LFO2 pink
            juce::Colour (0xffc0a030)   // LFO3 gold
        };
        for (int li = 0; li < 3; ++li)
        {
            currentAccent = drumLfoColors[li];
            auto& lfo = track.lfos[static_cast<size_t>(li)];

            addKnob ("SYN", 0, 1, lfo.sync ? 1.0f : 0.0f,
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].sync = (v > 0.5f); },
                     [](float v){ return v > 0.5f ? juce::String ("SYN") : juce::String ("FRE"); },
                     "lfo" + juce::String(li) + "Sync");
            addKnob ("RNG", 0, 1, lfo.hiRate ? 1.0f : 0.0f,
                     [this, li](float v){
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         bool newHi = (v > 0.5f);
                         if (newHi != l.hiRate) {
                             float knobPos = l.hiRate ? LFOEngine::hzToKnobHi (l.rate)
                                                      : LFOEngine::hzToKnob (l.rate);
                             l.hiRate = newHi;
                             l.rate = newHi ? LFOEngine::knobToHzHi (knobPos)
                                            : LFOEngine::knobToHz (knobPos);
                         }
                     },
                     [this, li](float v){
                         if (track.lfos[static_cast<size_t>(li)].sync) return juce::String ("---");
                         return v > 0.5f ? juce::String ("HI") : juce::String ("LO");
                     },
                     "lfo" + juce::String(li) + "HiRate");
            addKnob ("RATE", 0, 17, lfo.sync ? lfo.syncDiv
                         : (lfo.hiRate ? LFOEngine::hzToKnobHi (lfo.rate) : LFOEngine::hzToKnob (lfo.rate)),
                     [this, li](float v){
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         l.syncDiv = v;
                         l.rate = l.hiRate ? LFOEngine::knobToHzHi (v) : LFOEngine::knobToHz (v);
                     },
                     [this, li](float v){
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         if (l.sync)
                             return juce::String (LFOEngine::getDivName (static_cast<int>(v)));
                         float hz = l.hiRate ? LFOEngine::knobToHzHi (v) : LFOEngine::knobToHz (v);
                         return LFOEngine::formatHz (hz);
                     },
                     "lfo" + juce::String(li) + "Rate");
            addKnob ("AMT", -1, 1, lfo.depth,
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].depth = v; },
                     [](float v){
                         int pct = static_cast<int>(v * 100);
                         if (pct == 0) return juce::String ("OFF");
                         return (pct > 0 ? juce::String ("+") : juce::String ("")) + juce::String (pct) + "%";
                     },
                     "lfo" + juce::String(li) + "Depth");
            addKnob ("WAVE", 0, 5, static_cast<float>(lfo.shape),
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].shape = static_cast<int>(v); },
                     [](float v){
                         const char* n[] = {"SIN","TRI","SAW","SQR","RMP","S&H"};
                         return juce::String (n[std::clamp (static_cast<int>(v), 0, 5)]);
                     }, "lfo" + juce::String(li) + "Shape");
            addKnob ("DEST", 0, static_cast<float>(LFOEngine::kNumDrumTargets - 1),
                     static_cast<float>(lfo.target),
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].target = static_cast<int>(v); },
                     [](float v){
                         return juce::String (LFOEngine::getDrumTargetName (static_cast<int>(v)));
                     }, "lfo" + juce::String(li) + "Target");
            // Categorized popup menu for target selection
            {
                using PC = KnobComponent::PopupCategory;
                std::vector<PC> cats;
                auto mkCat = [](const juce::String& name, std::initializer_list<int> indices) {
                    PC c; c.name = name;
                    for (int i : indices)
                        c.items.push_back ({i, LFOEngine::getDrumTargetName (i)});
                    return c;
                };
                cats.push_back (mkCat ("PITCH/AMP", {0, 1, 3, 7, 10}));  // PCH, DEC, VOL, CLK, PDC
                cats.push_back (mkCat ("FILTER",    {2, 8, 9, 27, 28, 29})); // TON, CUT, RES, F.EV, F.A, F.D
                cats.push_back (mkCat ("SEND",      {4, 5, 6}));          // PAN, DLY, DST
                cats.push_back (mkCat ("FM",        {11, 12, 13, 14, 15}));
                cats.push_back (mkCat ("FX",        {16, 17, 18, 19, 20, 21, 22, 55, 57, 58, 59}));
                            cats.push_back (mkCat ("EQ/OUT",    {60, 61, 62, 63, 64}));
                            cats.push_back (mkCat ("DUCK",      {65, 66, 67}));
                            cats.push_back (mkCat ("OTT",       {79,80,81}));
                            cats.push_back (mkCat ("SAT",       {82,83,84,85}));
                            cats.push_back (mkCat ("PHASER",    {86,87,88,89}));
                            cats.push_back (mkCat ("FLANGER",   {90,91,92,93}));
                cats.push_back (mkCat ("SAMPLER",   {23, 24, 25, 26, 49, 50, 51, 52, 53, 54}));   // S.CT, S.TN, S.GN, S.ST
                cats.push_back (mkCat ("X-MOD",     {30, 31, 32, 33, 34, 35}));
                engineKnobs.getLast()->setCategorizedPopup (cats);
            }
            addKnob ("RTG", 0, 1, lfo.retrig ? 1.0f : 0.0f,
                     [this, li](float v){ track.lfos[static_cast<size_t>(li)].retrig = (v > 0.5f); },
                     [](float v){ return v > 0.5f ? juce::String ("ON") : juce::String ("OFF"); },
                     "lfo" + juce::String(li) + "Retrig");
            // ── Fade-in (green = timing) — sync follows LFO sync ──
            currentAccent = juce::Colour (0xff60c070);
            addKnob ("FDI", 0, 10, lfo.fadeIn,
                     [this, li](float v){
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         l.fadeIn = v;
                         l.fadeInSync = l.sync;
                     },
                     [this, li](float v){
                         if (v < 0.01f) return juce::String ("OFF");
                         auto& l = track.lfos[static_cast<size_t>(li)];
                         if (l.sync) {
                             static const char* sn[]={"1/32","1/16","1/8","1/4","1/2","1bar","2bar","4bar","8bar","16br","32br"};
                             return juce::String (sn[std::clamp (static_cast<int>(v), 0, 10)]);
                         }
                         if (v < 1.0f) return juce::String (static_cast<int>(v * 1000)) + "ms";
                         return juce::String (v, 1) + "s";
                     }, "lfo" + juce::String(li) + "FadeIn");

            // ── Extra modulation routes (blue = routing) ──
            currentAccent = juce::Colour (0xff6090dd);
            for (int ri = 0; ri < 3; ++ri)
            {
                auto& route = lfo.extraRoutes[static_cast<size_t>(ri)];
                juce::String rLabel = "D" + juce::String (ri + 2);
                juce::String aLabel = "A" + juce::String (ri + 2);
                addKnob (rLabel, -1, static_cast<float>(LFOEngine::kNumDrumTargets - 1),
                         static_cast<float>(route.target),
                         [this, li, ri](float v){ track.lfos[static_cast<size_t>(li)].extraRoutes[static_cast<size_t>(ri)].target = static_cast<int>(v); },
                         [](float v){
                             int t = static_cast<int>(v);
                             if (t < 0) return juce::String ("OFF");
                             return juce::String (LFOEngine::getDrumTargetName (t));
                         }, "lfo" + juce::String(li) + "ExDst" + juce::String(ri));
                {
                    using PC = KnobComponent::PopupCategory;
                    std::vector<PC> cats;
                    auto mkCat = [](const juce::String& name, std::initializer_list<int> indices) {
                        PC c; c.name = name;
                        for (int i : indices)
                            c.items.push_back ({i, LFOEngine::getDrumTargetName (i)});
                        return c;
                    };
                    { PC c; c.name = "---"; c.items.push_back ({-1, "OFF"}); cats.push_back (c); }
                    cats.push_back (mkCat ("PITCH/AMP", {0, 1, 3, 7, 10}));
                    cats.push_back (mkCat ("FILTER",    {2, 8, 9, 27, 28, 29}));
                    cats.push_back (mkCat ("SEND",      {4, 5, 6}));
                    cats.push_back (mkCat ("FM",        {11, 12, 13, 14, 15}));
                    cats.push_back (mkCat ("FX",        {16, 17, 18, 19, 20, 21, 22, 55, 57, 58, 59}));
                            cats.push_back (mkCat ("EQ/OUT",    {60, 61, 62, 63, 64}));
                            cats.push_back (mkCat ("DUCK",      {65, 66, 67}));
                            cats.push_back (mkCat ("OTT",       {79,80,81}));
                            cats.push_back (mkCat ("SAT",       {82,83,84,85}));
                            cats.push_back (mkCat ("PHASER",    {86,87,88,89}));
                            cats.push_back (mkCat ("FLANGER",   {90,91,92,93}));
                    cats.push_back (mkCat ("SAMPLER",   {23, 24, 25, 26, 49, 50, 51, 52, 53, 54}));
                    cats.push_back (mkCat ("X-MOD",     {30, 31, 32, 33, 34, 35}));
                    engineKnobs.getLast()->setCategorizedPopup (cats);
                }
                addKnob (aLabel, -1, 1, route.depth,
                         [this, li, ri](float v){ track.lfos[static_cast<size_t>(li)].extraRoutes[static_cast<size_t>(ri)].depth = v; },
                         [](float v){
                             int pct = static_cast<int>(v * 100);
                             if (pct == 0) return juce::String ("OFF");
                             return (pct > 0 ? juce::String ("+") : juce::String ("")) + juce::String (pct) + "%";
                         }, "lfo" + juce::String(li) + "ExAmt" + juce::String(ri));
            }
        }
    }

    void addKnob (const juce::String& name, float minVal, float maxVal, float initVal,
                  std::function<void(float)> onChange,
                  std::function<juce::String(float)> formatter = nullptr,
                  const juce::String& plockKeyOverride = "")
    {
        auto* knob = new KnobComponent (name, minVal, maxVal, initVal, formatter);
        knob->onChange = onChange;
        knob->setAccentColour (currentAccent);
        std::string pk = plockKeyOverride.isNotEmpty()
            ? plockKeyOverride.toStdString()
            : name.toLowerCase().toStdString();
        knob->setupPlock (&plockMode, &plockTargetStep, &track.seq, pk);
        if (motionRecPtr != nullptr && motionStepPtr != nullptr)
            knob->setupMotionRec (motionRecPtr, motionStepPtr, motionRecModePtr);
        if (macroEnginePtr != nullptr)
            knob->setupMacro (macroEnginePtr, 1, index);
        knob->onPlockWritten = [this]() { stepGrid.repaint(); };

        // ── Drum modulation assignment via right-click ──
        knob->modTargetId = drumParamKeyToModTarget (pk);
        if (knob->modTargetId >= 0)
        {
            int tgtId = knob->modTargetId;
            float kMin = minVal, kMax = maxVal;
            knob->onModRightClick = [this, tgtId, pk, kMin, kMax]()
            {
                showDrumModAssignPopup (tgtId, pk, kMin, kMax);
            };
        }

        knob->setVisible (false);
        engineKnobs.add (knob);
        addChildComponent (knob);
    }

    // ── Drum paramKey → LFO target ID ──
    static int drumParamKeyToModTarget (const std::string& key)
    {
        static const std::unordered_map<std::string, int> map = {
            {"pitch",0}, {"decay",1}, {"tone",2}, {"volume",3}, {"pan",4},
            {"delayMix",5}, {"distAmt",6}, {"click",7}, {"drumCut",8}, {"drumRes",9},
            {"pitchDec",10}, {"freq",0}, {"tune",0},
            {"fmMix",11}, {"fmRatio",12}, {"fmDepth",13}, {"fmDecay",14}, {"fmNoise",15},
            {"chorusRate",16}, {"chorusDepth",17}, {"delayTime",18}, {"delayFB",19},
            {"reverbSize",20}, {"reduxBits",21}, {"reduxRate",22},
            {"smpCut",23}, {"smpTune",24}, {"smpGain",25}, {"smpStart",26},
            {"drumFiltEnv",27}, {"drumFiltA",28}, {"drumFiltD",29},
            {"smpRes",49}, {"smpEnd",50}, {"smpFine",51}, {"smpFmAmt",52}, {"smpFmRatio",53}, {"smpFiltEnv",54},
            {"er1Pitch1",38}, {"er1Pitch2",39}, {"er1PDec1",40}, {"er1PDec2",41},
            {"er1Ring",42}, {"er1XMod",43}, {"er1Noise",44}, {"er1Cut",45}, {"er1Res",46}, {"er1Drive",47},
            {"snap",48}, {"cutoff",8},
            {"noise",15}, {"noiseDecay",14}, {"toneDecay",1}, {"spread",4},
            {"er1Decay",1}, {"er1NDec",14}, {"er1Wave1",38}, {"er1Wave2",39},
            // Extended targets (55-78)
            {"chorusMix",55}, {"reverbMix",56}, {"delayBeats",57}, {"delayDamp",58}, {"reverbDamp",59},
            {"eqLow",60}, {"eqMid",61}, {"eqHigh",62}, {"fxLP",63}, {"fxHP",64},
            {"duckDepth",65}, {"duckAtk",66}, {"duckRel",67},
            {"smpA",68}, {"smpD",69}, {"smpS",70}, {"smpR",71},
            {"smpFiltA",72}, {"smpFiltD",73}, {"smpFiltS",74}, {"smpFiltR",75},
            {"smpFmEnvA",76}, {"smpFmEnvD",77}, {"smpFmEnvS",78},
            // OTT + ProDist (79-85)
            {"ottDepth",79}, {"ottUpward",80}, {"ottDownward",81},
            {"proDistDrive",82}, {"proDistTone",83}, {"proDistMix",84}, {"proDistBias",85},
            // Phaser + Flanger (86-93)
            {"phaserMix",86}, {"phaserRate",87}, {"phaserDepth",88}, {"phaserFB",89},
            {"flangerMix",90}, {"flangerRate",91}, {"flangerDepth",92}, {"flangerFB",93},
        };
        auto it = map.find (key);
        return (it != map.end()) ? it->second : -1;
    }

    // ── Right-click drum modulation popup ──
    void showDrumModAssignPopup (int targetId, const std::string& paramKey, float minVal, float maxVal)
    {
        juce::PopupMenu menu;
        juce::String targetName = LFOEngine::getDrumTargetName (targetId);
        menu.addSectionHeader ("Modulate: " + targetName);

        // ── LFO 1/2/3 ──
        for (int li = 0; li < 3; ++li)
        {
            float curDepth = 0.0f;
            if (track.lfos[static_cast<size_t>(li)].target == targetId)
                curDepth = track.lfos[static_cast<size_t>(li)].depth;
            for (auto& r : track.lfos[static_cast<size_t>(li)].extraRoutes)
                if (r.target == targetId) curDepth = r.depth;
            juce::String lbl = "LFO " + juce::String (li + 1);
            if (std::abs (curDepth) > 0.001f)
                lbl += "  [" + juce::String (static_cast<int>(curDepth * 100)) + "%]";
            menu.addItem (100 + li, lbl);
        }
        menu.addSeparator();
        // ── MSEG 1/2/3 ──
        for (int mi = 0; mi < 3; ++mi)
        {
            float curDepth = 0.0f;
            if (track.msegs[static_cast<size_t>(mi)].target == targetId)
                curDepth = track.msegs[static_cast<size_t>(mi)].depth;
            for (auto& r : track.msegs[static_cast<size_t>(mi)].extraRoutes)
                if (r.target == targetId) curDepth = r.depth;
            juce::String lbl = "MSEG " + juce::String (mi + 1);
            if (std::abs (curDepth) > 0.001f)
                lbl += "  [" + juce::String (static_cast<int>(curDepth * 100)) + "%]";
            menu.addItem (200 + mi, lbl);
        }
        menu.addSeparator();
        // ── MACRO 1/2/3/4 ──
        if (macroEnginePtr != nullptr)
        {
            for (int mi = 0; mi < 4; ++mi)
            {
                float curDepth = 0.0f;
                for (auto& a : macroEnginePtr->macros[static_cast<size_t>(mi)].assignments)
                    if (a.trackType == 1 && a.trackIndex == index && a.paramKey == paramKey)
                        curDepth = a.depth;
                juce::String lbl = "MACRO " + juce::String (mi + 1);
                if (std::abs (curDepth) > 0.001f)
                    lbl += "  [" + juce::String (static_cast<int>(curDepth * 100)) + "%]";
                menu.addItem (300 + mi, lbl);
            }
        }
        menu.addSeparator();
        // ── VEL / KEY tracking ──
        {
            float velD = 0.0f;
            for (auto& vr : track.velRoutes) if (vr.target == targetId) velD = vr.depth;
            juce::String vl = "VELOCITY";
            if (std::abs (velD) > 0.001f) vl += "  [" + juce::String (static_cast<int>(velD * 100)) + "%]";
            menu.addItem (400, vl);
            float keyD = 0.0f;
            for (auto& kr : track.keyRoutes) if (kr.target == targetId) keyD = kr.depth;
            juce::String kl = "KEY TRACK";
            if (std::abs (keyD) > 0.001f) kl += "  [" + juce::String (static_cast<int>(keyD * 100)) + "%]";
            menu.addItem (401, kl);
        }

        int result = menu.show();
        if (result == 0) return;

        int sourceType = -1, sourceIdx = 0;
        float currentDepth = 0.0f;
        if (result >= 100 && result < 200)
        {
            sourceType = 0; sourceIdx = result - 100;
            if (track.lfos[static_cast<size_t>(sourceIdx)].target == targetId)
                currentDepth = track.lfos[static_cast<size_t>(sourceIdx)].depth;
            for (auto& r : track.lfos[static_cast<size_t>(sourceIdx)].extraRoutes)
                if (r.target == targetId) currentDepth = r.depth;
        }
        else if (result >= 200 && result < 300)
        {
            sourceType = 1; sourceIdx = result - 200;
            if (track.msegs[static_cast<size_t>(sourceIdx)].target == targetId)
                currentDepth = track.msegs[static_cast<size_t>(sourceIdx)].depth;
            for (auto& r : track.msegs[static_cast<size_t>(sourceIdx)].extraRoutes)
                if (r.target == targetId) currentDepth = r.depth;
        }
        else if (result >= 300 && result < 400 && macroEnginePtr != nullptr)
        {
            sourceType = 2; sourceIdx = result - 300;
            for (auto& a : macroEnginePtr->macros[static_cast<size_t>(sourceIdx)].assignments)
                if (a.trackType == 1 && a.trackIndex == index && a.paramKey == paramKey)
                    currentDepth = a.depth;
        }
        else if (result == 400)
        {
            sourceType = 3; sourceIdx = 0;
            for (auto& vr : track.velRoutes) if (vr.target == targetId) currentDepth = vr.depth;
        }
        else if (result == 401)
        {
            sourceType = 4; sourceIdx = 0;
            for (auto& kr : track.keyRoutes) if (kr.target == targetId) currentDepth = kr.depth;
        }
        if (sourceType < 0) return;

        static const char* srcNames[] = { "LFO", "MSEG", "MACRO", "VEL", "KEY" };
        juce::String title = juce::String (srcNames[sourceType]) + (sourceType < 3 ? (" " + juce::String (sourceIdx + 1)) : juce::String()) + " → " + targetName;
        auto ae = std::make_shared<juce::AlertWindow> (title, "Depth (-100% to +100%):", juce::MessageBoxIconType::NoIcon);
        ae->addTextEditor ("depth", juce::String (static_cast<int>(currentDepth * 100)), "Depth %:");
        ae->addButton ("Apply", 1);
        ae->addButton ("Remove", 2);
        ae->addButton ("Cancel", 0);
        ae->setAlwaysOnTop (true);

        int sType = sourceType, sIdx = sourceIdx, tId = targetId;
        std::string pKey = paramKey;
        float kMin = minVal, kMax = maxVal;

        ae->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, ae, sType, sIdx, tId, pKey, kMin, kMax](int r)
            {
                if (r == 1)
                {
                    float depth = ae->getTextEditorContents ("depth").getFloatValue() / 100.0f;
                    depth = juce::jlimit (-1.0f, 1.0f, depth);
                    applyDrumModDepth (sType, sIdx, tId, pKey, depth, kMin, kMax);
                }
                else if (r == 2)
                    applyDrumModDepth (sType, sIdx, tId, pKey, 0.0f, kMin, kMax);
            }), false);
    }

    void applyDrumModDepth (int sourceType, int sourceIdx, int targetId,
                            const std::string& paramKey, float depth, float minVal, float maxVal)
    {
        if (sourceType == 0) // LFO
        {
            auto& lfo = track.lfos[static_cast<size_t>(sourceIdx)];
            if (lfo.target == targetId) { lfo.depth = depth; repaint(); return; }
            for (auto& r : lfo.extraRoutes)
                if (r.target == targetId) { if (std::abs(depth)<0.001f) r={-1,0.0f}; else r.depth=depth; repaint(); return; }
            if (std::abs(depth) > 0.001f)
                for (auto& r : lfo.extraRoutes)
                    if (r.target < 0) { r.target=targetId; r.depth=depth; break; }
        }
        else if (sourceType == 1) // MSEG
        {
            auto& mseg = track.msegs[static_cast<size_t>(sourceIdx)];
            if (mseg.target == targetId) { mseg.depth = depth; repaint(); return; }
            for (auto& r : mseg.extraRoutes)
                if (r.target == targetId) { if (std::abs(depth)<0.001f) r={-1,0.0f}; else r.depth=depth; repaint(); return; }
            if (std::abs(depth) > 0.001f)
                for (auto& r : mseg.extraRoutes)
                    if (r.target < 0) { r.target=targetId; r.depth=depth; break; }
        }
        else if (sourceType == 2 && macroEnginePtr != nullptr)
        {
            if (std::abs(depth) < 0.001f)
            {
                auto& a = macroEnginePtr->macros[static_cast<size_t>(sourceIdx)].assignments;
                a.erase(std::remove_if(a.begin(), a.end(),
                    [&](const MacroAssignment& m){ return m.trackType==1 && m.trackIndex==index && m.paramKey==paramKey; }), a.end());
            }
            else
            {
                float curVal = 0.0f;
                for (auto* k : engineKnobs)
                    if (k->modTargetId == drumParamKeyToModTarget(paramKey)) { curVal = k->getValue(); break; }
                macroEnginePtr->macros[static_cast<size_t>(sourceIdx)].addAssignment(1, index, paramKey, curVal, minVal, maxVal, depth);
            }
        }
        else if (sourceType == 3) // VEL
        {
            for (auto& vr : track.velRoutes)
                if (vr.target == targetId) { vr.depth = depth; if (std::abs(depth) < 0.001f) vr.target = -1; repaint(); return; }
            if (std::abs(depth) > 0.001f)
                for (auto& vr : track.velRoutes)
                    if (vr.target < 0) { vr.target = targetId; vr.depth = depth; break; }
        }
        else if (sourceType == 4) // KEY
        {
            for (auto& kr : track.keyRoutes)
                if (kr.target == targetId) { kr.depth = depth; if (std::abs(depth) < 0.001f) kr.target = -1; repaint(); return; }
            if (std::abs(depth) > 0.001f)
                for (auto& kr : track.keyRoutes)
                    if (kr.target < 0) { kr.target = targetId; kr.depth = depth; break; }
        }
        repaint();
    }

    juce::Colour currentAccent { 0xffe0a020 };

    // Motion rec pointers
    std::atomic<bool>* motionRecPtr = nullptr;
    std::atomic<int>*  motionStepPtr = nullptr;
    std::atomic<int>*  motionRecModePtr = nullptr;
    MacroEngine*       macroEnginePtr = nullptr;
public:
    void setMotionRecPointers (std::atomic<bool>* recPtr, std::atomic<int>* stepPtr,
                               std::atomic<int>* modePtr = nullptr)
    {
        motionRecPtr = recPtr;
        motionStepPtr = stepPtr;
        motionRecModePtr = modePtr;
        for (auto* k : engineKnobs)
            k->setupMotionRec (recPtr, stepPtr, modePtr);
        mixKnob.setupMotionRec (recPtr, stepPtr, modePtr);
    }

    void setMacroEngine (MacroEngine* engine)
    {
        macroEnginePtr = engine;
        for (auto* k : engineKnobs)
            k->setupMacro (engine, 1, index);
        mixKnob.setupMacro (engine, 1, index);
    }
private:

    // ── FileDragAndDropTarget — drag samples onto drum track ──
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        // Accept audio files on ANY drum engine mode — will auto-switch to sampler on drop
        for (auto& f : files)
        {
            auto ext = juce::File (f).getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".mp3")
                return true;
        }
        return false;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { repaint(); }
    void fileDragExit (const juce::StringArray&) override { repaint(); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (auto& f : files)
        {
            juce::File file (f);
            if (file.isDirectory())
            {
                sampleFolder.clear();
                for (const auto& entry : juce::RangedDirectoryIterator (file, false, "*.wav;*.aiff;*.aif;*.flac;*.mp3"))
                    sampleFolder.add (entry.getFile());
                struct FileSorter { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()); } };
                FileSorter fs; sampleFolder.sort (fs);
                sampleFolderIdx = 0;
                if (!sampleFolder.isEmpty())
                {
                    track.drumEngine = 2; // auto-switch to sampler
                    loadSampleFromFile (sampleFolder[0]);
                    buildEngineKnobs();
                    resized();
                }
                return;
            }
            auto ext = file.getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".mp3")
            {
                track.drumEngine = 2; // auto-switch to sampler
                sampleFolder.clear();
                sampleFolderIdx = -1;
                loadSampleFromFile (file);
                buildEngineKnobs();
                resized();
                return;
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumTrackRow)
};
