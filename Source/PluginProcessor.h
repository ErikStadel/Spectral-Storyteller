#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/SpectralFrameBuffer.h"
#include "DSP/ObjectDatabase.h"
#include "DSP/TimelineData.h"
#include <memory>
#include <deque>

// Version tracking
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 5;
constexpr int VERSION_BUILD = 1;

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "Spectral Storyteller"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }
    
    juce::String getVersion() const { 
        return juce::String(VERSION_MAJOR) + "." + juce::String(VERSION_MINOR) + "." + juce::String(VERSION_BUILD);
    }

    juce::String getBuildInfo() const
    {
        return "v" + getVersion() + " (" + juce::String(__DATE__) + " " + juce::String(__TIME__) + ")";
    }

    /**
     * Get reference to spectral frame buffer for UI visualization.
     */
    SpectralFrameBuffer* getSpectralFrameBuffer() { return spectralFrameBuffer.get(); }

    /**
     * Get reference to object database.
     */
    ObjectDatabase* getObjectDatabase() { return objectDatabase.get(); }

    // PR4 Timeline API
    void addTimelineKeyframe(int objectIndex, double timeSec, float value);
    void deleteTimelineKeyframe(int objectIndex, double timeSec);
    std::vector<TimelineData::Keyframe> getTimelineKeyframes(int objectIndex) const;
    juce::String getTimelineTrackName(int objectIndex) const;
    void setTimelineTrackName(int objectIndex, const std::string& newName);
    void requestAutoDetectObjects(double captureSeconds = 3.0);
    void setAutoDetectRecordingEnabled(bool shouldRecord);
    bool getSegmentationOverlay(std::array<float, SpectralFrameBuffer::NUM_BINS>& transient,
                                std::array<float, SpectralFrameBuffer::NUM_BINS>& tonal,
                                std::array<float, SpectralFrameBuffer::NUM_BINS>& noise) const;
    double getTransportSeconds() const noexcept { return transportSeconds.load(); }
    bool isTransportPlaying() const noexcept { return transportPlaying.load(); }

private:
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* dryWetParam = nullptr;

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;
    static constexpr int delaySamples = fftSize - 1;
    static constexpr int outputBufferSize = fftSize + delaySamples + hopSize;

    juce::dsp::FFT fft;
    std::vector<float> window;
    std::array<std::vector<float>, 2> inputBuffers;
    std::array<std::vector<float>, 2> outputBuffers;
    std::array<std::vector<float>, 2> outputNormBuffers;
    std::array<int, 2> inputWritePos{ 0, 0 };
    std::array<int, 2> samplesInBuffer{ 0, 0 };
    std::array<int, 2> samplesSinceLastFrame{ 0, 0 };
    std::vector<float> fftData;
    std::array<float, ObjectDatabase::NUM_BINS> targetBinGains{};
    std::array<std::array<float, ObjectDatabase::NUM_BINS>, 2> currentBinGains{};

    static constexpr float maskSmoothAlpha = 0.30f;  // one-pole per STFT frame ≈ 30ms @ 48kHz/512hop
    float stftBlend = 0.0f;
    float stftBlendCoeff = 0.0f;
    double currentSampleRate = 48000.0;
    std::array<float, ObjectDatabase::MAX_OBJECTS> timelineObjectGains{};
    std::array<float, ObjectDatabase::MAX_OBJECTS> currentTimelineObjectGains{};

    std::atomic<double> transportSeconds{ 0.0 };
    std::atomic<bool> transportPlaying{ false };

    int64_t totalSamplesProcessed = 0;

    std::unique_ptr<SpectralFrameBuffer> spectralFrameBuffer;
    std::unique_ptr<ObjectDatabase> objectDatabase;
    TimelineData timelineData;

    // PR5: rule-based segmentation backend
    mutable juce::CriticalSection segmentationLock;
    std::array<float, SpectralFrameBuffer::NUM_BINS> previousMagnitudes{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> lastFlatness{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> overlayTransient{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> overlayTonal{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> overlayNoise{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> accumulatedTransient{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> accumulatedTonal{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> accumulatedNoise{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> previousLogMagnitudes{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> tonalPersistence{};
    std::deque<float> spectralFluxHistory;
    std::deque<float> hfcHistory;
    std::deque<float> odfHistory;
    bool hasPreviousMagnitudes = false;
    bool autoDetectActive = false;
    bool autoDetectRecording = false;
    bool overlayValid = false;
    int transientHoldFrames = 0;
    int64_t autoDetectStartSample = 0;
    int64_t autoDetectTargetSamples = 0;
    int autoDetectFrameCount = 0;
    int autoDetectTransientFrameCount = 0;
    int autoDetectNonTransientFrameCount = 0;

    void createHannWindow();
    void processStftFrame(int channel, int64_t currentSampleIndex);
    void updateTargetBinGains();
    void analyseSegmentationFrame(const float* fftInterleaved, int64_t currentSampleIndex);
    void applyCosineMaskSmoothing(const std::array<float, SpectralFrameBuffer::NUM_BINS>& input,
                                  std::array<float, SpectralFrameBuffer::NUM_BINS>& output) const;
    void resetAutoDetectAccumulation();
    void finalizeAutoDetectedObjects();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};