#pragma once
#include <cmath>
#include <algorithm>
#include <array>
#include <atomic>

// ═══════════════════════════════════════════════════════════════════
// MSEG — Multi-Stage Envelope Generator (Serum-style)
//
// - Up to 32 breakpoints (time, value, curve)
// - Loop region (sustain loop between two points)
// - One-shot / Loop / Ping-pong modes
// - Targets same parameters as LFO
// - Depth control (-1 to +1 bipolar)
// ═══════════════════════════════════════════════════════════════════

struct MSEGPoint
{
    float time  = 0.0f;   // 0-1 normalized position in total envelope
    float value = 0.5f;   // 0-1 output level (0.5 = center/zero for bipolar)
    int   curve = 0;      // shape type (0=linear, 1-24=Massive-style shapes)
    float tension = 0.0f; // -1 to +1: continuous bend within the shape (drag to adjust)
    float auxModY = 0.0f; // -1 to +1: how much aux LFO modulates this point's Y (value)
    float auxModX = 0.0f; // -1 to +1: how much aux LFO modulates this point's X (time)
    // Per-source cross-MSEG modulation: [0] = first other MSEG, [1] = second other MSEG
    // When editing MSEG 1: [0]=from MSEG 2, [1]=from MSEG 3
    // When editing MSEG 2: [0]=from MSEG 1, [1]=from MSEG 3
    // When editing MSEG 3: [0]=from MSEG 1, [1]=from MSEG 2
    float crossModY[2] = {0.0f, 0.0f};
    float crossModX[2] = {0.0f, 0.0f};
};

// Modulation routing slot (for multi-target Serum-style modulation)
struct ModRoute
{
    int   target = -1;    // -1 = unused slot
    float depth  = 0.0f;  // -1 to +1 bipolar
};

struct MSEGData
{
    static constexpr int kMaxPoints = 32;

    std::array<MSEGPoint, kMaxPoints> points;
    int numPoints = 2;         // minimum 2 (start + end)

    int    loopStart  = -1;    // -1 = no loop
    int    loopEnd    = -1;    // -1 = no loop
    int    loopMode   = 0;     // 0=one-shot, 1=loop, 2=ping-pong, 3=random

    int    target     = 1;     // same target IDs as LFO (1=CUT, etc.)
    float  depth      = 0.0f;  // -1 to +1 bipolar amount
    float  totalTime  = 1.0f;  // total envelope duration in seconds (0.01 - 30.0)
    bool   tempoSync  = false;
    float  syncBeats  = 1.0f;  // number of beats for total duration when synced
    int    syncDiv    = 3;     // 0=1/32, 1=1/16, 2=1/8, 3=1/4, 4=1/2, 5=1bar, 6=2bar, 7=4bar, 8=8bar, 9=16bar, 10=32bar
    int    gridX      = 8;     // horizontal grid divisions (1-32)
    int    gridY      = 4;     // vertical grid divisions (1-16)
    bool   transportSync = true;  // true = reset phase on DAW transport start

    // ── Aux LFO (modulates individual point positions) ──
    float  auxRate    = 2.0f;  // Hz when free (0.05 - 20)
    int    auxShape   = 0;     // 0=sin, 1=tri, 2=saw, 3=sqr, 4=s&h
    bool   auxSync    = false; // false=FREE (Hz), true=SYNC (beats)
    int    auxSyncDiv = 3;     // 0=1/32..10=32bar (same as main syncDiv)

    // ── Fade-in ──
    float  fadeIn     = 0.0f;    // fade-in time (seconds in FREE, sync div index in SYNC)
    bool   fadeInSync = false;   // true = fade-in time synced to BPM

    // Extra modulation routes (Serum-style multi-target)
    std::array<ModRoute, 16> extraRoutes;

    MSEGData()
    {
        // Default: simple attack-sustain-release shape
        points[0] = { 0.0f, 0.0f, 0 };  // start at zero
        points[1] = { 1.0f, 0.0f, 0 };  // end at zero
        numPoints = 2;
    }

    void setDefault4Point()
    {
        points[0] = { 0.0f,  0.0f, 0 };  // start
        points[1] = { 0.15f, 1.0f, 2 };  // attack peak (exp curve)
        points[2] = { 0.5f,  0.6f, 1 };  // sustain
        points[3] = { 1.0f,  0.0f, 1 };  // release
        numPoints = 4;
    }
};

