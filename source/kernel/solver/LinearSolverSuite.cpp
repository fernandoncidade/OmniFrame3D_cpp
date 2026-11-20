#include "LinearSolverSuite.hpp"
#include <Eigen/SparseCholesky>
#include <Eigen/SparseLU>
#include <Eigen/IterativeLinearSolvers>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace omniframe::kernel {

namespace {

std::uint64_t computeMatrixFingerprint(const SpMat& A, bool valuesToo) {
    std::uint64_t h = 14695981039346656037ULL;
    auto mix = [&](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(A.rows()));
    mix(static_cast<std::uint64_t>(A.cols()));
    mix(static_cast<std::uint64_t>(A.nonZeros()));
    for (int k = 0; k < A.outerSize(); ++k) {
        mix(static_cast<std::uint64_t>(A.outerIndexPtr()[k]));
        for (SpMat::InnerIterator it(A, k); it; ++it) {
            mix(static_cast<std::uint64_t>(it.row()));
            if (valuesToo) {
                union { double d; std::uint64_t u; } conv{it.value()};
                mix(conv.u);
            }
        }
    }
    return h;
}

template <typename DirectSolver>
void runIterativeRefinement(const SpMat& A, const VecX& b, DirectSolver& solver, VecX& x, int steps) {
    for (int s = 0; s < steps; ++s) {
        VecX r = b - A * x;
        const double bNorm = std::max(b.norm(), 1.0);
        if (r.norm() / bNorm < 1e-13) break;
        VecX dx = solver.solve(r);
        if (solver.info() != Eigen::Success || !dx.allFinite()) break;
        x += dx;
    }
}

void populatePivotDiagnosticsFromMatrix(const SpMat& A, LinearSolverStats& stats) {
    double maxDiag = 0.0;
    double minNonZeroDiag = std::numeric_limits<double>::max();
    for (int i = 0; i < A.rows(); ++i) {
        const double d = std::abs(A.coeff(i, i));
        maxDiag = std::max(maxDiag, d);
        if (d > 0.0) minNonZeroDiag = std::min(minNonZeroDiag, d);
    }
    stats.maxAbsDiag = maxDiag;
    if (stats.minAbsPivot <= 0.0 && minNonZeroDiag < std::numeric_limits<double>::max()) {
        stats.minAbsPivot = minNonZeroDiag;
    }
    if (stats.minAbsPivot > 0.0 && maxDiag > 0.0) {
        stats.conditionProxy = maxDiag / stats.minAbsPivot;
    }
}

void computeResidualStats(const SpMat& A, const VecX& b, const VecX& x, LinearSolverStats& stats) {
    const VecX r = b - A * x;
    stats.residualNorm = r.norm();
    const double bNorm = b.norm();
    stats.relativeResidual = (bNorm > 1e-15) ? (stats.residualNorm / bNorm) : stats.residualNorm;
}

} // namespace

struct FactorizationCache::Impl {
    std::uint64_t patternHash = 0;
    std::uint64_t valueHash = 0;
    LinearSolverType solverType = LinearSolverType::Auto;
    SparseOrderingType ordering = SparseOrderingType::Auto;
    bool hasPattern = false;
    bool hasFactors = false;

    Eigen::SimplicialLDLT<SpMat, Eigen::Lower, Eigen::AMDOrdering<int>> ldltAmd;
    Eigen::SimplicialLDLT<SpMat, Eigen::Lower, Eigen::NaturalOrdering<int>> ldltNat;
    Eigen::SimplicialLLT<SpMat, Eigen::Lower, Eigen::AMDOrdering<int>> lltAmd;
    Eigen::SparseLU<SpMat, Eigen::COLAMDOrdering<int>> luColamd;
    Eigen::SparseLU<SpMat, Eigen::AMDOrdering<int>> luAmd;
    Eigen::SparseLU<SpMat, Eigen::NaturalOrdering<int>> luNat;
};

FactorizationCache::FactorizationCache() : m_impl(std::make_unique<Impl>()) {}
FactorizationCache::~FactorizationCache() = default;

void FactorizationCache::invalidate() {
    m_impl->hasPattern = false;
    m_impl->hasFactors = false;
    m_impl->patternHash = 0;
    m_impl->valueHash = 0;
}

