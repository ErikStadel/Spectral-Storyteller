#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <array>

/**
 * ObjectDatabase: Verwaltet Spektral-Objekt-Masken.
 * PR1: Skeleton für zukünftige Implementierung.
 * PR3+: Wird mit Bin-Masken, Lasso-Editor, Solo/Mute erweitert.
 */
class ObjectDatabase
{
public:
    static constexpr int NUM_BINS = (1 << 11) / 2 + 1;  // 1025 (matching FFT_SIZE=2048)
    static constexpr int MAX_OBJECTS = 32;

    struct AutomationKeyframe
    {
        double timeSec = 0.0;
        float value = 1.0f;
        float curvature = 0.0f; // Segment curvature from this keyframe to the next keyframe.
    };

    struct FXParameter
    {
        std::string name;
        std::vector<AutomationKeyframe> keyframes;
    };

    struct FXModule
    {
        std::string name;
        bool enabled = true;
        int selectedParameterIndex = 0;
        int sourceObjectId = -1; // Used by Transform: object id or FILE_SOURCE_ID.
        std::vector<FXParameter> parameters;
    };

    static constexpr int FILE_SOURCE_ID = -2;

    struct ObjectMask
    {
        int id = -1;
        std::string name;
        std::array<bool, NUM_BINS> mask;
        bool solo = false;
        bool mute = false;
        bool recordEnabled = false;
        bool engaged = true;
        int color = 0xFF00AA00;  // Default: green
        float densityAnchorDb = -60.0f;
        bool densityAnchorValid = false;
        bool hasTimeFrequencyMask = false;
        std::vector<double> timeMaskFrameTimesSec;
        std::vector<std::array<bool, NUM_BINS>> timeMaskFrameMasks;
        std::vector<FXModule> fxChain;

        ObjectMask() : name("Object_"), mask{} {}
    };

    ObjectDatabase();
    ~ObjectDatabase() = default;

    /**
     * Add a new object mask with default empty state.
     */
    bool addObject(const std::string& name);

    /**
     * Remove an object by index.
     */
    void removeObject(int objectIndex);
    void moveObject(int fromIndex, int toIndex);

    /**
     * Get object mask at index (nullptr if out of bounds).
     */
    ObjectMask* getObject(int objectIndex);
    const ObjectMask* getObject(int objectIndex) const;
    bool getObjectCopy(int objectIndex, ObjectMask& outObject) const;
    bool getObjectCopyById(int objectId, ObjectMask& outObject) const;
    int getObjectIndexById(int objectId) const;
    int getObjectIdAtIndex(int objectIndex) const;

    /**
     * Get total number of objects.
     */
    int getNumObjects() const;

    /**
     * Clear all objects.
     */
    void clear();

    /**
     * Set bin mask for an object (1 = include bin, 0 = exclude).
     */
    void setObjectMask(int objectIndex, const std::array<bool, NUM_BINS>& mask);
    void setObjectColor(int objectIndex, int argbColour);

    /**
     * Get combined mask for audio DSP:
     * - If any object is solo: return ONLY soloed object bins (OR'd together)
     * - Otherwise: return all non-muted object bins (OR'd together)
     * - Empty result if all muted and no solo
     */
    std::array<bool, NUM_BINS> getAudioMask() const;

    /**
     * Get visual mask for UI rendering (all non-muted objects).
     */
    std::array<bool, NUM_BINS> getCombinedMask() const;

    /**
     * Set solo state for an object.
     */
    void setObjectSolo(int objectIndex, bool solo);

    /**
     * Set mute state for an object.
     */
    void setObjectMute(int objectIndex, bool mute);

    /**
     * Set custom name for an object (for timeline track display).
     */
    void setObjectName(int objectIndex, const std::string& newName);
    void setObjectRecordEnabled(int objectIndex, bool enabled);
    void setObjectEngaged(int objectIndex, bool engaged);
    void setSelectedObjectId(int objectId);
    int getSelectedObjectId() const;

    std::vector<FXModule> getObjectFxChain(int objectId) const;
    bool setObjectFxEnabled(int objectId, const std::string& effectName, bool enabled);
    bool addOrEnableObjectFx(int objectId, const std::string& effectName);
    bool setObjectFxSelectedParameter(int objectId, const std::string& effectName, int parameterIndex);
    std::string getObjectFxSelectedParameterName(int objectId, const std::string& effectName) const;
    bool setObjectFxSourceObjectId(int objectId, const std::string& effectName, int sourceObjectId);
    int getObjectFxSourceObjectId(int objectId, const std::string& effectName) const;
    bool setObjectDensityAnchor(int objectId, float anchorDb, bool valid);
    bool setObjectTimeFrequencyMask(int objectId,
                                    const std::vector<double>& frameTimesSec,
                                    const std::vector<std::array<bool, NUM_BINS>>& frameMasks,
                                    const std::array<bool, NUM_BINS>& combinedMask);

    void addAutomationKeyframe(int objectId,
                               const std::string& effectName,
                               const std::string& parameterName,
                               double timeSec,
                               float value,
                               float curvature = 0.0f);
    void setAutomationSegmentCurvature(int objectId,
                                       const std::string& effectName,
                                       const std::string& parameterName,
                                       double segmentStartTimeSec,
                                       float curvature);
    void deleteAutomationKeyframe(int objectId, const std::string& effectName, const std::string& parameterName, double timeSec);
    std::vector<AutomationKeyframe> getAutomationKeyframes(int objectId, const std::string& effectName, const std::string& parameterName) const;
    float getInterpolatedAutomationValue(int objectId,
                                         const std::string& effectName,
                                         const std::string& parameterName,
                                         double timeSec,
                                         float fallback = 1.0f) const;
    float getObjectFxParameterValue(int objectId,
                                    const std::string& effectName,
                                    const std::string& parameterName,
                                    float fallback = 1.0f) const;
    uint64_t getRevision() const;
    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

    /**
     * Returns true if any engaged object exists or if solo/mute states are active.
     * Used in DSP to decide whether to use STFT output or bypass.
     */
    bool isAnyMaskingActive() const;

private:
    static FXModule makeFxModule(const std::string& effectName);
    static void ensureBaseFx(ObjectMask& object);
    static std::string normaliseEffectName(const std::string& effectName);
    static int findFxIndexByName(const ObjectMask& object, const std::string& effectName);
    static int findParameterIndexByName(const FXModule& fx, const std::string& parameterName);

    std::vector<ObjectMask> objects;
    mutable juce::CriticalSection lock;
    int selectedObjectId = -1;
    int nextObjectId = 1;
    uint64_t revision = 0;
};
