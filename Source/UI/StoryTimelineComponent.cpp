#include "StoryTimelineComponent.h"
#include "../PluginProcessor.h"

StoryTimelineComponent::StoryTimelineComponent(PluginProcessor& processorRef)
    : processor(processorRef)
{
    setOpaque(true);
    laneScrollBar.setAutoHide(false);
    laneScrollBar.addListener(this);
    addAndMakeVisible(laneScrollBar);
    startTimerHz(30);
}

void StoryTimelineComponent::resized()
{
    updateScrollBar();
    laneScrollBar.setBounds(getWidth() - scrollBarWidth,
                            rulerHeight,
                            scrollBarWidth,
                            juce::jmax(0, getHeight() - rulerHeight));
}

void StoryTimelineComponent::refresh()
{
    selectedLaneIndex = -1;
    selectedKeyTime = -1.0;
    draggingKeyframe = false;
    draggingSegmentCurvature = false;
    curvatureDragLaneIndex = -1;
    curvatureDragSegmentStartTime = -1.0;
    curvatureDragStartValue = 0.0f;
    curvatureDragStartY = 0;
    hoveredLaneIndex = -1;
    hoveredKeyTime = -1.0;
    repaint();
}

void StoryTimelineComponent::timerCallback()
{
    repaint();
}

std::vector<StoryTimelineComponent::LaneView> StoryTimelineComponent::getVisibleLanes() const
{
    std::vector<LaneView> lanes;

    const int selectedObjectId = processor.getSelectedObjectId();
    if (selectedObjectId < 0)
        return lanes;

    const auto fxChain = processor.getFxChainForObject(selectedObjectId);
    for (const auto& fx : fxChain)
    {
        if (!fx.enabled)
            continue;

        LaneView lane;
        lane.effectName = fx.name;
        lane.selectedParameter = fx.selectedParameterIndex;
        for (const auto& param : fx.parameters)
            lane.parameterNames.add(param.name);

        if (lane.parameterNames.isEmpty())
            lane.parameterNames.add("Amount");

        lane.selectedParameter = juce::jlimit(0, lane.parameterNames.size() - 1, lane.selectedParameter);
        lanes.push_back(std::move(lane));
    }

    return lanes;
}

int StoryTimelineComponent::getLaneHeight(int laneCount) const
{
    juce::ignoreUnused(laneCount);
    const int available = juce::jmax(minLaneHeight, getHeight() - rulerHeight);
    return juce::jmax(minLaneHeight, available / preferredVisibleLanes);
}

int StoryTimelineComponent::getTimelineRightX() const
{
    return getWidth() - (laneScrollBar.isVisible() ? scrollBarWidth : 0);
}

void StoryTimelineComponent::updateScrollBar()
{
    const auto lanes = getVisibleLanes();
    const int visibleRows = getMaxVisibleRows();
    const int maxOffset = juce::jmax(0, static_cast<int>(lanes.size()) - visibleRows);

    scrollOffset = juce::jlimit(0, maxOffset, scrollOffset);

    const bool shouldShow = maxOffset > 0;
    laneScrollBar.setVisible(shouldShow);
    if (shouldShow)
    {
        laneScrollBar.setRangeLimits(0.0, static_cast<double>(lanes.size()));
        laneScrollBar.setCurrentRange(static_cast<double>(scrollOffset), static_cast<double>(visibleRows));
    }
}

juce::Rectangle<int> StoryTimelineComponent::getLaneParameterTabArea(int rowTop, int laneHeight) const
{
    return juce::Rectangle<int>(8, rowTop + 26, nameColumnWidth - 16, juce::jmax(20, laneHeight - 30));
}

juce::String StoryTimelineComponent::formatLaneValue(const juce::String& effectName, float normalizedValue) const
{
    if (effectName.equalsIgnoreCase("Pitch"))
    {
        const float semitones = (normalizedValue - 0.5f) * 4.0f;
        const float rounded = std::round(semitones * 10.0f) / 10.0f;
        return juce::String(rounded, 1) + " st";
    }

    if (effectName.equalsIgnoreCase("Volume"))
    {
        const float gainPercent = juce::jlimit(0.0f, 1.5f, normalizedValue) * 200.0f;
        return juce::String(static_cast<int>(std::round(gainPercent))) + "%";
    }

    return juce::String(normalizedValue, 2);
}

