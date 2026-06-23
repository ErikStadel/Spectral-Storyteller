#pragma once

#include <juce_core/juce_core.h>
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

    struct ObjectMask
    {
        std::string name;
        std::array<bool, NUM_BINS> mask;
        bool solo = false;
        bool mute = false;
        int color = 0xFF00AA00;  // Default: green

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

    /**
     * Get object mask at index (nullptr if out of bounds).
     */
    ObjectMask* getObject(int objectIndex);
    const ObjectMask* getObject(int objectIndex) const;

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

    /**
     * Get combined mask for all active (non-muted) objects.
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

private:
    std::vector<ObjectMask> objects;
    mutable juce::CriticalSection lock;
};
