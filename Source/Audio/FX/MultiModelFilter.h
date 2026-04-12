#pragma once
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════════
// MultiModelFilter — 6 professional filter models in a single channel
//
// Extracted from AnalogVoice to be reused across ALL synth/sampler engines.
// Each instance = ONE audio channel. Use two for stereo.
//
// Models:
//   0 = CLEAN  — Cytomic TPT SVF (zero-delay feedback, unconditionally stable)
//   1 = ACID   — TB-303 diode ladder (4-pole, self-oscillating, resonance narrows)
//   2 = DIRTY  — Driven SVF (same as CLEAN but with tanh post-saturation)
//   3 = SEM    — Oberheim SEM 2-pole SVF (warm, round, tanh integrators)
//   4 = ARP    — ARP 2600 aggressive 4-pole ladder (hard clipping, buzzy)
//   5 = LIQUID — Ripples/Jupiter VCA ladder (transparent, clean self-osc)
//
// Usage:
//   MultiModelFilterCh filterL, filterR;
//   filterL.prepare (sampleRate);
//   float outL = filterL.process (inL, cutHz, reso, fModel, fType, fPoles, sr);
// ═══════════════════════════════════════════════════════════════════════

struct MultiModelFilterCh
{
    // ── State for all models (only the active one is used per sample) ──
    struct SVFState   { float ic1eq = 0, ic2eq = 0;
                        void reset() { ic1eq = ic2eq = 0; } };
    struct LadderState { float s1 = 0, s2 = 0, s3 = 0, s4 = 0, prevFb = 0;
                         void reset() { s1 = s2 = s3 = s4 = prevFb = 0; } };
    struct SEMState   { float lp = 0, bp = 0, hp = 0;
                        void reset() { lp = bp = hp = 0; } };
    struct ARPState   { float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
                        void reset() { s1 = s2 = s3 = s4 = 0; } };
    struct LiquidState { float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
                         void reset() { s1 = s2 = s3 = s4 = 0; } };

    SVFState    svf1, svf2;   // two SVFs for 24dB cascade (CLEAN/DIRTY)
    LadderState ladder;
    SEMState    sem;
    ARPState    arp;
    LiquidState liquid;

    void reset()
    {
        svf1.reset(); svf2.reset();
        ladder.reset(); sem.reset();
        arp.reset(); liquid.reset();
    }

    // Soft reset for retrigger — preserve state to avoid clicks
    void softReset()
    {
        // Scale down state instead of zeroing — avoids click on retrigger
        auto scale = [](float& v, float f) { v *= f; };
        scale (svf1.ic1eq, 0.3f); scale (svf1.ic2eq, 0.3f);
        scale (svf2.ic1eq, 0.3f); scale (svf2.ic2eq, 0.3f);
        // Ladder/SEM/ARP/Liquid: only soft reset
        ladder.s1 *= 0.5f; ladder.s2 *= 0.5f; ladder.s3 *= 0.5f; ladder.s4 *= 0.5f;
        sem.lp *= 0.5f; sem.bp *= 0.5f;
        arp.s1 *= 0.5f; arp.s2 *= 0.5f; arp.s3 *= 0.5f; arp.s4 *= 0.5f;
        liquid.s1 *= 0.5f; liquid.s2 *= 0.5f; liquid.s3 *= 0.5f; liquid.s4 *= 0.5f;
    }

