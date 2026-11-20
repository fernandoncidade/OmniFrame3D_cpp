#pragma once

#include "../core/StructuralModel.hpp"
#include "../kernel/mesh/MeshGenerator.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace omniframe::io {

struct IfcGuidMap {
    std::unordered_map<NodeId, std::string> nodeGuids;
    std::unordered_map<ElementId, std::string> elementGuids;
    std::unordered_map<MaterialId, std::string> materialGuids;
    std::unordered_map<SectionId, std::string> sectionGuids;
};

struct IfcInteropReport {
    bool success = false;
    std::string errorMessage;
    std::string schemaDetected;
    int structuralNodesImported = 0;
    int curveMembersImported = 0;
    int surfaceMembersImported = 0;
    int entitiesExported = 0;
    std::vector<std::string> warnings;
};

/**
 * Interoperabilidade nativa BIM IFC4 (ISO 10303-21 STEP Physical File) em C++23:
 * - Exportação IFC4 completa com IfcStructuralAnalysisModel, IfcStructuralPointConnection,
 *   IfcStructuralCurveMember, IfcStructuralSurfaceMember, perfis (I-Shape / Rectangular / Circle),
 *   materiais e preservação determinística de GUIDs (IfcGloballyUniqueId).
 * - Importação de modelos analíticos IFC4/IFC2X3 + extração básica de geometria unifilar.
 */
class IfcInterop {
public:
    static std::string makeDeterministicGuid(const std::string& seedKey);

    static bool exportToFile(const std::string& filePath,
                             const StructuralModel& model,
                             const kernel::MeshContainer* shellMesh = nullptr,
                             IfcGuidMap* guidMap = nullptr,
                             IfcInteropReport* report = nullptr);

    static std::string exportToString(const StructuralModel& model,
                                      const kernel::MeshContainer* shellMesh = nullptr,
                                      IfcGuidMap* guidMap = nullptr,
                                      IfcInteropReport* report = nullptr);

    static bool importFromFile(const std::string& filePath,
                               StructuralModel& outModel,
                               kernel::MeshContainer* outShellMesh = nullptr,
                               IfcGuidMap* outGuidMap = nullptr,
                               IfcInteropReport* report = nullptr);

    static bool importFromString(const std::string& stepText,
                                 StructuralModel& outModel,
                                 kernel::MeshContainer* outShellMesh = nullptr,
                                 IfcGuidMap* outGuidMap = nullptr,
                                 IfcInteropReport* report = nullptr);
};

} // namespace omniframe::io
