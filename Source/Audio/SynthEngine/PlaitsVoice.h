#pragma once
#include <cmath>
#include <array>
#include <algorithm>
#include "../FX/MultiModelFilter.h"

// ═══════════════════════════════════════════════════════════════════
// PlaitsVoice v4 — Multi-model synthesis (MI Plaits-inspired)
//
// v4 improvements:
//   - VA: sub oscillator, wider detune, sync
//   - FM2: wider depth range, fine ratio
//   - GRN: 12 grains with better overlap
//   - WTB: 8 waveforms with smooth morphing
//   - STR: fractional delay interpolation (accurate tuning!)
//   - NOS: LP/BP/HP filter morphing via timbre
//   - MOD: 12 SVF resonators (like Elements v10 approach)
//   - SPH: 7 vowels, 4 formants
//   - BSS: sub + saturation + LP with resonance
// ═══════════════════════════════════════════════════════════════════

struct PlaitsParams
{
    int    model      = 0;
    float  harmonics  = 0.5f;
    float  timbre     = 0.5f;
    float  morph      = 0.0f;
    float  volume     = 0.8f;
    float  tune       = 0.0f;
    float  decay      = 0.5f;
    float  lpgColor   = 0.5f;
    // Filter
    float  cut        = 100.0f;  // 0-100 (mapped to Hz internally)
    float  res        = 0.0f;    // 0-1
    int    fType      = 0;       // 0=LP, 1=HP, 2=BP
    int    fModel     = 0;       // 0=CLN, 1=ACD, 2=DRT, 3=SEM, 4=ARP, 5=LQD
    int    fPoles     = 12;      // 6, 12, 24
    float  fenv       = 0.0f;    // filter envelope depth (0-100)
    float  fA         = 0.001f;  // filter ADSR
    float  fD         = 0.15f;
    float  fS         = 0.3f;
    float  fR         = 0.3f;
};

class PlaitsVoice
{
public:
    bool isPlaying() const { return playing; }
    bool isKilling() const { return playing && killFade > 0; }
    bool isGateActive() const { return false; } // Plaits always retriggers
    void releaseGate() { if (playing && killFade == 0) { killFade = 256; filtStage = 3; } }
    void kill() { 
        if (lpgEnv > 0.001f) 
            killFade = 256;
        else 
        { playing = false; lpgEnv = 0.0f; lpgS1 = lpgS2 = 0.0f; }
    }
    void updateParams (const PlaitsParams& p) { if (!hasPlocks) params = p; }
    bool hasPlocks = false;
    void setPlocked() { hasPlocks = true; }

    void prepare (double sr) { sampleRate = sr; dtSec = 1.0/sr; fdt = static_cast<float>(dtSec); reset(); }

    void reset()
    {
        phase = phase2 = modPhase = subPh = 0.0;
        playing = false;
        lpgEnv = lpgS1 = lpgS2 = fmFB = 0.0f;
        nSeed = 48271; samplesPlayed = 0;
        for (auto& s : delBuf) s = 0.0f;
        delWr = 0; ksLP = 0.0f;
        for (auto& g : grains) g = {};
        grainCtr = 0;
        for (auto& s : combBuf) s = 0.0f;
        combWr = 0; combLP = 0.0f;
        for (auto& m : modSt) m = {};
        for (auto& p : chPhase) p = 0.0;
        nzS1 = nzS2 = bsS1 = bsS2 = 0.0f;
        spS = {};
    }

