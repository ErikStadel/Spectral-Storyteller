#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/SpectralView.h"
#include "UI/SpectralSelector.h"
#include "UI/SourceView.h"
#include "UI/ObjectSidebar.h"
#include "UI/StoryTimelineComponent.h"
#include "UI/ModulationPanel.h"
#include "UI/FxRackPanel.h"
#include "UI/FxBrowserOverlay.h"
#include "UI/HardwareLookAndFeel.h"
#include <atomic>
#include <memory>

class PluginProcessor;

/**
 * HudPanel: dünne, halbtransparente Trägerfläche für Controls, die direkt
 * auf dem Spektrogramm schweben (Tool-Buttons, View-Gain-Slider).
 * Ohne diese Fläche wirken die Controls wie lose Fremdkörper über dem
 * bunten, sich bewegenden Spektrogramm-Inhalt. setInterceptsMouseClicks
 * ist bewusst aus, damit die eigentlichen Controls (die als spätere
 * Geschwister-Components darüber liegen) weiterhin klickbar bleiben.
 */
class HudPanel : public juce::Component
{
public:
    HudPanel() { setInterceptsMouseClicks(false, false); }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xFF003838)); // Solid petrol background
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(juce::Colour(0xFF004953)); // Highlight petrol border
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    }
};

/**
 * ToolbarButtonLookAndFeel: vereinheitlicht Font-Skala und Eckenradius der
 * Tool-Buttons (Rect/Brush/Source) mit dem Rest der Oberfläche (Labels
 * durchgängig 9-10pt bold, kleiner Radius wie bei den Label-Boxen im
 * Spektrogramm). Ersetzt JUCEs generisches Default-Button-Chrome, das
 * bisher stilistisch nicht zum neumorphen/dunklen Look passte.
 */
class ToolbarButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return juce::Font(11.0f, juce::Font::bold);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool /*shouldDrawButtonAsHighlighted*/, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        constexpr float corner = 4.0f;

        g.setColour(backgroundColour);
        g.fillRoundedRectangle(bounds, corner);

        g.setColour(juce::Colour(0xFF004953)); // Petrol highlight border
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        if (shouldDrawButtonAsDown)
        {
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillRoundedRectangle(bounds, corner);
        }
    }
};

class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter() { startTimerHz(30); }
    void setSource(std::atomic<float>* peakDb) { sourceDb = peakDb; }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xFF0C0A09));
        g.fillRoundedRectangle(r, 4.0f);

        const float minDb = -60.0f;
        const float maxDb = 6.0f;
        auto norm = [&](float db)
        {
            return juce::jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        };

        const float h = r.getHeight();
        const float lvlY = h - h * norm(currentDb);

        juce::ColourGradient grad(juce::Colour(0xFF6A00A8), 0.0f, r.getBottom(),
                                  juce::Colour(0xFFFFC400), 0.0f, r.getY(), false);
        grad.addColour(0.5, juce::Colour(0xFFFF2A00));
        g.setGradientFill(grad);
        g.fillRect(juce::Rectangle<float>(r.getX() + 1.0f, r.getY() + lvlY,
                                          r.getWidth() - 2.0f, h - lvlY - 1.0f));

        const float pkY = h - h * norm(peakHoldDb);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.fillRect(juce::Rectangle<float>(r.getX() + 1.0f, r.getY() + pkY - 0.5f,
                                          r.getWidth() - 2.0f, 1.0f));
    }

private:
    void timerCallback() override
    {
        if (sourceDb != nullptr)
        {
            const float v = sourceDb->load();
            currentDb = v;
            if (v > peakHoldDb)
            {
                peakHoldDb = v;
                peakHoldCounter = 30;
            }
            else if (--peakHoldCounter <= 0)
            {
                peakHoldDb = juce::jmax(-90.0f, peakHoldDb - 0.8f);
            }
        }

        repaint();
    }

    std::atomic<float>* sourceDb = nullptr;
    float currentDb = -90.0f;
    float peakHoldDb = -90.0f;
    int peakHoldCounter = 0;
};

class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::DragAndDropContainer
{
public:
    PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& processor;

    std::unique_ptr<SpectralView> spectralView;
    std::unique_ptr<SourceView> sourceView;
    std::unique_ptr<SpectralSelector> spectralSelector;
    juce::TextButton rectSelectButton { "Rect" };
    juce::TextButton lassoSelectButton { "Brush" };
    juce::TextButton viewModeButton { "Source" };
    ToolbarButtonLookAndFeel toolbarButtonLookAndFeel;
    HudPanel toolGroupPanel;
    HudPanel viewModePanel;
    HudPanel viewGainPanel;
    std::unique_ptr<ObjectSidebar> objectSidebar;
    std::unique_ptr<StoryTimelineComponent> storyTimeline;
    std::unique_ptr<ModulationPanel> modulationPanel;
    std::unique_ptr<FxRackPanel> fxRackPanel;
    std::unique_ptr<FxBrowserOverlay> fxBrowserOverlay;

    juce::Slider inputGainSlider;
    juce::Slider outputGainSlider;
    juce::Slider dryWetSlider;
    juce::Slider gateSlider;

    juce::Label inputLabel;
    juce::Label outputLabel;
    juce::Label dryWetLabel;
    juce::Label gateLabel;

    LevelMeter inputMeter;
    LevelMeter outputMeter;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetAttachment;

    juce::Label versionLabel;
    juce::TooltipWindow tooltipWindow;
    HardwareLookAndFeel knobLookAndFeel;

    void paintHeaderBar(juce::Graphics& g, juce::Rectangle<int> area);
    void paintMeterStrip(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label);
    void updateViewMode();

    static constexpr int headerHeight = 48;
    static constexpr int sidebarWidth = 320;
    static constexpr int meterStripWidth = 48;
    static constexpr int footerHeight = 240;
    static constexpr int timelineHeight = 112;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};