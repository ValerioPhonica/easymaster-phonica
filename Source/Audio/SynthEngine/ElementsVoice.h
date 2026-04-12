#pragma once
#include <cmath>
#include <array>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// ElementsVoice v12 — Modal synthesis
//
// v12: SPACE = Dattorro plate reverb (faithful MI Elements port)
//      4 input diffusion allpass + 2 cross-fed delay tanks
//      with modulated delays and LP/HP damping
// ═══════════════════════════════════════════════════════════════════

struct ElementsParams
{
    float bow       = 0.0f;
    float blow      = 0.0f;
    float strike    = 0.8f;
    float contour   = 0.5f;
    float mallet    = 0.5f;
    float flow      = 0.5f;
    float geometry  = 0.25f;
    float brightness = 0.5f;
    float damping   = 0.5f;
    float position  = 0.3f;
    float space     = 0.3f;
    float volume    = 0.8f;
    float tune      = 0.0f;
    float pitch     = 0.5f;
};

// ── Dattorro Plate Reverb (MI Elements faithful) ──────────────
class ElementsPlateReverb
{
public:
    void prepare (float sr)
    {
        sRate = sr;
        float r = sr / 32000.0f; // MI runs at 32kHz, scale all delays
        // Input diffusion allpass lengths
        idL[0] = dl(142, r); idL[1] = dl(107, r); idL[2] = dl(379, r); idL[3] = dl(277, r);
        // Tank A delays
        taModL = dl(672, r); taAp1L = dl(1800, r); taDel2L = dl(4453, r); taAp2L = dl(3163, r);
        // Tank B delays
        tbModL = dl(908, r); tbAp1L = dl(2656, r); tbDel2L = dl(3720, r); tbAp2L = dl(3163, r);
        // Output tap positions (from MI code)
        tL[0] = dl(353, r); tL[1] = dl(3627, r); tL[2] = dl(1990, r);
        tL[3] = dl(187, r); tL[4] = dl(1228, r); tL[5] = dl(2673, r);
        modPhase = 0.0;
        reset();
    }

    void reset()
    {
        for (auto& s : idBuf0) s=0; for (auto& s : idBuf1) s=0;
        for (auto& s : idBuf2) s=0; for (auto& s : idBuf3) s=0;
        idWr[0]=idWr[1]=idWr[2]=idWr[3]=0;
        for (auto& s : taModBuf) s=0; for (auto& s : taAp1Buf) s=0;
        for (auto& s : taDel2Buf) s=0; for (auto& s : taAp2Buf) s=0;
        for (auto& s : tbModBuf) s=0; for (auto& s : tbAp1Buf) s=0;
        for (auto& s : tbDel2Buf) s=0; for (auto& s : tbAp2Buf) s=0;
        taW[0]=taW[1]=taW[2]=taW[3]=0;
        tbW[0]=tbW[1]=tbW[2]=tbW[3]=0;
        lpA = lpB = hpA = hpB = 0.0f;
        tankFeedA = tankFeedB = 0.0f;
        modPhase = 0.0;
    }