    // ── Main process: one sample in → one sample out ──
    // cutHz:  filter cutoff in Hz (already modulated by envelope etc.)
    // reso:   0..1 resonance
    // fModel: 0=CLN, 1=ACD, 2=DRT, 3=SEM, 4=ARP, 5=LQD
    // fType:  0=LP, 1=HP, 2=BP, 3=Notch
    // fPoles: 6, 12, or 24 (dB/oct) — used by CLN, DRT, LQD
    // sr:     sample rate
    float process (float in, float cutHz, float reso, int fModel, int fType, int fPoles, float sr)
    {
        cutHz = std::clamp (cutHz, 16.0f, sr * 0.45f);

        switch (fModel)
        {
            case 1:  return runLadder303 (in, cutHz, reso, sr);
            case 3:  return runSEM (in, cutHz, reso, fType, sr);
            case 4:  return runARP2600 (in, cutHz, reso, sr);
            case 5:  return runLiquid (in, cutHz, reso, fType, fPoles, sr);
            case 0:  // CLEAN — fall through
            case 2:  // DIRTY — fall through
            default: return runCleanDirty (in, cutHz, reso, fModel, fType, fPoles, sr);
        }
    }

private:
    // ═══════════════════════════════════════════════
    // CLEAN / DIRTY — Cytomic TPT SVF
    // ═══════════════════════════════════════════════
    float runCleanDirty (float in, float cutHz, float reso, int fModel, int fType, int fPoles, float sr)
    {
        float Q = 0.5f + reso * 18.0f;
        float driveIn = (fModel == 2) ? 1.5f : 1.0f;
        float driven = in * driveIn;

        // First SVF pass (12dB/oct minimum)
        float out = runSVF (driven, cutHz, Q, fType, svf1, sr);

        if (fPoles >= 24)
        {
            out = runSVF (out, cutHz, Q * 0.6f, fType, svf2, sr);
        }
        else if (fPoles <= 6)
        {
            float s6 = runSVF (driven, cutHz * 2.0f, Q * 0.3f, fType, svf2, sr);
            out = s6 * 0.5f + out * 0.5f;
        }

        // DIRTY: tanh post-saturation
        if (fModel == 2)
            out = std::tanh (out * 1.2f);

        return out;
    }

    // ═══════════════════════════════════════════════
    // SVF — Cytomic TPT (zero-delay feedback)
    // ═══════════════════════════════════════════════
    static float runSVF (float in, float cutHz, float Q, int type, SVFState& s, float sr)
    {
        float g = std::tan (3.14159265f * std::clamp (cutHz, 16.0f, sr * 0.49f) / sr);
        float k = 1.0f / std::max (0.5f, Q);
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;

        float v3 = in - s.ic2eq;
        float v1 = a1 * s.ic1eq + a2 * v3;
        float v2 = s.ic2eq + a2 * s.ic1eq + a3 * v3;

        s.ic1eq = 2.0f * v1 - s.ic1eq;
        s.ic2eq = 2.0f * v2 - s.ic2eq;

        s.ic1eq = std::clamp (s.ic1eq, -8.0f, 8.0f);
        s.ic2eq = std::clamp (s.ic2eq, -8.0f, 8.0f);
        if (!std::isfinite (s.ic1eq)) s.ic1eq = 0;
        if (!std::isfinite (s.ic2eq)) s.ic2eq = 0;

        switch (type)
        {
            case 0:  return v2;                // LP
            case 1:  return in - k * v1 - v2;  // HP
            case 2:  return v1;                // BP
            case 3:  return in - k * v1;       // Notch
            default: return v2;
        }
    }

    // ═══════════════════════════════════════════════
    // TB-303 Diode Ladder — Stilson/Smith
    // ═══════════════════════════════════════════════
    float runLadder303 (float in, float cutHz, float reso, float sr)
    {
        float fc = std::clamp (cutHz / sr, 0.0001f, 0.45f);
        float f = std::clamp (2.0f * std::sin (3.14159265f * fc), 0.0001f, 0.99f);
        float fEff = f * (1.0f - reso * 0.15f);
        float fb = reso * 3.95f;
        float fbSig = std::tanh (ladder.s4 * 1.1f);
        float comp = 1.0f + reso * 1.2f;
        float u = in * comp - fb * fbSig;

        auto diode = [](float x) -> float {
            return (x >= 0.0f) ? x / (1.0f + 0.5f * x) : std::tanh (x * 1.2f);
        };

        ladder.s1 += fEff * (diode (u)          - ladder.s1);
        ladder.s2 += fEff * (diode (ladder.s1)   - ladder.s2);
        ladder.s3 += fEff * (diode (ladder.s2)   - ladder.s3);
        ladder.s4 += fEff * (diode (ladder.s3)   - ladder.s4);

        if (!std::isfinite (ladder.s4)) { ladder.reset(); return 0.0f; }
        return std::clamp (ladder.s4, -5.0f, 5.0f);
    }

