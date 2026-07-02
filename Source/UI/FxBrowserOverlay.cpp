#include "FxBrowserOverlay.h"

FxBrowserOverlay::FxBrowserOverlay()
{
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);

    cards = {
        { "Space Blur",   "SpaceBlur",  "Space & Depth",     "Atmospharischer Spektral-Reverb fur dichte Klangwolken.",  juce::Colour(0xFF0EA5E9) },
        { "Echo Bleed",   "Delay",      "Time-Domain",       "Flussiges Tape-Echo, das Frequenzen sanft verschmiert.",   juce::Colour(0xFF10B981) },
        { "Shade Contour","Filter",     "Tone Filter",       "Warmer, analog-modellierter Filter zur Frequenz-Formung.", juce::Colour(0xFFF59E0B) },
        { "Mass Forge",   "Compressor", "Dynamics",          "Adaptiver Dynamik-Compander mit physischem Gewicht.",      juce::Colour(0xFF3B82F6) },
        { "Heat Glow",    "Saturation", "Harmonics",         "Sattigungs-Schmiede fur Rohrenwarme und Oberton-Glow.",    juce::Colour(0xFFF97316) },
        { "Grit Edge",    "Distortion", "Destruction",       "Aggressives Hard-Clipping und Wavefolding-Zerstorung.",    juce::Colour(0xFFF43F5E) },
        { "Prism Focus",  "Contrast",   "Spectral Dynamics", "Regelt den spektralen Kontrast uber die Frequenzbins.",    juce::Colour(0xFFD946EF) },
        { "Stasis Cloud", "Freeze",     "Spectral Freeze",   "Friert das Spektrum ein. Erzeugt unendliche Flachen.",     juce::Colour(0xFF06B6D4) },
        { "Fluid Grain",  "Perlin",     "Noise Gen",         "Mathematisch fliessendes Perlin-Rauschen.",                juce::Colour(0xFF84CC16) },
        { "Chaos Float",  "Brownian",   "Noise Gen",         "Unberechenbare Brown'sche Molekularbewegung.",             juce::Colour(0xFF8B5CF6) }
    };
}

juce::Rectangle<int> FxBrowserOverlay::getPanelBounds() const
{
    const int w = juce::jmin(760, getWidth() - 40);
    const int h = juce::jmin(560, getHeight() - 40);
    return juce::Rectangle<int>((getWidth() - w) / 2, (getHeight() - h) / 2, w, h);
}

juce::Rectangle<int> FxBrowserOverlay::getGridViewport() const
{
    auto panel = getPanelBounds();
    panel.removeFromTop(40);   // header
    panel.removeFromBottom(24); // footer
    return panel.reduced(14, 12);
}

std::vector<juce::Rectangle<int>> FxBrowserOverlay::computeCardBounds() const
{
    std::vector<juce::Rectangle<int>> bounds;
    auto grid = getGridViewport();
    const int cardW = (grid.getWidth() - cardGap * (gridCols - 1)) / gridCols;

    for (int i = 0; i < static_cast<int>(cards.size()); ++i)
    {
        const int row = i / gridCols;
        const int col = i % gridCols;
        juce::Rectangle<int> card(
            grid.getX() + col * (cardW + cardGap),
            grid.getY() + row * (cardH + cardGap) - scrollOffsetY,
            cardW,
            cardH);
        bounds.push_back(card);
    }

    return bounds;
}

int FxBrowserOverlay::getContentHeight() const
{
    const int rows = (static_cast<int>(cards.size()) + gridCols - 1) / gridCols;
    return rows * cardH + (rows - 1) * cardGap;
}

