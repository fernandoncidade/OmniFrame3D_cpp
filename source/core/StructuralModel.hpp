#pragma once

#include "Types.hpp"
#include "Material.hpp"
#include "CrossSection.hpp"
#include "Node.hpp"
#include "Element.hpp"
#include "kernel/analysis/LoadSystem.hpp"
#include "kernel/core/KernelTypes.hpp"
#include "kernel/mesh/MeshGenerator.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace omniframe {

/**
 * @brief Índice espacial hash 3D leve com lista intrusiva (sem alocações por bucket)
 *        para consultas de proximidade em tempo esperado O(1).
 */
class SpatialPointHash {
public:
    static constexpr std::size_t kInvalidIdx = static_cast<std::size_t>(-1);

    explicit SpatialPointHash(double cellSizeMeters = 1e-4)
        : m_cellSize(cellSizeMeters > 1e-12 ? cellSizeMeters : 1e-4) {}

    void reserve(std::size_t count) {
        m_entries.reserve(count);
        m_buckets.reserve(count);
    }

    void insert(double x, double y, double z, std::size_t payloadIdx) {
        const CellKey key = cellKey(x, y, z);
        auto [it, inserted] = m_buckets.emplace(key, kInvalidIdx);
        const std::size_t idx = m_entries.size();
        m_entries.push_back({Vec3{x, y, z}, payloadIdx, it->second});
        it->second = idx;
    }

    [[nodiscard]] bool findNearestWithinSq(double x,
                                           double y,
                                           double z,
                                           double tolSq,
                                           std::size_t* outPayload = nullptr) const {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            return false;
        }
        const CellKey center = cellKey(x, y, z);
        double bestSq = tolSq;
        bool found = false;
        std::size_t bestPayload = 0;

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const CellKey probe{center.x + dx, center.y + dy, center.z + dz};
                    const auto it = m_buckets.find(probe);
                    if (it == m_buckets.end()) {
                        continue;
                    }
                    for (std::size_t entryIdx = it->second; entryIdx != kInvalidIdx;) {
                        const Entry& e = m_entries[entryIdx];
                        const double ddx = e.pos.x - x;
                        const double ddy = e.pos.y - y;
                        const double ddz = e.pos.z - z;
                        const double dSq = ddx * ddx + ddy * ddy + ddz * ddz;
                        if (dSq <= bestSq) {
                            bestSq = dSq;
                            bestPayload = e.payload;
                            found = true;
                        }
                        entryIdx = e.next;
                    }
                }
            }
        }
        if (found && outPayload) {
            *outPayload = bestPayload;
        }
        return found;
    }

private:
    struct CellKey {
        std::int64_t x = 0;
        std::int64_t y = 0;
        std::int64_t z = 0;
        bool operator==(const CellKey& o) const = default;
    };

    struct CellKeyHash {
        std::size_t operator()(const CellKey& k) const noexcept {
            const std::uint64_t h1 = static_cast<std::uint64_t>(k.x) * 0x9E3779B97F4A7C15ULL;
            const std::uint64_t h2 = static_cast<std::uint64_t>(k.y) * 0xBF58476D1CE4E5B9ULL;
            const std::uint64_t h3 = static_cast<std::uint64_t>(k.z) * 0x94D049BB133111EBULL;
            std::uint64_t h = h1 ^ (h2 + 0x9E3779B97F4A7C15ULL + (h1 << 6) + (h1 >> 2));
            h ^= (h3 + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2));
            return static_cast<std::size_t>(h);
        }
    };

    struct Entry {
        Vec3 pos;
        std::size_t payload = 0;
        std::size_t next = kInvalidIdx;
    };

    CellKey cellKey(double x, double y, double z) const {
        auto quant = [this](double v) -> std::int64_t {
            if (!std::isfinite(v)) return 0;
            const double scaled = std::clamp(std::floor(v / m_cellSize), -1.0e15, 1.0e15);
            return static_cast<std::int64_t>(scaled);
        };
        return {quant(x), quant(y), quant(z)};
    }

    double m_cellSize = 1e-4;
    std::vector<Entry> m_entries;
    std::unordered_map<CellKey, std::size_t, CellKeyHash> m_buckets;
};

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
    StructuralModel cloneWithoutHistory() const;
    [[nodiscard]] std::size_t estimatedMemoryBytes() const noexcept;
    [[nodiscard]] std::size_t estimatedHistoryMemoryBytes() const noexcept;

    // Presets
    static StructuralModel createBuildingFrameExample();
    static StructuralModel createCantileverLFrameExample();
    static StructuralModel createSpaceTrussExample();

    // Undo / Redo e modo lote (batch)
    void beginBatchUpdate();
    void endBatchUpdate(bool pushUndoAtEnd = false);
    bool isBatchUpdating() const { return m_batchUpdateDepth > 0; }

    struct ScopedUndoSuppression {
        explicit ScopedUndoSuppression(StructuralModel& model, bool pushUndoOnExit = false)
            : m_model(&model), m_pushOnExit(pushUndoOnExit) {
            m_model->beginBatchUpdate();
        }
        ~ScopedUndoSuppression() noexcept {
            if (m_model) {
                try {
                    m_model->endBatchUpdate(m_pushOnExit);
                } catch (...) {
                }
            }
        }
        ScopedUndoSuppression(const ScopedUndoSuppression&) = delete;
        ScopedUndoSuppression& operator=(const ScopedUndoSuppression&) = delete;

    private:
        StructuralModel* m_model = nullptr;
        bool m_pushOnExit = false;
    };

    std::uint64_t geometryRevision() const { return m_geometryRevision; }
    void bumpRevision() { ++m_geometryRevision; }

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
        kernel::LinearSolverOptions solverOptions;
        kernel::MeshContainer surfaceMesh;
    };

    ModelState captureState() const;
    void restoreState(const ModelState& state);
    void invalidateAllIndices() const;
    void rebuildNodeIndex() const;
    void rebuildElementIndex() const;
    void rebuildMaterialIndex() const;
    void rebuildSectionIndex() const;

    std::vector<Node> m_nodes;
    std::vector<Element> m_elements;
    std::vector<Material> m_materials;
    std::vector<CrossSection> m_sections;

    mutable std::unordered_map<NodeId, std::size_t> m_nodeIndexById;
    mutable std::unordered_map<ElementId, std::size_t> m_elementIndexById;
    mutable std::unordered_map<MaterialId, std::size_t> m_materialIndexById;
    mutable std::unordered_map<SectionId, std::size_t> m_sectionIndexById;
    mutable bool m_nodeIndexDirty = true;
    mutable bool m_elementIndexDirty = true;
    mutable bool m_materialIndexDirty = true;
    mutable bool m_sectionIndexDirty = true;
    mutable NodeId m_maxNodeId = 0;
    mutable ElementId m_maxElementId = 0;
    mutable MaterialId m_maxMaterialId = 0;
    mutable SectionId m_maxSectionId = 0;

    std::unordered_set<NodeId> m_selectedNodes;
    std::unordered_set<ElementId> m_selectedElements;

    std::vector<ModelState> m_history;
    size_t m_undoIndex = 0;
    int m_batchUpdateDepth = 0;
    std::uint64_t m_geometryRevision = 1;

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

