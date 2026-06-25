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

    targetBinGains.fill(1.0f);
    timelineObjectGains.fill(1.0f);
    currentTimelineObjectGains.fill(1.0f);
    previousMagnitudes.fill(0.0f);
    lastFlatness.fill(0.0f);
    overlayTransient.fill(0.0f);
    overlayTonal.fill(0.0f);
    overlayNoise.fill(0.0f);
    accumulatedTransient.fill(0.0f);
    accumulatedTonal.fill(0.0f);
    accumulatedNoise.fill(0.0f);
    peakTransient.fill(0.0f);
    peakTonal.fill(0.0f);
    peakNoise.fill(0.0f);
    recordedMagnitudeFrames.clear();
    previousLogMagnitudes.fill(0.0f);
    tonalPersistence.fill(0.0f);
    spectralFluxHistory.clear();
    hfcHistory.clear();
    odfHistory.clear();
    transientMeanHistory.clear();
    tonalMeanHistory.clear();
    noiseMeanHistory.clear();
    hasPreviousMagnitudes = false;
    autoDetectActive = false;
    autoDetectRecording = false;
    overlayValid = false;
    transientHoldFrames = 0;
    autoDetectFrameCount = 0;
    autoDetectTransientFrameCount = 0;
    autoDetectNonTransientFrameCount = 0;

    for (int ch = 0; ch < 2; ++ch)
    {
        inputBuffers[ch].assign(fftSize, 0.0f);
        outputBuffers[ch].assign(outputBufferSize, 0.0f);
        outputNormBuffers[ch].assign(outputBufferSize, 0.0f);
        currentBinGains[ch].fill(1.0f);
    }
}

PluginProcessor::~PluginProcessor() = default;

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    currentSampleRate = juce::jmax(1.0, sampleRate);
    // 30 ms one-pole smoothing for dry<->STFT crossfade.
    const double tauSeconds = 0.03;
    stftBlendCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (tauSeconds * currentSampleRate)));
    stftBlend = 0.0f;

    createHannWindow();

    for (int ch = 0; ch < 2; ++ch)
    {
        inputBuffers[ch].assign(fftSize, 0.0f);
        outputBuffers[ch].assign(outputBufferSize, 0.0f);
        outputNormBuffers[ch].assign(outputBufferSize, 0.0f);
        inputWritePos[ch] = 0;
        samplesInBuffer[ch] = 0;
        samplesSinceLastFrame[ch] = 0;
        currentBinGains[ch].fill(1.0f);
    }

    targetBinGains.fill(1.0f);
    timelineObjectGains.fill(1.0f);
    currentTimelineObjectGains.fill(1.0f);
    previousMagnitudes.fill(0.0f);
    previousLogMagnitudes.fill(0.0f);
    lastFlatness.fill(0.0f);
    tonalPersistence.fill(0.0f);
    overlayTransient.fill(0.0f);
    overlayTonal.fill(0.0f);
    overlayNoise.fill(0.0f);
    peakTransient.fill(0.0f);
    peakTonal.fill(0.0f);
    peakNoise.fill(0.0f);
    recordedMagnitudeFrames.clear();
    spectralFluxHistory.clear();
    hfcHistory.clear();
    odfHistory.clear();
    transientMeanHistory.clear();
    tonalMeanHistory.clear();
    noiseMeanHistory.clear();
    hasPreviousMagnitudes = false;
    autoDetectActive = false;
    autoDetectRecording = false;
    overlayValid = false;
    transientHoldFrames = 0;
    resetAutoDetectAccumulation();

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

void PluginProcessor::updateTargetBinGains()
{
    const double nowSec = transportSeconds.load();

    // Timeline interpolation per object (linear from keyframes).
    for (int obj = 0; obj < ObjectDatabase::MAX_OBJECTS; ++obj)
    {
        timelineObjectGains[static_cast<size_t>(obj)] =
            timelineData.getInterpolatedValue(obj, nowSec);
    }

    // Smooth object gains to avoid zipper noise from timeline updates.
    static constexpr float objectGainAlpha = 0.20f;
    for (int obj = 0; obj < ObjectDatabase::MAX_OBJECTS; ++obj)
    {
        float& g = currentTimelineObjectGains[static_cast<size_t>(obj)];
        const float target = timelineObjectGains[static_cast<size_t>(obj)];
        g = objectGainAlpha * target + (1.0f - objectGainAlpha) * g;
    }

    // Default: full passthrough when no objects exist
    if (objectDatabase == nullptr || objectDatabase->getNumObjects() == 0)
    {
        targetBinGains.fill(1.0f);
        return;
    }

    // --- Step 1: Build object-aware gain mask ---
    // Baseline pass-through unless solo constrains it.
    std::array<float, ObjectDatabase::NUM_BINS> raw{};
    raw.fill(1.0f);

    const int numObjects = objectDatabase->getNumObjects();
    bool anySolo = false;
    for (int obj = 0; obj < numObjects; ++obj)
    {
        ObjectDatabase::ObjectMask item;
        if (objectDatabase->getObjectCopy(obj, item) && item.solo)
        {
            anySolo = true;
            break;
        }
    }

    if (anySolo)
    {
        raw.fill(0.0f);

        for (int obj = 0; obj < numObjects; ++obj)
        {
            ObjectDatabase::ObjectMask item;
            if (!objectDatabase->getObjectCopy(obj, item))
                continue;

            if (!item.solo)
                continue;

            const float objectGain = currentTimelineObjectGains[static_cast<size_t>(obj)];
            for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
            {
                if (item.mask[static_cast<size_t>(bin)])
                    raw[static_cast<size_t>(bin)] = juce::jmax(raw[static_cast<size_t>(bin)], objectGain);
            }
        }
    }
    else
    {
        for (int obj = 0; obj < numObjects; ++obj)
        {
            ObjectDatabase::ObjectMask item;
            if (!objectDatabase->getObjectCopy(obj, item))
                continue;

            if (item.mute)
                continue;

            const float objectGain = currentTimelineObjectGains[static_cast<size_t>(obj)];
            for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
            {
                if (item.mask[static_cast<size_t>(bin)])
                    raw[static_cast<size_t>(bin)] *= objectGain;
            }
        }
    }

    // Step B: apply all mute masks after solo/base composition.
    for (int obj = 0; obj < numObjects; ++obj)
    {
        ObjectDatabase::ObjectMask item;
        if (!objectDatabase->getObjectCopy(obj, item) || !item.mute)
            continue;

        for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
        {
            if (item.mask[static_cast<size_t>(bin)])
                raw[static_cast<size_t>(bin)] *= 0.0f;
        }
    }

    // --- Step 2: Soft-edge smoothing with half-cosine kernel ---
    // Convolves binary mask with a cosine window of radius 6 bins.
    // This replaces hard brickwall edges with smooth fade-in/out transitions,
    // preventing spectral leakage and click artefacts at mask boundaries.
    static constexpr int radius = 6;
    for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
    {
        float weighted = 0.0f;
        float totalWeight = 0.0f;

        for (int k = -radius; k <= radius; ++k)
        {
            const int idx = juce::jlimit(0, ObjectDatabase::NUM_BINS - 1, bin + k);
            // Cosine weight: 1 at centre, 0 at ±(radius+1)
            const float w = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi
                * static_cast<float>(k) / static_cast<float>(radius + 1)));
            weighted    += raw[static_cast<size_t>(idx)] * w;
            totalWeight += w;
        }

        targetBinGains[static_cast<size_t>(bin)] =
            (totalWeight > 0.0f) ? (weighted / totalWeight) : raw[static_cast<size_t>(bin)];
    }
    // Note: NO additional gain compensation here.
    // The OLA normalisation (division by sum-of-squared-windows) already
    // ensures that passthrough gain = 1. Solo/Mute correctly attenuate
    // the selected bins without boosting anything.
}

