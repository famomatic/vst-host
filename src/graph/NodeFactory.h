#pragma once

#include "graph/Node.h"

#include <memory>
#include <string>
#include <vector>

namespace host::graph
{
/// Factory and registry for built-in (non-VST) graph nodes. Keeping node
/// creation in one place means adding an effect only touches this file plus
/// the node itself - Project (de)serialization and the add-node UI both go
/// through here.
struct NodeDescriptor
{
    std::string typeId;          ///< Stable id used in saved projects
    std::string displayName;      ///< Label shown in the add-node menu
    std::string category;         ///< Grouping: "Routing", "Effects", ...
    int defaultInputs { 2 };
    int defaultOutputs { 2 };
};

class NodeFactory
{
public:
    /// All built-in node types, for the add-node menu and project validation.
    [[nodiscard]] static const std::vector<NodeDescriptor>& getDescriptors();

    /// Create a built-in node by typeId. Returns nullptr for unknown types
    /// (including "VstFx", which is handled by the project loader, not here).
    [[nodiscard]] static std::unique_ptr<Node> create(const std::string& typeId);

    /// Create a built-in node from a persisted type string that may be the
    /// display name or the typeId, normalised for matching. Returns nullptr
    /// for unknown types.
    [[nodiscard]] static std::unique_ptr<Node> createFromPersistedName(const std::string& name);
};
} // namespace host::graph