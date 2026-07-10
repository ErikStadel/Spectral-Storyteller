#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/SpectralFrameBuffer.h"
#include "DSP/ObjectDatabase.h"
#include "DSP/TimelineData.h"
#include "DSP/ModulationMatrix.h"
#include "DSP/ShadeContour.h"
#include "DSP/MassForge.h"
#include "DSP/EchoBleed.h"
#include "DSP/SpaceBlur.h"
#include "DSP/HeatGlow.h"
#include "DSP/GritEdge.h"
#include "DSP/StasisCloud.h"
#include <memory>
#include <deque>
#include <unordered_map>

// Version tracking
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 10;
constexpr int VERSION_BUILD = 1;

class PluginProcessor : public juce::AudioProcessor,
                        public juce::ChangeBroadcaster
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
    double getTailLengthSeconds() const override { return 30.0; }

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
    // ─── Getter für das ModulationPanel ───
    juce::String getActiveFxEffectName() const 
    { 
        const juce::ScopedLock sl(activeFxLock); 
        return activeFxEffectName; 
    }
    
    juce::String getActiveFxParameterName() const 
    { 
        const juce::ScopedLock sl(activeFxLock); 
        return activeFxParameterName; 
    }

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
    void setFxParameterFollowTimeline(int objectId,
                                      const juce::String& effectName,
                                      const juce::String& parameterName,
                                      bool shouldFollowTimeline);
    bool getFxParameterFollowTimeline(int objectId,
                                      const juce::String& effectName,
                                      const juce::String& parameterName,
                                      bool fallback = false) const;
    void setFxStaticParameterValue(int objectId,
                                   const juce::String& effectName,
                                   const juce::String& parameterName,
                                   float value);
    void setFxAutomationSegmentCurvature(int objectId,
                                         const juce::String& effectName,
                                         const juce::String& parameterName,
                                         double segmentStartTimeSec,
                                         float curvature);
    void setTransformSourceObjectId(int objectId, int sourceObjectId);
    int getTransformSourceObjectId(int objectId) const;
    void loadTransformFileAsync(int objectId, const juce::File& file);
    struct TransformSourceViewData
    {
        struct WaveformSlice
        {
            float minSample = 0.0f;
            float maxSample = 0.0f;
        };

        juce::String displayName;
        std::vector<WaveformSlice> waveform;
        double durationSeconds = 0.0;
        double loopStartSeconds = 0.0;
        double loopEndSeconds = 0.0;
        bool hasData = false;
    };

    bool getTransformSourceViewData(int objectId, TransformSourceViewData& outData) const;
    bool setTransformLoopRange(int objectId, double loopStartSeconds, double loopEndSeconds);
    bool getTransformLoopRange(int objectId, double& outLoopStart, double& outLoopEnd) const;
    double getSourceTransportSeconds() const noexcept { return sourceTransportSeconds.load(); }
    bool isSourceTransportPlaying() const noexcept { return sourceTransportPlaying.load(); }
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
    // Scratch spectrum (2*fftSize) used to build one object's soft-masked slice
    // before its per-object ISTFT in reconstructAndOverlapAdd. Not per channel;
    // reconstruction runs one channel/one object at a time.
    std::vector<float> objectSpectrumScratch;
    std::array<float, ObjectDatabase::NUM_BINS> targetBinGains{};
    std::array<float, ObjectDatabase::NUM_BINS> targetBinPitchSemitones{};
    std::array<std::array<float, ObjectDatabase::NUM_BINS>, 2> currentBinGains{};
    std::array<int, ObjectDatabase::NUM_BINS> targetBinDominantObjectIds{};
    // Bug A fix: temporal hysteresis on the bin -> object assignment. The raw
    // per-frame dominant object can flicker when detection is uncertain; these
    // hold the debounced/committed owner and the pending-switch counter so a bin
    // only changes object after a candidate has persisted for a few frames.
    std::array<int, ObjectDatabase::NUM_BINS> committedDominantObjectIds{};
    std::array<int, ObjectDatabase::NUM_BINS> pendingDominantObjectIds{};
    std::array<int, ObjectDatabase::NUM_BINS> pendingDominantFrameCount{};
    float transientMuteCompressorGain = 1.0f;

    static constexpr float maskSmoothAlpha = 0.30f;  // one-pole per STFT frame ≈ 30ms @ 48kHz/512hop
    float stftBlend = 0.0f;
    float stftBlendCoeff = 0.0f;         // fast attack (~30ms) going wet
    float stftBlendReleaseCoeff = 0.0f;  // slow release (~3.5s) going dry → allows reverb/delay tails to ring out
    double currentSampleRate = 48000.0;
    std::array<float, ObjectDatabase::MAX_OBJECTS> timelineObjectGains{};
    std::array<float, ObjectDatabase::MAX_OBJECTS> currentTimelineObjectGains{};

    std::atomic<double> transportSeconds{ 0.0 };
    std::atomic<bool> transportPlaying{ false };
    std::atomic<double> sourceTransportSeconds{ 0.0 };
    std::atomic<bool> sourceTransportPlaying{ false };
    double sourceLoopPhaseAccSec = 0.0;
    bool lastDawWasPlaying = false;
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
        std::vector<TransformSourceViewData::WaveformSlice> waveform;
        double durationSeconds = 0.0;
        double loopStartSeconds = 0.0;
        double loopEndSeconds = 0.0;
        juce::String displayName;
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
    std::unordered_map<int, shade_contour::Settings> filterFxByObject;
    std::unordered_map<int, mass_forge::Settings> compressorFxByObject;
    std::unordered_map<int, mass_forge::State> compressorStateByObject;
    std::unordered_map<int, mass_forge::FrameParams> compressorParamsByObject;
    std::unordered_map<int, heat_glow::Settings> heatGlowFxByObject;
    std::array<std::unordered_map<int, heat_glow::State>, 2> heatGlowStateByChannel;
    std::unordered_map<int, grit_edge::Settings> gritEdgeFxByObject;
    std::array<std::unordered_map<int, grit_edge::State>, 2> gritEdgeStateByChannel;
    std::unordered_map<int, stasis_cloud::Settings> stasisCloudFxByObject;
    std::unordered_map<int, echo_bleed::Settings> delayFxByObject;
    std::array<std::unordered_map<int, echo_bleed::State>, 2> echoBleedStateByChannel;
    std::array<std::array<int, ObjectDatabase::NUM_BINS>, 2> delayTailOwnerByChannel{};
    std::unordered_map<int, space_blur::Settings> spaceBlurFxByObject;
    std::array<std::unordered_map<int, stasis_cloud::State>, 2> stasisCloudStateByChannel;
    // Bin-lock during freeze: once a bin is captured by a frozen object it keeps
    // ownership of that bin (per channel) until the object's freeze is released,
    // even if the per-frame dominant-object assignment flickers. Prevents the
    // frozen spectrum from jumping between unrelated states every frame.
    std::array<std::array<int, ObjectDatabase::NUM_BINS>, 2> stasisFreezeOwnerByChannel{};
    std::array<std::unordered_map<int, space_blur::State>, 2> spaceBlurStateByChannel;
    std::array<std::array<int, ObjectDatabase::NUM_BINS>, 2> spaceBlurTailOwnerByChannel{};
    std::array<std::unordered_map<int, TransformSmoothState>, 2> transformSmoothStates;
    mutable juce::CriticalSection transformFileLock;
    std::unordered_map<int, TransformFileData> transformFileBuffer;
    std::array<float, ObjectDatabase::NUM_BINS> currentAnalysisMagnitudes{};
    std::atomic<float> currentTempoBpm { 120.0f };

    void createHannWindow();
    void processStftFrame(int channel, int64_t currentSampleIndex);
    // Pipeline pre/post-ISTFT split. When this runs, the spectral (pre-ISTFT)
    // chain has already produced the processed spectrum in fftData. It splits
    // that spectrum into per-object slices using soft partition-of-unity masks,
    // runs a per-object ISTFT, applies the time-domain post-ISTFT chain to each
    // object's isolated audio, and overlap-adds every object plus the unowned
    // "rest" spectrum into the shared output ring (latency-aligned: all slices
    // share the same window and write position, so the summation cannot comb).
    void reconstructAndOverlapAdd(int channel, int64_t currentSampleIndex);
    void applyPostIstftChain(int channel, int objectId, float* timeFrame, int numSamples);
    void applyPhaseVocoderPitchShift(int channel);
    void applyTransformCrossSynthesis(int channel);
    void applyEchoBleedDelay(int channel);
    void applyStasisCloud(int channel);
    void applySpaceBlur(int channel);
    void updateTargetBinGains();
    void analyseSegmentationFrame(const float* fftInterleaved, int64_t currentSampleIndex);
    void applyCosineMaskSmoothing(const std::array<float, SpectralFrameBuffer::NUM_BINS>& input,
                                  std::array<float, SpectralFrameBuffer::NUM_BINS>& output) const;
    void resetAutoDetectAccumulation();
    void finalizeAutoDetectedObjects();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};