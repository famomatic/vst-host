#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <mutex>
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
        // Mirrors host::persist::EngineSettings::resamplerQuality.
        // 0=Linear 1=CatmullRom 2=Lagrange 3=WindowedSinc
        int resamplerQuality { 2 };
        bool pdcEnabled { true };
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
        [[nodiscard]] EngineConfig getEngineConfig() const;

        void setDeviceInfo(const DeviceInfo& info);
        [[nodiscard]] DeviceInfo getDeviceInfo() const;

        void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                              int numInputChannels,
                                              float* const* outputChannelData,
                                              int numOutputChannels,
                                              int numSamples,
                                              const juce::AudioIODeviceCallbackContext& context) override;

        void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
        void audioDeviceStopped() override;

    private:
        struct ProcessingState;

        [[nodiscard]] std::shared_ptr<ProcessingState> buildProcessingState(const EngineConfig& cfg,
                                                                            const DeviceInfo& info) const;
        void rebuildProcessingState(const EngineConfig& cfg, const DeviceInfo& info);
        void clearOutputs(float* const* outputChannelData, int numOutputChannels, int numSamples);

        std::atomic<std::shared_ptr<host::graph::GraphEngine>> graphEngine;
        mutable std::mutex configMutex_;
        EngineConfig engineConfig;
        DeviceInfo deviceInfo;

        std::atomic<std::shared_ptr<ProcessingState>> processingState_;
    };
}
