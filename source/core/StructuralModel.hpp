#pragma once

#include "Types.hpp"
#include "Material.hpp"
#include "CrossSection.hpp"
#include "Node.hpp"
#include "Element.hpp"

#include <vector>
#include <unordered_set>
#include <string>
#include <memory>

namespace omniframe {

struct ModelValidation {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
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
};

} // namespace omniframe

