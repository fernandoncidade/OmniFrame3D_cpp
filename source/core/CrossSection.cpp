#include "CrossSection.hpp"
#include <algorithm>
#include <cmath>

namespace omniframe {

void CrossSection::computeProperties() {
    switch (shape) {
        case SectionShape::Rectangular: {
            double b = std::max(dim1, 1e-4);
            double h = std::max(dim2, 1e-4);
            A = b * h;
            Iy = (b * std::pow(h, 3)) / 12.0; // flexão em x-z em torno de y
            Iz = (h * std::pow(b, 3)) / 12.0; // flexão em x-y em torno de z
            double a = std::max(b, h);
            double bs = std::min(b, h);
            double ratio = bs / a;
            J = a * std::pow(bs, 3) * (1.0 / 3.0 - 0.21 * ratio * (1.0 - std::pow(ratio, 4) / 12.0));
            break;
        }

        case SectionShape::CircularSolid: {
            double D = std::max(dim1, 1e-4);
            A = (PI * D * D) / 4.0;
            Iy = (PI * std::pow(D, 4)) / 64.0;
            Iz = Iy;
            J = (PI * std::pow(D, 4)) / 32.0;
            break;
        }

        case SectionShape::CircularTube: {
            double Dext = std::max(dim1, 1e-4);
            double t = std::clamp(dim2, 1e-5, Dext * 0.49);
            double Dint = Dext - 2.0 * t;
            A = (PI / 4.0) * (Dext * Dext - Dint * Dint);
            Iy = (PI / 64.0) * (std::pow(Dext, 4) - std::pow(Dint, 4));
            Iz = Iy;
            J = (PI / 32.0) * (std::pow(Dext, 4) - std::pow(Dint, 4));
            break;
        }

        case SectionShape::IShape: {
            double h = std::max(dim1, 1e-4);
            double bf = std::max(dim2, 1e-4);
            double tf = std::clamp(dim3, 1e-5, h * 0.45);
            double tw = std::clamp(dim4, 1e-5, bf * 0.45);
            double hw = h - 2.0 * tf;
            A = 2.0 * bf * tf + hw * tw;
            // Eixo forte y (altura h ao longo de z):
            Iy = (bf * std::pow(h, 3) - (bf - tw) * std::pow(hw, 3)) / 12.0;
            // Eixo fraco z (largura bf ao longo de y):
            Iz = (2.0 * tf * std::pow(bf, 3) + hw * std::pow(tw, 3)) / 12.0;
            // Torção livre de Saint-Venant aproximada para perfis abertos de parede fina:
            J = (2.0 * bf * std::pow(tf, 3) + hw * std::pow(tw, 3)) / 3.0;
            break;
        }

        case SectionShape::Generic:
            // Mantém os valores inseridos manualmente pelo usuário
            break;
    }
}

CrossSection CrossSection::makeRectangular(SectionId sid, std::string name, double b, double h) {
    return CrossSection(sid, std::move(name), SectionShape::Rectangular, b, h);
}

CrossSection CrossSection::makeCircularSolid(SectionId sid, std::string name, double D) {
    return CrossSection(sid, std::move(name), SectionShape::CircularSolid, D);
}

CrossSection CrossSection::makeCircularTube(SectionId sid, std::string name, double Dext, double t) {
    return CrossSection(sid, std::move(name), SectionShape::CircularTube, Dext, t);
}

CrossSection CrossSection::makeIShape(SectionId sid, std::string name, double h, double bf, double tf, double tw) {
    return CrossSection(sid, std::move(name), SectionShape::IShape, h, bf, tf, tw);
}

CrossSection CrossSection::makeGeneric(SectionId sid, std::string name, double a, double iy, double iz, double j) {
    CrossSection cs(sid, std::move(name), SectionShape::Generic);
    cs.A = a;
    cs.Iy = iy;
    cs.Iz = iz;
    cs.J = j;
    return cs;
}

} // namespace omniframe

