#pragma once

#include "KernelTypes.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace omniframe::kernel {

struct MpcTerm {
    int globalDof = -1;
    double coefficient = 0.0;
};

struct MpcConstraint {
    std::string label;
    int slaveDof = -1;
    std::vector<MpcTerm> masterTerms;
    double constantOffset = 0.0;
};

struct NodeDofState {
    NodeId nodeId = -1;
    std::array<int, NODAL_MAX_DOFS> globalDof{-1, -1, -1, -1, -1, -1, -1};
    std::array<bool, NODAL_MAX_DOFS> activeMask{true, true, true, true, true, true, false};
    std::array<bool, NODAL_MAX_DOFS> restrained{false, false, false, false, false, false, false};
    std::array<double, NODAL_MAX_DOFS> prescribedValue{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, NODAL_MAX_DOFS> springStiffness{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

struct InternalDofBlock {
    ElementId elementId = -1;
    int startGlobalDof = -1;
    int count = 0;
};

struct DofClassification {
    std::vector<bool> isRestrained;
    std::vector<bool> isInactive;
    std::vector<bool> isMpcSlave;
    std::vector<double> prescribedValue;
    int activeCount = 0;
    int restrainedCount = 0;
    int inactiveCount = 0;
    int mpcSlaveCount = 0;
};

/**
 * Gerenciador generalizado de graus de liberdade (DOFs) do núcleo FEA:
 * - Numera DOFs nodais (3D frame/shell/solid + warping opcional) e DOFs internos/bolha
 * - Suporta condições de contorno de Dirichlet, molas e restrições lineares multi-ponto (MPC)
 * - Constrói matriz exata de transformação cinemática T_mpc (eliminação sem penalidade artificial)
 * - Classifica DOFs acoplados, rotulados inativos e mecanismos
 */
class DofManager {
public:
    void clear();

    void registerNode(NodeId id,
                      const std::array<bool, NODAL_MAX_DOFS>& mask = {true, true, true, true, true, true, false});

    void setBoundaryCondition(NodeId id, int localDof, bool fixed, double prescribed = 0.0, double spring = 0.0);

    int allocateInternalDofs(ElementId elementId, int count);

    void finalizeNumbering();

    int totalDofs() const { return m_totalDofs; }
    int nodalDofCount() const { return m_nodalDofCount; }
    int internalDofCount() const { return m_totalDofs - m_nodalDofCount; }

    int getNodeGlobalDof(NodeId id, int localDof) const;
    const NodeDofState* findNodeState(NodeId id) const;
    const std::vector<NodeDofState>& nodeStates() const { return m_nodes; }
    const InternalDofBlock* findInternalBlock(ElementId elementId) const;

    bool addMpcConstraint(const MpcConstraint& constraint, std::string* errorMsg = nullptr);
    bool addRigidLink(NodeId masterNode,
                      const Vec3& masterPos,
                      NodeId slaveNode,
                      const Vec3& slavePos,
                      bool coupleRotations = true,
                      std::string* errorMsg = nullptr);
    bool addRigidDiaphragm(NodeId masterNode,
                           const Vec3& masterPos,
                           const std::vector<std::pair<NodeId, Vec3>>& slaveNodes,
                           int normalAxis = 2,
                           std::string* errorMsg = nullptr);

    const std::vector<MpcConstraint>& mpcConstraints() const { return m_mpcs; }

    bool buildMpcTransformation(SpMat& T_mpc,
                                VecX& U_offset,
                                std::vector<int>& fullToReducedMap,
                                int& reducedDofCount,
                                std::string* errorMsg = nullptr) const;

    DofClassification classifyDofs(const SpMat& K_orig,
                                   const VecX& F_orig,
                                   std::string* mechanismError = nullptr) const;

private:
    std::vector<NodeDofState> m_nodes;
    std::unordered_map<NodeId, std::size_t> m_nodeLookup;
    std::vector<InternalDofBlock> m_internalBlocks;
    std::unordered_map<ElementId, std::size_t> m_internalLookup;
    std::vector<MpcConstraint> m_mpcs;
    int m_nodalDofCount = 0;
    int m_totalDofs = 0;
};

} // namespace omniframe::kernel
