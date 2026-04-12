#include "PluginEditor.h"
#include "UI/Colours.h"
#include "Sequencer/ScaleUtils.h"
#include "State/PresetManager.h"
#include "Audio/Analysis/SampleAnalysis.h"
#include <random>

// ═══════════════════════════════════════
// MainContent — the scrollable inner panel
// ═══════════════════════════════════════
MainContent::MainContent (GrooveBoxProcessor& p)
    : processorRef (p)
{
    // HeaderBar setup (will be reparented to editor in GrooveBoxEditor constructor)

    headerBar.onPlayToggle = [this](bool play) {
        processorRef.state.playing.store (play);
    };
    headerBar.onStop = [this]() {
        processorRef.state.playing.store (false);
    };
    headerBar.onBPMChange = [this](float newBpm) {
        processorRef.state.bpm.store (newBpm);
    };
    headerBar.onClockModeChange = [this](bool ext) {
        processorRef.state.externalClock.store (ext);
    };

    headerBar.onSwingChange = [this](int swing) {
        processorRef.state.globalSwing.store (swing);
    };

    headerBar.onFillToggle = [this](bool fill) {
        processorRef.state.fillMode.store (fill);
    };

    headerBar.setMotionRecState (&processorRef.state);

    headerBar.onClearAllSolo = [this]() {
        for (auto& dt : processorRef.state.drumTracks) dt.solo = false;
        for (auto& st : processorRef.state.synthTracks) st.solo = false;
    };

    headerBar.onScaleRootChange = [this](int root) {
        processorRef.state.scaleRoot.store (root);
    };

    headerBar.onScaleTypeChange = [this](int type) {
        processorRef.state.scaleType.store (type);
    };

    headerBar.onQuantizeAll = [this]() {
        int root = processorRef.state.scaleRoot.load();
        int type = processorRef.state.scaleType.load();
        if (type == 0) return;
        processorRef.state.pushUndo();
        for (auto& st : processorRef.state.synthTracks)
        {
            for (int si = 0; si < st.length; ++si)
            {
                auto& step = st.seq.steps[static_cast<size_t>(si)];
                if (step.active)
                    step.noteIndex = static_cast<uint8_t>(ScaleUtils::quantizeNote (step.noteIndex, root, type));
            }
        }
        for (auto* row : synthRows)
            row->repaint();
    };

    headerBar.onGlobalInit = [this]() {
        juce::PopupMenu menu;
        menu.addItem (20, ">> INIT ALL (Factory Reset) <<");
        menu.addSeparator();
        menu.addItem (1, "RESET ALL (drums + synths)");
        menu.addSeparator();
        menu.addItem (2, "RESET ALL DRUMS ONLY");
        menu.addItem (3, "RESET ALL SYNTHS ONLY");
        menu.addSeparator();
        menu.addItem (11, "RESET ALL ENGINES (sound params only)");
        menu.addItem (12, "RESET ALL DRUM ENGINES");
        menu.addItem (13, "RESET ALL SYNTH ENGINES");
        menu.addSeparator();
        menu.addItem (14, "INIT ALL MODULATIONS (LFO + MSEG + MACRO)");
        menu.addItem (15, "INIT DRUM MODULATIONS");
        menu.addItem (16, "INIT SYNTH MODULATIONS");
        menu.addItem (17, "RESET ALL MACROS");
        menu.addSeparator();
        menu.addItem (4, "RESET STEPS ONLY (all tracks)");
        menu.addItem (5, "RESET VELOCITIES (all tracks)");
        menu.addItem (6, "RESET GATES (all tracks)");
        menu.addItem (7, "RESET RATCHETS (all tracks)");
        menu.addItem (8, "RESET P-LOCKS (all tracks)");
        menu.addItem (9, "RESET TRIG CONDITIONS (all tracks)");
        menu.addItem (10, "RESET NUDGE (all tracks)");

        menu.showMenuAsync (juce::PopupMenu::Options(),
            [this](int result) {
                if (result == 0) return;
                processorRef.state.pushUndo();
                auto applyDrums = [&](auto fn) {
                    for (auto& dt : processorRef.state.drumTracks)
                        for (int si = 0; si < kMaxSteps; ++si) fn (dt.seq.steps[static_cast<size_t>(si)]);
                };
                auto applySynths = [&](auto fn) {
                    for (auto& st : processorRef.state.synthTracks)
                        for (int si = 0; si < kMaxSteps; ++si) fn (st.seq.steps[static_cast<size_t>(si)]);
                };
                auto applyAll = [&](auto fn) { applyDrums (fn); applySynths (fn); };

                switch (result) {
                    case 20:
                        processorRef.state.initAll();
                        for (auto* row : drumRows) { row->rebuildKnobs(); row->repaint(); }
                        for (auto* row : synthRows) { row->rebuildKnobs(); row->repaint(); }
                        if (onPresetLoaded) onPresetLoaded(); // rebuild MasterFXRow
                        headerBar.setSwingDisplay (0);
                        break;
                    case 1:
                        applyAll ([](StepData& s){ s.reset(); });
                        for (auto& dt : processorRef.state.drumTracks) { dt.resetEngine(); dt.resetModulations(); }
                        for (auto& st : processorRef.state.synthTracks) { st.resetEngine(); st.resetModulations(); }
                        for (auto& mk : processorRef.state.macroEngine.macros) mk.clear();
                        break;
                    case 2:
                        applyDrums ([](StepData& s){ s.reset(); });
                        for (auto& dt : processorRef.state.drumTracks) { dt.resetEngine(); dt.resetModulations(); }
                        // Clear drum macro assignments
                        for (auto& mk : processorRef.state.macroEngine.macros)
                            mk.assignments.erase (std::remove_if (mk.assignments.begin(), mk.assignments.end(),
                                [](const MacroAssignment& a){ return a.trackType == 1; }), mk.assignments.end());
                        break;
                    case 3:
                        applySynths ([](StepData& s){ s.reset(); });
                        for (auto& st : processorRef.state.synthTracks) { st.resetEngine(); st.resetModulations(); }
                        for (auto& mk : processorRef.state.macroEngine.macros)
                            mk.assignments.erase (std::remove_if (mk.assignments.begin(), mk.assignments.end(),
                                [](const MacroAssignment& a){ return a.trackType == 0; }), mk.assignments.end());
                        break;
                    case 4:  applyAll ([](StepData& s){ s.active = false; }); break;
                    case 5:  applyAll ([](StepData& s){ s.velocity = 100; }); break;
                    case 6:  applyAll ([](StepData& s){ s.gate = 100; }); break;
                    case 7:  applyAll ([](StepData& s){ s.ratchet = 1; s.triplet = false; }); break;
                    case 8:  applyAll ([](StepData& s){ s.plocks.clear(); }); break;
                    case 9:  applyAll ([](StepData& s){ s.cond = TrigCondition::Always; }); break;
                    case 10: applyAll ([](StepData& s){ s.nudge = 0; }); break;
                    case 11:
                        for (auto& dt : processorRef.state.drumTracks) dt.resetEngine();
                        for (auto& st : processorRef.state.synthTracks) st.resetEngine();
                        break;
                    case 12:
                        for (auto& dt : processorRef.state.drumTracks) dt.resetEngine();
                        break;
                    case 13:
                        for (auto& st : processorRef.state.synthTracks) st.resetEngine();
                        break;
                    case 14:
                        for (auto& dt : processorRef.state.drumTracks) dt.resetModulations();
                        for (auto& st : processorRef.state.synthTracks) st.resetModulations();
                        for (auto& mk : processorRef.state.macroEngine.macros) mk.clear();
                        break;
                    case 15:
                        for (auto& dt : processorRef.state.drumTracks) dt.resetModulations();
                        for (auto& mk : processorRef.state.macroEngine.macros)
                            mk.assignments.erase (std::remove_if (mk.assignments.begin(), mk.assignments.end(),
                                [](const MacroAssignment& a){ return a.trackType == 1; }), mk.assignments.end());
                        break;
                    case 16:
                        for (auto& st : processorRef.state.synthTracks) st.resetModulations();
                        for (auto& mk : processorRef.state.macroEngine.macros)
                            mk.assignments.erase (std::remove_if (mk.assignments.begin(), mk.assignments.end(),
                                [](const MacroAssignment& a){ return a.trackType == 0; }), mk.assignments.end());
                        break;
                    case 17:
                        for (auto& mk : processorRef.state.macroEngine.macros) mk.clear();
                        break;
                }
                for (auto* row : drumRows) { row->rebuildKnobs(); row->repaint(); }
                for (auto* row : synthRows) { row->rebuildKnobs(); row->repaint(); }
                if (onPresetLoaded) onPresetLoaded();
            });
    };

    headerBar.onGlobalRandom = [this]() {
        juce::PopupMenu menu;
        menu.addItem (1, "RANDOMIZE ALL (drums 40% + synths 30%)");
        menu.addSeparator();
        menu.addItem (2, "RANDOMIZE DRUMS ONLY (50%)");
        menu.addItem (3, "RANDOMIZE DRUMS ONLY (25%)");
        menu.addItem (4, "RANDOMIZE DRUMS ONLY (75%)");
        menu.addSeparator();
        menu.addItem (5, "RANDOMIZE SYNTHS ONLY (50%)");
        menu.addItem (6, "RANDOMIZE SYNTHS ONLY (25%)");
        menu.addSeparator();
        menu.addItem (7, "RANDOMIZE VELOCITIES (all active steps)");
        menu.addItem (8, "RANDOMIZE GATES (all active steps)");
        menu.addItem (9, "RANDOMIZE RATCHETS (all active steps)");
        menu.addItem (10, "RANDOMIZE SYNTH NOTES (active steps)");

        menu.showMenuAsync (juce::PopupMenu::Options(),
            [this](int result) {
                if (result == 0) return;
                processorRef.state.pushUndo();
                std::mt19937 rng { std::random_device{}() };
                std::uniform_int_distribution<int> velDist (40, 127);
                std::uniform_int_distribution<int> gateDist (30, 150);
                std::uniform_int_distribution<int> noteDist (0, 11);
                std::uniform_int_distribution<int> ratchDist (1, 4);
                std::uniform_real_distribution<float> prob (0.0f, 1.0f);

                if (result == 1 || result == 2 || result == 3 || result == 4)
                {
                    float density = (result == 3) ? 0.25f : (result == 4) ? 0.75f : (result == 2) ? 0.5f : 0.4f;
                    for (auto& dt : processorRef.state.drumTracks)
                        for (int si = 0; si < dt.length; ++si) {
                            auto& step = dt.seq.steps[static_cast<size_t>(si)];
                            step.active = prob (rng) < density;
                            if (step.active) step.velocity = static_cast<uint8_t>(velDist (rng));
                        }
                }
                if (result == 1 || result == 5 || result == 6)
                {
                    float density = (result == 6) ? 0.25f : (result == 5) ? 0.5f : 0.3f;
                    for (auto& st : processorRef.state.synthTracks)
                        for (int si = 0; si < st.length; ++si) {
                            auto& step = st.seq.steps[static_cast<size_t>(si)];
                            step.active = prob (rng) < density;
                            if (step.active) {
                                step.noteIndex = static_cast<uint8_t>(noteDist (rng));
                                step.octave = 3;
                                step.velocity = static_cast<uint8_t>(velDist (rng));
                            }
                        }
                }
                if (result == 7)
                {
                    for (auto& dt : processorRef.state.drumTracks)
                        for (auto& s : dt.seq.steps) if (s.active) s.velocity = static_cast<uint8_t>(velDist (rng));
                    for (auto& st : processorRef.state.synthTracks)
                        for (auto& s : st.seq.steps) if (s.active) s.velocity = static_cast<uint8_t>(velDist (rng));
                }
                if (result == 8)
                {
                    for (auto& dt : processorRef.state.drumTracks)
                        for (auto& s : dt.seq.steps) if (s.active) s.gate = static_cast<uint8_t>(gateDist (rng));
                    for (auto& st : processorRef.state.synthTracks)
                        for (auto& s : st.seq.steps) if (s.active) s.gate = static_cast<uint8_t>(gateDist (rng));
                }
                if (result == 9)
                {
                    for (auto& dt : processorRef.state.drumTracks)
                        for (auto& s : dt.seq.steps) if (s.active) s.ratchet = static_cast<uint8_t>(ratchDist (rng));
                }
                if (result == 10)
                {
                    for (auto& st : processorRef.state.synthTracks)
                        for (auto& s : st.seq.steps) if (s.active) s.noteIndex = static_cast<uint8_t>(noteDist (rng));
                }
                for (auto* row : drumRows) row->repaint();
                for (auto* row : synthRows) row->repaint();
            });
    };

    // ── UNDO/REDO callback — repaint all rows after restore ──
    headerBar.onUndoRedo = [this]() {
        for (auto* row : drumRows) { row->rebuildKnobs(); row->repaint(); }
        for (auto* row : synthRows) { row->rebuildKnobs(); row->repaint(); }
    };

    // ── PRESET system ──
    headerBar.onPresetClick = [this]() {
        juce::PopupMenu menu;

        // ── SAVE (quick overwrite if already saved) ──
        if (lastGlobalPresetName.isNotEmpty())
            menu.addItem (1, "Save \"" + lastGlobalPresetName + "\"");
        menu.addItem (5, "Save As...");
        menu.addSeparator();

        // ── LOAD submenu ──
        std::vector<juce::File> loadFiles;
        auto loadMenu = PresetManager::buildBrowseMenu (
            PresetManager::PresetType::Global, "", 100, loadFiles);
        if (loadFiles.empty())
            loadMenu.addItem (-1, "(no presets)", false);
        menu.addSubMenu ("Load Global Preset", loadMenu);
        menu.addSeparator();

        // ── DELETE PRESET submenu ──
        std::vector<juce::File> delFiles;
        auto delMenu = PresetManager::buildBrowseMenu (
            PresetManager::PresetType::Global, "", 2000, delFiles);
        if (!delFiles.empty())
            menu.addSubMenu ("Delete Preset", delMenu);
        menu.addSeparator();

        // ── FOLDER ──
        menu.addItem (2, "New Folder...");

        // ── DELETE FOLDER submenu ──
        std::vector<juce::File> delFolders;
        auto folderDelMenu = PresetManager::buildFolderDeleteMenu (
            PresetManager::PresetType::Global, 3000, delFolders);
        if (!delFolders.empty())
            menu.addSubMenu ("Delete Folder", folderDelMenu);

        // Synchronous show — works in all hosts
        int result = menu.show();
        if (result == 0) return;

        if (result == 1 && lastGlobalPresetName.isNotEmpty()) // Quick overwrite
        {
            PresetManager::saveGlobal (processorRef.state, lastGlobalPresetName, lastGlobalPresetFolder);
        }
        else if (result == 5) // Save As...
        {
            PresetManager::showSaveDialog (
                PresetManager::PresetType::Global,
                lastGlobalPresetName.isNotEmpty() ? lastGlobalPresetName : "My Preset",
                [this](juce::String name, juce::String folder) {
                    PresetManager::saveGlobal (processorRef.state, name, folder);
                    lastGlobalPresetName = name;
                    lastGlobalPresetFolder = folder;
                });
        }
        else if (result == 2) // New folder
        {
            PresetManager::showNewFolderDialog (PresetManager::PresetType::Global);
        }
        else if (result >= 100 && result < 2000) // Load
        {
            int fileIdx = result - 100;
            if (fileIdx >= 0 && fileIdx < static_cast<int>(loadFiles.size()))
            {
                PresetManager::loadGlobal (processorRef.state, loadFiles[static_cast<size_t>(fileIdx)]);
                lastGlobalPresetName = loadFiles[static_cast<size_t>(fileIdx)].getFileNameWithoutExtension();
                auto parent = loadFiles[static_cast<size_t>(fileIdx)].getParentDirectory();
                auto typeDir = PresetManager::getTypeDir (PresetManager::PresetType::Global);
                lastGlobalPresetFolder = (parent == typeDir) ? juce::String() : parent.getFileName();
                for (auto* row : drumRows) { row->rebuildKnobs(); row->repaint(); }
                for (auto* row : synthRows) { row->rebuildKnobs(); row->repaint(); }
                headerBar.setBPMDisplay (processorRef.state.bpm.load());
                headerBar.setMasterVolume (processorRef.state.masterVolume.load());
                headerBar.setSwingDisplay (processorRef.state.globalSwing.load());
                if (onPresetLoaded) onPresetLoaded();
            }
        }
        else if (result >= 2000 && result < 3000) // Delete preset
        {
            int dIdx = result - 2000;
            if (dIdx >= 0 && dIdx < static_cast<int>(delFiles.size()))
                PresetManager::deletePreset (delFiles[static_cast<size_t>(dIdx)]);
        }
        else if (result >= 3000) // Delete folder
        {
            int fIdx = result - 3000;
            if (fIdx >= 0 && fIdx < static_cast<int>(delFolders.size()))
                PresetManager::deleteFolder (delFolders[static_cast<size_t>(fIdx)]);
        }
    };

    headerBar.setBPMDisplay (processorRef.state.bpm.load());

    // Master volume
    headerBar.onMasterVolumeChange = [this](float v) {
        processorRef.state.masterVolume.store (v);
    };

    // Drum section
    addAndMakeVisible (drumSectionLabel);
    drumSectionLabel.setText ("DRUM TRACKS", juce::dontSendNotification);
    drumSectionLabel.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
    drumSectionLabel.setColour (juce::Label::textColourId, Colours_GB::textDim);

    for (int i = 0; i < 10; ++i)
    {
        auto* row = new DrumTrackRow (i, processorRef.state.drumTracks[static_cast<size_t>(i)]);
        row->onExpandToggle = [this]() {
            setSize (getWidth(), getDesiredHeight());
        };
        row->setMotionRecPointers (&processorRef.state.motionRec,
                                    &processorRef.state.drumCurrentStep[static_cast<size_t>(i)],
                                    &processorRef.state.motionRecMode);
        row->setMacroEngine (&processorRef.state.macroEngine);
        drumRows.add (row);
        row->onBeforeEdit = [this]() { processorRef.state.pushUndo(); };
        row->onLinkSync = [this, i]() {
            processorRef.state.propagateLink (true, i);
            // Refresh UI on all linked drum rows
            int grp = processorRef.state.drumTracks[static_cast<size_t>(i)].linkGroup;
            if (grp > 0)
                for (int j = 0; j < 10; ++j)
                    if (j != i && processorRef.state.drumTracks[static_cast<size_t>(j)].linkGroup == grp)
                        drumRows[j]->refreshLinkedSteps();
        };
        row->onStepSync = [this, i]() {
            processorRef.state.propagateLinkSteps (true, i);
            int grp = processorRef.state.drumTracks[static_cast<size_t>(i)].linkGroup;
            if (grp > 0)
                for (int j = 0; j < 10; ++j)
                    if (j != i && processorRef.state.drumTracks[static_cast<size_t>(j)].linkGroup == grp)
                        drumRows[j]->refreshLinkedSteps();
        };
        addAndMakeVisible (row);
    }

    // Synth section
    addAndMakeVisible (synthSectionLabel);
    synthSectionLabel.setText ("SYNTH PARTS", juce::dontSendNotification);
    synthSectionLabel.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
    synthSectionLabel.setColour (juce::Label::textColourId, Colours_GB::textDim);

    for (int i = 0; i < 5; ++i)
    {
        auto* row = new SynthTrackRow (i, processorRef.state.synthTracks[static_cast<size_t>(i)]);
        row->onExpandToggle = [this]() {
            setSize (getWidth(), getDesiredHeight());
        };
        row->setMotionRecPointers (&processorRef.state.motionRec,
                                    &processorRef.state.synthCurrentStep[static_cast<size_t>(i)],
                                    &processorRef.state.motionRecMode);
        row->setResamplePointers (&processorRef.state.resampleSrc,
                                   &processorRef.state.resampleActive,
                                   &processorRef.state.resampleTarget,
                                   &processorRef.state.resampleLength,
                                   &processorRef.state.resampleTransportSync,
                                   &processorRef.state.resampleArmed,
                                   processorRef.getSampleRate());
        row->setMacroEngine (&processorRef.state.macroEngine);
        synthRows.add (row);
        row->onBeforeEdit = [this]() { processorRef.state.pushUndo(); };
        row->onLinkSync = [this, i]() {
            processorRef.state.propagateLink (false, i);
            int grp = processorRef.state.synthTracks[static_cast<size_t>(i)].linkGroup;
            if (grp > 0)
                for (int j = 0; j < 5; ++j)
                    if (j != i && processorRef.state.synthTracks[static_cast<size_t>(j)].linkGroup == grp)
                        synthRows[j]->refreshLinkedSteps();
        };
        row->onStepSync = [this, i]() {
            processorRef.state.propagateLinkSteps (false, i);
            int grp = processorRef.state.synthTracks[static_cast<size_t>(i)].linkGroup;
            if (grp > 0)
                for (int j = 0; j < 5; ++j)
                    if (j != i && processorRef.state.synthTracks[static_cast<size_t>(j)].linkGroup == grp)
                        synthRows[j]->refreshLinkedSteps();
        };
        addAndMakeVisible (row);
    }
}

