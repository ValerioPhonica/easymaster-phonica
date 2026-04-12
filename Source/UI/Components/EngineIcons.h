#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ═══════════════════════════════════════════════════════════════════
// EngineIcons — Small programmatic icons for drum types and synth engines
//
// Usage: EngineIcons::drawDrumIcon (g, bounds, drumType, engineMode)
//        EngineIcons::drawSynthIcon (g, bounds, synthModel)
//
// Each icon is 16-24px, drawn with juce::Graphics primitives.
// Colors: amber/orange for analog, blue for FM, teal for sampler,
//         purple for elements, green for plaits, cyan for wavetable
// ═══════════════════════════════════════════════════════════════════

namespace EngineIcons
{

// ─── Colors ──────────────────────────────────────────────────────
static inline juce::Colour colAna  {0xff00c8e0u}; // cyan accent
static inline juce::Colour colFM   {0xff6090ddu}; // blue
static inline juce::Colour colSmp  {0xff40b0b0u}; // teal
static inline juce::Colour colElem {0xff9070c0u}; // purple
static inline juce::Colour colPlts {0xff70c050u}; // green
static inline juce::Colour colWT   {0xff50c0e0u}; // cyan
static inline juce::Colour colGran {0xffd06090u}; // rose/pink
static inline juce::Colour colDim  {0xff384050u}; // dim outline

// ─── Drum Type Icons ─────────────────────────────────────────────
// drumType: 0=Kick, 1=Snare, 2=HHC, 3=HHO, 4=Clap, 5=Tom, 6=TomHi, 7=Cowbell, 8=Rimshot, 9=Crash
// engineMode: 0=ANA, 1=FM, 2=SMP
inline void drawDrumIcon (juce::Graphics& g, juce::Rectangle<float> b, int drumType, int engineMode = 0)
{
    juce::Colour col = (engineMode == 1) ? colFM : (engineMode == 2) ? colSmp : colAna;
    float cx = b.getCentreX(), cy = b.getCentreY();
    float r = std::min (b.getWidth(), b.getHeight()) * 0.42f;

    switch (drumType)
    {
        case 0: // KICK — filled circle with dot
        {
            g.setColour (col.withAlpha (0.3f));
            g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
            g.setColour (col);
            g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.2f);
            g.fillEllipse (cx - 2, cy - 2, 4, 4);
            break;
        }
        case 1: // SNARE — circle with X
        {
            g.setColour (col);
            g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.2f);
            float d = r * 0.6f;
            g.drawLine (cx - d, cy - d, cx + d, cy + d, 1.0f);
            g.drawLine (cx + d, cy - d, cx - d, cy + d, 1.0f);
            break;
        }
        case 2: // HH CLOSED — two triangles touching
        case 3: // HH OPEN — two triangles with gap
        {
            float gap = (drumType == 3) ? 2.0f : 0.0f;
            g.setColour (col);
            juce::Path top, bot;
            top.addTriangle (cx - r, cy - gap, cx + r, cy - gap, cx, cy - r - gap);
            bot.addTriangle (cx - r, cy + gap, cx + r, cy + gap, cx, cy + r + gap);
            g.strokePath (top, juce::PathStrokeType (1.1f));
            g.strokePath (bot, juce::PathStrokeType (1.1f));
            break;
        }
        case 4: // CLAP — two hands (vertical lines)
        {
            g.setColour (col);
            float w = r * 0.35f;
            for (int h = 0; h < 3; ++h)
            {
                float x = cx - r * 0.6f + h * r * 0.6f;
                g.drawLine (x, cy - r * 0.7f, x, cy + r * 0.7f, 1.5f);
            }
            break;
        }
        case 5: // TOM
        case 6: // TOM HI — oval (like drum head from above)
        {
            float ry = r * 0.6f;
            g.setColour (col.withAlpha (0.2f));
            g.fillEllipse (cx - r, cy - ry, r * 2, ry * 2);
            g.setColour (col);
            g.drawEllipse (cx - r, cy - ry, r * 2, ry * 2, 1.2f);
            if (drumType == 6) // TomHi: smaller
                g.drawEllipse (cx - r * 0.5f, cy - ry * 0.5f, r, ry, 0.8f);
            break;
        }
        case 7: // COWBELL — trapezoid
        {
            g.setColour (col);
            juce::Path p;
            p.startNewSubPath (cx - r * 0.4f, cy - r);
            p.lineTo (cx + r * 0.4f, cy - r);
            p.lineTo (cx + r * 0.7f, cy + r);
            p.lineTo (cx - r * 0.7f, cy + r);
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (1.2f));
            break;
        }
        case 8: // RIMSHOT — circle with horizontal line
        {
            g.setColour (col);
            g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.0f);
            g.drawLine (cx - r * 1.2f, cy, cx + r * 1.2f, cy, 1.5f);
            break;
        }
        case 9: // CRASH — star burst
        {
            g.setColour (col);
            for (int ray = 0; ray < 8; ++ray)
            {
                float angle = ray * 3.14159265f / 4.0f;
                float x1 = cx + std::cos (angle) * r * 0.2f;
                float y1 = cy + std::sin (angle) * r * 0.2f;
                float x2 = cx + std::cos (angle) * r * 1.1f;
                float y2 = cy + std::sin (angle) * r * 1.1f;
                g.drawLine (x1, y1, x2, y2, 0.9f);
            }
            break;
        }
        default: // fallback: simple dot
            g.setColour (col);
            g.fillEllipse (cx - 3, cy - 3, 6, 6);
            break;
    }
}

