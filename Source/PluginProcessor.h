#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/SpectralFrameBuffer.h"
#include "DSP/ObjectDatabase.h"
#include <memory>

// Version tracking
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 2;
constexpr int VERSION_BUILD = 3;

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

private:
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* dryWetParam = nullptr;

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;
    static constexpr int delaySamples = fftSize - hopSize;
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

    int64_t totalSamplesProcessed = 0;

    std::unique_ptr<SpectralFrameBuffer> spectralFrameBuffer;
    std::unique_ptr<ObjectDatabase> objectDatabase;

    void createHannWindow();
    void processStftFrame(int channel, int64_t currentSampleIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};