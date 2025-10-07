#pragma once

#include "graph/Node.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace host::graph
{
class GraphEngine
{
public:
    void setGraph(std::vector<std::unique_ptr<Node>> nodes,
                  std::vector<std::pair<int, int>> edges);

    void prepare(double sampleRate, int blockSize);
    void process(float** in, int inCh, int inFrames,
                 float** out, int outCh, int& outFrames,
                 double sampleRate, int blockSize);

private:
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<std::pair<int, int>> edges_;
    std::vector<Node*> schedule_;

    struct EdgeDelay
    {
        int samples = 0;
        std::vector<float> bufL;
        std::vector<float> bufR;
        size_t wp = 0;
    };

    std::vector<EdgeDelay> delays_;

    std::array<std::vector<float>, 2> scratch_;

    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    bool prepared_ = false;
};
} // namespace host::graph