void PluginProcessor::processStftFrame(int channel, int64_t currentSampleIndex)
{
    // --- Analysis window + FFT ---
    // Clear entire fftData buffer before use to prevent any residual state.
    juce::FloatVectorOperations::clear(fftData.data(), 2 * fftSize);

    const int frameStart = inputWritePos[channel];
    for (int i = 0; i < fftSize; ++i)
        fftData[i] = inputBuffers[channel][(frameStart + i) % fftSize] * window[i];

    fft.performRealOnlyForwardTransform(fftData.data(), false);

    // Write frame to spectral buffer for visualisation (first channel only)
    if (channel == 0)
    {
        spectralFrameBuffer->writeFrame(fftData.data(), currentSampleIndex);
        analyseSegmentationFrame(fftData.data(), currentSampleIndex);
    }

    // --- Spectral mask ---
    // Recompute target gains once per frame (same cost for both channels).
    if (channel == 0)
        updateTargetBinGains();

    // JUCE real-only format returns N complex bins interleaved as float array:
    //   re(k) = fftData[2*k], im(k) = fftData[2*k+1]
    // For real input, bins k=0..N/2 are independent.
    constexpr int nyquistBin = fftSize / 2;  // = 1024

    // DC (k=0)
    {
        float& gain = currentBinGains[channel][0];
        gain = maskSmoothAlpha * targetBinGains[0] + (1.0f - maskSmoothAlpha) * gain;
        fftData[0] *= gain;
        fftData[1] *= gain;
    }

    // Regular positive bins
    for (int bin = 1; bin < nyquistBin; ++bin)
    {
        float& gain = currentBinGains[channel][static_cast<size_t>(bin)];
        gain = maskSmoothAlpha * targetBinGains[static_cast<size_t>(bin)] + (1.0f - maskSmoothAlpha) * gain;
        const int reIdx = 2 * bin;
        const int imIdx = reIdx + 1;
        fftData[reIdx] *= gain;
        fftData[imIdx] *= gain;
    }

    // Nyquist (k=N/2)
    {
        float& gain = currentBinGains[channel][static_cast<size_t>(nyquistBin)];
        gain = maskSmoothAlpha * targetBinGains[static_cast<size_t>(nyquistBin)] + (1.0f - maskSmoothAlpha) * gain;
        const int reIdx = 2 * nyquistBin;
        const int imIdx = reIdx + 1;
        fftData[reIdx] *= gain;
        fftData[imIdx] *= gain;
    }

    fft.performRealOnlyInverseTransform(fftData.data());

    // Causal OLA write position: never write into the past.
    const int64_t writeBase64 = currentSampleIndex;
    const int writeBase = static_cast<int>(((writeBase64 % outputBufferSize) + outputBufferSize) % outputBufferSize);

    for (int i = 0; i < fftSize; ++i)
    {
        const float w = window[i];
        const float sample = fftData[i] * w;
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

    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            if (auto s = pos->getTimeInSeconds())
                transportSeconds.store(*s);
            else
                transportSeconds.store(static_cast<double>(totalSamplesProcessed) / currentSampleRate);

            transportPlaying.store(pos->getIsPlaying());
        }
        else
        {
            transportSeconds.store(static_cast<double>(totalSamplesProcessed) / currentSampleRate);
            transportPlaying.store(false);
        }
    }
    else
    {
        transportSeconds.store(static_cast<double>(totalSamplesProcessed) / currentSampleRate);
        transportPlaying.store(false);
    }
    const bool maskingActive = (objectDatabase != nullptr && objectDatabase->isAnyMaskingActive());

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

            const float norm = outputNormBuffers[ch][bufferPos];
            const float stftSample = (norm > 1.0e-9f)
                ? (outputBuffers[ch][bufferPos] / norm)
                : inputSample;

            // Smoothly crossfade STFT path in/out to avoid hard route switches.
            const float targetBlend = maskingActive ? 1.0f : 0.0f;
            stftBlend += (targetBlend - stftBlend) * stftBlendCoeff;
            stftBlend = juce::jlimit(0.0f, 1.0f, stftBlend);

            const float processedSample = stftBlend * stftSample + (1.0f - stftBlend) * inputSample;

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
    auto state = parameters.copyState();

    const auto existing = state.getChildWithName("TimelineData");
    if (existing.isValid())
        state.removeChild(existing, nullptr);

    state.addChild(timelineData.toValueTree(), -1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml(*xmlState);

        const auto timelineTree = state.getChildWithName("TimelineData");
        if (timelineTree.isValid())
        {
            timelineData.fromValueTree(timelineTree);
            state.removeChild(timelineTree, nullptr);
        }

        parameters.replaceState(state);
    }
}

void PluginProcessor::addTimelineKeyframe(int objectIndex, double timeSec, float value)
{
    if (objectIndex < 0 || objectIndex >= ObjectDatabase::MAX_OBJECTS)
        return;

    timelineData.addKeyframe(objectIndex, timeSec, value);
}

void PluginProcessor::deleteTimelineKeyframe(int objectIndex, double timeSec)
{
    if (objectIndex < 0 || objectIndex >= ObjectDatabase::MAX_OBJECTS)
        return;

    timelineData.deleteKeyframe(objectIndex, timeSec);
}

std::vector<TimelineData::Keyframe> PluginProcessor::getTimelineKeyframes(int objectIndex) const
{
    if (objectIndex < 0 || objectIndex >= ObjectDatabase::MAX_OBJECTS)
        return {};

    return timelineData.getKeyframes(objectIndex);
}

