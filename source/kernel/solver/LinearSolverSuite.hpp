#pragma once

#include "../core/KernelTypes.hpp"
#include <memory>

namespace omniframe::kernel {

class FactorizationCache {
public:
    FactorizationCache();
    ~FactorizationCache();

    FactorizationCache(const FactorizationCache&) = delete;
    FactorizationCache& operator=(const FactorizationCache&) = delete;

    void invalidate();
    bool isValidFor(const SpMat& A, LinearSolverType solverType, SparseOrderingType ordering) const;

private:
    friend class LinearSolverSuite;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class LinearSolverSuite {
public:
    static VecX solve(const SpMat& A,
                      const VecX& b,
                      const LinearSolverOptions& opts,
                      LinearSolverStats& stats,
                      FactorizationCache* cache = nullptr);

    static bool isMatrixApproximatelySymmetric(const SpMat& A, double relTol = 1e-10);
};

} // namespace omniframe::kernel
