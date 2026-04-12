#pragma once
#include <cmath>
#include <array>
#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../FX/MultiModelFilter.h"

// ═══════════════════════════════════════════════════════════════════
// GranularVoice — Granular synthesis engine (Granulator II / Pigments style)
//
// 48 grains with oldest-grain voice stealing
// Cubic Hermite interpolation for sample reading
// Cytomic SVF filter (Andrew Simper, stable at all frequencies)
// Scan position auto-movement (Granulator II style)
// Spawn jitter for organic feel
// tanh soft clip on output
// ═══════════════════════════════════════════════════════════════════

struct GranularParams
{
    int   mode       = 0;      // 0=STRD, 1=CLOUD, 2=SCRUB, 3=GLTCH, 4=FLUX
    float position   = 0.5f;   // 0-1 normalized sample position
    float grainSize  = 80.0f;  // ms (5-500)
    float density    = 10.0f;  // grains/sec (1-100)
    float spray      = 0.0f;   // 0-1 position randomization
    float pitchSpread= 0.0f;   // semitones random spread (-24..+24)
    float panSpread  = 0.0f;   // 0-1 stereo width
    int   shape      = 0;      // 0=Hann, 1=Tri, 2=Rect, 3=Tukey, 4=Gauss, 5=Saw
    int   direction  = 0;      // 0=FWD, 1=REV, 2=RND
    float tilt       = 50.0f;  // 0-100 envelope peak shift
    float texture    = 0.0f;   // 0-1 formant shift (pitch grain window independently)
    bool  freeze     = false;  // lock scan position
    float scan       = 0.0f;   // -1..+1 auto scan speed (0 = static)
    float tune       = 0.0f;   // semitones tuning
    float volume     = 0.8f;
    int   uniVoices  = 1;      // 1-8 unison voices
    float uniDetune  = 0.0f;   // 0-100 cents detune
    float uniStereo  = 0.0f;   // 0-1 stereo spread of unison voices
    int   quantize   = 0;      // 0=OFF, 1=OCT, 2=5TH, 3=TRAD, 4=SCAL
    float feedback   = 0.0f;   // 0-1 output→input feedback
    // Filter
    float cut = 75.0f, res = 0.25f;
    int   fType = 0;           // 0=LP, 1=HP, 2=BP
    int   fModel = 0;         // 0=CLN, 1=ACD, 2=DRT, 3=SEM, 4=ARP, 5=LQD
    int   fPoles = 12;        // 6, 12, 24 dB/oct
    float fenv = 0.0f;
    float fA = 0.005f, fD = 0.3f, fS = 0.6f, fR = 0.5f;
    // Amp envelope
    float aA = 0.01f, aD = 0.2f, aS = 0.7f, aR = 0.3f;
    // FM modulator (same architecture as AnalogVoice)
    float fmAmt     = 0.0f;   // 0-100 depth
    float fmRatio   = 2.0f;   // mod/carrier ratio
    float fmDecay   = 0.3f;   // mod envelope decay (sec)
    float fmSustain = 0.0f;   // 0-1 sustain level
    int   fmSnap    = 1;      // 0=FREE (atonal), 1=INT (tonal)
    float fmSpread  = 0.0f;   // 0-1 per-grain FM depth variation (0=uniform, 1=full random)
};

class GranularVoice
{
public:
    void prepare (double sr) { sampleRate = sr; }

