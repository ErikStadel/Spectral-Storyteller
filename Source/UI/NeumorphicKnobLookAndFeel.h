#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

/**
 * Shared neumorphic rotary knob look, matching the mockup's soft dark knobs.
 * Center-detent mapping is provided by the host slider's rotary angles.
 */
class NeumorphicKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit NeumorphicKnobLookAndFeel(juce::Colour pointerColour = juce::Colour(0xFFFF2A00))
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

        g.setColour(juce::Colour(0xFF09090B));
        g.fillEllipse(r);

        juce::ColourGradient grad(juce::Colour(0xFF52525B), r.getX(), r.getY(),
                                  juce::Colour(0xFF27272A), r.getRight(), r.getBottom(), true);
        g.setGradientFill(grad);
        g.fillEllipse(r.reduced(1.0f));

        g.setColour(juce::Colours::white.withAlpha(0.16f));
        g.drawEllipse(r.reduced(1.0f), 1.0f);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawEllipse(r, 1.4f);

        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float len = radius * 0.55f;
        const float x1 = c.x + std::cos(angle - juce::MathConstants<float>::halfPi) * (radius - len - 2.0f);
        const float y1 = c.y + std::sin(angle - juce::MathConstants<float>::halfPi) * (radius - len - 2.0f);
        const float x2 = c.x + std::cos(angle - juce::MathConstants<float>::halfPi) * (radius - 3.0f);
        const float y2 = c.y + std::sin(angle - juce::MathConstants<float>::halfPi) * (radius - 3.0f);

        g.setColour(accent);
        g.drawLine(x1, y1, x2, y2, 2.0f);
    }

private:
    juce::Colour accent;
};