class MSEGEngine
{
public:
    void prepare (float sr) { sampleRate = sr; }

    void trigger()
    {
        // Note retrigger: always reset (call site checks msegRetrig flag)
        phase = 0.0;
        direction = 1;
        finished = false;
        lastRndStep = -1;
        msegFadeElapsed = 0.0; // reset fade-in
        // RANDOM mode: pick a random grid slot immediately
        if (data != nullptr && data->loopMode == 3)
        {
            int gridRes = std::max (2, data->gridX);
            randomSlot = nextRandom (gridRes);
        }
    }

    void transportReset()
    {
        // Transport start: reset if transportSync enabled
        if (data != nullptr && data->transportSync)
        {
            phase = 0.0;
            direction = 1;
            finished = false;
            lastRndStep = -1; // force fresh random pick in RND mode
        }
    }

    void release()
    {
        // Jump to after loop end if we're in a loop
        if (data != nullptr && data->loopEnd > 0 && data->loopEnd < data->numPoints - 1)
        {
            int releasePoint = data->loopEnd;
            phase = static_cast<double>(data->points[releasePoint].time);
        }
    }

    // Tick one block — advances phase, returns current value (0-1)
    float tick (int numSamples, float bpm)
    {
        if (data == nullptr || data->numPoints < 2)
            return 0.0f;

        // ── Advance aux LFO ──
        {
            double auxRate = static_cast<double>(data->auxRate);
            if (data->auxSync && bpm > 20.0f)
            {
                // Synced: rate from beat division
                static const float divBeats[] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f};
                float beats = divBeats[std::clamp (data->auxSyncDiv, 0, 10)];
                auxRate = static_cast<double>(bpm) / (60.0 * beats);
            }
            double auxInc = auxRate * numSamples / sampleRate;
            auxPhase += auxInc;
            auxPhase -= std::floor (auxPhase);
            float p = static_cast<float>(auxPhase);
            switch (data->auxShape)
            {
                case 0: auxValue = std::sin (p * 6.2831853f); break; // sine
                case 1: auxValue = 4.0f * std::abs (p - 0.5f) - 1.0f; break; // tri
                case 2: auxValue = 2.0f * p - 1.0f; break; // saw
                case 3: auxValue = p < 0.5f ? 1.0f : -1.0f; break; // sqr
                case 4: // S&H: hold value, pick new at zero crossing
                    if (p < static_cast<float>(auxInc) + 0.01f)
                    {
                        auxRngState = auxRngState * 1664525u + 1013904223u;
                        auxValue = static_cast<float>(auxRngState >> 8) / 8388608.0f - 1.0f;
                    }
                    break;
                default: auxValue = std::sin (p * 6.2831853f); break;
            }
        }

        // CRITICAL: reset finished flag when NOT in one-shot mode
        // Modes 1=LOOP, 2=P.P, 3=RANDOM all cycle
        if (data->loopMode != 0 && finished)
        {
            finished = false;
            if (phase >= 1.0) phase = 0.0;
        }

        if (finished)
            return currentValue;

        float totalDur = data->totalTime;
        if (data->tempoSync && bpm > 0.0f)
        {
            // Beat divisions: 1/32, 1/16, 1/8, 1/4, 1/2, 1bar, 2bar, 4bar, 8bar, 16bar, 32bar
            static const float divBeats[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f };
            int divIdx = std::clamp (data->syncDiv, 0, 10);
            totalDur = divBeats[divIdx] * 60.0f / bpm;
        }
        // Apply external rate modulation (from LFO → MSEG)
        if (std::abs (extRateMod) > 0.001f)
            totalDur *= std::pow (2.0f, -extRateMod); // positive mod = faster
        totalDur = std::max (0.001f, totalDur);

        double phaseInc = static_cast<double>(numSamples) / (sampleRate * totalDur);
        phase += phaseInc * direction;

        // Clamp and handle loop
        float phaseF = static_cast<float>(phase);

        bool hasLoopMarkers = data->loopStart >= 0 && data->loopEnd > data->loopStart
            && data->loopEnd < data->numPoints;

