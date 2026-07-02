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

                ObjectDatabase::ObjectMask created;
                if (db->getObjectCopy(newIndex, created))
                    processor.calibrateDensityAnchor(created);

                processor.setSelectedObjectId(newObjectId);

                // Refresh sidebar
                if (objectSidebar)
                    objectSidebar->refresh();

                if (storyTimeline)
                    storyTimeline->refresh();

                if (modulationPanel)
                    modulationPanel->refresh();

                if (fxRackPanel)
                    fxRackPanel->refresh();
            }
        }
    });
    spectralSelector->setOnBrushComplete([this](const juce::Image& brushMask)
    {
        auto* db = processor.getObjectDatabase();
        if (db == nullptr || spectralView == nullptr || !brushMask.isValid())
            return;

        std::vector<double> frameTimesSec;
        std::vector<std::array<bool, SpectralFrameBuffer::NUM_BINS>> frameMasks;
        std::array<bool, SpectralFrameBuffer::NUM_BINS> combinedMask{};
        combinedMask.fill(false);

        if (!spectralView->buildTimeFrequencyMaskFromBrushMask(brushMask, frameTimesSec, frameMasks, combinedMask))
            return;

        int minBin = SpectralFrameBuffer::NUM_BINS - 1;
        int maxBin = 0;
        for (int bin = 0; bin < SpectralFrameBuffer::NUM_BINS; ++bin)
        {
            if (!combinedMask[static_cast<size_t>(bin)])
                continue;
            minBin = juce::jmin(minBin, bin);
            maxBin = juce::jmax(maxBin, bin);
        }

        if (minBin > maxBin)
            return;

        constexpr float nyquist = 24000.0f;
        constexpr float binWidthHz = nyquist / static_cast<float>(ObjectDatabase::NUM_BINS - 1);

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

        const std::string objName = ("Brush [" + formatFreq(minBin * binWidthHz) + " - "
                                   + formatFreq(maxBin * binWidthHz) + "]").toStdString();

        if (!db->addObject(objName))
            return;

        const int newIndex = db->getNumObjects() - 1;
        const int newObjectId = db->getObjectIdAtIndex(newIndex);
        if (newObjectId < 0)
            return;

        db->setObjectTimeFrequencyMask(newObjectId, frameTimesSec, frameMasks, combinedMask);

        ObjectDatabase::ObjectMask created;
        if (db->getObjectCopy(newIndex, created))
            processor.calibrateDensityAnchor(created);

        processor.setSelectedObjectId(newObjectId);

        if (objectSidebar)
            objectSidebar->refresh();
        if (storyTimeline)
            storyTimeline->refresh();
        if (modulationPanel)
            modulationPanel->refresh();
    });
    addAndMakeVisible(*spectralSelector);

    for (auto* button : { &rectSelectButton, &lassoSelectButton })
    {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(9001);
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF27272A));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF3F3F46));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFA1A1AA));
        button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        addAndMakeVisible(*button);
    }

    rectSelectButton.setToggleState(true, juce::dontSendNotification);
    rectSelectButton.setTooltip("Rechteck-Auswahl: erzeugt die bisherige 1D-Frequenzmaske");
    lassoSelectButton.setTooltip("Pinsel-Auswahl: erzeugt eine echte 2D-Zeit-Frequenz-Maske; Shift+Scroll aendert den Durchmesser");

    rectSelectButton.onClick = [this]()
    {
        if (spectralSelector)
            spectralSelector->setToolMode(SpectralSelector::ToolMode::Rectangle);
    };

    lassoSelectButton.onClick = [this]()
    {
        if (spectralSelector)
            spectralSelector->setToolMode(SpectralSelector::ToolMode::Brush);
    };

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
            if (fxRackPanel)
                fxRackPanel->refresh();
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
                if (fxRackPanel)
                    fxRackPanel->refresh();
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
                if (fxRackPanel)
                    fxRackPanel->refresh();
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

    // FX Rack panel
    fxRackPanel = std::make_unique<FxRackPanel>(processor);
    addAndMakeVisible(*fxRackPanel);

    // Editor-centered FX browser popup (hidden by default)
    fxBrowserOverlay = std::make_unique<FxBrowserOverlay>();
    fxBrowserOverlay->onEffectChosen = [this](const juce::String& fxName)
    {
        const int objId = processor.getSelectedObjectId();
        if (objId > 0)
        {
            processor.addOrEnableObjectFx(objId, fxName);
            if (fxRackPanel)
                fxRackPanel->refresh();
            if (storyTimeline)
                storyTimeline->refresh();
        }
    };
    fxBrowserOverlay->onClose = [this]
    {
        if (fxBrowserOverlay)
            fxBrowserOverlay->setVisible(false);
    };
    addChildComponent(*fxBrowserOverlay);

    fxRackPanel->onAddFxRequested = [this]
    {
        if (fxBrowserOverlay)
        {
            fxBrowserOverlay->setBounds(getLocalBounds());
            fxBrowserOverlay->setVisible(true);
            fxBrowserOverlay->toFront(true);
            fxBrowserOverlay->grabKeyboardFocus();
        }
    };

    // Input gain slider (rotary for meter strip)
    inputGainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    inputGainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    inputGainSlider.setRange(-24.0, 24.0, 0.1);
    inputGainSlider.setValue(0.0);
    inputGainSlider.setTextValueSuffix(" dB");
    inputGainSlider.setTooltip("Input Gain");
    inputGainSlider.setLookAndFeel(&knobLookAndFeel);
    addAndMakeVisible(inputGainSlider);

    inputLabel.setText("In", juce::dontSendNotification);
    inputLabel.setJustificationType(juce::Justification::centred);
    inputLabel.setFont(juce::Font(9.0f, juce::Font::bold));
    inputLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF71717A));
    addAndMakeVisible(inputLabel);

    inputMeter.setSource(&processor.getInputPeakDb());
    addAndMakeVisible(inputMeter);

    // Output gain slider (rotary for meter strip)
    outputGainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    outputGainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    outputGainSlider.setRange(-24.0, 24.0, 0.1);
    outputGainSlider.setValue(0.0);
    outputGainSlider.setTextValueSuffix(" dB");
    outputGainSlider.setTooltip("Output Gain");
    outputGainSlider.setLookAndFeel(&knobLookAndFeel);
    addAndMakeVisible(outputGainSlider);

    outputLabel.setText("Out", juce::dontSendNotification);
    outputLabel.setJustificationType(juce::Justification::centred);
    outputLabel.setFont(juce::Font(9.0f, juce::Font::bold));
    outputLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF71717A));
    addAndMakeVisible(outputLabel);

    outputMeter.setSource(&processor.getOutputPeakDb());
    addAndMakeVisible(outputMeter);

    // Dry/Wet slider (compact rotary in header area)
    dryWetSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    dryWetSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    dryWetSlider.setRange(0.0, 1.0, 0.001);
    dryWetSlider.setValue(1.0);
    dryWetSlider.setTooltip("Dry/Wet Mix");
    addAndMakeVisible(dryWetSlider);

    dryWetLabel.setText("D/W", juce::dontSendNotification);
    dryWetLabel.setJustificationType(juce::Justification::centred);
    dryWetLabel.setFont(juce::Font(9.0f, juce::Font::bold));
    dryWetLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF71717A));
    addAndMakeVisible(dryWetLabel);

    // Gate slider (compact rotary in header area)
    gateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gateSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gateSlider.setRange(-180.0, 6.0, 1.0);
    gateSlider.setValue(-96.0);
    gateSlider.setTooltip("Gate (dB)");
    gateSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xFFE0A96D));
    gateSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xFFE0A96D));
    gateSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF27272A));
    gateSlider.onValueChange = [this]
    {
        if (spectralView)
            spectralView->setGateDb(static_cast<float>(gateSlider.getValue()));
    };
    addAndMakeVisible(gateSlider);

    gateLabel.setText("View", juce::dontSendNotification);
    gateLabel.setJustificationType(juce::Justification::centred);
    gateLabel.setFont(juce::Font(9.0f, juce::Font::bold));
    gateLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFA1A1AA));
    addAndMakeVisible(gateLabel);

    // Attachments for parameter binding
    dryWetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "dryWet", dryWetSlider);
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "inputGain", inputGainSlider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), "outputGain", outputGainSlider);

    // Version info (hidden, used for state only)
    versionLabel.setText(processor.getBuildInfo(), juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::bottomRight);
    versionLabel.setVisible(false);
    addAndMakeVisible(versionLabel);

    setResizable(true, true);
    setResizeLimits(960, 540, 1920, 1080);
    setSize(1600, 900);
}