juce::String PluginProcessor::getTimelineTrackName(int objectIndex) const
{
    if (objectDatabase == nullptr || objectIndex < 0 || objectIndex >= objectDatabase->getNumObjects())
        return "?";

    ObjectDatabase::ObjectMask item;
    if (!objectDatabase->getObjectCopy(objectIndex, item))
        return "?";

    return item.name;
}

void PluginProcessor::setTimelineTrackName(int objectIndex, const std::string& newName)
{
    if (objectDatabase == nullptr || objectIndex < 0 || objectIndex >= objectDatabase->getNumObjects())
        return;

    objectDatabase->setObjectName(objectIndex, newName);
}

void PluginProcessor::requestAutoDetectObjects(double captureSeconds)
{
    const double seconds = juce::jlimit(0.5, 10.0, captureSeconds);

    juce::ScopedLock sl(segmentationLock);
    autoDetectTargetSamples = static_cast<int64_t>(seconds * currentSampleRate);
    autoDetectStartSample = totalSamplesProcessed;
    autoDetectActive = true;
    autoDetectRecording = false;
    resetAutoDetectAccumulation();
}

void PluginProcessor::setAutoDetectRecordingEnabled(bool shouldRecord)
{
    juce::ScopedLock sl(segmentationLock);

    if (shouldRecord)
    {
        autoDetectRecording = true;
        autoDetectActive = false;
        resetAutoDetectAccumulation();
        return;
    }

    const bool wasRecording = autoDetectRecording;
    autoDetectRecording = false;

    if (wasRecording)
        finalizeAutoDetectedObjects();
}

bool PluginProcessor::getSegmentationOverlay(std::array<float, SpectralFrameBuffer::NUM_BINS>& transient,
                                             std::array<float, SpectralFrameBuffer::NUM_BINS>& tonal,
                                             std::array<float, SpectralFrameBuffer::NUM_BINS>& noise) const
{
    juce::ScopedLock sl(segmentationLock);
    transient = overlayTransient;
    tonal = overlayTonal;
    noise = overlayNoise;
    return overlayValid;
}

juce::String PluginProcessor::getSegmentationDebugText() const
{
    juce::ScopedLock sl(segmentationLock);

    float tMean = 0.0f;
    float tonalMean = 0.0f;
    float noiseMean = 0.0f;
    for (int k = 0; k < SpectralFrameBuffer::NUM_BINS; ++k)
    {
        tMean += overlayTransient[static_cast<size_t>(k)];
        tonalMean += overlayTonal[static_cast<size_t>(k)];
        noiseMean += overlayNoise[static_cast<size_t>(k)];
    }

    const float invBins = 1.0f / static_cast<float>(SpectralFrameBuffer::NUM_BINS);
    tMean *= invBins;
    tonalMean *= invBins;
    noiseMean *= invBins;

    auto getMinMax = [](const std::deque<float>& hist, float fallback)
    {
        float minV = fallback;
        float maxV = fallback;
        if (!hist.empty())
        {
            minV = hist.front();
            maxV = hist.front();
            for (const float v : hist)
            {
                minV = juce::jmin(minV, v);
                maxV = juce::jmax(maxV, v);
            }
        }
        return std::pair<float, float>{ minV, maxV };
    };

    const auto [tMin, tMax] = getMinMax(transientMeanHistory, tMean);
    const auto [tonMin, tonMax] = getMinMax(tonalMeanHistory, tonalMean);
    const auto [nMin, nMax] = getMinMax(noiseMeanHistory, noiseMean);
    const auto [odfMin, odfMax] = getMinMax(odfHistory, 0.0f);
    const auto [fluxMin, fluxMax] = getMinMax(spectralFluxHistory, 0.0f);
    const auto [hfcMin, hfcMax] = getMinMax(hfcHistory, 0.0f);

    juce::String dbg = "Ranges";
    dbg += "\nT:" + juce::String(tMin * 100.0f, 1) + "-" + juce::String(tMax * 100.0f, 1) + "%";
    dbg += " Ton:" + juce::String(tonMin * 100.0f, 1) + "-" + juce::String(tonMax * 100.0f, 1) + "%";
    dbg += " N:" + juce::String(nMin * 100.0f, 1) + "-" + juce::String(nMax * 100.0f, 1) + "%";
    dbg += "\nODF:" + juce::String(odfMin, 2) + "-" + juce::String(odfMax, 2);
    dbg += " Flux:" + juce::String(fluxMin, 2) + "-" + juce::String(fluxMax, 2);
    dbg += " HFC:" + juce::String(hfcMin, 2) + "-" + juce::String(hfcMax, 2);
    dbg += " Frames:" + juce::String(autoDetectFrameCount);
    return dbg;
}

