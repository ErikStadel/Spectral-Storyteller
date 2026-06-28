#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/TimelineData.h"

class PluginProcessor;

class StoryTimelineComponent : public juce::Component,
                               private juce::Timer,
                               private juce::ScrollBar::Listener
{
public:
    explicit StoryTimelineComponent(PluginProcessor& processorRef);
    ~StoryTimelineComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    PluginProcessor& processor;

    static constexpr int rulerHeight = 22;
    static constexpr int minLaneHeight = 72;
    static constexpr int preferredVisibleLanes = 3;
    static constexpr int nameColumnWidth = 220;
    static constexpr int scrollBarWidth = 12;
    static constexpr float keyRadiusPx = 5.0f;
    static constexpr float deleteSnapRadiusPx = 14.0f;
    static constexpr double maxTimeSec = 60.0;

    int scrollOffset = 0;
    int selectedLaneIndex = -1;
    double selectedKeyTime = -1.0;
    bool draggingKeyframe = false;
    bool draggingSegmentCurvature = false;
    int curvatureDragLaneIndex = -1;
    double curvatureDragSegmentStartTime = -1.0;
    float curvatureDragStartValue = 0.0f;
    int curvatureDragStartY = 0;
    int hoveredLaneIndex = -1;
    double hoveredKeyTime = -1.0;
    juce::ScrollBar laneScrollBar { false };

    struct LaneView
    {
        juce::String effectName;
        juce::StringArray parameterNames;
        int selectedParameter = 0;
    };

    std::vector<LaneView> getVisibleLanes() const;
    int getLaneHeight(int laneCount) const;
    int getTimelineRightX() const;
    void updateScrollBar();
    juce::Rectangle<int> getLaneParameterTabArea(int rowTop, int laneHeight) const;
    juce::String formatLaneValue(const juce::String& effectName, float normalizedValue) const;
    void updateHoverState(const juce::MouseEvent& event);
    bool hitTestParameterTab(const juce::MouseEvent& event, int& laneIndex, int& parameterIndex) const;
    float applyCurvatureToT(float t, float curvature) const;

    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;
    int yToLaneRow(int y) const;
    double xToTime(int x) const;
    float timeToX(double timeSec) const;
    float yToValueInRow(int y, int rowTop) const;
    float valueToYInRow(float value, int rowTop) const;
    int getMaxVisibleRows() const;
    int getMaxScrollOffset() const;
    bool findNearestKeyframe(int x, int y, int& laneIndex, double& keyTimeSec) const;
    bool findNearestSegment(int x,
                            int y,
                            int& laneIndex,
                            double& segmentStartTimeSec,
                            float& segmentCurvature) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StoryTimelineComponent)
};
