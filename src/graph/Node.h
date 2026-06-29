#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <vector>

namespace host::graph
{
class ParameterQueue;

/// A single exposed parameter on a graph node, in normalized form so it can be
/// serialized, automated, and rendered in a generic UI without each node
/// inventing its own persistence scheme.
struct NodeParameter
{
    std::string id;          ///< Stable identifier used in presets/projects
    std::string displayName;  ///< Human-readable label
    double value { 0.0 };     ///< Current value in the range [min, max]
    double min { 0.0 };
    double max { 1.0 };
    double defaultValue { 0.0 };
    bool automatable { false };
};

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
    // ASIO/WASAPI host timestamp in nanoseconds, when provided by the device
    // callback. nullptr when the device does not supply one. Forwarded to VST
    // plug-ins so time-aware effects (delays, sync) stay sample-accurate.
    const std::uint64_t* hostTimeNs = nullptr;
};

class Node
{
public:
    virtual ~Node() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(ProcessContext& context) = 0;
    virtual int latencySamples() const { return 0; }
    virtual std::string name() const = 0;

    // Audio channel configuration. Returning a value <= 0 means the node is
    // channel-agnostic and will be fed the host bus width. Nodes with a fixed
    // bus layout (e.g. VST plug-ins) report their actual bus channel counts.
    virtual int inputChannelCount() const { return 0; }
    virtual int outputChannelCount() const { return 0; }

    /// Node type tag used for factory instantiation and persistence. Stable
    /// across versions; changing it breaks saved projects.
    virtual std::string typeId() const { return name(); }

    /// Exposed parameters in normalized form. Default: none. Effect nodes
    /// override this so their settings survive project save/load and presets.
    virtual std::vector<NodeParameter> getParameters() const { return {}; }

    /// Apply parameters coming from a loaded project/preset. Unknown ids are
    /// ignored so old projects load against newer nodes gracefully.
    virtual void setParameters(const std::vector<NodeParameter>& parameters) { juce::ignoreUnused(parameters); }

    /// Message-thread-safe request to change a single parameter. Queues the
    /// change so the audio thread can apply it at the next block boundary -
    /// this is what makes macro-driven edits glitch-free. Default impl writes
    /// straight through to setParameters for nodes that don't opt into ramping.
    virtual void requestParameterChange(const std::string& id, double value);

    /// Called by the audio thread at the top of process() to drain pending
    /// parameter changes. Effect nodes override this to pull values from the
    /// queue into smoothed targets. Default: no-op.
    virtual void applyParameterChanges() {}
};
} // namespace host::graph
