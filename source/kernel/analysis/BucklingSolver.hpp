#pragma once

#include "../assembly/GlobalAssembler.hpp"
#include "../core/KernelTypes.hpp"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <string>
#include <vector>

namespace omniframe::kernel {

struct BucklingOptions {
    int numModes = 4;
    int maxIterations = 250;
    double tolerance = 1e-8;
    bool includeNegativeFactors = false;
};

struct BucklingResult {
    bool success = false;
    std::string errorMessage;
    std::vector<double> loadFactors;   // Fatores críticos lambda_cr ordenados
    std::vector<VecX> modeShapesFull;  // Autovetores expandidos para DOFs globais (normalizados em máx translação=1)
    int iterationsPerformed = 0;
};

/**
 * Resolvedor linearizado de flambagem elástica (autovalor generalizado):
 *   (K_ff + lambda * Kg_ff) * phi = 0  <=>  (-Kg_ff) * phi = (1 / lambda) * K_ff * phi
 *
 * Implementa iteração em subespaço com ortogonalização K_ff-ortonormal (à prova de
 * matriz geométrica Kg de posto deficiente / modos nulos) e fatoração LDLT esparsa.
 */
class BucklingSolver {
public:
    static BucklingResult solve(const SpMat& K_ff,
                                const SpMat& Kg_ff,
                                const ReducedLinearSystem& redSys,
                                const BucklingOptions& opts = {});

    /**
     * Calcula coordenadas iniciais imperfeitas perturbando o modelo na direção
     * do modo de flambagem `modeFull` com amplitude máxima translacional `amplitude`.
     */
    static std::vector<Vec3> applyGeometricImperfection(const std::vector<Vec3>& baseNodeCoords,
                                                        const DofManager& dofMgr,
                                                        const VecX& modeFull,
                                                        double amplitude);
};

} // namespace omniframe::kernel
