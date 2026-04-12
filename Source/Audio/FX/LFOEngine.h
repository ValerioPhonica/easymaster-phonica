#pragma once
#include <cmath>
#include <array>
#include <algorithm>
#include <juce_core/juce_core.h>
#include "../../Sequencer/TrackState.h"

// ═══════════════════════════════════════════════════════════════════
// LFOEngine — 3 LFOs per track, continuous modulation
//
// Each LFO: shape (6), rate (free Hz smoothed OR BPM-synced with
// divisions incl. triplets/dotted), depth (-1 to +1 bipolar),
// target (all engine parameters)
// ═══════════════════════════════════════════════════════════════════

class LFOEngine
{
public:
    // ── Sync division table (slow→fast) ──
    static constexpr int kNumDivisions = 18;

    static float getSyncBeats (int idx)
    {
        static const float beats[kNumDivisions] = {
            128.0f,   // 0  32/1  (32 bars)
             64.0f,   // 1  16/1  (16 bars)
             32.0f,   // 2  8/1   (8 bars)
             16.0f,   // 3  4/1   (4 bars)
              8.0f,   // 4  2/1   (2 bars)
              4.0f,   // 5  1/1   (1 bar)
              3.0f,   // 6  1/2D  (dotted half)
              2.0f,   // 7  1/2
              1.333f,  // 8  1/2T  (triplet half)
              1.5f,   // 9  1/4D  (dotted quarter)
              1.0f,   // 10 1/4
              0.667f,  // 11 1/4T  (triplet quarter)
              0.75f,  // 12 1/8D  (dotted eighth)
              0.5f,   // 13 1/8
              0.333f,  // 14 1/8T  (triplet eighth)
              0.25f,  // 15 1/16
              0.167f,  // 16 1/16T (triplet sixteenth)
              0.125f   // 17 1/32
        };
        return beats[std::clamp (idx, 0, kNumDivisions - 1)];
    }

    static const char* getDivName (int idx)
    {
        static const char* names[kNumDivisions] = {
            "32/1","16/1","8/1","4/1","2/1","1/1","1/2D","1/2","1/2T",
            "1/4D","1/4","1/4T","1/8D","1/8","1/8T",
            "1/16","16T","1/32"
        };
        return names[std::clamp (idx, 0, kNumDivisions - 1)];
    }

    // ── Synth target names (ALL engines + FX + Sampler + new params + LFO cross-mod + Wavetable) ──
    static constexpr int kNumSynthTargets = 150;
    static const char* getSynthTargetName (int idx)
    {
        static const char* n[kNumSynthTargets] = {
            "Pitch","Cutoff","Reso","Volume","Pan",           // 0-4
            "Delay","Dist","Chorus","Reverb",                  // 5-8
            "PulseW","OscMix","Detune","SubLvl","UniSpr",      // 9-13
            "FiltEnv","AmpAtk","AmpDcy","AmpSus",              // 14-17
            "Charact","LinFM",                                 // 18-19
            "Op1Rat","Op2Rat","Op2Lvl","Op3Rat","Op3Lvl","Op4Rat","Op4Lvl","Op4FB", // 20-27
            "Bow","Blow","Strike","Geomet","Bright","Space",   // 28-33
            "Harmon","Timbre","Morph","PlDcay","LPGCol",       // 34-38
            "AmpRel",                                          // 39
            "ChoRate","ChoDep","DlyTim","DlyFB","RevSiz","BitCr","SmpRed", // 40-46
            "SmpCut","SmpRes","SmpGn","SmpSt","SmpEnd","SmpTun","SmpFin", // 47-53
            "SyncR","UniStr","FltAtk","FltDcy","FltSus","FltRel","FMRat","FMDcy", // 54-61
            "L1Rate","L1Dep","L2Rate","L2Dep","L3Rate","L3Dep", // 62-67
            "MsgRat","MsgDep",                                 // 68-69
            "WtPos1","WtPos2","WtMix","WtWrp1","WtWrp2","WtSub", // 70-75
            "GrnPos","GrnSiz","GrnDns","GrnSpr","GrnPch","GrnPan","GrnScn", // 76-82
            "GrnTex","GrnFm","GrnFmR","GrnFmD","GrnFmS","GrnFSp", // 83-88
            "SpFmA","SpFmR","DlyDmp","RevDmp","SFltEv",       // 89-93
            "FxLP","FxHP","EQ.Lo","EQ.Md","EQ.Hi","DlyBt",    // 94-99
            // ── Extended targets (100-134) ──
            "GrnFB","GrnTlt","GrnDtn","GrnStr","GrnUV",        // 100-104
            "ElCont","ElDamp","ElFlow","ElMall","ElPtch","ElPos", // 105-110
            "FMSus","Glide",                                   // 111-112
            "SpAtkA","SpDcyA","SpSusA","SpRelA",               // 113-116 sampler amp ADSR
            "SpFltA","SpFltD","SpFltS","SpFltR",               // 117-120 sampler filter ADSR
            "SpFmEA","SpFmED","SpFmES",                        // 121-123 sampler FM env
            "DkDpth","DkAtk","DkRel",                          // 124-126 ducking
            "Op2Dcy","Op3Dcy","Op4Dcy",                        // 127-129 FM op decays
            "CarAtk","CarDcy","CarSus","CarRel","CarLvl",       // 130-134 carrier ADSR+level
            "OTT","OTT.U","OTT.D",                               // 135-137 OTT compressor
            "S.Drv","S.Ton","S.Mix","S.Bia",                       // 138-141 ProDist
            "PhMix","PhRte","PhDep","PhFB",                        // 142-145 Phaser
            "FlMix","FlRte","FlDep","FlFB"                         // 146-149 Flanger
        };
        return n[std::clamp (idx, 0, kNumSynthTargets - 1)];
    }

    // ── Drum target names (FM engine + FX + Sampler + Filter Env + LFO cross-mod + ER-1) ──
    static constexpr int kNumDrumTargets = 94;
    static const char* getDrumTargetName (int idx)
    {
        static const char* n[kNumDrumTargets] = {
            "Pitch","Decay","Tone","Volume","Pan",           // 0-4
            "Delay","Dist","Click","Cutoff","Reso",          // 5-9
            "PchDcy",                                         // 10
            "FmMix","FmRat","FmDep","FmDcy","FmNoi",         // 11-15
            // FX modulation targets (16-22)
            "ChoRate","ChoDep","DlyTim","DlyFB","RevSiz","BitCr","SmpRed",
            // Sampler modulation targets (23-26)
            "SmpCut","SmpTun","SmpGn","SmpSt",
            // Filter envelope targets (27-29)
            "FltEnv","FltAtk","FltDcy",
            // Cross-mod (30-35)
            "L1Rate","L1Dep","L2Rate","L2Dep","L3Rate","L3Dep",
            // MSEG cross-mod (36-37)
            "MsgRat","MsgDep",
            // ER-1 targets (38-47)
            "E.P1","E.P2","E.PD1","E.PD2","E.Ring","E.XMod","E.Nz","E.Cut","E.Res","E.Drv",
            // Snap (48)
            "Snap",
            // Extended sampler targets (49-54)
            "SmpRes","SmpEnd","SmpFin","SpFmA","SpFmR","SFltEv",
            // ── NEW extended targets (55-78) ──
            "ChoMix","RevMix","DlyBt","DlyDmp","RevDmp",     // 55-59
            "EQ.Lo","EQ.Md","EQ.Hi","FxLP","FxHP",           // 60-64
            "DkDpth","DkAtk","DkRel",                        // 65-67
            "SpAtkA","SpDcyA","SpSusA","SpRelA",              // 68-71 sampler amp ADSR
            "SpFltA","SpFltD","SpFltS","SpFltR",              // 72-75 sampler filt ADSR
            "SpFmEA","SpFmED","SpFmES",                        // 76-78 sampler FM env
            "OTT","OTT.U","OTT.D",                               // 79-81 OTT compressor
            "S.Drv","S.Ton","S.Mix","S.Bia",                       // 82-85 ProDist
            "PhMix","PhRte","PhDep","PhFB",                        // 86-89 Phaser
            "FlMix","FlRte","FlDep","FlFB"                         // 90-93 Flanger
        };
        return n[std::clamp (idx, 0, kNumDrumTargets - 1)];
    }

    void prepare (double sr) { sRate = static_cast<float>(sr); }

