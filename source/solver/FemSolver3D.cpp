#include "FemSolver3D.hpp"
#include "kernel/core/DofManager.hpp"
#include "kernel/elements/BeamTruss1D.hpp"
#include "kernel/elements/IFiniteElement.hpp"
#include "kernel/elements/ShellElements.hpp"
#include "kernel/elements/SolidElements.hpp"
#include "kernel/materials/LinearElasticMaterial.hpp"
#include "kernel/assembly/GlobalAssembler.hpp"
#include "kernel/solver/LinearSolverSuite.hpp"
#include "kernel/postprocessing/PostProcessor.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <stop_token>
#include <unordered_map>
#include <unordered_set>

namespace omniframe {

namespace {

bool nodeHasExternalRotationalCoupling(const StructuralModel& model, NodeId nid, ElementId selfId) {
    const Node* node = model.findNode(nid);
    if (node) {
        for (int d = 3; d < 6; ++d) {
            if (node->support.fixed[d] ||
                node->support.springStiffness[d] > 1e-9 ||
                node->support.hasPrescribed[d]) {
                return true;
            }
        }
        if (node->load.moment.normSq() > 1e-12) {
            return true;
        }
    }
    for (const auto& link : model.getRigidLinks()) {
        if (link.masterNode == nid || link.slaveNode == nid) {
            return true;
        }
    }
    for (const auto& dia : model.getRigidDiaphragms()) {
        if (dia.masterNode == nid) {
            return true;
        }
        for (NodeId sid : dia.slaveNodes) {
            if (sid == nid) {
                return true;
            }
        }
    }
    for (const auto& other : model.getElements()) {
        if (other.id == selfId || other.formulation == ElementFormulation::Truss3D) {
            continue;
        }
        if (other.nodeStart == nid) {
            if (!other.releases.start[3] || !other.releases.start[4] || !other.releases.start[5]) {
                return true;
            }
        }
        if (other.nodeEnd == nid) {
            if (!other.releases.end[3] || !other.releases.end[4] || !other.releases.end[5]) {
                return true;
            }
        }
    }
    return false;
}

Element withEffectiveTorsionalRelease(const StructuralModel& model, const Element& elem) {
    Element eff = elem;
    if (eff.releases.start[4] && eff.releases.start[5] && !eff.releases.start[3] &&
        !nodeHasExternalRotationalCoupling(model, eff.nodeStart, eff.id)) {
        eff.releases.start[3] = true;
    }
    if (eff.releases.end[4] && eff.releases.end[5] && !eff.releases.end[3] &&
        !nodeHasExternalRotationalCoupling(model, eff.nodeEnd, eff.id)) {
        eff.releases.end[3] = true;
    }
    return eff;
}

class Frame1DKernelElement final : public kernel::IFiniteElement {
public:
    Frame1DKernelElement(const StructuralModel& model, const Element& elem)
        : m_model(model), m_elem(elem), m_nodes{elem.nodeStart, elem.nodeEnd} {}

    kernel::ElementId id() const override { return m_elem.id; }
    kernel::ElementFamily family() const override {
        return (m_elem.formulation == ElementFormulation::Truss3D)
            ? kernel::ElementFamily::Truss1D
            : kernel::ElementFamily::Frame1D;
    }
    std::string typeName() const override {
        switch (m_elem.formulation) {
            case ElementFormulation::Timoshenko3D: return "Timoshenko3D";
            case ElementFormulation::Truss3D: return "Truss3D";
            default: return "EulerBernoulli3D";
        }
    }
    kernel::MaterialId materialId() const override { return m_elem.materialId; }
    const std::vector<kernel::NodeId>& nodeIds() const override { return m_nodes; }

    std::array<bool, kernel::NODAL_MAX_DOFS> requiredNodeDofMask() const override {
        return {true, true, true, true, true, true, false};
    }

    int localDofCount() const override { return 12; }

    kernel::MatX computeGlobalStiffness() const override {
        return withEffectiveTorsionalRelease(m_model, m_elem).computeGlobalStiffnessMatrix(m_model);
    }

    kernel::MatX computeGlobalMass(bool consistent) const override {
        const Material* mat = m_model.findMaterial(m_elem.materialId);
        const CrossSection* sec = m_model.findCrossSection(m_elem.sectionId);
        if (!mat || !sec) return kernel::MatX::Zero(12, 12);

        kernel::Beam1DProps p;
        p.L = m_elem.computeLength(m_model);
        if (p.L <= 1e-12) return kernel::MatX::Zero(12, 12);
        p.E = mat->E;
        p.G = mat->G();
        p.alpha = mat->alpha;
        p.rhoConsistent = mat->consistentMassDensity();
        p.gammaKNM3 = mat->gamma;
        p.A = sec->A;
        p.Iy = sec->Iy;
        p.Iz = sec->Iz;
        p.Iyz = sec->Iyz;
        p.J = sec->J;
        p.Asy = sec->Asy;
        p.Asz = sec->Asz;
        switch (m_elem.formulation) {
            case ElementFormulation::Timoshenko3D: p.formulation = kernel::Beam1DFormulation::Timoshenko3D; break;
            case ElementFormulation::Truss3D: p.formulation = kernel::Beam1DFormulation::Truss3D; break;
            default: p.formulation = kernel::Beam1DFormulation::EulerBernoulli3D; break;
        }

        const auto mLoc = consistent
            ? kernel::BeamTruss1D::computeConsistentLocalMass(p)
            : kernel::BeamTruss1D::computeLumpedLocalMass(p);
        const auto T = m_elem.computeTransformationMatrix(m_model);
        return T.transpose() * mLoc * T;
    }

    kernel::MatX computeGlobalGeometricStiffness(const kernel::VecX& elementGlobalDisp) const override {
        if (elementGlobalDisp.size() != 12) return kernel::MatX::Zero(12, 12);
        const Material* mat = m_model.findMaterial(m_elem.materialId);
        const CrossSection* sec = m_model.findCrossSection(m_elem.sectionId);
        if (!mat || !sec) return kernel::MatX::Zero(12, 12);

        const Element effElem = withEffectiveTorsionalRelease(m_model, m_elem);
        const double L = effElem.computeLength(m_model);
        if (L <= 1e-12) return kernel::MatX::Zero(12, 12);

        const auto T = effElem.computeTransformationMatrix(m_model);
        const Eigen::Matrix<double, 12, 1> uLocNodal = T * elementGlobalDisp;
        const Eigen::Matrix<double, 12, 1> uRec = effElem.recoverLocalElementDisplacements(m_model, uLocNodal);
        const Eigen::Matrix<double, 12, 12> k0 = effElem.computeUncondensedLocalStiffnessMatrix(m_model);
        const Eigen::Matrix<double, 12, 1> f0 = effElem.computeUncondensedLocalFixedEndForces(m_model);
        Eigen::Matrix<double, 12, 1> fEndLoc = k0 * uRec + f0;
        for (int k = 0; k < 6; ++k) {
            if (effElem.releases.start[k]) fEndLoc(k) = 0.0;
            if (effElem.releases.end[k]) fEndLoc(k + 6) = 0.0;
        }
        // Consistente com releases axiais, carregamento axial distribuído e variação térmica:
        // em equilíbrio, N(0) = -fEndLoc(0) e N(L) = +fEndLoc(6); tiramos a média ao longo do vão.
        double N_axial = 0.5 * (-fEndLoc(0) + fEndLoc(6));
        if (effElem.releases.start[0] || effElem.releases.end[0]) {
            N_axial = effElem.releases.start[0] ? fEndLoc(6) : -fEndLoc(0);
            if (effElem.releases.start[0] && effElem.releases.end[0]) {
                N_axial = 0.0;
            }
        }

        kernel::Beam1DFormulation form = kernel::Beam1DFormulation::EulerBernoulli3D;
        if (effElem.formulation == ElementFormulation::Timoshenko3D) form = kernel::Beam1DFormulation::Timoshenko3D;
        else if (effElem.formulation == ElementFormulation::Truss3D) form = kernel::Beam1DFormulation::Truss3D;

        const auto kgLoc = kernel::BeamTruss1D::computeLocalGeometricStiffness(N_axial, L, form);
        return T.transpose() * kgLoc * T;
    }

    kernel::VecX computeGlobalLoadVector(const kernel::ElementLoadInput& load) const override {
        Element copy = withEffectiveTorsionalRelease(m_model, m_elem);
        copy.distLoad.active = load.hasDistributed;
        copy.distLoad.coordSystem = (load.coordSystem == 1) ? CoordSystem::Local : CoordSystem::Global;
        copy.distLoad.w = {load.distributedForce.x(), load.distributedForce.y(), load.distributedForce.z()};
        copy.thermalLoad.active = load.hasThermal;
        copy.thermalLoad.deltaT = load.deltaT;

        kernel::VecX fEq = -copy.computeGlobalFixedEndForces(m_model);
        if (load.hasBodyGravity) {
            const Material* mat = m_model.findMaterial(m_elem.materialId);
            const CrossSection* sec = m_model.findCrossSection(m_elem.sectionId);
            if (mat && sec) {
                Element swCopy = withEffectiveTorsionalRelease(m_model, m_elem);
                swCopy.thermalLoad.active = false;
                swCopy.distLoad.active = true;
                swCopy.distLoad.coordSystem = CoordSystem::Global;
                const double wPerM = mat->gamma * sec->A;
                swCopy.distLoad.w = {
                    wPerM * load.gravityAccel.x(),
                    wPerM * load.gravityAccel.y(),
                    wPerM * load.gravityAccel.z()
                };
                fEq -= swCopy.computeGlobalFixedEndForces(m_model);
            }
        }
        return fEq;
    }

private:
    const StructuralModel& m_model;
    const Element& m_elem;
    std::vector<kernel::NodeId> m_nodes;
};

struct ContinuumElemRecord {
    size_t meshElemIdx = 0;
    ElementId kernelElemId = 0;
    std::shared_ptr<kernel::IFiniteElement> elemPtr;
    std::shared_ptr<kernel::ShellElement> shellPtr;
    std::shared_ptr<kernel::SolidElement> solidPtr;
};

struct UnifiedSolverMapping {
    std::unordered_map<NodeId, kernel::NodeId> meshNodeToKernelId;
    std::vector<kernel::NodeId> kernelNodeForMeshIdx;
    std::vector<bool> meshNodeMergedWith1D;
    std::vector<ContinuumElemRecord> continuumElems;
};

std::unordered_map<MaterialId, std::shared_ptr<const kernel::IConstitutiveModel>>
buildKernelMaterialMap(const StructuralModel& model)
{
    std::unordered_map<MaterialId, std::shared_ptr<const kernel::IConstitutiveModel>> matMap;
    matMap.reserve(model.getMaterials().size());
    for (const auto& m : model.getMaterials()) {
        matMap[m.id] = std::make_shared<kernel::LinearElasticIsotropic>(
            m.id, m.name, m.E, m.nu, m.alpha, m.rho, m.gamma);
    }
    return matMap;
}

bool setupDofManagerFromModel(const StructuralModel& model,
                              kernel::DofManager& dofMgr,
                              UnifiedSolverMapping& mapping,
                              int& outRestrainedCount,
                              int& outSpringCount,
                              std::string* errMsg)
{
    dofMgr.clear();
    mapping = UnifiedSolverMapping{};
    outRestrainedCount = 0;
    outSpringCount = 0;

    const std::array<bool, kernel::NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};
    const std::array<bool, kernel::NODAL_MAX_DOFS> mask3{true, true, true, false, false, false, false};

