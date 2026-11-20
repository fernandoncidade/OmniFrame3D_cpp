/**
 * @file test_Material.cpp
 * @brief Teste de Propriedades Mecânicas dos Materiais e Elasticidade Isotrópica
 */

#include "TestHelper.hpp"
#include "core/Material.hpp"

using namespace omniframe;

int main() {
    std::cout << "============================================================\n";
    std::cout << "  TESTE: PROPRIEDADES MECANICAS DE MATERIAIS (ISOTROPIA)    \n";
    std::cout << "============================================================\n";

    Material steel = Material::makeStructuralSteel(1);
    TEST_ASSERT(steel.id == 1, "ID do aco estrutural incorreto");
    TEST_ASSERT(steel.E > 0.0, "Modulo de elasticidade E deve ser estritamente positivo");
    TEST_ASSERT(steel.nu >= 0.0 && steel.nu < 0.5, "Coeficiente de Poisson fora do intervalo elastico valido");
    
    // G = E / (2 * (1 + nu))
    double expectedG = steel.E / (2.0 * (1.0 + steel.nu));
    TEST_ASSERT(std::abs(steel.G() - expectedG) < 1e-4, "Modulo de cisalhamento G viola a hipotese de isotropia linear");
    TEST_ASSERT(steel.gamma > 0.0, "Peso especifico gamma deve ser positivo");

    std::cout << "  - Modulo de Elasticidade E: " << steel.E << " kPa\n";
    std::cout << "  - Modulo de Cisalhamento G: " << steel.G() << " kPa\n";
    std::cout << "  - Coeficiente de Poisson nu: " << steel.nu << "\n";
    std::cout << "  - Peso Especifico gamma: " << steel.gamma << " kN/m^3\n";

    Material concrete = Material::makeConcreteC30(2);
    TEST_ASSERT(concrete.id == 2, "ID do concreto incorreto");
    TEST_ASSERT(concrete.E > 0.0, "Modulo E do concreto deve ser positivo");
    double expectedConcreteG = concrete.E / (2.0 * (1.0 + concrete.nu));
    TEST_ASSERT(std::abs(concrete.G() - expectedConcreteG) < 1e-4, "Relacao isotropica do concreto invalida");

    std::cout << "\n[SUCESSO] Teste de Materiais concluido com exito!\n";
    return 0;
}
