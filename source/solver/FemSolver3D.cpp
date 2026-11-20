#include "FemSolver3D.hpp"
#include "kernel/core/DofManager.hpp"
#include "kernel/elements/BeamTruss1D.hpp"
#include "kernel/elements/IFiniteElement.hpp"
#include "kernel/assembly/GlobalAssembler.hpp"
#include "kernel/solver/LinearSolverSuite.hpp"
#include "kernel/postprocessing/PostProcessor.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

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

        const double L = m_elem.computeLength(m_model);
        if (L <= 1e-12) return kernel::MatX::Zero(12, 12);

        const auto T = m_elem.computeTransformationMatrix(m_model);
        const Eigen::Matrix<double, 12, 1> uLoc = T * elementGlobalDisp;
        const double axialStrain = (uLoc(6) - uLoc(0)) / L;
        double N_axial = (mat->E * sec->A) * axialStrain;
        if (m_elem.thermalLoad.active && std::abs(m_elem.thermalLoad.deltaT) > 0.0) {
            N_axial -= mat->E * sec->A * mat->alpha * m_elem.thermalLoad.deltaT;
        }

        kernel::Beam1DFormulation form = kernel::Beam1DFormulation::EulerBernoulli3D;
        if (m_elem.formulation == ElementFormulation::Timoshenko3D) form = kernel::Beam1DFormulation::Timoshenko3D;
        else if (m_elem.formulation == ElementFormulation::Truss3D) form = kernel::Beam1DFormulation::Truss3D;

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
                Element swCopy = m_elem;
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

