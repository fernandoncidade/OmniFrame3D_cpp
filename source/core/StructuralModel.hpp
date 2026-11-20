#pragma once

#include "Types.hpp"
#include "Material.hpp"
#include "CrossSection.hpp"
#include "Node.hpp"
#include "Element.hpp"
#include "kernel/analysis/LoadSystem.hpp"
#include "kernel/core/KernelTypes.hpp"
#include "kernel/mesh/MeshGenerator.hpp"

#include <vector>
#include <unordered_set>
#include <string>
#include <memory>

namespace omniframe {

struct RigidLinkSpec {
    NodeId masterNode = INVALID_ID;
    NodeId slaveNode = INVALID_ID;
    bool coupleRotations = true;
};

struct RigidDiaphragmSpec {
    NodeId masterNode = INVALID_ID;
    std::vector<NodeId> slaveNodes;
    int normalAxis = 2; // 0=X, 1=Y, 2=Z
};

struct ProjectMetadata {
    std::string projectName = "Projeto OmniFrame 3D";
    std::string engineerName = "Engenheiro Responsavel";
    std::string clientName = "Cliente / Empreendimento";
    std::string siteLocation = "Brasil";
    double soilSigmaAdm_kPa = 200.0;
    double footingB_m = 1.50;
    double footingL_m = 1.50;
    double windV0_ms = 35.0;
    double windS1 = 1.0;
    double windS2 = 1.0;
    double windS3 = 1.0;
};

enum class IssueSeverity {
    Error = 0,
    Warning = 1,
    Info = 2
};

struct ValidationIssue {
    IssueSeverity severity = IssueSeverity::Error;
    std::string code;
    std::string message;
    std::string category;
    std::string entityId;
};

struct ModelValidation {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<ValidationIssue> issues;
};

class StructuralModel {
public:
    StructuralModel();

    // Gestão de IDs
    NodeId getNextNodeId() const;
    ElementId getNextElementId() const;
    MaterialId getNextMaterialId() const;
    SectionId getNextSectionId() const;

    // Operações em Nós
    NodeId addNode(double x, double y, double z);
    NodeId addNode(const Node& node);
    bool removeNode(NodeId id);

    // Operações em Elementos (Barras)
    ElementId addElement(NodeId start, NodeId end, MaterialId mid = 1, SectionId sid = 1, double roll = 0.0);
    ElementId addElement(const Element& elem);
    bool removeElement(ElementId id);

    // Operações em Materiais
    MaterialId addMaterial(const Material& mat);
    bool removeMaterial(MaterialId id);

    // Operações em Seções Transversais
    SectionId addCrossSection(const CrossSection& sec);
    bool removeCrossSection(SectionId id);

    // Localização de Entidades
    Node* findNode(NodeId id);
    const Node* findNode(NodeId id) const;

    Element* findElement(ElementId id);
    const Element* findElement(ElementId id) const;

    Material* findMaterial(MaterialId id);
    const Material* findMaterial(MaterialId id) const;

    CrossSection* findCrossSection(SectionId id);
    const CrossSection* findCrossSection(SectionId id) const;

    // Acessores de Coleções
    std::vector<Node>& getNodes() { return m_nodes; }
    const std::vector<Node>& getNodes() const { return m_nodes; }

    std::vector<Element>& getElements() { return m_elements; }
    const std::vector<Element>& getElements() const { return m_elements; }

    std::vector<Material>& getMaterials() { return m_materials; }
    const std::vector<Material>& getMaterials() const { return m_materials; }

    std::vector<CrossSection>& getCrossSections() { return m_sections; }
    const std::vector<CrossSection>& getCrossSections() const { return m_sections; }

    // Sistema de Casos de Carga, Combinações, Históricos e MPCs (Fase 1+)
    kernel::LoadCaseId getNextLoadCaseId() const;
    kernel::LoadComboId getNextLoadCombinationId() const;
    kernel::LoadCaseId addLoadCase(const kernel::LoadCase& lc);
    kernel::LoadComboId addLoadCombination(const kernel::LoadCombination& comb);
    kernel::LoadCase* findLoadCase(kernel::LoadCaseId id);
    const kernel::LoadCase* findLoadCase(kernel::LoadCaseId id) const;
    kernel::LoadCombination* findLoadCombination(kernel::LoadComboId id);
    const kernel::LoadCombination* findLoadCombination(kernel::LoadComboId id) const;

