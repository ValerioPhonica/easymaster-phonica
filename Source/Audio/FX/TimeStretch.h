#pragma once
#include <cmath>
#include <algorithm>
#include <vector>

// ═══════════════════════════════════════════════════════════════════
// TimeStretch — High-quality time manipulation for DAW sync
//
// BEATS (1):  WSOLA 50ms grains, waveform-correlated splice, preserves transients
// TEXTURE (2): WSOLA 80ms grains, heavy overlap, smooth granular smearing
// REPITCH (3): Variable-speed playback, pitch follows time (like tape)
//
// KEY DESIGN: When stretchFactor ≈ 1.0 AND pitchRatio ≈ 1.0 (±2%),
// the engine BYPASSES granular processing and plays raw audio.
// This ensures "identical to original when BPM matches" — like Ableton.
//
// ANTI-DRIFT: nominalSourcePos is periodically re-anchored to prevent
// accumulated float errors from causing audible timing drift over
// long playback. This is critical for perfect grid sync.
//
// DAW SYNC: SamplerVoice tracks output count and calls forceRestart()
// at bar boundaries. forceRestart uses a micro-crossfade to avoid clicks.
// ═══════════════════════════════════════════════════════════════════

class TimeStretch
{
public:
    void prepare (float sr)
    {
        sampleRate = sr;
        reset();
    }

    void setBuffer (const float* data, int numSamples)
    {
        bufData = data;
        bufLen = numSamples;
        reset();
    }

    void reset()
    {
        readPos = 0.0;
        grainPhase = 0;
        crossfadePhase = 0;
        prevGrainStart = 0;
        nextGrainStart = 0;
        nominalSourcePos = 0.0;
        inCrossfade = false;
        finished = false;
        outputCount = 0;
        grainCount = 0;
        restartFadeCount = 0;
        restartFadeLen = 0;
        prevRestartSample = 0.0f;
        lastOutputSample = 0.0f;
        repitchLPState = 0.0f;
    }

    // mode: 1=BEATS, 2=TEXTURE, 3=REPITCH, 4=BEATS2 (transient-slicing)
    void setMode (int m) { mode = std::clamp (m, 1, 4); updateGrainParams(); }

    // stretch: >1 = slower (longer output), <1 = faster (shorter output)
    // pitchRatio: playback pitch multiplier (1.0 = original pitch)
    void setParams (float stretch, float pr)
    {
        stretchFactor = std::clamp (stretch, 0.1f, 8.0f);
        pitchRatio = std::clamp (pr, 0.25f, 4.0f);

        // Bypass detection: if stretch ≈ 1.0 AND pitch ≈ 1.0, skip WSOLA
        bool nearUnityStretch = (stretchFactor > 0.98f && stretchFactor < 1.02f);
        bool nearUnityPitch   = (pitchRatio > 0.98f && pitchRatio < 1.02f);
        bypass = nearUnityStretch && nearUnityPitch && (mode != 3) && (mode != 4);

        updateGrainParams();
    }

    // ── Warp markers: per-segment variable stretch ──
    // Each marker maps a normalized sample position (0-1) to a beat position.
    // Between markers, the local stretch ratio is adjusted so transients
    // align exactly with beats. Must be sorted by samplePos.
    struct WarpPt { float samplePos; float beatPos; };

    void setWarpMarkers (const std::vector<WarpPt>& markers, float totalBeats)
    {
        warpPts = markers;
        warpTotalBeats = totalBeats;
        useWarp = (markers.size() >= 2 && totalBeats > 0.0f);
        // If warp markers active, MUST disable bypass (even at unity stretch)
        if (useWarp) bypass = false;
        // CRITICAL: recalculate grain params — they may have been set to bypass values
        updateGrainParams();
    }

