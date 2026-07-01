#include "ObjectSidebar.h"

namespace
{
class FxRowButton : public juce::Button
{
public:
    FxRowButton() : juce::Button("FX") {}

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        const auto base = shouldDrawButtonAsDown ? juce::Colour(0xFFE0A96D).withAlpha(0.4f)
                         : shouldDrawButtonAsHighlighted ? juce::Colour(0xFFE0A96D).withAlpha(0.2f)
                         : juce::Colour(0xFF292524);

        g.setColour(base);
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(juce::Colour(0xFF44403C));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        g.setColour(juce::Colour(0xFFE0A96D));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawFittedText("FX", getLocalBounds(), juce::Justification::centred, 1);
    }
};

class FxOverlayComponent : public juce::Component
{
public:
    FxOverlayComponent(ObjectDatabase& databaseRef,
                       int objectIdRef,
                       std::function<float()> getTransientThresholdDbCallback,
                       std::function<void(float)> setTransientThresholdDbCallback,
                       std::function<void()> onCloseCallback)
        : database(databaseRef),
          objectId(objectIdRef),
          getTransientThresholdDb(std::move(getTransientThresholdDbCallback)),
          setTransientThresholdDb(std::move(setTransientThresholdDbCallback)),
          onClose(std::move(onCloseCallback))
    {
        setOpaque(true);
        setInterceptsMouseClicks(true, true);

        addAndMakeVisible(delayButton);
        addAndMakeVisible(filterButton);
        addAndMakeVisible(transientThresholdLabel);
        addAndMakeVisible(transientThresholdSlider);
        addAndMakeVisible(closeButton);

        delayButton.setButtonText("Delay");
        filterButton.setButtonText("Filter");
        closeButton.setButtonText("Close");

        delayButton.setClickingTogglesState(true);
        filterButton.setClickingTogglesState(true);

        transientThresholdLabel.setText("Threshold", juce::dontSendNotification);
        transientThresholdLabel.setJustificationType(juce::Justification::centredLeft);
        transientThresholdSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        transientThresholdSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 18);
        transientThresholdSlider.setRange(-60.0, 0.0, 0.1);
        transientThresholdSlider.setTextValueSuffix(" dB");
        transientThresholdSlider.onValueChange = [this]()
        {
            if (setTransientThresholdDb)
                setTransientThresholdDb(static_cast<float>(transientThresholdSlider.getValue()));
        };

        refreshFromDatabase();

        delayButton.onClick = [this]
        {
            database.addOrEnableObjectFx(objectId, "Delay");
            database.setObjectFxEnabled(objectId, "Delay", delayButton.getToggleState());
        };

        filterButton.onClick = [this]
        {
            database.addOrEnableObjectFx(objectId, "Filter");
            database.setObjectFxEnabled(objectId, "Filter", filterButton.getToggleState());
        };