    struct NodeBcState {
        std::array<bool, 6> fixed{false, false, false, false, false, false};
        std::array<double, 6> prescribed{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        std::array<double, 6> spring{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        bool needsRot = true;
    };
    std::unordered_map<kernel::NodeId, NodeBcState> bcMap;
    std::vector<kernel::NodeId> orderedKernelNodes;

    // Hash espacial de nós 1D para acoplamento automático 1D + 2D/3D em coordenadas coincidentes
    auto bucketKey = [](long long bx, long long by, long long bz) -> std::string {
        return std::to_string(bx) + ":" + std::to_string(by) + ":" + std::to_string(bz);
    };
    std::unordered_map<std::string, std::vector<size_t>> barNodeBuckets;
    const auto& nodes1D = model.getNodes();
    std::unordered_set<kernel::NodeId> usedKernelIds;
    usedKernelIds.reserve(nodes1D.size() + model.surfaceMesh().nodes.size());
    kernel::NodeId maxKernelId = 0;

    for (size_t i = 0; i < nodes1D.size(); ++i) {
        const auto& n = nodes1D[i];
        usedKernelIds.insert(n.id);
        maxKernelId = std::max(maxKernelId, n.id);
        orderedKernelNodes.push_back(n.id);

        NodeBcState st;
        st.needsRot = true;
        for (int d = 0; d < 6; ++d) {
            const bool isFixed = n.support.hasPrescribed[d] || n.support.fixed[d];
            const double val = n.support.hasPrescribed[d] ? n.support.prescribedValue[d] : 0.0;
            const double spring = n.support.springStiffness[d];
            if (!std::isfinite(val) || !std::isfinite(spring) || spring < 0.0) {
                if (errMsg) *errMsg = "Rigidez de mola ou recalque inválido no nó " + std::to_string(n.id) + ".";
                return false;
            }
            st.fixed[d] = isFixed;
            st.prescribed[d] = val;
            st.spring[d] = (spring > 1e-9) ? spring : 0.0;
        }
        bcMap[n.id] = st;

        constexpr double kMaxPhysCoordM = 1e9;
        if (!std::isfinite(n.position.x) || !std::isfinite(n.position.y) || !std::isfinite(n.position.z) ||
            std::abs(n.position.x) > kMaxPhysCoordM || std::abs(n.position.y) > kMaxPhysCoordM ||
            std::abs(n.position.z) > kMaxPhysCoordM) {
            if (errMsg) *errMsg = "Coordenadas fora dos limites fisicos (<= 1e9 m) no no " + std::to_string(n.id) + ".";
            return false;
        }
        const long long bx = std::llround(n.position.x * 1e4);
        const long long by = std::llround(n.position.y * 1e4);
        const long long bz = std::llround(n.position.z * 1e4);
        barNodeBuckets[bucketKey(bx, by, bz)].push_back(i);
    }

    // Verifica quais nós da malha precisam de DOFs rotacionais (cascas, Line2, ou rotação fixa/mola/momento)
    const auto& surf = model.surfaceMesh();
    std::unordered_set<NodeId> meshNodesWithRot;
    for (const auto& me : surf.elements) {
        if (!me.isSolid()) {
            for (NodeId nid : me.nodeIds) meshNodesWithRot.insert(nid);
        }
    }

    mapping.kernelNodeForMeshIdx.resize(surf.nodes.size(), -1);
    mapping.meshNodeMergedWith1D.assign(surf.nodes.size(), false);
    kernel::NodeId nextFreeId = std::max(maxKernelId + 1, 1);

    for (size_t i = 0; i < surf.nodes.size(); ++i) {
        const auto& mn = surf.nodes[i];
        for (int d = 0; d < 6; ++d) {
            if (!std::isfinite(mn.prescribedDisplacement[d]) ||
                !std::isfinite(mn.springStiffness[d]) || mn.springStiffness[d] < 0.0) {
                if (errMsg) *errMsg = "Rigidez de mola ou recalque inválido no nó de malha M" + std::to_string(mn.id) + ".";
                return false;
            }
        }

        // Busca nó 1D coincidente (tolerância 1e-5 m -> dist^2 <= 1e-10)
        constexpr double kMaxPhysCoordM = 1e9;
        if (!std::isfinite(mn.position.x()) || !std::isfinite(mn.position.y()) || !std::isfinite(mn.position.z()) ||
            std::abs(mn.position.x()) > kMaxPhysCoordM || std::abs(mn.position.y()) > kMaxPhysCoordM ||
            std::abs(mn.position.z()) > kMaxPhysCoordM) {
            if (errMsg) *errMsg = "Coordenadas fora dos limites fisicos (<= 1e9 m) no no de malha M" + std::to_string(mn.id) + ".";
            return false;
        }
        kernel::NodeId matched1D = -1;
        if (!nodes1D.empty()) {
            const long long bx = std::llround(mn.position.x() * 1e4);
            const long long by = std::llround(mn.position.y() * 1e4);
            const long long bz = std::llround(mn.position.z() * 1e4);
            double bestDist2 = 1e-10;
            for (long long dx = -1; dx <= 1; ++dx) {
                for (long long dy = -1; dy <= 1; ++dy) {
                    for (long long dz = -1; dz <= 1; ++dz) {
                        auto bit = barNodeBuckets.find(bucketKey(bx + dx, by + dy, bz + dz));
                        if (bit == barNodeBuckets.end()) continue;
                        for (size_t idx1D : bit->second) {
                            const auto& n1 = nodes1D[idx1D];
                            const double ddx = n1.position.x - mn.position.x();
                            const double ddy = n1.position.y - mn.position.y();
                            const double ddz = n1.position.z - mn.position.z();
                            const double d2 = ddx * ddx + ddy * ddy + ddz * ddz;
                            if (d2 <= bestDist2) {
                                bestDist2 = d2;
                                matched1D = n1.id;
                            }
                        }
                    }
                }
            }
        }

        kernel::NodeId kid = -1;
        if (matched1D >= 0) {
            kid = matched1D;
            mapping.meshNodeMergedWith1D[i] = true;
            auto& st = bcMap[kid];
            for (int d = 0; d < 6; ++d) {
                if (mn.fixed[d]) {
                    st.fixed[d] = true;
                    if (std::abs(mn.prescribedDisplacement[d]) > 0.0) {
                        st.prescribed[d] = mn.prescribedDisplacement[d];
                    }
                }
                if (mn.springStiffness[d] > 1e-9) {
                    st.spring[d] += mn.springStiffness[d];
                }
            }
        } else {
            if (mn.id > 0 && usedKernelIds.find(mn.id) == usedKernelIds.end()) {
                kid = mn.id;
            } else {
                while (usedKernelIds.find(nextFreeId) != usedKernelIds.end()) ++nextFreeId;
                kid = nextFreeId++;
            }
            usedKernelIds.insert(kid);
            maxKernelId = std::max(maxKernelId, kid);
            orderedKernelNodes.push_back(kid);

            NodeBcState st;
            st.needsRot = (meshNodesWithRot.find(mn.id) != meshNodesWithRot.end())
                       || mn.nodalMoment.squaredNorm() > 1e-18;
            for (int d = 3; d < 6; ++d) {
                if (mn.fixed[d] || mn.springStiffness[d] > 1e-9 || std::abs(mn.prescribedDisplacement[d]) > 0.0) {
                    st.needsRot = true;
                }
            }
            for (int d = 0; d < 6; ++d) {
                st.fixed[d] = mn.fixed[d] || (std::abs(mn.prescribedDisplacement[d]) > 0.0);
                st.prescribed[d] = mn.prescribedDisplacement[d];
                st.spring[d] = (mn.springStiffness[d] > 1e-9) ? mn.springStiffness[d] : 0.0;
            }
            bcMap[kid] = st;
        }
        mapping.meshNodeToKernelId[mn.id] = kid;
        mapping.kernelNodeForMeshIdx[i] = kid;
    }

    for (kernel::NodeId kid : orderedKernelNodes) {
        const auto& st = bcMap.at(kid);
        dofMgr.registerNode(kid, st.needsRot ? mask6 : mask3);
        const int maxDof = st.needsRot ? 6 : 3;
        for (int d = 0; d < maxDof; ++d) {
            if (st.fixed[d]) outRestrainedCount++;
            else if (st.spring[d] > 1e-9) outSpringCount++;
            dofMgr.setBoundaryCondition(kid, d, st.fixed[d], st.prescribed[d], st.spring[d]);
        }
    }
    dofMgr.finalizeNumbering();

    for (const auto& link : model.getRigidLinks()) {
        const Node* nm = model.findNode(link.masterNode);
        const Node* ns = model.findNode(link.slaveNode);
        if (!nm || !ns) {
            if (errMsg) *errMsg = "Ligação rígida referencia nó inexistente.";
            return false;
        }
        if (!dofMgr.addRigidLink(link.masterNode,
                                 {nm->position.x, nm->position.y, nm->position.z},
                                 link.slaveNode,
                                 {ns->position.x, ns->position.y, ns->position.z},
                                 link.coupleRotations,
                                 errMsg)) {
            return false;
        }
    }

    for (const auto& dia : model.getRigidDiaphragms()) {
        const Node* nm = model.findNode(dia.masterNode);
        if (!nm) {
            if (errMsg) *errMsg = "Diafragma rígido referencia nó mestre inexistente.";
            return false;
        }
        std::vector<std::pair<kernel::NodeId, kernel::Vec3>> slaves;
        slaves.reserve(dia.slaveNodes.size());
        for (NodeId sid : dia.slaveNodes) {
            const Node* ns = model.findNode(sid);
            if (!ns) {
                if (errMsg) *errMsg = "Diafragma rígido referencia nó escravo inexistente.";
                return false;
            }
            slaves.emplace_back(sid, kernel::Vec3{ns->position.x, ns->position.y, ns->position.z});
        }
        if (!dofMgr.addRigidDiaphragm(dia.masterNode,
                                      {nm->position.x, nm->position.y, nm->position.z},
                                      slaves,
                                      dia.normalAxis,
                                      errMsg)) {
            return false;
        }
    }

    return true;
}

class Line2MeshKernelElement final : public kernel::IFiniteElement {
public:
    Line2MeshKernelElement(kernel::ElementId eid,
                           std::vector<kernel::NodeId> nodeIds,
                           const std::array<kernel::Vec3, 2>& coords,
                           const Material& mat,
                           const CrossSection& sec)
        : m_id(eid), m_nodes(std::move(nodeIds)), m_coords(coords), m_mat(mat), m_sec(sec)
    {
        const kernel::Vec3 d = m_coords[1] - m_coords[0];
        m_props.L = d.norm();
        m_props.E = mat.E;
        m_props.G = mat.G();
        m_props.alpha = mat.alpha;
        m_props.rhoConsistent = mat.consistentMassDensity();
        m_props.gammaKNM3 = mat.gamma;
        m_props.A = sec.A;
        m_props.Iy = sec.Iy;
        m_props.Iz = sec.Iz;
        m_props.Iyz = sec.Iyz;
        m_props.J = sec.J;
        m_props.Asy = sec.Asy;
        m_props.Asz = sec.Asz;
        m_props.formulation = kernel::Beam1DFormulation::EulerBernoulli3D;
        m_R12 = buildRot12();
    }

    kernel::ElementId id() const override { return m_id; }
    kernel::ElementFamily family() const override { return kernel::ElementFamily::Frame1D; }
    std::string typeName() const override { return "Line2"; }
    kernel::MaterialId materialId() const override { return m_mat.id; }
    const std::vector<kernel::NodeId>& nodeIds() const override { return m_nodes; }
    std::array<bool, kernel::NODAL_MAX_DOFS> requiredNodeDofMask() const override {
        return {true, true, true, true, true, true, false};
    }
    int localDofCount() const override { return 12; }

    kernel::MatX computeGlobalStiffness() const override {
        if (m_props.L <= 1e-12) return kernel::MatX::Zero(12, 12);
        return m_R12.transpose() * kernel::BeamTruss1D::computeUncondensedLocalStiffness(m_props) * m_R12;
    }
    kernel::MatX computeGlobalMass(bool consistent) const override {
        if (m_props.L <= 1e-12) return kernel::MatX::Zero(12, 12);
        const auto mLoc = consistent
            ? kernel::BeamTruss1D::computeConsistentLocalMass(m_props)
            : kernel::BeamTruss1D::computeLumpedLocalMass(m_props);
        return m_R12.transpose() * mLoc * m_R12;
    }
    kernel::MatX computeGlobalGeometricStiffness(const kernel::VecX& uGlob) const override {
        if (uGlob.size() != 12 || m_props.L <= 1e-12) return kernel::MatX::Zero(12, 12);
        const Eigen::Matrix<double, 12, 1> uLoc = m_R12 * uGlob;
        const double N = m_props.E * m_props.A * (uLoc(6) - uLoc(0)) / m_props.L;
        return m_R12.transpose() * kernel::BeamTruss1D::computeLocalGeometricStiffness(N, m_props.L, m_props.formulation) * m_R12;
    }
    kernel::VecX computeGlobalLoadVector(const kernel::ElementLoadInput& load) const override {
        if (m_props.L <= 1e-12) return kernel::VecX::Zero(12);
        kernel::Vec3 qGlob = kernel::Vec3::Zero();
        kernel::Vec3 qLoc = kernel::Vec3::Zero();
        if (load.hasDistributed) {
            if (load.coordSystem == 1) qLoc += load.distributedForce;
            else qGlob += load.distributedForce;
        }
        if (load.hasBodyGravity) {
            qGlob += (m_props.gammaKNM3 * m_props.A) * load.gravityAccel;
        }
        qLoc += m_R3 * qGlob;
        const double dT = load.hasThermal ? load.deltaT : 0.0;
        return m_R12.transpose() * kernel::BeamTruss1D::computeConsistentLocalLoadVector(m_props, qLoc, dT);
    }

private:
    kernel::ElementId m_id;
    std::vector<kernel::NodeId> m_nodes;
    std::array<kernel::Vec3, 2> m_coords;
    Material m_mat;
    CrossSection m_sec;
    kernel::Beam1DProps m_props;
    kernel::Mat3 m_R3 = kernel::Mat3::Identity();
    kernel::MatX m_R12 = kernel::MatX::Zero(12, 12);

