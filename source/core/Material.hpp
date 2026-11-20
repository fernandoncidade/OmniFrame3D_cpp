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
    double rho = 7850.0;    // Massa Específica física (kg/m³), distinta de gamma

    // Parâmetros opcionais de plasticidade J2 / von Mises (Fase 3)
    bool isPlastic = false;
    double sigmaY = 2.5e5;  // Tensão de escoamento inicial (kPa = kN/m²; ex: 250 MPa = 2.5e5 kPa)
    double H_iso = 0.0;     // Módulo de encruamento isotrópico (kPa)
    double H_kin = 0.0;     // Módulo de encruamento cinemático linear (kPa)

    Material() = default;

    Material(MaterialId mid, std::string mname, double mE, double mnu, double malpha, double mgamma)
        : id(mid), name(std::move(mname)), E(mE), nu(mnu), alpha(malpha), gamma(mgamma),
          rho(mgamma * (1000.0 / 9.80665)) {}

    Material(MaterialId mid, std::string mname, double mE, double mnu, double malpha, double mgamma, double mrho)
        : id(mid), name(std::move(mname)), E(mE), nu(mnu), alpha(malpha), gamma(mgamma), rho(mrho) {}

    double G() const {
        return E / (2.0 * (1.0 + nu));
    }

    /// Massa específica consistente com unidades [kN, m, s]: t/m³ = kg/m³ / 1000 = kN·s²/m⁴
    double consistentMassDensity() const {
        return rho * 1e-3;
    }

    static Material makeStructuralSteel(MaterialId mid = 1) {
        Material m(mid, "Aço Estrutural (ASTM A36)", 2.0e8, 0.30, 1.2e-5, 78.5, 7850.0);
        m.sigmaY = 2.5e5;
        m.H_iso = 2.0e6;
        return m;
    }

    static Material makeStructuralSteelA572(MaterialId mid = 2) {
        Material m(mid, "Aço Alta Resistência (ASTM A572 Gr. 50)", 2.0e8, 0.30, 1.2e-5, 78.5, 7850.0);
        m.sigmaY = 3.45e5;
        m.H_iso = 2.0e6;
        return m;
    }

    static Material makeConcreteC25(MaterialId mid = 3) {
        Material m(mid, "Concreto C25", 2.8e7, 0.20, 1.0e-5, 25.0, 2500.0);
        m.sigmaY = 2.5e4;
        return m;
    }

    static Material makeConcreteC30(MaterialId mid = 2) {
        Material m(mid, "Concreto C30", 3.0e7, 0.20, 1.0e-5, 25.0, 2500.0);
        m.sigmaY = 3.0e4;
        return m;
    }

    static Material makeConcreteC40(MaterialId mid = 5) {
        Material m(mid, "Concreto C40", 3.5e7, 0.20, 1.0e-5, 25.0, 2500.0);
        m.sigmaY = 4.0e4;
        return m;
    }

    static Material makeAluminum6061(MaterialId mid = 3) {
        Material m(mid, "Alumínio 6061-T6", 7.0e7, 0.33, 2.3e-5, 27.0, 2700.0);
        m.sigmaY = 2.4e5;
        m.H_iso = 1.5e6;
        return m;
    }

    static Material makeTimber(MaterialId mid = 4) {
        Material m(mid, "Madeira Estrutural", 1.45e7, 0.20, 0.5e-5, 8.0, 800.0);
        m.sigmaY = 3.0e4;
        return m;
    }
};

} // namespace omniframe