    // ═══════════════════════════════════════════════
    // SEM — Oberheim 2-pole SVF
    // ═══════════════════════════════════════════════
    float runSEM (float in, float cutHz, float reso, int type, float sr)
    {
        float f = std::clamp (2.0f * std::sin (3.14159265f * std::clamp (cutHz, 16.0f, sr * 0.49f) / sr),
                              0.0001f, 0.99f);
        float q = std::max (0.05f, 1.0f - reso * 0.95f);
        float damp = q * 2.0f;

        sem.hp = in - sem.lp - damp * sem.bp;
        sem.bp += f * std::tanh (sem.hp * 0.8f);
        sem.lp += f * sem.bp;

        sem.lp = std::clamp (sem.lp, -8.0f, 8.0f);
        sem.bp = std::clamp (sem.bp, -8.0f, 8.0f);
        if (!std::isfinite (sem.lp)) { sem.reset(); return 0.0f; }

        switch (type)
        {
            case 0:  return sem.lp;
            case 1:  return sem.hp;
            case 2:  return sem.bp;
            case 3:  return sem.lp + sem.hp;   // Notch
            default: return sem.lp;
        }
    }

    // ═══════════════════════════════════════════════
    // ARP 2600 — Aggressive 4-pole ladder
    // ═══════════════════════════════════════════════
    float runARP2600 (float in, float cutHz, float reso, float sr)
    {
        float fc = std::clamp (cutHz / sr, 0.0001f, 0.497f);
        float g = 1.0f - std::exp (-2.0f * 3.14159265f * fc);
        float fb = reso * 4.1f;

        float fbSig = arp.s4 * fb;
        fbSig = (fbSig > 0.0f) ? std::tanh (fbSig * 2.0f) : std::tanh (fbSig * 1.5f);

        float input = std::clamp (in * 1.2f - fbSig, -4.0f, 4.0f);

        arp.s1 = std::clamp (arp.s1 + g * (input  - arp.s1), -5.0f, 5.0f);
        arp.s2 = std::clamp (arp.s2 + g * (arp.s1 - arp.s2), -5.0f, 5.0f);
        arp.s3 = std::clamp (arp.s3 + g * (arp.s2 - arp.s3), -5.0f, 5.0f);
        arp.s4 = std::clamp (arp.s4 + g * (arp.s3 - arp.s4), -5.0f, 5.0f);

        if (!std::isfinite (arp.s4)) { arp.reset(); return 0.0f; }
        return arp.s4;
    }

    // ═══════════════════════════════════════════════
    // LIQUID — Ripples/Jupiter VCA ladder
    // ═══════════════════════════════════════════════
    float runLiquid (float in, float cutHz, float reso, int type, int poles, float sr)
    {
        float g = std::tan (3.14159265f * cutHz / sr);
        float G = g / (1.0f + g);
        float fb = reso * 3.8f;
        float comp = 1.0f + reso * reso * 2.0f;

        float fbSig = liquid.s4 / (1.0f + std::abs (liquid.s4 * 0.3f));
        float u = in * comp - fb * fbSig;

        auto vcaSat = [](float x) -> float {
            if (x > 1.3f) return 1.0f;
            if (x < -1.3f) return -1.0f;
            return x - (x * x * x) * 0.1f;
        };

        liquid.s1 += G * (vcaSat (u)         - liquid.s1);
        liquid.s2 += G * (vcaSat (liquid.s1)  - liquid.s2);
        liquid.s3 += G * (vcaSat (liquid.s2)  - liquid.s3);
        liquid.s4 += G * (vcaSat (liquid.s3)  - liquid.s4);

        if (!std::isfinite (liquid.s4)) { liquid.reset(); return 0.0f; }

        float lp4 = liquid.s4;
        float lp2 = liquid.s2;
        float bp  = (liquid.s2 - liquid.s4) * 2.0f;
        float hp  = u / std::max (1.0f, comp) - liquid.s4;

        switch (type)
        {
            case 0:  return (poles >= 24) ? lp4 : lp2;
            case 1:  return hp;
            case 2:  return bp;
            default: return lp4;
        }
    }
};
