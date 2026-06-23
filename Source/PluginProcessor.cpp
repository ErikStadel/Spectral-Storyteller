#include "PluginProcessor.h"
#include "PluginEditor.h"

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("Parameters"),
                 { std::make_unique<juce::AudioParameterFloat>("dryWet", "Dry/Wet", 0.0f, 1.0f, 1.0f) }),
      fft(fftOrder),
      window(fftSize),
      fftData(2 * fftSize),
      spectralFrameBuffer(std::make_unique<SpectralFrameBuffer>()),
      objectDatabase(std::make_unique<ObjectDatabase>())
{
    dryWetParam = parameters.getRawParameterValue("dryWet");

    for (int ch = 0; ch < 2; ++ch)
    {
        inputBuffers[ch].assign(fftSize, 0.0f);
        outputBuffers[ch].assign(outputBufferSize, 0.0f);
        outputNormBuffers[ch].assign(outputBufferSize, 0.0f);
    }
}

PluginProcessor::~PluginProcessor() = default;

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);

    createHannWindow();

    for (int ch = 0; ch < 2; ++ch)
    {
        inputBuffers[ch].assign(fftSize, 0.0f);
        outputBuffers[ch].assign(outputBufferSize, 0.0f);
        outputNormBuffers[ch].assign(outputBufferSize, 0.0f);
        inputWritePos[ch] = 0;
        samplesInBuffer[ch] = 0;
        samplesSinceLastFrame[ch] = 0;
    }

    totalSamplesProcessed = 0;
    setLatencySamples(delaySamples);
}

void PluginProcessor::releaseResources()
{
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processStftFrame(int channel, int64_t currentSampleIndex)
{
    // Read one analysis frame from the circular input buffer and apply analysis window.
    const int frameStart = inputWritePos[channel];
    for (int i = 0; i < fftSize; ++i)
        fftData[i] = inputBuffers[channel][(frameStart + i) % fftSize] * window[i];

    std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);
    fft.performRealOnlyForwardTransform(fftData.data(), false);

    // Write frame to spectral buffer (only from first channel to avoid duplication)
    if (channel == 0)
        spectralFrameBuffer->writeFrame(fftData.data(), currentSampleIndex);

    // PR1 passthrough: no spectral modification.
    fft.performRealOnlyInverseTransform(fftData.data());

    // JUCE real-only inverse returns values scaled by fftSize.
    const float inverseScale = 1.0f / static_cast<float>(fftSize);
    const int writeBase = static_cast<int>((currentSampleIndex + delaySamples) % outputBufferSize);

    for (int i = 0; i < fftSize; ++i)
    {
        const float w = window[i];
        const float sample = (fftData[i] * inverseScale) * w;
        const int writePos = (writeBase + i) % outputBufferSize;
        outputBuffers[channel][writePos] += sample;
        outputNormBuffers[channel][writePos] += (w * w);
    }
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

    const float wet = juce::jlimit(0.0f, 1.0f, dryWetParam->load());
    const float dry = 1.0f - wet;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* channelData = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const int64_t currentSampleIndex = totalSamplesProcessed + i;
            const int bufferPos = static_cast<int>(currentSampleIndex % outputBufferSize);
            const float inputSample = channelData[i];

            inputBuffers[ch][inputWritePos[ch]] = inputSample;
            inputWritePos[ch] = (inputWritePos[ch] + 1) % fftSize;

            if (samplesInBuffer[ch] < fftSize)
                ++samplesInBuffer[ch];

            if (++samplesSinceLastFrame[ch] >= hopSize
                && samplesInBuffer[ch] == fftSize)
            {
                processStftFrame(ch, currentSampleIndex);
                samplesSinceLastFrame[ch] = 0;
            }

            const float processedSample = inputSample;

            channelData[i] = dry * inputSample + wet * processedSample;
            outputBuffers[ch][bufferPos] = 0.0f;
            outputNormBuffers[ch][bufferPos] = 0.0f;
        }
    }

    totalSamplesProcessed += numSamples;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

bool PluginProcessor::hasEditor() const
{
    return true;
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

void PluginProcessor::createHannWindow()
{
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<double>::pi * i / (fftSize - 1)));
}

//==============================================================================
// Factory function required by JUCE to create the plugin instance
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}