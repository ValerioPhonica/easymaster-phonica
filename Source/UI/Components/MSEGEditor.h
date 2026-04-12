#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Audio/FX/MSEGEngine.h"
#include "../Colours.h"
#include <set>
#include <map>

// ═══════════════════════════════════════════════════════════════════
// MSEGEditor — Serum-style multi-stage envelope editor
//
// INTERACTIONS:
//   Click+drag point    = move breakpoint (Shift = snap to grid)
//   Double-click empty  = add new point
//   Right-click point   = curve/loop/preset menu
//   Option+drag segment = bend curve (drag up/down to cycle types)
//   Cmd+drag            = step drawing mode
// ═══════════════════════════════════════════════════════════════════

class MSEGEditor : public juce::Component
{
public:
    MSEGEditor (MSEGData& d, int index = 0) : data (&d), msegIndex (index) {}

    void setData (MSEGData& d) { data = &d; selectedPoints.clear(); repaint(); }
    void setMsegIndex (int idx) { msegIndex = idx; }
    MSEGData& getData() { return *data; }

    void setPlayheadPosition (float p) { playhead = p; }
    void setAuxValue (float v) { auxLfoValue = v; }
    void setCrossValues (float v1, float v2) { crossVal1 = v1; crossVal2 = v2; }

    std::function<void()> onChange;

    // ── Static clipboard for copy/paste between MSEGs ──
    static MSEGData& getClipboard() { static MSEGData clip; return clip; }
    static bool& hasClipboard() { static bool has = false; return has; }

    // ── Paint ──────────────────────────────────────────────
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float w = bounds.getWidth(), h = bounds.getHeight();
        if (w < 2 || h < 2) return;

