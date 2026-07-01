#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/SpectralView.h"
#include "UI/SpectralSelector.h"
#include "UI/ObjectSidebar.h"
#include "UI/StoryTimelineComponent.h"
#include "UI/ModulationPanel.h"
#include "UI/FxRackPanel.h"
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
        g.setColour(juce::Colour(0xFF0C0A09));
        g.fillRoundedRectangle(r, 4.0f);

        const float minDb = -60.0f;
        const float maxDb = 6.0f;
        auto norm = [&](float db)
        {
            return juce::jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        };

        const float h = r.getHeight();
        const float lvlY = h - h * norm(currentDb);

        juce::ColourGradient grad(juce::Colour(0xFF6A00A8), 0.0f, r.getBottom(),
                                  juce::Colour(0xFFFFC400), 0.0f, r.getY(), false);
        grad.addColour(0.5, juce::Colour(0xFFFF2A00));
        g.setGradientFill(grad);
        g.fillRect(juce::Rectangle<float>(r.getX() + 1.0f, r.getY() + lvlY,
                                          r.getWidth() - 2.0f, h - lvlY - 1.0f));

        const float pkY = h - h * norm(peakHoldDb);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.fillRect(juce::Rectangle<float>(r.getX() + 1.0f, r.getY() + pkY - 0.5f,
                                          r.getWidth() - 2.0f, 1.0f));
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
    std::unique_ptr<FxRackPanel> fxRackPanel;

    juce::Slider inputGainSlider;
    juce::Slider outputGainSlider;
    juce::Slider dryWetSlider;
    juce::Slider gateSlider;

    juce::Label inputLabel;
    juce::Label outputLabel;
    juce::Label dryWetLabel;
    juce::Label gateLabel;

    LevelMeter inputMeter;
    LevelMeter outputMeter;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;

    juce::Label versionLabel;
    juce::TooltipWindow tooltipWindow;

    void paintHeaderBar(juce::Graphics& g, juce::Rectangle<int> area);
    void paintMeterStrip(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label);

    static constexpr int headerHeight = 44;
    static constexpr int sidebarWidth = 290;
    static constexpr int meterStripWidth = 44;
    static constexpr int footerHeight = 230;
    static constexpr int timelineHeight = 110;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
