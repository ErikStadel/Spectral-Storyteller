#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/SpectralView.h"
#include "UI/SpectralSelector.h"
#include "UI/ObjectSidebar.h"
#include "UI/StoryTimelineComponent.h"
#include "UI/ModulationPanel.h"
#include <atomic>
#include <memory>

class PluginProcessor;

class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter() { startTimerHz(30); }
    void setSource(std::atomic<float>* peakDb) { sourceDb = peakDb; }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xFF111418));
        g.fillRoundedRectangle(r, 2.0f);

        const float minDb = -60.0f;
        const float maxDb = 6.0f;
        auto norm = [&](float db)
        {
            return juce::jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        };

        const float h = r.getHeight();
        const float lvlY = h - h * norm(currentDb);
        auto bar = juce::Rectangle<float>(r.getX() + 1.0f,
                                          r.getY() + lvlY,
                                          r.getWidth() - 2.0f,
                                          h - lvlY - 1.0f);

        juce::Colour c = currentDb > 0.0f ? juce::Colours::red
                       : currentDb > -6.0f ? juce::Colours::orange
                       : juce::Colour(0xFF4CAF50);
        g.setColour(c);
        g.fillRect(bar);

        const float pkY = h - h * norm(peakHoldDb);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillRect(juce::Rectangle<float>(r.getX() + 1.0f,
                                          r.getY() + pkY - 1.0f,
                                          r.getWidth() - 2.0f,
                                          1.5f));
    }

private:
    void timerCallback() override
    {
        if (sourceDb != nullptr)
        {
            const float v = sourceDb->load();
            currentDb = v;
            if (v > peakHoldDb)
            {
                peakHoldDb = v;
                peakHoldCounter = 30;
            }
            else if (--peakHoldCounter <= 0)
            {
                peakHoldDb = juce::jmax(-90.0f, peakHoldDb - 0.8f);
            }
        }

        repaint();
    }

    std::atomic<float>* sourceDb = nullptr;
    float currentDb = -90.0f;
    float peakHoldDb = -90.0f;
    int peakHoldCounter = 0;
};

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::DragAndDropContainer
{
public:
    PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& processor;

    std::unique_ptr<SpectralView> spectralView;
    std::unique_ptr<SpectralSelector> spectralSelector;
    std::unique_ptr<ObjectSidebar> objectSidebar;
    std::unique_ptr<StoryTimelineComponent> storyTimeline;
    std::unique_ptr<ModulationPanel> modulationPanel;

    juce::Slider inputGainSlider;
    juce::Slider outputGainSlider;
    juce::Slider dryWetSlider;
    juce::Slider gateSlider;
    juce::Slider transientThresholdSlider;

    juce::Label inputLabel;
    juce::Label outputLabel;
    juce::Label dryWetLabel;
    juce::Label gateLabel;
    juce::Label transientThresholdLabel;

    LevelMeter inputMeter;
    LevelMeter outputMeter;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transientThresholdAttachment;

    juce::Label versionLabel;
    juce::TooltipWindow tooltipWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
