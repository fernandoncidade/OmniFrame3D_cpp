#include "LinearElasticMaterial.hpp"
#include <Eigen/Cholesky>
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cmath>

namespace omniframe::kernel {

LinearElasticIsotropic::LinearElasticIsotropic(MaterialId id,
                                               std::string name,
                                               double E_kPa,
                                               double nu,
                                               double alpha,
                                               double density_kg_m3,
                                               double gamma_kN_m3)
    : m_id(id)
    , m_name(std::move(name))
    , m_E(E_kPa)
    , m_nu(nu)
    , m_alpha(alpha)
    , m_rhoKgM3(density_kg_m3)
    , m_gammaKNM3(gamma_kN_m3) {}

Eigen::Matrix<double, 6, 6> LinearElasticIsotropic::constitutiveMatrix3D() const {
    const double coeff = m_E / ((1.0 + m_nu) * (1.0 - 2.0 * m_nu));
    const double c11 = coeff * (1.0 - m_nu);
    const double c12 = coeff * m_nu;
    const double c44 = m_E / (2.0 * (1.0 + m_nu));

    Eigen::Matrix<double, 6, 6> D = Eigen::Matrix<double, 6, 6>::Zero();
    D(0, 0) = c11; D(0, 1) = c12; D(0, 2) = c12;
    D(1, 0) = c12; D(1, 1) = c11; D(1, 2) = c12;
    D(2, 0) = c12; D(2, 1) = c12; D(2, 2) = c11;
    D(3, 3) = c44;
    D(4, 4) = c44;
    D(5, 5) = c44;
    return D;
}

Eigen::Matrix3d LinearElasticIsotropic::constitutiveMatrixPlaneStress() const {
    const double coeff = m_E / (1.0 - m_nu * m_nu);
    Eigen::Matrix3d D = Eigen::Matrix3d::Zero();
    D(0, 0) = coeff;          D(0, 1) = coeff * m_nu;
    D(1, 0) = coeff * m_nu;   D(1, 1) = coeff;
    D(2, 2) = m_E / (2.0 * (1.0 + m_nu));
    return D;
}

Eigen::Matrix3d LinearElasticIsotropic::constitutiveMatrixPlaneStrain() const {
    const double coeff = m_E / ((1.0 + m_nu) * (1.0 - 2.0 * m_nu));
    Eigen::Matrix3d D = Eigen::Matrix3d::Zero();
    D(0, 0) = coeff * (1.0 - m_nu); D(0, 1) = coeff * m_nu;
    D(1, 0) = coeff * m_nu;         D(1, 1) = coeff * (1.0 - m_nu);
    D(2, 2) = m_E / (2.0 * (1.0 + m_nu));
    return D;
}

Eigen::Matrix<double, 6, 1> LinearElasticIsotropic::thermalStrain3D(double deltaT) const {
    const double eps = m_alpha * deltaT;
    Eigen::Matrix<double, 6, 1> e;
    e << eps, eps, eps, 0.0, 0.0, 0.0;
    return e;
}

bool LinearElasticIsotropic::isValid(std::string* errorMsg) const {
    if (!std::isfinite(m_E) || m_E <= 0.0 ||
        !std::isfinite(m_nu) || m_nu <= -1.0 || m_nu >= 0.5 ||
        !std::isfinite(m_alpha) || m_alpha < 0.0 ||
        !std::isfinite(m_rhoKgM3) || m_rhoKgM3 < 0.0 ||
        !std::isfinite(m_gammaKNM3) || m_gammaKNM3 < 0.0) {
        if (errorMsg) *errorMsg = "Invalid isotropic linear elastic material constants.";
        return false;
    }
    return true;
}

LinearElasticIsotropic LinearElasticIsotropic::steelAstmA36(MaterialId id) {
    return LinearElasticIsotropic(id, "Aço Estrutural (ASTM A36)", 2.0e8, 0.30, 1.2e-5, 7850.0, 78.5);
}

LinearElasticIsotropic LinearElasticIsotropic::concreteC30(MaterialId id) {
    return LinearElasticIsotropic(id, "Concreto C30", 3.0e7, 0.20, 1.0e-5, 2500.0, 25.0);
}

LinearElasticOrthotropic::LinearElasticOrthotropic(MaterialId id,
                                                   std::string name,
                                                   double Ex, double Ey, double Ez,
                                                   double nuxy, double nuyz, double nuxz,
                                                   double Gxy, double Gyz, double Gxz,
                                                   double alphaX, double alphaY, double alphaZ,
                                                   double density_kg_m3,
                                                   double gamma_kN_m3)
    : m_id(id)
    , m_name(std::move(name))
    , m_Ex(Ex), m_Ey(Ey), m_Ez(Ez)
    , m_nuxy(nuxy), m_nuyz(nuyz), m_nuxz(nuxz)
    , m_Gxy(Gxy), m_Gyz(Gyz), m_Gxz(Gxz)
    , m_alphaX(alphaX), m_alphaY(alphaY), m_alphaZ(alphaZ)
    , m_rhoKgM3(density_kg_m3)
    , m_gammaKNM3(gamma_kN_m3) {}

Eigen::Matrix<double, 6, 6> LinearElasticOrthotropic::constitutiveMatrix3D() const {
    Eigen::Matrix<double, 6, 6> S = Eigen::Matrix<double, 6, 6>::Zero();
    S(0, 0) = 1.0 / m_Ex;
    S(1, 1) = 1.0 / m_Ey;
    S(2, 2) = 1.0 / m_Ez;
    S(0, 1) = S(1, 0) = -m_nuxy / m_Ex;
    S(0, 2) = S(2, 0) = -m_nuxz / m_Ex;
    S(1, 2) = S(2, 1) = -m_nuyz / m_Ey;
    S(3, 3) = 1.0 / m_Gxy;
    S(4, 4) = 1.0 / m_Gyz;
    S(5, 5) = 1.0 / m_Gxz;
    return S.inverse();
}

Eigen::Matrix3d LinearElasticOrthotropic::constitutiveMatrixPlaneStress() const {
    const double nuyx = m_nuxy * (m_Ey / m_Ex);
    const double denom = 1.0 - m_nuxy * nuyx;
    Eigen::Matrix3d D = Eigen::Matrix3d::Zero();
    D(0, 0) = m_Ex / denom;
    D(1, 1) = m_Ey / denom;
    D(0, 1) = D(1, 0) = (m_nuxy * m_Ey) / denom;
    D(2, 2) = m_Gxy;
    return D;
}

Eigen::Matrix3d LinearElasticOrthotropic::constitutiveMatrixPlaneStrain() const {
    const Eigen::Matrix<double, 6, 6> D3 = constitutiveMatrix3D();
    Eigen::Matrix3d D;
    D(0, 0) = D3(0, 0); D(0, 1) = D3(0, 1); D(0, 2) = 0.0;
    D(1, 0) = D3(1, 0); D(1, 1) = D3(1, 1); D(1, 2) = 0.0;
    D(2, 0) = 0.0;      D(2, 1) = 0.0;      D(2, 2) = D3(3, 3);
    return D;
}

Eigen::Matrix<double, 6, 1> LinearElasticOrthotropic::thermalStrain3D(double deltaT) const {
    Eigen::Matrix<double, 6, 1> e;
    e << m_alphaX * deltaT, m_alphaY * deltaT, m_alphaZ * deltaT, 0.0, 0.0, 0.0;
    return e;
}

bool LinearElasticOrthotropic::isValid(std::string* errorMsg) const {
    if (m_Ex <= 0.0 || m_Ey <= 0.0 || m_Ez <= 0.0 || m_Gxy <= 0.0 || m_Gyz <= 0.0 || m_Gxz <= 0.0 ||
        !std::isfinite(m_rhoKgM3) || m_rhoKgM3 < 0.0 || !std::isfinite(m_gammaKNM3) || m_gammaKNM3 < 0.0) {
        if (errorMsg) *errorMsg = "Invalid orthotropic elastic moduli or density.";
        return false;
    }
    const Eigen::Matrix<double, 6, 6> D = constitutiveMatrix3D();
    Eigen::LLT<Eigen::Matrix<double, 6, 6>> llt(D);
    if (!D.allFinite() || llt.info() != Eigen::Success) {
        if (errorMsg) *errorMsg = "Orthotropic constitutive tensor is not positive definite.";
        return false;
    }
    return true;
}

LinearElasticAnisotropic::LinearElasticAnisotropic(MaterialId id,
                                                   std::string name,
                                                   const Eigen::Matrix<double, 6, 6>& D3D,
                                                   double density_kg_m3,
                                                   double gamma_kN_m3,
                                                   double thermalAlpha)
    : m_id(id)
    , m_name(std::move(name))
    , m_D(0.5 * (D3D + D3D.transpose()))
    , m_rhoKgM3(density_kg_m3)
    , m_gammaKNM3(gamma_kN_m3)
    , m_alpha(thermalAlpha) {}

Eigen::Matrix3d LinearElasticAnisotropic::constitutiveMatrixPlaneStress() const {
    // [AUD-012] Condensação estática de Schur para tensão plana (sigma_zz = tau_yz = tau_xz = 0):
    // C_ps = C_pp - C_po * C_oo^-1 * C_op
    static constexpr std::array<int, 3> pIdx{0, 1, 3}; // xx, yy, xy
    static constexpr std::array<int, 3> oIdx{2, 4, 5}; // zz, yz, xz

    Eigen::Matrix3d Cpp, Cpo, Coo;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            Cpp(r, c) = m_D(pIdx[static_cast<std::size_t>(r)], pIdx[static_cast<std::size_t>(c)]);
            Cpo(r, c) = m_D(pIdx[static_cast<std::size_t>(r)], oIdx[static_cast<std::size_t>(c)]);
            Coo(r, c) = m_D(oIdx[static_cast<std::size_t>(r)], oIdx[static_cast<std::size_t>(c)]);
        }
    }

    const Eigen::Matrix3d Cop = Cpo.transpose();
    Eigen::LDLT<Eigen::Matrix3d> ldlt(Coo);
    const double maxDiag = std::max(1.0, Coo.diagonal().cwiseAbs().maxCoeff());
    Eigen::Matrix3d X;
    if (ldlt.info() == Eigen::Success && ldlt.vectorD().cwiseAbs().minCoeff() > 1e-16 * maxDiag) {
        X = ldlt.solve(Cop);
    } else {
        X = Coo.fullPivLu().solve(Cop);
    }
    const Eigen::Matrix3d Cps = Cpp - Cpo * X;
    return 0.5 * (Cps + Cps.transpose());
}

