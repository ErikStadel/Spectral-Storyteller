#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/SpectralView.h"
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
    
    std::unique_ptr<SpectralView> spectralView;
    juce::Slider dryWetSlider;
    juce::Slider gateSlider;
    juce::Slider curveSlider;
    juce::Label gateLabel;
    juce::Label curveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;
    juce::Label versionLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};