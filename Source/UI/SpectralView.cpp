#include "SpectralView.h"

SpectralView::SpectralView(SpectralFrameBuffer* frameBuffer)
    : frameBuffer(frameBuffer), magnitudeMin(-120.0f), magnitudeMax(0.0f), showGrid(true)
{
    setOpaque(true);
    rebuildLookupTables();
    startTimerHz(60);
}

SpectralView::~SpectralView()
{
    stopTimer();
}

void SpectralView::setFrameBuffer(SpectralFrameBuffer* buffer)
{
    frameBuffer = buffer;
    lastRenderedSampleIndex = -1;
}

void SpectralView::setMagnitudeRange(float minDb, float maxDb)
{
    magnitudeMin = minDb;
    magnitudeMax = maxDb;
    rebuildLookupTables();
}

void SpectralView::setShowGrid(bool shouldShow)
{
    showGrid = shouldShow;
}

void SpectralView::setGateDb(float gateDbValue)
{
    gateDb = juce::jlimit(-180.0f, 6.0f, gateDbValue);
    magnitudeMin = gateDb;
    rebuildLookupTables();
}

void SpectralView::setFrequencyCurve(float curveAmount)
{
    frequencyCurveAmount = juce::jlimit(0.0f, 10.0f, curveAmount);
    rebuildLookupTables();
}

void SpectralView::rebuildLookupTables()
{
    for (int i = 0; i < static_cast<int>(colourLut.size()); ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(colourLut.size() - 1);

        if (t < 0.33f)
        {
            const float u = t / 0.33f;
            colourLut[static_cast<size_t>(i)] = juce::Colour::fromRGB(
                static_cast<juce::uint8>(u * 100.0f),
                static_cast<juce::uint8>(40.0f + u * 160.0f),
                static_cast<juce::uint8>(100));
        }
        else if (t < 0.66f)
        {
            const float u = (t - 0.33f) / 0.33f;
            colourLut[static_cast<size_t>(i)] = juce::Colour::fromRGB(
                static_cast<juce::uint8>(100.0f - u * 100.0f),
                static_cast<juce::uint8>(200.0f + u * 55.0f),
                static_cast<juce::uint8>(100.0f - u * 100.0f));
        }
        else
        {
            const float u = (t - 0.66f) / 0.34f;
            colourLut[static_cast<size_t>(i)] = juce::Colour::fromRGB(
                static_cast<juce::uint8>(u * 255.0f),
                static_cast<juce::uint8>(255),
                static_cast<juce::uint8>(0));
        }
    }

    const int h = juce::jmax(1, getHeight());
    yToBinMap.resize(static_cast<size_t>(h));
    yBandStart.resize(static_cast<size_t>(h));
    yBandEnd.resize(static_cast<size_t>(h));
    rowGainDb.resize(static_cast<size_t>(h));
    smoothedRowDb.assign(static_cast<size_t>(h), magnitudeMin);

    const float maxBin = static_cast<float>(SpectralFrameBuffer::NUM_BINS - 1);
    const float logMax = std::log(maxBin + 1.0f);
    const float mappingCurve = 1.0f + (frequencyCurveAmount * 0.20f);

    for (int y = 0; y < h; ++y)
    {
        const float normY = 1.0f - (static_cast<float>(y) / static_cast<float>(h - 1));
        const float curved = std::pow(normY, mappingCurve);
        const float bin = std::exp(curved * logMax) - 1.0f;
        yToBinMap[static_cast<size_t>(y)] = juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1,
            static_cast<int>(bin));

        const int lowNeighbor = juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1,
            yToBinMap[static_cast<size_t>(juce::jmin(h - 1, y + 1))]);
        const int highNeighbor = juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1,
            yToBinMap[static_cast<size_t>(juce::jmax(0, y - 1))]);

        const int bandStart = juce::jmax(0, juce::jmin(lowNeighbor, highNeighbor));
        const int bandEnd = juce::jmin(SpectralFrameBuffer::NUM_BINS - 1,
            juce::jmax(lowNeighbor, highNeighbor) + 1);

        yBandStart[static_cast<size_t>(y)] = bandStart;
        yBandEnd[static_cast<size_t>(y)] = juce::jmax(bandStart, bandEnd);

        const float binNorm = static_cast<float>(yToBinMap[static_cast<size_t>(y)]) / maxBin;
        rowGainDb[static_cast<size_t>(y)] = lowFreqEmphasisDb * (1.0f - std::sqrt(binNorm));
    }
}