    void clearWarpMarkers()
    {
        warpPts.clear();
        useWarp = false;
        // Re-evaluate bypass (may have been disabled for warp)
        bool nearUnityStretch = (stretchFactor > 0.98f && stretchFactor < 1.02f);
        bool nearUnityPitch   = (pitchRatio > 0.98f && pitchRatio < 1.02f);
        bypass = nearUnityStretch && nearUnityPitch && (mode != 3) && (mode != 4);
        updateGrainParams();
    }

    void setRegion (int start, int end, bool loop)
    {
        startSample = std::max (0, start);
        endSample = std::min (bufLen, end);
        looping = loop;
        readPos = startSample;
        prevGrainStart = startSample;
        nextGrainStart = startSample + std::max (1, hopIn);
        nominalSourcePos = static_cast<double>(startSample);
        grainPhase = 0;
        crossfadePhase = 0;
        inCrossfade = false;
        finished = false;
        outputCount = 0;
        grainCount = 0;
    }

    void setReversed (bool rev) { reversed = rev; }

    // Force-restart with micro-crossfade to avoid clicks at loop boundaries
    void forceRestart()
    {
        prevRestartSample = lastOutputSample;
        restartFadeLen = std::max (32, std::min (128, static_cast<int>(sampleRate * 0.002f)));
        restartFadeCount = restartFadeLen;

        readPos = static_cast<double>(startSample);
        prevGrainStart = startSample;
        nextGrainStart = startSample + std::max (1, hopIn);
        nominalSourcePos = static_cast<double>(startSample);
        grainPhase = 0;
        crossfadePhase = 0;
        inCrossfade = false;
        finished = false;
        outputCount = 0;
        grainCount = 0;
    }

    // Per-sample pitch modulation (FM). Call BEFORE getNextSample().
    void setPitchMod (float mod) { currentPitchMod = mod; }

    float getNextSample()
    {
        if (bufData == nullptr || bufLen < 2 || finished) return 0.0f;

        float sample;

        if (bypass)
            sample = getNextSampleBypass();
        else if (mode == 3)
            sample = getNextSampleRepitch();
        else if (mode == 4)
            sample = getNextSampleBeats2();
        else
            sample = getNextSampleWSOLA();

        // Apply restart micro-crossfade
        if (restartFadeCount > 0)
        {
            float t = static_cast<float>(restartFadeCount) / static_cast<float>(restartFadeLen);
            // Hanning fade for smoother transition
            float alpha = 0.5f * (1.0f + std::cos (t * 3.14159265f));
            sample = sample * alpha + prevRestartSample * (1.0f - alpha);
            restartFadeCount--;
        }

        lastOutputSample = sample;
        return sample;
    }

    bool isFinished() const { return !looping && finished; }
    double getReadPos() const { return readPos; }
    int getOutputCount() const { return outputCount; }

private:
    const float* bufData = nullptr;
    int bufLen = 0;
    float sampleRate = 44100.0f;

    float stretchFactor = 1.0f;
    float pitchRatio = 1.0f;
    float currentPitchMod = 0.0f;
    int mode = 1;
    bool bypass = false;

    int grainSize = 2048;
    int hopOut = 1024;
    int hopIn = 1024;
    int crossfadeLen = 256;
    int searchRange = 128;
    int grainPhase = 0;
    int grainCount = 0;

    double readPos = 0.0;
    int prevGrainStart = 0;
    int nextGrainStart = 0;
    double nominalSourcePos = 0.0;
    int outputCount = 0;

    bool inCrossfade = false;
    int crossfadePhase = 0;
    double crossfadeOldPos = 0.0;

    // Restart crossfade (anti-click at DAW sync loop points)
    int restartFadeCount = 0;
    int restartFadeLen = 0;
    float prevRestartSample = 0.0f;
    float lastOutputSample = 0.0f;
    float repitchLPState = 0.0f;  // anti-alias filter state for REPITCH mode
    uint32_t rngState = 12345u;   // simple LCG state for TEXTURE jitter