        closeButton.onClick = [this]
        {
            if (onClose)
                onClose();
        };
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF1C1917));
        g.setColour(juce::Colour(0xAAE0A96D));
        g.drawRect(getLocalBounds(), 1);
    }

    void refreshFromDatabase()
    {
        isTransientObject = false;
        {
            const int n = database.getNumObjects();
            for (int i = 0; i < n; ++i)
            {
                ObjectDatabase::ObjectMask obj;
                if (!database.getObjectCopy(i, obj) || obj.id != objectId)
                    continue;

                isTransientObject = juce::String(obj.name).equalsIgnoreCase("Transients");
                break;
            }
        }

        const auto chain = database.getObjectFxChain(objectId);
        const bool delayEnabled = isFxEnabled(chain, "Delay");
        const bool filterEnabled = isFxEnabled(chain, "Filter");
        delayButton.setToggleState(delayEnabled, juce::dontSendNotification);
        filterButton.setToggleState(filterEnabled, juce::dontSendNotification);

        transientThresholdLabel.setVisible(isTransientObject);
        transientThresholdSlider.setVisible(isTransientObject);

        if (isTransientObject && getTransientThresholdDb)
        {
            transientThresholdSlider.setValue(getTransientThresholdDb(), juce::dontSendNotification);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto top = area.removeFromTop(24);
        top.removeFromRight(50);
        closeButton.setBounds(top.removeFromRight(48));
        area.removeFromTop(4);
        delayButton.setBounds(area.removeFromTop(26));
        area.removeFromTop(4);
        filterButton.setBounds(area.removeFromTop(26));

        if (isTransientObject)
        {
            auto bottomZone = area.removeFromBottom(56);
            transientThresholdLabel.setBounds(bottomZone.removeFromTop(18));
            bottomZone.removeFromTop(4);
            transientThresholdSlider.setBounds(bottomZone);
        }
    }

private:
    bool isFxEnabled(const std::vector<ObjectDatabase::FXModule>& chain, const juce::String& name) const
    {
        for (const auto& fx : chain)
        {
            if (juce::String(fx.name).equalsIgnoreCase(name))
                return fx.enabled;
        }

        return false;
    }

    ObjectDatabase& database;
    int objectId = -1;
    bool isTransientObject = false;
    std::function<float()> getTransientThresholdDb;
    std::function<void(float)> setTransientThresholdDb;
    std::function<void()> onClose;
    juce::TextButton delayButton;
    juce::TextButton filterButton;
    juce::Label transientThresholdLabel;
    juce::Slider transientThresholdSlider;
    juce::TextButton closeButton;
};
} // namespace

ObjectSidebar::ObjectSidebar(ObjectDatabase& db,
                             std::function<void(bool)> onAutoDetect,
                             std::function<juce::String()> statusProvider,
                             std::function<void(int)> onSelectedChanged,
                             std::function<void(const juce::String&, const juce::File&)> onCreateTransformObjectCallback,
                             std::function<int()> onCreateTransientObjectCallback,
                             std::function<float()> getTransientThresholdDbCallback,
                             std::function<void(float)> setTransientThresholdDbCallback)
    : database(db),
      onAutoDetectClicked(std::move(onAutoDetect)),
      autoDetectStatusProvider(std::move(statusProvider)),
      onSelectedObjectChanged(std::move(onSelectedChanged)),
      onCreateTransformObject(std::move(onCreateTransformObjectCallback)),
      onCreateTransientObject(std::move(onCreateTransientObjectCallback)),
      getTransientThresholdDb(std::move(getTransientThresholdDbCallback)),
      setTransientThresholdDb(std::move(setTransientThresholdDbCallback))
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    autoDetectButton = std::make_unique<juce::TextButton>("Auto-Detect");
    autoDetectButton->setClickingTogglesState(true);
    autoDetectButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF44403C));
    autoDetectButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFA333));
    autoDetectButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFE7E5E3));
    autoDetectButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFF111111));
    autoDetectButton->setTooltip("Neu-Detection fuer Record-aktive Objekte starten");
    autoDetectButton->onClick = [this]()
    {
        if (onAutoDetectClicked)
            onAutoDetectClicked(autoDetectButton->getToggleState());
    };
    addAndMakeVisible(*autoDetectButton);

    transformButton = std::make_unique<juce::TextButton>("+");
    transformButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF44403C));
    transformButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF4A76B7));
    transformButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFE7E5E3));
    transformButton->setTooltip("Objekt-Menue (Transform / Transient)");
    transformButton->onClick = [this]()
    {
        showTransformMenu();
    };
    addAndMakeVisible(*transformButton);

    rowScrollBar.setAutoHide(false);
    rowScrollBar.addListener(this);
    addAndMakeVisible(rowScrollBar);

    startTimerHz(10);
    rebuildRows();
}

ObjectSidebar::~ObjectSidebar()
{
    rowScrollBar.removeListener(this);
}