    void process (float in, float& outL, float& outR, float amount, float rvTime, float lpDamp)
    {
        float diffCoeff1 = 0.72f;  // reduced input diffusion — less metallic ring
        float diffCoeff2 = 0.55f;  // softer tank diffusion
        float decay = 0.25f + rvTime * 0.74f; // 0.25 to 0.99 — slightly tamed max

        // LP coefficient: lower = darker/warmer/more pastoso
        float lpC = 0.10f + lpDamp * 0.40f; // range 0.10-0.50 (darker than before)
        float hpC = 0.001f; // very gentle HP — keep the lows, pastoso!

        // ── Input diffusion (4 allpass in series) ──
        float sig = in * 0.85f; // slight input reduction to prevent buildup
        sig = apProcess (idBuf0.data(), idL[0], idWr[0], sig, diffCoeff1);
        sig = apProcess (idBuf1.data(), idL[1], idWr[1], sig, diffCoeff1);
        sig = apProcess (idBuf2.data(), idL[2], idWr[2], sig, diffCoeff2);
        sig = apProcess (idBuf3.data(), idL[3], idWr[3], sig, diffCoeff2);

        // ── Modulation (slower LFO + deeper for dreamy chorusing) ──
        modPhase += 0.22 / sRate; // ~0.22Hz — slower, more dreamy
        if (modPhase >= 1.0) modPhase -= 1.0;
        float mod = std::sin (static_cast<float>(modPhase) * 6.2832f);
        float mod2 = std::sin (static_cast<float>(modPhase * 1.73) * 6.2832f); // secondary LFO
        int modOffsetA = static_cast<int>((mod * 20.0f + mod2 * 8.0f) * sRate / 32000.0f);
        int modOffsetB = static_cast<int>((-mod * 20.0f + mod2 * 6.0f) * sRate / 32000.0f);

        // ── Tank A ──
        float inputA = sig + tankFeedB * decay;
        // Modulated delay
        writeDelay (taModBuf.data(), taModL, taW[0], inputA);
        float a1 = readDelay (taModBuf.data(), taModL, taW[0], taModL + modOffsetA);
        // LP damping
        lpA += (a1 - lpA) * lpC;
        a1 = lpA;
        // HP damping
        hpA += (a1 - hpA) * hpC;
        a1 = a1 - hpA;
        // Allpass 1
        a1 = apProcess (taAp1Buf.data(), taAp1L, taW[1], a1, -diffCoeff2);
        // Delay 2
        writeDelay (taDel2Buf.data(), taDel2L, taW[2], a1);
        float a2 = readDelay (taDel2Buf.data(), taDel2L, taW[2], taDel2L);
        // Allpass 2
        a2 = apProcess (taAp2Buf.data(), taAp2L, taW[3], a2, diffCoeff2);
        tankFeedA = std::tanh (a2 * decay * 0.9f); // soft saturation — pastoso warmth

        // ── Tank B ──
        float inputB = sig + tankFeedA * decay;
        writeDelay (tbModBuf.data(), tbModL, tbW[0], inputB);
        float b1 = readDelay (tbModBuf.data(), tbModL, tbW[0], tbModL + modOffsetB);
        lpB += (b1 - lpB) * lpC;
        b1 = lpB;
        hpB += (b1 - hpB) * hpC;
        b1 = b1 - hpB;
        b1 = apProcess (tbAp1Buf.data(), tbAp1L, tbW[1], b1, -diffCoeff2);
        writeDelay (tbDel2Buf.data(), tbDel2L, tbW[2], b1);
        float b2 = readDelay (tbDel2Buf.data(), tbDel2L, tbW[2], tbDel2L);
        b2 = apProcess (tbAp2Buf.data(), tbAp2L, tbW[3], b2, diffCoeff2);
        tankFeedB = std::tanh (b2 * decay * 0.9f); // soft saturation — pastoso warmth

        // ── Output taps (6 taps for stereo decorrelation, like MI) ──
        float tpL = readTap (taDel2Buf.data(), taDel2L, taW[2], tL[0])
                  + readTap (taDel2Buf.data(), taDel2L, taW[2], tL[1])
                  - readTap (tbAp1Buf.data(), tbAp1L, tbW[1], tL[2])
                  + readTap (tbDel2Buf.data(), tbDel2L, tbW[2], tL[3]);

        float tpR = readTap (tbDel2Buf.data(), tbDel2L, tbW[2], tL[0])
                  + readTap (tbDel2Buf.data(), tbDel2L, tbW[2], tL[4])
                  - readTap (taAp1Buf.data(), taAp1L, taW[1], tL[5])
                  + readTap (taDel2Buf.data(), taDel2L, taW[2], tL[3]);

        tpL *= 0.13f;  // reduced gain — less aggressive, more dreamy
        tpR *= 0.13f;

        // Mix
        float wet = amount;
        float dry = 1.0f - wet;
        outL = in * dry + tpL * wet;
        outR = in * dry + tpR * wet;
    }

private:
    float sRate = 44100.0f;
    double modPhase = 0;

