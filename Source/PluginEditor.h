#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/SpectralView.h"
#include "UI/SpectralSelector.h"
#include "UI/ObjectSidebar.h"
#include "UI/StoryTimelineComponent.h"
#include <memory>

class PluginProcessor;

class PluginEditor : public juce::AudioProcessorEditor
{
public:
    PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& processor;
    
    // Spectral visualization
    std::unique_ptr<SpectralView> spectralView;
    std::unique_ptr<SpectralSelector> spectralSelector;
    
    // Object management sidebar
    std::unique_ptr<ObjectSidebar> objectSidebar;

    // PR4 timeline editor
    std::unique_ptr<StoryTimelineComponent> storyTimeline;
    
    // Control sliders
    juce::Slider dryWetSlider;
    juce::Slider gateSlider;
    juce::Slider curveSlider;
    juce::Slider transientThresholdSlider;
    
    // Control labels
    juce::Label dryWetLabel;
    juce::Label gateLabel;
    juce::Label curveLabel;
    juce::Label transientThresholdLabel;
    
    // Attachments & UI
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transientThresholdAttachment;
    juce::Label versionLabel;
    juce::TooltipWindow tooltipWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};