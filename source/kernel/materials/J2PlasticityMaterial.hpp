#pragma once

#include "IConstitutiveModel.hpp"
#include <Eigen/Dense>
#include <vector>

namespace omniframe::kernel {

/**
 * Estado interno de plasticidade uniaxial 1D (fibras de viga / treliça).
 */
struct J2State1D {
    double epsPlastic = 0.0;
    double alphaIso = 0.0;
    double backstress = 0.0;
    double stress = 0.0;
    double tangentE = 0.0;
};

/**
 * Parâmetros do modelo elastoplástico J2 / von Mises com encruamento misto
 * (isotrópico + cinemático linear de Prager).
 */
struct J2PlasticityParams {
    MaterialId id = 1;
    std::string name = "J2Plasticity";
    double E = 2.0e8;          // kPa (kN/m^2)
    double nu = 0.30;          // Coeficiente de Poisson
    double sigmaY0 = 2.5e5;    // Tensão de escoamento inicial (kPa) -> ex: 250 MPa = 250000 kPa
    double H_iso = 0.0;        // Módulo de encruamento isotrópico linear (kPa)
    double H_kin = 0.0;        // Módulo de encruamento cinemático linear (kPa)
    double alphaThermal = 1.2e-5;
    double rhoKgM3 = 7850.0;
    double gammaKNM3 = 78.5;
};

/**
 * Integrador 1D exato por retorno radial (Euler implícito) com módulo tangente consistente.
 */
class J2Uniaxial1D {
public:
    explicit J2Uniaxial1D(const J2PlasticityParams& params);

    const J2PlasticityParams& params() const { return m_p; }

    /**
     * Integra incremento deformação total `epsTotal` a partir do estado convergido `statePrev`.
     * Não altera `statePrev`, garantindo consistência durante iterações de tentativa de Newton.
     */
    void integrate(double epsTotal, const J2State1D& statePrev, J2State1D& stateTrial) const;

private:
    J2PlasticityParams m_p;
};

/**
 * Estado interno de plasticidade J2 3D em notação de Voigt engenharia:
 * [eps_xx, eps_yy, eps_zz, gamma_xy, gamma_yz, gamma_xz].
 */
struct J2State3D {
    Eigen::Matrix<double, 6, 1> epsPlastic = Eigen::Matrix<double, 6, 1>::Zero();
    double alphaIso = 0.0;
    // Retro-tensão desviadora em Voigt [b_xx, b_yy, b_zz, b_xy, b_yz, b_xz] (simétrica, traço nulo)
    Eigen::Matrix<double, 6, 1> backstressDev = Eigen::Matrix<double, 6, 1>::Zero();
    Eigen::Matrix<double, 6, 1> stress = Eigen::Matrix<double, 6, 1>::Zero();
    double vonMises = 0.0;
};

/**
 * Modelo constitutivo 3D elastoplástico J2 (von Mises) com encruamento isotrópico e
 * cinemático linear, mapeamento de retorno radial exato e matriz tangente algorítmica
 * consistente de Simo-Taylor (simétrica 6x6).
 */
class J2Plasticity3D final : public IConstitutiveModel {
public:
    explicit J2Plasticity3D(const J2PlasticityParams& params);

    const J2PlasticityParams& params() const { return m_p; }

    MaterialId id() const override { return m_p.id; }
    const std::string& name() const override { return m_p.name; }
    std::string modelTypeName() const override { return "J2Plasticity3D"; }

    double youngsModulus() const override { return m_p.E; }
    double poissonRatio() const override { return m_p.nu; }
    double shearModulus() const override { return m_mu; }
    double thermalAlpha() const override { return m_p.alphaThermal; }
    double densityKgM3() const override { return m_p.rhoKgM3; }
    double specificWeightKNM3() const override { return m_p.gammaKNM3; }

    Eigen::Matrix<double, 6, 6> constitutiveMatrix3D() const override;
    Eigen::Matrix3d constitutiveMatrixPlaneStress() const override;
    Eigen::Matrix3d constitutiveMatrixPlaneStrain() const override;
    Eigen::Matrix<double, 6, 1> thermalStrain3D(double deltaT) const override;
    bool isValid(std::string* errorMsg = nullptr) const override;

    /**
     * Integra a relação constitutiva 3D para uma deformação total em engenharia Voigt `epsTotal`
     * (descontada eventual deformação térmica prévia se aplicável), gerando `stateOut` e o
     * tensor tangente consistente `D_ep` (6x6 simétrico).
     */
    void integrate(const Eigen::Matrix<double, 6, 1>& epsTotal,
                   const J2State3D& statePrev,
                   J2State3D& stateOut,
                   Eigen::Matrix<double, 6, 6>& D_ep) const;

private:
    J2PlasticityParams m_p;
    double m_lambda = 0.0;
    double m_mu = 0.0;
    double m_bulk = 0.0;
};

} // namespace omniframe::kernel
