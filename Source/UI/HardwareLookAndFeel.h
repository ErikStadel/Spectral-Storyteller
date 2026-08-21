#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

/**
 * Hardware rotary knob look, monochrome petrol aesthetic.
 */
class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit HardwareLookAndFeel(juce::Colour pointerColour = juce::Colour(0xFF00A0A0))
        : accent(pointerColour) {}

    void setAccentColour(juce::Colour c) { accent = c; }

    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        juce::ignoreUnused(slider);

        auto r = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                        static_cast<float>(width), static_cast<float>(height)).reduced(1.5f);
        const float radius = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre();

        // Solid hardware base (Dark Petrol border/shadow)
        g.setColour(juce::Colour(0xFF002828));
        g.fillEllipse(r);

        // Main knob body (Slightly lighter Petrol)
        g.setColour(juce::Colour(0xFF003838));
        g.fillEllipse(r.reduced(1.0f));

        // Subtle highlight rim for hardware feel (Highlight Petrol)
        g.setColour(juce::Colour(0xFF004953));
        g.drawEllipse(r.reduced(1.0f), 1.5f);

        // Pointer
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float len = radius * 0.55f;
        const float x1 = c.x + std::cos(angle - juce::MathConstants<float>::halfPi) * (radius - len - 1.0f);
        const float y1 = c.y + std::sin(angle - juce::MathConstants<float>::halfPi) * (radius - len - 1.0f);
        const float x2 = c.x + std::cos(angle - juce::MathConstants<float>::halfPi) * (radius - 2.0f);
        const float y2 = c.y + std::sin(angle - juce::MathConstants<float>::halfPi) * (radius - 2.0f);

        g.setColour(accent);
        g.drawLine(x1, y1, x2, y2, 2.5f); // Thicker, sharper line for hardware look
    }

private:
    juce::Colour accent;
};