    void noteOn (int noteIdx, int octave, float vel,
                 const GranularParams& p, float gateDuration,
                 std::shared_ptr<juce::AudioBuffer<float>> sampleBuf)
    {
        params = p;
        velocity = vel;
        bool wasPlaying = playing;
        playing = true;
        hasPlocks = false;
        gate = true;
        gateDur = gateDuration;
        gateTimer = 0.0f;
        sample = sampleBuf;

        // Base frequency from note
        int midiNote = noteIdx + (octave + 1) * 12;
        baseFreqRatio = std::pow (2.0f, (midiNote - 60 + p.tune) / 12.0f);

        // Reset all grains
        for (auto& g : grains) { g.active = false; g.age = 0; }
        grainSpawnCounter = 0.0;
        globalAge = 0;

        // Init scan position from params
        scanPos = p.position;

        // Envelope — preserve ampEnvVal on retrigger to avoid click
        if (!wasPlaying) ampEnvVal = 0.0f;
        ampStage = 0;
        filtEnvVal = 0.0f; filtStage = 0;
        killFadeSamples = 0;

        // Filter state reset
        filterL.reset(); filterR.reset();

        // FM modulator reset
        fmPhase = 0.0;
        fmEnvVal = 1.0f;
        fmAmtSmooth = 0.0f;
        fmPhaseMod = 0.0f;

        // RNG seed from note (each note gets unique grain pattern)
        rngState = static_cast<uint32_t>(midiNote * 7919 + 12345);
    }

    void kill()
    {
        if (playing && killFadeSamples == 0)
            killFadeSamples = 256;
    }

    void updateParams (const GranularParams& p) { params = p; }
    bool isPlaying() const { return playing; }
    bool isGateActive() const { return gate; }
    void releaseGate() { gate = false; }
    bool isKilling() const { return playing && killFadeSamples > 0; }

    void setSampleBuffer (std::shared_ptr<juce::AudioBuffer<float>> buf) { sample = buf; }

    // p-lock support
    bool hasPlocks = false;
    void setPlocked() { hasPlocks = true; }

    // ── Grain position readback for GUI visualization ──
    static constexpr int kMaxVisGrains = 16; // max grains to report to GUI
    struct GrainVis { float pos; float size; float amp; float pan; float pitch; bool reverse; };
    int getActiveGrainPositions (GrainVis* out, int maxOut) const
    {
        int count = 0;
        for (int i = 0; i < kMaxGrains && count < maxOut; ++i)
        {
            if (grains[i].active)
            {
                float grainPhase = static_cast<float>(grains[i].phase / grains[i].sizeSamples);
                float window = applyWindow (std::clamp (grainPhase, 0.0f, 1.0f), params.shape, params.tilt);
                double readPos = grains[i].startSample + grains[i].phase * grains[i].pitchRatio;
                int smpLen = sample ? sample->getNumSamples() : 1;
                float normPos = static_cast<float>(std::fmod (readPos, smpLen)) / std::max (1, smpLen);
                if (normPos < 0) normPos += 1.0f;
                out[count++] = { normPos, static_cast<float>(grains[i].sizeSamples / sampleRate), window,
                                 grains[i].panR - grains[i].panL, grains[i].pitchRatio, grains[i].reverse };
            }
        }
        return count;
    }

