#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/SpectralFrameBuffer.h"
#include "DSP/ObjectDatabase.h"
#include "DSP/TimelineData.h"
#include "DSP/ModulationMatrix.h"
#include <memory>
#include <deque>
#include <unordered_map>

// Version tracking
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 7;
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
    std::atomic<float>& getInputPeakDb()  { return inputPeakDb; }
    std::atomic<float>& getOutputPeakDb() { return outputPeakDb; }
    
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
    int getSelectedObjectId() const;
    void setSelectedObjectId(int objectId);

    // UI-only shared selection of the currently active FX parameter (rack <-> timeline).
    void setActiveFxSelection(const juce::String& effectName, const juce::String& parameterName);
    juce::String getActiveFxEffectName() const;
    juce::String getActiveFxParameterName() const;

    std::vector<ObjectDatabase::FXModule> getFxChainForObject(int objectId) const;
    std::vector<ObjectDatabase::FXModule> getFxChainForSelectedObject() const;
    void setObjectFxEnabled(int objectId, const juce::String& effectName, bool enabled);
    void addOrEnableObjectFx(int objectId, const juce::String& effectName);
    void setObjectFxSelectedParameter(int objectId, const juce::String& effectName, int parameterIndex);
    std::vector<ObjectDatabase::AutomationKeyframe> getFxAutomationKeyframes(int objectId,
                                                                              const juce::String& effectName,
                                                                              const juce::String& parameterName) const;
    void addFxAutomationKeyframe(int objectId,
                                 const juce::String& effectName,
                                 const juce::String& parameterName,
                                 double timeSec,
                                 float value,
                                 float curvature = 0.0f);
    void setFxAutomationSegmentCurvature(int objectId,
                                         const juce::String& effectName,
                                         const juce::String& parameterName,
                                         double segmentStartTimeSec,
                                         float curvature);
    void setTransformSourceObjectId(int objectId, int sourceObjectId);
    int getTransformSourceObjectId(int objectId) const;
    void loadTransformFileAsync(int objectId, const juce::File& file);
    int createTransformObjectFromPreset(const juce::String& presetName);
    int createTransformObjectFromFile(const juce::File& file);
    int createTransientObject();
    void deleteFxAutomationKeyframe(int objectId,
                                    const juce::String& effectName,
                                    const juce::String& parameterName,
                                    double timeSec);
    void requestAutoDetectObjects(double captureSeconds = 3.0);
    void cancelAutoDetectObjects();
    void setAutoDetectRecordingEnabled(bool shouldRecord);
    int getAutoDetectFrameCount() const;
    bool isAutoDetectRunning() const;
    bool getSegmentationOverlay(std::array<float, SpectralFrameBuffer::NUM_BINS>& transient,
                                std::array<float, SpectralFrameBuffer::NUM_BINS>& tonal,
                                std::array<float, SpectralFrameBuffer::NUM_BINS>& noise) const;
    juce::String getSegmentationDebugText() const;
    double getTransportSeconds() const noexcept { return transportSeconds.load(); }
    bool isTransportPlaying() const noexcept { return transportPlaying.load(); }

    ModulationMatrix& getModulationMatrix() { return modMatrix; }
    void calibrateDensityAnchor(ObjectDatabase::ObjectMask& obj);

