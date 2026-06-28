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
                const int newIndex = db->getNumObjects() - 1;
                const int newObjectId = db->getObjectIdAtIndex(newIndex);

                // Create binary mask for this object
                std::array<bool, ObjectDatabase::NUM_BINS> mask;
                mask.fill(false);
                for (int bin = clampedMinBin; bin <= clampedMaxBin; ++bin)
                    mask[bin] = true;

                db->setObjectMask(newIndex, mask);

                processor.setSelectedObjectId(newObjectId);

                // Refresh sidebar
                if (objectSidebar)
                    objectSidebar->refresh();

                if (storyTimeline)
                    storyTimeline->refresh();
            }
        }
    });
    addAndMakeVisible(*spectralSelector);

    // Create object sidebar
    objectSidebar = std::make_unique<ObjectSidebar>(
        *processor.getObjectDatabase(),
        [this](bool enabled)
        {
            processor.setAutoDetectRecordingEnabled(enabled);
        },
        [this]() -> juce::String
        {
            const int frames = processor.getAutoDetectFrameCount();
            const bool running = processor.isAutoDetectRunning();
            return (running ? "[REC " : "[") + juce::String(frames) + " frames]";
        },
        [this](int objectId)
        {
            processor.setSelectedObjectId(objectId);
            if (storyTimeline)
                storyTimeline->refresh();
        },
        [this](const juce::String& presetName, const juce::File& file)
        {
            int newObjectId = -1;
            if (file.existsAsFile())
                newObjectId = processor.createTransformObjectFromFile(file);
            else if (presetName.isNotEmpty())
                newObjectId = processor.createTransformObjectFromPreset(presetName);

            if (newObjectId > 0)
            {
                if (objectSidebar)
                    objectSidebar->refresh();
                if (storyTimeline)
                    storyTimeline->refresh();
            }
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

    // Transient threshold knob
    transientThresholdSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    transientThresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
    transientThresholdSlider.setRange(-60.0, 0.0, 0.1);
    transientThresholdSlider.setSkewFactorFromMidPoint(-24.0);
    transientThresholdSlider.setNumDecimalPlacesToDisplay(1);
    transientThresholdSlider.setTextValueSuffix(" dB");
    addAndMakeVisible(transientThresholdSlider);

    transientThresholdLabel.setText("Transient Gate", juce::dontSendNotification);
    transientThresholdLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(transientThresholdLabel);

    // Attachment for Dry/Wet parameter (audio processing)
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "dryWet", dryWetSlider);
    transientThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "transientThreshold", transientThresholdSlider);

    // Version info
    versionLabel.setText(processor.getBuildInfo(), juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::bottomRight);
    addAndMakeVisible(versionLabel);

    setSize(1550, 700);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A1F));
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    
    // Top: version label (24px height)
    auto topBar = area.removeFromTop(24);
    versionLabel.setBounds(topBar);
    area.removeFromTop(8);  // spacing
    
    // Reserve timeline strip; we will place it after side/center layout so the left object panel
    // can extend downward by a few rows without shrinking the spectrogram area.
    // Give the timeline enough vertical room for taller adaptive FX lanes.
    auto timelineArea = area.removeFromBottom(320);
    timelineArea.removeFromBottom(6);
    
    // Middle section: object sidebar + spectrogram + parameter sidebar
    const int sidebarWidth = 280;
    const int controlPanelWidth = 250;
    const int innerGap = 8;
    
    auto sidebar = area.removeFromLeft(sidebarWidth);
    area.removeFromLeft(innerGap);  // spacing

    auto controlPanel = area.removeFromRight(controlPanelWidth);
    area.removeFromRight(innerGap); // spacing to spectrogram
    
    if (objectSidebar)
        objectSidebar->setBounds(sidebar);

    auto controls = controlPanel.reduced(2);
    const int blockSpacing = 10;
    const int labelH = 18;

    auto topSliderBlock = controls.removeFromTop(static_cast<int>(controls.getHeight() * 0.62f));
    controls.removeFromTop(blockSpacing);
    auto gateKnobBlock = controls;

    auto sliderCols = topSliderBlock;
    const int colGap = 8;
    const int colWidth = (sliderCols.getWidth() - colGap * 2) / 3;

    auto dryCol = sliderCols.removeFromLeft(colWidth);
    sliderCols.removeFromLeft(colGap);
    auto gateCol = sliderCols.removeFromLeft(colWidth);
    sliderCols.removeFromLeft(colGap);
    auto curveCol = sliderCols;

    auto dryLabelArea = dryCol.removeFromTop(labelH);
    dryWetLabel.setBounds(dryLabelArea);
    dryCol.removeFromTop(2);
    dryWetSlider.setBounds(dryCol.reduced(2, 2));

    auto gateLabelArea = gateCol.removeFromTop(labelH);
    gateLabel.setBounds(gateLabelArea);
    gateCol.removeFromTop(2);
    gateSlider.setBounds(gateCol.reduced(2, 2));

    auto curveLabelArea = curveCol.removeFromTop(labelH);
    curveLabel.setBounds(curveLabelArea);
    curveCol.removeFromTop(2);
    curveSlider.setBounds(curveCol.reduced(2, 2));

    auto gateKnobLabel = gateKnobBlock.removeFromTop(labelH);
    transientThresholdLabel.setBounds(gateKnobLabel);
    gateKnobBlock.removeFromTop(4);
    transientThresholdSlider.setBounds(gateKnobBlock.reduced(12, 2));
    
    // Spectrogram fills remaining area
    if (spectralView)
        spectralView->setBounds(area);
    
    // Selector overlays the spectrogram
    if (spectralSelector)
        spectralSelector->setBounds(area);

    if (storyTimeline)
        storyTimeline->setBounds(timelineArea);
}