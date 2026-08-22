#include "SourceView.h"
#include "Typography.h"
#include "../PluginProcessor.h"

namespace
{
    constexpr float kTopPadding = 28.0f;
    constexpr float kBottomPadding = 22.0f;
    constexpr float kSidePadding = 18.0f;
    constexpr float kHandleHitRadius = 10.0f;
}

SourceView::SourceView(PluginProcessor& p)
    : processor(p)
{
    setInterceptsMouseClicks(true, true);
    startTimerHz(30);
    refreshSnapshot();
}

SourceView::~SourceView()
{
    stopTimer();
}

void SourceView::refreshSnapshot()
{
    const int objectId = processor.getSelectedObjectId();
    const bool objectChanged = (objectId != cachedObjectId);
    cachedObjectId = objectId;

    // Always update loop range cheaply (without copying waveform)
    if (activeHandle == DragHandle::None)
    {
        double ls = loopStartSeconds, le = loopEndSeconds;
        processor.getTransformLoopRange(objectId, ls, le);
        loopStartSeconds = ls;
        loopEndSeconds   = le;
    }

    // Only rebuild the waveform when the object changes or data first becomes available
    const bool wasHasData = hasData;
    if (!objectChanged && wasHasData)
        return;

    PluginProcessor::TransformSourceViewData data;
    const bool valid = processor.getTransformSourceViewData(objectId, data);

    displayName     = data.displayName;
    durationSeconds = data.durationSeconds;
    hasData         = valid;

    if (activeHandle == DragHandle::None)
    {
        loopStartSeconds = data.loopStartSeconds;
        loopEndSeconds   = data.loopEndSeconds;
    }

    waveformMin.clear();
    waveformMax.clear();

    if (valid)
    {
        waveformMin.reserve(data.waveform.size());
        waveformMax.reserve(data.waveform.size());

        const float sliceCount = (data.waveform.size() > 1)
                                     ? static_cast<float>(data.waveform.size() - 1)
                                     : 1.0f;
        for (size_t index = 0; index < data.waveform.size(); ++index)
        {
            const auto& slice = data.waveform[index];
            const float x = (data.waveform.size() <= 1) ? 0.0f : static_cast<float>(index) / sliceCount;
            waveformMin.emplace_back(x, slice.minSample);
            waveformMax.emplace_back(x, slice.maxSample);
        }
    }
}

void SourceView::timerCallback()
{
    refreshSnapshot();
    lastTransportSeconds = processor.getSourceTransportSeconds();
    lastTransportPlaying = processor.isSourceTransportPlaying();
    repaint();
}

void SourceView::resized()
{
}

juce::Rectangle<float> SourceView::waveformArea() const
{
    auto bounds = getLocalBounds().toFloat().reduced(kSidePadding, kTopPadding);
    bounds.removeFromBottom(kBottomPadding);
    return bounds;
}

float SourceView::timeToX(double timeSeconds) const
{
    const auto area = waveformArea();
    if (durationSeconds <= 1.0e-9)
        return area.getX();

    const float norm = juce::jlimit(0.0f, 1.0f, static_cast<float>(timeSeconds / durationSeconds));
    return area.getX() + static_cast<float>(norm) * area.getWidth();
}