    void renderBlock (float* outL, float* outR, int numSamples)
    {
        if (!playing || !sample || sample->getNumSamples() < 64)
            return;

        const float* smpData = sample->getReadPointer (0);
        const int smpLen = sample->getNumSamples();
        const float fdt = 1.0f / static_cast<float>(sampleRate);
        const float grainSizeSec = std::clamp (params.grainSize, 5.0f, 500.0f) / 1000.0f;
        const float baseInterval = 1.0f / std::max (1.0f, params.density);

        for (int i = 0; i < numSamples; ++i)
        {
            // Gate timing
            gateTimer += fdt;
            if (gate && gateTimer >= gateDur)
            {
                gate = false;
                ampStage = 3; // release
                filtStage = 3;
            }

            // ── Scan position auto-movement (Granulator II style) ──
            if (!params.freeze)
            {
                // scan: -1..+1 maps to speed, 0 = no movement
                // At |scan|=1, traverse full sample in ~2 seconds
                float scanSpeed = params.scan * 0.5f * fdt;
                scanPos += scanSpeed;
                // Wrap around
                if (scanPos > 1.0f) scanPos -= 1.0f;
                if (scanPos < 0.0f) scanPos += 1.0f;

                // Also follow position knob (blend toward it)
                // This lets you move the POS knob and hear the change
                scanPos += (params.position - scanPos) * 0.001f;
            }

            // ── FM Modulator (same architecture as AnalogVoice) ──
            if (params.fmAmt > 0.01f)
            {
                float ratio = params.fmRatio;
                if (params.fmSnap > 0)
                    ratio = std::round (ratio);
                float carrierHz = 261.63f * baseFreqRatio; // C4 base
                float modFreq = carrierHz * ratio;
                float modSig = std::sin (static_cast<float>(fmPhase * 6.283185307));
                fmPhase += static_cast<double>(modFreq) * static_cast<double>(fdt);
                if (fmPhase >= 1.0) fmPhase -= 1.0;
                // FM envelope: exponential decay to sustain
                float sus = std::clamp (params.fmSustain, 0.0f, 1.0f);
                float fmEnvTarget = sus + (1.0f - sus) * std::exp (-gateTimer
                    / std::max (0.01f, params.fmDecay));
                fmEnvVal = fmEnvTarget;
                // Exponential curve: subtle at low values, strong at high — more usable range
                float normAmt = params.fmAmt / 100.0f;
                float targetAmt = normAmt * normAmt * 0.4f; // quadratic: 10%→0.004, 50%→0.1, 100%→0.4
                fmAmtSmooth += (targetAmt - fmAmtSmooth) * 0.002f;
                fmPhaseMod = modSig * fmAmtSmooth * fmEnvVal;
            }
            else
            {
                fmAmtSmooth *= 0.99f;
                if (fmAmtSmooth > 0.001f)
                    fmPhaseMod *= 0.99f;
                else
                    fmPhaseMod = 0.0f;
            }

            // ── Spawn new grains ──
            grainSpawnCounter += fdt;
            // Add 10% jitter to spawn interval for organic feel
            float jitteredInterval = baseInterval * (0.9f + nextRandom() * 0.2f);
            if (grainSpawnCounter >= jitteredInterval)
            {
                grainSpawnCounter -= jitteredInterval;
                spawnGrain (smpLen, grainSizeSec);
            }

            // ── Render all active grains ──
            float sumL = 0.0f, sumR = 0.0f;
            for (auto& g : grains)
            {
                if (!g.active) continue;
                ++g.age;

                // Grain phase (0-1 within grain)
                float grainPhase = static_cast<float>(g.phase / g.sizeSamples);
                if (grainPhase >= 1.0f || grainPhase < 0.0f) { g.active = false; continue; }

                // Window envelope
                float window = applyWindow (grainPhase, params.shape, params.tilt);

                // ── Read sample with cubic Hermite interpolation ──
                // FM modulates pitch per-grain: spray controls FM depth variation
                float effectivePitch = g.pitchRatio * (1.0f + fmPhaseMod * g.fmSpread);
                double readPos = g.startSample + g.phase * static_cast<double>(effectivePitch);
                // Wrap position
                while (readPos >= smpLen) readPos -= smpLen;
                while (readPos < 0) readPos += smpLen;

                float smp = cubicHermite (smpData, smpLen, readPos);

                // Texture: formant preservation — shift the window independently of pitch
                if (params.texture > 0.01f)
                {
                    float texturePhase = std::fmod (grainPhase * (1.0f + params.texture * 2.0f), 1.0f);
                    float textureWindow = applyWindow (texturePhase, params.shape, params.tilt);
                    smp *= juce::jmap (params.texture, window, textureWindow);
                }
                else
                {
                    smp *= window;
                }

                sumL += smp * g.panL;
                sumR += smp * g.panR;

                // Advance grain
                g.phase += (g.reverse ? -1.0 : 1.0);
                if (g.reverse && g.phase <= 0.0) g.active = false;
            }

            // Normalize by ~sqrt(expectedOverlap) to prevent loudness scaling with density
            float expectedOverlap = std::max (1.0f, params.density * grainSizeSec);
            float normFactor = 1.0f / std::sqrt (expectedOverlap);
            sumL *= normFactor;
            sumR *= normFactor;

            // ── Multi-model filter (CLN/ACD/DRT/SEM/ARP/LQD) ──
            filtEnvVal = runADSR (filtEnvVal, filtStage, params.fA, params.fD, params.fS, params.fR, fdt);
            float cutHz = 20.0f * std::pow (2.0f, (params.cut * 0.01f) * 10.0f);
            float envOctaves = params.fenv * filtEnvVal * 7.0f;
            cutHz *= std::pow (2.0f, envOctaves);
            cutHz = std::clamp (cutHz, 16.0f, std::min (18000.0f, static_cast<float>(sampleRate) * 0.45f));

            sumL = filterL.process (sumL, cutHz, params.res, params.fModel, params.fType, params.fPoles,
                                     static_cast<float>(sampleRate));
            sumR = filterR.process (sumR, cutHz, params.res, params.fModel, params.fType, params.fPoles,
                                     static_cast<float>(sampleRate));

            // ── Amp envelope ──
            ampEnvVal = runADSR (ampEnvVal, ampStage, params.aA, params.aD, params.aS, params.aR, fdt);

            float killGain = 1.0f;
            if (killFadeSamples > 0)
            {
                killGain = static_cast<float>(killFadeSamples) / 256.0f;
                --killFadeSamples;
                if (killFadeSamples == 0) { playing = false; return; }
            }

            float gain = ampEnvVal * velocity * params.volume * killGain;

            // tanh soft clip to prevent harsh peaks from dense clouds
            float outSigL = std::tanh (sumL * gain);
            float outSigR = std::tanh (sumR * gain);

            // ── Feedback: mix output back into grain input ──
            if (params.feedback > 0.01f)
            {
                float fb = std::clamp (params.feedback, 0.0f, 0.95f); // cap to prevent runaway
                fbBufL[fbWritePos] = outSigL;
                fbBufR[fbWritePos] = outSigR;
                // Read from feedback buffer (delayed by ~1ms for stability)
                int fbDelay = std::max (32, static_cast<int>(sampleRate * 0.001));
                int fbReadPos = (fbWritePos - fbDelay + kFeedbackBufSize) % kFeedbackBufSize;
                outSigL += fbBufL[fbReadPos] * fb;
                outSigR += fbBufR[fbReadPos] * fb;
                // Soft clip feedback to prevent explosion
                outSigL = std::tanh (outSigL);
                outSigR = std::tanh (outSigR);
                fbWritePos = (fbWritePos + 1) % kFeedbackBufSize;
            }

            outL[i] += outSigL;
            outR[i] += outSigR;

            // Check if envelope has fully released
            if (!gate && ampStage == 3 && ampEnvVal < 0.0001f)
            {
                playing = false;
                return;
            }
        }
    }

private:
    static constexpr int kMaxGrains = 48;

