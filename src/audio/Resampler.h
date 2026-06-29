#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace host::audio
{
    /// Quality presets for the device-engine resampler. Index matches the
    /// persisted EngineSettings::resamplerQuality value.
    enum class ResamplerQuality
    {
        linear = 0,
        catmullRom = 1,
        lagrange = 2,
        windowedSinc = 3
    };

    [[nodiscard]] inline ResamplerQuality resamplerQualityFromIndex(int index) noexcept
    {
        switch (index)
        {
            case 0: return ResamplerQuality::linear;
            case 1: return ResamplerQuality::catmullRom;
            case 3: return ResamplerQuality::windowedSinc;
            case 2:
            default: return ResamplerQuality::lagrange;
        }
    }

    /// Type-erased single-channel resampler. Each JUCE interpolator is a
    /// distinct concrete type (GenericInterpolator<Traits, N>), so we erase
    /// behind this interface to let DeviceEngine swap quality at runtime.
    class IResamplerChannel
    {
    public:
        virtual ~IResamplerChannel() = default;
        virtual void prepare(double speedRatio, int bufferCapacity, int safetyMargin = 8) = 0;
        virtual void reset() noexcept = 0;
        virtual void push(const float* samples, int numSamples) = 0;
        virtual void pushSilence(int numSamples) = 0;
        [[nodiscard]] virtual bool canProcess(int numOutputSamples) const noexcept = 0;
        virtual int process(float* output, int numOutputSamples) noexcept = 0;
        [[nodiscard]] virtual int getStoredSamples() const noexcept = 0;
    };

    /// Concrete single-channel resampler built on a JUCE GenericInterpolator.
    /// The FIFO bookkeeping and realtime-safe overflow handling live here so
    /// every quality preset shares identical streaming semantics - only the
    /// interpolation kernel differs.
    template <typename Interpolator>
    class ResamplerChannel final : public IResamplerChannel
    {
    public:
        void prepare(double speedRatio, int bufferCapacity, int safetyMargin = 8) override
        {
            ratio = speedRatio > 0.0 ? speedRatio : 1.0;
            margin = std::max(safetyMargin, 4);
            buffer.resize(static_cast<size_t>(bufferCapacity));
            stored = 0;
            interpolator.reset();
        }

        void reset() noexcept override
        {
            stored = 0;
            interpolator.reset();
        }

        void push(const float* samples, int numSamples) override
        {
            if (numSamples <= 0)
                return;

            auto* dest = ensureSpace(numSamples);
            std::memcpy(dest, samples, static_cast<size_t>(numSamples) * sizeof(float));
            stored += numSamples;
        }

        void pushSilence(int numSamples) override
        {
            if (numSamples <= 0)
                return;
            std::fill_n(ensureSpace(numSamples), static_cast<size_t>(numSamples), 0.0f);
            stored += numSamples;
        }

        [[nodiscard]] bool canProcess(int numOutputSamples) const noexcept override
        {
            if (numOutputSamples <= 0)
                return false;

            const double required = numOutputSamples * ratio;
            const int needed = static_cast<int>(std::ceil(required)) + margin;
            return stored >= needed;
        }

        int process(float* output, int numOutputSamples) noexcept override
        {
            if (output == nullptr || numOutputSamples <= 0)
                return 0;

            if (stored <= 0)
            {
                std::fill_n(output, static_cast<size_t>(numOutputSamples), 0.0f);
                return 0;
            }

            const auto available = stored;
            const double required = numOutputSamples * ratio;
            const int needed = static_cast<int>(std::ceil(required)) + margin;

            const int maxOutputs = (ratio > 0.0)
                ? static_cast<int>(std::floor(std::max(0.0, static_cast<double>(available - margin)) / ratio))
                : available;

            const int outputsToProduce = (available >= needed)
                ? numOutputSamples
                : std::clamp(maxOutputs, 0, numOutputSamples);

            if (outputsToProduce <= 0)
            {
                std::fill_n(output, static_cast<size_t>(numOutputSamples), 0.0f);
                return 0;
            }

            const int consumed = interpolator.process(ratio, buffer.data(), output, outputsToProduce);
            consume(consumed);

            if (outputsToProduce < numOutputSamples)
                std::fill(output + outputsToProduce, output + numOutputSamples, 0.0f);

            return outputsToProduce;
        }

        [[nodiscard]] int getStoredSamples() const noexcept override { return stored; }

    private:
        float* ensureSpace(int additional)
        {
            // Realtime-safe: capacity is pre-allocated in prepare(). If a push
            // would overflow, drop the oldest samples via memmove only - never
            // reallocate from the audio thread.
            const int capacity = static_cast<int>(buffer.size());
            if (stored + additional > capacity)
            {
                const int excess = (stored + additional) - capacity;
                if (excess >= stored)
                {
                    stored = 0;
                }
                else
                {
                    std::memmove(buffer.data(),
                                 buffer.data() + excess,
                                 static_cast<size_t>(stored - excess) * sizeof(float));
                    stored -= excess;
                }
            }

            return buffer.data() + stored;
        }

        void consume(int numSamples)
        {
            if (numSamples <= 0)
                return;

            stored = std::max(0, stored - numSamples);
            if (stored > 0)
                std::memmove(buffer.data(), buffer.data() + numSamples, static_cast<size_t>(stored) * sizeof(float));
        }

        double ratio { 1.0 };
        int margin { 8 };
        std::vector<float> buffer;
        int stored { 0 };
        Interpolator interpolator;
    };

    /// Factory: build a single-channel resampler for the requested quality.
    std::unique_ptr<IResamplerChannel> createResamplerChannel(ResamplerQuality quality);

    /** Multi-channel wrapper around the single-channel resampler. */
    class BlockResampler
    {
    public:
        BlockResampler() = default;

        void prepare(int numChannels,
                     double speedRatio,
                     int maxInputChunk,
                     int maxOutputChunk,
                     ResamplerQuality quality,
                     int safetyMargin = 8)
        {
            channels.resize(static_cast<size_t>(std::max(0, numChannels)));
            ratio = speedRatio > 0.0 ? speedRatio : 1.0;
            margin = std::max(safetyMargin, 4);
            maxInput = std::max(maxInputChunk, 1);
            maxOutput = std::max(maxOutputChunk, 1);
            discardBuffer.resize(static_cast<size_t>(maxOutput));

            const int capacity = computeCapacity();
            for (auto& ch : channels)
            {
                ch = createResamplerChannel(quality);
                ch->prepare(ratio, capacity, margin);
            }
        }

        void reset()
        {
            for (auto& ch : channels)
                ch->reset();
        }

        void push(const float* const* inputs, int numSamples)
        {
            if (channels.empty() || numSamples <= 0)
                return;

            for (size_t i = 0; i < channels.size(); ++i)
            {
                const auto* source = (inputs != nullptr) ? inputs[i] : nullptr;
                if (source != nullptr)
                    channels[i]->push(source, numSamples);
                else
                    channels[i]->pushSilence(numSamples);
            }
        }

        [[nodiscard]] bool canProcess(int numOutputSamples) const
        {
            if (channels.empty())
                return false;

            for (const auto& ch : channels)
            {
                if (! ch->canProcess(numOutputSamples))
                    return false;
            }
            return true;
        }

        int process(float* const* outputs, int numOutputSamples)
        {
            if (channels.empty() || outputs == nullptr)
                return 0;

            int produced = numOutputSamples;
            for (size_t i = 0; i < channels.size(); ++i)
            {
                auto* dest = outputs[i] != nullptr ? outputs[i] : discardBuffer.data();
                const int channelProduced = channels[i]->process(dest, numOutputSamples);
                produced = std::min(produced, channelProduced);

                if (outputs[i] == nullptr && channelProduced > 0)
                    std::fill(discardBuffer.begin(), discardBuffer.begin() + channelProduced, 0.0f);
            }

            return produced;
        }

        [[nodiscard]] int getNumChannels() const noexcept { return static_cast<int>(channels.size()); }

    private:
        [[nodiscard]] int computeCapacity() const
        {
            const double requiredForOutput = std::ceil(maxOutput * ratio) + margin + 8.0;
            const int base = static_cast<int>(std::max({ requiredForOutput, static_cast<double>(maxInput), static_cast<double>(maxOutput) }));
            return std::max(base * 2, maxInput * 4);
        }

        std::vector<std::unique_ptr<IResamplerChannel>> channels;
        double ratio { 1.0 };
        int margin { 8 };
        int maxInput { 0 };
        int maxOutput { 0 };
        std::vector<float> discardBuffer;
    };
}
