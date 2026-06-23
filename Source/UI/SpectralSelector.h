#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <array>

/**
 * SpectralSelector: Overlay-Component für Rechteck-/Lasso-Auswahl auf Spektrogramm.
 * PR3: Einfache Rechteck-Auswahl (später: Lasso/Freehand)
 *
 * Nutzer zeichnet Rechteck auf dem Spektrogramm → gibt Bin-Range zurück.
 */
class SpectralSelector : public juce::Component
{
public:
    static constexpr int NUM_BINS = (1 << 11) / 2 + 1;  // 1025

    explicit SpectralSelector();
    ~SpectralSelector() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    // -------------------------------------------------------------------------
    // Selection API
    // -------------------------------------------------------------------------

    /**
     * Get the selected bin range [minBin, maxBin] from last completed selection.
     */
    void getSelectedBinRange(int& minBin, int& maxBin) const;

    /**
     * Get the selected frequency range [minFreq, maxFreq] in Hz.
     * Requires freqToY function (pass as callback).
     */
    void getSelectedFreqRange(float& minFreq, float& maxFreq, int height) const;

    /**
     * Set callback when selection is completed (MouseUp).
     */
    void setOnSelectionComplete(std::function<void(int minBin, int maxBin)> callback)
    {
        onSelectionComplete = callback;
    }

    void setOnSelectionStarted(std::function<void()> callback)
    {
        onSelectionStarted = callback;
    }

    void setOnSelectionFinished(std::function<void()> callback)
    {
        onSelectionFinished = callback;
    }

    void setYToBinMapper(std::function<int(int y, int height)> mapper)
    {
        yToBinMapper = mapper;
    }

    /**
     * Clear the current selection.
     */
    void clearSelection();

    /**
     * Check if a selection is currently being made.
     */
    bool isSelecting() const { return isActive; }

private:
    // Selection geometry
    int selectStartX = 0;
    int selectStartY = 0;
    int selectEndX = 0;
    int selectEndY = 0;
    
    bool isActive = false;

    // Result of last completed selection
    int selectedMinBin = 0;
    int selectedMaxBin = 0;
    int selectedMinY = 0;
    int selectedMaxY = 0;

    std::function<void(int, int)> onSelectionComplete;
    std::function<void()> onSelectionStarted;
    std::function<void()> onSelectionFinished;
    std::function<int(int, int)> yToBinMapper;

    /**
     * Convert Y-pixel to bin index (reverse of SpectralView::freqToY).
     * Simple linear approximation: y maps to bins based on height.
     */
    int yPixelToBin(int y, int height) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralSelector)
};
