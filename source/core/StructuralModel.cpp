#include "StructuralModel.hpp"
#include <algorithm>
#include <limits>

namespace omniframe {

StructuralModel::StructuralModel() {
    // Inicializa materiais e seções padrão
    addMaterial(Material::makeStructuralSteel(1));
    addMaterial(Material::makeConcreteC30(2));

    addCrossSection(CrossSection::makeRectangular(1, "Viga Concreto 20x50", 0.20, 0.50));
    addCrossSection(CrossSection::makeRectangular(2, "Pilar Concreto 30x30", 0.30, 0.30));
    addCrossSection(CrossSection::makeCircularTube(3, "Tubo Aço D200 t10", 0.20, 0.01));
    addCrossSection(CrossSection::makeIShape(4, "Perfil I W200x35.9", 0.201, 0.165, 0.0102, 0.0062));

    pushUndo();
}

NodeId StructuralModel::getNextNodeId() const {
    int maxId = 0;
    for (const auto& n : m_nodes) {
        if (n.id > maxId) maxId = n.id;
    }
    return maxId + 1;
}

ElementId StructuralModel::getNextElementId() const {
    int maxId = 0;
    for (const auto& e : m_elements) {
        if (e.id > maxId) maxId = e.id;
    }
    return maxId + 1;
}

MaterialId StructuralModel::getNextMaterialId() const {
    int maxId = 0;
    for (const auto& m : m_materials) {
        if (m.id > maxId) maxId = m.id;
    }
    return maxId + 1;
}

SectionId StructuralModel::getNextSectionId() const {
    int maxId = 0;
    for (const auto& s : m_sections) {
        if (s.id > maxId) maxId = s.id;
    }
    return maxId + 1;
}

NodeId StructuralModel::addNode(double x, double y, double z) {
    NodeId id = getNextNodeId();
    m_nodes.emplace_back(id, x, y, z);
    pushUndo();
    return id;
}

NodeId StructuralModel::addNode(const Node& node) {
    Node n = node;
    if (n.id <= 0 || findNode(n.id) != nullptr) {
        n.id = getNextNodeId();
    }
    m_nodes.push_back(n);
    pushUndo();
    return n.id;
}

bool StructuralModel::removeNode(NodeId id) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [id](const Node& n) { return n.id == id; });
    if (it == m_nodes.end()) return false;

    // Remove elementos conectados ao nó
    std::erase_if(m_elements, [id](const Element& e) {
        return e.nodeStart == id || e.nodeEnd == id;
    });

    m_selectedNodes.erase(id);
    m_nodes.erase(it);
    pushUndo();
    return true;
}

ElementId StructuralModel::addElement(NodeId start, NodeId end, MaterialId mid, SectionId sid, double roll) {
    if (start == end) return INVALID_ID;
    if (!findNode(start) || !findNode(end)) return INVALID_ID;

    ElementId id = getNextElementId();
    m_elements.emplace_back(id, start, end, mid, sid, roll);
    pushUndo();
    return id;
}

ElementId StructuralModel::addElement(const Element& elem) {
    Element e = elem;
    if (e.id <= 0 || findElement(e.id) != nullptr) {
        e.id = getNextElementId();
    }
    m_elements.push_back(e);
    pushUndo();
    return e.id;
}

bool StructuralModel::removeElement(ElementId id) {
    auto it = std::find_if(m_elements.begin(), m_elements.end(), [id](const Element& e) { return e.id == id; });
    if (it == m_elements.end()) return false;

    m_selectedElements.erase(id);
    m_elements.erase(it);
    pushUndo();
    return true;
}

MaterialId StructuralModel::addMaterial(const Material& mat) {
    Material m = mat;
    if (m.id <= 0 || findMaterial(m.id) != nullptr) {
        m.id = getNextMaterialId();
    }
    m_materials.push_back(m);
    return m.id;
}

bool StructuralModel::removeMaterial(MaterialId id) {
    auto it = std::find_if(m_materials.begin(), m_materials.end(), [id](const Material& m) { return m.id == id; });
    if (it == m_materials.end()) return false;
    m_materials.erase(it);
    return true;
}

SectionId StructuralModel::addCrossSection(const CrossSection& sec) {
    CrossSection s = sec;
    if (s.id <= 0 || findCrossSection(s.id) != nullptr) {
        s.id = getNextSectionId();
    }
    m_sections.push_back(s);
    return s.id;
}

bool StructuralModel::removeCrossSection(SectionId id) {
    auto it = std::find_if(m_sections.begin(), m_sections.end(), [id](const CrossSection& s) { return s.id == id; });
    if (it == m_sections.end()) return false;
    m_sections.erase(it);
    return true;
}