    void noteOn (int noteIdx, int octave, float velocity, const PlaitsParams& p,
                 float gateDuration = 0.2f)
    {
        params = p;
        vel = std::clamp (velocity, 0.0f, 1.0f);
        int semi = noteIdx + (octave + 2) * 12;
        baseFreq = 440.0f * std::pow (2.0f, (semi - 69.0f + params.tune) / 12.0f);
        baseFreq = std::clamp (baseFreq, 16.0f, 16000.0f);

        bool wasPlaying = playing;
        playing = true;
        hasPlocks = false;
        killFade = 0; // reset kill state on retrigger
        lpgEnv = 1.0f;
        samplesPlayed = 0;

        // Filter envelope: reset on fresh trigger, preserve on retrigger
        filtStage = 0;
        if (!wasPlaying) { filtEnvVal = 0.0f; filter.reset(); smoothCut = p.cut; }
        else { filter.softReset(); }

        int m = std::clamp (params.model, 0, 15);
        if (!wasPlaying) { phase = phase2 = modPhase = subPh = 0.0; }
        fmFB = lpgS1 = lpgS2 = 0.0f;
        nSeed = static_cast<uint32_t>(noteIdx * 7919 + octave * 6271 + 12345);
        nzS1 = nzS2 = bsS1 = bsS2 = 0.0f;
        for (auto& ms : modSt) ms = {};
        spS = {};
        grainCtr = 0;
        for (auto& cp : chPhase) cp = 0.0;
        if (m == 6) initStr();
        if (m == 13) initComb();
    }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        if (!playing) return;
        int m = std::clamp (params.model, 0, 15);
        float inc = static_cast<float>(baseFreq * dtSec);

        for (int i = 0; i < numSamples; ++i)
        {
            // LPG envelope
            float decayTime = 0.003f + params.decay * params.decay * 12.0f;
            lpgEnv *= std::exp (-fdt / (decayTime * 0.5f));

            float sample = 0.0f;
            switch (m)
            {
                case 0:  sample = mVA (inc); break;
                case 1:  sample = mWSH (inc); break;
                case 2:  sample = mFM2 (inc); break;
                case 3:  sample = mGRN (inc); break;
                case 4:  sample = mADD (inc); break;
                case 5:  sample = mWTB (inc); break;
                case 6:  sample = mSTR (); break;
                case 7:  sample = mNOS (inc); break;
                case 8:  sample = mMOD (inc); break;
                case 9:  sample = mCHD (inc); break;
                case 10: sample = mSPH (inc); break;
                case 11: sample = mPHS (inc); break;
                case 12: sample = mHRM (inc); break;
                case 13: sample = mRSN (inc); break;
                case 14: sample = mPRT (); break;
                case 15: sample = mBSS (inc); break;
            }

            // LPG
            float ampGain = lpgEnv;
            if (params.lpgColor > 0.02f)
            {
                float lpgCutEnv = lpgEnv * lpgEnv;
                float cutHz = 30.0f + lpgCutEnv * params.lpgColor * 19000.0f;
                cutHz = std::min (cutHz, static_cast<float>(sampleRate) * 0.49f);
                float g = std::tan (3.14159f * cutHz / static_cast<float>(sampleRate));
                float k = 1.414f;
                float a1 = 1.0f / (1.0f + g * (g + k));
                float a2 = g * a1;
                float a3 = g * a2;
                float v3 = sample - lpgS2;
                float v1 = a1 * lpgS1 + a2 * v3;
                float v2 = lpgS2 + a2 * lpgS1 + a3 * v3;
                lpgS1 = std::clamp (2.0f * v1 - lpgS1, -8.0f, 8.0f);
                lpgS2 = std::clamp (2.0f * v2 - lpgS2, -8.0f, 8.0f);
                sample = sample * (1.0f - params.lpgColor) + v2 * params.lpgColor;
            }

            float out = sample * ampGain * vel;

            // ── Multi-model filter with envelope ──
            if (params.cut < 99.0f || params.fenv > 0.5f)
            {
                // Auto-release filter when LPG envelope decays (for sequencer notes)
                if (filtStage < 3 && lpgEnv < 0.05f)
                    filtStage = 3;

                // Filter ADSR envelope
                switch (filtStage)
                {
                    case 0: // Attack
                        filtEnvVal += fdt / std::max (0.0005f, params.fA);
                        if (filtEnvVal >= 1.0f) { filtEnvVal = 1.0f; filtStage = 1; }
                        break;
                    case 1: // Decay
                        filtEnvVal -= fdt / std::max (0.001f, params.fD) * (filtEnvVal - params.fS);
                        if (filtEnvVal <= params.fS + 0.001f) { filtEnvVal = params.fS; filtStage = 2; }
                        break;
                    case 2: // Sustain
                        filtEnvVal = params.fS;
                        break;
                    case 3: // Release
                        filtEnvVal *= std::exp (-fdt / std::max (0.001f, params.fR));
                        break;
                }

                // Cutoff: base + envelope modulation (octave-based, 7 oct max)
                float baseCut = 20.0f * std::pow (1000.0f, params.cut / 100.0f); // 20Hz - 20kHz
                float envOctaves = filtEnvVal * params.fenv * 7.0f;
                float cutHz = baseCut * std::pow (2.0f, envOctaves);
                cutHz = std::clamp (cutHz, 20.0f, static_cast<float>(sampleRate) * 0.45f);

                // Smooth cutoff to avoid zipper noise
                smoothCut += (cutHz - smoothCut) * 0.01f;

                out = filter.process (out, smoothCut, params.res, params.fModel,
                                       params.fType, params.fPoles, static_cast<float>(sampleRate));
            }

            out = std::tanh (out * params.volume);
            if (killFade > 0)
            {
                out *= static_cast<float>(killFade) / 256.0f;
                --killFade;
                if (killFade == 0) { playing = false; break; }
            }
            if (! std::isfinite (out)) out = 0.0f;
            outL[i] += out; outR[i] += out;
            ++samplesPlayed;
            if (lpgEnv < 0.0001f) { playing = false; break; }
            if (samplesPlayed > static_cast<int>(sampleRate * 10.0)) { playing = false; break; }
        }
    }

