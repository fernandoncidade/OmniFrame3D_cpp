#pragma once

#include "../core/DofManager.hpp"
#include "../elements/IFiniteElement.hpp"
#include <memory>
#include <vector>

namespace omniframe::kernel {

struct AssembledSystem {
    SpMat K_full;
    VecX F_full;
    SpMat M_full;
    bool hasMass = false;
};

struct ReducedLinearSystem {
    SpMat K_ff;
    VecX rhs_f;
    SpMat T_mpc;
    VecX U_offset;
    VecX U_fixed_red;
    std::vector<int> fullToReducedMap;
    std::vector<int> freeReducedIndices;
    std::vector<int> reducedToFreeMap;
    int fullDofs = 0;
    int reducedDofs = 0;
    int freeDofs = 0;
    bool hasMpc = false;
};

class GlobalAssembler {
public:
    static bool assembleStiffnessAndLoad(const DofManager& dofMgr,
                                         const std::vector<std::shared_ptr<IFiniteElement>>& elements,
                                         const std::unordered_map<ElementId, ElementLoadInput>& elementLoads,
                                         const std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>>& nodalLoads,
                                         SpMat& K_full,
                                         VecX& F_full,
                                         std::string* errorMsg = nullptr);

    static bool assembleMass(const DofManager& dofMgr,
                             const std::vector<std::shared_ptr<IFiniteElement>>& elements,
                             bool consistent,
                             const std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>>& nodalMass,
                             SpMat& M_full,
                             std::string* errorMsg = nullptr);

    static bool assembleGeometricStiffness(const DofManager& dofMgr,
                                           const std::vector<std::shared_ptr<IFiniteElement>>& elements,
                                           const VecX& U_full,
                                           SpMat& Kg_full,
                                           std::string* errorMsg = nullptr);

    static bool buildReducedSystem(const DofManager& dofMgr,
                                   const SpMat& K_full,
                                   const VecX& F_full,
                                   const DofClassification& cls,
                                   ReducedLinearSystem& outSys,
                                   std::string* errorMsg = nullptr);

    static bool buildReducedRightHandSide(const DofManager& dofMgr,
                                          const SpMat& K_full,
                                          const VecX& F_full,
                                          const ReducedLinearSystem& cachedSys,
                                          VecX& outRhs_f,
                                          std::string* errorMsg = nullptr);

    static VecX reconstructFullDisplacements(const ReducedLinearSystem& sys,
                                             const VecX& U_free);
};

} // namespace omniframe::kernel
