#include "BeamTruss1D.hpp"
#include <algorithm>
#include <cmath>

namespace omniframe::kernel {

namespace {

Beam1DProps makePrincipalAxisProps(const Beam1DProps& p, double& outAlphaRad) {
    const double avg = 0.5 * (p.Iy + p.Iz);
    const double diff = 0.5 * (p.Iy - p.Iz);
    const double R = std::hypot(diff, p.Iyz);
    outAlphaRad = 0.5 * std::atan2(-2.0 * p.Iyz, p.Iy - p.Iz);
    const double c = std::cos(outAlphaRad);
    const double s = std::sin(outAlphaRad);

    Beam1DProps pp = p;
    pp.Iy = avg + R;
    pp.Iz = std::max(1e-18, avg - R);
    pp.Iyz = 0.0;
    if (p.Asy > 0.0 && p.Asz > 0.0) {
        pp.Asy = std::max(1e-12, p.Asy * c * c + p.Asz * s * s);
        pp.Asz = std::max(1e-12, p.Asy * s * s + p.Asz * c * c);
    }
    return pp;
}

Eigen::Matrix3d makePrincipalRotation3(double alphaRad) {
    const double c = std::cos(alphaRad);
    const double s = std::sin(alphaRad);
    Eigen::Matrix3d Rp = Eigen::Matrix3d::Identity();
    Rp(1, 1) = c;
    Rp(1, 2) = s;
    Rp(2, 1) = -s;
    Rp(2, 2) = c;
    return Rp;
}

Eigen::Matrix<double, 12, 12> makePrincipalRotation12(double alphaRad) {
    const Eigen::Matrix3d Rp = makePrincipalRotation3(alphaRad);
    Eigen::Matrix<double, 12, 12> Tp = Eigen::Matrix<double, 12, 12>::Zero();
    for (int blk = 0; blk < 4; ++blk) {
        Tp.block<3, 3>(3 * blk, 3 * blk) = Rp;
    }
    return Tp;
}

} // namespace

Eigen::Matrix<double, 12, 12> BeamTruss1D::computeUncondensedLocalStiffness(const Beam1DProps& p) {
    Eigen::Matrix<double, 12, 12> k0 = Eigen::Matrix<double, 12, 12>::Zero();
    const double L = p.L;
    if (!std::isfinite(L) || L <= 1e-12) {
        return k0;
    }

    if (std::isfinite(p.Iyz) && std::abs(p.Iyz) > 1e-18 && p.formulation != Beam1DFormulation::Truss3D) {
        double alpha = 0.0;
        const Beam1DProps pp = makePrincipalAxisProps(p, alpha);
        const Eigen::Matrix<double, 12, 12> Tp = makePrincipalRotation12(alpha);
        const Eigen::Matrix<double, 12, 12> kLoc = Tp.transpose() * computeUncondensedLocalStiffness(pp) * Tp;
        return 0.5 * (kLoc + kLoc.transpose());
    }

    // 1. Axial (DOFs 0 e 6)
    const double kAx = (p.E * p.A) / L;
    k0(0, 0) =  kAx; k0(0, 6) = -kAx;
    k0(6, 0) = -kAx; k0(6, 6) =  kAx;

    if (p.formulation == Beam1DFormulation::Truss3D) {
        return k0; // Treliça pura 3D: sem rigidez flexional ou torsional
    }

    // 2. Torção (DOFs 3 e 9)
    const double kTor = (p.G * p.J) / L;
    k0(3, 3) =  kTor; k0(3, 9) = -kTor;
    k0(9, 3) = -kTor; k0(9, 9) =  kTor;

    const double L2 = L * L;
    const double L3 = L2 * L;

    double phiY = 0.0;
    double phiZ = 0.0;
    if (p.formulation == Beam1DFormulation::Timoshenko3D) {
        if (p.G > 1e-12 && p.Asy > 1e-12) {
            phiY = (12.0 * p.E * p.Iz) / (p.G * p.Asy * L2);
        }
        if (p.G > 1e-12 && p.Asz > 1e-12) {
            phiZ = (12.0 * p.E * p.Iy) / (p.G * p.Asz * L2);
        }
    }

    // 3. Flexão no plano x-y em torno de z local (DOFs 1, 5, 7, 11), livre de shear locking
    const double denY = 1.0 + phiY;
    const double c1z = (12.0 * p.E * p.Iz) / (L3 * denY);
    const double c2z = (6.0 * p.E * p.Iz) / (L2 * denY);
    const double c3z = ((4.0 + phiY) * p.E * p.Iz) / (L * denY);
    const double c4z = ((2.0 - phiY) * p.E * p.Iz) / (L * denY);

    k0(1, 1)  +=  c1z; k0(1, 5)  +=  c2z; k0(1, 7)  += -c1z; k0(1, 11) +=  c2z;
    k0(5, 1)  +=  c2z; k0(5, 5)  +=  c3z; k0(5, 7)  += -c2z; k0(5, 11) +=  c4z;
    k0(7, 1)  += -c1z; k0(7, 5)  += -c2z; k0(7, 7)  +=  c1z; k0(7, 11) += -c2z;
    k0(11, 1) +=  c2z; k0(11, 5) +=  c4z; k0(11, 7) += -c2z; k0(11, 11) += c3z;

    // 4. Flexão no plano x-z em torno de y local (DOFs 2, 4, 8, 10) com theta_y = -dw/dx (+gamma_xz)
    const double denZ = 1.0 + phiZ;
    const double c1y = (12.0 * p.E * p.Iy) / (L3 * denZ);
    const double c2y = (6.0 * p.E * p.Iy) / (L2 * denZ);
    const double c3y = ((4.0 + phiZ) * p.E * p.Iy) / (L * denZ);
    const double c4y = ((2.0 - phiZ) * p.E * p.Iy) / (L * denZ);

    k0(2, 2)  +=  c1y; k0(2, 4)  += -c2y; k0(2, 8)  += -c1y; k0(2, 10) += -c2y;
    k0(4, 2)  += -c2y; k0(4, 4)  +=  c3y; k0(4, 8)  +=  c2y; k0(4, 10) +=  c4y;
    k0(8, 2)  += -c1y; k0(8, 4)  +=  c2y; k0(8, 8)  +=  c1y; k0(8, 10) +=  c2y;
    k0(10, 2) += -c2y; k0(10, 4) +=  c4y; k0(10, 8) +=  c2y; k0(10, 10) += c3y;

    return k0;
}

Eigen::Matrix<double, 12, 1> BeamTruss1D::computeConsistentLocalLoadVector(
    const Beam1DProps& p,
    const Vec3& qLoc,
    double deltaT)
{
    Eigen::Matrix<double, 12, 1> fe = Eigen::Matrix<double, 12, 1>::Zero();
    const double L = p.L;
    if (!std::isfinite(L) || L <= 1e-12) {
        return fe;
    }

    if (std::isfinite(qLoc.x()) && std::abs(qLoc.x()) > 1e-12) {
        fe(0) += 0.5 * qLoc.x() * L;
        fe(6) += 0.5 * qLoc.x() * L;
    }

    if (std::isfinite(qLoc.y()) && std::abs(qLoc.y()) > 1e-12) {
        fe(1) += 0.5 * qLoc.y() * L;
        fe(7) += 0.5 * qLoc.y() * L;
        if (p.formulation != Beam1DFormulation::Truss3D) {
            const double mZ = (qLoc.y() * L * L) / 12.0;
            fe(5) += mZ;
            fe(11) -= mZ;
        }
    }

    if (std::isfinite(qLoc.z()) && std::abs(qLoc.z()) > 1e-12) {
        fe(2) += 0.5 * qLoc.z() * L;
        fe(8) += 0.5 * qLoc.z() * L;
        if (p.formulation != Beam1DFormulation::Truss3D) {
            const double mY = (qLoc.z() * L * L) / 12.0;
            fe(4) -= mY;
            fe(10) += mY;
        }
    }

    if (std::isfinite(deltaT) && std::abs(deltaT) > 1e-9) {
        const double nTh = p.E * p.A * p.alpha * deltaT;
        fe(0) -= nTh;
        fe(6) += nTh;
    }

    return fe;
}

Eigen::Matrix<double, 12, 12> BeamTruss1D::computeConsistentLocalMass(const Beam1DProps& p) {
    Eigen::Matrix<double, 12, 12> m = Eigen::Matrix<double, 12, 12>::Zero();
    const double L = p.L;
    if (L <= 1e-12 || p.rhoConsistent <= 0.0) return m;

    const double totalMass = p.rhoConsistent * p.A * L;
    const double L2 = L * L;
    const double Ip = p.Iy + p.Iz;
    const double rx2 = (p.A > 1e-18) ? (Ip / p.A) : 0.0;

    if (p.formulation == Beam1DFormulation::Truss3D) {
        // Matriz consistente linear 3D para barra biarticulada (u, v, w)
        const double diag = totalMass / 3.0;
        const double off = totalMass / 6.0;
        for (int d = 0; d < 3; ++d) {
            m(d, d) = diag; m(d + 6, d + 6) = diag;
            m(d, d + 6) = off; m(d + 6, d) = off;
        }
        return m;
    }

    // Axial (DOFs 0, 6)
    const double cAx = totalMass / 6.0;
    m(0, 0) = 2.0 * cAx; m(0, 6) = cAx;
    m(6, 0) = cAx;       m(6, 6) = 2.0 * cAx;

    // Torção (DOFs 3, 9): inércia rotacional polar rho * Ip * L
    const double cTor = (p.rhoConsistent * Ip * L) / 6.0;
    m(3, 3) = 2.0 * cTor; m(3, 9) = cTor;
    m(9, 3) = cTor;       m(9, 9) = 2.0 * cTor;

    // Flexão consistente (Hermite 420-denominador para Euler-Bernoulli, reduzindo limpo)
    const double c = totalMass / 420.0;
    // x-y (DOFs 1, 5, 7, 11)
    m(1, 1)  =  156.0 * c;      m(1, 5)  =  22.0 * L * c;  m(1, 7)  =  54.0 * c;      m(1, 11) = -13.0 * L * c;
    m(5, 1)  =   22.0 * L * c;  m(5, 5)  =   4.0 * L2 * c; m(5, 7)  =  13.0 * L * c;  m(5, 11) =  -3.0 * L2 * c;
    m(7, 1)  =   54.0 * c;      m(7, 5)  =  13.0 * L * c;  m(7, 7)  = 156.0 * c;      m(7, 11) = -22.0 * L * c;
    m(11, 1) =  -13.0 * L * c;  m(11, 5) =  -3.0 * L2 * c; m(11, 7) = -22.0 * L * c;  m(11, 11) =  4.0 * L2 * c;

    // x-z (DOFs 2, 4, 8, 10) com sinal invertido em theta_y = -dw/dx
    m(2, 2)  =  156.0 * c;      m(2, 4)  = -22.0 * L * c;  m(2, 8)  =  54.0 * c;      m(2, 10) =  13.0 * L * c;
    m(4, 2)  =  -22.0 * L * c;  m(4, 4)  =   4.0 * L2 * c; m(4, 8)  = -13.0 * L * c;  m(4, 10) =  -3.0 * L2 * c;
    m(8, 2)  =   54.0 * c;      m(8, 4)  = -13.0 * L * c;  m(8, 8)  = 156.0 * c;      m(8, 10) =  22.0 * L * c;
    m(10, 2) =   13.0 * L * c;  m(10, 4) =  -3.0 * L2 * c; m(10, 8) =  22.0 * L * c;  m(10, 10) =  4.0 * L2 * c;

    // Inércia rotacional adicional de Timoshenko se aplicável
    if (p.formulation == Beam1DFormulation::Timoshenko3D && rx2 > 0.0) {
        const double crY = (p.rhoConsistent * p.Iz) / (30.0 * L);
        m(1, 1)  += 36.0 * crY; m(1, 5)  += 3.0 * L * crY; m(1, 7)  -= 36.0 * crY; m(1, 11) += 3.0 * L * crY;
        m(5, 1)  += 3.0 * L * crY; m(5, 5)  += 4.0 * L2 * crY; m(5, 7)  -= 3.0 * L * crY; m(5, 11) -= L2 * crY;
        m(7, 1)  -= 36.0 * crY; m(7, 5)  -= 3.0 * L * crY; m(7, 7)  += 36.0 * crY; m(7, 11) -= 3.0 * L * crY;
        m(11, 1) += 3.0 * L * crY; m(11, 5) -= L2 * crY; m(11, 7) -= 3.0 * L * crY; m(11, 11) += 4.0 * L2 * crY;

        const double crZ = (p.rhoConsistent * p.Iy) / (30.0 * L);
        m(2, 2)  += 36.0 * crZ; m(2, 4)  -= 3.0 * L * crZ; m(2, 8)  -= 36.0 * crZ; m(2, 10) -= 3.0 * L * crZ;
        m(4, 2)  -= 3.0 * L * crZ; m(4, 4)  += 4.0 * L2 * crZ; m(4, 8)  += 3.0 * L * crZ; m(4, 10) -= L2 * crZ;
        m(8, 2)  -= 36.0 * crZ; m(8, 4)  += 3.0 * L * crZ; m(8, 8)  += 36.0 * crZ; m(8, 10) += 3.0 * L * crZ;
        m(10, 2) -= 3.0 * L * crZ; m(10, 4) -= L2 * crZ; m(10, 8) += 3.0 * L * crZ; m(10, 10) += 4.0 * L2 * crZ;
    }

    return m;
}

Eigen::Matrix<double, 12, 12> BeamTruss1D::computeLumpedLocalMass(const Beam1DProps& p) {
    Eigen::Matrix<double, 12, 12> m = Eigen::Matrix<double, 12, 12>::Zero();
    if (p.L <= 1e-12 || p.rhoConsistent <= 0.0) return m;
    const double halfMass = 0.5 * p.rhoConsistent * p.A * p.L;
    for (int d : {0, 1, 2, 6, 7, 8}) {
        m(d, d) = halfMass;
    }
    if (p.formulation != Beam1DFormulation::Truss3D) {
        // Inércia rotacional concentrada de Hinton (M*L^2/24) + polar para evitar zeros na massa rotacional se necessário
        const double jRot = halfMass * (p.L * p.L) / 12.0;
        const double jTor = 0.5 * p.rhoConsistent * (p.Iy + p.Iz) * p.L;
        m(3, 3) = m(9, 9) = std::max(jTor, 1e-9 * halfMass);
        m(4, 4) = m(10, 10) = jRot;
        m(5, 5) = m(11, 11) = jRot;
    }
    return m;
}

Eigen::Matrix<double, 12, 12> BeamTruss1D::computeLocalGeometricStiffness(double N, double L) {
    return computeLocalGeometricStiffness(N, L, Beam1DFormulation::EulerBernoulli3D);
}

Eigen::Matrix<double, 12, 12> BeamTruss1D::computeLocalGeometricStiffness(double N,
                                                                          double L,
                                                                          Beam1DFormulation formulation)
{
    Eigen::Matrix<double, 12, 12> kg = Eigen::Matrix<double, 12, 12>::Zero();
    if (L <= 1e-12 || !std::isfinite(N)) return kg;

    if (formulation == Beam1DFormulation::Truss3D) {
        // Treliça 3D: rigidez geométrica transversal pura (N/L) sem acoplamento rotacional
        const double kStr = N / L;
        kg(1, 1) =  kStr; kg(1, 7) = -kStr;
        kg(7, 1) = -kStr; kg(7, 7) =  kStr;
        kg(2, 2) =  kStr; kg(2, 8) = -kStr;
        kg(8, 2) = -kStr; kg(8, 8) =  kStr;
        return kg;
    }

    // N > 0 = tração (enrijece, Kg > 0); N < 0 = compressão (amolece, Kg < 0)
    // Matriz geométrica consistente de Przemieniecki / McGuire-Gallagher
    const double a = (6.0 / 5.0) * (N / L);
    const double b = N / 10.0;
    const double c = (2.0 / 15.0) * N * L;
    const double d = -(1.0 / 30.0) * N * L;

    // Plano x-y (DOFs 1, 5, 7, 11)
    kg(1, 1) += a;  kg(1, 5) += b;  kg(1, 7) -= a;  kg(1, 11) += b;
    kg(5, 1) += b;  kg(5, 5) += c;  kg(5, 7) -= b;  kg(5, 11) += d;
    kg(7, 1) -= a;  kg(7, 5) -= b;  kg(7, 7) += a;  kg(7, 11) -= b;
    kg(11,1) += b;  kg(11,5) += d;  kg(11,7) -= b;  kg(11,11) += c;

    // Plano x-z (DOFs 2, 4, 8, 10) com theta_y = -dw/dx
    kg(2, 2) += a;  kg(2, 4) -= b;  kg(2, 8) -= a;  kg(2, 10) -= b;
    kg(4, 2) -= b;  kg(4, 4) += c;  kg(4, 8) += b;  kg(4, 10) += d;
    kg(8, 2) -= a;  kg(8, 4) += b;  kg(8, 8) += a;  kg(8, 10) += b;
    kg(10,2) -= b;  kg(10,4) += d;  kg(10,8) += b;  kg(10,10) += c;

    return kg;
}

Beam1DInternalEval BeamTruss1D::evaluateLocalDistribution(
    double xi,
    const Beam1DProps& p,
    const Vec3& qLoc,
    const Eigen::Matrix<double, 12, 1>& uLoc,
    const Eigen::Matrix<double, 12, 1>& fEndLoc)
{
    Beam1DInternalEval res;
    const double L = p.L;
    if (L <= 1e-12) return res;
    xi = std::clamp(xi, 0.0, 1.0);
    const double x = xi * L;

    const double N1 = fEndLoc(0);
    const double Vy1 = fEndLoc(1);
    const double Vz1 = fEndLoc(2);
    const double Tx1 = fEndLoc(3);
    const double My1 = fEndLoc(4);
    const double Mz1 = fEndLoc(5);

    res.N = -N1 - qLoc.x() * x;
    res.Vy = -Vy1 - qLoc.y() * x;
    res.Vz = -Vz1 - qLoc.z() * x;
    res.Tx = -Tx1;
    res.Mz = -Mz1 + Vy1 * x + 0.5 * qLoc.y() * x * x;
    res.My = -My1 - Vz1 * x - 0.5 * qLoc.z() * x * x;

    const double u1 = uLoc(0), v1 = uLoc(1), w1 = uLoc(2), th_y1 = uLoc(4), th_z1 = uLoc(5);
    const double u2 = uLoc(6), v2 = uLoc(7), w2 = uLoc(8), th_y2 = uLoc(10), th_z2 = uLoc(11);

    if (std::isfinite(p.Iyz) && std::abs(p.Iyz) > 1e-18 && p.formulation != Beam1DFormulation::Truss3D) {
        double alpha = 0.0;
        const Beam1DProps pp = makePrincipalAxisProps(p, alpha);
        const Eigen::Matrix3d Rp = makePrincipalRotation3(alpha);
        const Eigen::Matrix<double, 12, 12> Tp = makePrincipalRotation12(alpha);
        const Vec3 qPrin = Rp * qLoc;
        const Eigen::Matrix<double, 12, 1> uPrin = Tp * uLoc;
        const Eigen::Matrix<double, 12, 1> fPrin = Tp * fEndLoc;
        const Beam1DInternalEval evPrin = evaluateLocalDistribution(xi, pp, qPrin, uPrin, fPrin);
        res.defLocal = Rp.transpose() * evPrin.defLocal;
        return res;
    }

    double ux = (1.0 - xi) * u1 + xi * u2;
    if (std::abs(qLoc.x()) > 1e-9 && p.E * p.A > 1e-12) {
        ux += (qLoc.x() * L * L) / (2.0 * p.E * p.A) * xi * (1.0 - xi);
    }

    if (p.formulation == Beam1DFormulation::Truss3D) {
        const double vy = (1.0 - xi) * v1 + xi * v2;
        const double wz = (1.0 - xi) * w1 + xi * w2;
        res.defLocal = {ux, vy, wz};
        return res;
    }

    double phiY = 0.0;
    double phiZ = 0.0;
    if (p.formulation == Beam1DFormulation::Timoshenko3D) {
        if (p.G > 1e-12 && p.Asy > 1e-12) phiY = (12.0 * p.E * p.Iz) / (p.G * p.Asy * L * L);
        if (p.G > 1e-12 && p.Asz > 1e-12) phiZ = (12.0 * p.E * p.Iy) / (p.G * p.Asz * L * L);
    }

    const double xi2 = xi * xi;
    const double xi3 = xi2 * xi;

    const double muY = 1.0 / (1.0 + phiY);
    const double Ny1 = muY * (1.0 - 3.0 * xi2 + 2.0 * xi3 + phiY * (1.0 - xi));
    const double Ny2 = L * muY * (xi - 2.0 * xi2 + xi3 + 0.5 * phiY * (xi - xi2));
    const double Ny3 = muY * (3.0 * xi2 - 2.0 * xi3 + phiY * xi);
    const double Ny4 = L * muY * (-xi2 + xi3 - 0.5 * phiY * (xi - xi2));

    double vy = Ny1 * v1 + Ny2 * th_z1 + Ny3 * v2 + Ny4 * th_z2;
    if (std::abs(qLoc.y()) > 1e-9 && p.E * p.Iz > 1e-12) {
        vy += (qLoc.y() * std::pow(L, 4)) / (24.0 * p.E * p.Iz) * xi2 * std::pow(1.0 - xi, 2);
        if (p.formulation == Beam1DFormulation::Timoshenko3D && p.G * p.Asy > 1e-12) {
            vy += (qLoc.y() * L * L) / (2.0 * p.G * p.Asy) * xi * (1.0 - xi);
        }
    }

    const double muZ = 1.0 / (1.0 + phiZ);
    const double Nz1 = muZ * (1.0 - 3.0 * xi2 + 2.0 * xi3 + phiZ * (1.0 - xi));
    const double Nz2 = L * muZ * (xi - 2.0 * xi2 + xi3 + 0.5 * phiZ * (xi - xi2));
    const double Nz3 = muZ * (3.0 * xi2 - 2.0 * xi3 + phiZ * xi);
    const double Nz4 = L * muZ * (-xi2 + xi3 - 0.5 * phiZ * (xi - xi2));

    double wz = Nz1 * w1 + Nz2 * (-th_y1) + Nz3 * w2 + Nz4 * (-th_y2);
    if (std::abs(qLoc.z()) > 1e-9 && p.E * p.Iy > 1e-12) {
        wz += (qLoc.z() * std::pow(L, 4)) / (24.0 * p.E * p.Iy) * xi2 * std::pow(1.0 - xi, 2);
        if (p.formulation == Beam1DFormulation::Timoshenko3D && p.G * p.Asz > 1e-12) {
            wz += (qLoc.z() * L * L) / (2.0 * p.G * p.Asz) * xi * (1.0 - xi);
        }
    }

    res.defLocal = {ux, vy, wz};
    return res;
}

} // namespace omniframe::kernel
