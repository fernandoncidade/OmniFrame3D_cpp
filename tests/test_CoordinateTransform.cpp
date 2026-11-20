/**
 * @file test_CoordinateTransform.cpp
 * @brief Teste de Transformação de Coordenadas 3D e Matriz de Rotação Espacial [T]
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"

using namespace omniframe;

int main() {
    std::cout << "============================================================\n";
    std::cout << "  TESTE: TRANSFORMACAO ESPACIAL 3D E ORTOGONALIDADE [T]     \n";
    std::cout << "============================================================\n";

    Material steel = Material::makeStructuralSteel(1);
    CrossSection rect = CrossSection::makeRectangular(1, "Retangular 20x40", 0.20, 0.40);

    StructuralModel model;
    model.addMaterial(steel);
    model.addCrossSection(rect);

    // Barra inclinada nos 3 eixos (dx=3, dy=4, dz=5, roll=25 graus)
    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(3.0, 4.0, 5.0);
    ElementId elemId = model.addElement(n1, n2, 1, 1, 25.0);

    const Element* elem = model.findElement(elemId);
    TEST_ASSERT(elem != nullptr, "Elemento nao encontrado");

    // Matriz de transformação 12x12
    Eigen::Matrix<double, 12, 12> T = elem->computeTransformationMatrix(model);
    TEST_ASSERT(T.rows() == 12 && T.cols() == 12, "Dimensoes incorretas da matriz de transformacao T");

    // 1. Ortogonalidade: T * T^T = I
    Eigen::Matrix<double, 12, 12> T_ortho = T * T.transpose();
    TEST_ASSERT(T_ortho.isApprox(Eigen::Matrix<double, 12, 12>::Identity(), 1e-7),
                "Matriz de transformacao 3D deve ser estritamente ortogonal (T * T^T = I)");
    std::cout << "  - Ortogonalidade Espacial (T * T^T = I): CONFIRMADA [OK]\n";

    // 2. Congruência da matriz global: Ke_global = T^T * Ke_local * T
    Eigen::Matrix<double, 12, 12> kLocal = elem->computeLocalStiffnessMatrix(model);
    Eigen::Matrix<double, 12, 12> kGlobal = elem->computeGlobalStiffnessMatrix(model);
    Eigen::Matrix<double, 12, 12> kCalculated = T.transpose() * kLocal * T;

    TEST_ASSERT(kGlobal.isApprox(kCalculated, 1e-7),
                "Transformacao congruente de rigidez global Ke = T^T * k * T inconsistente");
    std::cout << "  - Congruencia Global Ke = T^T * k * T: CONFIRMADA [OK]\n";

    // 3. Simetria de Ke_global: K = K^T
    TEST_ASSERT(kGlobal.isApprox(kGlobal.transpose(), 1e-7),
                "Matriz de rigidez global no sistema global deve ser perfeitamente simetrica");
    std::cout << "  - Simetria da Matriz Global 12x12: CONFIRMADA [OK]\n";

    std::cout << "\n[SUCESSO] Teste de Transformação Espacial 3D concluido com exito!\n";
    return 0;
}