    int startSample = 0;
    int endSample = 0;
    bool looping = false;
    bool reversed = false;
    bool finished = false;

    // ── Warp markers for per-segment variable stretch ──
    std::vector<WarpPt> warpPts;
    float warpTotalBeats = 0.0f;
    bool useWarp = false;

    // Compute local stretch factor at a given normalized position (0-1) in the sample.
    // Returns the global stretchFactor adjusted by the warp marker spacing.
    float getLocalStretch (float normPos) const
    {
        if (!useWarp || warpPts.size() < 2) return stretchFactor;

        // Find the segment containing normPos
        size_t seg = 0;
        for (size_t i = 1; i < warpPts.size(); ++i)
        {
            if (warpPts[i].samplePos >= normPos) { seg = i - 1; break; }
            seg = i - 1;
        }
        if (seg >= warpPts.size() - 1) seg = warpPts.size() - 2;

        float sA = warpPts[seg].samplePos;
        float sB = warpPts[seg + 1].samplePos;
        float bA = warpPts[seg].beatPos;
        float bB = warpPts[seg + 1].beatPos;

        float smpFrac = sB - sA;
        float beatFrac = (bB - bA) / warpTotalBeats;

        if (smpFrac < 0.0001f || beatFrac < 0.0001f) return stretchFactor;

        // Local adjustment: if this segment's beat fraction is larger than
        // its sample fraction, it needs MORE stretch (slower); if smaller, LESS.
        // localStretch = globalStretch * (beatFraction / sampleFraction)
        return stretchFactor * (beatFrac / smpFrac);
    }

    // ═══ BYPASS: direct 1:1 playback — sounds IDENTICAL to original ═══
    float getNextSampleBypass()
    {
        float sample = readCubic (readPos);
        readPos += 1.0;
        outputCount++;

        if (endSample > startSample)
        {
            if (readPos >= endSample)
            {
                if (looping) readPos -= static_cast<double>(endSample - startSample);
                else finished = true;
            }
        }
        return sample;
    }

    // ═══ REPITCH: variable-speed, pitch follows time (tape mode) ═══
    // Includes soft anti-alias LP filter when speed > 1.0 to reduce aliasing
    float getNextSampleRepitch()
    {
        float speed = pitchRatio / std::max (0.01f, stretchFactor);
        speed *= (1.0f + currentPitchMod);

        float sample = readCubic (readPos);

        // Anti-alias: 1-pole LP at Nyquist/speed when speeding up
        float absSpeed = std::abs (speed);
        if (absSpeed > 1.05f)
        {
            float cutoff = 0.95f / absSpeed; // normalized cutoff < 1.0
            cutoff = std::clamp (cutoff, 0.1f, 0.99f);
            float alpha = cutoff / (cutoff + 1.0f); // 1-pole coefficient
            sample = repitchLPState + alpha * (sample - repitchLPState);
            repitchLPState = sample;
        }
        else
        {
            repitchLPState = sample; // track signal when no filtering needed
        }

        readPos += static_cast<double>(speed);
        outputCount++;

        if (endSample > startSample)
        {
            double regionLen = static_cast<double>(endSample - startSample);
            if (readPos >= endSample)
            {
                if (looping) readPos -= regionLen;
                else finished = true;
            }
            else if (readPos < startSample)
            {
                if (looping) readPos += regionLen;
                else finished = true;
            }
        }
        return sample;
    }

