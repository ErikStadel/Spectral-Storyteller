#include "HeatGlow.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace heat_glow
{
namespace
{
inline float clamp01(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

// Spectral tilt: glow=0 -> bass boost, glow=0.5 -> flat, glow=1 -> treble boost.
// Max ±8 dB across the full spectrum.
inline float tiltGain(float glow, int bin)
{
    const float glowValue = clamp01(glow);
    const float tiltDb = (glowValue - 0.5f) * 16.0f;   // ±8 dB at extremes
    const float binNorm = ObjectDatabase::NUM_BINS > 1
                            ? static_cast<float>(bin) / static_cast<float>(ObjectDatabase::NUM_BINS - 1)
                            : 0.0f;
    const float signedPosition = (binNorm * 2.0f) - 1.0f;  // -1 (bass) to +1 (treble)
    return std::pow(10.0f, (signedPosition * tiltDb) / 20.0f);
}
}

void processBin(int bin,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm)
{
    const float drive = clamp01(settings.drive);
    const float glow  = clamp01(settings.glow);
    const float heat  = clamp01(settings.heat);
    const float mix   = clamp01(settings.mix);

    // Input magnitude – STFT bins can be in the range ~50..1000 for typical audio.
    const float inMag = std::sqrt(inRe * inRe + inIm * inIm);

    if (inMag < 1.0e-12f)
    {
        outRe = inRe;
        outIm = inIm;
        return;
    }

    // Drive curve: quartic so the bottom half stays feathery while the top
    // half becomes very aggressive.
    const float driveAmount = drive * drive * drive * drive;

    // Spectral tilt via Glow – applied to the magnitude before saturation.
    const float tilt = tiltGain(glow, bin);
    const float tiltedMag = inMag * tilt;

    // Reference magnitude: lower value moves the saturation knee earlier, so
    // the effect kicks in more noticeably at moderate drive settings.
    constexpr float refMag = 40.0f;

    // satAmount: how deep into the saturation zone we are.
    //   0  for quiet bins  (nearly linear)
    // → 1  for loud bins   (full compression)
    // This makes the effect self-adapting: it behaves correctly regardless of
    // absolute signal level and never drives the output to zero.

    float wetMag;

    if (heat < 0.5f)
    {
        // ── Symmetric digital saturation (odd harmonics, punchy/tight) ─────────
        const float satAmount = tiltedMag / (refMag + tiltedMag);
        const float satGain   = 1.0f + driveAmount * 4.5f * satAmount;
        wetMag = tiltedMag / satGain;
    }
    else
    {
        // ── Tube-style saturation (softer knee, even-harmonic warmth) ──────────
        // Uses a slightly wider knee (1.4× refMag) and adds a gentle warmth
        // boost to moderate-level signals (imitates the "bloom" of a tube stage).
        const float softSatAmount = tiltedMag / (refMag * 1.4f + tiltedMag);
        const float satGain       = 1.0f + driveAmount * 3.4f * softSatAmount;
        // Warmth: quiet/medium bins get a small boost; loud bins clip as usual.
        const float warmth = 1.0f + driveAmount * 0.28f
                             * std::exp(-tiltedMag / (refMag * 3.0f));
        wetMag = tiltedMag * warmth / satGain;
    }

    // Makeup gain: compensates the average level reduction so perceived loudness
    // stays stable across the full Drive range.
    const float makeup = 1.0f + 1.10f * driveAmount;
    wetMag *= makeup;

    // Reconstruct complex bin: scale magnitude, preserve phase.
    // Then blend dry (original) and wet (saturated) according to Mix.
    const float wetScale  = wetMag / inMag;
    const float dryWeight = 1.0f - mix;

    outRe = inRe * dryWeight + inRe * wetScale * mix;
    outIm = inIm * dryWeight + inIm * wetScale * mix;
}
}
