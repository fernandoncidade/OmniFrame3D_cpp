#include "GlobalAssembler.hpp"
#include <cmath>

namespace omniframe::kernel {

namespace {

void fillElementDofIndices(const DofManager& dofMgr,
                           const IFiniteElement& elem,
                           std::vector<int>& outIndices)
{
    outIndices.clear();
    const auto mask = elem.requiredNodeDofMask();
    for (NodeId nid : elem.nodeIds()) {
        for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
            if (mask[d]) {
                outIndices.push_back(dofMgr.getNodeGlobalDof(nid, d));
            }
        }
    }
    if (const auto* blk = dofMgr.findInternalBlock(elem.id())) {
        for (int i = 0; i < blk->count; ++i) {
            outIndices.push_back(blk->startGlobalDof + i);
        }
    }
}

} // namespace

bool GlobalAssembler::assembleStiffnessAndLoad(
    const DofManager& dofMgr,
    const std::vector<std::shared_ptr<IFiniteElement>>& elements,
    const std::unordered_map<ElementId, ElementLoadInput>& elementLoads,
    const std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>>& nodalLoads,
    SpMat& K_full,
    VecX& F_full,
    std::string* errorMsg)
{
    const int nDofs = dofMgr.totalDofs();
    if (nDofs <= 0) {
        if (errorMsg) *errorMsg = "Zero global DOFs registered.";
        return false;
    }

    std::vector<Triplet> triplets;
    triplets.reserve(elements.size() * 144 + dofMgr.nodeStates().size() * 7);
    F_full = VecX::Zero(nDofs);

    std::vector<int> dofMap;
    for (const auto& elem : elements) {
        fillElementDofIndices(dofMgr, *elem, dofMap);
        const int nd = static_cast<int>(dofMap.size());

        const MatX Ke = elem->computeGlobalStiffness();
        if (Ke.rows() != nd || Ke.cols() != nd || !Ke.allFinite()) {
            if (errorMsg) *errorMsg = "Invalid or non-finite stiffness matrix for element " + std::to_string(elem->id());
            return false;
        }
        for (int r = 0; r < nd; ++r) {
            if (dofMap[r] < 0) continue;
            for (int c = 0; c < nd; ++c) {
                if (dofMap[c] < 0) continue;
                const double val = Ke(r, c);
                if (std::abs(val) > 1e-16) {
                    triplets.emplace_back(dofMap[r], dofMap[c], val);
                }
            }
        }

        auto lit = elementLoads.find(elem->id());
        if (lit != elementLoads.end()) {
            const VecX fe = elem->computeGlobalLoadVector(lit->second);
            if (fe.size() != nd || !fe.allFinite()) {
                if (errorMsg) *errorMsg = "Invalid or non-finite load vector for element " + std::to_string(elem->id());
                return false;
            }
            for (int r = 0; r < nd; ++r) {
                if (dofMap[r] >= 0) {
                    F_full(dofMap[r]) += fe(r);
                }
            }
        }
    }

    // Molas nodais e cargas nodais
    for (const auto& ns : dofMgr.nodeStates()) {
        for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
            const int g = ns.globalDof[d];
            if (g < 0) continue;
            if (!std::isfinite(ns.springStiffness[d]) || ns.springStiffness[d] < 0.0) {
                if (errorMsg) *errorMsg = "Invalid spring stiffness at node " + std::to_string(ns.nodeId);
                return false;
            }
            if (ns.springStiffness[d] > 1e-15) {
                triplets.emplace_back(g, g, ns.springStiffness[d]);
            }
        }
        auto nit = nodalLoads.find(ns.nodeId);
        if (nit != nodalLoads.end()) {
            for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
                const int g = ns.globalDof[d];
                if (g >= 0) {
                    F_full(g) += nit->second(d);
                }
            }
        }
    }

    if (!F_full.allFinite()) {
        if (errorMsg) *errorMsg = "Global force vector contains non-finite values.";
        return false;
    }

    K_full.resize(nDofs, nDofs);
    K_full.setFromTriplets(triplets.begin(), triplets.end());
    K_full.makeCompressed();
    return true;
}

