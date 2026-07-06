#include "SpaceBlur.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace space_blur
{
namespace
{
// â”€â”€ Schroeder all-pass filter (integer delay) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// H(z) = (z^{-D} - g) / (1 - g*z^{-D})    |H| = 1 for all frequencies.
// Produces phase dispersion â†’ subjective diffusion without colouring the level.
inline float apf(float x, float g, std::vector<float>& line, int& wp, int delaySamples)
{
    const int   sz       = static_cast<int>(line.size());
    const int   readPos  = ((wp - delaySamples) % sz + sz) % sz;
    const float delayed  = line[static_cast<size_t>(readPos)];
    const float w        = x + g * delayed;
    line[static_cast<size_t>(wp)] = w;
    wp = (wp + 1) % sz;
    return delayed - g * w;
}

// â”€â”€ Feedback comb filter with 1-pole LP in the feedback loop â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// dampFactor: 1.0 = bypass LP (metallic), < 1.0 = low-pass (warm/hall).
inline float comb(float      x,
                  float      g,
                  float      dampFactor,
                  float&     dampState,
                  std::vector<float>& line,
                  int&       wp,
                  int        delaySamples)
{
    const int   sz      = static_cast<int>(line.size());
    const int   readPos = ((wp - delaySamples) % sz + sz) % sz;
    const float out     = line[static_cast<size_t>(readPos)];
    dampState = dampFactor * out + (1.0f - dampFactor) * dampState;
    line[static_cast<size_t>(wp)] = x + dampState * g;
    wp = (wp + 1) % sz;
    return out;
}

// â”€â”€ Feedback comb with fractional (sub-sample) delay â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Used for LFO-modulated combs (spring boing/shimmer).
inline float combFrac(float  x,
                      float  g,
                      float  dampFactor,
                      float& dampState,
                      std::vector<float>& line,
                      int&   wp,
                      float  delaySamplesF)
{
    const int sz = static_cast<int>(line.size());
    float readF  = static_cast<float>(wp) - delaySamplesF;
    readF = std::fmod(readF, static_cast<float>(sz));
    if (readF < 0.0f) readF += static_cast<float>(sz);

    const int   r0   = static_cast<int>(readF) % sz;
    const int   r1   = (r0 + 1) % sz;
    const float frac = readF - std::floor(readF);
    const float out  = line[static_cast<size_t>(r0)] * (1.0f - frac)
                     + line[static_cast<size_t>(r1)] * frac;
    dampState = dampFactor * out + (1.0f - dampFactor) * dampState;
    line[static_cast<size_t>(wp)] = x + dampState * g;
    wp = (wp + 1) % sz;
    return out;
}
} // anon

void ensureState(State& state, double sampleRate)
{
    const double safeSr = juce::jmax(8000.0, sampleRate);
    if (state.initialized && std::abs(state.cachedSampleRate - safeSr) < 1.0)
        return;

    const int maxPreSamp  = static_cast<int>(safeSr * 0.026) + 64;   // 26ms max
    const int maxCombSamp = static_cast<int>(safeSr * 0.160) + 128;  // 160ms max (hall+size)
    const int maxAPFSamp  = static_cast<int>(safeSr * 0.062) + 64;   // 62ms max (blur+size)

    state.preDelayLine.assign(static_cast<size_t>(maxPreSamp), 0.0f);
    state.preWrite = 0;

    for (int i = 0; i < 4; ++i)
    {
        state.combLines[i].assign(static_cast<size_t>(maxCombSamp), 0.0f);
        state.combWrite[i] = 0;
        state.combDamp[i]  = 0.0f;
    }
    for (int i = 0; i < 2; ++i)
    {
        state.inAPFLines[i].assign( static_cast<size_t>(maxAPFSamp), 0.0f);
        state.inAPFWrite[i]  = 0;
        state.outAPFLines[i].assign(static_cast<size_t>(maxAPFSamp), 0.0f);
        state.outAPFWrite[i] = 0;
    }

    state.lfoPhase         = 0.0f;
    state.cachedSampleRate = safeSr;
    state.initialized      = true;
}

void processBlock(const Settings& settings,
                  State&          state,
                  double          sampleRate,
                  int             channel,
                  float*          buffer,
                  int             numSamples)
{
    ensureState(state, sampleRate);

    const float size  = juce::jlimit(0.0f, 1.0f, settings.size);
    const float decay = juce::jlimit(0.0f, 1.0f, settings.decay);
    const float blur  = juce::jlimit(0.0f, 1.0f, settings.blur);
    const float mix   = juce::jlimit(0.0f, 1.0f, settings.mix);

    if (mix < 1.0e-5f)
        return;

    const float sr       = static_cast<float>(juce::jmax(1.0, sampleRate));
    const float msToSamp = sr * 0.001f;

    // === Comb delay times: Spring â†’ Plate â†’ Hall via Blur ==================
    // Spring (blur=0): slightly irregular short delays → comb resonances (metallic)
    //   Ratios are NON-arithmetic to avoid the "boxy" modal coloration that
    //   arises when delays are evenly spaced.
    // Hall (blur=1): wider, coprime-ish delays → dense, non-resonant open tail.
    static constexpr float springCombMs[4] = { 23.7f, 27.5f, 29.2f, 32.1f };
    static constexpr float hallCombMs[4]   = { 41.0f, 53.5f, 65.2f, 80.3f };
    // Stereo width: channel 1 uses slightly offset delays to avoid mono collapse
    static constexpr float stereoOffMs[4]  = { 0.0f, 2.3f, 0.0f, 1.8f };

    const float sizeScale    = 0.45f + 1.35f * size;  // 0.45..1.80Ã—
    const int   maxCombSamp  = static_cast<int>(state.combLines[0].size());
    const int   maxAPFSamp   = static_cast<int>(state.inAPFLines[0].size());
    const int   maxPreSamp   = static_cast<int>(state.preDelayLine.size());

    float combSampF[4];
    int   combSampI[4];
    float avgCombDelaySec = 0.0f;
    for (int ci = 0; ci < 4; ++ci)
    {
        const float msBase   = springCombMs[ci] + (hallCombMs[ci] - springCombMs[ci]) * blur;
        const float msStereo = msBase + static_cast<float>(channel) * stereoOffMs[ci];
        combSampF[ci] = juce::jlimit(2.0f,
                                      static_cast<float>(maxCombSamp - 4),
                                      msStereo * sizeScale * msToSamp);
        combSampI[ci] = static_cast<int>(combSampF[ci] + 0.5f);
        avgCombDelaySec += combSampF[ci] / sr;
    }
    avgCombDelaySec /= 4.0f;

    // === APF delays (vary with blur and size for wider diffusion at high blur) =
    const int inAPFD[2] = {
        juce::jlimit(1, maxAPFSamp - 2, static_cast<int>(( 5.0f +  9.0f * blur +  7.0f * size) * msToSamp + 0.5f)),
        juce::jlimit(1, maxAPFSamp - 2, static_cast<int>(( 9.0f + 14.0f * blur + 11.0f * size) * msToSamp + 0.5f))
    };
    const int outAPFD[2] = {
        juce::jlimit(1, maxAPFSamp - 2, static_cast<int>((11.0f +  8.0f * blur +  9.0f * size) * msToSamp + 0.5f)),
        juce::jlimit(1, maxAPFSamp - 2, static_cast<int>((20.0f + 12.0f * blur + 13.0f * size) * msToSamp + 0.5f))
    };

    // === Blur-driven character =============================================
    // APF diffusion coefficient (higher = denser echo wash):
    //   blur=0 -> 0.15 (spring: sparse, distinct echoes)
    //   blur=1 -> 0.65 (hall: dense, smooth wash)
    const float inAPFG  = 0.15f + 0.50f * blur;
    const float outAPFG = 0.12f + 0.55f * blur;

    // LP cutoff in comb feedback path controls HF decay speed.
    // High cutoff = metallic (all freqs ring equally).
    // Low cutoff  = warm HF rolloff (hall-like openness, no harsh resonances).
    //   blur=0 -> fc = 18kHz (spring: practically flat, metallic)
    //   blur=1 -> fc = 2500Hz (hall: significant HF rolloff, open tail)
    const float lpCutoff = 18000.0f * std::pow(2500.0f / 18000.0f, blur);
    const float lpAlpha  = juce::jlimit(0.0f, 0.9999f,
                                         1.0f - std::exp(-juce::MathConstants<float>::twoPi
                                                         * lpCutoff / sr));

    // === RT60-based feedback gain =========================================
    // Maps decay [0..1] -> RT60 [0.5s..4.5s].
    // g = 10^(-3 * delaySeconds / RT60) is the physically correct formula.
    // Using the average comb delay for the per-frame computation.
    const float rt60Sec     = 0.5f + 4.0f * decay;  // 0.5 .. 4.5 seconds
    const float feedbackGain = juce::jlimit(0.10f, 0.97f,
                                             std::pow(10.0f, -3.0f * avgCombDelaySec / rt60Sec));

    // LFO: spring "boing" modulation (only significant at low Blur).
    // Modulates the read pointer of two combs in opposite directions.
    //   blur=0, size=1 â†’ Â±2.5 samples â‰ˆ Â±0.05ms slow pitch wobble
    //   blur=1         â†’ 0 (clean hall, no modulation)
    const float lfoDepth = (1.0f - blur) * (1.0f - blur) * size * 2.5f;
    const float lfoInc   = juce::MathConstants<float>::twoPi * 0.65f / sr;

    // Pre-delay (room arrival gap, size-scaled)
    const int preDelaySamples = juce::jlimit(0,
                                              maxPreSamp - 2,
                                              static_cast<int>(size * 18.0f * msToSamp + 0.5f));

    // === Mix gain =========================================================
    // At steady state a feedback comb with gain g amplifies by 1/(1-g).
    // Compensate by scaling wet with (1-g) so Mix=1 stays near unity.
    // The soft-limiter below catches residual peaks.
    const float dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
    const float wetGain = std::sin(mix * juce::MathConstants<float>::halfPi)
                        * juce::jlimit(0.04f, 0.55f, (1.0f - feedbackGain) * 2.2f);

    // === Per-sample processing ============================================
    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = buffer[i];
        float x = dry;

        // Pre-delay
        if (preDelaySamples > 0)
        {
            const int readPos = ((state.preWrite - preDelaySamples) % maxPreSamp + maxPreSamp) % maxPreSamp;
            const float preDel = state.preDelayLine[static_cast<size_t>(readPos)];
            state.preDelayLine[static_cast<size_t>(state.preWrite)] = x;
            state.preWrite = (state.preWrite + 1) % maxPreSamp;
            x = preDel;
        }

        // Input APF diffusion (weak at low Blur â†’ sparse/metallic, strong at high â†’ diffuse)
        x = apf(x, inAPFG, state.inAPFLines[0], state.inAPFWrite[0], inAPFD[0]);
        x = apf(x, inAPFG, state.inAPFLines[1], state.inAPFWrite[1], inAPFD[1]);

        // LFO update
        state.lfoPhase += lfoInc;
        if (state.lfoPhase > juce::MathConstants<float>::twoPi)
            state.lfoPhase -= juce::MathConstants<float>::twoPi;
        const float lfoMod = std::sin(state.lfoPhase) * lfoDepth;

        // 4 parallel feedback comb filters.
        // Combs 0+2: LFO-modulated (spring shimmer/boing at low Blur).
        // Combs 1+3: static integer delay (CPU-efficient).
        const float c0 = combFrac(x, feedbackGain, lpAlpha, state.combDamp[0],
                                   state.combLines[0], state.combWrite[0],
                                   combSampF[0] + lfoMod);
        const float c1 = comb(x, feedbackGain, lpAlpha, state.combDamp[1],
                               state.combLines[1], state.combWrite[1], combSampI[1]);
        const float c2 = combFrac(x, feedbackGain, lpAlpha, state.combDamp[2],
                                   state.combLines[2], state.combWrite[2],
                                   combSampF[2] - lfoMod * 0.7f);
        const float c3 = comb(x, feedbackGain, lpAlpha, state.combDamp[3],
                               state.combLines[3], state.combWrite[3], combSampI[3]);

        float y = (c0 + c1 + c2 + c3) * 0.25f;

        // Output APF diffusion (further smooths the comb output at high Blur)
        y = apf(y, outAPFG, state.outAPFLines[0], state.outAPFWrite[0], outAPFD[0]);
        y = apf(y, outAPFG, state.outAPFLines[1], state.outAPFWrite[1], outAPFD[1]);

        // Soft-limit to catch any residual peaks (unity drive → no audible
        // colouring at normal levels, only clips the occasional spike).
        y = std::tanh(y * 1.5f) * (1.0f / 1.5f);

        buffer[i] = dry * dryGain + y * wetGain;
    }
}
} // space_blur

