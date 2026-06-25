#include "SpectralView.h"

// =============================================================================
// Konstruktor / Destruktor
// =============================================================================

SpectralView::SpectralView(SpectralFrameBuffer* fb)
    : frameBuffer(fb)
{
    setOpaque(true);
    rebuildLookupTables();
    startTimerHz(60);
}

SpectralView::~SpectralView()
{
    stopTimer();
}

// =============================================================================
// Public API
// =============================================================================

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
    repaint();
}

void SpectralView::setGateDb(float gateDbValue)
{
    gateDb       = juce::jlimit(-180.0f, 6.0f, gateDbValue);
    magnitudeMin = gateDb;
    rebuildLookupTables();
}

void SpectralView::setFrequencyCurve(float curveAmount)
{
    frequencyCurveAmount = juce::jlimit(0.0f, 10.0f, curveAmount);
    rebuildLookupTables();
}

void SpectralView::setPaused(bool shouldPause)
{
    isPaused = shouldPause;
}

void SpectralView::setSegmentationOverlayProvider(std::function<bool(std::array<float, SpectralFrameBuffer::NUM_BINS>&,
                                                                     std::array<float, SpectralFrameBuffer::NUM_BINS>&,
                                                                     std::array<float, SpectralFrameBuffer::NUM_BINS>&)> provider)
{
    overlayProvider = std::move(provider);
}

void SpectralView::setDebugTextProvider(std::function<juce::String()> provider)
{
    debugTextProvider = std::move(provider);
}

int SpectralView::getBinForY(int y) const
{
    if (yToBinF.empty())
    {
        const int h = juce::jmax(1, getHeight());
        const float normY = static_cast<float>(juce::jlimit(0, h - 1, y))
            / static_cast<float>(h - 1);
        const float fallbackBin = (1.0f - normY) * static_cast<float>(SpectralFrameBuffer::NUM_BINS - 1);
        return juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1,
            static_cast<int>(std::round(fallbackBin)));
    }

    const int clampedY = juce::jlimit(0, static_cast<int>(yToBinF.size()) - 1, y);
    const float binF = yToBinF[static_cast<size_t>(clampedY)];
    return juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1,
        static_cast<int>(std::round(binF)));
}

// =============================================================================
// rebuildLookupTables
// =============================================================================

