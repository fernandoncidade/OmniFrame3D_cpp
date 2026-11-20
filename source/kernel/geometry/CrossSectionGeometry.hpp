#pragma once

#include "../core/KernelTypes.hpp"
#include <array>
#include <vector>

namespace omniframe::kernel {

enum class KernelSectionType : std::uint8_t {
    Rectangular = 0,
    CircularSolid = 1,
    CircularTube = 2,
    IShape = 3,
    Generic = 4,
    BoxRHS = 5,
    ChannelC = 6,
    AngleL = 7,
    TShape = 8,
    WeldedIShape = 9,
    Polygon = 10,
    Composite = 11
};

struct PolygonLoop2D {
    std::vector<std::array<double, 2>> vertices; // (y, z) no plano da seção
    bool isHole = false;
};

struct CompositeRectPart {
    double yCenter = 0.0;
    double zCenter = 0.0;
    double widthB = 0.1;   // dimensão ao longo de y local
    double heightH = 0.1;  // dimensão ao longo de z local
    double modularRatioN = 1.0; // razão modular E_i / E_ref para seções mistas
};

struct KernelSectionProps {
    double A = 0.0;
    double yc = 0.0;
    double zc = 0.0;
    double Iy = 0.0;   // flexão em torno de y local (altura em z)
    double Iz = 0.0;   // flexão em torno de z local (largura em y)
    double Iyz = 0.0;  // produto de inércia
    double I1 = 0.0;   // momento principal máximo
    double I2 = 0.0;   // momento principal mínimo
    double principalAngleRad = 0.0;
    double J = 0.0;    // constante de torção de Saint-Venant
    double Cw = 0.0;   // constante de empenamento de Vlasov (m^6)
    double Asy = 0.0;  // área efetiva de cisalhamento segundo y local
    double Asz = 0.0;  // área efetiva de cisalhamento segundo z local
    double WyPos = 0.0;
    double WyNeg = 0.0;
    double WzPos = 0.0;
    double WzNeg = 0.0;
};

class CrossSectionGeometry {
public:
    static void finalizePrincipalAxes(KernelSectionProps& props);

    static KernelSectionProps computeRectangular(double b, double h);
    static KernelSectionProps computeCircularSolid(double D, double nu = 0.3);
    static KernelSectionProps computeCircularTube(double Dext, double t, double nu = 0.3);
    static KernelSectionProps computeIShape(double h, double bf, double tf, double tw);
    static KernelSectionProps computeWeldedIShape(double h, double bfTop, double tfTop, double bfBot, double tfBot, double tw);
    static KernelSectionProps computeBoxRHS(double h, double b, double tf, double tw);
    static KernelSectionProps computeChannelC(double h, double bf, double tf, double tw);
    static KernelSectionProps computeAngleL(double h, double b, double t);
    static KernelSectionProps computeTShape(double h, double bf, double tf, double tw);
    static KernelSectionProps computePolygon(const std::vector<PolygonLoop2D>& loops);
    static KernelSectionProps computeComposite(const std::vector<CompositeRectPart>& parts);
};

} // namespace omniframe::kernel