        if (data->loopMode == 1) // LOOP
        {
            if (hasLoopMarkers)
            {
                float loopStartT = data->points[data->loopStart].time;
                float loopEndT   = data->points[data->loopEnd].time;
                float loopLen = loopEndT - loopStartT;
                if (loopLen > 0.0001f && phaseF >= loopEndT)
                    phase = static_cast<double>(loopStartT) + std::fmod (phase - static_cast<double>(loopStartT), static_cast<double>(loopLen));
            }
            else // No markers → loop entire envelope 0→1
            {
                if (phaseF >= 1.0f)
                    phase = std::fmod (phase, 1.0);
            }
        }
        else if (data->loopMode == 2) // PING-PONG
        {
            if (hasLoopMarkers)
            {
                float loopStartT = data->points[data->loopStart].time;
                float loopEndT   = data->points[data->loopEnd].time;
                float loopLen = loopEndT - loopStartT;
                if (loopLen > 0.0001f)
                {
                    // Reflect phase at boundaries (handles overshooting)
                    int bounces = 0;
                    while (bounces < 4)
                    {
                        float pf = static_cast<float>(phase);
                        if (direction > 0 && pf >= loopEndT)
                            { phase = 2.0 * static_cast<double>(loopEndT) - phase; direction = -1; }
                        else if (direction < 0 && pf <= loopStartT)
                            { phase = 2.0 * static_cast<double>(loopStartT) - phase; direction = 1; }
                        else break;
                        bounces++;
                    }
                }
            }
            else
            {
                // Reflect at 0-1 boundaries
                int bounces = 0;
                while (bounces < 4)
                {
                    if (direction > 0 && phase >= 1.0)
                        { phase = 2.0 - phase; direction = -1; }
                    else if (direction < 0 && phase <= 0.0)
                        { phase = -phase; direction = 1; }
                    else break;
                    bounces++;
                }
            }
        }
        else if (data->loopMode == 3) // RANDOM
        {
            // Phase advances normally → used as a clock to trigger random slot changes
            int gridRes = std::max (2, data->gridX);
            int currentStep = static_cast<int>(phaseF * static_cast<float>(gridRes));
            currentStep = std::clamp (currentStep, 0, gridRes - 1);

            // When we cross a step boundary, pick a NEW random slot
            if (currentStep != lastRndStep)
            {
                lastRndStep = currentStep;
                randomSlot = nextRandom (gridRes);
            }

            // Loop the phase (so the clock keeps ticking)
            if (phaseF >= 1.0f)
            {
                phase = std::fmod (phase, 1.0);
                lastRndStep = -1; // force new pick on wrap
            }

            // Evaluate MSEG at the center of the random slot
            float rndPhase = (static_cast<float>(randomSlot) + 0.5f) / static_cast<float>(gridRes);
            currentValue = evaluate (std::clamp (rndPhase, 0.0f, 1.0f));
            currentValue = applyFadeIn (currentValue, numSamples, bpm);
            return currentValue;
        }
        else // ONE-SHOT
        {
            if (phaseF >= 1.0f)
            {
                phase = 1.0;
                finished = true;
            }
        }

        currentValue = evaluate (static_cast<float>(std::clamp (phase, 0.0, 1.0)));
        currentValue = applyFadeIn (currentValue, numSamples, bpm);
        return currentValue;
    }

    float getValue() const { return currentValue; }
    float getPhase() const { return static_cast<float>(std::clamp (phase, 0.0, 1.0)); }
    float getAuxValue() const { return auxValue; }
    bool  isFinished() const { return finished; }

    void setData (MSEGData* d) { data = d; }

    // External modulation (from LFO → MSEG cross-mod)
    float extRateMod  = 0.0f;   // multiplies totalTime: pow(2, extRateMod)
    float extDepthMod = 0.0f;   // added to depth

    // Cross-MSEG modulation values (set by PluginProcessor from other MSEGs)
    float crossMsegValue1 = 0.0f; // output of other MSEG 1 (-1..+1)
    float crossMsegValue2 = 0.0f; // output of other MSEG 2 (-1..+1)

