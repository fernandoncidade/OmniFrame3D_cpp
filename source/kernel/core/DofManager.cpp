#include "DofManager.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace omniframe::kernel {

void DofManager::clear() {
    m_nodes.clear();
    m_nodeLookup.clear();
    m_internalBlocks.clear();
    m_internalLookup.clear();
    m_mpcs.clear();
    m_nodalDofCount = 0;
    m_totalDofs = 0;
}

void DofManager::registerNode(NodeId id, const std::array<bool, NODAL_MAX_DOFS>& mask) {
    auto it = m_nodeLookup.find(id);
    if (it != m_nodeLookup.end()) {
        for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
            if (mask[d]) {
                m_nodes[it->second].activeMask[d] = true;
            }
        }
        return;
    }

    NodeDofState state;
    state.nodeId = id;
    state.activeMask = mask;
    m_nodeLookup[id] = m_nodes.size();
    m_nodes.push_back(state);
}

void DofManager::setBoundaryCondition(NodeId id, int localDof, bool fixed, double prescribed, double spring) {
    if (localDof < 0 || localDof >= NODAL_MAX_DOFS) return;
    auto it = m_nodeLookup.find(id);
    if (it == m_nodeLookup.end()) return;
    NodeDofState& st = m_nodes[it->second];
    st.restrained[localDof] = fixed;
    st.prescribedValue[localDof] = prescribed;
    st.springStiffness[localDof] = spring;
}

int DofManager::allocateInternalDofs(ElementId elementId, int count) {
    if (count <= 0) return -1;
    auto it = m_internalLookup.find(elementId);
    if (it != m_internalLookup.end()) {
        m_internalBlocks[it->second].count = std::max(m_internalBlocks[it->second].count, count);
        return m_internalBlocks[it->second].startGlobalDof;
    }
    InternalDofBlock block;
    block.elementId = elementId;
    block.startGlobalDof = -1;
    block.count = count;
    m_internalLookup[elementId] = m_internalBlocks.size();
    m_internalBlocks.push_back(block);
    return -1;
}

void DofManager::finalizeNumbering() {
    int nextDof = 0;
    for (auto& node : m_nodes) {
        for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
            if (node.activeMask[d]) {
                node.globalDof[d] = nextDof++;
            } else {
                node.globalDof[d] = -1;
            }
        }
    }
    m_nodalDofCount = nextDof;

    for (auto& block : m_internalBlocks) {
        block.startGlobalDof = nextDof;
        nextDof += block.count;
    }
    m_totalDofs = nextDof;
}

int DofManager::getNodeGlobalDof(NodeId id, int localDof) const {
    if (localDof < 0 || localDof >= NODAL_MAX_DOFS) return -1;
    auto it = m_nodeLookup.find(id);
    if (it == m_nodeLookup.end()) return -1;
    return m_nodes[it->second].globalDof[localDof];
}

const NodeDofState* DofManager::findNodeState(NodeId id) const {
    auto it = m_nodeLookup.find(id);
    if (it == m_nodeLookup.end()) return nullptr;
    return &m_nodes[it->second];
}

const InternalDofBlock* DofManager::findInternalBlock(ElementId elementId) const {
    auto it = m_internalLookup.find(elementId);
    if (it == m_internalLookup.end()) return nullptr;
    return &m_internalBlocks[it->second];
}

