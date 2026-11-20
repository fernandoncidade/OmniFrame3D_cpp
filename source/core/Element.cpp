#include "Element.hpp"
#include "StructuralModel.hpp"
#include "kernel/elements/BeamTruss1D.hpp"
#include <cmath>
#include <vector>
#include <Eigen/LU>

namespace omniframe {

double Element::computeLength(const StructuralModel& model) const {
    const Node* n1 = model.findNode(nodeStart);
    const Node* n2 = model.findNode(nodeEnd);
    if (!n1 || !n2) return 0.0;
    double len = n1->position.distanceTo(n2->position);
    return std::isfinite(len) ? len : 0.0;
}

Vec3 Element::computeDirection(const StructuralModel& model) const {
    const Node* n1 = model.findNode(nodeStart);
    const Node* n2 = model.findNode(nodeEnd);
    if (!n1 || !n2) return {1.0, 0.0, 0.0};
    Vec3 diff = n2->position - n1->position;
    double len = diff.norm();
    if (!std::isfinite(len) || len <= 1e-12) return {1.0, 0.0, 0.0};
    return diff / len;
}

Eigen::Matrix3d Element::computeRotationMatrix3x3(const StructuralModel& model) const {
    Vec3 vx = computeDirection(model);
    double cx = vx.x;
    double cy = vx.y;
    double cz = vx.z;

    Eigen::Matrix3d R0 = Eigen::Matrix3d::Zero();

    // Verificação de barra vertical (paralela ao eixo Z global)
    double horizNorm = std::sqrt(cx * cx + cy * cy);
    if (horizNorm < 1e-6) {
        // Barra vertical: adota o eixo Y global como referência
        // R0 transforma coordenadas globais em locais: v_loc = R0 * v_glob
        // Se cz > 0, eixo local x aponta para +Z, local y para +Y, local z para -X
        // Matriz ortonormal destra com det = +1:
        double sgn = (cz > 0.0) ? 1.0 : -1.0;
        R0(0, 2) = sgn;
        R0(1, 1) = 1.0;
        R0(2, 0) = -sgn;
    } else {
        // Barra não vertical: usa k = (0, 0, 1) para base ortonormal
        Vec3 k(0.0, 0.0, 1.0);
        Vec3 vy = k.cross(vx).normalized();
        Vec3 vz = vx.cross(vy).normalized();

        R0(0, 0) = vx.x; R0(0, 1) = vx.y; R0(0, 2) = vx.z;
        R0(1, 0) = vy.x; R0(1, 1) = vy.y; R0(1, 2) = vy.z;
        R0(2, 0) = vz.x; R0(2, 1) = vz.y; R0(2, 2) = vz.z;
    }

    // Ângulo de giro (roll gamma) em torno do eixo x local da barra
    double cg = std::cos(rollAngle);
    double sg = std::sin(rollAngle);

    Eigen::Matrix3d Rgamma = Eigen::Matrix3d::Identity();
    Rgamma(1, 1) = cg;
    Rgamma(1, 2) = sg;
    Rgamma(2, 1) = -sg;
    Rgamma(2, 2) = cg;

    return Rgamma * R0;
}

Eigen::Matrix<double, 12, 12> Element::computeTransformationMatrix(const StructuralModel& model) const {
    Eigen::Matrix3d R = computeRotationMatrix3x3(model);
    Eigen::Matrix<double, 12, 12> T = Eigen::Matrix<double, 12, 12>::Zero();
    T.block<3, 3>(0, 0) = R;
    T.block<3, 3>(3, 3) = R;
    T.block<3, 3>(6, 6) = R;
    T.block<3, 3>(9, 9) = R;
    return T;
}

namespace {

struct ReleasePartition {
    std::vector<int> ret;
    std::vector<int> rel;
    Eigen::MatrixXd Kaa;
    Eigen::MatrixXd Kab;
    Eigen::MatrixXd Kba;
    Eigen::MatrixXd Kbb;
};

ReleasePartition partitionStiffness(const Eigen::Matrix<double, 12, 12>& k0, const ElementReleases& releases) {
    ReleasePartition p;
    for (int i = 0; i < 6; ++i) {
        if (releases.start[i]) p.rel.push_back(i);
        else p.ret.push_back(i);
    }
    for (int i = 0; i < 6; ++i) {
        if (releases.end[i]) p.rel.push_back(i + 6);
        else p.ret.push_back(i + 6);
    }

    int nA = static_cast<int>(p.ret.size());
    int nB = static_cast<int>(p.rel.size());
    p.Kaa.resize(nA, nA);
    p.Kab.resize(nA, nB);
    p.Kba.resize(nB, nA);
    p.Kbb.resize(nB, nB);

    for (int i = 0; i < nA; ++i) {
        for (int j = 0; j < nA; ++j) {
            p.Kaa(i, j) = k0(p.ret[i], p.ret[j]);
        }
        for (int j = 0; j < nB; ++j) {
            p.Kab(i, j) = k0(p.ret[i], p.rel[j]);
        }
    }
    for (int i = 0; i < nB; ++i) {
        for (int j = 0; j < nA; ++j) {
            p.Kba(i, j) = k0(p.rel[i], p.ret[j]);
        }
        for (int j = 0; j < nB; ++j) {
            p.Kbb(i, j) = k0(p.rel[i], p.rel[j]);
        }
    }
    return p;
}

Eigen::MatrixXd solveKbb(const Eigen::MatrixXd& Kbb, const Eigen::MatrixXd& rhs) {
    Eigen::FullPivLU<Eigen::MatrixXd> lu(Kbb);
    lu.setThreshold(1e-12);
    if (lu.isInvertible()) {
        return lu.solve(rhs);
    }
    // Fallback pseudo-inversa caso ambos os extremos tenham liberações simultâneas no mesmo modo
    Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXd> cod(Kbb);
    cod.setThreshold(1e-12);
    return cod.solve(rhs);
}

} // namespace

namespace {
kernel::Beam1DProps makeBeamProps(const Element& elem, const StructuralModel& model, double L) {
    const Material* mat = model.findMaterial(elem.materialId);
    const CrossSection* sec = model.findCrossSection(elem.sectionId);
    kernel::Beam1DProps p;
    p.L = L;
    p.E = mat ? mat->E : 2.0e8;
    p.G = mat ? mat->G() : (p.E / (2.0 * (1.0 + 0.3)));
    p.alpha = mat ? mat->alpha : 1.2e-5;
    p.rhoConsistent = mat ? mat->consistentMassDensity() : 7.85;
    p.gammaKNM3 = mat ? mat->gamma : 78.5;
    p.A = sec ? sec->A : 0.08;
    p.Iy = sec ? sec->Iy : 1e-3;
    p.Iz = sec ? sec->Iz : 1e-3;
    p.Iyz = sec ? sec->Iyz : 0.0;
    p.J = sec ? sec->J : 5e-4;
    p.Asy = (sec && sec->Asy > 0.0) ? sec->Asy : ((5.0 / 6.0) * p.A);
    p.Asz = (sec && sec->Asz > 0.0) ? sec->Asz : ((5.0 / 6.0) * p.A);
    p.formulation = static_cast<kernel::Beam1DFormulation>(static_cast<int>(elem.formulation));
    return p;
}
} // namespace

Eigen::Matrix<double, 12, 12> Element::computeUncondensedLocalStiffnessMatrix(const StructuralModel& model) const {
    double L = computeLength(model);
    if (!std::isfinite(L) || L <= 1e-12) {
        return Eigen::Matrix<double, 12, 12>::Zero();
    }
    return kernel::BeamTruss1D::computeUncondensedLocalStiffness(makeBeamProps(*this, model, L));
}

Eigen::Matrix<double, 12, 12> Element::computeLocalStiffnessMatrix(const StructuralModel& model) const {
    Eigen::Matrix<double, 12, 12> k0 = computeUncondensedLocalStiffnessMatrix(model);

    if (!releases.hasAnyRelease()) {
        return k0;
    }

    ReleasePartition p = partitionStiffness(k0, releases);
    if (p.rel.empty()) return k0;

    Eigen::Matrix<double, 12, 12> kFinal = Eigen::Matrix<double, 12, 12>::Zero();
    int nA = static_cast<int>(p.ret.size());

    if (nA > 0) {
        Eigen::MatrixXd Kred = p.Kaa - p.Kab * solveKbb(p.Kbb, p.Kba);
        for (int i = 0; i < nA; ++i) {
            for (int j = 0; j < nA; ++j) {
                kFinal(p.ret[i], p.ret[j]) = Kred(i, j);
            }
        }
    }

    return kFinal;
}

Eigen::Matrix<double, 12, 12> Element::computeGlobalStiffnessMatrix(const StructuralModel& model) const {
    Eigen::Matrix<double, 12, 12> T = computeTransformationMatrix(model);
    Eigen::Matrix<double, 12, 12> kLoc = computeLocalStiffnessMatrix(model);
    return T.transpose() * kLoc * T;
}

Eigen::Matrix<double, 12, 1> Element::computeUncondensedLocalFixedEndForces(const StructuralModel& model) const {
    Eigen::Matrix<double, 12, 1> f = Eigen::Matrix<double, 12, 1>::Zero();
    double L = computeLength(model);

    Vec3 qLoc{0.0, 0.0, 0.0};
    if (distLoad.active) {
        if (distLoad.coordSystem == CoordSystem::Global) {
            Eigen::Matrix3d R = computeRotationMatrix3x3(model);
            Eigen::Vector3d qg(distLoad.w.x, distLoad.w.y, distLoad.w.z);
            Eigen::Vector3d ql = R * qg;
            qLoc = {ql.x(), ql.y(), ql.z()};
        } else {
            qLoc = distLoad.w;
        }
    }

    // 1. Cargas Axiais qx
    if (std::abs(qLoc.x) > 1e-9) {
        f(0) += -qLoc.x * L / 2.0;
        f(6) += -qLoc.x * L / 2.0;
    }

    // 2. Flexão em x-y por qy
    if (std::abs(qLoc.y) > 1e-9) {
        f(1)  += -qLoc.y * L / 2.0;
        f(7)  += -qLoc.y * L / 2.0;
        if (formulation != ElementFormulation::Truss3D) {
            f(5)  += -qLoc.y * L * L / 12.0;
            f(11) +=  qLoc.y * L * L / 12.0;
        }
    }

    // 3. Flexão em x-z por qz
    if (std::abs(qLoc.z) > 1e-9) {
        f(2)  += -qLoc.z * L / 2.0;
        f(8)  += -qLoc.z * L / 2.0;
        if (formulation != ElementFormulation::Truss3D) {
            f(4)  +=  qLoc.z * L * L / 12.0;
            f(10) += -qLoc.z * L * L / 12.0;
        }
    }

    // 4. Variação Térmica deltaT
    if (thermalLoad.active && std::abs(thermalLoad.deltaT) > 1e-6) {
        const Material* mat = model.findMaterial(materialId);
        double E = mat ? mat->E : 2.0e8;
        double alpha = mat ? mat->alpha : 1.2e-5;
        const CrossSection* sec = model.findCrossSection(sectionId);
        double A = sec ? sec->A : 0.08;

        double Nth = E * A * alpha * thermalLoad.deltaT;
        f(0) +=  Nth;
        f(6) += -Nth;
    }

    return f;
}

Eigen::Matrix<double, 12, 1> Element::computeLocalFixedEndForces(const StructuralModel& model) const {
    Eigen::Matrix<double, 12, 1> f = computeUncondensedLocalFixedEndForces(model);

    // Condensação estática usando obrigatoriamente a matriz k0 NÃO condensada
    if (!releases.hasAnyRelease()) {
        return f;
    }

    Eigen::Matrix<double, 12, 12> k0 = computeUncondensedLocalStiffnessMatrix(model);
    ReleasePartition p = partitionStiffness(k0, releases);
    if (p.rel.empty()) return f;

    int nA = static_cast<int>(p.ret.size());
    int nB = static_cast<int>(p.rel.size());
    if (nA == 0) {
        return Eigen::Matrix<double, 12, 1>::Zero();
    }

    Eigen::VectorXd fa(nA), fb(nB);
    for (int i = 0; i < nA; ++i) fa(i) = f(p.ret[i]);
    for (int i = 0; i < nB; ++i) fb(i) = f(p.rel[i]);

    Eigen::VectorXd fred = fa - p.Kab * solveKbb(p.Kbb, fb);
    Eigen::Matrix<double, 12, 1> fNew = Eigen::Matrix<double, 12, 1>::Zero();
    for (int i = 0; i < nA; ++i) {
        fNew(p.ret[i]) = fred(i);
    }
    return fNew;
}

Eigen::Matrix<double, 12, 1> Element::recoverLocalElementDisplacements(
    const StructuralModel& model,
    const Eigen::Matrix<double, 12, 1>& uLocNodal) const
{
    if (!releases.hasAnyRelease()) {
        return uLocNodal;
    }

    Eigen::Matrix<double, 12, 12> k0 = computeUncondensedLocalStiffnessMatrix(model);
    Eigen::Matrix<double, 12, 1> f0 = computeUncondensedLocalFixedEndForces(model);
    ReleasePartition p = partitionStiffness(k0, releases);
    if (p.rel.empty()) {
        return uLocNodal;
    }

    int nA = static_cast<int>(p.ret.size());
    int nB = static_cast<int>(p.rel.size());

    Eigen::VectorXd uRet(nA), fb0(nB);
    for (int i = 0; i < nA; ++i) uRet(i) = uLocNodal(p.ret[i]);
    for (int i = 0; i < nB; ++i) fb0(i) = f0(p.rel[i]);

    Eigen::VectorXd rhs = -fb0;
    if (nA > 0) {
        rhs -= p.Kba * uRet;
    }

    Eigen::VectorXd uRel = solveKbb(p.Kbb, rhs);
    Eigen::Matrix<double, 12, 1> uRec = uLocNodal;
    for (int i = 0; i < nB; ++i) {
        uRec(p.rel[i]) = uRel(i);
    }
    return uRec;
}

Eigen::Matrix<double, 12, 1> Element::computeGlobalFixedEndForces(const StructuralModel& model) const {
    Eigen::Matrix<double, 12, 12> T = computeTransformationMatrix(model);
    Eigen::Matrix<double, 12, 1> fLoc = computeLocalFixedEndForces(model);
    return T.transpose() * fLoc;
}

InternalForces Element::evaluateAt(double xi, const StructuralModel& model) const {
    InternalForces res;
    double L = computeLength(model);
    if (!std::isfinite(L) || L <= 1e-12) {
        return res;
    }

    // Cargas locais distribuídas (ou recuperadas do equilíbrio das forças de extremidade pós-processadas em LoadCase)
    Vec3 qLoc{0.0, 0.0, 0.0};
    if (distLoad.active) {
        if (distLoad.coordSystem == CoordSystem::Global) {
            Eigen::Matrix3d R = computeRotationMatrix3x3(model);
            Eigen::Vector3d qg(distLoad.w.x, distLoad.w.y, distLoad.w.z);
            Eigen::Vector3d ql = R * qg;
            qLoc = {ql.x(), ql.y(), ql.z()};
        } else {
            qLoc = distLoad.w;
        }
    } else if (hasResults) {
        if (!(releases.start[0] && releases.end[0])) {
            qLoc.x = -(localEndForces(0) + localEndForces(6)) / L;
        }
        if (!(releases.start[1] && releases.end[1])) {
            qLoc.y = -(localEndForces(1) + localEndForces(7)) / L;
        }
        if (!(releases.start[2] && releases.end[2])) {
            qLoc.z = -(localEndForces(2) + localEndForces(8)) / L;
        }
    }

    const auto props = makeBeamProps(*this, model, L);
    const auto ev = kernel::BeamTruss1D::evaluateLocalDistribution(
        xi, props, {qLoc.x, qLoc.y, qLoc.z}, localDisplacements, localEndForces);

    res.N = ev.N;
    res.Vy = ev.Vy;
    res.Vz = ev.Vz;
    res.Tx = ev.Tx;
    res.My = ev.My;
    res.Mz = ev.Mz;
    res.defLocal = {ev.defLocal.x(), ev.defLocal.y(), ev.defLocal.z()};

    // Converte deslocamento local interpolado para coordenadas globais:
    Eigen::Matrix3d R = computeRotationMatrix3x3(model);
    Eigen::Vector3d dGlob = R.transpose() * ev.defLocal;
    res.defGlobal = {dGlob.x(), dGlob.y(), dGlob.z()};

    return res;
}

} // namespace omniframe
