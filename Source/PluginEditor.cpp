#include "PluginEditor.h"
#include "PluginProcessor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processor(p), tooltipWindow(this, 450)
{
    // Create spectrogram view
    spectralView = std::make_unique<SpectralView>(processor.getSpectralFrameBuffer());
    spectralView->setMagnitudeRange(-120.0f, 0.0f);
    spectralView->setShowGrid(true);
    spectralView->setGateDb(-96.0f);
    spectralView->setFrequencyCurve(2.0f);
    spectralView->setSegmentationOverlayProvider([this](std::array<float, SpectralFrameBuffer::NUM_BINS>& transient,
                                                        std::array<float, SpectralFrameBuffer::NUM_BINS>& tonal,
                                                        std::array<float, SpectralFrameBuffer::NUM_BINS>& noise)
    {
        return processor.getSegmentationOverlay(transient, tonal, noise);
    });
    addAndMakeVisible(*spectralView);

    // Create spectrogram selector (lasso/rectangle overlay)
    spectralSelector = std::make_unique<SpectralSelector>();
    spectralSelector->setYToBinMapper([this](int y, int height)
    {
        juce::ignoreUnused(height);
        if (spectralView)
            return spectralView->getBinForY(y);

        return juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1, y);
    });
    spectralSelector->setOnSelectionStarted([this]()
    {
        if (spectralView)
            spectralView->setPaused(true);
    });
    spectralSelector->setOnSelectionFinished([this]()
    {
        if (spectralView)
            spectralView->setPaused(false);
    });
    spectralSelector->setOnSelectionComplete([this](int minBin, int maxBin)
    {
        // Create new object from selection
        auto* db = processor.getObjectDatabase();
        if (db)
        {
            const int clampedMinBin = juce::jlimit(0, ObjectDatabase::NUM_BINS - 1, juce::jmin(minBin, maxBin));
            const int clampedMaxBin = juce::jlimit(0, ObjectDatabase::NUM_BINS - 1, juce::jmax(minBin, maxBin));

            constexpr float nyquist = 24000.0f;
            constexpr float binWidthHz = nyquist / static_cast<float>(ObjectDatabase::NUM_BINS - 1);

            const float minFreqHz = static_cast<float>(clampedMinBin) * binWidthHz;
            const float maxFreqHz = static_cast<float>(clampedMaxBin) * binWidthHz;

            auto formatFreq = [](float freqHz)
            {
                if (freqHz >= 1000.0f)
                {
                    const float kHz = freqHz / 1000.0f;
                    if (kHz >= 10.0f)
                        return juce::String(static_cast<int>(std::round(kHz))) + "kHz";

                    return juce::String(kHz, 1) + "kHz";
                }

                return juce::String(static_cast<int>(std::round(freqHz))) + "Hz";
            };

            const std::string objName = ("Objekt [" + formatFreq(minFreqHz) + " - " + formatFreq(maxFreqHz) + "]").toStdString();

            if (db->addObject(objName))
            {
                // Create binary mask for this object
                std::array<bool, ObjectDatabase::NUM_BINS> mask;
                mask.fill(false);
                for (int bin = clampedMinBin; bin <= clampedMaxBin; ++bin)
                    mask[bin] = true;

                db->setObjectMask(db->getNumObjects() - 1, mask);

                // Refresh sidebar
                if (objectSidebar)
                    objectSidebar->refresh();
            }
        }
    });
    addAndMakeVisible(*spectralSelector);

    // Create object sidebar
    objectSidebar = std::make_unique<ObjectSidebar>(*processor.getObjectDatabase(), [this](bool enabled)
    {
        processor.setAutoDetectRecordingEnabled(enabled);
    });
    addAndMakeVisible(*objectSidebar);

    // PR4: Story timeline (keyframe automation lanes)
    storyTimeline = std::make_unique<StoryTimelineComponent>(processor);
    addAndMakeVisible(*storyTimeline);

    // Dry/Wet slider
    dryWetSlider.setSliderStyle(juce::Slider::LinearVertical);
    dryWetSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 20);
    dryWetSlider.setRange(0.0, 1.0, 0.001);
    dryWetSlider.setValue(1.0);
    addAndMakeVisible(dryWetSlider);

    dryWetLabel.setText("Dry/Wet", juce::dontSendNotification);
    dryWetLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(dryWetLabel);

    // Gate slider
    gateSlider.setSliderStyle(juce::Slider::LinearVertical);
    gateSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 20);
    gateSlider.setRange(-180.0, 6.0, 1.0);
    gateSlider.setValue(-96.0);
    gateSlider.onValueChange = [this]
    {
        if (spectralView)
            spectralView->setGateDb(static_cast<float>(gateSlider.getValue()));
    };
    addAndMakeVisible(gateSlider);

    gateLabel.setText("Gate (dB)", juce::dontSendNotification);
    gateLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(gateLabel);

    // Curve slider
    curveSlider.setSliderStyle(juce::Slider::LinearVertical);
    curveSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 20);
    curveSlider.setRange(0.0, 10.0, 0.1);
    curveSlider.setValue(2.0);
    curveSlider.onValueChange = [this]
    {
        if (spectralView)
            spectralView->setFrequencyCurve(static_cast<float>(curveSlider.getValue()));
    };
    addAndMakeVisible(curveSlider);

    curveLabel.setText("Curve", juce::dontSendNotification);
    curveLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(curveLabel);

    // Attachment for Dry/Wet parameter (audio processing)
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "dryWet", dryWetSlider);

    // Version info
    versionLabel.setText(processor.getBuildInfo(), juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::bottomRight);
    addAndMakeVisible(versionLabel);

    setSize(1200, 700);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A1F));
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    
    // Top: version label (18px height)
    versionLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);  // spacing
    
    // Reserve timeline strip; we will place it after side/center layout so the left object panel
    // can extend downward by a few rows without shrinking the spectrogram area.
    // 4 visible object rows before scrolling: ruler (22) + 4 * rowHeight (52) = 230
    auto timelineArea = area.removeFromBottom(230);
    timelineArea.removeFromBottom(6);
    
    // Middle section: sidebar (left) + spectrogram (center) + controls (right)
    const int sidebarWidth = 200;
    const int controlPanelWidth = 96;
    const int innerGap = 8;
    
    auto sidebar = area.removeFromLeft(sidebarWidth);
    area.removeFromLeft(innerGap);  // spacing

    auto controlPanel = area.removeFromRight(controlPanelWidth);
    area.removeFromRight(innerGap); // spacing to spectrogram
    
    if (objectSidebar)
        objectSidebar->setBounds(sidebar);

    auto controls = controlPanel.reduced(2);
    const int sectionSpacing = 10;
    const int sectionHeight = (controls.getHeight() - sectionSpacing * 2) / 3;

    auto section1 = controls.removeFromTop(sectionHeight);
    controls.removeFromTop(sectionSpacing);
    auto section2 = controls.removeFromTop(sectionHeight);
    controls.removeFromTop(sectionSpacing);
    auto section3 = controls;

    const int labelH = 18;
    const int sliderBottomPad = 4;

    auto s1Label = section1.removeFromTop(labelH);
    dryWetLabel.setBounds(s1Label);
    section1.removeFromTop(2);
    dryWetSlider.setBounds(section1.reduced(6, sliderBottomPad));

    auto s2Label = section2.removeFromTop(labelH);
    gateLabel.setBounds(s2Label);
    section2.removeFromTop(2);
    gateSlider.setBounds(section2.reduced(6, sliderBottomPad));

    auto s3Label = section3.removeFromTop(labelH);
    curveLabel.setBounds(s3Label);
    section3.removeFromTop(2);
    curveSlider.setBounds(section3.reduced(6, sliderBottomPad));
    
    // Spectrogram fills remaining area
    if (spectralView)
        spectralView->setBounds(area);
    
    // Selector overlays the spectrogram
    if (spectralSelector)
        spectralSelector->setBounds(area);

    if (storyTimeline)
        storyTimeline->setBounds(timelineArea);
}