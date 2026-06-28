#include "graph/Nodes/GainNode.h"

#include <algorithm>

namespace host::graph::nodes
{
void GainNode::prepare(double sampleRate, int blockSize)
{
    (void) sampleRate;
    (void) blockSize;
}

void GainNode::process(ProcessContext& ctx)
{
    const float gain = gain_.load();
    const int frames = std::max(0, ctx.numFrames);
    const int inputs = std::max(0, ctx.numInputChannels);
    const int outputs = std::max(0, ctx.numOutputChannels);

    if (frames == 0 || outputs == 0 || ctx.outputChannels == nullptr)
        return;

    for (int outCh = 0; outCh < outputs; ++outCh)
    {
       float* dest = ctx.outputChannels[outCh];
        if (dest == nullptr)
            continue;

        // Copy input into output first, since the engine now keeps in/out
        // buffers separate.
        if (inputs > 0 && ctx.inputChannels != nullptr)
        {
            const int srcCh = outCh % inputs;
            const float* src = ctx.inputChannels[srcCh];
            if (src != nullptr)
                juce::FloatVectorOperations::copy(dest, src, frames);
            else
                juce::FloatVectorOperations::clear(dest, frames);
        }
        else
        {
            juce::FloatVectorOperations::clear(dest, frames);
        }

        juce::FloatVectorOperations::multiply(dest, gain, frames);
    }
}
} // namespace host::graph::nodes
