#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>

namespace
{
    float wrapPhaseToPi(float phase)
    {
        while (phase > juce::MathConstants<float>::pi)
            phase -= juce::MathConstants<float>::twoPi;
        while (phase < -juce::MathConstants<float>::pi)
            phase += juce::MathConstants<float>::twoPi;
        return phase;
    }

    std::array<float, ObjectDatabase::NUM_BINS> buildTransformPresetSpectrum(const juce::String &presetName)
    {
        std::array<float, ObjectDatabase::NUM_BINS> mags{};
        mags.fill(0.0f);

        const juce::String p = presetName.trim().toLowerCase();
        if (p.isEmpty())
            return mags;

        constexpr int maxHarmonics = 64;
        constexpr float fundamentalBin = 8.0f;

        auto addHarmonic = [&mags, fundamentalBin](int harmonic, float amplitude)
        {
            const int bin = juce::jlimit(0,
                                         ObjectDatabase::NUM_BINS - 1,
                                         juce::roundToInt(harmonic * fundamentalBin));
            mags[static_cast<size_t>(bin)] += juce::jmax(0.0f, amplitude);
        };

        if (p == "sine")
        {
            addHarmonic(1, 1.0f);
        }
        else if (p == "saw")
        {
            for (int h = 1; h <= maxHarmonics; ++h)
                addHarmonic(h, 1.0f / static_cast<float>(h));
        }
        else if (p == "square")
        {
            for (int h = 1; h <= maxHarmonics; h += 2)
                addHarmonic(h, 1.0f / static_cast<float>(h));
        }
        else if (p == "triangle")
        {
            for (int h = 1; h <= maxHarmonics; h += 2)
            {
                const float sign = ((h / 2) % 2 == 0) ? 1.0f : -1.0f;
                const float amp = sign / static_cast<float>(h * h);
                addHarmonic(h, std::abs(amp));
            }
        }

        float maxMag = 0.0f;
        for (float v : mags)
            maxMag = juce::jmax(maxMag, v);
        if (maxMag > 1.0e-6f)
        {
            for (float &v : mags)
                v /= maxMag;
        }

        return mags;
    }

    std::array<float, ObjectDatabase::NUM_BINS> makeFrameFromFile(const juce::AudioBuffer<float> &audio,
                                                                  const juce::dsp::FFT &fft,
                                                                  const std::vector<float> &window,
                                                                  int fftSize,
                                                                  int frameStart)
    {
        std::array<float, ObjectDatabase::NUM_BINS> mags{};
        mags.fill(0.0f);

        std::vector<float> fftData(static_cast<size_t>(2 * fftSize), 0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            float sample = 0.0f;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                const int sampleIndex = frameStart + i;
                if (sampleIndex < audio.getNumSamples())
                    sample += audio.getSample(ch, sampleIndex);
            }

            sample /= juce::jmax(1, audio.getNumChannels());
            fftData[static_cast<size_t>(i)] = sample * window[static_cast<size_t>(i)];
        }

        fft.performRealOnlyForwardTransform(fftData.data(), false);
        for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
        {
            const float re = fftData[static_cast<size_t>(2 * bin)];
            const float im = fftData[static_cast<size_t>(2 * bin + 1)];
            mags[static_cast<size_t>(bin)] = std::sqrt(re * re + im * im);
        }

        return mags;
    }
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("Parameters"),
{
    std::make_unique<juce::AudioParameterFloat>("dryWet", "Dry/Wet", 0.0f, 1.0f, 1.0f),

    std::make_unique<juce::AudioParameterFloat>(
        "transientThreshold",
        "Transient Threshold",
        -60.0f,
        0.0f,
        -24.0f),

    std::make_unique<juce::AudioParameterFloat>(
        "inputGain",
        "Input Gain",
        -24.0f,
        24.0f,
        0.0f),

    std::make_unique<juce::AudioParameterFloat>(
        "outputGain",
        "Output Gain",
        -24.0f,
        24.0f,
        0.0f)
}),
      fft(fftOrder),
      window(fftSize),
      fftData(2 * fftSize),
      spectralFrameBuffer(std::make_unique<SpectralFrameBuffer>()),
      objectDatabase(std::make_unique<ObjectDatabase>())
{
    dryWetParam = parameters.getRawParameterValue("dryWet");
    transientThresholdParam = parameters.getRawParameterValue("transientThreshold");

    targetBinGains.fill(1.0f);
    targetBinPitchSemitones.fill(0.0f);
    targetBinDominantObjectIds.fill(-1);
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
    recordedGateFrames.clear();
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
    transientGateHoldSamplesRemaining.store(0);
    transientGateOpen.store(false);
    autoDetectFrameCount = 0;
    autoDetectTransientFrameCount = 0;
    autoDetectNonTransientFrameCount = 0;
    selectedObjectId.store(objectDatabase->getSelectedObjectId());

    for (int ch = 0; ch < 2; ++ch)
    {
        inputBuffers[ch].assign(fftSize, 0.0f);
        outputBuffers[ch].assign(outputBufferSize, 0.0f);
        outputNormBuffers[ch].assign(outputBufferSize, 0.0f);
        currentBinGains[ch].fill(1.0f);
        phaseVocoderStates[ch].clear();
        transformSmoothStates[ch].clear();
    }
}

PluginProcessor::~PluginProcessor() = default;

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    currentSampleRate = juce::jmax(1.0, sampleRate);
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
        phaseVocoderStates[ch].clear();
        transformSmoothStates[ch].clear();
    }

    targetBinGains.fill(1.0f);
    targetBinPitchSemitones.fill(0.0f);
    targetBinDominantObjectIds.fill(-1);
    transformSettingsByObject.clear();
    spectralFxByObject.clear();
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
    recordedGateFrames.clear();
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
    transientGateHoldSamplesRemaining.store(0);
    transientGateOpen.store(false);
    {
        juce::ScopedLock sl(transformFileLock);
        transformFileBuffer.clear();
    }
    resetAutoDetectAccumulation();

    totalSamplesProcessed = 0;
    setLatencySamples(delaySamples);
}