// ─── Engine Mode Badge (small text badge: ANA/FM/SMP) ────────────
inline void drawEngineBadge (juce::Graphics& g, juce::Rectangle<float> b, int engineMode)
{
    const char* labels[] = {"ANA", "FM", "SMP"};
    juce::Colour cols[] = {colAna, colFM, colSmp};
    int idx = std::clamp (engineMode, 0, 2);

    g.setColour (cols[idx].withAlpha (0.15f));
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (cols[idx]);
    g.drawRoundedRectangle (b, 2.0f, 0.8f);
    g.setFont (juce::Font (8.0f, juce::Font::bold));
    g.drawText (labels[idx], b.toNearestInt(), juce::Justification::centred);
}

// ─── Synth Engine Icons ──────────────────────────────────────────
// model: 0=Analog, 1=FM, 2=DWGS, 3=Formant, 4=Sampler, 5=Wavetable
inline void drawSynthIcon (juce::Graphics& g, juce::Rectangle<float> b, int model)
{
    float cx = b.getCentreX(), cy = b.getCentreY();
    float r = std::min (b.getWidth(), b.getHeight()) * 0.42f;

    switch (model)
    {
        case 0: // ANALOG — sine wave
        {
            g.setColour (colAna);
            juce::Path p;
            for (int i = 0; i < 20; ++i)
            {
                float t = static_cast<float>(i) / 19;
                float x = b.getX() + t * b.getWidth();
                float y = cy - std::sin (t * 6.2831853f) * r * 0.8f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (1.3f));
            break;
        }
        case 1: // FM — modulated sine wave
        {
            g.setColour (colFM);
            juce::Path p;
            for (int i = 0; i < 20; ++i)
            {
                float t = static_cast<float>(i) / 19;
                float x = b.getX() + t * b.getWidth();
                float mod = std::sin (t * 12.566f) * 0.3f;
                float y = cy - std::sin (t * 6.2831853f + mod * 3) * r * 0.7f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (1.3f));
            break;
        }
        case 2: // DWGS — digital waveform (resonator circle)
        {
            g.setColour (colElem);
            g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.0f);
            g.drawLine (cx - r * 0.7f, cy, cx + r * 0.7f, cy, 0.8f);
            g.drawLine (cx, cy - r * 0.7f, cx, cy + r * 0.7f, 0.8f);
            g.fillEllipse (cx - 2, cy - 2, 4, 4);
            break;
        }
        case 3: // FORMANT — vowel triangle with dot
        {
            g.setColour (colPlts);
            juce::Path p;
            p.addTriangle (cx - r, cy + r * 0.7f, cx, cy - r, cx + r, cy + r * 0.7f);
            g.strokePath (p, juce::PathStrokeType (1.2f));
            g.fillEllipse (cx - 2, cy + r * 0.2f - 2, 4, 4);
            break;
        }
        case 4: // SAMPLER — waveform snippet
        {
            g.setColour (colSmp);
            juce::Path p;
            float pts[] = {0,0.2f,0.8f,0.5f,-0.3f,-0.9f,-0.4f,0.1f,0.6f,0.3f};
            for (int i = 0; i < 10; ++i)
            {
                float x = b.getX() + (float)i / 9 * b.getWidth();
                float y = cy - pts[i] * r * 0.8f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (1.3f));
            break;
        }
        case 5: // WAVETABLE — 3D stacked morphing waves (Serum-style)
        {
            for (int layer = 2; layer >= 0; --layer)
            {
                float offsetY = (2 - layer) * r * 0.35f;
                float offsetX = (2 - layer) * r * 0.15f;
                float alpha = (layer == 0) ? 1.0f : (layer == 1) ? 0.55f : 0.25f;
                g.setColour (colWT.withAlpha (alpha));
                juce::Path p;
                float morph = 1.0f + layer * 0.7f;
                for (int i = 0; i < 20; ++i)
                {
                    float t = static_cast<float>(i) / 19;
                    float x = b.getX() + offsetX + t * (b.getWidth() - offsetX * 2);
                    float y = cy - offsetY - std::sin (t * 6.2831853f * morph) * r * 0.4f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.strokePath (p, juce::PathStrokeType (layer == 0 ? 1.5f : 0.9f));
            }
            break;
        }
        case 6: // GRANULAR — scattered particle cloud
        {
            float positions[][2] = {
                {-0.6f,-0.3f}, {-0.2f,-0.7f}, {0.3f,-0.5f}, {0.7f,-0.2f},
                {-0.4f, 0.4f}, {0.1f, 0.2f},  {0.5f, 0.6f}, {-0.7f, 0.1f},
                {0.0f, -0.1f}, {0.4f, -0.8f}, {-0.3f, 0.7f}
            };
            float sizes[] = {2.5f, 1.8f, 3.0f, 2.0f, 2.2f, 3.5f, 1.5f, 2.0f, 2.8f, 1.6f, 2.0f};
            for (int j = 0; j < 11; ++j)
            {
                float px = cx + positions[j][0] * r;
                float py = cy + positions[j][1] * r;
                float sz = sizes[j];
                g.setColour (colGran.withAlpha (j < 6 ? 0.9f : 0.4f));
                g.fillEllipse (px - sz * 0.5f, py - sz * 0.5f, sz, sz);
            }
            break;
        }
        default:
            g.setColour (colAna);
            g.fillEllipse (cx - 3, cy - 3, 6, 6);
            break;
    }
}

// ─── Synth Engine Label ──────────────────────────────────────────
inline const char* getSynthModelName (int model)
{
    static const char* names[] = {"ANALOG", "FM", "DWGS", "FORMNT", "SAMPLR", "WAVTBL", "GRANUL"};
    return names[std::clamp (model, 0, 6)];
}

inline juce::Colour getSynthModelColour (int model)
{
    juce::Colour cols[] = {colAna, colFM, colElem, colPlts, colSmp, colWT, colGran};
    return cols[std::clamp (model, 0, 6)];
}

// model 2=DWGS (digital waveform, purple), 3=Formant (vowels, green)

// ─── Drum Type Name ──────────────────────────────────────────────
inline const char* getDrumTypeName (int type)
{
    static const char* names[] = {"KICK", "SNARE", "HH-C", "HH-O", "CLAP", "TOM", "TOM-H", "COWBL", "RIM", "CRASH"};
    return names[std::clamp (type, 0, 9)];
}

} // namespace EngineIcons
