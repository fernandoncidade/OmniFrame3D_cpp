#pragma once

#include "Types.hpp"
#include "kernel/geometry/CrossSectionGeometry.hpp"
#include <string>
#include <vector>

namespace omniframe {

class CrossSection {
public:
    SectionId id = INVALID_ID;
    std::string name = "Seção Padrão";
    SectionShape shape = SectionShape::Rectangular;

    // Dimensões geométricas (metros)
    double dim1 = 0.20;
    double dim2 = 0.40;
    double dim3 = 0.01;
    double dim4 = 0.01;
    double dim5 = 0.165;
    double dim6 = 0.0102;

    // Propriedades calculadas
    double A = 0.08;          // Área da seção transversal (m²)
    double Iy = 1.0667e-3;    // Momento de inércia em torno do eixo local y (m⁴)
    double Iz = 2.6667e-4;    // Momento de inércia em torno do eixo local z (m⁴)
    double J = 7.14e-4;       // Constante de torção de Saint-Venant (m⁴)
    double Iyz = 0.0;         // Produto de inércia (m⁴)
    double I1 = 1.0667e-3;    // Inércia principal máxima (m⁴)
    double I2 = 2.6667e-4;    // Inércia principal mínima (m⁴)
    double principalAngle = 0.0; // Ângulo principal (rad)
    double Cw = 0.0;          // Constante de empenamento de Vlasov (m⁶)
    double Asy = 0.066667;    // Área efetiva de cisalhamento segundo y (m²)
    double Asz = 0.066667;    // Área efetiva de cisalhamento segundo z (m²)

    std::vector<kernel::PolygonLoop2D> polygonLoops;
    std::vector<kernel::CompositeRectPart> compositeParts;

    CrossSection() = default;

    CrossSection(SectionId sid, std::string sname, SectionShape sshape,
                 double d1 = 0.20, double d2 = 0.40, double d3 = 0.01, double d4 = 0.01,
                 double d5 = 0.165, double d6 = 0.0102)
        : id(sid), name(std::move(sname)), shape(sshape),
          dim1(d1), dim2(d2), dim3(d3), dim4(d4), dim5(d5), dim6(d6) {
        computeProperties();
    }

    void computeProperties();

    // Presets úteis
    static CrossSection makeRectangular(SectionId sid, std::string name, double b, double h);
    static CrossSection makeCircularSolid(SectionId sid, std::string name, double D);
    static CrossSection makeCircularTube(SectionId sid, std::string name, double Dext, double t);
    static CrossSection makeIShape(SectionId sid, std::string name, double h, double bf, double tf, double tw);
    static CrossSection makeBoxRHS(SectionId sid, std::string name, double h, double b, double tf, double tw);
    static CrossSection makeChannelC(SectionId sid, std::string name, double h, double bf, double tf, double tw);
    static CrossSection makeAngleL(SectionId sid, std::string name, double h, double b, double t);
    static CrossSection makeTShape(SectionId sid, std::string name, double h, double bf, double tf, double tw);
    static CrossSection makeWeldedIShape(SectionId sid, std::string name, double h, double bfTop, double tfTop, double bfBot, double tfBot, double tw);
    static CrossSection makePolygon(SectionId sid, std::string name, std::vector<kernel::PolygonLoop2D> loops);
    static CrossSection makeComposite(SectionId sid, std::string name, std::vector<kernel::CompositeRectPart> parts);
    static CrossSection makeGeneric(SectionId sid, std::string name, double a, double iy, double iz, double j,
                                    double asy = -1.0, double asz = -1.0, double cw = 0.0);
};

} // namespace omniframe