void PluginProcessor::releaseResources()
{
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::updateTargetBinGains()
{
    const double nowSec = currentAnalysisFrameTimeSec;
    transformSettingsByObject.clear();
    spectralFxByObject.clear();

    const auto buildActiveMaskForTime = [nowSec](const ObjectDatabase::ObjectMask& item)
    {
        std::array<bool, ObjectDatabase::NUM_BINS> activeMask{};
        activeMask.fill(false);

        if (!item.hasTimeFrequencyMask || item.timeMaskFrameTimesSec.empty()
            || item.timeMaskFrameTimesSec.size() != item.timeMaskFrameMasks.size())
        {
            return item.mask;
        }

        auto lower = std::lower_bound(item.timeMaskFrameTimesSec.begin(), item.timeMaskFrameTimesSec.end(), nowSec);
        size_t selectedIndex = 0;
        if (lower == item.timeMaskFrameTimesSec.end())
            selectedIndex = item.timeMaskFrameTimesSec.size() - 1;
        else if (lower == item.timeMaskFrameTimesSec.begin())
            selectedIndex = 0;
        else
        {
            const size_t hi = static_cast<size_t>(std::distance(item.timeMaskFrameTimesSec.begin(), lower));
            const size_t lo = hi - 1;
            selectedIndex = (std::abs(item.timeMaskFrameTimesSec[hi] - nowSec) < std::abs(nowSec - item.timeMaskFrameTimesSec[lo])) ? hi : lo;
        }

        double maxDistance = 0.03;
        if (item.timeMaskFrameTimesSec.size() > 1)
        {
            if (selectedIndex > 0)
                maxDistance = juce::jmax(maxDistance, 0.5 * (item.timeMaskFrameTimesSec[selectedIndex] - item.timeMaskFrameTimesSec[selectedIndex - 1]));
            if (selectedIndex + 1 < item.timeMaskFrameTimesSec.size())
                maxDistance = juce::jmax(maxDistance, 0.5 * (item.timeMaskFrameTimesSec[selectedIndex + 1] - item.timeMaskFrameTimesSec[selectedIndex]));
        }

        if (std::abs(item.timeMaskFrameTimesSec[selectedIndex] - nowSec) > maxDistance)
            return activeMask;

        return item.timeMaskFrameMasks[selectedIndex];
    };

    const auto getModulatedNorm = [this, nowSec](int objectId,
                                                 const juce::String& fxName,
                                                 const juce::String& paramName,
                                                 float fallback)
    {
        const float base = objectDatabase->getInterpolatedAutomationValue(objectId,
                                                                          fxName.toStdString(),
                                                                          paramName.toStdString(),
                                                                          nowSec,
                                                                          fallback);
        const float mod = modMatrix.getModulation(objectId, fxName, paramName);
        return juce::jlimit(0.0f, 1.0f, base + mod);
    };

    // Volume automation from per-object FX chain (Volume/Gain).
    for (int obj = 0; obj < ObjectDatabase::MAX_OBJECTS; ++obj)
    {
        timelineObjectGains[static_cast<size_t>(obj)] = 1.0f;
    }

    if (objectDatabase != nullptr)
    {
        const int numObjects = objectDatabase->getNumObjects();
        for (int obj = 0; obj < numObjects; ++obj)
        {
            ObjectDatabase::ObjectMask item;
            if (!objectDatabase->getObjectCopy(obj, item))
                continue;

            const auto activeMask = buildActiveMaskForTime(item);

            if (!item.densityAnchorValid)
                calibrateDensityAnchor(item);

                        const float volumeNorm = getModulatedNorm(item.id, "Volume", "Gain", 0.5f);

            TransformSettings transformSettings;
                        transformSettings.modulatorGain = juce::jlimit(0.0f,
                                                                                                                     4.0f,
                                                                                                                     objectDatabase->getObjectFxParameterValue(item.id,
                                                                                                                                                                                                        "Transform",
                                                                                                                                                                                                        "Gain",
                                                                                                                                                                                                        1.0f));
            transformSettings.amount = juce::jlimit(0.0f,
                                                    1.0f,
                                                                                                        getModulatedNorm(item.id, "Transform", "Amount", 0.0f));
            transformSettings.smoothMs = juce::jlimit(0.0f,
                                                      500.0f,
                                                                                                            getModulatedNorm(item.id, "Transform", "Smooth", 0.0f) * 500.0f);
            transformSettings.sourceObjectId = objectDatabase->getObjectFxSourceObjectId(item.id, "Transform");
            transformSettingsByObject[item.id] = transformSettings;

            const float density = juce::jlimit(0.0f,
                                               1.0f,
                                               getModulatedNorm(item.id, "Density", "Density", 1.0f));
            const float brightnessNorm = juce::jlimit(0.0f,
                                                      1.0f,
                                                      getModulatedNorm(item.id, "Brightness", "Brightness", 0.5f));
            const float brightness = (brightnessNorm - 0.5f) * 2.0f;

            int lowBin = -1;
            int highBin = -1;
            float framePeakMag = 0.0f;
            for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
            {
                if (!activeMask[static_cast<size_t>(bin)])
                    continue;

                if (lowBin < 0)
                    lowBin = bin;
                highBin = bin;

                framePeakMag = juce::jmax(framePeakMag, currentAnalysisMagnitudes[static_cast<size_t>(bin)]);
            }

            const float anchorDb = item.densityAnchorValid ? item.densityAnchorDb : -60.0f;
            const float peakDb = juce::Decibels::gainToDecibels(framePeakMag, -120.0f);
            const float denseMinDb = juce::jmax(anchorDb + 18.0f, peakDb - 6.0f);
            // Mid-field emphasis: use a concave curve so Density changes are clearer around 0.7..0.4.
            const float midCurve = 0.62f;
            const float thresholdDb = (density >= 0.5f)
                                          ? juce::jmap(std::pow((1.0f - density) * 2.0f, midCurve), -120.0f, anchorDb)
                                          : juce::jmap(std::pow((0.5f - density) * 2.0f, midCurve), anchorDb, denseMinDb);

            SpectralFxSettings spectralSettings;
            spectralSettings.density = density;
            spectralSettings.brightness = brightness;
            spectralSettings.thresholdLin = juce::Decibels::decibelsToGain(thresholdDb, -120.0f);
            spectralSettings.lowBin = juce::jmax(0, lowBin);
            spectralSettings.highBin = juce::jmax(spectralSettings.lowBin, highBin);
            spectralSettings.centerBin = juce::jmax(1.0f,
                                                    (lowBin >= 0 && highBin >= lowBin)
                                                        ? 0.5f * static_cast<float>(lowBin + highBin)
                                                        : 1.0f);
            spectralSettings.tiltExp = brightness * 2.0f;

            float maxTiltBoost = 1.0f;
            if (lowBin >= 0 && highBin >= lowBin && std::abs(spectralSettings.brightness) > 1.0e-6f)
            {
                const float lowRatio = juce::jmax(1.0e-3f, static_cast<float>(lowBin) / spectralSettings.centerBin);
                const float highRatio = juce::jmax(1.0e-3f, static_cast<float>(highBin) / spectralSettings.centerBin);
                const float lowTilt = std::pow(lowRatio, spectralSettings.tiltExp);
                const float highTilt = std::pow(highRatio, spectralSettings.tiltExp);
                maxTiltBoost = juce::jmax(1.0f, juce::jmax(lowTilt, highTilt));
            }

            // Keep brightness as a redistribution rather than broad gain boost.
            spectralSettings.brightnessCompensation = 0.92f / maxTiltBoost;
            spectralFxByObject[item.id] = spectralSettings;

            timelineObjectGains[static_cast<size_t>(obj)] = juce::jlimit(0.0f, 2.0f, volumeNorm * 2.0f);
        }
    }

    // Smooth object gains to avoid zipper noise from timeline updates.
    static constexpr float objectGainAlpha = 0.20f;
    for (int obj = 0; obj < ObjectDatabase::MAX_OBJECTS; ++obj)
    {
        float &g = currentTimelineObjectGains[static_cast<size_t>(obj)];
        const float target = timelineObjectGains[static_cast<size_t>(obj)];
        g = objectGainAlpha * target + (1.0f - objectGainAlpha) * g;
    }

    // Default: full passthrough when no objects exist
    if (objectDatabase == nullptr || objectDatabase->getNumObjects() == 0)
    {
        targetBinGains.fill(1.0f);
        targetBinPitchSemitones.fill(0.0f);
        spectralFxByObject.clear();
        transientMuteCompressorGain = 1.0f;
        return;
    }

    // --- Step 1: Build object-aware gain mask ---
    // Baseline pass-through unless solo constrains it.
    std::array<float, ObjectDatabase::NUM_BINS> raw{};
    std::array<float, ObjectDatabase::NUM_BINS> pitchSum{};
    std::array<float, ObjectDatabase::NUM_BINS> pitchWeight{};
    std::array<float, ObjectDatabase::NUM_BINS> dominantStrength{};
    raw.fill(1.0f);
    pitchSum.fill(0.0f);
    pitchWeight.fill(0.0f);
    dominantStrength.fill(-1.0f);
    targetBinDominantObjectIds.fill(-1);
    std::array<bool, ObjectDatabase::NUM_BINS> tonalMuteMask{};
    std::array<bool, ObjectDatabase::NUM_BINS> ambientMuteMask{};
    tonalMuteMask.fill(false);
    ambientMuteMask.fill(false);

    const int numObjects = objectDatabase->getNumObjects();
    bool transientObjectMuted = false;
    bool tonalObjectMuted = false;
    bool ambientObjectMuted = false;
    bool anySolo = false;
    for (int obj = 0; obj < numObjects; ++obj)
    {
        ObjectDatabase::ObjectMask item;
        if (!objectDatabase->getObjectCopy(obj, item))
            continue;

        const auto activeMask = buildActiveMaskForTime(item);

        if (!item.engaged)
            continue;

        if (juce::String(item.name).equalsIgnoreCase("Transients") && item.mute)
            transientObjectMuted = true;

        if (juce::String(item.name).containsIgnoreCase("Tonal") && item.mute)
        {
            tonalObjectMuted = true;
            for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
            {
                if (activeMask[static_cast<size_t>(bin)])
                    tonalMuteMask[static_cast<size_t>(bin)] = true;
            }
        }

        if (juce::String(item.name).containsIgnoreCase("Ambient") && item.mute)
        {
            ambientObjectMuted = true;
            for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
            {
                if (activeMask[static_cast<size_t>(bin)])
                    ambientMuteMask[static_cast<size_t>(bin)] = true;
            }
        }

        if (item.solo)
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

            const auto activeMask = buildActiveMaskForTime(item);

            if (!item.engaged)
                continue;

            if (!item.solo)
                continue;

            float objectGain = currentTimelineObjectGains[static_cast<size_t>(obj)];
            const float pitchNorm = getModulatedNorm(item.id, "Pitch", "Semitones", 0.5f);
            const float objectSemitones = (juce::jlimit(0.0f, 1.0f, pitchNorm) - 0.5f) * 4.0f;
            const bool isTransientObject = juce::String(item.name).equalsIgnoreCase("Transients");
            if (isTransientObject && !transientGateOpen.load())
                objectGain = 0.0f;

            for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
            {
                if (activeMask[static_cast<size_t>(bin)])
                {
                    raw[static_cast<size_t>(bin)] = juce::jmax(raw[static_cast<size_t>(bin)], objectGain);
                    pitchSum[static_cast<size_t>(bin)] += objectSemitones;
                    pitchWeight[static_cast<size_t>(bin)] += 1.0f;
                    const auto tIt = transformSettingsByObject.find(item.id);
                    const bool transformActive = (tIt != transformSettingsByObject.end() && tIt->second.amount > 1.0e-4f);
                    const float effectiveStrength = objectGain + (transformActive ? 4.0f : 0.0f);
                    if (effectiveStrength > dominantStrength[static_cast<size_t>(bin)])
                    {
                        dominantStrength[static_cast<size_t>(bin)] = effectiveStrength;
                        targetBinDominantObjectIds[static_cast<size_t>(bin)] = item.id;
                    }
                }
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

            const auto activeMask = buildActiveMaskForTime(item);

            if (!item.engaged)
                continue;

            if (item.mute)
                continue;

            float objectGain = currentTimelineObjectGains[static_cast<size_t>(obj)];
            const float pitchNorm = getModulatedNorm(item.id, "Pitch", "Semitones", 0.5f);
            const float objectSemitones = (juce::jlimit(0.0f, 1.0f, pitchNorm) - 0.5f) * 4.0f;
            const bool isTransientObject = juce::String(item.name).equalsIgnoreCase("Transients");
            if (isTransientObject && !transientGateOpen.load())
                objectGain = 0.0f;

            for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
            {
                if (activeMask[static_cast<size_t>(bin)])
                {
                    raw[static_cast<size_t>(bin)] *= objectGain;
                    pitchSum[static_cast<size_t>(bin)] += objectSemitones;
                    pitchWeight[static_cast<size_t>(bin)] += 1.0f;
                    const auto tIt = transformSettingsByObject.find(item.id);
                    const bool transformActive = (tIt != transformSettingsByObject.end() && tIt->second.amount > 1.0e-4f);
                    const float effectiveStrength = objectGain + (transformActive ? 4.0f : 0.0f);
                    if (effectiveStrength > dominantStrength[static_cast<size_t>(bin)])
                    {
                        dominantStrength[static_cast<size_t>(bin)] = effectiveStrength;
                        targetBinDominantObjectIds[static_cast<size_t>(bin)] = item.id;
                    }
                }
            }
        }
    }

    // Step B: apply all mute masks after solo/base composition.
    for (int obj = 0; obj < numObjects; ++obj)
    {
        ObjectDatabase::ObjectMask item;
        if (!objectDatabase->getObjectCopy(obj, item) || !item.mute)
            continue;

        const auto activeMask = buildActiveMaskForTime(item);

        if (!item.engaged)
            continue;

        // Transient mute is handled as a gate-triggered compressor below,
        // not as a static spectral hard cut.
        if (juce::String(item.name).equalsIgnoreCase("Transients"))
            continue;

        for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
        {
            if (activeMask[static_cast<size_t>(bin)])
                raw[static_cast<size_t>(bin)] *= 0.0f;
        }
    }

    // Transient mute acts like a strong short-attack/short-release compressor
    // keyed by the transient gate, so attacks are reduced instead of removed.
    const float compTarget = (transientObjectMuted && transientGateOpen.load()) ? 0.22f : 1.0f;
    static constexpr float compAttackAlpha = 0.25f;
    static constexpr float compReleaseAlpha = 0.65f;
    if (compTarget < transientMuteCompressorGain)
        transientMuteCompressorGain = compAttackAlpha * transientMuteCompressorGain + (1.0f - compAttackAlpha) * compTarget;
    else
        transientMuteCompressorGain = compReleaseAlpha * transientMuteCompressorGain + (1.0f - compReleaseAlpha) * compTarget;

    for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
        raw[static_cast<size_t>(bin)] *= transientMuteCompressorGain;

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
            const float w = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * static_cast<float>(k) / static_cast<float>(radius + 1)));
            weighted += raw[static_cast<size_t>(idx)] * w;
            totalWeight += w;
        }

        targetBinGains[static_cast<size_t>(bin)] =
            (totalWeight > 0.0f) ? (weighted / totalWeight) : raw[static_cast<size_t>(bin)];
    }

    // Tonal mute should remove detected tonal bins from the summed mask
    // after composition, not only within class-local routing.
    if (tonalObjectMuted)
    {
        for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
        {
            if (tonalMuteMask[static_cast<size_t>(bin)])
                targetBinGains[static_cast<size_t>(bin)] = 0.0f;
        }
    }

    if (ambientObjectMuted)
    {
        for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
        {
            if (ambientMuteMask[static_cast<size_t>(bin)])
                targetBinGains[static_cast<size_t>(bin)] = 0.0f;
        }
    }

    for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
    {
        const float w = pitchWeight[static_cast<size_t>(bin)];
        targetBinPitchSemitones[static_cast<size_t>(bin)] = (w > 0.0f)
                                                                ? (pitchSum[static_cast<size_t>(bin)] / w)
                                                                : 0.0f;
    }

    // Note: NO additional gain compensation here.
    // The OLA normalisation (division by sum-of-squared-windows) already
    // ensures that passthrough gain = 1. Solo/Mute correctly attenuate
    // the selected bins without boosting anything.
}

