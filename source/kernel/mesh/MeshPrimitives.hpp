#pragma once

#include "../core/KernelTypes.hpp"
#include <string>
#include <vector>

namespace omniframe::kernel {

struct ElementGroup {
    int32_t id = 0;
    std::string name;
    std::vector<ElementId> elementIds;
};

struct NodeSet {
    int32_t id = 0;
    std::string name;
    std::vector<NodeId> nodeIds;
};

} // namespace omniframe::kernel
