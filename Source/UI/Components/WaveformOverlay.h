#pragma once
#include "../../Sequencer/TrackState.h"
#include "../Colours.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <algorithm>
#include <vector>

// ═══════════════════════════════════════════════════════════════════
// WaveformOverlay — Grid + warp markers for sampler waveform
//
// INTERACTIONS:
//  • Double-click ANYWHERE on waveform → add warp marker
//  • Left-click + drag on marker (±10px) → move horizontally
//  • Right-click on marker → remove it
//  • Right-click on ruler → grid division popup (AUTO/1/2/4/8/16/32)
// ═══════════════════════════════════════════════════════════════════

namespace WaveformOverlay
{

static constexpr int kRulerH = 12;
static constexpr float kMarkerW = 8.0f;
static constexpr float kMarkerH = 8.0f;
static constexpr float kHitRadius = 10.0f;

// ── Compute subdivision from gridDiv setting ──
static inline int getSubdivPerBeat (int gridDiv, float beatsVisible)
{
    if (gridDiv > 0) return std::max (1, gridDiv / 4);
    if (beatsVisible < 4.0f)  return 4;
    if (beatsVisible < 8.0f)  return 2;
    return 1;
}

// ── Draw beat/bar grid lines + ruler ──
static inline void drawGridOverlay (juce::Graphics& g,
                                     float wfX, float wfY, float wfW, float wfH,
                                     float viewStart, float viewEnd,
                                     float smpBPM, int smpBars, int smpWarp,
                                     int gridDiv = 0)
{
    if (smpBPM <= 0.0f || smpBars <= 0 || smpWarp == 0) return;

    static const float barLUT[] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
    float bars = barLUT[std::clamp (smpBars, 1, 8)];
    int totalBeats = std::max (1, static_cast<int>(bars * 4.0f));

    float visRange = viewEnd - viewStart;
    if (visRange < 0.0001f) return;

    g.setColour (juce::Colour (0x40000000));
    g.fillRect (wfX, wfY, wfW, static_cast<float>(kRulerH));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));

    float beatsVisible = static_cast<float>(totalBeats) * visRange;
    int subdivPerBeat = getSubdivPerBeat (gridDiv, beatsVisible);
    int totalSubdivs = totalBeats * subdivPerBeat;

    for (int sd = 0; sd <= totalSubdivs; ++sd)
    {
        float normPos = static_cast<float>(sd) / static_cast<float>(totalSubdivs);
        if (normPos < viewStart || normPos > viewEnd) continue;
        float pixX = wfX + ((normPos - viewStart) / visRange) * wfW;

        bool isBar = (sd % (4 * subdivPerBeat) == 0);
        bool isBeat = (sd % subdivPerBeat == 0);

        if (isBar)
        {
            g.setColour (juce::Colour (0x50ffffff));
            g.drawVerticalLine (static_cast<int>(pixX), wfY, wfY + wfH);
            int barNum = sd / (4 * subdivPerBeat) + 1;
            g.setColour (juce::Colour (0xb0ffffff));
            g.drawText (juce::String (barNum), static_cast<int>(pixX + 2),
                        static_cast<int>(wfY), 20, kRulerH, juce::Justification::centredLeft, false);
        }
        else if (isBeat)
        {
            g.setColour (juce::Colour (0x25ffffff));
            g.drawVerticalLine (static_cast<int>(pixX), wfY, wfY + wfH);
            g.setColour (juce::Colour (0x50ffffff));
            g.drawVerticalLine (static_cast<int>(pixX), wfY, wfY + static_cast<float>(kRulerH) * 0.6f);
        }
        else
        {
            g.setColour (juce::Colour (0x10ffffff));
            g.drawVerticalLine (static_cast<int>(pixX), wfY + static_cast<float>(kRulerH), wfY + wfH);
            g.setColour (juce::Colour (0x20ffffff));
            g.drawVerticalLine (static_cast<int>(pixX), wfY, wfY + static_cast<float>(kRulerH) * 0.35f);
        }
    }

    g.setColour (juce::Colour (0x30ffffff));
    g.drawHorizontalLine (static_cast<int>(wfY) + kRulerH, wfX, wfX + wfW);
}