bool DofManager::addMpcConstraint(const MpcConstraint& constraint, std::string* errorMsg) {
    if (constraint.slaveDof < 0 || constraint.slaveDof >= m_totalDofs) {
        if (errorMsg) *errorMsg = "Slave DOF out of range in MPC constraint.";
        return false;
    }
    if (!std::isfinite(constraint.constantOffset)) {
        if (errorMsg) *errorMsg = "Non-finite offset in MPC constraint.";
        return false;
    }
    for (const auto& m : m_mpcs) {
        if (m.slaveDof == constraint.slaveDof) {
            if (errorMsg) *errorMsg = "Duplicate slave DOF in MPC constraints.";
            return false;
        }
    }
    for (const auto& term : constraint.masterTerms) {
        if (term.globalDof < 0 || term.globalDof >= m_totalDofs || term.globalDof == constraint.slaveDof) {
            if (errorMsg) *errorMsg = "Invalid master DOF in MPC constraint.";
            return false;
        }
        if (!std::isfinite(term.coefficient)) {
            if (errorMsg) *errorMsg = "Non-finite coefficient in MPC constraint.";
            return false;
        }
        // Evita cadeia direta mestre-escravo não resolvida nesta versão
        for (const auto& existing : m_mpcs) {
            if (existing.slaveDof == term.globalDof) {
                if (errorMsg) *errorMsg = "Chained MPC slave referenced as master DOF.";
                return false;
            }
        }
    }
    for (const auto& existing : m_mpcs) {
        for (const auto& term : existing.masterTerms) {
            if (term.globalDof == constraint.slaveDof) {
                if (errorMsg) *errorMsg = "MPC slave DOF already used as master in another constraint.";
                return false;
            }
        }
    }
    m_mpcs.push_back(constraint);
    return true;
}

bool DofManager::addRigidLink(NodeId masterNode,
                              const Vec3& masterPos,
                              NodeId slaveNode,
                              const Vec3& slavePos,
                              bool coupleRotations,
                              std::string* errorMsg)
{
    if (masterNode == slaveNode) {
        if (errorMsg) *errorMsg = "Master and slave nodes cannot be identical in rigid link.";
        return false;
    }
    const Vec3 r = slavePos - masterPos;
    const int muX = getNodeGlobalDof(masterNode, 0);
    const int muY = getNodeGlobalDof(masterNode, 1);
    const int muZ = getNodeGlobalDof(masterNode, 2);
    const int mrX = getNodeGlobalDof(masterNode, 3);
    const int mrY = getNodeGlobalDof(masterNode, 4);
    const int mrZ = getNodeGlobalDof(masterNode, 5);

    const int suX = getNodeGlobalDof(slaveNode, 0);
    const int suY = getNodeGlobalDof(slaveNode, 1);
    const int suZ = getNodeGlobalDof(slaveNode, 2);
    if (muX < 0 || muY < 0 || muZ < 0 || suX < 0 || suY < 0 || suZ < 0) {
        if (errorMsg) *errorMsg = "Translational DOFs missing for rigid link nodes.";
        return false;
    }

    // u_s = u_m + theta_m x r =>
    // ux_s = ux_m + ry_m * rz - rz_m * ry
    // uy_s = uy_m + rz_m * rx - rx_m * rz
    // uz_s = uz_m + rx_m * ry - ry_m * rx
    MpcConstraint cx{"rigid_ux", suX, {{muX, 1.0}}, 0.0};
    MpcConstraint cy{"rigid_uy", suY, {{muY, 1.0}}, 0.0};
    MpcConstraint cz{"rigid_uz", suZ, {{muZ, 1.0}}, 0.0};
    if (mrY >= 0 && std::abs(r.z()) > 0.0) cx.masterTerms.push_back({mrY, r.z()});
    if (mrZ >= 0 && std::abs(r.y()) > 0.0) cx.masterTerms.push_back({mrZ, -r.y()});
    if (mrZ >= 0 && std::abs(r.x()) > 0.0) cy.masterTerms.push_back({mrZ, r.x()});
    if (mrX >= 0 && std::abs(r.z()) > 0.0) cy.masterTerms.push_back({mrX, -r.z()});
    if (mrX >= 0 && std::abs(r.y()) > 0.0) cz.masterTerms.push_back({mrX, r.y()});
    if (mrY >= 0 && std::abs(r.x()) > 0.0) cz.masterTerms.push_back({mrY, -r.x()});

    if (!addMpcConstraint(cx, errorMsg) ||
        !addMpcConstraint(cy, errorMsg) ||
        !addMpcConstraint(cz, errorMsg)) {
        return false;
    }

    if (coupleRotations) {
        const int srX = getNodeGlobalDof(slaveNode, 3);
        const int srY = getNodeGlobalDof(slaveNode, 4);
        const int srZ = getNodeGlobalDof(slaveNode, 5);
        if (mrX >= 0 && srX >= 0 && !addMpcConstraint({"rigid_rx", srX, {{mrX, 1.0}}, 0.0}, errorMsg)) return false;
        if (mrY >= 0 && srY >= 0 && !addMpcConstraint({"rigid_ry", srY, {{mrY, 1.0}}, 0.0}, errorMsg)) return false;
        if (mrZ >= 0 && srZ >= 0 && !addMpcConstraint({"rigid_rz", srZ, {{mrZ, 1.0}}, 0.0}, errorMsg)) return false;
    }

    return true;
}