void SpectralView::rebuildLookupTables()
{
    // -------------------------------------------------------------------------
    // 1. Cockos-thermische Farbpalette (8 Ankerpunkte, linear interpoliert)
    //
    //  t=0.00  Schwarz     (  0,   0,   0)  Stille / unterhalb Gate
    //  t=0.14  Navy        (  0,   0,  80)
    //  t=0.28  Blau        (  0,   0, 220)
    //  t=0.42  Cyan        (  0, 210, 210)
    //  t=0.57  Grün        (  0, 210,   0)
    //  t=0.71  Gelb        (220, 220,   0)
    //  t=0.85  Orange      (255, 120,   0)
    //  t=1.00  Weiß        (255, 255, 255)  Vollpegel
    // -------------------------------------------------------------------------
    struct Anchor { float t; uint8_t r, g, b; };
    static constexpr std::array<Anchor, 8> anchors = {{
        { 0.00f,   0,   0,   0 },
        { 0.14f,   0,   0,  80 },
        { 0.28f,   0,   0, 220 },
        { 0.42f,   0, 210, 210 },
        { 0.57f,   0, 210,   0 },
        { 0.71f, 220, 220,   0 },
        { 0.85f, 255, 120,   0 },
        { 1.00f, 255, 255, 255 },
    }};

    for (int i = 0; i < LUT_SIZE; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(LUT_SIZE - 1);

        int seg = static_cast<int>(anchors.size()) - 2;
        for (int s = 0; s < static_cast<int>(anchors.size()) - 1; ++s)
        {
            if (t <= anchors[static_cast<size_t>(s + 1)].t)
            {
                seg = s;
                break;
            }
        }

        const auto& lo = anchors[static_cast<size_t>(seg)];
        const auto& hi = anchors[static_cast<size_t>(seg + 1)];
        const float span = hi.t - lo.t;
        const float u    = (span > 0.0f) ? juce::jlimit(0.0f, 1.0f, (t - lo.t) / span) : 0.0f;

        colourLut[static_cast<size_t>(i)] = juce::Colour::fromRGB(
            static_cast<uint8_t>(lo.r + u * static_cast<float>(static_cast<int>(hi.r) - static_cast<int>(lo.r))),
            static_cast<uint8_t>(lo.g + u * static_cast<float>(static_cast<int>(hi.g) - static_cast<int>(lo.g))),
            static_cast<uint8_t>(lo.b + u * static_cast<float>(static_cast<int>(hi.b) - static_cast<int>(lo.b))));
    }

    // -------------------------------------------------------------------------
    // 2. Pro-Zeile-Tabellen: kontinuierliche Bin-Position (float!) statt int
    //
    // Kernfix: statt yToBin[y] = int(bin) speichern wir yToBinF[y] = float(bin).
    // Das erlaubt in appendFrameColumn() eine lineare Interpolation ZWISCHEN
    // benachbarten Bins, was sowohl das "Blocking" als auch das schwarze Loch heilt:
    //
    //   - Blocking: mehrere Pixel auf demselben int-Bin hatten identische Farbe.
    //     Mit float-Position interpolieren wir die Farbe kontinuierlich.
    //
    //   - Schwarzes Loch 30–55 Hz: Bin 1 (23–47 Hz) ist ein einziger FFT-Bin
    //     der auf ~80 Pixel gestreckt wird. Bin 2 (47–70 Hz) auf ~40 Pixel.
    //     Mit bilinearer Interpolation zwischen Bin 1 und Bin 2 bekommt jede
    //     Zeile einen kontinuierlichen Pegel statt eines harten Sprungs auf 0.
    // -------------------------------------------------------------------------

    const int h = juce::jmax(1, getHeight());

    yToBinF.resize(static_cast<size_t>(h));
    rowGainDb.resize(static_cast<size_t>(h));
    smoothedRowDb.assign(static_cast<size_t>(h), magnitudeMin);

    const float nyquist  = kSampleRate * 0.5f;
    const float logRange = std::log(nyquist / kMinFreq);
    const float numBinsF = static_cast<float>(SpectralFrameBuffer::NUM_BINS - 1);

    for (int y = 0; y < h; ++y)
    {
        // normY: 0 = oben (Nyquist), 1 = unten (kMinFreq)
        const float normY = static_cast<float>(y) / static_cast<float>(h - 1);

        // Log-Mapping mit optionaler Tiefen-Betonung via frequencyCurveAmount
        const float exponent = std::pow(1.0f - normY, 1.0f + frequencyCurveAmount * 0.12f);
        const float freq     = kMinFreq * std::exp(exponent * logRange);

        // Kontinuierliche (float!) Bin-Position
        const float binF = freq / nyquist * numBinsF;
        yToBinF[static_cast<size_t>(y)] = juce::jlimit(0.0f, numBinsF, binF);

        // Low-Freq-Emphasis
        const float binNorm = binF / numBinsF;
        rowGainDb[static_cast<size_t>(y)] = lowFreqEmphasisDb * (1.0f - std::sqrt(binNorm));
    }
}

// =============================================================================
// freqToY  –  Einheitliche Frequenz → Y-Pixel-Funktion (für Grid)
// =============================================================================

int SpectralView::freqToY(float freq, int height) const
{
    if (height <= 1) return 0;

    const float nyquist  = kSampleRate * 0.5f;
    const float logRange = std::log(nyquist / kMinFreq);
    const float logFreq  = std::log(juce::jlimit(kMinFreq, nyquist, freq) / kMinFreq);

    // Gleiche Formel wie in rebuildLookupTables() (ohne CurveAmount, der gilt nur
    // fürs Pixel-Mapping, nicht für Grid-Label-Positionen)
    const float normY = 1.0f - (logFreq / logRange);
    return juce::jlimit(0, height - 1,
        static_cast<int>(normY * static_cast<float>(height - 1)));
}

// =============================================================================
// magnitudeToColour
// =============================================================================

juce::Colour SpectralView::magnitudeToColour(float magDb) const noexcept
{
    const float normalized = (magDb - magnitudeMin) / (magnitudeMax - magnitudeMin);
    const int idx = juce::jlimit(0, LUT_SIZE - 1,
        static_cast<int>(normalized * static_cast<float>(LUT_SIZE - 1)));
    return colourLut[static_cast<size_t>(idx)];
}

