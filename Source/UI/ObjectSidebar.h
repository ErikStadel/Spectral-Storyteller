#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/ObjectDatabase.h"
#include <memory>

/**
 * ObjectSidebar: UI für Objekt-Verwaltung (Liste mit Solo/Mute).
 * PR3: Zeigt alle Objekte, erlaubt Solo/Mute per Objekt, Delete.
 */
class ObjectSidebar : public juce::Component, private juce::Timer
{
public:
    explicit ObjectSidebar(ObjectDatabase& database, std::function<void(bool)> onAutoDetect = {});
    ~ObjectSidebar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Update display after changes to ObjectDatabase
    void refresh();

private:
    ObjectDatabase& database;
    std::function<void(bool)> onAutoDetectClicked;

    // One button row per object
    struct ObjectRow
    {
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> deleteButton;
        juce::String name;
    };

    std::vector<ObjectRow> rows;
    std::unique_ptr<juce::TextButton> autoDetectButton;
    uint64_t lastKnownRevision = 0;

    static constexpr int BUTTON_WIDTH = 30;
    static constexpr int BUTTON_HEIGHT = 24;
    static constexpr int ROW_HEIGHT = 28;
    static constexpr int PADDING = 4;

    void timerCallback() override;
    void rebuildRows();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectSidebar)
};
