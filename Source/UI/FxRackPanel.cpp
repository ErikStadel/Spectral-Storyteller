#include "FxRackPanel.h"
#include "Typography.h"
#include "../PluginProcessor.h"
#include <array>

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

void FxRackPanel::showKnobContextMenu(const KnobView& knob, juce::Point<int> screenPosition)
{
    const int objId = processor.getSelectedObjectId();
    if (objId <= 0)
        return;

    const bool followTimeline = processor.getFxParameterFollowTimeline(objId, knob.fxName, knob.paramName, false);

    juce::PopupMenu menu;
    menu.addSectionHeader("Mode");
    menu.addItem(1, "Follow", true, followTimeline);
    menu.addItem(2, "Static", true, !followTimeline);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPosition.x, screenPosition.y, 1, 1)),
                       [this, objId, fxName = knob.fxName, paramName = knob.paramName](int result)
                       {
                           if (result == 1)
                               processor.setFxParameterFollowTimeline(objId, fxName, paramName, true);
                           else if (result == 2)
                               processor.setFxParameterFollowTimeline(objId, fxName, paramName, false);

                           rebuildModules();
                       });
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

bool FxRackPanel::useTwoByTwoLayout(const ModuleView& mod) const
{
    if (mod.isCore)
        return true;

    if (mod.knobs.size() != 4)
        return false;

    return mod.name.equalsIgnoreCase("Compressor")
        || mod.name.equalsIgnoreCase("Delay")
        || mod.name.equalsIgnoreCase("Saturation")
        || mod.name.equalsIgnoreCase("Distortion")
        || mod.name.equalsIgnoreCase("Freeze")
        || mod.name.equalsIgnoreCase("SpaceBlur")
        || mod.name.equalsIgnoreCase("Spaceblur");
}

juce::String FxRackPanel::getModuleDisplayName(const ModuleView& mod) const
{
    if (mod.isCore)
        return "Base";
    if (mod.name.equalsIgnoreCase("Filter"))
        return "Shade Contour";
    if (mod.name.equalsIgnoreCase("Compressor"))
        return "Mass Forge";
    if (mod.name.equalsIgnoreCase("Delay"))
        return "Echo Bleed";
    if (mod.name.equalsIgnoreCase("Saturation"))
        return "Heat Glow";
    if (mod.name.equalsIgnoreCase("Distortion"))
        return "Grit Edge";
    if (mod.name.equalsIgnoreCase("Freeze"))
        return "Stasis Cloud";
    if (mod.name.equalsIgnoreCase("SpaceBlur") || mod.name.equalsIgnoreCase("Spaceblur"))
        return "Space Blur";
    return mod.name;
}

bool FxRackPanel::isToggleParameter(const KnobView& knob) const
{
    return (knob.fxName.equalsIgnoreCase("Saturation") && knob.paramName.equalsIgnoreCase("Heat"))
        || (knob.fxName.equalsIgnoreCase("Freeze") && knob.paramName.equalsIgnoreCase("Freeze"));
}