    struct Grain
    {
        bool   active = false;
        double startSample = 0.0;
        double phase = 0.0;
        double sizeSamples = 4410.0;
        float  pitchRatio = 1.0f;
        float  panL = 0.707f, panR = 0.707f;
        float  fmSpread = 1.0f;  // per-grain FM depth multiplier (0..2, controlled by spray)
        bool   reverse = false;
        uint64_t age = 0;
    };

    std::array<Grain, kMaxGrains> grains;
    double sampleRate = 44100.0;
    GranularParams params;
    float velocity = 1.0f;
    bool playing = false, gate = false;
    float gateDur = 1.0f, gateTimer = 0.0f;
    float baseFreqRatio = 1.0f;
    double grainSpawnCounter = 0.0;
    uint64_t globalAge = 0;
    float scanPos = 0.5f;
    uint64_t grainCounter = 0;

    // Feedback buffer
    static constexpr int kFeedbackBufSize = 4096;
    float fbBufL[kFeedbackBufSize] {};
    float fbBufR[kFeedbackBufSize] {};
    int fbWritePos = 0;
    std::shared_ptr<juce::AudioBuffer<float>> sample;

    float ampEnvVal = 0.0f; int ampStage = 0;
    float filtEnvVal = 0.0f; int filtStage = 0;
    int killFadeSamples = 0;

    // Multi-model filter (6 models: CLN/ACD/DRT/SEM/ARP/LQD)
    MultiModelFilterCh filterL, filterR;

    // FM modulator state
    double fmPhase = 0.0;
    float  fmEnvVal = 1.0f;
    float  fmAmtSmooth = 0.0f;
    float  fmPhaseMod = 0.0f;  // current pitch modulation amount

