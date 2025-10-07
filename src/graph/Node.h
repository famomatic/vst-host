#pragma once

#include <cstddef>

namespace host::graph
{
struct ProcessCtx
{
    float** in = nullptr;
    int inCh = 0;
    float** out = nullptr;
    int outCh = 0;
    int numFrames = 0;
    double sampleRate = 0.0;
    int blockSize = 0;
};

class Node
{
public:
    virtual ~Node() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(ProcessCtx& ctx) = 0;
    virtual int latencySamples() const { return 0; }
};
} // namespace host::graph