    // Clamp and scale delay length
    static int dl (int samples32k, float ratio) { return std::clamp (static_cast<int>(samples32k * ratio), 1, 7999); }

    // Allpass filter
    float apProcess (float* buf, int len, int& wr, float in, float coeff)
    {
        int rd = (wr - len + 8000) % 8000;
        float delayed = buf[rd];
        float out = delayed - coeff * in;
        buf[wr] = std::clamp (in + coeff * delayed, -4.0f, 4.0f);
        wr = (wr + 1) % 8000;
        return out;
    }

    void writeDelay (float* buf, int len, int& wr, float in)
    {
        buf[wr] = std::clamp (in, -4.0f, 4.0f);
        wr = (wr + 1) % 8000;
    }

    float readDelay (float* buf, int /*len*/, int wr, int delaySmp)
    {
        int rd = (wr - delaySmp + 8000 * 2) % 8000;
        return buf[rd];
    }

    float readTap (float* buf, int /*len*/, int wr, int tap)
    {
        int rd = (wr - tap + 8000 * 2) % 8000;
        return buf[rd];
    }

    // Input diffusion buffers
    std::array<float, 8000> idBuf0{}, idBuf1{}, idBuf2{}, idBuf3{};
    int idWr[4] = {}, idL[4] = {};

    // Tank A buffers
    std::array<float, 8000> taModBuf{}, taAp1Buf{}, taDel2Buf{}, taAp2Buf{};
    int taW[4] = {}, taModL=0, taAp1L=0, taDel2L=0, taAp2L=0;

    // Tank B buffers
    std::array<float, 8000> tbModBuf{}, tbAp1Buf{}, tbDel2Buf{}, tbAp2Buf{};
    int tbW[4] = {}, tbModL=0, tbAp1L=0, tbDel2L=0, tbAp2L=0;

    // LP/HP damping states
    float lpA=0, lpB=0, hpA=0, hpB=0;

    // Cross-feed between tanks
    float tankFeedA=0, tankFeedB=0;

    // Output tap positions
    int tL[6] = {};
};

// ═══════════════════════════════════════════════════════════════════

class ElementsVoice
{
public:
    static constexpr int kN = 24;

    bool isPlaying() const { return playing; }
    bool isKilling() const { return playing && killFading; }
    bool isGateActive() const { return false; } // Elements always retriggers (no ADSR gate)
    void releaseGate() { if (playing) killFading = true; } // trigger fade-out
    void kill()
    {
        if (playing && fade > 0.001f)
            killFading = true; // will fade out via `fade` in renderBlock
        else
        {
            playing = false; fade = 0.0f;
            for (auto& m : md) { m.s1 = m.s2 = 0; }
            reverb.reset(); // clear reverb tail on kill
        }
    }
    void updateParams (const ElementsParams& p) { if (!hasPlocks) par = p; }
    bool hasPlocks = false;
    void setPlocked() { hasPlocks = true; }

    void prepare (double sr)
    {
        sRate = static_cast<float>(sr);
        dt = 1.0f / sRate;
        reverb.prepare (sRate);
        reset();
    }

    void reset()
    {
        for (auto& m : md) m.s1 = m.s2 = 0;
        excEnv = fade = bowSt = bowFeedback = blowBP1 = blowBP2 = malletLP = malletSine = 0.0f;
        bowPh = 0.0; nSeed = 48271;
        outLPL = outLPR = 0.0f;
        reverb.reset();
        playing = false; smpCnt = gateSmp = stage = 0;
    }