void ObjectSidebar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0x99292524));

    // Header area
    auto headerArea = getLocalBounds().removeFromTop(HEADER_BUTTON_HEIGHT + PADDING * 2);
    g.setColour(juce::Colour(0xFF292524));
    g.fillRect(headerArea);

    g.setColour(juce::Colour(0xFF44403C));
    g.drawHorizontalLine(headerArea.getBottom() - 1,
                          static_cast<float>(headerArea.getX()),
                          static_cast<float>(headerArea.getRight()));

    g.setColour(juce::Colour(0xFFA8A29E));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("OBJEKT-DATENBANK", 10, PADDING, getWidth() - 20, HEADER_BUTTON_HEIGHT,
               juce::Justification::centredLeft, false);

    // Draw object cards
    auto rowsArea = getRowsArea(false);

    if (selectedRow >= 0 && selectedRow < static_cast<int>(rows.size()))
    {
        const int visibleRow = selectedRow - rowScrollOffset;
        if (visibleRow >= 0 && visibleRow < getMaxVisibleRows())
        {
            auto sel = rowsArea.withTrimmedTop(visibleRow * ROW_HEIGHT).removeFromTop(ROW_HEIGHT).reduced(2, 2);
            g.setColour(juce::Colour(0xFF292524));
            g.fillRoundedRectangle(sel.toFloat(), 6.0f);
            g.setColour(juce::Colour(0x4DE0A96D));
            g.drawRoundedRectangle(sel.toFloat(), 6.0f, 1.0f);
        }
    }

    // Unselected card backgrounds
    const int visibleRows = getMaxVisibleRows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const int visibleRow = i - rowScrollOffset;
        if (visibleRow < 0 || visibleRow >= visibleRows)
            continue;
        if (i == selectedRow)
            continue;

        auto cardArea = rowsArea.withTrimmedTop(visibleRow * ROW_HEIGHT).removeFromTop(ROW_HEIGHT).reduced(2, 2);
        g.setColour(juce::Colour(0xB3292524));
        g.fillRoundedRectangle(cardArea.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xCC44403C));
        g.drawRoundedRectangle(cardArea.toFloat(), 6.0f, 1.0f);
    }

    // Object status dots and IDs
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const int visibleRow = i - rowScrollOffset;
        if (visibleRow < 0 || visibleRow >= visibleRows)
            continue;

        auto cardArea = rowsArea.withTrimmedTop(visibleRow * ROW_HEIGHT).removeFromTop(ROW_HEIGHT).reduced(2, 2);

        // Status dot
        const auto& row = rows[static_cast<size_t>(i)];
        juce::Colour dotColour;
        const juce::String nameLower = row.name.toLowerCase();
        if (nameLower.contains("transient"))
            dotColour = juce::Colour(0xFFFF5252);
        else if (nameLower.contains("tonal"))
            dotColour = juce::Colour(0xFF4AA3FF);
        else if (nameLower.contains("noise"))
            dotColour = juce::Colour(0xFF4FD16A);
        else
            dotColour = juce::Colour(0xFFA8A29E);

        const float dotX = static_cast<float>(cardArea.getX()) + 10.0f;
        const float dotY = static_cast<float>(cardArea.getY()) + 12.0f;
        g.setColour(dotColour);
        g.fillEllipse(dotX - 3.0f, dotY - 3.0f, 6.0f, 6.0f);

        // Object ID
        g.setColour(juce::Colour(0xFF78716C));
        g.setFont(juce::Font(9.0f));
        g.drawText("ID: #" + juce::String(row.objectId).paddedLeft('0', 2),
                   cardArea.withTrimmedRight(6), juce::Justification::topRight, false);
    }

    if (dragHoverRow >= 0 && dragHoverRow < static_cast<int>(rows.size()))
    {
        const int visibleRow = dragHoverRow - rowScrollOffset;
        if (visibleRow >= 0 && visibleRow < getMaxVisibleRows())
        {
            auto hover = rowsArea.withTrimmedTop(visibleRow * ROW_HEIGHT).removeFromTop(ROW_HEIGHT).reduced(3, 1);
            g.setColour(juce::Colour(0x88FFA333));
            g.drawRoundedRectangle(hover.toFloat(), 5.0f, 1.0f);
        }
    }
}

