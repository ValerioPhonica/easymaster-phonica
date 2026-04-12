#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <string>
#include <array>

// ── Macro System (4 knobs, unlimited assignments per macro) ──
// Each macro controls multiple parameters with individual depth.
// Modulation: param = baseValue + macroValue * depth * (max - min)

struct MacroAssignment
{
    int    trackType  = 0;     // 0=synth, 1=drum, 2=masterFX
    int    trackIndex = 0;     // which track (0-4 synth, 0-9 drum, 0=master)
    std::string paramKey;      // parameter name (matches p-lock keys)
    float  depth     = 0.5f;   // -1.0 to 1.0 — individual mod depth
    float  baseValue = 0.0f;   // param value when macro = 0
    float  minVal    = 0.0f;   // parameter range minimum
    float  maxVal    = 1.0f;   // parameter range maximum
};

struct MacroKnob
{
    float value = 0.0f;        // 0.0 to 1.0
    bool  armed = false;       // true = waiting for param assignment
    std::vector<MacroAssignment> assignments;
    juce::String name;         // e.g. "M1", "M2"

    void clear() { assignments.clear(); }

    void removeAssignment (int index)
    {
        if (index >= 0 && index < static_cast<int>(assignments.size()))
            assignments.erase (assignments.begin() + index);
    }

    void addAssignment (int tType, int tIdx, const std::string& key,
                        float base, float lo, float hi, float d = 0.5f)
    {
        // Check for duplicate — same track + param → update depth
        for (auto& a : assignments)
        {
            if (a.trackType == tType && a.trackIndex == tIdx && a.paramKey == key)
            {
                a.depth = d;
                a.baseValue = base;
                return;
            }
        }
        assignments.push_back ({ tType, tIdx, key, d, base, lo, hi });
    }

    // Apply macro modulation: returns the modulated value for a given assignment
    float getModulatedValue (const MacroAssignment& a) const
    {
        float range = a.maxVal - a.minVal;
        float mod = a.baseValue + value * a.depth * range;
        return std::clamp (mod, a.minVal, a.maxVal);
    }
};

struct MacroEngine
{
    static constexpr int kNumMacros = 4;
    std::array<MacroKnob, kNumMacros> macros;
    int armedMacro = -1;  // which macro is armed (-1 = none)

    MacroEngine()
    {
        for (int i = 0; i < kNumMacros; ++i)
            macros[static_cast<size_t>(i)].name = "M" + juce::String (i + 1);
    }

    void armMacro (int idx)
    {
        if (idx >= 0 && idx < kNumMacros)
        {
            // Toggle: if same macro, disarm; otherwise arm new
            if (armedMacro == idx)
                armedMacro = -1;
            else
                armedMacro = idx;
            // Clear all armed flags
            for (int i = 0; i < kNumMacros; ++i)
                macros[static_cast<size_t>(i)].armed = (i == armedMacro);
        }
    }

    void disarm() { armedMacro = -1; for (auto& m : macros) m.armed = false; }

    bool isAnyArmed() const { return armedMacro >= 0; }

    // Called when user moves a knob while a macro is armed
    void captureAssignment (int tType, int tIdx, const std::string& key,
                            float currentVal, float lo, float hi, float depth = 0.5f)
    {
        if (armedMacro >= 0 && armedMacro < kNumMacros)
        {
            macros[static_cast<size_t>(armedMacro)].addAssignment (
                tType, tIdx, key, currentVal, lo, hi, depth);
        }
    }

    // ── Save/Load ──
    void saveToXml (juce::XmlElement& parent) const
    {
        auto* macroEl = parent.createNewChildElement ("Macros");
        for (int i = 0; i < kNumMacros; ++i)
        {
            auto& m = macros[static_cast<size_t>(i)];
            auto* mk = macroEl->createNewChildElement ("Macro");
            mk->setAttribute ("index", i);
            mk->setAttribute ("value", static_cast<double>(m.value));
            for (const auto& a : m.assignments)
            {
                auto* ae = mk->createNewChildElement ("Assign");
                ae->setAttribute ("tt", a.trackType);
                ae->setAttribute ("ti", a.trackIndex);
                ae->setAttribute ("key", juce::String (a.paramKey));
                ae->setAttribute ("depth", static_cast<double>(a.depth));
                ae->setAttribute ("base", static_cast<double>(a.baseValue));
                ae->setAttribute ("min", static_cast<double>(a.minVal));
                ae->setAttribute ("max", static_cast<double>(a.maxVal));
            }
        }
    }

    void loadFromXml (const juce::XmlElement& parent)
    {
        auto* macroEl = parent.getChildByName ("Macros");
        if (!macroEl) return;
        for (auto* mk : macroEl->getChildIterator())
        {
            int idx = mk->getIntAttribute ("index", -1);
            if (idx < 0 || idx >= kNumMacros) continue;
            auto& m = macros[static_cast<size_t>(idx)];
            m.value = static_cast<float>(mk->getDoubleAttribute ("value", 0.0));
            m.assignments.clear();
            for (auto* ae : mk->getChildIterator())
            {
                MacroAssignment a;
                a.trackType = ae->getIntAttribute ("tt", 0);
                a.trackIndex = ae->getIntAttribute ("ti", 0);
                a.paramKey = ae->getStringAttribute ("key").toStdString();
                a.depth = static_cast<float>(ae->getDoubleAttribute ("depth", 0.5));
                a.baseValue = static_cast<float>(ae->getDoubleAttribute ("base", 0.0));
                a.minVal = static_cast<float>(ae->getDoubleAttribute ("min", 0.0));
                a.maxVal = static_cast<float>(ae->getDoubleAttribute ("max", 1.0));
                m.assignments.push_back (a);
            }
        }
    }
};
