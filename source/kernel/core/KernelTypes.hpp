#pragma once

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace omniframe::kernel {

using NodeId = int32_t;
using ElementId = int32_t;
using MaterialId = int32_t;
using SectionId = int32_t;
using LoadCaseId = int32_t;
using LoadComboId = int32_t;

using Vec3 = Eigen::Vector3d;
using Mat3 = Eigen::Matrix3d;
using VecX = Eigen::VectorXd;
using MatX = Eigen::MatrixXd;
using SpMat = Eigen::SparseMatrix<double>;
using Triplet = Eigen::Triplet<double>;

inline constexpr double STANDARD_GRAVITY = 9.80665; // m/s^2

enum class DofType : std::uint8_t {
    UX = 0,
    UY = 1,
    UZ = 2,
    RX = 3,
    RY = 4,
    RZ = 5,
    Warping = 6,
    InternalBubble = 7
};

inline constexpr int NODAL_STD_DOFS = 6;
inline constexpr int NODAL_MAX_DOFS = 7; // 6 standard + optional warping

enum class ElementFamily : std::uint8_t {
    Frame1D = 0,
    Truss1D = 1,
    Shell2D = 2,
    Solid3D = 3,
    SpringLink = 4
};

enum class AnalysisKind : std::uint8_t {
    LinearStatic = 0,
    PDeltaStatic = 1,
    NonlinearStatic = 2,
    LinearBuckling = 3,
    Modal = 4,
    Harmonic = 5,
    TransientTimeHistory = 6,
    ResponseSpectrum = 7
};

enum class LinearSolverType : std::uint8_t {
    Auto = 0,
    SparseLU = 1,
    SimplicialLDLT = 2,
    SimplicialLLT = 3,
    ConjugateGradient = 4,
    BiCGSTAB = 5
};

enum class SparseOrderingType : std::uint8_t {
    Auto = 0,
    AMD = 1,
    COLAMD = 2,
    Natural = 3
};

enum class PreconditionerType : std::uint8_t {
    Auto = 0,
    Diagonal = 1,
    IncompleteCholesky = 2,
    IncompleteLUT = 3,
    None = 4
};

struct LinearSolverOptions {
    LinearSolverType type = LinearSolverType::Auto;
    SparseOrderingType ordering = SparseOrderingType::Auto;
    PreconditionerType preconditioner = PreconditionerType::Auto;
    int maxIterations = 5000;
    double tolerance = 1e-10;
    bool iterativeRefinement = true;
    int refinementSteps = 2;
    double pivotZeroTolerance = 1e-12;
    bool checkSymmetry = true;
    bool allowFallback = true;
};

struct LinearSolverStats {
    LinearSolverType solverUsed = LinearSolverType::SparseLU;
    bool usedIterative = false;
    bool matrixSymmetric = false;
    int totalDofs = 0;
    int activeDofs = 0;
    int restrainedDofs = 0;
    int inactiveDofs = 0;
    int mpcEliminatedDofs = 0;
    int nnz = 0;
    int iterations = 0;
    double residualNorm = 0.0;
    double relativeResidual = 0.0;
    double minAbsPivot = 0.0;
    double maxAbsDiag = 0.0;
    double conditionProxy = 1.0;
    double factorTimeMs = 0.0;
    double solveTimeMs = 0.0;
    bool reusedFactorization = false;
    bool success = false;
    std::string errorCode;
    std::string message;
};

struct KernelDiagnostic {
    bool isError = false;
    std::string code;
    std::string message;
};

} // namespace omniframe::kernel