    kernel::MatX buildRot12() {
        kernel::Vec3 vx = m_coords[1] - m_coords[0];
        const double len = vx.norm();
        if (!std::isfinite(len) || len <= 1e-12) vx = kernel::Vec3(1.0, 0.0, 0.0);
        else vx /= len;

        // Idêntico a Element::computeRotationMatrix3x3 (inclui barras verticais)
        m_R3.setZero();
        const double horizNorm = std::hypot(vx.x(), vx.y());
        if (horizNorm < 1e-6) {
            const double sgn = (vx.z() > 0.0) ? 1.0 : -1.0;
            m_R3(0, 2) = sgn;
            m_R3(1, 1) = 1.0;
            m_R3(2, 0) = -sgn;
        } else {
            const kernel::Vec3 k(0.0, 0.0, 1.0);
            const kernel::Vec3 vy = k.cross(vx).normalized();
            const kernel::Vec3 vz = vx.cross(vy).normalized();
            m_R3.row(0) = vx.transpose();
            m_R3.row(1) = vy.transpose();
            m_R3.row(2) = vz.transpose();
        }
        kernel::MatX T = kernel::MatX::Zero(12, 12);
        for (int b = 0; b < 4; ++b) T.block<3, 3>(3 * b, 3 * b) = m_R3;
        return T;
    }
};

bool buildKernelElements(const StructuralModel& model,
                         UnifiedSolverMapping& mapping,
                         std::vector<std::shared_ptr<kernel::IFiniteElement>>& outElements,
                         std::string* errMsg = nullptr)
{
    outElements.clear();
    mapping.continuumElems.clear();
    const auto& barElems = model.getElements();
    const auto& surf = model.surfaceMesh();
    outElements.reserve(barElems.size() + surf.elements.size());

    std::unordered_set<ElementId> usedElemIds;
    usedElemIds.reserve(barElems.size() + surf.elements.size());
    ElementId maxElemId = 0;

    for (const auto& elem : barElems) {
        usedElemIds.insert(elem.id);
        maxElemId = std::max(maxElemId, elem.id);
        outElements.push_back(std::make_shared<Frame1DKernelElement>(model, elem));
    }

    if (surf.elements.empty()) {
        return true;
    }

    const auto matMap = buildKernelMaterialMap(model);
    std::unordered_map<NodeId, kernel::Vec3> meshPosMap;
    meshPosMap.reserve(surf.nodes.size());
    for (const auto& mn : surf.nodes) {
        meshPosMap[mn.id] = mn.position;
    }

    ElementId nextFreeElemId = std::max(maxElemId + 1, 1);
    mapping.continuumElems.reserve(surf.elements.size());

    for (size_t idx = 0; idx < surf.elements.size(); ++idx) {
        const auto& me = surf.elements[idx];
        auto matIt = matMap.find(me.materialId);
        if (matIt == matMap.end() || !matIt->second) {
            if (errMsg) *errMsg = "Elemento de malha E" + std::to_string(me.id) + " referencia material inválido.";
            return false;
        }

        ElementId keid = -1;
        if (me.id > 0 && usedElemIds.find(me.id) == usedElemIds.end()) {
            keid = me.id;
        } else {
            while (usedElemIds.find(nextFreeElemId) != usedElemIds.end()) ++nextFreeElemId;
            keid = nextFreeElemId++;
        }
        usedElemIds.insert(keid);
        maxElemId = std::max(maxElemId, keid);

        std::vector<kernel::NodeId> kNodeIds;
        std::vector<kernel::Vec3> kCoords;
        kNodeIds.reserve(me.nodeIds.size());
        kCoords.reserve(me.nodeIds.size());
        for (NodeId nid : me.nodeIds) {
            auto kit = mapping.meshNodeToKernelId.find(nid);
            auto pit = meshPosMap.find(nid);
            if (kit == mapping.meshNodeToKernelId.end() || pit == meshPosMap.end()) {
                if (errMsg) *errMsg = "Elemento de malha E" + std::to_string(me.id) + " referencia nó inexistente.";
                return false;
            }
            kNodeIds.push_back(kit->second);
            kCoords.push_back(pit->second);
        }

        try {
            ContinuumElemRecord rec;
            rec.meshElemIdx = idx;
            rec.kernelElemId = keid;

            switch (me.type) {
                case kernel::MeshCellType::Tri3:
                    rec.shellPtr = std::make_shared<kernel::ShellElement>(
                        keid, kernel::ShellElementType::Tri3MITC3, kNodeIds, kCoords, matIt->second, me.thickness);
                    rec.elemPtr = rec.shellPtr;
                    break;
                case kernel::MeshCellType::Quad4:
                    rec.shellPtr = std::make_shared<kernel::ShellElement>(
                        keid, kernel::ShellElementType::Quad4MITC4, kNodeIds, kCoords, matIt->second, me.thickness);
                    rec.elemPtr = rec.shellPtr;
                    break;
                case kernel::MeshCellType::Quad8:
                    rec.shellPtr = std::make_shared<kernel::ShellElement>(
                        keid, kernel::ShellElementType::Quad8Serendipity, kNodeIds, kCoords, matIt->second, me.thickness);
                    rec.elemPtr = rec.shellPtr;
                    break;
                case kernel::MeshCellType::Tet4:
                    rec.solidPtr = std::make_shared<kernel::SolidElement>(
                        keid, kernel::SolidElementType::Tet4, kNodeIds, kCoords, matIt->second);
                    rec.elemPtr = rec.solidPtr;
                    break;
                case kernel::MeshCellType::Tet10:
                    rec.solidPtr = std::make_shared<kernel::SolidElement>(
                        keid, kernel::SolidElementType::Tet10, kNodeIds, kCoords, matIt->second);
                    rec.elemPtr = rec.solidPtr;
                    break;
                case kernel::MeshCellType::Hex8:
                    rec.solidPtr = std::make_shared<kernel::SolidElement>(
                        keid, kernel::SolidElementType::Hex8BBar, kNodeIds, kCoords, matIt->second);
                    rec.elemPtr = rec.solidPtr;
                    break;
                case kernel::MeshCellType::Hex20:
                    rec.solidPtr = std::make_shared<kernel::SolidElement>(
                        keid, kernel::SolidElementType::Hex20Serendipity, kNodeIds, kCoords, matIt->second);
                    rec.elemPtr = rec.solidPtr;
                    break;
                case kernel::MeshCellType::Line2: {
                    const Material* mat = model.findMaterial(me.materialId);
                    const CrossSection* secPtr = (me.sectionId > 0)
                        ? model.findCrossSection(me.sectionId)
                        : (model.getCrossSections().empty() ? nullptr : &model.getCrossSections().front());
                    CrossSection fallbackSec = secPtr ? *secPtr : CrossSection::makeGeneric(999, "Line2Default", 0.01, 1e-5, 1e-5, 1e-5);
                    if (!mat || kCoords.size() < 2) {
                        if (errMsg) *errMsg = "Elemento Line2 inválido na malha.";
                        return false;
                    }
                    rec.elemPtr = std::make_shared<Line2MeshKernelElement>(
                        keid, std::vector<kernel::NodeId>{kNodeIds[0], kNodeIds[1]},
                        std::array<kernel::Vec3, 2>{kCoords[0], kCoords[1]}, *mat, fallbackSec);
                    break;
                }
            }
            if (rec.elemPtr) {
                outElements.push_back(rec.elemPtr);
                mapping.continuumElems.push_back(std::move(rec));
            }
        } catch (const std::exception& ex) {
            if (errMsg) *errMsg = std::string("Falha geométrica no elemento E") + std::to_string(me.id) + ": " + ex.what();
            return false;
        }
    }
    return true;
}

void extractLegacyLoadMaps(
    const StructuralModel& model,
    const UnifiedSolverMapping& mapping,
    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput>& elemLoads,
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>>& nodalLoads)
{
    elemLoads.clear();
    nodalLoads.clear();
    using NodalVec = Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>;
    for (const auto& n : model.getNodes()) {
        NodalVec& fv = nodalLoads.try_emplace(n.id, NodalVec::Zero()).first->second;
        fv(0) += n.load.force.x;
        fv(1) += n.load.force.y;
        fv(2) += n.load.force.z;
        fv(3) += n.load.moment.x;
        fv(4) += n.load.moment.y;
        fv(5) += n.load.moment.z;
    }
    for (size_t i = 0; i < model.surfaceMesh().nodes.size(); ++i) {
        const auto& mn = model.surfaceMesh().nodes[i];
        if (!mn.hasLoad()) continue;
        const kernel::NodeId kid = mapping.kernelNodeForMeshIdx[i];
        NodalVec& fv = nodalLoads.try_emplace(kid, NodalVec::Zero()).first->second;
        fv(0) += mn.nodalForce.x();
        fv(1) += mn.nodalForce.y();
        fv(2) += mn.nodalForce.z();
        fv(3) += mn.nodalMoment.x();
        fv(4) += mn.nodalMoment.y();
        fv(5) += mn.nodalMoment.z();
    }
    for (const auto& e : model.getElements()) {
        kernel::ElementLoadInput inp;
        inp.hasDistributed = e.distLoad.active;
        inp.coordSystem = (e.distLoad.coordSystem == CoordSystem::Local) ? 1 : 0;
        inp.distributedForce = {e.distLoad.w.x, e.distLoad.w.y, e.distLoad.w.z};
        inp.hasThermal = e.thermalLoad.active;
        inp.deltaT = e.thermalLoad.deltaT;
        elemLoads[e.id] = inp;
    }
    const auto& meshElems = model.surfaceMesh().elements;
    for (const auto& rec : mapping.continuumElems) {
        const auto& me = meshElems[rec.meshElemIdx];
        kernel::ElementLoadInput inp;
        inp.normalPressure = me.normalPressure;
        if (me.distributedLoad.squaredNorm() > 0.0) {
            inp.hasDistributed = true;
            inp.coordSystem = 0;
            inp.distributedForce = me.distributedLoad;
        }
        if (std::abs(me.deltaT) > 0.0) {
            inp.hasThermal = true;
            inp.deltaT = me.deltaT;
        }
        if (me.includeSelfWeight) {
            inp.hasBodyGravity = true;
            inp.gravityAccel = kernel::Vec3{0.0, 0.0, -1.0};
        }
        elemLoads[rec.kernelElemId] = inp;
    }
}

void extractCaseLoadMaps(
    const StructuralModel& model,
    const UnifiedSolverMapping& mapping,
    const kernel::LoadCase& lc,
    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput>& elemLoads,
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>>& nodalLoads)
{
    elemLoads.clear();
    nodalLoads.clear();
    using NodalVec = Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>;
    std::unordered_set<NodeId> barNodeIds;
    barNodeIds.reserve(model.getNodes().size());
    for (const auto& n : model.getNodes()) barNodeIds.insert(n.id);

    for (const auto& nEntry : lc.nodalLoads) {
        kernel::NodeId targetKid = nEntry.nodeId;
        if (barNodeIds.find(targetKid) == barNodeIds.end()) {
            if (auto it = mapping.meshNodeToKernelId.find(targetKid); it != mapping.meshNodeToKernelId.end()) {
                targetKid = it->second;
            }
        }
        NodalVec& vec = nodalLoads.try_emplace(targetKid, NodalVec::Zero()).first->second;
        for (int d = 0; d < 6; ++d) {
            vec(d) += nEntry.forceMoment[d];
        }
    }
    for (const auto& eEntry : lc.elementLoads) {
        elemLoads[eEntry.elementId] = eEntry.load;
    }
    if (lc.selfWeight.enabled && std::abs(lc.selfWeight.factor) > 0.0) {
        const kernel::Vec3 gAcc = lc.selfWeight.direction * lc.selfWeight.factor;
        for (const auto& e : model.getElements()) {
            auto& inp = elemLoads[e.id];
            inp.hasBodyGravity = true;
            inp.gravityAccel = gAcc;
        }
        for (const auto& rec : mapping.continuumElems) {
            auto& inp = elemLoads[rec.kernelElemId];
            inp.hasBodyGravity = true;
            inp.gravityAccel = gAcc;
        }
    }
}

bool buildFrame1DNonlinearSpecs(
    const StructuralModel& model,
    std::vector<kernel::Frame1DNonlinearElementSpec>& outSpecs,
    std::string* errMsg)
{
    outSpecs.clear();
    outSpecs.reserve(model.getElements().size());
    for (const auto& elem : model.getElements()) {
        const Node* n1 = model.findNode(elem.nodeStart);
        const Node* n2 = model.findNode(elem.nodeEnd);
        const Material* mat = model.findMaterial(elem.materialId);
        const CrossSection* sec = model.findCrossSection(elem.sectionId);
        if (!n1 || !n2 || !mat || !sec) {
            if (errMsg) *errMsg = "Referência inválida em elemento na análise não linear.";
            return false;
        }

        const Element effElem = withEffectiveTorsionalRelease(model, elem);
        kernel::Frame1DNonlinearElementSpec s;
        s.id = elem.id;
        s.nodeStart = elem.nodeStart;
        s.nodeEnd = elem.nodeEnd;
        s.X1 = {n1->position.x, n1->position.y, n1->position.z};
        s.X2 = {n2->position.x, n2->position.y, n2->position.z};
        // Element::computeRotationMatrix3x3 leva global -> local; R0 tem eixos locais nas colunas
        s.R0 = elem.computeRotationMatrix3x3(model).transpose();

        s.props.L = elem.computeLength(model);
        s.props.E = mat->E;
        s.props.G = mat->G();
        s.props.alpha = mat->alpha;
        s.props.rhoConsistent = mat->consistentMassDensity();
        s.props.gammaKNM3 = mat->gamma;
        s.props.A = sec->A;
        s.props.Iy = sec->Iy;
        s.props.Iz = sec->Iz;
        s.props.Iyz = sec->Iyz;
        s.props.J = sec->J;
        s.props.Asy = sec->Asy;
        s.props.Asz = sec->Asz;
        switch (elem.formulation) {
            case ElementFormulation::Timoshenko3D: s.props.formulation = kernel::Beam1DFormulation::Timoshenko3D; break;
            case ElementFormulation::Truss3D: s.props.formulation = kernel::Beam1DFormulation::Truss3D; break;
            default: s.props.formulation = kernel::Beam1DFormulation::EulerBernoulli3D; break;
        }
        s.releaseStart = effElem.releases.start;
        s.releaseEnd = effElem.releases.end;

        if (mat->isPlastic) {
            s.hasPlasticity = true;
            s.plasticParams.E = mat->E;
            s.plasticParams.nu = mat->nu;
            s.plasticParams.sigmaY0 = mat->sigmaY;
            s.plasticParams.H_iso = mat->H_iso;
            s.plasticParams.H_kin = mat->H_kin;
            s.plasticParams.alphaThermal = mat->alpha;
            s.plasticParams.rhoKgM3 = mat->rho;
            s.plasticParams.gammaKNM3 = mat->gamma;
        }
        outSpecs.push_back(std::move(s));
    }
    return true;
}

kernel::VecX computeReducedExternalReactions(
    const kernel::ReducedLinearSystem& redSys,
    const kernel::VecX& R_full)
{
    if (redSys.hasMpc && redSys.T_mpc.rows() == R_full.size()) {
        return (redSys.T_mpc.transpose() * R_full).eval();
    }
    return R_full;
}

double externalRestrainedReactionAt(
    int g,
    const kernel::DofClassification& cls,
    const kernel::ReducedLinearSystem& redSys,
    const kernel::VecX& R_ext_rd)
{
    if (g < 0 || g >= static_cast<int>(cls.isRestrained.size()) || !cls.isRestrained[g]) {
        return 0.0;
    }
    const int idx = (redSys.hasMpc && g < static_cast<int>(redSys.fullToReducedMap.size()))
        ? redSys.fullToReducedMap[g]
        : g;
    if (idx >= 0 && idx < R_ext_rd.size()) {
        return R_ext_rd(idx);
    }
    return 0.0;
}

void populatePostProcessingFromVectors(
    StructuralModel& model,
    const kernel::DofManager& dofMgr,
    const kernel::DofClassification& cls,
    const kernel::ReducedLinearSystem& redSys,
    const UnifiedSolverMapping& mapping,
    const std::unordered_map<kernel::ElementId, kernel::ElementLoadInput>& elemLoads,
    const std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>>& nodalLoads,
    const kernel::VecX& U_full,
    const kernel::VecX& R_full,
    const kernel::SpMat& K_orig,
    SolverResult& result,
    const kernel::LoadCase* activeLoadCase = nullptr)
{
    auto& nodes = model.getNodes();
    auto& elements = model.getElements();
    auto& surf = model.surfaceMesh();
    const int nNodes = static_cast<int>(nodes.size());
    const kernel::VecX R_ext_rd = computeReducedExternalReactions(redSys, R_full);

    std::unordered_map<NodeId, int> nodeIndex;
    for (int i = 0; i < nNodes; ++i) nodeIndex[nodes[i].id] = i;

    double maxDisp = 0.0;
    for (int i = 0; i < nNodes; ++i) {
        const auto& sup = nodes[i].support;
        for (int d = 0; d < 6; ++d) {
            const int g = dofMgr.getNodeGlobalDof(nodes[i].id, d);
            nodes[i].displacement[d] = (g >= 0 && g < U_full.size()) ? U_full(g) : 0.0;
            if (g >= 0 && cls.isRestrained[g]) {
                nodes[i].reaction[d] = externalRestrainedReactionAt(g, cls, redSys, R_ext_rd);
            } else if (sup.springStiffness[d] > 1e-9) {
                nodes[i].reaction[d] = -sup.springStiffness[d] * nodes[i].displacement[d];
            } else {
                nodes[i].reaction[d] = 0.0;
            }
        }
        maxDisp = std::max(maxDisp, nodes[i].displacementNorm());
    }

    // Deslocamentos e reações nodais da malha contínua 2D/3D
    std::unordered_map<NodeId, size_t> meshNodePosIdx;
    meshNodePosIdx.reserve(surf.nodes.size());
    for (size_t i = 0; i < surf.nodes.size(); ++i) {
        meshNodePosIdx[surf.nodes[i].id] = i;
        auto& mn = surf.nodes[i];
        const kernel::NodeId kid = mapping.kernelNodeForMeshIdx[i];
        for (int d = 0; d < 6; ++d) {
            const int g = dofMgr.getNodeGlobalDof(kid, d);
            mn.displacement[d] = (g >= 0 && g < U_full.size()) ? U_full(g) : 0.0;
            if (g >= 0 && cls.isRestrained[g]) {
                mn.reaction[d] = externalRestrainedReactionAt(g, cls, redSys, R_ext_rd);
            } else if (mn.springStiffness[d] > 1e-9) {
                mn.reaction[d] = -mn.springStiffness[d] * mn.displacement[d];
            } else {
                mn.reaction[d] = 0.0;
            }
        }
        mn.hasResults = true;
        maxDisp = std::max(maxDisp, mn.displacementNorm());
    }

    result.maxNodalDisplacement = maxDisp;
    result.totalStrainEnergy = kernel::PostProcessor::computeTotalStrainEnergy(K_orig, U_full);

    // Prepara mapa rápido de cargas por elemento se vier de LoadCase
    std::unordered_map<ElementId, kernel::ElementLoadInput> lcElemMap;
    if (activeLoadCase) {
        for (const auto& entry : activeLoadCase->elementLoads) lcElemMap[entry.elementId] = entry.load;
        if (activeLoadCase->selfWeight.enabled && std::abs(activeLoadCase->selfWeight.factor) > 0.0) {
            for (const auto& el : elements) {
                auto& inp = lcElemMap[el.id];
                inp.hasBodyGravity = true;
                inp.gravityAccel = activeLoadCase->selfWeight.direction * activeLoadCase->selfWeight.factor;
            }
        }
    }

    result.elementExtremes.clear();
    result.elementExtremes.reserve(elements.size());

    for (auto& elem : elements) {
        const int i1 = nodeIndex[elem.nodeStart];
        const int i2 = nodeIndex[elem.nodeEnd];

        Eigen::Matrix<double, 12, 1> uGlob = Eigen::Matrix<double, 12, 1>::Zero();
        for (int k = 0; k < 6; ++k) {
            uGlob(k) = nodes[i1].displacement[k];
            uGlob(k + 6) = nodes[i2].displacement[k];
        }
        elem.globalDisplacements = uGlob;

        Element evalElem = withEffectiveTorsionalRelease(model, elem);
        const kernel::ElementLoadInput* bodyLoadPtr = nullptr;
        if (activeLoadCase) {
            evalElem.distLoad.active = false;
            evalElem.distLoad.w = {0.0, 0.0, 0.0};
            evalElem.thermalLoad.active = false;
            evalElem.thermalLoad.deltaT = 0.0;
            if (auto it = lcElemMap.find(elem.id); it != lcElemMap.end()) {
                bodyLoadPtr = &it->second;
                if (it->second.hasDistributed) {
                    evalElem.distLoad.active = true;
                    evalElem.distLoad.coordSystem = (it->second.coordSystem == 1) ? CoordSystem::Local : CoordSystem::Global;
                    evalElem.distLoad.w = {it->second.distributedForce.x(), it->second.distributedForce.y(), it->second.distributedForce.z()};
                }
                if (it->second.hasThermal) {
                    evalElem.thermalLoad.active = true;
                    evalElem.thermalLoad.deltaT = it->second.deltaT;
                }
            }
        } else {
            if (auto it = elemLoads.find(elem.id); it != elemLoads.end()) {
                bodyLoadPtr = &it->second;
            }
        }
        if (bodyLoadPtr && bodyLoadPtr->hasBodyGravity) {
            const Material* mat = model.findMaterial(elem.materialId);
            const CrossSection* sec = model.findCrossSection(elem.sectionId);
            if (mat && sec) {
                const double wPerM = mat->gamma * sec->A;
                const Vec3 wGravityGlobal{
                    wPerM * bodyLoadPtr->gravityAccel.x(),
                    wPerM * bodyLoadPtr->gravityAccel.y(),
                    wPerM * bodyLoadPtr->gravityAccel.z()
                };
                if (!evalElem.distLoad.active) {
                    evalElem.distLoad.active = true;
                    evalElem.distLoad.coordSystem = CoordSystem::Global;
                    evalElem.distLoad.w = wGravityGlobal;
                } else if (evalElem.distLoad.coordSystem == CoordSystem::Global) {
                    evalElem.distLoad.w += wGravityGlobal;
                } else {
                    const Eigen::Matrix3d R = evalElem.computeRotationMatrix3x3(model);
                    const Eigen::Vector3d wLocal = R * Eigen::Vector3d(wGravityGlobal.x, wGravityGlobal.y, wGravityGlobal.z);
                    evalElem.distLoad.w += Vec3{wLocal.x(), wLocal.y(), wLocal.z()};
                }
            }
        }

        const Eigen::Matrix<double, 12, 12> T = evalElem.computeTransformationMatrix(model);
        const Eigen::Matrix<double, 12, 1> uLocNodal = T * uGlob;
        elem.localDisplacements = evalElem.recoverLocalElementDisplacements(model, uLocNodal);

        const Eigen::Matrix<double, 12, 12> k0 = evalElem.computeUncondensedLocalStiffnessMatrix(model);
        const Eigen::Matrix<double, 12, 1> f0 = evalElem.computeUncondensedLocalFixedEndForces(model);
        elem.localEndForces = k0 * elem.localDisplacements + f0;

        for (int k = 0; k < 6; ++k) {
            if (evalElem.releases.start[k]) elem.localEndForces(k) = 0.0;
            if (evalElem.releases.end[k]) elem.localEndForces(k + 6) = 0.0;
        }
        elem.hasResults = true;
        evalElem.globalDisplacements = elem.globalDisplacements;
        evalElem.localDisplacements = elem.localDisplacements;
        evalElem.localEndForces = elem.localEndForces;
        evalElem.hasResults = true;

        ElementExtremes ext;
        ext.elementId = elem.id;
        constexpr int Nsamples = 32;
        for (int s = 0; s <= Nsamples; ++s) {
            const double xi = static_cast<double>(s) / static_cast<double>(Nsamples);
            const InternalForces forces = evalElem.evaluateAt(xi, model);
            if (s == 0) {
                ext.N_min = ext.N_max = forces.N;
                ext.Vy_min = ext.Vy_max = forces.Vy;
                ext.Vz_min = ext.Vz_max = forces.Vz;
                ext.Tx_min = ext.Tx_max = forces.Tx;
                ext.My_min = ext.My_max = forces.My;
                ext.Mz_min = ext.Mz_max = forces.Mz;
                ext.maxDeflection = forces.defGlobal.norm();
            } else {
                ext.N_min = std::min(ext.N_min, forces.N);
                ext.N_max = std::max(ext.N_max, forces.N);
                ext.Vy_min = std::min(ext.Vy_min, forces.Vy);
                ext.Vy_max = std::max(ext.Vy_max, forces.Vy);
                ext.Vz_min = std::min(ext.Vz_min, forces.Vz);
                ext.Vz_max = std::max(ext.Vz_max, forces.Vz);
                ext.Tx_min = std::min(ext.Tx_min, forces.Tx);
                ext.Tx_max = std::max(ext.Tx_max, forces.Tx);
                ext.My_min = std::min(ext.My_min, forces.My);
                ext.My_max = std::max(ext.My_max, forces.My);
                ext.Mz_min = std::min(ext.Mz_min, forces.Mz);
                ext.Mz_max = std::max(ext.Mz_max, forces.Mz);
                ext.maxDeflection = std::max(ext.maxDeflection, forces.defGlobal.norm());
            }
        }
        result.elementExtremes.push_back(ext);
    }

    // Recuperação de esforços/tensões para elementos contínuos 2D (Shell) e 3D (Solid)
    result.continuumElementResults.clear();
    result.continuumElementResults.reserve(mapping.continuumElems.size());
    result.maxShellVonMises = 0.0;
    result.maxSolidVonMises = 0.0;

    for (const auto& rec : mapping.continuumElems) {
        auto& me = surf.elements[rec.meshElemIdx];
        const auto mask = rec.elemPtr->requiredNodeDofMask();
        const int nd = rec.elemPtr->localDofCount();
        kernel::VecX uElem = kernel::VecX::Zero(nd);
        int ptr = 0;
        for (kernel::NodeId kid : rec.elemPtr->nodeIds()) {
            for (int d = 0; d < kernel::NODAL_MAX_DOFS; ++d) {
                if (mask[d]) {
                    const int g = dofMgr.getNodeGlobalDof(kid, d);
                    uElem(ptr++) = (g >= 0 && g < U_full.size()) ? U_full(g) : 0.0;
                }
            }
        }

        double effDeltaT = me.deltaT;
        if (auto lit = elemLoads.find(rec.kernelElemId); lit != elemLoads.end() && lit->second.hasThermal) {
            effDeltaT = lit->second.deltaT;
        }

        ContinuumElementResult cr;
        cr.elementId = me.id;
        cr.cellTypeName = rec.elemPtr->typeName();

        if (rec.shellPtr) {
            const auto sr = rec.shellPtr->evaluateCenterStress(uElem, effDeltaT);
            me.hasResults = true;
            me.vonMisesStress = sr.maxVonMises;
            me.membraneResultants = {sr.Nxx, sr.Nyy, sr.Nxy};
            me.bendingResultants = {sr.Mxx, sr.Myy, sr.Mxy};
            me.transverseShear = {sr.Qxz, sr.Qyz};

            cr.isShell = true;
            cr.vonMisesStress = sr.maxVonMises;
            cr.membraneResultants = me.membraneResultants;
            cr.bendingResultants = me.bendingResultants;
            cr.transverseShear = me.transverseShear;
            result.maxShellVonMises = std::max(result.maxShellVonMises, sr.maxVonMises);
        } else if (rec.solidPtr) {
            const auto ss = rec.solidPtr->evaluateCenterStress(uElem, effDeltaT);
            me.hasResults = true;
            me.vonMisesStress = ss.vonMises;
            me.maxPrincipalStress = ss.majorPrincipal;
            me.minPrincipalStress = ss.minorPrincipal;
            me.stressVoigt = {ss.sxx, ss.syy, ss.szz, ss.txy, ss.tyz, ss.tzx};

            cr.isSolid = true;
            cr.vonMisesStress = ss.vonMises;
            cr.maxPrincipalStress = ss.majorPrincipal;
            cr.minPrincipalStress = ss.minorPrincipal;
            cr.stressVoigt = me.stressVoigt;
            result.maxSolidVonMises = std::max(result.maxSolidVonMises, ss.vonMises);
        } else {
            me.hasResults = true;
        }
        result.continuumElementResults.push_back(std::move(cr));
    }

    // Verificação de equilíbrio estático global (1D + 2D/3D unificados sem dupla contagem)
    Vec3 sumAppliedF{0.0, 0.0, 0.0};
    Vec3 sumAppliedM{0.0, 0.0, 0.0};
    Vec3 sumReacF{0.0, 0.0, 0.0};
    Vec3 sumReacM{0.0, 0.0, 0.0};
    double refForceMag = 0.0;
    double refMomentMag = 0.0;

    std::unordered_map<kernel::NodeId, Vec3> kernelNodePositions;
    kernelNodePositions.reserve(nodes.size() + surf.nodes.size());
    for (const auto& n : nodes) kernelNodePositions[n.id] = n.position;
    for (size_t i = 0; i < surf.nodes.size(); ++i) {
        const kernel::NodeId kid = mapping.kernelNodeForMeshIdx[i];
        kernelNodePositions.try_emplace(kid, Vec3{surf.nodes[i].position.x(), surf.nodes[i].position.y(), surf.nodes[i].position.z()});
    }

    // Soma cargas nodais e reações em todos os kernel nodes exatamente 1 vez
    for (const auto& ns : dofMgr.nodeStates()) {
        Vec3 pos{0.0, 0.0, 0.0};
        if (auto pit = kernelNodePositions.find(ns.nodeId); pit != kernelNodePositions.end()) {
            pos = pit->second;
        }
        if (auto lit = nodalLoads.find(ns.nodeId); lit != nodalLoads.end()) {
            const Vec3 nF{lit->second(0), lit->second(1), lit->second(2)};
            const Vec3 nM{lit->second(3), lit->second(4), lit->second(5)};
            sumAppliedF += nF;
            const Vec3 rCrossF = pos.cross(nF);
            sumAppliedM += nM + rCrossF;
            refForceMag += nF.norm();
            refMomentMag += nM.norm() + rCrossF.norm();
        }
        std::array<double, 6> rVec{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        for (int d = 0; d < 6; ++d) {
            const int g = ns.globalDof[d];
            if (g >= 0 && cls.isRestrained[g]) {
                rVec[d] = externalRestrainedReactionAt(g, cls, redSys, R_ext_rd);
            } else if (g >= 0 && ns.springStiffness[d] > 1e-9 && g < U_full.size()) {
                rVec[d] = -ns.springStiffness[d] * U_full(g);
            }
        }
        const Vec3 rF{rVec[0], rVec[1], rVec[2]};
        const Vec3 rM{rVec[3], rVec[4], rVec[5]};
        const Vec3 rCrossReac = pos.cross(rF);
        sumReacF += rF;
        sumReacM += rM + rCrossReac;
        refForceMag += rF.norm();
        refMomentMag += rM.norm() + rCrossReac.norm();
    }

    for (const auto& elem : elements) {
        bool hasDist = elem.distLoad.active;
        CoordSystem cs = elem.distLoad.coordSystem;
        Vec3 w = elem.distLoad.w;
        bool hasGravity = false;
        Vec3 gAcc{0.0, 0.0, 0.0};
        if (activeLoadCase) {
            hasDist = false;
            w = {0.0, 0.0, 0.0};
            cs = CoordSystem::Global;
            if (auto it = lcElemMap.find(elem.id); it != lcElemMap.end()) {
                if (it->second.hasDistributed) {
                    hasDist = true;
                    cs = (it->second.coordSystem == 1) ? CoordSystem::Local : CoordSystem::Global;
                    w = {it->second.distributedForce.x(), it->second.distributedForce.y(), it->second.distributedForce.z()};
                }
                if (it->second.hasBodyGravity) {
                    hasGravity = true;
                    gAcc = {it->second.gravityAccel.x(), it->second.gravityAccel.y(), it->second.gravityAccel.z()};
                }
            }
        }
        const double L = elem.computeLength(model);
        const Node* n1 = model.findNode(elem.nodeStart);
        const Node* n2 = model.findNode(elem.nodeEnd);
        const Vec3 midPos = (n1 && n2) ? ((n1->position + n2->position) * 0.5) : Vec3{0.0, 0.0, 0.0};

        if (hasDist) {
            Vec3 qGlob{0.0, 0.0, 0.0};
            if (cs == CoordSystem::Global) {
                qGlob = w;
            } else {
                const Eigen::Matrix3d R = elem.computeRotationMatrix3x3(model);
                const Eigen::Vector3d qg = R.transpose() * Eigen::Vector3d(w.x, w.y, w.z);
                qGlob = {qg.x(), qg.y(), qg.z()};
            }
            const Vec3 totalF = qGlob * L;
            sumAppliedF += totalF;
            refForceMag += totalF.norm();
            const Vec3 mDist = midPos.cross(totalF);
            sumAppliedM += mDist;
            refMomentMag += mDist.norm();
        }
        if (hasGravity) {
            const Material* mat = model.findMaterial(elem.materialId);
            const CrossSection* sec = model.findCrossSection(elem.sectionId);
            if (mat && sec) {
                const Vec3 wG = gAcc * (mat->gamma * sec->A * L);
                sumAppliedF += wG;
                refForceMag += wG.norm();
                const Vec3 mG = midPos.cross(wG);
                sumAppliedM += mG;
                refMomentMag += mG.norm();
            }
        }
    }

    // Cargas consistentes equivalentes nos elementos contínuos 2D/3D
    for (const auto& rec : mapping.continuumElems) {
        auto lit = elemLoads.find(rec.kernelElemId);
        if (lit == elemLoads.end()) continue;
        const kernel::VecX fe = rec.elemPtr->computeGlobalLoadVector(lit->second);
        const auto mask = rec.elemPtr->requiredNodeDofMask();
        const auto& nids = rec.elemPtr->nodeIds();
        int ptr = 0;
        for (kernel::NodeId kid : nids) {
            std::array<double, 6> nf{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            for (int d = 0; d < kernel::NODAL_MAX_DOFS; ++d) {
                if (mask[d]) {
                    if (d < 6 && ptr < fe.size()) nf[d] = fe(ptr);
                    ++ptr;
                }
            }
            Vec3 pos{0.0, 0.0, 0.0};
            if (auto pit = kernelNodePositions.find(kid); pit != kernelNodePositions.end()) {
                pos = pit->second;
            }
            const Vec3 fN{nf[0], nf[1], nf[2]};
            const Vec3 mN{nf[3], nf[4], nf[5]};
            sumAppliedF += fN;
            const Vec3 rCross = pos.cross(fN);
            sumAppliedM += mN + rCross;
            refForceMag += fN.norm();
            refMomentMag += mN.norm() + rCross.norm();
        }
    }

    result.equilibrium.sumAppliedForces = sumAppliedF;
    result.equilibrium.sumReactions = sumReacF;
    result.equilibrium.sumAppliedMoments = sumAppliedM;
    result.equilibrium.sumReactionMoments = sumReacM;
    result.equilibrium.residualForces = sumReacF + sumAppliedF;
    result.equilibrium.residualMoments = sumReacM + sumAppliedM;

    const double resFNorm = result.equilibrium.residualForces.norm();
    const double resMNorm = result.equilibrium.residualMoments.norm();
    const double tolF = std::max(1e-2, 1e-5 * refForceMag);
    const double tolM = std::max(1e-2, 1e-5 * refMomentMag);
    result.equilibrium.isBalanced = (resFNorm <= tolF) && (resMNorm <= tolM);
}

SolverResult solveStaticInternal(
    StructuralModel& model,
    const kernel::LinearSolverOptions& opts,
    const kernel::LoadCase* activeLoadCase,
    const std::stop_token& stopToken)
{
    const auto startTime = std::chrono::high_resolution_clock::now();
    SolverResult result;
    model.clearResults();

    if (stopToken.stop_requested()) {
        result.success = false;
        result.errorMessage = "Análise cancelada pelo usuário.";
        return result;
    }

    const ModelValidation val = model.validate();
    if (!val.valid) {
        result.success = false;
        result.errorMessage = val.errors.empty() ? "Erro de validação desconhecido." : val.errors[0];
        return result;
    }

    kernel::DofManager dofMgr;
    UnifiedSolverMapping mapping;
    int restrainedCount = 0;
    int springCount = 0;
    std::string setupErr;
    if (!setupDofManagerFromModel(model, dofMgr, mapping, restrainedCount, springCount, &setupErr)) {
        result.success = false;
        result.errorMessage = setupErr;
        return result;
    }

    result.totalNodes = static_cast<int>(model.getNodes().size());
    result.totalElements = static_cast<int>(model.getElements().size());
    result.totalSurfaceNodes = static_cast<int>(model.surfaceMesh().nodes.size());
    result.totalSurfaceElements = static_cast<int>(model.surfaceMesh().elements.size());
    result.totalDofs = dofMgr.totalDofs();
    result.restrainedDofs = restrainedCount;

    if (restrainedCount == 0 && springCount == 0) {
        result.success = false;
        result.errorMessage = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return result;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    std::string buildErr;
    if (!buildKernelElements(model, mapping, kElements, &buildErr)) {
        result.success = false;
        result.errorMessage = buildErr;
        return result;
    }

    if (stopToken.stop_requested()) {
        result.success = false;
        result.errorMessage = "Análise cancelada pelo usuário.";
        return result;
    }

    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
    if (activeLoadCase) {
        extractCaseLoadMaps(model, mapping, *activeLoadCase, elemLoads, nodalLoads);
    } else {
        extractLegacyLoadMaps(model, mapping, elemLoads, nodalLoads);
    }

    kernel::SpMat K_orig;
    kernel::VecX F_orig;
    std::string asmErr;
    if (!kernel::GlobalAssembler::assembleStiffnessAndLoad(dofMgr, kElements, elemLoads, nodalLoads, K_orig, F_orig, &asmErr)) {
        result.success = false;
        result.errorMessage = asmErr;
        return result;
    }

    std::string mechErr;
    const kernel::DofClassification cls = dofMgr.classifyDofs(K_orig, F_orig, &mechErr);
    if (!mechErr.empty()) {
        result.success = false;
        result.errorMessage = mechErr;
        return result;
    }

    kernel::ReducedLinearSystem redSys;
    if (!kernel::GlobalAssembler::buildReducedSystem(dofMgr, K_orig, F_orig, cls, redSys, &asmErr)) {
        result.success = false;
        result.errorMessage = asmErr;
        return result;
    }

    kernel::LinearSolverStats solverStats;
    kernel::VecX U_free;
    if (redSys.freeDofs > 0) {
        U_free = kernel::LinearSolverSuite::solve(redSys.K_ff, redSys.rhs_f, opts, solverStats, nullptr, stopToken);
        if (!solverStats.success) {
            result.success = false;
            if (solverStats.errorCode == "SINGULAR_MATRIX" || solverStats.errorCode == "NOT_SPD") {
                result.errorMessage = "Falha na fatoração da matriz de rigidez global (possível mecanismo ou matriz singular).";
            } else {
                result.errorMessage = solverStats.message.empty() ? "Falha ao resolver o sistema linear esparso K*U=F." : solverStats.message;
            }
            result.solverStats = solverStats;
            return result;
        }
    } else {
        solverStats.success = true;
    }
    solverStats.totalDofs = dofMgr.totalDofs();
    solverStats.restrainedDofs = cls.restrainedCount;
    solverStats.inactiveDofs = cls.inactiveCount;
    solverStats.mpcEliminatedDofs = cls.mpcSlaveCount;

    const kernel::VecX U_full = kernel::GlobalAssembler::reconstructFullDisplacements(redSys, U_free);
    if (!U_full.allFinite()) {
        result.success = false;
        result.errorMessage = "Falha ao resolver o sistema linear esparso K*U=F.";
        return result;
    }

    const kernel::VecX R_full = K_orig * U_full - F_orig;
    if (!R_full.allFinite()) {
        result.success = false;
        result.errorMessage = "Reações calculadas contêm valores não finitos.";
        return result;
    }

    populatePostProcessingFromVectors(
        model, dofMgr, cls, redSys, mapping, elemLoads, nodalLoads, U_full, R_full, K_orig, result, activeLoadCase);
    result.solverStats = solverStats;

    const auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    model.bumpRevision();
    result.success = true;
    return result;
}

} // namespace

SolverResult FemSolver3D::solve(StructuralModel& model) {
    return solveWithOptions(model, model.solverOptions());
}

SolverResult FemSolver3D::solveWithOptions(
    StructuralModel& model,
    const kernel::LinearSolverOptions& opts,
    const std::stop_token& stopToken)
{
    return solveStaticInternal(model, opts, nullptr, stopToken);
}

SolverResult FemSolver3D::solveLoadCase(
    StructuralModel& model,
    const kernel::LoadCase& lc,
    const kernel::LinearSolverOptions& opts,
    const std::stop_token& stopToken)
{
    return solveStaticInternal(model, opts, &lc, stopToken);
}

bool FemSolver3D::solveLoadCasesAndCombinations(
    StructuralModel& model,
    const kernel::LinearSolverOptions& opts,
    std::vector<kernel::CaseDisplacementResult>& outCaseResults,
    std::vector<kernel::CaseDisplacementResult>& outComboResults,
    kernel::EnvelopeExtremes& outEnvelope,
    std::string* errorMsg,
    const std::stop_token& stopToken)
{
    outCaseResults.clear();
    outComboResults.clear();
    outEnvelope = kernel::EnvelopeExtremes{};

    if (stopToken.stop_requested()) {
        if (errorMsg) *errorMsg = "Análise cancelada pelo usuário.";
        return false;
    }

    const ModelValidation val = model.validate();
    if (!val.valid) {
        if (errorMsg) *errorMsg = val.errors.empty() ? "Modelo inválido." : val.errors[0];
        return false;
    }

    kernel::DofManager dofMgr;
    UnifiedSolverMapping mapping;
    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, dofMgr, mapping, restrainedCount, springCount, errorMsg)) {
        return false;
    }
    if (restrainedCount == 0 && springCount == 0) {
        if (errorMsg) *errorMsg = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return false;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    if (!buildKernelElements(model, mapping, kElements, errorMsg)) {
        return false;
    }

    // Se o modelo ainda não tiver LoadCases explícitos, cria um caso implícito das cargas diretas (1D + 2D/3D)
    const bool useImplicitLegacy = model.getLoadCases().empty();
    std::vector<kernel::LoadCase> casesToSolve = model.getLoadCases();
    if (useImplicitLegacy) {
        kernel::LoadCase fallback;
        fallback.id = 1;
        fallback.name = "Caso Único";
        casesToSolve.push_back(std::move(fallback));
    }

    kernel::FactorizationCache cache;
    kernel::SpMat K_orig;
    kernel::ReducedLinearSystem cachedSys;
    kernel::DofClassification cls;
    bool systemBuilt = false;

    std::unordered_map<kernel::LoadCaseId, kernel::VecX> caseForceVectors;

    for (const auto& lc : casesToSolve) {
        if (stopToken.stop_requested()) {
            if (errorMsg) *errorMsg = "Análise cancelada pelo usuário.";
            return false;
        }
        std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
        std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
        if (useImplicitLegacy) {
            extractLegacyLoadMaps(model, mapping, elemLoads, nodalLoads);
        } else {
            extractCaseLoadMaps(model, mapping, lc, elemLoads, nodalLoads);
        }

        kernel::VecX F_case;
        if (!systemBuilt) {
            if (!kernel::GlobalAssembler::assembleStiffnessAndLoad(dofMgr, kElements, elemLoads, nodalLoads, K_orig, F_case, errorMsg)) {
                return false;
            }
            std::string mechErr;
            cls = dofMgr.classifyDofs(K_orig, F_case, &mechErr);
            if (!mechErr.empty()) {
                if (errorMsg) *errorMsg = mechErr;
                return false;
            }
            if (!kernel::GlobalAssembler::buildReducedSystem(dofMgr, K_orig, F_case, cls, cachedSys, errorMsg)) {
                return false;
            }
            systemBuilt = true;
        } else {
            if (!kernel::GlobalAssembler::assembleLoadOnly(dofMgr, kElements, elemLoads, nodalLoads, F_case, errorMsg)) {
                return false;
            }
            if (!kernel::GlobalAssembler::buildReducedRightHandSide(dofMgr, K_orig, F_case, cachedSys, cachedSys.rhs_f, errorMsg)) {
                return false;
            }
        }

        caseForceVectors[lc.id] = F_case;

        kernel::LinearSolverStats stats;
        kernel::VecX U_free;
        if (cachedSys.freeDofs > 0) {
            U_free = kernel::LinearSolverSuite::solve(cachedSys.K_ff, cachedSys.rhs_f, opts, stats, &cache, stopToken);
            if (!stats.success) {
                if (errorMsg) *errorMsg = stats.message;
                return false;
            }
        } else {
            stats.success = true;
        }
        stats.totalDofs = dofMgr.totalDofs();
        stats.restrainedDofs = cls.restrainedCount;
        stats.inactiveDofs = cls.inactiveCount;
        stats.mpcEliminatedDofs = cls.mpcSlaveCount;

        kernel::CaseDisplacementResult cr;
        cr.caseOrComboId = lc.id;
        cr.name = lc.name;
        cr.isCombination = false;
        cr.U_full = kernel::GlobalAssembler::reconstructFullDisplacements(cachedSys, U_free);
        cr.R_full = K_orig * cr.U_full - F_case;
        cr.solverStats = stats;
        outCaseResults.push_back(std::move(cr));
    }

    // Combinações (lineares com fatoração cacheada, ou 2ª ordem P-Delta quando comb.pDeltaEnabled == true)
    for (const auto& comb : model.getLoadCombinations()) {
        if (stopToken.stop_requested()) {
            if (errorMsg) *errorMsg = "Análise cancelada pelo usuário.";
            return false;
        }
        kernel::VecX F_comb = kernel::VecX::Zero(dofMgr.totalDofs());
        for (const auto& f : comb.factors) {
            auto it = caseForceVectors.find(f.loadCaseId);
            if (it != caseForceVectors.end()) {
                F_comb += f.gamma * it->second;
            }
        }

        kernel::CaseDisplacementResult cr;
        cr.caseOrComboId = comb.id;
        cr.name = comb.name;
        cr.isCombination = true;

        kernel::LinearSolverStats stats;
        stats.totalDofs = dofMgr.totalDofs();
        stats.restrainedDofs = cls.restrainedCount;
        stats.inactiveDofs = cls.inactiveCount;
        stats.mpcEliminatedDofs = cls.mpcSlaveCount;

        if (!comb.pDeltaEnabled || cachedSys.freeDofs == 0) {
            kernel::VecX rhs_comb;
            if (!kernel::GlobalAssembler::buildReducedRightHandSide(dofMgr, K_orig, F_comb, cachedSys, rhs_comb, errorMsg)) {
                return false;
            }

            kernel::VecX U_free;
            if (cachedSys.freeDofs > 0) {
                U_free = kernel::LinearSolverSuite::solve(cachedSys.K_ff, rhs_comb, opts, stats, &cache, stopToken);
                if (!stats.success) {
                    if (errorMsg) *errorMsg = stats.message;
                    return false;
                }
            } else {
                stats.success = true;
            }
            cr.U_full = kernel::GlobalAssembler::reconstructFullDisplacements(cachedSys, U_free);
            cr.R_full = K_orig * cr.U_full - F_comb;
            cr.solverStats = stats;
        } else if (model.surfaceMesh().elements.empty()) {
            std::vector<kernel::Frame1DNonlinearElementSpec> specs;
            if (!buildFrame1DNonlinearSpecs(model, specs, errorMsg)) {
                return false;
            }
            kernel::FrameNonlinearModel nlModel(
                dofMgr, cachedSys, F_comb, std::move(specs), kernel::NonlinearKinematics::PDeltaSecondOrder);
            kernel::NonlinearOptions pdOpts;
            pdOpts.method = kernel::NonlinearMethod::PDelta;
            pdOpts.kinematics = kernel::NonlinearKinematics::PDeltaSecondOrder;
            pdOpts.numSteps = 1;
            pdOpts.maxIterationsPerStep = 30;
            pdOpts.targetLoadFactor = 1.0;
            pdOpts.linearSolver = opts;

            const kernel::NonlinearResult nlRes = kernel::NonlinearSolver::solve(nlModel, pdOpts, stopToken);
            if (!nlRes.success) {
                if (errorMsg) {
                    *errorMsg = nlRes.errorMessage.empty()
                        ? ("Falha na análise P-Delta da combinação " + comb.name + ".")
                        : nlRes.errorMessage;
                }
                return false;
            }
            stats.success = true;
            stats.iterations = nlRes.totalIterations;
            cr.U_full = nlRes.finalU_full;
            cr.R_full = nlRes.finalReactionsFull;
            cr.solverStats = stats;
        } else {
            kernel::VecX rhs_comb;
            if (!kernel::GlobalAssembler::buildReducedRightHandSide(dofMgr, K_orig, F_comb, cachedSys, rhs_comb, errorMsg)) {
                return false;
            }
            kernel::VecX U_free_iter = kernel::LinearSolverSuite::solve(cachedSys.K_ff, rhs_comb, opts, stats, &cache, stopToken);
            if (!stats.success) {
                if (errorMsg) *errorMsg = stats.message;
                return false;
            }
            kernel::VecX U_full_iter = kernel::GlobalAssembler::reconstructFullDisplacements(cachedSys, U_free_iter);
            kernel::SpMat Kg_orig(dofMgr.totalDofs(), dofMgr.totalDofs());
            bool converged = false;
            int totalIter = 0;

            for (int iter = 0; iter < 30; ++iter) {
                if (stopToken.stop_requested()) {
                    if (errorMsg) *errorMsg = "Análise cancelada pelo usuário.";
                    return false;
                }
                ++totalIter;
                if (!kernel::GlobalAssembler::assembleGeometricStiffness(dofMgr, kElements, U_full_iter, Kg_orig, errorMsg)) {
                    return false;
                }
                const kernel::SpMat K_eff = (K_orig + Kg_orig).eval();
                kernel::ReducedLinearSystem iterSys;
                if (!kernel::GlobalAssembler::buildReducedSystem(dofMgr, K_eff, F_comb, cls, iterSys, errorMsg)) {
                    return false;
                }
                kernel::LinearSolverStats iterStats;
                const kernel::VecX U_free_next =
                    kernel::LinearSolverSuite::solve(iterSys.K_ff, iterSys.rhs_f, opts, iterStats, nullptr, stopToken);
                if (!iterStats.success || !U_free_next.allFinite()) {
                    if (errorMsg) {
                        *errorMsg = iterStats.message.empty()
                            ? ("Instabilidade elástica na análise P-Delta da combinação " + comb.name + ".")
                            : iterStats.message;
                    }
                    return false;
                }
                const double relDiff = (U_free_next - U_free_iter).norm() / std::max(1.0, U_free_next.norm());
                U_free_iter = U_free_next;
                U_full_iter = kernel::GlobalAssembler::reconstructFullDisplacements(iterSys, U_free_iter);
                stats = iterStats;
                if (relDiff <= 1e-6) {
                    converged = true;
                    break;
                }
            }
            if (!converged) {
                if (errorMsg) {
                    *errorMsg = "Análise P-Delta da combinação " + comb.name + " não convergiu em 30 iterações.";
                }
                return false;
            }
            stats.success = true;
            stats.iterations = totalIter;
            stats.totalDofs = dofMgr.totalDofs();
            stats.restrainedDofs = cls.restrainedCount;
            stats.inactiveDofs = cls.inactiveCount;
            stats.mpcEliminatedDofs = cls.mpcSlaveCount;
            cr.U_full = U_full_iter;
            cr.R_full = (K_orig + Kg_orig) * U_full_iter - F_comb;
            cr.solverStats = stats;
        }
        outComboResults.push_back(std::move(cr));
    }

    std::vector<std::pair<kernel::NodeId, std::array<int, 6>>> nodeDofMap;
    nodeDofMap.reserve(model.getNodes().size() + model.surfaceMesh().nodes.size());
    std::unordered_set<kernel::NodeId> seenEnvelopeIds;
    for (const auto& n : model.getNodes()) {
        std::array<int, 6> dofs;
        for (int d = 0; d < 6; ++d) dofs[d] = dofMgr.getNodeGlobalDof(n.id, d);
        seenEnvelopeIds.insert(n.id);
        nodeDofMap.emplace_back(n.id, dofs);
    }
    for (size_t i = 0; i < model.surfaceMesh().nodes.size(); ++i) {
        const kernel::NodeId kid = mapping.kernelNodeForMeshIdx[i];
        if (!seenEnvelopeIds.insert(kid).second) continue;
        std::array<int, 6> dofs;
        for (int d = 0; d < 6; ++d) dofs[d] = dofMgr.getNodeGlobalDof(kid, d);
        nodeDofMap.emplace_back(kid, dofs);
    }

    const auto& envelopeSource = outComboResults.empty() ? outCaseResults : outComboResults;
    outEnvelope = kernel::LoadEnvelope::compute(envelopeSource, nodeDofMap);
    return true;
}

kernel::BucklingResult FemSolver3D::solveBuckling(
    StructuralModel& model,
    const kernel::BucklingOptions& opts,
    const std::stop_token& stopToken)
{
    kernel::BucklingResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Análise de flambagem cancelada pelo usuário.";
        return out;
    }

    const ModelValidation val = model.validate();
    if (!val.valid) {
        out.errorMessage = val.errors.empty() ? "Erro de validação." : val.errors[0];
        return out;
    }

    kernel::DofManager dofMgr;
    UnifiedSolverMapping mapping;
    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, dofMgr, mapping, restrainedCount, springCount, &out.errorMessage)) {
        return out;
    }
    if (restrainedCount == 0 && springCount == 0) {
        out.errorMessage = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return out;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    if (!buildKernelElements(model, mapping, kElements, &out.errorMessage)) {
        return out;
    }

    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
    extractLegacyLoadMaps(model, mapping, elemLoads, nodalLoads);

    kernel::SpMat K_orig;
    kernel::VecX F_orig;
    if (!kernel::GlobalAssembler::assembleStiffnessAndLoad(dofMgr, kElements, elemLoads, nodalLoads, K_orig, F_orig, &out.errorMessage)) {
        return out;
    }

    std::string mechErr;
    const kernel::DofClassification cls = dofMgr.classifyDofs(K_orig, F_orig, &mechErr);
    if (!mechErr.empty()) {
        out.errorMessage = mechErr;
        return out;
    }

    kernel::ReducedLinearSystem redSys;
    if (!kernel::GlobalAssembler::buildReducedSystem(dofMgr, K_orig, F_orig, cls, redSys, &out.errorMessage)) {
        return out;
    }
    if (redSys.freeDofs <= 0) {
        out.errorMessage = "Sem graus de liberdade livres para análise de flambagem.";
        return out;
    }

    // 1. Passo estático linear de referência para obter esforços axiais U_ref
    kernel::LinearSolverStats linStats;
    const kernel::VecX U_free_ref = kernel::LinearSolverSuite::solve(
        redSys.K_ff, redSys.rhs_f, model.solverOptions(), linStats, nullptr, stopToken);
    if (!linStats.success) {
        out.errorMessage = "Falha no estado linear de referência para flambagem: " + linStats.message;
        return out;
    }
    const kernel::VecX U_full_ref = kernel::GlobalAssembler::reconstructFullDisplacements(redSys, U_free_ref);

    // 2. Montagem de Kg global e redução consistente com MPCs (P_free^T * Kg_orig * P_free)
    kernel::SpMat Kg_orig;
    if (!kernel::GlobalAssembler::assembleGeometricStiffness(dofMgr, kElements, U_full_ref, Kg_orig, &out.errorMessage)) {
        return out;
    }

    // Prolongamento P_free para redução exata sobre os mesmos DOFs livres/MPCs
    std::vector<kernel::Triplet> pTrips;
    if (redSys.hasMpc && redSys.T_mpc.rows() == redSys.fullDofs) {
        std::unordered_map<int, int> redToFree;
        for (int j = 0; j < redSys.freeDofs; ++j) redToFree[redSys.freeReducedIndices[j]] = j;
        for (int k = 0; k < redSys.T_mpc.outerSize(); ++k) {
            auto fit = redToFree.find(k);
            if (fit == redToFree.end()) continue;
            for (kernel::SpMat::InnerIterator it(redSys.T_mpc, k); it; ++it) {
                if (std::abs(it.value()) > 1e-16) pTrips.emplace_back(it.row(), fit->second, it.value());
            }
        }
    } else {
        pTrips.reserve(redSys.freeDofs);
        for (int j = 0; j < redSys.freeDofs; ++j) {
            pTrips.emplace_back(redSys.freeReducedIndices[j], j, 1.0);
        }
    }
    kernel::SpMat P_free(redSys.fullDofs, redSys.freeDofs);
    P_free.setFromTriplets(pTrips.begin(), pTrips.end());
    P_free.makeCompressed();

    const kernel::SpMat Kg_ff = (P_free.transpose() * Kg_orig * P_free).eval();
    return kernel::BucklingSolver::solve(redSys.K_ff, Kg_ff, redSys, opts, stopToken);
}

kernel::NonlinearResult FemSolver3D::solveNonlinear(
    StructuralModel& model,
    const kernel::NonlinearOptions& opts,
    const std::stop_token& stopToken)
{
    kernel::NonlinearResult out;
    model.clearResults();

    if (stopToken.stop_requested()) {
        out.errorMessage = "Análise não linear cancelada pelo usuário.";
        return out;
    }

    if (!model.surfaceMesh().elements.empty()) {
        out.errorMessage = "Análise não-linear corrotacional/J2 atualmente opera apenas sobre pórticos e treliças 1D; utilize Análise Estática, Flambagem Linearizada ou Dinâmica para malhas 2D/3D.";
        return out;
    }

    const ModelValidation val = model.validate();
    if (!val.valid) {
        out.errorMessage = val.errors.empty() ? "Erro de validação." : val.errors[0];
        return out;
    }

    kernel::DofManager dofMgr;
    UnifiedSolverMapping mapping;
    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, dofMgr, mapping, restrainedCount, springCount, &out.errorMessage)) {
        return out;
    }
    if (restrainedCount == 0 && springCount == 0) {
        out.errorMessage = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return out;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    if (!buildKernelElements(model, mapping, kElements, &out.errorMessage)) {
        return out;
    }

    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
    extractLegacyLoadMaps(model, mapping, elemLoads, nodalLoads);

    kernel::SpMat K_orig;
    kernel::VecX F_orig;
    if (!kernel::GlobalAssembler::assembleStiffnessAndLoad(dofMgr, kElements, elemLoads, nodalLoads, K_orig, F_orig, &out.errorMessage)) {
        return out;
    }

    std::string mechErr;
    const kernel::DofClassification cls = dofMgr.classifyDofs(K_orig, F_orig, &mechErr);
    if (!mechErr.empty()) {
        out.errorMessage = mechErr;
        return out;
    }

    kernel::ReducedLinearSystem redSys;
    if (!kernel::GlobalAssembler::buildReducedSystem(dofMgr, K_orig, F_orig, cls, redSys, &out.errorMessage)) {
        return out;
    }
    if (redSys.freeDofs <= 0) {
        out.errorMessage = "Sem graus de liberdade livres para análise não linear.";
        return out;
    }

    std::vector<kernel::Frame1DNonlinearElementSpec> specs;
    if (!buildFrame1DNonlinearSpecs(model, specs, &out.errorMessage)) {
        return out;
    }

    kernel::FrameNonlinearModel nlModel(
        dofMgr, redSys, F_orig, std::move(specs), opts.kinematics);
    out = kernel::NonlinearSolver::solve(nlModel, opts, stopToken);

    if (out.success && out.finalU_full.size() == dofMgr.totalDofs()) {
        const kernel::VecX R_ext_rd = computeReducedExternalReactions(redSys, out.finalReactionsFull);
        std::unordered_map<NodeId, int> nodeIndex;
        auto& nodes = model.getNodes();
        for (int ni = 0; ni < static_cast<int>(nodes.size()); ++ni) {
            nodeIndex[nodes[ni].id] = ni;
            const auto& sup = nodes[ni].support;
            for (int d = 0; d < 6; ++d) {
                const int g = dofMgr.getNodeGlobalDof(nodes[ni].id, d);
                const double u = (g >= 0) ? out.finalU_full(g) : 0.0;
                double r = 0.0;
                if (g >= 0 && out.finalReactionsFull.size() == dofMgr.totalDofs() && cls.isRestrained[g]) {
                    r = externalRestrainedReactionAt(g, cls, redSys, R_ext_rd);
                } else if (sup.springStiffness[d] > 1e-9) {
                    r = -sup.springStiffness[d] * u;
                }
                nodes[ni].displacement[d] = u;
                nodes[ni].reaction[d] = r;
            }
        }

        const auto& lastEv = nlModel.lastElementResults();
        auto& elems = model.getElements();
        for (size_t i = 0; i < elems.size() && i < lastEv.size(); ++i) {
            const auto i1 = nodeIndex[elems[i].nodeStart];
            const auto i2 = nodeIndex[elems[i].nodeEnd];
            for (int d = 0; d < 6; ++d) {
                elems[i].globalDisplacements(d) = nodes[i1].displacement[d];
                elems[i].globalDisplacements(d + 6) = nodes[i2].displacement[d];
            }
            const Element effElem = withEffectiveTorsionalRelease(model, elems[i]);
            const Eigen::Matrix<double, 12, 12> T = effElem.computeTransformationMatrix(model);
            const Eigen::Matrix<double, 12, 1> uLocNodal = T * elems[i].globalDisplacements;
            elems[i].localDisplacements = effElem.recoverLocalElementDisplacements(model, uLocNodal);
            elems[i].localEndForces = lastEv[i].fIntLocal;
            elems[i].hasResults = true;
        }

        // [AUD-010] Verificação real de equilíbrio estático global no estado final convergido
        const double lambda = out.achievedLoadFactor;
        const bool useDeformedPos = (opts.kinematics != kernel::NonlinearKinematics::SmallStrain);
        std::unordered_map<kernel::NodeId, Vec3> effNodePos;
        effNodePos.reserve(nodes.size());
        for (const auto& n : nodes) {
            Vec3 p = n.position;
            if (useDeformedPos) {
                p += Vec3{n.displacement[0], n.displacement[1], n.displacement[2]};
            }
            effNodePos[n.id] = p;
        }

        Vec3 sumAppliedF{0.0, 0.0, 0.0};
        Vec3 sumAppliedM{0.0, 0.0, 0.0};
        Vec3 sumReacF{0.0, 0.0, 0.0};
        Vec3 sumReacM{0.0, 0.0, 0.0};
        double refForceMag = 0.0;
        double refMomentMag = 0.0;

        for (const auto& ns : dofMgr.nodeStates()) {
            Vec3 pos{0.0, 0.0, 0.0};
            if (auto pit = effNodePos.find(ns.nodeId); pit != effNodePos.end()) {
                pos = pit->second;
            }
            if (auto lit = nodalLoads.find(ns.nodeId); lit != nodalLoads.end()) {
                const Vec3 nF{lambda * lit->second(0), lambda * lit->second(1), lambda * lit->second(2)};
                const Vec3 nM{lambda * lit->second(3), lambda * lit->second(4), lambda * lit->second(5)};
                sumAppliedF += nF;
                const Vec3 rCrossF = pos.cross(nF);
                sumAppliedM += nM + rCrossF;
                refForceMag += nF.norm();
                refMomentMag += nM.norm() + rCrossF.norm();
            }
            std::array<double, 6> rVec{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            for (int d = 0; d < 6; ++d) {
                const int g = ns.globalDof[d];
                if (g >= 0 && out.finalReactionsFull.size() == dofMgr.totalDofs() && cls.isRestrained[g]) {
                    rVec[d] = externalRestrainedReactionAt(g, cls, redSys, R_ext_rd);
                } else if (g >= 0 && ns.springStiffness[d] > 1e-9 && g < out.finalU_full.size()) {
                    rVec[d] = -ns.springStiffness[d] * out.finalU_full(g);
                }
            }
            const Vec3 rF{rVec[0], rVec[1], rVec[2]};
            const Vec3 rM{rVec[3], rVec[4], rVec[5]};
            const Vec3 rCrossReac = pos.cross(rF);
            sumReacF += rF;
            sumReacM += rM + rCrossReac;
            refForceMag += rF.norm();
            refMomentMag += rM.norm() + rCrossReac.norm();
        }

        for (const auto& elem : elems) {
            if (!elem.distLoad.active) continue;
            const double L = elem.computeLength(model);
            Vec3 p1{0.0, 0.0, 0.0};
            Vec3 p2{0.0, 0.0, 0.0};
            if (auto it1 = effNodePos.find(elem.nodeStart); it1 != effNodePos.end()) p1 = it1->second;
            if (auto it2 = effNodePos.find(elem.nodeEnd); it2 != effNodePos.end()) p2 = it2->second;
            const Vec3 midPos = (p1 + p2) * 0.5;

            Vec3 qGlob{0.0, 0.0, 0.0};
            if (elem.distLoad.coordSystem == CoordSystem::Global) {
                qGlob = elem.distLoad.w;
            } else {
                const Eigen::Matrix3d R = elem.computeRotationMatrix3x3(model);
                const Eigen::Vector3d qg = R.transpose() * Eigen::Vector3d(elem.distLoad.w.x, elem.distLoad.w.y, elem.distLoad.w.z);
                qGlob = {qg.x(), qg.y(), qg.z()};
            }
            const Vec3 totalF = qGlob * (L * lambda);
            sumAppliedF += totalF;
            refForceMag += totalF.norm();
            const Vec3 mDist = midPos.cross(totalF);
            sumAppliedM += mDist;
            refMomentMag += mDist.norm();
        }

        out.sumAppliedForces = kernel::Vec3(sumAppliedF.x, sumAppliedF.y, sumAppliedF.z);
        out.sumReactions = kernel::Vec3(sumReacF.x, sumReacF.y, sumReacF.z);
        out.sumAppliedMoments = kernel::Vec3(sumAppliedM.x, sumAppliedM.y, sumAppliedM.z);
        out.sumReactionMoments = kernel::Vec3(sumReacM.x, sumReacM.y, sumReacM.z);
        out.residualForces = out.sumReactions + out.sumAppliedForces;
        out.residualMoments = out.sumReactionMoments + out.sumAppliedMoments;

        const double tolF = std::max(1e-2, 1e-5 * refForceMag);
        const double momRelTol = (opts.kinematics == kernel::NonlinearKinematics::PDeltaSecondOrder) ? 1e-3 : 1e-5;
        const double tolM = std::max(1e-2, momRelTol * refMomentMag);
        out.equilibriumBalanced = (out.residualForces.norm() <= tolF) && (out.residualMoments.norm() <= tolM);
    }

    return out;
}

namespace {

struct AssembledDynamicSystem {
    kernel::DofManager dofMgr;
    UnifiedSolverMapping mapping;
    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    kernel::SpMat K_orig;
    kernel::SpMat M_full;
    kernel::VecX F_orig;
    kernel::ReducedLinearSystem redSys;
    kernel::SpMat M_ff;
    std::array<kernel::VecX, 3> infl;
};

bool assembleDynamicBaseSystem(const StructuralModel& model,
                               AssembledDynamicSystem& sys,
                               std::string& errOut)
{
    const ModelValidation val = model.validate();
    if (!val.valid) {
        errOut = val.errors.empty() ? "Erro de validação estrutural." : val.errors[0];
        return false;
    }

    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, sys.dofMgr, sys.mapping, restrainedCount, springCount, &errOut)) {
        return false;
    }
    if (restrainedCount == 0 && springCount == 0) {
        errOut = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return false;
    }

    if (!buildKernelElements(model, sys.mapping, sys.kElements, &errOut)) {
        return false;
    }

    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
    extractLegacyLoadMaps(model, sys.mapping, elemLoads, nodalLoads);

    if (!kernel::GlobalAssembler::assembleStiffnessAndLoad(
            sys.dofMgr, sys.kElements, elemLoads, nodalLoads, sys.K_orig, sys.F_orig, &errOut)) {
        return false;
    }

    std::string mechErr;
    const kernel::DofClassification cls = sys.dofMgr.classifyDofs(sys.K_orig, sys.F_orig, &mechErr);
    if (!mechErr.empty()) {
        errOut = mechErr;
        return false;
    }
    if (!kernel::GlobalAssembler::buildReducedSystem(sys.dofMgr, sys.K_orig, sys.F_orig, cls, sys.redSys, &errOut)) {
        return false;
    }

    if (!kernel::GlobalAssembler::assembleMass(sys.dofMgr, sys.kElements, true, {}, sys.M_full, &errOut)) {
        return false;
    }
    if (!kernel::DynamicSolver::reduceMassAndInfluence(sys.dofMgr, sys.M_full, sys.redSys, sys.M_ff, sys.infl, &errOut)) {
        return false;
    }
    return true;
}

} // namespace

