#include "Element.hpp"
#include "StructuralModel.hpp"
#include <cmath>
#include <vector>
#include <Eigen/LU>

namespace omniframe {

double Element::computeLength(const StructuralModel& model) const {
    const Node* n1 = model.findNode(nodeStart);
    const Node* n2 = model.findNode(nodeEnd);
    if (!n1 || !n2) return 1.0;
    double len = n1->position.distanceTo(n2->position);
    return std::max(len, 1e-6);
}

Vec3 Element::computeDirection(const StructuralModel& model) const {
    const Node* n1 = model.findNode(nodeStart);
    const Node* n2 = model.findNode(nodeEnd);
    if (!n1 || !n2) return {1.0, 0.0, 0.0};
    Vec3 diff = n2->position - n1->position;
    return diff.normalized();
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

Eigen::Matrix<double, 12, 12> Element::computeUncondensedLocalStiffnessMatrix(const StructuralModel& model) const {
    double L = computeLength(model);

    const Material* mat = model.findMaterial(materialId);
    double E = mat ? mat->E : 2.0e8;
    double nu = mat ? mat->nu : 0.3;
    double G = mat ? mat->G() : (E / (2.0 * (1.0 + nu)));

    const CrossSection* sec = model.findCrossSection(sectionId);
    double A = sec ? sec->A : 0.08;
    double Iy = sec ? sec->Iy : 1e-3;
    double Iz = sec ? sec->Iz : 1e-3;
    double J = sec ? sec->J : 5e-4;

    Eigen::Matrix<double, 12, 12> k0 = Eigen::Matrix<double, 12, 12>::Zero();

    // 1. Rigidez Axial (DOFs 0 e 6)
    double k_ax = (E * A) / L;
    k0(0, 0) =  k_ax; k0(0, 6) = -k_ax;
    k0(6, 0) = -k_ax; k0(6, 6) =  k_ax;

    // 2. Rigidez à Torção (DOFs 3 e 9)
    double k_tor = (G * J) / L;
    k0(3, 3) =  k_tor; k0(3, 9) = -k_tor;
    k0(9, 3) = -k_tor; k0(9, 9) =  k_tor;

    // 3. Flexão no plano local x-y em torno do eixo local z (DOFs 1, 5, 7, 11)
    double L2 = L * L;
    double L3 = L2 * L;
    double c1z = (12.0 * E * Iz) / L3;
    double c2z = (6.0 * E * Iz) / L2;
    double c3z = (4.0 * E * Iz) / L;
    double c4z = (2.0 * E * Iz) / L;

    k0(1, 1)  +=  c1z; k0(1, 5)  +=  c2z; k0(1, 7)  += -c1z; k0(1, 11) +=  c2z;
    k0(5, 1)  +=  c2z; k0(5, 5)  +=  c3z; k0(5, 7)  += -c2z; k0(5, 11) +=  c4z;
    k0(7, 1)  += -c1z; k0(7, 5)  += -c2z; k0(7, 7)  +=  c1z; k0(7, 11) += -c2z;
    k0(11, 1) +=  c2z; k0(11, 5) +=  c4z; k0(11, 7) += -c2z; k0(11, 11) +=  c3z;

    // 4. Flexão no plano local x-z em torno do eixo local y (DOFs 2, 4, 8, 10)
    // Sinais com theta_y = -dw/dx:
    double c1y = (12.0 * E * Iy) / L3;
    double c2y = (6.0 * E * Iy) / L2;
    double c3y = (4.0 * E * Iy) / L;
    double c4y = (2.0 * E * Iy) / L;

    k0(2, 2)  +=  c1y; k0(2, 4)  += -c2y; k0(2, 8)  += -c1y; k0(2, 10) += -c2y;
    k0(4, 2)  += -c2y; k0(4, 4)  +=  c3y; k0(4, 8)  +=  c2y; k0(4, 10) +=  c4y;
    k0(8, 2)  += -c1y; k0(8, 4)  +=  c2y; k0(8, 8)  +=  c1y; k0(8, 10) +=  c2y;
    k0(10, 2) += -c2y; k0(10, 4) +=  c4y; k0(10, 8) +=  c2y; k0(10, 10) +=  c3y;

    return k0;
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

    // Mantém pequena rigidez diagonal residual para DOFs liberados evitando pivô nulo em nós rotulados
    for (int idx : p.rel) {
        kFinal(idx, idx) = 1e-9;
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
        f(5)  += -qLoc.y * L * L / 12.0;
        f(7)  += -qLoc.y * L / 2.0;
        f(11) +=  qLoc.y * L * L / 12.0;
    }

    // 3. Flexão em x-z por qz
    if (std::abs(qLoc.z) > 1e-9) {
        f(2)  += -qLoc.z * L / 2.0;
        f(4)  +=  qLoc.z * L * L / 12.0;
        f(8)  += -qLoc.z * L / 2.0;
        f(10) += -qLoc.z * L * L / 12.0;
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
    double x = std::clamp(xi, 0.0, 1.0) * L;
    xi = std::clamp(xi, 0.0, 1.0);

    const Material* mat = model.findMaterial(materialId);
    double E = mat ? mat->E : 2.0e8;
    const CrossSection* sec = model.findCrossSection(sectionId);
    double A = sec ? sec->A : 0.08;
    double Iy = sec ? sec->Iy : 1e-3;
    double Iz = sec ? sec->Iz : 1e-3;

    // Cargas locais distribuídas
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

    // Forças nas extremidades locais obtidas do solver:
    // fLoc = [N1, Vy1, Vz1, Tx1, My1, Mz1, N2, Vy2, Vz2, Tx2, My2, Mz2]
    double N1   = localEndForces(0);
    double Vy1  = localEndForces(1);
    double Vz1  = localEndForces(2);
    double Tx1  = localEndForces(3);
    double My1  = localEndForces(4);
    double Mz1  = localEndForces(5);

    // Esforços internos no corte x:
    res.N  = -N1 - qLoc.x * x;
    res.Vy = -Vy1 - qLoc.y * x;
    res.Vz = -Vz1 - qLoc.z * x;
    res.Tx = -Tx1;
    res.Mz = -Mz1 + Vy1 * x + 0.5 * qLoc.y * x * x;
    res.My = -My1 - Vz1 * x - 0.5 * qLoc.z * x * x;

    // Interpolação de Deslocamentos Locais via Polinômios Cúbicos de Hermite:
    double u1   = localDisplacements(0);
    double v1   = localDisplacements(1);
    double w1   = localDisplacements(2);
    double rotx1 = localDisplacements(3);
    double th_y1 = localDisplacements(4); // theta_y = -dw/dx
    double th_z1 = localDisplacements(5); // theta_z = +dv/dx

    double u2   = localDisplacements(6);
    double v2   = localDisplacements(7);
    double w2   = localDisplacements(8);
    double rotx2 = localDisplacements(9);
    double th_y2 = localDisplacements(10);
    double th_z2 = localDisplacements(11);
    (void)rotx1;
    (void)rotx2;

    // Funções de forma de Hermite em xi in [0, 1]:
    double N_1 = 1.0 - 3.0 * xi * xi + 2.0 * xi * xi * xi;
    double N_2 = L * (xi - 2.0 * xi * xi + xi * xi * xi);
    double N_3 = 3.0 * xi * xi - 2.0 * xi * xi * xi;
    double N_4 = L * (-xi * xi + xi * xi * xi);

    // Deslocamento axial u(x)
    double ux = (1.0 - xi) * u1 + xi * u2;
    if (std::abs(qLoc.x) > 1e-9 && E * A > 1e-6) {
        ux += (qLoc.x * L * L) / (2.0 * E * A) * xi * (1.0 - xi);
    }

    // Deslocamento transversal local v(x) em y
    double vy = N_1 * v1 + N_2 * th_z1 + N_3 * v2 + N_4 * th_z2;
    if (std::abs(qLoc.y) > 1e-9 && E * Iz > 1e-6) {
        vy += (qLoc.y * std::pow(L, 4)) / (24.0 * E * Iz) * (xi * xi) * std::pow(1.0 - xi, 2);
    }

    // Deslocamento transversal local w(x) em z (dw/dx = -th_y)
    double wz = N_1 * w1 + N_2 * (-th_y1) + N_3 * w2 + N_4 * (-th_y2);
    if (std::abs(qLoc.z) > 1e-9 && E * Iy > 1e-6) {
        wz += (qLoc.z * std::pow(L, 4)) / (24.0 * E * Iy) * (xi * xi) * std::pow(1.0 - xi, 2);
    }

    res.defLocal = {ux, vy, wz};

    // Converte deslocamento local interpolado para coordenadas globais:
    Eigen::Matrix3d R = computeRotationMatrix3x3(model);
    Eigen::Vector3d dLoc(ux, vy, wz);
    Eigen::Vector3d dGlob = R.transpose() * dLoc;
    res.defGlobal = {dGlob.x(), dGlob.y(), dGlob.z()};

    return res;
}

} // namespace omniframe
