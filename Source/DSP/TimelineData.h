#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <map>

class TimelineData
{
public:
    struct Keyframe
    {
        double timeSec = 0.0;
        float value = 1.0f;
    };

    void addKeyframe(int objectIndex, double timeSec, float value);
    void deleteKeyframe(int objectIndex, double timeSec);
    float getInterpolatedValue(int objectIndex, double timeSec) const;
    std::vector<Keyframe> getKeyframes(int objectIndex) const;

    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

private:
    mutable juce::CriticalSection lock;
    std::map<int, std::vector<Keyframe>> tracks;
};
