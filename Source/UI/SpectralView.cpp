#include "SpectralView.h"
#include "Typography.h"
#include <algorithm>

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
    std::fill(visibleColumnHasData.begin(), visibleColumnHasData.end(), false);
    std::fill(visibleColumnTimesSec.begin(), visibleColumnTimesSec.end(), 0.0);
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

void SpectralView::setOverlayVisibility(bool shouldBeVisible)
{
    if (isOverlayVisible != shouldBeVisible)
    {
        isOverlayVisible = shouldBeVisible;
        repaint(); // Sorgt dafür, dass das Bild sofort aktualisiert wird
    }
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

void SpectralView::mouseMove(const juce::MouseEvent& e)
{
    cursorY = e.position.toInt().y;
    showCursorReadout = true;
    repaint();
}

void SpectralView::mouseExit(const juce::MouseEvent&)
{
    showCursorReadout = false;
    repaint();
}

void SpectralView::setExternalCursorPosition(int y, bool active)
{
    cursorY = y;
    showCursorReadout = active && y >= 0;
    repaint();
}

void SpectralView::setSegmentationOverlayProvider(std::function<bool(std::array<float, SpectralFrameBuffer::NUM_BINS>&,
                                                                     std::array<float, SpectralFrameBuffer::NUM_BINS>&,
                                                                     std::array<float, SpectralFrameBuffer::NUM_BINS>&)> provider)
{
    overlayProvider = std::move(provider);
}

void SpectralView::setSelectedObjectOverlayStateProvider(std::function<bool(int& selectedObjectId, uint64_t& revision)> provider)
{
    selectedObjectOverlayStateProvider = std::move(provider);
    selectedObjectOverlayDirty = true;
}

void SpectralView::setSelectedObjectOverlayDataProvider(std::function<bool(int objectId, SelectedObjectOverlayData& outData)> provider)
{
    selectedObjectOverlayDataProvider = std::move(provider);
    selectedObjectOverlayDirty = true;
}

void SpectralView::setAllObjectOverlayDataProvider(std::function<bool(std::vector<SelectedObjectOverlayData>& outData)> provider)
{
    allObjectOverlayDataProvider = std::move(provider);
    selectedObjectOverlayDirty = true;
}

void SpectralView::setShowAllObjectOverlays(bool shouldShowAll)
{
    if (showAllObjectOverlays == shouldShowAll)
        return;

    showAllObjectOverlays = shouldShowAll;
    selectedObjectOverlayDirty = true;
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

bool SpectralView::buildTimeFrequencyMaskFromBrushMask(const juce::Image& brushMask,
                                                       std::vector<double>& frameTimesSec,
                                                       std::vector<std::array<bool, SpectralFrameBuffer::NUM_BINS>>& frameMasks,
                                                       std::array<bool, SpectralFrameBuffer::NUM_BINS>& combinedMask) const
{
    frameTimesSec.clear();
    frameMasks.clear();
    combinedMask.fill(false);

    if (frameBuffer == nullptr || !brushMask.isValid() || getWidth() <= 0 || getHeight() <= 0)
        return false;

    const int width = juce::jmin(getWidth(), brushMask.getWidth());
    const int height = juce::jmin(getHeight(), brushMask.getHeight());
    juce::Image::BitmapData maskData(brushMask, juce::Image::BitmapData::readOnly);

    for (int x = 0; x < width; ++x)
    {
        if (x >= static_cast<int>(visibleColumnHasData.size()) || !visibleColumnHasData[static_cast<size_t>(x)])
            continue;

        std::array<bool, SpectralFrameBuffer::NUM_BINS> frameMask{};
        frameMask.fill(false);
        bool anySelected = false;

        for (int y = 0; y < height; ++y)
        {
            if (maskData.getPixelColour(x, y).getAlpha() <= 0)
                continue;

            const int bin = getBinForY(y);
            frameMask[static_cast<size_t>(bin)] = true;
            combinedMask[static_cast<size_t>(bin)] = true;
            anySelected = true;
        }

        if (!anySelected)
            continue;

        frameTimesSec.push_back(visibleColumnTimesSec[static_cast<size_t>(x)]);
        frameMasks.push_back(std::move(frameMask));
    }

    return !frameMasks.empty();
}

// =============================================================================
// rebuildLookupTables
// =============================================================================

void SpectralView::rebuildLookupTables()
{
    // -------------------------------------------------------------------------
    // 1. Spectral Storyteller Palette (Tailwind target)
    //
    //  silence/background: #000000 -> #0B0B2E
    //  very low energy:    #0B0B2E -> #2B0A7A
    //  low energy:         #2B0A7A -> #6A00A8
    //  mid energy:         #6A00A8 -> #CE0E7A
    //  high energy:        #CE0E7A -> #FF2A00
    //  very high energy:   #FF2A00 -> #FF6A00
    //  peak/transients:    #FFC400 -> #FFF4B0
    // -------------------------------------------------------------------------
    struct Anchor { float t; uint8_t r, g, b; };
    static constexpr std::array<Anchor, 8> anchors = {{
        { 0.00f,   0,   0,   0 },
        { 0.12f,  11,  11,  46 },
        { 0.26f,  43,  10, 122 },
        { 0.42f, 106,   0, 168 },
        { 0.62f, 206,  14, 122 },
        { 0.78f, 255,  42,   0 },
        { 0.90f, 255, 106,   0 },
        { 1.00f, 255, 244, 176 },
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

    if (visibleColumnTimesSec.size() == static_cast<size_t>(w) && visibleColumnHasData.size() == static_cast<size_t>(w))
    {
        for (int i = 0; i < w - 1; ++i)
        {
            visibleColumnTimesSec[static_cast<size_t>(i)] = visibleColumnTimesSec[static_cast<size_t>(i + 1)];
            visibleColumnHasData[static_cast<size_t>(i)] = visibleColumnHasData[static_cast<size_t>(i + 1)];
        }

        visibleColumnTimesSec[static_cast<size_t>(w - 1)] = frame.transportTimeSec;
        visibleColumnHasData[static_cast<size_t>(w - 1)] = true;
    }

    const int x = w - 1;

    if (selectedObjectOverlayImage.isValid())
        selectedObjectOverlayImage.moveImageSection(0, 0, 1, 0, w - 1, h);

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

        // Sanftes Fade-Out statt hartem Cutoff am Gate: vermeidet eine sichtbare
        // Kante/Banding an der Rauschgrenze. Übergang über kGateFadeRangeDb.
        const float gateFadeAmount = juce::jlimit(0.0f, 1.0f, (rawDb - gateDb) / kGateFadeRangeDb);
        const float targetDb = juce::jlimit(magnitudeMin, magnitudeMax, smooth);
        const float clippedDb = magnitudeMin + gateFadeAmount * (targetDb - magnitudeMin);
        auto pixel = magnitudeToColour(clippedDb);

        // FIX 1 & 2: Overlay nur zeichnen, wenn Objekte existieren UND Signal laut genug ist
        if (hasOverlay && isOverlayVisible)
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

            // ✨ DER "POLISH"-FIX FÜR DEN GRÜNEN HINTERGRUND ✨
            // Wir berechnen einen Fader-Wert basierend auf dem Abstand zum Gate.
            // Wenn das Signal unter dem Gate ist (Stille/Rauschen), wird gateFade zu 0.0.
            // Die Overlays werden dann nicht mehr auf den schwarzen Hintergrund gemalt.
            const float gateFade = juce::jlimit(0.0f, 1.0f, (smooth - gateDb) / 12.0f);

            const float alphaT = juce::jlimit(0.0f, 0.55f, t * 0.55f * gateFade);
            const float alphaTN = juce::jlimit(0.0f, 0.50f, tn * 0.50f * gateFade);
            const float alphaN = juce::jlimit(0.0f, 0.45f, n * 0.45f * gateFade);

            pixel = pixel.interpolatedWith(juce::Colour(0xFFFF5252), alphaT);   // Transient (Rot)
            pixel = pixel.interpolatedWith(juce::Colour(0xFF2FE0FF), alphaTN);  // Tonal (Cyan)
            pixel = pixel.interpolatedWith(juce::Colour(0xFF4FD16A), alphaN);   // Noise (Grün)
        }

        bmpData.setPixelColour(0, y, pixel);
    }

    writeSelectedObjectOverlayColumn(x, frame.transportTimeSec);
}

bool SpectralView::resolveFrameMaskForTime(const SelectedObjectOverlayData& data,
                                           double timeSec,
                                           std::array<bool, SpectralFrameBuffer::NUM_BINS>& outMask) const
{
    outMask.fill(false);

    if (!data.hasTimeFrequencyMask)
        return false;

    const auto& times = data.frameTimesSec;
    const auto& masks = data.frameMasks;
    if (times.empty() || times.size() != masks.size())
        return false;

    auto lower = std::lower_bound(times.begin(), times.end(), timeSec);
    size_t idx = 0;
    if (lower == times.end())
        idx = times.size() - 1;
    else if (lower == times.begin())
        idx = 0;
    else
    {
        const size_t hi = static_cast<size_t>(std::distance(times.begin(), lower));
        const size_t lo = hi - 1;
        idx = (std::abs(times[hi] - timeSec) < std::abs(timeSec - times[lo])) ? hi : lo;
    }

    double maxDistance = 0.03;
    if (times.size() > 1)
    {
        if (idx > 0)
            maxDistance = juce::jmax(maxDistance, 0.5 * (times[idx] - times[idx - 1]));
        if (idx + 1 < times.size())
            maxDistance = juce::jmax(maxDistance, 0.5 * (times[idx + 1] - times[idx]));
    }

    if (std::abs(times[idx] - timeSec) > maxDistance)
        return false;

    outMask = masks[idx];
    return true;
}

void SpectralView::writeSelectedObjectOverlayColumn(int x, double timeSec)
{
    if (!selectedObjectOverlayImage.isValid())
        return;

    const int h = getHeight();
    if (x < 0 || x >= selectedObjectOverlayImage.getWidth() || h <= 0)
        return;

    juce::Image::BitmapData overlayData(selectedObjectOverlayImage,
                                        x, 0, 1, h,
                                        juce::Image::BitmapData::writeOnly);

    if (visibleOverlayObjects.empty())
    {
        for (int y = 0; y < h; ++y)
            overlayData.setPixelColour(0, y, juce::Colours::transparentBlack);
        return;
    }

    std::vector<std::array<bool, SpectralFrameBuffer::NUM_BINS>> columnMasks;
    std::vector<bool> columnMaskValid;
    columnMasks.resize(visibleOverlayObjects.size());
    columnMaskValid.resize(visibleOverlayObjects.size(), false);

    for (size_t i = 0; i < visibleOverlayObjects.size(); ++i)
    {
        const auto& obj = visibleOverlayObjects[i];
        if (!obj.hasObject)
            continue;

        if (obj.hasTimeFrequencyMask)
            columnMaskValid[i] = resolveFrameMaskForTime(obj, timeSec, columnMasks[i]);
        else
        {
            columnMasks[i] = obj.combinedMask;
            columnMaskValid[i] = true;
        }
    }

    for (int y = 0; y < h; ++y)
    {
        juce::Colour pixel = juce::Colours::transparentBlack;

        const int bin = getBinForY(y);
        for (size_t i = 0; i < visibleOverlayObjects.size(); ++i)
        {
            if (!columnMaskValid[i])
                continue;

            const auto& maskForColumn = columnMasks[i];
            if (!maskForColumn[static_cast<size_t>(bin)])
                continue;

            bool edge = false;
            if (y == 0 || y == h - 1)
            {
                edge = true;
            }
            else
            {
                const int prevBin = getBinForY(y - 1);
                const int nextBin = getBinForY(y + 1);
                edge = !maskForColumn[static_cast<size_t>(prevBin)] || !maskForColumn[static_cast<size_t>(nextBin)];
            }

            const auto& obj = visibleOverlayObjects[i];
            const juce::Colour fillColour = obj.colour.withAlpha(0.18f);
            const juce::Colour edgeColour = obj.colour.withAlpha(0.64f);
            pixel = pixel.interpolatedWith(edge ? edgeColour : fillColour, 0.9f);
        }

        overlayData.setPixelColour(0, y, pixel);
    }
}

void SpectralView::rebuildSelectedObjectOverlayFromVisibleColumns()
{
    const int w = juce::jmax(1, getWidth());
    const int h = juce::jmax(1, getHeight());
    selectedObjectOverlayImage = juce::Image(juce::Image::ARGB, w, h, true);

    if (visibleOverlayObjects.empty())
        return;

    for (int x = 0; x < w; ++x)
    {
        if (x >= static_cast<int>(visibleColumnHasData.size()) || !visibleColumnHasData[static_cast<size_t>(x)])
            continue;

        writeSelectedObjectOverlayColumn(x, visibleColumnTimesSec[static_cast<size_t>(x)]);
    }
}

void SpectralView::updateSelectedObjectOverlayState()
{
    if (!selectedObjectOverlayStateProvider)
        return;

    int selectedObjectId = -1;
    uint64_t revision = 0;
    if (!selectedObjectOverlayStateProvider(selectedObjectId, revision))
        return;

    if (showAllObjectOverlays != cachedShowAllObjectOverlays
        || selectedObjectId != cachedSelectedObjectId
        || revision != cachedSelectedObjectRevision)
    {
        cachedShowAllObjectOverlays = showAllObjectOverlays;
        cachedSelectedObjectId = selectedObjectId;
        cachedSelectedObjectRevision = revision;

        visibleOverlayObjects.clear();
        if (showAllObjectOverlays)
        {
            if (allObjectOverlayDataProvider)
            {
                std::vector<SelectedObjectOverlayData> allData;
                if (allObjectOverlayDataProvider(allData))
                    visibleOverlayObjects = std::move(allData);
            }
        }
        else
        {
            SelectedObjectOverlayData newData;
            bool ok = false;
            if (selectedObjectId > 0 && selectedObjectOverlayDataProvider)
                ok = selectedObjectOverlayDataProvider(selectedObjectId, newData);

            if (ok)
                visibleOverlayObjects.push_back(std::move(newData));
        }

        selectedObjectOverlayDirty = true;
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

    g.setFont(Typography::getLabelFont(false));

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
    g.setFont(Typography::getValueFont());
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

        const int dbLabelW = 34;
        const int dbLabelH = 12;
        const int dbLabelY = juce::jlimit(0, height - dbLabelH, y - dbLabelH / 2);
        const int dbLabelX = width - dbLabelW - 2;

        g.setColour(juce::Colour(0x88000000));
        g.fillRect(dbLabelX, dbLabelY, dbLabelW, dbLabelH);

        g.setColour(juce::Colour(0xBBCCCCCC));
        g.drawText(juce::String(static_cast<int>(db)) + " dB",
                   dbLabelX, dbLabelY, dbLabelW, dbLabelH,
                   juce::Justification::centredRight, false);
    }
}

// =============================================================================
// drawCursorReadout  –  Frequenz-Crosshair unter der Maus
// =============================================================================

void SpectralView::drawCursorReadout(juce::Graphics& g)
{
    const int width  = getWidth();
    const int height = getHeight();
    if (width < 2 || height < 2 || cursorY < 0 || cursorY >= height)
        return;

    // Selbe Bin-Position wie im Renderer (yToBinF), damit die Anzeige exakt
    // dem entspricht, was gerade auf dem Bildschirm zu sehen ist.
    const int bin = getBinForY(cursorY);
    const float nyquist = kSampleRate * 0.5f;
    const float freq = (static_cast<float>(bin) / static_cast<float>(SpectralFrameBuffer::NUM_BINS - 1)) * nyquist;

    juce::String label = (freq >= 1000.0f)
        ? (juce::String(freq / 1000.0f, 2) + " kHz")
        : (juce::String(static_cast<int>(freq)) + " Hz");

    // Gestrichelte Fadenkreuz-Linie
    juce::Path straight;
    straight.startNewSubPath(0.0f, static_cast<float>(cursorY) + 0.5f);
    straight.lineTo(static_cast<float>(width), static_cast<float>(cursorY) + 0.5f);
    juce::Path dashed;
    const float dashLengths[] = { 4.0f, 3.0f };
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, straight, dashLengths, 2);
    g.setColour(juce::Colour(0x55FFFFFF));
    g.fillPath(dashed);

    // Readout-Chip
    g.setFont(Typography::getHeaderFont());
    const int labelW = 64;
    const int labelH = 16;
    const int labelY = juce::jlimit(0, height - labelH, cursorY - labelH / 2);
    const int labelX = width - labelW - 6;

    g.setColour(juce::Colour(0xDD000000));
    g.fillRoundedRectangle(static_cast<float>(labelX), static_cast<float>(labelY),
                           static_cast<float>(labelW), static_cast<float>(labelH), 3.0f);
    g.setColour(juce::Colours::white);
    g.drawText(label, labelX, labelY, labelW, labelH, juce::Justification::centred, false);
}

// =============================================================================
// paint / resized / timerCallback
// =============================================================================

void SpectralView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF0C0A09));

    if (spectrogramImage.isValid())
        g.drawImageAt(spectrogramImage, 0, 0);

    if (selectedObjectOverlayImage.isValid())
        g.drawImageAt(selectedObjectOverlayImage, 0, 0);

    if (showGrid)
        drawGrid(g);

    if (showCursorReadout)
        drawCursorReadout(g);
}

void SpectralView::resized()
{
    const int w = juce::jmax(1, getWidth());
    const int h = juce::jmax(1, getHeight());

    spectrogramImage = juce::Image(juce::Image::RGB, w, h, true);
    selectedObjectOverlayImage = juce::Image(juce::Image::ARGB, w, h, true);
    visibleColumnTimesSec.assign(static_cast<size_t>(w), 0.0);
    visibleColumnHasData.assign(static_cast<size_t>(w), false);
    selectedObjectOverlayDirty = true;
    rebuildLookupTables();
}

void SpectralView::timerCallback()
{
    updateSelectedObjectOverlayState();

    if (selectedObjectOverlayDirty)
    {
        rebuildSelectedObjectOverlayFromVisibleColumns();
        selectedObjectOverlayDirty = false;
        repaint();
    }

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