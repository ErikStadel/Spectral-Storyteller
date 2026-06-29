#include "ModulationPanel.h"
#include "../PluginProcessor.h"

ModulationPanel::ModulationPanel(PluginProcessor& p) : processor(p)
{
    addAndMakeVisible(slotCombo);
    for (int i = 0; i < ModulationMatrix::NUM_LFOS; ++i) slotCombo.addItem("LFO " + juce::String(i+1), 1 + i);
    for (int i = 0; i < ModulationMatrix::NUM_XY;   ++i) slotCombo.addItem("XY "  + juce::String(i+1), 100 + i);
    slotCombo.setSelectedId(1, juce::dontSendNotification);
    slotCombo.onChange = [this]
    {
        const int id = slotCombo.getSelectedId();
        if (id >= 100) { currentKind = SlotKind::XY;  currentIndex = id - 100; }
        else           { currentKind = SlotKind::LFO; currentIndex = id - 1;   }
        rewireForCurrentSlot();
    };

    // LFO
    addChildComponent(lfoRateCombo);
    addChildComponent(lfoShapeCombo);
    addChildComponent(lfoAmountSlider);
    addChildComponent(lfoTargetCombo);
    addChildComponent(lfoTargetLabel);
    addChildComponent(lfoScope);
    for (const auto& s : ModulationMatrix::rateLabels()) lfoRateCombo.addItem(s, lfoRateCombo.getNumItems() + 1);
    lfoShapeCombo.addItem("Sine", 1); lfoShapeCombo.addItem("Triangle", 2);
    lfoShapeCombo.addItem("Saw", 3);  lfoShapeCombo.addItem("Square", 4);
    lfoAmountSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    lfoAmountSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
    lfoAmountSlider.setRange(0.0, 1.0, 0.001);

    lfoRateCombo.onChange   = [this]{ processor.getModulationMatrix().lfo(currentIndex).rateIndex.store(lfoRateCombo.getSelectedItemIndex()); };
    lfoShapeCombo.onChange  = [this]{ processor.getModulationMatrix().lfo(currentIndex).shape.store(lfoShapeCombo.getSelectedItemIndex()); lfoScope.shape = (ModulationMatrix::Shape) lfoShapeCombo.getSelectedItemIndex(); };
    lfoAmountSlider.onValueChange = [this]{ processor.getModulationMatrix().lfo(currentIndex).amount.store((float) lfoAmountSlider.getValue()); };
    lfoTargetCombo.onChange = [this]{ applyTargetFromMenu(lfoTargetCombo, processor.getModulationMatrix().lfo(currentIndex).target); };
    lfoScope.getValue = [this]{ return processor.getModulationMatrix().getLfoVisualValue(currentIndex); };

    // XY
    addChildComponent(xyPad);
    addChildComponent(xyTargetXCombo); addChildComponent(xyTargetYCombo);
    addChildComponent(xyTargetXLabel); addChildComponent(xyTargetYLabel);
    xyPad.onMove = [this](float x, float y)
    {
        processor.getModulationMatrix().xy(currentIndex).x.store(x);
        processor.getModulationMatrix().xy(currentIndex).y.store(y);
    };
    xyPad.getXY = [this]
    {
        auto& s = processor.getModulationMatrix().xy(currentIndex);
        return std::pair<float,float>{ s.x.load(), s.y.load() };
    };
    xyTargetXCombo.onChange = [this]{ applyTargetFromMenu(xyTargetXCombo, processor.getModulationMatrix().xy(currentIndex).targetX); };
    xyTargetYCombo.onChange = [this]{ applyTargetFromMenu(xyTargetYCombo, processor.getModulationMatrix().xy(currentIndex).targetY); };

    rewireForCurrentSlot();
    startTimerHz(30);
}

void ModulationPanel::showLfo(bool s)
{
    juce::Component* controls[] = {
        &lfoRateCombo,
        &lfoShapeCombo,
        &lfoAmountSlider,
        &lfoTargetCombo,
        &lfoTargetLabel,
        &lfoScope
    };

    for (auto* c : controls)
        c->setVisible(s);
}
void ModulationPanel::showXy(bool s)
{
    juce::Component* controls[] = {
        &xyPad,
        &xyTargetXCombo,
        &xyTargetYCombo,
        &xyTargetXLabel,
        &xyTargetYLabel
    };

    for (auto* c : controls)
        c->setVisible(s);
}