void PluginProcessor::calibrateDensityAnchor(ObjectDatabase::ObjectMask& obj)
{
    if (objectDatabase == nullptr || obj.id < 0)
        return;

    double sum = 0.0;
    int count = 0;

    if (obj.hasTimeFrequencyMask
        && spectralFrameBuffer != nullptr
        && obj.timeMaskFrameTimesSec.size() == obj.timeMaskFrameMasks.size()
        && !obj.timeMaskFrameTimesSec.empty())
    {
        const int numFrames = spectralFrameBuffer->getNumFrames();
        for (size_t selectedIndex = 0; selectedIndex < obj.timeMaskFrameMasks.size(); ++selectedIndex)
        {
            SpectralFrameBuffer::Frame frame;
            bool matchedFrame = false;
            for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
            {
                if (!spectralFrameBuffer->copyFrame(frameIndex, frame))
                    continue;

                if (std::abs(frame.transportTimeSec - obj.timeMaskFrameTimesSec[selectedIndex]) > 1.0e-4)
                    continue;

                matchedFrame = true;
                for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
                {
                    if (!obj.timeMaskFrameMasks[selectedIndex][static_cast<size_t>(bin)])
                        continue;

                    const float mag = juce::Decibels::decibelsToGain(frame.magnitude[static_cast<size_t>(bin)], -120.0f);
                    if (mag > 1.0e-7f)
                    {
                        sum += mag;
                        ++count;
                    }
                }

                break;
            }

            if (!matchedFrame)
                continue;
        }
    }

    if (count == 0)
    {
        for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
        {
            if (!obj.mask[static_cast<size_t>(bin)])
                continue;

            const float mag = currentAnalysisMagnitudes[static_cast<size_t>(bin)];
            if (mag > 1.0e-7f)
            {
                sum += mag;
                ++count;
            }
        }
    }

    float anchorDb = -60.0f;
    if (count > 0)
    {
        const float avgMag = static_cast<float>(sum / static_cast<double>(count));
        anchorDb = juce::Decibels::gainToDecibels(avgMag, -120.0f);
    }

    obj.densityAnchorDb = anchorDb;
    obj.densityAnchorValid = true;
    objectDatabase->setObjectDensityAnchor(obj.id, anchorDb, true);
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

    constexpr int nyquistBin = fftSize / 2;
    for (int bin = 0; bin <= nyquistBin; ++bin)
    {
        const float re = fftData[static_cast<size_t>(2 * bin)];
        const float im = fftData[static_cast<size_t>(2 * bin + 1)];
        currentAnalysisMagnitudes[static_cast<size_t>(bin)] = std::sqrt(re * re + im * im);
    }

    const double blockTransportSec = transportSeconds.load();
    currentAnalysisFrameTimeSec = blockTransportSec
                                + (static_cast<double>(currentSampleIndex - totalSamplesProcessed) / juce::jmax(1.0, currentSampleRate));

    // Write frame to spectral buffer for visualisation (first channel only)
    if (channel == 0)
    {
        spectralFrameBuffer->writeFrame(fftData.data(), currentSampleIndex, currentAnalysisFrameTimeSec);
        analyseSegmentationFrame(fftData.data(), currentSampleIndex);
    }

    // --- Spectral mask ---
    // Recompute target gains once per frame (same cost for both channels).
    if (channel == 0)
        updateTargetBinGains();

    // JUCE real-only format returns N complex bins interleaved as float array:
    //   re(k) = fftData[2*k], im(k) = fftData[2*k+1]
    // For real input, bins k=0..N/2 are independent.

    auto applyBinGain = [this, channel](int bin)
    {
        float &smoothGain = currentBinGains[channel][static_cast<size_t>(bin)];
        smoothGain = maskSmoothAlpha * targetBinGains[static_cast<size_t>(bin)] + (1.0f - maskSmoothAlpha) * smoothGain;

        float spectralFactor = 1.0f;
        const int objectId = targetBinDominantObjectIds[static_cast<size_t>(bin)];
        const auto fxIt = spectralFxByObject.find(objectId);
        if (fxIt != spectralFxByObject.end())
        {
            const auto& spectral = fxIt->second;
            const float mag = currentAnalysisMagnitudes[static_cast<size_t>(bin)];

            if (mag < spectral.thresholdLin)
            {
                spectralFactor = 0.0f;
            }
            else if (std::abs(spectral.brightness) > 1.0e-6f && bin > 0)
            {
                const float ratio = static_cast<float>(bin) / spectral.centerBin;
                const float factor = std::pow(ratio, spectral.tiltExp);
                spectralFactor = juce::jlimit(0.125f, 8.0f, factor) * spectral.brightnessCompensation;
            }
        }

        const float appliedGain = smoothGain * spectralFactor;
        const int reIdx = 2 * bin;
        const int imIdx = reIdx + 1;
        fftData[reIdx] *= appliedGain;
        fftData[imIdx] *= appliedGain;
    };

    applyBinGain(0);
    for (int bin = 1; bin < nyquistBin; ++bin)
        applyBinGain(bin);
    applyBinGain(nyquistBin);

    applyTransformCrossSynthesis(channel);
    applyPhaseVocoderPitchShift(channel);

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

void PluginProcessor::applyTransformCrossSynthesis(int channel)
{
    constexpr int nyquistBin = fftSize / 2;
    auto &smoothStates = transformSmoothStates[channel];
    std::vector<int> firstFrameObjects;
    const bool transportIsPlaying = transportPlaying.load();
    const double transportSec = transportSeconds.load();

    std::array<float, ObjectDatabase::NUM_BINS> binMagnitudes{};
    for (int bin = 0; bin <= nyquistBin; ++bin)
    {
        const float re = fftData[2 * bin];
        const float im = fftData[2 * bin + 1];
        binMagnitudes[static_cast<size_t>(bin)] = std::sqrt(re * re + im * im);
    }

    for (int bin = 0; bin <= nyquistBin; ++bin)
    {
        const int carrierObjectId = targetBinDominantObjectIds[static_cast<size_t>(bin)];
        if (carrierObjectId < 0)
            continue;
        double carrierEnergy = 0.0;
        for (int b = 0; b <= nyquistBin; ++b)
            carrierEnergy += static_cast<double>(binMagnitudes[b]) * binMagnitudes[b];
        const float currentAnalysisRms = static_cast<float>(
            std::sqrt(carrierEnergy / juce::jmax(1, nyquistBin + 1)));

        const auto settingsIt = transformSettingsByObject.find(carrierObjectId);
        if (settingsIt == transformSettingsByObject.end())
            continue;

        const auto &settings = settingsIt->second;
        if (settings.amount <= 1.0e-4f)
            continue;

        float modMag = 0.0f;
        if (settings.sourceObjectId >= 0)
        {
            ObjectDatabase::ObjectMask sourceObj;
            if (objectDatabase->getObjectCopyById(settings.sourceObjectId, sourceObj))
            {
                float sourceEnergySq = 0.0f;
                float sourceWeight = 0.0f;

                for (int sourceBin = 0; sourceBin <= nyquistBin; ++sourceBin)
                {
                    if (!sourceObj.mask[static_cast<size_t>(sourceBin)])
                        continue;

                    const float sourceMag = currentAnalysisMagnitudes[static_cast<size_t>(sourceBin)];
                    sourceEnergySq += sourceMag * sourceMag;
                    sourceWeight += 1.0f;
                }

                // RMS als absolute Magnitude – gleiche Einheit wie carrierMag
                modMag = (sourceWeight > 0.0f)
                             ? std::sqrt(sourceEnergySq / sourceWeight)
                             : 0.0f;
            }
        }
        else if (settings.sourceObjectId == ObjectDatabase::FILE_SOURCE_ID || settings.sourceObjectId == -3)
        {
            // Externer Sound: nur abspielen, wenn DAW-Transport läuft.
            // -3 = interne Preset-Quelle (statisches Spektrum) → immer aktiv.
            const bool isExternalFile = (settings.sourceObjectId == ObjectDatabase::FILE_SOURCE_ID);
            if (isExternalFile && !transportIsPlaying)
            {
                modMag = 0.0f;
            }
            else
            {
                juce::ScopedLock sl(transformFileLock);
                const auto fileIt = transformFileBuffer.find(carrierObjectId);
                if (fileIt != transformFileBuffer.end() && !fileIt->second.frames.empty())
                {
                    const auto &sourceData = fileIt->second;
                    const auto &frames = sourceData.frames;
                    const int frameCount = static_cast<int>(frames.size());
                    int frameIndex = 0;

                    if (isExternalFile && sourceData.durationSeconds > 1.0e-6)
                    {
                        // fmod → Loop über die tatsächlich geladene Dauer
                        const double phase = std::fmod(juce::jmax(0.0, transportSec), sourceData.durationSeconds);
                        const double normalized = phase / sourceData.durationSeconds;
                        frameIndex = juce::jlimit(0, frameCount - 1,
                                                  static_cast<int>(std::floor(normalized * frameCount)));
                    }

                    modMag = frames[static_cast<size_t>(frameIndex)][static_cast<size_t>(bin)];
                }
            }
        }

        auto &smoothState = smoothStates[carrierObjectId];
        const bool isFirstFrame = !smoothState.initialized;
        if (isFirstFrame)
        {
            const bool seenBefore = std::find(firstFrameObjects.begin(), firstFrameObjects.end(), carrierObjectId) != firstFrameObjects.end();
            if (!seenBefore)
            {
                smoothState.smoothedMagnitudes.fill(0.0f);
                firstFrameObjects.push_back(carrierObjectId);
            }
        }

        const float smoothSec = settings.smoothMs * 0.001f;
        const float frameDt = static_cast<float>(hopSize / juce::jmax(1.0, currentSampleRate));
        const float alpha = (smoothSec <= 1.0e-6f)
                                ? 1.0f
                                : juce::jlimit(0.0f, 1.0f, frameDt / (smoothSec + frameDt));

        float &smoothed = smoothState.smoothedMagnitudes[static_cast<size_t>(bin)];
        if (isFirstFrame)
            smoothed = modMag;
        else
            smoothed += alpha * (modMag - smoothed);

        const float re = fftData[2 * bin];
        const float im = fftData[2 * bin + 1];
        const float carrierMag = std::sqrt(re * re + im * im);
        const float carrierPhase = std::atan2(im, re);

        // Fix C: gedämpftes RMS-Matching.
        // Ziel-RMS der Modulator-Normalisierung beim Laden = 0.1 (-20 dBFS).
        // Mit sqrt() begrenzen wir den Aufschaukel-Effekt bei lauten Carriern,
        // und jlimit kappt Extremwerte (Stille / Peaks).
        constexpr float kModulatorTargetRms = 0.1f;
        const float rmsRatio   = currentAnalysisRms / kModulatorTargetRms;
        const float matchGain  = std::sqrt(juce::jlimit(0.0f, 4.0f, rmsRatio));

        const float carrierRefLevel = matchGain;
        const float scaledModMag    = smoothed * carrierRefLevel;


        // Gain wirkt auf den INPUT (Carrier), nicht auf den Modulator.
        const float scaledCarrierMag = carrierMag * settings.modulatorGain;

        const float morphedMag = ((1.0f - settings.amount) * scaledCarrierMag) + (settings.amount * scaledModMag);

        fftData[2 * bin] = morphedMag * std::cos(carrierPhase);
        fftData[2 * bin + 1] = morphedMag * std::sin(carrierPhase);
    }

    for (int objectId : firstFrameObjects)
        smoothStates[objectId].initialized = true;
}

void PluginProcessor::applyPhaseVocoderPitchShift(int channel)
{
    constexpr int nyquistBin = fftSize / 2;
    std::array<float, 2 * fftSize> shifted{};

    auto &stateMap = phaseVocoderStates[channel];
    const float nominalCoeff = juce::MathConstants<float>::twoPi * static_cast<float>(hopSize) / static_cast<float>(fftSize);

    for (int k = 0; k <= nyquistBin; ++k)
    {
        const int objectId = juce::jmax(0, targetBinDominantObjectIds[static_cast<size_t>(k)]);
        auto &objState = stateMap[objectId];

        const float re = fftData[2 * k];
        const float im = fftData[2 * k + 1];
        const float magnitude = std::sqrt(re * re + im * im);
        const float phase = std::atan2(im, re);

        if (!objState.initialized)
        {
            objState.previousAnalysisPhase.fill(0.0f);
            objState.synthesisPhase.fill(0.0f);
            objState.initialized = true;
        }

        const float semitones = targetBinPitchSemitones[static_cast<size_t>(k)];
        const float ratio = std::pow(2.0f, semitones / 12.0f);
        const float nominalAdvance = nominalCoeff * static_cast<float>(k);
        const float delta = wrapPhaseToPi(phase - objState.previousAnalysisPhase[static_cast<size_t>(k)] - nominalAdvance);
        const float trueAdvance = nominalAdvance + delta;
        const float scaledAdvance = trueAdvance * ratio;
        const float synthPhase = objState.synthesisPhase[static_cast<size_t>(k)] + scaledAdvance;

        objState.previousAnalysisPhase[static_cast<size_t>(k)] = phase;
        objState.synthesisPhase[static_cast<size_t>(k)] = synthPhase;

        const float dstFloat = static_cast<float>(k) * ratio;
        if (dstFloat < 0.0f || dstFloat > static_cast<float>(nyquistBin))
            continue;

        const int dst0 = juce::jlimit(0, nyquistBin, static_cast<int>(std::floor(dstFloat)));
        const int dst1 = juce::jlimit(0, nyquistBin, dst0 + 1);
        const float frac = juce::jlimit(0.0f, 1.0f, dstFloat - static_cast<float>(dst0));

        const float outRe = magnitude * std::cos(synthPhase);
        const float outIm = magnitude * std::sin(synthPhase);

        shifted[static_cast<size_t>(2 * dst0)] += outRe * (1.0f - frac);
        shifted[static_cast<size_t>(2 * dst0 + 1)] += outIm * (1.0f - frac);
        shifted[static_cast<size_t>(2 * dst1)] += outRe * frac;
        shifted[static_cast<size_t>(2 * dst1 + 1)] += outIm * frac;
    }

    for (int i = 0; i < 2 * fftSize; ++i)
        fftData[static_cast<size_t>(i)] = shifted[static_cast<size_t>(i)];
}

void PluginProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    // Input Gain
    const float inputGainDb =
    parameters.getRawParameterValue("inputGain")->load();

    const float inputGain =
    juce::Decibels::decibelsToGain(inputGainDb);

    buffer.applyGain(inputGain);

    juce::AudioPlayHead::CurrentPositionInfo pos;
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = false;
    if (auto* ph = getPlayHead(); ph != nullptr && ph->getCurrentPosition(pos))
    {
        ppq = pos.ppqPosition;
        bpm = pos.bpm > 0.0 ? pos.bpm : 120.0;
        playing = pos.isPlaying;
    }
    modMatrix.setTransport(ppq, bpm, playing);



// Eingangspegel messen
float inMag = 0.0f;
for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    inMag = juce::jmax(inMag,
                       buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
inputPeakDb.store(
    juce::Decibels::gainToDecibels(inMag, -90.0f));
    if (auto *playHead = getPlayHead())
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

    // Manual transient gate: block RMS trigger + fixed 40 ms hold.
    double sumSq = 0.0;
    int sampleCount = 0;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float *in = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = in[i];
            sumSq += static_cast<double>(s) * static_cast<double>(s);
            ++sampleCount;
        }
    }

    const float blockRms = (sampleCount > 0)
                               ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(sampleCount)))
                               : 0.0f;
    const float thresholdDb = transientThresholdParam != nullptr ? transientThresholdParam->load() : -24.0f;
    const float thresholdLin = std::pow(10.0f, thresholdDb / 20.0f);
    const bool gateTrigger = (blockRms >= thresholdLin);
    const int holdSamples = static_cast<int>(std::round(0.04 * currentSampleRate));

    int remaining = transientGateHoldSamplesRemaining.load();
    if (gateTrigger)
        remaining = holdSamples;
    else
        remaining = juce::jmax(0, remaining - numSamples);

    transientGateHoldSamplesRemaining.store(remaining);
    transientGateOpen.store(remaining > 0);

    const float wet = juce::jlimit(0.0f, 1.0f, dryWetParam->load());
    const float dry = 1.0f - wet;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float *channelData = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const int64_t currentSampleIndex = totalSamplesProcessed + i;
            const int bufferPos = static_cast<int>(currentSampleIndex % outputBufferSize);
            const float inputSample = channelData[i];

            inputBuffers[ch][inputWritePos[ch]] = inputSample;
            inputWritePos[ch] = (inputWritePos[ch] + 1) % fftSize;

            if (samplesInBuffer[ch] < fftSize)
                ++samplesInBuffer[ch];

            if (++samplesSinceLastFrame[ch] >= hopSize && samplesInBuffer[ch] == fftSize)
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
    // Soft-Limit auf -1 dBFS, hartes Clip darüber
    const float ceiling = 0.89f; // ~ -1 dBFS
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
    auto* d = buffer.getWritePointer(ch);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float x = d[i];
        // tanh-Soft-Clip, dann hartes Cap
        x = std::tanh(x * 0.9f) * 1.1f;
        d[i] = juce::jlimit(-ceiling, ceiling, x);
    }
}
 // Output Gain
