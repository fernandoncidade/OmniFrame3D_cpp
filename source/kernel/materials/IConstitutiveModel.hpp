#pragma once

#include "../core/KernelTypes.hpp"
#include <Eigen/Dense>
#include <string>

namespace omniframe::kernel {

enum class StressStrainHypothesis : std::uint8_t {
    Uniaxial1D = 0,
    PlaneStress2D = 1,
    PlaneStrain2D = 2,
    Axisymmetric = 3,
    General3D = 4
};

/**
 * Interface constitutiva pura C++23 para modelos elásticos e inelásticos.
 * Separa explicitamente massa específica física rho (kg/m³ -> coerência dinâmica em t/m³ = kN·s²/m⁴)
 * do peso específico gamma (kN/m³ -> peso próprio estático).
 */
class IConstitutiveModel {
public:
    virtual ~IConstitutiveModel() = default;

    virtual MaterialId id() const = 0;
    virtual const std::string& name() const = 0;
    virtual std::string modelTypeName() const = 0;

    virtual double youngsModulus() const = 0;       // kN/m² (kPa)
    virtual double poissonRatio() const = 0;
    virtual double shearModulus() const = 0;        // kN/m² (kPa)
    virtual double thermalAlpha() const = 0;        // 1/°C
    virtual double densityKgM3() const = 0;         // rho em kg/m³
    virtual double specificWeightKNM3() const = 0;  // gamma em kN/m³

    /// Densidade consistente com sistema [kN, m, s]: 1 t/m³ = 1000 kg/m³ = 1 kN·s²/m⁴
    double consistentMassDensity() const { return densityKgM3() * 1e-3; }

    virtual Eigen::Matrix<double, 6, 6> constitutiveMatrix3D() const = 0;
    virtual Eigen::Matrix3d constitutiveMatrixPlaneStress() const = 0;
    virtual Eigen::Matrix3d constitutiveMatrixPlaneStrain() const = 0;
    virtual Eigen::Matrix<double, 6, 1> thermalStrain3D(double deltaT) const = 0;
    virtual bool isValid(std::string* errorMsg = nullptr) const = 0;
};

} // namespace omniframe::kernel
