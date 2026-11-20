#pragma once

#include "../core/KernelTypes.hpp"
#include <Eigen/Dense>
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
};

struct MeshElement {
    ElementId id = 0;
    MeshCellType type = MeshCellType::Quad4;
    std::vector<NodeId> nodeIds;
    std::string groupTag;
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
