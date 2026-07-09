#include "GritEdge.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace grit_edge
{
namespace
{
inline float clamp01(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

inline float edgePeakGain(int bin, double sampleRate, int fftSize, float edgeNorm)
{
    const float edgeDb = (clamp01(edgeNorm) - 0.5f) * 24.0f; // +/- 12 dB around noon.
    if (std::abs(edgeDb) < 1.0e-5f)
        return 1.0f;

    const float freqHz = static_cast<float>(bin) * static_cast<float>(sampleRate / juce::jmax(1, fftSize));
    const float clampedHz = juce::jlimit(20.0f, 20000.0f, freqHz);
    const float octFromCenter = std::log2(clampedHz / 4000.0f);

    // Smooth bell centered at 4 kHz, about +/- 1 octave useful range.
    constexpr float sigmaOct = 0.55f;
    const float bell = std::exp(-0.5f * (octFromCenter * octFromCenter) / (sigmaOct * sigmaOct));
    return dbToGain(edgeDb * bell);
}

inline float smoothstep(float x)
{
    x = juce::jlimit(0.0f, 1.0f, x);
    return x * x * (3.0f - 2.0f * x);
}

inline float gritDbFromControl(float grit)
{
    const float g = clamp01(grit);

    // Gain staging targets:
    // low  (OD)   ~ +20 .. +30 dB
    // mid  (Dist) ~ +30 .. +40 dB
    // high (Fuzz) ~ +40 .. +54 dB
    if (g <= 0.35f)
    {
        const float t = g / 0.35f;
        return 30.0f * t * t;
    }

    if (g <= 0.75f)
    {
        const float t = (g - 0.35f) / 0.40f;
        return 30.0f + 10.0f * std::pow(t, 1.15f);
    }

    const float t = (g - 0.75f) / 0.25f;
    return 40.0f + 14.0f * std::pow(t, 0.72f);
}
}

void processBin(int bin,
                double sampleRate,
                int fftSize,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm)
{
    const float grit = clamp01(settings.grit);
    const float edge = clamp01(settings.edge);
    const float asymmetry = clamp01(settings.asymmetry);
    const float mix = clamp01(settings.mix);

    // Keep DC untouched to avoid low-frequency hum/buzz build-up.
    if (bin == 0)
    {
        outRe = inRe;
        outIm = inIm;
        return;
    }

    if (mix <= 1.0e-5f)
    {
        outRe = inRe;
        outIm = inIm;
        return;
    }

    const float inMag = std::sqrt(inRe * inRe + inIm * inIm);
    if (inMag <= 1.0e-9f)
    {
        outRe = inRe;
        outIm = inIm;
        return;
    }

    const float gritDb = gritDbFromControl(grit);
    const float preGain = dbToGain(gritDb);

    // Drive only controls clipping amount. Character is selected by asymmetry.
    constexpr float kneeRef = 7.5f;
    const float driven = inMag * preGain;
    const float x = driven / kneeRef;

    // Asymmetry morph:
    //   0.0 -> modern tube amp style
    //   0.5 -> digital hard clipping
    //   1.0 -> fuzzy broken style
    const float tubeWeight = smoothstep(juce::jlimit(0.0f, 1.0f, 1.0f - asymmetry * 2.0f));
    const float fuzzWeight = smoothstep(juce::jlimit(0.0f, 1.0f, (asymmetry - 0.5f) * 2.0f));
    const float digitalWeight = juce::jmax(0.0f, 1.0f - tubeWeight - fuzzWeight);

    // Positive-domain transfer curves for spectral magnitudes.
    const float tubeN = std::tanh(x * 1.35f) * (1.0f + 0.18f * (1.0f - std::exp(-x * 1.1f)));
    const float digitalN = juce::jmin(1.0f, x);

    const float fuzzBase = std::pow(juce::jmin(1.0f, x), 0.45f);
    const float sputter = 0.86f + 0.14f * std::sin(juce::jlimit(0.0f, 24.0f, x) * 2.6f);
    const float fuzzN = juce::jlimit(0.0f, 1.22f, fuzzBase * sputter);

    float shapedN = tubeN * tubeWeight + digitalN * digitalWeight + fuzzN * fuzzWeight;

    // Edge is a post-emphasis at 4 kHz. Neutral at 12 o'clock.
    shapedN *= edgePeakGain(bin, sampleRate, fftSize, edge);

    // Preserve bass body as asymmetry moves into fuzz.
    const float freqHz = static_cast<float>(bin) * static_cast<float>(sampleRate / juce::jmax(1, fftSize));
    const float bassKeep = juce::jlimit(0.0f, 1.0f, (240.0f - freqHz) / 220.0f);
    shapedN *= 1.0f + fuzzWeight * bassKeep * 0.22f;

    float wetMag = shapedN * kneeRef;

    // ─── FIX: Aggressiverer Trim für Spektral-Magnituden ───
    const float edgeBoostDb = juce::jmax(0.0f, (edge - 0.5f) * 24.0f);
    const float trimDb      = -(gritDb * 0.75f + edgeBoostDb * 0.85f);
    wetMag *= dbToGain(juce::jlimit(-60.0f, 0.0f, trimDb));

    // Fester Ceiling für Magnituden (immer positiv)
    wetMag = 0.95f * std::tanh(wetMag / 0.95f);

    const float wetScale = wetMag / inMag;
    const float dryWeight = 1.0f - mix;

    outRe = inRe * dryWeight + (inRe * wetScale) * mix;
    outIm = inIm * dryWeight + (inIm * wetScale) * mix;
}

void processBlock(const Settings& settings,
                  State&          state,
                  double          sampleRate,
                  float*          buffer,
                  int             numSamples)
{
    const float grit      = clamp01(settings.grit);
    const float edge      = clamp01(settings.edge);
    const float asymmetry = clamp01(settings.asymmetry);
    const float mix       = clamp01(settings.mix);

    if (grit < 1.0e-5f || mix < 1.0e-5f)
        return;

    const float sr = static_cast<float>(juce::jmax(1.0, sampleRate));

    // Pre-gain (same drive-curve as spectral version)
    const float gritDb  = gritDbFromControl(grit);
    const float preGain = dbToGain(gritDb);

    // Waveshaper character weights (tube / digital / fuzz blend via Asymmetry)
    const float tubeWeight    = smoothstep(juce::jlimit(0.0f, 1.0f, 1.0f - asymmetry * 2.0f));
    const float fuzzWeight    = smoothstep(juce::jlimit(0.0f, 1.0f, (asymmetry - 0.5f) * 2.0f));
    const float digitalWeight = juce::jmax(0.0f, 1.0f - tubeWeight - fuzzWeight);

     // Output trim (same formula as spectral version)
    const float edgeBoostDb = juce::jmax(0.0f, (edge - 0.5f) * 24.0f);
    
    // FIX: Aggressivere Dämpfung (0.75 / 0.85 statt 0.5 / 0.6)
    // und tieferes Limit (-60 dB statt -48 dB), damit der Safety-Limiter 
    // nicht mehr hart clippen muss.
    const float trimDb      = -(gritDb * 0.75f + edgeBoostDb * 0.85f);
    const float trimGain    = dbToGain(juce::jlimit(-60.0f, 0.0f, trimDb));

    constexpr float kneeRef = 7.5f;

    // ── Edge EQ: biquad peaking filter at 4 kHz ───────────────────────────
    // Replaces the per-bin spectral gain of the old implementation with a
    // proper time-domain resonance. Bell width Q=2 (≈ 1 octave). Gain: ±12 dB.
    const float edgeGainDb = (edge - 0.5f) * 24.0f;  // -12..+12 dB
    float bq_b0 = 1.0f, bq_b1 = 0.0f, bq_b2 = 0.0f;
    float bq_a1 = 0.0f, bq_a2 = 0.0f;
    const bool applyEdge = std::abs(edgeGainDb) > 0.5f;
    if (applyEdge)
    {
        const float A     = std::pow(10.0f, edgeGainDb / 40.0f);  // sqrt(linear gain)
        const float w0    = juce::MathConstants<float>::twoPi * 4000.0f / sr;
        const float cosW0 = std::cos(w0);
        const float sinW0 = std::sin(w0);
        const float alpha = sinW0 / (2.0f * 2.0f);               // Q = 2.0
        const float a0inv = 1.0f / (1.0f + alpha / A);
        bq_b0 = (1.0f + alpha * A)  * a0inv;
        bq_b1 = (-2.0f * cosW0)     * a0inv;
        bq_b2 = (1.0f - alpha * A)  * a0inv;
        bq_a1 = (-2.0f * cosW0)     * a0inv;
        bq_a2 = (1.0f  - alpha / A) * a0inv;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float dry  = buffer[i];
        const float sign = (dry >= 0.0f) ? 1.0f : -1.0f;
        const float absX = std::abs(dry);

        const float xNorm = absX * preGain / kneeRef;

        const float tubeN    = std::tanh(xNorm * 1.35f)
                             * (1.0f + 0.18f * (1.0f - std::exp(-xNorm * 1.1f)));
        const float digitalN = juce::jmin(1.0f, xNorm);
        const float fuzzBase = std::pow(juce::jmin(1.0f, xNorm), 0.45f);
        const float sputter  = 0.86f + 0.14f * std::sin(juce::jlimit(0.0f, 24.0f, xNorm) * 2.6f);
        const float fuzzN    = juce::jlimit(0.0f, 1.22f, fuzzBase * sputter);

        float wet = sign * (tubeN * tubeWeight + digitalN * digitalWeight + fuzzN * fuzzWeight) * kneeRef;

        // Output trim anwenden (Kompensiert den massiven Pre-Gain)
        wet *= trimGain;

        // Edge EQ (biquad peaking)
        if (applyEdge)
        {
            const float yin = bq_b0 * wet + bq_b1 * state.x1 + bq_b2 * state.x2
                                          - bq_a1 * state.y1 - bq_a2 * state.y2;
            state.x2 = state.x1;  state.x1 = wet;
            state.y2 = state.y1;  state.y1 = yin;
            wet = yin;
        }

        // ─── FIX 2 & 3: Fester Safety-Limiter NACH dem EQ ───
        // Verhindert, dass der +12 dB Boost des EQs die DAW clippen lässt.
        // Ein fester Wert (~ -0.4 dBFS) ist sicherer als die alte Input-Skalierung.
        constexpr float safeCeiling = 0.95f; 
        if (std::abs(wet) > safeCeiling)
            wet = (wet >= 0.0f ? 1.0f : -1.0f) * safeCeiling * std::tanh(std::abs(wet) / safeCeiling);

        buffer[i] = (1.0f - mix) * dry + mix * wet;
    }
}
}