        // Background gradient
        juce::ColourGradient bg (juce::Colour (0xff0c0e12), 0, 0,
                                  juce::Colour (0xff08090d), 0, h, false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (juce::Colour (0xff252830));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        // Grid lines
        int gx = std::max (1, data->gridX), gy = std::max (1, data->gridY);
        g.setColour (juce::Colour (0xff181a20));
        for (int i = 1; i < gy; ++i)
            g.drawHorizontalLine (static_cast<int>(h * i / gy), 0, w);
        for (int i = 1; i < gx; ++i)
            g.drawVerticalLine (static_cast<int>(w * i / gx), 0, h);
        // Center line (zero for bipolar)
        g.setColour (juce::Colour (0xff2a2e38));
        g.drawHorizontalLine (static_cast<int>(h * 0.5f), 0, w);

        if (data->numPoints < 2) return;

        // Loop region highlight
        if (data->loopStart >= 0 && data->loopEnd > data->loopStart
            && data->loopEnd < data->numPoints)
        {
            float lsX = data->points[data->loopStart].time * w;
            float leX = data->points[data->loopEnd].time * w;
            g.setColour (juce::Colour (0x1830c090));
            g.fillRect (lsX, 0.0f, leX - lsX, h);
            g.setColour (juce::Colour (0xff30c090));
            g.drawVerticalLine (static_cast<int>(lsX), 0, h);
            g.drawVerticalLine (static_cast<int>(leX), 0, h);
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
            g.drawText ("S", static_cast<int>(lsX) - 5, 2, 10, 8, juce::Justification::centred);
            g.drawText ("E", static_cast<int>(leX) - 5, 2, 10, 8, juce::Justification::centred);
        }

        // Highlight segment being curve-bent
        if (curveDragSeg >= 0 && curveDragSeg < data->numPoints - 1)
        {
            float x0 = data->points[curveDragSeg].time * w;
            float x1 = data->points[curveDragSeg + 1].time * w;
            g.setColour (Colours_GB::accent.withAlpha (0.08f));
            g.fillRect (x0, 0.0f, x1 - x0, h);
        }

        // Envelope curve path (300 segments for smooth rendering)
        juce::Path envPath, fillPath;
        constexpr int kCurveRes = 300;
        for (int s = 0; s <= kCurveRes; ++s)
        {
            float t = s / static_cast<float>(kCurveRes);
            float val = evaluateAt (t);
            float x = t * w, y = (1.0f - val) * h;
            if (s == 0)
            {
                envPath.startNewSubPath (x, y);
                fillPath.startNewSubPath (x, h);
                fillPath.lineTo (x, y);
            }
            else
            {
                envPath.lineTo (x, y);
                fillPath.lineTo (x, y);
            }
        }
        fillPath.lineTo (w, h);
        fillPath.closeSubPath();

        // Fill gradient
        juce::ColourGradient fg (juce::Colour (0x3040d8e8), 0, 0,
                                  juce::Colour (0x0840d8e8), 0, h, false);
        g.setGradientFill (fg);
        g.fillPath (fillPath);
        g.setColour (juce::Colour (0xee40d8e8));
        g.strokePath (envPath, juce::PathStrokeType (1.8f));

        // Modulated curve overlay (when aux LFO or cross-MSEG is active)
        bool hasAnyAuxMod = false, hasAnyCrossMod = false;
        for (int i = 0; i < data->numPoints; ++i)
        {
            if (std::abs (data->points[i].auxModX) > 0.001f || std::abs (data->points[i].auxModY) > 0.001f)
                hasAnyAuxMod = true;
            if (std::abs (data->points[i].crossModY[0]) > 0.001f || std::abs (data->points[i].crossModX[0]) > 0.001f
             || std::abs (data->points[i].crossModY[1]) > 0.001f || std::abs (data->points[i].crossModX[1]) > 0.001f)
                hasAnyCrossMod = true;
        }
        bool anyModActive = (hasAnyAuxMod && std::abs (auxLfoValue) > 0.001f)
                          || (hasAnyCrossMod && (std::abs (crossVal1) > 0.001f || std::abs (crossVal2) > 0.001f));

        if (anyModActive)
        {
            // Temporarily apply ALL modulation to points
            std::array<MSEGPoint, MSEGData::kMaxPoints> orig;
            for (int i = 0; i < data->numPoints; ++i)
            {
                orig[static_cast<size_t>(i)] = data->points[i];
                float xMod = data->points[i].auxModX * auxLfoValue * 0.15f;
                float yMod = data->points[i].auxModY * auxLfoValue * 0.5f;
                xMod += data->points[i].crossModX[0] * crossVal1 * 0.15f;
                xMod += data->points[i].crossModX[1] * crossVal2 * 0.15f;
                yMod += data->points[i].crossModY[0] * crossVal1 * 0.5f;
                yMod += data->points[i].crossModY[1] * crossVal2 * 0.5f;
                data->points[i].time  = std::clamp (data->points[i].time  + xMod, 0.0f, 1.0f);
                data->points[i].value = std::clamp (data->points[i].value + yMod, 0.0f, 1.0f);
            }
            // Draw modulated curve
            juce::Path modPath;
            for (int s = 0; s <= kCurveRes; ++s)
            {
                float t = s / static_cast<float>(kCurveRes);
                float val = evaluateAt (t);
                float x = t * w, y = (1.0f - val) * h;
                if (s == 0) modPath.startNewSubPath (x, y);
                else        modPath.lineTo (x, y);
            }
            g.setColour (Colours_GB::accent.withAlpha (0.5f));
            g.strokePath (modPath, juce::PathStrokeType (1.2f));
            // Restore original points
            for (int i = 0; i < data->numPoints; ++i)
                data->points[i] = orig[static_cast<size_t>(i)];
        }

        // Breakpoints
        for (int i = 0; i < data->numPoints; ++i)
        {
            float px = data->points[i].time * w;
            float py = (1.0f - data->points[i].value) * h;

            // Draw modulated ghost position (aux LFO + cross-MSEG effect)
            float modX = data->points[i].auxModX * auxLfoValue * 0.15f;
            float modY = data->points[i].auxModY * auxLfoValue * 0.5f;
            modX += data->points[i].crossModX[0] * crossVal1 * 0.15f;
            modX += data->points[i].crossModX[1] * crossVal2 * 0.15f;
            modY += data->points[i].crossModY[0] * crossVal1 * 0.5f;
            modY += data->points[i].crossModY[1] * crossVal2 * 0.5f;
            if (std::abs (modX) > 0.001f || std::abs (modY) > 0.001f)
            {
                float gpx = std::clamp ((data->points[i].time + modX), 0.0f, 1.0f) * w;
                float gpy = std::clamp (1.0f - (data->points[i].value + modY), 0.0f, 1.0f) * h;
                // Ghost trail line
                g.setColour (juce::Colour (0x5040d8e8));
                g.drawLine (px, py, gpx, gpy, 1.0f);
                // Ghost dot
                g.setColour (juce::Colour (0x8040d8e8));
                g.fillEllipse (gpx - 4.0f, gpy - 4.0f, 8.0f, 8.0f);
            }

            bool isSel = selectedPoints.count (i) > 0;
            bool isDrag = (i == dragPoint);
            float sz = (isDrag || isSel) ? 9.0f : 6.0f;

            juce::Colour ptCol = juce::Colour (0xff40d8e8);
            if (data->loopStart == i || data->loopEnd == i)
                ptCol = juce::Colour (0xff30c090);
            if (isSel)
                ptCol = Colours_GB::accent;
            if (isDrag)
                ptCol = juce::Colour (0xffffd040);

            // Glow
            g.setColour (ptCol.withAlpha (0.25f));
            g.fillEllipse (px - sz, py - sz, sz * 2, sz * 2);
            // Dot
            g.setColour (ptCol);
            g.fillEllipse (px - sz * 0.5f, py - sz * 0.5f, sz, sz);
            // Ring
            g.setColour (ptCol.brighter (0.4f));
            g.drawEllipse (px - sz * 0.5f, py - sz * 0.5f, sz, sz, isSel ? 1.5f : 0.8f);

            // Aux mod indicator: magenta outer ring for points with aux modulation
            bool hasAux = std::abs (data->points[i].auxModY) > 0.01f
                       || std::abs (data->points[i].auxModX) > 0.01f;
            if (hasAux)
            {
                g.setColour (juce::Colour (0xffc050e0).withAlpha (0.7f)); // magenta
                g.drawEllipse (px - sz * 0.8f, py - sz * 0.8f, sz * 1.6f, sz * 1.6f, 1.5f);
            }

            // Cross-MSEG indicator: cyan outer ring
            bool hasCross = std::abs (data->points[i].crossModY[0]) > 0.01f
                         || std::abs (data->points[i].crossModX[0]) > 0.01f
                         || std::abs (data->points[i].crossModY[1]) > 0.01f
                         || std::abs (data->points[i].crossModX[1]) > 0.01f;
            if (hasCross)
            {
                g.setColour (juce::Colour (0xff40d8e8).withAlpha (0.6f)); // cyan
                g.drawEllipse (px - sz * 1.0f, py - sz * 1.0f, sz * 2.0f, sz * 2.0f, 1.2f);
            }

            // Curve type label + tension at midpoint of segment
            if (i < data->numPoints - 1)
            {
                static const char* ci[] = {
                    "LIN", "S", "SW",               // 0-2
                    "S4", "S8", "S16",               // 3-5
                    "TRI", "T1", "T2", "T3",         // 6-9
                    "\xe2\x88\xbf", "S1", "S2", "S3",// 10-13 sine
                    "E2", "E4", "PE2", "PE4",        // 14-17 expo
                    "C1", "C2", "C3", "C4", "C5", "C6", // 18-23
                    "X1", "X2", "??",                // 24-26
                    "P>", "<P"                       // 27-28
                };
                int ct = std::clamp (data->points[i].curve, 0, 28);
                float midT = (data->points[i].time + data->points[i + 1].time) * 0.5f;
                float midX = midT * w;
                float midY = (1.0f - evaluateAt (midT)) * h;
                juce::Colour labelCol = (curveDragSeg == i) ? Colours_GB::accent : juce::Colour (0xff505868);
                g.setColour (labelCol);
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
                // Show curve name + tension if non-zero
                juce::String label = ci[ct];
                float tens = data->points[i].tension;
                if (std::abs (tens) > 0.05f)
                    label += (tens > 0 ? "+" : "") + juce::String (static_cast<int>(tens * 100));
                g.drawText (label, static_cast<int>(midX) - 14, static_cast<int>(midY) - 12, 28, 10,
                            juce::Justification::centred);
            }
        }

        // Playhead (amber line + triangle + dot on curve)
        if (playhead >= 0.0f && playhead <= 1.0f)
        {
            float phX = playhead * w;

            // Vertical line (bright, thicker)
            g.setColour (Colours_GB::accent.withAlpha (0.5f));
            g.drawLine (phX, 0, phX, h, 1.5f);

            // Triangle at top
            juce::Path tri;
            tri.addTriangle (phX - 4, 0, phX + 4, 0, phX, 6);
            g.setColour (Colours_GB::accent);
            g.fillPath (tri);

            // Dot on curve — BIG with glow
            float phVal = evaluateAt (playhead);
            float dotY = (1.0f - phVal) * h;
            // Outer glow
            g.setColour (Colours_GB::accent.withAlpha (0.2f));
            g.fillEllipse (phX - 8.0f, dotY - 8.0f, 16.0f, 16.0f);
            // Inner glow
            g.setColour (Colours_GB::accent.withAlpha (0.4f));
            g.fillEllipse (phX - 5.0f, dotY - 5.0f, 10.0f, 10.0f);
            // Core dot
            g.setColour (Colours_GB::accent);
            g.fillEllipse (phX - 3.5f, dotY - 3.5f, 7.0f, 7.0f);
        }

        // Help text
        if (data->numPoints == 2 && data->depth == 0.0f)
        {
            g.setColour (juce::Colour (0xff404860));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
            g.drawText ("DBLCLICK=ADD  SHIFT=MULTI  OPT+DRAG=CURVE  RCLICK=MENU",
                        bounds.reduced (4), juce::Justification::centredBottom);
        }
    }