kernel::ModalResult FemSolver3D::solveModal(
    const StructuralModel& model,
    const kernel::ModalOptions& opts,
    const std::stop_token& stopToken)
{
    kernel::ModalResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Análise modal cancelada pelo usuário.";
        return out;
    }
    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }
    return kernel::DynamicSolver::solveModal(sys.redSys.K_ff, sys.M_ff, sys.infl, sys.redSys, opts, stopToken);
}

kernel::HarmonicResult FemSolver3D::solveHarmonic(
    const StructuralModel& model,
    const kernel::HarmonicOptions& opts,
    const std::stop_token& stopToken)
{
    kernel::HarmonicResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Análise harmônica cancelada pelo usuário.";
        return out;
    }
    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }
    // Contrato [AUD-002]: passar matriz vazia de amortecedores discretos; DynamicSolver aplica Rayleigh uma única vez.
    const kernel::SpMat C_discrete(sys.redSys.K_ff.rows(), sys.redSys.K_ff.cols());
    return kernel::DynamicSolver::solveHarmonic(
        sys.redSys.K_ff, sys.M_ff, C_discrete, sys.redSys.rhs_f, sys.redSys, opts, stopToken);
}

kernel::ResponseSpectrumResult FemSolver3D::solveResponseSpectrum(
    const StructuralModel& model,
    const kernel::ResponseSpectrumOptions& specOpts,
    const kernel::ModalOptions& modalOpts,
    const std::stop_token& stopToken)
{
    kernel::ResponseSpectrumResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Análise por espectro de resposta cancelada pelo usuário.";
        return out;
    }
    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }
    const kernel::ModalResult modal =
        kernel::DynamicSolver::solveModal(sys.redSys.K_ff, sys.M_ff, sys.infl, sys.redSys, modalOpts, stopToken);
    if (!modal.success) {
        out.errorMessage = modal.errorMessage;
        return out;
    }
    return kernel::DynamicSolver::solveResponseSpectrum(sys.K_orig, modal, specOpts, stopToken);
}