    std::vector<kernel::LoadCase>& getLoadCases() { return m_loadCases; }
    const std::vector<kernel::LoadCase>& getLoadCases() const { return m_loadCases; }
    std::vector<kernel::LoadCombination>& getLoadCombinations() { return m_loadCombinations; }
    const std::vector<kernel::LoadCombination>& getLoadCombinations() const { return m_loadCombinations; }
    std::vector<kernel::LoadHistory>& getLoadHistories() { return m_loadHistories; }
    const std::vector<kernel::LoadHistory>& getLoadHistories() const { return m_loadHistories; }
    std::vector<RigidLinkSpec>& getRigidLinks() { return m_rigidLinks; }
    const std::vector<RigidLinkSpec>& getRigidLinks() const { return m_rigidLinks; }
    std::vector<RigidDiaphragmSpec>& getRigidDiaphragms() { return m_rigidDiaphragms; }
    const std::vector<RigidDiaphragmSpec>& getRigidDiaphragms() const { return m_rigidDiaphragms; }

    kernel::LinearSolverOptions& solverOptions() { return m_solverOptions; }
    const kernel::LinearSolverOptions& solverOptions() const { return m_solverOptions; }

    ProjectMetadata& metadata() { return m_metadata; }
    const ProjectMetadata& metadata() const { return m_metadata; }

    kernel::MeshContainer& surfaceMesh() { return m_surfaceMesh; }
    const kernel::MeshContainer& surfaceMesh() const { return m_surfaceMesh; }

    std::string& preservedUnknownJson() { return m_preservedUnknownJson; }
    const std::string& preservedUnknownJson() const { return m_preservedUnknownJson; }
    std::vector<std::string>& serializationWarnings() { return m_serializationWarnings; }
    const std::vector<std::string>& serializationWarnings() const { return m_serializationWarnings; }

    // Seleção
    std::unordered_set<NodeId>& getSelectedNodes() { return m_selectedNodes; }
    const std::unordered_set<NodeId>& getSelectedNodes() const { return m_selectedNodes; }

    std::unordered_set<ElementId>& getSelectedElements() { return m_selectedElements; }
    const std::unordered_set<ElementId>& getSelectedElements() const { return m_selectedElements; }

    void selectNode(NodeId id, bool clearOthers = true);
    void selectElement(ElementId id, bool clearOthers = true);
    void toggleNodeSelection(NodeId id);
    void toggleElementSelection(ElementId id);
    void clearSelection();
    void deleteSelected();

    // Validação e Métricas
    ModelValidation validate() const;
    void getBoundingBox(Vec3& minPt, Vec3& maxPt) const;
    void populateStandardCatalog();
    void syncAutoCapturedViewportLoadCase();
    void clear();
    void clearAll();
    void clearResults();
    void clearUndoHistory();

    // Presets
    static StructuralModel createBuildingFrameExample();
    static StructuralModel createCantileverLFrameExample();
    static StructuralModel createSpaceTrussExample();

    // Undo / Redo
    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo() const { return m_undoIndex > 0; }
    bool canRedo() const { return m_undoIndex + 1 < m_history.size(); }

private:
    struct ModelState {
        std::vector<Node> nodes;
        std::vector<Element> elements;
        std::vector<Material> materials;
        std::vector<CrossSection> sections;
        std::vector<kernel::LoadCase> loadCases;
        std::vector<kernel::LoadCombination> loadCombinations;
        std::vector<kernel::LoadHistory> loadHistories;
        std::vector<RigidLinkSpec> rigidLinks;
        std::vector<RigidDiaphragmSpec> rigidDiaphragms;
        ProjectMetadata metadata;
        kernel::MeshContainer surfaceMesh;
    };

    ModelState captureState() const;
    void restoreState(const ModelState& state);

    std::vector<Node> m_nodes;
    std::vector<Element> m_elements;
    std::vector<Material> m_materials;
    std::vector<CrossSection> m_sections;

    std::unordered_set<NodeId> m_selectedNodes;
    std::unordered_set<ElementId> m_selectedElements;

    std::vector<ModelState> m_history;
    size_t m_undoIndex = 0;

    std::vector<kernel::LoadCase> m_loadCases;
    std::vector<kernel::LoadCombination> m_loadCombinations;
    std::vector<kernel::LoadHistory> m_loadHistories;
    std::vector<RigidLinkSpec> m_rigidLinks;
    std::vector<RigidDiaphragmSpec> m_rigidDiaphragms;
    kernel::LinearSolverOptions m_solverOptions;
    ProjectMetadata m_metadata;
    kernel::MeshContainer m_surfaceMesh;
    std::string m_preservedUnknownJson;
    std::vector<std::string> m_serializationWarnings;
};

} // namespace omniframe

