#include "SpaceBlur.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace space_blur
{
namespace
{
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

    inline float fracDelay(std::vector<float>& line, int wp, float delaySamplesF)
    {
        const int sz = static_cast<int>(line.size());
        float readF = static_cast<float>(wp) - delaySamplesF;
        while (readF < 0.0f) readF += static_cast<float>(sz);
        while (readF >= static_cast<float>(sz)) readF -= static_cast<float>(sz);
        
        const int r0 = static_cast<int>(readF);
        const int r1 = (r0 + 1) % sz;
        const float frac = readF - static_cast<float>(r0);
        
        return line[static_cast<size_t>(r0)] * (1.0f - frac)
             + line[static_cast<size_t>(r1)] * frac;
    }
}

void ensureState(State& state, double sampleRate)
{
    const double safeSr = juce::jmax(8000.0, sampleRate);
    if (state.initialized && std::abs(state.cachedSampleRate - safeSr) < 1.0)
        return;

    const int maxAPF = static_cast<int>(safeSr * 0.05) + 64;   // 50ms max for input diffusion
    const int maxTank = static_cast<int>(safeSr * 0.25) + 256; // 250ms max per tank line

    for (int i = 0; i < 4; ++i)
    {
        state.inAPFLines[i].assign(static_cast<size_t>(maxAPF), 0.0f);
        state.inAPFWrite[i] = 0;

        state.tankLines[i].assign(static_cast<size_t>(maxTank), 0.0f);
        state.tankWrite[i] = 0;
        state.tankDamp[i] = 0.0f;
        state.lfoPhases[i] = 0.0f;
    }

    state.cachedSampleRate = safeSr;
    state.initialized = true;
}

void processBlock(const Settings& settings, State& state, double sampleRate, int channel, float* buffer, int numSamples)
{
    ensureState(state, sampleRate);

    const float shape = juce::jlimit(0.0f, 1.0f, settings.shape);
    const float decay = juce::jlimit(0.0f, 1.0f, settings.decay);
    const float blur  = juce::jlimit(0.0f, 1.0f, settings.blur);
    const float mix   = juce::jlimit(0.0f, 1.0f, settings.mix);

    if (mix < 1.0e-5f) return;

    const float sr = static_cast<float>(juce::jmax(1.0, sampleRate));
    const float msToSamp = sr * 0.001f;

    // --- 1. APF Input Diffusion ---
    const float apfG = 0.3f + 0.45f * blur; 
    const int apfD[4] = {
        static_cast<int>(4.7f * msToSamp),
        static_cast<int>(8.3f * msToSamp),
        static_cast<int>(13.1f * msToSamp),
        static_cast<int>(21.7f * msToSamp)
    };

    // --- 2. FDN Tank Times & Modulation ---
    float plateTimesMs[4] = { 11.3f, 17.1f, 23.3f, 31.7f };
    float hallTimesMs[4]  = { 43.1f, 59.9f, 83.1f, 107.3f };
    
    const float s = shape;
    float tankDelayF[4];
    float avgDelayMs = 0.0f;
    
    const float stereoOff = (channel == 0) ? 1.0f : 1.13f;

    for (int i = 0; i < 4; ++i)
    {
        float t = plateTimesMs[i] * (1.0f - s) + hallTimesMs[i] * s;
        t *= stereoOff;
        tankDelayF[i] = t * msToSamp;
        avgDelayMs += t;
    }
    avgDelayMs *= 0.25f;

    const float modDepthMs = 0.1f + 1.9f * s; // 0.1ms to 2.0ms depth
    const float modDepthSamp = modDepthMs * msToSamp;
    
    const float lfoRates[4] = {
        0.47f * (1.0f + s),
        0.61f * (1.0f + s),
        0.79f * (1.0f + s),
        1.03f * (1.0f + s)
    };

    // --- 3. Damping and Feedback ---
    const float rt60Sec = 0.2f + 24.8f * (decay * decay); 
    const float avgDelaySec = avgDelayMs * 0.001f;
    const float fbGain = std::pow(10.0f, -3.0f * avgDelaySec / rt60Sec);
    const float maxFb = 0.999f;
    const float g = juce::jlimit(0.01f, maxFb, fbGain);

    const float dampCutoff = 4000.0f + 10000.0f * s - 2000.0f * blur;
    const float dampCutoffClipped = juce::jlimit(1000.0f, 20000.0f, dampCutoff);
    const float dampAlpha = juce::jlimit(0.0f, 0.999f, 1.0f - std::exp(-juce::MathConstants<float>::twoPi * dampCutoffClipped / sr));

    const float dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
    const float wetGain = std::sin(mix * juce::MathConstants<float>::halfPi) * juce::jlimit(0.1f, 0.8f, 1.0f - g * 0.5f);

    const float h = 0.5f;
    const int maxTankSamp = static_cast<int>(state.tankLines[0].size());

    // --- Process Loop ---
    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = buffer[i];
        float x = dry;

        // 1. Input Diffusion
        if (apfG > 0.05f)
        {
            x = apf(x, apfG, state.inAPFLines[0], state.inAPFWrite[0], apfD[0]);
            x = apf(x, apfG, state.inAPFLines[1], state.inAPFWrite[1], apfD[1]);
            x = apf(x, apfG, state.inAPFLines[2], state.inAPFWrite[2], apfD[2]);
            x = apf(x, apfG, state.inAPFLines[3], state.inAPFWrite[3], apfD[3]);
        }
        
        const float inInject = x * 0.5f;

        // 2. Read from tank (with LFO)
        float d[4];
        for (int k = 0; k < 4; ++k)
        {
            state.lfoPhases[k] += juce::MathConstants<float>::twoPi * lfoRates[k] / sr;
            if (state.lfoPhases[k] > juce::MathConstants<float>::twoPi)
                state.lfoPhases[k] -= juce::MathConstants<float>::twoPi;
                
            float mod = std::sin(state.lfoPhases[k]) * modDepthSamp;
            float readPos = tankDelayF[k] + mod;
            readPos = juce::jlimit(2.0f, static_cast<float>(maxTankSamp - 4), readPos);
            
            float raw = fracDelay(state.tankLines[k], state.tankWrite[k], readPos);
            
            state.tankDamp[k] = dampAlpha * raw + (1.0f - dampAlpha) * state.tankDamp[k];
            d[k] = state.tankDamp[k] * g;
        }
        
        // 3. Hadamard Matrix
        float v0 = h * ( d[0] + d[1] + d[2] + d[3]);
        float v1 = h * ( d[0] - d[1] + d[2] - d[3]);
        float v2 = h * ( d[0] + d[1] - d[2] - d[3]);
        float v3 = h * ( d[0] - d[1] - d[2] + d[3]);
        
        state.tankLines[0][static_cast<size_t>(state.tankWrite[0])] = v0 + inInject;
        state.tankLines[1][static_cast<size_t>(state.tankWrite[1])] = v1 + inInject;
        state.tankLines[2][static_cast<size_t>(state.tankWrite[2])] = v2 + inInject;
        state.tankLines[3][static_cast<size_t>(state.tankWrite[3])] = v3 + inInject;
        
        for (int k = 0; k < 4; ++k)
            state.tankWrite[k] = (state.tankWrite[k] + 1) % maxTankSamp;
            
        float out = (channel == 0) ? (d[0] + d[2] - d[1] - d[3]) * 0.5f
                                   : (d[0] + d[1] - d[2] - d[3]) * 0.5f;
                                   
        out = std::tanh(out * 1.2f) * (1.0f / 1.2f);
                                   
        buffer[i] = dry * dryGain + out * wetGain;
    }
}
}