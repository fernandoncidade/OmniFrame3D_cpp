#pragma once

#include "../core/StructuralModel.hpp"
#include "../kernel/mesh/MeshGenerator.hpp"
#include <string>
#include <vector>

namespace omniframe::io {

struct DxfImportOptions {
    double nodeMergeTolerance = 1e-6;
    MaterialId defaultMaterialId = 1;
    SectionId defaultSectionId = 1;
    bool importPolylinesAsFrames = true;
    std::string layerFilter; // vazio = todas as layers
};

struct DxfInteropReport {
    bool success = false;
    std::string errorMessage;
    int entitiesRead = 0;
    int lineCount = 0;
    int faceCount = 0;
    int mergedNodes = 0;
    std::vector<std::string> layersFound;
};

/**
 * Importador/Exportador nativo de DXF ASCII (ISO/AutoCAD DXF Entities):
 * - Suporta entidades 3D LINE, LWPOLYLINE, POLYLINE/VERTEX e 3DFACE.
 * - Deduplicação espacial automática de nós dentro de `nodeMergeTolerance`.
 * - Zero dependências externas (C++23 puro compatível com GUI e CLI).
 */
class DxfInterop {
public:
    static bool exportToFile(const std::string& filePath,
                             const StructuralModel& model,
                             const kernel::MeshContainer* surfaceOrSolidMesh = nullptr,
                             DxfInteropReport* report = nullptr);

    static std::string exportToString(const StructuralModel& model,
                                      const kernel::MeshContainer* surfaceOrSolidMesh = nullptr,
                                      DxfInteropReport* report = nullptr);

    static bool importFromFile(const std::string& filePath,
                               StructuralModel& outModel,
                               kernel::MeshContainer* outSurfaceMesh = nullptr,
                               const DxfImportOptions& opts = {},
                               DxfInteropReport* report = nullptr);

    static bool importFromString(const std::string& dxfText,
                                 StructuralModel& outModel,
                                 kernel::MeshContainer* outSurfaceMesh = nullptr,
                                 const DxfImportOptions& opts = {},
                                 DxfInteropReport* report = nullptr);
};

} // namespace omniframe::io
