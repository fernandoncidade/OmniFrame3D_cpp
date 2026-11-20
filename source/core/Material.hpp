#pragma once

#include "Types.hpp"
#include <string>

namespace omniframe {

class Material {
public:
    MaterialId id = INVALID_ID;
    std::string name = "Material Padrão";
    double E = 2.0e8;       // Módulo de Elasticidade (kN/m² = kPa) [Ex: 200 GPa = 2.0e8 kN/m²]
    double nu = 0.3;        // Coeficiente de Poisson (adimensional)
    double alpha = 1.2e-5;  // Coeficiente de Dilatação Térmica (1/°C)
    double gamma = 78.5;    // Peso Específico (kN/m³)

    Material() = default;

    Material(MaterialId mid, std::string mname, double mE, double mnu, double malpha, double mgamma)
        : id(mid), name(std::move(mname)), E(mE), nu(mnu), alpha(malpha), gamma(mgamma) {}

    double G() const {
        return E / (2.0 * (1.0 + nu));
    }

    static Material makeStructuralSteel(MaterialId mid = 1) {
        return Material(mid, "Aço Estrutural (ASTM A36)", 2.0e8, 0.30, 1.2e-5, 78.5);
    }

    static Material makeConcreteC30(MaterialId mid = 2) {
        return Material(mid, "Concreto C30", 3.0e7, 0.20, 1.0e-5, 25.0);
    }

    static Material makeAluminum6061(MaterialId mid = 3) {
        return Material(mid, "Alumínio 6061-T6", 7.0e7, 0.33, 2.3e-5, 27.0);
    }

    static Material makeTimber(MaterialId mid = 4) {
        return Material(mid, "Madeira Estrutural", 1.45e7, 0.20, 0.5e-5, 8.0);
    }
};

} // namespace omniframe

