#include "SpectralFrameBuffer.h"

SpectralFrameBuffer::SpectralFrameBuffer()
{
    frames.resize(MAX_FRAMES);
}

void SpectralFrameBuffer::writeFrame(const float* fftData, int64_t sampleIndex)
{
    juce::ScopedLock lock_(lock);

    Frame& frame = frames[writePos];
    frame.sampleIndex = sampleIndex;

    // Convert real-only FFT format to magnitude and phase.
    // Real-only format: [re0, re1, ..., reN, im1, ..., imN]
    // Bins: 0 (DC), 1..N-1 (positive freqs), N (Nyquist)
    
    const float fftScale = 1.0f / static_cast<float>(FFT_SIZE);

    // DC component (always real, no 2x single-sided scaling)
    frame.magnitude[0] = std::abs(fftData[0]) * fftScale;
    frame.phase[0] = 0.0f;

    // Positive frequency bins
    for (int k = 1; k < NUM_BINS - 1; ++k)
    {
        float re = fftData[k];
        float im = fftData[FFT_SIZE - k];
        frame.magnitude[k] = std::sqrt(re * re + im * im) * (2.0f * fftScale);
        frame.phase[k] = std::atan2(im, re);
    }

    // Nyquist component (always real, no 2x single-sided scaling)
    frame.magnitude[NUM_BINS - 1] = std::abs(fftData[FFT_SIZE / 2]) * fftScale;
    frame.phase[NUM_BINS - 1] = 0.0f;

    // Convert to dB scale (with floor to avoid very negative values)
    for (int k = 0; k < NUM_BINS; ++k)
    {
        float magLinear = frame.magnitude[k];
        float magDb = 20.0f * std::log10(magLinear + 1.0e-6f);
        frame.magnitude[k] = magDb;
    }

    writePos = (writePos + 1) % MAX_FRAMES;
    if (numFrames < MAX_FRAMES)
        ++numFrames;
}

bool SpectralFrameBuffer::copyFrame(int frameIndex, Frame& destination) const
{
    juce::ScopedLock lock_(lock);

    if (frameIndex < 0 || frameIndex >= numFrames)
        return false;

    int readPos = (writePos - numFrames + frameIndex) % MAX_FRAMES;
    if (readPos < 0)
        readPos += MAX_FRAMES;

    destination = frames[readPos];
    return true;
}

bool SpectralFrameBuffer::copyNewestFrame(Frame& destination) const
{
    juce::ScopedLock lock_(lock);

    if (numFrames == 0)
        return false;

    const int newestPos = (writePos - 1 + MAX_FRAMES) % MAX_FRAMES;
    destination = frames[newestPos];
    return true;
}

int SpectralFrameBuffer::getNumFrames() const
{
    juce::ScopedLock lock_(lock);
    return numFrames;
}

void SpectralFrameBuffer::clear()
{
    juce::ScopedLock lock_(lock);
    writePos = 0;
    numFrames = 0;
    for (auto& frame : frames)
    {
        std::fill(frame.magnitude.begin(), frame.magnitude.end(), 0.0f);
        std::fill(frame.phase.begin(), frame.phase.end(), 0.0f);
    }
}

int64_t SpectralFrameBuffer::getOldestSampleIndex() const
{
    juce::ScopedLock lock_(lock);
    if (numFrames == 0)
        return 0;
    int oldestPos = (writePos - numFrames) % MAX_FRAMES;
    if (oldestPos < 0)
        oldestPos += MAX_FRAMES;
    return frames[oldestPos].sampleIndex;
}

int64_t SpectralFrameBuffer::getNewestSampleIndex() const
{
    juce::ScopedLock lock_(lock);
    if (numFrames == 0)
        return 0;
    int newestPos = (writePos - 1 + MAX_FRAMES) % MAX_FRAMES;
    return frames[newestPos].sampleIndex;
}
