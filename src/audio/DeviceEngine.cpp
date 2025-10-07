#include "audio/DeviceEngine.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace host::audio
{
    DeviceEngine::DeviceEngine(std::shared_ptr<host::graph::GraphEngine> graphEngine)
        : graph(std::move(graphEngine))
    {
        engineBuffer.setSize(2, config.blockSize);
    }

    void DeviceEngine::setConfig(const EngineConfig& cfg)
    {
        config = cfg;
        if (graph)
            graph->setEngineFormat(config.sampleRate, config.blockSize);
    }

    void DeviceEngine::audioDeviceIOCallback(const float* const* inputChannelData,
                                             int numInputChannels,
                                             float* const* outputChannelData,
                                             int numOutputChannels,
                                             int numSamples)
    {
        ensureBuffers(std::max(1, std::max(numInputChannels, numOutputChannels)), config.blockSize);

        const auto deviceToEngineRatio = deviceFormat.sampleRate > 0.0
            ? config.sampleRate / deviceFormat.sampleRate
            : 1.0;
        const auto engineToDeviceRatio = deviceFormat.sampleRate > 0.0
            ? deviceFormat.sampleRate / config.sampleRate
            : 1.0;

        const auto numChannels = engineBuffer.getNumChannels();
        resamplerIn.prepare(numChannels, deviceToEngineRatio);
        resamplerOut.prepare(numChannels, engineToDeviceRatio);

        std::vector<const float*> deviceInputs(static_cast<size_t>(numChannels), nullptr);
        std::vector<float*> engineInputs(static_cast<size_t>(numChannels), nullptr);
        std::vector<const float*> engineOutputs(static_cast<size_t>(numChannels), nullptr);
        std::vector<float*> deviceOutputs(static_cast<size_t>(numChannels), nullptr);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch < numInputChannels && inputChannelData != nullptr)
                deviceInputs[static_cast<size_t>(ch)] = inputChannelData[ch];

            std::fill(engineIn[static_cast<size_t>(ch)].begin(), engineIn[static_cast<size_t>(ch)].end(), 0.0f);
            std::fill(engineOut[static_cast<size_t>(ch)].begin(), engineOut[static_cast<size_t>(ch)].end(), 0.0f);
            engineInputs[static_cast<size_t>(ch)] = engineIn[static_cast<size_t>(ch)].data();
            engineOutputs[static_cast<size_t>(ch)] = engineOut[static_cast<size_t>(ch)].data();

            if (ch < numOutputChannels && outputChannelData != nullptr)
                deviceOutputs[static_cast<size_t>(ch)] = outputChannelData[ch];
        }

        resamplerIn.process(deviceInputs.data(), numSamples, engineInputs.data(), config.blockSize);
        engineBuffer.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            engineBuffer.copyFrom(ch, 0, engineIn[static_cast<size_t>(ch)].data(), config.blockSize);

        if (graph)
        {
            graph->prepare();
            graph->process(engineBuffer);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            std::memcpy(engineOut[static_cast<size_t>(ch)].data(), engineBuffer.getReadPointer(ch), sizeof(float) * static_cast<size_t>(config.blockSize));

        resamplerOut.process(engineOutputs.data(), config.blockSize, deviceOutputs.data(), numSamples);
    }

    void DeviceEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
    {
        if (! device)
            return;

        deviceFormat.sampleRate = device->getCurrentSampleRate();
        deviceFormat.blockSize = device->getCurrentBufferSizeSamples();

        ensureBuffers(device->getActiveOutputChannels().countNumberOfSetBits(), config.blockSize);
    }

    void DeviceEngine::audioDeviceStopped()
    {
        engineBuffer.clear();
    }

    void DeviceEngine::ensureBuffers(int numChannels, int numSamples)
    {
        engineBuffer.setSize(numChannels, numSamples, false, false, true);
        engineIn.resize(static_cast<size_t>(numChannels));
        engineOut.resize(static_cast<size_t>(numChannels));
        for (int ch = 0; ch < numChannels; ++ch)
        {
            engineIn[static_cast<size_t>(ch)].resize(static_cast<size_t>(numSamples));
            engineOut[static_cast<size_t>(ch)].resize(static_cast<size_t>(numSamples));
        }
    }
}