void ObjectSidebar::showFxOverlay(int objectId)
{
    fxOverlay = std::make_unique<FxOverlayComponent>(
        database,
        objectId,
        getTransientThresholdDb,
        setTransientThresholdDb,
        [this]() { hideFxOverlay(); });

    fxOverlay->setBounds(getLocalBounds().reduced(PADDING));
    addAndMakeVisible(*fxOverlay);
    fxOverlay->toFront(false);
    repaint();
}

void ObjectSidebar::showTransformMenu()
{
    juce::PopupMenu menu;
    enum MenuIds
    {
        wtSine = 1,
        wtSaw,
        wtSquare,
        wtTriangle,
        loadFile,
        createTransient
    };

    menu.addSectionHeader("Transform");

    menu.addItem(wtSine, "Wavetable: Sine");
    menu.addItem(wtSaw, "Wavetable: Saw");
    menu.addItem(wtSquare, "Wavetable: Square");
    menu.addItem(wtTriangle, "Wavetable: Triangle");
    menu.addSeparator();
    menu.addItem(loadFile, "Datei laden...");
    menu.addSeparator();
    menu.addItem(createTransient, "Transient");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(transformButton.get()),
                       [this](int result)
                       {
                           if (result == 0)
                               return;

                           if (result == createTransient)
                           {
                               if (onCreateTransientObject)
                                   onCreateTransientObject();
                               return;
                           }

                           if (result == loadFile)
                           {
                               if (onCreateTransformObject == nullptr)
                                   return;
                               transformFileChooser = std::make_unique<juce::FileChooser>("Transform-Datei laden",
                                                                                           juce::File(),
                                                                                           "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                               transformFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                                  [this](const juce::FileChooser& chooser)
                                                                  {
                                                                      const auto file = chooser.getResult();
                                                                      if (file.existsAsFile() && onCreateTransformObject)
                                                                          onCreateTransformObject({}, file);
                                                                  });
                               return;
                           }

                           juce::String preset;
                           if (result == wtSine) preset = "Sine";
                           if (result == wtSaw) preset = "Saw";
                           if (result == wtSquare) preset = "Square";
                           if (result == wtTriangle) preset = "Triangle";

                           if (preset.isNotEmpty() && onCreateTransformObject != nullptr)
                               onCreateTransformObject(preset, juce::File());
                       });
}

void ObjectSidebar::hideFxOverlay()
{
    if (fxOverlay)
    {
        removeChildComponent(fxOverlay.get());
        fxOverlay.reset();
        repaint();
    }
}

void ObjectSidebar::timerCallback()
{
    if (autoDetectButton != nullptr)
    {
        juce::String text = "Auto-Detect";
        if (autoDetectStatusProvider)
        {
            const auto suffix = autoDetectStatusProvider();
            if (suffix.isNotEmpty())
                text = "Auto-Detect " + suffix;
        }

        if (autoDetectButton->getButtonText() != text)
            autoDetectButton->setButtonText(text);
    }

    const auto rev = database.getRevision();
    if (rev != lastKnownRevision)
    {
        lastKnownRevision = rev;
        rebuildRows();
    }

    if (fxOverlay)
    {
        if (auto* overlay = dynamic_cast<FxOverlayComponent*>(fxOverlay.get()))
            overlay->refreshFromDatabase();
    }
}