int MainContent::getDesiredHeight() const
{
    int h = 16 + 2; // drum label + gap (header is outside viewport now)

    for (auto* row : drumRows)
        h += row->getDesiredHeight() + 2;

    h += 8 + 16 + 2; // gap + synth label + gap

    for (auto* row : synthRows)
        h += row->getDesiredHeight() + 2;

    h += 20; // bottom padding
    return h;
}

void MainContent::resized()
{
    auto bounds = getLocalBounds().reduced (12, 4);

    // Drum section (headerBar is reparented to editor — not here)
    drumSectionLabel.setBounds (bounds.removeFromTop (16).withTrimmedLeft (8));
    bounds.removeFromTop (2);

    for (auto* row : drumRows)
    {
        int rowH = row->getDesiredHeight();
        row->setBounds (bounds.removeFromTop (rowH));
        bounds.removeFromTop (2);
    }

    bounds.removeFromTop (8);

    // Synth section
    synthSectionLabel.setBounds (bounds.removeFromTop (16).withTrimmedLeft (8));
    bounds.removeFromTop (2);

    for (auto* row : synthRows)
    {
        int rowH = row->getDesiredHeight();
        row->setBounds (bounds.removeFromTop (rowH));
        bounds.removeFromTop (2);
    }
}

void MainContent::paint (juce::Graphics& g)
{
    g.fillAll (Colours_GB::panel);

    // Drum accent bar
    auto dl = drumSectionLabel.getBounds();
    g.setColour (Colours_GB::accent);
    g.fillRect (dl.getX() - 11, dl.getY(), 3, dl.getHeight());

    // Synth accent bar
    auto sl = synthSectionLabel.getBounds();
    g.setColour (Colours_GB::blue);
    g.fillRect (sl.getX() - 11, sl.getY(), 3, sl.getHeight());
}