    // ═══ BEATS2: Ableton Beats-style transient slicing ═══
    //
    // HOW IT WORKS (identical concept to Ableton Live "Beats" mode):
    // 1. Audio is divided into SLICES at warp marker positions
    // 2. Each slice plays at ORIGINAL SPEED (1:1) — transients stay intact
    // 3. pitchRatio resamples each slice → independent pitch control
    // 4. When stretched (slower): slices end early, silence/fade fills the gap
    // 5. When compressed (faster): slices are truncated to fit
    // 6. 64-sample crossfade at slice boundaries prevents clicks
    //
    // Falls back to WSOLA if no warp markers present.
    float getNextSampleBeats2()
    {
        // Need at least 3 markers for slicing (start + internal + end)
        // With only 2 markers (start+end) = uniform stretch = use WSOLA
        if (!useWarp || warpPts.size() < 3)
            return getNextSampleWSOLA();

        int regionLen = (endSample > startSample) ? (endSample - startSample) : bufLen;
        if (regionLen < 2) { finished = true; return 0.0f; }

        // Total output length in samples (determined by stretch factor)
        double totalOutputLen = static_cast<double>(regionLen) * static_cast<double>(stretchFactor);
        if (totalOutputLen < 1.0) { finished = true; return 0.0f; }

        // Current output position as fraction of total output (0→1)
        double outFrac = static_cast<double>(outputCount) / totalOutputLen;
        if (outFrac >= 1.0)
        {
            if (looping) { outputCount = 0; outFrac = 0.0; }
            else { finished = true; return 0.0f; }
        }

        // Map output fraction → beat position
        float currentBeat = static_cast<float>(outFrac) * warpTotalBeats;

        // Find which slice we're in (between which pair of warp markers)
        size_t seg = 0;
        for (size_t i = 1; i < warpPts.size(); ++i)
        {
            if (currentBeat < warpPts[i].beatPos) { seg = i - 1; break; }
            seg = i - 1;
        }
        if (seg >= warpPts.size() - 1) seg = warpPts.size() - 2;

        // Slice boundaries (in beats and in normalized sample position)
        float beatA = warpPts[seg].beatPos;
        float beatB = warpPts[seg + 1].beatPos;
        float smpA  = warpPts[seg].samplePos;     // 0-1 normalized
        float smpB  = warpPts[seg + 1].samplePos;  // 0-1 normalized

        float sliceBeatLen = beatB - beatA;
        float sliceSmpLen  = smpB - smpA;

        if (sliceBeatLen < 0.0001f || std::abs (sliceSmpLen) < 0.0001f)
        {
            outputCount++;
            return 0.0f;
        }

        // How far are we into this slice's OUTPUT time window? (0→1)
        float sliceProgress = (currentBeat - beatA) / sliceBeatLen;

        // The slice's output duration in samples
        double sliceOutputSamples = (static_cast<double>(sliceBeatLen) / static_cast<double>(warpTotalBeats)) * totalOutputLen;

        // The slice's SOURCE duration in samples
        double sliceSourceSamples = static_cast<double>(sliceSmpLen) * static_cast<double>(regionLen);

        // How many source samples have we consumed in this slice?
        // Each output sample advances by pitchRatio source samples (for pitch shift)
        // At 1:1 speed, we consume 1 source sample per output sample
        float effectivePitch = std::abs (pitchRatio * (1.0f + currentPitchMod));
        if (effectivePitch < 0.01f) effectivePitch = 0.01f;

        // Source position within the slice
        // sliceProgress * sliceOutputSamples = how many output samples into this slice
        // Each output sample advances effectivePitch source samples
        double srcAdvance = static_cast<double>(sliceProgress) * sliceOutputSamples * static_cast<double>(effectivePitch);

        // If pitch > 1, source consumed faster than slice window → loop within slice
        if (srcAdvance >= sliceSourceSamples && sliceSourceSamples > 1.0)
            srcAdvance = std::fmod (srcAdvance, sliceSourceSamples);

        // Map to absolute source position
        double srcNorm = static_cast<double>(smpA) + srcAdvance / static_cast<double>(regionLen);
        double sourcePos = static_cast<double>(startSample) + srcNorm * static_cast<double>(regionLen);
        sourcePos = std::clamp (sourcePos, static_cast<double>(startSample), static_cast<double>(endSample - 1));

        float sample = readCubic (sourcePos);

        // Crossfade at slice boundaries (64 samples)
        float fadeGain = 1.0f;
        int xfLen = 64;
        int samplesIntoSlice = static_cast<int>(sliceProgress * sliceOutputSamples);
        int samplesLeftInSlice = static_cast<int>(sliceOutputSamples) - samplesIntoSlice;

        // Fade in at start of slice
        if (samplesIntoSlice < xfLen)
            fadeGain = static_cast<float>(samplesIntoSlice) / static_cast<float>(xfLen);
        // Fade out near end of source material within slice
        double srcRemaining = sliceSourceSamples - srcAdvance;
        if (srcRemaining < static_cast<double>(xfLen))
            fadeGain *= static_cast<float>(srcRemaining / static_cast<double>(xfLen));

        fadeGain = std::clamp (fadeGain, 0.0f, 1.0f);
        sample *= fadeGain;

        readPos = sourcePos;
        outputCount++;
        lastOutputSample = sample;
        return sample;
    }

