#pragma once
#include <optional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "KnobComponent.h"
#include "../../State/GrooveBoxState.h"
#include "../Colours.h"

class MasterFXRow : public juce::Component
{
public:
    MasterFXRow (GrooveBoxState& s) : state (s)
    {
        auto setupTab = [this](juce::TextButton& btn, int idx, const juce::String& name) {
            addAndMakeVisible (btn);
            btn.setButtonText (name);
            btn.setClickingTogglesState (false);
            btn.onClick = [this, idx]() { currentTab = idx; buildKnobs(); resized(); repaint(); };
        };
        setupTab (tabEQ, 0, "EQ");
        setupTab (tabComp, 1, "CMP");
        setupTab (tabLim, 2, "LIM");
        setupTab (tabGater, 3, "GTR");
        setupTab (tabDelay, 4, "DLY");
        setupTab (tabDJFilt, 5, "FLT");
        setupTab (tabMeter, 6, "Q");

        // PLK button for master FX p-lock recording
        addAndMakeVisible (plkBtn);
        plkBtn.setButtonText ("PLK");
        plkBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a2a));
        plkBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffa0a0a0));
        plkBtn.onClick = [this]() {
            plockMode = !plockMode;
            plkBtn.setButtonText (plockMode ? "PLK*" : "PLK");
            plkBtn.setColour (juce::TextButton::buttonColourId,
                              plockMode ? juce::Colour (0xff804020) : juce::Colour (0xff2a2a2a));
        };

        addAndMakeVisible (masterLabel);
        masterLabel.setText ("MASTER", juce::dontSendNotification);
        masterLabel.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
        masterLabel.setColour (juce::Label::textColourId, Colours_GB::accent);
        masterLabel.setJustificationType (juce::Justification::centredLeft);

        // Master chain preset save/load
        addAndMakeVisible (saveBtn);
        saveBtn.setButtonText ("SAV");
        saveBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a3050));
        saveBtn.setColour (juce::TextButton::textColourOffId, Colours_GB::accent);
        saveBtn.onClick = [this]() {
            masterChooser = std::make_shared<juce::FileChooser> ("Save Master Chain", juce::File(), "*.gbmaster");
            auto flags = juce::FileBrowserComponent::saveMode
                       | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::warnAboutOverwriting;
            masterChooser->launchAsync (flags, [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File())
                    file.replaceWithText (state.saveMasterChainToXml());
            });
        };
        addAndMakeVisible (loadBtn);
        loadBtn.setButtonText ("LDR");
        loadBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a3050));
        loadBtn.setColour (juce::TextButton::textColourOffId, Colours_GB::accent);
        loadBtn.onClick = [this]() {
            masterChooser = std::make_shared<juce::FileChooser> ("Load Master Chain", juce::File(), "*.gbmaster");
            auto flags = juce::FileBrowserComponent::openMode
                       | juce::FileBrowserComponent::canSelectFiles;
            masterChooser->launchAsync (flags, [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File())
                {
                    state.loadMasterChainFromXml (file.loadFileAsString());
                    buildKnobs(); repaint();
                }
            });
        };

        buildKnobs();
    }

    void buildKnobs()
    {
        for (auto* k : knobs) removeChildComponent (k);
        knobs.clear();
        knobPlockKeys.clear();

        if (currentTab == 0) // PULTEC EQ: LOW(B,A,F) → HIGH boost(B,F,BW) → HIGH atten(A,AF)
        {
            addKnob ("LO.B", 0, 10, state.pultecLowBoost.load(),
                     [this](float v){ state.pultecLowBoost.store (v); },
                     [](float v){ return juce::String (v, 1) + "dB"; }, juce::Colour (0xff60b080));
            addKnob ("LO.A", 0, 10, state.pultecLowAtten.load(),
                     [this](float v){ state.pultecLowAtten.store (v); },
                     [](float v){ return juce::String (v, 1) + "dB"; }, juce::Colour (0xff60b080));
            addKnob ("LO.F", 20, 200, state.pultecLowFreq.load(),
                     [this](float v){ state.pultecLowFreq.store (v); },
                     [](float v){ return juce::String (static_cast<int>(v)) + "Hz"; }, juce::Colour (0xff60b080));
            addKnob ("HI.B", 0, 10, state.pultecHighBoost.load(),
                     [this](float v){ state.pultecHighBoost.store (v); },
                     [](float v){ return juce::String (v, 1) + "dB"; }, juce::Colour (0xff50a0d0));
            addKnob ("HI.F", 3000, 16000, state.pultecHighFreq.load(),
                     [this](float v){ state.pultecHighFreq.store (v); },
                     [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "k" : juce::String (static_cast<int>(v)); },
                     juce::Colour (0xff50a0d0));
            addKnob ("BW", 0.3f, 4.0f, state.pultecHighBW.load(),
                     [this](float v){ state.pultecHighBW.store (v); },
                     [](float v){ return juce::String (v, 1); }, juce::Colour (0xff50a0d0));
            addKnob ("HI.A", 0, 10, state.pultecHighAtten.load(),
                     [this](float v){ state.pultecHighAtten.store (v); },
                     [](float v){ return juce::String (v, 1) + "dB"; }, juce::Colour (0xffc07090));
            addKnob ("A.FQ", 5000, 20000, state.pultecHiAttnFrq.load(),
                     [this](float v){ state.pultecHiAttnFrq.store (v); },
                     [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "k" : juce::String (static_cast<int>(v)); },
                     juce::Colour (0xffc07090));
        }
        else if (currentTab == 1)
        {
            addKnob ("STYL", 0, 3, static_cast<float>(state.compStyle.load()),
                     [this](float v){ state.compStyle.store (static_cast<int>(v + 0.5f)); },
                     [](float v){
                         const char* n[] = {"CLN","WRM","PNCH","GLUE"};
                         return juce::String (n[static_cast<int>(v + 0.5f) % 4]);
                     }, juce::Colour (0xffd08040));
            addKnob ("THRS", -40, 0, state.compThreshold.load(),
                     [this](float v){ state.compThreshold.store (v); },
                     [](float v){ return juce::String (static_cast<int>(v)) + "dB"; }, juce::Colour (0xff50a0d0));
            addKnob ("RATIO", 1, 20, state.compRatio.load(),
                     [this](float v){ state.compRatio.store (v); },
                     [](float v){ return juce::String (v, 1) + ":1"; }, juce::Colour (0xff50a0d0));
            addKnob ("KNEE", 0, 12, state.compKnee.load(),
                     [this](float v){ state.compKnee.store (v); },
                     [](float v){ return juce::String (v, 1) + "dB"; }, juce::Colour (0xff50a0d0));
            addKnob ("ATK", 0.1f, 100, state.compAttack.load(),
                     [this](float v){ state.compAttack.store (v); },
                     [](float v){ return juce::String (v, 1) + "ms"; }, juce::Colour (0xff60b080));
            addKnob ("REL", 10, 1000, state.compRelease.load(),
                     [this](float v){ state.compRelease.store (v); },
                     [](float v){ return v >= 1000 ? juce::String (v / 1000.0f, 1) + "s" : juce::String (static_cast<int>(v)) + "ms"; },
                     juce::Colour (0xff60b080));
            addKnob ("GAIN", 0, 12, state.compMakeup.load(),
                     [this](float v){ state.compMakeup.store (v); },
                     [](float v){ return juce::String (v, 1) + "dB"; }, juce::Colour (0xffc06040));
            addKnob ("SC HP", 0, 500, state.compScHP.load(),
                     [this](float v){ state.compScHP.store (v); },
                     [](float v){ return v < 16.0f ? juce::String ("OFF") : juce::String (static_cast<int>(v)) + "Hz"; },
                     juce::Colour (0xffa060c0)); // purple — sidechain section
        }
        else if (currentTab == 2) // LIMITER
        {
            addKnob ("IN", 0, 12, state.limInputGain.load(),
                     [this](float v){ state.limInputGain.store (v); },
                     [](float v){ return v < 0.1f ? juce::String ("0dB") : juce::String ("+") + juce::String (v, 1) + "dB"; },
                     juce::Colour (0xffc0a030));
            addKnob ("CEIL", -3, 0, state.limCeiling.load(),
                     [this](float v){ state.limCeiling.store (v); },
                     [](float v){ return juce::String (v, 1) + "dB"; }, juce::Colour (0xffc06040));
            addKnob ("REL", 10, 500, state.limRelease.load(),
                     [this](float v){ state.limRelease.store (v); },
                     [](float v){ return juce::String (static_cast<int>(v)) + "ms"; }, juce::Colour (0xffc06040));
            addKnob ("A.RL", 0, 1, state.limAutoRel.load(),
                     [this](float v){ state.limAutoRel.store (v); },
                     [](float v){ return v > 0.5f ? juce::String ("ON") : juce::String ("OFF"); },
                     juce::Colour (0xff60b080));
        }
        else if (currentTab == 3) // GATER
        {
            addKnob ("MIX", 0, 1, state.gaterMix.load(),
                     [this](float v){ state.gaterMix.store (v); },
                     [](float v){ return v < 0.02f ? juce::String ("OFF") : juce::String (static_cast<int>(v * 100)) + "%"; },
                     juce::Colour (0xffc0a030), "gaterMix");
            addKnob ("RATE", 0, 7, state.gaterRate.load(),
                     [this](float v){ state.gaterRate.store (v); },
                     [](float v){
                         const char* n[] = {"1/64","1/32","3/32","1/16","1/8","1/4","3/8","1/2"};
                         return juce::String (n[std::clamp (static_cast<int>(v), 0, 7)]);
                     }, juce::Colour (0xffc0a030), "gaterRate");
            addKnob ("DPTH", 0, 1, state.gaterDepth.load(),
                     [this](float v){ state.gaterDepth.store (v); },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; },
                     juce::Colour (0xffc0a030), "gaterDepth");
            addKnob ("SHPE", 0, 4, state.gaterShape.load(),
                     [this](float v){ state.gaterShape.store (v); },
                     [](float v){
                         const char* n[] = {"SQR","SAW","RMP","TRI","FLT"};
                         return juce::String (n[std::clamp (static_cast<int>(v), 0, 4)]);
                     }, juce::Colour (0xffc0a030), "gaterShape");
            addKnob ("SMTH", 0, 0.5f, state.gaterSmooth.load(),
                     [this](float v){ state.gaterSmooth.store (v); },
                     [](float v){ return juce::String (static_cast<int>(v * 1000)) + "ms"; },
                     juce::Colour (0xffc0a030), "gaterSmooth");
        }
        else if (currentTab == 4) // DELAY (IDENTICAL to per-track)
        {
            addKnob ("D.AL", 0, 3, state.mDelayAlgo.load(),
                     [this](float v){ state.mDelayAlgo.store (v); },
                     [](float v){ const char* n[] = {"DIG","TAPE","BBD","DIFF"}; return juce::String (n[static_cast<int>(v) % 4]); },
                     juce::Colour (0xff60b080), "mDelayAlgo");
            addKnob ("DLY", 0, 1, state.mDelayMix.load(),
                     [this](float v){ state.mDelayMix.store (v); },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; },
                     juce::Colour (0xff60b080), "mDelayMix");
            addKnob ("D.S", 0, 1, state.mDelaySync.load(),
                     [this](float v){ state.mDelaySync.store (v); },
                     [](float v){ return v > 0.5f ? juce::String ("SYN") : juce::String ("FRE"); },
                     juce::Colour (0xff60b080), "mDelaySync");
            addKnob ("D.B", 0.125f, 4.0f, state.mDelayBeats.load(),
                     [this](float v){
                         const float divs[] = {0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
                         float best = divs[0]; float bestDist = 999.0f;
                         for (float d : divs) { float dist = std::abs(v - d); if (dist < bestDist) { bestDist = dist; best = d; } }
                         state.mDelayBeats.store (best);
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
                     }, juce::Colour (0xff60b080), "mDelayBeats");
            addKnob ("D.TM", 0.001f, 2.0f, state.mDelayTime.load(),
                     [this](float v){ state.mDelayTime.store (v); },
                     [](float v){ return v < 1.0f ? juce::String (static_cast<int>(v * 1000)) + "ms"
                                                   : juce::String (v, 1) + "s"; },
                     juce::Colour (0xff60b080), "mDelayTime");
            addKnob ("D.FB", 0, 0.9f, state.mDelayFB.load(),
                     [this](float v){ state.mDelayFB.store (v); },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; },
                     juce::Colour (0xff60b080), "mDelayFB");
            addKnob ("D.P", 0, 1, state.mDelayPP.load(),
                     [this](float v){ state.mDelayPP.store (v); },
                     [](float v){ return static_cast<int>(v) > 0 ? juce::String ("P.P") : juce::String ("MNO"); },
                     juce::Colour (0xff60b080), "mDelayPP");
            addKnob ("D.LP", 0, 1, state.mDelayDamp.load(),
                     [this](float v){ state.mDelayDamp.store (v); },
                     [](float v){
                         if (v < 0.1f) return juce::String ("BRT");
                         if (v < 0.4f) return juce::String ("AIR");
                         if (v < 0.7f) return juce::String ("WRM");
                         return juce::String ("DRK");
                     }, juce::Colour (0xff60b080), "mDelayDamp");
        }
        else if (currentTab == 5) // DJ FILTER
        {
            addKnob ("FREQ", 0, 1, state.djFilterFreq.load(),
                     [this](float v){ state.djFilterFreq.store (v); },
                     [](float v){
                         if (std::abs (v - 0.5f) < 0.03f) return juce::String ("OFF");
                         if (v < 0.5f) {
                             float hz = 200.0f * std::pow (100.0f, v * 2.0f);
                             return juce::String ("LP ") + (hz >= 1000 ? juce::String (hz / 1000, 1) + "k" : juce::String (static_cast<int>(hz)));
                         }
                         float hz = 20.0f * std::pow (250.0f, (v - 0.5f) * 2.0f);
                         return juce::String ("HP ") + (hz >= 1000 ? juce::String (hz / 1000, 1) + "k" : juce::String (static_cast<int>(hz)));
                     }, juce::Colour (0xffc06040), "djFilterFreq");
            addKnob ("RES", 0, 1, state.djFilterRes.load(),
                     [this](float v){ state.djFilterRes.store (v); },
                     [](float v){ return juce::String (static_cast<int>(v * 100)) + "%"; },
                     juce::Colour (0xffc06040), "djFilterRes");
        }
        else // QUALITY (tab 6)
        {
            addKnob ("QUAL", 0, 2, static_cast<float>(state.quality.load()),
                     [this](float v){ state.quality.store (static_cast<int>(v + 0.5f)); },
                     [](float v){
                         int q = static_cast<int>(v + 0.5f);
                         switch (q) {
                             case 0: return juce::String ("ECO");
                             case 1: return juce::String ("STD");
                             case 2: return juce::String ("HQ");
                             default: return juce::String ("STD");
                         }
                     }, juce::Colour (0xff40c080));
        }
    }

    // Peak/GR levels now displayed in HeaderBar only

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (4, 2);

        // Leave right space for MIX OUT panel visual extension (matches HeaderBar)
        mixOutExtArea = bounds.removeFromRight (130);
        bounds.removeFromRight (4);

        auto leftCol = bounds.removeFromLeft (36);
        masterLabel.setBounds (leftCol.removeFromTop (12));
        leftCol.removeFromTop (1);
        saveBtn.setBounds (leftCol.removeFromBottom (11).reduced (1, 0));
        loadBtn.setBounds (leftCol.removeFromBottom (11).reduced (1, 0));
        leftCol.removeFromBottom (1);
        int tabH = std::min (12, (leftCol.getHeight() - 2) / 7);
        tabEQ.setBounds     (leftCol.removeFromTop (tabH).reduced (0, 0));
        tabComp.setBounds   (leftCol.removeFromTop (tabH).reduced (0, 0));
        tabLim.setBounds    (leftCol.removeFromTop (tabH).reduced (0, 0));
        tabGater.setBounds  (leftCol.removeFromTop (tabH).reduced (0, 0));
        tabDelay.setBounds  (leftCol.removeFromTop (tabH).reduced (0, 0));
        tabDJFilt.setBounds (leftCol.removeFromTop (tabH).reduced (0, 0));
        tabMeter.setBounds  (leftCol.removeFromTop (tabH).reduced (0, 0));

        auto ac = Colours_GB::accent;
        auto ic = juce::Colour (0xff2a2a2a);
        auto tc_on = juce::Colour (0xff000000);   // black text when selected
        auto tc_off = juce::Colour (0xffa0a0a0);  // gray text when not selected
        tabEQ.setColour     (juce::TextButton::buttonColourId, currentTab == 0 ? ac : ic);
        tabComp.setColour   (juce::TextButton::buttonColourId, currentTab == 1 ? ac : ic);
        tabLim.setColour    (juce::TextButton::buttonColourId, currentTab == 2 ? ac : ic);
        tabGater.setColour  (juce::TextButton::buttonColourId, currentTab == 3 ? ac : ic);
        tabDelay.setColour  (juce::TextButton::buttonColourId, currentTab == 4 ? ac : ic);
        tabDJFilt.setColour (juce::TextButton::buttonColourId, currentTab == 5 ? ac : ic);
        tabMeter.setColour  (juce::TextButton::buttonColourId, currentTab == 6 ? ac : ic);
        tabEQ.setColour     (juce::TextButton::textColourOffId, currentTab == 0 ? tc_on : tc_off);
        tabComp.setColour   (juce::TextButton::textColourOffId, currentTab == 1 ? tc_on : tc_off);
        tabLim.setColour    (juce::TextButton::textColourOffId, currentTab == 2 ? tc_on : tc_off);
        tabGater.setColour  (juce::TextButton::textColourOffId, currentTab == 3 ? tc_on : tc_off);
        tabDelay.setColour  (juce::TextButton::textColourOffId, currentTab == 4 ? tc_on : tc_off);
        tabDJFilt.setColour (juce::TextButton::textColourOffId, currentTab == 5 ? tc_on : tc_off);
        tabMeter.setColour  (juce::TextButton::textColourOffId, currentTab == 6 ? tc_on : tc_off);

        bounds.removeFromLeft (4);

        // PLK button — only on creative tabs
        bool isCreativeTab = (currentTab >= 3 && currentTab <= 5);
        plkBtn.setVisible (isCreativeTab);
        if (isCreativeTab)
            plkBtn.setBounds (bounds.removeFromRight (28).removeFromTop (16));

        bounds.removeFromRight (4);

        // Step grid — bottom row on creative tabs
        stepGridArea = {};
        if (isCreativeTab)
        {
            stepGridArea = bounds.removeFromBottom (22);
            bounds.removeFromBottom (2);
        }

        // #4: Add left margin so knob labels don't overlap tab header text
        bounds.removeFromLeft (24);

        int numK = static_cast<int>(knobs.size());
        if (numK > 0)
        {
            int knobW = std::min (55, bounds.getWidth() / numK);
            int spacing = knobW;
            for (int i = 0; i < numK; ++i)
                knobs[i]->setBounds (bounds.getX() + i * spacing, bounds.getY(), knobW, bounds.getHeight());
        }
    }

    void paint (juce::Graphics& g) override
    {
        // Main area background
        auto mainArea = getLocalBounds();
        if (!mixOutExtArea.isEmpty())
            mainArea.removeFromRight (mixOutExtArea.getWidth() + 4);
        g.setColour (Colours_GB::knobTrack);
        g.fillRoundedRectangle (mainArea.toFloat(), 4.0f);
        g.setColour (Colours_GB::border);
        g.drawRoundedRectangle (mainArea.toFloat().reduced (0.5f), 4.0f, 0.5f);

        // MIX OUT extension panel (visually continues HeaderBar's MIX OUT)
        if (!mixOutExtArea.isEmpty())
        {
            auto mp = mixOutExtArea.toFloat();
            g.setColour (juce::Colour (0xff0e0e12));
            g.fillRoundedRectangle (mp.getX(), mp.getY(), mp.getWidth(), mp.getHeight(), 4.0f);
            g.setColour (juce::Colour (0xff2a2a30));
            g.drawRoundedRectangle (mp.getX(), mp.getY(), mp.getWidth(), mp.getHeight(), 4.0f, 0.5f);
        }

        // Module name removed — already shown in tab selection menu on left

        // ── Step Grid (MX-1 style, on creative tabs) ──
        if (!stepGridArea.isEmpty() && (currentTab >= 3 && currentTab <= 5))
        {
            int numSteps = std::max (1, state.masterFXLength.load());
            int visSteps = std::min (numSteps, 16);
            float sw = static_cast<float>(stepGridArea.getWidth()) / visSteps;
            float sy = static_cast<float>(stepGridArea.getY());
            float sh = static_cast<float>(stepGridArea.getHeight());

            for (int s = 0; s < visSteps; ++s)
            {
                float sx = stepGridArea.getX() + s * sw;
                auto& step = state.masterFXSeq.steps[static_cast<size_t>(s)];
                bool active = step.active;
                bool playing = (s == playingStep);
                bool selected = (s == selectedStep && plockMode);

                // Background
                juce::Colour bg = active ? Colours_GB::accentDim.brighter (0.2f) : Colours_GB::knobTrack;
                if (playing) bg = active ? juce::Colour (0xffc08020) : juce::Colour (0xff404020);
                if (selected) bg = bg.brighter (0.3f);
                g.setColour (bg);
                g.fillRoundedRectangle (sx + 1, sy + 1, sw - 2, sh - 2, 2.0f);

                // Border
                g.setColour (selected ? Colours_GB::accent : Colours_GB::border);
                g.drawRoundedRectangle (sx + 1, sy + 1, sw - 2, sh - 2, 2.0f, 0.5f);

                // Step number
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
                g.setColour (active ? juce::Colour (0xffe0c060) : juce::Colour (0xff505050));
                g.drawText (juce::String (s + 1), static_cast<int>(sx), static_cast<int>(sy),
                            static_cast<int>(sw), static_cast<int>(sh), juce::Justification::centred, false);

                // P-lock indicator (dot)
                if (!step.plocks.empty())
                {
                    g.setColour (juce::Colour (0xffff6030));
                    g.fillEllipse (sx + sw - 6, sy + 2, 3, 3);
                }

                // Playhead bar
                if (playing)
                {
                    g.setColour (Colours_GB::accent);
                    g.fillRect (sx + 1, sy + sh - 3, sw - 2, 2.0f);
                }
            }
        }
    }

    int getDesiredHeight() const { return 110; }

    void setPlayingStep (int s) { playingStep = s; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (stepGridArea.isEmpty() || currentTab < 3 || currentTab > 5) return;
        if (!stepGridArea.contains (e.getPosition())) return;

        int numSteps = std::max (1, state.masterFXLength.load());
        int visSteps = std::min (numSteps, 16);
        float sw = static_cast<float>(stepGridArea.getWidth()) / visSteps;
        int clickedStep = static_cast<int>((e.x - stepGridArea.getX()) / sw);
        clickedStep = std::clamp (clickedStep, 0, visSteps - 1);

        // Right-click: context menu with PLK rec, copy/paste, clear
        if (e.mods.isRightButtonDown())
        {
            auto& step = state.masterFXSeq.steps[static_cast<size_t>(clickedStep)];
            juce::PopupMenu menu;

            // PLK rec toggle for this step
            bool isSelected = (plockMode && selectedStep == clickedStep);
            menu.addItem (1, isSelected ? "EXIT P-LOCK REC" : "P-LOCK REC (step " + juce::String (clickedStep + 1) + ")");
            menu.addSeparator();

            // Copy / Paste
            menu.addItem (2, "COPY STEP");
            menu.addItem (3, "PASTE STEP", masterFxClipboard.has_value());
            menu.addSeparator();

            // Clear options
            menu.addItem (4, "CLEAR STEP (deactivate + clear plocks)");
            if (!step.plocks.empty())
            {
                menu.addItem (5, "CLEAR ALL P-LOCKS (" + juce::String (static_cast<int>(step.plocks.size())) + ")");
                menu.addSeparator();
                int idx = 100;
                for (const auto& [key, val] : step.plocks)
                {
                    menu.addItem (idx, "x  " + juce::String (key) + " = " + juce::String (val, 2));
                    ++idx;
                }
            }

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                [this, clickedStep](int result) {
                    if (result == 0) return;
                    auto& s = state.masterFXSeq.steps[static_cast<size_t>(clickedStep)];
                    if (result == 1) // PLK REC toggle
                    {
                        if (plockMode && selectedStep == clickedStep)
                        {
                            plockMode = false;
                            selectedStep = -1;
                            plkBtn.setButtonText ("PLK");
                            plkBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a2a));
                        }
                        else
                        {
                            plockMode = true;
                            selectedStep = clickedStep;
                            plkBtn.setButtonText ("PLK*");
                            plkBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff804020));
                            // Recall p-lock values to knobs
                            for (auto& [ki, pk] : knobPlockKeys)
                            {
                                if (ki < knobs.size())
                                {
                                    auto it = s.plocks.find (pk);
                                    if (it != s.plocks.end())
                                        knobs[ki]->setValue (it->second);
                                }
                            }
                        }
                    }
                    else if (result == 2) // COPY
                    {
                        masterFxClipboard = s;
                    }
                    else if (result == 3 && masterFxClipboard.has_value()) // PASTE
                    {
                        s = *masterFxClipboard;
                    }
                    else if (result == 4) // CLEAR STEP
                    {
                        s.reset();
                    }
                    else if (result == 5) // CLEAR ALL PLOCKS
                    {
                        s.plocks.clear();
                    }
                    else if (result >= 100) // CLEAR individual plock
                    {
                        int idx = 0;
                        for (auto it = s.plocks.begin(); it != s.plocks.end(); ++it, ++idx)
                        {
                            if (idx == result - 100) { s.plocks.erase (it); break; }
                        }
                    }
                    repaint();
                });
            return;
        }

        if (plockMode)
        {
            // In PLK mode: select step for p-lock editing
            selectedStep = (selectedStep == clickedStep) ? -1 : clickedStep;
            // Recall p-lock values to knobs so user sees what's stored
            if (selectedStep >= 0)
            {
                auto& step = state.masterFXSeq.steps[static_cast<size_t>(selectedStep)];
                for (auto& [ki, pk] : knobPlockKeys)
                {
                    if (ki < knobs.size())
                    {
                        auto it = step.plocks.find (pk);
                        if (it != step.plocks.end())
                            knobs[ki]->setValue (it->second);
                    }
                }
            }
        }
        else
        {
            // Normal mode: toggle step active
            auto& step = state.masterFXSeq.steps[static_cast<size_t>(clickedStep)];
            step.active = !step.active;
            if (!step.active) step.reset();
        }
        repaint();
    }

