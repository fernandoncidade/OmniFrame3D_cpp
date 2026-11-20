#pragma once

#include "../core/KernelTypes.hpp"
#include <Eigen/Dense>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace omniframe::kernel {

enum class MeshCellType : std::uint8_t {
    Line2 = 0,
    Tri3 = 1,
    Quad4 = 2,
    Quad8 = 3,
    Tet4 = 4,
    Tet10 = 5,
    Hex8 = 6,
    Hex20 = 7
};

struct MeshNode {
    NodeId id = 0;
    Vec3 position = Vec3::Zero();
    bool isBoundary = false;
    std::string tag;

    // Boundary conditions, nodal loads and post-processing results
    // Kept after the first 4 members with default initializers for aggregate compatibility
    std::array<bool, 6> fixed{false, false, false, false, false, false};
    std::array<double, 6> springStiffness{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, 6> prescribedDisplacement{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    Vec3 nodalForce = Vec3::Zero();
    Vec3 nodalMoment = Vec3::Zero();

    bool hasResults = false;
    std::array<double, 6> displacement{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, 6> reaction{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    bool hasFixedDofs() const {
        for (bool b : fixed) if (b) return true;
        return false;
    }
    bool hasSprings() const {
        for (double k : springStiffness) if (k > 0.0) return true;
        return false;
    }
    bool hasPrescribedDisplacements() const {
        for (int i = 0; i < 6; ++i) {
            if (fixed[i] && std::abs(prescribedDisplacement[i]) > 1e-15) return true;
        }
        return false;
    }
    bool isRestrained() const { return hasFixedDofs() || hasSprings(); }
    bool isSupported() const { return hasFixedDofs(); }
    bool hasLoad() const {
        return nodalForce.squaredNorm() > 1e-24 || nodalMoment.squaredNorm() > 1e-24;
    }
    double displacementNorm() const {
        return std::sqrt(displacement[0] * displacement[0]
                       + displacement[1] * displacement[1]
                       + displacement[2] * displacement[2]);
    }
    void clearResults() {
        hasResults = false;
        displacement = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        reaction = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    }
};

struct MeshElement {
    ElementId id = 0;
    MeshCellType type = MeshCellType::Quad4;
    std::vector<NodeId> nodeIds;
    std::string groupTag;

    // Structural properties and loading
    int materialId = 1;
    int sectionId = 0;
    double thickness = 0.15;             // Espessura [m] para elementos Shell
    double normalPressure = 0.0;         // Pressão normal local (+z) [kPa] para Shell
    Vec3 distributedLoad = Vec3::Zero(); // Carregamento distribuído global [kN/m^2] ou [kN/m^3]
    double deltaT = 0.0;                 // Variação térmica uniforme [°C]
    bool includeSelfWeight = false;

    // Post-processing results (centroid)
    bool hasResults = false;
    double vonMisesStress = 0.0;
    double maxPrincipalStress = 0.0;
    double minPrincipalStress = 0.0;
    std::array<double, 3> membraneResultants{0.0, 0.0, 0.0}; // Nxx, Nyy, Nxy
    std::array<double, 3> bendingResultants{0.0, 0.0, 0.0};  // Mxx, Myy, Mxy
    std::array<double, 2> transverseShear{0.0, 0.0};         // Qxz, Qyz
    std::array<double, 6> stressVoigt{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // sxx, syy, szz, txy, tyz, tzx

    bool isShell() const {
        return type == MeshCellType::Tri3 || type == MeshCellType::Quad4 || type == MeshCellType::Quad8;
    }
    bool isSolid() const {
        return type == MeshCellType::Tet4 || type == MeshCellType::Tet10
            || type == MeshCellType::Hex8 || type == MeshCellType::Hex20;
    }
    void clearResults() {
        hasResults = false;
        vonMisesStress = 0.0;
        maxPrincipalStress = 0.0;
        minPrincipalStress = 0.0;
        membraneResultants = {0.0, 0.0, 0.0};
        bendingResultants = {0.0, 0.0, 0.0};
        transverseShear = {0.0, 0.0};
        stressVoigt = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    }
};

struct MeshContainer {
    std::vector<MeshNode> nodes;
    std::vector<MeshElement> elements;
    std::unordered_map<std::string, std::vector<NodeId>> nodeSets;
    std::unordered_map<std::string, std::vector<ElementId>> elementGroups;
};

struct MeshQualityMetrics {
    int totalElements = 0;
    int validElements = 0;
    int invertedOrDegenerateCount = 0;
    double minJacobian = 0.0;
    double minAspectRatio = 1.0;
    double maxAspectRatio = 1.0;
    double maxSkewness = 0.0; // [0 = ideal, 1 = degenerado]
    bool isValid() const { return totalElements > 0 && invertedOrDegenerateCount == 0; }
};

/**
 * Gerador nativo de malhas 1D, 2D (Tri3 / Quad4 / Quad8) e 3D (Tet4 / Hex8 / Hex20) em C++23:
 * - Refinamento de linhas 1D
 * - Geração estruturada 2D em planos arbitrários 3D
 * - Triangularização de Delaunay (Bowyer-Watson + suavização Laplaciana) para polígonos planos
 * - Geração estruturada 3D e extrusão 2D -> 3D com subdivisão conforme para Tet4/Hex8
 * - Diagnóstico completo de qualidade (Jacobiano mínimo, razão de aspecto, skewness)
 */
class MeshGenerator {
public:
    static MeshContainer generateLineMesh(const Vec3& p0,
                                          const Vec3& p1,
                                          int subdivisions,
                                          const std::string& groupTag = "Line1D");

    static MeshContainer generateStructuredQuadGrid(const Vec3& origin,
                                                    const Vec3& uSpanVec,
                                                    const Vec3& vSpanVec,
                                                    int nu,
                                                    int nv,
                                                    bool quadraticQuad8 = false,
                                                    const std::string& groupTag = "ShellQuad");

    static MeshContainer generateStructuredTriGrid(const Vec3& origin,
                                                   const Vec3& uSpanVec,
                                                   const Vec3& vSpanVec,
                                                   int nu,
                                                   int nv,
                                                   bool alternateDiagonal = true,
                                                   const std::string& groupTag = "ShellTri");

    static MeshContainer triangulatePolygonDelaunay(const std::vector<Eigen::Vector2d>& polygonXY,
                                                    double targetEdgeSize,
                                                    int smoothPasses = 4,
                                                    const Vec3& origin3D = Vec3::Zero(),
                                                    const Vec3& e1 = Vec3::UnitX(),
                                                    const Vec3& e2 = Vec3::UnitY(),
                                                    const std::string& groupTag = "DelaunayTri3");

    static MeshContainer generateStructuredHexGrid(const Vec3& origin,
                                                   const Vec3& sizeXYZ,
                                                   int nx,
                                                   int ny,
                                                   int nz,
                                                   bool quadraticHex20 = false,
                                                   const std::string& groupTag = "SolidHex");

    static MeshContainer generateStructuredTetGrid(const Vec3& origin,
                                                   const Vec3& sizeXYZ,
                                                   int nx,
                                                   int ny,
                                                   int nz,
                                                   const std::string& groupTag = "SolidTet4");

    static MeshContainer extrudeSurfaceTo3D(const MeshContainer& surface2D,
                                            const Vec3& extrudeVector,
                                            int layers,
                                            bool convertQuadToTet = false,
                                            const std::string& groupTag = "Extruded3D");

    static void assignPlaneNodeSet(MeshContainer& mesh,
                                   const std::string& setName,
                                   const Vec3& pointOnPlane,
                                   const Vec3& unitNormal,
                                   double tolerance = 1e-6);

    static MeshQualityMetrics evaluateQuality(const MeshContainer& mesh);
};

} // namespace omniframe::kernel