const float outputGainDb =
    parameters.getRawParameterValue("outputGain")->load();

const float outputGain =
    juce::Decibels::decibelsToGain(outputGainDb);

buffer.applyGain(outputGain);

// Ausgangspegel messen
float outMag = 0.0f;
for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    outMag = juce::jmax(outMag,
                        buffer.getMagnitude(ch, 0, buffer.getNumSamples()));

outputPeakDb.store(
    juce::Decibels::gainToDecibels(outMag, -90.0f));
}

juce::AudioProcessorEditor *PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

bool PluginProcessor::hasEditor() const
{
    return true;
}

void PluginProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    auto state = parameters.copyState();

    const auto existing = state.getChildWithName("TimelineData");
    if (existing.isValid())
        state.removeChild(existing, nullptr);

    const auto existingMod = state.getChildWithName("ModMatrix");
    if (existingMod.isValid())
        state.removeChild(existingMod, nullptr);

    const auto existingObjects = state.getChildWithName("ObjectDatabase");
    if (existingObjects.isValid())
        state.removeChild(existingObjects, nullptr);

    state.addChild(timelineData.toValueTree(), -1, nullptr);
    state.appendChild(modMatrix.toValueTree(), nullptr);
    if (objectDatabase != nullptr)
        state.appendChild(objectDatabase->toValueTree(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void *data, int sizeInBytes)
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

        const auto modTree = state.getChildWithName("ModMatrix");
        if (modTree.isValid())
        {
            modMatrix.fromValueTree(modTree);
            state.removeChild(modTree, nullptr);
        }

        const auto objectsTree = state.getChildWithName("ObjectDatabase");
        if (objectsTree.isValid() && objectDatabase != nullptr)
        {
            objectDatabase->fromValueTree(objectsTree);
            state.removeChild(objectsTree, nullptr);
        }

        parameters.replaceState(state);
    }
}

void PluginProcessor::addTimelineKeyframe(int objectIndex, double timeSec, float value)
{
    if (objectDatabase == nullptr || objectIndex < 0 || objectIndex >= objectDatabase->getNumObjects())
        return;

    const int objectId = objectDatabase->getObjectIdAtIndex(objectIndex);
    if (objectId < 0)
        return;

    objectDatabase->addAutomationKeyframe(objectId, "Volume", "Gain", timeSec, value);
}

void PluginProcessor::deleteTimelineKeyframe(int objectIndex, double timeSec)
{
    if (objectDatabase == nullptr || objectIndex < 0 || objectIndex >= objectDatabase->getNumObjects())
        return;

    const int objectId = objectDatabase->getObjectIdAtIndex(objectIndex);
    if (objectId < 0)
        return;

    objectDatabase->deleteAutomationKeyframe(objectId, "Volume", "Gain", timeSec);
}

