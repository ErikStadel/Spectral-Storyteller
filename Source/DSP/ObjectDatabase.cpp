#include "ObjectDatabase.h"

ObjectDatabase::ObjectDatabase()
{
}

bool ObjectDatabase::addObject(const std::string& name)
{
    juce::ScopedLock lock_(lock);

    if (objects.size() >= MAX_OBJECTS)
        return false;

    ObjectMask newObject;
    newObject.name = name.empty() ? ("Object_" + std::to_string(objects.size())) : name;
    newObject.mask.fill(false);

    objects.push_back(newObject);
    return true;
}

void ObjectDatabase::removeObject(int objectIndex)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects.erase(objects.begin() + objectIndex);
}

ObjectDatabase::ObjectMask* ObjectDatabase::getObject(int objectIndex)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return nullptr;

    return &objects[objectIndex];
}

const ObjectDatabase::ObjectMask* ObjectDatabase::getObject(int objectIndex) const
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return nullptr;

    return &objects[objectIndex];
}

int ObjectDatabase::getNumObjects() const
{
    juce::ScopedLock lock_(lock);
    return static_cast<int>(objects.size());
}

void ObjectDatabase::clear()
{
    juce::ScopedLock lock_(lock);
    objects.clear();
}

void ObjectDatabase::setObjectMask(int objectIndex, const std::array<bool, NUM_BINS>& mask)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].mask = mask;
}

std::array<bool, ObjectDatabase::NUM_BINS> ObjectDatabase::getCombinedMask() const
{
    juce::ScopedLock lock_(lock);

    std::array<bool, NUM_BINS> combined;
    combined.fill(false);

    bool anyActive = false;
    for (const auto& obj : objects)
    {
        if (!obj.mute)
        {
            anyActive = true;
            break;
        }
    }

    if (!anyActive)
    {
        combined.fill(true);
        return combined;
    }

    // Combine all non-muted masks
    for (const auto& obj : objects)
    {
        if (!obj.mute)
        {
            for (int i = 0; i < NUM_BINS; ++i)
                combined[i] = combined[i] || obj.mask[i];
        }
    }

    return combined;
}

void ObjectDatabase::setObjectSolo(int objectIndex, bool solo)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].solo = solo;
}

void ObjectDatabase::setObjectMute(int objectIndex, bool mute)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].mute = mute;
}