private:
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* dryWetParam = nullptr;
    std::atomic<float>* transientThresholdParam = nullptr;
    std::atomic<float> inputPeakDb { -90.0f };
    std::atomic<float> outputPeakDb{ -90.0f };

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;
    static constexpr int delaySamples = fftSize - 1;
    static constexpr int outputBufferSize = fftSize + delaySamples + hopSize;

    ModulationMatrix modMatrix;

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
    std::array<float, ObjectDatabase::NUM_BINS> targetBinPitchSemitones{};
    std::array<std::array<float, ObjectDatabase::NUM_BINS>, 2> currentBinGains{};
    std::array<int, ObjectDatabase::NUM_BINS> targetBinDominantObjectIds{};
    float transientMuteCompressorGain = 1.0f;

    static constexpr float maskSmoothAlpha = 0.30f;  // one-pole per STFT frame ≈ 30ms @ 48kHz/512hop
    float stftBlend = 0.0f;
    float stftBlendCoeff = 0.0f;
    double currentSampleRate = 48000.0;
    std::array<float, ObjectDatabase::MAX_OBJECTS> timelineObjectGains{};
    std::array<float, ObjectDatabase::MAX_OBJECTS> currentTimelineObjectGains{};

    std::atomic<double> transportSeconds{ 0.0 };
    std::atomic<bool> transportPlaying{ false };
    double currentAnalysisFrameTimeSec = 0.0;

    int64_t totalSamplesProcessed = 0;

    std::unique_ptr<SpectralFrameBuffer> spectralFrameBuffer;
    std::unique_ptr<ObjectDatabase> objectDatabase;
    TimelineData timelineData;

    // === Hybrid Segmentation + HPSS Pre-Pass (Step 2) ===
    std::array<float, SpectralFrameBuffer::NUM_BINS> hpHarmonicMask{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> hpPercussiveMask{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> hpsScore{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> broadbandFlux{};

    bool useHybridMode = true;
    bool useHPSSPrePass = true;           // Neuer Schalter
    int hpssIterations = 3;               // Für Median-Filter
    
    // === Log-Attack-Time für Transienten (Step 3) ===
    std::array<float, SpectralFrameBuffer::NUM_BINS> attackSlope{};        // aktuelle Steilheit pro Bin
    std::array<float, SpectralFrameBuffer::NUM_BINS> lastAttackTime{};     // LAT pro Bin
    float globalAttackSlope = 0.0f;                                        // für Frame-Level Entscheidung

    // In PluginProcessor.h, bei den anderen PR5-Arrays:
    std::array<float, SpectralFrameBuffer::NUM_BINS> tonalDetectionCount{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> tonalDetectionMagnitude{};  // Für Stärke-Gewichtung

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
    std::array<float, SpectralFrameBuffer::NUM_BINS> peakTransient{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> peakTonal{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> peakNoise{};
    std::vector<std::array<float, SpectralFrameBuffer::NUM_BINS>> recordedMagnitudeFrames;
    std::vector<bool> recordedGateFrames;
    std::array<float, SpectralFrameBuffer::NUM_BINS> previousLogMagnitudes{};
    std::array<float, SpectralFrameBuffer::NUM_BINS> tonalPersistence{};
    std::deque<float> spectralFluxHistory;
    std::deque<float> hfcHistory;
    std::deque<float> odfHistory;
    std::deque<float> transientMeanHistory;
    std::deque<float> tonalMeanHistory;
    std::deque<float> noiseMeanHistory;
    bool hasPreviousMagnitudes = false;
    bool autoDetectActive = false;
    bool autoDetectRecording = false;
    bool overlayValid = false;
    int transientHoldFrames = 0;
    std::atomic<int> transientGateHoldSamplesRemaining{ 0 };
    std::atomic<bool> transientGateOpen{ false };
    int64_t autoDetectStartSample = 0;
    int64_t autoDetectTargetSamples = 0;
    int autoDetectFrameCount = 0;
    int autoDetectTransientFrameCount = 0;
    int autoDetectNonTransientFrameCount = 0;
    std::atomic<int> selectedObjectId{ -1 };
    mutable juce::CriticalSection activeFxLock;
    juce::String activeFxEffectName;
    juce::String activeFxParameterName;

    struct PhaseVocoderObjectState
    {
        std::array<float, ObjectDatabase::NUM_BINS> previousAnalysisPhase{};
        std::array<float, ObjectDatabase::NUM_BINS> synthesisPhase{};
        bool initialized = false;
    };

    std::array<std::unordered_map<int, PhaseVocoderObjectState>, 2> phaseVocoderStates;

    struct TransformSettings
    {
        float modulatorGain = 1.0f;
        float amount = 0.0f;
        float smoothMs = 0.0f;
        int sourceObjectId = -1;
    };

    struct TransformSmoothState
    {
        std::array<float, ObjectDatabase::NUM_BINS> smoothedMagnitudes{};
        bool initialized = false;
    };

    struct TransformFileData
    {
        std::vector<std::array<float, ObjectDatabase::NUM_BINS>> frames;
        double durationSeconds = 0.0;
    };

    struct SpectralFxSettings
    {
        float density = 1.0f;
        float brightness = 0.0f;
        float thresholdLin = 0.0f;
        float centerBin = 1.0f;
        float tiltExp = 0.0f;
        int lowBin = 0;
        int highBin = 0;
        float brightnessCompensation = 1.0f;
    };

    std::unordered_map<int, TransformSettings> transformSettingsByObject;
    std::unordered_map<int, SpectralFxSettings> spectralFxByObject;
    std::array<std::unordered_map<int, TransformSmoothState>, 2> transformSmoothStates;
    mutable juce::CriticalSection transformFileLock;
    std::unordered_map<int, TransformFileData> transformFileBuffer;
    std::array<float, ObjectDatabase::NUM_BINS> currentAnalysisMagnitudes{};

    void createHannWindow();
    void processStftFrame(int channel, int64_t currentSampleIndex);
    void applyPhaseVocoderPitchShift(int channel);
    void applyTransformCrossSynthesis(int channel);
    void updateTargetBinGains();
    void analyseSegmentationFrame(const float* fftInterleaved, int64_t currentSampleIndex);
    void applyCosineMaskSmoothing(const std::array<float, SpectralFrameBuffer::NUM_BINS>& input,
                                  std::array<float, SpectralFrameBuffer::NUM_BINS>& output) const;
    void resetAutoDetectAccumulation();
    void finalizeAutoDetectedObjects();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};