std::vector<TimelineData::Keyframe> PluginProcessor::getTimelineKeyframes(int objectIndex) const
{
    if (objectDatabase == nullptr || objectIndex < 0 || objectIndex >= objectDatabase->getNumObjects())
        return {};

    const int objectId = objectDatabase->getObjectIdAtIndex(objectIndex);
    if (objectId < 0)
        return {};

    const auto keys = objectDatabase->getAutomationKeyframes(objectId, "Volume", "Gain");
    std::vector<TimelineData::Keyframe> out;
    out.reserve(keys.size());
    for (const auto &k : keys)
        out.push_back({k.timeSec, k.value});
    return out;
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

void PluginProcessor::setTimelineTrackName(int objectIndex, const std::string &newName)
{
    if (objectDatabase == nullptr || objectIndex < 0 || objectIndex >= objectDatabase->getNumObjects())
        return;

    objectDatabase->setObjectName(objectIndex, newName);
}

int PluginProcessor::getSelectedObjectId() const
{
    if (objectDatabase != nullptr)
        return objectDatabase->getSelectedObjectId();
    return selectedObjectId.load();
}

void PluginProcessor::setSelectedObjectId(int objectId)
{
    selectedObjectId.store(objectId);
    if (objectDatabase != nullptr)
        objectDatabase->setSelectedObjectId(objectId);
}

std::vector<ObjectDatabase::FXModule> PluginProcessor::getFxChainForObject(int objectId) const
{
    if (objectDatabase == nullptr || objectId < 0)
        return {};

    return objectDatabase->getObjectFxChain(objectId);
}

std::vector<ObjectDatabase::FXModule> PluginProcessor::getFxChainForSelectedObject() const
{
    return getFxChainForObject(getSelectedObjectId());
}

void PluginProcessor::setObjectFxEnabled(int objectId, const juce::String &effectName, bool enabled)
{
    if (objectDatabase == nullptr || objectId < 0)
        return;

    objectDatabase->setObjectFxEnabled(objectId, effectName.toStdString(), enabled);
}

void PluginProcessor::addOrEnableObjectFx(int objectId, const juce::String &effectName)
{
    if (objectDatabase == nullptr || objectId < 0)
        return;

    objectDatabase->addOrEnableObjectFx(objectId, effectName.toStdString());
}

void PluginProcessor::setObjectFxSelectedParameter(int objectId, const juce::String &effectName, int parameterIndex)
{
    if (objectDatabase == nullptr || objectId < 0)
        return;

    objectDatabase->setObjectFxSelectedParameter(objectId, effectName.toStdString(), parameterIndex);
}

std::vector<ObjectDatabase::AutomationKeyframe> PluginProcessor::getFxAutomationKeyframes(int objectId,
                                                                                          const juce::String &effectName,
                                                                                          const juce::String &parameterName) const
{
    if (objectDatabase == nullptr || objectId < 0)
        return {};

    return objectDatabase->getAutomationKeyframes(objectId, effectName.toStdString(), parameterName.toStdString());
}

void PluginProcessor::addFxAutomationKeyframe(int objectId,
                                              const juce::String &effectName,
                                              const juce::String &parameterName,
                                              double timeSec,
                                              float value,
                                              float curvature)
{
    if (objectDatabase == nullptr || objectId < 0)
        return;

    objectDatabase->addAutomationKeyframe(objectId,
                                          effectName.toStdString(),
                                          parameterName.toStdString(),
                                          timeSec,
                                          value,
                                          curvature);
}

void PluginProcessor::setFxAutomationSegmentCurvature(int objectId,
                                                      const juce::String &effectName,
                                                      const juce::String &parameterName,
                                                      double segmentStartTimeSec,
                                                      float curvature)
{
    if (objectDatabase == nullptr || objectId < 0)
        return;

    objectDatabase->setAutomationSegmentCurvature(objectId,
                                                  effectName.toStdString(),
                                                  parameterName.toStdString(),
                                                  segmentStartTimeSec,
                                                  curvature);
}

void PluginProcessor::setTransformSourceObjectId(int objectId, int sourceObjectId)
{
    if (objectDatabase == nullptr || objectId < 0)
        return;

    objectDatabase->setObjectFxSourceObjectId(objectId, "Transform", sourceObjectId);
}

int PluginProcessor::getTransformSourceObjectId(int objectId) const
{
    if (objectDatabase == nullptr || objectId < 0)
        return -1;

    return objectDatabase->getObjectFxSourceObjectId(objectId, "Transform");
}

void PluginProcessor::loadTransformFileAsync(int objectId, const juce::File &file)
{
    if (objectId < 0 || !file.existsAsFile())
        return;

    std::thread([this, objectId, file]()
                {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
            return;

        const int64_t numSamples = juce::jmin<int64_t>(reader->lengthInSamples, 1 << 20);
        if (numSamples <= 0)
            return;

        juce::AudioBuffer<float> fileAudio(static_cast<int>(reader->numChannels), static_cast<int>(numSamples));
        if (!reader->read(&fileAudio, 0, static_cast<int>(numSamples), 0, true, true))
            return;

            // Normalisierung auf Basis des RMS
double sumSq = 0.0;
int numChannels = fileAudio.getNumChannels();
int numSamps = fileAudio.getNumSamples();

for (int ch = 0; ch < numChannels; ++ch) {
    const float* d = fileAudio.getReadPointer(ch);
    for (int i = 0; i < numSamps; ++i) sumSq += (double)d[i]*d[i];
}
double rms  = std::sqrt(sumSq / juce::jmax(1, numChannels*numSamps));
float  peak = fileAudio.getMagnitude(0, numSamps);

// Ziel-RMS = -20 dBFS  (≈ 0.1) statt 1.0
const double targetRms = 0.1;
float scale = (rms > 1.0e-7) ? (float)(targetRms / rms) : 1.0f;

// Peak-Cap: nach Skalierung darf Peak <= 0.95 sein
if (peak * scale > 0.95f)
    scale = 0.95f / juce::jmax(1.0e-6f, peak);

for (int ch = 0; ch < numChannels; ++ch)
    fileAudio.applyGain(ch, 0, numSamps, scale);


        juce::dsp::FFT localFft(fftOrder);
        std::vector<std::array<float, ObjectDatabase::NUM_BINS>> frames;

        for (int64_t start = 0; start < numSamples; start += hopSize)
        {
            frames.push_back(makeFrameFromFile(fileAudio, localFft, window, fftSize, static_cast<int>(start)));
        }

        if (frames.empty())
            frames.push_back(makeFrameFromFile(fileAudio, localFft, window, fftSize, 0));

        juce::ScopedLock sl(transformFileLock);
        TransformFileData data;
        data.frames = std::move(frames);
        data.durationSeconds = juce::jmax(0.0, static_cast<double>(numSamples) / reader->sampleRate);
        transformFileBuffer[objectId] = std::move(data); })
        .detach();
}

int PluginProcessor::createTransformObjectFromPreset(const juce::String &presetName)
{
    if (objectDatabase == nullptr)
        return -1;

    const juce::String cleanName = presetName.trim();
    if (cleanName.isEmpty())
        return -1;

    const std::string objectName = ("Transform " + cleanName).toStdString();
    if (!objectDatabase->addObject(objectName))
        return -1;

    const int newIndex = objectDatabase->getNumObjects() - 1;
    const int newObjectId = objectDatabase->getObjectIdAtIndex(newIndex);
    if (newIndex < 0 || newObjectId < 0)
        return -1;

    std::array<bool, ObjectDatabase::NUM_BINS> fullMask{};
    fullMask.fill(true);
    objectDatabase->setObjectMask(newIndex, fullMask);
    objectDatabase->setObjectEngaged(newIndex, true);
    objectDatabase->setObjectMute(newIndex, false);
    objectDatabase->setObjectSolo(newIndex, false);

    {
        ObjectDatabase::ObjectMask created;
        if (objectDatabase->getObjectCopyById(newObjectId, created))
            calibrateDensityAnchor(created);
    }

    objectDatabase->addOrEnableObjectFx(newObjectId, "Transform");
    objectDatabase->setObjectFxEnabled(newObjectId, "Transform", true);
    objectDatabase->setObjectFxSourceObjectId(newObjectId, "Transform", -3);
    objectDatabase->addAutomationKeyframe(newObjectId, "Transform", "Amount", 0.0, 1.0f, 0.0f);
    objectDatabase->addAutomationKeyframe(newObjectId, "Transform", "Smooth", 0.0, 0.15f, 0.0f);

    const auto mags = buildTransformPresetSpectrum(cleanName);
    {
        juce::ScopedLock sl(transformFileLock);
        TransformFileData data;
        data.frames.push_back(mags);
        data.durationSeconds = 1.0;
        transformFileBuffer[newObjectId] = std::move(data);
    }

    setSelectedObjectId(newObjectId);
    return newObjectId;
}

int PluginProcessor::createTransformObjectFromFile(const juce::File &file)
{
    if (objectDatabase == nullptr || !file.existsAsFile())
        return -1;

    const std::string objectName = ("Transform File " + file.getFileNameWithoutExtension()).toStdString();
    if (!objectDatabase->addObject(objectName))
        return -1;

    const int newIndex = objectDatabase->getNumObjects() - 1;
    const int newObjectId = objectDatabase->getObjectIdAtIndex(newIndex);
    if (newIndex < 0 || newObjectId < 0)
        return -1;

    std::array<bool, ObjectDatabase::NUM_BINS> fullMask{};
    fullMask.fill(true);
    objectDatabase->setObjectMask(newIndex, fullMask);
    objectDatabase->setObjectEngaged(newIndex, true);
    objectDatabase->setObjectMute(newIndex, false);
    objectDatabase->setObjectSolo(newIndex, false);

    {
        ObjectDatabase::ObjectMask created;
        if (objectDatabase->getObjectCopyById(newObjectId, created))
            calibrateDensityAnchor(created);
    }

    objectDatabase->addOrEnableObjectFx(newObjectId, "Transform");
    objectDatabase->setObjectFxEnabled(newObjectId, "Transform", true);
    objectDatabase->setObjectFxSourceObjectId(newObjectId, "Transform", ObjectDatabase::FILE_SOURCE_ID);
    objectDatabase->addAutomationKeyframe(newObjectId, "Transform", "Amount", 0.0, 1.0f, 0.0f);
    objectDatabase->addAutomationKeyframe(newObjectId, "Transform", "Smooth", 0.0, 0.15f, 0.0f);

    loadTransformFileAsync(newObjectId, file);
    setSelectedObjectId(newObjectId);
    return newObjectId;
}

int PluginProcessor::createTransientObject()
{
    if (objectDatabase == nullptr)
        return -1;

    auto findObjectIndexByName = [this](const juce::String& wanted)
    {
        const int n = objectDatabase->getNumObjects();
        for (int i = 0; i < n; ++i)
        {
            ObjectDatabase::ObjectMask obj;
            if (!objectDatabase->getObjectCopy(i, obj))
                continue;

            if (juce::String(obj.name).equalsIgnoreCase(wanted))
                return i;
        }
        return -1;
    };

    std::array<bool, ObjectDatabase::NUM_BINS> transientMask{};
    for (int k = 0; k < ObjectDatabase::NUM_BINS; ++k)
        transientMask[static_cast<size_t>(k)] = true;

    int idx = findObjectIndexByName("Transients");
    if (idx < 0)
    {
        if (!objectDatabase->addObject("Transients"))
            return -1;

        idx = objectDatabase->getNumObjects() - 1;
    }

    objectDatabase->setObjectMask(idx, transientMask);
    objectDatabase->setObjectColor(idx, static_cast<int>(juce::Colour(0xFFFF5252).getARGB()));
    objectDatabase->setObjectEngaged(idx, true);

    const int objectId = objectDatabase->getObjectIdAtIndex(idx);
    {
        ObjectDatabase::ObjectMask created;
        if (objectDatabase->getObjectCopyById(objectId, created))
            calibrateDensityAnchor(created);
    }
    setSelectedObjectId(objectId);
    return objectId;
}

void PluginProcessor::deleteFxAutomationKeyframe(int objectId,
                                                 const juce::String &effectName,
                                                 const juce::String &parameterName,
                                                 double timeSec)
{
    if (objectDatabase == nullptr || objectId < 0)
        return;

    objectDatabase->deleteAutomationKeyframe(objectId, effectName.toStdString(), parameterName.toStdString(), timeSec);
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

void PluginProcessor::cancelAutoDetectObjects()
{
    juce::ScopedLock sl(segmentationLock);
    autoDetectActive = false;
    autoDetectRecording = false;
    autoDetectTargetSamples = 0;
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

int PluginProcessor::getAutoDetectFrameCount() const
{
    juce::ScopedLock sl(segmentationLock);
    return autoDetectFrameCount;
}

bool PluginProcessor::isAutoDetectRunning() const
{
    juce::ScopedLock sl(segmentationLock);
    return autoDetectActive || autoDetectRecording;
}

bool PluginProcessor::getSegmentationOverlay(std::array<float, SpectralFrameBuffer::NUM_BINS> &transient,
                                             std::array<float, SpectralFrameBuffer::NUM_BINS> &tonal,
                                             std::array<float, SpectralFrameBuffer::NUM_BINS> &noise) const
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

    auto getMinMax = [](const std::deque<float> &hist, float fallback)
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
        return std::pair<float, float>{minV, maxV};
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

void PluginProcessor::analyseSegmentationFrame(const float *fftInterleaved, int64_t currentSampleIndex)
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

    const bool isTransientFrame = transientGateOpen.load();

    spectralFluxHistory.push_back(0.0f);
    hfcHistory.push_back(hfc);
    odfHistory.push_back(isTransientFrame ? 1.0f : 0.0f);
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
                    magLin[static_cast<size_t>(juce::jmin(numBins - 1, k + 2))]};
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
            if (k >= 2)
                hps *= magLin[static_cast<size_t>(k / 2)];
            if (k >= 3)
                hps *= magLin[static_cast<size_t>(k / 3)];
            if (k >= 4)
                hps *= magLin[static_cast<size_t>(k / 4)];
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
            if (k < lowCut)
                lowFluxSum += delta;
            if (k > highCut)
                highFluxSum += delta;
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
    for (int k = 0; k < numBins; ++k)
    {
        const float currentEnergy = magLin[static_cast<size_t>(k)];
        const float prevEnergy = hasPreviousMagnitudes ? previousMagnitudes[static_cast<size_t>(k)] : currentEnergy;

        const float rawSlope = (currentEnergy - prevEnergy) / (prevEnergy + eps);
        const float positiveSlope = juce::jmax(0.0f, rawSlope);
        attackSlope[static_cast<size_t>(k)] = 0.60f * attackSlope[static_cast<size_t>(k)] + 0.40f * positiveSlope;

        const float effectiveSlope = attackSlope[static_cast<size_t>(k)];
        lastAttackTime[static_cast<size_t>(k)] = std::log10(1.0f / (effectiveSlope + eps) + 1.0f);

        if (effectiveSlope > 0.15f)
        {
            totalAttackSlope += effectiveSlope;
            ++attackBins;
        }
    }

    globalAttackSlope = attackBins > 0 ? totalAttackSlope / attackBins : 0.0f;

    // Manual transient gate is broadband and binary.
    transientMask.fill(isTransientFrame ? 1.0f : 0.0f);

    // Tonal persistence must not depend on the transient gate.
    for (auto &p : tonalPersistence)
        p *= 0.96f;

    for (int k = 2; k < numBins - 2; ++k)
    {
        // Bestehende Peak-Prominence-Logik
        const float centerLin = magLin[static_cast<size_t>(k)];
        if (!(centerLin > magLin[static_cast<size_t>(k - 1)] && centerLin >= magLin[static_cast<size_t>(k + 1)]))
            continue;

        std::array<float, 4> neighDb = {
            20.0f * std::log10(magLin[static_cast<size_t>(k - 2)] + eps),
            20.0f * std::log10(magLin[static_cast<size_t>(k - 1)] + eps),
            20.0f * std::log10(magLin[static_cast<size_t>(k + 1)] + eps),
            20.0f * std::log10(magLin[static_cast<size_t>(k + 2)] + eps)};
        std::sort(neighDb.begin(), neighDb.end());
        const float neighMedianDb = 0.5f * (neighDb[1] + neighDb[2]);
        const float centerDb = 20.0f * std::log10(centerLin + eps);
        const float prominenceDb = centerDb - neighMedianDb;

        float tonalCandidate = juce::jlimit(0.0f, 1.0f, (prominenceDb - 3.0f) / 5.0f);

        const float kNorm = static_cast<float>(k) / static_cast<float>(numBins - 1);
        const float lowBandBoost = juce::jlimit(1.0f, 1.22f,
                                                1.22f - 1.35f * juce::jmin(1.0f, kNorm / 0.16f));
        const float highBandPenalty = juce::jlimit(0.45f, 1.0f, 1.0f - 0.95f * juce::jmax(0.0f, kNorm - 0.62f));
        const float flatnessPenalty = juce::jlimit(0.20f, 1.0f,
                                                   1.08f - 1.10f * lastFlatness[static_cast<size_t>(k)]);
        const float attackPenalty = juce::jlimit(0.10f, 1.0f,
                                                 1.00f - 0.85f * juce::jlimit(0.0f, 1.0f, attackSlope[static_cast<size_t>(k)] / 0.45f));
        const float fluxPenalty = juce::jlimit(0.18f, 1.0f,
                                               1.00f - 0.75f * juce::jlimit(0.0f, 1.0f, broadbandFlux[static_cast<size_t>(k)] / 0.35f));
        tonalCandidate *= lowBandBoost;
        tonalCandidate *= highBandPenalty;
        tonalCandidate *= flatnessPenalty;
        tonalCandidate *= attackPenalty;
        tonalCandidate *= fluxPenalty;

        if (useHPSSPrePass)
        {
            tonalCandidate *= (0.50f + 0.70f * hpHarmonicMask[static_cast<size_t>(k)]);
            tonalCandidate *= (1.0f - 0.83f * hpPercussiveMask[static_cast<size_t>(k)]);
        }

        if (useHybridMode)
            tonalCandidate *= (0.55f + 0.75f * hpsScore[static_cast<size_t>(k)]);

        float percussiveLikelihood = 0.0f;
        if (kNorm > 0.10f)
        {
            const float fluxN = juce::jlimit(0.0f, 1.0f, broadbandFlux[static_cast<size_t>(k)] / 0.22f);
            const float attackN = juce::jlimit(0.0f, 1.0f, attackSlope[static_cast<size_t>(k)] / 0.14f);
            const float percN = juce::jlimit(0.0f, 1.0f, hpPercussiveMask[static_cast<size_t>(k)] / 0.62f);
            percussiveLikelihood = juce::jlimit(0.0f, 1.0f,
                                                0.42f * fluxN + 0.33f * attackN + 0.40f * percN);

            const float damping = (kNorm > 0.45f)
                                      ? (1.0f - 0.90f * percussiveLikelihood)
                                      : (1.0f - 0.75f * percussiveLikelihood);
            tonalCandidate *= juce::jlimit(0.08f, 1.0f, damping);

            // Extra snare suppression in mid-band while keeping low-end intact.
            if (kNorm > 0.20f && kNorm < 0.50f)
                tonalCandidate *= juce::jlimit(0.12f, 1.0f, 1.0f - 0.22f * percussiveLikelihood);
        }

        if (percussiveLikelihood > 0.58f)
        {
            tonalPersistence[static_cast<size_t>(k)] =
                0.95f * tonalPersistence[static_cast<size_t>(k)] + 0.05f * tonalCandidate;
        }
        else
        {
            tonalPersistence[static_cast<size_t>(k)] =
                0.88f * tonalPersistence[static_cast<size_t>(k)] + 0.12f * tonalCandidate;
        }

        // Nur während aktiver Detection akkumulieren
        // Basiert auf dem rohen tonalCandidate – BEVOR er gegen Noise/Transienten konkurriert
        if (autoDetectActive || autoDetectRecording)
        {
            const float kNorm = static_cast<float>(k) / static_cast<float>(numBins - 1);
            const bool lowBandExempt = (kNorm < 0.10f);
            // Flux is used as a conservative veto only when it agrees with
            // percussive/attack evidence, to avoid suppressing true tonal sustain.
            const bool strongFlux = (broadbandFlux[static_cast<size_t>(k)] > 0.085f);
            const bool veryStrongFlux = (broadbandFlux[static_cast<size_t>(k)] > 0.16f);
            const bool strongPercussive = (hpPercussiveMask[static_cast<size_t>(k)] > 0.50f);
            const bool fastAttack = (attackSlope[static_cast<size_t>(k)] > 0.075f);
            const bool snareBand = (kNorm > 0.20f && kNorm < 0.50f);
            const bool snareLike = snareBand && (broadbandFlux[static_cast<size_t>(k)] > 0.12f) && ((hpPercussiveMask[static_cast<size_t>(k)] > 0.46f) || (attackSlope[static_cast<size_t>(k)] > 0.07f));
            const bool percussiveLike = !lowBandExempt && ((strongFlux && (strongPercussive || fastAttack)) || (strongPercussive && fastAttack) || (veryStrongFlux && kNorm > 0.20f) || (strongFlux && kNorm > 0.45f) || snareLike);

            const float tonalCandidateMin = snareBand ? 0.16f : 0.12f;
            if (tonalCandidate > tonalCandidateMin && !percussiveLike) // Nur echte Sustain-Kandidaten zählen
            {
                tonalDetectionCount[static_cast<size_t>(k)] += 1.0f;
                // Magnitude als Gewichtung: stärkere Töne bekommen mehr Einfluss
                tonalDetectionMagnitude[static_cast<size_t>(k)] += tonalCandidate;
            }
        }

        tonalMask[static_cast<size_t>(k)] =
            juce::jlimit(0.0f, 1.0f, (tonalPersistence[static_cast<size_t>(k)] - 0.14f) / 0.86f);
    }

    // === Ambient as active noise class (not pure residual) ===
    for (int k = 0; k < numBins; ++k)
    {
        const float kNorm = static_cast<float>(k) / static_cast<float>(juce::jmax(1, numBins - 1));
        const float sfm = lastFlatness[static_cast<size_t>(k)];
        const float flatnessScore = juce::jlimit(0.0f, 1.0f, (sfm - 0.58f) / 0.30f);
        const float tonalDuck = juce::jlimit(0.04f, 1.0f, 1.0f - 0.96f * tonalMask[static_cast<size_t>(k)]);
        const float persistenceDuck = juce::jlimit(0.04f, 1.0f, 1.0f - 0.92f * tonalPersistence[static_cast<size_t>(k)]);
        const float harmonicPenalty = juce::jlimit(0.08f, 1.0f, 1.0f - 0.85f * hpHarmonicMask[static_cast<size_t>(k)]);
        const float hpsPenalty = juce::jlimit(0.20f, 1.0f, 1.0f - 0.65f * hpsScore[static_cast<size_t>(k)]);

        const float fluxN = juce::jlimit(0.0f, 1.0f, broadbandFlux[static_cast<size_t>(k)] / 0.24f);
        const float attackN = juce::jlimit(0.0f, 1.0f, attackSlope[static_cast<size_t>(k)] / 0.16f);
        const float percN = juce::jlimit(0.0f, 1.0f, hpPercussiveMask[static_cast<size_t>(k)] / 0.62f);
        const float percussiveLikelihood = juce::jlimit(0.0f, 1.0f,
                                                        0.45f * fluxN + 0.35f * attackN + 0.38f * percN);
        const float percussivePenalty = juce::jlimit(0.10f, 1.0f, 1.0f - 0.80f * percussiveLikelihood);

        const float lowBandPenalty = juce::jlimit(0.20f, 1.0f,
                                                  0.35f + 0.65f * juce::jmin(1.0f, kNorm / 0.11f));
        const float midHighBoost = juce::jlimit(1.0f, 1.10f,
                                                1.0f + 0.10f * juce::jmax(0.0f, juce::jmin(1.0f, (kNorm - 0.20f) / 0.50f)));

        float noiseScore = flatnessScore * tonalDuck * persistenceDuck * harmonicPenalty * hpsPenalty * percussivePenalty * lowBandPenalty;
        noiseScore *= midHighBoost;
        noiseMask[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, noiseScore);
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
        recordedGateFrames.push_back(transientGateOpen.load());

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

void PluginProcessor::applyCosineMaskSmoothing(const std::array<float, SpectralFrameBuffer::NUM_BINS> &input,
                                               std::array<float, SpectralFrameBuffer::NUM_BINS> &output) const
{
    static constexpr int radius = 6;
    for (int bin = 0; bin < SpectralFrameBuffer::NUM_BINS; ++bin)
    {
        float weighted = 0.0f;
        float totalWeight = 0.0f;

        for (int k = -radius; k <= radius; ++k)
        {
            const int idx = juce::jlimit(0, SpectralFrameBuffer::NUM_BINS - 1, bin + k);
            const float w = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * static_cast<float>(k) / static_cast<float>(radius + 1)));
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
    recordedGateFrames.clear();
    autoDetectFrameCount = 0;
    autoDetectTransientFrameCount = 0;
    autoDetectNonTransientFrameCount = 0;
    tonalDetectionCount.fill(0.0f);
    tonalDetectionMagnitude.fill(0.0f);
}

void PluginProcessor::finalizeAutoDetectedObjects()
{
    if (objectDatabase == nullptr || autoDetectFrameCount <= 0)
        return;

    constexpr int numBins = SpectralFrameBuffer::NUM_BINS;
    constexpr float eps = 1.0e-9f;
    const int numFrames = static_cast<int>(recordedMagnitudeFrames.size());

    std::array<bool, numBins> transientMask{};
    std::array<bool, numBins> tonalMask{};
    std::array<bool, numBins> noiseMask{};

    // Transients: broadband gate, unverändert
    for (int k = 0; k < numBins; ++k)
        transientMask[static_cast<size_t>(k)] = true;

    if (numFrames >= 4)
    {
        // ================================================================
        // SCHRITT 1: Mittlere Energie und Varianz pro Bin über alle Frames
        // rawMagLin ist linear + absolut → wir rechnen in dB für
        // skalierungsunabhängige Statistik.
        //
        // Performance: mean/variance are computed in one pass (Welford),
        // which preserves the metric while avoiding a second full log pass.
        // ================================================================
        std::array<float, numBins> meanDb{};
        std::array<float, numBins> varDb{};
        std::array<float, numBins> meanLin{};

        for (int k = 0; k < numBins; ++k)
        {
            float sumLin = 0.0f;
            float mean = 0.0f;
            float m2 = 0.0f;
            for (int t = 0; t < numFrames; ++t)
            {
                const float lin = recordedMagnitudeFrames[static_cast<size_t>(t)][static_cast<size_t>(k)] + eps;
                sumLin += lin;

                const float db = 20.0f * std::log10(lin);
                const float n = static_cast<float>(t + 1);
                const float delta = db - mean;
                mean += delta / n;
                const float delta2 = db - mean;
                m2 += delta * delta2;
            }

            const float invF = 1.0f / static_cast<float>(numFrames);
            meanDb[static_cast<size_t>(k)] = mean;
            meanLin[static_cast<size_t>(k)] = sumLin * invF;
            varDb[static_cast<size_t>(k)] = m2 * invF;
        }

        // ================================================================
        // SCHRITT 2: Globale Energie-Referenz für Noise-Floor-Cutoff
        // Bins deutlich unter dem globalen Schnitt werden ignoriert –
        // sonst wird stilles Rauschen als "stabil = tonal" erkannt.
        // ================================================================
        float globalMeanLin = 0.0f;
        for (int k = 0; k < numBins; ++k)
            globalMeanLin += meanLin[static_cast<size_t>(k)];
        globalMeanLin /= static_cast<float>(numBins);

        // Bins müssen genug absolute Energie haben, aber im Low-End etwas weniger
        // streng behandelt werden, damit Grundtöne nicht verschwinden.
        std::array<float, numBins> localFlatness{};
        std::array<float, numBins> energyFloorPerBin{};
        for (int k = 0; k < numBins; ++k)
        {
            const float kNorm = static_cast<float>(k) / static_cast<float>(juce::jmax(1, numBins - 1));
            const float floorScale = 0.05f + 0.075f * std::sqrt(kNorm);
            energyFloorPerBin[static_cast<size_t>(k)] = globalMeanLin * floorScale;

            const int lo = juce::jmax(0, k - 3);
            const int hi = juce::jmin(numBins - 1, k + 3);
            float geoLogSum = 0.0f;
            float arithSum = 0.0f;
            int n = 0;
            for (int i = lo; i <= hi; ++i)
            {
                const float m = meanLin[static_cast<size_t>(i)] + eps;
                geoLogSum += std::log(m);
                arithSum += m;
                ++n;
            }

            const float geo = std::exp(geoLogSum / static_cast<float>(juce::jmax(1, n)));
            const float arith = arithSum / static_cast<float>(juce::jmax(1, n));
            localFlatness[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, geo / (arith + eps));
        }

        // ================================================================
        // SCHRITT 3: Tonal-Score pro Bin
        //
        // Stabilitätsmaß: stdDev in dB. Kleine stdDev = stabile Hüllkurve.
        // Schwelle: 6 dB stdDev entspricht einer Hüllkurve die um ±6 dB
        // schwankt – das ist die Grenze zwischen Sustain und Attack/Decay.
        //
        //   stdDb < 3 dB  → sehr stabil  → Score nahe 1.0
        //   stdDb = 6 dB  → Grenzbereich → Score 0.0
        //   stdDb > 6 dB  → instabil     → kein Tonal
        // ================================================================
        std::array<float, numBins> tonalScore{};
        std::array<float, numBins> baselineStabilityScore{};
        std::array<float, numBins> evidenceDrivenScore{};

        for (int k = 0; k < numBins; ++k)
        {
            // Energie-Cutoff: Bins unter dem Noise Floor nicht klassifizieren
            if (meanLin[static_cast<size_t>(k)] < energyFloorPerBin[static_cast<size_t>(k)])
                continue;

            const float stdDb = std::sqrt(varDb[static_cast<size_t>(k)] + eps);
            const float stabilityScore = juce::jlimit(0.0f, 1.0f, 1.0f - stdDb / 8.0f);
            baselineStabilityScore[static_cast<size_t>(k)] = stabilityScore;

            const float count = tonalDetectionCount[static_cast<size_t>(k)];
            const float meanCandidateStrength = count > 0.0f
                                                    ? (tonalDetectionMagnitude[static_cast<size_t>(k)] / count)
                                                    : 0.0f;
            const float countVsDuration = count / std::sqrt(static_cast<float>(juce::jmax(1, numFrames)));
            const float evidenceScore = 1.0f - std::exp(-countVsDuration / 1.6f);
            const float strengthScore = juce::jlimit(0.0f, 1.0f, (meanCandidateStrength - 0.12f) / 0.48f);
            const float flatnessPenalty = juce::jlimit(0.40f, 1.0f,
                                                       1.00f - 0.75f * localFlatness[static_cast<size_t>(k)]);
            const float kNorm = static_cast<float>(k) / static_cast<float>(juce::jmax(1, numBins - 1));
            const float lowBandBoost = juce::jlimit(1.0f, 1.30f,
                                                    1.30f - 1.55f * juce::jmin(1.0f, kNorm / 0.14f));

            evidenceDrivenScore[static_cast<size_t>(k)] =
                (0.18f + 0.82f * evidenceScore) * (0.28f + 0.72f * strengthScore) * juce::jlimit(0.55f, 1.0f, 1.03f - 0.60f * localFlatness[static_cast<size_t>(k)]) * lowBandBoost;

            evidenceDrivenScore[static_cast<size_t>(k)] *=
                (0.42f + 0.58f * baselineStabilityScore[static_cast<size_t>(k)]);

            tonalScore[static_cast<size_t>(k)] =
                (0.28f + 0.72f * stabilityScore) * (0.58f + 0.42f * evidenceScore) * (0.62f + 0.38f * strengthScore) * flatnessPenalty * lowBandBoost;

            tonalScore[static_cast<size_t>(k)] = juce::jmax(
                tonalScore[static_cast<size_t>(k)],
                evidenceDrivenScore[static_cast<size_t>(k)] * 0.84f);
        }

        // ================================================================
        // SCHRITT 4: Lokale Peak-Bedingung
        // Ein tonaler Bin muss auch spektral herausragen – nicht nur stabil
        // sein. Das trennt breite Noise-Plateaus von echten Tönen.
        // Wir prüfen ob der Bin in einem 5-Bin-Fenster ein lokaler Peak ist
        // (auf dem mittleren Energie-Wert, nicht frame-by-frame).
        // ================================================================
        std::array<float, numBins> tonalScoreFinal{};

        for (int k = 2; k < numBins - 2; ++k)
        {
            if (tonalScore[static_cast<size_t>(k)] < 0.01f)
                continue;

            // Lokaler Peak in meanLin?
            const float center = meanLin[static_cast<size_t>(k)];
            const bool isPeak = center > meanLin[static_cast<size_t>(k - 1)] && center > meanLin[static_cast<size_t>(k - 2)] && center >= meanLin[static_cast<size_t>(k + 1)] && center >= meanLin[static_cast<size_t>(k + 2)];

            // Prominenz gegen Nachbar-Median in dB (wie im Echtzeit-Pfad)
            std::array<float, 4> neighDb = {
                20.0f * std::log10(meanLin[static_cast<size_t>(k - 2)] + eps),
                20.0f * std::log10(meanLin[static_cast<size_t>(k - 1)] + eps),
                20.0f * std::log10(meanLin[static_cast<size_t>(k + 1)] + eps),
                20.0f * std::log10(meanLin[static_cast<size_t>(k + 2)] + eps)};
            std::sort(neighDb.begin(), neighDb.end());
            const float neighMedianDb = 0.5f * (neighDb[1] + neighDb[2]);
            const float centerDb = 20.0f * std::log10(center + eps);
            const float prominenceDb = centerDb - neighMedianDb;

            const float kNorm = static_cast<float>(k) / static_cast<float>(juce::jmax(1, numBins - 1));
            const float requiredProminenceDb = juce::jmap(juce::jmin(1.0f, kNorm / 0.18f), 2.0f, 3.5f);

            // Breite/noisy Plateaus nicht nach Tonal ziehen.
            if (localFlatness[static_cast<size_t>(k)] > 0.82f)
                continue;

            const float count = tonalDetectionCount[static_cast<size_t>(k)];
            const float countVsDuration = count / std::sqrt(static_cast<float>(juce::jmax(1, numFrames)));
            const float recurringCountThreshold = (kNorm > 0.20f) ? 0.70f : 0.55f;
            const float recurringScoreThreshold = (kNorm > 0.20f) ? 0.24f : 0.20f;
            const bool strongRecurringEvidence = countVsDuration > recurringCountThreshold && evidenceDrivenScore[static_cast<size_t>(k)] > recurringScoreThreshold;

            if (!isPeak && !strongRecurringEvidence)
                continue;

            if (prominenceDb < requiredProminenceDb && !strongRecurringEvidence)
                continue;

            // Score = Stabilität * normierte Prominenz
            const float promScore = juce::jlimit(0.0f, 1.0f, (prominenceDb - requiredProminenceDb) / 7.0f);
            const float peakDrivenScore = tonalScore[static_cast<size_t>(k)] * (0.62f + 0.38f * promScore);
            const float recurringFallback = strongRecurringEvidence
                                                ? evidenceDrivenScore[static_cast<size_t>(k)] * 0.88f
                                                : 0.0f;
            tonalScoreFinal[static_cast<size_t>(k)] = juce::jmax(peakDrivenScore, recurringFallback);
        }

        // ================================================================
        // SCHRITT 5: Harmonische Bindung
        // Erkannte Grundtöne ziehen ihre Obertöne mit.
        // Läuft von unten nach oben damit Grundtöne zuerst etabliert sind.
        // ================================================================
        for (int k = 2; k < numBins / 2; ++k)
        {
            if (tonalScoreFinal[static_cast<size_t>(k)] < 0.18f)
                continue;

            for (int h = 2; h <= 8; ++h)
            {
                const int hBin = k * h;
                if (hBin >= numBins)
                    break;

                // Oberton erbt 65% des Grundton-Scores wenn er selbst schwächer ist
                const float inherited = tonalScoreFinal[static_cast<size_t>(k)] * 0.72f;
                if (inherited > tonalScoreFinal[static_cast<size_t>(hBin)])
                    tonalScoreFinal[static_cast<size_t>(hBin)] = inherited;
            }
        }

        int tonalBinCount = 0;
        for (int k = 0; k < numBins; ++k)
        {
            if (tonalScoreFinal[static_cast<size_t>(k)] > 0.12f)
                ++tonalBinCount;
        }

        // Safety fallback: keep the additive-evidence path, but prevent a fully
        // empty tonal object when the combined criteria are still too strict.
        if (tonalBinCount < 6)
        {
            for (int k = 2; k < numBins - 2; ++k)
            {
                if (meanLin[static_cast<size_t>(k)] < energyFloorPerBin[static_cast<size_t>(k)])
                    continue;

                const float center = meanLin[static_cast<size_t>(k)];
                std::array<float, 4> neighDb = {
                    20.0f * std::log10(meanLin[static_cast<size_t>(k - 2)] + eps),
                    20.0f * std::log10(meanLin[static_cast<size_t>(k - 1)] + eps),
                    20.0f * std::log10(meanLin[static_cast<size_t>(k + 1)] + eps),
                    20.0f * std::log10(meanLin[static_cast<size_t>(k + 2)] + eps)};
                std::sort(neighDb.begin(), neighDb.end());
                const float neighMedianDb = 0.5f * (neighDb[1] + neighDb[2]);
                const float centerDb = 20.0f * std::log10(center + eps);
                const float prominenceDb = centerDb - neighMedianDb;

                if (prominenceDb < 1.8f)
                    continue;

                const float fallbackScore = juce::jmax(
                    baselineStabilityScore[static_cast<size_t>(k)] * juce::jlimit(0.45f, 1.0f, 1.0f - 0.55f * localFlatness[static_cast<size_t>(k)]),
                    evidenceDrivenScore[static_cast<size_t>(k)] * 0.72f);

                tonalScoreFinal[static_cast<size_t>(k)] = juce::jmax(
                    tonalScoreFinal[static_cast<size_t>(k)],
                    fallbackScore * 0.58f);
            }
        }

        // ================================================================
        // SCHRITT 6: Ambient-Score pro Bin
        // Ambient ist eigene Klasse (nicht bloß Residual): hohe Flatness,
        // wenig Tonalität und geringe Kopplung an gate-offene Frames.
        // ================================================================
        std::array<float, numBins> ambientScore{};
        for (int k = 0; k < numBins; ++k)
        {
            if (meanLin[static_cast<size_t>(k)] < energyFloorPerBin[static_cast<size_t>(k)])
                continue;

            const float kNorm = static_cast<float>(k) / static_cast<float>(juce::jmax(1, numBins - 1));
            const float flatnessScore = juce::jlimit(0.0f, 1.0f,
                                                     (localFlatness[static_cast<size_t>(k)] - 0.56f) / 0.32f);
            const float nonTonal = juce::jlimit(0.02f, 1.0f,
                                                1.0f - 1.35f * tonalScoreFinal[static_cast<size_t>(k)]);
            const float stabilityPenalty = juce::jlimit(0.18f, 1.0f,
                                                        1.0f - 0.70f * baselineStabilityScore[static_cast<size_t>(k)]);
            const float tonalEvidencePenalty = juce::jlimit(0.10f, 1.0f,
                                                            1.0f - 0.90f * evidenceDrivenScore[static_cast<size_t>(k)]);
            const float lowBandPenalty = juce::jlimit(0.30f, 1.0f,
                                                      0.40f + 0.60f * juce::jmin(1.0f, kNorm / 0.12f));
            const float midHighBoost = juce::jlimit(1.0f, 1.10f,
                                                    1.0f + 0.10f * juce::jmax(0.0f, juce::jmin(1.0f, (kNorm - 0.22f) / 0.55f)));

            int activeFrames = 0;
            int gateOpenActiveFrames = 0;
            const float activeFloor = energyFloorPerBin[static_cast<size_t>(k)] * 0.85f;
            for (int t = 0; t < numFrames; ++t)
            {
                const float m = recordedMagnitudeFrames[static_cast<size_t>(t)][static_cast<size_t>(k)];
                if (m < activeFloor)
                    continue;

                ++activeFrames;
                if (t < static_cast<int>(recordedGateFrames.size()) && recordedGateFrames[static_cast<size_t>(t)])
                    ++gateOpenActiveFrames;
            }

            const float gateCoupling = activeFrames > 0
                                           ? static_cast<float>(gateOpenActiveFrames) / static_cast<float>(activeFrames)
                                           : 0.0f;
            const float gatePenalty = juce::jlimit(0.20f, 1.0f, 1.0f - 0.85f * gateCoupling);

            ambientScore[static_cast<size_t>(k)] = flatnessScore * nonTonal * stabilityPenalty * tonalEvidencePenalty * lowBandPenalty * gatePenalty * midHighBoost;
        }

        // ================================================================
        // SCHRITT 7: Masken aus Scores
        // Tonal bleibt führend auf tonal geprägten Bins.
        // Ambient folgt eigener Noise-Wahrscheinlichkeit.
        // ================================================================
        for (int k = 0; k < numBins; ++k)
        {
            tonalMask[static_cast<size_t>(k)] = (tonalScoreFinal[static_cast<size_t>(k)] > 0.12f);
            noiseMask[static_cast<size_t>(k)] =
                !tonalMask[static_cast<size_t>(k)] && (ambientScore[static_cast<size_t>(k)] > 0.30f) && (meanLin[static_cast<size_t>(k)] >= energyFloorPerBin[static_cast<size_t>(k)]);
        }
    }

    auto findObjectIndexByName = [this](const juce::String &wanted)
    {
        if (objectDatabase == nullptr)
            return -1;

        const int n = objectDatabase->getNumObjects();
        for (int i = 0; i < n; ++i)
        {
            ObjectDatabase::ObjectMask obj;
            if (!objectDatabase->getObjectCopy(i, obj))
                continue;

            if (juce::String(obj.name).equalsIgnoreCase(wanted))
                return i;
        }
        return -1;
    };

    auto ensureAndMaybeUpdate = [this, &findObjectIndexByName](const juce::String &name,
                                                               const std::array<bool, ObjectDatabase::NUM_BINS> &mask,
                                                               const juce::Colour &color)
    {
        int idx = findObjectIndexByName(name);
        if (idx < 0)
        {
            if (!objectDatabase->addObject(name.toStdString()))
                return;

            idx = objectDatabase->getNumObjects() - 1;
            objectDatabase->setObjectMask(idx, mask);
            objectDatabase->setObjectColor(idx, static_cast<int>(color.getARGB()));
            const int objectId = objectDatabase->getObjectIdAtIndex(idx);
            ObjectDatabase::ObjectMask created;
            if (objectDatabase->getObjectCopyById(objectId, created))
                calibrateDensityAnchor(created);
            return;
        }

        ObjectDatabase::ObjectMask existing;
        if (!objectDatabase->getObjectCopy(idx, existing))
            return;

        if (existing.recordEnabled)
        {
            objectDatabase->setObjectMask(idx, mask);
            ObjectDatabase::ObjectMask updated;
            if (objectDatabase->getObjectCopy(idx, updated))
                calibrateDensityAnchor(updated);
        }

        objectDatabase->setObjectColor(idx, static_cast<int>(color.getARGB()));
    };

    ensureAndMaybeUpdate("Transients", transientMask, juce::Colour(0xFFFF5252));
    ensureAndMaybeUpdate("Tonal Components", tonalMask, juce::Colour(0xFF4AA3FF));
    ensureAndMaybeUpdate("Ambient Noise", noiseMask, juce::Colour(0xFF4FD16A));
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
                                              2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
        window[i] = std::sqrt(juce::jmax(0.0f, hann));
    }
}

//==============================================================================
// Factory function required by JUCE to create the plugin instance
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}