void ObjectSidebar::resized()
{
    auto area = getLocalBounds().reduced(PADDING);
    area.removeFromTop(HEADER_BUTTON_HEIGHT + PADDING);

    // Header buttons: Auto-Detect and + side by side
    auto btnRow = area.removeFromTop(HEADER_BUTTON_HEIGHT);
    if (transformButton)
        transformButton->setBounds(btnRow.removeFromRight(HEADER_BUTTON_HEIGHT).reduced(0, 1));
    btnRow.removeFromRight(4);
    if (autoDetectButton)
        autoDetectButton->setBounds(btnRow.reduced(0, 1));
    area.removeFromTop(PADDING);

    updateScrollBar();

    const int scrollBarWidth = 12;
    auto contentArea = area;
    if (rowScrollBar.isVisible())
    {
        rowScrollBar.setBounds(contentArea.removeFromRight(scrollBarWidth));
        contentArea.removeFromRight(2);
    }

    const int visibleRows = getMaxVisibleRows();
    for (int rowIndex = 0; rowIndex < static_cast<int>(rows.size()); ++rowIndex)
    {
        auto& row = rows[static_cast<size_t>(rowIndex)];
        const int visibleRow = rowIndex - rowScrollOffset;
        const bool rowIsVisible = visibleRow >= 0 && visibleRow < visibleRows;

        row.recordButton->setVisible(rowIsVisible);
        row.engageButton->setVisible(rowIsVisible);
        row.nameLabel->setVisible(rowIsVisible);
        row.fxButton->setVisible(rowIsVisible);
        row.soloButton->setVisible(rowIsVisible);
        row.muteButton->setVisible(rowIsVisible);

        if (!rowIsVisible)
            continue;

        auto rowArea = contentArea.withTrimmedTop(visibleRow * ROW_HEIGHT).removeFromTop(ROW_HEIGHT).reduced(4, 4);

        // Card layout: top line has status dot space + name, bottom line has buttons
        auto topLine = rowArea.removeFromTop(16);
        topLine.removeFromLeft(18); // space for dot
        row.nameLabel->setBounds(topLine);

        rowArea.removeFromTop(4);
        auto bottomLine = rowArea.removeFromTop(LEFT_TOGGLE_H);

        // Left buttons: Eng, Rec, FX
        row.engageButton->setBounds(bottomLine.removeFromLeft(LEFT_TOGGLE_W));
        bottomLine.removeFromLeft(3);
        row.recordButton->setBounds(bottomLine.removeFromLeft(LEFT_TOGGLE_W));
        bottomLine.removeFromLeft(3);
        row.fxButton->setBounds(bottomLine.removeFromLeft(FX_BUTTON_W));

        // Right buttons: S, M
        auto rightPart = bottomLine;
        row.muteButton->setBounds(rightPart.removeFromRight(RIGHT_BUTTON_W + 2).removeFromTop(RIGHT_BUTTON_H));
        rightPart.removeFromRight(3);
        row.soloButton->setBounds(rightPart.removeFromRight(RIGHT_BUTTON_W + 2).removeFromTop(RIGHT_BUTTON_H));
    }

    if (fxOverlay)
    {
        fxOverlay->setBounds(getLocalBounds().reduced(PADDING));
        fxOverlay->toFront(false);
    }
}

void ObjectSidebar::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int maxOffset = getMaxRowScrollOffset();
    if (maxOffset <= 0)
        return;

    int nextOffset = rowScrollOffset;
    if (wheel.deltaY < -0.01f)
        ++nextOffset;
    else if (wheel.deltaY > 0.01f)
        --nextOffset;

    nextOffset = juce::jlimit(0, maxOffset, nextOffset);
    if (nextOffset != rowScrollOffset)
    {
        rowScrollOffset = nextOffset;
        updateScrollBar();
        resized();
        repaint();
    }
}

void ObjectSidebar::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved != &rowScrollBar)
        return;

    const int clampedOffset = juce::jlimit(0, getMaxRowScrollOffset(), juce::roundToInt(newRangeStart));
    if (clampedOffset != rowScrollOffset)
    {
        rowScrollOffset = clampedOffset;
        resized();
        repaint();
    }
}

int ObjectSidebar::getMaxVisibleRows() const
{
    const auto rowsArea = getRowsArea(false);
    return juce::jmax(1, rowsArea.getHeight() / juce::jmax(1, ROW_HEIGHT));
}

int ObjectSidebar::getMaxRowScrollOffset() const
{
    return juce::jmax(0, static_cast<int>(rows.size()) - getMaxVisibleRows());
}

