#pragma once

#include "IFiniteElement.hpp"
#include "../materials/IConstitutiveModel.hpp"
#include <Eigen/Dense>
#include <array>
#include <memory>
#include <vector>

namespace omniframe::kernel {

enum class SolidElementType : std::uint8_t {
    Tet4 = 0,
    Tet10 = 1,
    Hex8BBar = 2,
    Hex20Serendipity = 3
};

/**
 * Resultado de tensão e deformação 3D no centro/ponto de Gauss do elemento sólido.
 */
struct SolidStressState {
    double sxx = 0.0, syy = 0.0, szz = 0.0;
    double txy = 0.0, tyz = 0.0, tzx = 0.0; // Convenção engenharia: [sxx, syy, szz, txy, tyz, tzx]
    double vonMises = 0.0;
    double hydrostaticPressure = 0.0; // -1/3 * (sxx + syy + szz)
    double majorPrincipal = 0.0;
    double minorPrincipal = 0.0;
};

/**
 * Elemento finito sólido tridimensional contínuo (3 DOFs translacionais por nó: ux, uy, uz):
 *  - TET4: tetraedro linear 4 nós (validação estrita de Jacobiano positivo e volume).
 *  - TET10: tetraedro quadrático isoparamétrico 10 nós (quadratura de Hammer exata).
 *  - HEX8 (B-bar): hexaedro trilinear 8 nós com desacoplamento volumétrico/desviatório de Hughes (1980)
 *                  imune a travamento volumétrico próximo à incompressibilidade (nu -> 0.499).
 *  - HEX20: hexaedro quadrático Serendipity 20 nós com integração de Gauss 3x3x3 sem modos espúrios.
 *
 * Matriz de massa usa explicitamente `rhoConsistent` (nunca `gamma`); peso próprio usa `gamma`.
 */
class SolidElement final : public IFiniteElement {
public:
    SolidElement(ElementId eid,
                 SolidElementType solidType,
                 std::vector<NodeId> nodeIds,
                 std::vector<Vec3> nodeCoords,
                 std::shared_ptr<const IConstitutiveModel> material);

    ElementId id() const override { return m_id; }
    ElementFamily family() const override { return ElementFamily::Solid3D; }
    std::string typeName() const override;
    MaterialId materialId() const override { return m_material ? m_material->id() : -1; }
    const std::vector<NodeId>& nodeIds() const override { return m_nodeIds; }

    std::array<bool, NODAL_MAX_DOFS> requiredNodeDofMask() const override {
        return {true, true, true, false, false, false, false};
    }

    int localDofCount() const override { return static_cast<int>(m_nodeIds.size()) * 3; }

    SolidElementType solidType() const { return m_type; }
    double computeVolume() const;
    double computeMinJacobianDet() const;

    MatX computeGlobalStiffness() const override;
    MatX computeGlobalMass(bool consistent = true) const override;
    MatX computeGlobalGeometricStiffness(const VecX& elementGlobalDisp) const override;
    VecX computeGlobalLoadVector(const ElementLoadInput& load) const override;

    SolidStressState evaluateCenterStress(const VecX& elementGlobalDisp, double deltaT = 0.0) const;
    std::vector<SolidStressState> evaluateGaussPointStresses(const VecX& elementGlobalDisp, double deltaT = 0.0) const;

private:
    ElementId m_id = -1;
    SolidElementType m_type = SolidElementType::Hex8BBar;
    std::vector<NodeId> m_nodeIds;
    std::vector<Vec3> m_coords;
    std::shared_ptr<const IConstitutiveModel> m_material;

    void validateGeometryOrThrow() const;
    MatX stiffnessTet4() const;
    MatX stiffnessTet10() const;
    MatX stiffnessHex8BBar() const;
    MatX stiffnessHex20() const;
};

} // namespace omniframe::kernel