    void noteOn (int noteIdx, int octave, float velocity, const ElementsParams& p,
                 float gateDuration = 0.2f)
    {
        par = p;
        vel = std::clamp (velocity, 0.0f, 1.0f);
        int semi = noteIdx + (octave + 2) * 12;
        float pitchShift = (par.pitch - 0.5f) * 48.0f;
        freq = 440.0f * std::pow (2.0f, (semi - 69.0f + par.tune + pitchShift) / 12.0f);
        freq = std::clamp (freq, 16.0f, 14000.0f);

        for (auto& m : md) { m.s1 *= 0.02f; m.s2 *= 0.02f; }
        updateModes();

        excEnv = 1.0f; stage = 0;
        bool wasKillFading = killFading;
        float prevFade = fade;
        playing = true; hasPlocks = false; killFading = false; smpCnt = 0;
        fade = wasKillFading ? std::max (0.1f, prevFade) : 1.0f;
        gateSmp = static_cast<int>(gateDuration * sRate);
        bowPh = 0.0; bowSt = bowFeedback = blowBP1 = blowBP2 = malletLP = malletSine = 0.0f;
        nSeed = static_cast<uint32_t>(noteIdx * 7919 + octave * 104729 + 1);
    }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        if (!playing) return;
        if (smpCnt > static_cast<int>(sRate * 12.0f)) { kill(); return; }

        updateModes();

        float brtCut = 400.0f + par.brightness * par.brightness * 18000.0f;
        brtCut = std::min (brtCut, sRate * 0.49f);
        float brtCoeff = 1.0f - std::exp (-6.2832f * brtCut / sRate);

