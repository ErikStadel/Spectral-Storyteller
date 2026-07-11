#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/SpectralFrameBuffer.h"
#include <array>
#include <vector>
#include <functional>

/**
 * SpectralView – Live-Wasserfall-Spektrogramm für Spectral Storyteller.
 *
 * Kerndesign:
 *   - Thermische 8-Stufen-Palette (Schwarz → Navy → Blau → Cyan → Grün → Gelb → Orange → Weiß)
 *   - Kontinuierliches float-Bin-Mapping (yToBinF) statt int-Lookup →
 *     bilineare Interpolation zwischen FFT-Bins, eliminiert Blocking und
 *     schwarze Löcher im Bassbereich
 *   - Einheitliche freqToY()-Funktion für Rendering UND Grid → keine Verschiebung
 *   - Multi-Frame-Drain: alle neuen Frames pro Timer-Tick konsumiert
 */
class SpectralView : public juce::Component, public juce::Timer
{
public:
    struct SelectedObjectOverlayData
    {
        bool hasObject = false;
        int objectId = -1;
        juce::Colour colour = juce::Colour(0xFF00CCFF);
        bool hasTimeFrequencyMask = false;
        std::array<bool, SpectralFrameBuffer::NUM_BINS> combinedMask{};
        std::vector<double> frameTimesSec;
        std::vector<std::array<bool, SpectralFrameBuffer::NUM_BINS>> frameMasks;
    };

    explicit SpectralView(SpectralFrameBuffer* frameBuffer);
    ~SpectralView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void setOverlayVisibility(bool shouldBeVisible);
    bool hasActiveSpectralMasks() const;

    /**
     * Crosshair-Position von außen setzen. Notwendig, weil SpectralSelector
     * als Overlay über diesem Component liegt und alle Mausereignisse
     * abfängt – eigenes mouseMove kommt hier praktisch nie an.
     * y < 0 oder active=false blendet den Crosshair aus.
     */
    void setExternalCursorPosition(int y, bool active);

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    void setFrameBuffer(SpectralFrameBuffer* buffer);
    void setMagnitudeRange(float minDb, float maxDb);
    void setShowGrid(bool shouldShow);
    void setGateDb(float gateDbValue);
    void setFrequencyCurve(float curveAmount);
    void setPaused(bool shouldPause);
    void setSegmentationOverlayProvider(std::function<bool(std::array<float, SpectralFrameBuffer::NUM_BINS>&,
                                                            std::array<float, SpectralFrameBuffer::NUM_BINS>&,
                                                            std::array<float, SpectralFrameBuffer::NUM_BINS>&)> provider);
    void setSelectedObjectOverlayStateProvider(std::function<bool(int& selectedObjectId, uint64_t& revision)> provider);
    void setSelectedObjectOverlayDataProvider(std::function<bool(int objectId, SelectedObjectOverlayData& outData)> provider);
    void setAllObjectOverlayDataProvider(std::function<bool(std::vector<SelectedObjectOverlayData>& outData)> provider);
    void setShowAllObjectOverlays(bool shouldShowAll);
    int getBinForY(int y) const;
    bool buildTimeFrequencyMaskFromBrushMask(const juce::Image& brushMask,
                                             std::vector<double>& frameTimesSec,
                                             std::vector<std::array<bool, SpectralFrameBuffer::NUM_BINS>>& frameMasks,
                                             std::array<bool, SpectralFrameBuffer::NUM_BINS>& combinedMask) const;

private:
    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    SpectralFrameBuffer* frameBuffer = nullptr;

    float magnitudeMin         = -120.0f;
    float magnitudeMax         =    0.0f;
    float gateDb               =  -96.0f;
    float frequencyCurveAmount =    2.0f;
    bool  showGrid             = true;
    bool  isPaused             = false;
    bool isOverlayVisible = false;

    // Cursor-Frequenz-Readout (Hover-Crosshair)
    bool showCursorReadout = false;
    int  cursorY           = -1;