float StoryTimelineComponent::applyCurvatureToT(float t, float curvature) const
{
    t = juce::jlimit(0.0f, 1.0f, t);
    curvature = juce::jlimit(-1.0f, 1.0f, curvature);

    if (std::abs(curvature) < 1.0e-4f)
        return t;

    const float shape = 1.0f + 4.0f * std::abs(curvature);
    if (curvature > 0.0f)
        return 1.0f - std::pow(1.0f - t, shape);

    return std::pow(t, shape);
}

namespace
{
float effectiveSegmentCurvature(float curvature, float startValue, float endValue)
{
    return (endValue < startValue) ? -curvature : curvature;
}
}

void StoryTimelineComponent::updateHoverState(const juce::MouseEvent& event)
{
    hoveredLaneIndex = -1;
    hoveredKeyTime = -1.0;

    const int selectedObjectId = processor.getSelectedObjectId();
    if (selectedObjectId < 0)
        return;

    const auto lanes = getVisibleLanes();
    const int laneIndex = yToLaneRow(event.y);
    if (laneIndex < 0 || laneIndex >= static_cast<int>(lanes.size()))
        return;

    const int laneHeight = getLaneHeight(static_cast<int>(lanes.size()));
    const int visibleRow = laneIndex - scrollOffset;
    if (visibleRow < 0)
        return;

    const int rowTop = rulerHeight + visibleRow * laneHeight;
    const auto& lane = lanes[static_cast<size_t>(laneIndex)];
    const auto paramName = lane.parameterNames[lane.selectedParameter];
    const auto keys = processor.getFxAutomationKeyframes(selectedObjectId, lane.effectName, paramName);

    float bestDistanceSq = deleteSnapRadiusPx * deleteSnapRadiusPx;
    for (const auto& k : keys)
    {
        const float kx = timeToX(k.timeSec);
        const float ky = valueToYInRow(k.value, rowTop);
        const float dx = static_cast<float>(event.x) - kx;
        const float dy = static_cast<float>(event.y) - ky;
        const float distSq = dx * dx + dy * dy;

        if (distSq <= bestDistanceSq)
        {
            bestDistanceSq = distSq;
            hoveredLaneIndex = laneIndex;
            hoveredKeyTime = k.timeSec;
        }
    }
}

bool StoryTimelineComponent::hitTestParameterTab(const juce::MouseEvent& event, int& laneIndex, int& parameterIndex) const
{
    laneIndex = -1;
    parameterIndex = -1;

    const auto lanes = getVisibleLanes();
    const int lane = yToLaneRow(event.y);
    if (lane < 0 || lane >= static_cast<int>(lanes.size()))
        return false;

    const int visibleRow = lane - scrollOffset;
    if (visibleRow < 0)
        return false;

    const int laneHeight = getLaneHeight(static_cast<int>(lanes.size()));
    const int rowTop = rulerHeight + visibleRow * laneHeight;
    const auto tabArea = getLaneParameterTabArea(rowTop, laneHeight);
    if (!tabArea.contains(event.getPosition()))
        return false;

    const auto& params = lanes[static_cast<size_t>(lane)].parameterNames;
    if (params.isEmpty())
        return false;

    const int tabW = juce::jmax(22, tabArea.getWidth() / params.size());
    for (int i = 0; i < params.size(); ++i)
    {
        juce::Rectangle<int> tab(tabArea.getX() + i * tabW, tabArea.getY(), tabW - 2, tabArea.getHeight());
        if (tab.contains(event.getPosition()))
        {
            laneIndex = lane;
            parameterIndex = i;
            return true;
        }
    }

    return false;
}

