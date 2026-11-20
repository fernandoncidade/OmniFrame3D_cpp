#include "StructuralModel.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace omniframe {

StructuralModel::StructuralModel() {
    // Inicializa materiais e seções padrão
    addMaterial(Material::makeStructuralSteel(1));
    addMaterial(Material::makeConcreteC30(2));

    addCrossSection(CrossSection::makeRectangular(1, "Viga Concreto 20x50", 0.20, 0.50));
    addCrossSection(CrossSection::makeRectangular(2, "Pilar Concreto 30x30", 0.30, 0.30));
    addCrossSection(CrossSection::makeCircularTube(3, "Tubo Aço D200 t10", 0.20, 0.01));
    addCrossSection(CrossSection::makeIShape(4, "Perfil I W200x35.9", 0.201, 0.165, 0.0102, 0.0062));
    populateStandardCatalog();

    pushUndo();
}

void StructuralModel::invalidateAllIndices() const {
    m_nodeIndexDirty = true;
    m_elementIndexDirty = true;
    m_materialIndexDirty = true;
    m_sectionIndexDirty = true;
}

void StructuralModel::rebuildNodeIndex() const {
    m_nodeIndexById.clear();
    m_nodeIndexById.reserve(m_nodes.size());
    m_maxNodeId = 0;
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        const NodeId id = m_nodes[i].id;
        m_nodeIndexById[id] = i;
        if (id > m_maxNodeId) m_maxNodeId = id;
    }
    m_nodeIndexDirty = false;
}

void StructuralModel::rebuildElementIndex() const {
    m_elementIndexById.clear();
    m_elementIndexById.reserve(m_elements.size());
    m_maxElementId = 0;
    for (std::size_t i = 0; i < m_elements.size(); ++i) {
        const ElementId id = m_elements[i].id;
        m_elementIndexById[id] = i;
        if (id > m_maxElementId) m_maxElementId = id;
    }
    m_elementIndexDirty = false;
}

void StructuralModel::rebuildMaterialIndex() const {
    m_materialIndexById.clear();
    m_materialIndexById.reserve(m_materials.size());
    m_maxMaterialId = 0;
    for (std::size_t i = 0; i < m_materials.size(); ++i) {
        const MaterialId id = m_materials[i].id;
        m_materialIndexById[id] = i;
        if (id > m_maxMaterialId) m_maxMaterialId = id;
    }
    m_materialIndexDirty = false;
}

void StructuralModel::rebuildSectionIndex() const {
    m_sectionIndexById.clear();
    m_sectionIndexById.reserve(m_sections.size());
    m_maxSectionId = 0;
    for (std::size_t i = 0; i < m_sections.size(); ++i) {
        const SectionId id = m_sections[i].id;
        m_sectionIndexById[id] = i;
        if (id > m_maxSectionId) m_maxSectionId = id;
    }
    m_sectionIndexDirty = false;
}

NodeId StructuralModel::getNextNodeId() const {
    if (m_nodeIndexDirty || m_nodeIndexById.size() != m_nodes.size()) {
        rebuildNodeIndex();
    }
    return m_maxNodeId + 1;
}

ElementId StructuralModel::getNextElementId() const {
    if (m_elementIndexDirty || m_elementIndexById.size() != m_elements.size()) {
        rebuildElementIndex();
    }
    return m_maxElementId + 1;
}

MaterialId StructuralModel::getNextMaterialId() const {
    if (m_materialIndexDirty || m_materialIndexById.size() != m_materials.size()) {
        rebuildMaterialIndex();
    }
    return m_maxMaterialId + 1;
}

SectionId StructuralModel::getNextSectionId() const {
    if (m_sectionIndexDirty || m_sectionIndexById.size() != m_sections.size()) {
        rebuildSectionIndex();
    }
    return m_maxSectionId + 1;
}

NodeId StructuralModel::addNode(double x, double y, double z) {
    NodeId id = getNextNodeId();
    m_nodes.emplace_back(id, x, y, z);
    if (!m_nodeIndexDirty) {
        m_nodeIndexById[id] = m_nodes.size() - 1;
        if (id > m_maxNodeId) m_maxNodeId = id;
    }
    ++m_geometryRevision;
    pushUndo();
    return id;
}

NodeId StructuralModel::addNode(const Node& node) {
    Node n = node;
    if (n.id <= 0 || findNode(n.id) != nullptr) {
        n.id = getNextNodeId();
    }
    m_nodes.push_back(n);
    if (!m_nodeIndexDirty) {
        m_nodeIndexById[n.id] = m_nodes.size() - 1;
        if (n.id > m_maxNodeId) m_maxNodeId = n.id;
    }
    ++m_geometryRevision;
    pushUndo();
    return n.id;
}

bool StructuralModel::removeNode(NodeId id) {
    if (!findNode(id)) return false;
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [id](const Node& n) { return n.id == id; });
    if (it == m_nodes.end()) return false;

    std::unordered_set<ElementId> removedElemIds;
    std::erase_if(m_elements, [id, &removedElemIds](const Element& e) {
        if (e.nodeStart == id || e.nodeEnd == id) {
            removedElemIds.insert(e.id);
            return true;
        }
        return false;
    });

    for (ElementId eid : removedElemIds) {
        m_selectedElements.erase(eid);
    }

    for (auto& lc : m_loadCases) {
        std::erase_if(lc.nodalLoads, [id](const kernel::NodalLoadPatternEntry& nl) {
            return nl.nodeId == id;
        });
        if (!removedElemIds.empty()) {
            std::erase_if(lc.elementLoads, [&removedElemIds](const kernel::ElementLoadPatternEntry& el) {
                return removedElemIds.find(el.elementId) != removedElemIds.end();
            });
        }
    }

    std::erase_if(m_rigidLinks, [id](const RigidLinkSpec& rl) {
        return rl.masterNode == id || rl.slaveNode == id;
    });

    std::erase_if(m_rigidDiaphragms, [id](RigidDiaphragmSpec& rd) {
        if (rd.masterNode == id) return true;
        std::erase(rd.slaveNodes, id);
        return rd.slaveNodes.empty();
    });

    m_selectedNodes.erase(id);
    m_nodes.erase(it);
    m_nodeIndexDirty = true;
    if (!removedElemIds.empty()) {
        m_elementIndexDirty = true;
    }
    ++m_geometryRevision;
    pushUndo();
    return true;
}

ElementId StructuralModel::addElement(NodeId start, NodeId end, MaterialId mid, SectionId sid, double roll) {
    if (start == end) return INVALID_ID;
    if (!findNode(start) || !findNode(end)) return INVALID_ID;

    ElementId id = getNextElementId();
    m_elements.emplace_back(id, start, end, mid, sid, roll);
    if (!m_elementIndexDirty) {
        m_elementIndexById[id] = m_elements.size() - 1;
        if (id > m_maxElementId) m_maxElementId = id;
    }
    ++m_geometryRevision;
    pushUndo();
    return id;
}

ElementId StructuralModel::addElement(const Element& elem) {
    Element e = elem;
    if (e.id <= 0 || findElement(e.id) != nullptr) {
        e.id = getNextElementId();
    }
    m_elements.push_back(e);
    if (!m_elementIndexDirty) {
        m_elementIndexById[e.id] = m_elements.size() - 1;
        if (e.id > m_maxElementId) m_maxElementId = e.id;
    }
    ++m_geometryRevision;
    pushUndo();
    return e.id;
}

bool StructuralModel::removeElement(ElementId id) {
    if (!findElement(id)) return false;
    auto it = std::find_if(m_elements.begin(), m_elements.end(), [id](const Element& e) { return e.id == id; });
    if (it == m_elements.end()) return false;

    for (auto& lc : m_loadCases) {
        std::erase_if(lc.elementLoads, [id](const kernel::ElementLoadPatternEntry& el) {
            return el.elementId == id;
        });
    }

    m_selectedElements.erase(id);
    m_elements.erase(it);
    m_elementIndexDirty = true;
    ++m_geometryRevision;
    pushUndo();
    return true;
}

MaterialId StructuralModel::addMaterial(const Material& mat) {
    Material m = mat;
    if (m.id <= 0) {
        m.id = getNextMaterialId();
    } else if (Material* existing = findMaterial(m.id)) {
        const bool inUse =
            std::any_of(m_elements.begin(), m_elements.end(),
                        [&](const Element& e) { return e.materialId == m.id; }) ||
            std::any_of(m_surfaceMesh.elements.begin(), m_surfaceMesh.elements.end(),
                        [&](const kernel::MeshElement& me) { return me.materialId == m.id; });
        if (m.id > 2 && !inUse) {
            existing->id = getNextMaterialId();
            m_materialIndexDirty = true;
        } else {
            m.id = getNextMaterialId();
        }
    }
    m_materials.push_back(m);
    if (!m_materialIndexDirty) {
        m_materialIndexById[m.id] = m_materials.size() - 1;
        if (m.id > m_maxMaterialId) m_maxMaterialId = m.id;
    }
    ++m_geometryRevision;
    return m.id;
}