    juce::Image spectrogramImage;

    // Kontinuierliche (float) Bin-Position pro Y-Pixel.
    // Ersetzt das frühere int-Array yToBinMap und ermöglicht bilineare
    // Interpolation in interpolateMagnitude().
    std::vector<float> yToBinF;

    std::vector<float> rowGainDb;       // Low-Freq-Emphasis [dB] pro Zeile
    std::vector<float> smoothedRowDb;   // Temporales Glättungs-State pro Zeile

    std::function<bool(std::array<float, SpectralFrameBuffer::NUM_BINS>&,
                       std::array<float, SpectralFrameBuffer::NUM_BINS>&,
                       std::array<float, SpectralFrameBuffer::NUM_BINS>&)> overlayProvider;
    std::array<float, SpectralFrameBuffer::NUM_BINS> overlayTransient{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> overlayTonal{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> overlayNoise{};
    bool hasOverlay = false;

    std::function<bool(int& selectedObjectId, uint64_t& revision)> selectedObjectOverlayStateProvider;
    std::function<bool(int objectId, SelectedObjectOverlayData& outData)> selectedObjectOverlayDataProvider;
    std::function<bool(std::vector<SelectedObjectOverlayData>& outData)> allObjectOverlayDataProvider;
    SelectedObjectOverlayData selectedObjectOverlayData;
    std::vector<SelectedObjectOverlayData> visibleOverlayObjects;
    juce::Image selectedObjectOverlayImage;
    int cachedSelectedObjectId = -1;
    uint64_t cachedSelectedObjectRevision = 0;
    bool cachedShowAllObjectOverlays = false;
    bool showAllObjectOverlays = false;
    bool selectedObjectOverlayDirty = true;

    // 512-Einträge Farb-LUT
    static constexpr int LUT_SIZE = 512;
    std::array<juce::Colour, LUT_SIZE> colourLut{};

    int64_t lastRenderedSampleIndex = -1;
    std::vector<double> visibleColumnTimesSec;
    std::vector<bool> visibleColumnHasData;

    // Physikalische Konstanten
    static constexpr float kSampleRate       = 48000.0f;
    static constexpr float kMinFreq          =    20.0f;
    static constexpr float lowFreqEmphasisDb =     4.0f;
    static constexpr float temporalSmoothing =    0.28f;
    static constexpr float kGateFadeRangeDb  =    6.0f;   // Übergangsbreite [dB] für weiches Gate-Fade

    // Grid-Frequenzmarkierungen [Hz]
    static constexpr std::array<int, 10> kGridFreqs = {
        20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000
    };

    // -------------------------------------------------------------------------
    // Private Methoden
    // -------------------------------------------------------------------------

    void rebuildLookupTables();

    /** Frequenz [Hz] → Y-Pixelposition. Geteilt von Rendering und Grid. */
    int freqToY(float freq, int height) const;

    /** Schreibt einen neuen Frame als rechte Spalte ins spectrogramImage. */
    void appendFrameColumn(const SpectralFrameBuffer::Frame& frame);
    void updateSelectedObjectOverlayState();
    void rebuildSelectedObjectOverlayFromVisibleColumns();
    bool resolveFrameMaskForTime(const SelectedObjectOverlayData& data,
                                 double timeSec,
                                 std::array<bool, SpectralFrameBuffer::NUM_BINS>& outMask) const;
    void writeSelectedObjectOverlayColumn(int x, double timeSec);

    /** Grid-Linien und Labels über das Spektrogramm zeichnen. */
    void drawGrid(juce::Graphics& g);

    /** Frequenz-Crosshair + Hz-Readout an der Mausposition zeichnen. */
    void drawCursorReadout(juce::Graphics& g);

    /** dB-Wert → Farbe via LUT. */
    [[nodiscard]] juce::Colour magnitudeToColour(float magDb) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralView)
};