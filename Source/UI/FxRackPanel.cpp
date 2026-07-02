#include "FxRackPanel.h"
#include "../PluginProcessor.h"

FxRackPanel::FxRackPanel(PluginProcessor& p)
    : processor(p)
{
    setOpaque(false);
    startTimerHz(10);
    rebuildModules();
}

juce::Colour FxRackPanel::colourForFxName(const juce::String& fxName) const
{
    const juce::String n = fxName.toLowerCase();
    if (n.contains("base") || n.contains("density") || n.contains("brightness") || n.contains("gain") || n.contains("pitch"))
        return juce::Colour(0xFFA78BFA);
    if (n.contains("delay") || n.contains("echo"))
        return juce::Colour(0xFF10B981);
    if (n.contains("space") || n.contains("blur") || n.contains("reverb"))
        return juce::Colour(0xFF0EA5E9);
    if (n.contains("filter") || n.contains("shade"))
        return juce::Colour(0xFFF59E0B);
    if (n.contains("compress") || n.contains("mass"))
        return juce::Colour(0xFF3B82F6);
    if (n.contains("satur") || n.contains("heat"))
        return juce::Colour(0xFFF97316);
    if (n.contains("dist") || n.contains("grit"))
        return juce::Colour(0xFFF43F5E);
    if (n.contains("contrast") || n.contains("prism"))
        return juce::Colour(0xFFD946EF);
    if (n.contains("freeze") || n.contains("stasis"))
        return juce::Colour(0xFF06B6D4);
    if (n.contains("perlin") || n.contains("fluid"))
        return juce::Colour(0xFF84CC16);
    if (n.contains("brown") || n.contains("chaos"))
        return juce::Colour(0xFF8B5CF6);

    return juce::Colour(0xFFE0A96D);
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
    const auto* db = processor.getObjectDatabase();

    auto readValue = [&](const juce::String& fxName, const juce::String& paramName, float fallback)
    {
        if (db)
            return db->getInterpolatedAutomationValue(objId, fxName.toStdString(), paramName.toStdString(), t, fallback);
        return fallback;
    };

    // Collect the four base parameters into a single synthesized "Base" module.
    struct BaseSpec { juce::String label; juce::String fxName; juce::String paramName; float fallback; };
    static const std::array<BaseSpec, 4> baseSpecs = {{
        { "Density",    "Density",    "Density",    1.0f },
        { "Brightness", "Brightness", "Brightness", 0.5f },
        { "Volume",     "Volume",     "Gain",       0.5f },
        { "Pitch",      "Pitch",      "Semitones",  0.5f }
    }};

    ModuleView base;
    base.name = "Base";
    base.isCore = true;
    base.accentColour = juce::Colour(0xFFA78BFA);

    for (const auto& spec : baseSpecs)
    {
        bool present = false;
        for (const auto& fx : chain)
        {
            if (juce::String(fx.name).equalsIgnoreCase(spec.fxName))
            {
                present = fx.enabled;
                break;
            }
        }

        if (!present)
            continue;

        KnobView knob;
        knob.label = spec.label;
        knob.fxName = spec.fxName;
        knob.paramName = spec.paramName;
        knob.paramIndexInFx = 0;
        knob.value = readValue(spec.fxName, spec.paramName, spec.fallback);
        base.knobs.push_back(std::move(knob));
    }

    if (!base.knobs.empty())
        modules.push_back(std::move(base));

    // Remaining non-base FX modules keep their own cards.
    for (const auto& fx : chain)
    {
        if (!fx.enabled)
            continue;

        const juce::String fxName(fx.name);
        if (fxName.equalsIgnoreCase("Density") || fxName.equalsIgnoreCase("Brightness")
            || fxName.equalsIgnoreCase("Volume") || fxName.equalsIgnoreCase("Pitch"))
            continue;

        ModuleView mod;
        mod.name = fxName;
        mod.isCore = false;
        mod.accentColour = colourForFxName(fxName);

        int paramIndex = 0;
        for (const auto& param : fx.parameters)
        {
            KnobView knob;
            knob.label = juce::String(param.name);
            knob.fxName = fxName;
            knob.paramName = juce::String(param.name);
            knob.paramIndexInFx = paramIndex++;
            knob.value = readValue(fxName, knob.paramName, 0.5f);
            mod.knobs.push_back(std::move(knob));
        }

        modules.push_back(std::move(mod));
    }

    repaint();
}

