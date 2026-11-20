#pragma once

#include "Types.hpp"
#include <string>

namespace omniframe {

class CrossSection {
public:
    SectionId id = INVALID_ID;
    std::string name = "Seção Padrão";
    SectionShape shape = SectionShape::Rectangular;

    // Dimensões geométricas (metros)
    // Retangular: dim1 = b (largura em y), dim2 = h (altura em z)
    // Circular: dim1 = D
    // Tubular: dim1 = D_ext, dim2 = espessura t
    // Perfil I: dim1 = h, dim2 = b_f, dim3 = t_f, dim4 = t_w
    double dim1 = 0.20;
    double dim2 = 0.40;
    double dim3 = 0.01;
    double dim4 = 0.01;

    // Propriedades calculadas
    double A = 0.08;     // Área da seção transversal (m²)
    double Iy = 1.0667e-3; // Momento de inércia em torno do eixo local y (m⁴)
    double Iz = 2.6667e-4; // Momento de inércia em torno do eixo local z (m⁴)
    double J = 7.14e-4;    // Constante de torção de Saint-Venant (m⁴)

    CrossSection() = default;

    CrossSection(SectionId sid, std::string sname, SectionShape sshape,
                 double d1 = 0.20, double d2 = 0.40, double d3 = 0.01, double d4 = 0.01)
        : id(sid), name(std::move(sname)), shape(sshape),
          dim1(d1), dim2(d2), dim3(d3), dim4(d4) {
        computeProperties();
    }

    void computeProperties();

    // Presets úteis
    static CrossSection makeRectangular(SectionId sid, std::string name, double b, double h);
    static CrossSection makeCircularSolid(SectionId sid, std::string name, double D);
    static CrossSection makeCircularTube(SectionId sid, std::string name, double Dext, double t);
    static CrossSection makeIShape(SectionId sid, std::string name, double h, double bf, double tf, double tw);
    static CrossSection makeGeneric(SectionId sid, std::string name, double a, double iy, double iz, double j);
};

} // namespace omniframe