// =============================================================================
// interpolateMagnitude  –  Bilineare Bin-Interpolation
// =============================================================================
//
// Liest einen Pegel (dB) an einer kontinuierlichen Bin-Position binF aus dem
// Frame-Magnitude-Array heraus, indem es zwischen floor(binF) und ceil(binF)
// linear interpoliert.
//
// Das ist der Kernfix für beide Bugs:
//   - Glättet den Übergang zwischen niedrigen Bins (Bassbereich)
//   - Verhindert identische Farbe für mehrere Pixel auf dem gleichen int-Bin

static float interpolateMagnitude(const SpectralFrameBuffer::Frame& frame, float binF)
{
    const int   binLo   = static_cast<int>(binF);
    const int   binHi   = juce::jmin(binLo + 1, SpectralFrameBuffer::NUM_BINS - 1);
    const float frac    = binF - static_cast<float>(binLo);
    const float magLo   = frame.magnitude[static_cast<size_t>(binLo)];
    const float magHi   = frame.magnitude[static_cast<size_t>(binHi)];
    return magLo + frac * (magHi - magLo);
}

// =============================================================================
// appendFrameColumn  –  Neue Spalte in spectrogramImage einschreiben
// =============================================================================

void SpectralView::appendFrameColumn(const SpectralFrameBuffer::Frame& frame)
{
    const int w = getWidth();
    const int h = getHeight();

    if (w <= 1 || h <= 1 || !spectrogramImage.isValid())
        return;

    // Bild um 1 Pixel nach links scrollen
    spectrogramImage.moveImageSection(0, 0, 1, 0, w - 1, h);

    const int x = w - 1;

    // BitmapData nach moveImageSection: readWrite nötig, da moveImageSection
    // intern in-place kopiert hat; writeOnly wäre unsicher auf manchen Backends.
    juce::Image::BitmapData bmpData(spectrogramImage,
                                    x, 0, 1, h,
                                    juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < h; ++y)
    {
        const float binF = yToBinF[static_cast<size_t>(y)];

        // Interpolierter Pegel an kontinuierlicher Bin-Position
        const float rawDb = interpolateMagnitude(frame, binF);

        // Low-Freq-Emphasis + temporale Glättung
        const float emphasizedDb = rawDb + rowGainDb[static_cast<size_t>(y)];
        float& smooth = smoothedRowDb[static_cast<size_t>(y)];
        smooth = smooth + temporalSmoothing * (emphasizedDb - smooth);

        const float clippedDb = juce::jlimit(magnitudeMin, magnitudeMax, smooth);
        auto pixel = magnitudeToColour(clippedDb);

        if (hasOverlay)
        {
            const int binLo = juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1, static_cast<int>(binF));
            const int binHi = juce::jmin(SpectralFrameBuffer::NUM_BINS - 1, binLo + 1);
            const float frac = binF - static_cast<float>(binLo);

            const float t = overlayTransient[static_cast<size_t>(binLo)]
                          + frac * (overlayTransient[static_cast<size_t>(binHi)] - overlayTransient[static_cast<size_t>(binLo)]);
            const float tn = overlayTonal[static_cast<size_t>(binLo)]
                           + frac * (overlayTonal[static_cast<size_t>(binHi)] - overlayTonal[static_cast<size_t>(binLo)]);
            const float n = overlayNoise[static_cast<size_t>(binLo)]
                          + frac * (overlayNoise[static_cast<size_t>(binHi)] - overlayNoise[static_cast<size_t>(binLo)]);

            const float alphaT = juce::jlimit(0.0f, 0.55f, t * 0.55f);
            const float alphaTN = juce::jlimit(0.0f, 0.50f, tn * 0.50f);
            const float alphaN = juce::jlimit(0.0f, 0.45f, n * 0.45f);

            pixel = pixel.interpolatedWith(juce::Colour(0xFFFF5252), alphaT);
            pixel = pixel.interpolatedWith(juce::Colour(0xFF4AA3FF), alphaTN);
            pixel = pixel.interpolatedWith(juce::Colour(0xFF4FD16A), alphaN);
        }

        bmpData.setPixelColour(0, y, pixel);
    }
}

// =============================================================================
// drawGrid  –  Frequenzachse und Labels
// =============================================================================

