#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <string>

class ModulationMatrix
{
public:
    static constexpr int NUM_LFOS = 3;
    static constexpr int NUM_XY   = 3;

    enum class Shape { Sine = 0, Triangle, Saw, Square };

    // Rate sync values in beats (1 = 1/4, 0.25 = 1/16, 4 = 1 bar @ 4/4)
    static constexpr std::array<double, 9> kRateBeats {
        4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 1.0/3.0, 1.0/6.0, 1.0/12.0
    };
    static const juce::StringArray& rateLabels()
    {
        static const juce::StringArray l { "1 Bar","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T" };
        return l;
    }

    struct Target
    {
        int objectId = -1;
        juce::String fxName;
        juce::String paramName;
        bool isValid() const noexcept { return objectId > 0 && fxName.isNotEmpty() && paramName.isNotEmpty(); }
        bool operator==(const Target& o) const noexcept
        { return objectId==o.objectId && fxName==o.fxName && paramName==o.paramName; }
        juce::String toString() const
        { return isValid() ? (fxName + " · " + paramName) : juce::String("—"); }
    };

    struct LFOState
    {
        std::atomic<int>   rateIndex { 2 };       // 1/4
        std::atomic<int>   shape     { (int)Shape::Sine };
        std::atomic<float> amount    { 0.5f };    // 0..1 (bipolar amplitude)
        Target target;
        juce::CriticalSection targetLock;
    };

    struct XYState
    {
        std::atomic<float> x { 0.5f };
        std::atomic<float> y { 0.5f };
        Target targetX, targetY;
        juce::CriticalSection targetLock;
    };

    LFOState& lfo(int i)            { return lfos[(size_t) juce::jlimit(0, NUM_LFOS-1, i)]; }
    const LFOState& lfo(int i) const{ return lfos[(size_t) juce::jlimit(0, NUM_LFOS-1, i)]; }
    XYState&  xy (int i)            { return xys [(size_t) juce::jlimit(0, NUM_XY  -1, i)]; }
    const XYState&  xy (int i) const{ return xys [(size_t) juce::jlimit(0, NUM_XY  -1, i)]; }

    /** Returns additive modulation value in [-amount .. +amount] for a given target,
        summed over all sources pointing at it. Call from FX param lookup. */
    float getModulation(int objectId, const juce::String& fxName, const juce::String& paramName) const
    {
        float sum = 0.0f;
        for (int i = 0; i < NUM_LFOS; ++i)
        {
            Target t; { juce::ScopedLock sl(lfos[i].targetLock); t = lfos[i].target; }
            if (t.objectId==objectId && t.fxName==fxName && t.paramName==paramName)
                sum += currentLfoValue[i].load() * lfos[i].amount.load();
        }
        for (int i = 0; i < NUM_XY; ++i)
        {
            Target tx, ty;
            { juce::ScopedLock sl(xys[i].targetLock); tx = xys[i].targetX; ty = xys[i].targetY; }
            if (tx.objectId==objectId && tx.fxName==fxName && tx.paramName==paramName)
                sum += (xys[i].x.load() - 0.5f) * 2.0f * 0.5f; // bipolar, depth 0.5
            if (ty.objectId==objectId && ty.fxName==fxName && ty.paramName==paramName)
                sum += (xys[i].y.load() - 0.5f) * 2.0f * 0.5f;
        }
        return juce::jlimit(-1.0f, 1.0f, sum);
    }

    /** Advance LFO phases. Call once per processBlock with the current bpm + numSamples + sampleRate. */
    void advance(double bpm, double sampleRate, int numSamples) noexcept
    {
        if (bpm <= 0.0 || sampleRate <= 0.0) return;
        const double secondsPerBeat = 60.0 / bpm;
        const double dt = (double) numSamples / sampleRate;

        for (int i = 0; i < NUM_LFOS; ++i)
        {
            const int idx = juce::jlimit(0, (int) kRateBeats.size()-1, lfos[i].rateIndex.load());
            const double periodSec = kRateBeats[(size_t) idx] * secondsPerBeat;
            if (periodSec <= 1.0e-6) continue;
            double p = lfoPhase[i] + dt / periodSec;
            p -= std::floor(p);
            lfoPhase[i] = p;
            currentLfoValue[i].store(evalShape((Shape) lfos[i].shape.load(), (float) p));
        }
    }

    float getLfoVisualValue(int i) const { return currentLfoValue[i].load(); }

    // --- State persistence ---
    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& v);

private:
    static float evalShape(Shape s, float phase) noexcept
    {
        switch (s)
        {
            case Shape::Sine:     return std::sin(phase * juce::MathConstants<float>::twoPi);
            case Shape::Triangle: return 4.0f * std::abs(phase - 0.5f) - 1.0f;
            case Shape::Saw:      return 2.0f * phase - 1.0f;
            case Shape::Square:   return phase < 0.5f ? 1.0f : -1.0f;
        }
        return 0.0f;
    }

    std::array<LFOState, NUM_LFOS> lfos;
    std::array<XYState,  NUM_XY>   xys;
    std::array<double, NUM_LFOS> lfoPhase {};
    std::array<std::atomic<float>, NUM_LFOS> currentLfoValue {};
};
