#include "CrossSection.hpp"
#include <algorithm>
#include <cmath>

namespace omniframe {

void CrossSection::computeProperties() {
    kernel::KernelSectionProps p;
    bool computed = true;

    switch (shape) {
        case SectionShape::Rectangular:
            p = kernel::CrossSectionGeometry::computeRectangular(dim1, dim2);
            break;
        case SectionShape::CircularSolid:
            p = kernel::CrossSectionGeometry::computeCircularSolid(dim1);
            break;
        case SectionShape::CircularTube:
            if (dim1 > 0.0 && dim2 >= 0.5 * dim1) dim2 = 0.45 * dim1;
            p = kernel::CrossSectionGeometry::computeCircularTube(dim1, dim2);
            break;
        case SectionShape::IShape:
            if (dim1 > 0.0 && 2.0 * dim3 >= dim1) dim3 = 0.45 * dim1;
            if (dim2 > 0.0 && dim4 >= dim2) dim4 = 0.90 * dim2;
            p = kernel::CrossSectionGeometry::computeIShape(dim1, dim2, dim3, dim4);
            break;
        case SectionShape::BoxRHS:
            if (dim1 > 0.0 && 2.0 * dim3 >= dim1) dim3 = 0.45 * dim1;
            if (dim2 > 0.0 && 2.0 * dim4 >= dim2) dim4 = 0.45 * dim2;
            p = kernel::CrossSectionGeometry::computeBoxRHS(dim1, dim2, dim3, dim4);
            break;
        case SectionShape::ChannelC:
            if (dim1 > 0.0 && 2.0 * dim3 >= dim1) dim3 = 0.45 * dim1;
            if (dim2 > 0.0 && dim4 >= dim2) dim4 = 0.90 * dim2;
            p = kernel::CrossSectionGeometry::computeChannelC(dim1, dim2, dim3, dim4);
            break;
        case SectionShape::AngleL:
            if (dim1 > 0.0 && dim2 > 0.0 && dim3 >= std::min(dim1, dim2)) {
                dim3 = 0.90 * std::min(dim1, dim2);
            }
            p = kernel::CrossSectionGeometry::computeAngleL(dim1, dim2, dim3);
            break;
        case SectionShape::TShape:
            if (dim1 > 0.0 && dim3 >= dim1) dim3 = 0.90 * dim1;
            if (dim2 > 0.0 && dim4 >= dim2) dim4 = 0.90 * dim2;
            p = kernel::CrossSectionGeometry::computeTShape(dim1, dim2, dim3, dim4);
            break;
        case SectionShape::WeldedIShape:
            if (dim1 > 0.0 && (dim3 + dim6) >= dim1) {
                dim3 = 0.45 * dim1;
                dim6 = 0.45 * dim1;
            }
            if (dim2 > 0.0 && dim5 > 0.0 && dim4 >= std::min(dim2, dim5)) {
                dim4 = 0.90 * std::min(dim2, dim5);
            }
            p = kernel::CrossSectionGeometry::computeWeldedIShape(dim1, dim2, dim3, dim5, dim6, dim4);
            break;
        case SectionShape::Polygon:
            p = kernel::CrossSectionGeometry::computePolygon(polygonLoops);
            if (p.A <= 1e-12) computed = false;
            break;
        case SectionShape::Composite:
            p = kernel::CrossSectionGeometry::computeComposite(compositeParts);
            if (p.A <= 1e-12) computed = false;
            break;
        case SectionShape::Generic:
        default:
            computed = false;
            if (Asy <= 0.0) Asy = (5.0 / 6.0) * std::max(A, 1e-9);
            if (Asz <= 0.0) Asz = (5.0 / 6.0) * std::max(A, 1e-9);
            kernel::KernelSectionProps gp;
            gp.Iy = Iy; gp.Iz = Iz; gp.Iyz = Iyz;
            kernel::CrossSectionGeometry::finalizePrincipalAxes(gp);
            I1 = gp.I1;
            I2 = gp.I2;
            principalAngle = gp.principalAngleRad;
            break;
    }

    if (computed) {
        A = p.A;
        Iy = p.Iy;
        Iz = p.Iz;
        Iyz = p.Iyz;
        I1 = p.I1;
        I2 = p.I2;
        principalAngle = p.principalAngleRad;
        J = p.J;
        Cw = p.Cw;
        Asy = p.Asy;
        Asz = p.Asz;
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

CrossSection CrossSection::makeBoxRHS(SectionId sid, std::string name, double h, double b, double tf, double tw) {
    return CrossSection(sid, std::move(name), SectionShape::BoxRHS, h, b, tf, tw);
}

CrossSection CrossSection::makeChannelC(SectionId sid, std::string name, double h, double bf, double tf, double tw) {
    return CrossSection(sid, std::move(name), SectionShape::ChannelC, h, bf, tf, tw);
}

CrossSection CrossSection::makeAngleL(SectionId sid, std::string name, double h, double b, double t) {
    return CrossSection(sid, std::move(name), SectionShape::AngleL, h, b, t);
}

CrossSection CrossSection::makeTShape(SectionId sid, std::string name, double h, double bf, double tf, double tw) {
    return CrossSection(sid, std::move(name), SectionShape::TShape, h, bf, tf, tw);
}

CrossSection CrossSection::makeWeldedIShape(SectionId sid, std::string name, double h, double bfTop, double tfTop,
                                            double bfBot, double tfBot, double tw) {
    return CrossSection(sid, std::move(name), SectionShape::WeldedIShape, h, bfTop, tfTop, tw, bfBot, tfBot);
}

CrossSection CrossSection::makePolygon(SectionId sid, std::string name, std::vector<kernel::PolygonLoop2D> loops) {
    CrossSection cs(sid, std::move(name), SectionShape::Polygon);
    cs.polygonLoops = std::move(loops);
    cs.computeProperties();
    return cs;
}

CrossSection CrossSection::makeComposite(SectionId sid, std::string name, std::vector<kernel::CompositeRectPart> parts) {
    CrossSection cs(sid, std::move(name), SectionShape::Composite);
    cs.compositeParts = std::move(parts);
    cs.computeProperties();
    return cs;
}

CrossSection CrossSection::makeGeneric(SectionId sid, std::string name, double a, double iy, double iz, double j,
                                       double asy, double asz, double cw) {
    CrossSection cs(sid, std::move(name), SectionShape::Generic);
    cs.A = a;
    cs.Iy = iy;
    cs.Iz = iz;
    cs.J = j;
    cs.Asy = (asy > 0.0) ? asy : (5.0 / 6.0) * a;
    cs.Asz = (asz > 0.0) ? asz : (5.0 / 6.0) * a;
    cs.Cw = cw;
    cs.computeProperties();
    return cs;
}

} // namespace omniframe
