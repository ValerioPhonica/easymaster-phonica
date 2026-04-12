#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Colours.h"
#include "../../Sequencer/StepData.h"
#include "../../State/MacroEngine.h"
#include <functional>
#include <atomic>

class KnobComponent : public juce::Component
{
public:
    KnobComponent (const juce::String& labelText, float minVal, float maxVal, float defaultVal,
                   std::function<juce::String(float)> formatter = nullptr)
        : label (labelText), minValue (minVal), maxValue (maxVal), value (defaultVal),
          defaultValue (defaultVal), formatFunc (formatter)
    {
        setName (labelText);
        setRepaintsOnMouseActivity (true);
    }

    void setAccentColour (juce::Colour c) { accentColour = c; repaint(); }

    void setValue (float v, bool notify = true)
    {
        v = juce::jlimit (minValue, maxValue, v);
        if (std::abs (v - value) > 0.0001f)
        {
            value = v;
            repaint();

            bool plockActive = plockModePtr != nullptr && *plockModePtr
                && plockStepPtr != nullptr && *plockStepPtr >= 0
                && *plockStepPtr < static_cast<int>(kMaxSteps)
                && plockSeqPtr != nullptr;

            if (notify && !plockActive && onChange)
                onChange (value);

            if (notify && plockActive)
            {
                plockSeqPtr->steps[static_cast<size_t>(*plockStepPtr)].plocks[plockKeyStr] = v;
                if (onPlockWritten) onPlockWritten();
            }

            if (notify && dragging && motionRecPtr != nullptr && motionRecPtr->load()
                && motionStepPtr != nullptr && plockSeqPtr != nullptr)
            {
                int mStep = motionStepPtr->load();
                if (mStep >= 0 && mStep < static_cast<int>(kMaxSteps))
                {
                    auto& step = plockSeqPtr->steps[static_cast<size_t>(mStep)];
                    if (step.active)
                    {
                        step.plocks[plockKeyStr] = v;

                        if (motionRecModePtr != nullptr && motionRecModePtr->load() == 1)
                        {
                            if (mStep > 0)
                            {
                                auto& prev = plockSeqPtr->steps[static_cast<size_t>(mStep - 1)];
                                if (prev.active)
                                {
                                    auto it = prev.plocks.find (plockKeyStr);
                                    float prevVal = (it != prev.plocks.end()) ? it->second : motionRecLastVal;
                                    prev.plocks[plockKeyStr] = prevVal + (v - prevVal) * 0.5f;
                                }
                            }
                            if (mStep < static_cast<int>(kMaxSteps) - 1)
                            {
                                auto& next = plockSeqPtr->steps[static_cast<size_t>(mStep + 1)];
                                if (next.active)
                                {
                                    auto it = next.plocks.find (plockKeyStr);
                                    float nextVal = (it != next.plocks.end()) ? it->second : v;
                                    next.plocks[plockKeyStr] = v + (nextVal - v) * 0.5f;
                                }
                            }
                        }
                        motionRecLastVal = v;
                        if (onPlockWritten) onPlockWritten();
                    }
                }
            }
        }
    }

    float getValue() const { return value; }
    float getMinValue() const { return minValue; }
    float getMaxValue() const { return maxValue; }
    juce::String getLabel() const { return label; }

    std::function<void(float)> onChange;
    std::function<void()> onPlockWritten;
    std::function<void()> onClickPopup;
    std::function<void()> onModRightClick; // right-click modulation assignment

    int modTargetId = -1; // LFO/MSEG target ID (-1 = not modulatable)

    void setupPlock (bool* modePtr, int* stepPtr, StepSequence* seqPtr, const std::string& key)
    {
        plockModePtr = modePtr;
        plockStepPtr = stepPtr;
        plockSeqPtr  = seqPtr;
        plockKeyStr  = key;
    }

    void setupMotionRec (std::atomic<bool>* recPtr, std::atomic<int>* stepPtr,
                         std::atomic<int>* modePtr = nullptr)
    {
        motionRecPtr     = recPtr;
        motionStepPtr    = stepPtr;
        motionRecModePtr = modePtr;
    }

