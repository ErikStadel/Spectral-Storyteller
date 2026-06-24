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

    // Hybrider Segmentation (PR5 Erweiterung)
    std::array<float, SpectralFrameBuffer::NUM_BINS> hpsScore{};           // Harmonic Product Spectrum
    std::array<float, SpectralFrameBuffer::NUM_BINS> broadbandFlux{};      // Für Transienten
    bool useHybridMode = true;  // später als Parameter

    // === Hybrid Segmentation + HPSS Pre-Pass (Step 2) ===
    std::array<float, SpectralFrameBuffer::NUM_BINS> hpHarmonicMask{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> hpPercussiveMask{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> hpsScore{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> broadbandFlux{};

    bool useHybridMode = true;
    bool useHPSSPrePass = true;           // Neuer Schalter
    int hpssIterations = 3;               // Für Median-Filter
    
    // Object management sidebar
    std::unique_ptr<ObjectSidebar> objectSidebar;

    // PR4 timeline editor
    std::unique_ptr<StoryTimelineComponent> storyTimeline;
    
    // Control sliders
    juce::Slider dryWetSlider;
    juce::Slider gateSlider;
    juce::Slider curveSlider;
    
    // Control labels
    juce::Label dryWetLabel;
    juce::Label gateLabel;
    juce::Label curveLabel;
    
    // Attachments & UI
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;
    juce::Label versionLabel;
    juce::TooltipWindow tooltipWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};