void PluginProcessor::analyseSegmentationFrame(const float* fftInterleaved, int64_t currentSampleIndex)
{
    juce::ScopedLock sl(segmentationLock);

    constexpr int numBins = SpectralFrameBuffer::NUM_BINS;
    constexpr float eps = 1.0e-9f;

    std::array<float, numBins> magLin{};
    std::array<float, numBins> rawMagLin{};
    std::array<float, numBins> logMag{};
    std::array<float, numBins> posDeltaLog{};
    std::array<float, numBins> tonalMask{};
    std::array<float, numBins> transientMask{};
    std::array<float, numBins> noiseMask{};

    // Normalize per-frame magnitude so feature scales are content-level independent.
    float magSum = 0.0f;
    float hfc = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        const int reIdx = 2 * k;
        const int imIdx = reIdx + 1;
        const float re = fftInterleaved[reIdx];
        const float im = fftInterleaved[imIdx];
        const float rawMag = std::sqrt(re * re + im * im) + eps;
        rawMagLin[static_cast<size_t>(k)] = rawMag;
        magLin[static_cast<size_t>(k)] = rawMag;
        magSum += rawMag;
    }

    const float frameMeanMag = juce::jmax(eps, magSum / static_cast<float>(numBins));
    for (int k = 0; k < numBins; ++k)
    {
        magLin[static_cast<size_t>(k)] /= frameMeanMag;
        logMag[static_cast<size_t>(k)] = std::log(magLin[static_cast<size_t>(k)] + eps);

        const float kNorm = static_cast<float>(k + 1) / static_cast<float>(numBins);
        hfc += kNorm * magLin[static_cast<size_t>(k)];
    }

    float spectralFlux = 0.0f;
    if (hasPreviousMagnitudes)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const float delta = logMag[static_cast<size_t>(k)] - previousLogMagnitudes[static_cast<size_t>(k)];
            posDeltaLog[static_cast<size_t>(k)] = juce::jmax(0.0f, delta);
            spectralFlux += posDeltaLog[static_cast<size_t>(k)];
        }
    }

    // === ODF & Transient Detection (bestehend, leicht optimiert) ===
    auto calcZScore = [](float v, const std::deque<float>& hist)
    {
        if (hist.size() < 8)
            return 0.0f;

        float mean = 0.0f;
        for (const auto x : hist)
            mean += x;
        mean /= static_cast<float>(hist.size());

        float var = 0.0f;
        for (const auto x : hist)
        {
            const float d = x - mean;
            var += d * d;
        }
        var /= static_cast<float>(hist.size());
        const float stdev = std::sqrt(juce::jmax(1.0e-8f, var));
        return (v - mean) / stdev;
    };

    const float fluxZ = calcZScore(spectralFlux, spectralFluxHistory);
    const float hfcZ = calcZScore(hfc, hfcHistory);
    const float odf = fluxZ + 0.55f * hfcZ;

    auto medianMad = [](const std::deque<float>& hist, float& median, float& mad)
    {
        median = 0.0f;
        mad = 0.0f;
        if (hist.size() < 8)
            return;

        std::vector<float> sorted(hist.begin(), hist.end());
        std::sort(sorted.begin(), sorted.end());
        median = sorted[sorted.size() / 2];

        std::vector<float> absDev;
        absDev.reserve(sorted.size());
        for (const auto x : sorted)
            absDev.push_back(std::abs(x - median));
        std::sort(absDev.begin(), absDev.end());
        mad = absDev[absDev.size() / 2] * 1.4826f;
    };

    float odfMedian = 0.0f, odfMad = 0.0f;
    medianMad(odfHistory, odfMedian, odfMad);

    bool isTransientFrame = false;
    const float odfThreshold = odfMedian + 2.0f * juce::jmax(odfMad, 0.12f);
    if (hasPreviousMagnitudes && odf > odfThreshold)
    {
        isTransientFrame = true;
        transientHoldFrames = 3;
    }
    else if (transientHoldFrames > 0)
    {
        isTransientFrame = true;
        --transientHoldFrames;
    }

    spectralFluxHistory.push_back(spectralFlux);
    hfcHistory.push_back(hfc);
    odfHistory.push_back(odf);
   static constexpr size_t histWindow = 96;
    while (spectralFluxHistory.size() > histWindow)
        spectralFluxHistory.pop_front();
    while (hfcHistory.size() > histWindow)
        hfcHistory.pop_front();
    while (odfHistory.size() > histWindow)
        odfHistory.pop_front();

    // === Spectral Flatness (bestehend) ===
    for (int k = 0; k < numBins; ++k)
    {
        const int lo = juce::jmax(0, k - 3);
        const int hi = juce::jmin(numBins - 1, k + 3);
        float geoLogSum = 0.0f, arithSum = 0.0f;
        int n = 0;
        for (int i = lo; i <= hi; ++i)
        {
            const float m = magLin[static_cast<size_t>(i)];
            geoLogSum += std::log(m);
            arithSum += m;
            ++n;
        }
        const float geo = std::exp(geoLogSum / static_cast<float>(juce::jmax(1, n)));
        const float arith = arithSum / static_cast<float>(juce::jmax(1, n));
        lastFlatness[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, geo / (arith + eps));
    }

    if (useHPSSPrePass)
    {
        hpHarmonicMask.fill(0.5f);
        hpPercussiveMask.fill(0.5f);

        std::array<float, numBins> tempH{}, tempP{};

        for (int iter = 0; iter < hpssIterations; ++iter)
        {
            for (int k = 0; k < numBins; ++k)
            {
                float vals[5] = {
                    magLin[static_cast<size_t>(juce::jmax(0, k - 2))],
                    magLin[static_cast<size_t>(juce::jmax(0, k - 1))],
                    magLin[static_cast<size_t>(k)],
                    magLin[static_cast<size_t>(juce::jmin(numBins - 1, k + 1))],
                    magLin[static_cast<size_t>(juce::jmin(numBins - 1, k + 2))]
                };
                std::sort(std::begin(vals), std::end(vals));
                tempH[static_cast<size_t>(k)] = vals[2];
            }

            for (int k = 0; k < numBins; ++k)
            {
                const float prev = hasPreviousMagnitudes
                    ? previousMagnitudes[static_cast<size_t>(k)]
                    : magLin[static_cast<size_t>(k)];

                const float diff = magLin[static_cast<size_t>(k)] - prev;
                tempP[static_cast<size_t>(k)] = juce::jmax(0.0f, diff) * 1.2f + std::abs(diff) * 0.2f + eps;
            }

            for (int k = 0; k < numBins; ++k)
            {
                const float harmonicEnergy = tempH[static_cast<size_t>(k)] + eps;
                const float percussiveEnergy = tempP[static_cast<size_t>(k)] + eps;
                const float denom = harmonicEnergy + percussiveEnergy;

                const float hMask = harmonicEnergy / denom;
                const float pMask = percussiveEnergy / denom;

                hpHarmonicMask[static_cast<size_t>(k)] =
                    0.7f * hpHarmonicMask[static_cast<size_t>(k)] + 0.3f * juce::jlimit(0.0f, 1.0f, hMask);
                hpPercussiveMask[static_cast<size_t>(k)] =
                    0.7f * hpPercussiveMask[static_cast<size_t>(k)] + 0.3f * juce::jlimit(0.0f, 1.0f, pMask);
            }
        }
    }

    // === NEU: Hybrid Features ===
    if (useHybridMode)
    {
        // Harmonic Product Spectrum
        for (int k = 0; k < numBins; ++k)
        {
            float hps = magLin[static_cast<size_t>(k)];
            if (k >= 2) hps *= magLin[static_cast<size_t>(k/2)];
            if (k >= 3) hps *= magLin[static_cast<size_t>(k/3)];
            if (k >= 4) hps *= magLin[static_cast<size_t>(k/4)];
            hpsScore[static_cast<size_t>(k)] = std::pow(hps, 0.25f);
        }

        float minHps = hpsScore[0];
        float maxHps = hpsScore[0];
        for (int k = 1; k < numBins; ++k)
        {
            minHps = juce::jmin(minHps, hpsScore[static_cast<size_t>(k)]);
            maxHps = juce::jmax(maxHps, hpsScore[static_cast<size_t>(k)]);
        }
        const float hpsRange = juce::jmax(1.0e-6f, maxHps - minHps);
        for (int k = 0; k < numBins; ++k)
            hpsScore[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, (hpsScore[static_cast<size_t>(k)] - minHps) / hpsRange);

        // Broadband Flux
        float lowFluxSum = 0.0f, highFluxSum = 0.0f;
        const int lowCut = numBins / 8;
        const int highCut = numBins * 3 / 4;

        for (int k = 0; k < numBins; ++k)
        {
            const float delta = posDeltaLog[static_cast<size_t>(k)];
            if (k < lowCut) lowFluxSum += delta;
            if (k > highCut) highFluxSum += delta;
        }

        const bool isBroadband = (lowFluxSum > 0.30f && highFluxSum > 0.18f);
        for (int k = 0; k < numBins; ++k)
        {
            const float delta = posDeltaLog[static_cast<size_t>(k)];
            broadbandFlux[static_cast<size_t>(k)] = delta * (isBroadband ? 1.35f : 1.0f);
        }
    }

    // === NEU: Log-Attack-Time Berechnung ===
    float totalAttackSlope = 0.0f;
    int attackBins = 0;

    if (isTransientFrame)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const float currentEnergy = magLin[static_cast<size_t>(k)];
            const float prevEnergy = hasPreviousMagnitudes ? previousMagnitudes[static_cast<size_t>(k)] : currentEnergy;
            
            // Steilheit des Anstiegs (Attack Slope)
            float slope = (currentEnergy - prevEnergy) / (prevEnergy + eps);
            attackSlope[static_cast<size_t>(k)] = slope;

            // Log-Attack-Time approximiert (je steiler, desto kleiner LAT)
            lastAttackTime[static_cast<size_t>(k)] = std::log10(1.0f / (slope + eps) + 1.0f);

            if (slope > 0.15f)  // nur starke Anstiege zählen
            {
                totalAttackSlope += slope;
                ++attackBins;
            }
        }

        globalAttackSlope = attackBins > 0 ? totalAttackSlope / attackBins : 0.0f;
    }

   // === Masken-Verfeinerung mit LAT ===
    if (isTransientFrame)
    {
        float meanPos = 0.0f;
        for (auto d : posDeltaLog) meanPos += d;
        meanPos /= numBins;

        const float thresh = juce::jmax(0.008f, meanPos * 0.95f);

        for (int k = 0; k < numBins; ++k)
        {
            float score = (posDeltaLog[static_cast<size_t>(k)] - thresh) / (thresh + eps);
            const float kNorm = static_cast<float>(k) / static_cast<float>(numBins - 1);

            // LAT-Boost: kurze Attack-Time = starker Transient
            if (useHybridMode)
            {
                const float latFactor = juce::jlimit(0.0f, 2.5f, 3.0f / (lastAttackTime[static_cast<size_t>(k)] + 0.3f));
                score *= (1.0f + broadbandFlux[static_cast<size_t>(k)] * 0.75f + latFactor * 0.65f);
            }

            if (useHPSSPrePass)
                score *= (0.55f + 0.9f * hpPercussiveMask[static_cast<size_t>(k)]);

            const float lowMidBoost = 1.0f + 0.25f * juce::jmax(0.0f, 1.0f - std::abs(kNorm - 0.28f) / 0.28f);
            const float ultraHighPenalty = juce::jlimit(0.72f, 1.0f, 1.0f - 0.55f * juce::jmax(0.0f, kNorm - 0.78f));
            score *= lowMidBoost * ultraHighPenalty;

            transientMask[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, score);
        }

        std::array<float, numBins> widenedTransient = transientMask;
        for (int k = 1; k < numBins - 1; ++k)
        {
            const float localPeak = juce::jmax(transientMask[static_cast<size_t>(k)],
                juce::jmax(transientMask[static_cast<size_t>(k - 1)], transientMask[static_cast<size_t>(k + 1)]));
            widenedTransient[static_cast<size_t>(k)] = juce::jmax(widenedTransient[static_cast<size_t>(k)], localPeak * 0.72f);
        }
        transientMask = widenedTransient;
    }

    // Tonal candidate extraction runs every frame. During transient frames,
    // persistence is frozen to avoid smearing short attacks into tonal masks.
    if (!isTransientFrame)
    {
        for (auto& p : tonalPersistence)
            p *= 0.94f;
    }

    for (int k = 2; k < numBins - 2; ++k)
    {
        // Bestehende Peak-Prominence-Logik
        const float centerLin = magLin[static_cast<size_t>(k)];
        if (!(centerLin > magLin[static_cast<size_t>(k-1)] && centerLin >= magLin[static_cast<size_t>(k+1)]))
            continue;

        std::array<float, 4> neighDb = {
            20.0f * std::log10(magLin[static_cast<size_t>(k - 2)] + eps),
            20.0f * std::log10(magLin[static_cast<size_t>(k - 1)] + eps),
            20.0f * std::log10(magLin[static_cast<size_t>(k + 1)] + eps),
            20.0f * std::log10(magLin[static_cast<size_t>(k + 2)] + eps)
        };
        std::sort(neighDb.begin(), neighDb.end());
        const float neighMedianDb = 0.5f * (neighDb[1] + neighDb[2]);
        const float centerDb = 20.0f * std::log10(centerLin + eps);
        const float prominenceDb = centerDb - neighMedianDb;

        float tonalCandidate = juce::jlimit(0.0f, 1.0f, (prominenceDb - 3.0f) / 5.0f);

        // Attenuate very high bands to reduce whistle-like tonal bleed.
        const float kNorm = static_cast<float>(k) / static_cast<float>(numBins - 1);
        const float highBandPenalty = juce::jlimit(0.45f, 1.0f, 1.0f - 0.95f * juce::jmax(0.0f, kNorm - 0.62f));
        tonalCandidate *= highBandPenalty;

        if (useHPSSPrePass)
        {
            tonalCandidate *= (0.45f + 0.65f * hpHarmonicMask[static_cast<size_t>(k)]);
            tonalCandidate *= (1.0f - 0.55f * hpPercussiveMask[static_cast<size_t>(k)]);
        }

        if (useHybridMode)
            tonalCandidate *= (0.55f + 0.75f * hpsScore[static_cast<size_t>(k)]);

        if (!isTransientFrame)
        {
            tonalPersistence[static_cast<size_t>(k)] =
                0.90f * tonalPersistence[static_cast<size_t>(k)] + 0.10f * tonalCandidate;
        }

        tonalMask[static_cast<size_t>(k)] =
            juce::jlimit(0.0f, 1.0f, (tonalPersistence[static_cast<size_t>(k)] - 0.14f) / 0.86f);
    }

    // === Ambient als verbesserte Restklasse ===
    for (int k = 0; k < numBins; ++k)
    {
        float tSup = 1.0f - tonalMask[static_cast<size_t>(k)];
        float trSup = 1.0f - transientMask[static_cast<size_t>(k)];
        float sfm = lastFlatness[static_cast<size_t>(k)];

        const float flatnessGate = juce::jlimit(0.0f, 1.0f, (sfm - 0.32f) / 0.40f);
        float noiseScore = flatnessGate * std::pow(juce::jmax(0.0f, tSup * trSup), 0.65f);

        if (useHPSSPrePass)
        {
            const float nonHarmonic = 1.0f - hpHarmonicMask[static_cast<size_t>(k)];
            const float nonPercussive = 1.0f - hpPercussiveMask[static_cast<size_t>(k)];
            noiseScore *= (0.35f + 0.65f * nonHarmonic) * (0.60f + 0.40f * nonPercussive);
        }

        if (useHybridMode)
            noiseScore *= (0.55f + 0.45f * (1.0f - hpsScore[static_cast<size_t>(k)]));

        noiseMask[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, noiseScore * 0.82f);
    }

    // === Smoothing & Overlay (unverändert) ===
    std::array<float, numBins> smoothTransient{}, smoothTonal{}, smoothNoise{};
    applyCosineMaskSmoothing(transientMask, smoothTransient);
    applyCosineMaskSmoothing(tonalMask, smoothTonal);
    applyCosineMaskSmoothing(noiseMask, smoothNoise);

    for (int k = 0; k < numBins; ++k)
    {
        const float transW = smoothTransient[static_cast<size_t>(k)] * 1.35f;
        const float tonalW = smoothTonal[static_cast<size_t>(k)] * 1.20f;
        const float noiseW = smoothNoise[static_cast<size_t>(k)] * 0.65f;
        const float maxW = juce::jmax(transW, juce::jmax(tonalW, noiseW));
        const float sum = transW + tonalW + noiseW;

        if (sum > 1.0e-5f && maxW > 0.05f)
        {
            overlayTransient[static_cast<size_t>(k)] = transW / sum;
            overlayTonal[static_cast<size_t>(k)] = tonalW / sum;
            overlayNoise[static_cast<size_t>(k)] = noiseW / sum;
        }
        else
        {
            overlayTransient[static_cast<size_t>(k)] = 0.0f;
            overlayTonal[static_cast<size_t>(k)] = 0.0f;
            overlayNoise[static_cast<size_t>(k)] = 0.0f;
        }

        if (autoDetectActive || autoDetectRecording)
        {
            accumulatedTransient[static_cast<size_t>(k)] += overlayTransient[static_cast<size_t>(k)];
            accumulatedTonal[static_cast<size_t>(k)] += overlayTonal[static_cast<size_t>(k)];
            accumulatedNoise[static_cast<size_t>(k)] += overlayNoise[static_cast<size_t>(k)];
            peakTransient[static_cast<size_t>(k)] = juce::jmax(peakTransient[static_cast<size_t>(k)], overlayTransient[static_cast<size_t>(k)]);
            peakTonal[static_cast<size_t>(k)] = juce::jmax(peakTonal[static_cast<size_t>(k)], overlayTonal[static_cast<size_t>(k)]);
            peakNoise[static_cast<size_t>(k)] = juce::jmax(peakNoise[static_cast<size_t>(k)], overlayNoise[static_cast<size_t>(k)]);
        }

        previousMagnitudes[static_cast<size_t>(k)] = magLin[static_cast<size_t>(k)];
        previousLogMagnitudes[static_cast<size_t>(k)] = logMag[static_cast<size_t>(k)];
    }

    // Track frame-level class means over a sliding window for debug stability.
    float frameTransientMean = 0.0f;
    float frameTonalMean = 0.0f;
    float frameNoiseMean = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        frameTransientMean += overlayTransient[static_cast<size_t>(k)];
        frameTonalMean += overlayTonal[static_cast<size_t>(k)];
        frameNoiseMean += overlayNoise[static_cast<size_t>(k)];
    }
    const float invBins = 1.0f / static_cast<float>(numBins);
    frameTransientMean *= invBins;
    frameTonalMean *= invBins;
    frameNoiseMean *= invBins;

    transientMeanHistory.push_back(frameTransientMean);
    tonalMeanHistory.push_back(frameTonalMean);
    noiseMeanHistory.push_back(frameNoiseMean);

    static constexpr size_t overlayHistWindow = 96;
    while (transientMeanHistory.size() > overlayHistWindow)
        transientMeanHistory.pop_front();
    while (tonalMeanHistory.size() > overlayHistWindow)
        tonalMeanHistory.pop_front();
    while (noiseMeanHistory.size() > overlayHistWindow)
        noiseMeanHistory.pop_front();

    overlayValid = true;
    hasPreviousMagnitudes = true;

    if (autoDetectActive || autoDetectRecording)
    {
        recordedMagnitudeFrames.push_back(rawMagLin);

        ++autoDetectFrameCount;
        if (isTransientFrame)
            ++autoDetectTransientFrameCount;
        else
            ++autoDetectNonTransientFrameCount;

        if (autoDetectRecording)
            return;

        if ((currentSampleIndex - autoDetectStartSample) >= autoDetectTargetSamples)
        {
            finalizeAutoDetectedObjects();
            autoDetectActive = false;
        }
    }
}

