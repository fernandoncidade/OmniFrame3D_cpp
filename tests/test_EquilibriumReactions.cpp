/**
 * @file test_EquilibriumReactions.cpp
 * @brief Teste de Reações de Apoio e Equilíbrio Estático Global (Leis de Newton)
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"

using namespace omniframe;

int main() {
    std::cout << "============================================================\n";
    std::cout << "  TESTE: REACOES DE APOIO E EQUILIBRIO ESTATICO GLOBAL      \n";
    std::cout << "============================================================\n";

    Material steel = Material::makeStructuralSteel(1);
    CrossSection rect = CrossSection::makeRectangular(1, "Retangular 20x40", 0.20, 0.40);

    StructuralModel model;
    model.addMaterial(steel);
    model.addCrossSection(rect);

    // Pórtico plano simples (coluna + viga)
    // Nó 1 (0,0,0) engaste -> Nó 2 (0,3,0) -> Nó 3 (4,3,0)
    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(0.0, 3.0, 0.0);
    NodeId n3 = model.addNode(4.0, 3.0, 0.0);

    Node* node1 = model.findNode(n1);
    TEST_ASSERT(node1 != nullptr, "No 1 de base nao encontrado");
    node1->support = SupportRestraint::makeFixed();

    // Carga vertical no nó 3
    double appliedFy = -25.0; // kN
    double appliedFx = 10.0;  // kN
    Node* node3 = model.findNode(n3);
    TEST_ASSERT(node3 != nullptr, "No 3 nao encontrado");
    node3->load.force.y = appliedFy;
    node3->load.force.x = appliedFx;

    model.addElement(n1, n2, 1, 1, 0.0);
    model.addElement(n2, n3, 1, 1, 0.0);

    SolverResult result = FemSolver3D::solve(model);
    TEST_ASSERT(result.success, "Falha na solucao do portico");
    TEST_ASSERT(result.equilibrium.isBalanced, "Estrutura deve estar rigorosamente equilibrada estaticamente");

    // Reações no apoio 1:
    // Rx deve equilibrar Fx: Rx + Fx = 0 => Rx = -10.0 kN
    // Ry deve equilibrar Fy: Ry + Fy = 0 => Ry = +25.0 kN
    double rx = node1->reaction[0];
    double ry = node1->reaction[1];

    std::cout << "  - Forca aplicada Fx: " << appliedFx << " kN | Reacao Rx: " << rx << " kN\n";
    std::cout << "  - Forca aplicada Fy: " << appliedFy << " kN | Reacao Ry: " << ry << " kN\n";
    TEST_ASSERT(std::abs(rx + appliedFx) < 1e-4, "Reacao Rx nao equilibra a forca externa horizontal");
    TEST_ASSERT(std::abs(ry + appliedFy) < 1e-4, "Reacao Ry nao equilibra a forca externa vertical");

    // Resíduos estáticos globais
    std::cout << "  - Residuo Global de Forcas:  " 
              << std::abs(result.equilibrium.residualForces.x) + std::abs(result.equilibrium.residualForces.y) + std::abs(result.equilibrium.residualForces.z)
              << " kN [OK]\n";
    std::cout << "  - Residuo Global de Momentos: "
              << std::abs(result.equilibrium.residualMoments.x) + std::abs(result.equilibrium.residualMoments.y) + std::abs(result.equilibrium.residualMoments.z)
              << " kNm [OK]\n";

    std::cout << "\n[SUCESSO] Teste de Reações e Equilíbrio Estático concluido com exito!\n";
    return 0;
}
