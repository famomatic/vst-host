#include "audio/DeviceEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <utility>

namespace host::audio
{
    struct DeviceEngine::ProcessingState
    {
        juce::AudioBuffer<float> engineBuffer;
        BlockResampler inputResampler;
        BlockResampler outputResampler;
        std::vector<const float*> inputPointerScratch;
        std::vector<float*> outputPointerScratch;
        std::vector<float*> engineWritePointers;
        std::vector<const float*> engineReadPointers;
        int engineBlockSize { 1 };
    };

    namespace
    {
        constexpr int resamplerMargin = 12;

        [[nodiscard]] double safeRatio(double numerator, double denominator) noexcept
        {
            if (denominator <= 0.0 || numerator <= 0.0)
                return 1.0;
            return numerator / denominator;
        }
    }

    DeviceEngine::DeviceEngine()
    {
        deviceInfo.sampleRate = engineConfig.sampleRate;
        deviceInfo.blockSize = engineConfig.blockSize;
        deviceInfo.inputChannels = 2;
        deviceInfo.outputChannels = 2;
        rebuildProcessingState(engineConfig, deviceInfo);
    }

    void DeviceEngine::setGraph(std::shared_ptr<host::graph::GraphEngine> newGraph)
    {
        graphEngine.store(std::move(newGraph));

        EngineConfig cfg = getEngineConfig();

        if (auto graph = graphEngine.load())
        {
            graph->setPdcEnabled(cfg.pdcEnabled);
            graph->setEngineFormat(cfg.sampleRate, cfg.blockSize);
            graph->prepare();
        }
    }

    EngineConfig DeviceEngine::getEngineConfig() const
    {
        const std::lock_guard<std::mutex> lock(configMutex_);
        return engineConfig;
    }

    void DeviceEngine::setEngineConfig(const EngineConfig& cfg)
    {
        DeviceInfo currentDeviceInfo;
        EngineConfig appliedConfig = cfg;

        {
            const std::lock_guard<std::mutex> lock(configMutex_);
            engineConfig = cfg;
            currentDeviceInfo = deviceInfo;
            appliedConfig = engineConfig;
        }

        rebuildProcessingState(appliedConfig, currentDeviceInfo);

        if (auto graph = graphEngine.load())
        {
            graph->setPdcEnabled(appliedConfig.pdcEnabled);
            graph->setEngineFormat(appliedConfig.sampleRate, appliedConfig.blockSize);
            graph->prepare();
        }
    }

    void DeviceEngine::setDeviceInfo(const DeviceInfo& info)
    {
        EngineConfig currentEngineConfig;
        DeviceInfo appliedInfo = info;

        {
            const std::lock_guard<std::mutex> lock(configMutex_);
            deviceInfo = info;
            currentEngineConfig = engineConfig;
            appliedInfo = deviceInfo;
        }

        rebuildProcessingState(currentEngineConfig, appliedInfo);
    }

    DeviceInfo DeviceEngine::getDeviceInfo() const
    {
        const std::lock_guard<std::mutex> lock(configMutex_);
        return deviceInfo;
    }

