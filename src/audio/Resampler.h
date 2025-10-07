#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace host::audio
{
    /**
        Streaming resampler for a single channel.

        The resampler maintains an internal FIFO so that callers can push
        arbitrary-sized blocks and pull blocks of a different size/sample-rate
        without performing any allocations during the audio callback.
    */
    class ChannelResampler
    {
    public:
        ChannelResampler() = default;

        void prepare(double speedRatio, int bufferCapacity, int safetyMargin = 8)
        {
            ratio = speedRatio > 0.0 ? speedRatio : 1.0;
            margin = std::max(safetyMargin, 4);
            buffer.resize(static_cast<size_t>(bufferCapacity));
            stored = 0;
            interpolator.reset();
        }

        void reset()
        {
            stored = 0;
            interpolator.reset();
        }

        void push(const float* samples, int numSamples)
        {
            if (numSamples <= 0)
                return;

            if (samples == nullptr)
            {
                std::fill_n(ensureSpace(numSamples), static_cast<size_t>(numSamples), 0.0f);
                stored += numSamples;
                return;
            }

            auto* dest = ensureSpace(numSamples);
            std::memcpy(dest, samples, static_cast<size_t>(numSamples) * sizeof(float));
            stored += numSamples;
        }

        [[nodiscard]] bool canProcess(int numOutputSamples) const noexcept
        {
            if (numOutputSamples <= 0)
                return false;

            const double required = numOutputSamples * ratio;
            const int needed = static_cast<int>(std::ceil(required)) + margin;
            return stored >= needed;
        }

        int process(float* output, int numOutputSamples)
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

        [[nodiscard]] int getStoredSamples() const noexcept { return stored; }

    private:
        float* ensureSpace(int additional)
        {
            const int capacity = static_cast<int>(buffer.size());
            if (stored + additional <= capacity)
                return buffer.data() + stored;

            const int required = stored + additional;
            if (required > capacity)
            {
                // If we run out of space, drop the oldest samples to make room.
                const int excess = required - capacity;
                if (excess >= stored)
                {
                    stored = 0;
                }
                else
                {
                    std::memmove(buffer.data(), buffer.data() + excess, static_cast<size_t>(stored - excess) * sizeof(float));
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
        juce::LagrangeInterpolator interpolator;
    };

    /** Multi-channel wrapper around ChannelResampler. */
    class BlockResampler
    {
    public:
        BlockResampler() = default;

        void prepare(int numChannels,
                     double speedRatio,
                     int maxInputChunk,
                     int maxOutputChunk,
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
                ch.prepare(ratio, capacity, margin);
        }

        void reset()
        {
            for (auto& ch : channels)
                ch.reset();
        }

        void push(const float* const* inputs, int numSamples)
        {
            if (channels.empty() || numSamples <= 0)
                return;

            for (size_t i = 0; i < channels.size(); ++i)
            {
                const auto* source = (inputs != nullptr) ? inputs[i] : nullptr;
                channels[i].push(source, numSamples);
            }
        }

        [[nodiscard]] bool canProcess(int numOutputSamples) const
        {
            if (channels.empty())
                return false;

            for (const auto& ch : channels)
            {
                if (! ch.canProcess(numOutputSamples))
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
                const int channelProduced = channels[i].process(dest, numOutputSamples);
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

        std::vector<ChannelResampler> channels;
        double ratio { 1.0 };
        int margin { 8 };
        int maxInput { 0 };
        int maxOutput { 0 };
        std::vector<float> discardBuffer;
    };
}
