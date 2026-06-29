#include "ObjectDatabase.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
float wrapInterpolationWithCurvature(float t, float curvature)
{
    t = juce::jlimit(0.0f, 1.0f, t);
    curvature = juce::jlimit(-1.0f, 1.0f, curvature);

    if (std::abs(curvature) < 1.0e-4f)
        return t;

    const float shape = 1.0f + 4.0f * std::abs(curvature);
    if (curvature > 0.0f)
        return 1.0f - std::pow(1.0f - t, shape); // log-like: fast rise, then flatten.

    return std::pow(t, shape); // exp-like: slow start, steep finish.
}

float effectiveSegmentCurvature(float curvature, float startValue, float endValue)
{
    // Keep curve direction visually consistent for rising and falling segments.
    // Falling segments need inverted curvature sign to match rising-segment behavior.
    return (endValue < startValue) ? -curvature : curvature;
}
}

ObjectDatabase::FXModule ObjectDatabase::makeFxModule(const std::string& effectName)
{
    FXModule fx;
    fx.name = normaliseEffectName(effectName);
    fx.enabled = true;
    fx.selectedParameterIndex = 0;

    if (fx.name == "Volume")
    {
        fx.parameters.push_back({ "Gain", {} });
        fx.parameters[0].keyframes.push_back({ 0.0, 0.5f, 0.0f });
    }
    else if (fx.name == "Pitch")
    {
        fx.parameters.push_back({ "Semitones", {} });
        fx.parameters[0].keyframes.push_back({ 0.0, 0.5f, 0.0f });
    }
    else if (fx.name == "Delay")
    {
        fx.parameters.push_back({ "Time", {} });
        fx.parameters.push_back({ "Feedback", {} });
    }
    else if (fx.name == "Filter")
    {
        fx.parameters.push_back({ "Cutoff", {} });
        fx.parameters.push_back({ "Resonance", {} });
    }
    else if (fx.name == "Transform")
    {
        fx.selectedParameterIndex = 0;
        fx.parameters.push_back({ "Amount", {} });
        fx.parameters.push_back({ "Smooth", {} });
        fx.parameters[0].keyframes.push_back({ 0.0, 1.0f, 0.0f });
        fx.parameters[1].keyframes.push_back({ 0.0, 0.0f, 0.0f });
        fx.sourceObjectId = -1;
    }
    else if (fx.name == "Density")
    {
        fx.parameters.push_back({ "Density", {} });
        fx.parameters[0].keyframes.push_back({ 0.0, 1.0f, 0.0f });
    }
    else if (fx.name == "Brightness")
    {
        // Stored as 0..1 where 0.5 maps to neutral (0.0 in DSP bipolar domain).
        fx.parameters.push_back({ "Brightness", {} });
        fx.parameters[0].keyframes.push_back({ 0.0, 0.5f, 0.0f });
    }
    else
    {
        fx.parameters.push_back({ "Amount", {} });
    }

    return fx;
}