void FxBrowserOverlay::paint(juce::Graphics& g)
{
    // Dimmed backdrop
    g.fillAll(juce::Colour(0xD909090B));

    auto panel = getPanelBounds();
    g.setColour(juce::Colour(0xFF18181B));
    g.fillRoundedRectangle(panel.toFloat(), 12.0f);
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawRoundedRectangle(panel.toFloat(), 12.0f, 1.0f);

    // Header
    auto header = panel.removeFromTop(40);
    g.setColour(juce::Colour(0xE527272A));
    g.fillRoundedRectangle(header.toFloat().withTrimmedBottom(-6.0f), 12.0f);
    g.setColour(juce::Colour(0xFF71717A));
    g.setFont(juce::Font(10.0f));
    g.drawText("ROUTING", header.withTrimmedLeft(16).removeFromLeft(70), juce::Justification::centredLeft, false);
    g.setColour(juce::Colour(0xFFE4E4E7));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("LOAD SPECTRAL FX PROCESSOR", header.withTrimmedLeft(84), juce::Justification::centredLeft, false);
    g.setColour(juce::Colour(0xFF71717A));
    g.setFont(juce::Font(10.0f));
    g.drawText("[ESC] CLOSE", header.withTrimmedRight(14), juce::Justification::centredRight, false);

    // Footer
    auto footer = panel.removeFromBottom(24);
    g.setColour(juce::Colour(0xFF09090B));
    g.fillRect(footer);
    g.setColour(juce::Colour(0xFF52525B));
    g.setFont(juce::Font(9.0f));
    g.drawText("Tip: Click an effect card to add it to the selected object's rack.",
               footer.withTrimmedLeft(14), juce::Justification::centredLeft, false);
    g.drawText("v" + juce::String(1) + ".0", footer.withTrimmedRight(14), juce::Justification::centredRight, false);

    // Clip card grid to viewport for scroll
    auto viewport = getGridViewport();
    g.saveState();
    g.reduceClipRegion(viewport);

    const auto bounds = computeCardBounds();
    for (int i = 0; i < static_cast<int>(cards.size()); ++i)
    {
        const auto& card = bounds[static_cast<size_t>(i)];
        if (card.getBottom() < viewport.getY() || card.getY() > viewport.getBottom())
            continue;

        const auto& c = cards[static_cast<size_t>(i)];
        g.setColour(juce::Colour(0xCC0A0A0A));
        g.fillRoundedRectangle(card.toFloat(), 8.0f);
        g.setColour(juce::Colour(0xFF3F3F46));
        g.drawRoundedRectangle(card.toFloat(), 8.0f, 1.0f);

        auto inner = card.reduced(10, 8);
        auto titleRow = inner.removeFromTop(18);
        g.setColour(juce::Colour(0xFFE4E4E7));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(c.uiName, titleRow.withTrimmedRight(12), juce::Justification::centredLeft, false);
        g.setColour(c.accent);
        g.fillEllipse(static_cast<float>(titleRow.getRight() - 8), static_cast<float>(titleRow.getY() + 4), 6.0f, 6.0f);

        g.setColour(juce::Colour(0xFF71717A));
        g.setFont(juce::Font(8.5f));
        g.drawText(c.category.toUpperCase(), inner.removeFromTop(12), juce::Justification::centredLeft, false);

        g.setColour(juce::Colour(0xFFA1A1AA));
        g.setFont(juce::Font(9.5f));
        g.drawFittedText(c.description, inner, juce::Justification::topLeft, 3);
    }

    g.restoreState();
}

void FxBrowserOverlay::resized()
{
    scrollOffsetY = 0;
}

void FxBrowserOverlay::mouseDown(const juce::MouseEvent& event)
{
    if (!getPanelBounds().contains(event.getPosition()))
    {
        if (onClose)
            onClose();
        return;
    }

    // Close button area in header
    auto header = getPanelBounds().removeFromTop(40);
    if (header.withTrimmedRight(14).removeFromRight(90).contains(event.getPosition()))
    {
        if (onClose)
            onClose();
        return;
    }

    auto viewport = getGridViewport();
    if (!viewport.contains(event.getPosition()))
        return;

    const auto bounds = computeCardBounds();
    for (int i = 0; i < static_cast<int>(cards.size()); ++i)
    {
        if (bounds[static_cast<size_t>(i)].contains(event.getPosition()))
        {
            if (onEffectChosen)
                onEffectChosen(cards[static_cast<size_t>(i)].fxName);
            if (onClose)
                onClose();
            return;
        }
    }
}

void FxBrowserOverlay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int viewportH = getGridViewport().getHeight();
    const int maxScroll = juce::jmax(0, getContentHeight() - viewportH);
    scrollOffsetY = juce::jlimit(0, maxScroll, scrollOffsetY - static_cast<int>(wheel.deltaY * 60.0f));
    repaint();
}

bool FxBrowserOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }

    return false;
}