    // ── Mouse interactions ─────────────────────────────────
    void mouseDown (const juce::MouseEvent& e) override
    {
        float w = static_cast<float>(getWidth());
        float h = static_cast<float>(getHeight());
        if (w < 2 || h < 2) return;

        // Right-click = context menu
        if (e.mods.isRightButtonDown())
        {
            int n = findNearestPoint (e.x, e.y);
            if (n >= 0) showPointMenu (n, e.getScreenPosition());
            return;
        }

        // Cmd+drag = step drawing
        if (e.mods.isCommandDown())
        {
            stepDrawing = true;
            drawStep (e.x, e.y);
            return;
        }

        // Option+drag on segment = also tension drag (same as normal)
        if (e.mods.isAltDown())
        {
            curveDragSeg = findSegmentAt (static_cast<float>(e.x) / w);
            if (curveDragSeg >= 0)
            {
                curveDragStartY = e.y;
                curveDragOrigTension = data->points[curveDragSeg].tension;
            }
            repaint();
            return;
        }

        // Normal click: find point
        int nearest = findNearestPoint (e.x, e.y);

        if (nearest >= 0)
        {
            if (e.mods.isShiftDown())
            {
                // Shift+click: toggle selection
                if (selectedPoints.count (nearest))
                    selectedPoints.erase (nearest);
                else
                    selectedPoints.insert (nearest);
            }
            else
            {
                // Normal click on point: if not already selected, clear selection and select this
                if (!selectedPoints.count (nearest))
                {
                    selectedPoints.clear();
                    selectedPoints.insert (nearest);
                }
            }
            dragPoint = nearest;
            // Save initial positions of all selected points for group drag
            dragStartPositions.clear();
            for (int si : selectedPoints)
                dragStartPositions[si] = { data->points[si].time, data->points[si].value };
            dragMouseStartX = static_cast<float>(e.x) / w;
            dragMouseStartY = static_cast<float>(e.y) / h;
        }
        else
        {
            // No point nearby — check if on a segment for continuous tension drag
            int seg = findSegmentAt (static_cast<float>(e.x) / w);
            if (seg >= 0)
            {
                curveDragSeg = seg;
                curveDragStartY = e.y;
                curveDragOrigTension = data->points[seg].tension;
            }
            else if (!e.mods.isShiftDown())
                selectedPoints.clear();
            dragPoint = -1;
        }
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        float w = static_cast<float>(getWidth());
        float h = static_cast<float>(getHeight());
        if (w < 2 || h < 2) return;
        if (e.mods.isRightButtonDown()) return;

        // Only add point if click is NOT near an existing point
        int nearest = findNearestPoint (e.x, e.y);
        if (nearest >= 0) return; // double-click on point does nothing

        float nt = std::clamp (static_cast<float>(e.x) / w, 0.01f, 0.99f);
        float nv = std::clamp (1.0f - static_cast<float>(e.y) / h, 0.0f, 1.0f);
        if (data->gridX > 1) nt = std::round (nt * data->gridX) / data->gridX;
        if (data->gridY > 1) nv = std::round (nv * data->gridY) / data->gridY;
        addPoint (nt, nv);
        // Select the newly added point
        int newPt = findNearestPoint (e.x, e.y);
        if (newPt >= 0)
        {
            selectedPoints.clear();
            selectedPoints.insert (newPt);
            dragPoint = newPt;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        float w = static_cast<float>(getWidth());
        float h = static_cast<float>(getHeight());
        if (w < 2 || h < 2) return;

        if (stepDrawing) { drawStep (e.x, e.y); return; }

        // Curve bending: continuous tension drag (Serum-style)
        if (curveDragSeg >= 0)
        {
            float dy = static_cast<float>(curveDragStartY - e.y);
            float newTension = std::clamp (curveDragOrigTension + dy / 100.0f, -1.0f, 1.0f);
            if (data->points[curveDragSeg].tension != newTension)
            {
                data->points[curveDragSeg].tension = newTension;
                if (onChange) onChange();
            }
            repaint();
            return;
        }

        // Point dragging (single or multi)
        if (dragPoint < 0) return;

        float deltaT = static_cast<float>(e.x) / w - dragMouseStartX;
        float deltaV = -(static_cast<float>(e.y) / h - dragMouseStartY); // inverted Y

        if (selectedPoints.size() > 1 && selectedPoints.count (dragPoint))
        {
            // Multi-point drag: move all selected points by delta
            // First check boundaries
            bool canMove = true;
            for (auto& [si, startPos] : dragStartPositions)
            {
                float newT = startPos.first + deltaT;
                // Don't allow first/last points to move in time
                if (si == 0 || si == data->numPoints - 1) newT = startPos.first;
                if (newT < 0.0f || newT > 1.0f) canMove = false;
            }
            if (canMove)
            {
                for (auto& [si, startPos] : dragStartPositions)
                {
                    float newT = startPos.first + deltaT;
                    float newV = std::clamp (startPos.second + deltaV, 0.0f, 1.0f);
                    if (si == 0) newT = 0.0f;
                    else if (si == data->numPoints - 1) newT = 1.0f;
                    else newT = std::clamp (newT, 0.005f, 0.995f);

                    if (e.mods.isShiftDown())
                    {
                        if (data->gridX > 1) newT = std::round (newT * data->gridX) / data->gridX;
                        if (data->gridY > 1) newV = std::round (newV * data->gridY) / data->gridY;
                    }
                    data->points[si].time = newT;
                    data->points[si].value = newV;
                }
            }
        }
        else
        {
            // Single point drag
            float nt = std::clamp (static_cast<float>(e.x) / w, 0.0f, 1.0f);
            float nv = std::clamp (1.0f - static_cast<float>(e.y) / h, 0.0f, 1.0f);

            if (e.mods.isShiftDown())
            {
                if (data->gridX > 1) nt = std::round (nt * data->gridX) / data->gridX;
                if (data->gridY > 1) nv = std::round (nv * data->gridY) / data->gridY;
            }

            if (dragPoint == 0) nt = 0.0f;
            if (dragPoint == data->numPoints - 1) nt = 1.0f;
            if (dragPoint > 0)
                nt = std::max (nt, data->points[dragPoint - 1].time + 0.005f);
            if (dragPoint < data->numPoints - 1)
                nt = std::min (nt, data->points[dragPoint + 1].time - 0.005f);

            data->points[dragPoint].time = nt;
            data->points[dragPoint].value = nv;
        }

        if (onChange) onChange();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragPoint = -1;
        curveDragSeg = -1;
        curveDragOrigTension = 0.0f;
        stepDrawing = false;
        dragStartPositions.clear();
        repaint();
    }

private:
    MSEGData* data;
    int msegIndex = 0;  // which MSEG this editor represents (0, 1, or 2)
    float playhead = -1.0f;
    float auxLfoValue = 0.0f;
    float crossVal1 = 0.0f, crossVal2 = 0.0f; // cross-MSEG modulation values for visualization
    int   dragPoint = -1;
    bool  stepDrawing = false;
    int   curveDragSeg = -1;
    int   curveDragStartY = 0;
    int   curveDragOrigCurve = 0;  // unused, kept for compat
    float curveDragOrigTension = 0.0f;

    // Multi-selection
    std::set<int> selectedPoints;
    std::map<int, std::pair<float,float>> dragStartPositions; // point index → (time, value) at drag start
    float dragMouseStartX = 0.0f, dragMouseStartY = 0.0f;

    int findSegmentAt (float t) const
    {
        for (int i = 0; i < data->numPoints - 1; ++i)
            if (t >= data->points[i].time && t <= data->points[i + 1].time)
                return i;
        return -1;
    }

    void drawStep (int mx, int my)
    {
        float w = static_cast<float>(getWidth());
        float h = static_cast<float>(getHeight());
        int gx = std::max (1, data->gridX);
        float stepW = 1.0f / gx;
        int si = static_cast<int>((static_cast<float>(mx) / w) / stepW);
        float ss = si * stepW, se = ss + stepW;
        float val = std::clamp (1.0f - static_cast<float>(my) / h, 0.0f, 1.0f);
        if (data->gridY > 1) val = std::round (val * data->gridY) / data->gridY;
        for (int i = data->numPoints - 2; i >= 1; --i)
            if (data->points[i].time > ss + 0.001f && data->points[i].time < se - 0.001f)
                removePoint (i);
        ss = std::clamp (ss, 0.005f, 0.995f);
        se = std::clamp (se, 0.005f, 0.995f);
        if (ss > 0.005f && se < 0.995f)
        {
            addPoint (ss, val);
            addPoint (se - 0.003f, val);
        }
        if (onChange) onChange();
        repaint();
    }

    float evaluateAt (float t) const
    {
        if (data->numPoints < 2) return 0.5f;
        for (int i = 0; i < data->numPoints - 1; ++i)
        {
            const auto& p0 = data->points[i];
            const auto& p1 = data->points[i + 1];
            if (t >= p0.time && t <= p1.time)
            {
                float segLen = p1.time - p0.time;
                if (segLen < 0.0001f) return p0.value;
                return MSEGEngine::interpolate (p0.value, p1.value,
                    (t - p0.time) / segLen, p0.curve, p0.tension);
            }
        }
        return data->points[data->numPoints - 1].value;
    }

    int findNearestPoint (int mx, int my) const
    {
        float w = static_cast<float>(getWidth());
        float h = static_cast<float>(getHeight());
        int best = -1;
        float bestDist = 14.0f;
        for (int i = 0; i < data->numPoints; ++i)
        {
            float px = data->points[i].time * w;
            float py = (1.0f - data->points[i].value) * h;
            float dx = px - mx, dy = py - my;
            float dist = std::sqrt (dx * dx + dy * dy);
            if (dist < bestDist) { bestDist = dist; best = i; }
        }
        return best;
    }

    void addPoint (float t, float v)
    {
        if (data->numPoints >= MSEGData::kMaxPoints) return;
        int ii = data->numPoints;
        for (int i = 0; i < data->numPoints; ++i)
            if (data->points[i].time > t) { ii = i; break; }
        for (int i = data->numPoints; i > ii; --i)
            data->points[i] = data->points[i - 1];
        data->points[ii] = { t, v, 0 };
        data->numPoints++;
        if (data->loopStart >= ii) data->loopStart++;
        if (data->loopEnd >= ii) data->loopEnd++;
        if (onChange) onChange();
    }

    void removePoint (int i)
    {
        if (i <= 0 || i >= data->numPoints - 1 || data->numPoints <= 2) return;
        if (data->loopStart == i) data->loopStart = -1;
        if (data->loopEnd == i) data->loopEnd = -1;
        if (data->loopStart > i) data->loopStart--;
        if (data->loopEnd > i) data->loopEnd--;
        for (int j = i; j < data->numPoints - 1; ++j)
            data->points[j] = data->points[j + 1];
        data->numPoints--;
        if (onChange) onChange();
    }

    void showPointMenu (int pi, juce::Point<int> screenPos)
    {
        juce::PopupMenu cm;
        int cc = data->points[pi].curve;
        cm.addSectionHeader ("Basic");
        cm.addItem (201, "Linear",      true, cc == 0);
        cm.addItem (202, "Sinoid",      true, cc == 1);
        cm.addItem (203, "Switch",      true, cc == 2);
        cm.addSectionHeader ("Stairs");
        cm.addItem (204, "Stairs 4",    true, cc == 3);
        cm.addItem (205, "Stairs 8",    true, cc == 4);
        cm.addItem (206, "Stairs 16",   true, cc == 5);
        cm.addSectionHeader ("Triangle");
        cm.addItem (207, "Triangle",    true, cc == 6);
        cm.addItem (208, "Triangle 1.5",true, cc == 7);
        cm.addItem (209, "Triangle 2.5",true, cc == 8);
        cm.addItem (210, "Triangle 3.5",true, cc == 9);
        cm.addSectionHeader ("Sine");
        cm.addItem (211, "Sine 1",      true, cc == 10);
        cm.addItem (212, "Sine 1.5",    true, cc == 11);
        cm.addItem (213, "Sine 2.5",    true, cc == 12);
        cm.addItem (214, "Sine 3.5",    true, cc == 13);
        cm.addSectionHeader ("Expo");
        cm.addItem (215, "Expo x2",     true, cc == 14);
        cm.addItem (216, "Expo x4",     true, cc == 15);
        cm.addItem (217, "Peak Expo x2",true, cc == 16);
        cm.addItem (218, "Peak Expo x4",true, cc == 17);
        cm.addSectionHeader ("Curves");
        cm.addItem (219, "Curve 1",     true, cc == 18);
        cm.addItem (220, "Curve 2",     true, cc == 19);
        cm.addItem (221, "Curve 3",     true, cc == 20);
        cm.addItem (222, "Curve 4",     true, cc == 21);
        cm.addItem (223, "Curve 5",     true, cc == 22);
        cm.addItem (224, "Curve 6",     true, cc == 23);
        cm.addSectionHeader ("Extreme");
        cm.addItem (225, "Extreme 1",   true, cc == 24);
        cm.addItem (226, "Extreme 2",   true, cc == 25);
        cm.addItem (227, "Smooth Rnd",  true, cc == 26);
        cm.addItem (228, "Pulse End",   true, cc == 27);
        cm.addItem (229, "Pulse Start", true, cc == 28);

        // ── Aux LFO per-point modulation ──
        float curModY = data->points[pi].auxModY;
        float curModX = data->points[pi].auxModX;

        auto makeAmtMenu = [](int baseId, float curVal, bool hasNeg100 = true) {
            juce::PopupMenu m;
            m.addItem (baseId,     "OFF",   true, std::abs(curVal) < 0.01f);
            m.addItem (baseId + 1, "+25%",  true, std::abs(curVal - 0.25f) < 0.01f);
            m.addItem (baseId + 2, "+50%",  true, std::abs(curVal - 0.5f) < 0.01f);
            m.addItem (baseId + 3, "+100%", true, std::abs(curVal - 1.0f) < 0.01f);
            m.addItem (baseId + 4, "-25%",  true, std::abs(curVal + 0.25f) < 0.01f);
            m.addItem (baseId + 5, "-50%",  true, std::abs(curVal + 0.5f) < 0.01f);
            if (hasNeg100) m.addItem (baseId + 6, "-100%", true, std::abs(curVal + 1.0f) < 0.01f);
            return m;
        };

        // Build MOD submenu with all modulation sources
        juce::PopupMenu modMenu;
        modMenu.addSubMenu ("LFO \xe2\x86\x92 Y (value)", makeAmtMenu (30, curModY));
        modMenu.addSubMenu ("LFO \xe2\x86\x92 X (time)",  makeAmtMenu (40, curModX, false));
        modMenu.addSeparator();

        // Per-source MSEG modulation — show specific MSEG names
        // Determine which MSEGs are the "other" ones
        int otherA, otherB;
        if (msegIndex == 0)      { otherA = 1; otherB = 2; }
        else if (msegIndex == 1) { otherA = 0; otherB = 2; }
        else                     { otherA = 0; otherB = 1; }

        float cm0Y = data->points[pi].crossModY[0];
        float cm0X = data->points[pi].crossModX[0];
        float cm1Y = data->points[pi].crossModY[1];
        float cm1X = data->points[pi].crossModX[1];

        modMenu.addSubMenu ("MSEG " + juce::String (otherA + 1) + " \xe2\x86\x92 Y", makeAmtMenu (80, cm0Y));
        modMenu.addSubMenu ("MSEG " + juce::String (otherA + 1) + " \xe2\x86\x92 X", makeAmtMenu (90, cm0X, false));
        modMenu.addSeparator();
        modMenu.addSubMenu ("MSEG " + juce::String (otherB + 1) + " \xe2\x86\x92 Y", makeAmtMenu (180, cm1Y));
        modMenu.addSubMenu ("MSEG " + juce::String (otherB + 1) + " \xe2\x86\x92 X", makeAmtMenu (190, cm1X, false));

        juce::PopupMenu lm;
        lm.addItem (20, "Set LOOP START", true, data->loopStart == pi);
        lm.addItem (21, "Set LOOP END",   true, data->loopEnd == pi);
        lm.addItem (22, "Clear Loop");

        // ── Factory shape presets ──
        juce::PopupMenu factoryMenu;
        factoryMenu.addItem (60, "Default 4pt");
        factoryMenu.addItem (61, "Attack-Decay");
        factoryMenu.addItem (62, "Saw Rise");
        factoryMenu.addItem (63, "Saw Fall");
        factoryMenu.addItem (64, "Triangle");
        factoryMenu.addItem (65, "Multi-Stage");
        factoryMenu.addItem (66, "8-Step Random");

        // ── User presets from filesystem ──
        auto presetDir = getMSEGPresetDir();
        juce::PopupMenu pm;
        pm.addItem (100, "Save Preset...");
        pm.addItem (101, "New Category...");
        pm.addSeparator();
        pm.addSubMenu ("Factory Shapes", factoryMenu);

        // Scan categories (subdirectories)
        // Use sequential IDs to avoid hash collisions
        int nextLoadId = 1000;
        int nextDelId = 20000;
        int nextCatDelId = 30000;
        // Store mappings for callback
        std::map<int, juce::File> loadMap, delMap, catDelMap;

        if (presetDir.isDirectory())
        {
            auto cats = presetDir.findChildFiles (juce::File::findDirectories, false);
            cats.sort();
            for (auto& catDir : cats)
            {
                juce::PopupMenu catMenu;
                auto files = catDir.findChildFiles (juce::File::findFiles, false, "*.mseg");
                files.sort();
                for (int fi = 0; fi < files.size(); ++fi)
                {
                    int loadId = nextLoadId++;
                    loadMap[loadId] = files[fi];
                    catMenu.addItem (loadId, files[fi].getFileNameWithoutExtension());
                }
                if (files.size() > 0) catMenu.addSeparator();
                // Delete individual presets submenu
                juce::PopupMenu delMenu;
                for (int fi = 0; fi < files.size(); ++fi)
                {
                    int delId = nextDelId++;
                    delMap[delId] = files[fi];
                    delMenu.addItem (delId, files[fi].getFileNameWithoutExtension());
                }
                if (files.size() > 0)
                    catMenu.addSubMenu ("Delete Preset...", delMenu);
                // Delete category
                catMenu.addSeparator();
                int cdId = nextCatDelId++;
                catDelMap[cdId] = catDir;
                catMenu.addItem (cdId, "Delete Category \"" + catDir.getFileName() + "\"");

                pm.addSubMenu (catDir.getFileName(), catMenu);
            }
        }

        juce::PopupMenu menu;
        menu.addSubMenu ("CURVE", cm);
        menu.addSubMenu ("MOD", modMenu);
        menu.addSubMenu ("LOOP", lm);
        // Delete: single or multi
        int numDeletable = 0;
        for (int si : selectedPoints)
            if (si > 0 && si < data->numPoints - 1) numDeletable++;
        if (numDeletable > 1)
            menu.addItem (50, "DELETE " + juce::String (numDeletable) + " SELECTED");
        else if (pi > 0 && pi < data->numPoints - 1)
            menu.addItem (50, "DELETE POINT");
        menu.addSubMenu ("PRESETS", pm);
        menu.addSeparator();
        menu.addItem (70, "COPY MSEG");
        menu.addItem (71, "PASTE MSEG", hasClipboard());

        // Position popup at the mouse click point
        auto targetArea = juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1);
        auto opts = juce::PopupMenu::Options()
            .withTargetScreenArea (targetArea);
        auto* top = getTopLevelComponent();
        if (top) opts = opts.withParentComponent (top);

        auto presetDirCopy = presetDir;

        menu.showMenuAsync (opts,
            [this, pi, presetDirCopy, loadMap, delMap, catDelMap] (int r)
        {
            if (!r) return;

            // Curve types (201-229) — apply to all selected points (or just clicked point)
            if (r >= 201 && r <= 229)
            {
                int newCurve = r - 201;
                if (selectedPoints.size() > 1)
                    for (int si : selectedPoints)
                        { data->points[si].curve = newCurve; data->points[si].tension = 0.0f; }
                else
                    { data->points[pi].curve = newCurve; data->points[pi].tension = 0.0f; }
            }
            // Loop
            else if (r == 20) data->loopStart = pi;
            else if (r == 21) data->loopEnd = pi;
            else if (r == 22) { data->loopStart = -1; data->loopEnd = -1; }
            // Aux Mod Y (apply to all selected or just clicked point)
            else if (r >= 30 && r <= 36)
            {
                static const float yAmts[] = { 0.0f, 0.25f, 0.5f, 1.0f, -0.25f, -0.5f, -1.0f };
                float amt = yAmts[r - 30];
                if (selectedPoints.size() > 1)
                    for (int si : selectedPoints) data->points[si].auxModY = amt;
                else
                    data->points[pi].auxModY = amt;
            }
            // Aux Mod X
            else if (r >= 40 && r <= 45)
            {
                static const float xAmts[] = { 0.0f, 0.25f, 0.5f, 1.0f, -0.25f, -0.5f };
                float amt = xAmts[r - 40];
                if (selectedPoints.size() > 1)
                    for (int si : selectedPoints) data->points[si].auxModX = amt;
                else
                    data->points[pi].auxModX = amt;
            }
            // Cross-MSEG Mod Source 0 — Y
            else if (r >= 80 && r <= 86)
            {
                static const float cyAmts[] = { 0.0f, 0.25f, 0.5f, 1.0f, -0.25f, -0.5f, -1.0f };
                float amt = cyAmts[r - 80];
                if (selectedPoints.size() > 1)
                    for (int si : selectedPoints) data->points[si].crossModY[0] = amt;
                else
                    data->points[pi].crossModY[0] = amt;
            }
            // Cross-MSEG Mod Source 0 — X
            else if (r >= 90 && r <= 95)
            {
                static const float cxAmts[] = { 0.0f, 0.25f, 0.5f, 1.0f, -0.25f, -0.5f };
                float amt = cxAmts[r - 90];
                if (selectedPoints.size() > 1)
                    for (int si : selectedPoints) data->points[si].crossModX[0] = amt;
                else
                    data->points[pi].crossModX[0] = amt;
            }
            // Cross-MSEG Mod Source 1 — Y
            else if (r >= 180 && r <= 186)
            {
                static const float cyAmts[] = { 0.0f, 0.25f, 0.5f, 1.0f, -0.25f, -0.5f, -1.0f };
                float amt = cyAmts[r - 180];
                if (selectedPoints.size() > 1)
                    for (int si : selectedPoints) data->points[si].crossModY[1] = amt;
                else
                    data->points[pi].crossModY[1] = amt;
            }
            // Cross-MSEG Mod Source 1 — X
            else if (r >= 190 && r <= 195)
            {
                static const float cxAmts[] = { 0.0f, 0.25f, 0.5f, 1.0f, -0.25f, -0.5f };
                float amt = cxAmts[r - 190];
                if (selectedPoints.size() > 1)
                    for (int si : selectedPoints) data->points[si].crossModX[1] = amt;
                else
                    data->points[pi].crossModX[1] = amt;
            }
            // Delete point(s)
            else if (r == 50)
            {
                if (selectedPoints.size() > 1)
                {
                    std::vector<int> toDelete (selectedPoints.begin(), selectedPoints.end());
                    std::sort (toDelete.rbegin(), toDelete.rend());
                    for (int di : toDelete)
                        removePoint (di);
                    selectedPoints.clear();
                }
                else
                    removePoint (pi);
            }
            // Factory presets
            else if (r == 60) data->setDefault4Point();
            else if (r == 61) { data->points[0]={0,0,0}; data->points[1]={0.2f,1,1}; data->points[2]={1,0,2}; data->numPoints=3; }
            else if (r == 62) { data->points[0]={0,0,0}; data->points[1]={1,1,0}; data->numPoints=2; }
            else if (r == 63) { data->points[0]={0,1,0}; data->points[1]={1,0,0}; data->numPoints=2; }
            else if (r == 64) { data->points[0]={0,0,0}; data->points[1]={0.5f,1,0}; data->points[2]={1,0,0}; data->numPoints=3; }
            else if (r == 65) { data->points[0]={0,0,0}; data->points[1]={0.1f,1,2}; data->points[2]={0.25f,0.4f,3}; data->points[3]={0.4f,0.8f,1}; data->points[4]={0.6f,0.3f,0}; data->points[5]={0.8f,0.7f,3}; data->points[6]={1,0,1}; data->numPoints=7; }
            else if (r == 66) { data->numPoints=9; for(int i=0;i<=8;++i){data->points[i].time=i/8.0f; data->points[i].value=(i<8)?(std::rand()%100)/100.0f:0; data->points[i].curve=0;} }
            // Save / New Category
            else if (r == 100) showSavePresetDialog (presetDirCopy);
            else if (r == 101) showNewCategoryDialog (presetDirCopy);
            // Copy/Paste MSEG
            else if (r == 70)
            {
                getClipboard() = *data;
                hasClipboard() = true;
            }
            else if (r == 71 && hasClipboard())
            {
                *data = getClipboard();
            }
            // Load user preset (sequential IDs from map)
            else if (loadMap.count (r))
                loadMSEGPreset (loadMap.at (r));
            // Delete preset
            else if (delMap.count (r))
                delMap.at (r).deleteFile();
            // Delete category
            else if (catDelMap.count (r))
                catDelMap.at (r).deleteRecursively();

            selectedPoints.clear();
            if (onChange) onChange();
            repaint();
        });
    }

