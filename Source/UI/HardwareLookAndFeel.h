#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Typography.h"
#include <cmath>

/**
 * Advanced Hardware LookAndFeel for a dark monochrome aesthetic.
 */
class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel() {}

    // Helper to draw a raised hardware panel
    static void drawHardwarePanel(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
    {
        // Main faceplate background
        g.setColour(juce::Colour(0xFF18181A));
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Top-left highlight
        g.setColour(juce::Colour(0xFF2A2A2E));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
        
        // Bottom-right shadow
        g.setColour(juce::Colour(0xFF0A0A0C));
        g.drawRoundedRectangle(bounds.translated(0.0f, 1.0f).reduced(0.5f), cornerRadius, 1.0f);
    }

    // Helper to draw an inset screen or well
    static void drawHardwareInset(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
    {
        // Inset background
        g.setColour(juce::Colour(0xFF0A0A0B));
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Inner shadow / Bevel
        g.setColour(juce::Colour(0xFF000000));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.5f);
        
        // Bottom highlight for the cut
        g.setColour(juce::Colour(0xFF222225));
        g.drawRoundedRectangle(bounds.translated(0.0f, 1.0f).reduced(0.5f), cornerRadius, 1.0f);
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        juce::ignoreUnused(slider);

        auto r = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                        static_cast<float>(width), static_cast<float>(height)).reduced(2.0f);
        const float radius = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre();

        // 1. Drop shadow (fake it with a dark offset ellipse)
        g.setColour(juce::Colour(0x99000000));
        g.fillEllipse(r.translated(0.0f, 2.0f));

        // 2. Base metallic ring (outer rim)
        juce::ColourGradient rimGrad(juce::Colour(0xFF333336), c.x, r.getY(),
                                     juce::Colour(0xFF0A0A0B), c.x, r.getBottom(), false);
        g.setGradientFill(rimGrad);
        g.fillEllipse(r);

        // 3. Inner knob body (conical or radial gradient)
        auto innerR = r.reduced(2.0f);
        juce::ColourGradient bodyGrad(juce::Colour(0xFF262629), c.x, innerR.getY(),
                                      juce::Colour(0xFF141416), c.x, innerR.getBottom(), false);
        g.setGradientFill(bodyGrad);
        g.fillEllipse(innerR);

        // 4. Subtle top highlight on the knob face
        g.setColour(juce::Colour(0x33FFFFFF));
        g.drawEllipse(innerR.reduced(1.0f), 1.0f);

        // 5. Pointer
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float len = radius * 0.6f;
        const float x1 = c.x + std::cos(angle - juce::MathConstants<float>::halfPi) * (radius - len);
        const float y1 = c.y + std::sin(angle - juce::MathConstants<float>::halfPi) * (radius - len);
        const float x2 = c.x + std::cos(angle - juce::MathConstants<float>::halfPi) * (radius - 2.5f);
        const float y2 = c.y + std::sin(angle - juce::MathConstants<float>::halfPi) * (radius - 2.5f);

        // Draw a small inset groove for the pointer
        g.setColour(juce::Colour(0xFF000000));
        g.drawLine(x1, y1+1.0f, x2, y2+1.0f, 3.0f);
        
        // Draw the white indicator line
        g.setColour(juce::Colour(0xFFFFFFFF));
        g.drawLine(x1, y1, x2, y2, 2.0f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool isMouseOverButton, bool isButtonDown) override
    {
        juce::ignoreUnused(backgroundColour, isMouseOverButton);
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        constexpr float corner = 3.0f;
        
        const bool isToggled = button.getToggleState();
        const bool isPressed = isButtonDown || isToggled;

        if (isPressed)
        {
            // Pressed / Toggled state: Inset
            g.setColour(juce::Colour(0xFF101012));
            g.fillRoundedRectangle(bounds, corner);
            
            // Inner dark shadow
            g.setColour(juce::Colour(0xFF000000));
            g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.5f);
            
            if (isToggled)
            {
                // Subtle bright inner rim instead of a bottom LED
                g.setColour(juce::Colour(0x33FFFFFF));
                g.drawRoundedRectangle(bounds.reduced(1.5f), corner - 1.0f, 1.0f);
            }
        }
        else
        {
            // Raised hardware state
            juce::ColourGradient bgGrad(juce::Colour(0xFF2C2C2F), bounds.getX(), bounds.getY(),
                                        juce::Colour(0xFF1E1E20), bounds.getX(), bounds.getBottom(), false);
            g.setGradientFill(bgGrad);
            g.fillRoundedRectangle(bounds, corner);
            
            // Highlight top edge
            g.setColour(juce::Colour(0xFF3F3F42));
            g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);
            
            // Shadow bottom edge
            g.setColour(juce::Colour(0xFF0A0A0B));
            g.drawRoundedRectangle(bounds.translated(0.0f, 1.0f).reduced(0.5f), corner, 1.0f);
        }
    }
    
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override
    {
        if (font.isBold() || font.getTypefaceStyle().equalsIgnoreCase("Bold"))
            return Typography::getBoldTypeface();
        if (font.getTypefaceStyle().equalsIgnoreCase("SemiBold"))
            return Typography::getSemiBoldTypeface();
        if (font.getTypefaceStyle().equalsIgnoreCase("Medium"))
            return Typography::getMediumTypeface();
            
        return Typography::getMediumTypeface();
    }
    
    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return Typography::getHeaderFont();
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        juce::ignoreUnused(minSliderPos, maxSliderPos, style);
        
        const bool isHorizontal = slider.isHorizontal();
        auto track = slider.getLocalBounds().toFloat();
        
        // Draw track inset
        juce::Rectangle<float> slot = isHorizontal 
            ? track.withSizeKeepingCentre(track.getWidth() - 8.0f, 6.0f)
            : track.withSizeKeepingCentre(6.0f, track.getHeight() - 8.0f);
            
        drawHardwareInset(g, slot, 3.0f);
        
        // Draw thumb
        juce::Rectangle<float> thumb;
        if (isHorizontal)
            thumb = juce::Rectangle<float>(sliderPos - 6.0f, slot.getCentreY() - 10.0f, 12.0f, 20.0f);
        else
            thumb = juce::Rectangle<float>(slot.getCentreX() - 10.0f, sliderPos - 6.0f, 20.0f, 12.0f);
            
        // Thumb background
        juce::ColourGradient thumbGrad(juce::Colour(0xFF2C2C2F), thumb.getX(), thumb.getY(),
                                       juce::Colour(0xFF1E1E20), thumb.getX(), thumb.getBottom(), false);
        g.setGradientFill(thumbGrad);
        g.fillRoundedRectangle(thumb, 2.0f);
        
        g.setColour(juce::Colour(0xFF000000));
        g.drawRoundedRectangle(thumb, 2.0f, 1.0f);
        
        // Indicator line
        g.setColour(juce::Colour(0xFFFFFFFF));
        if (isHorizontal)
            g.fillRect(thumb.getCentreX() - 1.0f, thumb.getY() + 3.0f, 2.0f, thumb.getHeight() - 6.0f);
        else
            g.fillRect(thumb.getX() + 3.0f, thumb.getCentreY() - 1.0f, thumb.getWidth() - 6.0f, 2.0f);
    }
};
