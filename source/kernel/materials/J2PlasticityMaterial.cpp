#include "J2PlasticityMaterial.hpp"
#include "LinearElasticMaterial.hpp"
#include <algorithm>
#include <cmath>

namespace omniframe::kernel {

J2Uniaxial1D::J2Uniaxial1D(const J2PlasticityParams& params)
    : m_p(params)
{
    m_p.E = std::max(1e-6, m_p.E);
    m_p.sigmaY0 = std::max(1e-9, m_p.sigmaY0);
    m_p.H_iso = std::max(0.0, m_p.H_iso);
    m_p.H_kin = std::max(0.0, m_p.H_kin);
}

void J2Uniaxial1D::integrate(double epsTotal, const J2State1D& statePrev, J2State1D& stateTrial) const {
    const double E = m_p.E;
    const double H_iso = m_p.H_iso;
    const double H_kin = m_p.H_kin;
    const double sy = m_p.sigmaY0 + H_iso * statePrev.alphaIso;

    // Preditor elástico 1D
    const double sigmaTrial = E * (epsTotal - statePrev.epsPlastic);
    const double etaTrial = sigmaTrial - statePrev.backstress;
    const double absEta = std::abs(etaTrial);
    const double fTrial = absEta - sy;

    if (fTrial <= 1e-12 * m_p.sigmaY0) {
        stateTrial = statePrev;
        stateTrial.stress = sigmaTrial;
        stateTrial.tangentE = E;
        return;
    }

    const double denom = E + H_iso + H_kin;
    const double dGamma = fTrial / denom;
    const double sgn = (etaTrial >= 0.0) ? 1.0 : -1.0;

    stateTrial.epsPlastic = statePrev.epsPlastic + sgn * dGamma;
    stateTrial.alphaIso = statePrev.alphaIso + dGamma;
    stateTrial.backstress = statePrev.backstress + sgn * H_kin * dGamma;
    stateTrial.stress = sigmaTrial - sgn * E * dGamma;
    stateTrial.tangentE = (E * (H_iso + H_kin)) / denom;
}

J2Plasticity3D::J2Plasticity3D(const J2PlasticityParams& params)
    : m_p(params)
{
    m_p.E = std::max(1e-6, m_p.E);
    m_p.nu = std::clamp(m_p.nu, -0.99, 0.4995);
    m_p.sigmaY0 = std::max(1e-9, m_p.sigmaY0);
    m_p.H_iso = std::max(0.0, m_p.H_iso);
    m_p.H_kin = std::max(0.0, m_p.H_kin);

    m_mu = m_p.E / (2.0 * (1.0 + m_p.nu));
    m_lambda = (m_p.E * m_p.nu) / ((1.0 + m_p.nu) * (1.0 - 2.0 * m_p.nu));
    m_bulk = m_p.E / (3.0 * (1.0 - 2.0 * m_p.nu));
}

Eigen::Matrix<double, 6, 6> J2Plasticity3D::constitutiveMatrix3D() const {
    return LinearElasticIsotropic(m_p.id, m_p.name, m_p.E, m_p.nu, m_p.alphaThermal, m_p.rhoKgM3, m_p.gammaKNM3)
        .constitutiveMatrix3D();
}

Eigen::Matrix3d J2Plasticity3D::constitutiveMatrixPlaneStress() const {
    return LinearElasticIsotropic(m_p.id, m_p.name, m_p.E, m_p.nu, m_p.alphaThermal, m_p.rhoKgM3, m_p.gammaKNM3)
        .constitutiveMatrixPlaneStress();
}

Eigen::Matrix3d J2Plasticity3D::constitutiveMatrixPlaneStrain() const {
    return LinearElasticIsotropic(m_p.id, m_p.name, m_p.E, m_p.nu, m_p.alphaThermal, m_p.rhoKgM3, m_p.gammaKNM3)
        .constitutiveMatrixPlaneStrain();
}

bool J2Plasticity3D::isValid(std::string* errorMsg) const {
    if (!std::isfinite(m_p.E) || m_p.E <= 0.0) {
        if (errorMsg) *errorMsg = "Módulo E inválido em J2Plasticity3D.";
        return false;
    }
    if (!std::isfinite(m_p.sigmaY0) || m_p.sigmaY0 <= 0.0) {
        if (errorMsg) *errorMsg = "Tensão de escoamento sigmaY0 inválida em J2Plasticity3D.";
        return false;
    }
    return LinearElasticIsotropic(m_p.id, m_p.name, m_p.E, m_p.nu, m_p.alphaThermal, m_p.rhoKgM3, m_p.gammaKNM3)
        .isValid(errorMsg);
}

Eigen::Matrix<double, 6, 1> J2Plasticity3D::thermalStrain3D(double deltaT) const {
    Eigen::Matrix<double, 6, 1> eps = Eigen::Matrix<double, 6, 1>::Zero();
    const double th = m_p.alphaThermal * deltaT;
    eps(0) = th;
    eps(1) = th;
    eps(2) = th;
    return eps;
}

void J2Plasticity3D::integrate(const Eigen::Matrix<double, 6, 1>& epsTotal,
                               const J2State3D& statePrev,
                               J2State3D& stateOut,
                               Eigen::Matrix<double, 6, 6>& D_ep) const
{
    using Vec6 = Eigen::Matrix<double, 6, 1>;
    using Mat6 = Eigen::Matrix<double, 6, 6>;

    // Vetor identidade volumétrico em Voigt
    Vec6 I1 = Vec6::Zero();
    I1(0) = 1.0; I1(1) = 1.0; I1(2) = 1.0;

    // Deformação elástica tentativa em notação de engenharia [exx, eyy, ezz, gxy, gyz, gxz]
    const Vec6 epsE_trial = epsTotal - statePrev.epsPlastic;
    const double trEpsE = epsE_trial(0) + epsE_trial(1) + epsE_trial(2);
    const double meanP = m_bulk * trEpsE;

    // Deformação desviadora em componente tensorial [e_xx, e_yy, e_zz, e_xy, e_yz, e_xz]
    Vec6 eDev;
    eDev(0) = epsE_trial(0) - trEpsE / 3.0;
    eDev(1) = epsE_trial(1) - trEpsE / 3.0;
    eDev(2) = epsE_trial(2) - trEpsE / 3.0;
    eDev(3) = 0.5 * epsE_trial(3);
    eDev(4) = 0.5 * epsE_trial(4);
    eDev(5) = 0.5 * epsE_trial(5);

    const Vec6 sTrial = 2.0 * m_mu * eDev;
    const Vec6 etaTrial = sTrial - statePrev.backstressDev;

    // Norma tensorial de Frobenius: ||eta|| = sqrt(eta_xx^2 + eta_yy^2 + eta_zz^2 + 2*(eta_xy^2 + eta_yz^2 + eta_xz^2))
    const double normEtaSq = etaTrial(0) * etaTrial(0) + etaTrial(1) * etaTrial(1) + etaTrial(2) * etaTrial(2)
        + 2.0 * (etaTrial(3) * etaTrial(3) + etaTrial(4) * etaTrial(4) + etaTrial(5) * etaTrial(5));
    const double normEta = std::sqrt(std::max(0.0, normEtaSq));

    const double kSqrt2Over3 = std::sqrt(2.0 / 3.0);
    const double radiusPrev = kSqrt2Over3 * (m_p.sigmaY0 + m_p.H_iso * statePrev.alphaIso);
    const double fTrial = normEta - radiusPrev;

    if (fTrial <= 1e-12 * m_p.sigmaY0 || normEta <= 1e-18) {
        stateOut = statePrev;
        stateOut.stress = meanP * I1 + sTrial;
        const double sNormSq = sTrial(0) * sTrial(0) + sTrial(1) * sTrial(1) + sTrial(2) * sTrial(2)
            + 2.0 * (sTrial(3) * sTrial(3) + sTrial(4) * sTrial(4) + sTrial(5) * sTrial(5));
        stateOut.vonMises = std::sqrt(1.5 * std::max(0.0, sNormSq));
        D_ep = constitutiveMatrix3D();
        return;
    }

    // Mapeamento de retorno radial implícito
    const double denom = 2.0 * m_mu + (2.0 / 3.0) * (m_p.H_iso + m_p.H_kin);
    const double dGamma = fTrial / denom;
    const Vec6 n = etaTrial / normEta; // Normal tensorial unitária

    stateOut.alphaIso = statePrev.alphaIso + kSqrt2Over3 * dGamma;
    stateOut.backstressDev = statePrev.backstressDev + (2.0 / 3.0) * m_p.H_kin * dGamma * n;

    // Incremento de deformação plástica em engenharia Voigt: dEpsP = dGamma * [n11, n22, n33, 2*n12, 2*n23, 2*n13]
    Vec6 nEng = n;
    nEng(3) *= 2.0;
    nEng(4) *= 2.0;
    nEng(5) *= 2.0;
    stateOut.epsPlastic = statePrev.epsPlastic + dGamma * nEng;

    const Vec6 sNew = sTrial - 2.0 * m_mu * dGamma * n;
    stateOut.stress = meanP * I1 + sNew;

    const double sNewNormSq = sNew(0) * sNew(0) + sNew(1) * sNew(1) + sNew(2) * sNew(2)
        + 2.0 * (sNew(3) * sNew(3) + sNew(4) * sNew(4) + sNew(5) * sNew(5));
    stateOut.vonMises = std::sqrt(1.5 * std::max(0.0, sNewNormSq));

    // Tensor tangente consistente algorítmico (Simo-Taylor):
    // D_ep = K*(1 x 1) + 2*mu*theta1*I_dev_voigt - 2*mu*thetaBar*(n x n)
    const double phiG = (2.0 * m_mu * dGamma) / normEta;
    const double theta1 = 1.0 - phiG;
    const double continuumFactor = (2.0 * m_mu) / denom; // = 1 / (1 + (H_iso + H_kin)/(3*mu))
    const double thetaBar = continuumFactor - phiG;

    Mat6 I_dev = Mat6::Zero();
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            I_dev(i, j) = (i == j ? 1.0 : 0.0) - 1.0 / 3.0;
        }
    }
    I_dev(3, 3) = 0.5;
    I_dev(4, 4) = 0.5;
    I_dev(5, 5) = 0.5;

    D_ep = m_bulk * (I1 * I1.transpose())
        + 2.0 * m_mu * theta1 * I_dev
        - 2.0 * m_mu * thetaBar * (n * n.transpose());
}

} // namespace omniframe::kernel
