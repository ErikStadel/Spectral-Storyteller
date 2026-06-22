#include "PluginEditor.h"
#include "PluginProcessor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    dryWetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    dryWetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    dryWetSlider.setRange(0.0, 1.0, 0.001);
    dryWetSlider.setValue(1.0);
    addAndMakeVisible(dryWetSlider);

    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "dryWet", dryWetSlider);

    setSize(500, 220);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A1F));
    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawFittedText("Spectral Storyteller PR1", getLocalBounds().reduced(12), juce::Justification::topLeft, 1);

    g.setFont(14.0f);
    g.drawFittedText("Dry/Wet", getLocalBounds().withTrimmedTop(40).removeFromTop(24), juce::Justification::left, 1);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    dryWetSlider.setBounds(area.removeFromBottom(48));
}