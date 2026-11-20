#pragma once

#include "../core/KernelTypes.hpp"

namespace omniframe::kernel {

struct StressInvariants {
    double sigma1 = 0.0;
    double sigma2 = 0.0;
    double sigma3 = 0.0;
    double vonMises = 0.0;
    double tresca = 0.0;
    double hydrostatic = 0.0;
    double octShear = 0.0;
};

class PostProcessor {
public:
    /// Calcula tensões principais (sigma1 >= sigma2 >= sigma3), von Mises, Tresca e invariantes
    /// a partir do vetor de Voigt 3D: [sx, sy, sz, txy, tyz, tzx]
    static StressInvariants computeStressInvariants(const Eigen::Matrix<double, 6, 1>& sigmaVoigt);

    /// Energia de deformação elástica total U = 0.5 * U^T * K * U
    static double computeTotalStrainEnergy(const SpMat& K_full, const VecX& U_full);
};

} // namespace omniframe::kernel
