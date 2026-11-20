#pragma once

#include "core/StructuralModel.hpp"
#include <Eigen/Sparse>
#include <string>
#include <vector>
#include <chrono>

namespace omniframe {

struct EquilibriumCheck {
    Vec3 sumAppliedForces{0.0, 0.0, 0.0};
    Vec3 sumReactions{0.0, 0.0, 0.0};
    Vec3 sumAppliedMoments{0.0, 0.0, 0.0};
    Vec3 sumReactionMoments{0.0, 0.0, 0.0};
    Vec3 residualForces{0.0, 0.0, 0.0};
    Vec3 residualMoments{0.0, 0.0, 0.0};
    bool isBalanced = true;
};

struct ElementExtremes {
    ElementId elementId = INVALID_ID;
    double N_min = 0.0, N_max = 0.0;
    double Vy_min = 0.0, Vy_max = 0.0;
    double Vz_min = 0.0, Vz_max = 0.0;
    double Tx_min = 0.0, Tx_max = 0.0;
    double My_min = 0.0, My_max = 0.0;
    double Mz_min = 0.0, Mz_max = 0.0;
    double maxDeflection = 0.0;
};

struct SolverResult {
    bool success = false;
    std::string errorMessage;
    int totalNodes = 0;
    int totalElements = 0;
    int totalDofs = 0;
    int restrainedDofs = 0;
    double executionTimeMs = 0.0;

    EquilibriumCheck equilibrium;
    std::vector<ElementExtremes> elementExtremes;
    double maxNodalDisplacement = 0.0;
};

class FemSolver3D {
public:
    static SolverResult solve(StructuralModel& model);
};

} // namespace omniframe