// ═══════════════════════════════════════
// GrooveBoxEditor — outer frame with Viewport
// ═══════════════════════════════════════
GrooveBoxEditor::GrooveBoxEditor (GrooveBoxProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), content (p), masterFX (p.state)
{
    setLookAndFeel (&lookAndFeel);

    // Reparent headerBar from content to editor — sits ABOVE viewport, never scrolls
    addAndMakeVisible (content.headerBar);
    addAndMakeVisible (masterFX);

    // Wire preset/init callback so MasterFXRow knobs refresh after load
    content.onPresetLoaded = [this]() {
        masterFX.buildKnobs();
        masterFX.resized();
        masterFX.repaint();
        mixKnob.setValue (static_cast<double>(processorRef.state.masterVolume.load()), juce::dontSendNotification);
    };

    // ── ZOOM ──
    content.headerBar.onZoomChange = [this](float factor) {
        zoomFactor = factor;
        setTransform (juce::AffineTransform::scale (factor));
        auto* peer = getPeer();
        if (peer != nullptr)
        {
            int w = static_cast<int>(baseWidth * factor);
            int h = static_cast<int>(baseHeight * factor);
            setSize (baseWidth, baseHeight); // logical size stays the same
        }
    };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    // MIX volume knob — positioned in meter area (bottom right)
    addAndMakeVisible (mixKnob);
    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setRange (0.0, 1.5, 0.01);
    mixKnob.setValue (static_cast<double>(processorRef.state.masterVolume.load()), juce::dontSendNotification);
    mixKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    mixKnob.setColour (juce::Slider::thumbColourId, Colours_GB::accent);
    mixKnob.setColour (juce::Slider::rotarySliderFillColourId, Colours_GB::accent);
    mixKnob.setColour (juce::Slider::rotarySliderOutlineColourId, Colours_GB::knobTrack);
    mixKnob.onValueChange = [this]() {
        processorRef.state.masterVolume.store (static_cast<float>(mixKnob.getValue()));
    };

    setSize (1300, 750);
    setResizable (true, true);
    setResizeLimits (900, 400, 2000, 1400);

    startTimerHz (30);
}