private:
    // Evaluate envelope at normalized time t (0-1), with aux LFO + cross-MSEG modulating points
    float evaluate (float t) const
    {
        if (data == nullptr || data->numPoints < 2) return 0.0f;

        int np = data->numPoints;

        // Build modulated point positions
        float modTime[32], modVal[32];
        for (int i = 0; i < np; ++i)
        {
            // Aux LFO modulation
            float yMod = data->points[i].auxModY * auxValue * 0.5f;
            float xMod = data->points[i].auxModX * auxValue * 0.15f;
            // Cross-MSEG modulation: per-source, independent amounts
            yMod += data->points[i].crossModY[0] * crossMsegValue1 * 0.5f;
            yMod += data->points[i].crossModY[1] * crossMsegValue2 * 0.5f;
            xMod += data->points[i].crossModX[0] * crossMsegValue1 * 0.15f;
            xMod += data->points[i].crossModX[1] * crossMsegValue2 * 0.15f;

            modTime[i] = data->points[i].time + xMod;
            modVal[i]  = std::clamp (data->points[i].value + yMod, 0.0f, 1.0f);
        }
        modTime[0] = std::max (0.0f, modTime[0]);
        modTime[np - 1] = std::min (1.0f, modTime[np - 1]);
        for (int i = 1; i < np; ++i)
            modTime[i] = std::max (modTime[i], modTime[i - 1] + 0.0001f);

        for (int i = 0; i < np - 1; ++i)
        {
            if (t >= modTime[i] && t <= modTime[i + 1])
            {
                float segLen = modTime[i + 1] - modTime[i];
                if (segLen < 0.0001f) return modVal[i];
                float frac = (t - modTime[i]) / segLen;
                return interpolate (modVal[i], modVal[i + 1], frac,
                                    data->points[i].curve, data->points[i].tension);
            }
        }
        return modVal[np - 1];
    }

    // ── Public interpolation for editor rendering ──
public:
    // Interpolate between v0 and v1 at position t (0-1)
    static float interpolate (float v0, float v1, float t, int curveType, float tension = 0.0f)
    {
        // Apply tension as a power curve warp to t (affects ALL shapes)
        // tension > 0 = concave (fast start), tension < 0 = convex (slow start)
        if (std::abs (tension) > 0.01f)
        {
            float power = (tension > 0) ? 1.0f / (1.0f + tension * 3.0f)
                                         : 1.0f + std::abs (tension) * 3.0f;
            t = std::pow (std::clamp (t, 0.0f, 1.0f), power);
        }

        float d = v1 - v0;
        switch (curveType)
        {
            case 0: // Linear
                return v0 + d * t;

            case 1: // Sinoid (S-curve, smooth Hermite)
                return v0 + d * (t * t * (3.0f - 2.0f * t));

            case 2: // Switch (instant jump at midpoint)
                return t < 0.5f ? v0 : v1;

            case 3:  return v0 + d * std::floor (t * 4.0f) / 4.0f;   // Stairs 4
            case 4:  return v0 + d * std::floor (t * 8.0f) / 8.0f;   // Stairs 8
            case 5:  return v0 + d * std::floor (t * 16.0f) / 16.0f;  // Stairs 16

            case 6: // Triangle (peak at center)
                return v0 + d * (t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f);
            case 7: // Triangle 1.5 (peak at 1/3)
                return v0 + d * (t < 0.333f ? t * 3.0f : (1.0f - t) * 1.5f);
            case 8: // Triangle 2.5 (peak at 2/5)
                return v0 + d * (t < 0.4f ? t * 2.5f : (1.0f - t) * 1.667f);
            case 9: // Triangle 3.5 (peak at 3/7)
                return v0 + d * (t < 0.429f ? t * 2.333f : (1.0f - t) * 1.75f);

            case 10: // Sine 1 (half sine — smooth arch)
                return v0 + d * std::sin (t * 3.14159265f);
            case 11: // Sine 1.5
                return v0 + d * std::sin (t * 3.14159265f * 1.5f) / std::sin (3.14159265f * 1.5f);
            case 12: // Sine 2.5 (2.5 cycles → complex undulation)
                return v0 + d * (0.5f + 0.5f * std::sin (t * 3.14159265f * 5.0f - 1.5708f));
            case 13: // Sine 3.5
                return v0 + d * (0.5f + 0.5f * std::sin (t * 3.14159265f * 7.0f - 1.5708f));

            case 14: // Expo x2 (quadratic)
                return v0 + d * t * t;
            case 15: // Expo x4 (quartic)
                return v0 + d * t * t * t * t;
            case 16: // Peak Expo x2 (inverted quadratic — fast attack, slow tail)
                return v0 + d * (1.0f - (1.0f - t) * (1.0f - t));
            case 17: // Peak Expo x4
                { float r = 1.0f - t; return v0 + d * (1.0f - r * r * r * r); }

            case 18: // Curve 1 — asymmetric: fast rise, slow overshoot
            {
                float s = t * t * (3.0f - 2.0f * t);
                return v0 + d * (s + 0.2f * std::sin (t * 6.2832f) * (1.0f - t));
            }
            case 19: // Curve 2 — double bump
                return v0 + d * (0.5f - 0.5f * std::cos (t * 6.2832f));
            case 20: // Curve 3 — sharp attack, bounce
                return v0 + d * std::clamp (1.2f * t * t * (3.0f - 2.0f * t) + 0.15f * std::sin (t * 9.42f) * (1.0f - t), 0.0f, 1.0f);
            case 21: // Curve 4 — logarithmic feel
                return v0 + d * std::log1p (t * 9.0f) / std::log (10.0f);
            case 22: // Curve 5 — smooth asymmetric S with early inflection
            {
                float s = t < 0.3f ? (t / 0.3f) * (t / 0.3f) * 0.5f
                                    : 0.5f + (1.0f - std::pow (1.0f - (t - 0.3f) / 0.7f, 2.0f)) * 0.5f;
                return v0 + d * s;
            }
            case 23: // Curve 6 — reverse S (late attack)
            {
                float s = t < 0.7f ? std::pow (t / 0.7f, 2.0f) * 0.5f
                                    : 0.5f + (1.0f - (1.0f - (t - 0.7f) / 0.3f) * (1.0f - (t - 0.7f) / 0.3f)) * 0.5f;
                return v0 + d * s;
            }

            case 24: // Extreme 1 — sharp spike + ring
                return v0 + d * std::clamp (std::exp (-8.0f * (1.0f - t)) + 0.3f * std::sin (t * 18.85f) * std::exp (-3.0f * t), 0.0f, 1.0f);
            case 25: // Extreme 2 — chaotic fast oscillation
                return v0 + d * std::clamp (t + 0.4f * std::sin (t * 25.13f) * (1.0f - t) * t, 0.0f, 1.0f);

            case 26: // Smooth Random (deterministic layered sines)
            {
                float noise = 0.5f + 0.25f * std::sin (t * 13.37f)
                                   + 0.15f * std::sin (t * 31.41f + 1.7f)
                                   + 0.10f * std::sin (t * 67.89f + 3.1f);
                return v0 + d * std::clamp (noise, 0.0f, 1.0f);
            }

            case 27: return t < 0.9f ? v0 : v1; // Pulse End
            case 28: return t < 0.1f ? v0 : v1; // Pulse Start

            default: return v0 + d * t;
        }
    }