juce::Rectangle<int> ObjectSidebar::getRowsArea(bool includeScrollBarSpace) const
{
    auto area = getLocalBounds().reduced(PADDING);
    area.removeFromTop(HEADER_BUTTON_HEIGHT + PADDING);
    area.removeFromTop(HEADER_BUTTON_HEIGHT + PADDING);

    if (!includeScrollBarSpace && rowScrollBar.isVisible())
    {
        area.removeFromRight(12);
        area.removeFromRight(2);
    }

    return area;
}

void ObjectSidebar::updateScrollBar()
{
    const int visibleRows = getMaxVisibleRows();
    const int maxOffset = juce::jmax(0, static_cast<int>(rows.size()) - visibleRows);
    rowScrollOffset = juce::jlimit(0, maxOffset, rowScrollOffset);

    const bool shouldShow = maxOffset > 0;
    rowScrollBar.setVisible(shouldShow);

    if (shouldShow)
    {
        rowScrollBar.setRangeLimits(0.0, static_cast<double>(rows.size()));
        rowScrollBar.setCurrentRange(static_cast<double>(rowScrollOffset), static_cast<double>(visibleRows));
    }
}

void ObjectSidebar::rebuildRows()
{
    for (auto& row : rows)
    {
        if (row.recordButton) removeChildComponent(row.recordButton.get());
        if (row.engageButton) removeChildComponent(row.engageButton.get());
        if (row.nameLabel) removeChildComponent(row.nameLabel.get());
        if (row.fxButton) removeChildComponent(row.fxButton.get());
        if (row.soloButton) removeChildComponent(row.soloButton.get());
        if (row.muteButton) removeChildComponent(row.muteButton.get());
    }

    rows.clear();

    const int numObjects = database.getNumObjects();

    for (int i = 0; i < numObjects; ++i)
    {
        ObjectDatabase::ObjectMask obj;
        if (!database.getObjectCopy(i, obj))
            continue;

        ObjectRow row;
        row.objectId = obj.id;
        row.name = obj.name;

        // Eng button
        row.engageButton = std::make_unique<juce::TextButton>("Eng");
        row.engageButton->setClickingTogglesState(true);
        row.engageButton->setToggleState(obj.engaged, juce::dontSendNotification);
        row.engageButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF292524));
        row.engageButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0x6622D3EE));
        row.engageButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFA8A29E));
        row.engageButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFF22D3EE));
        row.engageButton->setTooltip("Objekt auf den Sound anwenden");
        row.engageButton->onClick = [this, objectId = obj.id, button = row.engageButton.get()]()
        {
            const int index = database.getObjectIndexById(objectId);
            if (index >= 0)
                database.setObjectEngaged(index, button->getToggleState());
        };
        row.engageButton->addMouseListener(this, true);
        addAndMakeVisible(*row.engageButton);

        // Rec button
        row.recordButton = std::make_unique<juce::TextButton>("Rec");
        row.recordButton->setClickingTogglesState(true);
        row.recordButton->setToggleState(obj.recordEnabled, juce::dontSendNotification);
        row.recordButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF292524));
        row.recordButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFDD5555));
        row.recordButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFA8A29E));
        row.recordButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        row.recordButton->setTooltip("Objekt bei Auto-Detect neu berechnen");
        row.recordButton->onClick = [this, objectId = obj.id, button = row.recordButton.get()]()
        {
            const int index = database.getObjectIndexById(objectId);
            if (index >= 0)
                database.setObjectRecordEnabled(index, button->getToggleState());
        };
        row.recordButton->addMouseListener(this, true);
        addAndMakeVisible(*row.recordButton);

        row.nameLabel = std::make_unique<juce::Label>();
        row.nameLabel->setText(row.name, juce::dontSendNotification);
        row.nameLabel->setJustificationType(juce::Justification::centredLeft);
        row.nameLabel->setEditable(false, true, false);
        row.nameLabel->setColour(juce::Label::textColourId, juce::Colour(0xFFF5F5F4));
        row.nameLabel->setFont(juce::Font(11.0f, juce::Font::bold));
        row.nameLabel->setTooltip(row.name);
        row.nameLabel->onTextChange = [this, objectId = obj.id, label = row.nameLabel.get()]
        {
            const auto newName = label->getText().trim();
            if (newName.isNotEmpty())
            {
                const int index = database.getObjectIndexById(objectId);
                if (index >= 0)
                    database.setObjectName(index, newName.toStdString());
                label->setTooltip(newName);
            }
        };
        row.nameLabel->addMouseListener(this, true);
        addAndMakeVisible(*row.nameLabel);

        row.fxButton = std::make_unique<FxRowButton>();
        row.fxButton->setTooltip("Effekte fuer dieses Objekt");
        row.fxButton->onClick = [this, objectId = obj.id]
        {
            showFxOverlay(objectId);
        };
        row.fxButton->addMouseListener(this, true);
        addAndMakeVisible(*row.fxButton);

        // Solo button
        row.soloButton = std::make_unique<juce::TextButton>("S");
        row.soloButton->setButtonText("S");
        row.soloButton->setClickingTogglesState(true);
        row.soloButton->setToggleState(obj.solo, juce::dontSendNotification);
        row.soloButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF0C0A09));
        row.soloButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF4AA3FF));
        row.soloButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFA8A29E));
        row.soloButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        row.soloButton->setTooltip("Solo");
        row.soloButton->onClick = [this, objectId = obj.id, button = row.soloButton.get()]()
        {
            const int index = database.getObjectIndexById(objectId);
            if (index >= 0)
                database.setObjectSolo(index, button->getToggleState());
        };
        row.soloButton->addMouseListener(this, true);
        addAndMakeVisible(*row.soloButton);

        // Mute button
        row.muteButton = std::make_unique<juce::TextButton>("M");
        row.muteButton->setButtonText("M");
        row.muteButton->setClickingTogglesState(true);
        row.muteButton->setToggleState(obj.mute, juce::dontSendNotification);
        row.muteButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF0C0A09));
        row.muteButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFF5252));
        row.muteButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFA8A29E));
        row.muteButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        row.muteButton->setTooltip("Mute");
        row.muteButton->onClick = [this, objectId = obj.id, button = row.muteButton.get()]()
        {
            const int index = database.getObjectIndexById(objectId);
            if (index >= 0)
                database.setObjectMute(index, button->getToggleState());
        };
        row.muteButton->addMouseListener(this, true);
        addAndMakeVisible(*row.muteButton);

        rows.push_back(std::move(row));
    }

    const int selectedId = database.getSelectedObjectId();
    selectedRow = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        if (rows[static_cast<size_t>(i)].objectId == selectedId)
        {
            selectedRow = i;
            break;
        }
    }

    if (selectedRow < 0 && !rows.empty())
    {
        selectedRow = 0;
        database.setSelectedObjectId(rows.front().objectId);
    }

    rowScrollOffset = juce::jlimit(0, getMaxRowScrollOffset(), rowScrollOffset);
    if (selectedRow >= 0)
    {
        if (selectedRow < rowScrollOffset)
            rowScrollOffset = selectedRow;
        else if (selectedRow >= rowScrollOffset + getMaxVisibleRows())
            rowScrollOffset = juce::jmax(0, selectedRow - getMaxVisibleRows() + 1);
    }

    resized();
    if (fxOverlay)
        fxOverlay->toFront(false);
    repaint();
}

