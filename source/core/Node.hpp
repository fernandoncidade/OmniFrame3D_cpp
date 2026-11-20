#pragma once

#include "Types.hpp"
#include <array>

namespace omniframe {

class Node {
public:
    NodeId id = INVALID_ID;
    Vec3 position{0.0, 0.0, 0.0}; // metros

    SupportRestraint support;
    NodalLoad load;

    // Resultados calculados pelo solver
    std::array<double, 6> displacement{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // uX, uY, uZ (m), rotX, rotY, rotZ (rad)
    std::array<double, 6> reaction{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};     // Rx, Ry, Rz (kN), Mx, My, Mz (kNm)

    Node() = default;

    Node(NodeId nid, double px, double py, double pz)
        : id(nid), position(px, py, pz) {}

    Node(NodeId nid, const Vec3& pos)
        : id(nid), position(pos) {}

    double x() const { return position.x; }
    double y() const { return position.y; }
    double z() const { return position.z; }

    bool isSupported() const {
        return support.isRestrained();
    }

    double displacementNorm() const {
        return std::sqrt(displacement[0] * displacement[0] +
                         displacement[1] * displacement[1] +
                         displacement[2] * displacement[2]);
    }

    void clearResults() {
        displacement.fill(0.0);
        reaction.fill(0.0);
    }
};

} // namespace omniframe