PluginEditor::~PluginEditor()
{
    inputGainSlider.setLookAndFeel(nullptr);
    outputGainSlider.setLookAndFeel(nullptr);
}

void PluginEditor::paintHeaderBar(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xE527272A));
    g.fillRect(area.toFloat());

    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawHorizontalLine(area.getBottom() - 1, static_cast<float>(area.getX()), static_cast<float>(area.getRight()));

    // Plugin name with gradient
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    juce::ColourGradient nameGrad(juce::Colour(0xFFE4E4E7), static_cast<float>(area.getX() + 16), 0.0f,
                                   juce::Colour(0xFFA1A1AA), static_cast<float>(area.getX() + 260), 0.0f, false);
    g.setGradientFill(nameGrad);
    g.drawText("SPCTRL /\\ ARC", area.withTrimmedLeft(16).withTrimmedRight(area.getWidth() / 2),
               juce::Justification::centredLeft, false);

    // Preset selector look
    auto presetArea = juce::Rectangle<int>(area.getX() + 260, area.getY() + 10, 160, area.getHeight() - 20);
    g.setColour(juce::Colour(0xFF18181B));
    g.fillRoundedRectangle(presetArea.toFloat(), 3.0f);
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawRoundedRectangle(presetArea.toFloat(), 3.0f, 1.0f);
    g.setColour(juce::Colour(0xFFE4E4E7));
    g.setFont(juce::Font(10.0f));
    g.drawText("Default Preset", presetArea.reduced(8, 0), juce::Justification::centredLeft, false);

    // Right side status
    g.setFont(juce::Font(10.0f));
    g.setColour(juce::Colour(0xFFA1A1AA));
    auto rightArea = area.withTrimmedLeft(area.getWidth() - 200).reduced(8, 0);
    g.drawText("CPU: --", rightArea.removeFromLeft(70), juce::Justification::centredRight, false);
    g.drawText(processor.getBuildInfo(), rightArea, juce::Justification::centredRight, false);
}