void ObjectSidebar::refresh()
{
    lastKnownRevision = database.getRevision();
    rebuildRows();
}

int ObjectSidebar::rowFromPoint(juce::Point<int> p) const
{
    auto area = getRowsArea(false);

    if (!area.contains(p))
        return -1;

    const int localY = p.y - area.getY();
    const int idx = (localY / ROW_HEIGHT) + rowScrollOffset;
    return (idx >= 0 && idx < static_cast<int>(rows.size())) ? idx : -1;
}

int ObjectSidebar::rowFromComponent(const juce::Component* c) const
{
    if (c == nullptr)
        return -1;

    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const auto& row = rows[static_cast<size_t>(i)];
        if (c == row.recordButton.get() || c == row.engageButton.get() || c == row.nameLabel.get()
            || c == row.fxButton.get() || c == row.soloButton.get() || c == row.muteButton.get())
            return i;
    }

    return -1;
}

int ObjectSidebar::rowFromMouseEvent(const juce::MouseEvent& e) const
{
    const int fromComp = rowFromComponent(e.originalComponent);
    if (fromComp >= 0)
        return fromComp;

    return rowFromPoint(e.position.toInt());
}

void ObjectSidebar::mouseDown(const juce::MouseEvent& e)
{
    if (fxOverlay && fxOverlay->getBounds().contains(e.getPosition()))
        return;

    const auto* clickedComponent = e.originalComponent;
    const bool clickedRowControl = [this, clickedComponent]() -> bool
    {
        if (clickedComponent == nullptr)
            return false;

        for (const auto& row : rows)
        {
            if (clickedComponent == row.recordButton.get() || clickedComponent == row.engageButton.get()
                || clickedComponent == row.fxButton.get() || clickedComponent == row.soloButton.get()
                || clickedComponent == row.muteButton.get())
                return true;
        }

        return false;
    }();

    const int row = rowFromMouseEvent(e);
    if (row < 0)
        return;

    if (!clickedRowControl)
    {
        selectedRow = row;
        if (row >= 0 && row < static_cast<int>(rows.size()))
        {
            const int objectId = rows[static_cast<size_t>(row)].objectId;
            database.setSelectedObjectId(objectId);
            if (onSelectedObjectChanged)
                onSelectedObjectChanged(objectId);
        }

        dragStartRow = row;
        dragHoverRow = row;
        grabKeyboardFocus();
    }

    if (e.mods.isAltDown() && e.mods.isLeftButtonDown())
    {
        database.removeObject(row);
        const int selectedId = database.getSelectedObjectId();
        selectedRow = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        {
            if (rows[static_cast<size_t>(i)].objectId == selectedId)
            {
                selectedRow = i;
                break;
            }
        }

        if (onSelectedObjectChanged)
            onSelectedObjectChanged(selectedId);
        dragStartRow = -1;
        dragHoverRow = -1;
    }

    repaint();
}

