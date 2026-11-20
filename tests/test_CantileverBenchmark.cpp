/**
 * @file test_CantileverBenchmark.cpp
 * @brief Benchmark Canônico — Viga em Balanço 3D contra Solução Analítica Euler-Bernoulli
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"

using namespace omniframe;

int main() {
    std::cout << "============================================================\n";
    std::cout << "  BENCHMARK CANONICO: VIGA EM BALANCO 3D (PL^3/3EI e TL/GJ) \n";
    std::cout << "============================================================\n";

    Material steel = Material::makeStructuralSteel(1);
    CrossSection rect = CrossSection::makeRectangular(100, "Retangular 20x40", 0.20, 0.40);

    StructuralModel model;
    MaterialId matId = model.addMaterial(steel);
    SectionId secId = model.addCrossSection(rect);

    double L = 6.0; // Comprimento em metros
    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(L, 0.0, 0.0);

    // Engaste completo no Nó 1 (6 DOFs fixos)
    Node* node1 = model.findNode(n1);
    TEST_ASSERT(node1 != nullptr, "No de engaste nao encontrado");
    node1->support = SupportRestraint::makeFixed();

    // Cargas pontuais na extremidade livre (Nó 2)
    double Py = -10.0; // kN (para baixo no eixo Y)
    double Pz = 5.0;   // kN (no eixo Z)
    double Mx = 2.0;   // kNm (torque em torno do eixo X)

    Node* node2 = model.findNode(n2);
    TEST_ASSERT(node2 != nullptr, "No de extremidade livre nao encontrado");
    node2->load.force.y = Py;
    node2->load.force.z = Pz;
    node2->load.moment.x = Mx;

    model.addElement(n1, n2, matId, secId, 0.0);

    // Resolução pelo Solver FEA 3D
    SolverResult result = FemSolver3D::solve(model);
    TEST_ASSERT(result.success, "Execucao do solver FEA falhou");
    std::cout << "  - Tempo de solucao: " << result.executionTimeMs << " ms\n";
    std::cout << "  - DOFs Totais: " << result.totalDofs << ", Restritos: " << result.restrainedDofs << "\n";

    const CrossSection* sec = model.findCrossSection(secId);
    TEST_ASSERT(sec != nullptr, "Secao da viga nao encontrada");

    // 1. Solução Analítica Fechada:
    // delta_y = Py * L^3 / (3 * E * Iz)
    double expectedDeltaY = (Py * std::pow(L, 3)) / (3.0 * steel.E * sec->Iz);
    double calculatedDeltaY = node2->displacement[1];
    double relErrorY = std::abs((calculatedDeltaY - expectedDeltaY) / expectedDeltaY);

    std::cout << "  - Deslocamento Y Analitico:  " << expectedDeltaY * 1000.0 << " mm\n";
    std::cout << "  - Deslocamento Y Calculado:  " << calculatedDeltaY * 1000.0 << " mm\n";
    std::cout << "  - Erro relativo vertical Y:  " << relErrorY * 100.0 << " %\n";
    TEST_ASSERT(relErrorY < 1e-3, "Deslocamento vertical Y diverge da solucao teorica Euler-Bernoulli");

    // delta_z = Pz * L^3 / (3 * E * Iy)
    double expectedDeltaZ = (Pz * std::pow(L, 3)) / (3.0 * steel.E * sec->Iy);
    double calculatedDeltaZ = node2->displacement[2];
    double relErrorZ = std::abs((calculatedDeltaZ - expectedDeltaZ) / expectedDeltaZ);

    std::cout << "  - Deslocamento Z Analitico:  " << expectedDeltaZ * 1000.0 << " mm\n";
    std::cout << "  - Deslocamento Z Calculado:  " << calculatedDeltaZ * 1000.0 << " mm\n";
    std::cout << "  - Erro relativo horizontal Z: " << relErrorZ * 100.0 << " %\n";
    TEST_ASSERT(relErrorZ < 1e-3, "Deslocamento horizontal Z diverge da solucao teorica Euler-Bernoulli");

    // theta_x = Mx * L / (G * J)
    double expectedThetaX = (Mx * L) / (steel.G() * sec->J);
    double calculatedThetaX = node2->displacement[3];
    double relErrorTorsion = std::abs((calculatedThetaX - expectedThetaX) / expectedThetaX);

    std::cout << "  - Rotacao Torcional Analitica: " << expectedThetaX << " rad\n";
    std::cout << "  - Rotacao Torcional Calculada: " << calculatedThetaX << " rad\n";
    std::cout << "  - Erro relativo torcao:        " << relErrorTorsion * 100.0 << " %\n";
    TEST_ASSERT(relErrorTorsion < 1e-3, "Rotacao de torcao diverge da solucao analitica de Saint-Venant");

    std::cout << "\n[SUCESSO] Benchmark Canônico validado com precisao absoluta!\n";
    return 0;
}