bool StructuralModel::removeMaterial(MaterialId id) {
    auto it = std::find_if(m_materials.begin(), m_materials.end(), [id](const Material& m) { return m.id == id; });
    if (it == m_materials.end()) return false;
    const bool inUse =
        std::any_of(m_elements.begin(), m_elements.end(),
                    [id](const Element& e) { return e.materialId == id; }) ||
        std::any_of(m_surfaceMesh.elements.begin(), m_surfaceMesh.elements.end(),
                    [id](const kernel::MeshElement& me) { return me.materialId == id; });
    if (inUse) return false;
    m_materials.erase(it);
    m_materialIndexDirty = true;
    ++m_geometryRevision;
    return true;
}

SectionId StructuralModel::addCrossSection(const CrossSection& sec) {
    CrossSection s = sec;
    if (s.id <= 0) {
        s.id = getNextSectionId();
    } else if (CrossSection* existing = findCrossSection(s.id)) {
        const bool inUse =
            std::any_of(m_elements.begin(), m_elements.end(),
                        [&](const Element& e) { return e.sectionId == s.id; }) ||
            std::any_of(m_surfaceMesh.elements.begin(), m_surfaceMesh.elements.end(),
                        [&](const kernel::MeshElement& me) { return me.sectionId == s.id; });
        if (s.id > 4 && !inUse) {
            existing->id = getNextSectionId();
            m_sectionIndexDirty = true;
        } else {
            s.id = getNextSectionId();
        }
    }
    m_sections.push_back(s);
    if (!m_sectionIndexDirty) {
        m_sectionIndexById[s.id] = m_sections.size() - 1;
        if (s.id > m_maxSectionId) m_maxSectionId = s.id;
    }
    ++m_geometryRevision;
    return s.id;
}

bool StructuralModel::removeCrossSection(SectionId id) {
    auto it = std::find_if(m_sections.begin(), m_sections.end(), [id](const CrossSection& s) { return s.id == id; });
    if (it == m_sections.end()) return false;
    const bool inUse =
        std::any_of(m_elements.begin(), m_elements.end(),
                    [id](const Element& e) { return e.sectionId == id; }) ||
        std::any_of(m_surfaceMesh.elements.begin(), m_surfaceMesh.elements.end(),
                    [id](const kernel::MeshElement& me) { return me.sectionId == id; });
    if (inUse) return false;
    m_sections.erase(it);
    m_sectionIndexDirty = true;
    ++m_geometryRevision;
    return true;
}

Node* StructuralModel::findNode(NodeId id) {
    return const_cast<Node*>(static_cast<const StructuralModel*>(this)->findNode(id));
}

const Node* StructuralModel::findNode(NodeId id) const {
    if (m_nodeIndexDirty || m_nodeIndexById.size() != m_nodes.size()) {
        rebuildNodeIndex();
    }
    if (const auto it = m_nodeIndexById.find(id); it != m_nodeIndexById.end()) {
        const std::size_t idx = it->second;
        if (idx < m_nodes.size() && m_nodes[idx].id == id) {
            return &m_nodes[idx];
        }
        rebuildNodeIndex();
        if (const auto it2 = m_nodeIndexById.find(id); it2 != m_nodeIndexById.end()) {
            const std::size_t idx2 = it2->second;
            if (idx2 < m_nodes.size() && m_nodes[idx2].id == id) {
                return &m_nodes[idx2];
            }
        }
    }
    return nullptr;
}

Element* StructuralModel::findElement(ElementId id) {
    return const_cast<Element*>(static_cast<const StructuralModel*>(this)->findElement(id));
}

const Element* StructuralModel::findElement(ElementId id) const {
    if (m_elementIndexDirty || m_elementIndexById.size() != m_elements.size()) {
        rebuildElementIndex();
    }
    if (const auto it = m_elementIndexById.find(id); it != m_elementIndexById.end()) {
        const std::size_t idx = it->second;
        if (idx < m_elements.size() && m_elements[idx].id == id) {
            return &m_elements[idx];
        }
        rebuildElementIndex();
        if (const auto it2 = m_elementIndexById.find(id); it2 != m_elementIndexById.end()) {
            const std::size_t idx2 = it2->second;
            if (idx2 < m_elements.size() && m_elements[idx2].id == id) {
                return &m_elements[idx2];
            }
        }
    }
    return nullptr;
}

Material* StructuralModel::findMaterial(MaterialId id) {
    return const_cast<Material*>(static_cast<const StructuralModel*>(this)->findMaterial(id));
}

const Material* StructuralModel::findMaterial(MaterialId id) const {
    if (m_materialIndexDirty || m_materialIndexById.size() != m_materials.size()) {
        rebuildMaterialIndex();
    }
    if (const auto it = m_materialIndexById.find(id); it != m_materialIndexById.end()) {
        const std::size_t idx = it->second;
        if (idx < m_materials.size() && m_materials[idx].id == id) {
            return &m_materials[idx];
        }
        rebuildMaterialIndex();
        if (const auto it2 = m_materialIndexById.find(id); it2 != m_materialIndexById.end()) {
            const std::size_t idx2 = it2->second;
            if (idx2 < m_materials.size() && m_materials[idx2].id == id) {
                return &m_materials[idx2];
            }
        }
    }
    return nullptr;
}

CrossSection* StructuralModel::findCrossSection(SectionId id) {
    return const_cast<CrossSection*>(static_cast<const StructuralModel*>(this)->findCrossSection(id));
}

const CrossSection* StructuralModel::findCrossSection(SectionId id) const {
    if (m_sectionIndexDirty || m_sectionIndexById.size() != m_sections.size()) {
        rebuildSectionIndex();
    }
    if (const auto it = m_sectionIndexById.find(id); it != m_sectionIndexById.end()) {
        const std::size_t idx = it->second;
        if (idx < m_sections.size() && m_sections[idx].id == id) {
            return &m_sections[idx];
        }
        rebuildSectionIndex();
        if (const auto it2 = m_sectionIndexById.find(id); it2 != m_sectionIndexById.end()) {
            const std::size_t idx2 = it2->second;
            if (idx2 < m_sections.size() && m_sections[idx2].id == id) {
                return &m_sections[idx2];
            }
        }
    }
    return nullptr;
}

void StructuralModel::selectNode(NodeId id, bool clearOthers) {
    if (clearOthers) {
        m_selectedNodes.clear();
        m_selectedElements.clear();
    }
    if (findNode(id)) {
        m_selectedNodes.insert(id);
    }
    ++m_geometryRevision;
}

void StructuralModel::selectElement(ElementId id, bool clearOthers) {
    if (clearOthers) {
        m_selectedNodes.clear();
        m_selectedElements.clear();
    }
    if (findElement(id)) {
        m_selectedElements.insert(id);
    }
    ++m_geometryRevision;
}

void StructuralModel::toggleNodeSelection(NodeId id) {
    if (m_selectedNodes.contains(id)) {
        m_selectedNodes.erase(id);
    } else if (findNode(id)) {
        m_selectedNodes.insert(id);
    }
    ++m_geometryRevision;
}

void StructuralModel::toggleElementSelection(ElementId id) {
    if (m_selectedElements.contains(id)) {
        m_selectedElements.erase(id);
    } else if (findElement(id)) {
        m_selectedElements.insert(id);
    }
    ++m_geometryRevision;
}

void StructuralModel::clearSelection() {
    m_selectedNodes.clear();
    m_selectedElements.clear();
    ++m_geometryRevision;
}

void StructuralModel::deleteSelected() {
    if (m_selectedNodes.empty() && m_selectedElements.empty()) return;

    std::unordered_set<ElementId> removedElemIds = m_selectedElements;
    const std::unordered_set<NodeId> removedNodeIds = m_selectedNodes;

    std::erase_if(m_elements, [&](const Element& e) {
        if (removedElemIds.find(e.id) != removedElemIds.end() ||
            removedNodeIds.find(e.nodeStart) != removedNodeIds.end() ||
            removedNodeIds.find(e.nodeEnd) != removedNodeIds.end()) {
            removedElemIds.insert(e.id);
            return true;
        }
        return false;
    });
    m_selectedElements.clear();
    m_elementIndexDirty = true;

    if (!removedNodeIds.empty()) {
        std::erase_if(m_nodes, [&removedNodeIds](const Node& n) {
            return removedNodeIds.find(n.id) != removedNodeIds.end();
        });
        m_nodeIndexDirty = true;
        std::erase_if(m_rigidLinks, [&removedNodeIds](const RigidLinkSpec& rl) {
            return removedNodeIds.find(rl.masterNode) != removedNodeIds.end() ||
                   removedNodeIds.find(rl.slaveNode) != removedNodeIds.end();
        });
        std::erase_if(m_rigidDiaphragms, [&removedNodeIds](RigidDiaphragmSpec& rd) {
            if (removedNodeIds.find(rd.masterNode) != removedNodeIds.end()) return true;
            std::erase_if(rd.slaveNodes, [&removedNodeIds](NodeId sid) {
                return removedNodeIds.find(sid) != removedNodeIds.end();
            });
            return rd.slaveNodes.empty();
        });
    }

    for (auto& lc : m_loadCases) {
        if (!removedNodeIds.empty()) {
            std::erase_if(lc.nodalLoads, [&removedNodeIds](const kernel::NodalLoadPatternEntry& nl) {
                return removedNodeIds.find(nl.nodeId) != removedNodeIds.end();
            });
        }
        if (!removedElemIds.empty()) {
            std::erase_if(lc.elementLoads, [&removedElemIds](const kernel::ElementLoadPatternEntry& el) {
                return removedElemIds.find(el.elementId) != removedElemIds.end();
            });
        }
    }

    m_selectedNodes.clear();
    ++m_geometryRevision;
    pushUndo();
}