GrooveBoxEditor::~GrooveBoxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GrooveBoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours_GB::bg);

    auto panelBounds = getLocalBounds().reduced (6);
    g.setColour (Colours_GB::border);
    g.drawRoundedRectangle (panelBounds.toFloat(), 10.0f, 1.0f);
}

void GrooveBoxEditor::paintOverChildren (juce::Graphics& g)
{
    // ── METERS + MIX label (painted OVER child components) ──
    if (meterExtBounds.getWidth() > 10 && meterExtBounds.getHeight() > 10)
    {
        auto mp = meterExtBounds.toFloat();
        float L = mp.getX();

        // Layout in 130px: [4][28 knob][14][10 VU L][4][10 VU R][14][10 GR C][4][10 GR L][6]
        //                    4   32     46   56     60  70       84   94      98 108      118
        //                                                                          → 130 total

        // "MIX" label above knob
        g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 8.0f, juce::Font::bold));
        g.setColour (Colours_GB::accent.withAlpha (0.8f));
        g.drawText ("MIX", static_cast<int>(L + 2), static_cast<int>(mp.getY() + 1),
                    34, 10, juce::Justification::centred);

        // dB readout below knob
        float maxPk = std::max (meterPeakL, meterPeakR);
        float db = (maxPk > 1e-6f) ? 20.0f * std::log10 (maxPk) : -60.0f;
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
        g.setColour (db > -1.0f ? juce::Colour (0xffff4040) : juce::Colour (0xff909090));
        juce::String dbStr = (db > -50.0f) ? juce::String (db, 1) : "-inf";
        g.drawText (dbStr, static_cast<int>(L + 2), static_cast<int>(mp.getBottom() - 11),
                    34, 10, juce::Justification::centred);

        // ── Meters — LEFT-aligned positions in 130px ──
        float my = mp.getY() + 2;
        float mh = mp.getHeight() - 14;
        float vuBarW = 12.0f;
        float grBarW = 12.0f;

        float mx = L + 50;  // VU L start

        auto dbNorm = [](float linear) -> float {
            if (linear < 1e-6f) return 0.0f;
            float db = 20.0f * std::log10 (linear);
            return std::clamp ((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        };
        auto meterColorAt = [](float normPos) -> juce::Colour {
            if (normPos > 0.92f) return juce::Colour (0xffdd3030);
            if (normPos > 0.75f) return juce::Colour (0xffc0a020);
            return Colours_GB::accent.withAlpha (0.8f);
        };

        float barW = vuBarW;
        auto drawVBar = [&](float bx, float normPk, float normRms)
        {
            g.setColour (juce::Colour (0xff080a10));
            g.fillRect (bx, my, barW, mh);
            float segH = 2.0f, segGap = 1.0f;
            int numSegs = static_cast<int>(mh / (segH + segGap));
            for (int si = 0; si < numSegs; ++si)
            {
                float segNorm = static_cast<float>(si) / static_cast<float>(numSegs);
                float segY = my + mh - static_cast<float>(si) * (segH + segGap) - segH;
                if (segY < my) break;
                if (segNorm < normRms)
                {
                    g.setColour (meterColorAt (segNorm).withAlpha (0.9f));
                    g.fillRect (bx, segY, barW, segH);
                }
                else if (segNorm < normPk)
                {
                    g.setColour (meterColorAt (segNorm).withAlpha (0.2f));
                    g.fillRect (bx, segY, barW, segH);
                }
            }
            if (normPk > 0.01f)
            {
                float pkY = my + mh * (1.0f - normPk);
                g.setColour (meterColorAt (normPk));
                g.fillRect (bx, std::max (my, pkY), barW, 1.5f);
            }
        };
        drawVBar (mx, dbNorm (meterPeakL), dbNorm (meterRmsL));
        drawVBar (mx + barW + 4.0f, dbNorm (meterPeakR), dbNorm (meterRmsR));

        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 6.0f, juce::Font::bold));
        g.setColour (juce::Colour (0x60ffffff));
        g.drawText ("L", static_cast<int>(mx), static_cast<int>(my + mh + 1), static_cast<int>(barW), 8, juce::Justification::centred);
        g.drawText ("R", static_cast<int>(mx + barW + 4), static_cast<int>(my + mh + 1), static_cast<int>(barW), 8, juce::Justification::centred);

        // GR meters
        float grX = L + 92;
        auto drawGR = [&](float gx, float grVal, const juce::String& label, juce::Colour col)
        {
            g.setColour (juce::Colour (0xff080a10));
            g.fillRect (gx, my, grBarW, mh);
            float grNorm = std::clamp (grVal / 12.0f, 0.0f, 1.0f);
            if (grNorm > 0.005f)
            {
                g.setColour (col.withAlpha (0.8f));
                g.fillRect (gx, my, grBarW, grNorm * mh);
            }
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 6.0f, juce::Font::bold));
            g.setColour (col.withAlpha (0.6f));
            g.drawText (label, static_cast<int>(gx), static_cast<int>(my + mh + 1), static_cast<int>(grBarW), 8, juce::Justification::centred);
        };
        drawGR (grX, meterCompGR, "C", juce::Colour (0xff4090e0));
        drawGR (grX + grBarW + 4, meterLimGR, "L", juce::Colour (0xffdd5040));
    }
}