void StoryTimelineComponent::mouseDown(const juce::MouseEvent& event)
{
    const int selectedObjectId = processor.getSelectedObjectId();
    if (selectedObjectId < 0)
        return;

    int laneForTab = -1;
    int parameterIndex = -1;
    if (hitTestParameterTab(event, laneForTab, parameterIndex))
    {
        const auto lanes = getVisibleLanes();
        if (laneForTab >= 0 && laneForTab < static_cast<int>(lanes.size()))
        {
            processor.setObjectFxSelectedParameter(selectedObjectId,
                                                   lanes[static_cast<size_t>(laneForTab)].effectName,
                                                   parameterIndex);
            repaint();
        }
        return;
    }

    if (event.mods.isAltDown())
    {
        int laneIndex = -1;
        double keyTimeSec = 0.0;
        if (findNearestKeyframe(event.x, event.y, laneIndex, keyTimeSec))
        {
            const auto lanes = getVisibleLanes();
            if (laneIndex >= 0 && laneIndex < static_cast<int>(lanes.size()))
            {
                const auto& lane = lanes[static_cast<size_t>(laneIndex)];
                const auto paramName = lane.parameterNames[lane.selectedParameter];
                processor.deleteFxAutomationKeyframe(selectedObjectId, lane.effectName, paramName, keyTimeSec);
                repaint();
            }
            return;
        }

        int segLaneIndex = -1;
        double segStartTime = 0.0;
        float segCurvature = 0.0f;
        if (findNearestSegment(event.x, event.y, segLaneIndex, segStartTime, segCurvature))
        {
            draggingSegmentCurvature = true;
            curvatureDragLaneIndex = segLaneIndex;
            curvatureDragSegmentStartTime = segStartTime;
            curvatureDragStartValue = segCurvature;
            curvatureDragStartY = event.y;
            return;
        }

        return;
    }

    int laneIndex = -1;
    double keyTimeSec = 0.0;
    if (findNearestKeyframe(event.x, event.y, laneIndex, keyTimeSec))
    {
        selectedLaneIndex = laneIndex;
        selectedKeyTime = keyTimeSec;
        draggingKeyframe = true;
    }

    updateHoverState(event);
}

void StoryTimelineComponent::mouseDrag(const juce::MouseEvent& event)
{
    const int selectedObjectId = processor.getSelectedObjectId();
    if (selectedObjectId < 0)
        return;

    if (draggingSegmentCurvature)
    {
        const auto lanes = getVisibleLanes();
        if (curvatureDragLaneIndex >= 0 && curvatureDragLaneIndex < static_cast<int>(lanes.size()))
        {
            const auto& lane = lanes[static_cast<size_t>(curvatureDragLaneIndex)];
            const auto paramName = lane.parameterNames[lane.selectedParameter];
            const float delta = static_cast<float>(curvatureDragStartY - event.y) * 0.01f;
            const float nextCurvature = juce::jlimit(-1.0f, 1.0f, curvatureDragStartValue + delta);
            processor.setFxAutomationSegmentCurvature(selectedObjectId,
                                                      lane.effectName,
                                                      paramName,
                                                      curvatureDragSegmentStartTime,
                                                      nextCurvature);
            repaint();
        }
        return;
    }

    if (!draggingKeyframe || selectedLaneIndex < 0 || selectedKeyTime < 0.0)
        return;

    const auto lanes = getVisibleLanes();
    if (selectedLaneIndex >= static_cast<int>(lanes.size()))
        return;

    const int laneHeight = getLaneHeight(static_cast<int>(lanes.size()));
    const int visibleRow = selectedLaneIndex - scrollOffset;
    if (visibleRow < 0)
        return;

    const int rowTop = rulerHeight + visibleRow * laneHeight;
    const double newTimeSec = xToTime(event.x);
    const float newValue = yToValueInRow(event.y, rowTop);
    const auto& lane = lanes[static_cast<size_t>(selectedLaneIndex)];
    const auto paramName = lane.parameterNames[lane.selectedParameter];

    processor.deleteFxAutomationKeyframe(selectedObjectId, lane.effectName, paramName, selectedKeyTime);
    processor.addFxAutomationKeyframe(selectedObjectId, lane.effectName, paramName, newTimeSec, newValue);
    selectedKeyTime = newTimeSec;
    repaint();
}

