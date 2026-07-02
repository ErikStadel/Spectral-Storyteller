#include "ModulationPanel.h"
#include "../PluginProcessor.h"

namespace
{
constexpr int noneItemId = 9999;

bool targetMatches(const ModulationMatrix::Target& t, const juce::String& text)
{
    if (!t.isValid())
        return false;

    return text == (t.fxName + " · " + t.paramName);
}
}

ModulationPanel::ModulationPanel(PluginProcessor& p) : processor(p)
{
    addAndMakeVisible(slotCombo);
    slotCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF18181B));
    slotCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF3F3F46));
    slotCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xFFD4D4D8));
    slotCombo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xFFA1A1AA));
    slotCombo.setJustificationType(juce::Justification::centredRight);
    for (int i = 0; i < ModulationMatrix::NUM_LFOS; ++i)
        slotCombo.addItem("LFO " + juce::String(i + 1), 1 + i);
    for (int i = 0; i < ModulationMatrix::NUM_XY; ++i)
        slotCombo.addItem("XY " + juce::String(i + 1), 100 + i);

    slotCombo.setSelectedId(1, juce::dontSendNotification);
    slotCombo.onChange = [this]
    {
        const int id = slotCombo.getSelectedId();
        if (id >= 100)
        {
            currentKind = SlotKind::XY;
            currentIndex = id - 100;
        }
        else
        {
            currentKind = SlotKind::LFO;
            currentIndex = id - 1;
        }

        rewireForCurrentSlot();
    };

    addChildComponent(lfoRateCombo);
    addChildComponent(lfoShapeCombo);
    addChildComponent(lfoTargetCombo);
    addChildComponent(lfoTargetLabel);
    addChildComponent(lfoScope);
    addChildComponent(lfoAmountTextLabel);
    addChildComponent(lfoOffsetTextLabel);
    addChildComponent(lfoAmountValue);
    addChildComponent(lfoOffsetValue);

    for (const auto& s : ModulationMatrix::rateLabels())
        lfoRateCombo.addItem(s, lfoRateCombo.getNumItems() + 1);

    lfoShapeCombo.addItem("Sine", 1);
    lfoShapeCombo.addItem("Triangle", 2);
    lfoShapeCombo.addItem("Saw", 3);
    lfoShapeCombo.addItem("Square", 4);

    for (auto* cb : { &lfoRateCombo, &lfoShapeCombo, &lfoTargetCombo, &xyTargetXCombo, &xyTargetYCombo })
    {
        cb->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF18181B));
        cb->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF3F3F46));
        cb->setColour(juce::ComboBox::textColourId, juce::Colour(0xFFD4D4D8));
        cb->setColour(juce::ComboBox::arrowColourId, juce::Colour(0xFFA1A1AA));
    }

    auto setupValueField = [](juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(0.0, 1.0, 0.001);
        s.setNumDecimalPlacesToDisplay(3);
        s.setColour(juce::Slider::thumbColourId, juce::Colour(0xFFE0A96D));
        s.setColour(juce::Slider::trackColourId, juce::Colour(0xFFE0A96D));
        s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF27272A));
    };

    setupValueField(lfoAmountValue);
    setupValueField(lfoOffsetValue);

    for (auto* label : { &lfoAmountTextLabel, &lfoOffsetTextLabel })
    {
        label->setJustificationType(juce::Justification::centredLeft);
        label->setFont(juce::Font(10.0f));
        label->setColour(juce::Label::textColourId, juce::Colour(0xFFA1A1AA));
    }

    lfoTargetLabel.setFont(juce::Font(9.0f, juce::Font::bold));
    lfoTargetLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFE0A96D));
    xyTargetXLabel.setFont(juce::Font(8.5f, juce::Font::bold));
    xyTargetYLabel.setFont(juce::Font(8.5f, juce::Font::bold));
    xyTargetXLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFE0A96D));
    xyTargetYLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFE0A96D));

    lfoRateCombo.onChange = [this]
    {
        processor.getModulationMatrix().lfo(currentIndex).rateIndex.store(lfoRateCombo.getSelectedItemIndex());
    };

    lfoShapeCombo.onChange = [this]
    {
        processor.getModulationMatrix().lfo(currentIndex).shape.store(lfoShapeCombo.getSelectedItemIndex());
        lfoScope.shape = static_cast<ModulationMatrix::Shape>(lfoShapeCombo.getSelectedItemIndex());
    };

    lfoAmountValue.onValueChange = [this]
    {
        processor.getModulationMatrix().lfo(currentIndex).amount.store(static_cast<float>(lfoAmountValue.getValue()));
    };

    lfoOffsetValue.onValueChange = [this]
    {
        processor.getModulationMatrix().lfo(currentIndex).phaseOffset.store(static_cast<float>(lfoOffsetValue.getValue()));
    };

    lfoTargetCombo.onChange = [this]
    {
        auto& s = processor.getModulationMatrix().lfo(currentIndex);
        applyTargetFromMenu(lfoTargetCombo, s.target);
    };

    lfoScope.getValue = [this]
    {
        return processor.getModulationMatrix().getLfoVisualValue(currentIndex);
    };

    addChildComponent(xyPad);
    addChildComponent(xyTargetXCombo);
    addChildComponent(xyTargetYCombo);
    addChildComponent(xyTargetXLabel);
    addChildComponent(xyTargetYLabel);

    xyPad.onMove = [this](float x, float y)
    {
        auto& s = processor.getModulationMatrix().xy(currentIndex);
        s.x.store(x);
        s.y.store(y);
    };

    xyPad.getXY = [this]
    {
        auto& s = processor.getModulationMatrix().xy(currentIndex);
        return std::pair<float, float> { s.x.load(), s.y.load() };
    };

    xyTargetXCombo.onChange = [this]
    {
        auto& s = processor.getModulationMatrix().xy(currentIndex);
        applyTargetFromMenu(xyTargetXCombo, s.targetX);
    };

    xyTargetYCombo.onChange = [this]
    {
        auto& s = processor.getModulationMatrix().xy(currentIndex);
        applyTargetFromMenu(xyTargetYCombo, s.targetY);
    };

    rewireForCurrentSlot();
    startTimerHz(30);
}

