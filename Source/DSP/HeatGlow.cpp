#include "HeatGlow.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace heat_glow
{
namespace
{
// ── Drive gain curve ──────────────────────────────────────────────────────
// drive=0   → G=1.0   (pass-through)
// drive=0.5 → G=2.5   (subtle saturation on peaks)
// drive=0.9 → G=15.0  (moderate, clearly audible drive)
// drive=1.0 → G=30.0  (heavy, last-10%-crunch territory)
inline float computeDriveGain(float drive)
{
    const float d = juce::jlimit(0.0f, 1.0f, drive);
    if (d <= 0.5f)
        return 1.0f + 6.0f * d * d;             // 1.0 .. 2.5
    if (d <= 0.9f)
    {
        const float t = (d - 0.5f) / 0.4f;
        return 2.5f + 12.5f * t * t;            // 2.5 .. 15.0
    }
    const float t = (d - 0.9f) / 0.1f;
    return 15.0f + 15.0f * t * t;               // 15.0 .. 30.0
}

// ── Digital saturation ────────────────────────────────────────────────────
// Algebraic limiter x / (1 + G*|x|). Unity small-signal gain, harder knee
// than tanh — sounds like transistor/digital clipping, rich in odd harmonics.
inline float digitalSat(float x, float G)
{
    return x / (1.0f + G * std::abs(x));
}

// ── Tube saturation ───────────────────────────────────────────────────────
// tanh with a mild positive quadratic asymmetry on the input, which generates
// even harmonics (2nd harmonic bloom) for warmth. The asymmetry coefficient
// scales with `heat` (0..1 blend toward full tube mode). Output is normalised
// so the small-signal gain is exactly 1 (divide by G). The slight DC offset
// introduced by the x^2 term is removed by the DC blocker that follows.
inline float tubeSat(float x, float G, float heat)
{
    const float tubeBlend = juce::jlimit(0.0f, 1.0f, (heat - 0.5f) * 2.0f);
    const float asymm = 0.10f * tubeBlend;          // 0 .. 0.10
    const float driven = G * x * (1.0f + asymm * x); // quadratic asymmetry on input
    return std::tanh(driven) / G;                    // normalised: small-signal gain = 1
}

} // anon

void processBlock(const Settings& settings,
                  State& state,
                  float* buffer,
                  int numSamples)
{
    const float drive = juce::jlimit(0.0f, 1.0f, settings.drive);
    const float glow  = juce::jlimit(0.0f, 1.0f, settings.glow);
    const float heat  = juce::jlimit(0.0f, 1.0f, settings.heat);
    const float mix   = juce::jlimit(0.0f, 1.0f, settings.mix);

    if (drive < 1.0e-5f)
        return; // pass-through: no drive → skip entirely

    const float G = computeDriveGain(drive);

    // Makeup: compensate the compression from saturation so the overall level
    // stays roughly stable as Drive is raised. G^0.35 is empirically smooth:
    //   G=1  → 1.00   G=2.5 → 1.42   G=15 → 2.51   G=30 → 3.11
    const float makeup = std::pow(G, 0.35f);

    // Glow: post-saturation 1-pole first-difference tone shelf.
    //   coeff = 0.0  (glow=0.5)  → flat (DC gain=1, Nyquist gain=1)
    //   coeff > 0   (glow→1.0)  → high-emphasis → harsh/bright (+3 dB Nyquist)
    //   coeff < 0   (glow→0.0)  → high-attenuation → warm/round (−4 dB Nyquist)
    // Applied post-saturation so the tone shape colours the distortion character.
    const float glowCoeff = (glow - 0.5f) * 0.40f;  // ±0.20

    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = buffer[i];

        // Waveshaper: digital or tube, gain-normalised so quiet signals are
        // barely touched; loud signals are compressed/clipped progressively.
        float sat;
        if (heat < 0.5f)
            sat = digitalSat(dry, G) * makeup;
        else
            sat = tubeSat(dry, G, heat) * makeup;

        // DC block (post-saturation): removes DC from tube asymmetry.
        // H(z) = (1 - z^-1) / (1 - 0.995 * z^-1)  ≈  HP at ~20 Hz @ 48 kHz
        const float dcOut = sat - state.dcX1 + 0.995f * state.dcY1;
        state.dcX1 = sat;
        state.dcY1 = dcOut;

        // Glow tone shaping: y[n] = x[n] + coeff * (x[n] - x[n-1])
        const float glowed = dcOut + glowCoeff * (dcOut - state.glowZ1);
        state.glowZ1 = dcOut;

        buffer[i] = (1.0f - mix) * dry + mix * glowed;
    }
}
} // heat_glow
