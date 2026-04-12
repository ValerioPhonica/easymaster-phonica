#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Audio/FX/ArpEngine.h"
#include "../../Audio/FX/LFOEngine.h"

// ═══════════════════════════════════════════════════════════════════
// ArpEditor — Step arpeggiator editor
//
// [ON] [VEL] [GATE] [PRM:Cut] [PRM2:Pan]  1/8 1oc UP 8 RTRIG ±50% ±25%
//
// Right-click PRM/PRM2 → full target popup (all 93 LFO targets)
// Left-click PRM/PRM2 → switch to param bar view
// ±% buttons → cycle modulation depth for each lane
// ═══════════════════════════════════════════════════════════════════

class ArpEditor : public juce::Component
{
public:
    ArpData* data = nullptr;
    std::function<void()> onChanged;
    void setData (ArpData& d) { data = &d; repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (data == nullptr) return;
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff14161c));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colour (0xff404555));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
        auto topRow = bounds.removeFromTop (32).reduced (4, 2);
        drawTopControls (g, topRow);
        drawBars (g, bounds.reduced (6, 4));
    }

    void mouseDown (const juce::MouseEvent& e) override { depthDragLane = 0; handleMouse (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { handleMouse (e, false); }
    void mouseUp (const juce::MouseEvent&) override { depthDragLane = 0; }
    int currentLayer = 0;

private:
    juce::Rectangle<float> onOffRect, velTabRect, gateTabRect, paramTabRect, param2TabRect;
    juce::Rectangle<float> divRect, octRect, dirRect, stepsRect, retrigRect, depth1Rect, depth2Rect;
    int depthDragLane = 0;      // 0=none, 1=depth1, 2=depth2
    int depthDragStart = 0;     // mouseY at drag start
    float depthDragInitVal = 0.0f; // depth value at drag start

    juce::Colour getLayerColour() const
    {
        const juce::Colour cols[] = { juce::Colour(0xffe8a030), juce::Colour(0xff40d8e8),
                                       juce::Colour(0xffc050e0), juce::Colour(0xff50e070) };
        return cols[std::clamp (currentLayer, 0, 3)];
    }

    juce::Rectangle<float> getBarArea() const
    {
        auto b = getLocalBounds().toFloat(); b.removeFromTop (32); return b.reduced (6, 4);
    }

    // ── Full target popup (all 93 synth targets, organized) ──
    void buildTargetMenu (juce::PopupMenu& menu, int cur)
    {
        // ID = target + 2 (0=nothing, 1=OFF)
        menu.addItem (1, "OFF", true, cur < 0);
        auto add = [&](const char* hdr, std::initializer_list<int> targets) {
            menu.addSeparator();
            menu.addSectionHeader (hdr);
            for (int t : targets)
                menu.addItem (t + 2, LFOEngine::getSynthTargetName (t), true, cur == t);
        };
        add ("PITCH / PAN",    {0, 4});
        add ("FILTER",         {1, 2, 14, 56, 57, 58, 59});
        add ("AMP",            {3, 15, 16, 17, 39});
        add ("OSC",            {9, 10, 11, 12, 13, 55, 54});
        add ("CHARACTER / FM", {18, 19, 60, 61});
        add ("FM 4-OP",        {20, 21, 22, 23, 24, 25, 26, 27});
        add ("ELEMENTS",       {28, 29, 30, 31, 32, 33});
        add ("PLAITS",         {34, 35, 36, 37, 38});
        add ("SAMPLER",        {47,48,49,50,51,52,53,89,90});
        add ("WAVETABLE",      {70, 71, 72, 73, 74, 75});
        add ("GRANULAR",       {76,77,78,79,80,81,82,83,84,85,86,87,88});
        add ("FX",             {5,6,7,8,40,41,42,43,44,45,46,91,92});
        add ("X-MOD",          {62, 63, 64, 65, 66, 67});
        add ("MSEG",           {68, 69});
    }

    void drawTopControls (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
        float x = area.getX(), y = area.getY(), h = area.getHeight();

        // ON/OFF
        onOffRect = { x, y, 28, h };
        g.setColour (data->enabled ? juce::Colour (0xffe8a030) : juce::Colour (0xff3a3d48));
        g.fillRoundedRectangle (onOffRect, 3.0f);
        g.setColour (data->enabled ? juce::Colours::black : juce::Colour (0xaaffffff));
        g.drawText (data->enabled ? "ON" : "OFF", onOffRect.toNearestInt(), juce::Justification::centred);
        x += 30;

        // Tabs
        auto tab = [&](juce::Rectangle<float>& r, int layer, juce::Colour c, const juce::String& lbl, float w) {
            r = { x, y, w, h };
            bool on = (currentLayer == layer);
            g.setColour (on ? c : juce::Colour (0xff2a2d38));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (on ? juce::Colours::black : juce::Colour (0xaaffffff));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
            g.drawText (lbl, r.toNearestInt(), juce::Justification::centred);
            x += w + 2;
        };

        tab (velTabRect, 0, juce::Colour (0xffe8a030), "VEL", 30);
        tab (gateTabRect, 1, juce::Colour (0xff40d8e8), "GATE", 34);

        juce::String p1 = data->assignTarget >= 0
            ? juce::String (LFOEngine::getSynthTargetName (data->assignTarget)) : "PRM";
        tab (paramTabRect, 2, juce::Colour (0xffc050e0), p1, 44);

        juce::String p2 = data->assign2Target >= 0
            ? juce::String (LFOEngine::getSynthTargetName (data->assign2Target)) : "PRM2";
        tab (param2TabRect, 3, juce::Colour (0xff50e070), p2, 44);

        x += 4;
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));

        auto btn = [&](juce::Rectangle<float>& r, const juce::String& t, float w, juce::Colour bg = juce::Colour(0xff2a2d38)) {
            r = { x, y, w, h };
            g.setColour (bg);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (juce::Colour (0xffeeeeff));
            g.drawText (t, r.toNearestInt(), juce::Justification::centred);
            x += w + 2;
        };

        btn (divRect, ArpData::getDivisionName (data->division), 34);
        btn (octRect, juce::String (data->octaves) + "oc", 28);
        btn (dirRect, ArpData::getDirectionName (data->direction), 34);
        btn (stepsRect, juce::String (data->getEffectiveLen()), 26);

        // RETRIG
        retrigRect = { x, y, 40, h };
        g.setColour (data->keyRetrig ? juce::Colour (0xff40d8e8) : juce::Colour (0xff3a3d48));
        g.fillRoundedRectangle (retrigRect, 3.0f);
        g.setColour (data->keyRetrig ? juce::Colours::black : juce::Colour (0xaaffffff));
        g.drawText ("RTRIG", retrigRect.toNearestInt(), juce::Justification::centred);
        x += 42;

        // Depth 1
        auto depthBtn = [&](juce::Rectangle<float>& r, float depth, juce::Colour col, bool active) {
            r = { x, y, 38, h };
            g.setColour (active ? col.withAlpha (0.4f) : juce::Colour (0xff2a2d38));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (active ? col : juce::Colour (0xff808090));
            int pct = static_cast<int>(depth * 100);
            g.drawText (pct == 0 ? "0%" : (pct > 0 ? "+" : "") + juce::String(pct) + "%",
                        r.toNearestInt(), juce::Justification::centred);
            x += 40;
        };

        depthBtn (depth1Rect, data->assignDepth, juce::Colour (0xffc050e0),
                  data->assignTarget >= 0 && std::abs (data->assignDepth) > 0.01f);
        depthBtn (depth2Rect, data->assign2Depth, juce::Colour (0xff50e070),
                  data->assign2Target >= 0 && std::abs (data->assign2Depth) > 0.01f);
    }

    void drawBars (juce::Graphics& g, juce::Rectangle<float> area)
    {
        int n = data->getEffectiveLen();
        if (n < 1) return;
        float barW = area.getWidth() / static_cast<float>(n);
        float maxH = area.getHeight();
        auto col = getLayerColour();

        for (int i = 0; i < n; ++i)
        {
            auto& s = data->steps[static_cast<size_t>(i)];
            float v = 0;
            switch (currentLayer) {
                case 0: v = s.velocity / 127.0f; break;
                case 1: v = s.gate / 200.0f; break;
                case 2: v = s.param; break;
                case 3: v = s.param2; break;
            }
            v = std::clamp (v, 0.0f, 1.0f);
            float bh = v * maxH, bx = area.getX() + i * barW;
            g.setColour (juce::Colour (0xff1e2028));
            g.fillRect (bx + 1, area.getY(), barW - 2, maxH);
            g.setColour (col.withAlpha (0.8f));
            g.fillRect (bx + 1, area.getY() + maxH - bh, barW - 2, bh);
            if (bh > 2) { g.setColour (col); g.fillRect (bx + 1, area.getY() + maxH - bh, barW - 2, 2.0f); }
            if (barW > 12) {
                g.setColour (juce::Colour (0x55ffffff));
                g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
                g.drawText (juce::String(i+1), (int)bx, (int)(area.getY()+maxH-10), (int)barW, 10, juce::Justification::centred);
            }
        }
    }

    void handleMouse (const juce::MouseEvent& e, bool isDown)
    {
        if (data == nullptr) return;
        auto pos = e.getPosition().toFloat();

        if (isDown && pos.y < 32)
        {
            if (onOffRect.contains(pos)) { data->enabled = !data->enabled; repaint(); notify(); return; }
            if (velTabRect.contains(pos))  { currentLayer = 0; repaint(); return; }
            if (gateTabRect.contains(pos)) { currentLayer = 1; repaint(); return; }
            if (paramTabRect.contains(pos)) {
                if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) showFullTargetPopup(1);
                else currentLayer = 2;
                repaint(); return;
            }
            if (param2TabRect.contains(pos)) {
                if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) showFullTargetPopup(2);
                else currentLayer = 3;
                repaint(); return;
            }
            if (divRect.contains(pos))   { data->division = (data->division+1)%8; repaint(); notify(); return; }
            if (octRect.contains(pos))   { data->octaves = (data->octaves%4)+1; repaint(); notify(); return; }
            if (dirRect.contains(pos))   { data->direction = (data->direction+1)%8; repaint(); notify(); return; }
            if (stepsRect.contains(pos)) {
                static const int c[] = {4,8,12,16,24,32};
                int cur=data->numSteps, nx=c[0];
                for(int i=0;i<5;++i) if(cur>=c[i]&&c[i+1]>cur){nx=c[i+1];break;}
                if(cur>=32) nx=4;
                data->numSteps=nx; repaint(); notify(); return;
            }
            if (retrigRect.contains(pos)) { data->keyRetrig=!data->keyRetrig; repaint(); notify(); return; }
            // Depth: start drag for continuous adjustment
            if (depth1Rect.contains(pos)) { depthDragLane = 1; depthDragStart = e.getPosition().y; depthDragInitVal = data->assignDepth; return; }
            if (depth2Rect.contains(pos)) { depthDragLane = 2; depthDragStart = e.getPosition().y; depthDragInitVal = data->assign2Depth; return; }
            return;
        }

        // Depth drag: continuous adjustment (mouse Y controls depth -100% to +100%)
        if (depthDragLane > 0)
        {
            float dy = static_cast<float>(depthDragStart - e.getPosition().y);  // up = positive
            float delta = dy * 0.01f; // 100px = full range
            float newVal = std::clamp (depthDragInitVal + delta, -1.0f, 1.0f);
            // Round to nearest 1%
            newVal = std::round (newVal * 100.0f) / 100.0f;
            if (depthDragLane == 1) data->assignDepth = newVal;
            else data->assign2Depth = newVal;
            repaint(); notify();
            return;
        }

        auto barArea = getBarArea();
        if (!barArea.contains(pos)) return;
        int n = data->getEffectiveLen(); if(n<1) return;
        float barW = barArea.getWidth()/static_cast<float>(n);
        int si = std::clamp((int)((pos.x-barArea.getX())/barW), 0, n-1);
        float v = std::clamp(1.0f-(pos.y-barArea.getY())/barArea.getHeight(), 0.0f, 1.0f);
        auto& s = data->steps[static_cast<size_t>(si)];
        switch(currentLayer) {
            case 0: s.velocity = static_cast<uint8_t>(std::clamp((int)(v*127),1,127)); break;
            case 1: s.gate = static_cast<uint8_t>(std::clamp((int)(v*200),5,200)); break;
            case 2: s.param = v; break;
            case 3: s.param2 = v; break;
        }
        repaint(); notify();
    }

    void notify() { if (onChanged) onChanged(); }
    void cycleDepth(float& d) {
        static const float a[]={0,0.25f,0.5f,1.0f,-0.25f,-0.5f,-1.0f};
        int nx=0; for(int i=0;i<6;++i) if(std::abs(d-a[i])<0.02f){nx=i+1;break;}
        d=a[nx%7];
    }

    void showFullTargetPopup(int lane)
    {
        int cur = (lane==1) ? data->assignTarget : data->assign2Target;
        juce::PopupMenu menu;
        buildTargetMenu(menu, cur);
        menu.showMenuAsync(juce::PopupMenu::Options(), [this,lane](int r) {
            if(r>0) {
                int tgt = (r==1) ? -1 : r-2; // id 1=OFF(-1), id 2=target 0, id 3=target 1...
                if(lane==1) data->assignTarget=tgt; else data->assign2Target=tgt;
                repaint(); notify();
            }
        });
    }
};