void StoryTimelineComponent::mouseUp(const juce::MouseEvent&)
{
    draggingKeyframe = false;
    draggingSegmentCurvature = false;
    curvatureDragLaneIndex = -1;
    curvatureDragSegmentStartTime = -1.0;
}

void StoryTimelineComponent::mouseMove(const juce::MouseEvent& event)
{
    updateHoverState(event);
    repaint();
}

void StoryTimelineComponent::mouseExit(const juce::MouseEvent&)
{
    hoveredLaneIndex = -1;
    hoveredKeyTime = -1.0;
    repaint();
}

void StoryTimelineComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int maxOffset = getMaxScrollOffset();
    if (maxOffset <= 0)
        return;

    int nextOffset = scrollOffset;
    if (wheel.deltaY < -0.01f)
        ++nextOffset;
    else if (wheel.deltaY > 0.01f)
        --nextOffset;

    nextOffset = juce::jlimit(0, maxOffset, nextOffset);
    if (nextOffset != scrollOffset)
    {
        scrollOffset = nextOffset;
        updateScrollBar();
        repaint();
    }
}

void StoryTimelineComponent::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved != &laneScrollBar)
        return;

    const int clampedOffset = juce::jlimit(0, getMaxScrollOffset(), juce::roundToInt(newRangeStart));
    if (clampedOffset != scrollOffset)
    {
        scrollOffset = clampedOffset;
        repaint();
    }
}

int StoryTimelineComponent::getMaxVisibleRows() const
{
    const int laneHeight = getLaneHeight(0);
    return juce::jmax(1, (getHeight() - rulerHeight) / juce::jmax(1, laneHeight));
}

double StoryTimelineComponent::xToTime(int x) const
{
    const int timelineX = juce::jmax(nameColumnWidth, x);
    const int w = juce::jmax(1, getTimelineRightX() - nameColumnWidth - 1);
    const double norm = juce::jlimit(0.0, 1.0, static_cast<double>(timelineX - nameColumnWidth) / static_cast<double>(w));
    return norm * maxTimeSec;
}

float StoryTimelineComponent::timeToX(double timeSec) const
{
    const double clamped = juce::jlimit(0.0, maxTimeSec, timeSec);
    const int w = juce::jmax(1, getTimelineRightX() - nameColumnWidth - 1);
    return static_cast<float>(nameColumnWidth) + static_cast<float>((clamped / maxTimeSec) * static_cast<double>(w));
}

float StoryTimelineComponent::yToValueInRow(int y, int rowTop) const
{
    const int laneHeight = getLaneHeight(0);
    const int localY = juce::jlimit(0, laneHeight - 1, y - rowTop);
    const float t = 1.0f - (static_cast<float>(localY) / static_cast<float>(juce::jmax(1, laneHeight - 1)));
    return juce::jlimit(0.0f, 1.0f, t);
}

float StoryTimelineComponent::valueToYInRow(float value, int rowTop) const
{
    const float clamped = juce::jlimit(0.0f, 1.0f, value);
    const int laneHeight = getLaneHeight(0);
    return static_cast<float>(rowTop) + (1.0f - clamped) * static_cast<float>(laneHeight - 1);
}

int StoryTimelineComponent::yToLaneRow(int y) const
{
    const int relY = y - rulerHeight;
    if (relY < 0)
        return -1;

    const int laneHeight = getLaneHeight(0);
    const int row = (relY / laneHeight) + scrollOffset;
    const int numLanes = static_cast<int>(getVisibleLanes().size());
    return (row < 0 || row >= numLanes) ? -1 : row;
}

int StoryTimelineComponent::getMaxScrollOffset() const
{
    const int numLanes = static_cast<int>(getVisibleLanes().size());
    return juce::jmax(0, numLanes - getMaxVisibleRows());
}

