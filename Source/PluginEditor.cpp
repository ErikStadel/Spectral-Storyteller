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

                if (modulationPanel)
                    modulationPanel->refresh();
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
            if (modulationPanel)
                modulationPanel->refresh();
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
                if (modulationPanel)
                    modulationPanel->refresh();
            }
        },
        [this]() -> int
        {
            const int objectId = processor.createTransientObject();
            if (objectId > 0)
            {
                if (objectSidebar)
                    objectSidebar->refresh();
                if (storyTimeline)
                    storyTimeline->refresh();
                if (modulationPanel)
                    modulationPanel->refresh();
            }

            return objectId;
        },
        [this]() -> float
        {
            if (auto* p = processor.getValueTreeState().getRawParameterValue("transientThreshold"))
                return p->load();
            return -24.0f;
        },
        [this](float thresholdDb)
        {
            if (auto* p = processor.getValueTreeState().getParameter("transientThreshold"))
                p->setValueNotifyingHost(p->convertTo0to1(thresholdDb));
        });
    addAndMakeVisible(*objectSidebar);

    // PR4: Story timeline (keyframe automation lanes)
    storyTimeline = std::make_unique<StoryTimelineComponent>(processor);
    addAndMakeVisible(*storyTimeline);

    modulationPanel = std::make_unique<ModulationPanel>(processor);
    addAndMakeVisible(*modulationPanel);
    modulationPanel->refresh();

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

        // Input gain + meter
    inputGainSlider.setSliderStyle(juce::Slider::LinearVertical);
    inputGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    inputGainSlider.setRange(-24.0, 24.0, 0.1);
    inputGainSlider.setValue(0.0);
    inputGainSlider.setTextValueSuffix(" dB");
    addAndMakeVisible(inputGainSlider);

    inputLabel.setText("Input", juce::dontSendNotification);
    inputLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(inputLabel);

    inputMeter.setSource(&processor.getInputPeakDb());
    addAndMakeVisible(inputMeter);

    // Output gain + meter
    outputGainSlider.setSliderStyle(juce::Slider::LinearVertical);
    outputGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    outputGainSlider.setRange(-24.0, 24.0, 0.1);
    outputGainSlider.setValue(0.0);
    outputGainSlider.setTextValueSuffix(" dB");
    addAndMakeVisible(outputGainSlider);

    outputLabel.setText("Output", juce::dontSendNotification);
    outputLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(outputLabel);

    outputMeter.setSource(&processor.getOutputPeakDb());
    addAndMakeVisible(outputMeter);


    // Attachment for Dry/Wet parameter (audio processing)
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "dryWet", dryWetSlider);
            inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "inputGain", inputGainSlider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "outputGain", outputGainSlider);


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
    const int modPanelWidth = 250 + 8;
    auto modArea = timelineArea.removeFromRight(modPanelWidth);
    
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
    const int labelH       = 18;
    const int meterW       = 10;   // schmaler Pegelmesser
    const int meterGap     = 4;

    // Oben: 4 vertikale Slider (Input | Output | Dry/Wet | Gate)
    auto topBlock = controls;

    const int colGap   = 6;
    const int numCols  = 4;
    const int colWidth = (topBlock.getWidth() - colGap * (numCols - 1)) / numCols;

    auto layoutSliderCol = [&](juce::Rectangle<int> col, juce::Label& lbl, juce::Slider& sld)
    {
        lbl.setBounds(col.removeFromTop(labelH));
        col.removeFromTop(2);
        sld.setBounds(col.reduced(2, 2));
    };

    auto layoutMeterCol = [&](juce::Rectangle<int> col, juce::Label& lbl,
                              juce::Slider& sld, LevelMeter& meter)
    {
        lbl.setBounds(col.removeFromTop(labelH));
        col.removeFromTop(2);
        auto meterArea = col.removeFromLeft(meterW);
        col.removeFromLeft(meterGap);
        meter.setBounds(meterArea.reduced(0, 2));
        sld.setBounds(col.reduced(2, 2));
    };

    auto inCol  = topBlock.removeFromLeft(colWidth); topBlock.removeFromLeft(colGap);
    auto outCol = topBlock.removeFromLeft(colWidth); topBlock.removeFromLeft(colGap);
    auto dryCol = topBlock.removeFromLeft(colWidth); topBlock.removeFromLeft(colGap);
    auto gateCol = topBlock;

    layoutMeterCol(inCol,  inputLabel,  inputGainSlider,  inputMeter);
    layoutMeterCol(outCol, outputLabel, outputGainSlider, outputMeter);
    layoutSliderCol(dryCol,  dryWetLabel, dryWetSlider);
    layoutSliderCol(gateCol, gateLabel,   gateSlider);

    // Spectrogram fills remaining area
    if (spectralView)
        spectralView->setBounds(area);
    
    // Selector overlays the spectrogram
    if (spectralSelector)
        spectralSelector->setBounds(area);

    if (modulationPanel)
        modulationPanel->setBounds(modArea);

    if (storyTimeline)
        storyTimeline->setBounds(timelineArea);
}