kernel::TransientResult FemSolver3D::solveSeismic(
    const StructuralModel& model,
    const std::function<kernel::Vec3(double timeSec, int stepIdx)>& groundAccelMS2Fn,
    const kernel::TransientOptions& opts,
    const std::stop_token& stopToken)
{
    kernel::TransientResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Análise sísmica cancelada pelo usuário.";
        return out;
    }
    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }
    // Contrato [AUD-002]: passar matriz vazia de amortecedores discretos; DynamicSolver aplica Rayleigh uma única vez.
    const kernel::SpMat C_discrete(sys.redSys.K_ff.rows(), sys.redSys.K_ff.cols());
    return kernel::DynamicSolver::solveSeismicTimeHistory(
        sys.redSys.K_ff, sys.M_ff, C_discrete, sys.infl, sys.redSys, groundAccelMS2Fn, opts, stopToken);
}

kernel::TransientResult FemSolver3D::solveTransient(
    const StructuralModel& model,
    const kernel::LoadHistory& history,
    const kernel::TransientOptions& opts,
    const std::stop_token& stopToken)
{
    return solveTransient(
        model,
        [&history](double timeSec, int /*stepIdx*/) -> double {
            return history.evaluateAt(timeSec);
        },
        history.loadCaseId,
        opts,
        stopToken);
}