bool DofManager::addRigidDiaphragm(NodeId masterNode,
                                   const Vec3& masterPos,
                                   const std::vector<std::pair<NodeId, Vec3>>& slaveNodes,
                                   int normalAxis,
                                   std::string* errorMsg)
{
    if (normalAxis < 0 || normalAxis > 2) {
        if (errorMsg) *errorMsg = "Invalid normal axis for rigid diaphragm.";
        return false;
    }
    const int ax1 = (normalAxis + 1) % 3;
    const int ax2 = (normalAxis + 2) % 3;

    const int mU1 = getNodeGlobalDof(masterNode, ax1);
    const int mU2 = getNodeGlobalDof(masterNode, ax2);
    const int mRn = getNodeGlobalDof(masterNode, 3 + normalAxis);
    if (mU1 < 0 || mU2 < 0 || mRn < 0) {
        if (errorMsg) *errorMsg = "Master node missing in-plane or normal rotation DOF for diaphragm.";
        return false;
    }

    for (const auto& [slaveId, slavePos] : slaveNodes) {
        if (slaveId == masterNode) continue;
        const int sU1 = getNodeGlobalDof(slaveId, ax1);
        const int sU2 = getNodeGlobalDof(slaveId, ax2);
        const int sRn = getNodeGlobalDof(slaveId, 3 + normalAxis);
        if (sU1 < 0 || sU2 < 0) {
            if (errorMsg) *errorMsg = "Slave node missing translational DOFs for diaphragm.";
            return false;
        }
        const Vec3 r = slavePos - masterPos;
        const double d1 = r[ax1];
        const double d2 = r[ax2];

        // No plano (ax1, ax2, normalAxis formando base cíclica destra):
        // u_s1 = u_m1 - d2 * r_n
        // u_s2 = u_m2 + d1 * r_n
        MpcConstraint c1{"diaphragm_u1", sU1, {{mU1, 1.0}, {mRn, -d2}}, 0.0};
        MpcConstraint c2{"diaphragm_u2", sU2, {{mU2, 1.0}, {mRn,  d1}}, 0.0};
        if (!addMpcConstraint(c1, errorMsg) || !addMpcConstraint(c2, errorMsg)) {
            return false;
        }
        if (sRn >= 0) {
            if (!addMpcConstraint({"diaphragm_rn", sRn, {{mRn, 1.0}}, 0.0}, errorMsg)) {
                return false;
            }
        }
    }
    return true;
}

