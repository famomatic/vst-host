#include "audio/DeviceEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <utility>

namespace host::audio
{
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
        prepareResamplers();
    }

    void DeviceEngine::setGraph(std::shared_ptr<host::graph::GraphEngine> newGraph)
    {
        graphEngine.store(std::move(newGraph));

        if (auto graph = graphEngine.load())
            graph->setEngineFormat(engineConfig.sampleRate, engineConfig.blockSize);
    }

    void DeviceEngine::setEngineConfig(const EngineConfig& cfg)
    {
        engineConfig = cfg;

        if (auto graph = graphEngine.load())
            graph->setEngineFormat(engineConfig.sampleRate, engineConfig.blockSize);

        prepareResamplers();
    }

    void DeviceEngine::setDeviceInfo(const DeviceInfo& info)
    {
        deviceInfo = info;
        prepareResamplers();
    }

    void DeviceEngine::audioDeviceIOCallback(const float* const* inputChannelData,
                                             int numInputChannels,
                                             float* const* outputChannelData,
                                             int numOutputChannels,
                                             int numSamples)
    {
        clearOutputs(outputChannelData, numOutputChannels, numSamples);

        if (numSamples <= 0 || engineBuffer.getNumChannels() == 0)
            return;

        const int channels = engineBuffer.getNumChannels();

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* source = (inputChannelData != nullptr && ch < numInputChannels && inputChannelData[ch] != nullptr)
                ? inputChannelData[ch]
                : nullptr;
            inputPointerScratch[static_cast<size_t>(ch)] = source;

            float* dest = (outputChannelData != nullptr && ch < numOutputChannels)
                ? outputChannelData[ch]
                : nullptr;
            outputPointerScratch[static_cast<size_t>(ch)] = dest;

            engineWritePointers[static_cast<size_t>(ch)] = engineBuffer.getWritePointer(ch);
            engineReadPointers[static_cast<size_t>(ch)] = engineBuffer.getReadPointer(ch);
        }

        inputResampler.push(inputPointerScratch.data(), numSamples);

        auto graph = graphEngine.load();
        const int engineBlockSize = std::max(1, engineConfig.blockSize);

        while (inputResampler.canProcess(engineBlockSize))
        {
            inputResampler.process(engineWritePointers.data(), engineBlockSize);

            if (graph)
                graph->process(engineBuffer);
            else
                engineBuffer.clear();

            outputResampler.push(engineReadPointers.data(), engineBlockSize);
        }

        const int produced = outputResampler.process(outputPointerScratch.data(), numSamples);
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

        inputResampler.reset();
        outputResampler.reset();
    }

    void DeviceEngine::audioDeviceStopped()
    {
        inputResampler.reset();
        outputResampler.reset();
        engineBuffer.clear();
    }

    void DeviceEngine::prepareResamplers()
    {
        const int numChannels = std::max(2, std::max(deviceInfo.inputChannels, deviceInfo.outputChannels));
        const int engineBlockSize = std::max(1, engineConfig.blockSize);
        const int deviceBlockSize = std::max(1, deviceInfo.blockSize);

        engineBuffer.setSize(numChannels, engineBlockSize, false, false, true);
        prepareScratchBuffers(numChannels);

        const int inputChunk = std::max(deviceBlockSize, engineBlockSize) * 2;
        const double deviceToEngine = safeRatio(deviceInfo.sampleRate, engineConfig.sampleRate);
        inputResampler.prepare(numChannels, deviceToEngine, inputChunk, engineBlockSize, resamplerMargin);
        inputResampler.reset();

        const int outputChunk = std::max(deviceBlockSize, engineBlockSize) * 2;
        const double engineToDevice = safeRatio(engineConfig.sampleRate, deviceInfo.sampleRate);
        outputResampler.prepare(numChannels, engineToDevice, engineBlockSize, outputChunk, resamplerMargin);
        outputResampler.reset();
    }

    void DeviceEngine::prepareScratchBuffers(int numChannels)
    {
        inputPointerScratch.resize(static_cast<size_t>(numChannels));
        outputPointerScratch.resize(static_cast<size_t>(numChannels));
        engineWritePointers.resize(static_cast<size_t>(numChannels));
        engineReadPointers.resize(static_cast<size_t>(numChannels));
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