bool GlobalAssembler::assembleMass(
    const DofManager& dofMgr,
    const std::vector<std::shared_ptr<IFiniteElement>>& elements,
    bool consistent,
    const std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>>& nodalMass,
    SpMat& M_full,
    std::string* errorMsg)
{
    const int nDofs = dofMgr.totalDofs();
    if (nDofs <= 0) {
        if (errorMsg) *errorMsg = "Zero global DOFs registered.";
        return false;
    }

    std::vector<Triplet> triplets;
    triplets.reserve(elements.size() * 144 + dofMgr.nodeStates().size() * 7);
    std::vector<int> dofMap;

    for (const auto& elem : elements) {
        fillElementDofIndices(dofMgr, *elem, dofMap);
        const int nd = static_cast<int>(dofMap.size());
        const MatX Me = elem->computeGlobalMass(consistent);
        if (Me.rows() != nd || Me.cols() != nd || !Me.allFinite()) {
            if (errorMsg) *errorMsg = "Invalid mass matrix for element " + std::to_string(elem->id());
            return false;
        }
        for (int r = 0; r < nd; ++r) {
            if (dofMap[r] < 0) continue;
            for (int c = 0; c < nd; ++c) {
                if (dofMap[c] < 0) continue;
                const double val = Me(r, c);
                if (std::abs(val) > 1e-18) {
                    triplets.emplace_back(dofMap[r], dofMap[c], val);
                }
            }
        }
    }

    for (const auto& ns : dofMgr.nodeStates()) {
        auto mit = nodalMass.find(ns.nodeId);
        if (mit == nodalMass.end()) continue;
        for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
            const int g = ns.globalDof[d];
            const double m = mit->second(d);
            if (g >= 0 && std::abs(m) > 0.0) {
                if (!std::isfinite(m) || m < 0.0) {
                    if (errorMsg) *errorMsg = "Invalid nodal mass at node " + std::to_string(ns.nodeId);
                    return false;
                }
                triplets.emplace_back(g, g, m);
            }
        }
    }

    M_full.resize(nDofs, nDofs);
    M_full.setFromTriplets(triplets.begin(), triplets.end());
    M_full.makeCompressed();
    return true;
}

bool GlobalAssembler::assembleGeometricStiffness(
    const DofManager& dofMgr,
    const std::vector<std::shared_ptr<IFiniteElement>>& elements,
    const VecX& U_full,
    SpMat& Kg_full,
    std::string* errorMsg)
{
    const int nDofs = dofMgr.totalDofs();
    std::vector<Triplet> triplets;
    triplets.reserve(elements.size() * 144);
    std::vector<int> dofMap;

    for (const auto& elem : elements) {
        fillElementDofIndices(dofMgr, *elem, dofMap);
        const int nd = static_cast<int>(dofMap.size());
        VecX uElem = VecX::Zero(nd);
        for (int r = 0; r < nd; ++r) {
            if (dofMap[r] >= 0 && dofMap[r] < U_full.size()) {
                uElem(r) = U_full(dofMap[r]);
            }
        }
        const MatX Kge = elem->computeGlobalGeometricStiffness(uElem);
        if (Kge.rows() != nd || Kge.cols() != nd || !Kge.allFinite()) {
            if (errorMsg) *errorMsg = "Invalid geometric stiffness matrix for element " + std::to_string(elem->id());
            return false;
        }
        for (int r = 0; r < nd; ++r) {
            if (dofMap[r] < 0) continue;
            for (int c = 0; c < nd; ++c) {
                if (dofMap[c] < 0) continue;
                const double val = Kge(r, c);
                if (std::abs(val) > 1e-16) {
                    triplets.emplace_back(dofMap[r], dofMap[c], val);
                }
            }
        }
    }

    Kg_full.resize(nDofs, nDofs);
    Kg_full.setFromTriplets(triplets.begin(), triplets.end());
    Kg_full.makeCompressed();
    return true;
}