ModelValidation StructuralModel::validate() const {
    ModelValidation res;

    auto addError = [&](const std::string& code, const std::string& msg,
                        const std::string& cat = "Model", const std::string& ent = "") {
        res.valid = false;
        res.errors.push_back(msg);
        res.issues.push_back({IssueSeverity::Error, code, msg, cat, ent});
    };
    auto addWarning = [&](const std::string& code, const std::string& msg,
                          const std::string& cat = "Model", const std::string& ent = "") {
        res.warnings.push_back(msg);
        res.issues.push_back({IssueSeverity::Warning, code, msg, cat, ent});
    };

    auto isFiniteVec3 = [](const Vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    auto isFiniteKVec3 = [](const kernel::Vec3& v) {
        return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
    };

    const bool hasBars = !m_elements.empty();
    const bool hasMesh = !m_surfaceMesh.elements.empty();

    if (!hasBars && !hasMesh) {
        if (m_nodes.size() < 2) {
            addError("ERR_TOO_FEW_NODES", "O modelo precisa ter pelo menos 2 nós.", "Geometry");
        }
        addError("ERR_NO_ELEMENTS",
                 "O modelo precisa ter pelo menos 1 barra ou 1 elemento de malha 2D/3D.",
                 "Topology");
    } else if (hasBars && !hasMesh && m_nodes.size() < 2) {
        addError("ERR_TOO_FEW_NODES", "O modelo precisa ter pelo menos 2 nós.", "Geometry");
    }

    std::unordered_set<MaterialId> matIds;
    for (const auto& m : m_materials) {
        if (m.id <= 0 || !matIds.insert(m.id).second) {
            addError("ERR_MATERIAL_ID",
                     "Material " + std::to_string(m.id) + " possui ID inválido ou duplicado.",
                     "Material", std::to_string(m.id));
        }
        if (!std::isfinite(m.E) || m.E <= 0.0 ||
            !std::isfinite(m.nu) || m.nu <= -1.0 || m.nu >= 0.5 ||
            !std::isfinite(m.alpha) || m.alpha < 0.0 ||
            !std::isfinite(m.gamma) || m.gamma < 0.0 ||
            !std::isfinite(m.rho) || m.rho < 0.0) {
            addError("ERR_MATERIAL_PARAMS",
                     "Material " + std::to_string(m.id) + " possui parâmetros físicos inválidos.",
                     "Material", std::to_string(m.id));
        }
    }

    std::unordered_set<SectionId> secIds;
    for (const auto& s : m_sections) {
        if (s.id <= 0 || !secIds.insert(s.id).second) {
            addError("ERR_SECTION_ID",
                     "Seção " + std::to_string(s.id) + " possui ID inválido ou duplicado.",
                     "Section", std::to_string(s.id));
        }
        if (!std::isfinite(s.A) || s.A <= 0.0 ||
            !std::isfinite(s.Iy) || s.Iy <= 0.0 ||
            !std::isfinite(s.Iz) || s.Iz <= 0.0 ||
            !std::isfinite(s.J) || s.J <= 0.0) {
            addError("ERR_SECTION_PARAMS",
                     "Seção " + std::to_string(s.id) + " possui propriedades geométricas inválidas.",
                     "Section", std::to_string(s.id));
        }
    }

    bool hasSupport = false;
    std::unordered_set<NodeId> nodeIds;
    nodeIds.reserve(m_nodes.size());
    for (const auto& n : m_nodes) {
        if (n.id <= 0 || !nodeIds.insert(n.id).second) {
            addError("ERR_NODE_ID",
                     "Nó " + std::to_string(n.id) + " possui ID inválido ou duplicado.",
                     "Node", std::to_string(n.id));
        }
        if (!isFiniteVec3(n.position) || !isFiniteVec3(n.load.force) || !isFiniteVec3(n.load.moment)) {
            addError("ERR_NODE_NONFINITE",
                     "Nó " + std::to_string(n.id) + " possui coordenadas ou forças não finitas.",
                     "Node", std::to_string(n.id));
        } else {
            constexpr double kMaxPhysCoordM = 1e9;
            if (std::abs(n.position.x) > kMaxPhysCoordM || std::abs(n.position.y) > kMaxPhysCoordM ||
                std::abs(n.position.z) > kMaxPhysCoordM) {
                addError("ERR_NODE_COORD_RANGE",
                         "Nó " + std::to_string(n.id) + " possui coordenadas fora dos limites físicos (|coord| <= 1e9 m).",
                         "Node", std::to_string(n.id));
            }
        }
        for (int i = 0; i < 6; ++i) {
            if (!std::isfinite(n.support.springStiffness[i]) || n.support.springStiffness[i] < 0.0 ||
                !std::isfinite(n.support.prescribedValue[i])) {
                addError("ERR_NODE_SUPPORT_PARAMS",
                         "Nó " + std::to_string(n.id) + " possui molas ou recalques inválidos.",
                         "BoundaryCondition", std::to_string(n.id));
                break;
            }
        }
        if (n.isSupported()) {
            hasSupport = true;
        }
    }

    std::unordered_set<NodeId> connectedNodes;
    connectedNodes.reserve(m_elements.size() * 2);
    std::unordered_set<ElementId> elemIds;
    elemIds.reserve(m_elements.size());

    for (const auto& e : m_elements) {
        if (e.id <= 0 || !elemIds.insert(e.id).second) {
            addError("ERR_ELEMENT_ID",
                     "Barra " + std::to_string(e.id) + " possui ID inválido ou duplicado.",
                     "Element", std::to_string(e.id));
        }
        const Node* n1 = findNode(e.nodeStart);
        const Node* n2 = findNode(e.nodeEnd);
        if (!n1 || !n2) {
            addError("ERR_ELEMENT_MISSING_NODE",
                     "Barra " + std::to_string(e.id) + " conecta a nó inexistente.",
                     "Topology", std::to_string(e.id));
            continue;
        }
        if (e.nodeStart == e.nodeEnd) {
            addError("ERR_ELEMENT_ZERO_LEN",
                     "Barra " + std::to_string(e.id) + " tem comprimento nulo.",
                     "Geometry", std::to_string(e.id));
        }
        connectedNodes.insert(e.nodeStart);
        connectedNodes.insert(e.nodeEnd);
        double len = e.computeLength(*this);
        if (!std::isfinite(len) || len < 1e-6) {
            addError("ERR_ELEMENT_ZERO_LEN",
                     "Barra " + std::to_string(e.id) + " tem comprimento nulo.",
                     "Geometry", std::to_string(e.id));
        }
        if (!std::isfinite(e.rollAngle) || !isFiniteVec3(e.distLoad.w) || !std::isfinite(e.thermalLoad.deltaT)) {
            addError("ERR_ELEMENT_NONFINITE",
                     "Barra " + std::to_string(e.id) + " possui parâmetros numéricos não finitos.",
                     "Element", std::to_string(e.id));
        }
        if (!findMaterial(e.materialId)) {
            addError("ERR_ELEMENT_MISSING_MATERIAL",
                     "Barra " + std::to_string(e.id) + " referencia material inexistente.",
                     "Material", std::to_string(e.id));
        }
        if (!findCrossSection(e.sectionId)) {
            addError("ERR_ELEMENT_MISSING_SECTION",
                     "Barra " + std::to_string(e.id) + " referencia seção transversal inexistente.",
                     "Section", std::to_string(e.id));
        }
    }

    // Validação da malha contínua 2D/3D (surfaceMesh)
    std::unordered_map<NodeId, const kernel::MeshNode*> meshNodeMap;
    meshNodeMap.reserve(m_surfaceMesh.nodes.size());
    std::unordered_set<NodeId> connectedMeshNodes;
    connectedMeshNodes.reserve(m_surfaceMesh.nodes.size());
    bool hasMeshConnectivityErrors = false;

    for (const auto& mn : m_surfaceMesh.nodes) {
        if (mn.id <= 0 || !meshNodeMap.emplace(mn.id, &mn).second) {
            addError("ERR_MESH_NODE_ID",
                     "Nó de malha M" + std::to_string(mn.id) + " possui ID inválido ou duplicado.",
                     "SurfaceMesh", "M" + std::to_string(mn.id));
        }
        if (!isFiniteKVec3(mn.position) || !isFiniteKVec3(mn.nodalForce) || !isFiniteKVec3(mn.nodalMoment)) {
            hasMeshConnectivityErrors = true;
            addError("ERR_MESH_NODE_NONFINITE",
                     "Nó de malha M" + std::to_string(mn.id) + " possui coordenadas ou carregamentos não finitos.",
                     "SurfaceMesh", "M" + std::to_string(mn.id));
        } else {
            constexpr double kMaxPhysCoordM = 1e9;
            if (std::abs(mn.position.x()) > kMaxPhysCoordM || std::abs(mn.position.y()) > kMaxPhysCoordM ||
                std::abs(mn.position.z()) > kMaxPhysCoordM) {
                hasMeshConnectivityErrors = true;
                addError("ERR_MESH_NODE_COORD_RANGE",
                         "Nó de malha M" + std::to_string(mn.id) + " possui coordenadas fora dos limites físicos (|coord| <= 1e9 m).",
                         "SurfaceMesh", "M" + std::to_string(mn.id));
            }
        }
        for (int i = 0; i < 6; ++i) {
            if (!std::isfinite(mn.springStiffness[i]) || mn.springStiffness[i] < 0.0 ||
                !std::isfinite(mn.prescribedDisplacement[i])) {
                addError("ERR_MESH_SUPPORT_PARAMS",
                         "Nó de malha M" + std::to_string(mn.id) + " possui molas ou recalques inválidos.",
                         "BoundaryCondition", "M" + std::to_string(mn.id));
                break;
            }
        }
        if (mn.isRestrained()) {
            hasSupport = true;
        }
    }

    auto minNodesForCell = [](kernel::MeshCellType t) -> size_t {
        switch (t) {
            case kernel::MeshCellType::Line2: return 2;
            case kernel::MeshCellType::Tri3:  return 3;
            case kernel::MeshCellType::Quad4: return 4;
            case kernel::MeshCellType::Quad8: return 8;
            case kernel::MeshCellType::Tet4:  return 4;
            case kernel::MeshCellType::Tet10: return 10;
            case kernel::MeshCellType::Hex8:  return 8;
            case kernel::MeshCellType::Hex20: return 20;
        }
        return 3;
    };

    std::unordered_set<ElementId> meshElemIds;
    meshElemIds.reserve(m_surfaceMesh.elements.size());
    for (const auto& me : m_surfaceMesh.elements) {
        if (me.id <= 0 || !meshElemIds.insert(me.id).second) {
            addError("ERR_MESH_ELEM_ID",
                     "Elemento de malha E" + std::to_string(me.id) + " possui ID inválido ou duplicado.",
                     "SurfaceMesh", "E" + std::to_string(me.id));
        }
        const size_t reqN = minNodesForCell(me.type);
        if (me.nodeIds.size() < reqN) {
            hasMeshConnectivityErrors = true;
            addError("ERR_MESH_ELEM_NODES",
                     "Elemento de malha E" + std::to_string(me.id) + " tem menos nós do que o exigido para sua topologia.",
                     "SurfaceMesh", "E" + std::to_string(me.id));
        }
        for (NodeId nid : me.nodeIds) {
            if (meshNodeMap.find(nid) == meshNodeMap.end()) {
                hasMeshConnectivityErrors = true;
                addError("ERR_MESH_MISSING_NODE",
                         "Elemento de malha E" + std::to_string(me.id) + " referencia nó de malha inexistente (" + std::to_string(nid) + ").",
                         "SurfaceMesh", "E" + std::to_string(me.id));
            } else {
                connectedMeshNodes.insert(nid);
            }
        }
        if (!findMaterial(me.materialId)) {
            addError("ERR_MESH_MISSING_MATERIAL",
                     "Elemento de malha E" + std::to_string(me.id) + " referencia material inexistente (" + std::to_string(me.materialId) + ").",
                     "Material", "E" + std::to_string(me.id));
        }
        if (me.isShell() && (!std::isfinite(me.thickness) || me.thickness <= 1e-6)) {
            addError("ERR_SHELL_THICKNESS",
                     "Elemento de casca E" + std::to_string(me.id) + " possui espessura nula ou inválida.",
                     "SurfaceMesh", "E" + std::to_string(me.id));
        }
        if (!std::isfinite(me.normalPressure) || !std::isfinite(me.deltaT) || !isFiniteKVec3(me.distributedLoad)) {
            addError("ERR_MESH_NONFINITE_LOAD",
                     "Elemento de malha E" + std::to_string(me.id) + " possui carregamento distribuído/térmico não finito.",
                     "SurfaceMesh", "E" + std::to_string(me.id));
        }
    }

    if (hasMesh) {
        if (!hasMeshConnectivityErrors) {
            const auto qm = kernel::MeshGenerator::evaluateQuality(m_surfaceMesh);
            if (qm.invertedOrDegenerateCount > 0) {
                addError("ERR_MESH_DEGENERATE",
                         "A malha 2D/3D contém " + std::to_string(qm.invertedOrDegenerateCount) +
                         " elemento(s) invertido(s) ou degenerado(s) (Jacobiano mínimo = " +
                         std::to_string(qm.minJacobian) + ").",
                         "MeshQuality");
            } else if (qm.maxAspectRatio > 25.0 || qm.maxSkewness > 0.88) {
                addWarning("WARN_MESH_QUALITY",
                           "Malha 2D/3D com distorção elevada (razão de aspecto máx. = " +
                           std::to_string(qm.maxAspectRatio) + ", skewness máx. = " +
                           std::to_string(qm.maxSkewness) + ").",
                           "MeshQuality");
            }
        }
        for (const auto& mn : m_surfaceMesh.nodes) {
            if (connectedMeshNodes.find(mn.id) == connectedMeshNodes.end()) {
                addWarning("WARN_MESH_ISOLATED_NODE",
                           "Nó de malha M" + std::to_string(mn.id) + " não está conectado a nenhum elemento de malha.",
                           "SurfaceMesh", "M" + std::to_string(mn.id));
            }
        }
    }

    // Validação de restrições MPC (RigidLink e RigidDiaphragm) e conflito Dirichlet vs escravo MPC
    for (const auto& rl : m_rigidLinks) {
        const Node* mn = findNode(rl.masterNode);
        const Node* sn = findNode(rl.slaveNode);
        if (!mn || !sn) {
            addError("ERR_MPC_MISSING_NODE",
                     "Link rígido referencia nó mestre (" + std::to_string(rl.masterNode) +
                     ") ou escravo (" + std::to_string(rl.slaveNode) + ") inexistente.",
                     "MPC");
            continue;
        }
        if (rl.masterNode == rl.slaveNode) {
            addError("ERR_MPC_INVALID",
                     "Link rígido possui nó mestre e escravo idênticos (" + std::to_string(rl.masterNode) + ").",
                     "MPC", std::to_string(rl.masterNode));
            continue;
        }
        connectedNodes.insert(rl.masterNode);
        connectedNodes.insert(rl.slaveNode);
        const int maxDof = rl.coupleRotations ? 6 : 3;
        for (int d = 0; d < maxDof; ++d) {
            if (sn->support.fixed[d] || sn->support.hasPrescribed[d]) {
                addError("ERR_MPC_SUPPORT_CONFLICT",
                         "Nó escravo " + std::to_string(rl.slaveNode) +
                         " de link rígido possui apoio Dirichlet ou deslocamento prescrito conflitante no DOF " +
                         std::to_string(d) + ".",
                         "BoundaryCondition", std::to_string(rl.slaveNode));
                break;
            }
        }
    }

    for (const auto& rd : m_rigidDiaphragms) {
        if (rd.normalAxis < 0 || rd.normalAxis > 2) {
            addError("ERR_MPC_INVALID",
                     "Diafragma rígido possui eixo normal inválido (" + std::to_string(rd.normalAxis) + ").",
                     "MPC");
            continue;
        }
        const Node* mn = findNode(rd.masterNode);
        if (!mn) {
            addError("ERR_MPC_MISSING_NODE",
                     "Diafragma rígido referencia nó mestre inexistente (" + std::to_string(rd.masterNode) + ").",
                     "MPC", std::to_string(rd.masterNode));
            continue;
        }
        connectedNodes.insert(rd.masterNode);
        const int ax1 = (rd.normalAxis + 1) % 3;
        const int ax2 = (rd.normalAxis + 2) % 3;
        const int rAx = 3 + rd.normalAxis;
        for (NodeId sid : rd.slaveNodes) {
            if (sid == rd.masterNode) {
                addError("ERR_MPC_INVALID",
                         "Diafragma rígido inclui o próprio nó mestre (" + std::to_string(sid) + ") como escravo.",
                         "MPC", std::to_string(sid));
                continue;
            }
            const Node* sn = findNode(sid);
            if (!sn) {
                addError("ERR_MPC_MISSING_NODE",
                         "Diafragma rígido referencia nó escravo inexistente (" + std::to_string(sid) + ").",
                         "MPC", std::to_string(sid));
                continue;
            }
            connectedNodes.insert(sid);
            for (int d : {ax1, ax2, rAx}) {
                if (sn->support.fixed[d] || sn->support.hasPrescribed[d]) {
                    addError("ERR_MPC_SUPPORT_CONFLICT",
                             "Nó escravo " + std::to_string(sid) +
                             " de diafragma rígido possui apoio Dirichlet ou deslocamento prescrito conflitante no DOF " +
                             std::to_string(d) + ".",
                             "BoundaryCondition", std::to_string(sid));
                    break;
                }
            }
        }
    }

    if (!hasSupport) {
        addError("ERR_NO_SUPPORT",
                 "Nenhum apoio definido no modelo. A estrutura terá movimento de corpo rígido.",
                 "BoundaryCondition");
    }

    // Nós 1D isolados (tolerando nós que coincidem com nós ativos da malha 2D/3D acoplada)
    if (hasMesh) {
        SpatialPointHash activeMeshPointHash(1e-4);
        activeMeshPointHash.reserve(connectedMeshNodes.size());
        for (std::size_t idx = 0; idx < m_surfaceMesh.nodes.size(); ++idx) {
            const auto& mn = m_surfaceMesh.nodes[idx];
            if (connectedMeshNodes.find(mn.id) == connectedMeshNodes.end()) continue;
            activeMeshPointHash.insert(mn.position.x(), mn.position.y(), mn.position.z(), idx);
        }
        for (const auto& n : m_nodes) {
            if (connectedNodes.find(n.id) != connectedNodes.end()) continue;
            if (!activeMeshPointHash.findNearestWithinSq(n.position.x, n.position.y, n.position.z, 1e-10)) {
                addError("ERR_ISOLATED_NODE",
                         "Nó " + std::to_string(n.id) + " está isolado sem nenhuma barra ou elemento conectado.",
                         "Topology", std::to_string(n.id));
            }
        }
    } else {
        for (const auto& n : m_nodes) {
            if (connectedNodes.find(n.id) != connectedNodes.end()) continue;
            addError("ERR_ISOLATED_NODE",
                     "Nó " + std::to_string(n.id) + " está isolado sem nenhuma barra ou elemento conectado.",
                     "Topology", std::to_string(n.id));
        }
    }

    // Checagem de componentes desconectadas / subestruturas sem apoio
    if ((hasBars || hasMesh) && res.valid) {
        struct VNode {
            bool isSupported = false;
            bool isActive = false;
            std::vector<size_t> adj;
        };
        std::vector<VNode> vNodes;
        std::unordered_map<NodeId, size_t> barToV;
        std::unordered_map<NodeId, size_t> meshToV;

        auto quantKey = [](double x, double y, double z) -> std::string {
            auto safeQuant = [](double v) -> long long {
                if (!std::isfinite(v) || std::abs(v) > 1e9) return 0LL;
                return std::llround(v * 1e5);
            };
            return std::to_string(safeQuant(x)) + ":" +
                   std::to_string(safeQuant(y)) + ":" +
                   std::to_string(safeQuant(z));
        };
        std::unordered_map<std::string, size_t> posToV;

        for (const auto& n : m_nodes) {
            const std::string key = quantKey(n.position.x, n.position.y, n.position.z);
            auto it = posToV.find(key);
            size_t idx = 0;
            if (it == posToV.end()) {
                idx = vNodes.size();
                vNodes.push_back({n.isSupported(), connectedNodes.count(n.id) > 0, {}});
                posToV[key] = idx;
            } else {
                idx = it->second;
                vNodes[idx].isSupported = vNodes[idx].isSupported || n.isSupported();
                vNodes[idx].isActive = vNodes[idx].isActive || (connectedNodes.count(n.id) > 0);
            }
            barToV[n.id] = idx;
        }
        for (const auto& mn : m_surfaceMesh.nodes) {
            const std::string key = quantKey(mn.position.x(), mn.position.y(), mn.position.z());
            auto it = posToV.find(key);
            size_t idx = 0;
            if (it == posToV.end()) {
                idx = vNodes.size();
                vNodes.push_back({mn.isRestrained(), connectedMeshNodes.count(mn.id) > 0, {}});
                posToV[key] = idx;
            } else {
                idx = it->second;
                vNodes[idx].isSupported = vNodes[idx].isSupported || mn.isRestrained();
                vNodes[idx].isActive = vNodes[idx].isActive || (connectedMeshNodes.count(mn.id) > 0);
            }
            meshToV[mn.id] = idx;
        }

        auto linkV = [&](size_t a, size_t b) {
            if (a != b) {
                vNodes[a].adj.push_back(b);
                vNodes[b].adj.push_back(a);
            }
        };
        for (const auto& e : m_elements) {
            auto i1 = barToV.find(e.nodeStart);
            auto i2 = barToV.find(e.nodeEnd);
            if (i1 != barToV.end() && i2 != barToV.end()) linkV(i1->second, i2->second);
        }
        for (const auto& me : m_surfaceMesh.elements) {
            for (size_t k = 1; k < me.nodeIds.size(); ++k) {
                auto i0 = meshToV.find(me.nodeIds[0]);
                auto ik = meshToV.find(me.nodeIds[k]);
                if (i0 != meshToV.end() && ik != meshToV.end()) linkV(i0->second, ik->second);
            }
        }
        for (const auto& rl : m_rigidLinks) {
            auto im = barToV.find(rl.masterNode);
            auto is = barToV.find(rl.slaveNode);
            if (im != barToV.end() && is != barToV.end()) linkV(im->second, is->second);
        }
        for (const auto& rd : m_rigidDiaphragms) {
            auto im = barToV.find(rd.masterNode);
            if (im == barToV.end()) continue;
            for (NodeId sid : rd.slaveNodes) {
                auto is = barToV.find(sid);
                if (is != barToV.end()) linkV(im->second, is->second);
            }
        }

        std::vector<bool> vis(vNodes.size(), false);
        int activeComponents = 0;
        for (size_t i = 0; i < vNodes.size(); ++i) {
            if (vis[i] || !vNodes[i].isActive) continue;
            ++activeComponents;
            bool compSup = false;
            std::vector<size_t> q{i};
            vis[i] = true;
            for (size_t head = 0; head < q.size(); ++head) {
                size_t cur = q[head];
                compSup = compSup || vNodes[cur].isSupported;
                for (size_t nb : vNodes[cur].adj) {
                    if (!vis[nb]) {
                        vis[nb] = true;
                        q.push_back(nb);
                    }
                }
            }
            if (!compSup) {
                addError("ERR_UNRESTRAINED_COMPONENT",
                         "Subestrutura/componente desconectada #" + std::to_string(activeComponents) +
                         " não possui nenhum apoio (mecanismo de corpo rígido).",
                         "Topology");
            }
        }
        if (activeComponents > 1) {
            addWarning("WARN_DISCONNECTED_COMPONENTS",
                       "O modelo possui " + std::to_string(activeComponents) +
                       " componentes estruturais desconectadas entre si.",
                       "Topology");
        }
    }

    return res;
}

void StructuralModel::getBoundingBox(Vec3& minPt, Vec3& maxPt) const {
    if (m_nodes.empty() && m_surfaceMesh.nodes.empty()) {
        minPt = {-5.0, -5.0, 0.0};
        maxPt = {5.0, 5.0, 5.0};
        return;
    }

    minPt = {std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity()};
    maxPt = {-std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity()};

    auto expandPt = [&](const Vec3& p) {
        minPt.x = std::min(minPt.x, p.x);
        minPt.y = std::min(minPt.y, p.y);
        minPt.z = std::min(minPt.z, p.z);

        maxPt.x = std::max(maxPt.x, p.x);
        maxPt.y = std::max(maxPt.y, p.y);
        maxPt.z = std::max(maxPt.z, p.z);
    };

    for (const auto& n : m_nodes) {
        expandPt(n.position);
    }
    for (const auto& mn : m_surfaceMesh.nodes) {
        expandPt(Vec3{mn.position.x(), mn.position.y(), mn.position.z()});
    }

    // Garante volume mínimo
    double eps = 0.5;
    if (maxPt.x - minPt.x < eps) { minPt.x -= eps; maxPt.x += eps; }
    if (maxPt.y - minPt.y < eps) { minPt.y -= eps; maxPt.y += eps; }
    if (maxPt.z - minPt.z < eps) { minPt.z -= eps; maxPt.z += eps; }
}

void StructuralModel::populateStandardCatalog() {
    auto hasMaterialLike = [this](const std::string& name, const std::string& altSubstr = "") {
        for (const auto& m : m_materials) {
            if (m.name == name) return true;
            if (!altSubstr.empty() && m.name.find(altSubstr) != std::string::npos) return true;
        }
        return false;
    };

    auto ensureMaterial = [&](const Material& preset, const std::string& altSubstr = "") {
        if (!hasMaterialLike(preset.name, altSubstr)) {
            Material copy = preset;
            copy.id = INVALID_ID;
            addMaterial(copy);
        }
    };

    ensureMaterial(Material::makeConcreteC30(), "Concreto Estrutural C30");
    ensureMaterial(Material::makeConcreteC25());
    ensureMaterial(Material::makeConcreteC40());
    ensureMaterial(Material::makeStructuralSteel(), "ASTM A36");
    ensureMaterial(Material::makeStructuralSteelA572(), "A572");
    ensureMaterial(Material::makeAluminum6061(), "Alumínio");
    ensureMaterial(Material::makeTimber(), "Madeira");

    auto hasSectionLike = [this](const std::string& name, const std::string& altName = "") {
        for (const auto& s : m_sections) {
            if (s.name == name) return true;
            if (!altName.empty() && s.name == altName) return true;
        }
        return false;
    };

    auto ensureSection = [&](const CrossSection& preset, const std::string& altName = "") {
        if (!hasSectionLike(preset.name, altName)) {
            CrossSection copy = preset;
            copy.id = INVALID_ID;
            addCrossSection(copy);
        }
    };

    ensureSection(CrossSection::makeRectangular(0, "Pilar 30x30", 0.30, 0.30), "Pilar Concreto 30x30");
    ensureSection(CrossSection::makeRectangular(0, "Pilar 40x40", 0.40, 0.40));
    ensureSection(CrossSection::makeRectangular(0, "Viga 20x50", 0.20, 0.50), "Viga Concreto 20x50");
    ensureSection(CrossSection::makeRectangular(0, "Viga 25x60", 0.25, 0.60));
    ensureSection(CrossSection::makeCircularTube(0, "Tubo Circular D200 e10", 0.20, 0.01), "Tubo Aço D200 t10");
    ensureSection(CrossSection::makeCircularTube(0, "Tubo Treliça D100 e6", 0.10, 0.006));
    ensureSection(CrossSection::makeIShape(0, "Perfil I W200x35.9", 0.201, 0.165, 0.0102, 0.0062));
    ensureSection(CrossSection::makeIShape(0, "Perfil I W310x79", 0.306, 0.254, 0.0146, 0.0088));
    ensureSection(CrossSection::makeBoxRHS(0, "Caixão RHS 200x100x8", 0.20, 0.10, 0.008, 0.008));
    ensureSection(CrossSection::makeChannelC(0, "Perfil U C200x20", 0.20, 0.075, 0.010, 0.007));
    ensureSection(CrossSection::makeAngleL(0, "Cantoneira L 100x100x10", 0.10, 0.10, 0.010));
    ensureSection(CrossSection::makeTShape(0, "Perfil T 300x150x12x8", 0.30, 0.15, 0.012, 0.008));
    ensureSection(CrossSection::makeCircularSolid(0, "Circular Maciça D250", 0.25));
}

void StructuralModel::syncAutoCapturedViewportLoadCase() {
    auto captureInto = [this](kernel::LoadCase& lc) {
        lc.nodalLoads.clear();
        lc.elementLoads.clear();
        for (const auto& n : m_nodes) {
            if (n.load.isActive()) {
                kernel::NodalLoadPatternEntry e;
                e.nodeId = n.id;
                e.forceMoment = {n.load.force.x, n.load.force.y, n.load.force.z,
                                 n.load.moment.x, n.load.moment.y, n.load.moment.z};
                lc.nodalLoads.push_back(e);
            }
        }
        for (const auto& el : m_elements) {
            if (el.distLoad.active || el.thermalLoad.active) {
                kernel::ElementLoadPatternEntry e;
                e.elementId = el.id;
                e.load.hasDistributed = el.distLoad.active;
                e.load.coordSystem = (el.distLoad.coordSystem == CoordSystem::Local) ? 1 : 0;
                e.load.distributedForce = {el.distLoad.w.x, el.distLoad.w.y, el.distLoad.w.z};
                e.load.hasThermal = el.thermalLoad.active;
                e.load.deltaT = el.thermalLoad.deltaT;
                lc.elementLoads.push_back(e);
            }
        }
    };

    bool hasAnyViewportLoad = false;
    for (const auto& n : m_nodes) {
        if (n.load.isActive()) { hasAnyViewportLoad = true; break; }
    }
    if (!hasAnyViewportLoad) {
        for (const auto& el : m_elements) {
            if (el.distLoad.active || el.thermalLoad.active) { hasAnyViewportLoad = true; break; }
        }
    }

    for (auto& lc : m_loadCases) {
        if (lc.name == "G1 - Cargas do Modelo (Viewport)" ||
            (lc.name == "G1 - Peso Próprio (NBR 6118)" && hasAnyViewportLoad)) {
            captureInto(lc);
        }
    }
}

void StructuralModel::clear() {
    m_nodes.clear();
    m_elements.clear();
    m_selectedNodes.clear();
    m_selectedElements.clear();
    m_loadCases.clear();
    m_loadCombinations.clear();
    m_loadHistories.clear();
    m_rigidLinks.clear();
    m_rigidDiaphragms.clear();
    m_surfaceMesh = kernel::MeshContainer{};
    m_metadata = ProjectMetadata{};
    m_solverOptions = kernel::LinearSolverOptions{};
    m_preservedUnknownJson.clear();
    m_serializationWarnings.clear();
    invalidateAllIndices();
    ++m_geometryRevision;
    populateStandardCatalog();
    pushUndo();
}

void StructuralModel::clearAll() {
    m_nodes.clear();
    m_elements.clear();
    m_materials.clear();
    m_sections.clear();
    m_selectedNodes.clear();
    m_selectedElements.clear();
    m_loadCases.clear();
    m_loadCombinations.clear();
    m_loadHistories.clear();
    m_rigidLinks.clear();
    m_rigidDiaphragms.clear();
    m_surfaceMesh = kernel::MeshContainer{};
    m_metadata = ProjectMetadata{};
    m_solverOptions = kernel::LinearSolverOptions{};
    m_preservedUnknownJson.clear();
    m_serializationWarnings.clear();
    invalidateAllIndices();
    ++m_geometryRevision;
    clearUndoHistory();
}

void StructuralModel::clearResults() {
    for (auto& n : m_nodes) n.clearResults();
    for (auto& e : m_elements) e.clearResults();
    for (auto& mn : m_surfaceMesh.nodes) mn.clearResults();
    for (auto& me : m_surfaceMesh.elements) me.clearResults();
    ++m_geometryRevision;
}

void StructuralModel::clearUndoHistory() {
    std::vector<ModelState>().swap(m_history);
    m_undoIndex = 0;
}

namespace {

std::size_t estimateStateContainersBytes(
    const std::vector<Node>& nodes,
    const std::vector<Element>& elements,
    const std::vector<Material>& materials,
    const std::vector<CrossSection>& sections,
    const std::vector<kernel::LoadCase>& loadCases,
    const std::vector<kernel::LoadCombination>& loadCombinations,
    const std::vector<kernel::LoadHistory>& loadHistories,
    const std::vector<RigidLinkSpec>& rigidLinks,
    const std::vector<RigidDiaphragmSpec>& rigidDiaphragms,
    const kernel::MeshContainer& surfaceMesh) noexcept {
    std::size_t total = 0;
    total += nodes.capacity() * sizeof(Node);
    total += elements.capacity() * sizeof(Element);
    total += materials.capacity() * sizeof(Material);
    total += sections.capacity() * sizeof(CrossSection);
    total += loadCases.capacity() * sizeof(kernel::LoadCase);
    for (const auto& lc : loadCases) {
        total += lc.nodalLoads.capacity() * sizeof(kernel::NodalLoadPatternEntry);
        total += lc.elementLoads.capacity() * sizeof(kernel::ElementLoadPatternEntry);
    }
    total += loadCombinations.capacity() * sizeof(kernel::LoadCombination);
    for (const auto& comb : loadCombinations) {
        total += comb.factors.capacity() * sizeof(kernel::LoadCombinationFactor);
    }
    total += loadHistories.capacity() * sizeof(kernel::LoadHistory);
    for (const auto& lh : loadHistories) {
        total += lh.points.capacity() * sizeof(kernel::LoadHistoryPoint);
    }
    total += rigidLinks.capacity() * sizeof(RigidLinkSpec);
    total += rigidDiaphragms.capacity() * sizeof(RigidDiaphragmSpec);
    for (const auto& rd : rigidDiaphragms) {
        total += rd.slaveNodes.capacity() * sizeof(NodeId);
    }
    total += surfaceMesh.nodes.capacity() * sizeof(kernel::MeshNode);
    total += surfaceMesh.elements.capacity() * sizeof(kernel::MeshElement);
    for (const auto& me : surfaceMesh.elements) {
        total += me.nodeIds.capacity() * sizeof(kernel::NodeId);
    }
    return total;
}

} // namespace

std::size_t StructuralModel::estimatedHistoryMemoryBytes() const noexcept {
    std::size_t total = m_history.capacity() * sizeof(ModelState);
    for (const ModelState& st : m_history) {
        total += estimateStateContainersBytes(
            st.nodes,
            st.elements,
            st.materials,
            st.sections,
            st.loadCases,
            st.loadCombinations,
            st.loadHistories,
            st.rigidLinks,
            st.rigidDiaphragms,
            st.surfaceMesh);
    }
    return total;
}

std::size_t StructuralModel::estimatedMemoryBytes() const noexcept {
    std::size_t total = sizeof(StructuralModel);
    total += estimateStateContainersBytes(
        m_nodes,
        m_elements,
        m_materials,
        m_sections,
        m_loadCases,
        m_loadCombinations,
        m_loadHistories,
        m_rigidLinks,
        m_rigidDiaphragms,
        m_surfaceMesh);
    total += m_preservedUnknownJson.capacity();
    total += m_serializationWarnings.capacity() * sizeof(std::string);
    total += estimatedHistoryMemoryBytes();
    return total;
}

StructuralModel StructuralModel::cloneWithoutHistory() const {
    StructuralModel out;
    out.clearAll();
    out.m_nodes = m_nodes;
    out.m_elements = m_elements;
    out.m_materials = m_materials;
    out.m_sections = m_sections;
    out.m_selectedNodes = m_selectedNodes;
    out.m_selectedElements = m_selectedElements;
    out.m_loadCases = m_loadCases;
    out.m_loadCombinations = m_loadCombinations;
    out.m_loadHistories = m_loadHistories;
    out.m_rigidLinks = m_rigidLinks;
    out.m_rigidDiaphragms = m_rigidDiaphragms;
    out.m_solverOptions = m_solverOptions;
    out.m_metadata = m_metadata;
    out.m_surfaceMesh = m_surfaceMesh;
    out.m_preservedUnknownJson = m_preservedUnknownJson;
    out.m_serializationWarnings = m_serializationWarnings;
    out.m_geometryRevision = m_geometryRevision;
    out.m_batchUpdateDepth = 0;
    out.invalidateAllIndices();
    return out;
}

StructuralModel StructuralModel::createBuildingFrameExample() {
    StructuralModel model;
    model.clearAll();

    // Materiais e Seções (capturando os IDs atribuídos)
    Material matConc = Material::makeConcreteC30(1);
    matConc.name = "Concreto Estrutural C30";
    MaterialId matId = model.addMaterial(matConc);

    CrossSection secCol = CrossSection::makeRectangular(1, "Pilar 30x30", 0.30, 0.30);
    CrossSection secBeam = CrossSection::makeRectangular(2, "Viga 20x50", 0.20, 0.50);
    SectionId secColId = model.addCrossSection(secCol);
    SectionId secBeamId = model.addCrossSection(secBeam);

    // Nós: Pavimento 0 (Base, Z=0)
    Node n1(1, 0.0, 0.0, 0.0); n1.support = SupportRestraint::makeFixed();
    Node n2(2, 6.0, 0.0, 0.0); n2.support = SupportRestraint::makeFixed();
    Node n3(3, 6.0, 5.0, 0.0); n3.support = SupportRestraint::makeFixed();
    Node n4(4, 0.0, 5.0, 0.0); n4.support = SupportRestraint::makeFixed();
    model.addNode(n1); model.addNode(n2); model.addNode(n3); model.addNode(n4);

    // Nós: Pavimento 1 (Z=3.0)
    Node n5(5, 0.0, 0.0, 3.0);
    Node n6(6, 6.0, 0.0, 3.0);
    Node n7(7, 6.0, 5.0, 3.0);
    Node n8(8, 0.0, 5.0, 3.0);
    // Vento lateral atuando no 1º pavimento
    n5.load.force.x = 20.0; // 20 kN
    n8.load.force.x = 20.0;
    model.addNode(n5); model.addNode(n6); model.addNode(n7); model.addNode(n8);

    // Nós: Pavimento 2 / Cobertura (Z=6.0)
    Node n9(9, 0.0, 0.0, 6.0);
    Node n10(10, 6.0, 0.0, 6.0);
    Node n11(11, 6.0, 5.0, 6.0);
    Node n12(12, 0.0, 5.0, 6.0);
    // Vento lateral na cobertura
    n9.load.force.x = 30.0; // 30 kN
    n12.load.force.x = 30.0;
    model.addNode(n9); model.addNode(n10); model.addNode(n11); model.addNode(n12);

    // Pilares do 1º Pavimento
    model.addElement(1, 5, matId, secColId);
    model.addElement(2, 6, matId, secColId);
    model.addElement(3, 7, matId, secColId);
    model.addElement(4, 8, matId, secColId);

    // Pilares do 2º Pavimento
    model.addElement(5, 9, matId, secColId);
    model.addElement(6, 10, matId, secColId);
    model.addElement(7, 11, matId, secColId);
    model.addElement(8, 12, matId, secColId);

    // Vigas do 1º Pavimento com carga distribuída vertical qZ = -15 kN/m
    DistributedLoad qBeam;
    qBeam.active = true;
    qBeam.coordSystem = CoordSystem::Global;
    qBeam.w = {0.0, 0.0, -15.0};

    Element eBeam1(9, 5, 6, matId, secBeamId); eBeam1.distLoad = qBeam; model.addElement(eBeam1);
    Element eBeam2(10, 6, 7, matId, secBeamId); eBeam2.distLoad = qBeam; model.addElement(eBeam2);
    Element eBeam3(11, 7, 8, matId, secBeamId); eBeam3.distLoad = qBeam; model.addElement(eBeam3);
    Element eBeam4(12, 8, 5, matId, secBeamId); eBeam4.distLoad = qBeam; model.addElement(eBeam4);

    // Vigas da Cobertura com carga qZ = -10 kN/m
    DistributedLoad qRoof;
    qRoof.active = true;
    qRoof.coordSystem = CoordSystem::Global;
    qRoof.w = {0.0, 0.0, -10.0};

    Element eBeam5(13, 9, 10, matId, secBeamId); eBeam5.distLoad = qRoof; model.addElement(eBeam5);
    Element eBeam6(14, 10, 11, matId, secBeamId); eBeam6.distLoad = qRoof; model.addElement(eBeam6);
    Element eBeam7(15, 11, 12, matId, secBeamId); eBeam7.distLoad = qRoof; model.addElement(eBeam7);
    Element eBeam8(16, 12, 9, matId, secBeamId); eBeam8.distLoad = qRoof; model.addElement(eBeam8);

    model.populateStandardCatalog();
    model.clearUndoHistory();
    model.pushUndo();
    return model;
}

StructuralModel StructuralModel::createCantileverLFrameExample() {
    StructuralModel model;
    model.clearAll();

    Material matSteel = Material::makeStructuralSteel(1);
    MaterialId matId = model.addMaterial(matSteel);

    CrossSection secTube = CrossSection::makeCircularTube(1, "Tubo Circular D200 e10", 0.20, 0.01);
    SectionId secId = model.addCrossSection(secTube);

    // Nó 1: Engaste perfeito na origem (0, 0, 0)
    Node n1(1, 0.0, 0.0, 0.0);
    n1.support = SupportRestraint::makeFixed();
    model.addNode(n1);

    // Nó 2: Vértice em (4.0, 0.0, 0.0)
    Node n2(2, 4.0, 0.0, 0.0);
    model.addNode(n2);

    // Nó 3: Extremidade livre do balanço em (4.0, 3.0, 0.0)
    Node n3(3, 4.0, 3.0, 0.0);
    // Carga vertical para baixo e força lateral que induzem flexão e torção pura acoplada
    n3.load.force = {0.0, 5.0, -15.0};
    model.addNode(n3);

    // Barra 1: ao longo do eixo X (comprimento 4.0 m)
    model.addElement(1, 2, matId, secId);

    // Barra 2: ao longo do eixo Y (comprimento 3.0 m)
    model.addElement(2, 3, matId, secId);

    model.populateStandardCatalog();
    model.clearUndoHistory();
    model.pushUndo();
    return model;
}

StructuralModel StructuralModel::createSpaceTrussExample() {
    StructuralModel model;
    model.clearAll();

    Material matSteel = Material::makeStructuralSteel(1);
    MaterialId matId = model.addMaterial(matSteel);

    CrossSection secTruss = CrossSection::makeCircularTube(1, "Tubo Treliça D100 e6", 0.10, 0.006);
    SectionId secId = model.addCrossSection(secTruss);

    // Passarela Treliçada Espacial: 4 vãos de 2.5 m (L = 10 m), largura 2.0 m, altura 2.0 m
    int numBays = 4;
    double bayLen = 2.5;
    double width = 2.0;
    double height = 2.0;

    // Nós Inferiores: Esquerdo (y=0) e Direito (y=width)
    for (int i = 0; i <= numBays; ++i) {
        double x = i * bayLen;
        Node nl(i * 2 + 1, x, 0.0, 0.0);
        Node nr(i * 2 + 2, x, width, 0.0);

        // Apoios nas extremidades garantindo estabilidade isostática 3D sem sobre-restringir expansão em Y
        if (i == 0) {
            nl.support = SupportRestraint::makePinned(); // Restringe uX, uY, uZ
            nr.support = SupportRestraint::makeRoller(2); // Apoio em X e Z (livre em Y)
            nr.support.fixed[0] = true;
        } else if (i == numBays) {
            nl.support = SupportRestraint::makeRoller(2); // Apoio móvel vertical (restringe uZ)
            nr.support = SupportRestraint::makeRoller(2); // Apoio móvel vertical (restringe uZ)
        }

        // Carga vertical de utilização nos nós intermediários do piso
        if (i > 0 && i < numBays) {
            nl.load.force.z = -20.0;
            nr.load.force.z = -20.0;
        }

        model.addNode(nl);
        model.addNode(nr);
    }

    // Nós Superiores: Z = height
    int offsetTop = (numBays + 1) * 2;
    for (int i = 0; i <= numBays; ++i) {
        double x = i * bayLen;
        Node nl(offsetTop + i * 2 + 1, x, 0.0, height);
        Node nr(offsetTop + i * 2 + 2, x, width, height);
        model.addNode(nl);
        model.addNode(nr);
    }

    ElementReleases trussRel = ElementReleases::makeTrussReleases();

    // Barras dos Banzos, Diagonais Laterais, Contraventamento em Planta (XY) e Pórticos Transversais (YZ)
    for (int i = 0; i < numBays; ++i) {
        int bL1 = i * 2 + 1;
        int bR1 = i * 2 + 2;
        int bL2 = (i + 1) * 2 + 1;
        int bR2 = (i + 1) * 2 + 2;

        int tL1 = offsetTop + bL1;
        int tR1 = offsetTop + bR1;
        int tL2 = offsetTop + bL2;
        int tR2 = offsetTop + bR2;

        // Banzos inferiores longitudinais
        Element e1(model.getNextElementId(), bL1, bL2, matId, secId); e1.releases = trussRel; model.addElement(e1);
        Element e2(model.getNextElementId(), bR1, bR2, matId, secId); e2.releases = trussRel; model.addElement(e2);

        // Banzos superiores longitudinais
        Element e3(model.getNextElementId(), tL1, tL2, matId, secId); e3.releases = trussRel; model.addElement(e3);
        Element e4(model.getNextElementId(), tR1, tR2, matId, secId); e4.releases = trussRel; model.addElement(e4);

        // Montantes verticais
        Element eVL(model.getNextElementId(), bL1, tL1, matId, secId); eVL.releases = trussRel; model.addElement(eVL);
        Element eVR(model.getNextElementId(), bR1, tR1, matId, secId); eVR.releases = trussRel; model.addElement(eVR);

        // Transversinas inferiores e superiores
        Element eTI(model.getNextElementId(), bL1, bR1, matId, secId); eTI.releases = trussRel; model.addElement(eTI);
        Element eTS(model.getNextElementId(), tL1, tR1, matId, secId); eTS.releases = trussRel; model.addElement(eTS);

        // Diagonal transversal YZ no plano do pórtico
        Element eDiagYZ(model.getNextElementId(), bL1, tR1, matId, secId); eDiagYZ.releases = trussRel; model.addElement(eDiagYZ);

        // Diagonais laterais XZ (Warren)
        Element eDiagL(model.getNextElementId(), bL1, tL2, matId, secId); eDiagL.releases = trussRel; model.addElement(eDiagL);
        Element eDiagR(model.getNextElementId(), bR1, tR2, matId, secId); eDiagR.releases = trussRel; model.addElement(eDiagR);

        // Diagonais de contraventamento horizontal em planta (XY) inferior e superior
        Element ePlanB(model.getNextElementId(), bL1, bR2, matId, secId); ePlanB.releases = trussRel; model.addElement(ePlanB);
        Element ePlanT(model.getNextElementId(), tL1, tR2, matId, secId); ePlanT.releases = trussRel; model.addElement(ePlanT);
    }

    // Fechamento da última seção transversal
    int lastBL = numBays * 2 + 1;
    int lastBR = numBays * 2 + 2;
    int lastTL = offsetTop + lastBL;
    int lastTR = offsetTop + lastBR;

    Element eVLend(model.getNextElementId(), lastBL, lastTL, matId, secId); eVLend.releases = trussRel; model.addElement(eVLend);
    Element eVRend(model.getNextElementId(), lastBR, lastTR, matId, secId); eVRend.releases = trussRel; model.addElement(eVRend);
    Element eTIend(model.getNextElementId(), lastBL, lastBR, matId, secId); eTIend.releases = trussRel; model.addElement(eTIend);
    Element eTSend(model.getNextElementId(), lastTL, lastTR, matId, secId); eTSend.releases = trussRel; model.addElement(eTSend);
    Element eDiagYZend(model.getNextElementId(), lastBL, lastTR, matId, secId); eDiagYZend.releases = trussRel; model.addElement(eDiagYZend);

    model.populateStandardCatalog();
    model.clearUndoHistory();
    model.pushUndo();
    return model;
}

StructuralModel::ModelState StructuralModel::captureState() const {
    return ModelState{
        m_nodes,
        m_elements,
        m_materials,
        m_sections,
        m_loadCases,
        m_loadCombinations,
        m_loadHistories,
        m_rigidLinks,
        m_rigidDiaphragms,
        m_metadata,
        m_solverOptions,
        m_surfaceMesh
    };
}

void StructuralModel::restoreState(const ModelState& state) {
    m_nodes = state.nodes;
    m_elements = state.elements;
    m_materials = state.materials;
    m_sections = state.sections;
    m_loadCases = state.loadCases;
    m_loadCombinations = state.loadCombinations;
    m_loadHistories = state.loadHistories;
    m_rigidLinks = state.rigidLinks;
    m_rigidDiaphragms = state.rigidDiaphragms;
    m_metadata = state.metadata;
    m_solverOptions = state.solverOptions;
    m_surfaceMesh = state.surfaceMesh;
    m_selectedNodes.clear();
    m_selectedElements.clear();
    invalidateAllIndices();
    ++m_geometryRevision;
}

void StructuralModel::beginBatchUpdate() {
    ++m_batchUpdateDepth;
}

void StructuralModel::endBatchUpdate(bool pushUndoAtEnd) {
    if (m_batchUpdateDepth > 0) {
        --m_batchUpdateDepth;
    }
    if (m_batchUpdateDepth == 0) {
        ++m_geometryRevision;
        if (pushUndoAtEnd) {
            pushUndo();
        }
    }
}

void StructuralModel::pushUndo() {
    if (m_batchUpdateDepth > 0) {
        return;
    }
    ++m_geometryRevision;
    if (m_undoIndex < m_history.size()) {
        m_history.erase(m_history.begin() + static_cast<ptrdiff_t>(m_undoIndex + 1), m_history.end());
    }
    m_history.push_back(captureState());
    if (m_history.size() > 50) {
        m_history.erase(m_history.begin());
    }
    m_undoIndex = m_history.size() - 1;
}

bool StructuralModel::undo() {
    if (!canUndo()) return false;
    --m_undoIndex;
    restoreState(m_history[m_undoIndex]);
    return true;
}

bool StructuralModel::redo() {
    if (!canRedo()) return false;
    ++m_undoIndex;
    restoreState(m_history[m_undoIndex]);
    return true;
}

kernel::LoadCaseId StructuralModel::getNextLoadCaseId() const {
    kernel::LoadCaseId maxId = 0;
    for (const auto& lc : m_loadCases) {
        if (lc.id > maxId) maxId = lc.id;
    }
    return maxId + 1;
}

kernel::LoadComboId StructuralModel::getNextLoadCombinationId() const {
    kernel::LoadComboId maxId = 0;
    for (const auto& comb : m_loadCombinations) {
        if (comb.id > maxId) maxId = comb.id;
    }
    return maxId + 1;
}

kernel::LoadCaseId StructuralModel::addLoadCase(const kernel::LoadCase& lc) {
    kernel::LoadCase copy = lc;
    if (copy.id <= 0) copy.id = getNextLoadCaseId();
    for (auto& existing : m_loadCases) {
        if (existing.id == copy.id) {
            existing = copy;
            return copy.id;
        }
    }
    m_loadCases.push_back(std::move(copy));
    return m_loadCases.back().id;
}

kernel::LoadComboId StructuralModel::addLoadCombination(const kernel::LoadCombination& comb) {
    kernel::LoadCombination copy = comb;
    if (copy.id <= 0) copy.id = getNextLoadCombinationId();
    for (auto& existing : m_loadCombinations) {
        if (existing.id == copy.id) {
            existing = copy;
            return copy.id;
        }
    }
    m_loadCombinations.push_back(std::move(copy));
    return m_loadCombinations.back().id;
}

kernel::LoadCase* StructuralModel::findLoadCase(kernel::LoadCaseId id) {
    for (auto& lc : m_loadCases) {
        if (lc.id == id) return &lc;
    }
    return nullptr;
}

const kernel::LoadCase* StructuralModel::findLoadCase(kernel::LoadCaseId id) const {
    for (const auto& lc : m_loadCases) {
        if (lc.id == id) return &lc;
    }
    return nullptr;
}

kernel::LoadCombination* StructuralModel::findLoadCombination(kernel::LoadComboId id) {
    for (auto& comb : m_loadCombinations) {
        if (comb.id == id) return &comb;
    }
    return nullptr;
}

const kernel::LoadCombination* StructuralModel::findLoadCombination(kernel::LoadComboId id) const {
    for (const auto& comb : m_loadCombinations) {
        if (comb.id == id) return &comb;
    }
    return nullptr;
}

} // namespace omniframe

