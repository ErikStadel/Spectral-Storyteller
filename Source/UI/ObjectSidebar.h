#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/ObjectDatabase.h"
#include <memory>

/**
 * ObjectSidebar: UI für Objekt-Verwaltung (Liste mit Solo/Mute).
 * PR3: Zeigt alle Objekte, erlaubt Solo/Mute per Objekt, Delete.
 */
class ObjectSidebar : public juce::Component,
                      private juce::Timer,
                      private juce::ScrollBar::Listener
{
public:
    explicit ObjectSidebar(ObjectDatabase& database,
                           std::function<void(bool)> onAutoDetect = {},
                           std::function<juce::String()> autoDetectStatusProvider = {},
                           std::function<void(int)> onSelectedObjectChanged = {},
                           std::function<void(const juce::String&, const juce::File&)> onCreateTransformObject = {},
                           std::function<int()> onCreateTransientObject = {},
                           std::function<float()> getTransientThresholdDb = {},
                           std::function<void(float)> setTransientThresholdDb = {});
    ~ObjectSidebar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // Update display after changes to ObjectDatabase
    void refresh();

private:
    ObjectDatabase& database;
    std::function<void(bool)> onAutoDetectClicked;
    std::function<juce::String()> autoDetectStatusProvider;
    std::function<void(int)> onSelectedObjectChanged;
    std::function<void(const juce::String&, const juce::File&)> onCreateTransformObject;
    std::function<int()> onCreateTransientObject;
    std::function<float()> getTransientThresholdDb;
    std::function<void(float)> setTransientThresholdDb;

    // One button row per object
    struct ObjectRow
    {
        std::unique_ptr<juce::TextButton> recordButton;
        std::unique_ptr<juce::TextButton> engageButton;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::Button> fxButton;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::TextButton> muteButton;
        int objectId = -1;
        juce::String name;
    };

    std::vector<ObjectRow> rows;
    std::unique_ptr<juce::TextButton> autoDetectButton;
    std::unique_ptr<juce::TextButton> transformButton;
    std::unique_ptr<juce::FileChooser> transformFileChooser;
    std::unique_ptr<juce::Component> fxOverlay;
    uint64_t lastKnownRevision = 0;
    int selectedRow = -1;
    int dragStartRow = -1;
    int dragHoverRow = -1;
    int rowScrollOffset = 0;
    juce::ScrollBar rowScrollBar { false };

    static constexpr int HEADER_BUTTON_HEIGHT = 26;
    static constexpr int ROW_HEIGHT = 52;
    static constexpr int RIGHT_BUTTON_W = 20;
    static constexpr int RIGHT_BUTTON_H = 13;
    static constexpr int FX_BUTTON_W = RIGHT_BUTTON_W;
    static constexpr int LEFT_TOGGLE_W = 38;
    static constexpr int LEFT_TOGGLE_H = 18;
    static constexpr int PADDING = 4;

    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;
    void rebuildRows();
    void showTransformMenu();
    void showFxOverlay(int objectId);
    void hideFxOverlay();
    int getMaxVisibleRows() const;
    int getMaxRowScrollOffset() const;
    juce::Rectangle<int> getRowsArea(bool includeScrollBarSpace) const;
    void updateScrollBar();
    int rowFromPoint(juce::Point<int> p) const;
    int rowFromMouseEvent(const juce::MouseEvent& e) const;
    int rowFromComponent(const juce::Component* c) const;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectSidebar)
};