    // ═══ WSOLA: Waveform Similarity Overlap-Add ═══
    // TRUE OLA: grains overlap by crossfadeLen samples.
    // Grain plays for hopOut samples at full level, then crossfades
    // for crossfadeLen samples with the next grain → constant energy.
    float getNextSampleWSOLA()
    {
        float effectivePitch = pitchRatio * (1.0f + currentPitchMod);

        float sample = readCubic (readPos);

        // ── Crossfade: blend old grain tail with new grain head ──
        if (inCrossfade && crossfadeLen > 0)
        {
            float t = static_cast<float>(crossfadePhase) / static_cast<float>(crossfadeLen);
            // Hanning power-complementary: fadeOut² + fadeIn² ≈ 1
            float fadeOut = 0.5f * (1.0f + std::cos (t * 3.14159265f));
            float fadeIn  = 1.0f - fadeOut;

            float oldSample = readCubic (crossfadeOldPos);
            sample = oldSample * fadeOut + sample * fadeIn;

            crossfadeOldPos += static_cast<double>(effectivePitch);
            crossfadePhase++;
            if (crossfadePhase >= crossfadeLen)
                inCrossfade = false;
        }

        readPos += static_cast<double>(effectivePitch);
        outputCount++;

        // ── Sample-accurate loop: reset when output reaches expected length ──
        // Without this, WSOLA loops based on source position (grain-aligned = imprecise)
        {
            int regionLen = (endSample > startSample) ? (endSample - startSample) : bufLen;
            int expectedOut = static_cast<int>(static_cast<double>(regionLen) * static_cast<double>(stretchFactor));
            if (expectedOut > 0 && outputCount >= expectedOut)
            {
                if (looping)
                {
                    outputCount = 0;
                    readPos = static_cast<double>(startSample);
                    nominalSourcePos = static_cast<double>(startSample);
                    grainPhase = 0;
                    grainCount = 0;
                    prevGrainStart = startSample;
                    nextGrainStart = startSample;
                    // Micro-crossfade for click-free loop restart
                    prevRestartSample = sample;
                    restartFadeCount = restartFadeLen;
                }
                else { finished = true; }
                return sample;
            }
        }

        // ── Grain boundary: trigger at hopOut (NOT grainSize!) ──
        // This ensures grains OVERLAP by crossfadeLen samples,
        // creating true overlap-add with constant energy output.
        grainPhase++;
        if (grainPhase >= hopOut && hopOut > 0)
        {
            grainPhase = 0;
            grainCount++;

            // Save old grain's current position for crossfade tail
            crossfadeOldPos = readPos;
            crossfadePhase = 0;
            inCrossfade = true;

            // ── Compute hopIn: use local stretch from warp markers ──
            int regionLen = (endSample > startSample) ? (endSample - startSample) : bufLen;
            float normPos = (regionLen > 0)
                ? static_cast<float>(nominalSourcePos - startSample) / static_cast<float>(regionLen)
                : 0.0f;
            float localStretch = getLocalStretch (std::clamp (normPos, 0.0f, 1.0f));
            int localHopIn = std::max (1, static_cast<int>(std::round (
                static_cast<float>(hopOut) / localStretch)));

            nominalSourcePos += static_cast<double>(localHopIn);

            // ── ANTI-DRIFT: gradual re-anchoring every 8 grains ──
            // Uses smooth correction (50%) instead of hard snap to avoid audible jumps
            if (!useWarp && (grainCount & 7) == 0)
            {
                int globalHopIn = std::max (1, static_cast<int>(std::round (
                    static_cast<float>(hopOut) / stretchFactor)));
                double expectedSourcePos = static_cast<double>(startSample)
                    + static_cast<double>(grainCount) * static_cast<double>(globalHopIn);
                double drift = nominalSourcePos - expectedSourcePos;
                if (std::abs (drift) > 2.0)
                    nominalSourcePos -= drift * 0.5; // correct 50% — smooth convergence
            }

            int candidateStart = static_cast<int>(std::round (nominalSourcePos));

            // TEXTURE mode: tiny random jitter (breaks periodic artifacts)
            if (mode == 2)
            {
                int jitterRange = static_cast<int>(sampleRate * 0.002f); // ±2ms (subtle, preserves timing)
                rngState = rngState * 1664525u + 1013904223u;
                int jitter = static_cast<int>(rngState % static_cast<uint32_t>(jitterRange * 2 + 1)) - jitterRange;
                candidateStart += jitter;
                candidateStart = std::clamp (candidateStart, startSample, std::max (startSample, endSample - crossfadeLen));
            }

            // NOTE: no splice search! The 50% overlap Hanning OLA provides
            // smooth constant-energy crossfade at ANY position. Splice search
            // caused audible timing shifts (speed fluctuation artifacts).

            prevGrainStart = nextGrainStart;
            nextGrainStart = candidateStart;
            readPos = static_cast<double>(nextGrainStart);

            if (endSample > startSample)
            {
                if (looping)
                {
                    while (nextGrainStart >= endSample)
                    {
                        nextGrainStart -= regionLen;
                        nominalSourcePos -= static_cast<double>(regionLen);
                    }
                    while (nextGrainStart < startSample)
                    {
                        nextGrainStart += regionLen;
                        nominalSourcePos += static_cast<double>(regionLen);
                    }
                    readPos = static_cast<double>(nextGrainStart);
                }
                else if (readPos >= endSample)
                {
                    finished = true;
                    return sample;
                }
            }
        }
        return sample;
    }

