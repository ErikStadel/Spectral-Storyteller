#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/SpectralFrameBuffer.h"
#include <memory>
#include <array>

/**
 * SpectralView: Custom JUCE Component für Live-Spektrogramm-Visualisierung.
 * Zeigt rollende vertikale FFT-Spektren mit Farbverlauf (Dunkelblau zu Gelb).
 */
class SpectralView : public juce::Component, public juce::Timer
{
public:
    SpectralView(SpectralFrameBuffer* frameBuffer);
    ~SpectralView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    /**
     * Set reference to frame buffer (non-owning).
     */
    void setFrameBuffer(SpectralFrameBuffer* buffer);

    /**
     * Set magnitude range for display (in dB).
     */
    void setMagnitudeRange(float minDb, float maxDb);

    /**
     * Enable/disable grid overlay.
     */
    void setShowGrid(bool shouldShow);

    /**
     * Set display gate in dB. Values below are clipped.
     */
    void setGateDb(float gateDbValue);

    /**
     * Set frequency curve amount in range [0..10].
     */
    void setFrequencyCurve(float curveAmount);

private:
    SpectralFrameBuffer* frameBuffer = nullptr;

    float magnitudeMin = -120.0f;
    float magnitudeMax = 0.0f;
    float gateDb = -96.0f;
    float frequencyCurveAmount = 2.0f;
    bool showGrid = true;
    juce::Image spectrogramImage;
    std::vector<int> yToBinMap;
    std::vector<int> yBandStart;
    std::vector<int> yBandEnd;
    std::vector<float> rowGainDb;
    std::vector<float> smoothedRowDb;
    std::array<juce::Colour, 256> colourLut{};
    SpectralFrameBuffer::Frame scratchFrame;
    int64_t lastRenderedSampleIndex = -1;

    static constexpr float lowFreqEmphasisDb = 3.0f;
    static constexpr float temporalSmoothing = 0.32f;

    void rebuildLookupTables();
    void appendFrameColumn(const SpectralFrameBuffer::Frame& frame);

    /**
     * Convert magnitude (dB) to color (dark blue to yellow).
     */
    juce::Colour magnitudeToColour(float magDb) const;

    /**
     * Draw a single vertical spectrum line at horizontal position.
     */
    void drawSpectrumLine(juce::Graphics& g, int x, const SpectralFrameBuffer::Frame* frame);
    void drawGrid(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralView)
};
