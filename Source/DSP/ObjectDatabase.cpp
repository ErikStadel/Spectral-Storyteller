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
    ++revision;
    return true;
}

void ObjectDatabase::removeObject(int objectIndex)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects.erase(objects.begin() + objectIndex);
    ++revision;
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

bool ObjectDatabase::getObjectCopy(int objectIndex, ObjectMask& outObject) const
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return false;

    outObject = objects[objectIndex];
    return true;
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
    ++revision;
}

void ObjectDatabase::setObjectMask(int objectIndex, const std::array<bool, NUM_BINS>& mask)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].mask = mask;
    ++revision;
}

void ObjectDatabase::setObjectColor(int objectIndex, int argbColour)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].color = argbColour;
    ++revision;
}

std::array<bool, ObjectDatabase::NUM_BINS> ObjectDatabase::getAudioMask() const
{
    juce::ScopedLock lock_(lock);

    std::array<bool, NUM_BINS> mask;

    // Check if any object is solo
    bool anySolo = false;
    for (const auto& obj : objects)
    {
        if (obj.solo)
        {
            anySolo = true;
            break;
        }
    }

    if (anySolo)
    {
        // Solo mode: block everything, then open ONLY solo'd bins
        mask.fill(false);
        for (const auto& obj : objects)
        {
            if (obj.solo)
            {
                for (int i = 0; i < NUM_BINS; ++i)
                    if (obj.mask[i]) mask[i] = true;
            }
        }
    }
    else
    {
        // Mute mode: all bins pass, then block bins of muted objects
        mask.fill(true);
        for (const auto& obj : objects)
        {
            if (obj.mute)
            {
                for (int i = 0; i < NUM_BINS; ++i)
                    if (obj.mask[i]) mask[i] = false;
            }
        }
    }

    return mask;
}

std::array<bool, ObjectDatabase::NUM_BINS> ObjectDatabase::getCombinedMask() const
{
    juce::ScopedLock lock_(lock);

    std::array<bool, NUM_BINS> combined;
    combined.fill(false);

    // Combine all non-muted masks for UI rendering
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
    ++revision;
}

void ObjectDatabase::setObjectMute(int objectIndex, bool mute)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].mute = mute;
    ++revision;
}

void ObjectDatabase::setObjectName(int objectIndex, const std::string& newName)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].name = newName;
    ++revision;
}

uint64_t ObjectDatabase::getRevision() const
{
    juce::ScopedLock lock_(lock);
    return revision;
}

bool ObjectDatabase::isAnyMaskingActive() const
{
    juce::ScopedLock lock_(lock);
    for (const auto& obj : objects)
        if (obj.solo || obj.mute) return true;
    return false;
}