bool GlobalAssembler::buildReducedSystem(const DofManager& dofMgr,
                                         const SpMat& K_full,
                                         const VecX& F_full,
                                         const DofClassification& cls,
                                         ReducedLinearSystem& outSys,
                                         std::string* errorMsg)
{
    outSys = ReducedLinearSystem{};
    outSys.fullDofs = dofMgr.totalDofs();
    outSys.hasMpc = !dofMgr.mpcConstraints().empty();

    SpMat K_red;
    VecX F_red;
    std::vector<bool> redFixed;
    VecX redFixedVal;

    if (outSys.hasMpc) {
        if (!dofMgr.buildMpcTransformation(outSys.T_mpc, outSys.U_offset, outSys.fullToReducedMap, outSys.reducedDofs, errorMsg)) {
            return false;
        }
        K_red = (outSys.T_mpc.transpose() * K_full * outSys.T_mpc).pruned();
        F_red = outSys.T_mpc.transpose() * (F_full - K_full * outSys.U_offset);
    } else {
        outSys.reducedDofs = outSys.fullDofs;
        outSys.fullToReducedMap.resize(outSys.fullDofs);
        for (int g = 0; g < outSys.fullDofs; ++g) outSys.fullToReducedMap[g] = g;
        K_red = K_full;
        F_red = F_full;
    }

    redFixed.assign(outSys.reducedDofs, false);
    redFixedVal = VecX::Zero(outSys.reducedDofs);

    for (int g = 0; g < outSys.fullDofs; ++g) {
        const int rd = outSys.fullToReducedMap[g];
        if (rd < 0) continue;
        if (cls.isRestrained[g]) {
            redFixed[rd] = true;
            redFixedVal(rd) = cls.prescribedValue[g];
        } else if (cls.isInactive[g]) {
            redFixed[rd] = true;
            redFixedVal(rd) = 0.0;
        }
    }

    // Caso MPC elimine DOF mas um slave estava restrito, já bloqueia conflito se relevante;
    // para DOFs independentes livres, extrai submatriz K_ff e rhs_f = F_f - K_fr * U_fixed
    outSys.reducedToFreeMap.assign(outSys.reducedDofs, -1);
    outSys.freeReducedIndices.clear();
    for (int rd = 0; rd < outSys.reducedDofs; ++rd) {
        if (!redFixed[rd]) {
            outSys.reducedToFreeMap[rd] = static_cast<int>(outSys.freeReducedIndices.size());
            outSys.freeReducedIndices.push_back(rd);
        }
    }
    outSys.freeDofs = static_cast<int>(outSys.freeReducedIndices.size());
    outSys.U_fixed_red = redFixedVal;

    // Verificação de rigidez após MPC caso existam MPCs criando acoplamentos
    if (outSys.freeDofs == 0) {
        outSys.K_ff.resize(0, 0);
        outSys.rhs_f.resize(0);
        return true;
    }

    std::vector<Triplet> ffTrips;
    ffTrips.reserve(static_cast<std::size_t>(K_red.nonZeros()));
    for (int k = 0; k < K_red.outerSize(); ++k) {
        for (SpMat::InnerIterator it(K_red, k); it; ++it) {
            const int rFree = outSys.reducedToFreeMap[it.row()];
            const int cFree = outSys.reducedToFreeMap[it.col()];
            if (rFree >= 0 && cFree >= 0) {
                ffTrips.emplace_back(rFree, cFree, it.value());
            }
        }
    }

    outSys.K_ff.resize(outSys.freeDofs, outSys.freeDofs);
    outSys.K_ff.setFromTriplets(ffTrips.begin(), ffTrips.end());
    outSys.K_ff.makeCompressed();

    const VecX rhsFullRed = F_red - K_red * redFixedVal;
    outSys.rhs_f.resize(outSys.freeDofs);
    for (int i = 0; i < outSys.freeDofs; ++i) {
        outSys.rhs_f(i) = rhsFullRed(outSys.freeReducedIndices[i]);
    }
    return true;
}

bool GlobalAssembler::buildReducedRightHandSide(const DofManager& dofMgr,
                                              const SpMat& K_full,
                                              const VecX& F_full,
                                              const ReducedLinearSystem& cachedSys,
                                              VecX& outRhs_f,
                                              std::string* errorMsg)
{
    (void)dofMgr;
    if (F_full.size() != cachedSys.fullDofs) {
        if (errorMsg) *errorMsg = "Force vector size mismatch with cached reduced system.";
        return false;
    }
    if (cachedSys.freeDofs == 0) {
        outRhs_f.resize(0);
        return true;
    }

    VecX rhsFullRed;
    if (cachedSys.hasMpc) {
        const SpMat K_red = (cachedSys.T_mpc.transpose() * K_full * cachedSys.T_mpc).pruned();
        const VecX F_red = cachedSys.T_mpc.transpose() * (F_full - K_full * cachedSys.U_offset);
        rhsFullRed = F_red - K_red * cachedSys.U_fixed_red;
    } else {
        rhsFullRed = F_full - K_full * cachedSys.U_fixed_red;
    }

    outRhs_f.resize(cachedSys.freeDofs);
    for (int i = 0; i < cachedSys.freeDofs; ++i) {
        outRhs_f(i) = rhsFullRed(cachedSys.freeReducedIndices[i]);
    }
    return true;
}

VecX GlobalAssembler::reconstructFullDisplacements(const ReducedLinearSystem& sys,
                                                   const VecX& U_free)
{
    VecX U_red = sys.U_fixed_red;
    for (int i = 0; i < sys.freeDofs; ++i) {
        U_red(sys.freeReducedIndices[i]) = U_free(i);
    }
    if (sys.hasMpc) {
        return sys.T_mpc * U_red + sys.U_offset;
    }
    return U_red;
}

} // namespace omniframe::kernel
