#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

// ── Warp marker: maps a sample position to a beat position ──
// Enables Ableton-style per-transient timing correction.
struct WarpMarker
{
    float samplePos = 0.0f;          // 0-1 normalized position in audio file
    float beatPos   = 0.0f;          // beat position (0 = start, 4 = beat 5, etc.)
    float originalSamplePos = 0.0f;  // auto-detected position (for reset)
    bool  isAuto    = true;          // true = auto-detected, false = user-placed
};

// ═══════════════════════════════════════════════════════════════════
// SampleAnalysis — Ableton-style auto BPM and bar detection
//
// PHILOSOPHY: A properly trimmed loop has a DETERMINISTIC relationship
// between its length, beat count, and BPM. The algorithm finds the
// (beats, BPM) pair that best explains the sample length.
//
// For a sample of duration D seconds:
//   BPM = beats * 60 / D
//   bars = beats / beatsPerBar (assume 4/4)
//
// The algorithm tries ALL plausible beat counts (1..128) and scores
// each candidate based on:
//   - Is the BPM a "round" number? (120, 128, 140 > 123.7)
//   - Is the beat count a power of 2? (4,8,16 > 3,5,7)
//   - Is the BPM in the common range? (80-170 > 40-80 or 170-220)
//   - Is the bar count musically sensible? (1,2,4,8 > 13,17)
//
// Returns BOTH bpm AND barCount in a single pass — no circular dependency.
// ═══════════════════════════════════════════════════════════════════