void ModulationPanel::rewireForCurrentSlot()
{
    const bool isLfo = currentKind == SlotKind::LFO;
    showLfo(isLfo); showXy(!isLfo);

    if (isLfo)
    {
        auto& s = processor.getModulationMatrix().lfo(currentIndex);
        lfoRateCombo .setSelectedItemIndex(s.rateIndex.load(), juce::dontSendNotification);
        lfoShapeCombo.setSelectedItemIndex(s.shape.load(),     juce::dontSendNotification);
        lfoAmountSlider.setValue(s.amount.load(),              juce::dontSendNotification);
        lfoScope.shape = (ModulationMatrix::Shape) s.shape.load();
        rebuildTargetMenu(lfoTargetCombo);
    }
    else
    {
        rebuildTargetMenu(xyTargetXCombo);
        rebuildTargetMenu(xyTargetYCombo);
    }
    resized(); repaint();
}

void ModulationPanel::refresh()
{
    if (currentKind == SlotKind::LFO) rebuildTargetMenu(lfoTargetCombo);
    else { rebuildTargetMenu(xyTargetXCombo); rebuildTargetMenu(xyTargetYCombo); }
}

void ModulationPanel::rebuildTargetMenu(juce::ComboBox& cb)
{
    cb.clear(juce::dontSendNotification);
    cb.addItem("— none —", 1);
    const int objId = processor.getSelectedObjectId();
    if (objId <= 0) return;
    int id = 2;
    for (const auto& fx : processor.getFxChainForSelectedObject())
    {
        if (! fx.enabled) continue;
        for (const auto& p : fx.parameters)
        {
            const juce::String label = juce::String(fx.name) + " · " + juce::String(p.name);
            cb.addItem(label, id++);
        }
    }
}

void ModulationPanel::applyTargetFromMenu(juce::ComboBox& cb, ModulationMatrix::Target& dst)
{
    const auto txt = cb.getText();
    ModulationMatrix::Target t;
    if (cb.getSelectedId() > 1 && txt.contains(" · "))
    {
        t.objectId  = processor.getSelectedObjectId();
        t.fxName    = txt.upToFirstOccurrenceOf(" · ", false, false);
        t.paramName = txt.fromFirstOccurrenceOf(" · ", false, false);
    }
    // pick the right lock
    if (currentKind == SlotKind::LFO)
    { juce::ScopedLock sl(processor.getModulationMatrix().lfo(currentIndex).targetLock); dst = t; }
    else
    { juce::ScopedLock sl(processor.getModulationMatrix().xy (currentIndex).targetLock); dst = t; }
    repaint();
}

// --- Drag & Drop ---
juce::String ModulationPanel::makeDragPayload(int objId, const juce::String& fx, const juce::String& p)
{ return "fxparam|" + juce::String(objId) + "|" + fx + "|" + p; }

bool ModulationPanel::parseDragPayload(const juce::var& v, int& objId, juce::String& fx, juce::String& p)
{
    const juce::String s = v.toString();
    if (! s.startsWith("fxparam|")) return false;
    auto parts = juce::StringArray::fromTokens(s, "|", "");
    if (parts.size() < 4) return false;
    objId = parts[1].getIntValue(); fx = parts[2]; p = parts[3];
    return true;
}

bool ModulationPanel::isInterestedInDragSource(const SourceDetails& d)
{
    int o; juce::String f, p;
    return parseDragPayload(d.description, o, f, p);
}

void ModulationPanel::itemDropped(const SourceDetails& d)
{
    int o; juce::String f, p;
    if (! parseDragPayload(d.description, o, f, p)) return;
    ModulationMatrix::Target t { o, f, p };

    // Decide target sub-slot by drop position when in XY mode
    if (currentKind == SlotKind::LFO)
    {
        juce::ScopedLock sl(processor.getModulationMatrix().lfo(currentIndex).targetLock);
        processor.getModulationMatrix().lfo(currentIndex).target = t;
    }
    else
    {
        const bool dropOnY = d.localPosition.y > getHeight() / 2;
        auto& s = processor.getModulationMatrix().xy(currentIndex);
        juce::ScopedLock sl(s.targetLock);
        (dropOnY ? s.targetY : s.targetX) = t;
    }
    refresh(); repaint();
}

