#pragma once

#include "ObjectDatabase.h"

namespace echo_bleed
{
// Maximum delay measured in STFT frames.
// At hopSize=512 / 48 kHz: 256 * 10.7 ms ≈ 2.7 s max delay.
constexpr int maxDelayFrames = 256;

struct Settings
{
    float timeSeconds = 0.18f;
    float feedback    = 0.30f;
    float bleed       = 0.30f;  // Blur: 0=digital/clean, 1=tape/warm
    float mix         = 0.30f;
};

// Per-object time-domain state. One instance per object per channel.
struct State
{
    // Flat ring buffer of STFT synthesis frames.
    // Layout: frameHistory[frameIndex * storedFrameSize + sampleIndex]
    // Size: maxDelayFrames * storedFrameSize.
    std::vector<float> frameHistory;
    int   writeIndex   = 0;
    int   storedFSize  = 0;    // detected frameSize for resize guard
    float lpZ1         = 0.0f; // tape LP filter state (continuous across frames)
    float apZ1         = 0.0f; // all-pass diffusion state
    float wowPhase     = 0.0f; // wow LFO phase (radians)
};

// Initialise / resize state for a given STFT frame size. No-op if already correct.
void ensureState(State& state, int frameSize);

// Process one per-object STFT synthesis frame (ISTFT output, before windowing).
//   sampleRate  - audio sample rate (used to compute per-sample LP alpha)
//   hopSeconds  - hopSize / sampleRate (STFT frame period, used for LFO and delay)
//   frame       - ISTFT output buffer for this object, frameSize samples (in/out)
void processBlock(const Settings& settings,
                  State& state,
                  double sampleRate,
                  float  hopSeconds,
                  float* frame,
                  int    frameSize);
}