Node* StructuralModel::findNode(NodeId id) {
    for (auto& n : m_nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const Node* StructuralModel::findNode(NodeId id) const {
    for (const auto& n : m_nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

Element* StructuralModel::findElement(ElementId id) {
    for (auto& e : m_elements) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const Element* StructuralModel::findElement(ElementId id) const {
    for (const auto& e : m_elements) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

Material* StructuralModel::findMaterial(MaterialId id) {
    for (auto& m : m_materials) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

const Material* StructuralModel::findMaterial(MaterialId id) const {
    for (const auto& m : m_materials) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

CrossSection* StructuralModel::findCrossSection(SectionId id) {
    for (auto& s : m_sections) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const CrossSection* StructuralModel::findCrossSection(SectionId id) const {
    for (const auto& s : m_sections) {
        if (s.id == id) return &s;
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
}

void StructuralModel::selectElement(ElementId id, bool clearOthers) {
    if (clearOthers) {
        m_selectedNodes.clear();
        m_selectedElements.clear();
    }
    if (findElement(id)) {
        m_selectedElements.insert(id);
    }
}

void StructuralModel::toggleNodeSelection(NodeId id) {
    if (m_selectedNodes.contains(id)) {
        m_selectedNodes.erase(id);
    } else if (findNode(id)) {
        m_selectedNodes.insert(id);
    }
}

void StructuralModel::toggleElementSelection(ElementId id) {
    if (m_selectedElements.contains(id)) {
        m_selectedElements.erase(id);
    } else if (findElement(id)) {
        m_selectedElements.insert(id);
    }
}

void StructuralModel::clearSelection() {
    m_selectedNodes.clear();
    m_selectedElements.clear();
}

void StructuralModel::deleteSelected() {
    if (m_selectedNodes.empty() && m_selectedElements.empty()) return;

    for (ElementId eid : m_selectedElements) {
        auto it = std::find_if(m_elements.begin(), m_elements.end(), [eid](const Element& e) { return e.id == eid; });
        if (it != m_elements.end()) {
            m_elements.erase(it);
        }
    }
    m_selectedElements.clear();

    for (NodeId nid : m_selectedNodes) {
        std::erase_if(m_elements, [nid](const Element& e) {
            return e.nodeStart == nid || e.nodeEnd == nid;
        });
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nid](const Node& n) { return n.id == nid; });
        if (it != m_nodes.end()) {
            m_nodes.erase(it);
        }
    }
    m_selectedNodes.clear();
    pushUndo();
}

ModelValidation StructuralModel::validate() const {
    ModelValidation res;

    if (m_nodes.size() < 2) {
        res.valid = false;
        res.errors.push_back("O modelo precisa ter pelo menos 2 nós.");
    }

    if (m_elements.empty()) {
        res.valid = false;
        res.errors.push_back("O modelo precisa ter pelo menos 1 barra.");
    }

    bool hasSupport = false;
    for (const auto& n : m_nodes) {
        if (n.isSupported()) {
            hasSupport = true;
            break;
        }
    }
    if (!hasSupport) {
        res.valid = false;
        res.errors.push_back("Nenhum apoio definido no modelo. A estrutura terá movimento de corpo rígido.");
    }

    std::unordered_set<NodeId> connectedNodes;
    connectedNodes.reserve(m_elements.size() * 2);

    for (const auto& e : m_elements) {
        const Node* n1 = findNode(e.nodeStart);
        const Node* n2 = findNode(e.nodeEnd);
        if (!n1 || !n2) {
            res.valid = false;
            res.errors.push_back("Barra " + std::to_string(e.id) + " conecta a nó inexistente.");
            continue;
        }
        connectedNodes.insert(e.nodeStart);
        connectedNodes.insert(e.nodeEnd);
        double len = e.computeLength(*this);
        if (len < 1e-4) {
            res.valid = false;
            res.errors.push_back("Barra " + std::to_string(e.id) + " tem comprimento nulo.");
        }
        if (!findMaterial(e.materialId)) {
            res.warnings.push_back("Barra " + std::to_string(e.id) + " está sem material válido atribuído.");
        }
        if (!findCrossSection(e.sectionId)) {
            res.warnings.push_back("Barra " + std::to_string(e.id) + " está sem seção válida atribuída.");
        }
    }

    for (const auto& n : m_nodes) {
        if (connectedNodes.find(n.id) == connectedNodes.end()) {
            res.valid = false;
            res.errors.push_back("Nó " + std::to_string(n.id) + " está isolado sem nenhuma barra conectada.");
        }
    }

    return res;
}

void StructuralModel::getBoundingBox(Vec3& minPt, Vec3& maxPt) const {
    if (m_nodes.empty()) {
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

    for (const auto& n : m_nodes) {
        minPt.x = std::min(minPt.x, n.x());
        minPt.y = std::min(minPt.y, n.y());
        minPt.z = std::min(minPt.z, n.z());

        maxPt.x = std::max(maxPt.x, n.x());
        maxPt.y = std::max(maxPt.y, n.y());
        maxPt.z = std::max(maxPt.z, n.z());
    }

    // Garante volume mínimo
    double eps = 0.5;
    if (maxPt.x - minPt.x < eps) { minPt.x -= eps; maxPt.x += eps; }
    if (maxPt.y - minPt.y < eps) { minPt.y -= eps; maxPt.y += eps; }
    if (maxPt.z - minPt.z < eps) { minPt.z -= eps; maxPt.z += eps; }
}

void StructuralModel::clear() {
    m_nodes.clear();
    m_elements.clear();
    m_selectedNodes.clear();
    m_selectedElements.clear();
    pushUndo();
}

void StructuralModel::clearAll() {
    m_nodes.clear();
    m_elements.clear();
    m_materials.clear();
    m_sections.clear();
    m_selectedNodes.clear();
    m_selectedElements.clear();
    clearUndoHistory();
}

void StructuralModel::clearResults() {
    for (auto& n : m_nodes) n.clearResults();
    for (auto& e : m_elements) e.clearResults();
}

void StructuralModel::clearUndoHistory() {
    m_history.clear();
    m_undoIndex = 0;
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

    model.clearUndoHistory();
    model.pushUndo();
    return model;
}

StructuralModel::ModelState StructuralModel::captureState() const {
    return ModelState{m_nodes, m_elements, m_materials, m_sections};
}

void StructuralModel::restoreState(const ModelState& state) {
    m_nodes = state.nodes;
    m_elements = state.elements;
    m_materials = state.materials;
    m_sections = state.sections;
    m_selectedNodes.clear();
    m_selectedElements.clear();
}

void StructuralModel::pushUndo() {
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

} // namespace omniframe