void ObjectDatabase::ensureBaseFx(ObjectMask& object)
{
    auto ensureAtIndex = [&object](const std::string& effectName, int wantedIndex)
    {
        const int idx = findFxIndexByName(object, effectName);
        if (idx < 0)
        {
            auto it = object.fxChain.begin() + juce::jlimit(0, static_cast<int>(object.fxChain.size()), wantedIndex);
            object.fxChain.insert(it, makeFxModule(effectName));
            return;
        }

        if (idx != wantedIndex)
        {
            auto fx = object.fxChain[static_cast<size_t>(idx)];
            object.fxChain.erase(object.fxChain.begin() + idx);
            auto it = object.fxChain.begin() + juce::jlimit(0, static_cast<int>(object.fxChain.size()), wantedIndex);
            object.fxChain.insert(it, std::move(fx));
        }
    };

    ensureAtIndex("Density", 0);
    ensureAtIndex("Brightness", 1);

    const int densityIdx = findFxIndexByName(object, "Density");
    if (densityIdx >= 0)
    {
        auto& density = object.fxChain[static_cast<size_t>(densityIdx)];
        density.enabled = true;
        if (density.parameters.empty())
            density.parameters.push_back({ "Density", {} });
        if (density.parameters[0].keyframes.empty())
            density.parameters[0].keyframes.push_back({ 0.0, 1.0f, 0.0f });
    }

    const int brightnessIdx = findFxIndexByName(object, "Brightness");
    if (brightnessIdx >= 0)
    {
        auto& brightness = object.fxChain[static_cast<size_t>(brightnessIdx)];
        brightness.enabled = true;
        if (brightness.parameters.empty())
            brightness.parameters.push_back({ "Brightness", {} });
        if (brightness.parameters[0].keyframes.empty())
            brightness.parameters[0].keyframes.push_back({ 0.0, 0.5f, 0.0f });
    }

    const int volIdx = findFxIndexByName(object, "Volume");
    if (volIdx < 0)
        object.fxChain.push_back(makeFxModule("Volume"));
    else
        object.fxChain[static_cast<size_t>(volIdx)].enabled = true;

    const int fixedVolIdx = findFxIndexByName(object, "Volume");
    if (fixedVolIdx >= 0)
    {
        auto& volume = object.fxChain[static_cast<size_t>(fixedVolIdx)];
        if (volume.parameters.empty())
            volume.parameters.push_back({ "Gain", {} });
        if (volume.parameters[0].keyframes.empty())
            volume.parameters[0].keyframes.push_back({ 0.0, 0.5f, 0.0f });
    }

    const int pitchIdx = findFxIndexByName(object, "Pitch");
    if (pitchIdx < 0)
        object.fxChain.push_back(makeFxModule("Pitch"));
    else
        object.fxChain[static_cast<size_t>(pitchIdx)].enabled = true;

    const int fixedPitchIdx = findFxIndexByName(object, "Pitch");
    if (fixedPitchIdx >= 0)
    {
        auto& pitch = object.fxChain[static_cast<size_t>(fixedPitchIdx)];
        if (pitch.parameters.empty())
            pitch.parameters.push_back({ "Semitones", {} });
        if (pitch.parameters[0].keyframes.empty())
            pitch.parameters[0].keyframes.push_back({ 0.0, 0.5f, 0.0f });
    }
}

std::string ObjectDatabase::normaliseEffectName(const std::string& effectName)
{
    if (effectName.empty())
        return "Effect";

    std::string name = effectName;
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    for (size_t i = 1; i < name.size(); ++i)
        name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    return name;
}

int ObjectDatabase::findFxIndexByName(const ObjectMask& object, const std::string& effectName)
{
    const juce::String wanted(normaliseEffectName(effectName));
    for (int i = 0; i < static_cast<int>(object.fxChain.size()); ++i)
    {
        if (juce::String(object.fxChain[static_cast<size_t>(i)].name).equalsIgnoreCase(wanted))
            return i;
    }

    return -1;
}

int ObjectDatabase::findParameterIndexByName(const FXModule& fx, const std::string& parameterName)
{
    const juce::String wanted(parameterName);
    for (int i = 0; i < static_cast<int>(fx.parameters.size()); ++i)
    {
        if (juce::String(fx.parameters[static_cast<size_t>(i)].name).equalsIgnoreCase(wanted))
            return i;
    }

    return -1;
}

ObjectDatabase::ObjectDatabase()
{
}

bool ObjectDatabase::addObject(const std::string& name)
{
    juce::ScopedLock lock_(lock);

    if (objects.size() >= MAX_OBJECTS)
        return false;

    ObjectMask newObject;
    newObject.id = nextObjectId++;
    newObject.name = name.empty() ? ("Object_" + std::to_string(objects.size())) : name;
    newObject.mask.fill(false);
    ensureBaseFx(newObject);

    objects.push_back(newObject);
    if (selectedObjectId < 0)
        selectedObjectId = newObject.id;
    ++revision;
    return true;
}