bool StoryTimelineComponent::findNearestKeyframe(int x, int y, int& laneIndex, double& keyTimeSec) const
{
    laneIndex = -1;
    keyTimeSec = 0.0;

    float bestDistanceSq = deleteSnapRadiusPx * deleteSnapRadiusPx;
    bool found = false;

    const int maxVisibleRows = getMaxVisibleRows();
    const auto lanes = getVisibleLanes();
    const int selectedObjectId = processor.getSelectedObjectId();
    const int laneHeight = getLaneHeight(static_cast<int>(lanes.size()));
    for (int displayRow = 0; displayRow < maxVisibleRows && (scrollOffset + displayRow) < static_cast<int>(lanes.size()); ++displayRow)
    {
        const int candidateLaneIndex = scrollOffset + displayRow;
        const int rowTop = rulerHeight + displayRow * laneHeight;
        const auto& lane = lanes[static_cast<size_t>(candidateLaneIndex)];
        const auto paramName = lane.parameterNames[lane.selectedParameter];

        const auto keys = processor.getFxAutomationKeyframes(selectedObjectId, lane.effectName, paramName);
        for (const auto& k : keys)
        {
            const float kx = timeToX(k.timeSec);
            const float ky = valueToYInRow(k.value, rowTop);
            const float dx = static_cast<float>(x) - kx;
            const float dy = static_cast<float>(y) - ky;
            const float distSq = dx * dx + dy * dy;

            if (distSq <= bestDistanceSq)
            {
                bestDistanceSq = distSq;
                laneIndex = candidateLaneIndex;
                keyTimeSec = k.timeSec;
                found = true;
            }
        }
    }

    return found;
}

bool StoryTimelineComponent::findNearestSegment(int x,
                                                int y,
                                                int& laneIndex,
                                                double& segmentStartTimeSec,
                                                float& segmentCurvature) const
{
    laneIndex = -1;
    segmentStartTimeSec = 0.0;
    segmentCurvature = 0.0f;

    float bestDistanceSq = (deleteSnapRadiusPx + 6.0f) * (deleteSnapRadiusPx + 6.0f);
    bool found = false;

    const int maxVisibleRows = getMaxVisibleRows();
    const auto lanes = getVisibleLanes();
    const int selectedObjectId = processor.getSelectedObjectId();
    const int laneHeight = getLaneHeight(static_cast<int>(lanes.size()));

    for (int displayRow = 0; displayRow < maxVisibleRows && (scrollOffset + displayRow) < static_cast<int>(lanes.size()); ++displayRow)
    {
        const int candidateLaneIndex = scrollOffset + displayRow;
        const int rowTop = rulerHeight + displayRow * laneHeight;
        const auto& lane = lanes[static_cast<size_t>(candidateLaneIndex)];
        const auto paramName = lane.parameterNames[lane.selectedParameter];
        const auto keys = processor.getFxAutomationKeyframes(selectedObjectId, lane.effectName, paramName);

        if (keys.size() < 2)
            continue;

        for (size_t i = 0; i + 1 < keys.size(); ++i)
        {
            const auto& a = keys[i];
            const auto& b = keys[i + 1];
            const float x0 = timeToX(a.timeSec);
            const float x1 = timeToX(b.timeSec);
            const float y0 = valueToYInRow(a.value, rowTop);
            const float y1 = valueToYInRow(b.value, rowTop);

            constexpr int samplesPerSegment = 12;
            juce::Point<float> prev(x0, y0);
            for (int s = 1; s <= samplesPerSegment; ++s)
            {
                const float t = static_cast<float>(s) / static_cast<float>(samplesPerSegment);
                const float curve = effectiveSegmentCurvature(a.curvature, a.value, b.value);
                const float shapedT = applyCurvatureToT(t, curve);
                juce::Point<float> p(x0 + (x1 - x0) * t,
                                     y0 + (y1 - y0) * shapedT);

                const juce::Line<float> line(prev, p);
                const auto nearest = line.findNearestPointTo(juce::Point<float>(static_cast<float>(x), static_cast<float>(y)));
                const float dx = nearest.x - static_cast<float>(x);
                const float dy = nearest.y - static_cast<float>(y);
                const float distSq = dx * dx + dy * dy;

                if (distSq <= bestDistanceSq)
                {
                    bestDistanceSq = distSq;
                    laneIndex = candidateLaneIndex;
                    segmentStartTimeSec = a.timeSec;
                    segmentCurvature = a.curvature;
                    found = true;
                }

                prev = p;
            }
        }
    }

    return found;
}

void StoryTimelineComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const int selectedObjectId = processor.getSelectedObjectId();
    if (selectedObjectId < 0)
        return;

    const int laneIndex = yToLaneRow(event.y);
    if (laneIndex < 0)
        return;

    const auto lanes = getVisibleLanes();
    if (laneIndex >= static_cast<int>(lanes.size()))
        return;

    const double timeSec = xToTime(event.x);
    const int laneHeight = getLaneHeight(static_cast<int>(lanes.size()));
    const int visibleRow = laneIndex - scrollOffset;
    const int rowTop = rulerHeight + visibleRow * laneHeight;
    const float value = yToValueInRow(event.y, rowTop);
    const auto& lane = lanes[static_cast<size_t>(laneIndex)];
    const auto paramName = lane.parameterNames[lane.selectedParameter];

    processor.addFxAutomationKeyframe(selectedObjectId, lane.effectName, paramName, timeSec, value);
    repaint();
}

void StoryTimelineComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xFF15151A));
    updateScrollBar();

    const int selectedObjectId = processor.getSelectedObjectId();
    if (selectedObjectId < 0)
    {
        g.setColour(juce::Colour(0x66FFFFFF));
        g.drawText("Select an object in the sidebar to edit FX automation.",
                   bounds, juce::Justification::centred, false);
        return;
    }

    const auto lanes = getVisibleLanes();
    if (lanes.empty())
    {
        g.setColour(juce::Colour(0x66FFFFFF));
        g.drawText("No active FX lanes for selected object.",
                   bounds, juce::Justification::centred, false);
        return;
    }

    const int maxVisibleRows = getMaxVisibleRows();
    scrollOffset = juce::jlimit(0, getMaxScrollOffset(), scrollOffset);
    const int laneHeight = getLaneHeight(static_cast<int>(lanes.size()));
    const int timelineRight = getTimelineRightX();

    auto ruler = bounds.removeFromTop(rulerHeight);
    g.setColour(juce::Colour(0xFF2A2A32));
    g.fillRect(ruler.withTrimmedRight(getWidth() - timelineRight));

    g.setColour(juce::Colour(0x88FFFFFF));
    for (int sec = 0; sec <= static_cast<int>(maxTimeSec); sec += 5)
    {
        const int x = static_cast<int>(timeToX(static_cast<double>(sec)));
        g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
        g.drawText(juce::String(sec) + "s", x + 2, 2, 28, rulerHeight - 4, juce::Justification::centredLeft, false);
    }

    for (int displayRow = 0; displayRow < maxVisibleRows && (scrollOffset + displayRow) < static_cast<int>(lanes.size()); ++displayRow)
    {
        const int laneIndex = scrollOffset + displayRow;
        const int y = rulerHeight + displayRow * laneHeight;
        const auto& lane = lanes[static_cast<size_t>(laneIndex)];

        g.setColour((displayRow % 2 == 0) ? juce::Colour(0xFF1B1B22) : juce::Colour(0xFF202028));
        g.fillRect(0, y, timelineRight, laneHeight);

        g.setColour(juce::Colour(0xFF23232D));
        g.fillRect(0, y, nameColumnWidth, laneHeight);

        g.setColour(juce::Colour(0x66FFFFFF));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(timelineRight));

        const auto objectName = processor.getTimelineTrackName(processor.getObjectDatabase()->getObjectIndexById(selectedObjectId));
        g.setColour(juce::Colour(0xFFAAAAAA));
        g.drawText(objectName + " / " + lane.effectName, 8, y + 2, nameColumnWidth - 12, 18, juce::Justification::centredLeft, true);

        const auto tabArea = getLaneParameterTabArea(y, laneHeight);
        const int tabCount = juce::jmax(1, lane.parameterNames.size());
        const int tabW = juce::jmax(22, tabArea.getWidth() / tabCount);
        for (int p = 0; p < lane.parameterNames.size(); ++p)
        {
            juce::Rectangle<int> tab(tabArea.getX() + p * tabW, tabArea.getY(), tabW - 2, tabArea.getHeight());
            const bool isSelected = (p == lane.selectedParameter);
            g.setColour(isSelected ? juce::Colour(0xFF4A76B7) : juce::Colour(0xFF2D2D38));
            g.fillRoundedRectangle(tab.toFloat(), 3.0f);
            g.setColour(isSelected ? juce::Colour(0xFFEAF3FF) : juce::Colour(0xFFB8B8C6));
            g.drawText(lane.parameterNames[p], tab, juce::Justification::centred, true);
        }

        g.setColour(juce::Colour(0x335E5E72));
        g.drawVerticalLine(nameColumnWidth, static_cast<float>(y), static_cast<float>(y + laneHeight));

        if (lane.effectName.equalsIgnoreCase("Pitch"))
        {
            const float zeroY = valueToYInRow(0.5f, y);
            g.setColour(juce::Colour(0x66D9E6FF));
            g.drawLine(static_cast<float>(nameColumnWidth), zeroY, static_cast<float>(timelineRight), zeroY, 1.0f);
        }

        const auto keys = processor.getFxAutomationKeyframes(selectedObjectId,
                                                             lane.effectName,
                                                             lane.parameterNames[lane.selectedParameter]);
        if (keys.size() >= 2)
        {
            g.setColour(juce::Colour(0xFF6CB4FF));
            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                const auto& a = keys[i];
                const auto& b = keys[i + 1];
                const float x0 = timeToX(a.timeSec);
                const float x1 = timeToX(b.timeSec);
                const float y0 = valueToYInRow(a.value, y);
                const float y1 = valueToYInRow(b.value, y);

                constexpr int samplesPerSegment = 16;
                juce::Point<float> prev(x0, y0);
                for (int s = 1; s <= samplesPerSegment; ++s)
                {
                    const float t = static_cast<float>(s) / static_cast<float>(samplesPerSegment);
                    const float curve = effectiveSegmentCurvature(a.curvature, a.value, b.value);
                    const float shapedT = applyCurvatureToT(t, curve);
                    const juce::Point<float> p(x0 + (x1 - x0) * t,
                                               y0 + (y1 - y0) * shapedT);
                    g.drawLine(prev.x, prev.y, p.x, p.y, 1.5f);
                    prev = p;
                }
            }
        }

        for (const auto& k : keys)
        {
            const float x = timeToX(k.timeSec);
            const float ky = valueToYInRow(k.value, y);
            const juce::Point<float> p(x, ky);
            const bool isHovered = (laneIndex == hoveredLaneIndex && std::abs(k.timeSec - hoveredKeyTime) < 1.0e-3);

            g.setColour(isHovered ? juce::Colour(0xFFFFD96B) : juce::Colour(0xFFE8F1FF));
            g.fillEllipse(p.x - keyRadiusPx, p.y - keyRadiusPx, keyRadiusPx * 2.0f, keyRadiusPx * 2.0f);
            g.setColour(isHovered ? juce::Colour(0xFFFFD96B) : juce::Colour(0xAA14324A));
            g.drawEllipse(p.x - keyRadiusPx, p.y - keyRadiusPx, keyRadiusPx * 2.0f, keyRadiusPx * 2.0f, isHovered ? 2.0f : 1.0f);

            const auto label = formatLaneValue(lane.effectName, k.value);
            juce::Rectangle<int> labelBounds(static_cast<int>(p.x) + 8, static_cast<int>(p.y) - 8, 54, 16);
            g.setColour(juce::Colour(0xAA0F1116));
            g.fillRoundedRectangle(labelBounds.toFloat(), 3.0f);
            g.setColour(isHovered ? juce::Colour(0xFFFFF4CC) : juce::Colour(0xFFCFD6E6));
            g.drawText(label, labelBounds, juce::Justification::centredLeft, false);
        }
    }

    const double t = processor.getTransportSeconds();
    const int playX = static_cast<int>(timeToX(t));
    g.setColour(processor.isTransportPlaying() ? juce::Colour(0xFFFF5A5A) : juce::Colour(0x66FF5A5A));
    g.drawVerticalLine(playX, 0.0f, static_cast<float>(getHeight()));
}
