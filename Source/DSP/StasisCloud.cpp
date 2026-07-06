#include "StasisCloud.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstring>

namespace stasis_cloud
{
namespace
{
// Deterministic hash → [0, 1). Used to seed per-voice positions and detune.
inline float hashToUnit(int a, int b, int c)
{
    uint32_t x = 0x9E3779B9u;
    x ^= static_cast<uint32_t>(a) + 0x85EBCA6Bu + (x << 6) + (x >> 2);
    x ^= static_cast<uint32_t>(b) + 0xC2B2AE35u + (x << 6) + (x >> 2);
    x ^= static_cast<uint32_t>(c) + 0x27D4EB2Fu + (x << 6) + (x >> 2);
    x ^= x >> 15; x *= 0x85EBCA6Bu;
    x ^= x >> 13; x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}
} // anon

void ensureState(State& state, int frameSize)
{
    if (state.storedFrameSize == frameSize && !state.captureRing.empty())
        return;

    state.storedFrameSize = frameSize;
    state.captureRing.assign(static_cast<size_t>(State::MAX_CAPTURE_FRAMES)
                             * static_cast<size_t>(frameSize), 0.0f);
    state.scratchFrame.assign(static_cast<size_t>(frameSize), 0.0f);
    state.captureWriteIdx = 0;
    state.capturedFrames  = 0;
    state.freezeActive    = false;
    state.blendRamp       = 0.0f;
    state.numActiveVoices = 0;
    for (auto& v : state.voices) v = State::Voice{};
    state.initialized = true;
}

void processBlock(const Settings& settings,
                  State&          state,
                  int             channel,
                  float*          frame,
                  int             frameSize)
{
    ensureState(state, frameSize);

    const float freeze = juce::jlimit(0.0f, 1.0f, settings.freeze);
    const float size   = juce::jlimit(0.0f, 1.0f, settings.size);
    const float cloud  = juce::jlimit(0.0f, 1.0f, settings.cloud);
    const float mix    = juce::jlimit(0.0f, 1.0f, settings.mix);

    const bool shouldFreeze = freeze > 0.5f;

    // === Capture live audio (only when not frozen, to avoid feedback) ===
    if (!shouldFreeze)
    {
        const size_t dst = static_cast<size_t>(state.captureWriteIdx)
                         * static_cast<size_t>(frameSize);
        std::memcpy(state.captureRing.data() + dst, frame,
                    static_cast<size_t>(frameSize) * sizeof(float));
        state.captureWriteIdx = (state.captureWriteIdx + 1) % State::MAX_CAPTURE_FRAMES;
        state.capturedFrames  = juce::jmin(state.capturedFrames + 1,
                                           State::MAX_CAPTURE_FRAMES);
    }

    // === Freeze activation ===
    if (shouldFreeze && !state.freezeActive)
    {
        if (state.capturedFrames < 1)
            return; // nothing captured yet, stay live

        state.freezeActive = true;
        state.blendRamp    = 0.0f;

        // Map Size → frozen loop length (quadratic: small stays small)
        const int available = juce::jmin(state.capturedFrames, State::MAX_CAPTURE_FRAMES);
        state.frozenFrameCount = juce::jlimit(1, available,
            1 + static_cast<int>(size * size * static_cast<float>(available - 1)));

        // The frozen loop uses the most recently captured N frames.
        // captureWriteIdx is the NEXT write slot, so the last written frame
        // is at (captureWriteIdx - 1 + MAX) % MAX.
        state.frozenStartIdx = ((state.captureWriteIdx - state.frozenFrameCount)
                                + State::MAX_CAPTURE_FRAMES * 4) % State::MAX_CAPTURE_FRAMES;

        // Voice count: Cloud=0 → 1 voice, Cloud=1 → MAX_VOICES voices
        state.numActiveVoices = juce::jlimit(1, State::MAX_VOICES,
            1 + juce::roundToInt(cloud * static_cast<float>(State::MAX_VOICES - 1)));

        for (int v = 0; v < State::MAX_VOICES; ++v)
        {
            state.voices[v].active = (v < state.numActiveVoices);
            if (v >= state.numActiveVoices) continue;

            // Base position: spread voices evenly across the frozen loop
            const float basePos = (static_cast<float>(v) / static_cast<float>(state.numActiveVoices))
                                * static_cast<float>(state.frozenFrameCount);

            // Cloud>0: add random jitter to the start position
            const float jitterRange = cloud * static_cast<float>(state.frozenFrameCount) * 0.5f;
            const float jitter = (cloud > 0.01f)
                ? (hashToUnit(channel, v, state.frozenStartIdx) - 0.5f) * jitterRange
                : 0.0f;

            state.voices[v].readPos = juce::jlimit(0.0f,
                static_cast<float>(state.frozenFrameCount) - 0.001f,
                basePos + jitter);
            state.voices[v].phaseAcc = 0.0f;

            // Shimmer: slow organic phase drift per voice (only meaningful at Cloud>0)
            if (cloud > 0.01f)
            {
                const float seed1 = hashToUnit(channel, v + 1, state.frozenStartIdx + 1);
                const float seed2 = hashToUnit(channel, v + 2, state.frozenStartIdx + 2);
                const float baseVel  = juce::jmap(size, 0.0f, 1.0f, 0.020f, 0.004f);
                const float sign     = (seed1 < 0.5f) ? -1.0f : 1.0f;
                const float speedMul = juce::jmap(seed2, 0.0f, 1.0f, 0.60f, 1.40f);
                state.voices[v].phaseVel = baseVel * speedMul * sign * cloud;
            }
            else
            {
                state.voices[v].phaseVel = 0.0f;
            }
        }
    }
    else if (!shouldFreeze && state.freezeActive)
    {
        // Release freeze
        state.freezeActive    = false;
        state.blendRamp       = 0.0f;
        state.numActiveVoices = 0;
        for (auto& v : state.voices) v.active = false;
    }

    if (!state.freezeActive || mix < 1.0e-5f)
        return;

    // Smooth blend-in ramp (avoids click on freeze activate)
    const float blendRate = 0.05f; // ~20 frames to fully engage
    state.blendRamp = juce::jlimit(0.0f, 1.0f, state.blendRamp + blendRate);

    // === Granular synthesis ===
    std::fill(state.scratchFrame.begin(), state.scratchFrame.end(), 0.0f);

    // Constant-power voice gain
    const float voiceGain = 1.0f / std::sqrt(static_cast<float>(juce::jmax(1, state.numActiveVoices)));
    const float fN        = static_cast<float>(state.frozenFrameCount);

    for (int v = 0; v < State::MAX_VOICES; ++v)
    {
        auto& voice = state.voices[v];
        if (!voice.active) continue;

        // ── Grain amplitude envelope ──────────────────────────────────
        // Cloud=0: rectangular (full amplitude, click at wrap point)
        // Cloud=1: Hann shape  (smooth fade in/out, no click)
        const float grainPhase = voice.readPos / fN;  // 0..1
        const float hannAmp    = 0.5f * (1.0f - std::cos(
                                    juce::MathConstants<float>::twoPi * grainPhase));
        const float amplitude  = juce::jmap(cloud, 1.0f, hannAmp) * voiceGain;

        // ── Linear interpolation between adjacent frozen frames ───────
        const int   fi0      = static_cast<int>(std::floor(voice.readPos)) % state.frozenFrameCount;
        const int   fi1      = (fi0 + 1) % state.frozenFrameCount;
        const float frac     = voice.readPos - std::floor(voice.readPos);
        const int   ringIdx0 = (state.frozenStartIdx + fi0) % State::MAX_CAPTURE_FRAMES;
        const int   ringIdx1 = (state.frozenStartIdx + fi1) % State::MAX_CAPTURE_FRAMES;
        const float* src0 = state.captureRing.data() + static_cast<size_t>(ringIdx0) * static_cast<size_t>(frameSize);
        const float* src1 = state.captureRing.data() + static_cast<size_t>(ringIdx1) * static_cast<size_t>(frameSize);

        const float w0 = amplitude * (1.0f - frac);
        const float w1 = amplitude * frac;
        for (int i = 0; i < frameSize; ++i)
            state.scratchFrame[static_cast<size_t>(i)] += src0[i] * w0 + src1[i] * w1;

        // ── Advance read position with shimmer modulation ─────────────
        // phaseVel: slow per-frame LFO that causes subtle organic pitch drift
        voice.phaseAcc += voice.phaseVel;
        if (voice.phaseAcc >  juce::MathConstants<float>::pi) voice.phaseAcc -= juce::MathConstants<float>::twoPi;
        if (voice.phaseAcc < -juce::MathConstants<float>::pi) voice.phaseAcc += juce::MathConstants<float>::twoPi;

        // Advance read position: speed slightly modulated by shimmer (±0.8% at cloud=1)
        const float speed = 1.0f + std::sin(voice.phaseAcc) * 0.008f * cloud;
        voice.readPos += speed;
        if (voice.readPos >= fN) voice.readPos -= fN;
        if (voice.readPos <  0.0f) voice.readPos += fN;
    }

    // === Mix wet/dry ===
    const float wetAmt = state.blendRamp * mix;
    const float dryAmt = 1.0f - wetAmt;
    for (int i = 0; i < frameSize; ++i)
        frame[i] = dryAmt * frame[i] + wetAmt * state.scratchFrame[static_cast<size_t>(i)];
}
} // stasis_cloud
