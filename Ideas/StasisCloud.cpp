#include "StasisCloud.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace stasis_cloud
{
namespace
{
inline float clamp01(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

inline float wrapPhase(float phase)
{
    while (phase > juce::MathConstants<float>::pi)
        phase -= juce::MathConstants<float>::twoPi;
    while (phase < -juce::MathConstants<float>::pi)
        phase += juce::MathConstants<float>::twoPi;
    return phase;
}

inline float smoothstep(float x)
{
    x = clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}

inline float normalizedSize(float size)
{
    return clamp01(size);
}

inline float hashUnit(int bin, float size, int salt)
{
    const float x = static_cast<float>(bin) * 12.9898f
                  + normalizedSize(size) * 78.233f
                  + static_cast<float>(salt) * 37.719f;
    return std::fmod(std::sin(x) * 43758.5453f, 1.0f) < 0.0f
        ? std::fmod(std::sin(x) * 43758.5453f, 1.0f) + 1.0f
        : std::fmod(std::sin(x) * 43758.5453f, 1.0f);
}

inline float spectralBlurAmount(float blur, float size)
{
    const float sizeAmount = normalizedSize(size);
    return clamp01(blur) * (0.20f + 0.80f * (1.0f - sizeAmount));
}

inline float temporalBlurAmount(float blur, float size)
{
    const float sizeAmount = normalizedSize(size);
    return clamp01(blur) * (0.04f + 0.24f * sizeAmount);
}

inline float blurMagnitude(const std::array<float, ObjectDatabase::NUM_BINS>& mags, int bin, float blur, float size)
{
    const float spread = spectralBlurAmount(blur, size);
    if (spread <= 1.0e-5f)
        return mags[static_cast<size_t>(bin)];

    const int maxRadius = juce::jlimit(1,
                                       ObjectDatabase::NUM_BINS - 1,
                                       juce::roundToInt(juce::jmap(spread, 0.0f, 1.0f, 1.0f, 48.0f)));
    const int radius = juce::jlimit(1,
                                     maxRadius,
                                     juce::roundToInt(1.0f + spread * static_cast<float>(maxRadius)));

    const float sigma = juce::jmax(0.75f, 1.0f + 0.18f * static_cast<float>(radius) + 5.0f * spread);
    float weightedSum = 0.0f;
    float weightTotal = 0.0f;

    for (int offset = -radius; offset <= radius; ++offset)
    {
        const int sourceBin = juce::jlimit(0, ObjectDatabase::NUM_BINS - 1, bin + offset);
        const float distance = static_cast<float>(std::abs(offset));
        const float weight = std::exp(-(distance * distance) / (2.0f * sigma * sigma));
        weightedSum += mags[static_cast<size_t>(sourceBin)] * weight;
        weightTotal += weight;
    }

    return (weightTotal > 1.0e-6f) ? (weightedSum / weightTotal) : mags[static_cast<size_t>(bin)];
}

}

void captureSnapshot(State& state,
                     const std::array<float, ObjectDatabase::NUM_BINS>& liveMagnitudes,
                     const float* fftData,
                     int fftSize,
                     int hopSize,
                     float size)
{
    state.capturedSize = normalizedSize(size);
    for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
    {
        state.frozenMagnitudes[static_cast<size_t>(bin)] = liveMagnitudes[static_cast<size_t>(bin)];
        state.smoothedMagnitudes[static_cast<size_t>(bin)] = liveMagnitudes[static_cast<size_t>(bin)];

        // Capture the REAL inter-bin phase relationship at the moment of freeze
        // instead of scattering every bin to a random phase. This preserves the
        // source material's natural phase coherence, which is a big part of what
        // reads as "organic" rather than "metallic" in a frozen spectrum.
        // NOTE: assumes fftData is interleaved [re0, im0, re1, im1, ...], the layout
        // produced by juce::dsp::FFT::performRealOnlyForwardTransform. Adjust the
        // indexing here if your FFT wrapper packs bins differently.
        const float capturedRe = fftData[2 * bin];
        const float capturedIm = fftData[2 * bin + 1];
        state.phaseAccumulator[static_cast<size_t>(bin)] = std::atan2(capturedIm, capturedRe);

        // phaseVelocity is now a small, slow organic detune/shimmer that rides on
        // TOP of the physically correct per-hop rotation (added in processBin, using
        // hopSize/fftSize). It should stay subtle - it gives the drone its slow
        // evolving movement, it must not carry the bin's actual pitch anymore.
        const float shimmerSign = hashUnit(bin, size, 1);
        const float shimmerAmount = hashUnit(bin, size, 2);
        const float baseVelocity = juce::jmap(state.capturedSize, 0.0f, 1.0f, 0.020f, 0.004f);
        state.phaseVelocity[static_cast<size_t>(bin)] = baseVelocity
                                                      * juce::jmap(shimmerAmount, 0.0f, 1.0f, 0.60f, 1.40f)
                                                      * ((shimmerSign < 0.5f) ? -1.0f : 1.0f);
    }

    juce::ignoreUnused(fftSize, hopSize);
    state.captureBlend = 0.0f;
    state.initialized = true;
    state.freezeWasActive = true;
}

void processBin(int bin,
                int fftSize,
                int hopSize,
                double sampleRate,
                const Settings& settings,
                const std::array<float, ObjectDatabase::NUM_BINS>& liveMagnitudes,
                float inRe,
                float inIm,
                State& state,
                float& outRe,
                float& outIm)
{
    const float freezeOn = clamp01(settings.freeze);
    const float blur = clamp01(settings.blur);
    const float size = normalizedSize(settings.size);
    const float mix = clamp01(settings.mix);

    if (freezeOn <= 1.0e-5f || !state.initialized)
    {
        state.freezeWasActive = false;
        state.captureBlend = 0.0f;
        outRe = inRe;
        outIm = inIm;
        return;
    }

    const float frozenMag = blurMagnitude(state.frozenMagnitudes, bin, blur, state.capturedSize);
    const float frozenWeight = smoothstep(state.captureBlend);
    const float temporalAlpha = juce::jlimit(0.005f,
                                             0.22f,
                                             temporalBlurAmount(blur, state.capturedSize) * (0.35f + 0.65f * (1.0f - state.capturedSize)));
    const float targetMag = frozenMag * (0.92f + 0.08f * state.capturedSize);

    state.smoothedMagnitudes[static_cast<size_t>(bin)] += (targetMag - state.smoothedMagnitudes[static_cast<size_t>(bin)]) * juce::jlimit(0.01f, 0.35f, temporalAlpha);

    // The physically correct per-hop phase advance for this bin: a bin at index
    // `bin` represents a sinusoid that must advance by 2*pi*bin*hopSize/fftSize every
    // time the STFT moves forward by one hop. This is what keeps successive synthesis
    // frames coherent during overlap-add. The previous implementation had NO such
    // term - phaseVelocity was an arbitrary, frequency-unrelated value - so every
    // frame's phase drifted out of alignment with the last, causing destructive
    // interference between overlapping frames. That is exactly what reads as a
    // periodic "machine gun" burst, and it does not go away with Size/Blur because
    // those controls never touched this rotation.
    const float naturalIncrement = juce::MathConstants<float>::twoPi
                                  * static_cast<float>(bin)
                                  * static_cast<float>(hopSize)
                                  / static_cast<float>(fftSize);

    const float phaseJitterAmount = (1.0f - state.capturedSize) * (0.10f + 0.55f * blur);
    const float phaseJitter = phaseJitterAmount * std::sin(state.phaseAccumulator[static_cast<size_t>(bin)] * 1.73f
                                                          + static_cast<float>(bin) * 0.11f);
    const float driftMod = 0.65f + 0.70f * state.capturedSize;
    state.phaseAccumulator[static_cast<size_t>(bin)] = wrapPhase(state.phaseAccumulator[static_cast<size_t>(bin)]
                                                                 + naturalIncrement
                                                                 + state.phaseVelocity[static_cast<size_t>(bin)] * driftMod
                                                                 + phaseJitter * 0.18f);

    const float wetMag = state.smoothedMagnitudes[static_cast<size_t>(bin)]
                       * (0.96f + 0.10f * blur)
                       * (0.90f + 0.18f * frozenWeight);
    const float wetPhase = state.phaseAccumulator[static_cast<size_t>(bin)];

    const float wetRe = wetMag * std::cos(wetPhase);
    const float wetIm = wetMag * std::sin(wetPhase);

    const float dry = 1.0f - mix;
    outRe = inRe * dry + wetRe * mix;
    outIm = inIm * dry + wetIm * mix;

    const float outMag = std::sqrt(outRe * outRe + outIm * outIm);
    const float ceiling = juce::jmax(1.0e-6f, wetMag * (1.35f + 0.45f * state.capturedSize));
    if (outMag > ceiling)
    {
        const float scale = ceiling / outMag;
        outRe *= scale;
        outIm *= scale;
    }

    juce::ignoreUnused(sampleRate);
    juce::ignoreUnused(liveMagnitudes);
}
}
