#include "TimelineData.h"
#include <algorithm>

void TimelineData::addKeyframe(int objectIndex, double timeSec, float value)
{
    juce::ScopedLock sl(lock);

    auto& keys = tracks[objectIndex];
    value = juce::jlimit(0.0f, 1.0f, value);

    for (auto& k : keys)
    {
        if (std::abs(k.timeSec - timeSec) < 1.0e-3)
        {
            k.value = value;
            return;
        }
    }

    keys.push_back({ timeSec, value });
    std::sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b)
    {
        return a.timeSec < b.timeSec;
    });
}

void TimelineData::deleteKeyframe(int objectIndex, double timeSec)
{
    juce::ScopedLock sl(lock);

    auto it = tracks.find(objectIndex);
    if (it == tracks.end())
        return;

    auto& keys = it->second;
    keys.erase(std::remove_if(keys.begin(), keys.end(),
        [timeSec](const Keyframe& k)
        {
            return std::abs(k.timeSec - timeSec) < 1.0e-3;
        }),
        keys.end());
}

float TimelineData::getInterpolatedValue(int objectIndex, double timeSec) const
{
    juce::ScopedLock sl(lock);

    auto it = tracks.find(objectIndex);
    if (it == tracks.end() || it->second.empty())
        return 1.0f;

    const auto& keys = it->second;
    if (timeSec <= keys.front().timeSec)
        return keys.front().value;
    if (timeSec >= keys.back().timeSec)
        return keys.back().value;

    for (size_t i = 0; i + 1 < keys.size(); ++i)
    {
        const auto& a = keys[i];
        const auto& b = keys[i + 1];
        if (timeSec >= a.timeSec && timeSec <= b.timeSec)
        {
            const double span = b.timeSec - a.timeSec;
            const float t = (span > 1.0e-9) ? static_cast<float>((timeSec - a.timeSec) / span) : 0.0f;
            return a.value + t * (b.value - a.value);
        }
    }

    return 1.0f;
}

std::vector<TimelineData::Keyframe> TimelineData::getKeyframes(int objectIndex) const
{
    juce::ScopedLock sl(lock);

    auto it = tracks.find(objectIndex);
    if (it == tracks.end())
        return {};

    return it->second;
}

juce::ValueTree TimelineData::toValueTree() const
{
    juce::ScopedLock sl(lock);

    juce::ValueTree root("TimelineData");
    for (const auto& [objectIndex, keys] : tracks)
    {
        juce::ValueTree track("Track");
        track.setProperty("objectIndex", objectIndex, nullptr);

        for (const auto& k : keys)
        {
            juce::ValueTree node("Keyframe");
            node.setProperty("timeSec", k.timeSec, nullptr);
            node.setProperty("value", k.value, nullptr);
            track.addChild(node, -1, nullptr);
        }

        root.addChild(track, -1, nullptr);
    }

    return root;
}

void TimelineData::fromValueTree(const juce::ValueTree& tree)
{
    juce::ScopedLock sl(lock);
    tracks.clear();

    if (!tree.isValid() || !tree.hasType("TimelineData"))
        return;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto track = tree.getChild(i);
        if (!track.hasType("Track"))
            continue;

        const int objectIndex = static_cast<int>(track.getProperty("objectIndex", 0));
        auto& keys = tracks[objectIndex];

        for (int k = 0; k < track.getNumChildren(); ++k)
        {
            const auto node = track.getChild(k);
            if (!node.hasType("Keyframe"))
                continue;

            const double t = static_cast<double>(node.getProperty("timeSec", 0.0));
            const float v = static_cast<float>(node.getProperty("value", 1.0f));
            keys.push_back({ t, juce::jlimit(0.0f, 1.0f, v) });
        }

        std::sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b)
        {
            return a.timeSec < b.timeSec;
        });
    }
}
