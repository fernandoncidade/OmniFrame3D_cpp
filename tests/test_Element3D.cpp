/**
 * @file test_Element3D.cpp
 * @brief Teste de Topologia, Comprimento Euclidiano e Matriz de Rigidez Local 12x12
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"

using namespace omniframe;

int main() {
    std::cout << "============================================================\n";
    std::cout << "  TESTE: ELEMENTO DE BARRA 3D (EULER-BERNOULLI 12x12 LOCAL) \n";
    std::cout << "============================================================\n";

    Material steel = Material::makeStructuralSteel(1);
    CrossSection rect = CrossSection::makeRectangular(100, "Retangular 20x40", 0.20, 0.40);

    StructuralModel model;
    MaterialId matId = model.addMaterial(steel);
    SectionId secId = model.addCrossSection(rect);

    NodeId n1 = model.addNode(1.0, 2.0, 3.0);
    NodeId n2 = model.addNode(5.0, 2.0, 6.0); // dx=4, dy=0, dz=3 => L = sqrt(16 + 9) = 5.0
    ElementId elemId = model.addElement(n1, n2, matId, secId, 0.0);

    const Element* elem = model.findElement(elemId);
    TEST_ASSERT(elem != nullptr, "Elemento 3D nao foi encontrado no modelo");

    double L = elem->computeLength(model);
    TEST_ASSERT(std::abs(L - 5.0) < 1e-6, "Comprimento euclidiano 3D divergente (esperado: 5.0 m)");
    std::cout << "  - Comprimento Euclidiano 3D: " << L << " m [OK]\n";

    // Cálculo da matriz local 12x12
    Eigen::Matrix<double, 12, 12> kLocal = elem->computeLocalStiffnessMatrix(model);
    TEST_ASSERT(kLocal.rows() == 12 && kLocal.cols() == 12, "Dimensoes da matriz local incorretas (esperado 12x12)");

    // Simetria matemática rigorosa: K = K^T
    TEST_ASSERT(kLocal.isApprox(kLocal.transpose(), 1e-8), "Matriz local de rigidez 12x12 deve ser rigorosamente simetrica");
    std::cout << "  - Simetria da Matriz Local 12x12: CONFIRMADA [OK]\n";

    // Termos de rigidez axial: k(0,0) = EA/L
    double expectedAxialK = (steel.E * rect.A) / L;
    TEST_ASSERT(std::abs(kLocal(0, 0) - expectedAxialK) < 1e-4, "Termo de rigidez axial EA/L incorreto");
    TEST_ASSERT(std::abs(kLocal(6, 6) - expectedAxialK) < 1e-4, "Termo de rigidez axial no no final incorreto");
    std::cout << "  - Rigidez Axial EA/L: " << kLocal(0, 0) << " kN/m [OK]\n";

    std::cout << "\n[SUCESSO] Teste de Elemento 3D concluido com exito!\n";
    return 0;
}