bool FxRackPanel::isKnobSelected(const KnobView& knob) const
{
    return processor.getActiveFxEffectName().equalsIgnoreCase(knob.fxName)
        && processor.getActiveFxParameterName().equalsIgnoreCase(knob.paramName);
}

std::vector<FxRackPanel::ModuleLayout> FxRackPanel::computeLayouts() const
{
    std::vector<ModuleLayout> layouts;

    auto area = getLocalBounds().reduced(4);
    int x = area.getX() - scrollOffsetX;
    const int top = area.getY();
    const int cardH = area.getHeight();

    for (int m = 0; m < static_cast<int>(modules.size()); ++m)
    {
        const auto& mod = modules[static_cast<size_t>(m)];
        const int w = mod.isCore ? moduleWidthCore : moduleWidthFx;

        ModuleLayout layout;
        layout.moduleIndex = m;
        layout.isCore = mod.isCore;
        layout.cardBounds = juce::Rectangle<int>(x, top, w, cardH);

        auto card = layout.cardBounds;
        auto header = card.removeFromTop(22);
        layout.closeButtonBounds = juce::Rectangle<int>(header.getRight() - 18, header.getY(), 18, header.getHeight());

        auto content = card.reduced(6, 4);
        const int numKnobs = static_cast<int>(mod.knobs.size());

        if (numKnobs > 0)
        {
            if (mod.isCore)
            {
                const int cols = 2;
                const int rows = 2;
                const int cellW = content.getWidth() / cols;
                const int cellH = content.getHeight() / rows;
                for (int i = 0; i < juce::jmin(numKnobs, 4); ++i)
                {
                    const int r = i / cols;
                    const int c = i % cols;
                    juce::Rectangle<int> cell(content.getX() + c * cellW,
                                              content.getY() + r * cellH,
                                              cellW, cellH);
                    KnobHit hit;
                    hit.moduleIndex = m;
                    hit.knobIndex = i;
                    hit.bounds = cell;
                    layout.knobs.push_back(hit);
                }
            }
            else if (numKnobs == 2)
            {
                const int cellH = content.getHeight() / 2;
                for (int i = 0; i < 2; ++i)
                {
                    juce::Rectangle<int> cell(content.getX(), content.getY() + i * cellH,
                                              content.getWidth(), cellH);
                    KnobHit hit;
                    hit.moduleIndex = m;
                    hit.knobIndex = i;
                    hit.bounds = cell;
                    layout.knobs.push_back(hit);
                }
            }
            else
            {
                const int cellW = content.getWidth() / juce::jmax(1, numKnobs);
                for (int i = 0; i < numKnobs; ++i)
                {
                    juce::Rectangle<int> cell(content.getX() + i * cellW, content.getY(),
                                              cellW, content.getHeight());
                    KnobHit hit;
                    hit.moduleIndex = m;
                    hit.knobIndex = i;
                    hit.bounds = cell;
                    layout.knobs.push_back(hit);
                }
            }
        }

        layouts.push_back(std::move(layout));
        x += w + moduleGap;
    }

    return layouts;
}

