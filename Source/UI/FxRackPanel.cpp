#include "FxRackPanel.h"
#include "../PluginProcessor.h"

FxRackPanel::FxRackPanel(PluginProcessor& p)
    : processor(p)
{
    setOpaque(false);
    startTimerHz(10);
    rebuildModules();
}

void FxRackPanel::timerCallback()
{
    rebuildModules();
}

void FxRackPanel::refresh()
{
    rebuildModules();
}

void FxRackPanel::rebuildModules()
{
    modules.clear();

    const int objId = processor.getSelectedObjectId();
    if (objId <= 0)
        return;

    const auto chain = processor.getFxChainForObject(objId);
    const double t = processor.getTransportSeconds();

    static const juce::Colour kCoreAccent(0xFFA78BFA);

    static const std::array<juce::Colour, 6> fxColours = {
        juce::Colour(0xFF34D399),
        juce::Colour(0xFF38BDF8),
        juce::Colour(0xFFFB923C),
        juce::Colour(0xFFF472B6),
        juce::Colour(0xFFA78BFA),
        juce::Colour(0xFF4ADE80)
    };

    int colourIdx = 0;

    for (const auto& fx : chain)
    {
        if (!fx.enabled)
            continue;

        ModuleView mod;
        mod.name = fx.name;

        const auto* db = processor.getObjectDatabase();
        const bool isBase = juce::String(fx.name).equalsIgnoreCase("Base");
        mod.isCore = isBase;
        mod.accentColour = isBase ? kCoreAccent : fxColours[static_cast<size_t>(colourIdx % static_cast<int>(fxColours.size()))];

        if (!isBase)
            ++colourIdx;

        for (const auto& param : fx.parameters)
        {
            float val = 0.5f;
            if (db)
                val = db->getInterpolatedAutomationValue(objId, fx.name, param.name, t, 0.5f);

            mod.params.push_back({ juce::String(param.name), val });
        }

        modules.push_back(std::move(mod));
    }

    repaint();
}

void FxRackPanel::drawKnob(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                           float normValue, juce::Colour accent, bool isSelected) const
{
    const float centreX = area.getCentreX();
    const float labelH = 14.0f;
    const float knobSize = juce::jmin(static_cast<float>(area.getWidth()) - 4.0f, static_cast<float>(area.getHeight()) - labelH - 4.0f);
    const float knobRadius = knobSize * 0.5f;
    const float centreY = area.getY() + (area.getHeight() - labelH) * 0.5f;

    const auto slotRect = juce::Rectangle<float>(centreX - knobRadius - 2.0f, centreY - knobRadius - 2.0f,
                                                  knobRadius * 2.0f + 4.0f, knobRadius * 2.0f + 4.0f);
    g.setColour(juce::Colour(0xFF0C0A09));
    g.fillEllipse(slotRect);

    const auto knobRect = juce::Rectangle<float>(centreX - knobRadius, centreY - knobRadius,
                                                  knobRadius * 2.0f, knobRadius * 2.0f);

    juce::ColourGradient grad(juce::Colour(0xFF52525B), knobRect.getX(), knobRect.getY(),
                              juce::Colour(0xFF27272A), knobRect.getRight(), knobRect.getBottom(), true);
    g.setGradientFill(grad);
    g.fillEllipse(knobRect);

    g.setColour(isSelected ? accent : juce::Colour(0xFF57534E));
    g.drawEllipse(knobRect, 1.2f);

    const float startAngle = juce::MathConstants<float>::pi * 0.75f;
    const float endAngle = juce::MathConstants<float>::pi * 2.25f;
    const float angle = startAngle + normValue * (endAngle - startAngle);

    const float indicatorLen = knobRadius * 0.55f;
    const float ix = centreX + std::sin(angle) * (knobRadius - 3.0f - indicatorLen);
    const float iy = centreY - std::cos(angle) * (knobRadius - 3.0f - indicatorLen);
    const float ix2 = centreX + std::sin(angle) * (knobRadius - 3.0f);
    const float iy2 = centreY - std::cos(angle) * (knobRadius - 3.0f);

    g.setColour(isSelected ? accent.brighter(0.3f) : juce::Colour(0xFFD6D3D1));
    g.drawLine(ix, iy, ix2, iy2, 2.0f);

    g.setColour(isSelected ? accent : juce::Colour(0xFFD6D3D1));
    g.setFont(juce::Font(9.0f, isSelected ? juce::Font::bold : juce::Font::plain));
    g.drawText(label, juce::Rectangle<float>(static_cast<float>(area.getX()), static_cast<float>(area.getBottom()) - labelH,
               static_cast<float>(area.getWidth()), labelH),
               juce::Justification::centredTop, false);
}

