#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>
#include <vector>

#include "audio/Resampler.h"
#include "graph/GraphEngine.h"

namespace host::audio
{
    struct EngineConfig
    {
        double sampleRate { 48000.0 };
        int blockSize { 256 };
    };

    struct DeviceInfo
    {
        double sampleRate { 0.0 };
        int blockSize { 0 };
        int inputChannels { 0 };
        int outputChannels { 0 };
    };

    class DeviceEngine : public juce::AudioIODeviceCallback
    {
    public:
        DeviceEngine();
        ~DeviceEngine() override = default;

        void setGraph(std::shared_ptr<host::graph::GraphEngine> graphEngine);

        void setEngineConfig(const EngineConfig& cfg);
        [[nodiscard]] EngineConfig getEngineConfig() const noexcept { return engineConfig; }

        void setDeviceInfo(const DeviceInfo& info);
        [[nodiscard]] DeviceInfo getDeviceInfo() const noexcept { return deviceInfo; }

        void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                              int numInputChannels,
                                              float* const* outputChannelData,
                                              int numOutputChannels,
                                              int numSamples,
                                              const juce::AudioIODeviceCallbackContext& context) override;

        void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
        void audioDeviceStopped() override;

    private:
        void prepareResamplers();
        void prepareScratchBuffers(int numChannels);
        void clearOutputs(float* const* outputChannelData, int numOutputChannels, int numSamples);

        std::atomic<std::shared_ptr<host::graph::GraphEngine>> graphEngine;
        EngineConfig engineConfig;
        DeviceInfo deviceInfo;

        juce::AudioBuffer<float> engineBuffer;

        BlockResampler inputResampler;
        BlockResampler outputResampler;

        std::vector<const float*> inputPointerScratch;
        std::vector<float*> outputPointerScratch;
        std::vector<float*> engineWritePointers;
        std::vector<const float*> engineReadPointers;
    };
}