void ObjectDatabase::removeObject(int objectIndex)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    const int removedId = objects[static_cast<size_t>(objectIndex)].id;
    objects.erase(objects.begin() + objectIndex);
    if (selectedObjectId == removedId)
        selectedObjectId = objects.empty() ? -1 : objects.front().id;
    ++revision;
}

void ObjectDatabase::moveObject(int fromIndex, int toIndex)
{
    juce::ScopedLock lock_(lock);

    const int size = static_cast<int>(objects.size());
    if (fromIndex < 0 || fromIndex >= size || toIndex < 0 || toIndex >= size || fromIndex == toIndex)
        return;

    auto moved = objects[static_cast<size_t>(fromIndex)];
    objects.erase(objects.begin() + fromIndex);
    objects.insert(objects.begin() + toIndex, moved);
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
    ensureBaseFx(outObject);
    return true;
}

bool ObjectDatabase::getObjectCopyById(int objectId, ObjectMask& outObject) const
{
    juce::ScopedLock lock_(lock);

    for (const auto& object : objects)
    {
        if (object.id == objectId)
        {
            outObject = object;
            ensureBaseFx(outObject);
            return true;
        }
    }

    return false;
}

int ObjectDatabase::getObjectIndexById(int objectId) const
{
    juce::ScopedLock lock_(lock);

    for (int i = 0; i < static_cast<int>(objects.size()); ++i)
    {
        if (objects[static_cast<size_t>(i)].id == objectId)
            return i;
    }

    return -1;
}

int ObjectDatabase::getObjectIdAtIndex(int objectIndex) const
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return -1;

    return objects[static_cast<size_t>(objectIndex)].id;
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
    selectedObjectId = -1;
    nextObjectId = 1;
    ++revision;
}

void ObjectDatabase::setObjectMask(int objectIndex, const std::array<bool, NUM_BINS>& mask)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].mask = mask;
    ensureBaseFx(objects[objectIndex]);
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

void ObjectDatabase::setObjectRecordEnabled(int objectIndex, bool enabled)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].recordEnabled = enabled;
    ++revision;
}

void ObjectDatabase::setObjectEngaged(int objectIndex, bool engaged)
{
    juce::ScopedLock lock_(lock);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects.size()))
        return;

    objects[objectIndex].engaged = engaged;
    ++revision;
}

void ObjectDatabase::setSelectedObjectId(int objectId)
{
    juce::ScopedLock lock_(lock);

    if (objectId < 0)
    {
        if (selectedObjectId != -1)
        {
            selectedObjectId = -1;
            ++revision;
        }
        return;
    }

    for (const auto& object : objects)
    {
        if (object.id == objectId)
        {
            if (selectedObjectId != objectId)
            {
                selectedObjectId = objectId;
                ++revision;
            }
            return;
        }
    }
}

int ObjectDatabase::getSelectedObjectId() const
{
    juce::ScopedLock lock_(lock);
    return selectedObjectId;
}

std::vector<ObjectDatabase::FXModule> ObjectDatabase::getObjectFxChain(int objectId) const
{
    juce::ScopedLock lock_(lock);

    for (const auto& object : objects)
    {
        if (object.id == objectId)
        {
            ObjectMask copy = object;
            ensureBaseFx(copy);
            auto chain = copy.fxChain;
            for (auto& fx : chain)
            {
                if (juce::String(fx.name).equalsIgnoreCase("Transform") && fx.parameters.size() >= 3)
                {
                    fx.parameters.erase(std::remove_if(fx.parameters.begin(), fx.parameters.end(),
                        [](const FXParameter& parameter)
                        {
                            return juce::String(parameter.name).equalsIgnoreCase("Gain");
                        }),
                        fx.parameters.end());

                    if (fx.selectedParameterIndex >= static_cast<int>(fx.parameters.size()))
                        fx.selectedParameterIndex = 0;
                }

                fx.name = normaliseEffectName(fx.name);
            }
            return chain;
        }
    }

    return {};
}