void SpectralView::drawGrid(juce::Graphics& g)
{
    const int width  = getWidth();
    const int height = getHeight();
    if (width < 2 || height < 2) return;

    g.setFont(juce::Font(9.5f));

    for (int freq : kGridFreqs)
    {
        const int y = freqToY(static_cast<float>(freq), height);

        g.setColour(juce::Colour(0x22FFFFFF));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(width));

        juce::String label;
        if (freq >= 1000)
            label = (freq % 1000 == 0)
                    ? (juce::String(freq / 1000) + "k")
                    : (juce::String(freq / 1000.0f, 1) + "k");
        else
            label = juce::String(freq);

        const int labelW = 26;
        const int labelH = 12;
        const int labelY = juce::jlimit(0, height - labelH, y - labelH / 2);

        g.setColour(juce::Colour(0x88000000));
        g.fillRect(2, labelY, labelW, labelH);

        g.setColour(juce::Colour(0xBBCCCCCC));
        g.drawText(label, 3, labelY, labelW, labelH,
                   juce::Justification::centredLeft, false);
    }

    // dB-Skala rechts
    g.setFont(juce::Font(9.0f));
    static constexpr int numDbMarks = 5;
    for (int i = 0; i <= numDbMarks; ++i)
    {
        const float db = magnitudeMin + static_cast<float>(i) *
                         (magnitudeMax - magnitudeMin) / static_cast<float>(numDbMarks);
        const float normY = 1.0f - juce::jlimit(0.0f, 1.0f,
            (db - magnitudeMin) / (magnitudeMax - magnitudeMin));
        const int y = juce::jlimit(0, height - 1,
            static_cast<int>(normY * static_cast<float>(height - 1)));

        g.setColour(juce::Colour(0x18FFFFFF));
        g.drawHorizontalLine(y, static_cast<float>(width - 36), static_cast<float>(width));

        g.setColour(juce::Colour(0x99AAAAAA));
        g.drawText(juce::String(static_cast<int>(db)) + "dB",
                   width - 35, y - 6, 33, 12,
                   juce::Justification::centredRight, false);
    }
}

// =============================================================================
// paint / resized / timerCallback
// =============================================================================

void SpectralView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF080810));

    if (spectrogramImage.isValid())
        g.drawImageAt(spectrogramImage, 0, 0);

    if (showGrid)
        drawGrid(g);

    if (debugText.isNotEmpty())
    {
        const int boxW = juce::jmin(getWidth() - 12, 360);
        const int boxH = 56;
        const juce::Rectangle<int> box(6, 6, boxW, boxH);

        g.setColour(juce::Colour(0xAA000000));
        g.fillRoundedRectangle(box.toFloat(), 5.0f);
        g.setColour(juce::Colour(0x66FFFFFF));
        g.drawRoundedRectangle(box.toFloat(), 5.0f, 1.0f);

        g.setColour(juce::Colour(0xFFE8E8E8));
        g.setFont(juce::Font(11.0f));
        g.drawFittedText(debugText, box.reduced(6, 4), juce::Justification::topLeft, 3);
    }
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
    if (isPaused)
        return;

    if (frameBuffer == nullptr)
        return;

    const int totalFrames = frameBuffer->getNumFrames();
    if (totalFrames == 0)
        return;

    SpectralFrameBuffer::Frame tmp;
    if (!frameBuffer->copyNewestFrame(tmp))
        return;

    if (tmp.sampleIndex == lastRenderedSampleIndex)
        return;

    if (overlayProvider)
        hasOverlay = overlayProvider(overlayTransient, overlayTonal, overlayNoise);
    else
        hasOverlay = false;

    if (debugTextProvider)
        debugText = debugTextProvider();
    else
        debugText.clear();

    // Multi-Frame-Drain: alle neuen Frames konsumieren, max. 8 pro Tick
    static constexpr int kMaxFramesPerTick = 8;
    int rendered = 0;
    bool didRender = false;

    for (int fi = 0; fi < totalFrames && rendered < kMaxFramesPerTick; ++fi)
    {
        SpectralFrameBuffer::Frame frame;
        if (!frameBuffer->copyFrame(fi, frame))
            continue;

        if (frame.sampleIndex <= lastRenderedSampleIndex)
            continue;

        appendFrameColumn(frame);
        lastRenderedSampleIndex = frame.sampleIndex;
        didRender = true;
        ++rendered;
    }

    if (didRender)
        repaint();
}