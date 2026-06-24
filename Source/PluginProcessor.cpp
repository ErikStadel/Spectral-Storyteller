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
    previousLogMagnitudes.fill(0.0f);
    tonalPersistence.fill(0.0f);
    spectralFluxHistory.clear();
    hfcHistory.clear();
    odfHistory.clear();
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
    spectralFluxHistory.clear();
    hfcHistory.clear();
    odfHistory.clear();
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
    // Baseline pass-through unless muted/solo constrains it.
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

            if (!item.solo || item.mute)
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

            const float objectGain = item.mute ? 0.0f : currentTimelineObjectGains[static_cast<size_t>(obj)];
            for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
            {
                if (item.mask[static_cast<size_t>(bin)])
                    raw[static_cast<size_t>(bin)] *= objectGain;
            }
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

void PluginProcessor::analyseSegmentationFrame(const float* fftInterleaved, int64_t currentSampleIndex)
{
    juce::ScopedLock sl(segmentationLock);

    constexpr int numBins = SpectralFrameBuffer::NUM_BINS;
    constexpr float eps = 1.0e-9f;

    std::array<float, numBins> magLin{};
    std::array<float, numBins> logMag{};
    std::array<float, numBins> posDeltaLog{};
    std::array<float, numBins> tonalMask{};
    std::array<float, numBins> transientMask{};
    std::array<float, numBins> noiseMask{};

    float hfc = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        const int reIdx = 2 * k;
        const int imIdx = reIdx + 1;
        const float re = fftInterleaved[reIdx];
        const float im = fftInterleaved[imIdx];
        magLin[static_cast<size_t>(k)] = std::sqrt(re * re + im * im) + eps;
        logMag[static_cast<size_t>(k)] = std::log(magLin[static_cast<size_t>(k)]);
        hfc += static_cast<float>(k + 1) * magLin[static_cast<size_t>(k)];
    }

    float spectralFlux = 0.0f;
    if (hasPreviousMagnitudes)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const float delta = logMag[static_cast<size_t>(k)] - previousLogMagnitudes[static_cast<size_t>(k)];
            const float positiveDelta = juce::jmax(0.0f, delta);
            posDeltaLog[static_cast<size_t>(k)] = positiveDelta;
            spectralFlux += positiveDelta;
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
    const float odf = fluxZ + 0.6f * hfcZ;

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
    const float odfThreshold = odfMedian + 2.8f * juce::jmax(odfMad, 0.15f);
    if (hasPreviousMagnitudes && odf > odfThreshold)
    {
        isTransientFrame = true;
        transientHoldFrames = 2;
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

    // === NEU: Hybrid Features ===
    if (useHybridMode)
    {
        // 1. Harmonic Product Spectrum (HPS) für Tonalität
        for (int k = 0; k < numBins; ++k)
        {
            float hps = magLin[static_cast<size_t>(k)];
            if (k >= 2) hps *= magLin[static_cast<size_t>(k / 2)];
            if (k >= 3) hps *= magLin[static_cast<size_t>(k / 3)];
            if (k >= 4) hps *= magLin[static_cast<size_t>(k / 4)];
            hpsScore[static_cast<size_t>(k)] = std::pow(hps, 0.25f);   // Normalisierung
        }

        // 2. Broadband Flux für Transienten
        float lowFluxSum = 0.0f, highFluxSum = 0.0f;
        const int lowCut = numBins / 8;
        const int highCut = numBins * 3 / 4;

        for (int k = 0; k < numBins; ++k)
        {
            const float delta = posDeltaLog[static_cast<size_t>(k)];
            if (k < lowCut) lowFluxSum += delta;
            if (k > highCut) highFluxSum += delta;

            const bool isBroadband = (lowFluxSum > 0.4f && highFluxSum > 0.25f);
            broadbandFlux[static_cast<size_t>(k)] = delta * (isBroadband ? 1.8f : 1.0f);
        }
    }

    // === Masken-Berechnung ===
    if (isTransientFrame)
    {
        float meanPos = 0.0f;
        for (const auto d : posDeltaLog) meanPos += d;
        meanPos /= static_cast<float>(numBins);

        const float transientBinThreshold = juce::jmax(0.02f, meanPos * 1.4f);

        for (int k = 0; k < numBins; ++k)
        {
            float score = (posDeltaLog[static_cast<size_t>(k)] - transientBinThreshold) 
                        / (transientBinThreshold + eps);
            if (useHybridMode)
                score *= (1.0f + broadbandFlux[static_cast<size_t>(k)] * 0.7f);

            transientMask[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, score);
        }

        for (auto& p : tonalPersistence) p *= 0.88f;   // stärker dämpfen
    }
    else
    {
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

            const float tonalCandidate = prominenceDb > 4.5f ? 1.0f : 0.0f;

            const float prominenceDb = /* ... */;
            float tonalCandidate = prominenceDb > 4.5f ? 1.0f : 0.0f;

            if (useHybridMode)
                tonalCandidate *= (hpsScore[static_cast<size_t>(k)] * 2.8f);   // starker HPS-Boost

            tonalPersistence[static_cast<size_t>(k)] =
                0.82f * tonalPersistence[static_cast<size_t>(k)] + 0.18f * tonalCandidate;

            tonalMask[static_cast<size_t>(k)] =
                juce::jlimit(0.0f, 1.0f, (tonalPersistence[static_cast<size_t>(k)] - 0.25f) / 0.75f);
        }
    }

    // === Ambient als verbesserte Restklasse ===
    for (int k = 0; k < numBins; ++k)
    {
        const float tSup = 1.0f - tonalMask[static_cast<size_t>(k)];
        const float trSup = 1.0f - transientMask[static_cast<size_t>(k)];
        const float sfm = lastFlatness[static_cast<size_t>(k)];

        float noiseScore = sfm * tSup * trSup;
        if (useHybridMode)
            noiseScore *= (hpsScore[static_cast<size_t>(k)] < 0.25f ? 1.6f : 1.0f);

        noiseMask[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, noiseScore * 1.2f);
    }

    // === Smoothing & Overlay (unverändert) ===
    std::array<float, numBins> smoothTransient{}, smoothTonal{}, smoothNoise{};
    applyCosineMaskSmoothing(transientMask, smoothTransient);
    applyCosineMaskSmoothing(tonalMask, smoothTonal);
    applyCosineMaskSmoothing(noiseMask, smoothNoise);

    for (int k = 0; k < numBins; ++k)
    {
        const float sum = smoothTransient[static_cast<size_t>(k)]
                        + smoothTonal[static_cast<size_t>(k)]
                        + smoothNoise[static_cast<size_t>(k)];

        if (sum > 1.0e-5f)
        {
            overlayTransient[static_cast<size_t>(k)] = smoothTransient[static_cast<size_t>(k)] / sum;
            overlayTonal[static_cast<size_t>(k)] = smoothTonal[static_cast<size_t>(k)] / sum;
            overlayNoise[static_cast<size_t>(k)] = smoothNoise[static_cast<size_t>(k)] / sum;
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
        }

        previousMagnitudes[static_cast<size_t>(k)] = magLin[static_cast<size_t>(k)];
        previousLogMagnitudes[static_cast<size_t>(k)] = logMag[static_cast<size_t>(k)];
    }

    overlayValid = true;
    hasPreviousMagnitudes = true;

    if (autoDetectActive || autoDetectRecording)
    {
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

    const float transientNorm = static_cast<float>(juce::jmax(1, autoDetectTransientFrameCount));
    const float nonTransientNorm = static_cast<float>(juce::jmax(1, autoDetectNonTransientFrameCount));

    for (int k = 0; k < SpectralFrameBuffer::NUM_BINS; ++k)
    {
        const float t = accumulatedTransient[static_cast<size_t>(k)] / transientNorm;
        const float tn = accumulatedTonal[static_cast<size_t>(k)] / nonTransientNorm;
        const float n = accumulatedNoise[static_cast<size_t>(k)] / nonTransientNorm;

        if (t >= tn && t >= n && t > 0.15f)
            transientMask[static_cast<size_t>(k)] = true;
        else if (tn >= t && tn >= n && tn > 0.12f)
            tonalMask[static_cast<size_t>(k)] = true;
        else if (n > 0.12f)
            noiseMask[static_cast<size_t>(k)] = true;
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