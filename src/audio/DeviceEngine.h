#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

#include "audio/Resampler.h"
#include "graph/GraphEngine.h"

namespace host::audio
{
    struct EngineConfig
    {
        double sampleRate { 48000.0 };
        int blockSize { 256 };
    };

    class DeviceEngine : public juce::AudioIODeviceCallback
    {
    public:
        explicit DeviceEngine(std::shared_ptr<host::graph::GraphEngine> graphEngine);
        ~DeviceEngine() override = default;

        void setConfig(const EngineConfig& cfg);
        EngineConfig getConfig() const noexcept { return config; }

        void audioDeviceIOCallback(const float* const* inputChannelData,
                                   int numInputChannels,
                                   float* const* outputChannelData,
                                   int numOutputChannels,
                                   int numSamples) override;

        void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
        void audioDeviceStopped() override;

    private:
        void ensureBuffers(int numChannels, int numSamples);

        std::shared_ptr<host::graph::GraphEngine> graph;
        EngineConfig config;
        EngineConfig deviceFormat;
        std::vector<std::vector<float>> engineIn;
        std::vector<std::vector<float>> engineOut;
        juce::AudioBuffer<float> engineBuffer;
        Resampler resamplerIn;
        Resampler resamplerOut;
    };
}