void FxRackPanel::drawModuleCard(juce::Graphics& g, juce::Rectangle<int> area, const ModuleView& mod) const
{
    g.setColour(juce::Colour(0xCC292524));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xFF44403C));
    g.drawRoundedRectangle(area.toFloat(), 6.0f, 1.0f);

    if (!mod.isCore)
    {
        g.setColour(mod.accentColour);
        g.fillRect(juce::Rectangle<float>(static_cast<float>(area.getX()), static_cast<float>(area.getY()),
                                           2.0f, static_cast<float>(area.getHeight())));
    }

    auto headerArea = area.removeFromTop(22);
    g.setColour(mod.isCore ? juce::Colour(0xE644403C) : mod.accentColour.withAlpha(0.15f));
    g.fillRoundedRectangle(headerArea.toFloat().withTrimmedBottom(-3.0f), 6.0f);
    g.fillRect(headerArea.toFloat().withTrimmedTop(3.0f));

    g.setColour(juce::Colour(0xFF44403C).withAlpha(0.5f));
    g.drawHorizontalLine(headerArea.getBottom(), static_cast<float>(headerArea.getX()), static_cast<float>(headerArea.getRight()));

    const float dotX = static_cast<float>(headerArea.getX()) + 8.0f;
    const float dotY = static_cast<float>(headerArea.getCentreY());
    g.setColour(mod.accentColour);
    g.fillEllipse(dotX - 2.5f, dotY - 2.5f, 5.0f, 5.0f);

    g.setColour(mod.isCore ? juce::Colour(0xFFE7E5E3) : mod.accentColour);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    juce::String displayName = mod.name.toUpperCase();
    if (mod.isCore)
        displayName = "BASE PARAMETERS";
    g.drawText(displayName, headerArea.withTrimmedLeft(18).withTrimmedRight(4),
               juce::Justification::centredLeft, true);

    if (mod.isCore)
    {
        g.setColour(juce::Colour(0xFF78716C));
        g.setFont(juce::Font(8.0f));
        g.drawText("CORE", headerArea.withTrimmedRight(6), juce::Justification::centredRight, false);
    }

    auto knobArea = area.reduced(4, 2);
    const int numParams = static_cast<int>(mod.params.size());
    if (numParams == 0)
        return;

    const int cols = mod.isCore ? 4 : juce::jmin(numParams, 3);
    const int knobW = knobArea.getWidth() / juce::jmax(1, cols);

    for (int i = 0; i < numParams && i < cols; ++i)
    {
        auto kArea = knobArea.removeFromLeft(knobW);
        const bool isSel = (i == 0 && mod.isCore);
        drawKnob(g, kArea, mod.params[static_cast<size_t>(i)].first,
                 mod.params[static_cast<size_t>(i)].second, mod.accentColour, isSel);
    }
}

void FxRackPanel::drawAddFxButton(juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour(juce::Colour(0x4D292524));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);

    auto dashed = area.toFloat().reduced(4.0f);
    g.setColour(juce::Colour(0xFF44403C));
    g.drawRoundedRectangle(dashed, 6.0f, 1.0f);

    g.setColour(juce::Colour(0xFF78716C));
    g.setFont(juce::Font(22.0f));
    g.drawText("+", area.withTrimmedBottom(16), juce::Justification::centredBottom, false);

    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText("ADD FX", area.withTrimmedTop(area.getHeight() / 2 + 4), juce::Justification::centredTop, false);
}

void FxRackPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1C1917));

    auto area = getLocalBounds().reduced(4);
    area.translate(-scrollOffsetX, 0);

    for (const auto& mod : modules)
    {
        const int w = mod.isCore ? moduleWidthCore : moduleWidthFx;
        auto modArea = area.removeFromLeft(w);
        area.removeFromLeft(moduleGap);

        if (modArea.getRight() + scrollOffsetX >= 0 && modArea.getX() + scrollOffsetX < getWidth())
            drawModuleCard(g, modArea.translated(scrollOffsetX, 0).withY(modArea.getY()).withHeight(modArea.getHeight()), mod);
    }

    auto addArea = area.removeFromLeft(addFxWidth);
    if (addArea.getRight() + scrollOffsetX >= 0 && addArea.getX() + scrollOffsetX < getWidth())
        drawAddFxButton(g, addArea.translated(scrollOffsetX, 0).withY(addArea.getY()).withHeight(addArea.getHeight()));
}

void FxRackPanel::resized()
{
    scrollOffsetX = 0;
}

void FxRackPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    int totalWidth = 0;
    for (const auto& mod : modules)
        totalWidth += (mod.isCore ? moduleWidthCore : moduleWidthFx) + moduleGap;
    totalWidth += addFxWidth;

    const int maxScroll = juce::jmax(0, totalWidth - getWidth() + 16);
    scrollOffsetX = juce::jlimit(0, maxScroll, scrollOffsetX - static_cast<int>(wheel.deltaX * 80.0f + wheel.deltaY * 80.0f));
    repaint();
}

void FxRackPanel::mouseDown(const juce::MouseEvent& event)
{
    int x = event.x + scrollOffsetX - 4;
    for (const auto& mod : modules)
    {
        const int w = mod.isCore ? moduleWidthCore : moduleWidthFx;
        x -= w + moduleGap;
    }

    if (x >= 0 && x < addFxWidth)
    {
        const int objId = processor.getSelectedObjectId();
        if (objId > 0)
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Delay");
            menu.addItem(2, "Filter");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                               [this, objId](int result)
                               {
                                   if (result == 1)
                                       processor.addOrEnableObjectFx(objId, "Delay");
                                   else if (result == 2)
                                       processor.addOrEnableObjectFx(objId, "Filter");
                                   rebuildModules();
                               });
        }
    }
}
