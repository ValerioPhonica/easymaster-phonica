#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Audio/SynthEngine/WavetableVoice.h"

// ═══════════════════════════════════════════════════════════════════
// WavetableEditor — Serum-style popup wavetable editor
//
// • 3D waterfall display (left) + single frame editor (right)
// • Brush tool panel: 8 shapes (pencil, line, sine, saw, etc.)
// • Frame strip at bottom with thumbnails + navigation
// • FFT harmonic bar display at top of editor
// • Right-click menu: factory tables, generate, import, frame ops
// • Click+drag to navigate frames
// ═══════════════════════════════════════════════════════════════════

class WavetableEditor : public juce::Component
{
public:
    WavetableEditor (WavetableData& wtData) : data (&wtData)
    {
        if (data->isEmpty()) *data = WavetableData::createBasic();
    }

    std::function<void()> onChange;
    std::function<void(float)> onFrameChange;  // fired when user navigates frames, param = 0-1 position

    void setData (WavetableData& newData)
    {
        data = &newData;
        if (data->isEmpty()) *data = WavetableData::createBasic();
        currentFrame = 0;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        if (bounds.getWidth() < 20 || bounds.getHeight() < 20) return;

        // ── Outer panel: rounded dark background with subtle border ──
        g.setColour (juce::Colour (0xff0d0f14));
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        g.setColour (juce::Colour (0xff2a3040));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);

        auto inner = bounds.reduced (3);

        // Layout: header(22) | main area | frame strip(40) | toolbar(16)
        auto header   = inner.removeFromTop (22);
        auto toolbar  = inner.removeFromBottom (16);
        auto strip    = inner.removeFromBottom (40);
        inner.removeFromTop (2); // gap
        inner.removeFromBottom (2); // gap
        auto mainArea = inner;

        paintHeader (g, header);
        paintToolbar (g, toolbar);
        paintFrameStrip (g, strip);

        // Main: left 36% = waterfall, right 64% = editor
        int splitX = mainArea.getWidth() * 36 / 100;
        auto wfArea = mainArea.removeFromLeft (splitX);
        mainArea.removeFromLeft (2); // gap
        auto edArea = mainArea;

        paintWaterfall (g, wfArea);

