#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/ObjectDatabase.h"
#include <vector>

class PluginProcessor;

class FxRackPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit FxRackPanel(PluginProcessor& processorRef);
    ~FxRackPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& event) override;

    void refresh();

private:
    PluginProcessor& processor;

    struct ModuleView
    {
        juce::String name;
        bool isCore = false;
        juce::Colour accentColour;
        std::vector<std::pair<juce::String, float>> params;
    };

    std::vector<ModuleView> modules;
    int scrollOffsetX = 0;

    void timerCallback() override;
    void rebuildModules();

    void drawKnob(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                  float normValue, juce::Colour accent, bool isSelected) const;
    void drawModuleCard(juce::Graphics& g, juce::Rectangle<int> area, const ModuleView& mod) const;
    void drawAddFxButton(juce::Graphics& g, juce::Rectangle<int> area) const;

    static constexpr int moduleWidthCore = 280;
    static constexpr int moduleWidthFx = 160;
    static constexpr int addFxWidth = 90;
    static constexpr int moduleGap = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxRackPanel)
};