void FxRackPanel::drawToggleButton(juce::Graphics& g,
                                   juce::Rectangle<int> area,
                                   const KnobView& knob,
                                   juce::Colour accent,
                                   bool isSelected) const
{
    const float labelH = 16.0f;
    const float valueH = 14.0f;
    const float centreX = static_cast<float>(area.getCentreX());
    const float contentH = static_cast<float>(area.getHeight()) - labelH - valueH;
    const float side = juce::jmin(34.0f, juce::jmin(static_cast<float>(area.getWidth()) - 6.0f, contentH - 6.0f));
    const float x = centreX - side * 0.5f;
    const float y = static_cast<float>(area.getY()) + (contentH - side) * 0.5f;
    const bool isOn = knob.value >= 0.5f;

    const juce::Rectangle<float> buttonRect(x, y, side, side);
    constexpr float corner = 4.0f;

    if (isOn)
    {
        // Pressed / Toggled state: Inset
        g.setColour(juce::Colour(0xFF101012));
        g.fillRoundedRectangle(buttonRect, corner);
        
        g.setColour(juce::Colour(0xFF000000));
        g.drawRoundedRectangle(buttonRect.reduced(0.5f), corner, 1.5f);
        
        // Subtle bright inner rim instead of a neon glow
        g.setColour(juce::Colour(0x33FFFFFF));
        g.drawRoundedRectangle(buttonRect.reduced(1.5f), corner - 1.0f, 1.0f);
    }
    else
    {
        // Raised hardware state
        juce::ColourGradient bgGrad(juce::Colour(0xFF2C2C2F), buttonRect.getX(), buttonRect.getY(),
                                    juce::Colour(0xFF1E1E20), buttonRect.getX(), buttonRect.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(buttonRect, corner);
        
        // Highlight top edge
        g.setColour(juce::Colour(0xFF3F3F42));
        g.drawRoundedRectangle(buttonRect.reduced(0.5f), corner, 1.0f);
        
        // Shadow bottom edge
        g.setColour(juce::Colour(0xFF0A0A0B));
        g.drawRoundedRectangle(buttonRect.translated(0.0f, 1.0f).reduced(0.5f), corner, 1.0f);
    }

    if (isSelected)
    {
        g.setColour(accent.withAlpha(0.35f));
        g.drawRoundedRectangle(buttonRect.expanded(2.0f), corner + 2.0f, 1.0f);
    }
    
    g.setColour(isOn ? accent.brighter(0.30f) : juce::Colour(0xFFB4B4B8));
    g.setFont(Typography::getLabelFont(isSelected));
    g.drawText(knob.label,
               juce::Rectangle<float>(static_cast<float>(area.getX()),
                                      static_cast<float>(area.getBottom()) - labelH - valueH,
                                      static_cast<float>(area.getWidth()),
                                      labelH),
               juce::Justification::centredTop,
               false);

    g.setColour(isOn ? accent : juce::Colour(0xFFAAAAAA));
    g.setFont(Typography::getValueFont());
    g.drawText(isOn ? "On" : "Off",
               juce::Rectangle<float>(static_cast<float>(area.getX()),
                                      static_cast<float>(area.getBottom()) - valueH,
                                      static_cast<float>(area.getWidth()),
                                      valueH),
               juce::Justification::centredTop,
               false);
}

juce::String FxRackPanel::formatKnobValue(const KnobView& knob) const
{
    const float v = juce::jlimit(0.0f, 1.0f, knob.value);
    const auto sizeLabel = [v]()
    {
        static const std::array<int, 4> sizes = { 512, 1024, 2048, 4096 };
        const int idx = juce::jlimit(0,
                                     static_cast<int>(sizes.size()) - 1,
                                     juce::roundToInt(v * static_cast<float>(sizes.size() - 1)));
        return juce::String(sizes[static_cast<size_t>(idx)]);
    };

    if (knob.fxName.equalsIgnoreCase("Pitch") || knob.paramName.equalsIgnoreCase("Semitones"))
        return juce::String((v - 0.5f) * 4.0f, 1) + " st";

    if (knob.fxName.equalsIgnoreCase("Volume") || knob.paramName.equalsIgnoreCase("Gain"))
        return juce::String(static_cast<int>(std::round(v * 200.0f))) + "%";

    if (knob.fxName.equalsIgnoreCase("Compressor") && knob.paramName.equalsIgnoreCase("Threshold"))
        return juce::String(juce::jmap(v, -48.0f, 0.0f), 1) + " dB";

    if (knob.fxName.equalsIgnoreCase("Compressor") && knob.paramName.equalsIgnoreCase("Forge"))
        return juce::String(1.0f + 49.0f * v * v, 1) + ":1";

    if (knob.fxName.equalsIgnoreCase("Compressor") && knob.paramName.equalsIgnoreCase("Mix"))
        return juce::String(static_cast<int>(std::round(v * 100.0f))) + "%";

    if (knob.fxName.equalsIgnoreCase("Filter") && knob.paramName.equalsIgnoreCase("Low Cut"))
    {
        const float hz = 20.0f * std::pow(10.0f, v * 2.0f);
        return hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " kHz" : juce::String(static_cast<int>(std::round(hz))) + " Hz";
    }

    if (knob.fxName.equalsIgnoreCase("Filter") && knob.paramName.equalsIgnoreCase("High Cut"))
    {
        const float hz = 1000.0f * std::pow(10.0f, v * 1.30103f);
        return hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " kHz" : juce::String(static_cast<int>(std::round(hz))) + " Hz";
    }

    if (knob.fxName.equalsIgnoreCase("Delay") && knob.paramName.equalsIgnoreCase("Time"))
    {
        static const std::array<juce::String, 5> straight = { "1/1", "1/2", "1/4", "1/8", "1/16" };
        static const std::array<juce::String, 5> dotted = { "1/1.", "1/2.", "1/4.", "1/8.", "1/16." };
        if (v >= 0.5f)
        {
            const float t = juce::jlimit(0.0f, 1.0f, (v - 0.5f) / 0.5f);
            const int idx = juce::jlimit(0, static_cast<int>(straight.size()) - 1,
                                         static_cast<int>(std::round(t * static_cast<float>(straight.size() - 1))));
            return straight[static_cast<size_t>(idx)];
        }

        const float t = juce::jlimit(0.0f, 1.0f, (0.5f - v) / 0.5f);
        const int idx = juce::jlimit(0, static_cast<int>(dotted.size()) - 1,
                                     static_cast<int>(std::round(t * static_cast<float>(dotted.size() - 1))));
        return dotted[static_cast<size_t>(idx)];
    }

    if (knob.fxName.equalsIgnoreCase("Delay") && (knob.paramName.equalsIgnoreCase("Feedback")
                                                 || knob.paramName.equalsIgnoreCase("Bleed")
                                                 || knob.paramName.equalsIgnoreCase("Mix")))
        return juce::String(static_cast<int>(std::round(v * 100.0f))) + "%";

    if (knob.fxName.equalsIgnoreCase("Saturation") && knob.paramName.equalsIgnoreCase("Heat"))
        return v >= 0.5f ? "On" : "Off";

    if (knob.fxName.equalsIgnoreCase("Saturation")
        && (knob.paramName.equalsIgnoreCase("Drive")
         || knob.paramName.equalsIgnoreCase("Glow")
         || knob.paramName.equalsIgnoreCase("Mix")))
        return juce::String(static_cast<int>(std::round(v * 100.0f))) + "%";

    if (knob.fxName.equalsIgnoreCase("Distortion") && knob.paramName.equalsIgnoreCase("Edge"))
    {
        const float edgeDb = (v - 0.5f) * 24.0f;
        return juce::String(edgeDb, 1) + " dB";
    }

    if (knob.fxName.equalsIgnoreCase("Distortion")
        && (knob.paramName.equalsIgnoreCase("Grit")
         || knob.paramName.equalsIgnoreCase("Asymmetry")
         || knob.paramName.equalsIgnoreCase("Mix")))
        return juce::String(static_cast<int>(std::round(v * 100.0f))) + "%";

    if (knob.fxName.equalsIgnoreCase("Freeze") && knob.paramName.equalsIgnoreCase("Freeze"))
        return v >= 0.5f ? "On" : "Off";

    if (knob.fxName.equalsIgnoreCase("Freeze") && knob.paramName.equalsIgnoreCase("Size"))
        return juce::String(static_cast<int>(std::round(v * 100.0f))) + "%";

    if (knob.fxName.equalsIgnoreCase("Freeze")
        && (knob.paramName.equalsIgnoreCase("Cloud")
         || knob.paramName.equalsIgnoreCase("Mix")))
        return juce::String(static_cast<int>(std::round(v * 100.0f))) + "%";

    if ((knob.fxName.equalsIgnoreCase("SpaceBlur") || knob.fxName.equalsIgnoreCase("Spaceblur"))
        && (knob.paramName.equalsIgnoreCase("Size")
         || knob.paramName.equalsIgnoreCase("Decay")
         || knob.paramName.equalsIgnoreCase("Blur")
         || knob.paramName.equalsIgnoreCase("Mix")))
        return juce::String(static_cast<int>(std::round(v * 100.0f))) + "%";

    if (knob.fxName.equalsIgnoreCase("Density"))
        return juce::String(static_cast<int>(std::round(v * 100.0f))) + "%";

    if (knob.fxName.equalsIgnoreCase("Brightness"))
        return juce::String(static_cast<int>(std::round((v - 0.5f) * 200.0f))) + "%";

    return juce::String(v, 2);
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
            if (useTwoByTwoLayout(mod))
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

void FxRackPanel::drawKnob(juce::Graphics& g, juce::Rectangle<int> area, const KnobView& knob,
                           juce::Colour accent, bool isSelected) const
{
    const float labelH = 16.0f;
    const float valueH = 14.0f;
    const float centreX = static_cast<float>(area.getCentreX());
    const float knobBlockH = static_cast<float>(area.getHeight()) - labelH - valueH;
    const float diameter = juce::jmin(static_cast<float>(knobDiameter),
                                      juce::jmin(static_cast<float>(area.getWidth()) - 6.0f, knobBlockH - 4.0f));
    const float radius = diameter * 0.5f;
    const float centreY = static_cast<float>(area.getY()) + knobBlockH * 0.5f;

    if (isToggleParameter(knob))
    {
        drawToggleButton(g, area, knob, accent, isSelected);
        return;
    }

    const juce::Rectangle<float> slotRect(centreX - radius - 2.0f, centreY - radius - 2.0f,
                                          diameter + 4.0f, diameter + 4.0f);
    
    // 1. Drop shadow
    g.setColour(juce::Colour(0x99000000));
    g.fillEllipse(slotRect.translated(0.0f, 2.0f));

    // 2. Base metallic ring (outer rim)
    juce::ColourGradient rimGrad(juce::Colour(0xFF333336), slotRect.getCentreX(), slotRect.getY(),
                                 juce::Colour(0xFF0A0A0B), slotRect.getCentreX(), slotRect.getBottom(), false);
    g.setGradientFill(rimGrad);
    g.fillEllipse(slotRect);

    // 3. Inner knob body (conical or radial gradient)
    auto innerR = slotRect.reduced(2.0f);
    juce::ColourGradient bodyGrad(juce::Colour(0xFF262629), innerR.getCentreX(), innerR.getY(),
                                  juce::Colour(0xFF141416), innerR.getCentreX(), innerR.getBottom(), false);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(innerR);

    // 4. Subtle top highlight on the knob face
    g.setColour(juce::Colour(0x33FFFFFF));
    g.drawEllipse(innerR.reduced(1.0f), 1.0f);

    // Only the selected knob gets a glowing accent ring
    if (isSelected)
    {
        g.setColour(accent.withAlpha(0.55f));
        g.drawEllipse(slotRect, 1.0f);
    }

    // Pointer
    const float clamped = juce::jlimit(0.0f, 1.0f, knob.value);
    const float angle = (clamped - 0.5f) * (juce::MathConstants<float>::pi * 1.5f);
    const float len = radius * 0.6f;
    const float ix = centreX + std::sin(angle) * (radius - len);
    const float iy = centreY - std::cos(angle) * (radius - len);
    const float ix2 = centreX + std::sin(angle) * (radius - 2.5f);
    const float iy2 = centreY - std::cos(angle) * (radius - 2.5f);

    g.setColour(juce::Colour(0xFF000000));
    g.drawLine(ix, iy+1.0f, ix2, iy2+1.0f, 3.0f);

    g.setColour(juce::Colour(0xFFFFFFFF));
    g.drawLine(ix, iy, ix2, iy2, 2.0f);

    g.setColour(isSelected ? accent : juce::Colour(0xFFCCCCCC));
    g.setFont(Typography::getLabelFont(isSelected));
    g.drawText(knob.label, juce::Rectangle<float>(static_cast<float>(area.getX()),
                                             static_cast<float>(area.getBottom()) - labelH - valueH,
                                             static_cast<float>(area.getWidth()), labelH),
               juce::Justification::centredTop, false);

    g.setColour(juce::Colour(0xFFAAAAAA));
    g.setFont(Typography::getValueFont());
    g.drawText(formatKnobValue(knob), juce::Rectangle<float>(static_cast<float>(area.getX()),
                                                             static_cast<float>(area.getBottom()) - valueH,
                                                             static_cast<float>(area.getWidth()), valueH),
               juce::Justification::centredTop, false);
}

void FxRackPanel::drawModuleCard(juce::Graphics& g, const ModuleLayout& layout, const ModuleView& mod) const
{
    auto area = layout.cardBounds;

    g.setColour(juce::Colour(0xFF141416));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xFF333336));
    g.drawRoundedRectangle(area.toFloat(), 6.0f, 1.0f);

    auto headerArea = area.removeFromTop(22);
    g.setColour(juce::Colour(0xFF1E1E20)); // Monochrome header
    g.fillRoundedRectangle(headerArea.toFloat().withTrimmedBottom(-3.0f), 6.0f);
    g.fillRect(headerArea.toFloat().withTrimmedTop(3.0f));

    g.setColour(juce::Colour(0xFF333336).withAlpha(0.5f));
    g.drawHorizontalLine(headerArea.getBottom(), static_cast<float>(headerArea.getX()), static_cast<float>(headerArea.getRight()));

    const float dotX = static_cast<float>(headerArea.getX()) + 8.0f;
    const float dotY = static_cast<float>(headerArea.getCentreY());
    g.setColour(mod.accentColour);
    g.fillEllipse(dotX - 2.5f, dotY - 2.5f, 5.0f, 5.0f);

    // Make text white/silver instead of accent color
    g.setColour(juce::Colour(0xFFE4E4E7));
    g.setFont(Typography::getHeaderFont());
    g.drawText(getModuleDisplayName(mod).toUpperCase(), headerArea.withTrimmedLeft(18).withTrimmedRight(20),
               juce::Justification::centredLeft, true);

    if (mod.isCore)
    {
        g.setColour(juce::Colour(0xFFAAAAAA));
        g.setFont(Typography::getMicroFont(false));
        g.drawText("CORE", headerArea.withTrimmedRight(6), juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour(juce::Colour(0xFFAAAAAA));
        g.setFont(Typography::getHeaderFont());
        g.drawText("x", layout.closeButtonBounds, juce::Justification::centred, false);
    }

    for (const auto& hit : layout.knobs)
    {
        const auto& knob = mod.knobs[static_cast<size_t>(hit.knobIndex)];
        drawKnob(g, hit.bounds, knob, mod.accentColour, isKnobSelected(knob));
    }
}

void FxRackPanel::drawAddFxButton(juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour(juce::Colour(0x4D27272A));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);

    auto dashed = area.toFloat().reduced(4.0f);
    g.setColour(juce::Colour(0xFF333336));
    g.drawRoundedRectangle(dashed, 6.0f, 1.0f);

    g.setColour(juce::Colour(0xFFAAAAAA));
    g.setFont(Typography::getTitleFont().withHeight(22.0f));
    g.drawText("+", area.withTrimmedBottom(16), juce::Justification::centredBottom, false);

    g.setFont(Typography::getMicroFont(true));
    g.drawText("ADD FX", area.withTrimmedTop(area.getHeight() / 2 + 4), juce::Justification::centredTop, false);
}

void FxRackPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF18181A));

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

            if (event.mods.isRightButtonDown())
            {
                showKnobContextMenu(knob, event.getScreenPosition());
                return;
            }

            processor.setActiveFxSelection(knob.fxName, knob.paramName);
            processor.setObjectFxSelectedParameter(objId, knob.fxName, knob.paramIndexInFx);

            if (isToggleParameter(knob))
            {
                const float toggledValue = knob.value >= 0.5f ? 0.0f : 1.0f;
                if (processor.getFxParameterFollowTimeline(objId, knob.fxName, knob.paramName, false))
                    processor.addFxAutomationKeyframe(objId, knob.fxName, knob.paramName, processor.getTransportSeconds(), toggledValue);
                else
                    processor.setFxStaticParameterValue(objId, knob.fxName, knob.paramName, toggledValue);

                rebuildModules();
                return;
            }

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

    const bool followTimeline = processor.getFxParameterFollowTimeline(objId, knob.fxName, knob.paramName, false);
    if (followTimeline)
    {
        processor.addFxAutomationKeyframe(objId, knob.fxName, knob.paramName, processor.getTransportSeconds(), norm);
    }
    else
    {
        processor.setFxStaticParameterValue(objId, knob.fxName, knob.paramName, norm);
    }

    rebuildModules();
}

void FxRackPanel::mouseUp(const juce::MouseEvent&)
{
    draggingKnob = false;
    activeModuleIndex = -1;
    activeKnobIndex = -1;
}