bool ObjectDatabase::setObjectFxEnabled(int objectId, const std::string& effectName, bool enabled)
{
    juce::ScopedLock lock_(lock);

    for (auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        ensureBaseFx(object);
        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return false;

        auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        if (juce::String(fx.name).equalsIgnoreCase("Volume") || juce::String(fx.name).equalsIgnoreCase("Pitch"))
            fx.enabled = true;
        else
            fx.enabled = enabled;

        ++revision;
        return true;
    }

    return false;
}

bool ObjectDatabase::addOrEnableObjectFx(int objectId, const std::string& effectName)
{
    juce::ScopedLock lock_(lock);

    for (auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        ensureBaseFx(object);
        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex >= 0)
        {
            object.fxChain[static_cast<size_t>(fxIndex)].enabled = true;
            ++revision;
            return true;
        }

        object.fxChain.push_back(makeFxModule(effectName));
        ++revision;
        return true;
    }

    return false;
}

bool ObjectDatabase::setObjectFxSelectedParameter(int objectId, const std::string& effectName, int parameterIndex)
{
    juce::ScopedLock lock_(lock);

    for (auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return false;

        auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        if (fx.parameters.empty())
            return false;

        fx.selectedParameterIndex = juce::jlimit(0, static_cast<int>(fx.parameters.size()) - 1, parameterIndex);
        ++revision;
        return true;
    }

    return false;
}

bool ObjectDatabase::setObjectFxSourceObjectId(int objectId, const std::string& effectName, int sourceObjectId)
{
    juce::ScopedLock lock_(lock);

    for (auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        ensureBaseFx(object);
        int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
        {
            object.fxChain.push_back(makeFxModule(effectName));
            fxIndex = static_cast<int>(object.fxChain.size()) - 1;
        }

        object.fxChain[static_cast<size_t>(fxIndex)].sourceObjectId = sourceObjectId;
        ++revision;
        return true;
    }

    return false;
}

int ObjectDatabase::getObjectFxSourceObjectId(int objectId, const std::string& effectName) const
{
    juce::ScopedLock lock_(lock);

    for (const auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return -1;

        return object.fxChain[static_cast<size_t>(fxIndex)].sourceObjectId;
    }

    return -1;
}

bool ObjectDatabase::setObjectDensityAnchor(int objectId, float anchorDb, bool valid)
{
    juce::ScopedLock lock_(lock);

    for (auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        object.densityAnchorDb = anchorDb;
        object.densityAnchorValid = valid;
        ++revision;
        return true;
    }

    return false;
}

std::string ObjectDatabase::getObjectFxSelectedParameterName(int objectId, const std::string& effectName) const
{
    juce::ScopedLock lock_(lock);

    for (const auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return {};

        const auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        if (fx.parameters.empty())
            return {};

        const int idx = juce::jlimit(0, static_cast<int>(fx.parameters.size()) - 1, fx.selectedParameterIndex);
        return fx.parameters[static_cast<size_t>(idx)].name;
    }

    return {};
}

void ObjectDatabase::addAutomationKeyframe(int objectId,
                                           const std::string& effectName,
                                           const std::string& parameterName,
                                           double timeSec,
                                           float value,
                                           float curvature)
{
    juce::ScopedLock lock_(lock);

    for (auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        ensureBaseFx(object);
        int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
        {
            object.fxChain.push_back(makeFxModule(effectName));
            fxIndex = static_cast<int>(object.fxChain.size()) - 1;
        }

        auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        int paramIndex = findParameterIndexByName(fx, parameterName);
        if (paramIndex < 0)
        {
            fx.parameters.push_back({ parameterName.empty() ? "Amount" : parameterName, {} });
            paramIndex = static_cast<int>(fx.parameters.size()) - 1;
        }

        auto& keys = fx.parameters[static_cast<size_t>(paramIndex)].keyframes;
        value = juce::jlimit(0.0f, 1.0f, value);
        curvature = juce::jlimit(-1.0f, 1.0f, curvature);

        for (auto& k : keys)
        {
            if (std::abs(k.timeSec - timeSec) < 1.0e-3)
            {
                k.value = value;
                ++revision;
                return;
            }
        }

        keys.push_back({ timeSec, value, curvature });
        std::sort(keys.begin(), keys.end(), [](const AutomationKeyframe& a, const AutomationKeyframe& b)
        {
            return a.timeSec < b.timeSec;
        });
        ++revision;
        return;
    }
}

