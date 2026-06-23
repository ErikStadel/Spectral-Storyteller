#include "PluginEditor.h"
#include "PluginProcessor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Create spectrogram view
    spectralView = std::make_unique<SpectralView>(processor.getSpectralFrameBuffer());
    spectralView->setMagnitudeRange(-120.0f, 0.0f);
    spectralView->setShowGrid(true);
    spectralView->setGateDb(-96.0f);
    spectralView->setFrequencyCurve(2.0f);
    addAndMakeVisible(*spectralView);

    dryWetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    dryWetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    dryWetSlider.setRange(0.0, 1.0, 0.001);
    dryWetSlider.setValue(1.0);
    addAndMakeVisible(dryWetSlider);

    gateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    gateSlider.setRange(-180.0, 6.0, 1.0);
    gateSlider.setValue(-96.0);
    gateSlider.onValueChange = [this]
    {
        if (spectralView)
            spectralView->setGateDb(static_cast<float>(gateSlider.getValue()));
    };
    addAndMakeVisible(gateSlider);

    curveSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    curveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    curveSlider.setRange(0.0, 10.0, 0.1);
    curveSlider.setValue(2.0);
    curveSlider.onValueChange = [this]
    {
        if (spectralView)
            spectralView->setFrequencyCurve(static_cast<float>(curveSlider.getValue()));
    };
    addAndMakeVisible(curveSlider);

    gateLabel.setText("Gate (dB)", juce::dontSendNotification);
    gateLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(gateLabel);

    curveLabel.setText("Curve", juce::dontSendNotification);
    curveLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(curveLabel);

    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "dryWet", dryWetSlider);

    versionLabel.setText(processor.getBuildInfo(), juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::bottomRight);
    addAndMakeVisible(versionLabel);

    setSize(1000, 700);
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
    auto area = getLocalBounds().reduced(12);
    
    // Top: version label
    versionLabel.setBounds(area.removeFromTop(20));
    
    // Bottom controls
    auto bottom = area.removeFromBottom(108);

    auto row1 = bottom.removeFromTop(32);
    gateLabel.setBounds(row1.removeFromLeft(100));
    gateSlider.setBounds(row1);

    auto row2 = bottom.removeFromTop(32);
    curveLabel.setBounds(row2.removeFromLeft(100));
    curveSlider.setBounds(row2);

    auto row3 = bottom.removeFromTop(32);
    dryWetSlider.setBounds(row3);
    
    // Center: Spectrogram view fills remaining space
    if (spectralView)
        spectralView->setBounds(area);
}