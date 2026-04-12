#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "GrooveBoxState.h"
#include <vector>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// PresetManager — Handles global, drum-engine, and synth-engine presets
//
// File structure:
//   ~/Documents/Phonica School/GrooveBox Phonica/Presets/
//     Global/
//       <user folders>/
//       preset.xml
//     Drum/
//       <user folders>/
//       preset.xml
//     Synth/
//       <user folders>/
//       preset.xml
// ═══════════════════════════════════════════════════════════════════

class PresetManager
{
public:
    enum class PresetType { Global, Drum, Synth };

    PresetManager() = default;

    // ── Directory helpers ──
    static juce::File getPresetsRoot()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Phonica School")
            .getChildFile ("GrooveBox Phonica")
            .getChildFile ("Presets");
    }

    static juce::File getTypeDir (PresetType type)
    {
        switch (type)
        {
            case PresetType::Global: return getPresetsRoot().getChildFile ("Global");
            case PresetType::Drum:   return getPresetsRoot().getChildFile ("Drum");
            case PresetType::Synth:  return getPresetsRoot().getChildFile ("Synth");
        }
        return getPresetsRoot();
    }

    static void ensureDirs()
    {
        getTypeDir (PresetType::Global).createDirectory();
        getTypeDir (PresetType::Drum).createDirectory();
        getTypeDir (PresetType::Synth).createDirectory();
    }

    // ═══════════════════════════════════════
    // GLOBAL PRESET — full plugin state
    // ═══════════════════════════════════════
    static bool saveGlobal (const GrooveBoxState& state, const juce::String& name,
                            const juce::String& folder = "")
    {
        ensureDirs();
        auto dir = getTypeDir (PresetType::Global);
        if (folder.isNotEmpty())
        {
            dir = dir.getChildFile (folder);
            dir.createDirectory();
        }
        auto file = dir.getChildFile (name + ".xml");

        juce::XmlElement root ("GrooveBoxPreset");
        root.setAttribute ("version", 1);
        root.setAttribute ("type", "global");
        root.setAttribute ("name", name);
        state.saveToXml (root);
        return root.writeTo (file);
    }

    static bool loadGlobal (GrooveBoxState& state, const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (!xml || xml->getTagName() != "GrooveBoxPreset") return false;
        state.loadFromXml (*xml);
        return true;
    }

    // ═══════════════════════════════════════
    // DRUM ENGINE PRESET — single drum track
    // ═══════════════════════════════════════
    static bool saveDrumEngine (const DrumTrackState& dt, const juce::String& name,
                                const juce::String& folder = "")
    {
        ensureDirs();
        auto dir = getTypeDir (PresetType::Drum);
        if (folder.isNotEmpty()) { dir = dir.getChildFile (folder); dir.createDirectory(); }
        auto file = dir.getChildFile (name + ".xml");

        juce::XmlElement root ("DrumEnginePreset");
        root.setAttribute ("version", 1);
        root.setAttribute ("name", name);
        root.setAttribute ("drumType", drumId (dt.type));
        saveDrumParams (root, dt);
        return root.writeTo (file);
    }

    static bool loadDrumEngine (DrumTrackState& dt, const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (!xml || xml->getTagName() != "DrumEnginePreset") return false;
        loadDrumParams (*xml, dt);
        return true;
    }

    // ═══════════════════════════════════════
    // SYNTH ENGINE PRESET — single synth track
    // ═══════════════════════════════════════
    static bool saveSynthEngine (const SynthTrackState& st, const juce::String& name,
                                 const juce::String& folder = "")
    {
        ensureDirs();
        auto dir = getTypeDir (PresetType::Synth);
        if (folder.isNotEmpty()) { dir = dir.getChildFile (folder); dir.createDirectory(); }
        auto file = dir.getChildFile (name + ".xml");

        juce::XmlElement root ("SynthEnginePreset");
        root.setAttribute ("version", 1);
        root.setAttribute ("name", name);
        root.setAttribute ("model", static_cast<int>(st.model));
        saveSynthParams (root, st);
        return root.writeTo (file);
    }

    static bool loadSynthEngine (SynthTrackState& st, const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (!xml || xml->getTagName() != "SynthEnginePreset") return false;
        loadSynthParams (*xml, st);
        return true;
    }

    // ═══════════════════════════════════════
    // RENAME / DELETE
    // ═══════════════════════════════════════
    static bool renamePreset (const juce::File& file, const juce::String& newName)
    {
        auto newFile = file.getParentDirectory().getChildFile (newName + ".xml");
        if (newFile.exists()) return false;
        return file.moveFileTo (newFile);
    }

    static bool deletePreset (const juce::File& file)
    {
        return file.deleteFile();
    }

    // ═══════════════════════════════════════
    // FOLDER MANAGEMENT
    // ═══════════════════════════════════════
    static bool createFolder (PresetType type, const juce::String& folderName)
    {
        auto dir = getTypeDir (type).getChildFile (folderName);
        return dir.createDirectory();
    }

    static bool deleteFolder (const juce::File& folder)
    {
        return folder.deleteRecursively();
    }

    // ═══════════════════════════════════════
    // LISTING — presets + folders
    // ═══════════════════════════════════════
    struct PresetEntry
    {
        juce::String name;
        juce::File   file;
        bool         isFolder = false;
    };

    static std::vector<PresetEntry> listPresets (PresetType type, const juce::String& subfolder = "")
    {
        ensureDirs();
        auto dir = getTypeDir (type);
        if (subfolder.isNotEmpty())
            dir = dir.getChildFile (subfolder);

        std::vector<PresetEntry> result;

        // Add folders first
        for (auto& child : dir.findChildFiles (juce::File::findDirectories, false))
            result.push_back ({ child.getFileName(), child, true });

        // Then XML files
        for (auto& child : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
            result.push_back ({ child.getFileNameWithoutExtension(), child, false });

        // Sort alphabetically
        std::sort (result.begin(), result.end(),
                   [](const PresetEntry& a, const PresetEntry& b)
                   {
                       if (a.isFolder != b.isFolder) return a.isFolder;
                       return a.name.compareIgnoreCase (b.name) < 0;
                   });
        return result;
    }

    // ═══════════════════════════════════════
    // POPUP MENU BUILDERS
    // ═══════════════════════════════════════
    static void showSaveDialog (PresetType type, const juce::String& defaultName,
                                std::function<void(juce::String name, juce::String folder)> onSave)
    {
        auto ae = std::make_shared<juce::AlertWindow> (
            "Save Preset", "Enter preset name:", juce::MessageBoxIconType::NoIcon);
        ae->addTextEditor ("name", defaultName, "Name:");
        ae->addButton ("Save", 1);
        ae->addButton ("Cancel", 0);

        // Folder combo
        auto folders = listFolders (type);
        juce::StringArray folderNames;
        folderNames.add ("[Root]");
        for (auto& f : folders) folderNames.add (f);
        ae->addComboBox ("folder", folderNames, "Folder:");
        ae->setAlwaysOnTop (true);

        ae->enterModalState (true, juce::ModalCallbackFunction::create (
            [ae, onSave, type](int result)
            {
                if (result == 1)
                {
                    auto name = ae->getTextEditorContents ("name").trim();
                    auto folderIdx = ae->getComboBoxComponent ("folder")->getSelectedItemIndex();
                    juce::String folder;
                    if (folderIdx > 0)
                    {
                        auto folders2 = listFolders (type);
                        if (folderIdx - 1 < static_cast<int>(folders2.size()))
                            folder = folders2[static_cast<size_t>(folderIdx - 1)];
                    }
                    if (name.isNotEmpty() && onSave)
                    {
                        // Check if file already exists → ask to overwrite
                        auto dir = getTypeDir (type);
                        if (folder.isNotEmpty())
                            dir = dir.getChildFile (folder);
                        auto file = dir.getChildFile (name + ".xml");
                        if (file.existsAsFile())
                        {
                            auto confirm = std::make_shared<juce::AlertWindow> (
                                "Overwrite?",
                                "Preset \"" + name + "\" already exists.\nDo you want to overwrite it?",
                                juce::MessageBoxIconType::WarningIcon);
                            confirm->addButton ("Overwrite", 1);
                            confirm->addButton ("Cancel", 0);
                            confirm->setAlwaysOnTop (true);
                            confirm->enterModalState (true, juce::ModalCallbackFunction::create (
                                [confirm, onSave, name, folder](int r)
                                {
                                    if (r == 1 && onSave)
                                        onSave (name, folder);
                                }), false);
                        }
                        else
                        {
                            onSave (name, folder);
                        }
                    }
                }
            }), false);
    }

    static void showNewFolderDialog (PresetType type, std::function<void()> onDone = nullptr)
    {
        auto ae = std::make_shared<juce::AlertWindow> (
            "New Folder", "Enter folder name:", juce::MessageBoxIconType::NoIcon);
        ae->addTextEditor ("name", "", "Folder:");
        ae->addButton ("Create", 1);
        ae->addButton ("Cancel", 0);
        ae->enterModalState (true, juce::ModalCallbackFunction::create (
            [ae, type, onDone](int result)
            {
                if (result == 1)
                {
                    auto name = ae->getTextEditorContents ("name").trim();
                    if (name.isNotEmpty())
                    {
                        createFolder (type, name);
                        if (onDone) onDone();
                    }
                }
            }), false);  // false = shared_ptr owns lifetime
    }

    static juce::PopupMenu buildBrowseMenu (PresetType type, const juce::String& subfolder,
                                             int baseId, std::vector<juce::File>& outFiles)
    {
        int counter = baseId;
        return buildBrowseMenuInternal (type, subfolder, counter, outFiles);
    }

    static juce::PopupMenu buildFolderDeleteMenu (PresetType type, int baseId,
                                                    std::vector<juce::File>& outFolders)
    {
        juce::PopupMenu menu;
        auto dir = getTypeDir (type);
        int id = baseId;
        for (auto& child : dir.findChildFiles (juce::File::findDirectories, false))
        {
            outFolders.push_back (child);
            menu.addItem (id++, child.getFileName());
        }
        return menu;
    }

private:
    static juce::PopupMenu buildBrowseMenuInternal (PresetType type, const juce::String& subfolder,
                                                     int& counter, std::vector<juce::File>& outFiles)
    {
        juce::PopupMenu menu;
        auto entries = listPresets (type, subfolder);
        for (auto& e : entries)
        {
            if (e.isFolder)
            {
                auto sub = buildBrowseMenuInternal (type, e.name, counter, outFiles);
                menu.addSubMenu (e.name, sub);
            }
            else
            {
                outFiles.push_back (e.file);
                menu.addItem (counter++, e.name);
            }
        }
        return menu;
    }

    static std::vector<juce::String> listFolders (PresetType type)
    {
        std::vector<juce::String> result;
        auto dir = getTypeDir (type);
        for (auto& child : dir.findChildFiles (juce::File::findDirectories, false))
            result.push_back (child.getFileName());
        std::sort (result.begin(), result.end());
        return result;
    }

    // ═══════════════════════════════════════
    // DRUM PARAM SERIALIZATION (ALL fields)
    // ═══════════════════════════════════════
    static void saveDrumParams (juce::XmlElement& el, const DrumTrackState& dt)
    {
        // Engine
        el.setAttribute ("pitch", (double) dt.pitch);
        el.setAttribute ("pitchDec", (double) dt.pitchDec);
        el.setAttribute ("decay", (double) dt.decay);
        el.setAttribute ("click", (double) dt.click);
        el.setAttribute ("tune", (double) dt.tune);
        el.setAttribute ("tone", (double) dt.tone);
        el.setAttribute ("toneDecay", (double) dt.toneDecay);
        el.setAttribute ("noiseDecay", (double) dt.noiseDecay);
        el.setAttribute ("snap", (double) dt.snap);
        el.setAttribute ("cutoff", (double) dt.cutoff);
        el.setAttribute ("freq", (double) dt.freq);
        el.setAttribute ("freq1", (double) dt.freq1);
        el.setAttribute ("freq2", (double) dt.freq2);
        el.setAttribute ("spread", (double) dt.spread);
        el.setAttribute ("noise", (double) dt.noise);
        el.setAttribute ("pitchEnd", (double) dt.pitchEnd);
        // FM
        el.setAttribute ("fmMix", (double) dt.fmMix);
        el.setAttribute ("fmRatio", (double) dt.fmRatio);
        el.setAttribute ("fmDepth", (double) dt.fmDepth);
        el.setAttribute ("fmDecay", (double) dt.fmDecay);
        el.setAttribute ("fmNoise", (double) dt.fmNoise);
        el.setAttribute ("fmNoiseType", dt.fmNoiseType);
        // FX
        el.setAttribute ("distAmt", (double) dt.distAmt);
        el.setAttribute ("reduxBits", (double) dt.reduxBits);
        el.setAttribute ("reduxRate", (double) dt.reduxRate);
        el.setAttribute ("chorusDepth", (double) dt.chorusDepth);
        el.setAttribute ("chorusRate", (double) dt.chorusRate);
        el.setAttribute ("chorusMix", (double) dt.chorusMix);
        el.setAttribute ("delayMix", (double) dt.delayMix);
        el.setAttribute ("delayTime", (double) dt.delayTime);
        el.setAttribute ("delayFB", (double) dt.delayFB);
        el.setAttribute ("delaySync", (int) dt.delaySync);
        el.setAttribute ("delayBeats", (double) dt.delayBeats);
        el.setAttribute ("delayPP", dt.delayPP);
        el.setAttribute ("delayDamp", (double) dt.delayDamp);
        el.setAttribute ("reverbMix", (double) dt.reverbMix);
        el.setAttribute ("reverbSize", (double) dt.reverbSize);
        el.setAttribute ("reverbDamp", (double) dt.reverbDamp);
        // Filter + pan + duck
        el.setAttribute ("drumCut", (double) dt.drumCut);
        el.setAttribute ("drumRes", (double) dt.drumRes);
        el.setAttribute ("drumFiltEnv", (double) dt.drumFiltEnv);
        el.setAttribute ("drumFiltA", (double) dt.drumFiltA);
        el.setAttribute ("drumFiltD", (double) dt.drumFiltD);
        el.setAttribute ("fxLP", (double) dt.fxLP);
        el.setAttribute ("fxHP", (double) dt.fxHP);
        el.setAttribute ("eqLow", (double) dt.eqLow);
        el.setAttribute ("eqMid", (double) dt.eqMid);
        el.setAttribute ("eqHigh", (double) dt.eqHigh);
        el.setAttribute ("pan", (double) dt.pan);
        el.setAttribute ("duckSrc", dt.duckSrc);
        el.setAttribute ("duckDepth", (double) dt.duckDepth);
        el.setAttribute ("duckAtk", (double) dt.duckAtk);
        el.setAttribute ("duckRel", (double) dt.duckRel);
        // Pro Distortion
        el.setAttribute ("proDistModel", dt.proDistModel);
        el.setAttribute ("proDistDrive", (double) dt.proDistDrive);
        el.setAttribute ("proDistTone", (double) dt.proDistTone);
        el.setAttribute ("proDistMix", (double) dt.proDistMix);
        el.setAttribute ("proDistBias", (double) dt.proDistBias);
        el.setAttribute ("chokeGroup", dt.chokeGroup);
        el.setAttribute ("ottDepth", (double) dt.ottDepth);
        el.setAttribute ("ottUpward", (double) dt.ottUpward);
        el.setAttribute ("ottDownward", (double) dt.ottDownward);
        el.setAttribute ("phaserMix", (double) dt.phaserMix);
        el.setAttribute ("phaserRate", (double) dt.phaserRate);
        el.setAttribute ("phaserDepth", (double) dt.phaserDepth);
        el.setAttribute ("phaserFB", (double) dt.phaserFB);
        el.setAttribute ("flangerMix", (double) dt.flangerMix);
        el.setAttribute ("flangerRate", (double) dt.flangerRate);
        el.setAttribute ("flangerDepth", (double) dt.flangerDepth);
        el.setAttribute ("flangerFB", (double) dt.flangerFB);
        // Volume
        el.setAttribute ("volume", (double) dt.volume);
        // Drum engine mode
        el.setAttribute ("drumEngine", dt.drumEngine);
        el.setAttribute ("subModel", dt.subModel);
        // ER-1 params
        el.setAttribute ("er1Wave1", dt.er1Wave1); el.setAttribute ("er1Pitch1", (double) dt.er1Pitch1);
        el.setAttribute ("er1PDec1", (double) dt.er1PDec1); el.setAttribute ("er1Wave2", dt.er1Wave2);
        el.setAttribute ("er1Pitch2", (double) dt.er1Pitch2); el.setAttribute ("er1PDec2", (double) dt.er1PDec2);
        el.setAttribute ("er1Ring", (double) dt.er1Ring); el.setAttribute ("er1XMod", (double) dt.er1XMod);
        el.setAttribute ("er1Noise", (double) dt.er1Noise); el.setAttribute ("er1NDec", (double) dt.er1NDec);
        el.setAttribute ("er1Cut", (double) dt.er1Cut); el.setAttribute ("er1Res", (double) dt.er1Res);
        el.setAttribute ("er1Decay", (double) dt.er1Decay); el.setAttribute ("er1Drive", (double) dt.er1Drive);
        // Sampler
        el.setAttribute ("smpStart", (double) dt.smpStart);
        el.setAttribute ("smpEnd", (double) dt.smpEnd);
        el.setAttribute ("smpGain", (double) dt.smpGain);
        el.setAttribute ("smpLoop", dt.smpLoop);
        el.setAttribute ("smpPlayMode", dt.smpPlayMode);
        el.setAttribute ("smpReverse", dt.smpReverse);
        el.setAttribute ("smpTune", (double) dt.smpTune);
        el.setAttribute ("smpFine", (double) dt.smpFine);
        el.setAttribute ("smpCut", (double) dt.smpCut);
        el.setAttribute ("smpRes", (double) dt.smpRes);
        el.setAttribute ("smpFType", dt.smpFType);
        el.setAttribute ("smpFModel", dt.smpFModel);
        el.setAttribute ("smpFPoles", dt.smpFPoles);
        el.setAttribute ("smpFiltEnv", (double) dt.smpFiltEnv);
        el.setAttribute ("smpFiltA", (double) dt.smpFiltA);
        el.setAttribute ("smpFiltD", (double) dt.smpFiltD);
        el.setAttribute ("smpFiltS", (double) dt.smpFiltS);
        el.setAttribute ("smpFiltR", (double) dt.smpFiltR);
        el.setAttribute ("smpRootNote", dt.smpRootNote);
        el.setAttribute ("smpA", (double) dt.smpA);
        el.setAttribute ("smpD", (double) dt.smpD);
        el.setAttribute ("smpS", (double) dt.smpS);
        el.setAttribute ("smpR", (double) dt.smpR);
        el.setAttribute ("smpStretch", (double) dt.smpStretch);
        el.setAttribute ("smpWarp", dt.smpWarp);
        el.setAttribute ("smpBPM", (double) dt.smpBPM);
        el.setAttribute ("smpFileSR", (double) dt.smpFileSR);
        el.setAttribute ("smpBpmSync", dt.smpBpmSync);
        el.setAttribute ("smpSyncMul", dt.smpSyncMul);
        el.setAttribute ("smpBars", dt.smpBars);
        // Warp markers
        if (!dt.warpMarkers.empty())
        {
            auto* wml = el.createNewChildElement ("WarpMarkers");
            for (const auto& wm : dt.warpMarkers)
            {
                auto* wme = wml->createNewChildElement ("WM");
                wme->setAttribute ("sp", (double) wm.samplePos);
                wme->setAttribute ("bp", (double) wm.beatPos);
                wme->setAttribute ("auto", wm.isAuto ? 1 : 0);
                wme->setAttribute ("osp", (double) wm.originalSamplePos);
            }
        }
        el.setAttribute ("smpFmAmt", (double) dt.smpFmAmt);
        el.setAttribute ("smpFmRatio", (double) dt.smpFmRatio);
        el.setAttribute ("smpFmEnvA", (double) dt.smpFmEnvA);
        el.setAttribute ("smpFmEnvD", (double) dt.smpFmEnvD);
        el.setAttribute ("smpFmEnvS", (double) dt.smpFmEnvS);
        if (dt.samplePath.isNotEmpty())
            el.setAttribute ("samplePath", dt.samplePath);
        // LFOs
        for (int li = 0; li < 3; ++li)
        {
            auto* lel = el.createNewChildElement ("LFO");
            lel->setAttribute ("idx", li);
            lel->setAttribute ("target", dt.lfos[static_cast<size_t>(li)].target);
            lel->setAttribute ("shape", dt.lfos[static_cast<size_t>(li)].shape);
            lel->setAttribute ("rate", (double) dt.lfos[static_cast<size_t>(li)].rate);
            lel->setAttribute ("depth", (double) dt.lfos[static_cast<size_t>(li)].depth);
            lel->setAttribute ("sync", (int) dt.lfos[static_cast<size_t>(li)].sync);
            lel->setAttribute ("syncDiv", (double) dt.lfos[static_cast<size_t>(li)].syncDiv);
            lel->setAttribute ("retrig", (int) dt.lfos[static_cast<size_t>(li)].retrig);
            lel->setAttribute ("hiRate", (int) dt.lfos[static_cast<size_t>(li)].hiRate);
            lel->setAttribute ("fadeIn", (double) dt.lfos[static_cast<size_t>(li)].fadeIn);
            lel->setAttribute ("fadeInSync", (int) dt.lfos[static_cast<size_t>(li)].fadeInSync);
            for (int ri = 0; ri < 16; ++ri)
            {
                auto& r = dt.lfos[static_cast<size_t>(li)].extraRoutes[static_cast<size_t>(ri)];
                if (r.target >= 0)
                {
                    auto* rel = lel->createNewChildElement ("Route");
                    rel->setAttribute ("idx", ri);
                    rel->setAttribute ("tgt", r.target);
                    rel->setAttribute ("dep", (double) r.depth);
                }
            }
        }
        // MSEGs (3 per track)
        for (int mi = 0; mi < 3; ++mi)
            saveMSEGData (el, dt.msegs[static_cast<size_t>(mi)], mi);

        // Velocity & Key tracking routes
        for (int ri = 0; ri < 4; ++ri)
        {
            if (dt.velRoutes[ri].target >= 0) {
                auto* vr = el.createNewChildElement ("VelRoute");
                vr->setAttribute ("idx", ri); vr->setAttribute ("tgt", dt.velRoutes[ri].target);
                vr->setAttribute ("dep", (double) dt.velRoutes[ri].depth);
            }
            if (dt.keyRoutes[ri].target >= 0) {
                auto* kr = el.createNewChildElement ("KeyRoute");
                kr->setAttribute ("idx", ri); kr->setAttribute ("tgt", dt.keyRoutes[ri].target);
                kr->setAttribute ("dep", (double) dt.keyRoutes[ri].depth);
            }
        }
    }

    static void loadDrumParams (const juce::XmlElement& el, DrumTrackState& dt)
    {
        dt.pitch = (float) el.getDoubleAttribute ("pitch", dt.pitch);
        dt.pitchDec = (float) el.getDoubleAttribute ("pitchDec", dt.pitchDec);
        dt.decay = (float) el.getDoubleAttribute ("decay", dt.decay);
        dt.click = (float) el.getDoubleAttribute ("click", dt.click);
        dt.tune = (float) el.getDoubleAttribute ("tune", dt.tune);
        dt.tone = (float) el.getDoubleAttribute ("tone", dt.tone);
        dt.toneDecay = (float) el.getDoubleAttribute ("toneDecay", dt.toneDecay);
        dt.noiseDecay = (float) el.getDoubleAttribute ("noiseDecay", dt.noiseDecay);
        dt.snap = (float) el.getDoubleAttribute ("snap", dt.snap);
        dt.cutoff = (float) el.getDoubleAttribute ("cutoff", dt.cutoff);
        dt.freq = (float) el.getDoubleAttribute ("freq", dt.freq);
        dt.freq1 = (float) el.getDoubleAttribute ("freq1", dt.freq1);
        dt.freq2 = (float) el.getDoubleAttribute ("freq2", dt.freq2);
        dt.spread = (float) el.getDoubleAttribute ("spread", dt.spread);
        dt.noise = (float) el.getDoubleAttribute ("noise", dt.noise);
        dt.pitchEnd = (float) el.getDoubleAttribute ("pitchEnd", dt.pitchEnd);
        dt.fmMix = (float) el.getDoubleAttribute ("fmMix", dt.fmMix);
        dt.fmRatio = (float) el.getDoubleAttribute ("fmRatio", dt.fmRatio);
        dt.fmDepth = (float) el.getDoubleAttribute ("fmDepth", dt.fmDepth);
        dt.fmDecay = (float) el.getDoubleAttribute ("fmDecay", dt.fmDecay);
        dt.fmNoise = (float) el.getDoubleAttribute ("fmNoise", dt.fmNoise);
        dt.fmNoiseType = el.getIntAttribute ("fmNoiseType", dt.fmNoiseType);
        dt.distAmt = (float) el.getDoubleAttribute ("distAmt", dt.distAmt);
        dt.reduxBits = (float) el.getDoubleAttribute ("reduxBits", dt.reduxBits);
        dt.reduxRate = (float) el.getDoubleAttribute ("reduxRate", dt.reduxRate);
        dt.chorusDepth = (float) el.getDoubleAttribute ("chorusDepth", dt.chorusDepth);
        dt.chorusRate = (float) el.getDoubleAttribute ("chorusRate", dt.chorusRate);
        dt.chorusMix = (float) el.getDoubleAttribute ("chorusMix", dt.chorusMix);
        dt.delayMix = (float) el.getDoubleAttribute ("delayMix", dt.delayMix);
        dt.delayTime = (float) el.getDoubleAttribute ("delayTime", dt.delayTime);
        dt.delayFB = (float) el.getDoubleAttribute ("delayFB", dt.delayFB);
        dt.delaySync = el.getBoolAttribute ("delaySync", dt.delaySync);
        dt.delayBeats = (float) el.getDoubleAttribute ("delayBeats", dt.delayBeats);
        dt.delayPP = el.getIntAttribute ("delayPP", dt.delayPP);
        dt.delayDamp = (float) el.getDoubleAttribute ("delayDamp", dt.delayDamp);
        dt.reverbMix = (float) el.getDoubleAttribute ("reverbMix", dt.reverbMix);
        dt.reverbSize = (float) el.getDoubleAttribute ("reverbSize", dt.reverbSize);
        dt.reverbDamp = (float) el.getDoubleAttribute ("reverbDamp", dt.reverbDamp);
        dt.drumCut = (float) el.getDoubleAttribute ("drumCut", dt.drumCut);
        dt.drumRes = (float) el.getDoubleAttribute ("drumRes", dt.drumRes);
        dt.drumFiltEnv = (float) el.getDoubleAttribute ("drumFiltEnv", dt.drumFiltEnv);
        dt.drumFiltA = (float) el.getDoubleAttribute ("drumFiltA", dt.drumFiltA);
        dt.drumFiltD = (float) el.getDoubleAttribute ("drumFiltD", dt.drumFiltD);
        dt.fxLP = (float) el.getDoubleAttribute ("fxLP", dt.fxLP);
        dt.fxHP = (float) el.getDoubleAttribute ("fxHP", dt.fxHP);
        dt.eqLow = (float) el.getDoubleAttribute ("eqLow", dt.eqLow);
        dt.eqMid = (float) el.getDoubleAttribute ("eqMid", dt.eqMid);
        dt.eqHigh = (float) el.getDoubleAttribute ("eqHigh", dt.eqHigh);
        dt.pan = (float) el.getDoubleAttribute ("pan", dt.pan);
        dt.duckSrc = el.getIntAttribute ("duckSrc", dt.duckSrc);
        dt.duckDepth = (float) el.getDoubleAttribute ("duckDepth", dt.duckDepth);
        dt.duckAtk = (float) el.getDoubleAttribute ("duckAtk", dt.duckAtk);
        dt.duckRel = (float) el.getDoubleAttribute ("duckRel", dt.duckRel);
        dt.proDistModel = el.getIntAttribute ("proDistModel", dt.proDistModel);
        dt.proDistDrive = (float) el.getDoubleAttribute ("proDistDrive", dt.proDistDrive);
        dt.proDistTone = (float) el.getDoubleAttribute ("proDistTone", dt.proDistTone);
        dt.proDistMix = (float) el.getDoubleAttribute ("proDistMix", dt.proDistMix);
        dt.proDistBias = (float) el.getDoubleAttribute ("proDistBias", dt.proDistBias);
        dt.chokeGroup = el.getIntAttribute ("chokeGroup", 0);
        dt.ottDepth = (float) el.getDoubleAttribute ("ottDepth", dt.ottDepth);
        dt.ottUpward = (float) el.getDoubleAttribute ("ottUpward", dt.ottUpward);
        dt.ottDownward = (float) el.getDoubleAttribute ("ottDownward", dt.ottDownward);
        dt.phaserMix = (float) el.getDoubleAttribute ("phaserMix", dt.phaserMix);
        dt.phaserRate = (float) el.getDoubleAttribute ("phaserRate", dt.phaserRate);
        dt.phaserDepth = (float) el.getDoubleAttribute ("phaserDepth", dt.phaserDepth);
        dt.phaserFB = (float) el.getDoubleAttribute ("phaserFB", dt.phaserFB);
        dt.flangerMix = (float) el.getDoubleAttribute ("flangerMix", dt.flangerMix);
        dt.flangerRate = (float) el.getDoubleAttribute ("flangerRate", dt.flangerRate);
        dt.flangerDepth = (float) el.getDoubleAttribute ("flangerDepth", dt.flangerDepth);
        dt.flangerFB = (float) el.getDoubleAttribute ("flangerFB", dt.flangerFB);
        dt.volume = (float) el.getDoubleAttribute ("volume", dt.volume);
        dt.drumEngine = el.getIntAttribute ("drumEngine", dt.drumEngine);
        dt.subModel = el.getIntAttribute ("subModel", 0);
        // ER-1 params
        dt.er1Wave1 = el.getIntAttribute ("er1Wave1", dt.er1Wave1);
        dt.er1Pitch1 = (float) el.getDoubleAttribute ("er1Pitch1", dt.er1Pitch1);
        dt.er1PDec1 = (float) el.getDoubleAttribute ("er1PDec1", dt.er1PDec1);
        dt.er1Wave2 = el.getIntAttribute ("er1Wave2", dt.er1Wave2);
        dt.er1Pitch2 = (float) el.getDoubleAttribute ("er1Pitch2", dt.er1Pitch2);
        dt.er1PDec2 = (float) el.getDoubleAttribute ("er1PDec2", dt.er1PDec2);
        dt.er1Ring = (float) el.getDoubleAttribute ("er1Ring", dt.er1Ring);
        dt.er1XMod = (float) el.getDoubleAttribute ("er1XMod", dt.er1XMod);
        dt.er1Noise = (float) el.getDoubleAttribute ("er1Noise", dt.er1Noise);
        dt.er1NDec = (float) el.getDoubleAttribute ("er1NDec", dt.er1NDec);
        dt.er1Cut = (float) el.getDoubleAttribute ("er1Cut", dt.er1Cut);
        dt.er1Res = (float) el.getDoubleAttribute ("er1Res", dt.er1Res);
        dt.er1Decay = (float) el.getDoubleAttribute ("er1Decay", dt.er1Decay);
        dt.er1Drive = (float) el.getDoubleAttribute ("er1Drive", dt.er1Drive);
        // Sampler
        dt.smpStart = (float) el.getDoubleAttribute ("smpStart", dt.smpStart);
        dt.smpEnd = (float) el.getDoubleAttribute ("smpEnd", dt.smpEnd);
        dt.smpGain = (float) el.getDoubleAttribute ("smpGain", dt.smpGain);
        dt.smpLoop = el.getIntAttribute ("smpLoop", dt.smpLoop);
        dt.smpPlayMode = el.getIntAttribute ("smpPlayMode", dt.smpPlayMode);
        dt.smpReverse = el.getIntAttribute ("smpReverse", dt.smpReverse);
        dt.smpTune = (float) el.getDoubleAttribute ("smpTune", dt.smpTune);
        dt.smpFine = (float) el.getDoubleAttribute ("smpFine", dt.smpFine);
        dt.smpCut = (float) el.getDoubleAttribute ("smpCut", dt.smpCut);
        dt.smpRes = (float) el.getDoubleAttribute ("smpRes", dt.smpRes);
        dt.smpFType = el.getIntAttribute ("smpFType", dt.smpFType);
        dt.smpFModel = el.getIntAttribute ("smpFModel", dt.smpFModel);
        dt.smpFPoles = el.getIntAttribute ("smpFPoles", dt.smpFPoles);
        dt.smpFiltEnv = (float) el.getDoubleAttribute ("smpFiltEnv", dt.smpFiltEnv);
        dt.smpFiltA = (float) el.getDoubleAttribute ("smpFiltA", dt.smpFiltA);
        dt.smpFiltD = (float) el.getDoubleAttribute ("smpFiltD", dt.smpFiltD);
        dt.smpFiltS = (float) el.getDoubleAttribute ("smpFiltS", dt.smpFiltS);
        dt.smpFiltR = (float) el.getDoubleAttribute ("smpFiltR", dt.smpFiltR);
        dt.smpRootNote = el.getIntAttribute ("smpRootNote", dt.smpRootNote);
        dt.smpA = (float) el.getDoubleAttribute ("smpA", dt.smpA);
        dt.smpD = (float) el.getDoubleAttribute ("smpD", dt.smpD);
        dt.smpS = (float) el.getDoubleAttribute ("smpS", dt.smpS);
        dt.smpR = (float) el.getDoubleAttribute ("smpR", dt.smpR);
        dt.smpStretch = (float) el.getDoubleAttribute ("smpStretch", dt.smpStretch);
        dt.smpWarp = el.getIntAttribute ("smpWarp", dt.smpWarp);
        dt.smpBPM = (float) el.getDoubleAttribute ("smpBPM", dt.smpBPM);
        dt.smpFileSR = (float) el.getDoubleAttribute ("smpFileSR", dt.smpFileSR);
        dt.smpBpmSync = el.getIntAttribute ("smpBpmSync", dt.smpBpmSync);
        dt.smpSyncMul = el.getIntAttribute ("smpSyncMul", dt.smpSyncMul);
        dt.smpBars = el.getIntAttribute ("smpBars", dt.smpBars);
        // Warp markers
        dt.warpMarkers.clear();
        if (auto* wml = el.getChildByName ("WarpMarkers"))
        {
            for (auto* wme = wml->getChildByName ("WM"); wme; wme = wme->getNextElementWithTagName ("WM"))
            {
                WarpMarker wm;
                wm.samplePos = (float) wme->getDoubleAttribute ("sp", 0.0);
                wm.beatPos = (float) wme->getDoubleAttribute ("bp", 0.0);
                wm.isAuto = wme->getIntAttribute ("auto", 1) != 0;
                wm.originalSamplePos = (float) wme->getDoubleAttribute ("osp", wm.samplePos);
                dt.warpMarkers.push_back (wm);
            }
        }
        dt.smpFmAmt = (float) el.getDoubleAttribute ("smpFmAmt", dt.smpFmAmt);
        dt.smpFmRatio = (float) el.getDoubleAttribute ("smpFmRatio", dt.smpFmRatio);
        dt.smpFmEnvA = (float) el.getDoubleAttribute ("smpFmEnvA", dt.smpFmEnvA);
        dt.smpFmEnvD = (float) el.getDoubleAttribute ("smpFmEnvD", dt.smpFmEnvD);
        dt.smpFmEnvS = std::clamp ((float) el.getDoubleAttribute ("smpFmEnvS", dt.smpFmEnvS), 0.0f, 1.0f);
        // Sample path — reload file from disk if present
        dt.samplePath = el.getStringAttribute ("samplePath", dt.samplePath);
        if (dt.samplePath.isNotEmpty() && dt.sampleData == nullptr)
        {
            juce::File f (dt.samplePath);
            if (f.existsAsFile())
            {
                juce::AudioFormatManager afm;
                afm.registerBasicFormats();
                if (auto reader = std::unique_ptr<juce::AudioFormatReader>(afm.createReaderFor (f)))
                {
                    auto buf = std::make_shared<juce::AudioBuffer<float>> (static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                    reader->read (buf.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
                    dt.sampleData = buf;
                    dt.smpFileSR = static_cast<float>(reader->sampleRate);
                }
            }
        }
        // LFOs
        for (auto* lel = el.getChildByName ("LFO"); lel; lel = lel->getNextElementWithTagName ("LFO"))
        {
            int li = lel->getIntAttribute ("idx", -1);
            if (li < 0 || li >= 3) continue;
            auto& lfo = dt.lfos[static_cast<size_t>(li)];
            lfo.target = lel->getIntAttribute ("target", lfo.target);
            lfo.shape = lel->getIntAttribute ("shape", lfo.shape);
            lfo.rate = (float) lel->getDoubleAttribute ("rate", lfo.rate);
            lfo.depth = (float) lel->getDoubleAttribute ("depth", lfo.depth);
            lfo.sync = lel->getBoolAttribute ("sync", lfo.sync);
            lfo.syncDiv = (float) lel->getDoubleAttribute ("syncDiv", lfo.syncDiv);
            lfo.retrig = lel->getBoolAttribute ("retrig", lfo.retrig);
            lfo.hiRate = lel->getBoolAttribute ("hiRate", lfo.hiRate);
            lfo.fadeIn = (float) lel->getDoubleAttribute ("fadeIn", lfo.fadeIn);
            lfo.fadeInSync = lel->getBoolAttribute ("fadeInSync", lfo.fadeInSync);
            for (auto* rel = lel->getChildByName ("Route"); rel; rel = rel->getNextElementWithTagName ("Route"))
            {
                int ri = rel->getIntAttribute ("idx", -1);
                if (ri >= 0 && ri < 16)
                {
                    lfo.extraRoutes[static_cast<size_t>(ri)].target = rel->getIntAttribute ("tgt", -1);
                    lfo.extraRoutes[static_cast<size_t>(ri)].depth = (float) rel->getDoubleAttribute ("dep", 0.0);
                }
            }
        }
        // MSEGs (3 per track)
        for (int mi = 0; mi < 3; ++mi)
            loadMSEGData (el, dt.msegs[static_cast<size_t>(mi)], mi);

        // Velocity & Key tracking routes
        for (auto& vr : dt.velRoutes) vr = {-1, 0.0f};
        for (auto& kr : dt.keyRoutes) kr = {-1, 0.0f};
        for (auto* vr = el.getChildByName ("VelRoute"); vr; vr = vr->getNextElementWithTagName ("VelRoute"))
        {
            int ri = vr->getIntAttribute ("idx", -1);
            if (ri >= 0 && ri < 4) { dt.velRoutes[ri].target = vr->getIntAttribute ("tgt", -1); dt.velRoutes[ri].depth = (float) vr->getDoubleAttribute ("dep", 0.0); }
        }
        for (auto* kr = el.getChildByName ("KeyRoute"); kr; kr = kr->getNextElementWithTagName ("KeyRoute"))
        {
            int ri = kr->getIntAttribute ("idx", -1);
            if (ri >= 0 && ri < 4) { dt.keyRoutes[ri].target = kr->getIntAttribute ("tgt", -1); dt.keyRoutes[ri].depth = (float) kr->getDoubleAttribute ("dep", 0.0); }
        }
    }

    // ═══════════════════════════════════════
    // SYNTH PARAM SERIALIZATION (ALL fields)
    // ═══════════════════════════════════════
    static void saveSynthParams (juce::XmlElement& el, const SynthTrackState& st)
    {
        el.setAttribute ("model", static_cast<int>(st.model));
        el.setAttribute ("volume", (double) st.volume);
        el.setAttribute ("mono", (int) st.mono);
        el.setAttribute ("glide", (double) st.glide);
        el.setAttribute ("glideType", st.glideType);
        el.setAttribute ("chordMode", st.chordMode);
        el.setAttribute ("chordInversion", st.chordInversion);
        el.setAttribute ("chordVoicing", st.chordVoicing);
        // Analog
        el.setAttribute ("w1", st.w1);
        el.setAttribute ("w2", st.w2);
        el.setAttribute ("tune", (double) st.tune);
        el.setAttribute ("detune", (double) st.detune);
        el.setAttribute ("mix2", (double) st.mix2);
        el.setAttribute ("subLevel", (double) st.subLevel);
        el.setAttribute ("oscSync", (int) st.oscSync);
        el.setAttribute ("syncRatio", (double) st.syncRatio);
        el.setAttribute ("pwm", (double) st.pwm);
        el.setAttribute ("unison", st.unison);
        el.setAttribute ("uniSpread", (double) st.uniSpread);
        el.setAttribute ("uniStereo", (double) st.uniStereo);
        // Character
        el.setAttribute ("charType", st.charType);
        el.setAttribute ("charAmt", (double) st.charAmt);
        // Linear FM
        el.setAttribute ("fmLinAmt", (double) st.fmLinAmt);
        el.setAttribute ("fmLinRatio", (double) st.fmLinRatio);
        el.setAttribute ("fmLinDecay", (double) st.fmLinDecay);
        el.setAttribute ("fmLinSustain", (double) st.fmLinSustain);
        el.setAttribute ("fmLinSnap", st.fmLinSnap);
        // Filter
        el.setAttribute ("fType", st.fType);
        el.setAttribute ("fPoles", st.fPoles);
        el.setAttribute ("fModel", st.fModel);
        el.setAttribute ("cut", (double) st.cut);
        el.setAttribute ("res", (double) st.res);
        el.setAttribute ("fenv", (double) st.fenv);
        el.setAttribute ("fA", (double) st.fA); el.setAttribute ("fD", (double) st.fD);
        el.setAttribute ("fS", (double) st.fS); el.setAttribute ("fR", (double) st.fR);
        el.setAttribute ("aA", (double) st.aA); el.setAttribute ("aD", (double) st.aD);
        el.setAttribute ("aS", (double) st.aS); el.setAttribute ("aR", (double) st.aR);
        // FM 4-Op
        el.setAttribute ("fmAlgo", st.fmAlgo);
        el.setAttribute ("cRatio", (double) st.cRatio);
        el.setAttribute ("cLevel", (double) st.cLevel);
        el.setAttribute ("r2", (double) st.r2); el.setAttribute ("l2", (double) st.l2);
        el.setAttribute ("dc2", (double) st.dc2);
        el.setAttribute ("r3", (double) st.r3); el.setAttribute ("l3", (double) st.l3);
        el.setAttribute ("dc3", (double) st.dc3);
        el.setAttribute ("r4", (double) st.r4); el.setAttribute ("l4", (double) st.l4);
        el.setAttribute ("dc4", (double) st.dc4);
        el.setAttribute ("fmFeedback", (double) st.fmFeedback);
        el.setAttribute ("cA", (double) st.cA); el.setAttribute ("cD", (double) st.cD);
        el.setAttribute ("cS", (double) st.cS); el.setAttribute ("cR", (double) st.cR);
        // Elements
        el.setAttribute ("elemBow", (double) st.elemBow);
        el.setAttribute ("elemBlow", (double) st.elemBlow);
        el.setAttribute ("elemStrike", (double) st.elemStrike);
        el.setAttribute ("elemContour", (double) st.elemContour);
        el.setAttribute ("elemMallet", (double) st.elemMallet);
        el.setAttribute ("elemFlow", (double) st.elemFlow);
        el.setAttribute ("elemGeometry", (double) st.elemGeometry);
        el.setAttribute ("elemBright", (double) st.elemBright);
        el.setAttribute ("elemDamping", (double) st.elemDamping);
        el.setAttribute ("elemPosition", (double) st.elemPosition);
        el.setAttribute ("elemSpace", (double) st.elemSpace);
        el.setAttribute ("elemPitch", (double) st.elemPitch);
        // Plaits
        el.setAttribute ("plaitsModel", st.plaitsModel);
        el.setAttribute ("plaitsHarmonics", (double) st.plaitsHarmonics);
        el.setAttribute ("plaitsTimbre", (double) st.plaitsTimbre);
        el.setAttribute ("plaitsMorph", (double) st.plaitsMorph);
        el.setAttribute ("plaitsDecay", (double) st.plaitsDecay);
        el.setAttribute ("plaitsLpgColor", (double) st.plaitsLpgColor);
        // Sampler
        el.setAttribute ("smpStart", (double) st.smpStart);
        el.setAttribute ("smpEnd", (double) st.smpEnd);
        el.setAttribute ("smpGain", (double) st.smpGain);
        el.setAttribute ("smpLoop", st.smpLoop);
        el.setAttribute ("smpPlayMode", st.smpPlayMode);
        el.setAttribute ("smpReverse", st.smpReverse);
        el.setAttribute ("smpTune", (double) st.smpTune);
        el.setAttribute ("smpFine", (double) st.smpFine);
        el.setAttribute ("smpA", (double) st.smpA);
        el.setAttribute ("smpD", (double) st.smpD);
        el.setAttribute ("smpS", (double) st.smpS);
        el.setAttribute ("smpR", (double) st.smpR);
        el.setAttribute ("smpCut", (double) st.smpCut);
        el.setAttribute ("smpRes", (double) st.smpRes);
        el.setAttribute ("smpFType", st.smpFType);
        el.setAttribute ("smpFModel", st.smpFModel);
        el.setAttribute ("smpFPoles", st.smpFPoles);
        el.setAttribute ("smpFiltEnv", (double) st.smpFiltEnv);
        el.setAttribute ("smpFiltA", (double) st.smpFiltA);
        el.setAttribute ("smpFiltD", (double) st.smpFiltD);
        el.setAttribute ("smpFiltS", (double) st.smpFiltS);
        el.setAttribute ("smpFiltR", (double) st.smpFiltR);
        el.setAttribute ("smpRootNote", st.smpRootNote);
        el.setAttribute ("smpFmAmt", (double) st.smpFmAmt);
        el.setAttribute ("smpFmRatio", (double) st.smpFmRatio);
        el.setAttribute ("smpFmEnvA", (double) st.smpFmEnvA);
        el.setAttribute ("smpFmEnvD", (double) st.smpFmEnvD);
        el.setAttribute ("smpFmEnvS", (double) st.smpFmEnvS);
        el.setAttribute ("smpStretch", (double) st.smpStretch);
        el.setAttribute ("smpWarp", st.smpWarp);
        el.setAttribute ("smpBPM", (double) st.smpBPM);
        el.setAttribute ("smpFileSR", (double) st.smpFileSR);
        el.setAttribute ("smpBpmSync", st.smpBpmSync);
        el.setAttribute ("smpSyncMul", st.smpSyncMul);
        el.setAttribute ("smpBars", st.smpBars);
        // Warp markers
        if (!st.warpMarkers.empty())
        {
            auto* wml = el.createNewChildElement ("WarpMarkers");
            for (const auto& wm : st.warpMarkers)
            {
                auto* wme = wml->createNewChildElement ("WM");
                wme->setAttribute ("sp", (double) wm.samplePos);
                wme->setAttribute ("bp", (double) wm.beatPos);
                wme->setAttribute ("auto", wm.isAuto ? 1 : 0);
                wme->setAttribute ("osp", (double) wm.originalSamplePos);
            }
        }
        el.setAttribute ("formLfoRate", (double) st.formLfoRate);
        el.setAttribute ("formLfoDepth", (double) st.formLfoDepth);
        el.setAttribute ("formV1", (double) st.formV1);
        el.setAttribute ("formV2", (double) st.formV2);
        el.setAttribute ("formMorph", (double) st.formMorph);
        if (st.samplePath.isNotEmpty())
            el.setAttribute ("samplePath", st.samplePath);
        // Granular
        el.setAttribute ("grainPos", (double) st.grainPos);
        el.setAttribute ("grainSize", (double) st.grainSize);
        el.setAttribute ("grainDensity", (double) st.grainDensity);
        el.setAttribute ("grainSpray", (double) st.grainSpray);
        el.setAttribute ("grainPitch", (double) st.grainPitch);
        el.setAttribute ("grainPan", (double) st.grainPan);
        el.setAttribute ("grainShape", st.grainShape);
        el.setAttribute ("grainDir", st.grainDir);
        el.setAttribute ("grainMix", (double) st.grainMix);
        el.setAttribute ("grainFreeze", (int) st.grainFreeze);
        el.setAttribute ("grainTexture", (double) st.grainTexture);
        el.setAttribute ("grainScan", (double) st.grainScan);
        el.setAttribute ("grainMode", st.grainMode);
        el.setAttribute ("grainTilt", (double) st.grainTilt);
        el.setAttribute ("grainUniVoices", st.grainUniVoices);
        el.setAttribute ("grainUniDetune", (double) st.grainUniDetune);
        el.setAttribute ("grainUniStereo", (double) st.grainUniStereo);
        el.setAttribute ("grainQuantize", st.grainQuantize);
        el.setAttribute ("grainFeedback", (double) st.grainFeedback);
        el.setAttribute ("grainFmAmt", (double) st.grainFmAmt);
        el.setAttribute ("grainFmRatio", (double) st.grainFmRatio);
        el.setAttribute ("grainFmDecay", (double) st.grainFmDecay);
        el.setAttribute ("grainFmSus", (double) st.grainFmSus);
        el.setAttribute ("grainFmSnap", st.grainFmSnap);
        el.setAttribute ("grainFmSpread", (double) st.grainFmSpread);
        // FX
        el.setAttribute ("distAmt", (double) st.distAmt);
        el.setAttribute ("reduxBits", (double) st.reduxBits);
        el.setAttribute ("reduxRate", (double) st.reduxRate);
        el.setAttribute ("chorusDepth", (double) st.chorusDepth);
        el.setAttribute ("chorusRate", (double) st.chorusRate);
        el.setAttribute ("chorusMix", (double) st.chorusMix);
        el.setAttribute ("delayMix", (double) st.delayMix);
        el.setAttribute ("delayTime", (double) st.delayTime);
        el.setAttribute ("delayFB", (double) st.delayFB);
        el.setAttribute ("delaySync", (int) st.delaySync);
        el.setAttribute ("delayBeats", (double) st.delayBeats);
        el.setAttribute ("delayPP", st.delayPP);
        el.setAttribute ("delayDamp", (double) st.delayDamp);
        el.setAttribute ("delayAlgo", st.delayAlgo);
        el.setAttribute ("reverbMix", (double) st.reverbMix);
        el.setAttribute ("reverbSize", (double) st.reverbSize);
        el.setAttribute ("reverbDamp", (double) st.reverbDamp);
        el.setAttribute ("reverbAlgo", st.reverbAlgo);
        // Filters + pan + duck
        el.setAttribute ("fxLP", (double) st.fxLP);
        el.setAttribute ("fxHP", (double) st.fxHP);
        el.setAttribute ("eqLow", (double) st.eqLow);
        el.setAttribute ("eqMid", (double) st.eqMid);
        el.setAttribute ("eqHigh", (double) st.eqHigh);
        el.setAttribute ("pan", (double) st.pan);
        el.setAttribute ("duckSrc", st.duckSrc);
        el.setAttribute ("duckDepth", (double) st.duckDepth);
        el.setAttribute ("duckAtk", (double) st.duckAtk);
        el.setAttribute ("duckRel", (double) st.duckRel);
        // Pro Distortion
        el.setAttribute ("proDistModel", st.proDistModel);
        el.setAttribute ("proDistDrive", (double) st.proDistDrive);
        el.setAttribute ("proDistTone", (double) st.proDistTone);
        el.setAttribute ("proDistMix", (double) st.proDistMix);
        el.setAttribute ("proDistBias", (double) st.proDistBias);
        el.setAttribute ("chokeGroup", st.chokeGroup);
        el.setAttribute ("ottDepth", (double) st.ottDepth);
        el.setAttribute ("ottUpward", (double) st.ottUpward);
        el.setAttribute ("ottDownward", (double) st.ottDownward);
        el.setAttribute ("phaserMix", (double) st.phaserMix);
        el.setAttribute ("phaserRate", (double) st.phaserRate);
        el.setAttribute ("phaserDepth", (double) st.phaserDepth);
        el.setAttribute ("phaserFB", (double) st.phaserFB);
        el.setAttribute ("flangerMix", (double) st.flangerMix);
        el.setAttribute ("flangerRate", (double) st.flangerRate);
        el.setAttribute ("flangerDepth", (double) st.flangerDepth);
        el.setAttribute ("flangerFB", (double) st.flangerFB);
        // Arp
        el.setAttribute ("arpOn", (int) st.arp.enabled);
        el.setAttribute ("arpDir", st.arp.direction);
        el.setAttribute ("arpOct", st.arp.octaves);
        el.setAttribute ("arpDiv", st.arp.division);
        el.setAttribute ("arpSteps", st.arp.numSteps);
        el.setAttribute ("arpRetrig", (int) st.arp.keyRetrig);
        el.setAttribute ("arpLoop", st.arp.loopLen);
        el.setAttribute ("arpTgt", st.arp.assignTarget);
        el.setAttribute ("arpDepth", (double) st.arp.assignDepth);
        el.setAttribute ("arpTgt2", st.arp.assign2Target);
        el.setAttribute ("arpDepth2", (double) st.arp.assign2Depth);
        // Arp extra modulation routes
        for (int ri = 0; ri < 16; ++ri)
        {
            auto& route = st.arp.extraRoutes[static_cast<size_t>(ri)];
            if (route.target >= 0)
            {
                auto* rel = el.createNewChildElement ("ArpRoute");
                rel->setAttribute ("idx", ri);
                rel->setAttribute ("tgt", route.target);
                rel->setAttribute ("dep", (double) route.depth);
            }
        }
        {
            auto* arpEl = el.createNewChildElement ("ArpSteps");
            for (int s = 0; s < st.arp.numSteps; ++s)
            {
                auto* stepEl = arpEl->createNewChildElement ("S");
                stepEl->setAttribute ("v", st.arp.steps[static_cast<size_t>(s)].velocity);
                stepEl->setAttribute ("g", st.arp.steps[static_cast<size_t>(s)].gate);
                stepEl->setAttribute ("p", (double) st.arp.steps[static_cast<size_t>(s)].param);
                stepEl->setAttribute ("p2", (double) st.arp.steps[static_cast<size_t>(s)].param2);
            }
        }
        // LFOs
        for (int li = 0; li < 3; ++li)
        {
            auto* lel = el.createNewChildElement ("LFO");
            lel->setAttribute ("idx", li);
            lel->setAttribute ("target", st.lfos[static_cast<size_t>(li)].target);
            lel->setAttribute ("shape", st.lfos[static_cast<size_t>(li)].shape);
            lel->setAttribute ("rate", (double) st.lfos[static_cast<size_t>(li)].rate);
            lel->setAttribute ("depth", (double) st.lfos[static_cast<size_t>(li)].depth);
            lel->setAttribute ("sync", (int) st.lfos[static_cast<size_t>(li)].sync);
            lel->setAttribute ("syncDiv", (double) st.lfos[static_cast<size_t>(li)].syncDiv);
            lel->setAttribute ("retrig", (int) st.lfos[static_cast<size_t>(li)].retrig);
            lel->setAttribute ("hiRate", (int) st.lfos[static_cast<size_t>(li)].hiRate);
            lel->setAttribute ("fadeIn", (double) st.lfos[static_cast<size_t>(li)].fadeIn);
            lel->setAttribute ("fadeInSync", (int) st.lfos[static_cast<size_t>(li)].fadeInSync);
            for (int ri = 0; ri < 16; ++ri)
            {
                auto& r = st.lfos[static_cast<size_t>(li)].extraRoutes[static_cast<size_t>(ri)];
                if (r.target >= 0)
                {
                    auto* rel = lel->createNewChildElement ("Route");
                    rel->setAttribute ("idx", ri);
                    rel->setAttribute ("tgt", r.target);
                    rel->setAttribute ("dep", (double) r.depth);
                }
            }
        }
        // MSEGs (3 per track)
        for (int mi = 0; mi < 3; ++mi)
            saveMSEGData (el, st.msegs[static_cast<size_t>(mi)], mi);

        // Velocity & Key tracking routes
        for (int ri = 0; ri < 4; ++ri)
        {
            if (st.velRoutes[ri].target >= 0) {
                auto* vr = el.createNewChildElement ("VelRoute");
                vr->setAttribute ("idx", ri); vr->setAttribute ("tgt", st.velRoutes[ri].target);
                vr->setAttribute ("dep", (double) st.velRoutes[ri].depth);
            }
            if (st.keyRoutes[ri].target >= 0) {
                auto* kr = el.createNewChildElement ("KeyRoute");
                kr->setAttribute ("idx", ri); kr->setAttribute ("tgt", st.keyRoutes[ri].target);
                kr->setAttribute ("dep", (double) st.keyRoutes[ri].depth);
            }
        }
    }

    static void loadSynthParams (const juce::XmlElement& el, SynthTrackState& st)
    {
        st.model = (SynthModel) el.getIntAttribute ("model", (int) st.model);
        st.volume = (float) el.getDoubleAttribute ("volume", st.volume);
        st.mono = el.getBoolAttribute ("mono", st.mono);
        st.glide = (float) el.getDoubleAttribute ("glide", st.glide);
        st.glideType = el.getIntAttribute ("glideType", st.glideType);
        st.chordMode = el.getIntAttribute ("chordMode", st.chordMode);
        st.chordInversion = el.getIntAttribute ("chordInversion", st.chordInversion);
        st.chordVoicing = el.getIntAttribute ("chordVoicing", st.chordVoicing);
        st.w1 = el.getIntAttribute ("w1", st.w1);
        st.w2 = el.getIntAttribute ("w2", st.w2);
        st.tune = (float) el.getDoubleAttribute ("tune", st.tune);
        st.detune = (float) el.getDoubleAttribute ("detune", st.detune);
        st.mix2 = (float) el.getDoubleAttribute ("mix2", st.mix2);
        st.subLevel = (float) el.getDoubleAttribute ("subLevel", st.subLevel);
        st.oscSync = el.getBoolAttribute ("oscSync", st.oscSync);
        st.syncRatio = (float) el.getDoubleAttribute ("syncRatio", st.syncRatio);
        st.pwm = (float) el.getDoubleAttribute ("pwm", st.pwm);
        st.unison = el.getIntAttribute ("unison", st.unison);
        st.uniSpread = (float) el.getDoubleAttribute ("uniSpread", st.uniSpread);
        st.uniStereo = (float) el.getDoubleAttribute ("uniStereo", st.uniStereo);
        st.charType = el.getIntAttribute ("charType", st.charType);
        st.charAmt = (float) el.getDoubleAttribute ("charAmt", st.charAmt);
        st.fmLinAmt = (float) el.getDoubleAttribute ("fmLinAmt", st.fmLinAmt);
        st.fmLinRatio = (float) el.getDoubleAttribute ("fmLinRatio", st.fmLinRatio);
        st.fmLinDecay = (float) el.getDoubleAttribute ("fmLinDecay", st.fmLinDecay);
        st.fmLinSustain = std::clamp ((float) el.getDoubleAttribute ("fmLinSustain", st.fmLinSustain), 0.0f, 1.0f);
        st.fmLinSnap = el.getIntAttribute ("fmLinSnap", st.fmLinSnap);
        st.fType = el.getIntAttribute ("fType", st.fType);
        st.fPoles = el.getIntAttribute ("fPoles", st.fPoles);
        st.fModel = el.getIntAttribute ("fModel", st.fModel);
        st.cut = (float) el.getDoubleAttribute ("cut", st.cut);
        st.res = (float) el.getDoubleAttribute ("res", st.res);
        st.fenv = (float) el.getDoubleAttribute ("fenv", st.fenv);
        st.fA = (float) el.getDoubleAttribute ("fA", st.fA); st.fD = (float) el.getDoubleAttribute ("fD", st.fD);
        st.fS = (float) el.getDoubleAttribute ("fS", st.fS); st.fR = (float) el.getDoubleAttribute ("fR", st.fR);
        st.aA = (float) el.getDoubleAttribute ("aA", st.aA); st.aD = (float) el.getDoubleAttribute ("aD", st.aD);
        st.aS = (float) el.getDoubleAttribute ("aS", st.aS); st.aR = (float) el.getDoubleAttribute ("aR", st.aR);
        st.fmAlgo = el.getIntAttribute ("fmAlgo", st.fmAlgo);
        st.cRatio = (float) el.getDoubleAttribute ("cRatio", st.cRatio);
        st.cLevel = (float) el.getDoubleAttribute ("cLevel", st.cLevel);
        st.r2 = (float) el.getDoubleAttribute ("r2", st.r2); st.l2 = (float) el.getDoubleAttribute ("l2", st.l2);
        st.dc2 = (float) el.getDoubleAttribute ("dc2", st.dc2);
        st.r3 = (float) el.getDoubleAttribute ("r3", st.r3); st.l3 = (float) el.getDoubleAttribute ("l3", st.l3);
        st.dc3 = (float) el.getDoubleAttribute ("dc3", st.dc3);
        st.r4 = (float) el.getDoubleAttribute ("r4", st.r4); st.l4 = (float) el.getDoubleAttribute ("l4", st.l4);
        st.dc4 = (float) el.getDoubleAttribute ("dc4", st.dc4);
        st.fmFeedback = (float) el.getDoubleAttribute ("fmFeedback", st.fmFeedback);
        st.cA = (float) el.getDoubleAttribute ("cA", st.cA); st.cD = (float) el.getDoubleAttribute ("cD", st.cD);
        st.cS = (float) el.getDoubleAttribute ("cS", st.cS); st.cR = (float) el.getDoubleAttribute ("cR", st.cR);
        st.elemBow = (float) el.getDoubleAttribute ("elemBow", st.elemBow);
        st.elemBlow = (float) el.getDoubleAttribute ("elemBlow", st.elemBlow);
        st.elemStrike = (float) el.getDoubleAttribute ("elemStrike", st.elemStrike);
        st.elemContour = (float) el.getDoubleAttribute ("elemContour", st.elemContour);
        st.elemMallet = (float) el.getDoubleAttribute ("elemMallet", st.elemMallet);
        st.elemFlow = (float) el.getDoubleAttribute ("elemFlow", st.elemFlow);
        st.elemGeometry = (float) el.getDoubleAttribute ("elemGeometry", st.elemGeometry);
        st.elemBright = (float) el.getDoubleAttribute ("elemBright", st.elemBright);
        st.elemDamping = (float) el.getDoubleAttribute ("elemDamping", st.elemDamping);
        st.elemPosition = (float) el.getDoubleAttribute ("elemPosition", st.elemPosition);
        st.elemSpace = (float) el.getDoubleAttribute ("elemSpace", st.elemSpace);
        st.elemPitch = (float) el.getDoubleAttribute ("elemPitch", st.elemPitch);
        st.plaitsModel = el.getIntAttribute ("plaitsModel", st.plaitsModel);
        st.plaitsHarmonics = (float) el.getDoubleAttribute ("plaitsHarmonics", st.plaitsHarmonics);
        st.plaitsTimbre = (float) el.getDoubleAttribute ("plaitsTimbre", st.plaitsTimbre);
        st.plaitsMorph = (float) el.getDoubleAttribute ("plaitsMorph", st.plaitsMorph);
        st.plaitsDecay = (float) el.getDoubleAttribute ("plaitsDecay", st.plaitsDecay);
        st.plaitsLpgColor = (float) el.getDoubleAttribute ("plaitsLpgColor", st.plaitsLpgColor);
        // Sampler
        st.smpStart = (float) el.getDoubleAttribute ("smpStart", st.smpStart);
        st.smpEnd = (float) el.getDoubleAttribute ("smpEnd", st.smpEnd);
        st.smpGain = (float) el.getDoubleAttribute ("smpGain", st.smpGain);
        st.smpLoop = el.getIntAttribute ("smpLoop", st.smpLoop);
        st.smpPlayMode = el.getIntAttribute ("smpPlayMode", st.smpPlayMode);
        st.smpReverse = el.getIntAttribute ("smpReverse", st.smpReverse);
        st.smpTune = (float) el.getDoubleAttribute ("smpTune", st.smpTune);
        st.smpFine = (float) el.getDoubleAttribute ("smpFine", st.smpFine);
        st.smpA = (float) el.getDoubleAttribute ("smpA", st.smpA);
        st.smpD = (float) el.getDoubleAttribute ("smpD", st.smpD);
        st.smpS = (float) el.getDoubleAttribute ("smpS", st.smpS);
        st.smpR = (float) el.getDoubleAttribute ("smpR", st.smpR);
        st.smpCut = (float) el.getDoubleAttribute ("smpCut", st.smpCut);
        st.smpRes = (float) el.getDoubleAttribute ("smpRes", st.smpRes);
        st.smpFType = el.getIntAttribute ("smpFType", st.smpFType);
        st.smpFModel = el.getIntAttribute ("smpFModel", st.smpFModel);
        st.smpFPoles = el.getIntAttribute ("smpFPoles", st.smpFPoles);
        st.smpFiltEnv = (float) el.getDoubleAttribute ("smpFiltEnv", st.smpFiltEnv);
        st.smpFiltA = (float) el.getDoubleAttribute ("smpFiltA", st.smpFiltA);
        st.smpFiltD = (float) el.getDoubleAttribute ("smpFiltD", st.smpFiltD);
        st.smpFiltS = (float) el.getDoubleAttribute ("smpFiltS", st.smpFiltS);
        st.smpFiltR = (float) el.getDoubleAttribute ("smpFiltR", st.smpFiltR);
        st.smpRootNote = el.getIntAttribute ("smpRootNote", st.smpRootNote);
        st.smpFmAmt = (float) el.getDoubleAttribute ("smpFmAmt", st.smpFmAmt);
        st.smpFmRatio = (float) el.getDoubleAttribute ("smpFmRatio", st.smpFmRatio);
        st.smpFmEnvA = (float) el.getDoubleAttribute ("smpFmEnvA", st.smpFmEnvA);
        st.smpFmEnvD = (float) el.getDoubleAttribute ("smpFmEnvD", st.smpFmEnvD);
        st.smpFmEnvS = std::clamp ((float) el.getDoubleAttribute ("smpFmEnvS", st.smpFmEnvS), 0.0f, 1.0f);
        st.smpStretch = (float) el.getDoubleAttribute ("smpStretch", st.smpStretch);
        st.smpWarp = el.getIntAttribute ("smpWarp", st.smpWarp);
        st.smpBPM = (float) el.getDoubleAttribute ("smpBPM", st.smpBPM);
        st.smpFileSR = (float) el.getDoubleAttribute ("smpFileSR", st.smpFileSR);
        st.smpBpmSync = el.getIntAttribute ("smpBpmSync", st.smpBpmSync);
        st.smpSyncMul = el.getIntAttribute ("smpSyncMul", st.smpSyncMul);
        st.smpBars = el.getIntAttribute ("smpBars", st.smpBars);
        // Warp markers
        st.warpMarkers.clear();
        if (auto* wml = el.getChildByName ("WarpMarkers"))
        {
            for (auto* wme = wml->getChildByName ("WM"); wme; wme = wme->getNextElementWithTagName ("WM"))
            {
                WarpMarker wm;
                wm.samplePos = (float) wme->getDoubleAttribute ("sp", 0.0);
                wm.beatPos = (float) wme->getDoubleAttribute ("bp", 0.0);
                wm.isAuto = wme->getIntAttribute ("auto", 1) != 0;
                wm.originalSamplePos = (float) wme->getDoubleAttribute ("osp", wm.samplePos);
                st.warpMarkers.push_back (wm);
            }
        }
        st.formLfoRate = (float) el.getDoubleAttribute ("formLfoRate", st.formLfoRate);
        st.formLfoDepth = (float) el.getDoubleAttribute ("formLfoDepth", st.formLfoDepth);
        st.formV1 = (float) el.getDoubleAttribute ("formV1", st.formV1);
        st.formV2 = (float) el.getDoubleAttribute ("formV2", st.formV2);
        st.formMorph = (float) el.getDoubleAttribute ("formMorph", st.formMorph);
        st.samplePath = el.getStringAttribute ("samplePath", st.samplePath);
        if (st.samplePath.isNotEmpty() && st.sampleData == nullptr)
        {
            juce::File f (st.samplePath);
            if (f.existsAsFile())
            {
                juce::AudioFormatManager afm;
                afm.registerBasicFormats();
                if (auto reader = std::unique_ptr<juce::AudioFormatReader>(afm.createReaderFor (f)))
                {
                    auto buf = std::make_shared<juce::AudioBuffer<float>> (static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                    reader->read (buf.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
                    st.sampleData = buf;
                    st.smpFileSR = static_cast<float>(reader->sampleRate);
                }
            }
        }
        // Granular
        st.grainPos = (float) el.getDoubleAttribute ("grainPos", st.grainPos);
        st.grainSize = (float) el.getDoubleAttribute ("grainSize", st.grainSize);
        st.grainDensity = (float) el.getDoubleAttribute ("grainDensity", st.grainDensity);
        st.grainSpray = (float) el.getDoubleAttribute ("grainSpray", st.grainSpray);
        st.grainPitch = (float) el.getDoubleAttribute ("grainPitch", st.grainPitch);
        st.grainPan = (float) el.getDoubleAttribute ("grainPan", st.grainPan);
        st.grainShape = el.getIntAttribute ("grainShape", st.grainShape);
        st.grainDir = el.getIntAttribute ("grainDir", st.grainDir);
        st.grainMix = (float) el.getDoubleAttribute ("grainMix", st.grainMix);
        st.grainFreeze = el.getBoolAttribute ("grainFreeze", st.grainFreeze);
        st.grainTexture = (float) el.getDoubleAttribute ("grainTexture", st.grainTexture);
        st.grainScan = (float) el.getDoubleAttribute ("grainScan", st.grainScan);
        st.grainFmAmt = (float) el.getDoubleAttribute ("grainFmAmt", st.grainFmAmt);
        st.grainFmRatio = (float) el.getDoubleAttribute ("grainFmRatio", st.grainFmRatio);
        st.grainFmDecay = (float) el.getDoubleAttribute ("grainFmDecay", st.grainFmDecay);
        st.grainFmSus = (float) el.getDoubleAttribute ("grainFmSus", st.grainFmSus);
        st.grainFmSnap = el.getIntAttribute ("grainFmSnap", st.grainFmSnap);
        st.grainFmSpread = (float) el.getDoubleAttribute ("grainFmSpread", st.grainFmSpread);
        st.grainMode = el.getIntAttribute ("grainMode", st.grainMode);
        st.grainTilt = (float) el.getDoubleAttribute ("grainTilt", st.grainTilt);
        st.grainUniVoices = el.getIntAttribute ("grainUniVoices", st.grainUniVoices);
        st.grainUniDetune = (float) el.getDoubleAttribute ("grainUniDetune", st.grainUniDetune);
        st.grainUniStereo = (float) el.getDoubleAttribute ("grainUniStereo", st.grainUniStereo);
        st.grainQuantize = el.getIntAttribute ("grainQuantize", st.grainQuantize);
        st.grainFeedback = (float) el.getDoubleAttribute ("grainFeedback", st.grainFeedback);
        st.distAmt = (float) el.getDoubleAttribute ("distAmt", st.distAmt);
        st.reduxBits = (float) el.getDoubleAttribute ("reduxBits", st.reduxBits);
        st.reduxRate = (float) el.getDoubleAttribute ("reduxRate", st.reduxRate);
        st.chorusDepth = (float) el.getDoubleAttribute ("chorusDepth", st.chorusDepth);
        st.chorusRate = (float) el.getDoubleAttribute ("chorusRate", st.chorusRate);
        st.chorusMix = (float) el.getDoubleAttribute ("chorusMix", st.chorusMix);
        st.delayMix = (float) el.getDoubleAttribute ("delayMix", st.delayMix);
        st.delayTime = (float) el.getDoubleAttribute ("delayTime", st.delayTime);
        st.delayFB = (float) el.getDoubleAttribute ("delayFB", st.delayFB);
        st.delaySync = el.getBoolAttribute ("delaySync", st.delaySync);
        st.delayBeats = (float) el.getDoubleAttribute ("delayBeats", st.delayBeats);
        st.delayPP = el.getIntAttribute ("delayPP", st.delayPP);
        st.delayDamp = (float) el.getDoubleAttribute ("delayDamp", st.delayDamp);
        st.delayAlgo = el.getIntAttribute ("delayAlgo", st.delayAlgo);
        st.reverbMix = (float) el.getDoubleAttribute ("reverbMix", st.reverbMix);
        st.reverbSize = (float) el.getDoubleAttribute ("reverbSize", st.reverbSize);
        st.reverbDamp = (float) el.getDoubleAttribute ("reverbDamp", st.reverbDamp);
        st.reverbAlgo = el.getIntAttribute ("reverbAlgo", st.reverbAlgo);
        st.fxLP = (float) el.getDoubleAttribute ("fxLP", st.fxLP);
        st.fxHP = (float) el.getDoubleAttribute ("fxHP", st.fxHP);
        st.eqLow = (float) el.getDoubleAttribute ("eqLow", st.eqLow);
        st.eqMid = (float) el.getDoubleAttribute ("eqMid", st.eqMid);
        st.eqHigh = (float) el.getDoubleAttribute ("eqHigh", st.eqHigh);
        st.pan = (float) el.getDoubleAttribute ("pan", st.pan);
        st.duckSrc = el.getIntAttribute ("duckSrc", st.duckSrc);
        st.duckDepth = (float) el.getDoubleAttribute ("duckDepth", st.duckDepth);
        st.duckAtk = (float) el.getDoubleAttribute ("duckAtk", st.duckAtk);
        st.duckRel = (float) el.getDoubleAttribute ("duckRel", st.duckRel);
        st.proDistModel = el.getIntAttribute ("proDistModel", st.proDistModel);
        st.proDistDrive = (float) el.getDoubleAttribute ("proDistDrive", st.proDistDrive);
        st.proDistTone = (float) el.getDoubleAttribute ("proDistTone", st.proDistTone);
        st.proDistMix = (float) el.getDoubleAttribute ("proDistMix", st.proDistMix);
        st.proDistBias = (float) el.getDoubleAttribute ("proDistBias", st.proDistBias);
        st.chokeGroup = el.getIntAttribute ("chokeGroup", 0);
        st.ottDepth = (float) el.getDoubleAttribute ("ottDepth", st.ottDepth);
        st.ottUpward = (float) el.getDoubleAttribute ("ottUpward", st.ottUpward);
        st.ottDownward = (float) el.getDoubleAttribute ("ottDownward", st.ottDownward);
        st.phaserMix = (float) el.getDoubleAttribute ("phaserMix", st.phaserMix);
        st.phaserRate = (float) el.getDoubleAttribute ("phaserRate", st.phaserRate);
        st.phaserDepth = (float) el.getDoubleAttribute ("phaserDepth", st.phaserDepth);
        st.phaserFB = (float) el.getDoubleAttribute ("phaserFB", st.phaserFB);
        st.flangerMix = (float) el.getDoubleAttribute ("flangerMix", st.flangerMix);
        st.flangerRate = (float) el.getDoubleAttribute ("flangerRate", st.flangerRate);
        st.flangerDepth = (float) el.getDoubleAttribute ("flangerDepth", st.flangerDepth);
        st.flangerFB = (float) el.getDoubleAttribute ("flangerFB", st.flangerFB);
        st.arp.enabled = el.getBoolAttribute ("arpOn", st.arp.enabled);
        st.arp.direction = el.getIntAttribute ("arpDir", st.arp.direction);
        st.arp.octaves = el.getIntAttribute ("arpOct", st.arp.octaves);
        st.arp.division = el.getIntAttribute ("arpDiv", st.arp.division);
        st.arp.numSteps = el.getIntAttribute ("arpSteps", st.arp.numSteps);
        st.arp.keyRetrig = el.getBoolAttribute ("arpRetrig", st.arp.keyRetrig);
        st.arp.loopLen = el.getIntAttribute ("arpLoop", st.arp.loopLen);
        st.arp.assignTarget = el.getIntAttribute ("arpTgt", st.arp.assignTarget);
        st.arp.assignDepth = (float) el.getDoubleAttribute ("arpDepth", st.arp.assignDepth);
        st.arp.assign2Target = el.getIntAttribute ("arpTgt2", st.arp.assign2Target);
        st.arp.assign2Depth = (float) el.getDoubleAttribute ("arpDepth2", st.arp.assign2Depth);
        if (auto* arpEl = el.getChildByName ("ArpSteps"))
        {
            int s = 0;
            for (auto* stepEl = arpEl->getChildByName ("S"); stepEl && s < kArpMaxSteps;
                 stepEl = stepEl->getNextElementWithTagName ("S"), ++s)
            {
                st.arp.steps[static_cast<size_t>(s)].velocity = static_cast<uint8_t>(stepEl->getIntAttribute ("v", 100));
                st.arp.steps[static_cast<size_t>(s)].gate = static_cast<uint8_t>(stepEl->getIntAttribute ("g", 80));
                st.arp.steps[static_cast<size_t>(s)].param = (float) stepEl->getDoubleAttribute ("p", 0.5);
                st.arp.steps[static_cast<size_t>(s)].param2 = (float) stepEl->getDoubleAttribute ("p2", 0.5);
            }
        }
        // Load arp extra modulation routes
        for (auto* rel = el.getChildByName ("ArpRoute"); rel; rel = rel->getNextElementWithTagName ("ArpRoute"))
        {
            int ri = rel->getIntAttribute ("idx", -1);
            if (ri >= 0 && ri < 16)
            {
                st.arp.extraRoutes[static_cast<size_t>(ri)].target = rel->getIntAttribute ("tgt", -1);
                st.arp.extraRoutes[static_cast<size_t>(ri)].depth = (float) rel->getDoubleAttribute ("dep", 0.0);
            }
        }
        for (auto* lel = el.getChildByName ("LFO"); lel; lel = lel->getNextElementWithTagName ("LFO"))
        {
            int li = lel->getIntAttribute ("idx", -1);
            if (li < 0 || li >= 3) continue;
            auto& lfo = st.lfos[static_cast<size_t>(li)];
            lfo.target = lel->getIntAttribute ("target", lfo.target);
            lfo.shape = lel->getIntAttribute ("shape", lfo.shape);
            lfo.rate = (float) lel->getDoubleAttribute ("rate", lfo.rate);
            lfo.depth = (float) lel->getDoubleAttribute ("depth", lfo.depth);
            lfo.sync = lel->getBoolAttribute ("sync", lfo.sync);
            lfo.syncDiv = (float) lel->getDoubleAttribute ("syncDiv", lfo.syncDiv);
            lfo.retrig = lel->getBoolAttribute ("retrig", lfo.retrig);
            lfo.hiRate = lel->getBoolAttribute ("hiRate", lfo.hiRate);
            lfo.fadeIn = (float) lel->getDoubleAttribute ("fadeIn", lfo.fadeIn);
            lfo.fadeInSync = lel->getBoolAttribute ("fadeInSync", lfo.fadeInSync);
            for (auto* rel = lel->getChildByName ("Route"); rel; rel = rel->getNextElementWithTagName ("Route"))
            {
                int ri = rel->getIntAttribute ("idx", -1);
                if (ri >= 0 && ri < 16)
                {
                    lfo.extraRoutes[static_cast<size_t>(ri)].target = rel->getIntAttribute ("tgt", -1);
                    lfo.extraRoutes[static_cast<size_t>(ri)].depth = (float) rel->getDoubleAttribute ("dep", 0.0);
                }
            }
        }
        // MSEGs (3 per track)
        for (int mi = 0; mi < 3; ++mi)
            loadMSEGData (el, st.msegs[static_cast<size_t>(mi)], mi);

        // Velocity & Key tracking routes
        for (auto& vr : st.velRoutes) vr = {-1, 0.0f};
        for (auto& kr : st.keyRoutes) kr = {-1, 0.0f};
        for (auto* vr = el.getChildByName ("VelRoute"); vr; vr = vr->getNextElementWithTagName ("VelRoute"))
        {
            int ri = vr->getIntAttribute ("idx", -1);
            if (ri >= 0 && ri < 4) { st.velRoutes[ri].target = vr->getIntAttribute ("tgt", -1); st.velRoutes[ri].depth = (float) vr->getDoubleAttribute ("dep", 0.0); }
        }
        for (auto* kr = el.getChildByName ("KeyRoute"); kr; kr = kr->getNextElementWithTagName ("KeyRoute"))
        {
            int ri = kr->getIntAttribute ("idx", -1);
            if (ri >= 0 && ri < 4) { st.keyRoutes[ri].target = kr->getIntAttribute ("tgt", -1); st.keyRoutes[ri].depth = (float) kr->getDoubleAttribute ("dep", 0.0); }
        }
    }

    // ═══════════════════════════════════════
    // MSEG SERIALIZATION HELPERS
    // ═══════════════════════════════════════
    static void saveMSEGData (juce::XmlElement& el, const MSEGData& mseg, int idx)
    {
        juce::String tag = "MSEG" + juce::String (idx);
        auto* mel = el.createNewChildElement (tag);
        mel->setAttribute ("numPoints", mseg.numPoints);
        mel->setAttribute ("target", mseg.target);
        mel->setAttribute ("depth", (double) mseg.depth);
        mel->setAttribute ("totalTime", (double) mseg.totalTime);
        mel->setAttribute ("tempoSync", (int) mseg.tempoSync);
        mel->setAttribute ("syncDiv", mseg.syncDiv);
        mel->setAttribute ("loopStart", mseg.loopStart);
        mel->setAttribute ("loopEnd", mseg.loopEnd);
        mel->setAttribute ("loopMode", mseg.loopMode);
        mel->setAttribute ("gridX", mseg.gridX);
        mel->setAttribute ("gridY", mseg.gridY);
        mel->setAttribute ("transportSync", (int) mseg.transportSync);
        mel->setAttribute ("auxRate", (double) mseg.auxRate);
        mel->setAttribute ("auxShape", mseg.auxShape);
        mel->setAttribute ("auxSync", (int) mseg.auxSync);
        mel->setAttribute ("auxSyncDiv", mseg.auxSyncDiv);
        mel->setAttribute ("fadeIn", (double) mseg.fadeIn);
        mel->setAttribute ("fadeInSync", (int) mseg.fadeInSync);
        for (int i = 0; i < mseg.numPoints; ++i)
        {
            auto* pt = mel->createNewChildElement ("PT");
            pt->setAttribute ("t", (double) mseg.points[i].time);
            pt->setAttribute ("v", (double) mseg.points[i].value);
            pt->setAttribute ("c", mseg.points[i].curve);
            if (std::abs (mseg.points[i].tension) > 0.001f)
                pt->setAttribute ("tn", (double) mseg.points[i].tension);
            if (std::abs (mseg.points[i].auxModY) > 0.001f)
                pt->setAttribute ("my", (double) mseg.points[i].auxModY);
            if (std::abs (mseg.points[i].auxModX) > 0.001f)
                pt->setAttribute ("mx", (double) mseg.points[i].auxModX);
            if (std::abs (mseg.points[i].crossModY[0]) > 0.001f)
                pt->setAttribute ("cy0", (double) mseg.points[i].crossModY[0]);
            if (std::abs (mseg.points[i].crossModX[0]) > 0.001f)
                pt->setAttribute ("cx0", (double) mseg.points[i].crossModX[0]);
            if (std::abs (mseg.points[i].crossModY[1]) > 0.001f)
                pt->setAttribute ("cy1", (double) mseg.points[i].crossModY[1]);
            if (std::abs (mseg.points[i].crossModX[1]) > 0.001f)
                pt->setAttribute ("cx1", (double) mseg.points[i].crossModX[1]);
        }
        // Extra modulation routes
        for (int ri = 0; ri < 16; ++ri)
        {
            auto& r = mseg.extraRoutes[static_cast<size_t>(ri)];
            if (r.target >= 0)
            {
                auto* rel = mel->createNewChildElement ("Route");
                rel->setAttribute ("idx", ri);
                rel->setAttribute ("tgt", r.target);
                rel->setAttribute ("dep", (double) r.depth);
            }
        }
    }

    static void loadMSEGData (const juce::XmlElement& el, MSEGData& mseg, int idx)
    {
        juce::String tag = "MSEG" + juce::String (idx);
        auto* mel = el.getChildByName (tag);
        // Backward compat: old presets have single "MSEG" tag → load into index 0
        if (!mel && idx == 0) mel = el.getChildByName ("MSEG");
        if (!mel) return;
        mseg.target = mel->getIntAttribute ("target", mseg.target);
        mseg.depth = (float) mel->getDoubleAttribute ("depth", mseg.depth);
        mseg.totalTime = (float) mel->getDoubleAttribute ("totalTime", mseg.totalTime);
        mseg.tempoSync = mel->getBoolAttribute ("tempoSync", mseg.tempoSync);
        mseg.syncDiv = mel->getIntAttribute ("syncDiv", mseg.syncDiv);
        mseg.loopMode = std::clamp (mel->getIntAttribute ("loopMode", mseg.loopMode), 0, 3);
        mseg.gridX = std::clamp (mel->getIntAttribute ("gridX", mseg.gridX), 1, 32);
        mseg.gridY = std::clamp (mel->getIntAttribute ("gridY", mseg.gridY), 1, 16);
        mseg.transportSync = mel->getBoolAttribute ("transportSync", mseg.transportSync);
        mseg.auxRate = std::clamp ((float) mel->getDoubleAttribute ("auxRate", mseg.auxRate), 0.05f, 20.0f);
        mseg.auxShape = std::clamp (mel->getIntAttribute ("auxShape", mseg.auxShape), 0, 4);
        mseg.auxSync = mel->getBoolAttribute ("auxSync", mseg.auxSync);
        mseg.auxSyncDiv = std::clamp (mel->getIntAttribute ("auxSyncDiv", mseg.auxSyncDiv), 0, 10);
        mseg.fadeIn = std::clamp ((float) mel->getDoubleAttribute ("fadeIn", mseg.fadeIn), 0.0f, 10.0f);
        mseg.fadeInSync = mel->getBoolAttribute ("fadeInSync", mseg.fadeInSync);

        // Load points
        int pi = 0;
        for (auto* pt = mel->getChildByName ("PT"); pt && pi < MSEGData::kMaxPoints;
             pt = pt->getNextElementWithTagName ("PT"))
        {
            mseg.points[pi].time  = std::clamp ((float) pt->getDoubleAttribute ("t", 0.0), 0.0f, 1.0f);
            mseg.points[pi].value = std::clamp ((float) pt->getDoubleAttribute ("v", 0.5), 0.0f, 1.0f);
            mseg.points[pi].curve = std::clamp (pt->getIntAttribute ("c", 0), 0, 28);
            mseg.points[pi].tension = std::clamp ((float) pt->getDoubleAttribute ("tn", 0.0), -1.0f, 1.0f);
            mseg.points[pi].auxModY = std::clamp ((float) pt->getDoubleAttribute ("my", 0.0), -1.0f, 1.0f);
            mseg.points[pi].auxModX = std::clamp ((float) pt->getDoubleAttribute ("mx", 0.0), -1.0f, 1.0f);
            // Per-source cross-MSEG mod (new: cy0/cx0/cy1/cx1, old compat: cy/cx → source 0)
            float oldCY = std::clamp ((float) pt->getDoubleAttribute ("cy", 0.0), -1.0f, 1.0f);
            float oldCX = std::clamp ((float) pt->getDoubleAttribute ("cx", 0.0), -1.0f, 1.0f);
            mseg.points[pi].crossModY[0] = std::clamp ((float) pt->getDoubleAttribute ("cy0", oldCY), -1.0f, 1.0f);
            mseg.points[pi].crossModX[0] = std::clamp ((float) pt->getDoubleAttribute ("cx0", oldCX), -1.0f, 1.0f);
            mseg.points[pi].crossModY[1] = std::clamp ((float) pt->getDoubleAttribute ("cy1", 0.0), -1.0f, 1.0f);
            mseg.points[pi].crossModX[1] = std::clamp ((float) pt->getDoubleAttribute ("cx1", 0.0), -1.0f, 1.0f);
            pi++;
        }
        mseg.numPoints = std::max (pi, 2);
        mseg.points[0].time = 0.0f;
        mseg.points[mseg.numPoints - 1].time = 1.0f;
        int ls = mel->getIntAttribute ("loopStart", -1);
        int le = mel->getIntAttribute ("loopEnd", -1);
        mseg.loopStart = (ls >= 0 && ls < mseg.numPoints) ? ls : -1;
        mseg.loopEnd   = (le > mseg.loopStart && le < mseg.numPoints) ? le : -1;
        if (mseg.loopStart < 0) mseg.loopEnd = -1;
        // Load extra modulation routes
        for (auto* rel = mel->getChildByName ("Route"); rel; rel = rel->getNextElementWithTagName ("Route"))
        {
            int ri = rel->getIntAttribute ("idx", -1);
            if (ri >= 0 && ri < 16)
            {
                mseg.extraRoutes[static_cast<size_t>(ri)].target = rel->getIntAttribute ("tgt", -1);
                mseg.extraRoutes[static_cast<size_t>(ri)].depth = (float) rel->getDoubleAttribute ("dep", 0.0);
            }
        }
    }

    // The global serialization reuses these helpers
    friend class GrooveBoxState;
};