void ObjectSidebar::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStartRow < 0)
        return;

    const int hover = rowFromMouseEvent(e);
    if (hover >= 0)
    {
        dragHoverRow = hover;
        repaint();
    }
}

void ObjectSidebar::mouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);

    if (dragStartRow >= 0 && dragHoverRow >= 0 && dragHoverRow != dragStartRow)
    {
        int selectedIdBefore = -1;
        if (selectedRow >= 0 && selectedRow < static_cast<int>(rows.size()))
            selectedIdBefore = rows[static_cast<size_t>(selectedRow)].objectId;

        database.moveObject(dragStartRow, dragHoverRow);

        if (selectedIdBefore >= 0)
            database.setSelectedObjectId(selectedIdBefore);

        const int selectedId = database.getSelectedObjectId();
        selectedRow = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        {
            if (rows[static_cast<size_t>(i)].objectId == selectedId)
            {
                selectedRow = i;
                break;
            }
        }

        if (onSelectedObjectChanged)
            onSelectedObjectChanged(selectedId);
    }

    dragStartRow = -1;
    dragHoverRow = -1;
    repaint();
}

bool ObjectSidebar::keyPressed(const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && selectedRow >= 0 && selectedRow < database.getNumObjects())
    {
        database.removeObject(selectedRow);
        const int selectedId = database.getSelectedObjectId();
        selectedRow = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        {
            if (rows[static_cast<size_t>(i)].objectId == selectedId)
            {
                selectedRow = i;
                break;
            }
        }

        if (onSelectedObjectChanged)
            onSelectedObjectChanged(selectedId);
        repaint();
        return true;
    }

    return false;
}
