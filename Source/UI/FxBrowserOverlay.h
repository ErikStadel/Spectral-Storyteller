#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

/**
 * FxBrowserOverlay: Editor-centered modal FX chooser.
 * Mirrors the "fx-browser-overlay" popup from the HTML mockup.
 * - Dimmed backdrop covering the whole editor
 * - Centered panel with header + scrollable card grid + footer
 * - Click a card to instantiate the effect; ESC or backdrop click closes
 */
class FxBrowserOverlay : public juce::Component
{
public:
    FxBrowserOverlay();
    ~FxBrowserOverlay() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    /** Called with the backend FX name when a card is chosen. */
    std::function<void(const juce::String&)> onEffectChosen;
    /** Called when the overlay wants to close itself. */
    std::function<void()> onClose;

private:
    struct FxCard
    {
        juce::String uiName;
        juce::String fxName;
        juce::String category;
        juce::String description;
        juce::Colour accent;
    };

    std::vector<FxCard> cards;
    int scrollOffsetY = 0;

    juce::Rectangle<int> getPanelBounds() const;
    juce::Rectangle<int> getGridViewport() const;
    int getContentHeight() const;
    std::vector<juce::Rectangle<int>> computeCardBounds() const;

    static constexpr int cardH = 74;
    static constexpr int cardGap = 10;
    static constexpr int gridCols = 3;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBrowserOverlay)
};