    // ── MSEG Preset directory ──
    static juce::File getMSEGPresetDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Phonica School")
            .getChildFile ("GrooveBox Phonica")
            .getChildFile ("Presets")
            .getChildFile ("MSEG");
    }

    // ── Save MSEG preset to file ──
    void saveMSEGPreset (const juce::File& file)
    {
        juce::String content;
        content << "MSEG_PRESET\n";
        content << "numPoints:" << data->numPoints << "\n";
        for (int i = 0; i < data->numPoints; ++i)
            content << "p:" << data->points[i].time << "," << data->points[i].value << "," << data->points[i].curve << "\n";
        content << "loopStart:" << data->loopStart << "\n";
        content << "loopEnd:" << data->loopEnd << "\n";
        content << "loopMode:" << data->loopMode << "\n";
        file.replaceWithText (content);
    }

    // ── Load MSEG preset from file ──
    void loadMSEGPreset (const juce::File& file)
    {
        auto lines = juce::StringArray::fromLines (file.loadFileAsString());
        if (lines.size() < 2 || !lines[0].startsWith ("MSEG_PRESET")) return;
        int pi = 0;
        int fileNumPoints = 2;
        int fileLoopStart = -1, fileLoopEnd = -1, fileLoopMode = 0;
        for (auto& line : lines)
        {
            if (line.startsWith ("numPoints:"))
                fileNumPoints = std::clamp (line.fromFirstOccurrenceOf (":", false, false).getIntValue(), 2, MSEGData::kMaxPoints);
            else if (line.startsWith ("p:") && pi < MSEGData::kMaxPoints)
            {
                auto parts = juce::StringArray::fromTokens (line.fromFirstOccurrenceOf (":", false, false), ",", "");
                if (parts.size() >= 3)
                {
                    data->points[pi].time  = std::clamp (parts[0].getFloatValue(), 0.0f, 1.0f);
                    data->points[pi].value = std::clamp (parts[1].getFloatValue(), 0.0f, 1.0f);
                    data->points[pi].curve = std::clamp (parts[2].getIntValue(), 0, 5);
                    pi++;
                }
            }
            else if (line.startsWith ("loopStart:"))
                fileLoopStart = line.fromFirstOccurrenceOf (":", false, false).getIntValue();
            else if (line.startsWith ("loopEnd:"))
                fileLoopEnd = line.fromFirstOccurrenceOf (":", false, false).getIntValue();
            else if (line.startsWith ("loopMode:"))
                fileLoopMode = std::clamp (line.fromFirstOccurrenceOf (":", false, false).getIntValue(), 0, 2);
        }
        // Use actual parsed point count (not file's numPoints which could disagree)
        data->numPoints = std::clamp (pi, 2, MSEGData::kMaxPoints);
        // Ensure first point starts at 0, last at 1
        data->points[0].time = 0.0f;
        data->points[data->numPoints - 1].time = 1.0f;
        // Validate loop markers against actual numPoints
        data->loopStart = (fileLoopStart >= 0 && fileLoopStart < data->numPoints) ? fileLoopStart : -1;
        data->loopEnd   = (fileLoopEnd > data->loopStart && fileLoopEnd < data->numPoints) ? fileLoopEnd : -1;
        if (data->loopStart < 0) data->loopEnd = -1; // can't have end without start
        data->loopMode = fileLoopMode;
    }

    // ── Save preset dialog ──
    void showSavePresetDialog (juce::File presetDir)
    {
        presetDir.createDirectory();
        // Build category list
        auto cats = presetDir.findChildFiles (juce::File::findDirectories, false);
        cats.sort();
        if (cats.isEmpty())
        {
            // Create default "User" category
            presetDir.getChildFile ("User").createDirectory();
            cats = presetDir.findChildFiles (juce::File::findDirectories, false);
        }

        auto* aw = new juce::AlertWindow ("Save MSEG Preset", "Enter preset name:", juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", "", "Preset Name:");
        aw->addComboBox ("category", {}, "Category:");
        auto* combo = aw->getComboBoxComponent ("category");
        for (int i = 0; i < cats.size(); ++i)
            combo->addItem (cats[i].getFileName(), i + 1);
        combo->setSelectedItemIndex (0);
        aw->addButton ("Save", 1);
        aw->addButton ("Cancel", 0);
        aw->setAlwaysOnTop (true);

        aw->enterModalState (false, juce::ModalCallbackFunction::create (
            [this, aw, cats] (int result)
        {
            if (result == 1)
            {
                auto name = aw->getTextEditorContents ("name").trim();
                int catIdx = aw->getComboBoxComponent ("category")->getSelectedItemIndex();
                if (name.isNotEmpty() && catIdx >= 0 && catIdx < cats.size())
                {
                    auto file = cats[catIdx].getChildFile (name + ".mseg");
                    saveMSEGPreset (file);
                }
            }
            delete aw;
        }));
    }

    // ── New category dialog ──
    void showNewCategoryDialog (juce::File presetDir)
    {
        presetDir.createDirectory();
        auto* aw = new juce::AlertWindow ("New MSEG Category", "Enter category name:", juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", "", "Category Name:");
        aw->addButton ("Create", 1);
        aw->addButton ("Cancel", 0);
        aw->setAlwaysOnTop (true);

        aw->enterModalState (false, juce::ModalCallbackFunction::create (
            [aw, presetDir] (int result)
        {
            if (result == 1)
            {
                auto name = aw->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                    presetDir.getChildFile (name).createDirectory();
            }
            delete aw;
        }));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MSEGEditor)
};