kernel::TransientResult FemSolver3D::solveTransient(
    const StructuralModel& model,
    const std::function<double(double timeSec, int stepIdx)>& loadScaleFn,
    kernel::LoadCaseId baseLoadCaseId,
    const kernel::TransientOptions& opts,
    const std::stop_token& stopToken)
{
    kernel::TransientResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Análise transiente cancelada pelo usuário.";
        return out;
    }
    if (!loadScaleFn) {
        out.errorMessage = "Função temporal de carregamento nula.";
        return out;
    }

    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }

    kernel::VecX baseRhs_f = sys.redSys.rhs_f;
    if (baseLoadCaseId > 0 && !model.getLoadCases().empty()) {
        const kernel::LoadCase* foundCase = nullptr;
        for (const auto& lc : model.getLoadCases()) {
            if (lc.id == baseLoadCaseId) {
                foundCase = &lc;
                break;
            }
        }
        if (!foundCase) {
            out.errorMessage = "Caso de carga base ID " + std::to_string(baseLoadCaseId)
                             + " não encontrado para integração transiente.";
            return out;
        }
        std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
        std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
        extractCaseLoadMaps(model, sys.mapping, *foundCase, elemLoads, nodalLoads);

        kernel::SpMat K_unused;
        kernel::VecX F_case;
        if (!kernel::GlobalAssembler::assembleStiffnessAndLoad(
                sys.dofMgr, sys.kElements, elemLoads, nodalLoads, K_unused, F_case, &out.errorMessage)) {
            return out;
        }
        if (!kernel::GlobalAssembler::buildReducedRightHandSide(
                sys.dofMgr, sys.K_orig, F_case, sys.redSys, baseRhs_f, &out.errorMessage)) {
            return out;
        }
    }

    // Contrato [AUD-002]: passar matriz vazia de amortecedores discretos; DynamicSolver aplica Rayleigh uma única vez.
    const kernel::SpMat C_discrete(sys.redSys.K_ff.rows(), sys.redSys.K_ff.cols());

    return kernel::DynamicSolver::solveTransientForceHistory(
        sys.redSys.K_ff,
        sys.M_ff,
        C_discrete,
        sys.redSys,
        [&baseRhs_f, &loadScaleFn](double timeSec, int stepIdx) -> kernel::VecX {
            return loadScaleFn(timeSec, stepIdx) * baseRhs_f;
        },
        opts,
        stopToken);
}