    void DeviceEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                        int numInputChannels,
                                                        float* const* outputChannelData,
                                                        int numOutputChannels,
                                                        int numSamples,
                                                        const juce::AudioIODeviceCallbackContext& context)
    {
        juce::ignoreUnused(context);
        clearOutputs(outputChannelData, numOutputChannels, numSamples);

        // ASIO/WASAPI may supply a high-resolution host timestamp; forward it
        // so time-aware plugins (delays, sync) can lock to the hardware clock.
        const std::uint64_t* hostTimeNs = (context.hostTimeNs != nullptr) ? context.hostTimeNs : nullptr;

        auto state = processingState_.load(std::memory_order_acquire);
        if (! state || numSamples <= 0 || state->engineBuffer.getNumChannels() == 0)
            return;

        const int channels = state->engineBuffer.getNumChannels();

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* source = (inputChannelData != nullptr && ch < numInputChannels && inputChannelData[ch] != nullptr)
                ? inputChannelData[ch]
                : nullptr;
            state->inputPointerScratch[static_cast<size_t>(ch)] = source;

            float* dest = (outputChannelData != nullptr && ch < numOutputChannels)
                ? outputChannelData[ch]
                : nullptr;
            state->outputPointerScratch[static_cast<size_t>(ch)] = dest;

            state->engineWritePointers[static_cast<size_t>(ch)] = state->engineBuffer.getWritePointer(ch);
            state->engineReadPointers[static_cast<size_t>(ch)] = state->engineBuffer.getReadPointer(ch);
        }

        state->inputResampler.push(state->inputPointerScratch.data(), numSamples);

        auto graph = graphEngine.load();
        const int engineBlockSize = std::max(1, state->engineBlockSize);

        while (state->inputResampler.canProcess(engineBlockSize))
        {
            state->inputResampler.process(state->engineWritePointers.data(), engineBlockSize);

            int produced = engineBlockSize;

            if (graph)
            {
                produced = graph->process(state->engineBuffer, hostTimeNs);
                if (produced <= 0)
                {
                    state->engineBuffer.clear();
                    break;
                }
            }
            else
            {
                state->engineBuffer.clear();
            }

            state->outputResampler.push(state->engineReadPointers.data(), produced);
        }

        const int produced = state->outputResampler.process(state->outputPointerScratch.data(), numSamples);
        if (produced < numSamples)
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
            {
                if (outputChannelData != nullptr && outputChannelData[ch] != nullptr)
                    juce::FloatVectorOperations::clear(outputChannelData[ch] + produced, numSamples - produced);
            }
        }
    }

    void DeviceEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
    {
        if (device == nullptr)
            return;

        DeviceInfo info;
        info.sampleRate = device->getCurrentSampleRate();
        info.blockSize = device->getCurrentBufferSizeSamples();
        info.inputChannels = device->getActiveInputChannels().countNumberOfSetBits();
        info.outputChannels = device->getActiveOutputChannels().countNumberOfSetBits();
        setDeviceInfo(info);
    }

    void DeviceEngine::audioDeviceStopped()
    {
        EngineConfig cfg;
        DeviceInfo info;
        {
            const std::lock_guard<std::mutex> lock(configMutex_);
            cfg = engineConfig;
            info = deviceInfo;
        }

        rebuildProcessingState(cfg, info);
    }

    std::shared_ptr<DeviceEngine::ProcessingState> DeviceEngine::buildProcessingState(const EngineConfig& cfg,
                                                                                       const DeviceInfo& info) const
    {
        auto state = std::make_shared<ProcessingState>();

        const int numChannels = std::max(2, std::max(info.inputChannels, info.outputChannels));
        const int engineBlockSize = std::max(1, cfg.blockSize);
        const int deviceBlockSize = std::max(1, info.blockSize);

        state->engineBlockSize = engineBlockSize;
        state->engineBuffer.setSize(numChannels, engineBlockSize, false, false, true);

        state->inputPointerScratch.resize(static_cast<size_t>(numChannels));
        state->outputPointerScratch.resize(static_cast<size_t>(numChannels));
        state->engineWritePointers.resize(static_cast<size_t>(numChannels));
        state->engineReadPointers.resize(static_cast<size_t>(numChannels));

        const int inputChunk = std::max(deviceBlockSize, engineBlockSize) * 2;
        const double deviceToEngine = safeRatio(info.sampleRate, cfg.sampleRate);
        state->inputResampler.prepare(numChannels, deviceToEngine, inputChunk, engineBlockSize,
                                      resamplerQualityFromIndex(cfg.resamplerQuality), resamplerMargin);
        state->inputResampler.reset();

        const int outputChunk = std::max(deviceBlockSize, engineBlockSize) * 2;
        const double engineToDevice = safeRatio(cfg.sampleRate, info.sampleRate);
        state->outputResampler.prepare(numChannels, engineToDevice, engineBlockSize, outputChunk,
                                       resamplerQualityFromIndex(cfg.resamplerQuality), resamplerMargin);
        state->outputResampler.reset();

        return state;
    }

    void DeviceEngine::rebuildProcessingState(const EngineConfig& cfg, const DeviceInfo& info)
    {
        processingState_.store(buildProcessingState(cfg, info), std::memory_order_release);
    }

    void DeviceEngine::clearOutputs(float* const* outputChannelData, int numOutputChannels, int numSamples)
    {
        if (outputChannelData == nullptr || numSamples <= 0)
            return;

        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            if (auto* dest = outputChannelData[ch])
                juce::FloatVectorOperations::clear(dest, numSamples);
        }
    }
}
