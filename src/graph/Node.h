#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>

namespace host::graph
{
struct ProcessContext
{
    juce::AudioBuffer<float>& audioBuffer;
    float** inputChannels = nullptr;
    float** outputChannels = nullptr;
    int numInputChannels = 0;
    int numOutputChannels = 0;
    double sampleRate = 0.0;
    int blockSize = 0;
    int numFrames = 0;
};

class Node
{
public:
    virtual ~Node() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(ProcessContext& context) = 0;
    virtual int latencySamples() const { return 0; }
    virtual std::string name() const = 0;
};
} // namespace host::graph
