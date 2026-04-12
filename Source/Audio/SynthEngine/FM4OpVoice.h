#pragma once
#include "AnalogVoice.h" // for SynthVoiceParams
#include "../FX/MultiModelFilter.h"
#include <cmath>
#include <array>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// FM4OpVoice PRO — 4-Operator FM Synthesis
//
// Each operator has:
//   ■ Frequency ratio (to base note)
//   ■ Output level (0-100)
//   ■ Individual decay envelope (mod fades independently)
//
// OP1 = Carrier (output to audio)
// OP2, OP3, OP4 = Modulators (modify OP1's frequency)
// OP4 has self-feedback (DX7-style)
//
// 8 algorithms define how operators connect:
//   0: [4→3→2→1]     Full serial — max complexity, metallic
//   1: [3→2]+[4]→1   Two mods into carrier, parallel feel
//   2: [4→3]+[2→1]   Two pairs — bass body + bright attack
//   3: [4→2]+[3]→1   Branch topology
//   4: [4→3→1]+[2]   Chain + parallel carrier
//   5: [4+3+2]→1     Three mods into one carrier — very bright
//   6: [4→3]+[2]+[1] One modulated pair + two carriers
//   7: [4]+[3]+[2]+[1] Pure additive — organ/drawbar
//
// Carrier has full ADSR for amplitude control.
// Stereo output (mono signal written to L+R).
// ═══════════════════════════════════════════════════════════════════
class FM4OpVoice
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        dtSec = 1.0 / sr;
        reset();
    }

    void reset()
    {
        for (auto& p : phase) p = 0.0;
        fbOutput = 0.0f;
        carEnvVal = filtEnvVal = 0.0f;
        carStage = filtStage = 0;
        filter.reset();
        for (auto& e : modEnvVal) e = 1.0f;
        playing = false;
        samplesPlayed = gateSamples = 0;
    }

    void noteOn (int noteIdx, int octave, float velocity, const SynthVoiceParams& p,
                 float gateDuration = 0.2f)
    {
        params = p;
        vel = std::clamp (velocity, 0.0f, 1.0f);

        float bHz = noteToHz (noteIdx, octave);
        bHz *= std::pow (2.0f, params.tune / 12.0f);
        bHz = std::clamp (bHz, 8.0f, 20000.0f);

        // Anti-click: micro-glide on retrigger
        bool wasActive = playing && carEnvVal > 0.001f;
        if (wasActive && baseHz > 1.0f)
        {
            glideRatio = baseHz / std::max (1.0f, bHz); // old/new pitch ratio
            glideCoeff = std::exp (-1.0f / (static_cast<float>(sampleRate) * 0.002f)); // 2ms
        }
        else
        {
            glideRatio = 1.0f;
            glideCoeff = 0.0f;
        }
        baseHz = bHz;

        // Operator frequencies
        opFreq[0] = baseHz * std::max (0.1f, params.cRatio);
        opFreq[1] = baseHz * std::max (0.1f, params.r2);
        opFreq[2] = baseHz * std::max (0.1f, params.r3);
        opFreq[3] = baseHz * std::max (0.1f, params.r4);

        // Operator mod depths — exponential mapping for fine control at low values
        // Level 0-100 → exp curve: sweet spot is 0-30, extreme at 70+
        opLevel[0] = params.cLevel;                           // carrier level
        auto expLevel = [](float lvl, float hz) {
            float norm = lvl * 0.01f; // 0-1
            float curved = norm * norm * norm; // cubic curve: fine control at low end
            return curved * hz * 3.0f; // max ≈ 3× baseHz deviation
        };
        opLevel[1] = expLevel (params.l2, baseHz);
        opLevel[2] = expLevel (params.l3, baseHz);
        opLevel[3] = expLevel (params.l4, baseHz);

        // Per-operator decay times
        opDecay[1] = std::max (0.005f, params.dc2);
        opDecay[2] = std::max (0.005f, params.dc3);
        opDecay[3] = std::max (0.005f, params.dc4);

        fbAmount = params.fmFeedback * 0.003f;

        playing = true;
        hasPlocks = false;
        killFadeSamples = 0; // CRITICAL: reset kill state on retrigger
        carStage = filtStage = 0;
        // Anti-click: preserve envelope if voice was already playing
        if (!wasActive)
        {
            carEnvVal = filtEnvVal = 0.0f;
            filter.reset();
        }
        else
        {
            // Gentle filter reset on retrigger
            filter.softReset();
        }
        for (int oi = 0; oi < 4; ++oi)
        {
            if (wasActive)
                modEnvVal[oi] = std::max (modEnvVal[oi], 0.8f); // smooth bump, no hard reset
            else
                modEnvVal[oi] = 1.0f;
        }
        samplesPlayed = 0;
        gateSamples = std::max (64, static_cast<int>(gateDuration * sampleRate));
    }

    bool isPlaying() const { return playing; }
    bool isKilling() const { return playing && killFadeSamples > 0; }
    bool isGateActive() const { return playing && carStage < 3; }
    void releaseGate() { if (playing && carStage < 3) { carStage = 3; filtStage = 3; } }
    void kill()
    {
        if (carEnvVal > 0.001f)
            killFadeSamples = 256;
        else
        {
            playing = false;
            carEnvVal = filtEnvVal = 0.0f;
            filter.reset();
        }
    }
    void updateParams (const SynthVoiceParams& p)
    {
        if (!hasPlocks) params = p;
    }
    bool hasPlocks = false;
    void setPlocked() { hasPlocks = true; }

    void setGlide (float fromFreq, float gTime)
    {
        glideRatio = fromFreq / std::max (1.0f, baseHz);
        glideCoeff = (gTime > 0.001f) ? std::exp (-1.0f / (static_cast<float>(sampleRate) * gTime)) : 0.0f;
    }

    void setGlideTarget (float targetFreq, float gTime)
    {
        // For legato: change base frequency, set glide from current position
        float currentHz = baseHz * glideRatio;
        baseHz = targetFreq;
        glideRatio = currentHz / std::max (1.0f, baseHz);
        glideCoeff = (gTime > 0.001f) ? std::exp (-1.0f / (static_cast<float>(sampleRate) * gTime)) : 0.0f;
        // Update operator freqs
        opFreq[0] = baseHz * std::max (0.1f, params.cRatio);
        opFreq[1] = baseHz * std::max (0.1f, params.r2);
        opFreq[2] = baseHz * std::max (0.1f, params.r3);
        opFreq[3] = baseHz * std::max (0.1f, params.r4);
    }

    void updateGate (float gateDuration)
    {
        gateSamples = std::max (gateSamples, static_cast<int>(gateDuration * sampleRate));
        // Reopen gate if in release → jump back to sustain stage
        if (carStage >= 3) carStage = 2;
        if (filtStage >= 3) filtStage = 2;
    }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        if (!playing) return;

        // Hard timeout: 10 seconds max
        if (samplesPlayed > static_cast<int>(sampleRate * 10.0)) { kill(); return; }

        const int algo = std::clamp (params.fmAlgo, 0, 7);
        const float fdt = static_cast<float>(dtSec);

        for (int i = 0; i < numSamples; ++i)
        {
            // ── Glide (portamento) ──
            if (std::abs (glideRatio - 1.0f) > 0.0001f)
            {
                glideRatio = 1.0f + (glideRatio - 1.0f) * glideCoeff;
                if (std::abs (glideRatio - 1.0f) < 0.0001f) glideRatio = 1.0f;
            }
            float gR = glideRatio;

            if (carStage < 3 && samplesPlayed >= gateSamples)
                carStage = filtStage = 3;

            // ── Per-operator mod envelopes (exponential decay) ──
            float env2 = modEnvVal[1];
            float env3 = modEnvVal[2];
            float env4 = modEnvVal[3];
            modEnvVal[1] *= std::exp (-fdt / opDecay[1]);
            modEnvVal[2] *= std::exp (-fdt / opDecay[2]);
            modEnvVal[3] *= std::exp (-fdt / opDecay[3]);

            // Scaled mod amounts = level × envelope
            float m2 = opLevel[1] * env2;
            float m3 = opLevel[2] * env3;
            float m4 = opLevel[3] * env4;

            // ── OP4 with self-feedback ──
            float op4out = fmSine (phase[3], fbOutput * fbAmount);
            fbOutput = op4out;

            // ── Compute based on algorithm ──
            float carrierOut = 0.0f;

            switch (algo)
            {
                case 0: // [4→3→2→1] full serial
                {
                    float op3 = fmSine (phase[2], op4out * m4);
                    float op2 = fmSine (phase[1], op3 * m3);
                    carrierOut = fmSine (phase[0], op2 * m2);
                    break;
                }
                case 1: // [3→2]+[4]→1
                {
                    float op3 = fmSine (phase[2], 0.0f);
                    float op2 = fmSine (phase[1], op3 * m3);
                    carrierOut = fmSine (phase[0], op2 * m2 + op4out * m4);
                    break;
                }
                case 2: // [4→3]+[2→1] two pairs
                {
                    float op3 = fmSine (phase[2], op4out * m4);
                    float op2 = fmSine (phase[1], 0.0f);
                    float car1 = fmSine (phase[0], op2 * m2);
                    carrierOut = (car1 + op3) * 0.7f;
                    break;
                }
                case 3: // [4→2]+[3]→1
                {
                    float op3 = fmSine (phase[2], 0.0f);
                    float op2 = fmSine (phase[1], op4out * m4);
                    carrierOut = fmSine (phase[0], op2 * m2 + op3 * m3);
                    break;
                }
                case 4: // [4→3→1]+[2]
                {
                    float op3 = fmSine (phase[2], op4out * m4);
                    float op2 = fmSine (phase[1], 0.0f);
                    float car1 = fmSine (phase[0], op3 * m3);
                    carrierOut = (car1 + op2) * 0.7f;
                    break;
                }
                case 5: // [4+3+2]→1 three into one
                {
                    float op3 = fmSine (phase[2], 0.0f);
                    float op2 = fmSine (phase[1], 0.0f);
                    carrierOut = fmSine (phase[0], op4out * m4 + op3 * m3 + op2 * m2);
                    break;
                }
                case 6: // [4→3]+[2]+[1] pair + two carriers
                {
                    float op3 = fmSine (phase[2], op4out * m4);
                    float op2 = fmSine (phase[1], 0.0f);
                    float car1 = fmSine (phase[0], 0.0f);
                    carrierOut = (car1 + op2 + op3) * 0.5f;
                    break;
                }
                case 7: // additive: [4]+[3]+[2]+[1]
                {
                    float op3 = fmSine (phase[2], 0.0f);
                    float op2 = fmSine (phase[1], 0.0f);
                    float car1 = fmSine (phase[0], 0.0f);
                    carrierOut = (car1 + op2 + op3 + op4out) * 0.4f;
                    break;
                }
            }

            // Advance phases
            for (int op = 0; op < 4; ++op)
            {
                phase[op] += static_cast<double>(opFreq[op] * gR) * dtSec;
                if (phase[op] >= 1.0) phase[op] -= std::floor (phase[op]);
            }

            // ── Carrier ADSR ──
            carEnvVal = runADSR (carEnvVal, carStage, params.cA, params.cD, params.cS, params.cR, fdt);

            float rawOut = carrierOut * opLevel[0];

            // ── Filter (multi-model: CLN/ACD/DRT/SEM/ARP/LQD) ──
            if (params.cut < 99.5f || params.fenv > 0.01f)
            {
                filtEnvVal = runADSR (filtEnvVal, filtStage, params.fA, params.fD, params.fS, params.fR, fdt);
                float cutHz = 20.0f * std::pow (2.0f, (params.cut * 0.01f) * 10.0f);
                float envOctaves = params.fenv * filtEnvVal * 7.0f;
                float modCut = cutHz * std::pow (2.0f, envOctaves);
                modCut = std::clamp (modCut, 16.0f, std::min (18000.0f, static_cast<float>(sampleRate) * 0.45f));

                rawOut = filter.process (rawOut, modCut, params.res, params.fModel, params.fType, params.fPoles,
                                         static_cast<float>(sampleRate));
            }

            float sample = rawOut * carEnvVal * vel * params.volume;

            // Kill fade (anti-click on voice steal)
            if (killFadeSamples > 0)
            {
                sample *= static_cast<float>(killFadeSamples) / 256.0f;
                --killFadeSamples;
                if (killFadeSamples == 0) { playing = false; break; }
            }

            // Smooth tanh-style soft clip (no hard transition = no click)
            sample = std::tanh (sample);

            outL[i] += sample;
            outR[i] += sample;
            ++samplesPlayed;

            if (carStage == 3 && carEnvVal < 0.0001f)
            {
                playing = false;
                break;
            }
            // Hard timeout: max 10 seconds
            if (samplesPlayed > static_cast<int>(sampleRate * 10.0))
            {
                kill();
                break;
            }
        }
    }