bool DofManager::buildMpcTransformation(SpMat& T_mpc,
                                        VecX& U_offset,
                                        std::vector<int>& fullToReducedMap,
                                        int& reducedDofCount,
                                        std::string* errorMsg) const
{
    if (m_totalDofs <= 0) {
        if (errorMsg) *errorMsg = "No DOFs registered.";
        return false;
    }

    std::vector<int> slaveToMpcIdx(m_totalDofs, -1);
    for (int i = 0; i < static_cast<int>(m_mpcs.size()); ++i) {
        const int sd = m_mpcs[i].slaveDof;
        if (sd < 0 || sd >= m_totalDofs || slaveToMpcIdx[sd] >= 0) {
            if (errorMsg) *errorMsg = "Invalid or duplicate MPC slave DOF.";
            return false;
        }
        slaveToMpcIdx[sd] = i;
    }

    fullToReducedMap.assign(m_totalDofs, -1);
    reducedDofCount = 0;
    for (int g = 0; g < m_totalDofs; ++g) {
        if (slaveToMpcIdx[g] < 0) {
            fullToReducedMap[g] = reducedDofCount++;
        }
    }

    U_offset = VecX::Zero(m_totalDofs);
    std::vector<Triplet> trips;
    trips.reserve(static_cast<std::size_t>(m_totalDofs) + m_mpcs.size() * 4);

    for (int g = 0; g < m_totalDofs; ++g) {
        if (fullToReducedMap[g] >= 0) {
            trips.emplace_back(g, fullToReducedMap[g], 1.0);
        } else {
            const MpcConstraint& c = m_mpcs[slaveToMpcIdx[g]];
            U_offset(g) = c.constantOffset;
            for (const auto& term : c.masterTerms) {
                const int col = fullToReducedMap[term.globalDof];
                if (col < 0) {
                    if (errorMsg) *errorMsg = "Unresolved master DOF in MPC transformation.";
                    return false;
                }
                if (std::abs(term.coefficient) > 0.0) {
                    trips.emplace_back(g, col, term.coefficient);
                }
            }
        }
    }

    T_mpc.resize(m_totalDofs, reducedDofCount);
    T_mpc.setFromTriplets(trips.begin(), trips.end());
    return true;
}