void ObjectDatabase::setAutomationSegmentCurvature(int objectId,
                                                   const std::string& effectName,
                                                   const std::string& parameterName,
                                                   double segmentStartTimeSec,
                                                   float curvature)
{
    juce::ScopedLock lock_(lock);

    curvature = juce::jlimit(-1.0f, 1.0f, curvature);

    for (auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return;

        auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        const int paramIndex = findParameterIndexByName(fx, parameterName);
        if (paramIndex < 0)
            return;

        auto& keys = fx.parameters[static_cast<size_t>(paramIndex)].keyframes;
        if (keys.size() < 2)
            return;

        for (size_t i = 0; i + 1 < keys.size(); ++i)
        {
            if (std::abs(keys[i].timeSec - segmentStartTimeSec) < 1.0e-3)
            {
                keys[i].curvature = curvature;
                ++revision;
                return;
            }
        }

        return;
    }
}

void ObjectDatabase::deleteAutomationKeyframe(int objectId,
                                              const std::string& effectName,
                                              const std::string& parameterName,
                                              double timeSec)
{
    juce::ScopedLock lock_(lock);

    for (auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return;

        auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        const int paramIndex = findParameterIndexByName(fx, parameterName);
        if (paramIndex < 0)
            return;

        auto& keys = fx.parameters[static_cast<size_t>(paramIndex)].keyframes;
        keys.erase(std::remove_if(keys.begin(), keys.end(),
            [timeSec](const AutomationKeyframe& k)
            {
                return std::abs(k.timeSec - timeSec) < 1.0e-3;
            }),
            keys.end());
        ++revision;
        return;
    }
}

std::vector<ObjectDatabase::AutomationKeyframe> ObjectDatabase::getAutomationKeyframes(int objectId,
                                                                                         const std::string& effectName,
                                                                                         const std::string& parameterName) const
{
    juce::ScopedLock lock_(lock);

    for (const auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return {};

        const auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        const int paramIndex = findParameterIndexByName(fx, parameterName);
        if (paramIndex < 0)
            return {};

        return fx.parameters[static_cast<size_t>(paramIndex)].keyframes;
    }

    return {};
}

float ObjectDatabase::getInterpolatedAutomationValue(int objectId,
                                                     const std::string& effectName,
                                                     const std::string& parameterName,
                                                     double timeSec,
                                                     float fallback) const
{
    juce::ScopedLock lock_(lock);

    for (const auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return fallback;

        const auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        if (!fx.enabled)
            return fallback;

        const int paramIndex = findParameterIndexByName(fx, parameterName);
        if (paramIndex < 0)
            return fallback;

        const auto& keys = fx.parameters[static_cast<size_t>(paramIndex)].keyframes;
        if (keys.empty())
            return fallback;

        if (timeSec <= keys.front().timeSec)
            return keys.front().value;
        if (timeSec >= keys.back().timeSec)
            return keys.back().value;

        for (size_t i = 0; i + 1 < keys.size(); ++i)
        {
            const auto& a = keys[i];
            const auto& b = keys[i + 1];
            if (timeSec >= a.timeSec && timeSec <= b.timeSec)
            {
                const double span = b.timeSec - a.timeSec;
                const float t = (span > 1.0e-9) ? static_cast<float>((timeSec - a.timeSec) / span) : 0.0f;
                const float curvature = effectiveSegmentCurvature(a.curvature, a.value, b.value);
                const float shapedT = wrapInterpolationWithCurvature(t, curvature);
                return a.value + shapedT * (b.value - a.value);
            }
        }

        return fallback;
    }

    return fallback;
}

