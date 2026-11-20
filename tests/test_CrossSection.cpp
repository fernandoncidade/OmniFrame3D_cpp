/**
 * @file test_CrossSection.cpp
 * @brief Teste de Geometria e Propriedades Inerciais de Seções Transversais
 */

#include "TestHelper.hpp"
#include "core/CrossSection.hpp"

using namespace omniframe;

int main() {
    std::cout << "============================================================\n";
    std::cout << "  TESTE: SECOES TRANSVERSAIS PARAMETRICAS (A, Iz, Iy, J)    \n";
    std::cout << "============================================================\n";

    // 1. Seção Retangular (b=0.20m, h=0.40m)
    CrossSection rect = CrossSection::makeRectangular(1, "Retangular 20x40", 0.20, 0.40);
    double expectedAreaRect = 0.20 * 0.40;
    double expectedIyRect = (0.20 * std::pow(0.40, 3)) / 12.0; // b * h^3 / 12 (em torno de y)
    double expectedIzRect = (0.40 * std::pow(0.20, 3)) / 12.0; // h * b^3 / 12 (em torno de z)

    TEST_ASSERT(std::abs(rect.A - expectedAreaRect) < 1e-6, "Area de secao retangular incorreta");
    TEST_ASSERT(std::abs(rect.Iz - expectedIzRect) < 1e-6, "Inercia Iz de secao retangular incorreta");
    TEST_ASSERT(std::abs(rect.Iy - expectedIyRect) < 1e-6, "Inercia Iy de secao retangular incorreta");
    TEST_ASSERT(rect.J > 0.0, "Constante de torcao J deve ser positiva");
    std::cout << "  - Secao Retangular (20x40cm): Area = " << rect.A << " m^2, Iz = " << rect.Iz << " m^4, Iy = " << rect.Iy << " m^4 [OK]\n";

    // 2. Seção Circular Maciça (d=0.30m, r=0.15m)
    CrossSection circ = CrossSection::makeCircularSolid(2, "Circular d=30cm", 0.30);
    double r = 0.15;
    double expectedAreaCirc = 3.14159265358979323846 * r * r;
    double expectedInertiaCirc = (3.14159265358979323846 * std::pow(0.30, 4)) / 64.0;
    double expectedJCirc = 2.0 * expectedInertiaCirc;

    TEST_ASSERT(std::abs(circ.A - expectedAreaCirc) < 1e-5, "Area de secao circular incorreta");
    TEST_ASSERT(std::abs(circ.Iz - expectedInertiaCirc) < 1e-6, "Inercia Iz circular incorreta");
    TEST_ASSERT(std::abs(circ.Iy - expectedInertiaCirc) < 1e-6, "Inercia Iy circular incorreta");
    TEST_ASSERT(std::abs(circ.J - expectedJCirc) < 1e-6, "Constante de torcao J circular incorreta");
    std::cout << "  - Secao Circular Macica (d=30cm): Area = " << circ.A << " m^2, J = " << circ.J << " m^4 [OK]\n";

    // 3. Seção Circular Oca / Tubo (dExt=0.20m, t=0.01m)
    CrossSection tube = CrossSection::makeCircularTube(3, "Tubo d=20cm t=1cm", 0.20, 0.01);
    TEST_ASSERT(tube.A > 0.0 && tube.A < expectedAreaCirc, "Area do tubo deve ser coerente com raio e espessura");
    TEST_ASSERT(tube.Iz > 0.0 && tube.Iy > 0.0 && tube.J > 0.0, "Inercias do tubo devem ser positivas");
    std::cout << "  - Tubo Circular (d=20cm, t=10mm): Area = " << tube.A << " m^2, Iz = " << tube.Iz << " m^4 [OK]\n";

    std::cout << "\n[SUCESSO] Teste de Seções Transversais concluido com exito!\n";
    return 0;
}
