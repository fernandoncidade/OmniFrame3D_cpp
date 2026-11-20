#pragma once

#include "IFiniteElement.hpp"
#include "../materials/IConstitutiveModel.hpp"
#include <Eigen/Dense>
#include <array>
#include <memory>
#include <vector>

namespace omniframe::kernel {

enum class ShellElementType : std::uint8_t {
    Tri3MITC3 = 0,
    Quad4MITC4 = 1,
    Quad8Serendipity = 2
};

/**
 * Esforços internos de casca (por unidade de largura) e tensões nas fibras extremas.
 */
struct ShellStressResultant {
    double Nxx = 0.0, Nyy = 0.0, Nxy = 0.0; // Forças de membrana (kN/m)
    double Mxx = 0.0, Myy = 0.0, Mxy = 0.0; // Momentos fletores e torçores (kN*m/m)
    double Qxz = 0.0, Qyz = 0.0;            // Forças cortantes transversais (kN/m)
    double sigmaVonMisesTop = 0.0;          // Tensão de von Mises na face superior (+t/2) (kPa)
    double sigmaVonMisesMid = 0.0;          // Tensão de von Mises no plano médio (kPa)
    double sigmaVonMisesBot = 0.0;          // Tensão de von Mises na face inferior (-t/2) (kPa)
    double maxVonMises = 0.0;
};

/**
 * Elemento finito de placa/casca 3D geral com formulações livres de shear locking:
 *  - TRI3 / MITC3 (Membrana CST + Mindlin-Reissner com cisalhamento covariante MITC3 de Lee & Bathe + estabilização drilling ortogonal a corpo rígido)
 *  - QUAD4 / MITC4 (Membrana isoparamétrica + modos incompatíveis QM6 condensados + MITC4 de Bathe-Dvorkin + drilling)
 *  - QUAD8 (Serendipity quadrático 8 nós curvo com integração de Gauss 3x3 / 2x2)
 *
 * Sistema local de coordenadas do elemento (e1, e2 no plano médio, e3 normal).
 * Matriz de massa usa explicitamente `rhoConsistent` (nunca `gamma`); peso próprio usa `gamma`.
 */
class ShellElement final : public IFiniteElement {
public:
    ShellElement(ElementId eid,
                 ShellElementType shellType,
                 std::vector<NodeId> nodeIds,
                 std::vector<Vec3> nodeCoords,
                 std::shared_ptr<const IConstitutiveModel> material,
                 double uniformThickness,
                 std::vector<double> nodalThicknesses = {},
                 double drillingStiffnessScale = 0.05);

    ElementId id() const override { return m_id; }
    ElementFamily family() const override { return ElementFamily::Shell2D; }
    std::string typeName() const override;
    MaterialId materialId() const override { return m_material ? m_material->id() : -1; }
    const std::vector<NodeId>& nodeIds() const override { return m_nodeIds; }

    std::array<bool, NODAL_MAX_DOFS> requiredNodeDofMask() const override {
        return {true, true, true, true, true, true, false};
    }

    int localDofCount() const override { return static_cast<int>(m_nodeIds.size()) * 6; }

    ShellElementType shellType() const { return m_type; }
    double uniformThickness() const { return m_thickness; }
    const Mat3& localTriad() const { return m_localTriad; } // Colunas: [e1, e2, e3]
    double computeArea() const;

    MatX computeGlobalStiffness() const override;
    MatX computeGlobalMass(bool consistent = true) const override;
    MatX computeGlobalGeometricStiffness(const VecX& elementGlobalDisp) const override;
    VecX computeGlobalLoadVector(const ElementLoadInput& load) const override;

    /**
     * Recupera esforços resultantes (N, M, Q) e tensões equivalentes de von Mises (topo/meio/fundo)
     * no baricentro do elemento a partir dos deslocamentos globais dos nós.
     */
    ShellStressResultant evaluateCenterStress(const VecX& elementGlobalDisp, double deltaT = 0.0) const;

private:
    ElementId m_id = -1;
    ShellElementType m_type = ShellElementType::Quad4MITC4;
    std::vector<NodeId> m_nodeIds;
    std::vector<Vec3> m_coords;
    std::shared_ptr<const IConstitutiveModel> m_material;
    double m_thickness = 0.15;
    std::vector<double> m_nodalThick;
    double m_drillingScale = 0.05;
    Mat3 m_localTriad = Mat3::Identity(); // Colunas: e1, e2, e3 normal
    std::vector<Eigen::Vector2d> m_localXY;

    void initializeGeometry();
    double thicknessAtWeights(const VecX& shapeN) const;
    Eigen::Matrix<double, 2, 2> transverseShearModulusMatrix() const;
    MatX buildLocalToGlobalBlock() const;

    MatX stiffnessTri3() const;
    MatX stiffnessQuad4() const;
    MatX stiffnessQuad8() const;
};

} // namespace omniframe::kernel
