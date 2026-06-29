#include "graph/Node.h"

namespace host::graph
{
void Node::requestParameterChange(const std::string& id, double value)
{
    // Default: apply synchronously. Effect nodes that need glitch-free
    // smoothing override requestParameterChange to enqueue into a
    // ParameterQueue and applyParameterChanges to drain it on the audio
    // thread. The synchronous fallback keeps simple nodes working without
    // any queue machinery.
    auto params = getParameters();
    bool found = false;
    for (auto& p : params)
    {
        if (p.id == id)
        {
            p.value = value;
            found = true;
            break;
        }
    }
    if (found)
        setParameters(params);
}
} // namespace host::graph