    // Reset phase for LFOs with retrig=true (called on note trigger)
    void retrigger (const std::array<LFOSettings, 3>& settings)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (settings[static_cast<size_t>(i)].retrig)
                phase[i] = 0.0f;
            // Always reset fade-in on retrigger (if fade is configured)
            if (settings[static_cast<size_t>(i)].fadeIn > 0.0f)
                fadeElapsed[i] = 0.0;
        }
    }

    // ── Exponential rate mapping for LOW mode knob (0-17 → 0.05Hz-30Hz) ──
    static float knobToHz (float knobVal)
    {
        return 0.05f * std::pow (600.0f, knobVal / 17.0f);
    }
    static float hzToKnob (float hz)
    {
        if (hz <= 0.05f) return 0.0f;
        return std::clamp (17.0f * std::log (hz / 0.05f) / std::log (600.0f), 0.0f, 17.0f);
    }

    // ── HIGH range mapping (0-17 → 100Hz-20kHz) — audio-rate FM/AM ──
    static float knobToHzHi (float knobVal)
    {
        return 100.0f * std::pow (200.0f, knobVal / 17.0f);
    }
    static float hzToKnobHi (float hz)
    {
        if (hz <= 100.0f) return 0.0f;
        return std::clamp (17.0f * std::log (hz / 100.0f) / std::log (200.0f), 0.0f, 17.0f);
    }
    static juce::String formatHz (float hz)
    {
        if (hz < 1.0f)    return juce::String (hz, 2) + "Hz";
        if (hz < 100.0f)  return juce::String (hz, 1) + "Hz";
        if (hz < 1000.0f) return juce::String (static_cast<int>(hz)) + "Hz";
        return juce::String (hz / 1000.0f, 1) + "kHz";
    }

    // crossModBase: first target index for cross-mod (62 for synth, 27 for drum)
    // Cross-mod targets: base+0=L1R, base+1=L1D, base+2=L2R, base+3=L2D, base+4=L3R, base+5=L3D
    void tick (const std::array<LFOSettings, 3>& settings, int numSamples, float bpm,
               int crossModBase = -1)
    {
        // ── Step 1: Collect cross-mod deltas from PREVIOUS block ──
        float rateMod[3]  = { 0.0f, 0.0f, 0.0f };
        float depthMod[3] = { 0.0f, 0.0f, 0.0f };
        if (crossModBase >= 0)
        {
            for (int i = 0; i < 3; ++i)
            {
                int tgt = settings[static_cast<size_t>(i)].target;
                float v = values[i]; // previous block's value
                if (std::abs (v) < 0.0001f) continue;
                int cm = tgt - crossModBase;
                if (cm >= 0 && cm < 6)
                {
                    int lfoIdx = cm / 2;
                    if (lfoIdx == i) continue; // no self-mod
                    if (cm % 2 == 0) rateMod[lfoIdx]  += v * 5.0f;
                    else             depthMod[lfoIdx] += v * 0.5f;
                }
            }
        }

        // ── Step 2: Compute all 3 LFO values with cross-mod applied ──
        for (int i = 0; i < 3; ++i)
        {
            const auto& s = settings[static_cast<size_t>(i)];
            float effDepth = s.depth + depthMod[i] + extDepthMod[i];

            // Check if any route needs this LFO (main depth or extra routes)
            bool anyRouteActive = (std::abs (effDepth) >= 0.001f);
            for (auto& r : s.extraRoutes)
                if (r.target >= 0 && std::abs (r.depth) > 0.0001f) anyRouteActive = true;

            if (!anyRouteActive) { values[i] = 0.0f; rawShape[i] = 0.0f; continue; }

            // Rate: free Hz or synced to BPM, with cross-mod + external mod offset
            float hz;
            if (s.sync && bpm > 20.0f)
            {
                int divIdx = std::clamp (static_cast<int>(s.syncDiv), 0, kNumDivisions - 1);
                float beats = getSyncBeats (divIdx);
                hz = bpm / 60.0f / std::max (0.01f, beats);
                // Apply rate mod as multiplier in sync mode
                if (std::abs (rateMod[i] + extRateMod[i]) > 0.001f)
                    hz *= std::pow (2.0f, (rateMod[i] + extRateMod[i]) * 0.2f);
            }
            else
            {
                // LOW mode: 0.05-30Hz. HIGH mode: 0.05-20kHz (audio rate)
                float maxHz = s.hiRate ? (sRate * 0.45f) : 30.0f;
                hz = std::clamp (s.rate + rateMod[i] + extRateMod[i], 0.05f, maxHz);
            }

            // Advance phase (works for both LOW and HIGH — at audio rates, natural aliasing creates FM-like sidebands)
            float advance = hz * static_cast<float>(numSamples) / sRate;
            phase[i] += advance;
            if (phase[i] >= 1.0f) phase[i] -= std::floor (phase[i]);

            // Generate shape (-1 to +1)
            float p = phase[i];
            float v = 0.0f;
            switch (s.shape)
            {
                case 0: v = std::sin (p * 6.2832f);                break; // sine
                case 1: v = 4.0f * std::abs (p - 0.5f) - 1.0f;    break; // triangle
                case 2: v = 2.0f * p - 1.0f;                       break; // saw down
                case 3: v = (p < 0.5f) ? 1.0f : -1.0f;            break; // square
                case 4: v = 1.0f - 2.0f * p;                       break; // ramp up
                case 5: // sample & hold
                    if (p < lastPhase[i])
                        shVal[i] = 2.0f * hashNoise (static_cast<int>(shCounter[i]++)) - 1.0f;
                    v = shVal[i];
                    break;
            }
            lastPhase[i] = p;

            // Smooth output — adaptive: lighter smoothing for faster LFOs
            float rawVal = v * effDepth; // bipolar: negative depth = inverted
            rawShape[i] = v; // store raw shape (-1..+1) for extra route depths
            // Time constant scales with LFO period (faster LFO = less smoothing)
            float smoothTime = std::min (0.005f, 0.15f / std::max (hz, 0.1f));
            float smoothCoeff = std::exp (-static_cast<float>(numSamples) / (sRate * smoothTime));
            smoothed[i] = rawVal + (smoothed[i] - rawVal) * smoothCoeff;
            values[i] = smoothed[i];
        }

        // ── Fade-in envelope (applies to all 3 LFOs after value computation) ──
        float blockSec = static_cast<float>(numSamples) / sRate;
        for (int i = 0; i < 3; ++i)
        {
            const auto& s = settings[static_cast<size_t>(i)];
            if (s.fadeIn <= 0.0f) continue; // no fade configured
            fadeElapsed[i] += static_cast<double>(blockSec);
            // Compute fade time in seconds
            float fadeTimeSec = s.fadeIn; // FREE mode: direct seconds
            if (s.fadeInSync && bpm > 20.0f)
            {
                // SYNC mode: fadeIn is a sync division index (same as rate syncDiv)
                static const float divBeats[] = {0.125f,0.25f,0.5f,1,2,4,8,16,32,64,128};
                int idx = std::clamp (static_cast<int>(s.fadeIn), 0, 10);
                fadeTimeSec = divBeats[idx] * 60.0f / bpm;
            }
            if (fadeTimeSec < 0.001f) continue;
            float fade = std::clamp (static_cast<float>(fadeElapsed[i]) / fadeTimeSec, 0.0f, 1.0f);
            values[i] *= fade;
            rawShape[i] *= fade;
        }
    }

    // ── Apply to SynthTrackState (ALL engine params) ──
    void applyToSynth (SynthTrackState& st) const
    {
        for (int i = 0; i < 3; ++i)
        {
            float v = values[i];
            if (std::abs (v) < 0.0001f) continue;
            int tgt = st.lfos[static_cast<size_t>(i)].target;
            switch (tgt)
            {
                case 0:  st.tune      += v * 2.0f;    break;
                case 1:  st.cut       += v * 40.0f;   break;
                case 2:  st.res       += v * 0.3f;    break;
                case 3:  st.volume    *= 1.0f + v * 0.8f; break;
                case 4:  st.pan       += v;            break;
                case 5:  st.delayMix  += v * 0.5f;    break;
                case 6:  st.distAmt   += v * 0.5f;    break;
                case 7:  st.chorusMix += v * 0.5f;    break;
                case 8:  st.reverbMix += v * 0.5f;    break;
                case 9:  st.pwm       += v * 0.3f;    break;
                case 10: st.mix2      += v * 0.5f;    break;
                case 11: st.detune    += v * 6.0f;    break;
                case 12: st.subLevel  += v * 0.5f;    break;
                case 13: st.uniSpread += v * 0.5f;    break;
                case 14: st.fenv      += v * 0.5f;    break;
                case 15: st.aA        *= std::pow (4.0f, v); break;
                case 16: st.aD        *= std::pow (4.0f, v); break;
                case 17: st.aS        += v * 0.5f;    break;
                case 18: st.charAmt   += v * 0.5f;    break;
                case 19: st.fmLinAmt  += v * 50.0f;   break;
                case 20: st.cRatio    *= std::pow (2.0f, v); break;
                case 21: st.r2        *= std::pow (2.0f, v); break;
                case 22: st.l2        += v * 50.0f;   break;
                case 23: st.r3        *= std::pow (2.0f, v); break;
                case 24: st.l3        += v * 50.0f;   break;
                case 25: st.r4        *= std::pow (2.0f, v); break;
                case 26: st.l4        += v * 50.0f;   break;
                case 27: st.fmFeedback += v * 0.5f;   break;
                case 28: st.elemBow   += v * 0.5f;    break;
                case 29: st.elemBlow  += v * 0.5f;    break;
                case 30: st.elemStrike += v * 0.5f;   break;
                case 31: st.elemGeometry += v * 0.3f;  break;
                case 32: st.elemBright += v * 0.5f;    break;
                case 33: st.elemSpace += v * 0.5f;     break;
                case 34: st.plaitsHarmonics += v * 0.5f; break;
                case 35: st.plaitsTimbre += v * 0.5f;  break;
                case 36: st.plaitsMorph += v * 0.5f;   break;
                case 37: st.plaitsDecay += v * 0.5f;   break;
                case 38: st.plaitsLpgColor += v * 0.5f; break;
                case 39: st.aR        *= std::pow (4.0f, v); break;
                // FX modulation targets (40-46)
                case 40: st.chorusRate  += v * 2.0f;    break;
                case 41: st.chorusDepth += v * 0.5f;    break;
                case 42: st.delayTime   *= std::pow (2.0f, v); break;
                case 43: st.delayFB     += v * 0.3f;    break;
                case 44: st.reverbSize  *= std::pow (2.0f, v); break;
                case 45: st.reduxBits   += v * -8.0f;   break; // negative = more crush
                case 46: st.reduxRate   += v * 0.5f;    break;
                // Sampler modulation targets (47-53)
                case 47: st.smpCut     += v * 40.0f;    break;
                case 48: st.smpRes     += v * 0.3f;     break;
                case 49: st.smpGain    *= 1.0f + v * 0.8f; break;
                case 50: st.smpStart   += v * 0.3f;     break;
                case 51: st.smpEnd     += v * 0.3f;     break;
                case 52: st.smpTune    += v * 12.0f;    break;
                case 53: st.smpFine    += v * 0.5f;     break;
                // NEW targets (54-61): syncRatio, uniStereo, filter ADSR, fmLinRatio, fmLinDecay
                case 54: st.syncRatio  *= std::pow (2.0f, v); break; // SYR
                case 55: st.uniStereo  += v * 0.5f;    break; // UST
                case 56: st.fA         *= std::pow (4.0f, v); break; // F.A
                case 57: st.fD         *= std::pow (4.0f, v); break; // F.D
                case 58: st.fS         += v * 0.5f;    break; // F.S
                case 59: st.fR         *= std::pow (4.0f, v); break; // F.R
                case 60: st.fmLinRatio *= std::pow (2.0f, v); break; // FLR
                case 61: st.fmLinDecay *= std::pow (4.0f, v); break; // FLD
                // ── X-MOD (62-67) ──
                case 62: { auto& tl = st.lfos[0]; tl.rate *= std::pow (2.0f, v); break; } // L1Rate
                case 63: { auto& tl = st.lfos[0]; tl.depth += v * 0.5f; break; }          // L1Dpth
                case 64: { auto& tl = st.lfos[1]; tl.rate *= std::pow (2.0f, v); break; } // L2Rate
                case 65: { auto& tl = st.lfos[1]; tl.depth += v * 0.5f; break; }          // L2Dpth
                case 66: { auto& tl = st.lfos[2]; tl.rate *= std::pow (2.0f, v); break; } // L3Rate
                case 67: { auto& tl = st.lfos[2]; tl.depth += v * 0.5f; break; }          // L3Dpth
                // ── MSEG (68-69) ──
                case 68: break; // MSEG rate (handled in cross-mod)
                case 69: break; // MSEG depth (handled in cross-mod)
                // ── Wavetable (70-75) ──
                case 70: st.wtPos1     += v * 0.5f;    break; // WtPos1
                case 71: st.wtPos2     += v * 0.5f;    break; // WtPos2
                case 72: st.wtMix      += v * 0.5f;    break; // WtMix
                case 73: st.wtWarpAmt1 += v * 0.5f;    break; // WtWrp1
                case 74: st.wtWarpAmt2 += v * 0.5f;    break; // WtWrp2
                case 75: st.wtSubLevel += v * 0.5f;    break; // WtSub
                // ── Granular (76-88) ──
                case 76: st.grainPos     += v * 0.3f;     break; // GrnPos
                case 77: st.grainSize    *= std::pow (4.0f, v); break; // GrnSiz
                case 78: st.grainDensity *= std::pow (4.0f, v); break; // GrnDns
                case 79: st.grainSpray   += v * 0.5f;     break; // GrnSpr
                case 80: st.grainPitch   += v * 12.0f;    break; // GrnPch
                case 81: st.grainPan     += v * 0.5f;     break; // GrnPan
                case 82: st.grainScan    += v * 0.5f;     break; // GrnScn
                case 83: st.grainTexture += v * 0.5f;     break; // GrnTex
                case 84: st.grainFmAmt  += v * 50.0f;     break; // GrnFmA
                case 85: st.grainFmRatio += v * 4.0f;     break; // GrnFmR
                case 86: st.grainFmDecay += v * 1.0f;     break; // GrnFmD
                case 87: st.grainFmSus  += v * 0.5f;      break; // GrnFmS
                case 88: st.grainFmSpread += v * 0.5f;    break; // GrnFmSp
                // Sampler FM (89-90) + damp (91-92) + filter env (93)
                case 89: st.smpFmAmt    += v * 50.0f;  break;
                case 90: st.smpFmRatio  += v * 4.0f;   break;
                case 91: st.delayDamp   += v * 0.5f;    break;
                case 92: st.reverbDamp  += v * 0.5f;    break;
                case 93: st.smpFiltEnv  += v * 50.0f;  break;
                // FX filter + EQ + delay beats (94-99)
                case 94: st.fxLP       *= std::pow (2.0f, v * 2.0f); break;
                case 95: st.fxHP       *= std::pow (2.0f, v * 2.0f); break;
                case 96: st.eqLow      += v * 12.0f;   break;
                case 97: st.eqMid      += v * 12.0f;   break;
                case 98: st.eqHigh     += v * 12.0f;   break;
                case 99: st.delayBeats *= std::pow (2.0f, v); break;
                // ── Extended targets 100-134 ──
                case 100: st.grainFeedback += v * 0.5f;  break; // GrnFB
                case 101: st.grainTilt     += v * 50.0f;  break; // GrnTlt
                case 102: st.grainUniDetune += v * 50.0f; break; // GrnDtn
                case 103: st.grainUniStereo += v * 0.5f;  break; // GrnStr
                case 104: st.grainUniVoices += static_cast<int>(v * 4); break; // GrnUV
                case 105: st.elemContour   += v * 0.5f;   break; // ElCont
                case 106: st.elemDamping   += v * 0.5f;   break; // ElDamp
                case 107: st.elemFlow      += v * 0.5f;   break; // ElFlow
                case 108: st.elemMallet    += v * 0.5f;   break; // ElMall
                case 109: st.elemPitch     += v * 0.5f;   break; // ElPtch
                case 110: st.elemPosition  += v * 0.5f;   break; // ElPos
                case 111: st.fmLinSustain  += v * 0.5f;   break; // FMSus
                case 112: st.glide         += v * 0.5f;   break; // Glide
                case 113: st.smpA          *= std::pow (4.0f, v); break; // SpAtkA
                case 114: st.smpD          *= std::pow (4.0f, v); break; // SpDcyA
                case 115: st.smpS          += v * 0.5f;   break; // SpSusA
                case 116: st.smpR          *= std::pow (4.0f, v); break; // SpRelA
                case 117: st.smpFiltA      *= std::pow (4.0f, v); break; // SpFltA
                case 118: st.smpFiltD      *= std::pow (4.0f, v); break; // SpFltD
                case 119: st.smpFiltS      += v * 0.5f;   break; // SpFltS
                case 120: st.smpFiltR      *= std::pow (4.0f, v); break; // SpFltR
                case 121: st.smpFmEnvA     *= std::pow (4.0f, v); break; // SpFmEA
                case 122: st.smpFmEnvD     *= std::pow (4.0f, v); break; // SpFmED
                case 123: st.smpFmEnvS     += v * 0.5f;   break; // SpFmES
                case 124: st.duckDepth     += v * 0.5f;   break; // DkDpth
                case 125: st.duckAtk       *= std::pow (4.0f, v); break; // DkAtk
                case 126: st.duckRel       *= std::pow (4.0f, v); break; // DkRel
                case 127: st.dc2           *= std::pow (4.0f, v); break; // Op2Dcy
                case 128: st.dc3           *= std::pow (4.0f, v); break; // Op3Dcy
                case 129: st.dc4           *= std::pow (4.0f, v); break; // Op4Dcy
                case 130: st.cA            *= std::pow (4.0f, v); break; // CarAtk
                case 131: st.cD            *= std::pow (4.0f, v); break; // CarDcy
                case 132: st.cS            += v * 0.5f;   break; // CarSus
                case 133: st.cR            *= std::pow (4.0f, v); break; // CarRel
                case 134: st.cLevel        += v * 50.0f;  break; // CarLvl
                // OTT + ProDist (135-141)
                case 135: st.ottDepth      += v * 0.5f;   break;
                case 136: st.ottUpward     += v * 0.5f;   break;
                case 137: st.ottDownward   += v * 0.5f;   break;
                case 138: st.proDistDrive  += v * 0.5f;   break;
                case 139: st.proDistTone   += v * 0.5f;   break;
                case 140: st.proDistMix    += v * 0.5f;   break;
                case 141: st.proDistBias   += v * 0.5f;   break;
                // Phaser + Flanger (142-149)
                case 142: st.phaserMix     += v * 0.5f;   break;
                case 143: st.phaserRate    += v * 2.0f;   break;
                case 144: st.phaserDepth   += v * 0.5f;   break;
                case 145: st.phaserFB      += v * 0.5f;   break;
                case 146: st.flangerMix    += v * 0.5f;   break;
                case 147: st.flangerRate   += v * 2.0f;   break;
                case 148: st.flangerDepth  += v * 0.5f;   break;
                case 149: st.flangerFB     += v * 0.5f;   break;
                default: break;
            }

            // ── Extra routes (Serum-style multi-target) ──
            float lfoRaw = rawShape[i]; // raw shape (-1 to +1) before main depth
            for (auto& route : st.lfos[static_cast<size_t>(i)].extraRoutes)
            {
                if (route.target >= 0 && std::abs (route.depth) > 0.0001f)
                    applyModToSynth (st, route.target, lfoRaw * route.depth);
            }
        }
        st.tune = std::clamp(st.tune, -24.f, 24.f); st.cut = std::clamp(st.cut, 0.f, 100.f);
        st.res = std::clamp(st.res, 0.f, 1.f); st.volume = std::clamp(st.volume, 0.f, 2.f);
        st.pan = std::clamp(st.pan, -1.f, 1.f); st.delayMix = std::clamp(st.delayMix, 0.f, 1.f);
        st.distAmt = std::clamp(st.distAmt, 0.f, 1.f); st.chorusMix = std::clamp(st.chorusMix, 0.f, 1.f);
        st.reverbMix = std::clamp(st.reverbMix, 0.f, 1.f); st.pwm = std::clamp(st.pwm, 0.05f, 0.95f);
        st.mix2 = std::clamp(st.mix2, 0.f, 1.f); st.detune = std::clamp(st.detune, -24.f, 24.f);
        st.subLevel = std::clamp(st.subLevel, 0.f, 1.f); st.uniSpread = std::clamp(st.uniSpread, 0.f, 1.f);
        st.fenv = std::clamp(st.fenv, 0.f, 1.f); st.aA = std::clamp(st.aA, 0.001f, 4.f);
        st.aD = std::clamp(st.aD, 0.01f, 4.f); st.aS = std::clamp(st.aS, 0.f, 1.f);
        st.aR = std::clamp(st.aR, 0.01f, 6.f); st.charAmt = std::clamp(st.charAmt, 0.f, 1.f);
        st.fmLinAmt = std::clamp(st.fmLinAmt, 0.f, 100.f); st.cRatio = std::clamp(st.cRatio, 0.25f, 16.f);
        st.r2 = std::clamp(st.r2, 0.25f, 16.f); st.l2 = std::clamp(st.l2, 0.f, 100.f);
        st.r3 = std::clamp(st.r3, 0.25f, 16.f); st.l3 = std::clamp(st.l3, 0.f, 100.f);
        st.r4 = std::clamp(st.r4, 0.25f, 16.f); st.l4 = std::clamp(st.l4, 0.f, 100.f);
        st.fmFeedback = std::clamp(st.fmFeedback, 0.f, 1.f);
        st.elemBow = std::clamp(st.elemBow, 0.f, 1.f); st.elemBlow = std::clamp(st.elemBlow, 0.f, 1.f);
        st.elemStrike = std::clamp(st.elemStrike, 0.f, 1.f); st.elemGeometry = std::clamp(st.elemGeometry, 0.f, 1.f);
        st.elemBright = std::clamp(st.elemBright, 0.f, 1.f); st.elemSpace = std::clamp(st.elemSpace, 0.f, 1.f);
        st.plaitsHarmonics = std::clamp(st.plaitsHarmonics, 0.f, 1.f); st.plaitsTimbre = std::clamp(st.plaitsTimbre, 0.f, 1.f);
        st.plaitsMorph = std::clamp(st.plaitsMorph, 0.f, 1.f); st.plaitsDecay = std::clamp(st.plaitsDecay, 0.f, 1.f);
        st.plaitsLpgColor = std::clamp(st.plaitsLpgColor, 0.f, 1.f);
        st.chorusRate = std::clamp(st.chorusRate, 0.1f, 10.f); st.chorusDepth = std::clamp(st.chorusDepth, 0.f, 1.f);
        st.delayTime = std::clamp(st.delayTime, 0.001f, 2.f); st.delayFB = std::clamp(st.delayFB, 0.f, 0.95f);
        st.reverbSize = std::clamp(st.reverbSize, 0.05f, 4.f);
        st.reduxBits = std::clamp(st.reduxBits, 1.f, 16.f); st.reduxRate = std::clamp(st.reduxRate, 0.f, 1.f);
        st.smpCut = std::clamp(st.smpCut, 0.f, 100.f); st.smpRes = std::clamp(st.smpRes, 0.f, 1.f);
        st.smpGain = std::clamp(st.smpGain, 0.f, 2.f);
        st.smpStart = std::clamp(st.smpStart, 0.f, 1.f); st.smpEnd = std::clamp(st.smpEnd, 0.f, 1.f);
        st.smpTune = std::clamp(st.smpTune, -24.f, 24.f); st.smpFine = std::clamp(st.smpFine, -1.f, 1.f);
        st.syncRatio = std::clamp(st.syncRatio, 0.25f, 16.f); st.uniStereo = std::clamp(st.uniStereo, 0.f, 1.f);
        st.fA = std::clamp(st.fA, 0.001f, 4.f); st.fD = std::clamp(st.fD, 0.01f, 4.f);
        st.fS = std::clamp(st.fS, 0.f, 1.f); st.fR = std::clamp(st.fR, 0.01f, 6.f);
        st.fmLinRatio = std::clamp(st.fmLinRatio, 0.25f, 16.f); st.fmLinDecay = std::clamp(st.fmLinDecay, 0.01f, 4.f);
        st.wtPos1 = std::clamp(st.wtPos1, 0.f, 1.f); st.wtPos2 = std::clamp(st.wtPos2, 0.f, 1.f);
        st.wtMix = std::clamp(st.wtMix, 0.f, 1.f); st.wtWarpAmt1 = std::clamp(st.wtWarpAmt1, 0.f, 1.f);
        st.wtWarpAmt2 = std::clamp(st.wtWarpAmt2, 0.f, 1.f); st.wtSubLevel = std::clamp(st.wtSubLevel, 0.f, 1.f);
        // Granular
        st.grainPos = std::clamp(st.grainPos, 0.f, 1.f); st.grainSize = std::clamp(st.grainSize, 0.001f, 1.f);
        st.grainDensity = std::clamp(st.grainDensity, 0.1f, 100.f); st.grainSpray = std::clamp(st.grainSpray, 0.f, 1.f);
        st.grainPitch = std::clamp(st.grainPitch, -48.f, 48.f); st.grainPan = std::clamp(st.grainPan, 0.f, 1.f);
        st.grainScan = std::clamp(st.grainScan, -2.f, 2.f); st.grainTexture = std::clamp(st.grainTexture, 0.f, 1.f);
        st.grainFmAmt = std::clamp(st.grainFmAmt, 0.f, 200.f); st.grainFmRatio = std::clamp(st.grainFmRatio, 0.25f, 16.f);
        st.grainFmDecay = std::clamp(st.grainFmDecay, 0.01f, 4.f); st.grainFmSus = std::clamp(st.grainFmSus, 0.f, 1.f);
        st.grainFmSpread = std::clamp(st.grainFmSpread, 0.f, 1.f);
        // FX filter/EQ/duck
        st.fxLP = std::clamp(st.fxLP, 200.f, 20000.f); st.fxHP = std::clamp(st.fxHP, 20.f, 5000.f);
        st.eqLow = std::clamp(st.eqLow, -12.f, 12.f); st.eqMid = std::clamp(st.eqMid, -12.f, 12.f);
        st.eqHigh = std::clamp(st.eqHigh, -12.f, 12.f);
        st.delayDamp = std::clamp(st.delayDamp, 0.f, 1.f); st.reverbDamp = std::clamp(st.reverbDamp, 0.f, 1.f);
        st.duckDepth = std::clamp(st.duckDepth, 0.f, 1.f); st.duckAtk = std::clamp(st.duckAtk, 0.001f, 0.1f);
        st.duckRel = std::clamp(st.duckRel, 0.01f, 1.f);
        // ProDist
        st.proDistDrive = std::clamp(st.proDistDrive, 0.f, 1.f); st.proDistTone = std::clamp(st.proDistTone, 0.f, 1.f);
        st.proDistMix = std::clamp(st.proDistMix, 0.f, 1.f); st.proDistBias = std::clamp(st.proDistBias, 0.f, 1.f);
        st.ottDepth = std::clamp(st.ottDepth, 0.f, 1.f); st.ottUpward = std::clamp(st.ottUpward, 0.f, 1.f);
        st.ottDownward = std::clamp(st.ottDownward, 0.f, 1.f);
    }

    // ── Apply to DrumTrackState (ALL drum params) ──
    void applyToDrum (DrumTrackState& dt) const
    {
        for (int i = 0; i < 3; ++i)
        {
            float v = values[i];
            if (std::abs (v) < 0.0001f) continue;
            int tgt = dt.lfos[static_cast<size_t>(i)].target;
            switch (tgt)
            {
                case 0:  dt.pitch    *= std::pow (2.0f, v); break; // ±1 octave multiplicative
                case 1:  dt.decay    += v * 0.5f;    break;
                case 2:  dt.tone     += v * 0.5f;    break;
                case 3:  dt.volume   *= 1.0f + v * 0.8f; break;
                case 4:  dt.pan      += v;            break;
                case 5:  dt.delayMix += v * 0.5f;    break;
                case 6:  dt.distAmt  += v * 0.5f;    break;
                case 7:  dt.click    += v * 0.5f;    break;
                case 8:  dt.drumCut  += v * 30.0f;   break;
                case 9:  dt.drumRes  += v * 0.3f;    break;
                case 10: dt.pitchDec += v * 0.3f;    break;
                // FM engine
                case 11: dt.fmMix    += v * 0.5f;    break;
                case 12: dt.fmRatio  *= std::pow (2.0f, v); break;
                case 13: dt.fmDepth  += v * 40.0f;   break;
                case 14: dt.fmDecay  += v * 0.3f;    break;
                case 15: dt.fmNoise  += v * 0.5f;    break;
                // FX modulation targets (16-22)
                case 16: dt.chorusRate  += v * 2.0f;    break;
                case 17: dt.chorusDepth += v * 0.5f;    break;
                case 18: dt.delayTime   *= std::pow (2.0f, v); break;
                case 19: dt.delayFB     += v * 0.3f;    break;
                case 20: dt.reverbSize  *= std::pow (2.0f, v); break;
                case 21: dt.reduxBits   += v * -8.0f;   break;
                case 22: dt.reduxRate   += v * 0.5f;    break;
                // Sampler targets (23-26)
                case 23: dt.smpCut     += v * 40.0f;    break;
                case 24: dt.smpTune    += v * 12.0f;    break;
                case 25: dt.smpGain    *= 1.0f + v * 0.8f; break;
                case 26: dt.smpStart   += v * 0.3f;     break;
                // Filter envelope targets (27-29)
                case 27: dt.drumFiltEnv += v * 50.0f;    break;
                case 28: dt.drumFiltA   += v * 0.05f;    break;
                case 29: dt.drumFiltD   += v * 0.5f;     break;
                // Extended sampler targets (49-54) — IRON RULE: match synth
                case 49: dt.smpRes     += v * 0.3f;     break;
                case 50: dt.smpEnd     += v * 0.3f;     break;
                case 51: dt.smpFine    += v * 50.0f;    break;
                case 52: dt.smpFmAmt   += v * 50.0f;    break;
                case 53: dt.smpFmRatio += v * 4.0f;     break;
                case 54: dt.smpFiltEnv += v * 50.0f;    break;
                // ER-1 targets (38-47)
                case 38: dt.er1Pitch1  *= std::pow (2.0f, v); break;
                case 39: dt.er1Pitch2  *= std::pow (2.0f, v); break;
                case 40: dt.er1PDec1   += v * 0.2f;    break;
                case 41: dt.er1PDec2   += v * 0.2f;    break;
                case 42: dt.er1Ring    += v * 0.5f;    break;
                case 43: dt.er1XMod    += v * 200.0f;  break;
                case 44: dt.er1Noise   += v * 0.5f;    break;
                case 45: dt.er1Cut     *= std::pow (2.0f, v * 2.0f); break;
                case 46: dt.er1Res     += v * 0.4f;    break;
                case 47: dt.er1Drive   += v * 0.5f;    break;
                case 48: dt.snap      += v * 0.5f;    break;
                // ── Extended targets (55-78) ──
                case 55: dt.chorusMix  += v * 0.5f;    break;
                case 56: dt.reverbMix  += v * 0.5f;    break;
                case 57: dt.delayBeats *= std::pow (2.0f, v); break;
                case 58: dt.delayDamp  += v * 0.5f;    break;
                case 59: dt.reverbDamp += v * 0.5f;    break;
                case 60: dt.eqLow     += v * 12.0f;   break;
                case 61: dt.eqMid     += v * 12.0f;   break;
                case 62: dt.eqHigh    += v * 12.0f;   break;
                case 63: dt.fxLP      *= std::pow (2.0f, v * 2.0f); break;
                case 64: dt.fxHP      *= std::pow (2.0f, v * 2.0f); break;
                case 65: dt.duckDepth += v * 0.5f;    break;
                case 66: dt.duckAtk   *= std::pow (4.0f, v); break;
                case 67: dt.duckRel   *= std::pow (4.0f, v); break;
                case 68: dt.smpA      *= std::pow (4.0f, v); break;
                case 69: dt.smpD      *= std::pow (4.0f, v); break;
                case 70: dt.smpS      += v * 0.5f;    break;
                case 71: dt.smpR      *= std::pow (4.0f, v); break;
                case 72: dt.smpFiltA  *= std::pow (4.0f, v); break;
                case 73: dt.smpFiltD  *= std::pow (4.0f, v); break;
                case 74: dt.smpFiltS  += v * 0.5f;    break;
                case 75: dt.smpFiltR  *= std::pow (4.0f, v); break;
                case 76: dt.smpFmEnvA *= std::pow (4.0f, v); break;
                case 77: dt.smpFmEnvD *= std::pow (4.0f, v); break;
                case 78: dt.smpFmEnvS += v * 0.5f;    break;
                // OTT + ProDist (79-85)
                case 79: dt.ottDepth      += v * 0.5f;   break;
                case 80: dt.ottUpward     += v * 0.5f;   break;
                case 81: dt.ottDownward   += v * 0.5f;   break;
                case 82: dt.proDistDrive  += v * 0.5f;   break;
                case 83: dt.proDistTone   += v * 0.5f;   break;
                case 84: dt.proDistMix    += v * 0.5f;   break;
                case 85: dt.proDistBias   += v * 0.5f;   break;
                case 86: dt.phaserMix     += v * 0.5f;   break;
                case 87: dt.phaserRate    += v * 2.0f;   break;
                case 88: dt.phaserDepth   += v * 0.5f;   break;
                case 89: dt.phaserFB      += v * 0.5f;   break;
                case 90: dt.flangerMix    += v * 0.5f;   break;
                case 91: dt.flangerRate   += v * 2.0f;   break;
                case 92: dt.flangerDepth  += v * 0.5f;   break;
                case 93: dt.flangerFB     += v * 0.5f;   break;
                // 30-35: LFO cross-mod (handled internally by tick)
                default: break;
            }

            // ── Extra routes (Serum-style multi-target) ──
            float lfoRaw = rawShape[i];
            for (auto& route : dt.lfos[static_cast<size_t>(i)].extraRoutes)
            {
                if (route.target >= 0 && std::abs (route.depth) > 0.0001f)
                    applyModToDrum (dt, route.target, lfoRaw * route.depth);
            }
        }
        dt.pitch = std::clamp(dt.pitch, 10.f, 500.f); dt.decay = std::clamp(dt.decay, 0.f, 2.f);
        dt.tone = std::clamp(dt.tone, 0.f, 1.f); dt.volume = std::clamp(dt.volume, 0.f, 2.f);
        dt.pan = std::clamp(dt.pan, -1.f, 1.f); dt.delayMix = std::clamp(dt.delayMix, 0.f, 1.f);
        dt.distAmt = std::clamp(dt.distAmt, 0.f, 1.f); dt.click = std::clamp(dt.click, 0.f, 1.f);
        dt.drumCut = std::clamp(dt.drumCut, 0.f, 100.f); dt.drumRes = std::clamp(dt.drumRes, 0.f, 1.f);
        dt.drumFiltEnv = std::clamp(dt.drumFiltEnv, 0.f, 100.f);
        dt.drumFiltA = std::clamp(dt.drumFiltA, 0.0001f, 0.2f);
        dt.drumFiltD = std::clamp(dt.drumFiltD, 0.01f, 2.f);
        dt.pitchDec = std::clamp(dt.pitchDec, 0.001f, 1.f);
        dt.fmMix = std::clamp(dt.fmMix, 0.f, 1.f); dt.fmRatio = std::clamp(dt.fmRatio, 0.25f, 16.f);
        dt.fmDepth = std::clamp(dt.fmDepth, 0.f, 200.f); dt.fmDecay = std::clamp(dt.fmDecay, 0.005f, 1.f);
        dt.fmNoise = std::clamp(dt.fmNoise, 0.f, 1.f);
        dt.chorusRate = std::clamp(dt.chorusRate, 0.1f, 10.f); dt.chorusDepth = std::clamp(dt.chorusDepth, 0.f, 1.f);
        dt.delayTime = std::clamp(dt.delayTime, 0.001f, 2.f); dt.delayFB = std::clamp(dt.delayFB, 0.f, 0.95f);
        dt.reverbSize = std::clamp(dt.reverbSize, 0.05f, 4.f);
        dt.reduxBits = std::clamp(dt.reduxBits, 1.f, 16.f); dt.reduxRate = std::clamp(dt.reduxRate, 0.f, 1.f);
        dt.smpCut = std::clamp(dt.smpCut, 0.f, 100.f); dt.smpRes = std::clamp(dt.smpRes, 0.f, 1.f);
        dt.smpGain = std::clamp(dt.smpGain, 0.f, 2.f); dt.smpTune = std::clamp(dt.smpTune, -24.f, 24.f);
        dt.smpStart = std::clamp(dt.smpStart, 0.f, 1.f); dt.smpEnd = std::clamp(dt.smpEnd, 0.f, 1.f);
        // ER-1 clamps
        dt.er1Pitch1 = std::clamp(dt.er1Pitch1, 20.f, 4000.f); dt.er1Pitch2 = std::clamp(dt.er1Pitch2, 20.f, 4000.f);
        dt.er1PDec1 = std::clamp(dt.er1PDec1, 0.001f, 1.f); dt.er1PDec2 = std::clamp(dt.er1PDec2, 0.001f, 1.f);
        dt.er1Ring = std::clamp(dt.er1Ring, 0.f, 1.f); dt.er1XMod = std::clamp(dt.er1XMod, 0.f, 1000.f);
        dt.er1Noise = std::clamp(dt.er1Noise, 0.f, 1.f);
        dt.er1Cut = std::clamp(dt.er1Cut, 50.f, 20000.f); dt.er1Res = std::clamp(dt.er1Res, 0.f, 0.99f);
        dt.er1Drive = std::clamp(dt.er1Drive, 0.f, 1.f);
        dt.snap = std::clamp(dt.snap, 0.f, 1.f);
        // FX filter/EQ/duck
        dt.fxLP = std::clamp(dt.fxLP, 200.f, 20000.f); dt.fxHP = std::clamp(dt.fxHP, 20.f, 5000.f);
        dt.eqLow = std::clamp(dt.eqLow, -12.f, 12.f); dt.eqMid = std::clamp(dt.eqMid, -12.f, 12.f);
        dt.eqHigh = std::clamp(dt.eqHigh, -12.f, 12.f);
        dt.delayDamp = std::clamp(dt.delayDamp, 0.f, 1.f); dt.reverbDamp = std::clamp(dt.reverbDamp, 0.f, 1.f);
        dt.chorusMix = std::clamp(dt.chorusMix, 0.f, 1.f);
        dt.duckDepth = std::clamp(dt.duckDepth, 0.f, 1.f); dt.duckAtk = std::clamp(dt.duckAtk, 0.001f, 0.1f);
        dt.duckRel = std::clamp(dt.duckRel, 0.01f, 1.f);
        // ProDist
        dt.proDistDrive = std::clamp(dt.proDistDrive, 0.f, 1.f); dt.proDistTone = std::clamp(dt.proDistTone, 0.f, 1.f);
        dt.proDistMix = std::clamp(dt.proDistMix, 0.f, 1.f); dt.proDistBias = std::clamp(dt.proDistBias, 0.f, 1.f);
        dt.ottDepth = std::clamp(dt.ottDepth, 0.f, 1.f); dt.ottUpward = std::clamp(dt.ottUpward, 0.f, 1.f);
        dt.ottDownward = std::clamp(dt.ottDownward, 0.f, 1.f);
    }

    void reset()
    {
        for (auto& p : phase) p = 0;
        for (auto& v : values) v = 0;
        for (auto& s : smoothed) s = 0;
        for (auto& r : extRateMod) r = 0;
        for (auto& d : extDepthMod) d = 0;
    }

    // External modulation inputs (from MSEG → LFO cross-mod)
    std::array<float, 3> extRateMod {};   // added to rate before computing
    std::array<float, 3> extDepthMod {};  // added to depth before applying