void PluginProcessor::applyCosineMaskSmoothing(const std::array<float, SpectralFrameBuffer::NUM_BINS>& input,
                                               std::array<float, SpectralFrameBuffer::NUM_BINS>& output) const
{
    static constexpr int radius = 6;
    for (int bin = 0; bin < SpectralFrameBuffer::NUM_BINS; ++bin)
    {
        float weighted = 0.0f;
        float totalWeight = 0.0f;

        for (int k = -radius; k <= radius; ++k)
        {
            const int idx = juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1, bin + k);
            const float w = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi
                * static_cast<float>(k) / static_cast<float>(radius + 1)));
            weighted += input[static_cast<size_t>(idx)] * w;
            totalWeight += w;
        }

        output[static_cast<size_t>(bin)] = (totalWeight > 0.0f) ? (weighted / totalWeight) : input[static_cast<size_t>(bin)];
    }
}

void PluginProcessor::resetAutoDetectAccumulation()
{
    accumulatedTransient.fill(0.0f);
    accumulatedTonal.fill(0.0f);
    accumulatedNoise.fill(0.0f);
    peakTransient.fill(0.0f);
    peakTonal.fill(0.0f);
    peakNoise.fill(0.0f);
    recordedMagnitudeFrames.clear();
    autoDetectFrameCount = 0;
    autoDetectTransientFrameCount = 0;
    autoDetectNonTransientFrameCount = 0;
}