private:
    GrooveBoxState& state;
    int currentTab = 0;
    int playingStep = -1;
    int selectedStep = -1; // for p-lock editing
    bool plockMode = false;
    std::optional<StepData> masterFxClipboard;

    juce::TextButton tabEQ, tabComp, tabLim, tabGater, tabDelay, tabDJFilt, tabMeter;
    juce::TextButton plkBtn;
    juce::TextButton saveBtn, loadBtn;
    std::shared_ptr<juce::FileChooser> masterChooser;
    juce::Label masterLabel;
    juce::Rectangle<int> stepGridArea;
    juce::Rectangle<int> mixOutExtArea;  // visual extension of HeaderBar's MIX OUT panel
    juce::OwnedArray<KnobComponent> knobs;
    std::map<int, std::string> knobPlockKeys; // knob index → plock key

    void addKnob (const juce::String& name, float minV, float maxV, float initV,
                  std::function<void(float)> onChange,
                  std::function<juce::String(float)> fmt, juce::Colour accent,
                  const juce::String& plockKey = "")
    {
        auto* k = new KnobComponent (name, minV, maxV, initV, fmt);
        // Wrap onChange to also write p-lock when in PLK mode on a creative tab
        if (plockKey.isNotEmpty())
        {
            k->onChange = [this, onChange, pk = plockKey.toStdString()](float v) {
                bool inPlock = plockMode && selectedStep >= 0 && selectedStep < kMaxSteps
                    && (currentTab >= 3 && currentTab <= 5);
                if (inPlock)
                {
                    // Only write p-lock, don't change global param
                    auto& step = state.masterFXSeq.steps[static_cast<size_t>(selectedStep)];
                    if (step.active) // only write to already-active steps
                        step.plocks[pk] = v;
                }
                else
                {
                    onChange (v); // normal mode: update global param
                }
            };
        }
        else
        {
            k->onChange = onChange;
        }
        k->setAccentColour (accent);
        // Set plock key for macro param identification (null pointers = no actual plock mode)
        std::string macroKey = plockKey.isNotEmpty() ? plockKey.toStdString() : name.toLowerCase().toStdString();
        k->setupPlock (nullptr, nullptr, nullptr, macroKey);
        k->setupMacro (&state.macroEngine, 2, 0);
        addAndMakeVisible (k);
        if (plockKey.isNotEmpty())
            knobPlockKeys[knobs.size()] = plockKey.toStdString();
        knobs.add (k);
    }

    // Removed: old VU and GR meters are now in HeaderBar
};