    void setupMacro (MacroEngine* engine, int tType, int tIdx)
    {
        macroEnginePtr  = engine;
        macroTrackType  = tType;
        macroTrackIdx   = tIdx;
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        bool hover = isMouseOver();

        // === LABEL (top) — 9px bold, high contrast ===
        auto labelArea = bounds.removeFromTop (11);
        g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 8.5f, juce::Font::bold));
        g.setColour (hover ? juce::Colour (0xffe8ecf4) : juce::Colour (0xff9098a8));
        g.drawText (label, labelArea.toNearestInt(), juce::Justification::centred);

        // === VALUE TEXT (bottom) — accent colored ===
        auto valueArea = bounds.removeFromBottom (11);
        juce::String valText;
        if (formatFunc) valText = formatFunc (value);
        else            valText = juce::String (value, 1);
        g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 8.0f, juce::Font::plain));
        g.setColour (accentColour.withAlpha (hover ? 1.0f : 0.75f));
        g.drawText (valText, valueArea.toNearestInt(), juce::Justification::centred);

        // === KNOB AREA ===
        auto knobArea = bounds.reduced (2, 0);
        float diameter = std::min (knobArea.getWidth(), knobArea.getHeight());
        float radius = diameter / 2.0f;
        float cx = knobArea.getCentreX();
        float cy = knobArea.getCentreY();

        float normVal = (value - minValue) / std::max (0.0001f, maxValue - minValue);
        float startAngle = juce::degreesToRadians (-225.0f);
        float fullSweep = juce::degreesToRadians (270.0f);
        float valAngle = startAngle + normVal * fullSweep;

        // === ARC TRACK (background) ===
        float arcR = radius + 1.5f;
        {
            juce::Path trackArc;
            trackArc.addCentredArc (cx, cy, arcR, arcR, 0.0f, startAngle, startAngle + fullSweep, true);
            g.setColour (juce::Colour (0xff1a1e26));
            g.strokePath (trackArc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                            juce::PathStrokeType::rounded));
        }

        // === ARC FILL (value) - bipolar support ===
        bool isBipolar = (minValue < -0.001f);
        float arcAlpha = hover ? 1.0f : 0.75f;

        if (isBipolar)
        {
            float centerNorm = (-minValue) / std::max (0.0001f, maxValue - minValue);
            float centerAngle = startAngle + centerNorm * fullSweep;

            // Center tick
            float tickLen = 3.0f;
            float tickAngleRad = centerAngle + juce::MathConstants<float>::pi;
            float tx1 = cx + (arcR - 1.0f) * std::sin (tickAngleRad);
            float ty1 = cy + (arcR - 1.0f) * std::cos (tickAngleRad);
            float tx2 = cx + (arcR + tickLen) * std::sin (tickAngleRad);
            float ty2 = cy + (arcR + tickLen) * std::cos (tickAngleRad);
            g.setColour (juce::Colour (0x40ffffff));
            g.drawLine (tx1, ty1, tx2, ty2, 1.0f);

            if (std::abs (normVal - centerNorm) > 0.01f)
            {
                float fromA = (normVal < centerNorm) ? valAngle : centerAngle;
                float toA = (normVal < centerNorm) ? centerAngle : valAngle;
                juce::Path fillArc;
                fillArc.addCentredArc (cx, cy, arcR, arcR, 0.0f, fromA, toA, true);
                g.setColour (accentColour.withAlpha (arcAlpha * 0.12f));
                g.strokePath (fillArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
                g.setColour (accentColour.withAlpha (arcAlpha));
                g.strokePath (fillArc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
            }
        }
        else if (normVal > 0.005f)
        {
            juce::Path fillArc;
            fillArc.addCentredArc (cx, cy, arcR, arcR, 0.0f, startAngle, valAngle, true);
            g.setColour (accentColour.withAlpha (arcAlpha * 0.10f));
            g.strokePath (fillArc, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            g.setColour (accentColour.withAlpha (arcAlpha));
            g.strokePath (fillArc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
        }

        // === KNOB BODY — 3D metallic with strong gradient ===
        float bodyR = radius - 3.5f;
        if (bodyR > 2.0f)
        {
            // Drop shadow
            g.setColour (juce::Colour (0xff040608));
            g.fillEllipse (cx - bodyR - 1.5f, cy - bodyR + 0.5f, (bodyR + 1.5f) * 2, (bodyR + 1.5f) * 2);

            // Outer ring
            g.setColour (hover ? juce::Colour (0xff404858) : juce::Colour (0xff2a2e38));
            g.fillEllipse (cx - bodyR - 0.5f, cy - bodyR - 0.5f, (bodyR + 0.5f) * 2, (bodyR + 0.5f) * 2);

            // Body gradient — strong top-light to bottom-dark
            juce::ColourGradient bodyGrad (
                juce::Colour (0xff444c5c), cx, cy - bodyR * 0.8f,   // bright top
                juce::Colour (0xff181c24), cx, cy + bodyR * 0.9f,   // dark bottom
                false);
            g.setGradientFill (bodyGrad);
            g.fillEllipse (cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);

            // Top highlight crescent
            g.setColour (juce::Colour (0x12ffffff));
            g.fillEllipse (cx - bodyR * 0.7f, cy - bodyR * 0.9f,
                           bodyR * 1.4f, bodyR * 0.9f);
        }

        // === LINE INDICATOR (more visible than dot) ===
        float lineInner = bodyR * 0.35f;
        float lineOuter = bodyR * 0.75f;
        float angle = -135.0f + normVal * 270.0f;
        float angleRad = juce::degreesToRadians (angle);
        float sinA = std::sin (angleRad), cosA = -std::cos (angleRad);
        float x1 = cx + lineInner * sinA, y1 = cy + lineInner * cosA;
        float x2 = cx + lineOuter * sinA, y2 = cy + lineOuter * cosA;

        // Glow behind line
        g.setColour (accentColour.withAlpha (hover ? 0.25f : 0.12f));
        g.drawLine (x1, y1, x2, y2, 4.0f);
        // Bright line
        g.setColour (accentColour.brighter (0.2f).withAlpha (hover ? 1.0f : 0.85f));
        g.drawLine (x1, y1, x2, y2, 1.8f);

        // === MOTION PLAYBACK ghost indicator ===
        if (motionStepPtr != nullptr && plockSeqPtr != nullptr && !plockKeyStr.empty())
        {
            int curStep = motionStepPtr->load();
            if (curStep >= 0 && curStep < static_cast<int>(kMaxSteps))
            {
                auto& stepPlocks = plockSeqPtr->steps[static_cast<size_t>(curStep)].plocks;
                auto it = stepPlocks.find (plockKeyStr);
                if (it != stepPlocks.end())
                {
                    float plockNorm = (it->second - minValue) / std::max (0.0001f, maxValue - minValue);
                    plockNorm = juce::jlimit (0.0f, 1.0f, plockNorm);
                    float plockAngle = -135.0f + plockNorm * 270.0f;
                    float plockRad = juce::degreesToRadians (plockAngle);
                    float gx = cx + (arcR) * std::sin (plockRad);
                    float gy = cy - (arcR) * std::cos (plockRad);
                    float ghostR = std::max (1.8f, bodyR * 0.14f);
                    // Ghost glow on arc
                    g.setColour (juce::Colour (0xff00ddff).withAlpha (0.25f));
                    g.fillEllipse (gx - ghostR * 2.5f, gy - ghostR * 2.5f, ghostR * 5.0f, ghostR * 5.0f);
                    // Ghost dot on arc (cyan)
                    g.setColour (juce::Colour (0xff00ddff).withAlpha (0.85f));
                    g.fillEllipse (gx - ghostR * 1.2f, gy - ghostR * 1.2f, ghostR * 2.4f, ghostR * 2.4f);
                }
            }
        }

        // === RED recording indicator ===
        if (motionRecPtr != nullptr && motionRecPtr->load())
        {
            float recDotR = 2.5f;
            float recX = cx + radius - 2.0f;
            float recY = cy - radius + 2.0f;
            g.setColour (juce::Colour (0xffcc2020).withAlpha (0.5f));
            g.fillEllipse (recX - recDotR * 2, recY - recDotR * 2, recDotR * 4, recDotR * 4);
            g.setColour (juce::Colour (0xffff3030));
            g.fillEllipse (recX - recDotR, recY - recDotR, recDotR * 2, recDotR * 2);
        }

        // === MACRO indicators ===
        if (macroEnginePtr != nullptr && !plockKeyStr.empty())
        {
            static const juce::Colour macroColors[] = {
                juce::Colour (0xff40c8e0), juce::Colour (0xffe040c0),
                juce::Colour (0xffe0c040), juce::Colour (0xff50e070)
            };

            float mdX = cx - radius + 1.0f;
            float mdY = cy - radius + 1.0f;
            for (int mi = 0; mi < MacroEngine::kNumMacros; ++mi)
            {
                auto& mk = macroEnginePtr->macros[static_cast<size_t>(mi)];
                for (auto& a : mk.assignments)
                {
                    if (a.trackType == macroTrackType && a.trackIndex == macroTrackIdx
                        && a.paramKey == plockKeyStr)
                    {
                        g.setColour (macroColors[mi].withAlpha (0.35f));
                        g.fillEllipse (mdX - 3.0f, mdY - 3.0f, 6.0f, 6.0f);
                        if (a.depth >= 0.0f)
                        {
                            g.setColour (macroColors[mi]);
                            g.fillEllipse (mdX - 1.5f, mdY - 1.5f, 3.0f, 3.0f);
                        }
                        else
                        {
                            g.setColour (macroColors[mi]);
                            g.drawEllipse (mdX - 1.5f, mdY - 1.5f, 3.0f, 3.0f, 1.0f);
                        }
                        mdX += 5.0f;
                        break;
                    }
                }
            }

            if (macroEnginePtr->isAnyArmed())
            {
                int armed = macroEnginePtr->armedMacro;
                auto col = macroColors[armed];
                g.setColour (col.withAlpha (0.2f));
                g.drawEllipse (cx - radius - 1.0f, cy - radius - 1.0f,
                               (radius + 1.0f) * 2, (radius + 1.0f) * 2, 1.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // ── Right-click → modulation assignment (FIRST check, before anything else) ──
        if (modTargetId >= 0 && onModRightClick)
        {
            bool rightClick = e.mods.isRightButtonDown()
                           || e.mods.isPopupMenu()
                           || (e.mods.isCtrlDown() && e.mods.isLeftButtonDown());
            if (rightClick)
            {
                onModRightClick();
                return;
            }
        }

        if (macroEnginePtr != nullptr && macroEnginePtr->isAnyArmed())
        {
            float captureDepth = e.mods.isAltDown() ? -0.5f : 0.5f;
            macroEnginePtr->captureAssignment (macroTrackType, macroTrackIdx,
                                               plockKeyStr, value, minValue, maxValue, captureDepth);
            macroEnginePtr->disarm();
            repaint();
            if (auto* p = getParentComponent()) p->repaint();
            return;
        }
        if (onClickPopup)
        {
            onClickPopup();
            return;
        }
        if (!popupCategories.empty())
        {
            juce::PopupMenu menu;
            int currentIdx = static_cast<int>(std::round (value));
            bool first = true;
            for (auto& cat : popupCategories)
            {
                if (!first) menu.addSeparator();
                first = false;
                menu.addSectionHeader (cat.name);
                for (auto& [idx, name] : cat.items)
                    menu.addItem (idx + 2, name, true, idx == currentIdx);
            }
            int result = menu.show();
            if (result > 0)
                setValue (static_cast<float>(result - 2));
            return;
        }
        if (!popupItems.empty())
        {
            juce::PopupMenu menu;
            int currentIdx = static_cast<int>(std::round (value));
            for (int i = 0; i < static_cast<int>(popupItems.size()); ++i)
                menu.addItem (i + 1, popupItems[static_cast<size_t>(i)], true, i == currentIdx);

            int result = menu.show();
            if (result > 0)
                setValue (static_cast<float>(result - 1));
            return;
        }
        dragStartY = e.getPosition().y;
        dragStartValue = value;
        dragging = true;
    }

    void setPopupItems (const std::vector<juce::String>& items) { popupItems = items; }

    struct PopupCategory {
        juce::String name;
        std::vector<std::pair<int, juce::String>> items;
    };
    void setCategorizedPopup (const std::vector<PopupCategory>& cats) { popupCategories = cats; popupItems.clear(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
        {
            float dy = static_cast<float>(dragStartY - e.getPosition().y);
            float range = maxValue - minValue;
            float sensitivity = range / 120.0f;
            setValue (dragStartValue + dy * sensitivity);
        }
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override { setValue (defaultValue); }

private:
    juce::String label;
    float minValue, maxValue, value, defaultValue;
    std::function<juce::String(float)> formatFunc;
    juce::Colour accentColour { Colours_GB::accent };
    int dragStartY = 0;
    float dragStartValue = 0;
    bool dragging = false;

    bool*         plockModePtr = nullptr;
    int*          plockStepPtr = nullptr;
    StepSequence* plockSeqPtr  = nullptr;
    std::string   plockKeyStr;

    std::atomic<bool>* motionRecPtr  = nullptr;
    std::atomic<int>*  motionStepPtr = nullptr;
    std::atomic<int>*  motionRecModePtr = nullptr;
    float              motionRecLastVal = 0.0f;
    std::vector<juce::String> popupItems;
    std::vector<PopupCategory> popupCategories;

    MacroEngine* macroEnginePtr = nullptr;
    int          macroTrackType = 0;
    int          macroTrackIdx  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};