void PluginProcessor::finalizeAutoDetectedObjects()
{
    if (objectDatabase == nullptr || autoDetectFrameCount <= 0)
        return;

    objectDatabase->clear();

    std::array<bool, SpectralFrameBuffer::NUM_BINS> transientMask{};
    std::array<bool, SpectralFrameBuffer::NUM_BINS> tonalMask{};
    std::array<bool, SpectralFrameBuffer::NUM_BINS> noiseMask{};

    if (recordedMagnitudeFrames.size() < 4)
    {
        objectDatabase->addObject("Transients");
        objectDatabase->setObjectMask(0, transientMask);
        objectDatabase->setObjectColor(0, static_cast<int>(juce::Colour(0xFFFF5252).getARGB()));

        objectDatabase->addObject("Tonal Components");
        objectDatabase->setObjectMask(1, tonalMask);
        objectDatabase->setObjectColor(1, static_cast<int>(juce::Colour(0xFF4AA3FF).getARGB()));

        objectDatabase->addObject("Ambient Noise");
        objectDatabase->setObjectMask(2, noiseMask);
        objectDatabase->setObjectColor(2, static_cast<int>(juce::Colour(0xFF4FD16A).getARGB()));
        return;
    }

    constexpr int numBins = SpectralFrameBuffer::NUM_BINS;
    constexpr float eps = 1.0e-9f;
    const int numFrames = static_cast<int>(recordedMagnitudeFrames.size());

    std::vector<std::array<float, numBins>> hBase(static_cast<size_t>(numFrames));
    std::vector<std::array<float, numBins>> pBase(static_cast<size_t>(numFrames));
    std::vector<std::array<float, numBins>> transientFinal(static_cast<size_t>(numFrames));
    std::vector<std::array<float, numBins>> tonalFinal(static_cast<size_t>(numFrames));
    std::vector<std::array<float, numBins>> ambientFinal(static_cast<size_t>(numFrames));

    // Pass 1: 2D HPSS base masks from fixed-size median-filtered energies.
    static constexpr int tonalWindowFrames = 15;
    static constexpr int transientWindowBins = 11;
    static constexpr int tRadius = tonalWindowFrames / 2;
    static constexpr int fRadius = transientWindowBins / 2;
    std::vector<float> work;
    work.reserve(static_cast<size_t>(juce::jmax(2 * tRadius + 1, 2 * fRadius + 1)));

    auto medianOf = [&work](int radius, auto valueAt)
    {
        work.clear();
        work.reserve(static_cast<size_t>(2 * radius + 1));
        for (int i = -radius; i <= radius; ++i)
            work.push_back(valueAt(i));

        const auto midIt = work.begin() + static_cast<std::ptrdiff_t>(work.size() / 2);
        std::nth_element(work.begin(), midIt, work.end());
        return *midIt;
    };

    for (int t = 0; t < numFrames; ++t)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const float harmonicEnergy = medianOf(tRadius, [&](int dt)
            {
                const int tt = juce::jlimit(0, numFrames - 1, t + dt);
                return recordedMagnitudeFrames[static_cast<size_t>(tt)][static_cast<size_t>(k)];
            });

            const float percussiveEnergy = medianOf(fRadius, [&](int df)
            {
                const int kk = juce::jlimit(0, numBins - 1, k + df);
                return recordedMagnitudeFrames[static_cast<size_t>(t)][static_cast<size_t>(kk)];
            });

            const float denom = harmonicEnergy + percussiveEnergy + eps;
            hBase[static_cast<size_t>(t)][static_cast<size_t>(k)] = harmonicEnergy / denom;
            pBase[static_cast<size_t>(t)][static_cast<size_t>(k)] = percussiveEnergy / denom;
        }
    }

    // Pass 2: rectified broadband flux gatekeeper on frame axis.
    std::vector<float> rectFlux(static_cast<size_t>(numFrames), 0.0f);
    for (int t = 1; t < numFrames; ++t)
    {
        float flux = 0.0f;
        for (int k = 0; k < numBins; ++k)
        {
            const float d = recordedMagnitudeFrames[static_cast<size_t>(t)][static_cast<size_t>(k)]
                          - recordedMagnitudeFrames[static_cast<size_t>(t - 1)][static_cast<size_t>(k)];
            const float pos = juce::jmax(0.0f, d);
            flux += pos * pos;
        }
        rectFlux[static_cast<size_t>(t)] = flux;
    }

    float fluxMean = 0.0f;
    for (const float v : rectFlux)
        fluxMean += v;
    fluxMean /= static_cast<float>(juce::jmax(1, numFrames));
    const float fluxThr = juce::jmax(1.0e-6f, fluxMean * 2.5f);

    std::vector<bool> onsetFrame(static_cast<size_t>(numFrames), false);
    for (int t = 1; t < numFrames - 1; ++t)
    {
        const float prev = rectFlux[static_cast<size_t>(t - 1)];
        const float curr = rectFlux[static_cast<size_t>(t)];
        const float next = rectFlux[static_cast<size_t>(t + 1)];
        const bool localPeak = (curr > prev) && (curr > next);
        onsetFrame[static_cast<size_t>(t)] = localPeak && (curr > fluxThr);
    }

    for (int t = 0; t < numFrames; ++t)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const float p = pBase[static_cast<size_t>(t)][static_cast<size_t>(k)];
            transientFinal[static_cast<size_t>(t)][static_cast<size_t>(k)] =
                onsetFrame[static_cast<size_t>(t)] ? 1.0f : (0.15f * p);
        }
    }

    // Pass 3: local SFM validation for tonal mask.
    std::vector<float> frameFluxNorm = rectFlux;
    float fluxMax = 0.0f;
    for (const float f : rectFlux)
        fluxMax = juce::jmax(fluxMax, f);
    const float invFluxMax = (fluxMax > eps) ? (1.0f / fluxMax) : 0.0f;
    for (auto& v : frameFluxNorm)
        v *= invFluxMax;

    std::vector<float> globalFlatness(static_cast<size_t>(numFrames), 0.0f);
    for (int t = 0; t < numFrames; ++t)
    {
        float logSum = 0.0f;
        float sum = 0.0f;
        for (int k = 0; k < numBins; ++k)
        {
            const float m = recordedMagnitudeFrames[static_cast<size_t>(t)][static_cast<size_t>(k)] + eps;
            logSum += std::log(m);
            sum += m;
        }
        const float geo = std::exp(logSum / static_cast<float>(numBins));
        const float arith = sum / static_cast<float>(numBins);
        globalFlatness[static_cast<size_t>(t)] = juce::jlimit(0.0f, 1.0f, geo / (arith + eps));
    }

    std::vector<std::array<float, numBins>> ambientBias(static_cast<size_t>(numFrames));

    static constexpr int sfmRadius = 4;
    for (int t = 0; t < numFrames; ++t)
    {
        for (int k = 0; k < numBins; ++k)
        {
            float tonal = hBase[static_cast<size_t>(t)][static_cast<size_t>(k)];

            float logSum = 0.0f;
            float linSum = 0.0f;
            int n = 0;
            for (int df = -sfmRadius; df <= sfmRadius; ++df)
            {
                const int kk = juce::jlimit(0, numBins - 1, k + df);
                const float m = recordedMagnitudeFrames[static_cast<size_t>(t)][static_cast<size_t>(kk)] + eps;
                logSum += std::log(m);
                linSum += m;
                ++n;
            }
            const float geo = std::exp(logSum / static_cast<float>(juce::jmax(1, n)));
            const float arith = linSum / static_cast<float>(juce::jmax(1, n));
            const float localSfm = juce::jlimit(0.0f, 1.0f, geo / (arith + eps));

            const float freqHz = (static_cast<float>(k) / static_cast<float>(juce::jmax(1, numBins - 1)))
                               * static_cast<float>(0.5 * currentSampleRate);
            const bool isBass = (freqHz < 200.0f);

            if (localSfm > 0.6f)
                tonal = 0.0f;

            if (isBass && localSfm > 0.50f)
            {
                tonal = 0.0f;
                ambientBias[static_cast<size_t>(t)][static_cast<size_t>(k)] = 1.0f;
            }

            if (onsetFrame[static_cast<size_t>(t)])
                tonal *= 0.2f;

            tonalFinal[static_cast<size_t>(t)][static_cast<size_t>(k)] = tonal;
        }
    }

    // Pass 4: ambient as residual + positive noise evidence.
    for (int t = 0; t < numFrames; ++t)
    {
        const float lowFluxFactor = 1.0f - juce::jlimit(0.0f, 1.0f, frameFluxNorm[static_cast<size_t>(t)]);
        const float flatBoostFrame = juce::jlimit(0.0f, 1.0f, (globalFlatness[static_cast<size_t>(t)] - 0.5f) / 0.4f);

        for (int k = 0; k < numBins; ++k)
        {
            const float tMask = transientFinal[static_cast<size_t>(t)][static_cast<size_t>(k)];
            const float hMask = tonalFinal[static_cast<size_t>(t)][static_cast<size_t>(k)];
            const float residual = juce::jlimit(0.0f, 1.0f, 1.0f - tMask - hMask);
            const float noiseBoost = 0.35f * flatBoostFrame * lowFluxFactor;
            const float bassBoost = 0.60f * ambientBias[static_cast<size_t>(t)][static_cast<size_t>(k)];
            ambientFinal[static_cast<size_t>(t)][static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, residual + noiseBoost + bassBoost);
        }
    }

    // Aggregate frame masks into per-bin object scores.
    std::array<float, numBins> transientScores{};
    std::array<float, numBins> tonalScores{};
    std::array<float, numBins> noiseScores{};
    for (int k = 0; k < numBins; ++k)
    {
        float tSum = 0.0f;
        float hSum = 0.0f;
        float nSum = 0.0f;
        for (int t = 0; t < numFrames; ++t)
        {
            tSum += transientFinal[static_cast<size_t>(t)][static_cast<size_t>(k)];
            hSum += tonalFinal[static_cast<size_t>(t)][static_cast<size_t>(k)];
            nSum += ambientFinal[static_cast<size_t>(t)][static_cast<size_t>(k)];
        }

        const float invFrames = 1.0f / static_cast<float>(numFrames);
        transientScores[static_cast<size_t>(k)] = tSum * invFrames;
        tonalScores[static_cast<size_t>(k)] = hSum * invFrames;
        noiseScores[static_cast<size_t>(k)] = nSum * invFrames;
    }

    auto percentile = [](std::vector<float> values, float q)
    {
        if (values.empty())
            return 0.0f;
        q = juce::jlimit(0.0f, 1.0f, q);
        const size_t idx = static_cast<size_t>(q * static_cast<float>(values.size() - 1));
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
        return values[idx];
    };

    std::vector<float> tVals(transientScores.begin(), transientScores.end());
    std::vector<float> hVals(tonalScores.begin(), tonalScores.end());
    std::vector<float> nVals(noiseScores.begin(), noiseScores.end());

    const float tThr = juce::jmax(0.22f, percentile(tVals, 0.62f));
    const float hThr = juce::jmax(0.08f, percentile(hVals, 0.64f));
    const float nThr = juce::jmax(0.10f, percentile(nVals, 0.60f));

    for (int k = 0; k < numBins; ++k)
    {
        const float t = transientScores[static_cast<size_t>(k)];
        const float h = tonalScores[static_cast<size_t>(k)];
        const float n = noiseScores[static_cast<size_t>(k)];

        bool assigned = false;

        if (t >= h && t >= n && t > tThr)
        {
            transientMask[static_cast<size_t>(k)] = true;
            assigned = true;
        }
        else if (h >= t && h >= n && h > hThr)
        {
            tonalMask[static_cast<size_t>(k)] = true;
            assigned = true;
        }
        else if (n > nThr)
        {
            noiseMask[static_cast<size_t>(k)] = true;
            assigned = true;
        }

        // Strict residual assignment over full bin range (0..Nyquist):
        // never leave bins unassigned, to avoid artificial cuts in any path.
        if (!assigned)
        {
            if (t >= h && t >= n)
                transientMask[static_cast<size_t>(k)] = true;
            else if (h >= t && h >= n)
                tonalMask[static_cast<size_t>(k)] = true;
            else
                noiseMask[static_cast<size_t>(k)] = true;
        }
    }

    objectDatabase->addObject("Transients");
    objectDatabase->setObjectMask(0, transientMask);
    objectDatabase->setObjectColor(0, static_cast<int>(juce::Colour(0xFFFF5252).getARGB()));

    objectDatabase->addObject("Tonal Components");
    objectDatabase->setObjectMask(1, tonalMask);
    objectDatabase->setObjectColor(1, static_cast<int>(juce::Colour(0xFF4AA3FF).getARGB()));

    objectDatabase->addObject("Ambient Noise");
    objectDatabase->setObjectMask(2, noiseMask);
    objectDatabase->setObjectColor(2, static_cast<int>(juce::Colour(0xFF4FD16A).getARGB()));
}

void PluginProcessor::createHannWindow()
{
    // sqrt-Hann window for both analysis and synthesis.
    // Analysis * Synthesis = sqrt-Hann * sqrt-Hann = Hann.
    // OLA normalization divides by sum(Hann) ≈ 2.0 at steady state
    // (75% overlap), giving perfect reconstruction for any smooth mask.
    for (int i = 0; i < fftSize; ++i)
    {
        const float hann = 0.5f * (1.0f - std::cos(
            2.0f * juce::MathConstants<float>::pi * static_cast<float>(i)
            / static_cast<float>(fftSize - 1)));
        window[i] = std::sqrt(juce::jmax(0.0f, hann));
    }
}

//==============================================================================
// Factory function required by JUCE to create the plugin instance
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}