    void updateGrainParams()
    {
        if (mode == 3 || bypass)
        {
            grainSize = 1; crossfadeLen = 0; searchRange = 0;
            hopOut = 1; hopIn = 1;
            return;
        }
        if (mode == 1 || mode == 4) // BEATS + BEATS2 (BEATS2 uses these for WSOLA fallback)
        {
            grainSize = static_cast<int>(sampleRate * 0.080f);   // 80ms grain
            crossfadeLen = grainSize / 2;                         // 50% overlap (true OLA)
            searchRange = static_cast<int>(sampleRate * 0.012f);  // 12ms search (tight! preserves groove)
        }
        else // TEXTURE — smooth granular, 50% overlap + jitter
        {
            grainSize = static_cast<int>(sampleRate * 0.120f);   // 120ms grain
            crossfadeLen = grainSize / 2;                         // 50% overlap
            searchRange = static_cast<int>(sampleRate * 0.004f);  // 4ms minimal
        }
        grainSize = std::max (128, grainSize);
        crossfadeLen = std::clamp (crossfadeLen, 32, grainSize / 2);
        hopOut = grainSize - crossfadeLen;
        hopIn = std::max (1, static_cast<int>(std::round (
            static_cast<float>(hopOut) / stretchFactor)));
    }

    // ── Cubic Hermite interpolation ──
    float readCubic (double pos) const
    {
        if (reversed && endSample > startSample)
            pos = static_cast<double>(startSample + endSample - 1) - pos;

        int idx = static_cast<int>(pos);
        float frac = static_cast<float>(pos - idx);

        if (idx < 1 || idx >= bufLen - 2)
        {
            if (idx >= 0 && idx < bufLen - 1)
                return bufData[idx] * (1.0f - frac) + bufData[idx + 1] * frac;
            if (idx >= 0 && idx < bufLen)
                return bufData[idx];
            return 0.0f;
        }

        float y0 = bufData[idx - 1], y1 = bufData[idx];
        float y2 = bufData[idx + 1], y3 = bufData[idx + 2];
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + y1;
    }

