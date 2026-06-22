#pragma once

#include <JuceHeader.h>

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
    std::array<int, 2> inputWritePos{ 0, 0 };
    std::array<int, 2> samplesInBuffer{ 0, 0 };
    std::array<int, 2> samplesSinceLastFrame{ 0, 0 };
    std::vector<float> fftData;

    int64_t totalSamplesProcessed = 0;

    void createHannWindow();
    void processStftFrame(int channel, int64_t currentSampleIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};