double SourceView::xToTime(float x) const
{
    const auto area = waveformArea();
    if (area.getWidth() <= 1.0f || durationSeconds <= 1.0e-9)
        return 0.0;

    const float norm = juce::jlimit(0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    return norm * durationSeconds;
}

void SourceView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xFF111113));

    juce::ColourGradient bg(juce::Colour(0xFF18181B), bounds.getCentreX(), bounds.getY(),
                            juce::Colour(0xFF0B0B0D), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds.reduced(8.0f), 10.0f);

    auto area = waveformArea();
    g.setColour(juce::Colour(0xFF2A2A2E));
    g.drawRoundedRectangle(area.expanded(2.0f), 6.0f, 1.0f);

    g.setColour(juce::Colour(0xFFA1A1AA));
    g.setFont(Typography::getTitleFont());
    g.drawText(hasData ? (displayName.isNotEmpty() ? displayName : "Source") : "No Transform File Loaded",
               juce::Rectangle<int>(12, 8, getWidth() - 24, 18),
               juce::Justification::centredLeft,
               false);

    if (!hasData || waveformMin.empty())
    {
        g.setFont(Typography::getTitleFont().withHeight(15.0f));
        g.setColour(juce::Colour(0xFF71717A));
        g.drawFittedText("Load a Transform file to inspect its waveform and edit loop points.",
                         getLocalBounds().reduced(28),
                         juce::Justification::centred,
                         2);
        return;
    }

    const float midY = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.40f;

    juce::Path waveformPath;
    for (size_t i = 0; i < waveformMin.size(); ++i)
    {
        const float x = area.getX() + waveformMin[i].x * area.getWidth();
        const float y1 = midY - waveformMax[i].y * halfHeight;
        const float y2 = midY - waveformMin[i].y * halfHeight;
        waveformPath.startNewSubPath(x, y1);
        waveformPath.lineTo(x, y2);
    }

    g.setColour(juce::Colour(0xFF80CFFF));
    g.strokePath(waveformPath, juce::PathStrokeType(1.5f));

    const float loopStartX = timeToX(loopStartSeconds);
    const float loopEndX = timeToX(loopEndSeconds);

    g.setColour(juce::Colour(0x3380CFFF));
    g.fillRect(juce::Rectangle<float>(loopStartX, area.getY(), juce::jmax(0.0f, loopEndX - loopStartX), area.getHeight()));

    g.setColour(juce::Colour(0xFFE0A96D));
    g.drawLine(loopStartX, area.getY(), loopStartX, area.getBottom(), 2.0f);
    g.drawLine(loopEndX, area.getY(), loopEndX, area.getBottom(), 2.0f);

    double playheadDispSec = loopStartSeconds;
    if (durationSeconds > 1.0e-9)
    {
        const double loopLen = juce::jmax(1.0e-3, loopEndSeconds - loopStartSeconds);
        double phase = std::fmod(lastTransportSeconds, loopLen);
        if (phase < 0.0) phase += loopLen;
        playheadDispSec = juce::jlimit(loopStartSeconds, loopEndSeconds, loopStartSeconds + phase);
    }
    const float playheadX = timeToX(playheadDispSec);

    g.setColour(lastTransportPlaying ? juce::Colour(0xFFFFC400) : juce::Colour(0xFF9CA3AF));
    g.drawLine(playheadX, area.getY() - 4.0f, playheadX, area.getBottom() + 4.0f, lastTransportPlaying ? 2.5f : 1.5f);

    g.setFont(Typography::getHeaderFont());
    g.setColour(juce::Colour(0xFFA1A1AA));
    g.drawText(juce::String(loopStartSeconds, 2) + "s", juce::Rectangle<int>(static_cast<int>(loopStartX) - 18, area.getBottom() + 4, 40, 14), juce::Justification::centred, false);
    g.drawText(juce::String(loopEndSeconds, 2) + "s", juce::Rectangle<int>(static_cast<int>(loopEndX) - 18, area.getBottom() + 4, 40, 14), juce::Justification::centred, false);
}

void SourceView::mouseDown(const juce::MouseEvent& event)
{
    if (!hasData)
        return;

    const auto area = waveformArea();
    const float startX = timeToX(loopStartSeconds);
    const float endX = timeToX(loopEndSeconds);
    const float mouseX = juce::jlimit(area.getX(), area.getRight(), static_cast<float>(event.position.x));

    if (std::abs(mouseX - startX) <= kHandleHitRadius)
        activeHandle = DragHandle::LoopStart;
    else if (std::abs(mouseX - endX) <= kHandleHitRadius)
        activeHandle = DragHandle::LoopEnd;
    else if (mouseX > startX && mouseX < endX)
        activeHandle = (std::abs(mouseX - startX) < std::abs(mouseX - endX)) ? DragHandle::LoopStart : DragHandle::LoopEnd;
    else
        activeHandle = DragHandle::None;

    mouseDrag(event);
}

void SourceView::mouseDrag(const juce::MouseEvent& event)
{
    if (!hasData || activeHandle == DragHandle::None)
        return;

    constexpr double kMinLoopSeconds = 0.02;
    double targetTime = juce::jlimit(0.0, durationSeconds, xToTime(static_cast<float>(event.position.x)));

    if (activeHandle == DragHandle::LoopStart)
    {
        targetTime = juce::jlimit(0.0, loopEndSeconds - kMinLoopSeconds, targetTime);
        loopStartSeconds = targetTime;
    }
    else if (activeHandle == DragHandle::LoopEnd)
    {
        targetTime = juce::jlimit(loopStartSeconds + kMinLoopSeconds, durationSeconds, targetTime);
        loopEndSeconds = targetTime;
    }

    processor.setTransformLoopRange(processor.getSelectedObjectId(), loopStartSeconds, loopEndSeconds);
    repaint();
}

void SourceView::mouseUp(const juce::MouseEvent&)
{
    activeHandle = DragHandle::None;
}