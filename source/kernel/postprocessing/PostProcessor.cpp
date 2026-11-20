#include "PostProcessor.hpp"
#include <Eigen/Eigenvalues>
#include <algorithm>
#include <cmath>

namespace omniframe::kernel {

StressInvariants PostProcessor::computeStressInvariants(const Eigen::Matrix<double, 6, 1>& s) {
    Mat3 T;
    T << s(0), s(3), s(5),
         s(3), s(1), s(4),
         s(5), s(4), s(2);

    Eigen::SelfAdjointEigenSolver<Mat3> es(T);
    const auto& evals = es.eigenvalues(); // crescente: evals(0) <= evals(1) <= evals(2)

    StressInvariants inv;
    inv.sigma1 = evals(2);
    inv.sigma2 = evals(1);
    inv.sigma3 = evals(0);
    inv.hydrostatic = (inv.sigma1 + inv.sigma2 + inv.sigma3) / 3.0;
    inv.tresca = inv.sigma1 - inv.sigma3;

    const double d12 = inv.sigma1 - inv.sigma2;
    const double d23 = inv.sigma2 - inv.sigma3;
    const double d31 = inv.sigma3 - inv.sigma1;
    inv.vonMises = std::sqrt(0.5 * (d12 * d12 + d23 * d23 + d31 * d31));
    inv.octShear = (std::sqrt(2.0) / 3.0) * inv.vonMises;
    return inv;
}

double PostProcessor::computeTotalStrainEnergy(const SpMat& K_full, const VecX& U_full) {
    if (K_full.rows() != U_full.size() || U_full.size() == 0) return 0.0;
    return 0.5 * U_full.dot(K_full * U_full);
}

} // namespace omniframe::kernel
