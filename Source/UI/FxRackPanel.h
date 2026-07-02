#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/ObjectDatabase.h"
#include <vector>
#include <functional>

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
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    void refresh();

    /** Invoked when the "Add FX" slot is clicked; the editor shows the centered popup. */
    std::function<void()> onAddFxRequested;

private:
    PluginProcessor& processor;

    struct KnobView
    {
        juce::String label;      // Display label
        juce::String fxName;     // Backend FX module name
        juce::String paramName;  // Backend parameter name
        int paramIndexInFx = 0;  // Parameter index within backend FX module
        float value = 0.5f;
    };

    struct ModuleView
    {
        juce::String name;       // Backend FX module name (non-core); "Base" for core
        bool isCore = false;
        juce::Colour accentColour;
        std::vector<KnobView> knobs;
    };

    struct KnobHit
    {
        juce::Rectangle<int> bounds;
        int moduleIndex = -1;
        int knobIndex = -1;
    };

    struct ModuleLayout
    {
        juce::Rectangle<int> cardBounds;
        juce::Rectangle<int> closeButtonBounds;
        int moduleIndex = -1;
        bool isCore = false;
        std::vector<KnobHit> knobs;
    };

    std::vector<ModuleView> modules;
    int scrollOffsetX = 0;

    int activeModuleIndex = -1;
    int activeKnobIndex = -1;
    bool draggingKnob = false;
    float dragStartValue = 0.5f;

    void timerCallback() override;
    void rebuildModules();

    std::vector<ModuleLayout> computeLayouts() const;
    bool isKnobSelected(const KnobView& knob) const;

    void drawKnob(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                  float normValue, juce::Colour accent, bool isSelected) const;
    void drawModuleCard(juce::Graphics& g, const ModuleLayout& layout, const ModuleView& mod) const;
    void drawAddFxButton(juce::Graphics& g, juce::Rectangle<int> area) const;
    juce::Colour colourForFxName(const juce::String& fxName) const;

    static constexpr int moduleWidthCore = 160;
    static constexpr int moduleWidthFx = 160;
    static constexpr int addFxWidth = 90;
    static constexpr int moduleGap = 8;
    static constexpr int knobDiameter = 34;  // matches In/Out gain knob size

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxRackPanel)
};
