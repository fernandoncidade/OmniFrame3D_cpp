#pragma once

#include "../core/KernelTypes.hpp"
#include "../materials/IConstitutiveModel.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace omniframe::kernel {

struct KernelNode {
    NodeId id = -1;
    Vec3 position = Vec3::Zero();
};

struct ElementLoadInput {
    bool hasDistributed = false;
    int coordSystem = 0; // 0 = Global, 1 = Local
    Vec3 distributedForce = Vec3::Zero();
    bool hasThermal = false;
    double deltaT = 0.0;
    double normalPressure = 0.0; // para cascas / faces
    bool hasBodyGravity = false;
    Vec3 gravityAccel = Vec3::Zero();
};

/**
 * Interface base generalizada para elementos finitos 1D, 2D e 3D.
 */
class IFiniteElement {
public:
    virtual ~IFiniteElement() = default;

    virtual ElementId id() const = 0;
    virtual ElementFamily family() const = 0;
    virtual std::string typeName() const = 0;
    virtual MaterialId materialId() const = 0;

    virtual const std::vector<NodeId>& nodeIds() const = 0;
    virtual std::array<bool, NODAL_MAX_DOFS> requiredNodeDofMask() const = 0;
    virtual int localDofCount() const = 0;

    virtual MatX computeGlobalStiffness() const = 0;
    virtual MatX computeGlobalMass(bool consistent = true) const = 0;
    virtual MatX computeGlobalGeometricStiffness(const VecX& elementGlobalDisp) const = 0;
    virtual VecX computeGlobalLoadVector(const ElementLoadInput& load) const = 0;
};

} // namespace omniframe::kernel
