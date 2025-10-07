#include "graph/Nodes/GainNode.h"

#include <algorithm>

namespace host::graph::nodes
{
void GainNode::prepare(double sampleRate, int blockSize)
{
    (void) sampleRate;
    (void) blockSize;
}

void GainNode::process(ProcessCtx& ctx)
{
    const float gain = gain_.load();
    const int frames = std::max(0, ctx.numFrames);

    for (int ch = 0; ch < ctx.outCh; ++ch)
    {
        float* channel = (ctx.out != nullptr) ? ctx.out[ch] : nullptr;
        if (channel == nullptr)
            continue;

        for (int i = 0; i < frames; ++i)
            channel[i] *= gain;
    }
}
} // namespace host::graph::nodes
