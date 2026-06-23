#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/TimelineData.h"

class PluginProcessor;

class StoryTimelineComponent : public juce::Component, private juce::Timer
{
public:
    explicit StoryTimelineComponent(PluginProcessor& processorRef);
    ~StoryTimelineComponent() override = default;

    void paint(juce::Graphics& g) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    PluginProcessor& processor;

    static constexpr int rulerHeight = 22;
    static constexpr int rowHeight = 52;
    static constexpr int nameColumnWidth = 180;
    static constexpr float keyRadiusPx = 5.0f;
    static constexpr float deleteSnapRadiusPx = 14.0f;
    static constexpr double maxTimeSec = 60.0;

    int scrollOffset = 0;
    int selectedTrackIndex = -1;
    double selectedKeyTime = -1.0;
    bool draggingKeyframe = false;

    void timerCallback() override;
    int yToObjectRow(int y) const;
    double xToTime(int x) const;
    float timeToX(double timeSec) const;
    float yToValueInRow(int y, int rowTop) const;
    float valueToYInRow(float value, int rowTop) const;
    int getMaxVisibleRows() const;
    int getMaxScrollOffset() const;
    bool findNearestKeyframe(int x, int y, int& objectIndex, double& keyTimeSec) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StoryTimelineComponent)
};
