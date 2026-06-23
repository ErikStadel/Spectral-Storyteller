#include "StoryTimelineComponent.h"
#include "../PluginProcessor.h"

StoryTimelineComponent::StoryTimelineComponent(PluginProcessor& processorRef)
    : processor(processorRef)
{
    setOpaque(true);
    startTimerHz(30);
}

void StoryTimelineComponent::timerCallback()
{
    repaint();
}

void StoryTimelineComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isAltDown())
    {
        int objectIndex = -1;
        double keyTimeSec = 0.0;
        if (findNearestKeyframe(event.x, event.y, objectIndex, keyTimeSec))
        {
            processor.deleteTimelineKeyframe(objectIndex, keyTimeSec);
            repaint();
        }
        return;
    }

    int objectIndex = -1;
    double keyTimeSec = 0.0;
    if (findNearestKeyframe(event.x, event.y, objectIndex, keyTimeSec))
    {
        selectedTrackIndex = objectIndex;
        selectedKeyTime = keyTimeSec;
        draggingKeyframe = true;
    }
}

void StoryTimelineComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!draggingKeyframe || selectedTrackIndex < 0 || selectedKeyTime < 0.0)
        return;

    const int visibleRow = selectedTrackIndex - scrollOffset;
    if (visibleRow < 0)
        return;

    const int rowTop = rulerHeight + visibleRow * rowHeight;
    const double newTimeSec = xToTime(event.x);
    const float newValue = yToValueInRow(event.y, rowTop);

    processor.deleteTimelineKeyframe(selectedTrackIndex, selectedKeyTime);
    processor.addTimelineKeyframe(selectedTrackIndex, newTimeSec, newValue);
    selectedKeyTime = newTimeSec;
    repaint();
}

void StoryTimelineComponent::mouseUp(const juce::MouseEvent&)
{
    draggingKeyframe = false;
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
        repaint();
    }
}

int StoryTimelineComponent::getMaxVisibleRows() const
{
    return juce::jmax(1, (getHeight() - rulerHeight) / rowHeight);
}

double StoryTimelineComponent::xToTime(int x) const
{
    const int timelineX = juce::jmax(nameColumnWidth, x);
    const int w = juce::jmax(1, getWidth() - nameColumnWidth - 1);
    const double norm = juce::jlimit(0.0, 1.0, static_cast<double>(timelineX - nameColumnWidth) / static_cast<double>(w));
    return norm * maxTimeSec;
}

float StoryTimelineComponent::timeToX(double timeSec) const
{
    const double clamped = juce::jlimit(0.0, maxTimeSec, timeSec);
    const int w = juce::jmax(1, getWidth() - nameColumnWidth - 1);
    return static_cast<float>(nameColumnWidth) + static_cast<float>((clamped / maxTimeSec) * static_cast<double>(w));
}

float StoryTimelineComponent::yToValueInRow(int y, int rowTop) const
{
    const int localY = juce::jlimit(0, rowHeight - 1, y - rowTop);
    const float t = 1.0f - (static_cast<float>(localY) / static_cast<float>(juce::jmax(1, rowHeight - 1)));
    return juce::jlimit(0.0f, 1.0f, t);
}

float StoryTimelineComponent::valueToYInRow(float value, int rowTop) const
{
    const float clamped = juce::jlimit(0.0f, 1.0f, value);
    return static_cast<float>(rowTop) + (1.0f - clamped) * static_cast<float>(rowHeight - 1);
}

int StoryTimelineComponent::yToObjectRow(int y) const
{
    const int relY = y - rulerHeight;
    if (relY < 0)
        return -1;

    const int row = (relY / rowHeight) + scrollOffset;
    const int numObjects = processor.getObjectDatabase()->getNumObjects();
    return (row < 0 || row >= numObjects) ? -1 : row;
}

int StoryTimelineComponent::getMaxScrollOffset() const
{
    const int numObjects = processor.getObjectDatabase()->getNumObjects();
    return juce::jmax(0, numObjects - getMaxVisibleRows());
}

bool StoryTimelineComponent::findNearestKeyframe(int x, int y, int& objectIndex, double& keyTimeSec) const
{
    objectIndex = -1;
    keyTimeSec = 0.0;

    float bestDistanceSq = deleteSnapRadiusPx * deleteSnapRadiusPx;
    bool found = false;

    const int maxVisibleRows = getMaxVisibleRows();
    const int numObjects = processor.getObjectDatabase()->getNumObjects();
    for (int displayRow = 0; displayRow < maxVisibleRows && (scrollOffset + displayRow) < numObjects; ++displayRow)
    {
        const int candidateObjectIndex = scrollOffset + displayRow;
        const int rowTop = rulerHeight + displayRow * rowHeight;

        const auto keys = processor.getTimelineKeyframes(candidateObjectIndex);
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
                objectIndex = candidateObjectIndex;
                keyTimeSec = k.timeSec;
                found = true;
            }
        }
    }

    return found;
}

void StoryTimelineComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const int row = yToObjectRow(event.y);
    if (row < 0)
        return;

    const double timeSec = xToTime(event.x);
    const int visibleRow = row - scrollOffset;
    const int rowTop = rulerHeight + visibleRow * rowHeight;
    const float value = yToValueInRow(event.y, rowTop);

    processor.addTimelineKeyframe(row, timeSec, value);
    repaint();
}

void StoryTimelineComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xFF15151A));

    const int numObjects = processor.getObjectDatabase()->getNumObjects();
    if (numObjects == 0)
    {
        g.setColour(juce::Colour(0x66FFFFFF));
        g.drawText("No objects yet. Create one in the spectrogram.",
                   bounds, juce::Justification::centred, false);
        return;
    }

    const int maxVisibleRows = getMaxVisibleRows();
    scrollOffset = juce::jlimit(0, getMaxScrollOffset(), scrollOffset);

    // ruler
    auto ruler = bounds.removeFromTop(rulerHeight);
    g.setColour(juce::Colour(0xFF2A2A32));
    g.fillRect(ruler);

    g.setColour(juce::Colour(0x88FFFFFF));
    for (int sec = 0; sec <= static_cast<int>(maxTimeSec); sec += 5)
    {
        const int x = static_cast<int>(timeToX(static_cast<double>(sec)));
        g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
        g.drawText(juce::String(sec) + "s", x + 2, 2, 28, rulerHeight - 4, juce::Justification::centredLeft, false);
    }

    // rows + keyframes
    for (int displayRow = 0; displayRow < maxVisibleRows && (scrollOffset + displayRow) < numObjects; ++displayRow)
    {
        const int objectIndex = scrollOffset + displayRow;
        const int y = rulerHeight + displayRow * rowHeight;

        g.setColour((displayRow % 2 == 0) ? juce::Colour(0xFF1B1B22) : juce::Colour(0xFF202028));
        g.fillRect(0, y, getWidth(), rowHeight);

        g.setColour(juce::Colour(0xFF23232D));
        g.fillRect(0, y, nameColumnWidth, rowHeight);

        g.setColour(juce::Colour(0x66FFFFFF));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));

        // Object name
        const auto objName = processor.getTimelineTrackName(objectIndex);
        g.setColour(juce::Colour(0xFFAAAAAA));
        g.drawText(objName, 8, y, nameColumnWidth - 12, rowHeight, juce::Justification::centredLeft, true);

        g.setColour(juce::Colour(0x335E5E72));
        g.drawVerticalLine(nameColumnWidth, static_cast<float>(y), static_cast<float>(y + rowHeight));

        const auto keys = processor.getTimelineKeyframes(objectIndex);
        juce::Point<float> prev;
        bool hasPrev = false;
        for (const auto& k : keys)
        {
            const float x = timeToX(k.timeSec);
            const float ky = valueToYInRow(k.value, y);
            const juce::Point<float> p(x, ky);

            if (hasPrev)
            {
                g.setColour(juce::Colour(0xFF6CB4FF));
                g.drawLine(prev.x, prev.y, p.x, p.y, 1.5f);
            }

            g.setColour(juce::Colour(0xFFE8F1FF));
            g.fillEllipse(p.x - keyRadiusPx, p.y - keyRadiusPx, keyRadiusPx * 2.0f, keyRadiusPx * 2.0f);
            g.setColour(juce::Colour(0xAA14324A));
            g.drawEllipse(p.x - keyRadiusPx, p.y - keyRadiusPx, keyRadiusPx * 2.0f, keyRadiusPx * 2.0f, 1.0f);

            prev = p;
            hasPrev = true;
        }
    }

    // transport playhead
    const double t = processor.getTransportSeconds();
    const int playX = static_cast<int>(timeToX(t));
    g.setColour(processor.isTransportPlaying() ? juce::Colour(0xFFFF5A5A) : juce::Colour(0x66FF5A5A));
    g.drawVerticalLine(playX, 0.0f, static_cast<float>(getHeight()));

    const int maxOffset = getMaxScrollOffset();
    if (maxOffset > 0)
    {
        const int trackAreaHeight = getHeight() - rulerHeight;
        const float visibleRatio = static_cast<float>(maxVisibleRows) / static_cast<float>(numObjects);
        const int thumbHeight = juce::jmax(20, static_cast<int>(visibleRatio * static_cast<float>(trackAreaHeight)));
        const float scrollNorm = static_cast<float>(scrollOffset) / static_cast<float>(juce::jmax(1, maxOffset));
        const int thumbY = rulerHeight + static_cast<int>(scrollNorm * static_cast<float>(juce::jmax(0, trackAreaHeight - thumbHeight)));

        g.setColour(juce::Colour(0x33444455));
        g.fillRect(getWidth() - 8, rulerHeight, 6, trackAreaHeight);
        g.setColour(juce::Colour(0x889A9AB0));
        g.fillRoundedRectangle(static_cast<float>(getWidth() - 8), static_cast<float>(thumbY), 6.0f, static_cast<float>(thumbHeight), 3.0f);
    }
}
