#pragma once

#include "ObjectDatabase.h"

namespace grit_edge
{
struct Settings
{
    float grit       = 0.0f;
    float edge       = 0.5f;
    float asymmetry  = 0.5f;
    float mix        = 0.5f;
};

// Per-object, per-channel state for the time-domain post-ISTFT processing.
// Holds biquad delay lines for the Edge peak-EQ at 4 kHz.
struct State
{
    float x1 = 0.0f, x2 = 0.0f;  // biquad input history
    float y1 = 0.0f, y2 = 0.0f;  // biquad output history
};

// Time-domain waveshaper: same tube/digital/fuzz transfer curves as the
// spectral version but applied per-sample with sign preservation, plus
// a proper biquad peaking EQ for the Edge parameter.
void processBlock(const Settings& settings,
                  State&          state,
                  double          sampleRate,
                  float*          buffer,
                  int             numSamples);

// Legacy spectral per-bin function (kept for reference, no longer called).
void processBin(int bin,
                double sampleRate,
                int fftSize,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm);
}
