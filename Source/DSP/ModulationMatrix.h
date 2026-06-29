#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>
#include <string>

class ModulationMatrix
{
public:
    static constexpr int NUM_LFOS = 3;
    static constexpr int NUM_XY = 3;

    enum class Shape { Sine = 0, Triangle, Saw, Square };

    static const juce::StringArray& rateLabels()
    {
        static const juce::StringArray labels {
            "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bar", "4 Bar"
        };
        return labels;
    }

    struct Target
    {
        int objectId = -1;
        juce::String fxName;
        juce::String paramName;

        bool isValid() const noexcept
        {
            return objectId > 0 && fxName.isNotEmpty() && paramName.isNotEmpty();
        }

        bool operator==(const Target& o) const noexcept
        {
            return objectId == o.objectId && fxName == o.fxName && paramName == o.paramName;
        }

        juce::String toString() const
        {
            return isValid() ? (fxName + " · " + paramName) : juce::String("none");
        }
    };

    struct LFOState
    {
        std::atomic<int> rateIndex { 4 };
        std::atomic<int> shape { 0 };
        std::atomic<float> amount { 0.0f };
        std::atomic<float> phaseOffset { 0.0f };
        Target target;
        juce::CriticalSection targetLock;
    };

    struct XYState
    {
        std::atomic<float> x { 0.5f };
        std::atomic<float> y { 0.5f };
        Target targetX;
        Target targetY;
        juce::CriticalSection targetLock;
    };

    LFOState& lfo(int i)             { return lfos[static_cast<size_t>(juce::jlimit(0, NUM_LFOS - 1, i))]; }
    const LFOState& lfo(int i) const { return lfos[static_cast<size_t>(juce::jlimit(0, NUM_LFOS - 1, i))]; }
    XYState& xy(int i)               { return xys[static_cast<size_t>(juce::jlimit(0, NUM_XY - 1, i))]; }
    const XYState& xy(int i) const   { return xys[static_cast<size_t>(juce::jlimit(0, NUM_XY - 1, i))]; }

    void setTransport(double ppqPositionAtBlockStart, double bpm, bool isPlaying) noexcept
    {
        currentPpq.store(ppqPositionAtBlockStart);
        currentBpm.store(bpm > 0.0 ? bpm : 120.0);
        transportActive.store(isPlaying);
    }

    static double rateIndexToBeats(int idx) noexcept
    {
        static constexpr double table[] = { 0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        return table[juce::jlimit(0, static_cast<int>(std::size(table)) - 1, idx)];
    }

    float evalLfo(const LFOState& l) const noexcept
    {
        const double beats = rateIndexToBeats(l.rateIndex.load());
        const double ppq = currentPpq.load();

        double phase = std::fmod(ppq / beats, 1.0);
        if (phase < 0.0)
            phase += 1.0;

        phase = std::fmod(phase + static_cast<double>(l.phaseOffset.load()), 1.0);
        const float p = static_cast<float>(phase);

        float y = 0.0f;
        switch (l.shape.load())
        {
            case 0: y = std::sin(juce::MathConstants<float>::twoPi * p); break;
            case 1: y = 4.0f * std::fabs(p - 0.5f) - 1.0f; break;
            case 2: y = 2.0f * p - 1.0f; break;
            default: y = (p < 0.5f) ? 1.0f : -1.0f; break;
        }

        return y * l.amount.load();
    }

    float getModulation(int objectId, const juce::String& fxName, const juce::String& paramName) const
    {
        float sum = 0.0f;

        for (int i = 0; i < NUM_LFOS; ++i)
        {
            Target t;
            {
                juce::ScopedLock sl(lfos[static_cast<size_t>(i)].targetLock);
                t = lfos[static_cast<size_t>(i)].target;
            }

            if (t.objectId == objectId && t.fxName == fxName && t.paramName == paramName)
                sum += evalLfo(lfos[static_cast<size_t>(i)]);
        }

        for (int i = 0; i < NUM_XY; ++i)
        {
            Target tx;
            Target ty;
            {
                juce::ScopedLock sl(xys[static_cast<size_t>(i)].targetLock);
                tx = xys[static_cast<size_t>(i)].targetX;
                ty = xys[static_cast<size_t>(i)].targetY;
            }

            if (tx.objectId == objectId && tx.fxName == fxName && tx.paramName == paramName)
                sum += (xys[static_cast<size_t>(i)].x.load() - 0.5f) * 2.0f * 0.5f;
            if (ty.objectId == objectId && ty.fxName == fxName && ty.paramName == paramName)
                sum += (xys[static_cast<size_t>(i)].y.load() - 0.5f) * 2.0f * 0.5f;
        }

        return juce::jlimit(-1.0f, 1.0f, sum);
    }

    float getLfoVisualValue(int i) const
    {
        return evalLfo(lfo(i));
    }

    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& v);

private:
    std::array<LFOState, NUM_LFOS> lfos;
    std::array<XYState, NUM_XY> xys;

    std::atomic<double> currentPpq { 0.0 };
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<bool> transportActive { false };
};