private:
    double sampleRate = 44100.0;
    double dtSec = 1.0 / 44100.0;
    SynthVoiceParams params;
    float vel = 0.8f;
    bool playing = false;
    int  killFadeSamples = 0;
    float baseHz = 440.0f;
    float glideRatio = 1.0f;
    float glideCoeff = 0.0f;

    std::array<double, 4> phase {};
    std::array<float, 4>  opFreq {};
    std::array<float, 4>  opLevel {};   // [0]=carrier level, [1-3]=mod depth in Hz
    std::array<float, 4>  opDecay {};   // [1-3]=individual decay times
    std::array<float, 4>  modEnvVal {}; // [1-3]=current envelope value (1→0)

    float fbOutput = 0.0f;
    float fbAmount = 0.0f;

    float carEnvVal = 0.0f;
    float filtEnvVal = 0.0f;
    int carStage = 0;
    int filtStage = 0;
    int samplesPlayed = 0, gateSamples = 0;

    // Multi-model filter (6 models: CLN/ACD/DRT/SEM/ARP/LQD)
    MultiModelFilterCh filter;

    static float fmSine (double phase, float modulation)
    {
        double p = phase + static_cast<double>(modulation);
        return std::sin (static_cast<float>(p * 6.283185307179586));
    }

    static float noteToHz (int noteIdx, int octave)
    {
        int semi = noteIdx + (octave + 2) * 12;
        return 440.0f * std::pow (2.0f, (semi - 69.0f) / 12.0f);
    }

    static float runADSR (float val, int& stage, float a, float d, float s, float r, float fdt)
    {
        a = std::max (0.001f, a);
        d = std::max (0.001f, d);
        r = std::max (0.001f, r);
        switch (stage)
        {
            case 0:
                val += fdt / a;
                if (val >= 1.0f) { val = 1.0f; stage = 1; }
                break;
            case 1:
            {
                float tgt = std::clamp (s, 0.0f, 1.0f);
                val = tgt + (val - tgt) * std::exp (-fdt / (d * 0.35f));
                if (std::abs (val - tgt) < 0.001f) { val = tgt; stage = 2; }
                break;
            }
            case 2:
                val = std::clamp (s, 0.0f, 1.0f);
                break;
            case 3:
                val *= std::exp (-fdt / (r * 0.35f));
                break;
        }
        return val;
    }

};