bool FemSolver3D::applyBucklingImperfection(
    StructuralModel& model,
    const kernel::BucklingResult& buckling,
    int modeIndex,
    double amplitude,
    std::string* errorMsg)
{
    if (!buckling.success || buckling.modeShapesFull.empty()) {
        if (errorMsg) *errorMsg = "Resultado de flambagem inválido ou sem modos disponíveis.";
        return false;
    }
    if (modeIndex < 0 || modeIndex >= static_cast<int>(buckling.modeShapesFull.size())) {
        if (errorMsg) *errorMsg = "Índice de modo de flambagem fora do intervalo.";
        return false;
    }
    if (!std::isfinite(amplitude) || std::abs(amplitude) <= 0.0) {
        if (errorMsg) *errorMsg = "Amplitude de imperfeição geométrica deve ser não nula e finita.";
        return false;
    }

    kernel::DofManager dofMgr;
    UnifiedSolverMapping mapping;
    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, dofMgr, mapping, restrainedCount, springCount, errorMsg)) {
        return false;
    }

    const kernel::VecX& modeFull = buckling.modeShapesFull[static_cast<size_t>(modeIndex)];
    if (modeFull.size() != dofMgr.totalDofs()) {
        if (errorMsg) *errorMsg = "Dimensão do modo de flambagem não corresponde aos DOFs atuais do modelo.";
        return false;
    }

    // Indexa posições atuais na exata ordem de dofMgr.nodeStates()
    std::unordered_map<kernel::NodeId, kernel::Vec3> currentPos;
    const auto& states = dofMgr.nodeStates();
    currentPos.reserve(states.size());
    for (const auto& n : model.getNodes()) {
        currentPos[n.id] = kernel::Vec3(n.position.x, n.position.y, n.position.z);
    }
    for (size_t i = 0; i < model.surfaceMesh().nodes.size(); ++i) {
        const kernel::NodeId kid = mapping.kernelNodeForMeshIdx[i];
        currentPos.try_emplace(kid, model.surfaceMesh().nodes[i].position);
    }

    std::vector<kernel::Vec3> baseCoords;
    baseCoords.reserve(states.size());
    for (const auto& st : states) {
        auto it = currentPos.find(st.nodeId);
        baseCoords.push_back((it != currentPos.end()) ? it->second : kernel::Vec3::Zero());
    }

    const std::vector<kernel::Vec3> perturbed =
        kernel::BucklingSolver::applyGeometricImperfection(baseCoords, dofMgr, modeFull, amplitude);

    std::unordered_map<kernel::NodeId, kernel::Vec3> updatedPos;
    updatedPos.reserve(states.size());
    for (size_t i = 0; i < states.size() && i < perturbed.size(); ++i) {
        updatedPos[states[i].nodeId] = perturbed[i];
    }

    for (auto& n : model.getNodes()) {
        if (auto it = updatedPos.find(n.id); it != updatedPos.end()) {
            n.position = Vec3{it->second.x(), it->second.y(), it->second.z()};
        }
    }
    for (size_t i = 0; i < model.surfaceMesh().nodes.size(); ++i) {
        const kernel::NodeId kid = mapping.kernelNodeForMeshIdx[i];
        if (auto it = updatedPos.find(kid); it != updatedPos.end()) {
            model.surfaceMesh().nodes[i].position = it->second;
        }
    }

    model.clearResults();
    return true;
}