    // ── Waveform-correlated splice search (BEATS mode) ──
    // Finds the best splice point near target using normalized cross-correlation.
    // The goal: find where the waveform shape MATCHES the end of the previous grain,
    // so the crossfade sounds seamless.
    int findBestSplice (int target, int range) const
    {
        if (bufData == nullptr || bufLen < 2) return target;

        int lo = std::max (startSample, target - range);
        int hi = std::min (endSample > 0 ? endSample - 1 : bufLen - 1, target + range);
        if (lo >= hi) return target;

        int best = target;
        float bestScore = -1e9f; // higher is better now

        // Correlation window: ~1.5ms at 44.1kHz = 64 samples
        int corrHalf = std::min (64, hopOut / 4);
        corrHalf = std::max (8, corrHalf);

        // Reference: where the previous grain's crossfade will read from
        int refPos = prevGrainStart + hopOut;
        if (refPos < startSample) refPos = startSample;
        if (refPos >= endSample && endSample > startSample) refPos = endSample - 1;
        bool hasRef = (refPos >= corrHalf + 1 && refPos < bufLen - corrHalf - 1);

        for (int i = lo; i <= hi; ++i)
        {
            if (i < corrHalf + 1 || i >= bufLen - corrHalf - 1) continue;

            float score = 0.0f;

            // 1) Normalized cross-correlation with reference (primary criterion)
            if (hasRef)
            {
                float sumXY = 0.0f, sumXX = 0.0f, sumYY = 0.0f;
                for (int j = -corrHalf; j <= corrHalf; ++j)
                {
                    float x = bufData[std::clamp (refPos + j, 0, bufLen - 1)];
                    float y = bufData[std::clamp (i + j, 0, bufLen - 1)];
                    sumXY += x * y;
                    sumXX += x * x;
                    sumYY += y * y;
                }
                float denom = std::sqrt (sumXX * sumYY);
                float ncc = (denom > 1e-10f) ? (sumXY / denom) : 0.0f;
                score += ncc * 10.0f; // NCC: -1 to +1, heavily weighted
            }

            // 2) Gentle zero-crossing bonus (not dominant)
            if (i > 0 && i < bufLen - 1)
            {
                if ((bufData[i - 1] >= 0 && bufData[i] < 0) ||
                    (bufData[i - 1] < 0 && bufData[i] >= 0))
                    score += 1.5f;
            }

            // 3) Low amplitude bonus (prefer quiet splice points)
            score -= std::abs (bufData[i]) * 1.0f;

            // 4) Low energy bonus (avoid transients)
            if (i > 4 && i < bufLen - 4)
            {
                float energy = 0.0f;
                for (int j = -4; j <= 4; ++j)
                    energy += bufData[i + j] * bufData[i + j];
                score -= energy * 0.02f;
            }

            // 5) Distance penalty — CRITICAL for groove preservation
            // Timing is MORE important than clean splicing. Strongly prefer target position.
            float distPenalty = static_cast<float>(std::abs (i - target)) / static_cast<float>(std::max (1, range));
            score -= distPenalty * distPenalty * 8.0f; // quadratic: penalizes far positions much more

            if (score > bestScore) { bestScore = score; best = i; }
        }
        return best;
    }
};