        // Editor right side: FFT on top, then brush+frame editor
        auto fftArea = edArea.removeFromTop (std::min (44, edArea.getHeight() / 3));
        edArea.removeFromTop (2);
        paintFFT (g, fftArea);
        paintBrushPanel (g, edArea.removeFromLeft (52));
        edArea.removeFromLeft (2);
        paintFrameEditor (g, edArea);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) { showContextMenu (e.getScreenPosition()); return; }
        if (waterfallBounds.contains (e.getPosition()))
        {
            isWaterfallDrag = true;
            dragStartFrame = currentFrame;
            dragStartY = e.getPosition().y;
            return;
        }
        if (fftBounds.contains (e.getPosition())) { drawOnFFT (e.x, e.y); return; }
        if (brushBounds.contains (e.getPosition())) { selectBrush (e.x, e.y); return; }
        if (stripBounds.contains (e.getPosition())) { selectFrameFromStrip (e.x); return; }
        if (editBounds.contains (e.getPosition())) { isDrawing = true; drawOnFrame (e.x, e.y); }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isWaterfallDrag && data->numFrames > 1)
        {
            // Continuous position from drag (smooth, not quantized to frames)
            float dragRange = static_cast<float>(waterfallBounds.getHeight());
            float delta = static_cast<float>(dragStartY - e.getPosition().y);
            float frameDelta = delta / dragRange * static_cast<float>(data->numFrames);
            float continuousFrame = static_cast<float>(dragStartFrame) + frameDelta;
            continuousFrame = std::clamp (continuousFrame, 0.0f, static_cast<float>(data->numFrames - 1));
            // Update display frame (integer for visual)
            int newFrame = static_cast<int>(std::round (continuousFrame));
            newFrame = std::clamp (newFrame, 0, data->numFrames - 1);
            currentFrame = newFrame;
            // Send CONTINUOUS position (smooth for audio)
            if (onFrameChange && data->numFrames > 1)
            {
                float pos = continuousFrame / static_cast<float>(data->numFrames - 1);
                onFrameChange (std::clamp (pos, 0.0f, 1.0f));
            }
            repaint();
            return;
        }
        if (isDrawing && editBounds.contains (e.getPosition())) drawOnFrame (e.x, e.y);
        if (fftBounds.contains (e.getPosition())) drawOnFFT (e.x, e.y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (isWaterfallDrag) { isWaterfallDrag = false; return; }
        if (isDrawing) { isDrawing = false; data->buildMipMaps(); if (onChange) onChange(); }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override
    {
        // Disabled — wavetable frame navigation is click+drag only
    }

    int getCurrentFrame() const { return currentFrame; }
    void setCurrentFrame (int f) { currentFrame = std::clamp (f, 0, std::max (0, data->numFrames - 1)); fireFrameChange(); repaint(); }

    // ── Public display methods (called by SynthTrackRow for real-time updates) ──
    void setDisplayPosition (float p) { if (std::abs(displayPos - p) > 0.001f) { displayPos = p; repaint(); } }
    void setDisplayWarp (int mode, float amt) {
        if (displayWarpMode != mode || std::abs(displayWarpAmt - amt) > 0.005f)
        { displayWarpMode = mode; displayWarpAmt = amt; repaint(); }
    }

private:
    WavetableData* data;
    int currentFrame = 0;
    float displayPos = 0.0f; // 0-1, real-time WT position from knob
    int displayWarpMode = 0;   // current warp mode for visualization
    float displayWarpAmt = 0.0f; // current warp amount for visualization
    bool isDrawing = false;
    bool isWaterfallDrag = false;
    int dragStartFrame = 0;
    int dragStartY = 0;
    int currentBrush = 0; // 0=pencil, 1=line, 2=sine, 3=saw, 4=tri, 5=square, 6=noise, 7=smooth

    // Cached bounds for hit testing
    juce::Rectangle<int> editBounds, fftBounds, brushBounds, stripBounds, waterfallBounds;

    void fireFrameChange()
    {
        if (onFrameChange && data && data->numFrames > 1)
        {
            float pos = static_cast<float>(currentFrame) / static_cast<float>(data->numFrames - 1);
            onFrameChange (std::clamp (pos, 0.0f, 1.0f));
        }
    }

    static inline const juce::Colour amber {0xff00c8e0u}; // cyan accent
    static inline const juce::Colour dimText {0xff506070u};

    // ═══ HEADER ═══
    void paintHeader (juce::Graphics& g, juce::Rectangle<int> r)
    {
        // Subtle gradient header
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff1e2230), static_cast<float>(r.getX()), static_cast<float>(r.getY()),
            juce::Colour (0xff16181e), static_cast<float>(r.getX()), static_cast<float>(r.getBottom()), false));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        // Bottom accent line
        g.setColour (amber.withAlpha (0.4f));
        g.fillRect (r.getX() + 4, r.getBottom() - 1, r.getWidth() - 8, 1);

        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
        g.setColour (amber);
        g.drawText ("WT: " + data->name + "  [" + juce::String (data->numFrames) + "f]",
                     r.reduced (8, 0), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff80a0c0));
        g.drawText ("Frame " + juce::String (currentFrame + 1) + "/" + juce::String (data->numFrames),
                     r.reduced (8, 0), juce::Justification::centredRight);
    }

    // ═══ TOOLBAR ═══
    void paintToolbar (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (juce::Colour (0xff10121a));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (juce::Colour (0xff404860));
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
        g.drawText ("Right-click: factory tables, import, generate | Wheel: scroll frames | Cl...",
                     r.reduced (6, 0), juce::Justification::centredLeft);
    }

    // ═══ 3D WATERFALL ═══
    void paintWaterfall (juce::Graphics& g, juce::Rectangle<int> r)
    {
        waterfallBounds = r;
        // Panel background with subtle border
        g.setColour (juce::Colour (0xff08090e));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff1e2230));
        g.drawRoundedRectangle (r.toFloat(), 4.0f, 0.5f);
        if (data->isEmpty()) return;

        auto inner = r.reduced (4);
        int vis = std::min (data->numFrames, 32);
        float stepY = static_cast<float>(inner.getHeight()) / (vis + 2);
        float wfW = static_cast<float>(inner.getWidth()) * 0.85f;

        for (int fi = 0; fi < vis; ++fi)
        {
            int frameIdx = fi * data->numFrames / vis;
            const float* frame = data->getFrame (frameIdx);
            if (!frame) continue;

            float baseY = inner.getY() + (vis - fi) * stepY;
            float baseX = inner.getX() + fi * 1.5f + 4;

            juce::Path path;
            for (int s = 0; s < static_cast<int>(wfW); ++s)
            {
                int si = s * WavetableData::kFrameSize / static_cast<int>(wfW);
                float v = frame[std::min (si, WavetableData::kFrameSize - 1)];
                float x = baseX + s, y = baseY - v * stepY * 1.5f;
                if (s == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }

            bool cur = (frameIdx == currentFrame || (fi > 0 && (fi - 1) * data->numFrames / vis <= currentFrame && currentFrame <= frameIdx));
            g.setColour (cur ? amber : juce::Colour (0xff2080c0).withAlpha (0.7f));
            g.strokePath (path, juce::PathStrokeType (cur ? 2.0f : 0.9f));
        }

        // ── Real-time interpolated waveform at displayPos (bright cyan) ──
        if (data->numFrames >= 2)
        {
            float framePos = displayPos * static_cast<float>(data->numFrames - 1);
            int baseFrame = static_cast<int>(std::floor (framePos));
            float frac = framePos - static_cast<float>(baseFrame);
            int f0 = std::clamp (baseFrame - 1, 0, data->numFrames - 1);
            int f1 = std::clamp (baseFrame,     0, data->numFrames - 1);
            int f2 = std::clamp (baseFrame + 1, 0, data->numFrames - 1);
            int f3 = std::clamp (baseFrame + 2, 0, data->numFrames - 1);

            const float* fr0 = data->getFrame(f0);
            const float* fr1 = data->getFrame(f1);
            const float* fr2 = data->getFrame(f2);
            const float* fr3 = data->getFrame(f3);
            if (fr0 && fr1 && fr2 && fr3)
            {
                // Draw at center of the stack
                float posY = inner.getY() + inner.getHeight() * 0.5f;
                float posX = inner.getX() + 4;
                juce::Path interpPath;
                for (int s = 0; s < static_cast<int>(wfW); ++s)
                {
                    int si = s * WavetableData::kFrameSize / static_cast<int>(wfW);
                    si = std::min (si, WavetableData::kFrameSize - 1);
                    // Cubic Hermite between frames
                    float s0 = fr0[si], s1 = fr1[si], s2 = fr2[si], s3 = fr3[si];
                    float c1 = 0.5f*(s2-s0), c2 = s0-2.5f*s1+2*s2-0.5f*s3, c3 = 0.5f*(s3-s0)+1.5f*(s1-s2);
                    float v = ((c3*frac+c2)*frac+c1)*frac+s1;
                    float x = posX + s, y = posY - v * stepY * 2.0f;
                    if (s == 0) interpPath.startNewSubPath (x, y); else interpPath.lineTo (x, y);
                }
                // Glow
                g.setColour (juce::Colour (0xff40e0ff).withAlpha (0.22f));
                g.strokePath (interpPath, juce::PathStrokeType (5.0f));
                // Core
                g.setColour (juce::Colour (0xff40e0ff));
                g.strokePath (interpPath, juce::PathStrokeType (2.0f));

                // Position text
                g.setFont (8.0f);
                g.setColour (juce::Colour (0xff40e0ff));
                int pctPos = static_cast<int>(displayPos * 100);
                g.drawText ("POS:" + juce::String(pctPos) + "%", inner.getRight() - 60, inner.getY() + 2, 56, 12, juce::Justification::right);
            }
        }
    }

    // ═══ FFT HARMONIC BARS ═══
    void paintFFT (juce::Graphics& g, juce::Rectangle<int> r)
    {
        fftBounds = r;
        g.setColour (juce::Colour (0xff08090e));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff1e2230));
        g.drawRoundedRectangle (r.toFloat(), 4.0f, 0.5f);
        if (data->isEmpty() || currentFrame >= data->numFrames) return;

        const float* frame = data->getFrame (currentFrame);
        if (!frame) return;

        auto inner = r.reduced (3);
        int numBins = std::min (32, inner.getWidth() / 4);
        float barW = static_cast<float>(inner.getWidth()) / numBins;

        for (int h = 0; h < numBins; ++h)
        {
            float re = 0, im = 0;
            for (int s = 0; s < WavetableData::kFrameSize; ++s)
            {
                float angle = 6.2831853f * (h + 1) * s / WavetableData::kFrameSize;
                re += frame[s] * std::cos (angle);
                im += frame[s] * std::sin (angle);
            }
            float mag = std::sqrt (re * re + im * im) / (WavetableData::kFrameSize / 2);
            mag = std::min (1.0f, mag);

            float x = inner.getX() + h * barW;
            float barH = mag * (inner.getHeight() - 2);
            g.setColour (juce::Colour (0xff20b0c0).withAlpha (0.6f + mag * 0.4f));
            g.fillRoundedRectangle (x + 1, inner.getBottom() - barH - 1, barW - 2, barH, 1.0f);
        }

        g.setColour (juce::Colour (0xff405060));
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain));
        g.drawText ("HARMONICS (click to draw)", r.reduced (4, 1), juce::Justification::topLeft);
    }

    // ═══ BRUSH PANEL ═══
    void paintBrushPanel (juce::Graphics& g, juce::Rectangle<int> r)
    {
        brushBounds = r;
        g.setColour (juce::Colour (0xff0c0e14));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff1e2230));
        g.drawRoundedRectangle (r.toFloat(), 4.0f, 0.5f);

        auto inner = r.reduced (2);
        const char* names[] = {"PEN","LINE","SIN","SAW","TRI","SQR","RND","SMO"};
        int btnH = inner.getHeight() / 8;

        for (int i = 0; i < 8; ++i)
        {
            auto btn = juce::Rectangle<int>(inner.getX() + 1, inner.getY() + i * btnH + 1, inner.getWidth() - 2, btnH - 2);
            bool sel = (i == currentBrush);
            g.setColour (sel ? amber.withAlpha (0.25f) : juce::Colour (0xff141820));
            g.fillRoundedRectangle (btn.toFloat(), 2.5f);
            if (sel)
            {
                g.setColour (amber.withAlpha (0.5f));
                g.drawRoundedRectangle (btn.toFloat(), 2.5f, 0.7f);
            }
            g.setColour (sel ? amber : juce::Colour (0xff506070));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, sel ? juce::Font::bold : juce::Font::plain));
            g.drawText (names[i], btn, juce::Justification::centred);
        }
    }

    // ═══ FRAME EDITOR ═══
    void paintFrameEditor (juce::Graphics& g, juce::Rectangle<int> r)
    {
        editBounds = r;
        // Panel with subtle border
        g.setColour (juce::Colour (0xff08090e));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff1e2230));
        g.drawRoundedRectangle (r.toFloat(), 4.0f, 0.5f);

        auto inner = r.reduced (2);

        // Grid
        g.setColour (juce::Colour (0xff161a24));
        float midY = inner.getY() + inner.getHeight() * 0.5f;
        g.drawHorizontalLine ((int)midY, (float)inner.getX(), (float)inner.getRight());
        for (int gi = 1; gi < 8; ++gi)
            g.drawVerticalLine (inner.getX() + inner.getWidth() * gi / 8, (float)inner.getY(), (float)inner.getBottom());

        // Center line highlight
        g.setColour (juce::Colour (0xff252a36));
        g.drawHorizontalLine ((int)midY, (float)inner.getX(), (float)inner.getRight());

        if (data->isEmpty() || currentFrame >= data->numFrames) return;
        const float* frame = data->getFrame (currentFrame);
        if (!frame) return;

        // Waveform with glow
        juce::Path path;
        float eW = (float)inner.getWidth(), eH = (float)inner.getHeight();
        for (int s = 0; s < (int)eW; ++s)
        {
            int si = s * WavetableData::kFrameSize / (int)eW;
            float v = frame[std::min (si, WavetableData::kFrameSize - 1)];
            float x = inner.getX() + s;
            float y = midY - v * eH * 0.45f;
            if (s == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        // Subtle glow
        g.setColour (amber.withAlpha (0.2f));
        g.strokePath (path, juce::PathStrokeType (5.0f));
        // Main line
        g.setColour (amber.withAlpha (0.95f));
        g.strokePath (path, juce::PathStrokeType (1.8f));

        // ── Warped waveform overlay (cyan) — shows actual output shape ──
        if (displayWarpMode > 0 && displayWarpAmt > 0.005f)
        {
            auto wm = static_cast<WarpMode>(displayWarpMode);
            juce::Path warpPath;
            for (int s = 0; s < (int)eW; ++s)
            {
                float phase = static_cast<float>(s) / eW;
                float warpedPhase = applyWarp (phase, displayWarpAmt, wm);
                // Read sample at warped phase position (linear interpolation)
                float fIdx = warpedPhase * (WavetableData::kFrameSize - 1);
                int i0 = std::clamp (static_cast<int>(fIdx), 0, WavetableData::kFrameSize - 2);
                float frac = fIdx - static_cast<float>(i0);
                float v = frame[i0] * (1.0f - frac) + frame[i0 + 1] * frac;
                float x = inner.getX() + s;
                float y = midY - v * eH * 0.45f;
                if (s == 0) warpPath.startNewSubPath (x, y); else warpPath.lineTo (x, y);
            }
            // Glow
            g.setColour (juce::Colour (0xff40e0ff).withAlpha (0.12f));
            g.strokePath (warpPath, juce::PathStrokeType (3.5f));
            // Core
            g.setColour (juce::Colour (0xff40e0ff).withAlpha (0.85f));
            g.strokePath (warpPath, juce::PathStrokeType (1.2f));

            // Warp mode label
            const char* warpNames[] = {"","BND+","BND-","B+-","ASYM","FM","QNTZ","MIR","SQZ","WRP","SYN","SAT"};
            int wIdx = std::clamp (displayWarpMode, 0, 11);
            g.setColour (juce::Colour (0xff40e0ff).withAlpha (0.7f));
            g.setFont (8.0f);
            g.drawText (juce::String (warpNames[wIdx]) + " " + juce::String (static_cast<int>(displayWarpAmt * 100)) + "%",
                        inner.getX() + 2, inner.getY() + 2, 80, 10, juce::Justification::left);
        }
    }

    // ═══ FRAME STRIP ═══
    void paintFrameStrip (juce::Graphics& g, juce::Rectangle<int> r)
    {
        stripBounds = r;
        g.setColour (juce::Colour (0xff0c0e14));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff1e2230));
        g.drawRoundedRectangle (r.toFloat(), 4.0f, 0.5f);
        if (data->isEmpty()) return;

        auto inner = r.reduced (2, 2);
        int numVis = std::min (data->numFrames, inner.getWidth() / 8);
        if (numVis < 1) numVis = 1;
        float thumbW = static_cast<float>(inner.getWidth()) / numVis;

        for (int fi = 0; fi < numVis; ++fi)
        {
            int frameIdx = fi * data->numFrames / numVis;
            const float* frame = data->getFrame (frameIdx);
            if (!frame) continue;

            float x = inner.getX() + fi * thumbW;
            float w = thumbW - 1;
            float h = (float)inner.getHeight() - 4;
            float midY = inner.getY() + h * 0.5f + 2;

            bool isCur = (frameIdx == currentFrame);
            g.setColour (isCur ? juce::Colour (0xff1a2438) : juce::Colour (0xff10121a));
            g.fillRoundedRectangle (x, (float)inner.getY() + 1, w, h + 2, 2.0f);
            if (isCur)
            {
                g.setColour (amber.withAlpha (0.4f));
                g.drawRoundedRectangle (x, (float)inner.getY() + 1, w, h + 2, 2.0f, 0.7f);
            }

            // Mini waveform
            juce::Path mini;
            for (int s = 0; s < (int)w; ++s)
            {
                int si = s * WavetableData::kFrameSize / std::max (1, (int)w);
                float v = frame[std::min (si, WavetableData::kFrameSize - 1)];
                float px = x + s, py = midY - v * h * 0.4f;
                if (s == 0) mini.startNewSubPath (px, py); else mini.lineTo (px, py);
            }
            g.setColour (isCur ? amber : juce::Colour (0xff2878b0));
            g.strokePath (mini, juce::PathStrokeType (isCur ? 1.2f : 0.8f));

            // Frame number
            if (thumbW > 14)
            {
                g.setColour (juce::Colour (0xff405060));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
                g.drawText (juce::String (frameIdx + 1), (int)x, inner.getBottom() - 10, (int)w, 10, juce::Justification::centred);
            }
        }
    }

    // ─── Drawing ───
    void drawOnFrame (int mx, int my)
    {
        if (data->isEmpty() || currentFrame >= data->numFrames) return;
        if (editBounds.getWidth() < 2 || editBounds.getHeight() < 2) return;
        float relX = std::clamp ((float)(mx - editBounds.getX()) / editBounds.getWidth(), 0.0f, 1.0f);
        float relY = std::clamp ((float)(my - editBounds.getY()) / editBounds.getHeight(), 0.0f, 1.0f);
        float value = 1.0f - relY * 2.0f;
        int smpIdx = (int)(relX * (WavetableData::kFrameSize - 1));
        int base = currentFrame * WavetableData::kFrameSize;

        int bw = WavetableData::kFrameSize / 64; // brush width
        if (currentBrush == 7) bw = WavetableData::kFrameSize / 32; // smooth = wider

        for (int b = -bw; b <= bw; ++b)
        {
            int idx = smpIdx + b;
            if (idx < 0 || idx >= WavetableData::kFrameSize) continue;
            float w = 1.0f - std::abs ((float)b) / (bw + 1);
            float& sample = data->frames[static_cast<size_t>(base + idx)];

            switch (currentBrush)
            {
                case 0: sample = sample * (1 - w * 0.7f) + value * w * 0.7f; break; // pencil
                case 1: sample = value; break; // line (hard)
                case 2: { float ph = (float)idx / WavetableData::kFrameSize; sample = std::sin (ph * 6.2831853f) * w + sample * (1 - w); } break;
                case 3: { float ph = (float)idx / WavetableData::kFrameSize; sample = (2 * ph - 1) * w + sample * (1 - w); } break;
                case 4: { float ph = (float)idx / WavetableData::kFrameSize; float t = ph < 0.5f ? 4*ph-1 : 3-4*ph; sample = t * w + sample * (1 - w); } break;
                case 5: { float ph = (float)idx / WavetableData::kFrameSize; sample = (ph < 0.5f ? 1.0f : -1.0f) * w + sample * (1 - w); } break;
                case 6: sample = ((float)std::rand() / RAND_MAX * 2 - 1) * w + sample * (1 - w); break;
                case 7: // smooth
                {
                    int p = (idx - 1 + WavetableData::kFrameSize) % WavetableData::kFrameSize;
                    int n = (idx + 1) % WavetableData::kFrameSize;
                    float avg = (data->frames[static_cast<size_t>(base+p)] + sample * 2 + data->frames[static_cast<size_t>(base+n)]) * 0.25f;
                    sample = avg; break;
                }
            }
        }
        repaint();
    }

    void drawOnFFT (int mx, int my)
    {
        if (data->isEmpty() || currentFrame >= data->numFrames) return;
        if (fftBounds.getWidth() < 4 || fftBounds.getHeight() < 2) return;
        int numBins = std::min (32, fftBounds.getWidth() / 4);
        if (numBins < 1) return;
        float barW = (float)fftBounds.getWidth() / numBins;
        int bin = (int)((mx - fftBounds.getX()) / barW);
        if (bin < 0 || bin >= numBins) return;

        float mag = 1.0f - std::clamp ((float)(my - fftBounds.getY()) / fftBounds.getHeight(), 0.0f, 1.0f);

        // Reconstruct: set this harmonic's amplitude
        int base = currentFrame * WavetableData::kFrameSize;
        int h = bin + 1;
        for (int s = 0; s < WavetableData::kFrameSize; ++s)
        {
            float angle = 6.2831853f * h * s / WavetableData::kFrameSize;
            // Additive: just add/subtract this harmonic (simplified)
            data->frames[static_cast<size_t>(base + s)] += mag * 0.1f * std::sin (angle);
        }

        // Normalize
        float peak = 0;
        for (int s = 0; s < WavetableData::kFrameSize; ++s)
            peak = std::max (peak, std::abs (data->frames[static_cast<size_t>(base + s)]));
        if (peak > 1.0f)
            for (int s = 0; s < WavetableData::kFrameSize; ++s)
                data->frames[static_cast<size_t>(base + s)] /= peak;

        data->buildMipMaps();
        if (onChange) onChange();
        repaint();
    }

    void selectBrush (int mx, int my)
    {
        int idx = (my - brushBounds.getY()) * 8 / brushBounds.getHeight();
        currentBrush = std::clamp (idx, 0, 7);
        repaint();
    }

    void selectFrameFromStrip (int mx)
    {
        if (data->isEmpty()) return;
        float rel = (float)(mx - stripBounds.getX()) / stripBounds.getWidth();
        rel = std::clamp (rel, 0.0f, 1.0f);
        currentFrame = std::clamp ((int)(rel * data->numFrames), 0, data->numFrames - 1);
        // Send continuous position (smooth for audio)
        if (onFrameChange && data->numFrames > 1)
            onFrameChange (rel);
        repaint();
    }

    void showContextMenu (juce::Point<int> screenPos)
    {
        juce::PopupMenu menu;

        juce::PopupMenu factoryMenu;
        factoryMenu.addItem (100, "Init Table (blank 8 frames)");
        factoryMenu.addSeparator();
        factoryMenu.addItem (101, "Basic (Sine>Saw>Sq)");
        factoryMenu.addItem (102, "PWM");
        factoryMenu.addItem (103, "Formant (Vowels)");
        factoryMenu.addItem (104, "Metallic (Bells)");
        factoryMenu.addItem (105, "Digital (Harmonics)");
        factoryMenu.addItem (106, "SuperSaw");
        factoryMenu.addItem (107, "Vocal Choir");
        factoryMenu.addItem (108, "Pluck");
        factoryMenu.addItem (109, "Spectral");
        menu.addSubMenu ("Factory Tables", factoryMenu);

        juce::PopupMenu genMenu;
        genMenu.addItem (201, "Sine"); genMenu.addItem (202, "Saw"); genMenu.addItem (203, "Square");
        genMenu.addItem (204, "Triangle"); genMenu.addItem (205, "Noise"); genMenu.addItem (206, "Additive 8H");
        menu.addSubMenu ("Generate Frame", genMenu);

        menu.addSeparator();
        menu.addItem (301, "Import .wav...");
        menu.addItem (310, "Clear Frame");
        menu.addItem (311, "Normalize Frame");
        menu.addItem (312, "Invert Frame");
        menu.addItem (313, "Smooth Frame");
        menu.addSeparator();
        menu.addItem (401, "Add Frame After");
        menu.addItem (402, "Delete Frame");
        menu.addItem (403, "Duplicate Frame");
        menu.addSeparator();
        juce::PopupMenu morphMenu;
        morphMenu.addItem (404, "Spectral Morph (FFT interpolation)");
        morphMenu.addItem (405, "Linear Crossfade (time-domain)");
        morphMenu.addItem (406, "Harmonic Fade (additive)");
        morphMenu.addItem (407, "Random Fill (randomize middle)");
        menu.addSubMenu ("Morph / Fill Frames", morphMenu);

        auto opts = juce::PopupMenu::Options().withTargetScreenArea ({screenPos.x, screenPos.y, 1, 1});
        if (auto* top = getTopLevelComponent()) opts = opts.withParentComponent (top);

        menu.showMenuAsync (opts, [this] (int r) { handleMenuResult (r); });
    }

    void handleMenuResult (int r)
    {
        if (r == 0) return;
        // Init Table — blank 8 frames for drawing from scratch
        if (r == 100)
        {
            data->name = "Init"; data->numFrames = 8;
            data->frames.assign (static_cast<size_t>(8 * WavetableData::kFrameSize), 0.0f);
            data->buildMipMaps();
            currentFrame = 0; if (onChange) onChange(); repaint(); return;
        }
        if (r >= 101 && r <= 109)
        {
            switch (r) {
                case 101: *data = WavetableData::createBasic(); break;
                case 102: *data = WavetableData::createPWM(); break;
                case 103: *data = WavetableData::createFormant(); break;
                case 104: *data = WavetableData::createMetallic(); break;
                case 105: *data = WavetableData::createDigital(); break;
                case 106: *data = WavetableData::createSuperSaw(); break;
                case 107: *data = WavetableData::createVocalChoir(); break;
                case 108: *data = WavetableData::createPluck(); break;
                case 109: *data = WavetableData::createSpectral(); break;
            }
            currentFrame = 0; if (onChange) onChange(); repaint();
        }
        else if (r >= 201 && r <= 206)
        {
            ensureMinFrames();
            int base = currentFrame * WavetableData::kFrameSize;
            for (int s = 0; s < WavetableData::kFrameSize; ++s)
            {
                float ph = (float)s / WavetableData::kFrameSize;
                float t = ph * 6.2831853f;
                float v = 0;
                switch (r) {
                    case 201: v = std::sin (t); break;
                    case 202: v = 2*ph-1; break;
                    case 203: v = ph<0.5f?1:-1; break;
                    case 204: v = ph<0.5f?(4*ph-1):(3-4*ph); break;
                    case 205: v = (float)std::rand()/RAND_MAX*2-1; break;
                    case 206: for (int h=1;h<=8;++h) v+=std::sin(t*h)/h; v*=0.5f; break;
                }
                data->frames[static_cast<size_t>(base + s)] = v;
            }
            data->buildMipMaps(); if (onChange) onChange(); repaint();
        }
        else if (r == 301)
        {
            auto chooser = std::make_shared<juce::FileChooser>("Import Wavetable", juce::File(), "*.wav;*.aif;*.aiff");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser](const juce::FileChooser& fc) {
                    auto file = fc.getResult();
                    if (file.existsAsFile()) {
                        juce::AudioFormatManager fm; fm.registerBasicFormats();
                        if (auto reader = std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(file))) {
                            juce::AudioBuffer<float> buf(1, (int)reader->lengthInSamples);
                            reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, false);
                            data->importFromBuffer(buf);
                            data->name = file.getFileNameWithoutExtension();
                            currentFrame = 0; if (onChange) onChange(); repaint();
                        }
                    }
                });
        }
        else if (r == 310) { clearFrame(); }
        else if (r == 311) { normalizeFrame(); }
        else if (r == 312) { invertFrame(); }
        else if (r == 313) { smoothFrame(); }
        else if (r == 401) { addFrame(); }
        else if (r == 402) { deleteFrame(); }
        else if (r == 403) { duplicateFrame(); }
        else if (r == 404) { spectralMorphAll(); }
        else if (r == 405) { linearMorphAll(); }
        else if (r == 406) { harmonicMorphAll(); }
        else if (r == 407) { randomFillMiddle(); }
    }

    void ensureMinFrames() { if (data->isEmpty()) { data->numFrames=1; data->frames.resize(WavetableData::kFrameSize,0); currentFrame=0; } }

    void clearFrame() {
        if (data->isEmpty()||currentFrame>=data->numFrames) return;
        int b=currentFrame*WavetableData::kFrameSize;
        std::fill(data->frames.begin()+b, data->frames.begin()+b+WavetableData::kFrameSize, 0.0f);
        data->buildMipMaps(); if(onChange) onChange(); repaint();
    }
    void normalizeFrame() {
        if (data->isEmpty()||currentFrame>=data->numFrames) return;
        int b=currentFrame*WavetableData::kFrameSize; float pk=0;
        for(int s=0;s<WavetableData::kFrameSize;++s) pk=std::max(pk,std::abs(data->frames[static_cast<size_t>(b+s)]));
        if(pk>0.001f){float g=1/pk; for(int s=0;s<WavetableData::kFrameSize;++s) data->frames[static_cast<size_t>(b+s)]*=g;}
        data->buildMipMaps(); if(onChange) onChange(); repaint();
    }
    void invertFrame() {
        if (data->isEmpty()||currentFrame>=data->numFrames) return;
        int b=currentFrame*WavetableData::kFrameSize;
        for(int s=0;s<WavetableData::kFrameSize;++s) data->frames[static_cast<size_t>(b+s)]*=-1;
        data->buildMipMaps(); if(onChange) onChange(); repaint();
    }
    void smoothFrame() {
        if (data->isEmpty()||currentFrame>=data->numFrames) return;
        int b=currentFrame*WavetableData::kFrameSize;
        std::vector<float> tmp(WavetableData::kFrameSize);
        for(int s=0;s<WavetableData::kFrameSize;++s){
            int p=(s-1+WavetableData::kFrameSize)%WavetableData::kFrameSize, n=(s+1)%WavetableData::kFrameSize;
            tmp[static_cast<size_t>(s)]=(data->frames[static_cast<size_t>(b+p)]+data->frames[static_cast<size_t>(b+s)]*2+data->frames[static_cast<size_t>(b+n)])*0.25f;
        }
        std::copy(tmp.begin(),tmp.end(),data->frames.begin()+b);
        data->buildMipMaps(); if(onChange) onChange(); repaint();
    }
    void addFrame() {
        if(data->numFrames>=WavetableData::kMaxFrames) return;
        int pos=(currentFrame+1)*WavetableData::kFrameSize;
        data->frames.insert(data->frames.begin()+pos, WavetableData::kFrameSize, 0.0f);
        data->numFrames++; currentFrame++;
        data->buildMipMaps(); if(onChange) onChange(); repaint();
    }
    void deleteFrame() {
        if(data->numFrames<=1) return;
        int pos=currentFrame*WavetableData::kFrameSize;
        data->frames.erase(data->frames.begin()+pos, data->frames.begin()+pos+WavetableData::kFrameSize);
        data->numFrames--; currentFrame=std::min(currentFrame,data->numFrames-1);
        data->buildMipMaps(); if(onChange) onChange(); repaint();
    }
    void duplicateFrame() {
        if(data->numFrames>=WavetableData::kMaxFrames||data->isEmpty()||currentFrame>=data->numFrames) return;
        int src=currentFrame*WavetableData::kFrameSize;
        int pos=(currentFrame+1)*WavetableData::kFrameSize;
        std::vector<float> copy(data->frames.begin()+src, data->frames.begin()+src+WavetableData::kFrameSize);
        data->frames.insert(data->frames.begin()+pos, copy.begin(), copy.end());
        data->numFrames++; currentFrame++;
        data->buildMipMaps(); if(onChange) onChange(); repaint();
    }

    // Spectral morph: interpolate frequency domain between first and last frame
    void spectralMorphAll() {
        if (data->numFrames < 3) return;
        int N = WavetableData::kFrameSize;
        int numBins = N / 2;
        const float* first = data->getFrame (0);
        const float* last = data->getFrame (data->numFrames - 1);
        if (!first || !last) return;

        // DFT of first and last frame
        std::vector<float> magA(static_cast<size_t>(numBins)), phA(static_cast<size_t>(numBins));
        std::vector<float> magB(static_cast<size_t>(numBins)), phB(static_cast<size_t>(numBins));
        for (int h = 0; h < numBins; ++h)
        {
            float reA=0,imA=0,reB=0,imB=0;
            for (int s = 0; s < N; ++s)
            {
                float angle = 6.2831853f * (h+1) * s / N;
                float c = std::cos(angle), sn = std::sin(angle);
                reA += first[s]*c; imA += first[s]*sn;
                reB += last[s]*c;  imB += last[s]*sn;
            }
            magA[static_cast<size_t>(h)] = std::sqrt(reA*reA+imA*imA);
            phA[static_cast<size_t>(h)] = std::atan2(imA,reA);
            magB[static_cast<size_t>(h)] = std::sqrt(reB*reB+imB*imB);
            phB[static_cast<size_t>(h)] = std::atan2(imB,reB);
        }

        // Interpolate middle frames in spectral domain
        for (int f = 1; f < data->numFrames - 1; ++f)
        {
            float t = static_cast<float>(f) / (data->numFrames - 1);
            int base = f * N;
            std::fill(data->frames.begin()+base, data->frames.begin()+base+N, 0.0f);
            for (int h = 0; h < numBins; ++h)
            {
                float mag = magA[static_cast<size_t>(h)]*(1-t) + magB[static_cast<size_t>(h)]*t;
                float ph = phA[static_cast<size_t>(h)]*(1-t) + phB[static_cast<size_t>(h)]*t;
                for (int s = 0; s < N; ++s)
                    data->frames[static_cast<size_t>(base+s)] += mag * std::cos(6.2831853f*(h+1)*s/N - ph) / (N/2);
            }
        }
        data->buildMipMaps(); if(onChange) onChange(); repaint();
    }

    // Linear crossfade: simple time-domain interpolation between first and last frame
    void linearMorphAll() {
        if (data->numFrames < 3) return;
        int N = WavetableData::kFrameSize;
        const float* first = data->getFrame (0);
        const float* last = data->getFrame (data->numFrames - 1);
        if (!first || !last) return;
        for (int f = 1; f < data->numFrames - 1; ++f)
        {
            float t = static_cast<float>(f) / (data->numFrames - 1);
            int base = f * N;
            for (int s = 0; s < N; ++s)
                data->frames[static_cast<size_t>(base + s)] = first[s] * (1.0f - t) + last[s] * t;
        }
        data->buildMipMaps(); if (onChange) onChange(); repaint();
    }

    // Harmonic fade: interpolate individual harmonics (additive synthesis)
    void harmonicMorphAll() {
        if (data->numFrames < 3) return;
        int N = WavetableData::kFrameSize;
        int maxH = std::min (64, N / 2);
        const float* first = data->getFrame (0);
        const float* last = data->getFrame (data->numFrames - 1);
        if (!first || !last) return;
        // Extract harmonic amplitudes via DFT
        std::vector<float> ampA(static_cast<size_t>(maxH)), ampB(static_cast<size_t>(maxH));
        for (int h = 0; h < maxH; ++h)
        {
            float rA=0,iA=0,rB=0,iB=0;
            for (int s = 0; s < N; ++s)
            {
                float a = 6.2831853f * (h+1) * s / N;
                rA += first[s]*std::cos(a); iA += first[s]*std::sin(a);
                rB += last[s]*std::cos(a);  iB += last[s]*std::sin(a);
            }
            ampA[static_cast<size_t>(h)] = std::sqrt(rA*rA+iA*iA) / (N/2);
            ampB[static_cast<size_t>(h)] = std::sqrt(rB*rB+iB*iB) / (N/2);
        }
        // Build frames with interpolated harmonic amplitudes
        for (int f = 1; f < data->numFrames - 1; ++f)
        {
            float t = static_cast<float>(f) / (data->numFrames - 1);
            int base = f * N;
            std::fill(data->frames.begin()+base, data->frames.begin()+base+N, 0.0f);
            for (int h = 0; h < maxH; ++h)
            {
                float amp = ampA[static_cast<size_t>(h)] * (1-t) + ampB[static_cast<size_t>(h)] * t;
                for (int s = 0; s < N; ++s)
                    data->frames[static_cast<size_t>(base+s)] += amp * std::sin(6.2831853f*(h+1)*s/N);
            }
        }
        data->buildMipMaps(); if (onChange) onChange(); repaint();
    }

    // Random fill: randomize middle frames with filtered noise
    void randomFillMiddle() {
        if (data->numFrames < 3) return;
        int N = WavetableData::kFrameSize;
        for (int f = 1; f < data->numFrames - 1; ++f)
        {
            int base = f * N;
            // Generate random waveform
            for (int s = 0; s < N; ++s)
                data->frames[static_cast<size_t>(base+s)] = (float)std::rand()/RAND_MAX*2.0f-1.0f;
            // Simple lowpass smooth (3 passes)
            for (int p = 0; p < 3; ++p)
                for (int s = 1; s < N-1; ++s)
                    data->frames[static_cast<size_t>(base+s)] =
                        data->frames[static_cast<size_t>(base+s-1)] * 0.25f +
                        data->frames[static_cast<size_t>(base+s)] * 0.5f +
                        data->frames[static_cast<size_t>(base+s+1)] * 0.25f;
        }
        data->buildMipMaps(); if (onChange) onChange(); repaint();
    }
};
