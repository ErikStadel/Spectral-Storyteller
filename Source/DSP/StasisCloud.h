#pragma once

#include "ObjectDatabase.h"
#include <array>
#include <vector>

namespace stasis_cloud
{
struct Settings
{
    float freeze = 0.0f;  // 0=live, 1=frozen
    float size   = 0.5f;  // loop length (0=shortest, 1=longest captured material)
    float cloud  = 0.5f;  // diffusion macro (0=metallic/1-voice, 1=ambient/multi-voice)
    float mix    = 0.5f;  // wet/dry
};

// Per-object, per-channel state for the granular freeze engine.
// Holds a ring buffer of captured ISTFT synthesis frames, plus the state for
// up to MAX_VOICES simultaneously active grain voices.
struct State
{
    static constexpr int MAX_CAPTURE_FRAMES = 128;
    static constexpr int MAX_VOICES         = 6;

    // ── Capture ring buffer ───────────────────────────────────────────────
    // Flat layout: captureRing[frameIndex * storedFrameSize + sampleIndex]
    std::vector<float> captureRing;
    int   captureWriteIdx = 0;   // next write position [0, MAX_CAPTURE_FRAMES)
    int   capturedFrames  = 0;   // how many valid frames are in the ring
    int   storedFrameSize = 0;   // remembered for resize guard

    // Scratch buffer for grain mix output (avoids per-call heap allocation)
    std::vector<float> scratchFrame;

    // ── Freeze state ──────────────────────────────────────────────────────
    bool  freezeActive     = false;
    int   frozenFrameCount = 0;   // frames in the current frozen loop
    int   frozenStartIdx   = 0;   // ring index where the frozen loop begins
    float blendRamp        = 0.0f; // 0=live → 1=fully frozen (smooth engage ramp)

    // ── Grain voices ──────────────────────────────────────────────────────
        struct Voice
    {
        float readPos = 0.0f;      // fractional frame position in frozen loop
        float phaseAcc = 0.0f;     // shimmer LFO accumulator
        float phaseVel = 0.0f;     // shimmer phase velocity (radians per frame)
        float lpz = 0.0f;          // NEU: 1-pole lowpass filter state for "Blur" diffusion
        bool active = false;
    };
    std::array<Voice, MAX_VOICES> voices{};
    int numActiveVoices = 0;

    bool initialized = false;
};

// Allocate / resize state for the given STFT frame size. No-op if already correct.
void ensureState(State& state, int frameSize);

// Process one per-object ISTFT synthesis frame (in/out).
//   channel — 0 or 1, seeds per-voice detune directions for stereo width.
//   frame   — unwindowed ISTFT output; reconstructAndOverlapAdd windows + OLAs it.
void processBlock(const Settings& settings,
                  State&          state,
                  int             channel,
                  float*          frame,
                  int             frameSize);
}
