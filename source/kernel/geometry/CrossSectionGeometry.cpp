#include "CrossSectionGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace omniframe::kernel {

namespace {
constexpr double kPi = std::numbers::pi;
}

void CrossSectionGeometry::finalizePrincipalAxes(KernelSectionProps& p) {
    const double avg = 0.5 * (p.Iy + p.Iz);
    const double diff = 0.5 * (p.Iy - p.Iz);
    const double R = std::sqrt(diff * diff + p.Iyz * p.Iyz);
    p.I1 = avg + R;
    p.I2 = std::max(1e-18, avg - R);
    if (std::abs(p.Iyz) < 1e-18 && std::abs(p.Iy - p.Iz) < 1e-18) {
        p.principalAngleRad = 0.0;
    } else {
        p.principalAngleRad = 0.5 * std::atan2(-2.0 * p.Iyz, p.Iy - p.Iz);
    }
}

KernelSectionProps CrossSectionGeometry::computeRectangular(double b, double h) {
    b = std::max(b, 1e-5);
    h = std::max(h, 1e-5);
    KernelSectionProps p;
    p.A = b * h;
    p.Iy = (b * std::pow(h, 3)) / 12.0;
    p.Iz = (h * std::pow(b, 3)) / 12.0;

    // Aproximação precisa de Timoshenko / Roark para torção de retângulo sólido
    const double longSide = std::max(b, h);
    const double shortSide = std::min(b, h);
    const double ratio = shortSide / longSide;
    const double beta = (1.0 / 3.0) - 0.21 * ratio * (1.0 - std::pow(ratio, 4) / 12.0);
    p.J = beta * longSide * std::pow(shortSide, 3);

    // Áreas de cisalhamento de Mindlin/Timoshenko k_s = 5/6
    p.Asy = (5.0 / 6.0) * p.A;
    p.Asz = (5.0 / 6.0) * p.A;
    p.Cw = 0.0;
    p.WyPos = p.WyNeg = p.Iy / (0.5 * h);
    p.WzPos = p.WzNeg = p.Iz / (0.5 * b);
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeCircularSolid(double D, double nu) {
    D = std::max(D, 1e-5);
    const double r = 0.5 * D;
    KernelSectionProps p;
    p.A = kPi * r * r;
    p.Iy = 0.25 * kPi * std::pow(r, 4);
    p.Iz = p.Iy;
    p.J = 0.5 * kPi * std::pow(r, 4);
    const double ks = (6.0 * (1.0 + nu)) / (7.0 + 6.0 * nu); // Cowper (1966)
    p.Asy = ks * p.A;
    p.Asz = ks * p.A;
    p.WyPos = p.WyNeg = p.Iy / r;
    p.WzPos = p.WzNeg = p.Iz / r;
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeCircularTube(double Dext, double t, double nu) {
    Dext = std::max(Dext, 1e-4);
    t = std::clamp(t, 1e-5, 0.499 * Dext);
    const double ro = 0.5 * Dext;
    const double ri = ro - t;
    const double m = ri / ro;
    const double m2 = m * m;
    KernelSectionProps p;
    p.A = kPi * (ro * ro - ri * ri);
    p.Iy = 0.25 * kPi * (std::pow(ro, 4) - std::pow(ri, 4));
    p.Iz = p.Iy;
    p.J = 0.5 * kPi * (std::pow(ro, 4) - std::pow(ri, 4));

    // Cowper (1966) para tubo circular oco
    const double m2p = 1.0 + m2;
    const double ks = (6.0 * (1.0 + nu) * m2p * m2p) /
                      ((7.0 + 6.0 * nu) * m2p * m2p + (20.0 + 12.0 * nu) * m2);
    p.Asy = ks * p.A;
    p.Asz = ks * p.A;
    p.WyPos = p.WyNeg = p.Iy / ro;
    p.WzPos = p.WzNeg = p.Iz / ro;
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeIShape(double h, double bf, double tf, double tw) {
    h = std::max(h, 1e-4);
    bf = std::max(bf, 1e-4);
    tf = std::clamp(tf, 1e-5, 0.45 * h);
    tw = std::clamp(tw, 1e-5, bf);
    const double hw = std::max(1e-5, h - 2.0 * tf);
    const double h0 = h - tf;

    KernelSectionProps p;
    p.A = 2.0 * bf * tf + hw * tw;
    p.Iy = (bf * std::pow(h, 3) - (bf - tw) * std::pow(hw, 3)) / 12.0;
    p.Iz = (2.0 * tf * std::pow(bf, 3) + hw * std::pow(tw, 3)) / 12.0;
    p.J = (2.0 * bf * std::pow(tf, 3) + hw * std::pow(tw, 3)) / 3.0;
    p.Cw = (p.Iz * h0 * h0) * 0.25;
    p.Asz = h * tw;                      // alma resiste cortante Vz (norma / prática)
    p.Asy = (5.0 / 6.0) * (2.0 * bf * tf); // mesas resistem cortante Vy
    p.WyPos = p.WyNeg = p.Iy / (0.5 * h);
    p.WzPos = p.WzNeg = p.Iz / (0.5 * bf);
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeWeldedIShape(double h, double bfTop, double tfTop, double bfBot, double tfBot, double tw) {
    h = std::max(h, 1e-4);
    bfTop = std::max(bfTop, 1e-4);
    bfBot = std::max(bfBot, 1e-4);
    tfTop = std::clamp(tfTop, 1e-5, 0.45 * h);
    tfBot = std::clamp(tfBot, 1e-5, 0.45 * h);
    tw = std::clamp(tw, 1e-5, std::min(bfTop, bfBot));
    const double hw = std::max(1e-5, h - tfTop - tfBot);

    const double Atop = bfTop * tfTop;
    const double Abot = bfBot * tfBot;
    const double Aweb = hw * tw;

    const double zBotC = 0.5 * tfBot;
    const double zWebC = tfBot + 0.5 * hw;
    const double zTopC = h - 0.5 * tfTop;

    KernelSectionProps p;
    p.A = Atop + Abot + Aweb;
    p.zc = (Abot * zBotC + Aweb * zWebC + Atop * zTopC) / p.A;

    auto steiner = [&](double A_i, double Ilocal, double z_i) {
        const double d = z_i - p.zc;
        return Ilocal + A_i * d * d;
    };
    p.Iy = steiner(Abot, bfBot * std::pow(tfBot, 3) / 12.0, zBotC) +
           steiner(Aweb, tw * std::pow(hw, 3) / 12.0, zWebC) +
           steiner(Atop, bfTop * std::pow(tfTop, 3) / 12.0, zTopC);
    p.Iz = (tfBot * std::pow(bfBot, 3) + hw * std::pow(tw, 3) + tfTop * std::pow(bfTop, 3)) / 12.0;
    p.J = (bfBot * std::pow(tfBot, 3) + hw * std::pow(tw, 3) + bfTop * std::pow(tfTop, 3)) / 3.0;

    const double Izt = tfTop * std::pow(bfTop, 3) / 12.0;
    const double Izb = tfBot * std::pow(bfBot, 3) / 12.0;
    const double h0 = h - 0.5 * (tfTop + tfBot);
    p.Cw = (h0 * h0 * Izt * Izb) / std::max(Izt + Izb, 1e-18);
    p.Asz = hw * tw;
    p.Asy = (5.0 / 6.0) * (Atop + Abot);
    p.WyPos = p.Iy / std::max(h - p.zc, 1e-6);
    p.WyNeg = p.Iy / std::max(p.zc, 1e-6);
    const double bMax = 0.5 * std::max(bfTop, bfBot);
    p.WzPos = p.WzNeg = p.Iz / bMax;
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeBoxRHS(double h, double b, double tf, double tw) {
    h = std::max(h, 1e-4);
    b = std::max(b, 1e-4);
    tf = std::clamp(tf, 1e-5, 0.45 * h);
    tw = std::clamp(tw, 1e-5, 0.45 * b);
    const double hi = std::max(1e-5, h - 2.0 * tf);
    const double bi = std::max(1e-5, b - 2.0 * tw);

    KernelSectionProps p;
    p.A = b * h - bi * hi;
    p.Iy = (b * std::pow(h, 3) - bi * std::pow(hi, 3)) / 12.0;
    p.Iz = (h * std::pow(b, 3) - hi * std::pow(bi, 3)) / 12.0;

    // Fórmula fechada de Bredt para seção caixão unicelular de paredes finas
    const double hm = h - tf;
    const double bm = b - tw;
    const double Am = bm * hm;
    const double denom = 2.0 * (bm / tf + hm / tw);
    p.J = (4.0 * Am * Am) / std::max(denom, 1e-18);
    p.Asz = 2.0 * hi * tw;
    p.Asy = 2.0 * bi * tf;
    p.WyPos = p.WyNeg = p.Iy / (0.5 * h);
    p.WzPos = p.WzNeg = p.Iz / (0.5 * b);
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeChannelC(double h, double bf, double tf, double tw) {
    h = std::max(h, 1e-4);
    bf = std::max(bf, 1e-4);
    tf = std::clamp(tf, 1e-5, 0.45 * h);
    tw = std::clamp(tw, 1e-5, 0.9 * bf);
    const double hw = std::max(1e-5, h - 2.0 * tf);

    const double Aweb = h * tw;
    const double Afl = (bf - tw) * tf;
    const double yWeb = 0.5 * tw;
    const double yFl = tw + 0.5 * (bf - tw);

    KernelSectionProps p;
    p.A = Aweb + 2.0 * Afl;
    p.yc = (Aweb * yWeb + 2.0 * Afl * yFl) / p.A;

    p.Iy = (tw * std::pow(h, 3)) / 12.0 +
           2.0 * (((bf - tw) * std::pow(tf, 3)) / 12.0 + Afl * std::pow(0.5 * (h - tf), 2));
    p.Iz = (h * std::pow(tw, 3)) / 12.0 + Aweb * std::pow(yWeb - p.yc, 2) +
           2.0 * ((tf * std::pow(bf - tw, 3)) / 12.0 + Afl * std::pow(yFl - p.yc, 2));
    p.J = (2.0 * bf * std::pow(tf, 3) + hw * std::pow(tw, 3)) / 3.0;

    // Constante de empenamento aproximada clássica para perfil U com mesas iguais
    const double h0 = h - tf;
    const double b0 = bf - 0.5 * tw;
    p.Cw = (tf * std::pow(b0, 3) * h0 * h0 / 12.0) *
           ((3.0 * b0 * tf + 2.0 * h0 * tw) / std::max(6.0 * b0 * tf + h0 * tw, 1e-18));

    p.Asz = hw * tw;
    p.Asy = (5.0 / 6.0) * (2.0 * bf * tf);
    p.WyPos = p.WyNeg = p.Iy / (0.5 * h);
    p.WzPos = p.Iz / std::max(bf - p.yc, 1e-6);
    p.WzNeg = p.Iz / std::max(p.yc, 1e-6);
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeAngleL(double h, double b, double t) {
    h = std::max(h, 1e-4);
    b = std::max(b, 1e-4);
    t = std::clamp(t, 1e-5, 0.9 * std::min(h, b));

    // Perna vertical (largura t, altura h em z:[0,h], y:[0,t]) e perna horizontal (y:[t,b], z:[0,t])
    const double A1 = t * h;
    const double y1 = 0.5 * t;
    const double z1 = 0.5 * h;

    const double A2 = (b - t) * t;
    const double y2 = t + 0.5 * (b - t);
    const double z2 = 0.5 * t;

    KernelSectionProps p;
    p.A = A1 + A2;
    p.yc = (A1 * y1 + A2 * y2) / p.A;
    p.zc = (A1 * z1 + A2 * z2) / p.A;

    const double dy1 = y1 - p.yc, dz1 = z1 - p.zc;
    const double dy2 = y2 - p.yc, dz2 = z2 - p.zc;

    p.Iy = (t * std::pow(h, 3)) / 12.0 + A1 * dz1 * dz1 +
           ((b - t) * std::pow(t, 3)) / 12.0 + A2 * dz2 * dz2;
    p.Iz = (h * std::pow(t, 3)) / 12.0 + A1 * dy1 * dy1 +
           (t * std::pow(b - t, 3)) / 12.0 + A2 * dy2 * dy2;
    p.Iyz = A1 * dy1 * dz1 + A2 * dy2 * dz2;
    p.J = ((h + b - t) * std::pow(t, 3)) / 3.0;
    p.Asz = A1;
    p.Asy = A2 + t * t;
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeTShape(double h, double bf, double tf, double tw) {
    h = std::max(h, 1e-4);
    bf = std::max(bf, 1e-4);
    tf = std::clamp(tf, 1e-5, 0.9 * h);
    tw = std::clamp(tw, 1e-5, bf);
    const double hw = std::max(1e-5, h - tf);

    const double Afl = bf * tf;
    const double Aweb = tw * hw;
    const double zWeb = 0.5 * hw;
    const double zFl = hw + 0.5 * tf;

    KernelSectionProps p;
    p.A = Afl + Aweb;
    p.zc = (Aweb * zWeb + Afl * zFl) / p.A;
    p.Iy = (tw * std::pow(hw, 3)) / 12.0 + Aweb * std::pow(zWeb - p.zc, 2) +
           (bf * std::pow(tf, 3)) / 12.0 + Afl * std::pow(zFl - p.zc, 2);
    p.Iz = (hw * std::pow(tw, 3) + tf * std::pow(bf, 3)) / 12.0;
    p.J = (bf * std::pow(tf, 3) + hw * std::pow(tw, 3)) / 3.0;
    p.Asz = hw * tw;
    p.Asy = (5.0 / 6.0) * Afl;
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computePolygon(const std::vector<PolygonLoop2D>& loops) {
    KernelSectionProps p;
    double sumA = 0.0;
    double sumMy = 0.0;
    double sumMz = 0.0;
    double sumIyOrigin = 0.0;
    double sumIzOrigin = 0.0;
    double sumIyzOrigin = 0.0;

    for (const auto& loop : loops) {
        const auto& pts = loop.vertices;
        const std::size_t n = pts.size();
        if (n < 3) continue;

        double A2 = 0.0, cy6 = 0.0, cz6 = 0.0;
        double iyy12 = 0.0, izz12 = 0.0, iyz24 = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& a = pts[i];
            const auto& b = pts[(i + 1) % n];
            const double y0 = a[0], z0 = a[1];
            const double y1 = b[0], z1 = b[1];
            const double cross = y0 * z1 - y1 * z0;
            A2 += cross;
            cy6 += (y0 + y1) * cross;
            cz6 += (z0 + z1) * cross;
            iyy12 += (z0 * z0 + z0 * z1 + z1 * z1) * cross;
            izz12 += (y0 * y0 + y0 * y1 + y1 * y1) * cross;
            iyz24 += (y0 * z1 + 2.0 * y0 * z0 + 2.0 * y1 * z1 + y1 * z0) * cross;
        }

        // Garante sinal consistente: sólido sempre positivo, furo sempre negativo
        const double sgnRaw = (A2 >= 0.0) ? 1.0 : -1.0;
        const double factor = (loop.isHole ? -1.0 : 1.0) * sgnRaw;

        sumA += factor * 0.5 * A2;
        sumMz += factor * (cy6 / 6.0);  // integral y dA
        sumMy += factor * (cz6 / 6.0);  // integral z dA
        sumIyOrigin += factor * (iyy12 / 12.0);
        sumIzOrigin += factor * (izz12 / 12.0);
        sumIyzOrigin += factor * (iyz24 / 24.0);
    }

    if (sumA <= 1e-12) return p;
    p.A = sumA;
    p.yc = sumMz / p.A;
    p.zc = sumMy / p.A;
    p.Iy = std::max(1e-18, sumIyOrigin - p.A * p.zc * p.zc);
    p.Iz = std::max(1e-18, sumIzOrigin - p.A * p.yc * p.yc);
    p.Iyz = sumIyzOrigin - p.A * p.yc * p.zc;

    // Aproximação padrão de Saint-Venant de Barré para seções compactas sólidas: J = A^4 / (40 * (Iy + Iz))
    const double Ip = p.Iy + p.Iz;
    p.J = (Ip > 1e-18) ? std::pow(p.A, 4) / (40.0 * Ip) : 0.0;
    p.Asy = (5.0 / 6.0) * p.A;
    p.Asz = (5.0 / 6.0) * p.A;
    finalizePrincipalAxes(p);
    return p;
}

KernelSectionProps CrossSectionGeometry::computeComposite(const std::vector<CompositeRectPart>& parts) {
    KernelSectionProps p;
    double sumA = 0.0, sumAy = 0.0, sumAz = 0.0;
    double Jsum = 0.0;

    for (const auto& pt : parts) {
        const double Ai = pt.modularRatioN * pt.widthB * pt.heightH;
        sumA += Ai;
        sumAy += Ai * pt.yCenter;
        sumAz += Ai * pt.zCenter;
        const double ls = std::max(pt.widthB, pt.heightH);
        const double ss = std::min(pt.widthB, pt.heightH);
        const double r = ss / std::max(ls, 1e-12);
        const double beta = (1.0 / 3.0) - 0.21 * r * (1.0 - std::pow(r, 4) / 12.0);
        Jsum += pt.modularRatioN * beta * ls * std::pow(ss, 3);
    }
    if (sumA <= 1e-12) return p;

    p.A = sumA;
    p.yc = sumAy / sumA;
    p.zc = sumAz / sumA;

    for (const auto& pt : parts) {
        const double Ai = pt.modularRatioN * pt.widthB * pt.heightH;
        const double dy = pt.yCenter - p.yc;
        const double dz = pt.zCenter - p.zc;
        p.Iy += pt.modularRatioN * (pt.widthB * std::pow(pt.heightH, 3)) / 12.0 + Ai * dz * dz;
        p.Iz += pt.modularRatioN * (pt.heightH * std::pow(pt.widthB, 3)) / 12.0 + Ai * dy * dy;
        p.Iyz += Ai * dy * dz;
    }
    p.J = Jsum;
    p.Asy = (5.0 / 6.0) * p.A;
    p.Asz = (5.0 / 6.0) * p.A;
    finalizePrincipalAxes(p);
    return p;
}

} // namespace omniframe::kernel