FemSolver3D::NodalDisplacementLayout FemSolver3D::buildNodalDisplacementLayout(
    const StructuralModel& model)
{
    NodalDisplacementLayout layout;
    kernel::DofManager dofMgr;
    UnifiedSolverMapping mapping;
    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, dofMgr, mapping, restrainedCount, springCount, nullptr)) {
        return layout;
    }

    layout.expectedDofs = dofMgr.totalDofs();
    const auto& nodes = model.getNodes();
    layout.frameEntries.reserve(nodes.size());
    for (const auto& n : nodes) {
        layout.frameEntries.push_back(NodalDisplacementEntry{
            n.id,
            dofMgr.getNodeGlobalDof(n.id, 0),
            dofMgr.getNodeGlobalDof(n.id, 1),
            dofMgr.getNodeGlobalDof(n.id, 2),
        });
    }

    const auto& mNodes = model.surfaceMesh().nodes;
    layout.meshEntries.reserve(mNodes.size());
    for (size_t i = 0; i < mNodes.size(); ++i) {
        const kernel::NodeId kid = mapping.kernelNodeForMeshIdx[i];
        layout.meshEntries.push_back(NodalDisplacementEntry{
            mNodes[i].id,
            dofMgr.getNodeGlobalDof(kid, 0),
            dofMgr.getNodeGlobalDof(kid, 1),
            dofMgr.getNodeGlobalDof(kid, 2),
        });
    }

    layout.valid = true;
    return layout;
}

double FemSolver3D::computeMaxTranslationalNorm(
    const NodalDisplacementLayout& layout,
    const StructuralModel& model,
    const kernel::VecX& uFull)
{
    if (uFull.size() == 0) {
        return 0.0;
    }

    const Eigen::Index nDofs = uFull.size();
    auto evalEntryNormSq = [&](const NodalDisplacementEntry& e) -> double {
        const double ux = (e.dx >= 0 && e.dx < nDofs) ? uFull(e.dx) : 0.0;
        const double uy = (e.dy >= 0 && e.dy < nDofs) ? uFull(e.dy) : 0.0;
        const double uz = (e.dz >= 0 && e.dz < nDofs) ? uFull(e.dz) : 0.0;
        return ux * ux + uy * uy + uz * uz;
    };

    if (layout.valid && nDofs == static_cast<Eigen::Index>(layout.expectedDofs)) {
        double maxSq = 0.0;
        for (const auto& e : layout.frameEntries) {
            maxSq = std::max(maxSq, evalEntryNormSq(e));
        }
        for (const auto& e : layout.meshEntries) {
            maxSq = std::max(maxSq, evalEntryNormSq(e));
        }
        return std::sqrt(maxSq);
    }

    const auto& nodes = model.getNodes();
    if (nDofs >= static_cast<Eigen::Index>(nodes.size() * 6)) {
        double maxSq = 0.0;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const Eigen::Index base = static_cast<Eigen::Index>(i * 6);
            const double ux = uFull(base);
            const double uy = uFull(base + 1);
            const double uz = uFull(base + 2);
            maxSq = std::max(maxSq, ux * ux + uy * uy + uz * uz);
        }
        return std::sqrt(maxSq);
    }
    return 0.0;
}

FemSolver3D::NodalVectorField FemSolver3D::extractNodalDisplacements(
    const NodalDisplacementLayout& layout,
    const StructuralModel& model,
    const kernel::VecX& uFull)
{
    NodalVectorField out;
    if (uFull.size() == 0) {
        return out;
    }

    const Eigen::Index nDofs = uFull.size();
    if (layout.valid && nDofs == static_cast<Eigen::Index>(layout.expectedDofs)) {
        auto readVec = [&](const NodalDisplacementEntry& e) -> Vec3 {
            const double ux = (e.dx >= 0 && e.dx < nDofs) ? uFull(e.dx) : 0.0;
            const double uy = (e.dy >= 0 && e.dy < nDofs) ? uFull(e.dy) : 0.0;
            const double uz = (e.dz >= 0 && e.dz < nDofs) ? uFull(e.dz) : 0.0;
            return Vec3(ux, uy, uz);
        };

        out.frameNodes.reserve(layout.frameEntries.size());
        for (const auto& e : layout.frameEntries) {
            const Vec3 v = readVec(e);
            out.frameNodes[e.id] = v;
            out.maxTranslationalNorm = std::max(out.maxTranslationalNorm, v.norm());
        }

        out.meshNodes.reserve(layout.meshEntries.size());
        for (const auto& e : layout.meshEntries) {
            const Vec3 v = readVec(e);
            out.meshNodes[e.id] = v;
            out.maxTranslationalNorm = std::max(out.maxTranslationalNorm, v.norm());
        }
        return out;
    }

    // Fallback seguro para vetores puramente nodais (6 DOFs por nó)
    const auto& nodes = model.getNodes();
    if (nDofs >= static_cast<Eigen::Index>(nodes.size() * 6)) {
        out.frameNodes.reserve(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            const Eigen::Index base = static_cast<Eigen::Index>(i * 6);
            const Vec3 v(uFull(base), uFull(base + 1), uFull(base + 2));
            out.frameNodes[nodes[i].id] = v;
            out.maxTranslationalNorm = std::max(out.maxTranslationalNorm, v.norm());
        }
    }
    return out;
}

FemSolver3D::NodalVectorField FemSolver3D::extractNodalDisplacements(
    const StructuralModel& model,
    const kernel::VecX& uFull)
{
    return extractNodalDisplacements(buildNodalDisplacementLayout(model), model, uFull);
}

} // namespace omniframe