bool FactorizationCache::isValidFor(const SpMat& A, LinearSolverType solverType, SparseOrderingType ordering) const {
    return m_impl->hasFactors &&
           m_impl->solverType == solverType &&
           m_impl->ordering == ordering &&
           m_impl->valueHash == computeMatrixFingerprint(A, true);
}

bool LinearSolverSuite::isMatrixApproximatelySymmetric(const SpMat& A, double relTol) {
    if (A.rows() != A.cols()) return false;
    const SpMat At = A.transpose();
    const SpMat diff = A - At;
    double maxA = 0.0;
    for (int k = 0; k < A.outerSize(); ++k) {
        for (SpMat::InnerIterator it(A, k); it; ++it) {
            maxA = std::max(maxA, std::abs(it.value()));
        }
    }
    double maxDiff = 0.0;
    for (int k = 0; k < diff.outerSize(); ++k) {
        for (SpMat::InnerIterator it(diff, k); it; ++it) {
            maxDiff = std::max(maxDiff, std::abs(it.value()));
        }
    }
    return maxDiff <= relTol * std::max(maxA, 1.0);
}

VecX LinearSolverSuite::solve(const SpMat& A,
                              const VecX& b,
                              const LinearSolverOptions& opts,
                              LinearSolverStats& stats,
                              FactorizationCache* cache)
{
    stats = LinearSolverStats{};
    stats.totalDofs = static_cast<int>(A.rows());
    stats.activeDofs = static_cast<int>(A.rows());
    stats.nnz = static_cast<int>(A.nonZeros());

    if (A.rows() != A.cols() || A.rows() != b.size()) {
        stats.success = false;
        stats.errorCode = "DIMENSION_MISMATCH";
        stats.message = "Incompatible matrix/vector dimensions in linear solver.";
        return {};
    }

    if (A.rows() == 0) {
        stats.success = true;
        return VecX{};
    }

    if (!b.allFinite()) {
        stats.success = false;
        stats.errorCode = "NON_FINITE_RHS";
        stats.message = "Right-hand side vector contains non-finite values.";
        return {};
    }

    const bool sym = opts.checkSymmetry ? isMatrixApproximatelySymmetric(A) : false;
    stats.matrixSymmetric = sym;

    LinearSolverType chosen = opts.type;
    if (chosen == LinearSolverType::Auto) {
        if (sym) {
            chosen = LinearSolverType::SimplicialLDLT;
        } else {
            chosen = LinearSolverType::SparseLU;
        }
    }
    stats.solverUsed = chosen;

    const auto t0 = std::chrono::high_resolution_clock::now();
    VecX x;

    auto solveWithCachedOrNewFactor = [&](auto& solverObj, LinearSolverType actualType, SparseOrderingType actualOrder) -> bool {
        const std::uint64_t valHash = cache ? computeMatrixFingerprint(A, true) : 0;
        const std::uint64_t patHash = cache ? computeMatrixFingerprint(A, false) : 0;
        const bool sameVals = cache && cache->m_impl->hasFactors &&
                              cache->m_impl->solverType == actualType &&
                              cache->m_impl->ordering == actualOrder &&
                              cache->m_impl->valueHash == valHash;
        if (sameVals) {
            stats.reusedFactorization = true;
        } else {
            const bool samePattern = cache && cache->m_impl->hasPattern &&
                                     cache->m_impl->solverType == actualType &&
                                     cache->m_impl->ordering == actualOrder &&
                                     cache->m_impl->patternHash == patHash;
            if (!samePattern) {
                solverObj.analyzePattern(A);
            }
            solverObj.factorize(A);
            if (solverObj.info() != Eigen::Success) {
                return false;
            }
            if (cache) {
                cache->m_impl->hasPattern = true;
                cache->m_impl->hasFactors = true;
                cache->m_impl->patternHash = patHash;
                cache->m_impl->valueHash = valHash;
                cache->m_impl->solverType = actualType;
                cache->m_impl->ordering = actualOrder;
            }
        }
        const auto tFact = std::chrono::high_resolution_clock::now();
        stats.factorTimeMs = std::chrono::duration<double, std::milli>(tFact - t0).count();

        x = solverObj.solve(b);
        if (solverObj.info() != Eigen::Success || !x.allFinite()) {
            return false;
        }
        if (opts.iterativeRefinement && opts.refinementSteps > 0) {
            runIterativeRefinement(A, b, solverObj, x, opts.refinementSteps);
        }
        return true;
    };

    FactorizationCache localCache;
    FactorizationCache* activeCache = cache ? cache : &localCache;

    if (chosen == LinearSolverType::SimplicialLDLT) {
        const SpMat A_sym = sym ? A : SpMat(0.5 * (A + SpMat(A.transpose())));
        auto checkLdltPivots = [&](const auto& ldltSolver) -> bool {
            const VecX D = ldltSolver.vectorD();
            if (!D.allFinite() || D.size() == 0) return false;
            double maxD = 0.0;
            double minAbsD = std::numeric_limits<double>::max();
            for (int i = 0; i < D.size(); ++i) {
                const double ad = std::abs(D(i));
                maxD = std::max(maxD, ad);
                minAbsD = std::min(minAbsD, ad);
            }
            stats.minAbsPivot = minAbsD;
            stats.maxAbsDiag = maxD;
            stats.conditionProxy = (minAbsD > 0.0) ? (maxD / minAbsD) : std::numeric_limits<double>::infinity();
            if (minAbsD <= opts.pivotZeroTolerance * std::max(maxD, 1.0)) {
                return false;
            }
            return true;
        };

        bool ok = false;
        if (opts.ordering == SparseOrderingType::Natural) {
            auto& s = activeCache->m_impl->ldltNat;
            const std::uint64_t vh = cache ? computeMatrixFingerprint(A_sym, true) : 0;
            const std::uint64_t ph = cache ? computeMatrixFingerprint(A_sym, false) : 0;
            if (cache && cache->m_impl->hasFactors && cache->m_impl->solverType == chosen && cache->m_impl->ordering == opts.ordering && cache->m_impl->valueHash == vh) {
                stats.reusedFactorization = true;
                ok = true;
            } else {
                if (!(cache && cache->m_impl->hasPattern && cache->m_impl->solverType == chosen && cache->m_impl->ordering == opts.ordering && cache->m_impl->patternHash == ph)) {
                    s.analyzePattern(A_sym);
                }
                s.factorize(A_sym);
                ok = (s.info() == Eigen::Success);
                if (ok && cache) {
                    cache->m_impl->hasPattern = cache->m_impl->hasFactors = true;
                    cache->m_impl->patternHash = ph; cache->m_impl->valueHash = vh;
                    cache->m_impl->solverType = chosen; cache->m_impl->ordering = opts.ordering;
                }
            }
            if (ok && checkLdltPivots(s)) {
                x = s.solve(b);
                ok = (s.info() == Eigen::Success && x.allFinite());
                if (ok && opts.iterativeRefinement) runIterativeRefinement(A_sym, b, s, x, opts.refinementSteps);
            } else {
                ok = false;
            }
        } else {
            auto& s = activeCache->m_impl->ldltAmd;
            const std::uint64_t vh = cache ? computeMatrixFingerprint(A_sym, true) : 0;
            const std::uint64_t ph = cache ? computeMatrixFingerprint(A_sym, false) : 0;
            if (cache && cache->m_impl->hasFactors && cache->m_impl->solverType == chosen && cache->m_impl->ordering == SparseOrderingType::AMD && cache->m_impl->valueHash == vh) {
                stats.reusedFactorization = true;
                ok = true;
            } else {
                if (!(cache && cache->m_impl->hasPattern && cache->m_impl->solverType == chosen && cache->m_impl->ordering == SparseOrderingType::AMD && cache->m_impl->patternHash == ph)) {
                    s.analyzePattern(A_sym);
                }
                s.factorize(A_sym);
                ok = (s.info() == Eigen::Success);
                if (ok && cache) {
                    cache->m_impl->hasPattern = cache->m_impl->hasFactors = true;
                    cache->m_impl->patternHash = ph; cache->m_impl->valueHash = vh;
                    cache->m_impl->solverType = chosen; cache->m_impl->ordering = SparseOrderingType::AMD;
                }
            }
            if (ok && checkLdltPivots(s)) {
                x = s.solve(b);
                ok = (s.info() == Eigen::Success && x.allFinite());
                if (ok && opts.iterativeRefinement) runIterativeRefinement(A_sym, b, s, x, opts.refinementSteps);
            } else {
                ok = false;
            }
        }

        if (!ok) {
            if (opts.allowFallback) {
                bool luOk = false;
                if (opts.ordering == SparseOrderingType::AMD) {
                    luOk = solveWithCachedOrNewFactor(activeCache->m_impl->luAmd, LinearSolverType::SparseLU, SparseOrderingType::AMD);
                } else if (opts.ordering == SparseOrderingType::Natural) {
                    luOk = solveWithCachedOrNewFactor(activeCache->m_impl->luNat, LinearSolverType::SparseLU, SparseOrderingType::Natural);
                } else {
                    luOk = solveWithCachedOrNewFactor(activeCache->m_impl->luColamd, LinearSolverType::SparseLU, SparseOrderingType::COLAMD);
                }
                if (luOk) {
                    stats.solverUsed = LinearSolverType::SparseLU;
                    stats.message = "LDLT falhou; fallback seguro para SparseLU concluído com sucesso.";
                    populatePivotDiagnosticsFromMatrix(A, stats);
                    const auto tEndFallback = std::chrono::high_resolution_clock::now();
                    stats.solveTimeMs = std::chrono::duration<double, std::milli>(tEndFallback - t0).count();
                    if (stats.factorTimeMs <= 0.0) stats.factorTimeMs = stats.solveTimeMs;
                    computeResidualStats(A, b, x, stats);
                    stats.success = x.allFinite();
                    return x;
                }
            }
            stats.success = false;
            stats.errorCode = "SINGULAR_MATRIX";
            stats.message = "Falha na fatoração LDLT (matriz singular, mecanismo ou pivô próximo de zero).";
            return {};
        }
    } else if (chosen == LinearSolverType::SimplicialLLT) {
        auto& s = activeCache->m_impl->lltAmd;
        if (!solveWithCachedOrNewFactor(s, chosen, SparseOrderingType::AMD)) {
            if (opts.allowFallback) {
                // Fallback seguro para LDLT (matriz simétrica indefinida / pivô não-positivo) ou SparseLU
                LinearSolverOptions fallbackOpts = opts;
                fallbackOpts.type = sym ? LinearSolverType::SimplicialLDLT : LinearSolverType::SparseLU;
                fallbackOpts.allowFallback = false;
                LinearSolverStats fbStats;
                VecX fbX = solve(A, b, fallbackOpts, fbStats, cache);
                if (fbStats.success) {
                    stats = fbStats;
                    stats.message = "Cholesky LLT não aplicável (matriz não-SPD); fallback seguro para "
                                  + std::string(sym ? "SimplicialLDLT" : "SparseLU") + " concluído.";
                    return fbX;
                }
                if (sym) {
                    fallbackOpts.type = LinearSolverType::SparseLU;
                    fbX = solve(A, b, fallbackOpts, fbStats, cache);
                    if (fbStats.success) {
                        stats = fbStats;
                        stats.message = "Cholesky LLT/LDLT falhou; fallback seguro para SparseLU concluído.";
                        return fbX;
                    }
                }
            }
            stats.success = false;
            stats.errorCode = "NOT_SPD";
            stats.message = "Falha na fatoração Cholesky LLT (matriz não é simétrica definida positiva).";
            return {};
        }
        populatePivotDiagnosticsFromMatrix(A, stats);
    } else if (chosen == LinearSolverType::SparseLU) {
        bool ok = false;
        if (opts.ordering == SparseOrderingType::AMD) {
            ok = solveWithCachedOrNewFactor(activeCache->m_impl->luAmd, chosen, SparseOrderingType::AMD);
        } else if (opts.ordering == SparseOrderingType::Natural) {
            ok = solveWithCachedOrNewFactor(activeCache->m_impl->luNat, chosen, SparseOrderingType::Natural);
        } else {
            ok = solveWithCachedOrNewFactor(activeCache->m_impl->luColamd, chosen, SparseOrderingType::COLAMD);
        }
        if (!ok) {
            stats.success = false;
            stats.errorCode = "SINGULAR_MATRIX";
            stats.message = "Falha na fatoração SparseLU (matriz singular ou mecanismo).";
            return {};
        }
        populatePivotDiagnosticsFromMatrix(A, stats);
    } else if (chosen == LinearSolverType::ConjugateGradient) {
        stats.usedIterative = true;
        bool ok = false;
        if (opts.preconditioner == PreconditionerType::IncompleteCholesky) {
            Eigen::ConjugateGradient<SpMat, Eigen::Lower | Eigen::Upper, Eigen::IncompleteCholesky<double>> cg;
            cg.setMaxIterations(opts.maxIterations);
            cg.setTolerance(opts.tolerance);
            cg.compute(A);
            if (cg.info() == Eigen::Success) {
                x = cg.solve(b);
                stats.iterations = static_cast<int>(cg.iterations());
                ok = (cg.info() == Eigen::Success && x.allFinite());
            }
        }
        if (!ok && opts.preconditioner != PreconditionerType::None) {
            Eigen::ConjugateGradient<SpMat, Eigen::Lower | Eigen::Upper, Eigen::DiagonalPreconditioner<double>> cg;
            cg.setMaxIterations(opts.maxIterations);
            cg.setTolerance(opts.tolerance);
            cg.compute(A);
            if (cg.info() == Eigen::Success) {
                x = cg.solve(b);
                stats.iterations = static_cast<int>(cg.iterations());
                ok = (cg.info() == Eigen::Success && x.allFinite());
            }
        } else if (!ok && opts.preconditioner == PreconditionerType::None) {
            Eigen::ConjugateGradient<SpMat, Eigen::Lower | Eigen::Upper, Eigen::IdentityPreconditioner> cg;
            cg.setMaxIterations(opts.maxIterations);
            cg.setTolerance(opts.tolerance);
            cg.compute(A);
            if (cg.info() == Eigen::Success) {
                x = cg.solve(b);
                stats.iterations = static_cast<int>(cg.iterations());
                ok = (cg.info() == Eigen::Success && x.allFinite());
            }
        }
        if (!ok) {
            stats.success = false;
            stats.errorCode = "ITERATIVE_DIVERGENCE";
            stats.message = "Conjugate Gradient failed to converge.";
            return {};
        }
        populatePivotDiagnosticsFromMatrix(A, stats);
    } else if (chosen == LinearSolverType::BiCGSTAB) {
        stats.usedIterative = true;
        bool ok = false;
        if (opts.preconditioner == PreconditionerType::IncompleteLUT || opts.preconditioner == PreconditionerType::Auto) {
            Eigen::BiCGSTAB<SpMat, Eigen::IncompleteLUT<double>> bicg;
            bicg.setMaxIterations(opts.maxIterations);
            bicg.setTolerance(opts.tolerance);
            bicg.compute(A);
            if (bicg.info() == Eigen::Success) {
                x = bicg.solve(b);
                stats.iterations = static_cast<int>(bicg.iterations());
                ok = (bicg.info() == Eigen::Success && x.allFinite());
            }
        }
        if (!ok) {
            Eigen::BiCGSTAB<SpMat, Eigen::DiagonalPreconditioner<double>> bicg;
            bicg.setMaxIterations(opts.maxIterations);
            bicg.setTolerance(opts.tolerance);
            bicg.compute(A);
            if (bicg.info() == Eigen::Success) {
                x = bicg.solve(b);
                stats.iterations = static_cast<int>(bicg.iterations());
                ok = (bicg.info() == Eigen::Success && x.allFinite());
            }
        }
        if (!ok) {
            stats.success = false;
            stats.errorCode = "ITERATIVE_DIVERGENCE";
            stats.message = "BiCGSTAB failed to converge.";
            return {};
        }
        populatePivotDiagnosticsFromMatrix(A, stats);
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    stats.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (stats.factorTimeMs <= 0.0) stats.factorTimeMs = stats.solveTimeMs;

    computeResidualStats(A, b, x, stats);
    stats.success = x.allFinite();
    return x;
}

} // namespace omniframe::kernel
