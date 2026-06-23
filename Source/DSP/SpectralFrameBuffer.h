#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

/**
 * SpectralFrameBuffer: Ringpuffer für spektrale Frames über Zeit.
 * Speichert Magnitude und Phase der FFT-Bins für Live-Spektrogramm-Darstellung.
 */
class SpectralFrameBuffer
{
public:
    static constexpr int FFT_SIZE = 1 << 11;  // 2048
    static constexpr int NUM_BINS = FFT_SIZE / 2 + 1;  // Real-only FFT: N/2+1 bins
    static constexpr int MAX_FRAMES = 512;  // ~5.5 seconds @ 48kHz, 75% overlap

    struct Frame
    {
        std::vector<float> magnitude;
        std::vector<float> phase;
        int64_t sampleIndex = 0;

        Frame() : magnitude(NUM_BINS, 0.0f), phase(NUM_BINS, 0.0f) {}
    };

    SpectralFrameBuffer();
    ~SpectralFrameBuffer() = default;

    /**
     * Write a frame of complex FFT data (real-only format).
     * Automatically computes magnitude and phase.
     */
    void writeFrame(const float* fftData, int64_t sampleIndex);

    /**
     * Read magnitude frame at index (oldest to newest).
        * Returns false if index is out of bounds.
     */
        bool copyFrame(int frameIndex, Frame& destination) const;

        /**
        * Copy newest frame into destination.
        * Returns false when buffer is empty.
        */
        bool copyNewestFrame(Frame& destination) const;

    /**
     * Get total number of frames in buffer.
     */
    int getNumFrames() const;

    /**
     * Clear all frames.
     */
    void clear();

    /**
     * Get the oldest frame's sample index (for time alignment).
     */
    int64_t getOldestSampleIndex() const;

    /**
     * Get the newest frame's sample index.
     */
    int64_t getNewestSampleIndex() const;

private:
    std::vector<Frame> frames;
    int writePos = 0;
    int numFrames = 0;
    mutable juce::CriticalSection lock;
};