void GrooveBoxEditor::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    // HeaderBar — fixed at top, 80px (full width including MIX OUT panel)
    content.headerBar.setBounds (bounds.removeFromTop (80));
    bounds.removeFromTop (2);

    // MasterFX — FULL WIDTH (no carve = no black box)
    int mfxH = masterFX.getDesiredHeight();
    auto mfxRow = bounds.removeFromTop (mfxH);
    masterFX.setBounds (mfxRow);
    // Meter overlay — matches MasterFXRow's mixOutExtArea (130px right + 4px gap)
    int meterPanelW = 130;
    meterExtBounds = juce::Rectangle<int>(mfxRow.getRight() - meterPanelW - 4, mfxRow.getY() + 2, meterPanelW, mfxH - 4);
    // MIX knob — left side of 130px panel
    int knobSize = 28;
    int knobX = meterExtBounds.getX() + 4;
    int knobY = meterExtBounds.getY() + (meterExtBounds.getHeight() - knobSize) / 2 - 2;
    mixKnob.setBounds (knobX, knobY, knobSize, knobSize);
    bounds.removeFromTop (2);

    // Viewport — everything below header scrolls
    viewport.setBounds (bounds);

    int contentW = viewport.getMaximumVisibleWidth();
    int contentH = std::max (content.getDesiredHeight(), viewport.getMaximumVisibleHeight());
    content.setSize (contentW, contentH);
}