Eigen::Matrix3d LinearElasticAnisotropic::constitutiveMatrixPlaneStrain() const {
    static constexpr std::array<int, 3> pIdx{0, 1, 3};
    Eigen::Matrix3d Cpp;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            Cpp(r, c) = m_D(pIdx[static_cast<std::size_t>(r)], pIdx[static_cast<std::size_t>(c)]);
        }
    }
    return 0.5 * (Cpp + Cpp.transpose());
}

Eigen::Matrix<double, 6, 1> LinearElasticAnisotropic::thermalStrain3D(double deltaT) const {
    const double eps = m_alpha * deltaT;
    Eigen::Matrix<double, 6, 1> e;
    e << eps, eps, eps, 0.0, 0.0, 0.0;
    return e;
}

bool LinearElasticAnisotropic::isValid(std::string* errorMsg) const {
    if (!m_D.allFinite() || !std::isfinite(m_rhoKgM3) || m_rhoKgM3 < 0.0 || !std::isfinite(m_gammaKNM3) || m_gammaKNM3 < 0.0) {
        if (errorMsg) *errorMsg = "Non-finite anisotropic constitutive properties.";
        return false;
    }
    Eigen::LLT<Eigen::Matrix<double, 6, 6>> llt(m_D);
    if (llt.info() != Eigen::Success) {
        if (errorMsg) *errorMsg = "Anisotropic constitutive tensor must be symmetric positive definite.";
        return false;
    }
    return true;
}

} // namespace omniframe::kernel
