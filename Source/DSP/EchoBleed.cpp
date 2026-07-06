#include "EchoBleed.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace echo_bleed
{
void ensureState(State& state, int frameSize)
{
    if (state.storedFSize == frameSize && !state.frameHistory.empty())
        return;

    state.storedFSize = frameSize;
    state.frameHistory.assign(static_cast<size_t>(maxDelayFrames)
                              * static_cast<size_t>(frameSize), 0.0f);
    state.writeIndex = 0;
    state.lpZ1       = 0.0f;
    state.apZ1       = 0.0f;
    state.wowPhase   = 0.0f;
}

void processBlock(const Settings& settings,
                  State& state,
                  double sampleRate,
                  float  hopSeconds,
                  float* frame,
                  int    frameSize)
{
    ensureState(state, frameSize);

    const float blur     = juce::jlimit(0.0f, 1.0f, settings.bleed);
    const float feedback = juce::jlimit(0.0f, 0.997f, settings.feedback);
    const float mix      = juce::jlimit(0.0f, 1.0f, settings.mix);

    if (mix < 1.0e-5f)
        return;

    const float sr      = static_cast<float>(juce::jmax(1.0, sampleRate));
    const float safeHop = juce::jmax(1.0e-4f, hopSeconds);

    // Delay in STFT frames (frame-domain, same resolution as old spectral impl)
    const float delayFramesF = juce::jlimit(1.0f,
                                             static_cast<float>(maxDelayFrames - 2),
                                             settings.timeSeconds / safeHop);

    // === Wow LFO (frame-rate, 0.8 Hz) ===================================
    // Wobbles the read pointer in units of STFT frames.
    // blur=0 → no wow.  blur=1 → ±0.4 frames ≈ ±4 ms @ 48kHz/512hop.
    // The blur² curve keeps it inaudible at low Blur and gently rising at high.
    const float wowDepth = blur * blur * 0.40f;
    const float wowInc   = juce::MathConstants<float>::twoPi * 0.8f * safeHop;
    state.wowPhase += wowInc;
    if (state.wowPhase > juce::MathConstants<float>::twoPi)
        state.wowPhase -= juce::MathConstants<float>::twoPi;

    float readPos = static_cast<float>(state.writeIndex)
                  - delayFramesF
                  - std::sin(state.wowPhase) * wowDepth;
    while (readPos < 0.0f)  readPos += static_cast<float>(maxDelayFrames);
    while (readPos >= static_cast<float>(maxDelayFrames)) readPos -= static_cast<float>(maxDelayFrames);

    const int   readIdx0 = static_cast<int>(readPos) % maxDelayFrames;
    const int   readIdx1 = (readIdx0 + 1) % maxDelayFrames;
    const float frac     = readPos - std::floor(readPos);

    // === Blur character params ==========================================
    // 1. LP in feedback path: controls tape HF rolloff.
    //    blur² curve gives gentle onset so mild warmth starts ~blur=0.3.
    //    blur=0   → 20 000 Hz (flat, digital crisp repeats)
    //    blur=0.5 →  ~8 300 Hz (warm, lightly coloured repeats)
    //    blur=1   →  2 500 Hz (tape-dark, clearly warm repeats)
    const float blurSq   = blur * blur;
    const float lpCutoff = 20000.0f * std::pow(2500.0f / 20000.0f, blurSq);
    // alpha uses SAMPLE rate (this is a per-sample filter inside the frame loop)
    const float lpAlpha  = juce::jlimit(0.0f, 0.9999f,
                                         1.0f - std::exp(-juce::MathConstants<float>::twoPi
                                                         * lpCutoff / sr));

    // 2. All-pass diffusion: temporal bleed/smear — deliberately kept subtle.
    //    g=0 (digital clean) → g=0.18 (gentle echo diffusion, not reverb-like)
    const float apCoeff = blur * 0.18f;

    // 3. Tape saturation in feedback: adds even harmonics and soft compression.
    //    drive=1 (no effect) → drive=2.0 (mild magnetic-saturation compression)
    const float satDrive = 1.0f + blurSq * 1.0f;
    const float satNorm  = 1.0f / satDrive;

    // Constant-power wet/dry cross-fade
    const float dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
    const float wetGain = std::sin(mix * juce::MathConstants<float>::halfPi);

    // Pointers into the flat ring buffer
    const size_t fSz = static_cast<size_t>(frameSize);
    float*       writeSlot  = state.frameHistory.data() + static_cast<size_t>(state.writeIndex) * fSz;
    const float* readSlot0  = state.frameHistory.data() + static_cast<size_t>(readIdx0) * fSz;
    const float* readSlot1  = state.frameHistory.data() + static_cast<size_t>(readIdx1) * fSz;

    for (int i = 0; i < frameSize; ++i)
    {
        const float dry = frame[i];

        // Linear-interpolated read from the delay history
        const float delayed = readSlot0[i] + (readSlot1[i] - readSlot0[i]) * frac;

        // ---- Feedback path character processing -------------------------
        float fb = delayed;

        // 1. Low-pass (tape HF rolloff): runs per-sample, state persists across frames
        state.lpZ1 += lpAlpha * (fb - state.lpZ1);
        fb = state.lpZ1;

        // 2. All-pass diffusion (Schroeder 1-sample AP): gentle temporal smear
        if (apCoeff > 1.0e-4f)
        {
            const float apOut = -apCoeff * fb + state.apZ1;
            state.apZ1 = fb + apCoeff * apOut;
            fb = apOut;
        }

        // 3. Saturation (magnetic tape compression)
        if (satDrive > 1.001f)
            fb = std::tanh(fb * satDrive) * satNorm;
        // -----------------------------------------------------------------

        // Write into ring buffer: dry input + processed feedback
        writeSlot[i] = dry + fb * feedback;

        // Output: the first repeat is the un-processed delayed signal;
        // coloring accumulates over subsequent repeats (authentic tape behaviour).
        frame[i] = dry * dryGain + delayed * wetGain;
    }

    state.writeIndex = (state.writeIndex + 1) % maxDelayFrames;
}
} // echo_bleed