namespace SampleAnalysis
{

// ─── Analysis result ────────────────────────────────────────────
struct AnalysisResult
{
    float bpm       = 0.0f;
    int   beats     = 0;       // total beat count (e.g. 16)
    int   bars      = 0;       // bar count (beats/4 in 4/4)
    int   barIndex  = 0;       // encoded for barLUT: 1=1/4, 2=1/2, 3=1, 4=2, 5=4, 6=8, 7=16, 8=32
    int   rootNote  = -1;
    bool  isLoop    = false;
    float confidence = 0.0f;   // 0-1 how confident the detection is
};

// ─── Score how "round" a BPM value is ───────────────────────────
static inline float bpmRoundness (float bpm)
{
    float score = 0.0f;

    // Exact integer -> big bonus
    float frac = bpm - std::floor (bpm);
    if (frac < 0.05f || frac > 0.95f) score += 3.0f;
    // Half-integer (e.g. 87.5)
    else if (std::abs (frac - 0.5f) < 0.05f) score += 1.5f;

    // Divisible by 5 (100, 105, 110, 115, 120...)
    float mod5 = std::fmod (bpm, 5.0f);
    if (mod5 < 0.3f || mod5 > 4.7f) score += 2.0f;

    // Divisible by 10
    float mod10 = std::fmod (bpm, 10.0f);
    if (mod10 < 0.3f || mod10 > 9.7f) score += 1.5f;

    // Iconic tempos: massive bonus
    float iconicTempos[] = {
        70, 75, 80, 85, 88, 90, 92, 95, 98,
        100, 105, 108, 110, 112, 115, 118, 120, 122, 124, 125,
        126, 128, 130, 132, 134, 135, 136, 138, 140, 142, 144, 145,
        148, 150, 152, 155, 158, 160, 165, 170, 172, 174, 175, 180
    };
    for (float t : iconicTempos)
    {
        float d = std::abs (bpm - t);
        if (d < 0.3f) { score += 5.0f; break; }
        if (d < 1.0f) { score += 2.0f; break; }
    }

    return score;
}

// ─── Score how "musical" a beat count is ────────────────────────
static inline float beatCountScore (int beats)
{
    if (beats == 4)   return 12.0f;  // 1 bar
    if (beats == 8)   return 11.0f;  // 2 bars
    if (beats == 16)  return 10.0f;  // 4 bars
    if (beats == 32)  return 8.0f;   // 8 bars
    if (beats == 2)   return 7.0f;   // half bar
    if (beats == 64)  return 6.0f;   // 16 bars
    if (beats == 1)   return 5.0f;   // quarter note
    if (beats == 128) return 4.0f;   // 32 bars
    if (beats % 4 == 0) return 3.5f; // even bar multiples
    if (beats == 3 || beats == 6 || beats == 12 || beats == 24) return 3.0f;
    if (beats % 2 == 0) return 2.0f;
    return 0.5f; // odd — very unlikely
}

// ─── Score BPM range likelihood ─────────────────────────────────
static inline float bpmRangeScore (float bpm)
{
    if (bpm >= 85.0f && bpm <= 165.0f) return 5.0f;
    if (bpm >= 75.0f && bpm <= 180.0f) return 3.0f;
    if (bpm >= 60.0f && bpm <= 200.0f) return 1.5f;
    return 0.2f;
}

// ─── Bar count encoding for barLUT ──────────────────────────────
// barLUT: {0, 0.25, 0.5, 1, 2, 4, 8, 16, 32}
//  index:   0    1    2   3  4  5  6   7   8
static inline int barsToIndex (float bars)
{
    const float snap[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
    int best = 3; // default 1 bar
    float bestDist = 999.0f;
    for (int i = 0; i < 8; ++i)
    {
        // Log-ratio distance so 0.25 vs 0.5 weighs same as 16 vs 32
        float ratio = bars / snap[i];
        float dist = std::abs (std::log2 (std::max (0.001f, ratio)));
        if (dist < bestDist) { bestDist = dist; best = i + 1; }
    }
    return best;
}

// ═════════════════════════════════════════════════════════════════
// MAIN: detectBPMAndBars — unified detection
// ═════════════════════════════════════════════════════════════════
inline AnalysisResult detectBPMAndBars (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    AnalysisResult result;
    if (buffer.getNumSamples() < 512 || buffer.getNumChannels() < 1 || sampleRate < 1.0)
        return result;

    int totalSamples = buffer.getNumSamples();
    double durationSec = static_cast<double>(totalSamples) / sampleRate;

    if (durationSec < 0.2) return result;   // too short (one-shot)
    if (durationSec > 180.0) return result;  // too long

    // ════════════════════════════════════════════
    // STEP 1: Try ALL plausible beat counts
    // ════════════════════════════════════════════
    struct Candidate {
        int   beats;
        float bpm;
        float score;
    };
    std::vector<Candidate> candidates;
    candidates.reserve (200);

    for (int beats = 1; beats <= 128; ++beats)
    {
        double rawBPM = static_cast<double>(beats) * 60.0 / durationSec;
        if (rawBPM < 40.0 || rawBPM > 220.0) continue;

        float bpmF = static_cast<float>(rawBPM);
        float score = 0.0f;
        score += bpmRoundness (bpmF);
        score += beatCountScore (beats);
        score += bpmRangeScore (bpmF);

        // Whole-bar bonus
        float bars = static_cast<float>(beats) / 4.0f;
        float barsFrac = bars - std::floor (bars);
        if (barsFrac < 0.01f) score += 2.0f;

        candidates.push_back ({beats, bpmF, score});
    }

    if (candidates.empty()) return result;

    // ════════════════════════════════════════════
    // STEP 2: Onset autocorrelation verification
    // ════════════════════════════════════════════
    float onsetBPM = 0.0f;
    if (totalSamples > 4096)
    {
        const float* data = buffer.getReadPointer (0);
        int windowSize = std::max (64, static_cast<int>(sampleRate * 0.01));
        int numWindows = std::min (totalSamples / windowSize, 2048);

        if (numWindows >= 16)
        {
            std::vector<float> energy (static_cast<size_t>(numWindows), 0.0f);
            for (int w = 0; w < numWindows; ++w)
            {
                float sum = 0.0f;
                int start = w * windowSize;
                for (int s = 0; s < windowSize && start + s < totalSamples; ++s)
                {
                    float v = data[start + s];
                    sum += v * v;
                }
                energy[static_cast<size_t>(w)] = std::sqrt (sum / static_cast<float>(windowSize));
            }

            std::vector<float> onset (static_cast<size_t>(numWindows), 0.0f);
            for (int w = 1; w < numWindows; ++w)
            {
                float diff = energy[static_cast<size_t>(w)] - energy[static_cast<size_t>(w - 1)];
                onset[static_cast<size_t>(w)] = std::max (0.0f, diff);
            }

            float windowsPerSec = static_cast<float>(sampleRate) / static_cast<float>(windowSize);
            int minLag = std::max (1, static_cast<int>(windowsPerSec * 60.0f / 220.0f));
            int maxLag = std::min (numWindows / 2, static_cast<int>(windowsPerSec * 60.0f / 40.0f));

            if (minLag < maxLag)
            {
                float bestCorr = 0.0f;
                int bestLag = 0;

                for (int lag = minLag; lag <= maxLag; ++lag)
                {
                    float corr = 0.0f, norm = 0.0f;
                    int count = numWindows - lag;
                    for (int w = 0; w < count; ++w)
                    {
                        corr += onset[static_cast<size_t>(w)] * onset[static_cast<size_t>(w + lag)];
                        norm += onset[static_cast<size_t>(w)] * onset[static_cast<size_t>(w)];
                    }
                    if (norm > 1e-10f) corr /= norm;
                    float lagBias = 1.0f + 0.05f * static_cast<float>(lag - minLag) / static_cast<float>(std::max (1, maxLag - minLag));
                    corr /= lagBias;

                    if (corr > bestCorr) { bestCorr = corr; bestLag = lag; }
                }

                if (bestLag >= 1 && bestCorr > 0.01f)
                {
                    float lagSec = static_cast<float>(bestLag) * static_cast<float>(windowSize) / static_cast<float>(sampleRate);
                    onsetBPM = 60.0f / lagSec;
                    while (onsetBPM > 220.0f) onsetBPM *= 0.5f;
                    while (onsetBPM < 40.0f) onsetBPM *= 2.0f;
                }
            }
        }
    }

    // ════════════════════════════════════════════
    // STEP 3: Boost candidates that match onset
    // ════════════════════════════════════════════
    if (onsetBPM > 0.0f)
    {
        for (auto& c : candidates)
        {
            float ratios[] = {0.5f, 1.0f, 2.0f};
            for (float r : ratios)
            {
                float adjusted = c.bpm * r;
                float diff = std::abs (adjusted - onsetBPM);
                float tolerance = onsetBPM * 0.05f;
                if (diff < tolerance) { c.score += 4.0f; break; }
            }
        }
    }

    // ════════════════════════════════════════════
    // STEP 4: Pick the winner + octave correction
    // ════════════════════════════════════════════
    std::sort (candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    // Octave correction: if halving the beat count gives a valid BPM
    // with a reasonable score, prefer fewer bars (simpler interpretation).
    // This prevents "8 bars at 120" winning over "4 bars at 60".
    int bestIdx = 0;
    for (int ci = 1; ci < static_cast<int>(candidates.size()); ++ci)
    {
        auto& top = candidates[static_cast<size_t>(bestIdx)];
        auto& alt = candidates[static_cast<size_t>(ci)];
        // Check if alt is the "half" version of top (half beats, half BPM)
        if (alt.beats * 2 == top.beats && alt.bpm >= 50.0f && alt.bpm <= 200.0f)
        {
            // Prefer the simpler version if its score is at least 60% of the top
            if (alt.score >= top.score * 0.6f)
                bestIdx = ci;
        }
    }

    auto& winner = candidates[static_cast<size_t>(bestIdx)];
    float runnerScore = candidates.size() > 1 ? candidates[bestIdx == 0 ? 1 : 0].score : 0.0f;
    result.confidence = std::clamp ((winner.score - runnerScore) / std::max (1.0f, winner.score), 0.0f, 1.0f);

    // Keep full precision — no rounding (important for grid accuracy)
    result.bpm = winner.bpm;
    result.beats = winner.beats;
    result.bars = std::max (1, winner.beats / 4);
    result.barIndex = barsToIndex (static_cast<float>(winner.beats) / 4.0f);
    result.isLoop = true;

    return result;
}

// ─── Root Note Detection ───────────────────────────────────────
inline int detectRootNote (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (buffer.getNumSamples() < 512 || buffer.getNumChannels() < 1 || sampleRate < 1.0)
        return -1;

    const float* data = buffer.getReadPointer (0);
    int totalSamples = buffer.getNumSamples();

    int analyzeStart = totalSamples / 10;
    int analyzeEnd = std::min (totalSamples, totalSamples * 4 / 10);
    int analyzeLen = analyzeEnd - analyzeStart;
    if (analyzeLen < 512) { analyzeStart = 0; analyzeLen = std::min (totalSamples, 8192); }
    analyzeLen = std::min (analyzeLen, 8192);

    int minPeriod = std::max (2, static_cast<int>(sampleRate / 4200.0));
    int maxPeriod = std::min (analyzeLen / 2, static_cast<int>(sampleRate / 30.0));
    if (minPeriod >= maxPeriod) return -1;

    float bestCorr = -1.0f;
    int bestPeriod = 0;

    for (int period = minPeriod; period <= maxPeriod; ++period)
    {
        float corr = 0.0f, energy = 0.0f;
        int count = analyzeLen - period;
        if (count < 64) continue;
        for (int s = 0; s < count; ++s)
        {
            int idx1 = analyzeStart + s;
            int idx2 = analyzeStart + s + period;
            if (idx1 >= totalSamples || idx2 >= totalSamples) break;
            corr += data[idx1] * data[idx2];
            energy += data[idx1] * data[idx1];
        }
        if (energy < 0.0001f) continue;
        corr /= energy;
        if (corr > bestCorr) { bestCorr = corr; bestPeriod = period; }
    }

    if (bestPeriod < 1 || bestCorr < 0.5f) return -1;
    float freq = static_cast<float>(sampleRate) / static_cast<float>(bestPeriod);
    float midiNote = 69.0f + 12.0f * std::log2 (freq / 440.0f);
    return std::clamp (static_cast<int>(std::round (midiNote)), 0, 127);
}

// ─── Convenience: detect all at once ────────────────────────────
inline AnalysisResult analyze (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    auto result = detectBPMAndBars (buffer, sampleRate);
    result.rootNote = detectRootNote (buffer, sampleRate);
    return result;
}

// ─── Transient Detection ───────────────────────────────────
// Detects sharp energy increases (onsets/transients) in the audio.
// Returns normalized positions (0-1) of each transient.
// Used for:
//   1. Warp marker auto-placement near transients
//   2. Visual transient indicators on waveform display
//   3. Beat-aligned time stretch quality improvement
inline std::vector<float> detectTransients (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                             float sensitivity = 0.5f)
{
    std::vector<float> transients;
    if (buffer.getNumSamples() < 512 || buffer.getNumChannels() < 1 || sampleRate < 1.0)
        return transients;

    const float* data = buffer.getReadPointer (0);
    int totalSamples = buffer.getNumSamples();

    // ── Energy envelope with short window (~3ms) ──
    int windowSize = std::max (32, static_cast<int>(sampleRate * 0.003));
    int numWindows = totalSamples / windowSize;
    if (numWindows < 8) return transients;

    std::vector<float> energy (static_cast<size_t>(numWindows), 0.0f);
    for (int w = 0; w < numWindows; ++w)
    {
        float sum = 0.0f;
        int start = w * windowSize;
        for (int s = 0; s < windowSize && start + s < totalSamples; ++s)
        {
            float v = data[start + s];
            sum += v * v;
        }
        energy[static_cast<size_t>(w)] = std::sqrt (sum / static_cast<float>(windowSize));
    }

    // ── Onset detection function: positive energy derivative ──
    std::vector<float> onset (static_cast<size_t>(numWindows), 0.0f);
    float maxOnset = 0.0f;
    for (int w = 1; w < numWindows; ++w)
    {
        float diff = energy[static_cast<size_t>(w)] - energy[static_cast<size_t>(w - 1)];
        onset[static_cast<size_t>(w)] = std::max (0.0f, diff);
        maxOnset = std::max (maxOnset, onset[static_cast<size_t>(w)]);
    }
    if (maxOnset < 1e-6f) return transients;

    // ── Adaptive threshold: median + sensitivity-scaled peak ──
    // Sort onset values for median calculation
    std::vector<float> sortedOnset;
    for (int w = 0; w < numWindows; ++w)
        if (onset[static_cast<size_t>(w)] > 0.0f)
            sortedOnset.push_back (onset[static_cast<size_t>(w)]);

    float median = 0.0f;
    if (!sortedOnset.empty())
    {
        std::sort (sortedOnset.begin(), sortedOnset.end());
        median = sortedOnset[sortedOnset.size() / 2];
    }

    // threshold: lower sensitivity → lower threshold → more transients detected
    float threshold = median + (maxOnset - median) * (1.0f - sensitivity) * 0.5f;
    threshold = std::max (threshold, maxOnset * 0.05f); // minimum 5% of peak

    // ── Minimum gap between transients: ~30ms ──
    int minGapWindows = std::max (2, static_cast<int>(0.030 * sampleRate / static_cast<double>(windowSize)));

    // ── Pick peaks above threshold with minimum gap ──
    int lastTransient = -minGapWindows;
    for (int w = 1; w < numWindows - 1; ++w)
    {
        if (onset[static_cast<size_t>(w)] > threshold
            && onset[static_cast<size_t>(w)] >= onset[static_cast<size_t>(w - 1)]
            && onset[static_cast<size_t>(w)] >= onset[static_cast<size_t>(w + 1)]
            && (w - lastTransient) >= minGapWindows)
        {
            // Convert to normalized position
            float normPos = static_cast<float>(w * windowSize) / static_cast<float>(totalSamples);
            transients.push_back (normPos);
            lastTransient = w;
        }
    }

    return transients;
}

// ─── Auto Warp Markers: place markers at transients aligned to beats ──
// Given detected BPM/bars and transients, creates warp markers that
// map each transient to its nearest expected beat position.
inline std::vector<WarpMarker> generateAutoWarpMarkers (
    const AnalysisResult& analysis,
    const std::vector<float>& transients,
    int totalSamples, double sampleRate)
{
    std::vector<WarpMarker> markers;
    if (analysis.bpm <= 0.0f || analysis.beats <= 0 || totalSamples < 512)
        return markers;

    double durationSec = static_cast<double>(totalSamples) / sampleRate;
    double secPerBeat = 60.0 / static_cast<double>(analysis.bpm);
    int totalBeats = analysis.beats;

    // Always add start and end markers
    WarpMarker startM;
    startM.samplePos = 0.0f;
    startM.originalSamplePos = 0.0f;
    startM.beatPos = 0.0f;
    startM.isAuto = true;
    markers.push_back (startM);

    // For each transient, find nearest expected beat and create marker
    for (float tPos : transients)
    {
        double timeSec = static_cast<double>(tPos) * durationSec;
        double beatExact = timeSec / secPerBeat;

        // Snap to nearest beat
        double nearestBeat = std::round (beatExact);
        if (nearestBeat < 0) nearestBeat = 0;
        if (nearestBeat > totalBeats) nearestBeat = totalBeats;

        // Only add if the transient is reasonably close to a beat (within 0.35 beats)
        double deviation = std::abs (beatExact - nearestBeat);
        if (deviation < 0.35)
        {
            WarpMarker m;
            m.samplePos = tPos;
            m.originalSamplePos = tPos;
            m.beatPos = static_cast<float>(nearestBeat);
            m.isAuto = true;
            markers.push_back (m);
        }
    }

    // End marker
    WarpMarker endM;
    endM.samplePos = 1.0f;
    endM.originalSamplePos = 1.0f;
    endM.beatPos = static_cast<float>(totalBeats);
    endM.isAuto = true;
    markers.push_back (endM);

    // Remove duplicates (markers at same beat position — keep the one closest to expected)
    if (markers.size() > 2)
    {
        std::sort (markers.begin(), markers.end(),
            [](const WarpMarker& a, const WarpMarker& b) { return a.beatPos < b.beatPos; });

        std::vector<WarpMarker> unique;
        unique.push_back (markers[0]);
        for (size_t i = 1; i < markers.size(); ++i)
        {
            if (std::abs (markers[i].beatPos - unique.back().beatPos) > 0.01f)
                unique.push_back (markers[i]);
        }
        markers = unique;
    }

    return markers;
}

} // namespace SampleAnalysis