bool setupDofManagerFromModel(const StructuralModel& model,
                              kernel::DofManager& dofMgr,
                              int& outRestrainedCount,
                              int& outSpringCount,
                              std::string* errMsg)
{
    dofMgr.clear();
    outRestrainedCount = 0;
    outSpringCount = 0;

    const std::array<bool, kernel::NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};
    for (const auto& n : model.getNodes()) {
        dofMgr.registerNode(n.id, mask6);
        for (int d = 0; d < 6; ++d) {
            const bool isFixed = n.support.hasPrescribed[d] || n.support.fixed[d];
            const double val = n.support.hasPrescribed[d] ? n.support.prescribedValue[d] : 0.0;
            const double spring = n.support.springStiffness[d];
            if (!std::isfinite(val) || !std::isfinite(spring) || spring < 0.0) {
                if (errMsg) *errMsg = "Rigidez de mola ou recalque inválido no nó " + std::to_string(n.id) + ".";
                return false;
            }
            if (isFixed) outRestrainedCount++;
            else if (spring > 1e-9) outSpringCount++;
            dofMgr.setBoundaryCondition(n.id, d, isFixed, val, (spring > 1e-9 ? spring : 0.0));
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

void buildKernelElements(const StructuralModel& model,
                         std::vector<std::shared_ptr<kernel::IFiniteElement>>& outElements)
{
    outElements.clear();
    outElements.reserve(model.getElements().size());
    for (const auto& elem : model.getElements()) {
        outElements.push_back(std::make_shared<Frame1DKernelElement>(model, elem));
    }
}

void extractLegacyLoadMaps(
    const StructuralModel& model,
    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput>& elemLoads,
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>>& nodalLoads)
{
    elemLoads.clear();
    nodalLoads.clear();
    for (const auto& n : model.getNodes()) {
        Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1> fv =
            Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>::Zero();
        fv(0) = n.load.force.x;
        fv(1) = n.load.force.y;
        fv(2) = n.load.force.z;
        fv(3) = n.load.moment.x;
        fv(4) = n.load.moment.y;
        fv(5) = n.load.moment.z;
        nodalLoads[n.id] = fv;
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
}

void extractCaseLoadMaps(
    const StructuralModel& model,
    const kernel::LoadCase& lc,
    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput>& elemLoads,
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>>& nodalLoads)
{
    elemLoads.clear();
    nodalLoads.clear();
    using NodalVec = Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>;
    for (const auto& nEntry : lc.nodalLoads) {
        // Fixed-size Eigen vectors default-construct uninitialized; zero-initialize on first insert.
        NodalVec& vec = nodalLoads.try_emplace(nEntry.nodeId, NodalVec::Zero()).first->second;
        for (int d = 0; d < 6; ++d) {
            vec(d) += nEntry.forceMoment[d];
        }
    }
    for (const auto& eEntry : lc.elementLoads) {
        elemLoads[eEntry.elementId] = eEntry.load;
    }
    if (lc.selfWeight.enabled && std::abs(lc.selfWeight.factor) > 0.0) {
        for (const auto& e : model.getElements()) {
            auto& inp = elemLoads[e.id];
            inp.hasBodyGravity = true;
            inp.gravityAccel = lc.selfWeight.direction * lc.selfWeight.factor;
        }
    }
}

void populatePostProcessingFromVectors(
    StructuralModel& model,
    const kernel::DofManager& dofMgr,
    const kernel::DofClassification& cls,
    const kernel::VecX& U_full,
    const kernel::VecX& R_full,
    const kernel::SpMat& K_orig,
    SolverResult& result,
    const kernel::LoadCase* activeLoadCase = nullptr)
{
    auto& nodes = model.getNodes();
    auto& elements = model.getElements();
    const int nNodes = static_cast<int>(nodes.size());

    std::unordered_map<NodeId, int> nodeIndex;
    for (int i = 0; i < nNodes; ++i) nodeIndex[nodes[i].id] = i;

    double maxDisp = 0.0;
    for (int i = 0; i < nNodes; ++i) {
        const auto& sup = nodes[i].support;
        for (int d = 0; d < 6; ++d) {
            const int g = dofMgr.getNodeGlobalDof(nodes[i].id, d);
            nodes[i].displacement[d] = (g >= 0 && g < U_full.size()) ? U_full(g) : 0.0;
            if (g >= 0 && g < R_full.size() && (cls.isRestrained[g] || cls.isMpcSlave[g])) {
                nodes[i].reaction[d] = R_full(g);
            } else if (sup.springStiffness[d] > 1e-9) {
                nodes[i].reaction[d] = -sup.springStiffness[d] * nodes[i].displacement[d];
            } else {
                nodes[i].reaction[d] = 0.0;
            }
        }
        maxDisp = std::max(maxDisp, nodes[i].displacementNorm());
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
        if (activeLoadCase) {
            evalElem.distLoad.active = false;
            evalElem.distLoad.w = {0.0, 0.0, 0.0};
            evalElem.thermalLoad.active = false;
            evalElem.thermalLoad.deltaT = 0.0;
            if (auto it = lcElemMap.find(elem.id); it != lcElemMap.end()) {
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

    // Verificação de equilíbrio estático global
    Vec3 sumAppliedF{0.0, 0.0, 0.0};
    Vec3 sumAppliedM{0.0, 0.0, 0.0};
    Vec3 sumReacF{0.0, 0.0, 0.0};
    Vec3 sumReacM{0.0, 0.0, 0.0};
    double refForceMag = 0.0;
    double refMomentMag = 0.0;

    for (const auto& n : nodes) {
        Vec3 nF = n.load.force;
        Vec3 nM = n.load.moment;
        if (activeLoadCase) {
            nF = {0.0, 0.0, 0.0};
            nM = {0.0, 0.0, 0.0};
            for (const auto& ne : activeLoadCase->nodalLoads) {
                if (ne.nodeId == n.id) {
                    nF += {ne.forceMoment[0], ne.forceMoment[1], ne.forceMoment[2]};
                    nM += {ne.forceMoment[3], ne.forceMoment[4], ne.forceMoment[5]};
                }
            }
        }
        sumAppliedF += nF;
        const Vec3 rCrossF = n.position.cross(nF);
        sumAppliedM += nM + rCrossF;
        refForceMag += nF.norm();
        refMomentMag += nM.norm() + rCrossF.norm();

        const Vec3 rF{n.reaction[0], n.reaction[1], n.reaction[2]};
        const Vec3 rM{n.reaction[3], n.reaction[4], n.reaction[5]};
        const Vec3 rCrossReac = n.position.cross(rF);
        sumReacF += rF;
        sumReacM += rM + rCrossReac;
        refForceMag += rF.norm();
        refMomentMag += rM.norm() + rCrossReac.norm();
    }

    for (const auto& elem : elements) {
        bool hasDist = elem.distLoad.active;
        CoordSystem cs = elem.distLoad.coordSystem;
        Vec3 w = elem.distLoad.w;
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
            }
        }
        if (!hasDist) continue;
        const double L = elem.computeLength(model);
        Vec3 qGlob{0.0, 0.0, 0.0};
        if (cs == CoordSystem::Global) {
            qGlob = w;
        } else {
            const Eigen::Matrix3d R = elem.computeRotationMatrix3x3(model);
            const Eigen::Vector3d ql(w.x, w.y, w.z);
            const Eigen::Vector3d qg = R.transpose() * ql;
            qGlob = {qg.x(), qg.y(), qg.z()};
        }
        const Vec3 totalF = qGlob * L;
        sumAppliedF += totalF;
        refForceMag += totalF.norm();

        const Node* n1 = model.findNode(elem.nodeStart);
        const Node* n2 = model.findNode(elem.nodeEnd);
        if (n1 && n2) {
            const Vec3 midPos = (n1->position + n2->position) * 0.5;
            const Vec3 mDist = midPos.cross(totalF);
            sumAppliedM += mDist;
            refMomentMag += mDist.norm();
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

} // namespace

SolverResult FemSolver3D::solve(StructuralModel& model) {
    return solveWithOptions(model, model.solverOptions());
}

SolverResult FemSolver3D::solveWithOptions(StructuralModel& model, const kernel::LinearSolverOptions& opts) {
    const auto startTime = std::chrono::high_resolution_clock::now();
    SolverResult result;
    model.clearResults();

    const ModelValidation val = model.validate();
    if (!val.valid) {
        result.success = false;
        result.errorMessage = val.errors.empty() ? "Erro de validação desconhecido." : val.errors[0];
        return result;
    }

    kernel::DofManager dofMgr;
    int restrainedCount = 0;
    int springCount = 0;
    std::string setupErr;
    if (!setupDofManagerFromModel(model, dofMgr, restrainedCount, springCount, &setupErr)) {
        result.success = false;
        result.errorMessage = setupErr;
        return result;
    }

    result.totalNodes = static_cast<int>(model.getNodes().size());
    result.totalElements = static_cast<int>(model.getElements().size());
    result.totalDofs = dofMgr.totalDofs();
    result.restrainedDofs = restrainedCount;

    if (restrainedCount == 0 && springCount == 0) {
        result.success = false;
        result.errorMessage = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return result;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    buildKernelElements(model, kElements);

    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
    extractLegacyLoadMaps(model, elemLoads, nodalLoads);

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
        U_free = kernel::LinearSolverSuite::solve(redSys.K_ff, redSys.rhs_f, opts, solverStats);
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

    populatePostProcessingFromVectors(model, dofMgr, cls, U_full, R_full, K_orig, result);
    result.solverStats = solverStats;

    const auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    result.success = true;
    return result;
}

bool FemSolver3D::solveLoadCasesAndCombinations(
    StructuralModel& model,
    const kernel::LinearSolverOptions& opts,
    std::vector<kernel::CaseDisplacementResult>& outCaseResults,
    std::vector<kernel::CaseDisplacementResult>& outComboResults,
    kernel::EnvelopeExtremes& outEnvelope,
    std::string* errorMsg)
{
    outCaseResults.clear();
    outComboResults.clear();
    outEnvelope = kernel::EnvelopeExtremes{};

    const ModelValidation val = model.validate();
    if (!val.valid) {
        if (errorMsg) *errorMsg = val.errors.empty() ? "Modelo inválido." : val.errors[0];
        return false;
    }

    kernel::DofManager dofMgr;
    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, dofMgr, restrainedCount, springCount, errorMsg)) {
        return false;
    }
    if (restrainedCount == 0 && springCount == 0) {
        if (errorMsg) *errorMsg = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return false;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    buildKernelElements(model, kElements);

    // Se o modelo ainda não tiver LoadCases explícitos, cria um caso implícito das cargas diretas
    std::vector<kernel::LoadCase> casesToSolve = model.getLoadCases();
    if (casesToSolve.empty()) {
        kernel::LoadCase fallback;
        fallback.id = 1;
        fallback.name = "Caso Único";
        for (const auto& n : model.getNodes()) {
            fallback.nodalLoads.push_back({n.id, {n.load.force.x, n.load.force.y, n.load.force.z,
                                                  n.load.moment.x, n.load.moment.y, n.load.moment.z}});
        }
        for (const auto& e : model.getElements()) {
            kernel::ElementLoadInput inp;
            inp.hasDistributed = e.distLoad.active;
            inp.coordSystem = (e.distLoad.coordSystem == CoordSystem::Local) ? 1 : 0;
            inp.distributedForce = {e.distLoad.w.x, e.distLoad.w.y, e.distLoad.w.z};
            inp.hasThermal = e.thermalLoad.active;
            inp.deltaT = e.thermalLoad.deltaT;
            fallback.elementLoads.push_back({e.id, inp});
        }
        casesToSolve.push_back(std::move(fallback));
    }

    kernel::FactorizationCache cache;
    kernel::SpMat K_orig;
    kernel::ReducedLinearSystem cachedSys;
    kernel::DofClassification cls;
    bool systemBuilt = false;

    std::unordered_map<kernel::LoadCaseId, kernel::VecX> caseForceVectors;

    for (const auto& lc : casesToSolve) {
        std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
        std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
        extractCaseLoadMaps(model, lc, elemLoads, nodalLoads);

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
            kernel::SpMat K_unused;
            if (!kernel::GlobalAssembler::assembleStiffnessAndLoad(dofMgr, kElements, elemLoads, nodalLoads, K_unused, F_case, errorMsg)) {
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
            U_free = kernel::LinearSolverSuite::solve(cachedSys.K_ff, cachedSys.rhs_f, opts, stats, &cache);
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

    // Combinações lineares reutilizando a mesma fatoração cacheada em K_ff
    for (const auto& comb : model.getLoadCombinations()) {
        kernel::VecX F_comb = kernel::VecX::Zero(dofMgr.totalDofs());
        for (const auto& f : comb.factors) {
            auto it = caseForceVectors.find(f.loadCaseId);
            if (it != caseForceVectors.end()) {
                F_comb += f.gamma * it->second;
            }
        }

        kernel::VecX rhs_comb;
        if (!kernel::GlobalAssembler::buildReducedRightHandSide(dofMgr, K_orig, F_comb, cachedSys, rhs_comb, errorMsg)) {
            return false;
        }

        kernel::LinearSolverStats stats;
        kernel::VecX U_free;
        if (cachedSys.freeDofs > 0) {
            U_free = kernel::LinearSolverSuite::solve(cachedSys.K_ff, rhs_comb, opts, stats, &cache);
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
        cr.caseOrComboId = comb.id;
        cr.name = comb.name;
        cr.isCombination = true;
        cr.U_full = kernel::GlobalAssembler::reconstructFullDisplacements(cachedSys, U_free);
        cr.R_full = K_orig * cr.U_full - F_comb;
        cr.solverStats = stats;
        outComboResults.push_back(std::move(cr));
    }

    std::vector<std::pair<kernel::NodeId, std::array<int, 6>>> nodeDofMap;
    nodeDofMap.reserve(model.getNodes().size());
    for (const auto& n : model.getNodes()) {
        std::array<int, 6> dofs;
        for (int d = 0; d < 6; ++d) dofs[d] = dofMgr.getNodeGlobalDof(n.id, d);
        nodeDofMap.emplace_back(n.id, dofs);
    }

    const auto& envelopeSource = outComboResults.empty() ? outCaseResults : outComboResults;
    outEnvelope = kernel::LoadEnvelope::compute(envelopeSource, nodeDofMap);
    return true;
}

kernel::BucklingResult FemSolver3D::solveBuckling(
    StructuralModel& model,
    const kernel::BucklingOptions& opts)
{
    kernel::BucklingResult out;
    const ModelValidation val = model.validate();
    if (!val.valid) {
        out.errorMessage = val.errors.empty() ? "Erro de validação." : val.errors[0];
        return out;
    }

    kernel::DofManager dofMgr;
    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, dofMgr, restrainedCount, springCount, &out.errorMessage)) {
        return out;
    }
    if (restrainedCount == 0 && springCount == 0) {
        out.errorMessage = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return out;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    buildKernelElements(model, kElements);

    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
    extractLegacyLoadMaps(model, elemLoads, nodalLoads);

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
        redSys.K_ff, redSys.rhs_f, model.solverOptions(), linStats, nullptr);
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
    return kernel::BucklingSolver::solve(redSys.K_ff, Kg_ff, redSys, opts);
}

kernel::NonlinearResult FemSolver3D::solveNonlinear(
    StructuralModel& model,
    const kernel::NonlinearOptions& opts)
{
    kernel::NonlinearResult out;
    model.clearResults();

    const ModelValidation val = model.validate();
    if (!val.valid) {
        out.errorMessage = val.errors.empty() ? "Erro de validação." : val.errors[0];
        return out;
    }

    kernel::DofManager dofMgr;
    int restrainedCount = 0;
    int springCount = 0;
    if (!setupDofManagerFromModel(model, dofMgr, restrainedCount, springCount, &out.errorMessage)) {
        return out;
    }
    if (restrainedCount == 0 && springCount == 0) {
        out.errorMessage = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return out;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    buildKernelElements(model, kElements);

    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
    extractLegacyLoadMaps(model, elemLoads, nodalLoads);

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
    specs.reserve(model.getElements().size());
    for (const auto& elem : model.getElements()) {
        const Node* n1 = model.findNode(elem.nodeStart);
        const Node* n2 = model.findNode(elem.nodeEnd);
        const Material* mat = model.findMaterial(elem.materialId);
        const CrossSection* sec = model.findCrossSection(elem.sectionId);
        if (!n1 || !n2 || !mat || !sec) {
            out.errorMessage = "Referência inválida em elemento na análise não linear.";
            return out;
        }

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
        s.props.J = sec->J;
        s.props.Asy = sec->Asy;
        s.props.Asz = sec->Asz;
        switch (elem.formulation) {
            case ElementFormulation::Timoshenko3D: s.props.formulation = kernel::Beam1DFormulation::Timoshenko3D; break;
            case ElementFormulation::Truss3D: s.props.formulation = kernel::Beam1DFormulation::Truss3D; break;
            default: s.props.formulation = kernel::Beam1DFormulation::EulerBernoulli3D; break;
        }
        s.releaseStart = elem.releases.start;
        s.releaseEnd = elem.releases.end;

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
        specs.push_back(std::move(s));
    }

    kernel::FrameNonlinearModel nlModel(
        dofMgr, redSys, F_orig, std::move(specs), opts.kinematics);
    out = kernel::NonlinearSolver::solve(nlModel, opts);

    if (out.success && out.finalU_full.size() == dofMgr.totalDofs()) {
        std::unordered_map<NodeId, int> nodeIndex;
        auto& nodes = model.getNodes();
        for (int ni = 0; ni < static_cast<int>(nodes.size()); ++ni) {
            nodeIndex[nodes[ni].id] = ni;
            for (int d = 0; d < 6; ++d) {
                const int g = dofMgr.getNodeGlobalDof(nodes[ni].id, d);
                const double u = (g >= 0) ? out.finalU_full(g) : 0.0;
                const double r = (g >= 0 && out.finalReactionsFull.size() == dofMgr.totalDofs() &&
                                  (cls.isRestrained[g] || cls.isMpcSlave[g]))
                    ? out.finalReactionsFull(g) : 0.0;
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
            elems[i].localEndForces = lastEv[i].fIntLocal;
            elems[i].hasResults = true;
        }
    }

    return out;
}

namespace {

struct AssembledDynamicSystem {
    kernel::DofManager dofMgr;
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
    if (!setupDofManagerFromModel(model, sys.dofMgr, restrainedCount, springCount, &errOut)) {
        return false;
    }
    if (restrainedCount == 0 && springCount == 0) {
        errOut = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return false;
    }

    std::vector<std::shared_ptr<kernel::IFiniteElement>> kElements;
    buildKernelElements(model, kElements);

    std::unordered_map<kernel::ElementId, kernel::ElementLoadInput> elemLoads;
    std::unordered_map<kernel::NodeId, Eigen::Matrix<double, kernel::NODAL_MAX_DOFS, 1>> nodalLoads;
    extractLegacyLoadMaps(model, elemLoads, nodalLoads);

    if (!kernel::GlobalAssembler::assembleStiffnessAndLoad(
            sys.dofMgr, kElements, elemLoads, nodalLoads, sys.K_orig, sys.F_orig, &errOut)) {
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

    if (!kernel::GlobalAssembler::assembleMass(sys.dofMgr, kElements, true, {}, sys.M_full, &errOut)) {
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
    const kernel::ModalOptions& opts)
{
    kernel::ModalResult out;
    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }
    return kernel::DynamicSolver::solveModal(sys.redSys.K_ff, sys.M_ff, sys.infl, sys.redSys, opts);
}

kernel::HarmonicResult FemSolver3D::solveHarmonic(
    const StructuralModel& model,
    const kernel::HarmonicOptions& opts)
{
    kernel::HarmonicResult out;
    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }
    kernel::RayleighDampingParams damp;
    damp.alphaM = opts.rayleighAlphaM;
    damp.betaK = opts.rayleighBetaK;
    const kernel::SpMat C_ff = kernel::DynamicSolver::assembleRayleighDamping(sys.M_ff, sys.redSys.K_ff, damp);
    return kernel::DynamicSolver::solveHarmonic(sys.redSys.K_ff, sys.M_ff, C_ff, sys.redSys.rhs_f, sys.redSys, opts);
}

kernel::ResponseSpectrumResult FemSolver3D::solveResponseSpectrum(
    const StructuralModel& model,
    const kernel::ResponseSpectrumOptions& specOpts,
    const kernel::ModalOptions& modalOpts)
{
    kernel::ResponseSpectrumResult out;
    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }
    const kernel::ModalResult modal =
        kernel::DynamicSolver::solveModal(sys.redSys.K_ff, sys.M_ff, sys.infl, sys.redSys, modalOpts);
    if (!modal.success) {
        out.errorMessage = modal.errorMessage;
        return out;
    }
    return kernel::DynamicSolver::solveResponseSpectrum(sys.K_orig, modal, specOpts);
}

kernel::TransientResult FemSolver3D::solveSeismic(
    const StructuralModel& model,
    const std::function<kernel::Vec3(double timeSec, int stepIdx)>& groundAccelMS2Fn,
    const kernel::TransientOptions& opts)
{
    kernel::TransientResult out;
    AssembledDynamicSystem sys;
    if (!assembleDynamicBaseSystem(model, sys, out.errorMessage)) {
        return out;
    }
    kernel::RayleighDampingParams damp;
    damp.alphaM = opts.rayleighAlphaM;
    damp.betaK = opts.rayleighBetaK;
    const kernel::SpMat C_ff = kernel::DynamicSolver::assembleRayleighDamping(sys.M_ff, sys.redSys.K_ff, damp);
    return kernel::DynamicSolver::solveSeismicTimeHistory(
        sys.redSys.K_ff, sys.M_ff, C_ff, sys.infl, sys.redSys, groundAccelMS2Fn, opts);
}

} // namespace omniframe
