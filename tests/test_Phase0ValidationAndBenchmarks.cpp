#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include "io/ProjectSerializer.hpp"
#include "kernel/mesh/MeshGenerator.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
#include <limits>

using namespace omniframe;

namespace {

void testFakeReleaseStiffnessRemoved() {
    StructuralModel model;
    model.clear();

    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(4.0, 0.0, 0.0);

    model.findNode(n1)->support.fixed = {true, true, true, false, false, false};
    model.findNode(n2)->support.fixed[2] = true; // vertical roller

    ElementId eId = model.addElement(n1, n2, 1, 1);
    Element* elem = model.findElement(eId);
    TEST_ASSERT(elem != nullptr, "Elemento valido");
    elem->releases.start = {false, false, false, true, true, true};
    elem->releases.end   = {false, false, false, true, true, true};

    Eigen::Matrix<double, 12, 12> kLoc = elem->computeLocalStiffnessMatrix(model);
    for (int idx : {3, 4, 5, 9, 10, 11}) {
        TEST_ASSERT(kLoc(idx, idx) == 0.0, "Rigidez diagonal residual artificial 1e-9 deve ser zero");
    }

    // Momento aplicado em nó totalmente rotulado deve acusar mecanismo
    model.findNode(n2)->load.moment.y = 5.0;
    SolverResult failRes = FemSolver3D::solve(model);
    TEST_ASSERT(!failRes.success, "Momento aplicado em no rotulado sem rigidez rotacional deve falhar como mecanismo");

    // Treliça espacial intacta deve continuar convergindo perfeitamente sem rigidez artificial
    StructuralModel truss = StructuralModel::createSpaceTrussExample();
    SolverResult trussRes = FemSolver3D::solve(truss);
    TEST_ASSERT(trussRes.success && trussRes.equilibrium.isBalanced, "Trelica espacial 3D deve resolver com DOFs rotacionais inativos");
}

void testZeroLengthAndFiniteValidation() {
    StructuralModel model;
    model.clear();

    NodeId n1 = model.addNode(1.0, 2.0, 3.0);
    NodeId n2 = model.addNode(1.0, 2.0, 3.0); // coincidente
    model.findNode(n1)->support.fixed = {true, true, true, true, true, true};
    ElementId eId = model.addElement(n1, n2, 1, 1);

    const Element* elem = model.findElement(eId);
    TEST_ASSERT(elem->computeLength(model) == 0.0, "Comprimento nulo nao deve sofrer clamping silencioso para 1e-6");
    TEST_ASSERT(elem->computeUncondensedLocalStiffnessMatrix(model).allFinite(), "Matriz local com L=0 deve ser finita (zeros)");
    TEST_ASSERT(!model.validate().valid, "Comprimento nulo deve invalidar modelo");

    // Teste NaN e material inexistente
    model.findNode(n2)->position = {5.0, 2.0, 3.0};
    TEST_ASSERT(model.validate().valid, "Modelo corrigido deve validar");

    model.findElement(eId)->materialId = 9999;
    TEST_ASSERT(!model.validate().valid, "Material inexistente deve gerar erro de validacao");
    model.findElement(eId)->materialId = 1;

    model.findNode(n2)->position.x = std::numeric_limits<double>::quiet_NaN();
    TEST_ASSERT(!model.validate().valid, "Coordenada NaN deve invalidar modelo");
}

void testSerializerTransactionalAndStrict() {
    StructuralModel original = StructuralModel::createCantileverLFrameExample();
    const size_t origNodeCount = original.getNodes().size();
    const size_t origElemCount = original.getElements().size();

    QTemporaryDir tmpDir;
    TEST_ASSERT(tmpDir.isValid(), "Diretorio temporario valido");

    // JSON corrompido ou com E negativo não deve corromper modelo atual
    const QString badJsonPath = tmpDir.filePath("bad_neg_E.omniframe");
    {
        QFile f(badJsonPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Abrir arquivo bad_neg_E");
        f.write(R"({"version":"1.0.0","materials":[{"id":1,"name":"X","E":-10,"nu":0.3}],"sections":[],"nodes":[],"elements":[]})");
        f.close();
    }
    TEST_ASSERT(!ProjectSerializer::loadFromFile(badJsonPath, original), "Carga com E negativo deve falhar");
    TEST_ASSERT(original.getNodes().size() == origNodeCount && original.getElements().size() == origElemCount,
                "Falha na leitura deve preservar modelo anterior intacto");

    // JSON com string no lugar de número deve falhar
    const QString nanJsonPath = tmpDir.filePath("bad_str.omniframe");
    {
        QFile f(nanJsonPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Abrir arquivo bad_str");
        f.write(R"({"version":"1.0.0","nodes":[{"id":1,"x":"NaN","y":0,"z":0}]})");
        f.close();
    }
    TEST_ASSERT(!ProjectSerializer::loadFromFile(nanJsonPath, original), "Coordenada nao numerica deve falhar");
    TEST_ASSERT(original.getNodes().size() == origNodeCount, "Falha de tipo nao deve apagar modelo anterior");

    // Roundtrip atômico válido
    const QString goodPath = tmpDir.filePath("good_model.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(goodPath, original), "Save atomico deve retornar true");

    StructuralModel reloaded;
    TEST_ASSERT(ProjectSerializer::loadFromFile(goodPath, reloaded), "Reload de arquivo valido deve ter sucesso");

    SolverResult r1 = FemSolver3D::solve(original);
    SolverResult r2 = FemSolver3D::solve(reloaded);
    TEST_ASSERT(r1.success && r2.success, "Ambos modelos original e recarregado devem convergir");
    TEST_ASSERT(std::abs(r1.maxNodalDisplacement - r2.maxNodalDisplacement) < 1e-12,
                "Roundtrip de serializacao deve preservar solucao exata");
}

void testAnalyticalBeamsPhase0() {
    // 1. Viga biapoiada com UDL: L = 6 m, q = -10 kN/m em Z global (plano x-z, flexão em torno de y)
    {
        StructuralModel m;
        m.clear();
        m.addCrossSection(CrossSection::makeGeneric(10, "Secao Teste", 0.05, 1.0e-4, 2.0e-4, 1.0e-4));

        NodeId n1 = m.addNode(0.0, 0.0, 0.0);
        NodeId n2 = m.addNode(3.0, 0.0, 0.0);
        NodeId n3 = m.addNode(6.0, 0.0, 0.0);

        m.findNode(n1)->support.fixed = {true, true, true, true, false, true};
        m.findNode(n3)->support.fixed = {false, true, true, false, false, true};

        ElementId e1 = m.addElement(n1, n2, 1, 10);
        ElementId e2 = m.addElement(n2, n3, 1, 10);
        for (ElementId eid : {e1, e2}) {
            auto* el = m.findElement(eid);
            el->distLoad.active = true;
            el->distLoad.coordSystem = CoordSystem::Global;
            el->distLoad.w = {0.0, 0.0, -10.0};
        }

        SolverResult res = FemSolver3D::solve(m);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Viga biapoiada UDL deve convergir e equilibrar");

        const double L = 6.0, E = 2.0e8, Iy = 1.0e-4, q = 10.0;
        const double wExact = (5.0 * q * std::pow(L, 4)) / (384.0 * E * Iy);
        const double wCalc = std::abs(m.findNode(n2)->displacement[2]);
        TEST_ASSERT(std::abs(wCalc - wExact) < 1e-8, "Flecha analitica exata da viga biapoiada UDL");
        TEST_ASSERT(std::abs(m.findNode(n1)->reaction[2] - 30.0) < 1e-6, "Reacao vertical esquerda = qL/2 = 30 kN");
        TEST_ASSERT(std::abs(m.findNode(n3)->reaction[2] - 30.0) < 1e-6, "Reacao vertical direita = qL/2 = 30 kN");

        const double mMid = std::abs(m.findElement(e1)->evaluateAt(1.0, m).My);
        TEST_ASSERT(std::abs(mMid - (q * L * L / 8.0)) < 1e-5, "Momento fletor maximo = qL^2/8 = 45 kNm");
    }

    // 2. Viga biengastada com UDL
    {
        StructuralModel m;
        m.clear();
        m.addCrossSection(CrossSection::makeGeneric(10, "Secao Teste", 0.05, 1.0e-4, 2.0e-4, 1.0e-4));

        NodeId n1 = m.addNode(0.0, 0.0, 0.0);
        NodeId n2 = m.addNode(3.0, 0.0, 0.0);
        NodeId n3 = m.addNode(6.0, 0.0, 0.0);
        m.findNode(n1)->support.fixed = {true, true, true, true, true, true};
        m.findNode(n3)->support.fixed = {true, true, true, true, true, true};

        ElementId e1 = m.addElement(n1, n2, 1, 10);
        ElementId e2 = m.addElement(n2, n3, 1, 10);
        for (ElementId eid : {e1, e2}) {
            auto* el = m.findElement(eid);
            el->distLoad.active = true;
            el->distLoad.coordSystem = CoordSystem::Global;
            el->distLoad.w = {0.0, 0.0, -12.0};
        }

        SolverResult res = FemSolver3D::solve(m);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Viga biengastada UDL deve convergir");

        const double L = 6.0, E = 2.0e8, Iy = 1.0e-4, q = 12.0;
        const double wExact = (q * std::pow(L, 4)) / (384.0 * E * Iy);
        const double wCalc = std::abs(m.findNode(n2)->displacement[2]);
        TEST_ASSERT(std::abs(wCalc - wExact) < 1e-8, "Flecha exata viga biengastada UDL");
        TEST_ASSERT(std::abs(std::abs(m.findNode(n1)->reaction[4]) - (q * L * L / 12.0)) < 1e-5,
                    "Momento de engastamento = qL^2/12 = 36 kNm");
    }

    // 3. Torção pura e flexão oblíqua biaxial + teste de escala elevada (equilíbrio scale-aware)
    {
        StructuralModel m;
        m.clear();
        m.addCrossSection(CrossSection::makeGeneric(10, "Secao Assim", 0.06, 2.0e-4, 0.8e-4, 0.5e-4));

        NodeId n1 = m.addNode(0.0, 0.0, 0.0);
        NodeId n2 = m.addNode(4.0, 0.0, 0.0);
        m.findNode(n1)->support.fixed = {true, true, true, true, true, true};

        const double Fy = -1.0e6;
        const double Fz = -2.0e6;
        const double Tx = 5.0e5;
        m.findNode(n2)->load.force = {0.0, Fy, Fz};
        m.findNode(n2)->load.moment = {Tx, 0.0, 0.0};

        m.addElement(n1, n2, 1, 10);
        SolverResult res = FemSolver3D::solve(m);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced,
                    "Flexao biaxial e torcao sob cargas grandes deve manter equilibrio scale-aware");

        const Material* mat = m.findMaterial(1);
        const CrossSection* sec = m.findCrossSection(10);
        const double L = 4.0;
        const double thetaExact = (Tx * L) / (mat->G() * sec->J);
        const double vExact = (Fy * std::pow(L, 3)) / (3.0 * mat->E * sec->Iz);
        const double wExact = (Fz * std::pow(L, 3)) / (3.0 * mat->E * sec->Iy);

        TEST_ASSERT(std::abs(m.findNode(n2)->displacement[3] - thetaExact) / thetaExact < 1e-8, "Torcao pura exata");
        TEST_ASSERT(std::abs(m.findNode(n2)->displacement[1] - vExact) / std::abs(vExact) < 1e-8, "Flexao Y exata");
        TEST_ASSERT(std::abs(m.findNode(n2)->displacement[2] - wExact) / std::abs(wExact) < 1e-8, "Flexao Z exata");
    }
}

void testMissingMeshNodesNoCrash() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser valido para malha corrompida");

    struct Case {
        kernel::MeshCellType cellType;
        std::vector<NodeId> nodeIds;
        const char* name;
    };
    const std::vector<Case> cases = {
        {kernel::MeshCellType::Tri3,  {1, 2, 999}, "Tri3"},
        {kernel::MeshCellType::Quad4, {1, 2, 3, 999}, "Quad4"},
        {kernel::MeshCellType::Tet4,  {1, 2, 3, 999}, "Tet4"},
        {kernel::MeshCellType::Hex8,  {1, 2, 3, 4, 5, 6, 7, 999}, "Hex8"},
    };

    for (size_t idx = 0; idx < cases.size(); ++idx) {
        StructuralModel m;
        m.getMaterials().clear();
        m.getCrossSections().clear();
        const MaterialId matId = m.addMaterial(Material::makeConcreteC30(1));

        // Adiciona nos 1..8 validos (999 propositalmente ausente)
        const double coords[8][3] = {
            {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
            {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}
        };
        for (int nid = 1; nid <= 8; ++nid) {
            kernel::MeshNode mn;
            mn.id = nid;
            mn.position = kernel::Vec3(coords[nid - 1][0], coords[nid - 1][1], coords[nid - 1][2]);
            if (nid <= 4) mn.fixed = {true, true, true, true, true, true};
            m.surfaceMesh().nodes.push_back(mn);
        }

        kernel::MeshElement me;
        me.id = 1;
        me.type = cases[idx].cellType;
        me.materialId = matId;
        me.thickness = 0.20;
        me.nodeIds = cases[idx].nodeIds;
        m.surfaceMesh().elements.push_back(me);

        // evaluateQuality direto nunca deve lancar std::out_of_range ([AUD-007])
        const auto qm = kernel::MeshGenerator::evaluateQuality(m.surfaceMesh());
        TEST_ASSERT(qm.invertedOrDegenerateCount == 1 && qm.validElements == 0,
                    std::string("evaluateQuality deve contabilizar no 999 ausente sem excecao em ") + cases[idx].name);

        const ModelValidation val = m.validate();
        bool hasMissingNodeCode = false;
        for (const auto& iss : val.issues) {
            if (iss.code == "ERR_MESH_MISSING_NODE") {
                hasMissingNodeCode = true;
                break;
            }
        }
        TEST_ASSERT(!val.valid && hasMissingNodeCode,
                    std::string("validate() deve acusar ERR_MESH_MISSING_NODE para ") + cases[idx].name);

        // Roundtrip via .omniframe tambem nao deve crashar
        const QString path = tmp.filePath(QString("missing_%1.omniframe").arg(cases[idx].name));
        TEST_ASSERT(ProjectSerializer::saveToFile(path, m), "Salvar .omniframe com malha inconsistente");
        StructuralModel loaded;
        if (ProjectSerializer::loadFromFile(path, loaded)) {
            const ModelValidation lval = loaded.validate();
            TEST_ASSERT(!lval.valid, "Modelo recarregado com no 999 deve permanecer invalido");
        }
    }
}

void testAud026ModelCascadeCleanupAndMeshQualityAllCorners() {
    // 1. Limpeza em cascata ao remover nos/elementos e protecao de material/secao em uso
    {
        StructuralModel m;
        m.clear();
        const NodeId n1 = m.addNode(0.0, 0.0, 0.0);
        const NodeId n2 = m.addNode(3.0, 0.0, 0.0);
        const NodeId n3 = m.addNode(3.0, 3.0, 0.0);
        const ElementId e1 = m.addElement(n1, n2, 1, 1);
        const ElementId e2 = m.addElement(n2, n3, 1, 1);

        TEST_ASSERT(!m.removeMaterial(1), "removeMaterial deve recusar exclusao de material em uso por barra 1D");
        TEST_ASSERT(!m.removeCrossSection(1), "removeCrossSection deve recusar exclusao de secao em uso por barra 1D");

        kernel::LoadCase lc;
        lc.id = 1;
        lc.name = "TesteCascata";
        lc.nodalLoads.push_back({n2, {10.0, 0.0, 0.0, 0.0, 0.0, 0.0}});
        lc.elementLoads.push_back({e1, kernel::ElementLoadInput{}});
        lc.elementLoads.push_back({e2, kernel::ElementLoadInput{}});
        m.addLoadCase(lc);
        m.getRigidLinks().push_back({n1, n2, true});
        m.getRigidDiaphragms().push_back({n1, {n2, n3}, 2});
        m.selectElement(e1, true);

        TEST_ASSERT(m.removeNode(n2), "removeNode(n2) deve suceder");
        TEST_ASSERT(m.getElements().empty(), "Barras incidentes a n2 devem ser removidas");
        TEST_ASSERT(m.getSelectedElements().empty(), "m_selectedElements deve limpar barras removidas por removeNode");
        TEST_ASSERT(m.getLoadCases()[0].nodalLoads.empty(), "nodalLoads de n2 devem ser removidas");
        TEST_ASSERT(m.getLoadCases()[0].elementLoads.empty(), "elementLoads das barras incidentes devem ser removidas");
        TEST_ASSERT(m.getRigidLinks().empty(), "rigidLinks referenciando n2 devem ser removidos");
        TEST_ASSERT(m.getRigidDiaphragms().size() == 1 && m.getRigidDiaphragms()[0].slaveNodes.size() == 1 &&
                        m.getRigidDiaphragms()[0].slaveNodes[0] == n3,
                    "rigidDiaphragms deve remover apenas o escravo n2 mantendo n3");

        const StructuralModel cloned = m.cloneWithoutHistory();
        TEST_ASSERT(!cloned.canUndo() && cloned.getNodes().size() == m.getNodes().size(),
                    "cloneWithoutHistory deve copiar dados sem historico de undo");
    }

    // 2. MeshGenerator::evaluateQuality deve detectar Quad4 bowtie e Hex8 com canto 7 invertido
    {
        kernel::MeshContainer quadBowtie;
        quadBowtie.nodes = {
            {1, kernel::Vec3(0.0, 0.0, 0.0), false, ""},
            {2, kernel::Vec3(1.0, 1.0, 0.0), false, ""},
            {3, kernel::Vec3(1.0, 0.0, 0.0), false, ""},
            {4, kernel::Vec3(0.0, 1.0, 0.0), false, ""}
        };
        quadBowtie.elements.push_back({1, kernel::MeshCellType::Quad4, {1, 2, 3, 4}, ""});
        const auto qQuad = kernel::MeshGenerator::evaluateQuality(quadBowtie);
        TEST_ASSERT(qQuad.invertedOrDegenerateCount == 1 && qQuad.minJacobian <= 0.0,
                    "evaluateQuality deve detectar Quad4 auto-intersectante (bowtie)");

        kernel::MeshContainer hexCorner7Inv =
            kernel::MeshGenerator::generateStructuredHexGrid(
                kernel::Vec3(0.0, 0.0, 0.0), kernel::Vec3(1.0, 1.0, 1.0), 1, 1, 1, false, "H");
        // No 8 (indice 7 = canto p7=(0,1,1)) deslocado para baixo de z=0 inverte apenas j7 (j0 e j6 permanecem > 0)
        hexCorner7Inv.nodes[7].position = kernel::Vec3(1.5, -0.5, 1.0);
        const auto qHex = kernel::MeshGenerator::evaluateQuality(hexCorner7Inv);
        TEST_ASSERT(qHex.invertedOrDegenerateCount == 1 && qHex.minJacobian <= 0.0,
                    "evaluateQuality deve testar todos os 8 cantos do Hex8 (incluindo canto 7)");
    }
}

} // namespace

int main() {
    testFakeReleaseStiffnessRemoved();
    testZeroLengthAndFiniteValidation();
    testSerializerTransactionalAndStrict();
    testAnalyticalBeamsPhase0();
    testMissingMeshNodesNoCrash();
    testAud026ModelCascadeCleanupAndMeshQualityAllCorners();

    std::cout << "[OK] test_Phase0ValidationAndBenchmarks passou com sucesso." << std::endl;
    return 0;
}
