#pragma once

#include "../core/KernelTypes.hpp"
#include <Eigen/Dense>
#include <array>

namespace omniframe::kernel {

enum class Beam1DFormulation : std::uint8_t {
    EulerBernoulli3D = 0,
    Timoshenko3D = 1,
    Truss3D = 2
};

struct Beam1DProps {
    double L = 1.0;
    double E = 2.0e8;
    double G = 7.69e7;
    double alpha = 1.2e-5;
    double rhoConsistent = 7.85; // t/m^3 = kN*s^2/m^4
    double gammaKNM3 = 78.5;     // kN/m^3

    double A = 0.08;
    double Iy = 1e-3;
    double Iz = 1e-3;
    double J = 5e-4;
    double Asy = 0.066667; // Área de cisalhamento em Y local
    double Asz = 0.066667; // Área de cisalhamento em Z local

    Beam1DFormulation formulation = Beam1DFormulation::EulerBernoulli3D;
};

struct Beam1DInternalEval {
    double N = 0.0;
    double Vy = 0.0;
    double Vz = 0.0;
    double Tx = 0.0;
    double My = 0.0;
    double Mz = 0.0;
    Vec3 defLocal = Vec3::Zero();
};

class BeamTruss1D {
public:
    static Eigen::Matrix<double, 12, 12> computeUncondensedLocalStiffness(const Beam1DProps& p);

    static Eigen::Matrix<double, 12, 12> computeConsistentLocalMass(const Beam1DProps& p);
    static Eigen::Matrix<double, 12, 12> computeLumpedLocalMass(const Beam1DProps& p);

    static Eigen::Matrix<double, 12, 12> computeLocalGeometricStiffness(double N_axial, double L);
    static Eigen::Matrix<double, 12, 12> computeLocalGeometricStiffness(double N_axial,
                                                                        double L,
                                                                        Beam1DFormulation formulation);

    static Beam1DInternalEval evaluateLocalDistribution(
        double xi,
        const Beam1DProps& p,
        const Vec3& qLoc,
        const Eigen::Matrix<double, 12, 1>& uLoc,
        const Eigen::Matrix<double, 12, 1>& fEndLoc);
};

} // namespace omniframe::kernel