DofClassification DofManager::classifyDofs(const SpMat& K_orig,
                                           const VecX& F_orig,
                                           std::string* mechanismError) const
{
    DofClassification cls;
    cls.isRestrained.assign(m_totalDofs, false);
    cls.isInactive.assign(m_totalDofs, false);
    cls.isMpcSlave.assign(m_totalDofs, false);
    cls.prescribedValue.assign(m_totalDofs, 0.0);

    for (const auto& c : m_mpcs) {
        if (c.slaveDof >= 0 && c.slaveDof < m_totalDofs) {
            cls.isMpcSlave[c.slaveDof] = true;
            cls.mpcSlaveCount++;
        }
    }

    for (const auto& node : m_nodes) {
        for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
            const int g = node.globalDof[d];
            if (g < 0) continue;
            if (node.restrained[d]) {
                cls.isRestrained[g] = true;
                cls.prescribedValue[g] = node.prescribedValue[d];
                cls.restrainedCount++;
            }
        }
    }

    VecX diagAbsSum = VecX::Zero(m_totalDofs);
    for (int k = 0; k < K_orig.outerSize(); ++k) {
        for (SpMat::InnerIterator it(K_orig, k); it; ++it) {
            diagAbsSum(it.row()) += std::abs(it.value());
        }
    }

    for (const auto& node : m_nodes) {
        bool hasTransStiff = false;
        for (int d = 0; d < 3; ++d) {
            const int g = node.globalDof[d];
            if (g >= 0 && (cls.isRestrained[g] || cls.isMpcSlave[g] || diagAbsSum(g) > 1e-12)) {
                hasTransStiff = true;
            }
        }
        for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
            const int g = node.globalDof[d];
            if (g < 0 || cls.isRestrained[g] || cls.isMpcSlave[g]) continue;

            if (diagAbsSum(g) <= 1e-12) {
                if (d < 3 || !hasTransStiff) {
                    if (mechanismError) {
                        *mechanismError = "Falha na fatoração da matriz de rigidez global (possível mecanismo ou matriz singular).";
                    }
                    return cls;
                }
                if (g < F_orig.size() && std::abs(F_orig(g)) > 1e-12) {
                    if (mechanismError) {
                        *mechanismError = "Mecanismo detectado: momento aplicado em nó rotulado sem rigidez rotacional.";
                    }
                    return cls;
                }
                cls.isInactive[g] = true;
                cls.inactiveCount++;
            } else {
                cls.activeCount++;
            }
        }
    }

    // Detecta componentes puramente rotacionais com modo rígido de giro axial livre (torção sem restrição e sem momento aplicado)
    std::vector<bool> isRotDof(m_totalDofs, false);
    for (const auto& node : m_nodes) {
        for (int d = 3; d < NODAL_MAX_DOFS; ++d) {
            const int g = node.globalDof[d];
            if (g >= 0 && g < m_totalDofs && !cls.isRestrained[g] && !cls.isMpcSlave[g] && !cls.isInactive[g]) {
                isRotDof[g] = true;
            }
        }
    }

    std::vector<bool> isPureRotFree = isRotDof;
    for (int g = 0; g < m_totalDofs; ++g) {
        if (isPureRotFree[g] && g < F_orig.size() && std::abs(F_orig(g)) > 1e-12) {
            isPureRotFree[g] = false;
        }
    }

    for (int k = 0; k < K_orig.outerSize(); ++k) {
        for (SpMat::InnerIterator it(K_orig, k); it; ++it) {
            if (std::abs(it.value()) <= 1e-12) continue;
            const int r = it.row();
            const int c = it.col();
            if (!isRotDof[r] || !isRotDof[c]) {
                if (r >= 0 && r < m_totalDofs) isPureRotFree[r] = false;
                if (c >= 0 && c < m_totalDofs) isPureRotFree[c] = false;
            }
        }
    }

    std::vector<std::vector<int>> rotNeighbors(m_totalDofs);
    std::vector<double> rotRowSum(m_totalDofs, 0.0);
    std::vector<double> rotDiag(m_totalDofs, 0.0);
    std::vector<bool> couplesOutsidePureRot(m_totalDofs, false);
    for (int k = 0; k < K_orig.outerSize(); ++k) {
        for (SpMat::InnerIterator it(K_orig, k); it; ++it) {
            const double val = it.value();
            if (std::abs(val) <= 1e-12) continue;
            const int r = it.row();
            const int c = it.col();
            if (isPureRotFree[r]) {
                if (r == c) rotDiag[r] = val;
                if (!isPureRotFree[c]) {
                    couplesOutsidePureRot[r] = true;
                } else {
                    rotRowSum[r] += val;
                    if (r != c) rotNeighbors[r].push_back(c);
                }
            }
            if (isPureRotFree[c] && !isPureRotFree[r]) {
                couplesOutsidePureRot[c] = true;
            }
            if (isPureRotFree[r] && isPureRotFree[c] && r != c) {
                rotNeighbors[c].push_back(r);
            }
        }
    }

    std::vector<bool> visitedRot(m_totalDofs, false);
    for (int seed = 0; seed < m_totalDofs; ++seed) {
        if (!isPureRotFree[seed] || visitedRot[seed]) continue;
        std::vector<int> component;
        std::vector<int> queue;
        queue.push_back(seed);
        visitedRot[seed] = true;
        bool validComponent = true;

        for (size_t qi = 0; qi < queue.size(); ++qi) {
            const int u = queue[qi];
            component.push_back(u);
            if (couplesOutsidePureRot[u]) {
                validComponent = false;
            }
            for (int v : rotNeighbors[u]) {
                if (!isPureRotFree[v]) {
                    validComponent = false;
                } else if (!visitedRot[v]) {
                    visitedRot[v] = true;
                    queue.push_back(v);
                }
            }
        }

        if (validComponent && !component.empty()) {
            for (int u : component) {
                if (std::abs(rotRowSum[u]) > 1e-9 * std::max(1.0, std::abs(rotDiag[u]))) {
                    validComponent = false;
                    break;
                }
            }
        }

        if (validComponent && !component.empty()) {
            const int rep = component.front();
            if (!cls.isInactive[rep]) {
                cls.isInactive[rep] = true;
                cls.inactiveCount++;
                if (cls.activeCount > 0) cls.activeCount--;
            }
        }
    }

    for (const auto& blk : m_internalBlocks) {
        for (int i = 0; i < blk.count; ++i) {
            const int g = blk.startGlobalDof + i;
            if (diagAbsSum(g) <= 1e-12) {
                cls.isInactive[g] = true;
                cls.inactiveCount++;
            } else {
                cls.activeCount++;
            }
        }
    }

    return cls;
}

} // namespace omniframe::kernel
