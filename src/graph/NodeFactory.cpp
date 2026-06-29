#include "graph/NodeFactory.h"

#include "graph/Nodes/AudioIn.h"
#include "graph/Nodes/AudioOut.h"
#include "graph/Nodes/GainNode.h"
#include "graph/Nodes/EqualizerNode.h"
#include "graph/Nodes/CompressorNode.h"
#include "graph/Nodes/ReverbNode.h"
#include "graph/Nodes/DelayNode.h"
#include "graph/Nodes/Merge.h"
#include "graph/Nodes/Mix.h"
#include "graph/Nodes/Split.h"

#include <algorithm>
#include <cctype>

namespace host::graph
{
namespace
{
    std::string normalise(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), s.end());
        return s;
    }

    const std::vector<NodeDescriptor>& descriptors()
    {
        static const std::vector<NodeDescriptor> list {
            { "AudioIn",  "Audio In",  "Routing", 0, 2 },
            { "AudioOut", "Audio Out", "Routing", 2, 0 },
            { "Gain",     "Gain",      "Effects", 2, 2 },
            { "Equalizer","Equalizer", "Effects", 2, 2 },
            { "Compressor","Compressor","Effects", 2, 2 },
            { "Reverb",   "Reverb",    "Effects", 2, 2 },
            { "Delay",    "Delay",     "Effects", 2, 2 },
            { "Mix",      "Mix",       "Routing", 2, 2 },
            { "Split",    "Split",     "Routing", 2, 2 },
            { "Merge",    "Merge",     "Routing", 2, 2 },
        };
        return list;
    }
}

const std::vector<NodeDescriptor>& NodeFactory::getDescriptors()
{
    return descriptors();
}

std::unique_ptr<Node> NodeFactory::create(const std::string& typeId)
{
    const auto n = normalise(typeId);

    if (n == "audioin" || n == "audioinnode")
        return std::make_unique<nodes::AudioInNode>();
    if (n == "audioout" || n == "audiooutnode")
        return std::make_unique<nodes::AudioOutNode>();
    if (n == "gain")
        return std::make_unique<nodes::GainNode>();
    if (n == "equalizer" || n == "eq")
        return std::make_unique<nodes::EqualizerNode>();
    if (n == "compressor" || n == "comp")
        return std::make_unique<nodes::CompressorNode>();
    if (n == "reverb")
        return std::make_unique<nodes::ReverbNode>();
    if (n == "delay")
        return std::make_unique<nodes::DelayNode>();
    if (n == "mix")
        return std::make_unique<nodes::MixNode>();
    if (n == "split")
        return std::make_unique<nodes::SplitNode>();
    if (n == "merge")
        return std::make_unique<nodes::MergeNode>();

    return nullptr;
}

std::unique_ptr<Node> NodeFactory::createFromPersistedName(const std::string& name)
{
    if (name.empty())
        return nullptr;

    // Exact typeId match first.
    if (auto node = create(name))
        return node;

    // Fall back to display-name matching for older project files.
    for (const auto& d : descriptors())
    {
        if (normalise(d.displayName) == normalise(name))
            return create(d.typeId);
    }

    return nullptr;
}
} // namespace host::graph