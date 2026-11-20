#pragma once

#include "Types.hpp"
#include <Eigen/Dense>
#include <array>

namespace omniframe {

class StructuralModel;

class Element {
public:
    ElementId id = INVALID_ID;
    NodeId nodeStart = INVALID_ID;
    NodeId nodeEnd = INVALID_ID;
    MaterialId materialId = INVALID_ID;
    SectionId sectionId = INVALID_ID;

    double rollAngle = 0.0; // Ângulo de giro da seção (radianos)
    ElementFormulation formulation = ElementFormulation::EulerBernoulli3D;
    ElementReleases releases;
    DistributedLoad distLoad;
    ThermalLoad thermalLoad;

    // Resultados calculados pelo solver
    Eigen::Matrix<double, 12, 1> globalDisplacements = Eigen::Matrix<double, 12, 1>::Zero();
    Eigen::Matrix<double, 12, 1> localDisplacements = Eigen::Matrix<double, 12, 1>::Zero();
    Eigen::Matrix<double, 12, 1> localEndForces = Eigen::Matrix<double, 12, 1>::Zero();
    bool hasResults = false;

    Element() = default;

    Element(ElementId eid, NodeId start, NodeId end, MaterialId mid = 1, SectionId sid = 1, double roll = 0.0)
        : id(eid), nodeStart(start), nodeEnd(end), materialId(mid), sectionId(sid), rollAngle(roll) {}

    double computeLength(const StructuralModel& model) const;
    Vec3 computeDirection(const StructuralModel& model) const;

    // Matriz de rotação 3x3 que leva do sistema global para o local: v_local = R * v_global
    Eigen::Matrix3d computeRotationMatrix3x3(const StructuralModel& model) const;

    // Matriz de transformação 12x12 em blocos: T = diag(R, R, R, R)
    Eigen::Matrix<double, 12, 12> computeTransformationMatrix(const StructuralModel& model) const;

    // Matriz de rigidez local 12x12 não condensada (12 DOFs completos) e condensada se houver liberações
    Eigen::Matrix<double, 12, 12> computeUncondensedLocalStiffnessMatrix(const StructuralModel& model) const;
    Eigen::Matrix<double, 12, 12> computeLocalStiffnessMatrix(const StructuralModel& model) const;

    // Matriz de rigidez global 12x12: K_glob = T^T * K_loc * T
    Eigen::Matrix<double, 12, 12> computeGlobalStiffnessMatrix(const StructuralModel& model) const;

    // Vetores de forças de engastamento perfeito (Fixed-End Forces) não condensados e condensados
    Eigen::Matrix<double, 12, 1> computeUncondensedLocalFixedEndForces(const StructuralModel& model) const;
    Eigen::Matrix<double, 12, 1> computeLocalFixedEndForces(const StructuralModel& model) const;
    Eigen::Matrix<double, 12, 1> computeGlobalFixedEndForces(const StructuralModel& model) const;

    // Recupera os 12 deslocamentos/rotações locais reais (incluindo DOFs condensados pelas rótulas)
    Eigen::Matrix<double, 12, 1> recoverLocalElementDisplacements(
        const StructuralModel& model,
        const Eigen::Matrix<double, 12, 1>& uLocNodal) const;

    // Pós-processamento: cálculo dos esforços internos e deformada em xi in [0, 1]
    InternalForces evaluateAt(double xi, const StructuralModel& model) const;

    void clearResults() {
        globalDisplacements.setZero();
        localDisplacements.setZero();
        localEndForces.setZero();
        hasResults = false;
    }
};

} // namespace omniframe