        for (int i = 0; i < numSamples; ++i)
        {
            fade = std::min (1.0f, fade + dt * 600.0f);
            if (stage < 2 && smpCnt >= gateSmp) stage = 2;

            // ── Exciter envelope ──
            float ct = 0.002f + par.contour * 0.4f;
            if (stage == 0) { excEnv = 1.0f; stage = 1; }
            else if (stage == 1)
            {
                float sus = std::max (par.bow, par.blow) * 0.4f;
                excEnv = sus + (excEnv - sus) * std::exp (-dt / ct);
            }
            else excEnv *= std::exp (-dt / 0.004f);

            // ── EXCITER ──
            float exc = 0.0f;

            // STRIKE: noise + tonal sine burst
            int sDurSmp = std::max (8, static_cast<int>((0.0002f + (1.0f - par.mallet) * 0.03f) * sRate));
            if (smpCnt < sDurSmp)
            {
                float t = static_cast<float>(smpCnt) / static_cast<float>(sDurSmp);
                float env = t < 0.03f ? t * 33.0f : std::exp (-t * 3.0f);
                float n = xnoise();
                float fc = 0.02f + par.mallet * 0.95f;
                malletLP += (n - malletLP) * fc;
                malletSine += freq * dt;
                if (malletSine >= 1.0f) malletSine -= std::floor (malletSine);
                float tonalAmt = (1.0f - par.mallet) * 0.7f;
                float strikeOut = malletLP * (1.0f - tonalAmt)
                                + std::sin (malletSine * 6.2832f) * tonalAmt;
                exc += strikeOut * par.strike * vel * 5.5f * env;
            }

            // BLOW: resonant bandpass noise
            if (par.blow > 0.01f)
            {
                float n = xnoise();
                float turb = 1.0f - par.flow * 0.5f * (1.0f + std::sin (smpCnt * dt * 4.5f));
                n *= turb;
                float blowF = freq * (0.5f + par.brightness * 1.5f);
                blowF = std::min (blowF, sRate * 0.45f);
                float g = std::tan (3.14159f * blowF / sRate);
                float k = 3.5f - par.flow * 2.5f;
                float a1 = 1.0f / (1.0f + g * (g + k));
                float v3 = n - blowBP2;
                float v1 = a1 * blowBP1 + g * a1 * v3;
                float v2 = blowBP2 + g * a1 * blowBP1 + g * g * a1 * v3;
                blowBP1 = std::clamp (2.0f * v1 - blowBP1, -4.0f, 4.0f);
                blowBP2 = std::clamp (2.0f * v2 - blowBP2, -4.0f, 4.0f);
                exc += v1 * par.blow * excEnv * 2.5f;
            }

            // BOW: friction with resonator feedback
            if (par.bow > 0.01f)
            {
                bowPh += static_cast<double>(freq) * static_cast<double>(dt);
                if (bowPh >= 1.0) bowPh -= 1.0;
                float bowVel = static_cast<float>(2.0 * bowPh - 1.0) * par.bow;
                float diff = bowVel - bowSt - bowFeedback * 0.15f;
                float fric = diff * std::exp (-diff * diff * 2.5f);
                bowSt += fric * 0.3f;
                exc += (fric + xnoise() * 0.04f * par.bow) * excEnv * 2.5f;
            }

            // ── RESONATOR (24 SVF bandpass) ──
            float rL = 0.0f, rR = 0.0f;
            for (int m = 0; m < kN; ++m)
            {
                auto& c = md[m];
                if (c.freq < 16.0f || c.freq > sRate * 0.45f) continue;

                float v3 = exc * c.excG - c.s2;
                float v1 = c.a1 * c.s1 + c.a2 * v3;
                float v2 = c.s2 + c.a2 * c.s1 + c.a3 * v3;
                c.s1 = 2.0f * v1 - c.s1;
                c.s2 = 2.0f * v2 - c.s2;
                c.s1 = std::clamp (c.s1, -10.0f, 10.0f);
                c.s2 = std::clamp (c.s2, -10.0f, 10.0f);

                float sig = v1 * c.outG;
                rL += sig * c.panL;
                rR += sig * c.panR;
            }

            bowFeedback = md[0].s1 * 0.5f;

            // Output brightness LP
            outLPL += (rL - outLPL) * brtCoeff;
            outLPR += (rR - outLPR) * brtCoeff;
            // Flush denormals in brightness filter
            if (std::abs (outLPL) < 1e-18f) outLPL = 0.0f;
            if (std::abs (outLPR) < 1e-18f) outLPR = 0.0f;
            rL = outLPL;
            rR = outLPR;

            rL = std::tanh (rL * 1.2f);
            rR = std::tanh (rR * 1.2f);

            // ── SPACE (Dattorro plate reverb — MI faithful) ──
            float wL = rL, wR = rR;
            if (par.space > 0.01f)
            {
                float mono = (rL + rR) * 0.5f;
                float rvL, rvR;
                // Squared curve for gradual wet opening
                float wetAmt = par.space * par.space;
                // Shorter reverb: low space = very short, high = medium-long
                float rvTime = 0.3f + par.space * 0.55f; // 0.3 to 0.85 (was 0.5-0.99)
                float lpDamp = 0.12f + par.brightness * 0.45f;
                reverb.process (mono, rvL, rvR, wetAmt, rvTime, lpDamp);
                wL = rvL;
                wR = rvR;
            }

            float g = par.volume * vel * fade;
            // Kill fade — rapid but smooth fade to zero
            if (killFading)
            {
                fade *= 0.85f; // ~0.7ms at 44.1kHz (was 0.95 = too slow)
                if (fade < 0.001f)
                {
                    playing = false; fade = 0.0f; killFading = false;
                    for (auto& m : md) { m.s1 = m.s2 = 0; }
                    reverb.reset(); // CRITICAL: clear reverb tail!
                    outLPL = outLPR = 0.0f;
                    blowBP1 = blowBP2 = 0.0f;
                    bowSt = bowFeedback = 0.0f;
                    break;
                }
                g = par.volume * vel * fade;
            }
            // Sanitize output (physical modeling can produce NaN/Inf)
            if (! std::isfinite (wL)) wL = 0.0f;
            if (! std::isfinite (wR)) wR = 0.0f;
            outL[i] += wL * g;
            outR[i] += wR * g;
            ++smpCnt;

            if (stage >= 2 && (smpCnt & 127) == 0)
            {
                float e = 0.0f;
                for (auto& c : md) e += c.s1 * c.s1 + c.s2 * c.s2;
                e /= static_cast<float>(kN);
                int rel = smpCnt - gateSmp;
                if (e < 0.00001f || rel > static_cast<int>(sRate * 6.0f))
                {
                    fade *= 0.88f;
                    if (fade < 0.001f) { kill(); break; }
                }
            }
        }
    }