private:
    double sampleRate = 44100.0, dtSec = 1.0/44100.0;
    float fdt = 1.0f/44100.0f;
    PlaitsParams params;
    float baseFreq = 440.0f, vel = 0.8f;
    bool playing = false;
    int killFade = 0;
    int samplesPlayed = 0;
    float lpgEnv = 0.0f, lpgS1 = 0.0f, lpgS2 = 0.0f;
    double phase = 0, phase2 = 0, modPhase = 0, subPh = 0;
    float fmFB = 0;
    uint32_t nSeed = 48271;

    // ── Multi-model filter + envelope ──
    MultiModelFilterCh filter;
    float filtEnvVal = 0.0f;
    int   filtStage  = 0;   // 0=A, 1=D, 2=S, 3=R
    float smoothCut  = 100.0f;

    static constexpr int kMD = 4096;
    std::array<float, kMD> delBuf {}, combBuf {};
    int delWr = 0, combWr = 0;
    float ksLP = 0, combLP = 0;
    struct Grain { double ph=0; float freq=440,amp=0; int life=0,maxL=0; };
    static constexpr int kMG = 12; // 12 grains (was 8)
    std::array<Grain, kMG> grains {};
    int grainCtr = 0;
    struct ModSt { float s1=0, s2=0; }; // SVF state per modal resonator
    std::array<ModSt, 12> modSt;
    float nzS1=0, nzS2=0, bsS1=0, bsS2=0;

    // ══════ 0: Virtual Analog (sub + wider detune + sync) ══════
    float mVA (float inc)
    {
        float det = 1.0f + params.harmonics * 0.08f; // wider detune (8%)
        float pw = 0.1f + params.timbre * 0.8f;
        float mix = params.morph;
        // Osc 1: saw/square morph
        float saw = f2p1(phase) - pb(phase, inc);
        float sq = (phase < (double)pw ? 1.0f : -1.0f);
        sq += pb(phase, inc) - pb(std::fmod(phase+1.0-pw,1.0), inc);
        float o1 = saw * (1.0f-mix) + sq * mix;
        // Osc 2: detuned saw
        float i2 = inc * det;
        float o2 = f2p1(phase2) - pb(phase2, i2);
        // Sub oscillator (1 oct down, sine)
        float sub = std::sin(f(subPh) * 6.2832f) * (1.0f - params.timbre) * 0.5f;
        adv(phase, inc); adv(phase2, i2); adv(subPh, inc*0.5f);
        return (o1 + o2 * 0.65f + sub) * 0.4f;
    }

    // ══════ 1: Waveshaper ══════
    float mWSH (float inc)
    {
        float s = std::sin(f(phase) * 6.2832f);
        adv(phase, inc);
        float dr = 1.0f + params.harmonics * 25.0f;
        float sh = (s + params.morph * 0.5f) * dr;
        float t = params.timbre;
        float soft = std::tanh(sh);
        float hard = std::clamp(sh,-1.0f,1.0f);
        float fold = std::sin(sh);
        float asym = std::tanh(sh + sh*sh*0.3f); // asymmetric
        if (t < 0.33f) return lrp(soft, hard, t*3.0f) * 0.6f;
        if (t < 0.66f) return lrp(hard, fold, (t-0.33f)*3.0f) * 0.6f;
        return lrp(fold, asym, (t-0.66f)*3.0f) * 0.6f;
    }

    // ══════ 2: 2-Op FM (wider range) ══════
    float mFM2 (float inc)
    {
        float ratio = std::round((0.5f + params.harmonics * 8.0f) * 2.0f) / 2.0f;
        float depth = params.timbre * params.timbre * 8.0f; // wider depth
        float fb = params.morph * 0.7f;
        float mod = std::sin(f(modPhase) * 6.2832f + fmFB * fb);
        adv(modPhase, inc * ratio);
        fmFB = mod;
        float car = std::sin(f(phase) * 6.2832f + mod * depth);
        adv(phase, inc);
        return car * 0.7f;
    }

    // ══════ 3: Granular (12 grains) ══════
    float mGRN (float inc)
    {
        float density = 2.0f + params.harmonics * 50.0f;
        float sz = 0.002f + params.timbre * 0.15f;
        float pr = params.morph;
        grainCtr++;
        int interval = std::max(1, (int)(sampleRate / density));
        if (grainCtr >= interval) { grainCtr = 0; spGr(sz, pr); }
        float out = 0.0f;
        for (auto& g : grains) {
            if (g.life < g.maxL) {
                float t = (float)g.life / (float)g.maxL;
                float env = 0.5f * (1.0f - std::cos(t * 6.2832f)); // Hann window
                out += std::sin(f(g.ph) * 6.2832f) * env * g.amp;
                g.ph += (double)g.freq * dtSec;
                if (g.ph >= 1.0) g.ph -= std::floor(g.ph);
                g.life++;
            }
        }
        return out * 0.22f;
    }

    // ══════ 4: Additive ══════
    float mADD (float inc)
    {
        int nH = 2 + (int)(params.harmonics * 22);
        float tilt = 1.0f - params.timbre;
        float eo = params.morph;
        float out = 0.0f, mx = (float)sampleRate * 0.45f;
        for (int h = 1; h <= nH; ++h) {
            if (baseFreq * h > mx) break;
            float amp = 1.0f / std::pow((float)h, 0.5f + tilt * 1.5f);
            if (h%2 == 0) amp *= (1.0f - eo);
            else if (h > 1) amp *= (0.5f + eo * 0.5f);
            out += std::sin(f(std::fmod(phase*h,1.0)) * 6.2832f) * amp;
        }
        adv(phase, inc);
        return out * 0.2f / std::sqrt((float)std::min(nH,16));
    }

    // ══════ 5: Wavetable (8 waves, timbre=fold, morph=phase mod) ══════
    float mWTB (float inc)
    {
        float pos = params.harmonics;
        float p = f(phase);
        float pm = params.morph * std::sin(p*6.2832f*2.0f) * 0.3f;
        float wp = p + pm; wp -= std::floor(wp);
        float w0 = std::sin(wp*6.2832f);
        float w1 = 4.0f*std::abs(wp-0.5f)-1.0f;
        float w2 = 2.0f*wp-1.0f;
        float w3 = (wp<0.5f)?1.0f:-1.0f;
        float w4 = std::sin(wp*6.2832f*2.0f)*std::exp(-wp*3.0f);
        float w5 = std::sin(wp*6.2832f*3.0f)*std::exp(-wp*4.0f);
        float w6 = std::tanh(std::sin(wp*6.2832f)*3.0f);
        float w7 = std::sin(wp*6.2832f + 2.0f*std::sin(wp*6.2832f*3.0f));

        float t = pos * 7.0f;
        int idx = std::clamp((int)t, 0, 6);
        float frac = t - (float)idx;
        float waves[] = {w0,w1,w2,w3,w4,w5,w6,w7};
        float out = waves[idx]*(1.0f-frac) + waves[idx+1]*frac;
        adv(phase, inc);
        // timbre = wavefold amount (adds harmonics)
        float fold = 1.0f + params.timbre * 4.0f;
        return std::sin(out * fold) * 0.6f;
    }

    // ══════ 6: String (fractional delay for proper tuning) ══════
    void initStr()
    {
        for (auto& s : delBuf) s = 0.0f;
        int dL = std::clamp((int)(sampleRate/baseFreq), 2, kMD-2);
        for (int i = 0; i < dL; ++i)
            delBuf[i] = (2.0f*hn(i*73+19)-1.0f) * vel;
        int pick = std::max(1, (int)(dL*(0.1f+params.morph*0.4f)));
        for (int i = pick; i < dL; ++i)
            delBuf[i] -= delBuf[i-pick]*0.5f;
        delWr = dL; ksLP = 0.0f;
    }
    float mSTR()
    {
        // Fractional delay for accurate tuning
        float dLf = static_cast<float>(sampleRate / baseFreq);
        dLf = std::clamp(dLf, 2.0f, static_cast<float>(kMD - 2));
        int dI = static_cast<int>(dLf);
        float frac = dLf - static_cast<float>(dI);
        int r0 = (delWr - dI + kMD) % kMD;
        int r1 = (r0 - 1 + kMD) % kMD;
        float s = delBuf[r0] + (delBuf[r1] - delBuf[r0]) * frac;
        // harmonics = brightness of string
        float fc = 0.1f + params.harmonics * 0.88f;
        ksLP = ksLP*(1.0f-fc) + s*fc;
        // timbre = decay (sustain)
        float dmp = 0.993f + params.timbre * 0.0069f;
        // morph = body resonance (nonlinear saturation in feedback)
        float bodyDrive = 1.0f + params.morph * 3.0f;
        float driven = std::tanh(ksLP * bodyDrive) / bodyDrive;
        delBuf[delWr] = driven * dmp;
        delWr = (delWr+1) % kMD;
        return s;
    }

    // ══════ 7: Noise (harmonics=freq, timbre=Q, morph=LP→BP→HP) ══════
    float mNOS (float inc)
    {
        float n = xn();
        float fq = baseFreq * (0.25f + params.harmonics * 4.0f);
        fq = std::min(fq, (float)sampleRate*0.45f);
        float Q = 0.5f + params.timbre * 40.0f;

        float g = std::tan(3.14159f * fq / (float)sampleRate);
        float k = 1.0f / std::max(0.5f, Q);
        float a1 = 1.0f / (1.0f + g*(g+k));
        float v3 = n - nzS2;
        float v1 = a1*nzS1 + g*a1*v3;
        float v2 = nzS2 + g*a1*nzS1 + g*g*a1*v3;
        nzS1 = std::clamp(2.0f*v1-nzS1, -8.0f, 8.0f);
        nzS2 = std::clamp(2.0f*v2-nzS2, -8.0f, 8.0f);
        float lp = v2, bp = v1, hp = n - k*v1 - v2;

        // Morph: full 0-1 range → LP → BP → HP
        float m = params.morph;
        float out;
        if (m < 0.5f) out = lrp(lp, bp, m * 2.0f);
        else          out = lrp(bp, hp, (m - 0.5f) * 2.0f);
        return out * 0.5f;
    }

    // ══════ 8: Modal Resonator (12 SVF bandpass, like Elements) ══════
    float mMOD (float inc)
    {
        float exc = 0.0f;
        int bLen = (int)(sampleRate * 0.008f);
        if (samplesPlayed < bLen) {
            float t = (float)samplesPlayed / (float)bLen;
            exc = xn() * vel * std::exp(-t*4.0f) * 3.5f;
        }
        float geo = params.harmonics;
        float out = 0.0f;
        for (int m = 0; m < 12; ++m) {
            float n = (float)(m+1);
            float ratio = n + geo * (std::sqrt(n*(n+1.5f)) - n);
            float mFreq = baseFreq * ratio;
            if (mFreq > sampleRate*0.42f) continue;

            // Q controls ring time — from damping (morph) and mode number
            float decT = 0.05f + params.morph * params.morph * 8.0f;
            float mDecT = decT / (1.0f + (float)m * 0.1f);
            float Q = 3.14159f * mFreq * mDecT / 6.908f;
            Q = std::clamp(Q, 1.0f, 800.0f);

            // Brightness reduces high mode Q
            float brtF = 1.0f / (1.0f + (float)m * (1.0f-params.timbre) * 0.25f);
            Q *= brtF;
            Q = std::max(1.0f, Q);

            float g = std::tan(3.14159f * mFreq / (float)sampleRate);
            float k = 1.0f / Q;
            float a1 = 1.0f / (1.0f + g*(g+k));
            float a2 = g * a1;
            float v3 = exc - modSt[m].s2;
            float v1 = a1*modSt[m].s1 + a2*v3;
            float v2 = modSt[m].s2 + a2*modSt[m].s1 + g*a2*v3;
            modSt[m].s1 = std::clamp(2.0f*v1-modSt[m].s1, -6.0f, 6.0f);
            modSt[m].s2 = std::clamp(2.0f*v2-modSt[m].s2, -6.0f, 6.0f);

            out += v1 * (0.4f / (1.0f + (float)m * 0.08f));
        }
        return std::tanh(out * 1.5f) * 0.6f;
    }

    // ══════ 9: Chord (4 PolyBLEP voices, timbre=shape+detune) ══════
    std::array<double,4> chPhase {};
    float mCHD (float inc)
    {
        static const float ch[][4] = {
            {0,7,12,24},{0,4,7,12},{0,3,7,12},{0,5,7,12},
            {0,4,7,11},{0,3,7,10},{0,4,7,10},{0,2,7,12}
        };
        int ci = std::clamp((int)(params.harmonics*7.99f),0,7);
        float det = params.timbre * params.timbre * 0.06f; // wider detune (up to 6%)
        float inv = params.morph;
        float out = 0.0f;
        for (int v = 0; v < 4; ++v) {
            float semi = ch[ci][v];
            if (inv>0.33f && v==0) semi += 12.0f;
            if (inv>0.66f && v==1) semi += 12.0f;
            float vInc = inc * std::pow(2.0f, semi/12.0f);
            vInc *= 1.0f + (hn(v*13+7)-0.5f)*det;
            // timbre also morphs saw→square
            float saw = f2p1(chPhase[v]) - pb(chPhase[v], vInc);
            float pw = 0.5f;
            float sq = (chPhase[v] < (double)pw ? 1.0f : -1.0f);
            sq += pb(chPhase[v], vInc) - pb(std::fmod(chPhase[v]+1.0-pw,1.0), vInc);
            float voice = saw * (1.0f - params.timbre) + sq * params.timbre;
            out += voice;
            adv(chPhase[v], vInc);
        }
        return out * 0.17f;
    }

    // ══════ 10: Speech (7 vowels, 4 formants) ══════
    struct SpS { float s1a=0,s2a=0,s1b=0,s2b=0,s1c=0,s2c=0,s1d=0,s2d=0; } spS;
    float mSPH (float inc)
    {
        static const float vF[][4] = {
            {800,1200,2500,3500}, {350,2000,2800,3600}, {270,2300,3000,3700},
            {450,800,2830,3500},  {325,700,2530,3200},  {600,1000,2200,3000},
            {400,1700,2500,3400}
        };
        float vi = params.harmonics * 5.99f;
        int v0 = std::clamp((int)vi,0,5); int v1=v0+1;
        float vt = vi - (float)v0;
        float f1=vF[v0][0]*(1-vt)+vF[v1][0]*vt;
        float f2=vF[v0][1]*(1-vt)+vF[v1][1]*vt;
        float f3=vF[v0][2]*(1-vt)+vF[v1][2]*vt;
        float f4=vF[v0][3]*(1-vt)+vF[v1][3]*vt;
        float shift = 0.5f + params.timbre;
        float buzz = f2p1(phase) - pb(phase, inc);
        float noise = xn();
        float nm = params.morph;
        float src = buzz*(1.0f-nm*0.7f) + noise*nm*0.5f;
        adv(phase, inc);
        float r1=bpf(src,f1*shift,12,spS.s1a,spS.s2a);
        float r2=bpf(src,f2*shift,14,spS.s1b,spS.s2b);
        float r3=bpf(src,f3*shift,16,spS.s1c,spS.s2c);
        float r4=bpf(src,f4*shift,10,spS.s1d,spS.s2d);
        return std::tanh((r1+r2*0.7f+r3*0.35f+r4*0.15f)*2.2f)*0.55f;
    }

    // ══════ 11: Phase Distortion ══════
    float mPHS (float inc)
    {
        float d = params.harmonics * 0.9f;
        float shape = params.timbre;
        float p = f(phase);
        float warped;
        if (shape<0.5f) {
            float s=shape*2.0f, bp=0.5f-d*0.4f*s;
            warped = (p<bp) ? p*0.5f/std::max(0.01f,bp) : 0.5f+(p-bp)*0.5f/std::max(0.01f,1.0f-bp);
        } else {
            float cycles = 1.0f + (shape-0.5f)*2.0f*(1.0f+d*4.0f);
            warped = std::fmod(p*cycles, 1.0f);
        }
        float out = std::sin(warped*6.2832f + params.morph*std::sin(p*6.2832f*2.0f));
        adv(phase, inc);
        return out * 0.7f;
    }

    // ══════ 12: Harmonic Oscillator ══════
    float mHRM (float inc)
    {
        int nH = 1 + (int)(params.harmonics*15);
        float br = params.timbre, odd = params.morph;
        float out = 0.0f, mx = (float)sampleRate*0.4f;
        for (int h=1; h<=nH; ++h) {
            if (baseFreq*h > mx) break;
            float amp = std::pow(br, (float)(h-1)*0.3f);
            if (h%2==0) amp *= (1.0f-odd);
            out += std::sin(f(std::fmod(phase*h,1.0))*6.2832f) * amp;
        }
        adv(phase, inc);
        return out * 0.22f / std::sqrt((float)std::min(nH,12));
    }

    // ══════ 13: Comb Resonator ══════
    void initComb() { for(auto&s:combBuf) s=0; combWr=0; combLP=0; }
    float mRSN (float inc)
    {
        float exc = 0.0f;
        int bL = (int)(sampleRate*0.008f);
        if (samplesPlayed < bL) {
            float t=(float)samplesPlayed/(float)bL;
            exc = xn()*vel*(1.0f-t)*2.5f;
        }
        exc += xn()*params.morph*0.1f;
        // Fractional delay comb
        float dLf = static_cast<float>(sampleRate/baseFreq);
        dLf = std::clamp(dLf, 2.0f, (float)(kMD-2));
        int dI = (int)dLf;
        float frac = dLf - (float)dI;
        int r0 = (combWr-dI+kMD) % kMD;
        int r1 = (r0-1+kMD) % kMD;
        float del = combBuf[r0] + (combBuf[r1]-combBuf[r0])*frac;
        float fc = 0.1f + params.harmonics*0.85f;
        combLP = combLP*(1.0f-fc) + del*fc;
        float fb = 0.9f + params.timbre*0.098f;
        combBuf[combWr] = std::tanh(exc + combLP*fb);
        combWr = (combWr+1) % kMD;
        return del * 0.8f;
    }

    // ══════ 14: Particle (timbre=length+bright, morph=spread) ══════
    float mPRT()
    {
        float density = 1.0f + params.harmonics*60.0f;
        float spread = params.morph;
        int interval = std::max(1, (int)(sampleRate/density));
        float out = 0.0f;
        if (samplesPlayed % interval == 0) {
            float fR = baseFreq*(1.0f+xn()*spread);
            // timbre: 0=short+bright clicks, 1=long+soft tones
            float gL = 0.0002f + params.timbre * params.timbre * 0.08f;
            for (auto&g:grains) {
                if (g.life>=g.maxL) {
                    g.freq=fR; g.ph=0; g.life=0;
                    g.maxL=std::max(8,(int)(gL*sampleRate));
                    g.amp=0.3f+std::abs(xn())*0.7f;
                    break;
                }
            }
        }
        for (auto&g:grains) {
            if (g.life<g.maxL) {
                float t=(float)g.life/(float)g.maxL;
                // Envelope shape: timbre low = sharp attack, high = smooth hann
                float env;
                if (params.timbre < 0.5f)
                    env = (1.0f-t) * (1.0f-t); // sharp decay
                else
                    env = 0.5f * (1.0f - std::cos(t * 6.2832f)); // hann
                out += std::sin(f(g.ph)*6.2832f)*env*g.amp;
                g.ph += (double)g.freq*dtSec;
                if(g.ph>=1.0) g.ph -= std::floor(g.ph);
                g.life++;
            }
        }
        return out * 0.25f;
    }

    // ══════ 15: Bass (sub + drive + resonant LP) ══════
    float mBSS (float inc)
    {
        float sub = std::sin(f(subPh)*6.2832f);
        float saw = f2p1(phase2) - pb(phase2, inc);
        float sqr = (phase<0.5)?1.0f:-1.0f;
        sqr += pb(phase, inc) - pb(std::fmod(phase+0.5,1.0), inc);
        float sMix = 1.0f - params.harmonics;
        float wMix = params.harmonics;
        adv(subPh, inc*0.5f); adv(phase, inc); adv(phase2, inc);
        float raw = sub*sMix + (saw*0.6f + sqr*0.4f)*wMix;
        float dr = 1.0f + params.timbre * 12.0f;
        raw = std::tanh(raw * dr);
        // Resonant LP
        float cutHz = 60.0f + params.morph * 10000.0f;
        cutHz = std::min(cutHz, (float)sampleRate*0.49f);
        float g = std::tan(3.14159f * cutHz / (float)sampleRate);
        float k = 0.5f + (1.0f-params.morph)*1.0f; // more resonance at low cutoff
        float a1 = 1.0f / (1.0f + g*(g+k));
        float v3 = raw - bsS2;
        float v1 = a1*bsS1 + g*a1*v3;
        float v2 = bsS2 + g*a1*bsS1 + g*g*a1*v3;
        bsS1 = std::clamp(2.0f*v1-bsS1,-8.0f,8.0f);
        bsS2 = std::clamp(2.0f*v2-bsS2,-8.0f,8.0f);
        return v2 * 0.85f;
    }

    // ══════ Helpers ══════
    static float f(double p) { return (float)p; }
    static float f2p1(double p) { return (float)(2.0*p-1.0); }
    static float pb(double t, float inc) {
        double di=(double)inc; if(di<1e-12) return 0;
        if(t<di){double x=t/di; return (float)(x+x-x*x-1.0);}
        if(t>1.0-di){double x=(t-1.0)/di; return (float)(x*x+x+x+1.0);}
        return 0;
    }
    static void adv(double&p, float inc) { p+=(double)inc; if(p>=1.0)p-=std::floor(p); }
    static float lrp(float a,float b,float t) { return a+(b-a)*t; }
    float bpf(float in, float freq, float Q, float&s1, float&s2) {
        freq=std::min(freq,(float)sampleRate*0.49f);
        float g=std::tan(3.14159f*freq/(float)sampleRate);
        float k=1.0f/std::max(0.5f,Q);
        float a1=1.0f/(1.0f+g*(g+k));
        float v3=in-s2;
        float v1=a1*s1+g*a1*v3;
        float v2=s2+g*a1*s1+g*g*a1*v3;
        s1=std::clamp(2.0f*v1-s1,-8.0f,8.0f);
        s2=std::clamp(2.0f*v2-s2,-8.0f,8.0f);
        if(!std::isfinite(s1))s1=0; if(!std::isfinite(s2))s2=0;
        return v1;
    }
    void spGr(float sz, float pr) {
        for(auto&g:grains) {
            if(g.life>=g.maxL) {
                g.freq=baseFreq*(1.0f+(xn())*pr*0.5f);
                g.ph=0; g.maxL=std::max(32,(int)(sz*sampleRate));
                g.life=0; g.amp=0.3f+std::abs(xn())*0.7f;
                break;
            }
        }
    }
    static float hn(int x) {
        x=((x>>16)^x)*0x45d9f3b; x=((x>>16)^x)*0x45d9f3b; x=(x>>16)^x;
        return (float)(x&0xFFFF)/65535.0f;
    }
    float xn() {
        nSeed ^= nSeed<<13; nSeed ^= nSeed>>17; nSeed ^= nSeed<<5;
        return (float)((int32_t)nSeed) / 2147483648.0f;
    }
};
