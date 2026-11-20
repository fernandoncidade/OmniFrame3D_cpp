#pragma once

#include "core/StructuralModel.hpp"
#include "kernel/analysis/BucklingSolver.hpp"
#include "kernel/analysis/DynamicSolver.hpp"
#include "kernel/analysis/LoadSystem.hpp"
#include "kernel/analysis/NonlinearSolver.hpp"
#include "kernel/core/KernelTypes.hpp"
#include <Eigen/Sparse>
#include <array>
#include <functional>
#include <string>
#include <unordered_map>
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

struct ContinuumElementResult {
    ElementId elementId = INVALID_ID;
    bool isShell = false;
    bool isSolid = false;
    std::string cellTypeName;
    double vonMisesStress = 0.0;
    double maxPrincipalStress = 0.0;
    double minPrincipalStress = 0.0;
    std::array<double, 3> membraneResultants{0.0, 0.0, 0.0}; // Nxx, Nyy, Nxy
    std::array<double, 3> bendingResultants{0.0, 0.0, 0.0};  // Mxx, Myy, Mxy
    std::array<double, 2> transverseShear{0.0, 0.0};         // Qxz, Qyz
    std::array<double, 6> stressVoigt{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

struct SolverResult {
    bool success = false;
    std::string errorMessage;
    int totalNodes = 0;
    int totalElements = 0;
    int totalSurfaceNodes = 0;
    int totalSurfaceElements = 0;
    int totalDofs = 0;
    int restrainedDofs = 0;
    double executionTimeMs = 0.0;

    EquilibriumCheck equilibrium;
    std::vector<ElementExtremes> elementExtremes;
    std::vector<ContinuumElementResult> continuumElementResults;
    double maxNodalDisplacement = 0.0;
    double maxShellVonMises = 0.0;
    double maxSolidVonMises = 0.0;
    double totalStrainEnergy = 0.0;
    kernel::LinearSolverStats solverStats;
};

class FemSolver3D {
public:
    static SolverResult solve(StructuralModel& model);
    static SolverResult solveWithOptions(StructuralModel& model, const kernel::LinearSolverOptions& opts);
    static bool solveLoadCasesAndCombinations(
        StructuralModel& model,
        const kernel::LinearSolverOptions& opts,
        std::vector<kernel::CaseDisplacementResult>& outCaseResults,
        std::vector<kernel::CaseDisplacementResult>& outComboResults,
        kernel::EnvelopeExtremes& outEnvelope,
        std::string* errorMsg = nullptr);

    static kernel::BucklingResult solveBuckling(
        StructuralModel& model,
        const kernel::BucklingOptions& opts = {});

    static kernel::NonlinearResult solveNonlinear(
        StructuralModel& model,
        const kernel::NonlinearOptions& opts = {});

    static kernel::ModalResult solveModal(
        const StructuralModel& model,
        const kernel::ModalOptions& opts = {});

    static kernel::HarmonicResult solveHarmonic(
        const StructuralModel& model,
        const kernel::HarmonicOptions& opts = {});

    static kernel::ResponseSpectrumResult solveResponseSpectrum(
        const StructuralModel& model,
        const kernel::ResponseSpectrumOptions& specOpts,
        const kernel::ModalOptions& modalOpts = {});

    static kernel::TransientResult solveSeismic(
        const StructuralModel& model,
        const std::function<kernel::Vec3(double timeSec, int stepIdx)>& groundAccelMS2Fn,
        const kernel::TransientOptions& opts = {});

    static kernel::TransientResult solveTransient(
        const StructuralModel& model,
        const kernel::LoadHistory& history,
        const kernel::TransientOptions& opts = {});

    static kernel::TransientResult solveTransient(
        const StructuralModel& model,
        const std::function<double(double timeSec, int stepIdx)>& loadScaleFn,
        kernel::LoadCaseId baseLoadCaseId = 0,
        const kernel::TransientOptions& opts = {});

    struct NodalVectorField {
        std::unordered_map<NodeId, Vec3> frameNodes;
        std::unordered_map<NodeId, Vec3> meshNodes;
        double maxTranslationalNorm = 0.0;
    };

    static NodalVectorField extractNodalDisplacements(
        const StructuralModel& model,
        const kernel::VecX& uFull);

    static bool applyBucklingImperfection(
        StructuralModel& model,
        const kernel::BucklingResult& buckling,
        int modeIndex,
        double amplitude,
        std::string* errorMsg = nullptr);
};

} // namespace omniframe