// ── Draw warp markers — triangles + full-height dotted lines ──
static inline void drawWarpMarkers (juce::Graphics& g,
                                      float wfX, float wfY, float wfW, float wfH,
                                      float viewStart, float viewEnd,
                                      const std::vector<WarpMarker>& markers,
                                      int hoveredMarker = -1,
                                      int draggedMarker = -1,
                                      float totalBeats = 0.0f)
{
    if (markers.empty()) return;
    float visRange = viewEnd - viewStart;
    if (visRange < 0.0001f) return;
    bool useBeatPos = (totalBeats > 0.001f);

    float markerY = wfY + static_cast<float>(kRulerH) - 1.0f;

    for (int i = 0; i < static_cast<int>(markers.size()); ++i)
    {
        const auto& m = markers[static_cast<size_t>(i)];
        float normPos = useBeatPos ? (m.beatPos / totalBeats) : m.samplePos;
        if (normPos < viewStart || normPos > viewEnd) continue;
        float pixX = wfX + ((normPos - viewStart) / visRange) * wfW;
        bool isEdge = (i == 0 || i == static_cast<int>(markers.size()) - 1);
        if (isEdge) continue;

        juce::Colour markerCol = m.isAuto ? juce::Colour (0xccffaa40) : juce::Colour (0xddff6820);
        if (i == hoveredMarker) markerCol = markerCol.brighter (0.4f);
        if (i == draggedMarker) markerCol = juce::Colour (0xffff8800);

        // Full-height dotted vertical line
        g.setColour (markerCol.withAlpha (0.3f));
        for (float yy = markerY + kMarkerH + 2; yy < wfY + wfH; yy += 4.0f)
            g.drawVerticalLine (static_cast<int>(pixX), yy, std::min (yy + 2.0f, wfY + wfH));

        // Downward triangle ▼
        juce::Path tri;
        tri.startNewSubPath (pixX, markerY + kMarkerH);
        tri.lineTo (pixX - kMarkerW * 0.5f, markerY);
        tri.lineTo (pixX + kMarkerW * 0.5f, markerY);
        tri.closeSubPath();
        g.setColour (markerCol);
        g.fillPath (tri);
        g.setColour (markerCol.darker (0.3f));
        g.strokePath (tri, juce::PathStrokeType (0.5f));
    }
}

// ── Hit test: find marker under mouse — FULL WAVEFORM HEIGHT ──
static inline int hitTestMarker (float mouseX, float mouseY,
                                   float wfX, float wfY, float wfW, float wfH,
                                   float viewStart, float viewEnd,
                                   const std::vector<WarpMarker>& markers,
                                   float totalBeats = 0.0f)
{
    if (markers.empty()) return -1;
    float visRange = viewEnd - viewStart;
    if (visRange < 0.0001f) return -1;
    if (mouseY < wfY - 2 || mouseY > wfY + wfH + 2) return -1;
    bool useBeatPos = (totalBeats > 0.001f);

    float bestDist = kHitRadius;
    int bestIdx = -1;

    for (int i = 1; i < static_cast<int>(markers.size()) - 1; ++i)
    {
        const auto& m = markers[static_cast<size_t>(i)];
        float normPos = useBeatPos ? (m.beatPos / totalBeats) : m.samplePos;
        if (normPos < viewStart || normPos > viewEnd) continue;
        float pixX = wfX + ((normPos - viewStart) / visRange) * wfW;
        float dist = std::abs (mouseX - pixX);
        if (dist < bestDist) { bestDist = dist; bestIdx = i; }
    }
    return bestIdx;
}

// ── Check if click is in ruler area ──
static inline bool isInRuler (float mouseY, float wfY)
{
    return mouseY >= wfY && mouseY < wfY + static_cast<float>(kRulerH) + 2.0f;
}