void FxRackPanel::drawKnob(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                           float normValue, juce::Colour accent, bool isSelected) const
{
    const float labelH = 12.0f;
    const float centreX = static_cast<float>(area.getCentreX());
    const float knobBlockH = static_cast<float>(area.getHeight()) - labelH;
    const float diameter = juce::jmin(static_cast<float>(knobDiameter),
                                      juce::jmin(static_cast<float>(area.getWidth()) - 6.0f, knobBlockH - 4.0f));
    const float radius = diameter * 0.5f;
    const float centreY = static_cast<float>(area.getY()) + knobBlockH * 0.5f;

    const juce::Rectangle<float> slotRect(centreX - radius - 2.0f, centreY - radius - 2.0f,
                                          diameter + 4.0f, diameter + 4.0f);
    g.setColour(juce::Colour(0xFF09090B));
    g.fillEllipse(slotRect);

    const juce::Rectangle<float> knobRect(centreX - radius, centreY - radius, diameter, diameter);
    juce::ColourGradient grad(juce::Colour(0xFF52525B), knobRect.getX(), knobRect.getY(),
                              juce::Colour(0xFF27272A), knobRect.getRight(), knobRect.getBottom(), true);
    g.setGradientFill(grad);
    g.fillEllipse(knobRect);

    if (isSelected)
    {
        g.setColour(accent.withAlpha(0.30f));
        g.drawEllipse(knobRect.expanded(2.4f), 3.0f);
        g.setColour(accent.withAlpha(0.55f));
        g.drawEllipse(knobRect.expanded(1.0f), 1.8f);
    }

    // Only the selected knob gets a glowing accent ring; all others stay neutral.
    g.setColour(isSelected ? accent : juce::Colour(0xFF52525B));
    g.drawEllipse(knobRect, isSelected ? 1.6f : 1.0f);

    // Center-detent mapping: 0 -> -135deg, 0.5 -> 0deg (straight up), 1 -> +135deg.
    const float clamped = juce::jlimit(0.0f, 1.0f, normValue);
    const float angle = (clamped - 0.5f) * (juce::MathConstants<float>::pi * 1.5f);
    const float innerR = radius - 3.0f - radius * 0.5f;
    const float outerR = radius - 3.0f;
    const float ix = centreX + std::sin(angle) * innerR;
    const float iy = centreY - std::cos(angle) * innerR;
    const float ix2 = centreX + std::sin(angle) * outerR;
    const float iy2 = centreY - std::cos(angle) * outerR;

    g.setColour(isSelected ? accent.brighter(0.3f) : juce::Colour(0xFFD4D4D8));
    g.drawLine(ix, iy, ix2, iy2, 2.0f);

    g.setColour(isSelected ? accent : juce::Colour(0xFFA1A1AA));
    g.setFont(juce::Font(8.5f, isSelected ? juce::Font::bold : juce::Font::plain));
    g.drawText(label, juce::Rectangle<float>(static_cast<float>(area.getX()),
                                             static_cast<float>(area.getBottom()) - labelH,
                                             static_cast<float>(area.getWidth()), labelH),
               juce::Justification::centredTop, false);
}

void FxRackPanel::drawModuleCard(juce::Graphics& g, const ModuleLayout& layout, const ModuleView& mod) const
{
    auto area = layout.cardBounds;

    g.setColour(juce::Colour(0xCC27272A));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawRoundedRectangle(area.toFloat(), 6.0f, 1.0f);

    if (!mod.isCore)
    {
        g.setColour(mod.accentColour);
        g.fillRect(juce::Rectangle<float>(static_cast<float>(area.getX()), static_cast<float>(area.getY()),
                                           2.0f, static_cast<float>(area.getHeight())));
    }

    auto headerArea = area.removeFromTop(22);
    g.setColour(mod.isCore ? juce::Colour(0xE53F3F46) : mod.accentColour.withAlpha(0.15f));
    g.fillRoundedRectangle(headerArea.toFloat().withTrimmedBottom(-3.0f), 6.0f);
    g.fillRect(headerArea.toFloat().withTrimmedTop(3.0f));

    g.setColour(juce::Colour(0xFF3F3F46).withAlpha(0.5f));
    g.drawHorizontalLine(headerArea.getBottom(), static_cast<float>(headerArea.getX()), static_cast<float>(headerArea.getRight()));

    const float dotX = static_cast<float>(headerArea.getX()) + 8.0f;
    const float dotY = static_cast<float>(headerArea.getCentreY());
    g.setColour(mod.accentColour);
    g.fillEllipse(dotX - 2.5f, dotY - 2.5f, 5.0f, 5.0f);

    g.setColour(mod.isCore ? juce::Colour(0xFFE4E4E7) : mod.accentColour);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText(mod.name.toUpperCase(), headerArea.withTrimmedLeft(18).withTrimmedRight(20),
               juce::Justification::centredLeft, true);

    if (mod.isCore)
    {
        g.setColour(juce::Colour(0xFF71717A));
        g.setFont(juce::Font(8.0f));
        g.drawText("CORE", headerArea.withTrimmedRight(6), juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour(juce::Colour(0xFF71717A));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText("x", layout.closeButtonBounds, juce::Justification::centred, false);
    }

    for (const auto& hit : layout.knobs)
    {
        const auto& knob = mod.knobs[static_cast<size_t>(hit.knobIndex)];
        drawKnob(g, hit.bounds, knob.label, knob.value, mod.accentColour, isKnobSelected(knob));
    }
}

void FxRackPanel::drawAddFxButton(juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour(juce::Colour(0x4D27272A));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);

    auto dashed = area.toFloat().reduced(4.0f);
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawRoundedRectangle(dashed, 6.0f, 1.0f);

    g.setColour(juce::Colour(0xFF71717A));
    g.setFont(juce::Font(22.0f));
    g.drawText("+", area.withTrimmedBottom(16), juce::Justification::centredBottom, false);

    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText("ADD FX", area.withTrimmedTop(area.getHeight() / 2 + 4), juce::Justification::centredTop, false);
}

void FxRackPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF18181B));

    const auto layouts = computeLayouts();
    for (const auto& layout : layouts)
    {
        if (layout.cardBounds.getRight() < 0 || layout.cardBounds.getX() > getWidth())
            continue;
        drawModuleCard(g, layout, modules[static_cast<size_t>(layout.moduleIndex)]);
    }

    int addX = getLocalBounds().reduced(4).getX() - scrollOffsetX;
    for (const auto& mod : modules)
        addX += (mod.isCore ? moduleWidthCore : moduleWidthFx) + moduleGap;

    juce::Rectangle<int> addArea(addX, getLocalBounds().reduced(4).getY(), addFxWidth, getHeight() - 8);
    if (addArea.getRight() >= 0 && addArea.getX() < getWidth())
        drawAddFxButton(g, addArea);
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
    const auto layouts = computeLayouts();

    for (const auto& layout : layouts)
    {
        const auto& mod = modules[static_cast<size_t>(layout.moduleIndex)];

        // Remove FX via close button in header.
        if (!mod.isCore && layout.closeButtonBounds.contains(event.getPosition()))
        {
            const int objId = processor.getSelectedObjectId();
            if (objId > 0)
            {
                processor.setObjectFxEnabled(objId, mod.name, false);
                rebuildModules();
            }
            return;
        }

        for (const auto& hit : layout.knobs)
        {
            if (!hit.bounds.contains(event.getPosition()))
                continue;

            const auto& knob = mod.knobs[static_cast<size_t>(hit.knobIndex)];
            const int objId = processor.getSelectedObjectId();
            if (objId <= 0)
                return;

            processor.setActiveFxSelection(knob.fxName, knob.paramName);
            processor.setObjectFxSelectedParameter(objId, knob.fxName, knob.paramIndexInFx);

            activeModuleIndex = layout.moduleIndex;
            activeKnobIndex = hit.knobIndex;
            draggingKnob = true;
            dragStartValue = knob.value;
            rebuildModules();
            return;
        }
    }

    // Add FX slot: after the last module.
    int addX = getLocalBounds().reduced(4).getX() - scrollOffsetX;
    for (const auto& mod : modules)
        addX += (mod.isCore ? moduleWidthCore : moduleWidthFx) + moduleGap;

    juce::Rectangle<int> addArea(addX, getLocalBounds().reduced(4).getY(), addFxWidth, getHeight() - 8);
    if (addArea.contains(event.getPosition()))
    {
        if (onAddFxRequested)
            onAddFxRequested();
    }
}

void FxRackPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (!draggingKnob || activeModuleIndex < 0 || activeKnobIndex < 0)
        return;

    if (activeModuleIndex >= static_cast<int>(modules.size()))
        return;

    const auto& mod = modules[static_cast<size_t>(activeModuleIndex)];
    if (activeKnobIndex >= static_cast<int>(mod.knobs.size()))
        return;

    const auto& knob = mod.knobs[static_cast<size_t>(activeKnobIndex)];

    // Gentle sensitivity: full sweep needs ~200px of vertical travel.
    const float delta = static_cast<float>(-event.getDistanceFromDragStartY()) / 200.0f;
    const float norm = juce::jlimit(0.0f, 1.0f, dragStartValue + delta);

    const int objId = processor.getSelectedObjectId();
    if (objId <= 0)
        return;

    processor.addFxAutomationKeyframe(objId, knob.fxName, knob.paramName, processor.getTransportSeconds(), norm);
    rebuildModules();
}

void FxRackPanel::mouseUp(const juce::MouseEvent&)
{
    draggingKnob = false;
    activeModuleIndex = -1;
    activeKnobIndex = -1;
}
