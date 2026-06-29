#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/ModulationMatrix.h"

class PluginProcessor;

class ModulationPanel : public juce::Component,
                        public juce::DragAndDropTarget,
                        private juce::Timer
{
public:
    explicit ModulationPanel(PluginProcessor& p);
    void refresh();                          // call when selected object / FX chain changes

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    /** Encode/decode helpers shared with DraggableFxHeader. */
    static juce::String makeDragPayload(int objectId, const juce::String& fx, const juce::String& param);
    static bool parseDragPayload(const juce::var& v, int& objId, juce::String& fx, juce::String& param);

private:
    enum class SlotKind { LFO, XY };
    void timerCallback() override { repaint(); }
    void rebuildTargetMenu(juce::ComboBox& cb);
    void applyTargetFromMenu(juce::ComboBox& cb, ModulationMatrix::Target& dst);

    PluginProcessor& processor;

    // Slot selector
    juce::ComboBox slotCombo;                // "LFO 1..3" / "XY 1..3"
    SlotKind currentKind = SlotKind::LFO;
    int      currentIndex = 0;

    // LFO controls
    juce::ComboBox lfoRateCombo, lfoShapeCombo, lfoTargetCombo;
    juce::Slider   lfoAmountSlider;
    juce::Label    lfoTargetLabel { {}, "Target" };

    // XY controls
    juce::ComboBox xyTargetXCombo, xyTargetYCombo;
    juce::Label    xyTargetXLabel { {}, "X →" }, xyTargetYLabel { {}, "Y →" };
    struct XYPad : juce::Component
    {
        std::function<void(float,float)> onMove;
        std::function<std::pair<float,float>()> getXY;
        void mouseDown(const juce::MouseEvent& e) override { mouseDrag(e); }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            const float x = juce::jlimit(0.0f, 1.0f, (float) e.position.x / (float) juce::jmax(1, getWidth()));
            const float y = juce::jlimit(0.0f, 1.0f, 1.0f - (float) e.position.y / (float) juce::jmax(1, getHeight()));
            if (onMove) onMove(x, y);
            repaint();
        }
        void paint(juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour(juce::Colour(0xFF111418)); g.fillRoundedRectangle(r, 4.0f);
            g.setColour(juce::Colour(0xFF2A2E36));
            for (int i = 1; i < 4; ++i)
            {
                const float xv = r.getX() + r.getWidth()  * i / 4.0f;
                const float yv = r.getY() + r.getHeight() * i / 4.0f;
                g.drawLine(xv, r.getY(), xv, r.getBottom());
                g.drawLine(r.getX(), yv, r.getRight(), yv);
            }
            if (getXY)
            {
                auto [x,y] = getXY();
                const float px = r.getX() + x * r.getWidth();
                const float py = r.getY() + (1.0f - y) * r.getHeight();
                g.setColour(juce::Colour(0xFF4CA8FF));
                g.fillEllipse(px - 7, py - 7, 14, 14);
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                g.drawEllipse(px - 7, py - 7, 14, 14, 1.2f);
            }
        }
    } xyPad;

    // Visual LFO scope
    struct LFOScope : juce::Component
    {
        std::function<float()> getValue;
        ModulationMatrix::Shape shape = ModulationMatrix::Shape::Sine;
        void paint(juce::Graphics& g) override;
    } lfoScope;

    // dnd drop overlay
    bool isDragOver = false;

    void showLfo(bool show);
    void showXy(bool show);
    void rewireForCurrentSlot();
};