// ── Grid division right-click popup menu ──
static inline void showGridDivMenu (juce::Component* parent, int currentDiv,
                                      std::function<void(int)> onSelect)
{
    juce::PopupMenu menu;
    menu.addItem (100, "AUTO",          true, currentDiv == 0);
    menu.addSeparator();
    menu.addItem (101, "1/1  (bars)",   true, currentDiv == 1);
    menu.addItem (102, "1/2",           true, currentDiv == 2);
    menu.addItem (104, "1/4  (beats)",  true, currentDiv == 4);
    menu.addItem (108, "1/8",           true, currentDiv == 8);
    menu.addItem (116, "1/16",          true, currentDiv == 16);
    menu.addItem (132, "1/32",          true, currentDiv == 32);

    menu.showMenuAsync (juce::PopupMenu::Options()
        .withTargetComponent (parent)
        .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards),
        [onSelect](int result) {
            if (result == 0) return;
            int div = 0;
            switch (result) {
                case 100: div = 0; break;   case 101: div = 1; break;
                case 102: div = 2; break;   case 104: div = 4; break;
                case 108: div = 8; break;   case 116: div = 16; break;
                case 132: div = 32; break;
            }
            if (onSelect) onSelect (div);
        });
}

// ── Convert pixel X to normalized sample position ──
static inline float pixelToNorm (float mouseX, float wfX, float wfW,
                                   float viewStart, float viewEnd)
{
    float visRange = viewEnd - viewStart;
    return viewStart + ((mouseX - wfX) / wfW) * visRange;
}

// ── Right-click marker context menu ──
// markerIdx: which marker was right-clicked
// totalBeats: total beats in the sample (for quantize calculation)
static inline void showMarkerMenu (juce::Component* parent, int markerIdx,
                                     std::vector<WarpMarker>& markers, float totalBeats,
                                     std::function<void()> onChanged)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Remove marker");
    menu.addItem (6, "Remove ALL markers");
    menu.addSeparator();
    menu.addItem (2, "Quantize this marker");
    menu.addItem (3, "Quantize ALL markers");
    menu.addSeparator();
    menu.addItem (4, "Reset this marker");
    menu.addItem (5, "Reset ALL markers");

    // Capture by value what we need (markerIdx, totalBeats, pointers via lambda capture)
    auto* markersPtr = &markers;
    menu.showMenuAsync (juce::PopupMenu::Options()
        .withTargetComponent (parent)
        .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards),
        [markersPtr, markerIdx, totalBeats, onChanged](int result) {
            if (result == 0 || markersPtr == nullptr) return;
            auto& m = *markersPtr;

            switch (result)
            {
                case 1: // Remove
                    if (markerIdx > 0 && markerIdx < static_cast<int>(m.size()) - 1)
                        m.erase (m.begin() + markerIdx);
                    break;

                case 2: // Quantize this marker (snap samplePos to where beatPos naturally falls)
                    if (markerIdx > 0 && markerIdx < static_cast<int>(m.size()) - 1 && totalBeats > 0)
                        m[static_cast<size_t>(markerIdx)].samplePos = m[static_cast<size_t>(markerIdx)].beatPos / totalBeats;
                    break;

                case 3: // Quantize ALL markers
                    if (totalBeats > 0)
                        for (size_t i = 1; i < m.size() - 1; ++i)
                            m[i].samplePos = m[i].beatPos / totalBeats;
                    break;

                case 4: // Reset this marker
                    if (markerIdx >= 0 && markerIdx < static_cast<int>(m.size()))
                    {
                        m[static_cast<size_t>(markerIdx)].samplePos = m[static_cast<size_t>(markerIdx)].originalSamplePos;
                        m[static_cast<size_t>(markerIdx)].isAuto = true;
                    }
                    break;

                case 5: // Reset ALL markers
                    for (auto& wm : m) { wm.samplePos = wm.originalSamplePos; wm.isAuto = true; }
                    break;

                case 6: // Remove ALL markers → reset to 2 basic markers (start+end)
                {
                    m.clear();
                    WarpMarker startM; startM.samplePos = 0.0f; startM.beatPos = 0.0f;
                    startM.originalSamplePos = 0.0f; startM.isAuto = true;
                    WarpMarker endM; endM.samplePos = 1.0f; endM.beatPos = totalBeats;
                    endM.originalSamplePos = 1.0f; endM.isAuto = true;
                    m.push_back (startM);
                    m.push_back (endM);
                    break;
                }
            }

            // Re-sort after any modification
            std::sort (m.begin(), m.end(),
                [](const WarpMarker& a, const WarpMarker& b) { return a.samplePos < b.samplePos; });

            if (onChanged) onChanged();
        });
}

} // namespace WaveformOverlay
