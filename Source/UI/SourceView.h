#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PluginProcessor;

class SourceView : public juce::Component, private juce::Timer
{
public:
    explicit SourceView(PluginProcessor& processor);
    ~SourceView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum class DragHandle
    {
        None,
        LoopStart,
        LoopEnd
    };

    void timerCallback() override;
    void refreshSnapshot();
    float timeToX(double timeSeconds) const;
    double xToTime(float x) const;
    juce::Rectangle<float> waveformArea() const;

    PluginProcessor& processor;

    juce::String displayName;
    std::vector<juce::Point<float>> waveformMin;
    std::vector<juce::Point<float>> waveformMax;
    double durationSeconds = 0.0;
    double loopStartSeconds = 0.0;
    double loopEndSeconds = 0.0;
    bool hasData = false;
    int cachedObjectId = -1;
    DragHandle activeHandle = DragHandle::None;
    double lastTransportSeconds = 0.0;
    bool lastTransportPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceView)
};