// --- Paint / layout ---
void ModulationPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF1F2227));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(juce::Colour(0xFF2A2E36));
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);

    // Show current routing names prominently
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    juce::String header;
    if (currentKind == SlotKind::LFO)
    {
        ModulationMatrix::Target t;
        { juce::ScopedLock sl(processor.getModulationMatrix().lfo(currentIndex).targetLock);
          t = processor.getModulationMatrix().lfo(currentIndex).target; }
        header = "LFO " + juce::String(currentIndex + 1) + "  →  " + t.toString();
    }
    else
    {
        ModulationMatrix::Target tx, ty;
        { juce::ScopedLock sl(processor.getModulationMatrix().xy(currentIndex).targetLock);
          tx = processor.getModulationMatrix().xy(currentIndex).targetX;
          ty = processor.getModulationMatrix().xy(currentIndex).targetY; }
        header = "XY " + juce::String(currentIndex + 1) + "   X: " + tx.toString() + "   Y: " + ty.toString();
    }
    g.drawText(header, getLocalBounds().reduced(12, 6).removeFromTop(20), juce::Justification::centredLeft);

    if (isDragOver)
    {
        g.setColour(juce::Colour(0xFF4CA8FF).withAlpha(0.25f));
        g.fillRoundedRectangle(r.reduced(2.0f), 6.0f);
    }
}

void ModulationPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    area.removeFromTop(22); // header text

    auto top = area.removeFromTop(28);
    slotCombo.setBounds(top.removeFromLeft(120));

    area.removeFromTop(8);

    if (currentKind == SlotKind::LFO)
    {
        auto row = area.removeFromTop(60);
        auto rate  = row.removeFromLeft(110); lfoRateCombo .setBounds(rate.removeFromTop(24));
        row.removeFromLeft(8);
        auto shape = row.removeFromLeft(110); lfoShapeCombo.setBounds(shape.removeFromTop(24));
        row.removeFromLeft(8);
        lfoAmountSlider.setBounds(row.removeFromLeft(64));

        area.removeFromTop(6);
        auto tgtRow = area.removeFromTop(24);
        lfoTargetLabel.setBounds(tgtRow.removeFromLeft(60));
        lfoTargetCombo.setBounds(tgtRow);

        area.removeFromTop(8);
        lfoScope.setBounds(area);
    }
    else
    {
        auto tgtX = area.removeFromTop(22);
        xyTargetXLabel.setBounds(tgtX.removeFromLeft(36)); xyTargetXCombo.setBounds(tgtX);
        auto tgtY = area.removeFromTop(22);
        xyTargetYLabel.setBounds(tgtY.removeFromLeft(36)); xyTargetYCombo.setBounds(tgtY);
        area.removeFromTop(6);
        xyPad.setBounds(area);
    }
}

void ModulationPanel::LFOScope::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF111418)); g.fillRoundedRectangle(r, 4.0f);

    juce::Path p;
    const int N = juce::jmax(2, (int) r.getWidth());
    for (int i = 0; i < N; ++i)
    {
        const float ph = (float) i / (float) (N - 1);
        float v = 0.0f;
        switch (shape)
        {
            case ModulationMatrix::Shape::Sine:     v = std::sin(ph * juce::MathConstants<float>::twoPi); break;
            case ModulationMatrix::Shape::Triangle: v = 4.0f * std::abs(ph - 0.5f) - 1.0f; break;
            case ModulationMatrix::Shape::Saw:      v = 2.0f * ph - 1.0f; break;
            case ModulationMatrix::Shape::Square:   v = ph < 0.5f ? 1.0f : -1.0f; break;
        }
        const float y = r.getCentreY() - v * (r.getHeight() * 0.4f);
        if (i == 0) p.startNewSubPath(r.getX() + (float) i, y);
        else        p.lineTo       (r.getX() + (float) i, y);
    }
    g.setColour(juce::Colour(0xFF4CA8FF));
    g.strokePath(p, juce::PathStrokeType(1.5f));

    if (getValue)
    {
        const float v = juce::jlimit(-1.0f, 1.0f, getValue());
        const float y = r.getCentreY() - v * (r.getHeight() * 0.4f);
        g.setColour(juce::Colours::white);
        g.fillEllipse(r.getRight() - 8.0f, y - 3.0f, 6.0f, 6.0f);
    }
}