private:
    float sRate = 44100.0f;
public:
    float getValue (int i) const { return (i >= 0 && i < 3) ? values[static_cast<size_t>(i)] : 0.0f; }
    float getPhase (int i) const { return (i >= 0 && i < 3) ? phase[static_cast<size_t>(i)] : 0.0f; }
private:
    std::array<float, 3> phase {};
    std::array<float, 3> values {};
    std::array<float, 3> smoothed {};
    std::array<float, 3> lastPhase {};
    std::array<float, 3> shVal {};
    std::array<int, 3> shCounter {};
    std::array<float, 3> rawShape {}; // raw shape value (-1..+1) before depth, for extra routes
    std::array<double, 3> fadeElapsed {{ 99.0, 99.0, 99.0 }}; // time since trigger (starts high = fully faded in)

    static float hashNoise (int x)
    {
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        return static_cast<float>(x & 0xFFFF) / 65535.0f;
    }

public:
    // Static helpers for single-target modulation (used by MSEG)
    static void applyModToSynth (SynthTrackState& st, int tgt, float v)
    {
        if (std::abs (v) < 0.0001f) return;
        switch (tgt)
        {
            case 0:  st.tune      += v * 2.0f;    break;
            case 1:  st.cut       += v * 40.0f;   break;
            case 2:  st.res       += v * 0.3f;    break;
            case 3:  st.volume    *= 1.0f + v * 0.8f; break;
            case 4:  st.pan       += v;            break;
            case 5:  st.delayMix  += v * 0.5f;    break;
            case 6:  st.distAmt   += v * 0.5f;    break;
            case 7:  st.chorusMix += v * 0.5f;    break;
            case 8:  st.reverbMix += v * 0.5f;    break;
            case 9:  st.pwm       += v * 0.3f;    break;
            case 10: st.mix2      += v * 0.5f;    break;
            case 11: st.detune    += v * 6.0f;    break;
            case 12: st.subLevel  += v * 0.5f;    break;
            case 13: st.uniSpread += v * 0.5f;    break;
            case 14: st.fenv      += v * 0.5f;    break;
            case 15: st.aA        *= std::pow (4.0f, v); break;
            case 16: st.aD        *= std::pow (4.0f, v); break;
            case 17: st.aS        += v * 0.5f;    break;
            case 18: st.charAmt   += v * 0.5f;    break;
            case 19: st.fmLinAmt  += v * 50.0f;   break;
            // FM operator params (20-27)
            case 20: st.cRatio    *= std::pow (2.0f, v); break;
            case 21: st.r2        *= std::pow (2.0f, v); break;
            case 22: st.l2        += v * 50.0f;   break;
            case 23: st.r3        *= std::pow (2.0f, v); break;
            case 24: st.l3        += v * 50.0f;   break;
            case 25: st.r4        *= std::pow (2.0f, v); break;
            case 26: st.l4        += v * 50.0f;   break;
            case 27: st.fmFeedback += v * 0.5f;   break;
            // Elements (28-33)
            case 28: st.elemBow    += v * 0.5f;   break;
            case 29: st.elemBlow   += v * 0.5f;   break;
            case 30: st.elemStrike += v * 0.5f;   break;
            case 31: st.elemGeometry += v * 0.3f;  break;
            case 32: st.elemBright += v * 0.5f;    break;
            case 33: st.elemSpace  += v * 0.5f;    break;
            // Plaits (34-38)
            case 34: st.plaitsHarmonics += v * 0.5f; break;
            case 35: st.plaitsTimbre += v * 0.5f;  break;
            case 36: st.plaitsMorph += v * 0.5f;   break;
            case 37: st.plaitsDecay += v * 0.5f;   break;
            case 38: st.plaitsLpgColor += v * 0.5f; break;
            case 39: st.aR        *= std::pow (4.0f, v); break;
            // FX targets (40-46)
            case 40: st.chorusRate  += v * 2.0f;    break;
            case 41: st.chorusDepth += v * 0.5f;    break;
            case 42: st.delayTime   *= std::pow (2.0f, v); break;
            case 43: st.delayFB     += v * 0.3f;    break;
            case 44: st.reverbSize  *= std::pow (2.0f, v); break;
            case 45: st.reduxBits   += v * -8.0f;   break;
            case 46: st.reduxRate   += v * 0.5f;    break;
            // Sampler targets (47-53)
            case 47: st.smpCut     += v * 40.0f;    break;
            case 48: st.smpRes     += v * 0.3f;     break;
            case 49: st.smpGain    *= 1.0f + v * 0.8f; break;
            case 50: st.smpStart   += v * 0.3f;     break;
            case 51: st.smpEnd     += v * 0.3f;     break;
            case 52: st.smpTune    += v * 12.0f;    break;
            case 53: st.smpFine    += v * 0.5f;     break;
            // Extended targets (54-61)
            case 54: st.syncRatio *= std::pow (2.0f, v); break;
            case 55: st.uniStereo += v * 0.5f;    break;
            case 56: st.fA        *= std::pow (4.0f, v); break;
            case 57: st.fD        *= std::pow (4.0f, v); break;
            case 58: st.fS        += v * 0.5f;    break;
            case 59: st.fR        *= std::pow (4.0f, v); break;
            case 60: st.fmLinRatio *= std::pow (2.0f, v); break;
            case 61: st.fmLinDecay *= std::pow (4.0f, v); break;
            // 62-67: LFO cross-mod (handled externally by processor)
            // 68-69: MSEG cross-mod (handled externally by processor)
            // ── Wavetable targets ──
            case 70: st.wtPos1     += v * 0.5f;    break;
            case 71: st.wtPos2     += v * 0.5f;    break;
            case 72: st.wtMix      += v * 0.5f;    break;
            case 73: st.wtWarpAmt1 += v * 0.5f;    break;
            case 74: st.wtWarpAmt2 += v * 0.5f;    break;
            case 75: st.wtSubLevel += v * 0.5f;    break;
            // ── Granular targets ──
            case 76: st.grainPos     += v * 0.3f;     break; // position shift
            case 77: st.grainSize    *= std::pow (4.0f, v); break; // size multiply
            case 78: st.grainDensity *= std::pow (4.0f, v); break; // density multiply
            case 79: st.grainSpray   += v * 0.5f;     break; // spray
            case 80: st.grainPitch   += v * 12.0f;    break; // pitch spread ±12st
            case 81: st.grainPan     += v * 0.5f;     break; // pan spread
            case 82: st.grainScan    += v * 0.5f;     break; // scan speed
            // Granular FM targets (83-88)
            case 83: st.grainTexture += v * 0.5f;     break;
            case 84: st.grainFmAmt  += v * 50.0f;     break;
            case 85: st.grainFmRatio += v * 4.0f;     break;
            case 86: st.grainFmDecay += v * 1.0f;     break;
            case 87: st.grainFmSus  += v * 0.5f;      break;
            case 88: st.grainFmSpread += v * 0.5f;    break;
            // Sampler FM (89-90)
            case 89: st.smpFmAmt    += v * 50.0f;     break;
            case 90: st.smpFmRatio  += v * 4.0f;      break;
            // FX damp (91-92)
            case 91: st.delayDamp   += v * 0.5f;      break;
            case 92: st.reverbDamp  += v * 0.5f;      break;
            case 93: st.smpFiltEnv  += v * 50.0f;     break;
            // FX filter + EQ + delay beats (94-99)
            case 94: st.fxLP       *= std::pow (2.0f, v * 2.0f); break;
            case 95: st.fxHP       *= std::pow (2.0f, v * 2.0f); break;
            case 96: st.eqLow      += v * 12.0f;   break;
            case 97: st.eqMid      += v * 12.0f;   break;
            case 98: st.eqHigh     += v * 12.0f;   break;
            case 99: st.delayBeats *= std::pow (2.0f, v); break;
            // ── Extended targets 100-134 ──
            case 100: st.grainFeedback += v * 0.5f;  break;
            case 101: st.grainTilt     += v * 50.0f;  break;
            case 102: st.grainUniDetune += v * 50.0f; break;
            case 103: st.grainUniStereo += v * 0.5f;  break;
            case 104: st.grainUniVoices += static_cast<int>(v * 4); break;
            case 105: st.elemContour   += v * 0.5f;   break;
            case 106: st.elemDamping   += v * 0.5f;   break;
            case 107: st.elemFlow      += v * 0.5f;   break;
            case 108: st.elemMallet    += v * 0.5f;   break;
            case 109: st.elemPitch     += v * 0.5f;   break;
            case 110: st.elemPosition  += v * 0.5f;   break;
            case 111: st.fmLinSustain  += v * 0.5f;   break;
            case 112: st.glide         += v * 0.5f;   break;
            case 113: st.smpA          *= std::pow (4.0f, v); break;
            case 114: st.smpD          *= std::pow (4.0f, v); break;
            case 115: st.smpS          += v * 0.5f;   break;
            case 116: st.smpR          *= std::pow (4.0f, v); break;
            case 117: st.smpFiltA      *= std::pow (4.0f, v); break;
            case 118: st.smpFiltD      *= std::pow (4.0f, v); break;
            case 119: st.smpFiltS      += v * 0.5f;   break;
            case 120: st.smpFiltR      *= std::pow (4.0f, v); break;
            case 121: st.smpFmEnvA     *= std::pow (4.0f, v); break;
            case 122: st.smpFmEnvD     *= std::pow (4.0f, v); break;
            case 123: st.smpFmEnvS     += v * 0.5f;   break;
            case 124: st.duckDepth     += v * 0.5f;   break;
            case 125: st.duckAtk       *= std::pow (4.0f, v); break;
            case 126: st.duckRel       *= std::pow (4.0f, v); break;
            case 127: st.dc2           *= std::pow (4.0f, v); break;
            case 128: st.dc3           *= std::pow (4.0f, v); break;
            case 129: st.dc4           *= std::pow (4.0f, v); break;
            case 130: st.cA            *= std::pow (4.0f, v); break;
            case 131: st.cD            *= std::pow (4.0f, v); break;
            case 132: st.cS            += v * 0.5f;   break;
            case 133: st.cR            *= std::pow (4.0f, v); break;
            case 134: st.cLevel        += v * 50.0f;  break;
            case 135: st.ottDepth      += v * 0.5f;   break;
            case 136: st.ottUpward     += v * 0.5f;   break;
            case 137: st.ottDownward   += v * 0.5f;   break;
            case 138: st.proDistDrive  += v * 0.5f;   break;
            case 139: st.proDistTone   += v * 0.5f;   break;
            case 140: st.proDistMix    += v * 0.5f;   break;
            case 141: st.proDistBias   += v * 0.5f;   break;
            case 142: st.phaserMix     += v * 0.5f;   break;
            case 143: st.phaserRate    += v * 2.0f;   break;
            case 144: st.phaserDepth   += v * 0.5f;   break;
            case 145: st.phaserFB      += v * 0.5f;   break;
            case 146: st.flangerMix    += v * 0.5f;   break;
            case 147: st.flangerRate   += v * 2.0f;   break;
            case 148: st.flangerDepth  += v * 0.5f;   break;
            case 149: st.flangerFB     += v * 0.5f;   break;
            default: break;
        }
    }

    static void applyModToDrum (DrumTrackState& dt, int tgt, float v)
    {
        if (std::abs (v) < 0.0001f) return;
        switch (tgt)
        {
            case 0:  dt.pitch    *= std::pow (2.0f, v); break; // ±1 octave multiplicative
            case 1:  dt.decay    += v * 0.5f;    break;
            case 2:  dt.tone     += v * 0.5f;    break;
            case 3:  dt.volume   *= 1.0f + v * 0.8f; break;
            case 4:  dt.pan      += v;            break;
            case 5:  dt.delayMix += v * 0.5f;    break;
            case 6:  dt.distAmt  += v * 0.5f;    break;
            case 7:  dt.click    += v * 0.5f;    break;
            case 8:  dt.drumCut  += v * 30.0f;   break;
            case 9:  dt.drumRes  += v * 0.3f;    break;
            case 10: dt.pitchDec += v * 0.3f;    break;
            // FM engine (11-15)
            case 11: dt.fmMix    += v * 0.5f;    break;
            case 12: dt.fmRatio  *= std::pow (2.0f, v); break;
            case 13: dt.fmDepth  += v * 40.0f;   break;
            case 14: dt.fmDecay  += v * 0.3f;    break;
            case 15: dt.fmNoise  += v * 0.5f;    break;
            // FX targets (16-22)
            case 16: dt.chorusRate  += v * 2.0f;    break;
            case 17: dt.chorusDepth += v * 0.5f;    break;
            case 18: dt.delayTime   *= std::pow (2.0f, v); break;
            case 19: dt.delayFB     += v * 0.3f;    break;
            case 20: dt.reverbSize  *= std::pow (2.0f, v); break;
            case 21: dt.reduxBits   += v * -8.0f;   break;
            case 22: dt.reduxRate   += v * 0.5f;    break;
            // Sampler targets (23-26)
            case 23: dt.smpCut     += v * 40.0f;    break;
            case 24: dt.smpTune    += v * 12.0f;    break;
            case 25: dt.smpGain    *= 1.0f + v * 0.8f; break;
            case 26: dt.smpStart   += v * 0.3f;     break;
            // Filter envelope targets (27-29)
            case 27: dt.drumFiltEnv += v * 50.0f;    break;
            case 28: dt.drumFiltA   += v * 0.05f;    break;
            case 29: dt.drumFiltD   += v * 0.5f;     break;
            // Extended sampler targets (49-54) — IRON RULE: match synth
            case 49: dt.smpRes     += v * 0.3f;     break;
            case 50: dt.smpEnd     += v * 0.3f;     break;
            case 51: dt.smpFine    += v * 50.0f;    break;
            case 52: dt.smpFmAmt   += v * 50.0f;    break;
            case 53: dt.smpFmRatio += v * 4.0f;     break;
            case 54: dt.smpFiltEnv += v * 50.0f;    break;
            // ER-1 targets (38-47)
            case 38: dt.er1Pitch1  *= std::pow (2.0f, v); break;
            case 39: dt.er1Pitch2  *= std::pow (2.0f, v); break;
            case 40: dt.er1PDec1   += v * 0.2f;    break;
            case 41: dt.er1PDec2   += v * 0.2f;    break;
            case 42: dt.er1Ring    += v * 0.5f;    break;
            case 43: dt.er1XMod    += v * 200.0f;  break;
            case 44: dt.er1Noise   += v * 0.5f;    break;
            case 45: dt.er1Cut     *= std::pow (2.0f, v * 2.0f); break;
            case 46: dt.er1Res     += v * 0.4f;    break;
            case 47: dt.er1Drive   += v * 0.5f;    break;
                case 48: dt.snap      += v * 0.5f;    break;
            // ── Extended targets (55-78) ──
            case 55: dt.chorusMix  += v * 0.5f;    break;
            case 56: dt.reverbMix  += v * 0.5f;    break;
            case 57: dt.delayBeats *= std::pow (2.0f, v); break;
            case 58: dt.delayDamp  += v * 0.5f;    break;
            case 59: dt.reverbDamp += v * 0.5f;    break;
            case 60: dt.eqLow     += v * 12.0f;   break;
            case 61: dt.eqMid     += v * 12.0f;   break;
            case 62: dt.eqHigh    += v * 12.0f;   break;
            case 63: dt.fxLP      *= std::pow (2.0f, v * 2.0f); break;
            case 64: dt.fxHP      *= std::pow (2.0f, v * 2.0f); break;
            case 65: dt.duckDepth += v * 0.5f;    break;
            case 66: dt.duckAtk   *= std::pow (4.0f, v); break;
            case 67: dt.duckRel   *= std::pow (4.0f, v); break;
            case 68: dt.smpA      *= std::pow (4.0f, v); break;
            case 69: dt.smpD      *= std::pow (4.0f, v); break;
            case 70: dt.smpS      += v * 0.5f;    break;
            case 71: dt.smpR      *= std::pow (4.0f, v); break;
            case 72: dt.smpFiltA  *= std::pow (4.0f, v); break;
            case 73: dt.smpFiltD  *= std::pow (4.0f, v); break;
            case 74: dt.smpFiltS  += v * 0.5f;    break;
            case 75: dt.smpFiltR  *= std::pow (4.0f, v); break;
            case 76: dt.smpFmEnvA *= std::pow (4.0f, v); break;
            case 77: dt.smpFmEnvD *= std::pow (4.0f, v); break;
            case 78: dt.smpFmEnvS += v * 0.5f;    break;
            case 79: dt.ottDepth      += v * 0.5f;   break;
            case 80: dt.ottUpward     += v * 0.5f;   break;
            case 81: dt.ottDownward   += v * 0.5f;   break;
            case 82: dt.proDistDrive  += v * 0.5f;   break;
            case 83: dt.proDistTone   += v * 0.5f;   break;
            case 84: dt.proDistMix    += v * 0.5f;   break;
            case 85: dt.proDistBias   += v * 0.5f;   break;
            case 86: dt.phaserMix     += v * 0.5f;   break;
            case 87: dt.phaserRate    += v * 2.0f;   break;
            case 88: dt.phaserDepth   += v * 0.5f;   break;
            case 89: dt.phaserFB      += v * 0.5f;   break;
            case 90: dt.flangerMix    += v * 0.5f;   break;
            case 91: dt.flangerRate   += v * 2.0f;   break;
            case 92: dt.flangerDepth  += v * 0.5f;   break;
            case 93: dt.flangerFB     += v * 0.5f;   break;
            // 30-35: LFO cross-mod (handled externally by processor)
            // 36-37: MSEG cross-mod (handled externally by processor)
            default: break;
        }
    }
};
