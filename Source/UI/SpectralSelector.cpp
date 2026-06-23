#include "SpectralSelector.h"

SpectralSelector::SpectralSelector()
{
    setOpaque(false);
}

SpectralSelector::~SpectralSelector() = default;

void SpectralSelector::paint(juce::Graphics& g)
{
    if (!isActive)
        return;

    // Draw selection rectangle with semi-transparent fill
    const int x = juce::jmin(selectStartX, selectEndX);
    const int y = juce::jmin(selectStartY, selectEndY);
    const int w = std::abs(selectEndX - selectStartX) + 1;
    const int h = std::abs(selectEndY - selectStartY) + 1;

    g.setColour(juce::Colour(0x3300CCFF));  // Light blue, semi-transparent
    g.fillRect(x, y, w, h);

    g.setColour(juce::Colour(0xDD00CCFF));  // Bright blue outline
    g.drawRect(x, y, w, h, 2);
}

void SpectralSelector::mouseDown(const juce::MouseEvent& event)
{
    selectStartX = event.x;
    selectStartY = event.y;
    selectEndX = event.x;
    selectEndY = event.y;
    isActive = true;

    if (onSelectionStarted)
        onSelectionStarted();

    repaint();
}

void SpectralSelector::mouseDrag(const juce::MouseEvent& event)
{
    selectEndX = event.x;
    selectEndY = event.y;
    repaint();
}

void SpectralSelector::mouseUp(const juce::MouseEvent& event)
{
    selectEndX = event.x;
    selectEndY = event.y;

    const int minY = juce::jmin(selectStartY, selectEndY);
    const int maxY = juce::jmax(selectStartY, selectEndY);

    selectedMinY = minY;
    selectedMaxY = maxY;

    // Convert pixel Y to bin indices
    selectedMinBin = yPixelToBin(minY, getHeight());
    selectedMaxBin = yPixelToBin(maxY, getHeight());

    if (selectedMinBin > selectedMaxBin)
        std::swap(selectedMinBin, selectedMaxBin);

    isActive = false;
    repaint();

    if (onSelectionFinished)
        onSelectionFinished();

    if (onSelectionComplete)
        onSelectionComplete(selectedMinBin, selectedMaxBin);
}

void SpectralSelector::clearSelection()
{
    isActive = false;
    selectedMinBin = 0;
    selectedMaxBin = 0;
    selectedMinY = 0;
    selectedMaxY = 0;
    repaint();
}

int SpectralSelector::yPixelToBin(int y, int height) const
{
    if (yToBinMapper)
        return juce::jlimit(0, NUM_BINS - 1, yToBinMapper(y, height));

    if (height <= 1)
        return 0;

    // Linear mapping: y=0 (top, high freq) → bin NUM_BINS-1
    //                 y=height-1 (bottom, low freq) → bin 0
    const float normY = static_cast<float>(y) / static_cast<float>(height - 1);
    const int bin = juce::jlimit(0, NUM_BINS - 1,
        static_cast<int>((1.0f - normY) * static_cast<float>(NUM_BINS - 1)));
    return bin;
}

void SpectralSelector::getSelectedBinRange(int& minBin, int& maxBin) const
{
    minBin = selectedMinBin;
    maxBin = selectedMaxBin;
}

void SpectralSelector::getSelectedFreqRange(float& minFreq, float& maxFreq, int height) const
{
    // Simple approximation: bin → frequency assuming Nyquist = 24kHz @ 48kHz SR
    constexpr float kSampleRate = 48000.0f;
    constexpr float kNyquist = kSampleRate * 0.5f;
    const float binWidth = kNyquist / static_cast<float>(NUM_BINS - 1);

    minFreq = static_cast<float>(selectedMinBin) * binWidth;
    maxFreq = static_cast<float>(selectedMaxBin) * binWidth;
}
