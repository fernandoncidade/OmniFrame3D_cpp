/**
 * @file test_ProjectSerializer.cpp
 * @brief Teste de Serialização e Desserialização de Projetos (.omni JSON)
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/ProjectSerializer.hpp"

using namespace omniframe;

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

    std::cout << "  - Nós originais: " << originalModel.getNodes().size() 
              << " | Nós carregados: " << loadedModel.getNodes().size() << " [OK]\n";
    std::cout << "  - Elementos originais: " << originalModel.getElements().size()
              << " | Elementos carregados: " << loadedModel.getElements().size() << " [OK]\n";

    std::cout << "\n[SUCESSO] Teste de Serialização .omni concluido com 100% de integridade!\n";
    return 0;
}
