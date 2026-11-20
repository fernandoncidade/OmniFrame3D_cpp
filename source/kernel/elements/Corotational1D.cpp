#include "Corotational1D.hpp"
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>

namespace omniframe::kernel {

namespace {

inline Mat3 skewMatrix(const Vec3& v) {
    Mat3 W;
    W <<     0.0, -v.z(),  v.y(),
          v.z(),     0.0, -v.x(),
         -v.y(),  v.x(),     0.0;
    return W;
}

Eigen::Matrix<double, 12, 12> applyStaticReleaseCondensation(
    const Eigen::Matrix<double, 12, 12>& k,
    const std::array<bool, 6>& relStart,
    const std::array<bool, 6>& relEnd,
    Eigen::Matrix<double, 12, 1>* fInOut = nullptr)
{
    std::array<bool, 12> isRel{};
    int relCount = 0;
    for (int i = 0; i < 6; ++i) {
        isRel[i] = relStart[i];
        isRel[i + 6] = relEnd[i];
        if (isRel[i]) ++relCount;
        if (isRel[i + 6]) ++relCount;
    }
    if (relCount == 0) return k;

    std::vector<int> keepIdx;
    std::vector<int> relIdx;
    keepIdx.reserve(12 - relCount);
    relIdx.reserve(relCount);
    for (int i = 0; i < 12; ++i) {
        if (isRel[i]) relIdx.push_back(i);
        else keepIdx.push_back(i);
    }

    const int nKeep = static_cast<int>(keepIdx.size());
    const int nRel = static_cast<int>(relIdx.size());
    if (nKeep == 0) {
        if (fInOut) fInOut->setZero();
        return Eigen::Matrix<double, 12, 12>::Zero();
    }

    MatX k_rr(nRel, nRel);
    MatX k_kr(nKeep, nRel);
    MatX k_kk(nKeep, nKeep);
    VecX f_r(nRel);
    VecX f_k(nKeep);

    for (int i = 0; i < nRel; ++i) {
        if (fInOut) f_r(i) = (*fInOut)(relIdx[i]);
        for (int j = 0; j < nRel; ++j) k_rr(i, j) = k(relIdx[i], relIdx[j]);
    }
    for (int i = 0; i < nKeep; ++i) {
        if (fInOut) f_k(i) = (*fInOut)(keepIdx[i]);
        for (int j = 0; j < nRel; ++j) k_kr(i, j) = k(keepIdx[i], relIdx[j]);
        for (int j = 0; j < nKeep; ++j) k_kk(i, j) = k(keepIdx[i], keepIdx[j]);
    }

    Eigen::FullPivLU<MatX> lu(k_rr);
    Eigen::Matrix<double, 12, 12> kOut = Eigen::Matrix<double, 12, 12>::Zero();
    if (lu.isInvertible()) {
        MatX kRed = k_kk - k_kr * lu.solve(k_kr.transpose());
        for (int i = 0; i < nKeep; ++i) {
            for (int j = 0; j < nKeep; ++j) {
                kOut(keepIdx[i], keepIdx[j]) = kRed(i, j);
            }
        }
        if (fInOut) {
            VecX fRed = f_k - k_kr * lu.solve(f_r);
            fInOut->setZero();
            for (int i = 0; i < nKeep; ++i) {
                (*fInOut)(keepIdx[i]) = fRed(i);
            }
        }
    } else {
        for (int i = 0; i < nKeep; ++i) {
            for (int j = 0; j < nKeep; ++j) kOut(keepIdx[i], keepIdx[j]) = k(keepIdx[i], keepIdx[j]);
        }
        if (fInOut) {
            for (int r : relIdx) (*fInOut)(r) = 0.0;
        }
    }
    return kOut;
}

} // namespace

Mat3 CorotationalBeam3D::expSO3(const Vec3& theta) {
    const double phi2 = theta.squaredNorm();
    const Mat3 W = skewMatrix(theta);
    if (phi2 < 1e-12) {
        return Mat3::Identity() + (1.0 - phi2 / 6.0) * W + (0.5 - phi2 / 24.0) * (W * W);
    }
    const double phi = std::sqrt(phi2);
    const double s = std::sin(phi) / phi;
    const double c = (1.0 - std::cos(phi)) / phi2;
    return Mat3::Identity() + s * W + c * (W * W);
}

Vec3 CorotationalBeam3D::logSO3(const Mat3& R) {
    const double tr = std::clamp(R.trace(), -1.0 + 1e-15, 3.0);
    const double cosPhi = std::clamp(0.5 * (tr - 1.0), -1.0, 1.0);
    const Vec3 axialVec(R(2, 1) - R(1, 2), R(0, 2) - R(2, 0), R(1, 0) - R(0, 1));
    if (cosPhi > 1.0 - 1e-10) {
        const double phi2 = axialVec.squaredNorm() * 0.25;
        const double factor = 0.5 * (1.0 + phi2 / 6.0);
        return factor * axialVec;
    }
    const Eigen::AngleAxisd aa(R);
    return aa.angle() * aa.axis();
}

std::vector<SectionFiber> CorotationalBeam3D::buildDefaultFibers(
    double A, double Iy, double Iz, int ny, int nz)
{
    ny = std::max(2, ny);
    nz = std::max(2, nz);
    // Dimensões retangulares equivalentes que reproduzem exatamente A, Iy e Iz:
    // A = b*h, Iz = b*h^3/12 => h = sqrt(12 * Iz / A), b = sqrt(12 * Iy / A)
    // E para compensar discretização de ponto médio em n divisões, corrigimos coordenadas
    const double h = (A > 1e-18 && Iz > 1e-24) ? std::sqrt(12.0 * Iz / A) : std::sqrt(std::max(1e-12, A));
    const double b = (A > 1e-18 && Iy > 1e-24) ? std::sqrt(12.0 * Iy / A) : std::sqrt(std::max(1e-12, A));

    // Coordenadas de ponto médio em [-h/2, h/2] possuem momento de inércia discreto (1 - 1/n^2) do exato.
    // Fator corretivo torna Sum(y^2 dA) == Iz e Sum(z^2 dA) == Iy exatos para qualquer ny, nz >= 2!
    const double scaleY = (ny > 1) ? 1.0 / std::sqrt(1.0 - 1.0 / (double(ny) * ny)) : 1.0;
    const double scaleZ = (nz > 1) ? 1.0 / std::sqrt(1.0 - 1.0 / (double(nz) * nz)) : 1.0;

    std::vector<SectionFiber> out;
    out.reserve(static_cast<size_t>(ny) * nz);
    const double dA = A / static_cast<double>(ny * nz);
    const double dy = h / static_cast<double>(ny);
    const double dz = b / static_cast<double>(nz);

    for (int iy = 0; iy < ny; ++iy) {
        const double y = (-0.5 * h + (iy + 0.5) * dy) * scaleY;
        for (int iz = 0; iz < nz; ++iz) {
            const double z = (-0.5 * b + (iz + 0.5) * dz) * scaleZ;
            out.push_back({y, z, dA});
        }
    }
    return out;
}

Corotational1DEvalResult CorotationalBeam3D::evaluate(
    const Vec3& X1,
    const Vec3& X2,
    const Mat3& R0,
    const Beam1DProps& props,
    const Vec3& u1,
    const Vec3& theta1Inc,
    const Vec3& u2,
    const Vec3& theta2Inc,
    const Corotational1DState& prevState,
    const J2PlasticityParams* plasticParams,
    const std::vector<SectionFiber>* fibers,
    const std::array<bool, 6>& releaseStart,
    const std::array<bool, 6>& releaseEnd,
    bool pDeltaOnly)
{
    Corotational1DEvalResult out;
    const Vec3 x0_12 = X2 - X1;
    const double L0 = x0_12.norm();
    if (!std::isfinite(L0) || L0 <= 1e-12) {
        return out;
    }

    // Atualiza rotações nodais multiplicativamente em SO(3) quando corrotacional
    Mat3 R1 = prevState.R1;
    Mat3 R2 = prevState.R2;
    if (!pDeltaOnly) {
        R1 = expSO3(theta1Inc) * prevState.R1;
        R2 = expSO3(theta2Inc) * prevState.R2;
    }
    out.trialState.R1 = R1;
    out.trialState.R2 = R2;

    if (pDeltaOnly) {
        // Formulação geometricamente não linear de 2ª ordem P-Delta sobre sistema de referência R0
        Beam1DProps pRef = props;
        pRef.L = L0;
        Eigen::Matrix<double, 12, 12> T0 = Eigen::Matrix<double, 12, 12>::Zero();
        for (int blk = 0; blk < 4; ++blk) {
            T0.block<3, 3>(3 * blk, 3 * blk) = R0.transpose();
        }
        Eigen::Matrix<double, 12, 1> uGlob;
        uGlob << u1, theta1Inc, u2, theta2Inc;
        Eigen::Matrix<double, 12, 1> uLoc = T0 * uGlob;

        Eigen::Matrix<double, 12, 12> kmLoc = BeamTruss1D::computeUncondensedLocalStiffness(pRef);
        const double axialForce = (props.E * props.A / L0) * (uLoc(6) - uLoc(0));
        Eigen::Matrix<double, 12, 1> fLoc = kmLoc * uLoc;

        if (props.formulation != Beam1DFormulation::Truss3D) {
            kmLoc = applyStaticReleaseCondensation(kmLoc, releaseStart, releaseEnd, &fLoc);
        }

        const Eigen::Matrix<double, 12, 12> kgLoc =
            BeamTruss1D::computeLocalGeometricStiffness(axialForce, L0, props.formulation);
        Eigen::Matrix<double, 12, 12> ktLoc = kmLoc + kgLoc;
        fLoc += kgLoc * uLoc;

        out.currentLength = L0 + (uLoc(6) - uLoc(0));
        out.axialForce = axialForce;
        out.fIntLocal = fLoc;
        out.fIntGlobal = T0.transpose() * fLoc;
        out.KtGlobal = T0.transpose() * ktLoc * T0;
        return out;
    }

    // --- Formulação Corrotacional 3D Exata ---
    const Vec3 x12 = (X2 + u2) - (X1 + u1);
    const double L = x12.norm();
    if (!std::isfinite(L) || L <= 1e-12) {
        return out;
    }
    out.currentLength = L;
    const Vec3 r1 = x12 / L;

    // Caso específico Treliça 3D corrotacional (sem graus rotacionais nem momento)
    if (props.formulation == Beam1DFormulation::Truss3D) {
        const double epsAx = (L - L0) / L0;
        double N = 0.0;
        double EA_t = props.E * props.A;
        if (plasticParams != nullptr) {
            J2Uniaxial1D j2(*plasticParams);
            J2State1D stPrev;
            if (!prevState.gaussStates.empty() && !prevState.gaussStates[0].fibers.empty()) {
                stPrev = prevState.gaussStates[0].fibers[0];
            }
            J2State1D stTrial;
            j2.integrate(epsAx, stPrev, stTrial);
            N = stTrial.stress * props.A;
            EA_t = stTrial.tangentE * props.A;
            if (stTrial.alphaIso > stPrev.alphaIso + 1e-12) out.yielded = true;
            BeamPlasticGaussState gSt;
            gSt.fibers.push_back(stTrial);
            out.trialState.gaussStates.push_back(std::move(gSt));
        } else {
            N = EA_t * epsAx;
        }
        out.axialForce = N;

        Eigen::Matrix<double, 12, 1> fGlob = Eigen::Matrix<double, 12, 1>::Zero();
        fGlob.segment<3>(0) = -N * r1;
        fGlob.segment<3>(6) =  N * r1;
        out.fIntGlobal = fGlob;
        out.fIntLocal(0) = -N;
        out.fIntLocal(6) =  N;

        const Mat3 r1r1 = r1 * r1.transpose();
        const Mat3 k3 = (EA_t / L0) * r1r1 + (N / L) * (Mat3::Identity() - r1r1);
        Eigen::Matrix<double, 12, 12> Kt = Eigen::Matrix<double, 12, 12>::Zero();
        Kt.block<3, 3>(0, 0) =  k3;
        Kt.block<3, 3>(0, 6) = -k3;
        Kt.block<3, 3>(6, 0) = -k3;
        Kt.block<3, 3>(6, 6) =  k3;
        out.KtGlobal = Kt;
        return out;
    }

    // Construção da tríade corrotacional R_e = [r1, r2, r3] a partir do vetor alma médio
    Vec3 pAvg = 0.5 * (R1 * R0.col(1) + R2 * R0.col(1));
    Vec3 v3 = r1.cross(pAvg);
    if (v3.squaredNorm() < 1e-12) {
        pAvg = 0.5 * (R1 * R0.col(2) + R2 * R0.col(2));
        v3 = r1.cross(pAvg);
    }
    const Vec3 r3 = v3.normalized();
    const Vec3 r2 = r3.cross(r1);
    Mat3 Re;
    Re.col(0) = r1;
    Re.col(1) = r2;
    Re.col(2) = r3;

    Eigen::Matrix<double, 12, 12> Te = Eigen::Matrix<double, 12, 12>::Zero();
    for (int blk = 0; blk < 4; ++blk) {
        Te.block<3, 3>(3 * blk, 3 * blk) = Re.transpose();
    }

    // Deformações rotacionais locais naturais em relação à tríade deformada Re
    const Vec3 th1Loc = logSO3(Re.transpose() * R1 * R0);
    const Vec3 th2Loc = logSO3(Re.transpose() * R2 * R0);
    const double uAx = L - L0;
    const double phiTwist = th2Loc.x() - th1Loc.x();
    const double th1y = th1Loc.y(), th1z = th1Loc.z();
    const double th2y = th2Loc.y(), th2z = th2Loc.z();

    // Matriz cinemática natural B0 (6x12) em coordenadas locais deformadas de comprimento L:
    // q_nat = [uAx, phiTwist, th1y, th1z, th2y, th2z]^T
    Eigen::Matrix<double, 6, 12> B0 = Eigen::Matrix<double, 6, 12>::Zero();
    const double invL = 1.0 / L;
    // 0: uAx = u2x - u1x
    B0(0, 0) = -1.0; B0(0, 6) = 1.0;
    // 1: phiTwist = th2x - th1x
    B0(1, 3) = -1.0; B0(1, 9) = 1.0;
    // 2: th1y - (- (w2 - w1)/L) = th1y + (w2 - w1)/L
    B0(2, 2) = -invL; B0(2, 4) = 1.0; B0(2, 8) = invL;
    // 3: th1z - (v2 - v1)/L
    B0(3, 1) = invL; B0(3, 5) = 1.0; B0(3, 7) = -invL;
    // 4: th2y + (w2 - w1)/L
    B0(4, 2) = -invL; B0(4, 8) = invL; B0(4, 10) = 1.0;
    // 5: th2z - (v2 - v1)/L
    B0(5, 1) = invL; B0(5, 7) = -invL; B0(5, 11) = 1.0;

    // Resposta constitutiva natural f_nat (6x1) = [N, Tx, My1, Mz1, My2, Mz2] e D_nat (6x6)
    Eigen::Matrix<double, 6, 1> fNat = Eigen::Matrix<double, 6, 1>::Zero();
    Eigen::Matrix<double, 6, 6> DNat = Eigen::Matrix<double, 6, 6>::Zero();

    if (plasticParams == nullptr || fibers == nullptr || fibers->empty()) {
        // Caminho elástico linear local exato (inclui Timoshenko ou Euler-Bernoulli)
        const double kAx = (props.E * props.A) / L0;
        const double kTor = (props.G * props.J) / L0;
        const double L02 = L0 * L0;
        double phiY = 0.0, phiZ = 0.0;
        if (props.formulation == Beam1DFormulation::Timoshenko3D) {
            if (props.G > 1e-12 && props.Asy > 1e-12) phiY = (12.0 * props.E * props.Iz) / (props.G * props.Asy * L02);
            if (props.G > 1e-12 && props.Asz > 1e-12) phiZ = (12.0 * props.E * props.Iy) / (props.G * props.Asz * L02);
        }
        const double c3y = ((4.0 + phiZ) * props.E * props.Iy) / (L0 * (1.0 + phiZ));
        const double c4y = ((2.0 - phiZ) * props.E * props.Iy) / (L0 * (1.0 + phiZ));
        const double c3z = ((4.0 + phiY) * props.E * props.Iz) / (L0 * (1.0 + phiY));
        const double c4z = ((2.0 - phiY) * props.E * props.Iz) / (L0 * (1.0 + phiY));

        DNat(0, 0) = kAx;
        DNat(1, 1) = kTor;
        DNat(2, 2) = c3y; DNat(2, 4) = c4y;
        DNat(4, 2) = c4y; DNat(4, 4) = c3y;
        DNat(3, 3) = c3z; DNat(3, 5) = c4z;
        DNat(5, 3) = c4z; DNat(5, 5) = c3z;

        Eigen::Matrix<double, 6, 1> qNat;
        qNat << uAx, phiTwist, th1y, th1z, th2y, th2z;
        fNat = DNat * qNat;
    } else {
        // Quadratura de Gauss-Lobatto em 5 pontos ao longo do comprimento + fibras na seção
        static constexpr std::array<double, 5> kXi = {
            0.0,
            0.5 * (1.0 - 0.65465367070797714), // sqrt(3/7)
            0.5,
            0.5 * (1.0 + 0.65465367070797714),
            1.0
        };
        static constexpr std::array<double, 5> kW = {
            0.05, 49.0 / 180.0, 16.0 / 45.0, 49.0 / 180.0, 0.05
        };

        const J2Uniaxial1D j2(*plasticParams);
        const double eps0 = uAx / L0;
        const double kTor = (props.G * props.J) / L0;

        out.trialState.gaussStates.resize(5);
        for (int g = 0; g < 5; ++g) {
            const double xi = kXi[g];
            const double wDx = kW[g] * L0;
            const double b1 = (-4.0 + 6.0 * xi) / L0;
            const double b2 = (-2.0 + 6.0 * xi) / L0;

            const double kappaZ = b1 * th1z + b2 * th2z;
            const double kappaY = b1 * th1y + b2 * th2y;

            auto& gTrial = out.trialState.gaussStates[g];
            gTrial.fibers.resize(fibers->size());

            double N_sec = 0.0, My_sec = 0.0, Mz_sec = 0.0;
            double EA_t = 0.0, ESy_t = 0.0, ESz_t = 0.0, EIy_t = 0.0, EIz_t = 0.0, EIyz_t = 0.0;

            const bool hasPrev = (prevState.gaussStates.size() == 5
                && prevState.gaussStates[g].fibers.size() == fibers->size());
            const J2State1D defaultFib{};

            for (size_t fi = 0; fi < fibers->size(); ++fi) {
                const auto& fib = (*fibers)[fi];
                const double epsFib = eps0 - fib.y * kappaZ + fib.z * kappaY;
                const J2State1D& stPrev = hasPrev ? prevState.gaussStates[g].fibers[fi] : defaultFib;
                J2State1D& stOut = gTrial.fibers[fi];
                j2.integrate(epsFib, stPrev, stOut);
                if (stOut.alphaIso > stPrev.alphaIso + 1e-12) out.yielded = true;

                const double fA = stOut.stress * fib.area;
                const double eA = stOut.tangentE * fib.area;
                N_sec += fA;
                My_sec += fib.z * fA;
                Mz_sec -= fib.y * fA;

                EA_t += eA;
                ESy_t += fib.z * eA;
                ESz_t += fib.y * eA;
                EIy_t += fib.z * fib.z * eA;
                EIz_t += fib.y * fib.y * eA;
                EIyz_t += fib.y * fib.z * eA;
            }

            // Integração virtual work ao longo da barra: f_nat += wDx * B_sec^T * S_sec
            const double iL0 = 1.0 / L0;
            fNat(0) += wDx * iL0 * N_sec;
            fNat(2) += wDx * b1 * My_sec;
            fNat(4) += wDx * b2 * My_sec;
            fNat(3) += wDx * b1 * Mz_sec;
            fNat(5) += wDx * b2 * Mz_sec;

            // Matriz tangente natural D_nat (6x6) para índices [0:uAx, 2:th1y, 3:th1z, 4:th2y, 5:th2z]
            // B_sec linhas: eps0 = uAx/L0, kappaY = b1*th1y + b2*th2y, kappaZ = b1*th1z + b2*th2z
            // D_sec = [EA, ESy, -ESz; ESy, EIy, -EIyz; -ESz, -EIyz, EIz]
            DNat(0, 0) += wDx * iL0 * iL0 * EA_t;
            DNat(0, 2) += wDx * iL0 * b1 * ESy_t;
            DNat(0, 4) += wDx * iL0 * b2 * ESy_t;
            DNat(0, 3) -= wDx * iL0 * b1 * ESz_t;
            DNat(0, 5) -= wDx * iL0 * b2 * ESz_t;

            DNat(2, 0) = DNat(0, 2);
            DNat(4, 0) = DNat(0, 4);
            DNat(3, 0) = DNat(0, 3);
            DNat(5, 0) = DNat(0, 5);

            DNat(2, 2) += wDx * b1 * b1 * EIy_t;
            DNat(2, 4) += wDx * b1 * b2 * EIy_t;
            DNat(4, 2) = DNat(2, 4);
            DNat(4, 4) += wDx * b2 * b2 * EIy_t;

            DNat(3, 3) += wDx * b1 * b1 * EIz_t;
            DNat(3, 5) += wDx * b1 * b2 * EIz_t;
            DNat(5, 3) = DNat(3, 5);
            DNat(5, 5) += wDx * b2 * b2 * EIz_t;

            DNat(2, 3) -= wDx * b1 * b1 * EIyz_t;
            DNat(2, 5) -= wDx * b1 * b2 * EIyz_t;
            DNat(4, 3) -= wDx * b2 * b1 * EIyz_t;
            DNat(4, 5) -= wDx * b2 * b2 * EIyz_t;
            DNat(3, 2) = DNat(2, 3);
            DNat(5, 2) = DNat(2, 5);
            DNat(3, 4) = DNat(4, 3);
            DNat(5, 4) = DNat(4, 5);
        }

        // Torção permanece elástica linear
        DNat(1, 1) = kTor;
        fNat(1) = kTor * phiTwist;
    }

    const double N = fNat(0);
    out.axialForce = N;

    Eigen::Matrix<double, 12, 1> fLoc = B0.transpose() * fNat;
    Eigen::Matrix<double, 12, 12> kmLoc = B0.transpose() * DNat * B0;

    // Aplica rótulas de extremidade via condensação estática antes de acrescentar Kg
    kmLoc = applyStaticReleaseCondensation(kmLoc, releaseStart, releaseEnd, &fLoc);

    // Termos geométricos locais exatos da variação de T_e e de B0(L)
    const double Qy = fLoc(1); // = (Mz1 + Mz2)/L
    const double Qz = fLoc(2); // = -(My1 + My2)/L
    Eigen::Matrix<double, 12, 12> kgLoc = Eigen::Matrix<double, 12, 12>::Zero();
    const double kStr = N * invL;
    kgLoc(1, 1) =  kStr; kgLoc(1, 7) = -kStr;
    kgLoc(7, 1) = -kStr; kgLoc(7, 7) =  kStr;
    kgLoc(2, 2) =  kStr; kgLoc(2, 8) = -kStr;
    kgLoc(8, 2) = -kStr; kgLoc(8, 8) =  kStr;

    // Acoplamento simétrico momento-corte-axial via Hessiana exata de atan2(dy, dx) / atan2(dz, dx)
    const double cQy = Qy * invL;
    const double cQz = Qz * invL;
    kgLoc(0, 1) +=  cQy; kgLoc(1, 0) +=  cQy;
    kgLoc(0, 7) += -cQy; kgLoc(7, 0) += -cQy;
    kgLoc(6, 1) += -cQy; kgLoc(1, 6) += -cQy;
    kgLoc(6, 7) +=  cQy; kgLoc(7, 6) +=  cQy;

    kgLoc(0, 2) +=  cQz; kgLoc(2, 0) +=  cQz;
    kgLoc(0, 8) += -cQz; kgLoc(8, 0) += -cQz;
    kgLoc(6, 2) += -cQz; kgLoc(2, 6) += -cQz;
    kgLoc(6, 8) +=  cQz; kgLoc(8, 6) +=  cQz;

    out.fIntLocal = fLoc;
    out.fIntGlobal = Te.transpose() * fLoc;
    out.KtGlobal = Te.transpose() * (kmLoc + kgLoc) * Te;
    return out;
}

} // namespace omniframe::kernel
