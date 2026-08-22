#pragma once
#include <juce_graphics/juce_graphics.h>
#include "Fonts.h"

namespace Typography
{
    inline juce::Typeface::Ptr getRegularTypeface()
    {
        static auto typeface = juce::Typeface::createSystemTypefaceFor(Fonts::InterRegular_ttf, Fonts::InterRegular_ttfSize);
        return typeface;
    }

    inline juce::Typeface::Ptr getMediumTypeface()
    {
        static auto typeface = juce::Typeface::createSystemTypefaceFor(Fonts::InterMedium_ttf, Fonts::InterMedium_ttfSize);
        return typeface;
    }

    inline juce::Typeface::Ptr getSemiBoldTypeface()
    {
        static auto typeface = juce::Typeface::createSystemTypefaceFor(Fonts::InterSemiBold_ttf, Fonts::InterSemiBold_ttfSize);
        return typeface;
    }

    inline juce::Typeface::Ptr getBoldTypeface()
    {
        static auto typeface = juce::Typeface::createSystemTypefaceFor(Fonts::InterBold_ttf, Fonts::InterBold_ttfSize);
        return typeface;
    }

    // Type scale (High readability, clear weights)
    inline juce::Font getTitleFont()
    {
        juce::Font f(getBoldTypeface());
        f.setHeight(16.0f);
        return f;
    }

    inline juce::Font getHeaderFont()
    {
        juce::Font f(getBoldTypeface());
        f.setHeight(14.0f);
        return f;
    }

    inline juce::Font getLabelFont(bool bold = false)
    {
        juce::Font f(bold ? getBoldTypeface() : getSemiBoldTypeface());
        f.setHeight(13.0f);
        return f;
    }

    inline juce::Font getValueFont()
    {
        juce::Font f(getMediumTypeface());
        f.setHeight(12.0f);
        return f;
    }

    inline juce::Font getMicroFont(bool bold = false)
    {
        juce::Font f(bold ? getBoldTypeface() : getSemiBoldTypeface());
        f.setHeight(11.0f);
        return f;
    }
}