void GrooveBoxEditor::timerCallback()
{
    constexpr float decay = 0.85f;

    for (int i = 0; i < 10; ++i)
    {
        int step = processorRef.getDrumPlayStep (i);
        content.drumRows[i]->setPlayingStep (step);

        // Level meter with decay
        float lv = processorRef.drumLevels[static_cast<size_t>(i)].load();
        content.drumRows[i]->setLevel (lv);
        processorRef.drumLevels[static_cast<size_t>(i)].store (lv * decay);

        // MSEG playhead animation (pass all 3 phases)
        content.drumRows[i]->setMsegPlayhead (
            processorRef.state.drumMsegPhase[static_cast<size_t>(i)][0].load(),
            processorRef.state.drumMsegPhase[static_cast<size_t>(i)][1].load(),
            processorRef.state.drumMsegPhase[static_cast<size_t>(i)][2].load());
        content.drumRows[i]->syncMuteSolo();
    }

    for (int i = 0; i < 5; ++i)
    {
        int step = processorRef.sequencer.getSynthPlayingStep (i);
        content.synthRows[i]->setPlayingStep (step);

        float lv = processorRef.synthLevels[static_cast<size_t>(i)].load();
        content.synthRows[i]->setLevel (lv);
        processorRef.synthLevels[static_cast<size_t>(i)].store (lv * decay);

        // LFO activity readback
        content.synthRows[i]->setLfoValues (
            processorRef.state.synthLfoValues[static_cast<size_t>(i)][0].load(),
            processorRef.state.synthLfoValues[static_cast<size_t>(i)][1].load(),
            processorRef.state.synthLfoValues[static_cast<size_t>(i)][2].load());

        // MSEG playhead animation (pass all 3 phases)
        float synthCross[3][2] = {
            { processorRef.state.synthMsegCross[static_cast<size_t>(i)][0][0].load(),
              processorRef.state.synthMsegCross[static_cast<size_t>(i)][0][1].load() },
            { processorRef.state.synthMsegCross[static_cast<size_t>(i)][1][0].load(),
              processorRef.state.synthMsegCross[static_cast<size_t>(i)][1][1].load() },
            { processorRef.state.synthMsegCross[static_cast<size_t>(i)][2][0].load(),
              processorRef.state.synthMsegCross[static_cast<size_t>(i)][2][1].load() }
        };
        content.synthRows[i]->setMsegPlayhead (
            processorRef.state.synthMsegPhase[static_cast<size_t>(i)][0].load(),
            processorRef.state.synthMsegPhase[static_cast<size_t>(i)][1].load(),
            processorRef.state.synthMsegPhase[static_cast<size_t>(i)][2].load(),
            processorRef.state.synthMsegAux[static_cast<size_t>(i)][0].load(),
            processorRef.state.synthMsegAux[static_cast<size_t>(i)][1].load(),
            processorRef.state.synthMsegAux[static_cast<size_t>(i)][2].load(),
            synthCross);
        content.synthRows[i]->syncMuteSolo();
        content.synthRows[i]->syncResampleBtn();
    }

    // Always refresh BPM display (works for both INT and EXT clock modes)
    content.headerBar.setBPMDisplay (processorRef.state.bpm.load());

    content.headerBar.setPlayState (processorRef.state.playing.load());
    content.headerBar.setGlobalSoloActive (processorRef.state.anySolo());

    // Master VU meter (HeaderBar + extension panel)
    meterPeakL = processorRef.state.peakL.load();
    meterPeakR = processorRef.state.peakR.load();
    meterRmsL  = processorRef.state.rmsL.load();
    meterRmsR  = processorRef.state.rmsR.load();
    meterCompGR = processorRef.state.compGR.load();
    meterLimGR  = processorRef.state.limGR.load();
    content.headerBar.setPeakLevels (meterPeakL, meterPeakR, meterRmsL, meterRmsR);
    content.headerBar.setGRLevels (meterCompGR, meterLimGR);
    // Sync MIX knob with state (for preset loads etc.)
    float curMasterVol = processorRef.state.masterVolume.load();
    if (std::abs (static_cast<float>(mixKnob.getValue()) - curMasterVol) > 0.001f)
        mixKnob.setValue (static_cast<double>(curMasterVol), juce::dontSendNotification);
    if (!meterExtBounds.isEmpty())
        repaint (meterExtBounds);
    masterFX.setPlayingStep (processorRef.state.masterFXStep.load());
    masterFX.repaint();

    // ── Resample auto-stop & finalize ──
    if (processorRef.state.resampleReady.load())
    {
        processorRef.state.resampleReady.store (false);
        int target = processorRef.state.resampleTarget.load();
        int len = processorRef.state.resampleLength.load();
        if (target >= 0 && target < 5 && len > 64)
        {
            auto& st = processorRef.state.synthTracks[static_cast<size_t>(target)];
            auto newBuf = std::make_shared<juce::AudioBuffer<float>> (1, len);
            juce::FloatVectorOperations::copy (newBuf->getWritePointer (0),
                processorRef.resampleBuf.data(), len);
            st.sampleData = newBuf;
            st.samplePath = "[resampled]";

            // Auto-detect BPM + bars + root note (unified detection)
            auto analysis = SampleAnalysis::analyze (*newBuf, processorRef.getSampleRate());
            if (analysis.isLoop && analysis.bpm > 0.0f)
            {
                st.smpBPM = analysis.bpm;
                st.smpBars = analysis.barIndex;
                st.smpBpmSync = 1;
                if (st.smpWarp == 0) st.smpWarp = 1;
            }
            if (analysis.rootNote >= 0)
                st.smpRootNote = analysis.rootNote;

            // Rebuild GUI
            for (auto* row : content.synthRows)
                row->rebuildKnobs();
        }
    }
}