float ObjectDatabase::getObjectFxParameterValue(int objectId,
                                                const std::string& effectName,
                                                const std::string& parameterName,
                                                float fallback) const
{
    juce::ScopedLock lock_(lock);

    for (const auto& object : objects)
    {
        if (object.id != objectId)
            continue;

        const int fxIndex = findFxIndexByName(object, effectName);
        if (fxIndex < 0)
            return fallback;

        const auto& fx = object.fxChain[static_cast<size_t>(fxIndex)];
        if (!fx.enabled)
            return fallback;

        const int paramIndex = findParameterIndexByName(fx, parameterName);
        if (paramIndex < 0)
            return fallback;

        const auto& keys = fx.parameters[static_cast<size_t>(paramIndex)].keyframes;
        if (keys.empty())
            return fallback;

        // Non-automated read: use the canonical setup value at the first keyframe.
        return keys.front().value;
    }

    return fallback;
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
    {
        if (obj.engaged)
            return true;
        if (obj.solo || obj.mute)
            return true;
    }

    return false;
}

juce::ValueTree ObjectDatabase::toValueTree() const
{
    juce::ScopedLock lock_(lock);

    juce::ValueTree root("ObjectDatabase");
    root.setProperty("selectedObjectId", selectedObjectId, nullptr);
    root.setProperty("nextObjectId", nextObjectId, nullptr);

    for (const auto& object : objects)
    {
        juce::ValueTree objNode("Object");
        objNode.setProperty("id", object.id, nullptr);
        objNode.setProperty("name", juce::String(object.name), nullptr);
        objNode.setProperty("solo", object.solo, nullptr);
        objNode.setProperty("mute", object.mute, nullptr);
        objNode.setProperty("recordEnabled", object.recordEnabled, nullptr);
        objNode.setProperty("engaged", object.engaged, nullptr);
        objNode.setProperty("color", object.color, nullptr);
        objNode.setProperty("densityAnchorDb", object.densityAnchorDb, nullptr);
        objNode.setProperty("densityAnchorValid", object.densityAnchorValid, nullptr);

        juce::String maskBits;
        maskBits.preallocateBytes(NUM_BINS + 1);
        for (int i = 0; i < NUM_BINS; ++i)
            maskBits << (object.mask[static_cast<size_t>(i)] ? '1' : '0');
        objNode.setProperty("mask", maskBits, nullptr);

        juce::ValueTree fxRoot("FxChain");
        for (const auto& fx : object.fxChain)
        {
            juce::ValueTree fxNode("Fx");
            fxNode.setProperty("name", juce::String(fx.name), nullptr);
            fxNode.setProperty("enabled", fx.enabled, nullptr);
            fxNode.setProperty("selectedParameterIndex", fx.selectedParameterIndex, nullptr);
            fxNode.setProperty("sourceObjectId", fx.sourceObjectId, nullptr);

            for (const auto& parameter : fx.parameters)
            {
                juce::ValueTree paramNode("Param");
                paramNode.setProperty("name", juce::String(parameter.name), nullptr);

                for (const auto& keyframe : parameter.keyframes)
                {
                    juce::ValueTree keyNode("Key");
                    keyNode.setProperty("time", keyframe.timeSec, nullptr);
                    keyNode.setProperty("value", keyframe.value, nullptr);
                    keyNode.setProperty("curvature", keyframe.curvature, nullptr);
                    paramNode.appendChild(keyNode, nullptr);
                }

                fxNode.appendChild(paramNode, nullptr);
            }

            fxRoot.appendChild(fxNode, nullptr);
        }

        objNode.appendChild(fxRoot, nullptr);
        root.appendChild(objNode, nullptr);
    }

    return root;
}