juce::Colour SpectralView::magnitudeToColour(float magDb) const
{
    const float normalized = juce::jlimit(0.0f, 1.0f,
        (magDb - magnitudeMin) / (magnitudeMax - magnitudeMin));
    const int idx = juce::jlimit(0, static_cast<int>(colourLut.size()) - 1,
        static_cast<int>(normalized * static_cast<float>(colourLut.size() - 1)));
    return colourLut[static_cast<size_t>(idx)];
}

void SpectralView::appendFrameColumn(const SpectralFrameBuffer::Frame& frame)
{
    const int w = getWidth();
    const int h = getHeight();

    if (w <= 1 || h <= 1 || !spectrogramImage.isValid())
        return;

    spectrogramImage.moveImageSection(0, 0, 1, 0, w - 1, h);

    const int x = w - 1;
    for (int y = 0; y < h; ++y)
    {
        const int startBin = yBandStart[static_cast<size_t>(y)];
        const int endBin = yBandEnd[static_cast<size_t>(y)];

        float maxDb = gateDb;
        float avgDb = 0.0f;
        int bandCount = 0;
        for (int bin = startBin; bin <= endBin; ++bin)
        {
            const float v = frame.magnitude[static_cast<size_t>(bin)];
            maxDb = juce::jmax(maxDb, v);
            avgDb += v;
            ++bandCount;
        }

        if (bandCount > 0)
            avgDb /= static_cast<float>(bandCount);

        const float pooledDb = (0.65f * maxDb) + (0.35f * avgDb);

        const float emphasizedDb = pooledDb + rowGainDb[static_cast<size_t>(y)];
        const float prev = smoothedRowDb[static_cast<size_t>(y)];
        const float smoothDb = prev + temporalSmoothing * (emphasizedDb - prev);
        const float clippedDb = juce::jlimit(gateDb, magnitudeMax, smoothDb);
        smoothedRowDb[static_cast<size_t>(y)] = clippedDb;

        spectrogramImage.setPixelAt(x, y, magnitudeToColour(clippedDb));
    }
}

void SpectralView::drawSpectrumLine(juce::Graphics& g, int x, const SpectralFrameBuffer::Frame* frame)
{
    if (!frame)
        return;

    const int width = getWidth();
    const int height = getHeight();
    const int numBins = SpectralFrameBuffer::NUM_BINS;

    // Map frequency bins to vertical positions (logarithmic scale for perceptual accuracy)
    for (int bin = 1; bin < numBins - 1; ++bin)
    {
        float magDb = frame->magnitude[bin];
        juce::Colour colour = magnitudeToColour(magDb);

        // Map bin index to vertical position (logarithmic frequency scale)
        float logBin = std::log(bin + 1.0f) / std::log(numBins + 1.0f);
        int y = static_cast<int>(height * (1.0f - logBin));
        y = juce::jlimit(0, height - 1, y);

        // Draw a small vertical line segment
        g.setColour(colour);
        if (y > 0 && y < height)
            g.drawLine(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x), static_cast<float>(y + 1), 1.0f);
    }
}

void SpectralView::drawGrid(juce::Graphics& g)
{
    const int width = getWidth();
    const int height = getHeight();

    g.setColour(juce::Colour(0xFF333333));
    g.setFont(10.0f);

    // Horizontal grid lines at 1kHz intervals (approximate)
    const int numBins = SpectralFrameBuffer::NUM_BINS;
    for (int freq = 0; freq <= 20000; freq += 2000)
    {
        if (freq == 0)
            continue;  // Skip DC
        float logBin = std::log(freq + 1.0f) / std::log(20000.0f + 1.0f);
        int y = static_cast<int>(height * (1.0f - logBin));
        if (y > 0 && y < height)
        {
            g.drawLine(0.0f, static_cast<float>(y), static_cast<float>(width), static_cast<float>(y), 0.5f);
            if (freq % 4000 == 0)
                g.drawText(juce::String(freq / 1000) + "k", 2, y - 8, 30, 16, juce::Justification::topLeft, false);
        }
    }
}

void SpectralView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A1F));

    if (spectrogramImage.isValid())
        g.drawImageAt(spectrogramImage, 0, 0);

    if (showGrid)
        drawGrid(g);
}

void SpectralView::resized()
{
    const int w = juce::jmax(1, getWidth());
    const int h = juce::jmax(1, getHeight());

    spectrogramImage = juce::Image(juce::Image::RGB, w, h, true);
    rebuildLookupTables();
}

void SpectralView::timerCallback()
{
    if (frameBuffer == nullptr)
        return;

    SpectralFrameBuffer::Frame newest;
    if (!frameBuffer->copyNewestFrame(newest))
        return;

    if (newest.sampleIndex == lastRenderedSampleIndex)
        return;

    appendFrameColumn(newest);
    lastRenderedSampleIndex = newest.sampleIndex;
    repaint();
}