void ModulationPanel::showLfo(bool show)
{
    juce::Component* controls[] = {
        &lfoRateCombo,
        &lfoShapeCombo,
        &lfoTargetCombo,
        &lfoTargetLabel,
        &lfoScope,
        &lfoAmountTextLabel,
        &lfoOffsetTextLabel,
        &lfoAmountValue,
        &lfoOffsetValue
    };

    for (auto* c : controls)
        c->setVisible(show);
}

void ModulationPanel::showXy(bool show)
{
    juce::Component* controls[] = {
        &xyPad,
        &xyTargetXCombo,
        &xyTargetYCombo,
        &xyTargetXLabel,
        &xyTargetYLabel
    };

    for (auto* c : controls)
        c->setVisible(show);
}

void ModulationPanel::rewireForCurrentSlot()
{
    const bool isLfo = currentKind == SlotKind::LFO;
    showLfo(isLfo);
    showXy(!isLfo);

    if (isLfo)
    {
        auto& s = processor.getModulationMatrix().lfo(currentIndex);
        lfoRateCombo.setSelectedItemIndex(s.rateIndex.load(), juce::dontSendNotification);
        lfoShapeCombo.setSelectedItemIndex(s.shape.load(), juce::dontSendNotification);
        lfoAmountValue.setValue(s.amount.load(), juce::dontSendNotification);
        lfoOffsetValue.setValue(s.phaseOffset.load(), juce::dontSendNotification);
        lfoScope.shape = static_cast<ModulationMatrix::Shape>(s.shape.load());
    }

    refresh();
    resized();
    repaint();
}

void ModulationPanel::refresh()
{
    rebuildTargetMenu(lfoTargetCombo);
    rebuildTargetMenu(xyTargetXCombo);
    rebuildTargetMenu(xyTargetYCombo);

    const auto& l = processor.getModulationMatrix().lfo(currentIndex);
    int lfoId = noneItemId;
    for (int i = 0; i < lfoTargetCombo.getNumItems(); ++i)
    {
        const auto txt = lfoTargetCombo.getItemText(i);
        if (targetMatches(l.target, txt))
        {
            lfoId = lfoTargetCombo.getItemId(i);
            break;
        }
    }
    lfoTargetCombo.setSelectedId(lfoId, juce::dontSendNotification);

    const auto& xy = processor.getModulationMatrix().xy(currentIndex);

    int xId = noneItemId;
    for (int i = 0; i < xyTargetXCombo.getNumItems(); ++i)
    {
        const auto txt = xyTargetXCombo.getItemText(i);
        if (targetMatches(xy.targetX, txt))
        {
            xId = xyTargetXCombo.getItemId(i);
            break;
        }
    }
    xyTargetXCombo.setSelectedId(xId, juce::dontSendNotification);

    int yId = noneItemId;
    for (int i = 0; i < xyTargetYCombo.getNumItems(); ++i)
    {
        const auto txt = xyTargetYCombo.getItemText(i);
        if (targetMatches(xy.targetY, txt))
        {
            yId = xyTargetYCombo.getItemId(i);
            break;
        }
    }
    xyTargetYCombo.setSelectedId(yId, juce::dontSendNotification);
}