private:
    float sRate = 44100.0f, dt = 1.0f / 44100.0f;
    ElementsParams par;
    float freq = 440.0f, vel = 0.8f;
    bool playing = false;
    bool killFading = false;
    int smpCnt = 0, gateSmp = 0, stage = 0;
    float excEnv = 0, fade = 0;
    float bowSt = 0, bowFeedback = 0;
    float blowBP1 = 0, blowBP2 = 0;
    float malletLP = 0, malletSine = 0;
    double bowPh = 0;
    uint32_t nSeed = 48271;
    float outLPL = 0, outLPR = 0;

    ElementsPlateReverb reverb;

    struct Mode {
        float s1 = 0, s2 = 0;
        float a1 = 0, a2 = 0, a3 = 0;
        float freq = 440.0f;
        float excG = 1.0f, outG = 0.2f;
        float panL = 0.7f, panR = 0.3f;
    };
    std::array<Mode, kN> md;

    void updateModes()
    {
        float geo = par.geometry;
        float brt = par.brightness;
        float dmp = par.damping;

        for (int i = 0; i < kN; ++i)
        {
            float n = static_cast<float>(i + 1);

            float harmonic = n;
            float stiff = n * std::sqrt (1.0f + 0.001f * n * n * geo * geo);
            float metal = std::sqrt (n * (n + 1.5f));
            float bell = n * n * 0.44f + n * 0.56f;
            float inharm = n + 0.013f * n * n * n;

            float ratio;
            if (geo < 0.2f)       ratio = harmonic + geo * 5.0f * (stiff - harmonic);
            else if (geo < 0.4f)  ratio = stiff + (geo - 0.2f) * 5.0f * (metal - stiff);
            else if (geo < 0.6f)  ratio = metal + (geo - 0.4f) * 5.0f * (bell - metal);
            else if (geo < 0.8f)  ratio = bell + (geo - 0.6f) * 5.0f * (inharm - bell);
            else                  ratio = inharm * (1.0f + (geo - 0.8f) * 2.0f);

            float f = freq * std::max (0.5f, ratio);
            f = std::clamp (f, 16.0f, sRate * 0.45f);
            md[i].freq = f;

            float decayTime = 0.03f + dmp * dmp * 3.0f;  // 0.03 to 3s (was 16s!)
            float modeDecay = decayTime / (1.0f + static_cast<float>(i) * 0.12f); // faster high-mode decay
            float Q = 3.14159f * f * modeDecay / 6.908f;
            Q = std::clamp (Q, 1.0f, 200.0f); // max 200 (was 2500!)

            float brtFactor = 1.0f / (1.0f + static_cast<float>(i) * (1.0f - brt) * 0.35f);
            Q *= brtFactor;
            Q = std::max (1.0f, Q);

            float g = std::tan (3.14159f * f / sRate);
            float k = 1.0f / Q;
            md[i].a1 = 1.0f / (1.0f + g * (g + k));
            md[i].a2 = g * md[i].a1;
            md[i].a3 = g * md[i].a2;

            float pe = std::sin (n * 3.14159f * (0.02f + par.position * 0.96f));
            md[i].excG = pe * pe / (1.0f + static_cast<float>(i) * 0.03f);

            md[i].outG = 0.45f / (1.0f + static_cast<float>(i) * 0.06f);

            float phi = 0.618034f;
            float p = std::fmod (0.15f + static_cast<float>(i) * phi, 1.0f);
            md[i].panL = std::cos (p * 1.5708f);
            md[i].panR = std::sin (p * 1.5708f);
        }
    }

    float xnoise()
    {
        nSeed ^= nSeed << 13; nSeed ^= nSeed >> 17; nSeed ^= nSeed << 5;
        return static_cast<float>(static_cast<int32_t>(nSeed)) / 2147483648.0f;
    }
};
