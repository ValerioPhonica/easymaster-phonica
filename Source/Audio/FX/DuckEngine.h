#pragma once
#include <array>
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
// DuckEngine — Musical sidechain ducking with lookahead
//
// Proper compressor-style envelope with slew-limited gain to
// avoid clicks on kick transients. The gain cannot drop more
// than maxSlewPerSample, giving an implicit lookahead effect.
//
// Sounds like classic sidechain pumping (Nicky Romero style).
// ═══════════════════════════════════════════════════════════════════
class DuckEngine
{
public:
    static constexpr int kMaxTracks = 15;

    void prepare (double sr)
    {
        sampleRate = static_cast<float>(sr);
        // Slew limit: max gain drop per sample ≈ 3ms fade at any rate
        maxSlewDown = 1.0f / (static_cast<float>(sr) * 0.003f); // ~0.0075 at 44.1k
        for (auto& e : envelopes) e = 1.0f;
        for (auto& o : output) o = 1.0f;
        for (auto& t : triggerPending) t = false;
        for (auto& h : holdSamples) h = 0;
        for (auto& s : stage) s = Stage::Idle;
    }

    // Call when a track fires (sequencer event)
    void triggerTrack (int srcIdx)
    {
        if (srcIdx >= 0 && srcIdx < kMaxTracks)
            triggerPending[srcIdx] = true;
    }

    // Process one sample for ALL targets
    void processSample (const int* duckSrc, const float* depth,
                        const float* atk, const float* rel, int numTracks)
    {
        for (int tgt = 0; tgt < numTracks && tgt < kMaxTracks; ++tgt)
        {
            int src = duckSrc[tgt];
            if (src < 0 || src >= kMaxTracks)
            {
                // No ducking source — smoothly return to 1.0
                envelopes[tgt] += (1.0f - envelopes[tgt]) * 0.002f;
                envelopes[tgt] = std::min (envelopes[tgt], 1.0f);
                stage[tgt] = Stage::Idle;
                output[tgt] = envelopes[tgt]; // no slew needed for release
                continue;
            }

            float duckFloor = 1.0f - std::clamp (depth[tgt], 0.0f, 1.0f);
            float atkSec = std::max (0.0005f, atk[tgt]);
            float relSec = std::max (0.01f, rel[tgt]);

            // Re-trigger resets to attack regardless of current stage
            if (triggerPending[src])
            {
                stage[tgt] = Stage::Attack;
                holdSamples[tgt] = 0;
            }

            switch (stage[tgt])
            {
                case Stage::Attack:
                {
                    // Fast exponential attack toward duck floor
                    float atkCoeff = std::exp (-1.0f / (sampleRate * atkSec));
                    envelopes[tgt] = duckFloor + (envelopes[tgt] - duckFloor) * atkCoeff;

                    // Transition to hold when close to floor
                    if (envelopes[tgt] <= duckFloor + 0.005f)
                    {
                        envelopes[tgt] = duckFloor;
                        stage[tgt] = Stage::Hold;
                        holdSamples[tgt] = static_cast<int>(sampleRate * 0.01f);
                    }
                    break;
                }
                case Stage::Hold:
                {
                    envelopes[tgt] = duckFloor;
                    holdSamples[tgt]--;
                    if (holdSamples[tgt] <= 0)
                        stage[tgt] = Stage::Release;
                    break;
                }
                case Stage::Release:
                {
                    float relCoeff = std::exp (-1.0f / (sampleRate * relSec));
                    envelopes[tgt] = 1.0f + (envelopes[tgt] - 1.0f) * relCoeff;

                    if (envelopes[tgt] >= 0.999f)
                    {
                        envelopes[tgt] = 1.0f;
                        stage[tgt] = Stage::Idle;
                    }
                    break;
                }
                case Stage::Idle:
                default:
                    break;
            }

            envelopes[tgt] = std::clamp (envelopes[tgt], 0.0f, 1.0f);

            // ── Slew limiter: anti-click lookahead effect ──
            // Gain can't drop faster than maxSlewDown per sample (~3ms full fade)
            // Gain CAN rise freely (release is already smooth)
            float target = envelopes[tgt];
            if (target < output[tgt])
                output[tgt] = std::max (target, output[tgt] - maxSlewDown);
            else
                output[tgt] = target; // rising: no slew limit
        }

        // Clear ALL triggers after every target has seen them
        for (auto& t : triggerPending) t = false;
    }

    float getGain (int idx) const
    {
        if (idx < 0 || idx >= kMaxTracks) return 1.0f;
        return output[idx]; // slew-limited output, not raw envelope
    }

private:
    enum class Stage { Idle, Attack, Hold, Release };

    float sampleRate = 44100.0f;
    float maxSlewDown = 0.0075f; // max gain drop per sample
    std::array<float, kMaxTracks> envelopes {};  // raw envelope
    std::array<float, kMaxTracks> output {};      // slew-limited output
    std::array<bool, kMaxTracks> triggerPending {};
    std::array<int, kMaxTracks> holdSamples {};
    std::array<Stage, kMaxTracks> stage {};
};