void ModulationPanel::rebuildTargetMenu(juce::ComboBox& cb)
{
    cb.clear(juce::dontSendNotification);
    cb.addItem("none", noneItemId);

    const int objId = processor.getSelectedObjectId();
    if (objId <= 0)
        return;

    int id = 1;
    while (id == noneItemId)
        ++id;

    for (const auto& fx : processor.getFxChainForSelectedObject())
    {
        if (!fx.enabled)
            continue;

        for (const auto& p : fx.parameters)
        {
            cb.addItem(juce::String(fx.name) + " · " + juce::String(p.name), id++);
            if (id == noneItemId)
                ++id;
        }
    }
}

void ModulationPanel::applyTargetFromMenu(juce::ComboBox& cb, ModulationMatrix::Target& dst)
{
    ModulationMatrix::Target t;

    if (cb.getSelectedId() != noneItemId)
    {
        const juce::String txt = cb.getText();
        if (txt.contains(" · "))
        {
            t.objectId = processor.getSelectedObjectId();
            t.fxName = txt.upToFirstOccurrenceOf(" · ", false, false);
            t.paramName = txt.fromFirstOccurrenceOf(" · ", false, false);
        }
    }

    if (currentKind == SlotKind::LFO)
    {
        auto& l = processor.getModulationMatrix().lfo(currentIndex);
        juce::ScopedLock sl(l.targetLock);
        dst = t;
    }
    else
    {
        auto& xy = processor.getModulationMatrix().xy(currentIndex);
        juce::ScopedLock sl(xy.targetLock);
        dst = t;
    }

    repaint();
}

juce::String ModulationPanel::makeDragPayload(int objId, const juce::String& fx, const juce::String& param)
{
    return "fxparam|" + juce::String(objId) + "|" + fx + "|" + param;
}

bool ModulationPanel::parseDragPayload(const juce::var& v, int& objId, juce::String& fx, juce::String& param)
{
    const juce::String s = v.toString();
    if (!s.startsWith("fxparam|"))
        return false;

    auto parts = juce::StringArray::fromTokens(s, "|", "");
    if (parts.size() < 4)
        return false;

    objId = parts[1].getIntValue();
    fx = parts[2];
    param = parts[3];
    return true;
}

bool ModulationPanel::isInterestedInDragSource(const SourceDetails& d)
{
    int objId = -1;
    juce::String fx;
    juce::String param;
    return parseDragPayload(d.description, objId, fx, param);
}

void ModulationPanel::itemDropped(const SourceDetails& d)
{
    int objId = -1;
    juce::String fx;
    juce::String param;
    if (!parseDragPayload(d.description, objId, fx, param))
        return;

    ModulationMatrix::Target t { objId, fx, param };

    if (currentKind == SlotKind::LFO)
    {
        auto& l = processor.getModulationMatrix().lfo(currentIndex);
        juce::ScopedLock sl(l.targetLock);
        l.target = t;
    }
    else
    {
        auto& s = processor.getModulationMatrix().xy(currentIndex);
        const bool dropOnY = d.localPosition.y > getHeight() / 2;
        juce::ScopedLock sl(s.targetLock);
        (dropOnY ? s.targetY : s.targetX) = t;
    }

    refresh();
    repaint();
}

void ModulationPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xCC27272A));
    g.fillRoundedRectangle(r, 0.0f);
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawRect(r, 1.0f);

    // Header with accent colour
    auto headerBounds = getLocalBounds().reduced(8, 6).removeFromTop(20);
    g.setColour(juce::Colour(0xFFE0A96D));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("MODULATION HUB", headerBounds, juce::Justification::centredLeft, false);

    // Separator
    g.setColour(juce::Colour(0xFF3F3F46));
    g.drawHorizontalLine(headerBounds.getBottom() + 2, r.getX() + 6.0f, r.getRight() - 6.0f);

    juce::String headerText;
    bool hasTarget = false;

    if (currentKind == SlotKind::LFO)
    {
        const auto& l = processor.getModulationMatrix().lfo(currentIndex);
        hasTarget = l.target.isValid();
        headerText = hasTarget ? (l.target.fxName + " · " + l.target.paramName)
                               : juce::String("none");
    }
    else
    {
        const auto& s = processor.getModulationMatrix().xy(currentIndex);
        const juce::String xText = s.targetX.isValid() ? (s.targetX.fxName + " · " + s.targetX.paramName)
                                                       : juce::String("none");
        const juce::String yText = s.targetY.isValid() ? (s.targetY.fxName + " · " + s.targetY.paramName)
                                                       : juce::String("none");
        hasTarget = s.targetX.isValid() || s.targetY.isValid();
        headerText = "X: " + xText + "   Y: " + yText;
    }

    if (isDragOver)
    {
        g.setColour(juce::Colour(0xFFE0A96D).withAlpha(0.20f));
        g.fillRoundedRectangle(r.reduced(2.0f), 0.0f);
    }
}

void ModulationPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    auto header = area.removeFromTop(20);
    slotCombo.setBounds(header.removeFromRight(94).reduced(0, 1));

    area.removeFromTop(8);

    if (currentKind == SlotKind::LFO)
    {
        auto scopeArea = area.removeFromTop(84);
        lfoScope.setBounds(scopeArea.reduced(2));

        area.removeFromTop(4);
        auto row = area.removeFromTop(20);
        auto rate = row.removeFromLeft((row.getWidth() - 6) / 2);
        row.removeFromLeft(6);
        lfoRateCombo.setBounds(rate);
        lfoShapeCombo.setBounds(row);

        area.removeFromTop(4);
        auto slidersArea = area.removeFromTop(42);
        auto amountCol = slidersArea.removeFromLeft((slidersArea.getWidth() - 10) / 2);
        slidersArea.removeFromLeft(10);
        auto offsetCol = slidersArea;

        lfoAmountTextLabel.setBounds(amountCol.removeFromTop(14));
        lfoAmountValue.setBounds(amountCol.reduced(0, 3));
        lfoOffsetTextLabel.setBounds(offsetCol.removeFromTop(14));
        lfoOffsetValue.setBounds(offsetCol.reduced(0, 3));

        auto tgtRow = getLocalBounds().reduced(10).removeFromBottom(22);
        lfoTargetLabel.setBounds(tgtRow.removeFromLeft(64));
        lfoTargetCombo.setBounds(tgtRow);
    }
    else
    {
        auto bottomTargets = getLocalBounds().reduced(10).removeFromBottom(46);
        auto targetCols = bottomTargets;

        const int padSize = juce::jmax(64, juce::jmin(128, juce::jmin(area.getWidth(), area.getHeight() - 54)));
        auto padZone = area.removeFromTop(padSize + 6);
        xyPad.setBounds(padZone.withSizeKeepingCentre(padSize, padSize));

        auto left = targetCols.removeFromLeft((targetCols.getWidth() - 6) / 2);
        targetCols.removeFromLeft(6);
        auto right = targetCols;

        xyTargetXLabel.setBounds(left.removeFromTop(10));
        xyTargetXCombo.setBounds(left.removeFromTop(18));
        xyTargetYLabel.setBounds(right.removeFromTop(10));
        xyTargetYCombo.setBounds(right.removeFromTop(18));
    }
}

void ModulationPanel::LFOScope::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF0C0A09));
    g.fillRoundedRectangle(r, 4.0f);

    juce::Path p;
    const int N = juce::jmax(2, static_cast<int>(r.getWidth()));
    for (int i = 0; i < N; ++i)
    {
        const float ph = static_cast<float>(i) / static_cast<float>(N - 1);
        float v = 0.0f;

        switch (shape)
        {
            case ModulationMatrix::Shape::Sine:     v = std::sin(ph * juce::MathConstants<float>::twoPi); break;
            case ModulationMatrix::Shape::Triangle: v = 4.0f * std::abs(ph - 0.5f) - 1.0f; break;
            case ModulationMatrix::Shape::Saw:      v = 2.0f * ph - 1.0f; break;
            case ModulationMatrix::Shape::Square:   v = ph < 0.5f ? 1.0f : -1.0f; break;
        }

        const float y = r.getCentreY() - v * (r.getHeight() * 0.4f);
        if (i == 0)
            p.startNewSubPath(r.getX() + static_cast<float>(i), y);
        else
            p.lineTo(r.getX() + static_cast<float>(i), y);
    }

    g.setColour(juce::Colour(0xFFE0A96D));
    g.strokePath(p, juce::PathStrokeType(1.5f));

    if (getValue)
    {
        const float v = juce::jlimit(-1.0f, 1.0f, getValue());
        const float y = r.getCentreY() - v * (r.getHeight() * 0.4f);
        g.setColour(juce::Colours::white);
        g.fillEllipse(r.getRight() - 8.0f, y - 3.0f, 6.0f, 6.0f);
    }
}
