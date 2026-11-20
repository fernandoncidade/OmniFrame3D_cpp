/**
 * @file test_ProjectSerializer.cpp
 * @brief Teste de Serialização e Desserialização de Projetos (.omni JSON)
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/ProjectSerializer.hpp"

#include <QFile>
#include <QTemporaryDir>

using namespace omniframe;

namespace {

void testSchema21AndBackwardCompat() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir invalido");

    StructuralModel m;
    m.metadata().projectName = "Torre Corporativa Schema 2.1";
    m.metadata().engineerName = "Eng. Estrutural Silva";
    m.metadata().clientName = "Incorporadora Alfa";
    m.metadata().siteLocation = "Sao Paulo - SP";
    m.metadata().soilSigmaAdm_kPa = 320.0;
    m.metadata().footingB_m = 2.2;
    m.metadata().footingL_m = 2.5;
    m.metadata().windV0_ms = 42.0;

    Material plasticMat = Material::makeStructuralSteel(m.getNextMaterialId());
    plasticMat.name = "Aco J2 S355";
    plasticMat.isPlastic = true;
    plasticMat.sigmaY = 355000.0;
    plasticMat.H_iso = 12000.0;
    plasticMat.H_kin = 4500.0;
    const MaterialId pmid = m.addMaterial(plasticMat);

    CrossSection compSec = CrossSection::makeComposite(
        m.getNextSectionId(),
        "Viga Mista Aco-Concreto",
        {
            {0.0, -0.15, 0.20, 0.30, 1.0},
            {0.0,  0.10, 0.80, 0.12, 0.15}
        });
    const SectionId csid = m.addCrossSection(compSec);

    const NodeId n1 = m.addNode(0.0, 0.0, 0.0);
    const NodeId n2 = m.addNode(5.0, 0.0, 0.0);
    m.findNode(n1)->support = SupportRestraint::makeFixed();
    m.addElement(n1, n2, pmid, csid);

    kernel::LoadCase lc;
    lc.id = 1;
    lc.name = "PP + Permanente";
    lc.category = kernel::LoadCaseCategory::Dead;
    lc.selfWeight.enabled = true;
    lc.selfWeight.factor = 1.0;
    m.addLoadCase(lc);

    kernel::LoadCombination comb;
    comb.id = 1;
    comb.name = "ELU Normal";
    comb.limitState = kernel::LimitStateType::ELU_Normal;
    comb.pDeltaEnabled = true;
    comb.factors.push_back({1, 1.40});
    m.addLoadCombination(comb);

    m.surfaceMesh() = kernel::MeshGenerator::generateStructuredQuadGrid(
        kernel::Vec3(0.0, 0.0, 0.0),
        kernel::Vec3(4.0, 0.0, 0.0),
        kernel::Vec3(0.0, 3.0, 0.0),
        2, 2, false);

    const QString path21 = tmp.filePath("schema21.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(path21, m), "Salvar projeto Schema 2.1.0");

    StructuralModel loaded21;
    TEST_ASSERT(ProjectSerializer::loadFromFile(path21, loaded21), "Carregar projeto Schema 2.1.0");
    TEST_ASSERT(loaded21.metadata().projectName == "Torre Corporativa Schema 2.1",
                "Metadados projectName preservados");
    TEST_ASSERT(std::abs(loaded21.metadata().soilSigmaAdm_kPa - 320.0) < 1e-9 &&
                    std::abs(loaded21.metadata().windV0_ms - 42.0) < 1e-9,
                "Parametros NBR em metadata preservados");
    const Material* lmat = loaded21.findMaterial(pmid);
    TEST_ASSERT(lmat && lmat->isPlastic && std::abs(lmat->sigmaY - 355000.0) < 1e-6 &&
                    std::abs(lmat->H_iso - 12000.0) < 1e-6 && std::abs(lmat->H_kin - 4500.0) < 1e-6,
                "Plasticidade J2 do material preservada no Schema 2.1.0");
    const CrossSection* lsec = loaded21.findCrossSection(csid);
    TEST_ASSERT(lsec && lsec->compositeParts.size() == 2 &&
                    std::abs(lsec->compositeParts[1].modularRatioN - 0.15) < 1e-9,
                "Partes compostas da secao preservadas no Schema 2.1.0");
    TEST_ASSERT(loaded21.surfaceMesh().nodes.size() == 9 && loaded21.surfaceMesh().elements.size() == 4,
                "Malha 2D/3D persistida e recarregada no Schema 2.1.0");
    TEST_ASSERT(loaded21.getLoadCases().size() == 1 && loaded21.getLoadCombinations().size() == 1 &&
                    loaded21.getLoadCombinations()[0].pDeltaEnabled,
                "Casos de carga e combinacoes P-Delta preservados");

    // Compatibilidade retroativa com Schema 1.0 minimo
    const QString path10 = tmp.filePath("legacy10.omni");
    {
        QFile f(path10);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Criar legacy Schema 1.0");
        f.write(R"json({
  "version": "1.0.0",
  "materials": [{"id": 1, "name": "Aco", "E": 200000000.0, "nu": 0.3, "alpha": 1.2e-5, "gamma": 78.5}],
  "sections": [{"id": 1, "name": "Ret", "shape": 0, "A": 0.08, "Iy": 0.001, "Iz": 0.0005, "J": 0.0002}],
  "nodes": [
    {"id": 1, "x": 0.0, "y": 0.0, "z": 0.0, "support": {"fixed": [true,true,true,true,true,true]}},
    {"id": 2, "x": 3.0, "y": 0.0, "z": 0.0}
  ],
  "elements": [{"id": 1, "nodeStart": 1, "nodeEnd": 2, "materialId": 1, "sectionId": 1}],
  "futureExtensionTag": {"experimental": 42}
})json");
    }
    StructuralModel loaded10;
    TEST_ASSERT(ProjectSerializer::loadFromFile(path10, loaded10),
                "Schema 1.0 legado deve continuar carregando sem falha");
    TEST_ASSERT(loaded10.getNodes().size() == 2 && !loaded10.serializationWarnings().empty(),
                "Campos desconhecidos devem gerar aviso e ser preservados");
    std::cout << "  - Schema 2.1.0 + compatibilidade retroativa Schema 1.0 [OK]\n";
}

} // namespace

int main() {
    std::cout << "============================================================\n";
    std::cout << "  TESTE: SERIALIZACAO E DESSERIALIZACAO (.omni JSON)        \n";
    std::cout << "============================================================\n";

    Material steel = Material::makeStructuralSteel(1);
    CrossSection rect = CrossSection::makeRectangular(1, "Retangular 20x40", 0.20, 0.40);

    StructuralModel originalModel;
    originalModel.addMaterial(steel);
    originalModel.addCrossSection(rect);

    NodeId n1 = originalModel.addNode(0.0, 0.0, 0.0);
    NodeId n2 = originalModel.addNode(4.0, 0.0, 0.0);
    NodeId n3 = originalModel.addNode(4.0, 3.0, 0.0);

    Node* node1 = originalModel.findNode(n1);
    TEST_ASSERT(node1 != nullptr, "No 1 nao encontrado");
    node1->support = SupportRestraint::makeFixed();

    Node* node3 = originalModel.findNode(n3);
    TEST_ASSERT(node3 != nullptr, "No 3 nao encontrado");
    node3->load.force.y = -50.0;
    node3->load.force.z = 12.5;

    originalModel.addElement(n1, n2, 1, 1, 0.0);
    originalModel.addElement(n2, n3, 1, 1, 90.0);

    // Salvar em arquivo temporário .omni
    QString testFilePath = "teste_serializacao.omni";
    bool saved = ProjectSerializer::saveToFile(testFilePath, originalModel);
    TEST_ASSERT(saved, "Falha ao serializar projeto estrutural para disco (.omni)");
    std::cout << "  - Projeto serializado em: " << testFilePath.toStdString() << " [OK]\n";

    // Recarregar em um novo modelo
    StructuralModel loadedModel;
    bool loaded = ProjectSerializer::loadFromFile(testFilePath, loadedModel);
    TEST_ASSERT(loaded, "Falha ao carregar projeto desserializado (.omni)");
    std::cout << "  - Projeto recarregado do disco [OK]\n";

    // Validar fidelidade dimensional e topológica
    TEST_ASSERT(loadedModel.getNodes().size() == originalModel.getNodes().size(),
                "Numero de nos desserializados diverge do modelo original");
    TEST_ASSERT(loadedModel.getElements().size() == originalModel.getElements().size(),
                "Numero de elementos desserializados diverge do modelo original");
    TEST_ASSERT(loadedModel.getMaterials().size() == originalModel.getMaterials().size(),
                "Numero de materiais diverge");
    TEST_ASSERT(loadedModel.getCrossSections().size() == originalModel.getCrossSections().size(),
                "Numero de secoes transversais diverge");

    const Node* loadedNode1 = loadedModel.findNode(n1);
    TEST_ASSERT(loadedNode1 != nullptr, "No 1 carregado nao encontrado");
    TEST_ASSERT(loadedNode1->support.fixed[0] && loadedNode1->support.fixed[1] && loadedNode1->support.fixed[2],
                "Condicoes de contorno de apoio nao preservadas na desserializacao");

    const Node* loadedNode3 = loadedModel.findNode(n3);
    TEST_ASSERT(loadedNode3 != nullptr, "No 3 carregado nao encontrado");
    TEST_ASSERT(std::abs(loadedNode3->load.force.y - (-50.0)) < 1e-6,
                "Forca nodal Y nao preservada na desserializacao");
    TEST_ASSERT(std::abs(loadedNode3->load.force.z - 12.5) < 1e-6,
                "Forca nodal Z nao preservada na desserializacao");

    std::cout << "  - Elementos originais: " << originalModel.getElements().size()
              << " | Elementos carregados: " << loadedModel.getElements().size() << " [OK]\n";

    testSchema21AndBackwardCompat();

    std::cout << "\n[SUCESSO] Teste de Serialização .omni (Schemas 1.0 / 2.0 / 2.1) concluido com 100% de integridade!\n";
    return 0;
}
