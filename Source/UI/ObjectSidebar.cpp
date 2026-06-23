#include "ObjectSidebar.h"

ObjectSidebar::ObjectSidebar(ObjectDatabase& db, std::function<void(bool)> onAutoDetect)
    : database(db), onAutoDetectClicked(std::move(onAutoDetect))
{
    setOpaque(true);

    autoDetectButton = std::make_unique<juce::TextButton>("Auto-Detect Objects");
    autoDetectButton->setClickingTogglesState(true);
    autoDetectButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A42));
    autoDetectButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFFA333));
    autoDetectButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFE8E8E8));
    autoDetectButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFF111111));
    autoDetectButton->onClick = [this]()
    {
        if (onAutoDetectClicked)
            onAutoDetectClicked(autoDetectButton->getToggleState());
    };
    addAndMakeVisible(*autoDetectButton);

    startTimerHz(10);
    rebuildRows();
}

ObjectSidebar::~ObjectSidebar() = default;

void ObjectSidebar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF2A2A30));

    // Draw header
    g.setColour(juce::Colour(0xFF1A1A1F));
    g.fillRect(getLocalBounds().removeFromTop(BUTTON_HEIGHT + PADDING * 2));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f).boldened());
    g.drawText("Objects", 6, 4, getWidth() - 12, BUTTON_HEIGHT,
               juce::Justification::centredLeft, false);

    // Light divider line
    g.setColour(juce::Colour(0x44FFFFFF));
    g.drawLine(4, BUTTON_HEIGHT + PADDING * 2, getWidth() - 4, BUTTON_HEIGHT + PADDING * 2);
}

void ObjectSidebar::timerCallback()
{
    const auto rev = database.getRevision();
    if (rev != lastKnownRevision)
    {
        lastKnownRevision = rev;
        rebuildRows();
    }
}

void ObjectSidebar::resized()
{
    auto area = getLocalBounds().reduced(PADDING);
    area.removeFromTop(BUTTON_HEIGHT + PADDING);

    if (autoDetectButton)
    {
        auto btnArea = area.removeFromTop(BUTTON_HEIGHT + 2);
        autoDetectButton->setBounds(btnArea.reduced(0, 1));
        area.removeFromTop(PADDING);
    }

    for (auto& row : rows)
    {
        auto rowArea = area.removeFromTop(ROW_HEIGHT);

        const auto buttonStrip = BUTTON_WIDTH * 3 + PADDING * 2;
        const int nameWidth = juce::jmax(40, rowArea.getWidth() - buttonStrip - PADDING);
        row.nameLabel->setBounds(rowArea.removeFromLeft(nameWidth));
        rowArea.removeFromLeft(PADDING);

        row.deleteButton->setBounds(rowArea.removeFromRight(BUTTON_WIDTH));
        rowArea.removeFromRight(PADDING);

        row.muteButton->setBounds(rowArea.removeFromRight(BUTTON_WIDTH));
        rowArea.removeFromRight(PADDING);

        row.soloButton->setBounds(rowArea.removeFromRight(BUTTON_WIDTH));
    }
}

void ObjectSidebar::rebuildRows()
{
    for (auto& row : rows)
    {
        if (row.nameLabel) removeChildComponent(row.nameLabel.get());
        if (row.soloButton) removeChildComponent(row.soloButton.get());
        if (row.muteButton) removeChildComponent(row.muteButton.get());
        if (row.deleteButton) removeChildComponent(row.deleteButton.get());
    }

    rows.clear();

    const int numObjects = database.getNumObjects();

    for (int i = 0; i < numObjects; ++i)
    {
        ObjectDatabase::ObjectMask obj;
        if (!database.getObjectCopy(i, obj))
            continue;

        ObjectRow row;
        row.name = obj.name;

        row.nameLabel = std::make_unique<juce::Label>();
        row.nameLabel->setText(row.name, juce::dontSendNotification);
        row.nameLabel->setJustificationType(juce::Justification::centredLeft);
        row.nameLabel->setEditable(false, true, false);
        row.nameLabel->setColour(juce::Label::textColourId, juce::Colour(0xFFE0E0E0));
        row.nameLabel->setTooltip(row.name);
        row.nameLabel->onTextChange = [this, i, label = row.nameLabel.get()]
        {
            const auto newName = label->getText().trim();
            if (newName.isNotEmpty())
            {
                database.setObjectName(i, newName.toStdString());
                label->setTooltip(newName);
            }
        };
        addAndMakeVisible(*row.nameLabel);

        // Solo button
        row.soloButton = std::make_unique<juce::TextButton>("S");
        row.soloButton->setButtonText("S");
        row.soloButton->setClickingTogglesState(true);
        row.soloButton->setToggleState(obj.solo, juce::dontSendNotification);
        row.soloButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A42));
        row.soloButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFF1C40F));
        row.soloButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFE8E8E8));
        row.soloButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFF111111));
        row.soloButton->onClick = [this, i, button = row.soloButton.get()]()
        {
            database.setObjectSolo(i, button->getToggleState());
        };
        addAndMakeVisible(*row.soloButton);

        // Mute button
        row.muteButton = std::make_unique<juce::TextButton>("M");
        row.muteButton->setButtonText("M");
        row.muteButton->setClickingTogglesState(true);
        row.muteButton->setToggleState(obj.mute, juce::dontSendNotification);
        row.muteButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A42));
        row.muteButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFE74C3C));
        row.muteButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFE8E8E8));
        row.muteButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));
        row.muteButton->onClick = [this, i, button = row.muteButton.get()]()
        {
            database.setObjectMute(i, button->getToggleState());
        };
        addAndMakeVisible(*row.muteButton);

        // Delete button
        row.deleteButton = std::make_unique<juce::TextButton>("X");
        row.deleteButton->setButtonText("X");
        row.deleteButton->onClick = [this, i]()
        {
            database.removeObject(i);
            refresh();
        };
        addAndMakeVisible(*row.deleteButton);

        rows.push_back(std::move(row));
    }

    resized();
    repaint();
}

void ObjectSidebar::refresh()
{
    lastKnownRevision = database.getRevision();
    rebuildRows();
}