private:
    MSEGData* data = nullptr;
    float sampleRate = 44100.0f;
    double phase = 0.0;
    int direction = 1;
    float currentValue = 0.0f;
    bool finished = false;

    // RANDOM mode state
    int randomSlot = 0;
    int lastRndStep = -1;
    uint32_t rngState = 12345;

    // Aux LFO state (modulates individual point positions)
    double auxPhase = 0.0;
    float  auxValue = 0.0f;    // current aux LFO output (-1..+1)
    uint32_t auxRngState = 54321; // for S&H shape

    // Fade-in state
    double msegFadeElapsed = 99.0; // time since trigger (starts high = fully faded in)

    float applyFadeIn (float val, int numSamples, float bpm)
    {
        if (data == nullptr || data->fadeIn <= 0.0f) return val;
        float blockSec = static_cast<float>(numSamples) / sampleRate;
        msegFadeElapsed += static_cast<double>(blockSec);
        float fadeTimeSec = data->fadeIn;
        if (data->fadeInSync && bpm > 20.0f)
        {
            static const float divBeats[] = {0.125f,0.25f,0.5f,1,2,4,8,16,32,64,128};
            int idx = std::clamp (static_cast<int>(data->fadeIn), 0, 10);
            fadeTimeSec = divBeats[idx] * 60.0f / bpm;
        }
        if (fadeTimeSec < 0.001f) return val;
        float fade = std::clamp (static_cast<float>(msegFadeElapsed) / fadeTimeSec, 0.0f, 1.0f);
        // Fade applies to the bipolar deviation from center (0.5)
        return 0.5f + (val - 0.5f) * fade;
    }

    // Fast LCG random: returns 0..max-1
    int nextRandom (int max)
    {
        rngState = rngState * 1664525u + 1013904223u;
        return static_cast<int>((rngState >> 8) % static_cast<uint32_t>(std::max (1, max)));
    }
};