void PluginEditor::paintMeterStrip(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label)
{
    juce::ignoreUnused(label);
    g.setColour(juce::Colour(0xB31C1917));
    g.fillRect(area.toFloat());
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawRect(area.toFloat(), 1.0f);
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF18181B));

    // Header bar
    paintHeaderBar(g, getLocalBounds().removeFromTop(headerHeight));

    // Footer separator
    auto footerTop = getHeight() - footerHeight;
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawHorizontalLine(footerTop, 0.0f, static_cast<float>(getWidth()));

    // Sidebar border
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawVerticalLine(sidebarWidth, static_cast<float>(headerHeight), static_cast<float>(getHeight()));
}

void PluginEditor::resized()
{
    auto area = getLocalBounds();

    area.removeFromTop(headerHeight);

    // Footer
    auto footer = area.removeFromBottom(footerHeight);

    // Center layout
    auto center = area;

    auto sidebar = center.removeFromLeft(sidebarWidth);
    if (objectSidebar)
        objectSidebar->setBounds(sidebar);

    auto inStrip = center.removeFromLeft(meterStripWidth);
    auto outStrip = center.removeFromRight(meterStripWidth);

    auto centerColumn = center;
    auto timelineArea = centerColumn.removeFromBottom(timelineHeight).reduced(2, 2);
    centerColumn.removeFromBottom(2);
    auto spectralArea = centerColumn.reduced(2, 2);

    const auto spectralBounds = spectralArea;
    if (spectralView)
        spectralView->setBounds(spectralBounds);
    if (spectralSelector)
        spectralSelector->setBounds(spectralBounds);
    if (storyTimeline)
        storyTimeline->setBounds(timelineArea);

    // Rect/Brush tools in spectral view top-left
    juce::Rectangle<int> toolArea(spectralBounds.getX() + 8, spectralBounds.getY() + 8, 116, 20);
    rectSelectButton.setBounds(toolArea.removeFromLeft(54));
    toolArea.removeFromLeft(6);
    lassoSelectButton.setBounds(toolArea.removeFromLeft(56));

    // View gain control in spectral view bottom-right
    juce::Rectangle<int> viewGainArea(spectralBounds.getRight() - 140, spectralBounds.getBottom() - 30, 132, 20);
    gateLabel.setBounds(viewGainArea.removeFromLeft(44));
    gateSlider.setBounds(viewGainArea.reduced(3, 5));

    // Dry/Wet remains attached but hidden from the mockup-centric surface
    dryWetLabel.setBounds(0, 0, 0, 0);
    dryWetSlider.setBounds(0, 0, 0, 0);

    auto layoutMeterStrip = [](juce::Rectangle<int> stripArea,
                               juce::Label& lbl,
                               juce::Slider& gain,
                               LevelMeter& meter)
    {
        auto strip = stripArea.reduced(6, 6);
        lbl.setBounds(strip.removeFromTop(12));

        auto knobArea = strip.removeFromBottom(34);
        const int knobSize = juce::jmin(knobArea.getWidth(), knobArea.getHeight());
        gain.setBounds(knobArea.withSizeKeepingCentre(knobSize, knobSize));

        strip.removeFromBottom(4);
        const int meterW = juce::jmin(12, strip.getWidth());
        meter.setBounds(strip.withSizeKeepingCentre(meterW, strip.getHeight()));
    };

    layoutMeterStrip(inStrip, inputLabel, inputGainSlider, inputMeter);
    layoutMeterStrip(outStrip, outputLabel, outputGainSlider, outputMeter);

    // Footer layout: ModulationPanel (left) + FxRackPanel (right)
    auto modArea = footer.removeFromLeft(sidebarWidth);
    if (modulationPanel)
        modulationPanel->setBounds(modArea.reduced(2));

    if (fxRackPanel)
        fxRackPanel->setBounds(footer.reduced(2));

    if (fxBrowserOverlay && fxBrowserOverlay->isVisible())
        fxBrowserOverlay->setBounds(getLocalBounds());

    versionLabel.setBounds(0, 0, 0, 0);
}