    uint32_t rngState = 12345;
    float nextRandom()
    {
        rngState = rngState * 1664525u + 1013904223u;
        return static_cast<float>(rngState >> 8) / 16777216.0f;
    }
    float nextRandomBipolar() { return nextRandom() * 2.0f - 1.0f; }

    // ── Cubic Hermite interpolation ──
    static float cubicHermite (const float* data, int len, double pos)
    {
        int i1 = static_cast<int>(pos);
        float frac = static_cast<float>(pos - i1);

        auto wrap = [len](int idx) -> int {
            while (idx < 0) idx += len;
            return idx % len;
        };

        float y0 = data[wrap (i1 - 1)];
        float y1 = data[wrap (i1)];
        float y2 = data[wrap (i1 + 1)];
        float y3 = data[wrap (i1 + 2)];

        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    void spawnGrain (int smpLen, float grainSizeSec)
    {
        int uniCount = std::clamp (params.uniVoices, 1, 8);
        float detuneRange = params.uniDetune; // cents

        for (int u = 0; u < uniCount; ++u)
        {
            // Find inactive grain slot OR steal oldest
            int slot = -1;
            uint64_t oldestAge = 0;
            int oldestSlot = 0;
            for (int j = 0; j < kMaxGrains; ++j)
            {
                if (!grains[j].active) { slot = j; break; }
                if (grains[j].age > oldestAge) { oldestAge = grains[j].age; oldestSlot = j; }
            }
            if (slot < 0) slot = oldestSlot;

            auto& g = grains[slot];
            g.active = true;
            g.age = 0;

            // ── MODE-dependent position + size ──
            float localSize = grainSizeSec;
            float pos = scanPos;

            switch (params.mode)
            {
                case 0: // STRD
                    pos += nextRandomBipolar() * params.spray * 0.3f;
                    break;
                case 1: // CLOUD — fully random pos, random size ±50%
                    pos = nextRandom();
                    localSize *= (0.5f + nextRandom());
                    break;
                case 2: // SCRUB — locked to POS, no spray
                    pos = params.position;
                    break;
                case 3: // GLTCH — random pos, random size 5-200ms
                    pos = nextRandom();
                    localSize = (5.0f + nextRandom() * 195.0f) / 1000.0f;
                    break;
                case 4: // FLUX — slow drift + light spray
                {
                    float drift = std::sin (static_cast<float>(grainCounter) * 0.017f) * 0.15f;
                    pos += drift + nextRandomBipolar() * 0.05f;
                    localSize *= (0.8f + nextRandom() * 0.4f);
                    break;
                }
                default:
                    pos += nextRandomBipolar() * params.spray * 0.3f;
                    break;
            }

            pos = std::clamp (pos, 0.0f, 0.9999f);
            g.startSample = pos * (smpLen - 1);
            g.sizeSamples = static_cast<double>(localSize * sampleRate);

            // ── Pitch + UNISON detune + QUANTIZE ──
            float pitchOffset = nextRandomBipolar() * params.pitchSpread;

            if (uniCount > 1)
            {
                float uniPos = (static_cast<float>(u) / static_cast<float>(uniCount - 1)) * 2.0f - 1.0f;
                pitchOffset += uniPos * detuneRange / 100.0f;
            }

            if (params.quantize > 0 && std::abs (pitchOffset) > 0.01f)
            {
                auto snapNearest = [](float val, const float* iv, int n) {
                    float best = 0; float bestD = 999;
                    for (int k = 0; k < n; ++k)
                    { float d = std::abs (val - iv[k]); if (d < bestD) { bestD = d; best = iv[k]; } }
                    return best;
                };
                switch (params.quantize)
                {
                    case 1: pitchOffset = std::round (pitchOffset / 12.0f) * 12.0f; break; // OCT
                    case 2: { static const float iv[] = {0,7,12,-5,-12}; pitchOffset = snapNearest (pitchOffset, iv, 5); break; } // 5TH
                    case 3: { static const float iv[] = {0,4,7,12,-5,-8,-12}; pitchOffset = snapNearest (pitchOffset, iv, 7); break; } // TRAD
                    case 4: pitchOffset = std::round (pitchOffset); break; // SCAL
                }
            }

            g.pitchRatio = baseFreqRatio * std::pow (2.0f, pitchOffset / 12.0f);

            // Direction
            g.reverse = (params.direction == 1) || (params.direction == 2 && nextRandom() > 0.5f);
            g.phase = g.reverse ? g.sizeSamples - 1 : 0.0;

            // Pan — random spread from panSpread + deterministic unison stereo spread
            float panRnd = nextRandomBipolar() * params.panSpread;
            if (uniCount > 1 && params.uniStereo > 0.001f)
            {
                float uniPan = (static_cast<float>(u) / static_cast<float>(uniCount - 1)) * 2.0f - 1.0f;
                panRnd += uniPan * params.uniStereo;
            }
            float pan = std::clamp (0.5f + panRnd * 0.5f, 0.0f, 1.0f);
            g.panL = std::cos (pan * 1.5707963f);
            g.panR = std::sin (pan * 1.5707963f);

            g.fmSpread = 1.0f + nextRandomBipolar() * params.fmSpread;
        }
        grainCounter++;
    }

    static float applyWindow (float phase, int shape, float tilt = 50.0f)
    {
        phase = std::clamp (phase, 0.0f, 1.0f);
        // Apply TILT: shift the envelope peak position
        // tilt=0: peak at start (percussive), tilt=50: center, tilt=100: peak at end (swell)
        float tiltNorm = std::clamp (tilt, 0.0f, 100.0f) / 100.0f;
        if (std::abs (tiltNorm - 0.5f) > 0.01f)
        {
            // Remap phase through a power curve to shift the peak
            float power = (tiltNorm < 0.5f)
                ? 0.2f + tiltNorm * 1.6f   // 0→0.2 (fast attack), 0.5→1.0
                : 1.0f + (tiltNorm - 0.5f) * 4.0f; // 0.5→1.0, 1.0→3.0 (slow attack)
            phase = std::pow (phase, power);
        }
        switch (shape)
        {
            case 0: return 0.5f * (1.0f - std::cos (phase * 6.2831853f));  // Hann
            case 1: return 1.0f - 2.0f * std::abs (phase - 0.5f);         // Triangle
            case 2: // Rectangle (cosine fade edges)
            {
                constexpr float fade = 0.02f;
                if (phase < fade) return 0.5f * (1.0f - std::cos (phase / fade * 3.14159f));
                if (phase > 1.0f - fade) return 0.5f * (1.0f - std::cos ((1.0f - phase) / fade * 3.14159f));
                return 1.0f;
            }
            case 3: // Tukey (25% edges)
            {
                constexpr float edge = 0.25f;
                if (phase < edge) return 0.5f * (1.0f - std::cos (phase / edge * 3.14159f));
                if (phase > 1.0f - edge) return 0.5f * (1.0f - std::cos ((1.0f - phase) / edge * 3.14159f));
                return 1.0f;
            }
            case 4: // Gaussian
            {
                float x = (phase - 0.5f) / 0.4f;
                return std::exp (-0.5f * x * x);
            }
            case 5: // SAW (descending — percussive)
            {
                float saw = 1.0f - phase;
                // Soft fade at very end to prevent click
                if (phase > 0.95f) saw *= (1.0f - phase) / 0.05f;
                return saw;
            }
            default: return 0.5f * (1.0f - std::cos (phase * 6.2831853f));
        }
    }

    float runADSR (float val, int& stage, float a, float d, float s, float r, float dt)
    {
        switch (stage)
        {
            case 0: val += dt / std::max (0.001f, a);
                    if (val >= 1.0f) { val = 1.0f; stage = 1; } break;
            case 1: val -= dt / std::max (0.001f, d) * (1.0f - s);
                    if (val <= s) { val = s; stage = 2; } break;
            case 2: break;
            case 3: val -= dt / std::max (0.001f, r) * val;
                    if (val < 0.0001f) val = 0.0f; break;
        }
        return val;
    }
};