void ObjectDatabase::fromValueTree(const juce::ValueTree& tree)
{
    juce::ScopedLock lock_(lock);

    if (!tree.isValid() || !tree.hasType("ObjectDatabase"))
        return;

    objects.clear();
    selectedObjectId = static_cast<int>(tree.getProperty("selectedObjectId", -1));
    nextObjectId = static_cast<int>(tree.getProperty("nextObjectId", 1));

    int maxId = 0;
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto objNode = tree.getChild(i);
        if (!objNode.hasType("Object"))
            continue;

        ObjectMask object;
        object.id = static_cast<int>(objNode.getProperty("id", -1));
        object.name = objNode.getProperty("name", "Object").toString().toStdString();
        object.solo = static_cast<bool>(objNode.getProperty("solo", false));
        object.mute = static_cast<bool>(objNode.getProperty("mute", false));
        object.recordEnabled = static_cast<bool>(objNode.getProperty("recordEnabled", false));
        object.engaged = static_cast<bool>(objNode.getProperty("engaged", true));
        object.color = static_cast<int>(objNode.getProperty("color", static_cast<int>(0xFF00AA00)));
        object.densityAnchorDb = static_cast<float>(objNode.getProperty("densityAnchorDb", -60.0f));
        object.densityAnchorValid = static_cast<bool>(objNode.getProperty("densityAnchorValid", false));
        object.mask.fill(false);

        const auto maskBits = objNode.getProperty("mask", juce::String()).toString();
        const int maskLen = juce::jmin(NUM_BINS, maskBits.length());
        for (int bin = 0; bin < maskLen; ++bin)
            object.mask[static_cast<size_t>(bin)] = (maskBits[bin] == '1');

        const auto fxRoot = objNode.getChildWithName("FxChain");
        if (fxRoot.isValid())
        {
            object.fxChain.clear();
            for (int fxIdx = 0; fxIdx < fxRoot.getNumChildren(); ++fxIdx)
            {
                const auto fxNode = fxRoot.getChild(fxIdx);
                if (!fxNode.hasType("Fx"))
                    continue;

                FXModule fx;
                fx.name = fxNode.getProperty("name", "Effect").toString().toStdString();
                fx.enabled = static_cast<bool>(fxNode.getProperty("enabled", true));
                fx.selectedParameterIndex = static_cast<int>(fxNode.getProperty("selectedParameterIndex", 0));
                fx.sourceObjectId = static_cast<int>(fxNode.getProperty("sourceObjectId", -1));

                for (int p = 0; p < fxNode.getNumChildren(); ++p)
                {
                    const auto paramNode = fxNode.getChild(p);
                    if (!paramNode.hasType("Param"))
                        continue;

                    FXParameter parameter;
                    parameter.name = paramNode.getProperty("name", "Amount").toString().toStdString();

                    for (int k = 0; k < paramNode.getNumChildren(); ++k)
                    {
                        const auto keyNode = paramNode.getChild(k);
                        if (!keyNode.hasType("Key"))
                            continue;

                        AutomationKeyframe key;
                        key.timeSec = static_cast<double>(keyNode.getProperty("time", 0.0));
                        key.value = static_cast<float>(keyNode.getProperty("value", 1.0f));
                        key.curvature = static_cast<float>(keyNode.getProperty("curvature", 0.0f));
                        parameter.keyframes.push_back(key);
                    }

                    std::sort(parameter.keyframes.begin(), parameter.keyframes.end(),
                              [](const AutomationKeyframe& a, const AutomationKeyframe& b)
                              {
                                  return a.timeSec < b.timeSec;
                              });
                    fx.parameters.push_back(std::move(parameter));
                }

                object.fxChain.push_back(std::move(fx));
            }
        }

        ensureBaseFx(object);
        maxId = juce::jmax(maxId, object.id);
        objects.push_back(std::move(object));
    }

    if (nextObjectId <= maxId)
        nextObjectId = maxId + 1;

    ++revision;
}


