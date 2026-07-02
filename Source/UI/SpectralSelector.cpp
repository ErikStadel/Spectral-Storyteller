#include "SpectralSelector.h"

SpectralSelector::SpectralSelector()
{
    setOpaque(false);
}

SpectralSelector::~SpectralSelector() = default;

void SpectralSelector::ensureBrushMaskImage()
{
    const int w = juce::jmax(1, getWidth());
    const int h = juce::jmax(1, getHeight());
    if (!brushMaskImage.isValid() || brushMaskImage.getWidth() != w || brushMaskImage.getHeight() != h)
        brushMaskImage = juce::Image(juce::Image::ARGB, w, h, true);
}

void SpectralSelector::stampBrushAt(juce::Point<int> point)
{
    ensureBrushMaskImage();

    juce::Graphics g(brushMaskImage);
    g.setColour(juce::Colour(0xCC00CCFF));

    const float radius = 0.5f * static_cast<float>(brushDiameterPixels);
    g.fillEllipse(static_cast<float>(point.x) - radius,
                  static_cast<float>(point.y) - radius,
                  radius * 2.0f,
                  radius * 2.0f);
}

void SpectralSelector::stampBrushLine(juce::Point<int> from, juce::Point<int> to)
{
    const float dx = static_cast<float>(to.x - from.x);
    const float dy = static_cast<float>(to.y - from.y);
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float step = juce::jmax(1.0f, static_cast<float>(brushDiameterPixels) * 0.25f);
    const int numSteps = juce::jmax(1, static_cast<int>(std::ceil(distance / step)));

    for (int i = 0; i <= numSteps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(numSteps);
        const int x = juce::roundToInt(static_cast<float>(from.x) + dx * t);
        const int y = juce::roundToInt(static_cast<float>(from.y) + dy * t);
        stampBrushAt({ x, y });
    }
}

void SpectralSelector::paint(juce::Graphics& g)
{
    if (toolMode == ToolMode::Brush)
    {
        if (brushMaskImage.isValid())
            g.drawImageAt(brushMaskImage, 0, 0);

        if (hasHoverPoint)
        {
            const float radius = 0.5f * static_cast<float>(brushDiameterPixels);
            g.setColour(juce::Colour(0x2200CCFF));
            g.fillEllipse(static_cast<float>(hoverPoint.x) - radius,
                          static_cast<float>(hoverPoint.y) - radius,
                          radius * 2.0f,
                          radius * 2.0f);
            g.setColour(juce::Colour(0xEE00CCFF));
            g.drawEllipse(static_cast<float>(hoverPoint.x) - radius,
                          static_cast<float>(hoverPoint.y) - radius,
                          radius * 2.0f,
                          radius * 2.0f,
                          1.5f);
        }

        if (!isActive)
            return;

        return;
    }

    if (!isActive)
        return;

    g.setColour(juce::Colour(0x3300CCFF));

    const int x = juce::jmin(selectStartX, selectEndX);
    const int y = juce::jmin(selectStartY, selectEndY);
    const int w = std::abs(selectEndX - selectStartX) + 1;
    const int h = std::abs(selectEndY - selectStartY) + 1;

    g.fillRect(x, y, w, h);
    g.setColour(juce::Colour(0xDD00CCFF));
    g.drawRect(x, y, w, h, 2);
}

void SpectralSelector::mouseDown(const juce::MouseEvent& event)
{
    selectStartX = event.x;
    selectStartY = event.y;
    selectEndX = event.x;
    selectEndY = event.y;
    isActive = true;
    hoverPoint = event.getPosition();
    hasHoverPoint = true;

    if (toolMode == ToolMode::Brush)
    {
        brushMaskImage = juce::Image();
        ensureBrushMaskImage();
        lastBrushPoint = event.getPosition();
        stampBrushAt(lastBrushPoint);
    }

    if (onSelectionStarted)
        onSelectionStarted();

    repaint();
}

void SpectralSelector::mouseDrag(const juce::MouseEvent& event)
{
    selectEndX = event.x;
    selectEndY = event.y;
    hoverPoint = event.getPosition();
    hasHoverPoint = true;

    if (toolMode == ToolMode::Brush)
    {
        stampBrushLine(lastBrushPoint, event.getPosition());
        lastBrushPoint = event.getPosition();
    }

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

    if (toolMode == ToolMode::Brush)
    {
        if (brushMaskImage.isValid() && onBrushComplete)
            onBrushComplete(brushMaskImage);
    }
    else if (onSelectionComplete)
        onSelectionComplete(selectedMinBin, selectedMaxBin);

    brushMaskImage = juce::Image();
}

void SpectralSelector::mouseMove(const juce::MouseEvent& event)
{
    hoverPoint = event.getPosition();
    hasHoverPoint = true;
    repaint();
}

void SpectralSelector::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    hasHoverPoint = false;
    repaint();
}

void SpectralSelector::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (toolMode == ToolMode::Brush && event.mods.isShiftDown())
    {
        brushDiameterPixels = juce::jlimit(6, 180,
                                           brushDiameterPixels + juce::roundToInt(-wheel.deltaY * 48.0f));
        hoverPoint = event.getPosition();
        hasHoverPoint = true;
        repaint();
        return;
    }

    Component::mouseWheelMove(event, wheel);
}

void SpectralSelector::resized()
{
    if (toolMode == ToolMode::Brush && isActive)
        ensureBrushMaskImage();
}

void SpectralSelector::clearSelection()
{
    isActive = false;
    selectedMinBin = 0;
    selectedMaxBin = 0;
    selectedMinY = 0;
    selectedMaxY = 0;
    brushMaskImage = juce::Image();
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
