#include "SpaceBlur.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace space_blur
{
namespace
{
    // Schroeder all-pass filter
    inline float apf(float x, float g, std::vector<float>& line, int& wp, int delaySamples)
    {
        const int sz = static_cast<int>(line.size());
        const int readPos = ((wp - delaySamples) % sz + sz) % sz;
        const float delayed = line[static_cast<size_t>(readPos)];
        const float w = x + g * delayed;
        line[static_cast<size_t>(wp)] = w;
        wp = (wp + 1) % sz;
        return delayed - g * w;
    }

    // Feedback comb with LP damping and fractional delay (for modulation)
    inline float combFrac(float x, float g, float dampFactor, float& dampState,
                          std::vector<float>& line, int& wp, float delaySamplesF)
    {
        const int sz = static_cast<int>(line.size());
        float readF = static_cast<float>(wp) - delaySamplesF;
        readF = std::fmod(readF, static_cast<float>(sz));
        if (readF < 0.0f) readF += static_cast<float>(sz);
        
        const int r0 = static_cast<int>(readF) % sz;
        const int r1 = (r0 + 1) % sz;
        const float frac = readF - std::floor(readF);
        
        const float out = line[static_cast<size_t>(r0)] * (1.0f - frac)
                        + line[static_cast<size_t>(r1)] * frac;
                        
        dampState = dampFactor * out + (1.0f - dampFactor) * dampState;
        line[static_cast<size_t>(wp)] = x + dampState * g;
        wp = (wp + 1) % sz;
        return out;
    }
}

void ensureState(State& state, double sampleRate)
{
    const double safeSr = juce::jmax(8000.0, sampleRate);
    if (state.initialized && std::abs(state.cachedSampleRate - safeSr) < 1.0)
        return;

    const int maxPreSamp  = static_cast<int>(safeSr * 0.150) + 128; // 150ms max
    const int maxDispSamp = static_cast<int>(safeSr * 0.015) + 64;  // 15ms max
    const int maxCombSamp = static_cast<int>(safeSr * 0.350) + 256; 
    const int maxAPFSamp  = static_cast<int>(safeSr * 0.095) + 128; // 95ms max für breiteren Wash

    state.preDelayLine.assign(static_cast<size_t>(maxPreSamp), 0.0f);
    state.preWrite = 0;

    for (int i = 0; i < 3; ++i) {
        state.dispLines[i].assign(static_cast<size_t>(maxDispSamp), 0.0f);
        state.dispWrite[i] = 0;
    }
    for (int i = 0; i < 8; ++i) {
        state.combLines[i].assign(static_cast<size_t>(maxCombSamp), 0.0f);
        state.combWrite[i] = 0;
        state.combDamp[i] = 0.0f;
    }
    for (int i = 0; i < 4; ++i) {
        state.outAPFLines[i].assign(static_cast<size_t>(maxAPFSamp), 0.0f);
        state.outAPFWrite[i] = 0;
    }

    state.lfoPhase1 = 0.0f;
    state.lfoPhase2 = 0.0f;
    state.cachedSampleRate = safeSr;
    state.initialized = true;
}

void processBlock(const Settings& settings, State& state, double sampleRate, int channel, float* buffer, int numSamples)
{
    ensureState(state, sampleRate);

    const float size  = juce::jlimit(0.0f, 1.0f, settings.size);
    const float decay = juce::jlimit(0.0f, 1.0f, settings.decay);
    const float blur  = juce::jlimit(0.0f, 1.0f, settings.blur);
    const float mix   = juce::jlimit(0.0f, 1.0f, settings.mix);

    if (mix < 1.0e-5f) return;

    const float sr = static_cast<float>(juce::jmax(1.0, sampleRate));
    const float msToSamp = sr * 0.001f;

    // === 1. Morphing Delay Times (Spring -> Plate -> Hall) =================
    // 8 Combs für hohe Echo-Dichte (verhindert "Boxiness").
    // Spring (blur=0): geclustert -> metallische Resonanzen
    // Hall (blur=1): weit & teilerfremd -> offener, weicher Decay
    static constexpr float springCombMs[8] = { 21.3f, 25.7f, 29.1f, 33.8f, 38.2f, 42.5f, 47.9f, 53.1f };
    static constexpr float hallCombMs[8]   = { 75.1f, 91.8f, 119.2f, 148.7f, 177.4f, 208.9f, 249.1f, 292.5f };
    
    const float stereoOff = (channel == 0) ? 0.0f : (2.5f + 8.5f * blur); // 2.5ms bei Spring, 11ms bei Hall
    const float sizeScale = 0.5f + 1.5f * size; 
    
    const int maxCombSamp = static_cast<int>(state.combLines[0].size());
    const int maxPreSamp  = static_cast<int>(state.preDelayLine.size());
    const int maxDispSamp = static_cast<int>(state.dispLines[0].size());
    const int maxAPFSamp  = static_cast<int>(state.outAPFLines[0].size());

    float combSampF[8];
    float avgCombDelaySec = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        const float msBase = springCombMs[i] + (hallCombMs[i] - springCombMs[i]) * blur;
        combSampF[i] = juce::jlimit(2.0f, static_cast<float>(maxCombSamp - 4), (msBase + stereoOff) * sizeScale * msToSamp);
        avgCombDelaySec += combSampF[i] / sr;
    }
    avgCombDelaySec /= 8.0f;

    // === 2. Dispersion Network (Der Spring "Drip") ========================
    // 3 kurze APFs in Serie. Erzeugt Phasendispersion (der "Plink"-Sound).
    // Aktiv bei blur=0, faded sanft aus Richtung Plate/Hall.
    const int dispD[3] = {
        juce::jlimit(1, maxDispSamp - 2, static_cast<int>(2.1f * msToSamp)),
        juce::jlimit(1, maxDispSamp - 2, static_cast<int>(3.4f * msToSamp)),
        juce::jlimit(1, maxDispSamp - 2, static_cast<int>(5.7f * msToSamp))
    };
    const float dispGain = 0.75f * (1.0f - blur); 


    // === 4. HF Damping (Air Absorption) ===================================
    // Spring: hell/metallisch (hohe Grenzfrequenz). Hall: warm/offen (tiefe GF).
    const float lpCutoff = 18000.0f * std::pow(9000.0f / 18000.0f, blur * 0.6f); 
    const float lpAlpha = juce::jlimit(0.0f, 0.9999f,
        1.0f - std::exp(-juce::MathConstants<float>::twoPi * lpCutoff / sr));

    // === 5. RT60 Feedback Gain ============================================
    const float rt60Sec = 0.3f + 5.7f * decay; // 0.3s bis exakt 6.0s Nachhallzeit
    const float feedbackGain = juce::jlimit(0.10f, 0.98f,
        std::pow(10.0f, -3.0f * avgCombDelaySec / rt60Sec));

    // === 6. LFO Modulation ================================================
   const float lfoDepth = (1.0f - blur * 0.2f) * size * (4.0f + 8.0f * blur); 
    const float lfoInc1 = juce::MathConstants<float>::twoPi * (0.4f + 0.8f * blur) / sr;
    const float lfoInc2 = juce::MathConstants<float>::twoPi * (0.55f + 0.9f * blur) / sr;

        // === 7. Early Reflections (Breiter Stereo-Spread) ====================
    const int preDelaySamples = juce::jlimit(0, maxPreSamp - 2, static_cast<int>((10.0f + 45.0f * size) * msToSamp + 0.5f));
    const float erMix = blur * 0.45f; // Bis zu 45% ER-Anteil für den Open Hall

    // Stereo Spread: Linker und rechter Kanal bekommen leicht asymmetrische Taps
    const float erStereoSpread = (channel == 0) ? 1.0f : 1.13f; 
    const int erTaps[4] = { 
        static_cast<int>(preDelaySamples * erStereoSpread), 
        static_cast<int>(preDelaySamples * 0.73f * erStereoSpread), 
        static_cast<int>(preDelaySamples * 0.41f * (2.0f - erStereoSpread)), 
        static_cast<int>(preDelaySamples * 0.17f * (2.0f - erStereoSpread)) 
    };

    // === 8. Output Diffusion (Längere APFs für breiteren Wash) ==========
    const int outAPFD[4] = {
        juce::jlimit(1, maxAPFSamp - 2, static_cast<int>((11.3f + 18.0f * blur) * msToSamp)),
        juce::jlimit(1, maxAPFSamp - 2, static_cast<int>((15.8f + 26.0f * blur) * msToSamp)),
        juce::jlimit(1, maxAPFSamp - 2, static_cast<int>((21.2f + 38.0f * blur) * msToSamp)),
        juce::jlimit(1, maxAPFSamp - 2, static_cast<int>((28.7f + 52.0f * blur) * msToSamp)) // ~80ms Diffusion
    };
    const float outAPFG = 0.55f + 0.20f * blur; // Stärkere Diffusion bei Hall

    // Mix Gains
    const float dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
    const float wetGain = std::sin(mix * juce::MathConstants<float>::halfPi) * 0.65f
                        * juce::jlimit(0.05f, 0.6f, (1.0f - feedbackGain) * 2.5f);

    // === Per-Sample Processing ============================================
    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = buffer[i];
        float x = dry;

        // Pre-Delay
        if (preDelaySamples > 0)
        {
            const int readPos = ((state.preWrite - preDelaySamples) % maxPreSamp + maxPreSamp) % maxPreSamp;
            const float preDel = state.preDelayLine[static_cast<size_t>(readPos)];
            state.preDelayLine[static_cast<size_t>(state.preWrite)] = x;
            state.preWrite = (state.preWrite + 1) % maxPreSamp;
            x = preDel;
        }

        // Early Reflections (4-Tap Pseudo-Raum, skaliert mit der Size!)
        float erSignal = 0.0f;
        if (erMix > 0.01f && preDelaySamples > 10)
        {
            const int erTaps[4] = { 
                preDelaySamples, 
                static_cast<int>(preDelaySamples * 0.73f), 
                static_cast<int>(preDelaySamples * 0.41f), 
                static_cast<int>(preDelaySamples * 0.17f) 
            };
            for(int t=0; t<4; ++t) {
                const int rPos = ((state.preWrite - erTaps[t]) % maxPreSamp + maxPreSamp) % maxPreSamp;
                erSignal += state.preDelayLine[static_cast<size_t>(rPos)];
            }
            erSignal *= 0.25f;
        }

        // Dispersion Network (Spring "Drip")
        float dispX = x;
        if (dispGain > 0.05f)
        {
            dispX = apf(dispX, dispGain, state.dispLines[0], state.dispWrite[0], dispD[0]);
            dispX = apf(dispX, dispGain, state.dispLines[1], state.dispWrite[1], dispD[1]);
            dispX = apf(dispX, dispGain, state.dispLines[2], state.dispWrite[2], dispD[2]);
        }

        // LFO Update
        state.lfoPhase1 += lfoInc1;
        if (state.lfoPhase1 > juce::MathConstants<float>::twoPi) state.lfoPhase1 -= juce::MathConstants<float>::twoPi;
        state.lfoPhase2 += lfoInc2;
        if (state.lfoPhase2 > juce::MathConstants<float>::twoPi) state.lfoPhase2 -= juce::MathConstants<float>::twoPi;

        const float mod1 = std::sin(state.lfoPhase1) * lfoDepth;
        const float mod2 = std::sin(state.lfoPhase2) * lfoDepth * 0.8f;

        // 8 Parallel Comb Filters
        float combSum = 0.0f;
        combSum += combFrac(dispX, feedbackGain, lpAlpha, state.combDamp[0], state.combLines[0], state.combWrite[0], combSampF[0] + mod1);
        combSum += combFrac(dispX, feedbackGain, lpAlpha, state.combDamp[1], state.combLines[1], state.combWrite[1], combSampF[1] - mod2);
        combSum += combFrac(dispX, feedbackGain, lpAlpha, state.combDamp[2], state.combLines[2], state.combWrite[2], combSampF[2] + mod2 * 0.7f);
        combSum += combFrac(dispX, feedbackGain, lpAlpha, state.combDamp[3], state.combLines[3], state.combWrite[3], combSampF[3] - mod1 * 0.6f);
        combSum += combFrac(dispX, feedbackGain, lpAlpha, state.combDamp[4], state.combLines[4], state.combWrite[4], combSampF[4] + mod1 * 0.9f);
        combSum += combFrac(dispX, feedbackGain, lpAlpha, state.combDamp[5], state.combLines[5], state.combWrite[5], combSampF[5] - mod2 * 0.5f);
        combSum += combFrac(dispX, feedbackGain, lpAlpha, state.combDamp[6], state.combLines[6], state.combWrite[6], combSampF[6] + mod2 * 0.8f);
        combSum += combFrac(dispX, feedbackGain, lpAlpha, state.combDamp[7], state.combLines[7], state.combWrite[7], combSampF[7] - mod1 * 0.4f);
        
        float y = combSum * 0.125f; // Normalize 8 combs

        // Output Diffusion
        y = apf(y, outAPFG, state.outAPFLines[0], state.outAPFWrite[0], outAPFD[0]);
        y = apf(y, outAPFG, state.outAPFLines[1], state.outAPFWrite[1], outAPFD[1]);
        y = apf(y, outAPFG, state.outAPFLines[2], state.outAPFWrite[2], outAPFD[2]);
        y = apf(y, outAPFG, state.outAPFLines[3], state.outAPFWrite[3], outAPFD[3]);

        // Mix Early Reflections with Late Reverb
        y = y * (1.0f - erMix) + erSignal * erMix;

        // Soft Limiter (fängt Peaks ab, ohne hart zu clippen)
        y = std::tanh(y * 1.2f) * (1.0f / 1.2f);

        buffer[i] = dry * dryGain + y * wetGain;
    }
}

} // namespace space_blur