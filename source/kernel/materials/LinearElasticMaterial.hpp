#pragma once

#include "IConstitutiveModel.hpp"

namespace omniframe::kernel {

class LinearElasticIsotropic final : public IConstitutiveModel {
public:
    LinearElasticIsotropic() = default;
    LinearElasticIsotropic(MaterialId id,
                           std::string name,
                           double E_kPa,
                           double nu,
                           double alpha,
                           double density_kg_m3,
                           double gamma_kN_m3);

    MaterialId id() const override { return m_id; }
    const std::string& name() const override { return m_name; }
    std::string modelTypeName() const override { return "LinearElasticIsotropic"; }

    double youngsModulus() const override { return m_E; }
    double poissonRatio() const override { return m_nu; }
    double shearModulus() const override { return m_E / (2.0 * (1.0 + m_nu)); }
    double thermalAlpha() const override { return m_alpha; }
    double densityKgM3() const override { return m_rhoKgM3; }
    double specificWeightKNM3() const override { return m_gammaKNM3; }

    Eigen::Matrix<double, 6, 6> constitutiveMatrix3D() const override;
    Eigen::Matrix3d constitutiveMatrixPlaneStress() const override;
    Eigen::Matrix3d constitutiveMatrixPlaneStrain() const override;
    Eigen::Matrix<double, 6, 1> thermalStrain3D(double deltaT) const override;
    bool isValid(std::string* errorMsg = nullptr) const override;

    static LinearElasticIsotropic steelAstmA36(MaterialId id = 1);
    static LinearElasticIsotropic concreteC30(MaterialId id = 2);

private:
    MaterialId m_id = 1;
    std::string m_name = "Steel";
    double m_E = 2.0e8;
    double m_nu = 0.30;
    double m_alpha = 1.2e-5;
    double m_rhoKgM3 = 7850.0;
    double m_gammaKNM3 = 78.5;
};

class LinearElasticOrthotropic final : public IConstitutiveModel {
public:
    LinearElasticOrthotropic(MaterialId id,
                             std::string name,
                             double Ex, double Ey, double Ez,
                             double nuxy, double nuyz, double nuxz,
                             double Gxy, double Gyz, double Gxz,
                             double alphaX, double alphaY, double alphaZ,
                             double density_kg_m3,
                             double gamma_kN_m3);

    MaterialId id() const override { return m_id; }
    const std::string& name() const override { return m_name; }
    std::string modelTypeName() const override { return "LinearElasticOrthotropic"; }

    double youngsModulus() const override { return m_Ex; }
    double poissonRatio() const override { return m_nuxy; }
    double shearModulus() const override { return m_Gxy; }
    double thermalAlpha() const override { return m_alphaX; }
    double densityKgM3() const override { return m_rhoKgM3; }
    double specificWeightKNM3() const override { return m_gammaKNM3; }

    Eigen::Matrix<double, 6, 6> constitutiveMatrix3D() const override;
    Eigen::Matrix3d constitutiveMatrixPlaneStress() const override;
    Eigen::Matrix3d constitutiveMatrixPlaneStrain() const override;
    Eigen::Matrix<double, 6, 1> thermalStrain3D(double deltaT) const override;
    bool isValid(std::string* errorMsg = nullptr) const override;

private:
    MaterialId m_id = 1;
    std::string m_name = "Orthotropic";
    double m_Ex, m_Ey, m_Ez;
    double m_nuxy, m_nuyz, m_nuxz;
    double m_Gxy, m_Gyz, m_Gxz;
    double m_alphaX, m_alphaY, m_alphaZ;
    double m_rhoKgM3 = 1800.0;
    double m_gammaKNM3 = 17.65;
};

class LinearElasticAnisotropic final : public IConstitutiveModel {
public:
    LinearElasticAnisotropic(MaterialId id,
                             std::string name,
                             const Eigen::Matrix<double, 6, 6>& D3D,
                             double density_kg_m3,
                             double gamma_kN_m3,
                             double thermalAlpha = 1.0e-5);

    MaterialId id() const override { return m_id; }
    const std::string& name() const override { return m_name; }
    std::string modelTypeName() const override { return "LinearElasticAnisotropic"; }

    double youngsModulus() const override { return m_D(0, 0); }
    double poissonRatio() const override { return 0.0; }
    double shearModulus() const override { return m_D(3, 3); }
    double thermalAlpha() const override { return m_alpha; }
    double densityKgM3() const override { return m_rhoKgM3; }
    double specificWeightKNM3() const override { return m_gammaKNM3; }

    Eigen::Matrix<double, 6, 6> constitutiveMatrix3D() const override { return m_D; }
    Eigen::Matrix3d constitutiveMatrixPlaneStress() const override;
    Eigen::Matrix3d constitutiveMatrixPlaneStrain() const override;
    Eigen::Matrix<double, 6, 1> thermalStrain3D(double deltaT) const override;
    bool isValid(std::string* errorMsg = nullptr) const override;

private:
    MaterialId m_id = 1;
    std::string m_name = "Anisotropic";
    Eigen::Matrix<double, 6, 6> m_D;
    double m_rhoKgM3 = 7850.0;
    double m_gammaKNM3 = 78.5;
    double m_alpha = 1.0e-5;
};

} // namespace omniframe::kernel
