#include "NbrCodeChecks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <utility>

namespace omniframe::kernel::codes {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct SteelSectionGeom {
    double Ag = 0.0;
    double Ix = 0.0;
    double Iy = 0.0;
    double Wx = 0.0;
    double Wy = 0.0;
    double Zx = 0.0;
    double Zy = 0.0;
    double J = 0.0;
    double Cw = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double r0 = 0.0;
    double hFlat = 0.0;  ///< Altura plana da alma
    double bFlange = 0.0;///< Largura de calculo FLM/local (half-flange para I, parte plana para RHS)
    double tf = 0.0;
    double tw = 0.0;
};

SteelSectionGeom computeSteelGeom(const SteelSectionSpec& s) {
    SteelSectionGeom g{};
    const double d = std::max(1e-4, s.d_m);
    const double bf = std::max(1e-4, s.bf_m);
    const double tf = std::max(1e-5, s.tf_m);
    const double tw = std::max(1e-5, s.tw_m);

    switch (s.kind) {
    case SteelSectionKind::IShapeRolled:
    case SteelSectionKind::IShapeWelded: {
        const double hw = std::max(1e-4, d - 2.0 * tf);
        g.Ag = 2.0 * bf * tf + hw * tw;
        g.Ix = (bf * std::pow(d, 3) - (bf - tw) * std::pow(hw, 3)) / 12.0;
        g.Iy = (2.0 * tf * std::pow(bf, 3) + hw * std::pow(tw, 3)) / 12.0;
        g.Wx = 2.0 * g.Ix / d;
        g.Wy = 2.0 * g.Iy / bf;
        g.Zx = bf * tf * (d - tf) + 0.25 * tw * hw * hw;
        g.Zy = 0.5 * tf * bf * bf + 0.25 * hw * tw * tw;
        g.J = (2.0 * bf * std::pow(tf, 3) + hw * std::pow(tw, 3)) / 3.0;
        const double hCent = d - tf;
        // NBR 8800 D.2.8-a): Cw = Iy * (d - tf)^2 / 4 para secao I duplamente simetrica
        g.Cw = g.Iy * hCent * hCent * 0.25;
        g.hFlat = std::max(1e-4, hw - 2.0 * s.fillet_m);
        g.bFlange = 0.5 * bf;
        g.tf = tf;
        g.tw = tw;
        break;
    }
    case SteelSectionKind::RectangularTube: {
        const double bi = std::max(1e-4, bf - 2.0 * tw);
        const double di = std::max(1e-4, d - 2.0 * tf);
        g.Ag = bf * d - bi * di;
        g.Ix = (bf * std::pow(d, 3) - bi * std::pow(di, 3)) / 12.0;
        g.Iy = (d * std::pow(bf, 3) - di * std::pow(bi, 3)) / 12.0;
        g.Wx = 2.0 * g.Ix / d;
        g.Wy = 2.0 * g.Iy / bf;
        g.Zx = (bf * d * d - bi * di * di) * 0.25;
        g.Zy = (d * bf * bf - di * bi * bi) * 0.25;
        const double Am = (bf - tw) * (d - tf);
        const double perim = 2.0 * ((bf - tw) / tf + (d - tf) / tw);
        g.J = (perim > 1e-12) ? (4.0 * Am * Am / perim) : 1e-12;
        g.Cw = 0.0;
        g.hFlat = std::max(1e-4, d - 2.0 * tf - 2.0 * s.fillet_m);
        g.bFlange = std::max(1e-4, bf - 2.0 * tw - 2.0 * s.fillet_m);
        g.tf = tf;
        g.tw = tw;
        break;
    }
    case SteelSectionKind::CircularTube: {
        const double D = d;
        const double t = std::min(tf, 0.49 * D);
        const double Di = std::max(0.0, D - 2.0 * t);
        g.Ag = 0.25 * kPi * (D * D - Di * Di);
        g.Ix = (kPi / 64.0) * (std::pow(D, 4) - std::pow(Di, 4));
        g.Iy = g.Ix;
        g.Wx = 2.0 * g.Ix / D;
        g.Wy = g.Wx;
        g.Zx = (std::pow(D, 3) - std::pow(Di, 3)) / 6.0;
        g.Zy = g.Zx;
        g.J = 2.0 * g.Ix;
        g.Cw = 0.0;
        g.hFlat = D;
        g.bFlange = D;
        g.tf = t;
        g.tw = t;
        break;
    }
    case SteelSectionKind::RectangularSolid: {
        g.Ag = bf * d;
        g.Ix = bf * std::pow(d, 3) / 12.0;
        g.Iy = d * std::pow(bf, 3) / 12.0;
        g.Wx = bf * d * d / 6.0;
        g.Wy = d * bf * bf / 6.0;
        g.Zx = bf * d * d / 4.0;
        g.Zy = d * bf * bf / 4.0;
        const double a = std::max(bf, d);
        const double b = std::min(bf, d);
        const double beta = 1.0 / 3.0 - 0.21 * (b / a) * (1.0 - std::pow(b / a, 4) / 12.0);
        g.J = beta * a * std::pow(b, 3);
        g.Cw = 0.0;
        g.hFlat = d;
        g.bFlange = bf;
        g.tf = bf;
        g.tw = bf;
        break;
    }
    case SteelSectionKind::CircularSolid: {
        const double D = d;
        g.Ag = 0.25 * kPi * D * D;
        g.Ix = (kPi / 64.0) * std::pow(D, 4);
        g.Iy = g.Ix;
        g.Wx = (kPi / 32.0) * std::pow(D, 3);
        g.Wy = g.Wx;
        g.Zx = std::pow(D, 3) / 6.0;
        g.Zy = g.Zx;
        g.J = 2.0 * g.Ix;
        g.Cw = 0.0;
        g.hFlat = D;
        g.bFlange = D;
        g.tf = D;
        g.tw = D;
        break;
    }
    }

    g.rx = std::sqrt(std::max(1e-18, g.Ix / g.Ag));
    g.ry = std::sqrt(std::max(1e-18, g.Iy / g.Ag));
    g.r0 = std::sqrt(g.rx * g.rx + g.ry * g.ry);
    return g;
}

/// Interpolacao trilinear por faixas NBR 8800:2024 Anexo D (Eqs. D.2.1/D.2.2) em MN.m.
double evalBendingBandMNm(double lambda, double lambdaP, double lambdaR,
                          double Mpl_MNm, double Mr_MNm, double Mcr_MNm) {
    if (lambda <= lambdaP) return Mpl_MNm;
    if (lambda <= lambdaR && (lambdaR > lambdaP + 1e-12)) {
        return Mpl_MNm - (Mpl_MNm - Mr_MNm) * ((lambda - lambdaP) / (lambdaR - lambdaP));
    }
    return Mcr_MNm;
}

/// Interpolacao trilinear para FLT (NBR 8800:2024 D.2.1 / Anexo E) com fator Cb e teto Mcap (Mpl ou My_e).
/// Preserva monotonicidade nao crescente em lambda e continuidade C0 em lambdaP e lambdaR (independentes de Cb).
double evalFltBandMNm(double lambda, double lambdaP, double lambdaR, double cb,
                      double Mpl_MNm, double Mr_MNm, double Mcr_MNm, double Mcap_MNm) {
    if (lambda <= lambdaP) return Mcap_MNm;
    if (lambda <= lambdaR && (lambdaR > lambdaP + 1e-12)) {
        const double lin = Mpl_MNm - (Mpl_MNm - Mr_MNm) * ((lambda - lambdaP) / (lambdaR - lambdaP));
        return std::min(Mcap_MNm, cb * lin);
    }
    return std::min(Mcap_MNm, Mcr_MNm);
}

/// Fator de cisalhamento NBR 8800:2024 Sec. 5.4.3.1.1 (kN).
double evalShearBandkN(double lambda, double lambdaP, double lambdaR, double Vpl_kN, double gammaA1) {
    if (lambda <= lambdaP) return Vpl_kN / gammaA1;
    if (lambda <= lambdaR) return (lambdaP / lambda) * (Vpl_kN / gammaA1);
    return 1.24 * std::pow(lambdaP / lambda, 2) * (Vpl_kN / gammaA1);
}

/// Largura efetiva para compressao local NBR 8800:2024 Sec. 5.3.4.2 + Tabela 5.
double effectiveWidth5_3_4_2(double b, double t, double btLim, double chi,
                             double fy_MPa, double c1, double c2) {
    if (t <= 1e-12 || b <= 1e-12) return 0.0;
    const double bt = b / t;
    const double chiSafe = std::max(1e-6, chi);
    if (bt <= btLim / std::sqrt(chiSafe)) return b;
    const double sigmaEl = std::pow(c2 * btLim / bt, 2) * fy_MPa;
    const double ratio = std::max(0.0, sigmaEl / (chiSafe * fy_MPa));
    const double sqrtRatio = std::sqrt(ratio);
    const double bef = b * (1.0 - c1 * sqrtRatio) * sqrtRatio;
    return std::clamp(bef, 0.0, b);
}

/// Calcula resposta axial (kN) e momentos (kNm) em secao retangular de concreto armado
/// dada deformacao no centroide eps0 e curvaturas (kx, ky) [1/m] seguindo NBR 6118:2023 Sec. 8.2.10.1 e 17.2.2.
/// Convencao: N > 0 = TRACAO, N < 0 = COMPRESSAO; Mx = integral(-y * sigma dA), My = integral(x * sigma dA).
struct FiberForceResult {
    double N_kN = 0.0;
    double Mx_kNm = 0.0;
    double My_kNm = 0.0;
};

struct RebarPoint {
    double x = 0.0;
    double y = 0.0;
    double area_m2 = 0.0;
};

double concreteStressMPa(double eps, double fcd_MPa, double fck_MPa) {
    // eps < 0 = encurtamento (compressao)
    if (eps >= 0.0) return 0.0;
    const double eComp = -eps; // positivo na compressao
    const double etaC = NbrCodeChecks::concreteEtaC(fck_MPa);
    const double ec2 = (fck_MPa <= 50.0)
        ? 0.0020
        : (0.0020 + 0.000085 * std::pow(fck_MPa - 50.0, 0.53));
    const double nExp = (fck_MPa <= 50.0)
        ? 2.0
        : (1.4 + 23.4 * std::pow((90.0 - std::min(90.0, fck_MPa)) / 100.0, 4));
    const double peak = 0.85 * etaC * fcd_MPa; // positivo, retornamos negativo (compressao)
    if (eComp >= ec2) return -peak;
    return -peak * (1.0 - std::pow(1.0 - eComp / ec2, nExp));
}

double steelStressMPa(double eps, double fyd_MPa, double Es_MPa = 210000.0) {
    const double s = Es_MPa * eps;
    return std::clamp(s, -fyd_MPa, fyd_MPa);
}

std::vector<RebarPoint> buildPerimeterBars(double bx_m, double hy_m, double dPrime,
                                           double totalAs_cm2, int barsPerSide) {
    const int nSide = std::max(2, barsPerSide);
    const double xMax = std::max(1e-4, 0.5 * bx_m - dPrime);
    const double yMax = std::max(1e-4, 0.5 * hy_m - dPrime);

    std::vector<std::pair<double, double>> coords;
    coords.reserve(4u * static_cast<size_t>(nSide - 1));
    for (int i = 0; i < nSide; ++i) {
        const double t = -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(nSide - 1);
        coords.emplace_back(t * xMax, yMax);  // borda superior
        coords.emplace_back(t * xMax, -yMax); // borda inferior
    }
    for (int i = 1; i + 1 < nSide; ++i) {
        const double t = -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(nSide - 1);
        coords.emplace_back(-xMax, t * yMax); // borda esquerda
        coords.emplace_back(xMax, t * yMax);  // borda direita
    }
    const double barArea_m2 = (totalAs_cm2 * 1e-4) / static_cast<double>(coords.size());
    std::vector<RebarPoint> out;
    out.reserve(coords.size());
    for (const auto& [x, y] : coords) out.push_back({x, y, barArea_m2});
    return out;
}

FiberForceResult integrateColumnFibers(double bx_m, double hy_m, double fck_MPa,
                                       double fcd_MPa, double fyd_MPa,
                                       const std::vector<RebarPoint>& bars,
                                       double eps0, double kx, double ky) {
    constexpr int kNx = 24;
    constexpr int kNy = 24;
    const double dx = bx_m / kNx;
    const double dy = hy_m / kNy;
    const double dA = dx * dy;

    FiberForceResult res{};
    for (int ix = 0; ix < kNx; ++ix) {
        const double x = -0.5 * bx_m + (ix + 0.5) * dx;
        for (int iy = 0; iy < kNy; ++iy) {
            const double y = -0.5 * hy_m + (iy + 0.5) * dy;
            const double eps = eps0 + kx * y - ky * x;
            const double sig_MPa = concreteStressMPa(eps, fcd_MPa, fck_MPa);
            if (sig_MPa == 0.0) continue;
            const double force_kN = sig_MPa * dA * 1000.0; // MPa * m^2 = MN = 1000 kN
            res.N_kN += force_kN;
            res.Mx_kNm += -y * force_kN; // y > 0 comprimida (<0) => -y*F > 0 (+Mx)
            res.My_kNm += x * force_kN;  // x < 0 comprimida (<0) => x*F > 0 (+My)
        }
    }
    for (const auto& b : bars) {
        const double eps = eps0 + kx * b.y - ky * b.x;
        const double sig_MPa = steelStressMPa(eps, fyd_MPa);
        const double force_kN = sig_MPa * b.area_m2 * 1000.0;
        res.N_kN += force_kN;
        res.Mx_kNm += -b.y * force_kN;
        res.My_kNm += b.x * force_kN;
    }
    return res;
}

/// Avalia ponto na fronteira ULS da secao para direcao de curvatura angleRad e parametro t in [0, 1].
/// t = 0 => tracao uniforme (+0.010); t = 1 => compressao uniforme (-eps_c2).
FiberForceResult evalColumnUlsState(double bx_m, double hy_m, double dPrime,
                                    double fck_MPa, double fcd_MPa, double fyd_MPa,
                                    const std::vector<RebarPoint>& bars,
                                    double angleRad, double tNorm) {
    static_cast<void>(dPrime);
    const double t = std::clamp(tNorm, 0.0, 1.0);
    const double ec2 = (fck_MPa <= 50.0)
        ? 0.0020
        : (0.0020 + 0.000085 * std::pow(fck_MPa - 50.0, 0.53));
    const double ecu = (fck_MPa <= 50.0)
        ? 0.0035
        : (0.0026 + 0.035 * std::pow((90.0 - std::min(90.0, fck_MPa)) / 100.0, 4));
    constexpr double esu = 0.010;

    // Vetor diretor normalizado da flexao no plano das deformacoes:
    // nComp = (nx, ny) aponta para o lado mais comprimido.
    const double cx = std::cos(angleRad);
    const double sy = std::sin(angleRad);
    // Para que Mx > 0 quando angle=0: lado comprimido em y > 0 (eps decresce com +y)
    // eps(x, y) = eps0 - kTotal * sCoord, onde sCoord = sy * x + cx * y
    const double sMaxConcrete = std::abs(sy) * (0.5 * bx_m) + std::abs(cx) * (0.5 * hy_m);
    double sMaxBar = 0.0;
    for (const auto& b : bars) {
        sMaxBar = std::max(sMaxBar, std::abs(sy * b.x + cx * b.y));
    }
    const double depthTotal = sMaxConcrete + sMaxBar; // dist entre fibra mais compr e aco mais trac

    // Dominio parametrizado linearmente em termos de inclinacao kTotal >= 0 e deslocamento eps0:
    // Se t in [0, 0.35]: reta a -> pivote A (aco mais tracionado em +esu, fibra mais compr sobe de +esu ate -ecu)
    // Se t in [0.35, 0.70]: pivote B (fibra mais compr em -ecu, aco mais tracionado vai de +esu ate eps_b0)
    // Se t in [0.70, 1.00]: pivote C (s_C = (1 - ec2/ecu)*(2*sMaxConcrete) na fibra de compressao, vai ate reta b -ec2)
    double kTotal = 0.0;
    double epsAtSMaxConcrete = 0.0;

    if (t <= 0.35) {
        const double u = t / 0.35; // 0..1
        epsAtSMaxConcrete = esu * (1.0 - u) + (-ecu) * u;
        const double epsAtMinusSBar = esu;
        kTotal = (epsAtMinusSBar - epsAtSMaxConcrete) / std::max(1e-6, depthTotal);
    } else if (t <= 0.70) {
        const double u = (t - 0.35) / 0.35; // 0..1
        epsAtSMaxConcrete = -ecu;
        const double epsMinConcOpp = -ecu + (2.0 * sMaxConcrete / std::max(1e-6, depthTotal)) * (ecu + esu);
        // Vai da fronteira do Dominio 2 ate linha neutra no contorno comprimido oposto (-sMaxConcrete) com eps=0
        const double kStart = (esu - (-ecu)) / std::max(1e-6, depthTotal);
        const double kEnd = ecu / std::max(1e-6, 2.0 * sMaxConcrete);
        kTotal = kStart * (1.0 - u) + kEnd * u;
        (void)epsMinConcOpp;
    } else {
        const double u = (t - 0.70) / 0.30; // 0..1
        // Pivote C a distancia dC = (ec2 / ecu) * (2*sMaxConcrete) a partir de +sMaxConcrete => sC = sMaxConcrete - 2*sMaxConcrete*(1 - ec2/ecu)
        const double sC = sMaxConcrete * (2.0 * ec2 / ecu - 1.0);
        // No inicio (u=0): LN em -sMaxConcrete, eps(sMaxConcrete)=-ecu, eps(sC)=-ec2
        // No fim (u=1): reta b uniforme eps=-ec2, kTotal=0
        const double kEndDom4 = ecu / std::max(1e-6, 2.0 * sMaxConcrete);
        kTotal = kEndDom4 * (1.0 - u);
        epsAtSMaxConcrete = -ec2 - kTotal * (sMaxConcrete - sC);
    }

    const double eps0 = epsAtSMaxConcrete + kTotal * sMaxConcrete;
    // eps = eps0 - kTotal * (sy * x + cx * y) = eps0 + (-kTotal * cx)*y - (kTotal * sy)*x
    return integrateColumnFibers(bx_m, hy_m, fck_MPa, fcd_MPa, fyd_MPa, bars,
                                 eps0, -kTotal * cx, kTotal * sy);
}

/// Resolve capacidade de momento uniaxial ao longo da direcao angleRad para N = N_Sd (se exequivel).
bool solveColumnMomentAtN(double bx_m, double hy_m, double dPrime,
                          double fck_MPa, double fcd_MPa, double fyd_MPa,
                          const std::vector<RebarPoint>& bars,
                          double angleRad, double N_Sd_kN,
                          FiberForceResult& outRes) {
    const auto rMin = evalColumnUlsState(bx_m, hy_m, dPrime, fck_MPa, fcd_MPa, fyd_MPa, bars, angleRad, 0.0);
    const auto rMax = evalColumnUlsState(bx_m, hy_m, dPrime, fck_MPa, fcd_MPa, fyd_MPa, bars, angleRad, 1.0);
    if (N_Sd_kN > rMin.N_kN + 1e-6 || N_Sd_kN < rMax.N_kN - 1e-6) return false;

    double lo = 0.0, hi = 1.0;
    for (int it = 0; it < 36; ++it) {
        const double mid = 0.5 * (lo + hi);
        const auto rm = evalColumnUlsState(bx_m, hy_m, dPrime, fck_MPa, fcd_MPa, fyd_MPa, bars, angleRad, mid);
        if (rm.N_kN > N_Sd_kN) lo = mid;
        else hi = mid;
    }
    outRes = evalColumnUlsState(bx_m, hy_m, dPrime, fck_MPa, fcd_MPa, fyd_MPa, bars, angleRad, 0.5 * (lo + hi));
    return true;
}

} // namespace

std::vector<StandardCoverageInfo> NbrCodeChecks::getStandardCoverageMatrix() {
    return {
        {"ABNT NBR 6118:2023",
         "Projeto de estruturas de concreto",
         ComplianceScopeStatus::LocalPdfVerified,
         "NBR_6118-2023_Projetos.de.estruturas.de.concreto.pdf",
         "Sec. 8.2/11.8/12/13.4/14.6.4.3/17.2/17.3/17.4 (ELU flexao, cortante Modelo I, pilar P-Mx-My, ELS-W/flechas)."},
        {"ABNT NBR 6120:2019",
         "Acoes para o calculo de estruturas de edificacoes",
         ComplianceScopeStatus::LocalPdfVerified,
         "NBR_6120-2019_Cargas.Para.o.Calculo.de.Estruturas.de.Edificacoes.pdf",
         "Sec. 6 e Tabelas 10-11 (cargas variaveis e divisorias sem posicao definida)."},
        {"ABNT NBR 6122:2022",
         "Projeto e execucao de fundacoes",
         ComplianceScopeStatus::LocalPdfVerified,
         "NBR_6122-2022_Proejto.e.Execucao.de.Fundacoes.pdf",
         "Sec. 6.2.1.1.1 Tab. 1 e Sec. 6.3 (tensao admissivel/resistente em fundacao rasa)."},
        {"ABNT NBR 6123:2023",
         "Forcas devidas ao vento em edificacoes",
         ComplianceScopeStatus::LocalPdfVerified,
         "NBR_6123-2023_Forcas.Devidas.ao.Vento.em.Edificacoes.pdf",
         "Sec. 4.2 (Vk = V0*S1*S2*S3 e pressao dinamica q = 0.613*Vk^2 N/m^2)."},
        {"ABNT NBR 7480:2022",
         "Aco destinado a armaduras de estruturas de concreto armado",
         ComplianceScopeStatus::LocalPdfVerified,
         "NBR_7480-2022_Aco.Destinado.Armaduras.Estruturas.Concreto.Armado.pdf",
         "Sec. 4.1-4.4 (CA-25, CA-50, CA-60, Es=210 GPa, rho=7850 kg/m^3)."},
        {"ABNT NBR 8800:2024",
         "Projeto de estruturas de aco e mistas de edificacoes",
         ComplianceScopeStatus::LocalPdfVerified,
         "NBR_8800-2024_Projeto.de.Estruturas.de.Aco.e.de.Estruturas.Mistas.de.Aco.e.Concreto.de.Edificacoes.pdf",
         "Tab. 1-5 e Sec. 5.2, 5.3, 5.4.2, 5.4.3, 5.5.1.2, Anexos D/E (tracao, compressao, flexao FLT/FLM/FLA, cortante, interacao PMM)."},
        {"ABNT NBR 14323:2013",
         "Dimensionamento em situacao de incendio (aco/misto)",
         ComplianceScopeStatus::PartialReferenceOnly,
         "NBR_14323-2013_Projeto.de.Estruturas.de.Aco.e.de.Estruturas.Mistas.de.Aco.e.Concreto.de.Edificios.em.Situacao.de.Incendio.pdf",
         "PDF presente na biblioteca local; reducao termica de materiais nao acoplada automaticamente neste ciclo."},
        {"ABNT NBR 15575:2013-2024",
         "Edificacoes habitacionais - Desempenho",
         ComplianceScopeStatus::PartialReferenceOnly,
         "NBR_15575-2024_Edificacoes.habitacionais_Desempenho-P1_Requisitos.gerais.pdf",
         "PDF presente na biblioteca local; referencia global de vida util e desempenho estrutural."},
        {"ABNT NBR 8681",
         "Acoes e seguranca nas estruturas",
         ComplianceScopeStatus::OutOfLocalPdfScope,
         "",
         "PDF ausente na biblioteca local Normas_Tecnicas_NBRs; combinacoes atendidas via NBR 6118 Tab. 11.1-11.4 e NBR 8800 Tab. 1-3."},
        {"ABNT NBR 14762",
         "Estruturas de aco constituidas por perfis formados a frio",
         ComplianceScopeStatus::OutOfLocalPdfScope,
         "",
         "PDF ausente na biblioteca local Normas_Tecnicas_NBRs; perfis dobrados a frio fora do escopo automatico."},
        {"ABNT NBR 15421",
         "Projeto de estruturas resistentes a sismos",
         ComplianceScopeStatus::OutOfLocalPdfScope,
         "",
         "PDF ausente na biblioteca local Normas_Tecnicas_NBRs; espectros sismicos fornecidos apenas por entrada parametrica do usuario."},
        {"AISC 360 / Eurocode",
         "Normas internacionais complementares",
         ComplianceScopeStatus::OutOfLocalPdfScope,
         "",
         "Fora do escopo da biblioteca normativa local."}
    };
}

CombinationFactors NbrCodeChecks::concreteFactors(CombinationSituation sit) {
    CombinationFactors f{};
    switch (sit) {
    case CombinationSituation::Normal:
        f.gamma_g_unfav = 1.40;
        f.gamma_g_fav = 1.00;
        f.gamma_q = 1.40;
        f.gamma_w = 1.40;
        f.gamma_t = 1.20;
        f.gamma_c = 1.40;
        f.gamma_s = 1.15;
        break;
    case CombinationSituation::SpecialOrConstruction:
        f.gamma_g_unfav = 1.30;
        f.gamma_g_fav = 1.00;
        f.gamma_q = 1.20;
        f.gamma_w = 1.20;
        f.gamma_t = 1.00;
        f.gamma_c = 1.20;
        f.gamma_s = 1.15;
        break;
    case CombinationSituation::Exceptional:
        f.gamma_g_unfav = 1.20;
        f.gamma_g_fav = 1.00;
        f.gamma_q = 1.00;
        f.gamma_w = 1.00;
        f.gamma_t = 0.00;
        f.gamma_c = 1.20;
        f.gamma_s = 1.00;
        break;
    }
    f.gamma_a1 = 1.10;
    f.gamma_a2 = 1.35;
    return f;
}

CombinationFactors NbrCodeChecks::steelFactors(CombinationSituation sit, DeadLoadClass8800 deadClass) {
    CombinationFactors f{};
    auto pickDead = [&](double st, double pre, double cast, double mas, double grp) {
        switch (deadClass) {
        case DeadLoadClass8800::StructuralSteel: return st;
        case DeadLoadClass8800::PrecastOrTimber: return pre;
        case DeadLoadClass8800::CastInPlaceConcrete: return cast;
        case DeadLoadClass8800::NonStructuralMasonryOrFinish: return mas;
        case DeadLoadClass8800::GroupedPermanent: return grp;
        }
        return grp;
    };

    switch (sit) {
    case CombinationSituation::Normal:
        f.gamma_g_unfav = pickDead(1.25, 1.30, 1.35, 1.50, 1.35);
        f.gamma_g_fav = 1.00;
        f.gamma_q = 1.50;
        f.gamma_w = 1.40;
        f.gamma_t = 1.20;
        f.gamma_a1 = 1.10;
        f.gamma_a2 = 1.35;
        f.gamma_c = 1.40;
        f.gamma_s = 1.15;
        break;
    case CombinationSituation::SpecialOrConstruction:
        f.gamma_g_unfav = pickDead(1.15, 1.20, 1.25, 1.40, 1.25);
        f.gamma_g_fav = 1.00;
        f.gamma_q = 1.30;
        f.gamma_w = 1.20;
        f.gamma_t = 1.00;
        f.gamma_a1 = 1.10;
        f.gamma_a2 = 1.35;
        f.gamma_c = 1.20;
        f.gamma_s = 1.15;
        break;
    case CombinationSituation::Exceptional:
        f.gamma_g_unfav = pickDead(1.10, 1.15, 1.15, 1.30, 1.15);
        f.gamma_g_fav = 1.00;
        f.gamma_q = 1.00;
        f.gamma_w = 1.00;
        f.gamma_t = 0.00;
        f.gamma_a1 = 1.00;
        f.gamma_a2 = 1.15;
        f.gamma_c = 1.20;
        f.gamma_s = 1.00;
        break;
    }
    return f;
}

PsiFactors NbrCodeChecks::occupancyPsi(OccupancyCategory cat) {
    switch (cat) {
    case OccupancyCategory::Residential: return {0.5, 0.4, 0.3};
    case OccupancyCategory::CommercialOrPublic: return {0.7, 0.6, 0.4};
    case OccupancyCategory::LibraryArchiveGarageRoof: return {0.8, 0.7, 0.6};
    }
    return {0.5, 0.4, 0.3};
}

PsiFactors NbrCodeChecks::windPsi() { return {0.6, 0.3, 0.0}; }
PsiFactors NbrCodeChecks::temperaturePsi() { return {0.6, 0.5, 0.3}; }

double NbrCodeChecks::unpositionedPartitionSurcharge6120_kPa(double linearWeight_kNPerM) {
    if (linearWeight_kNPerM <= 0.0) return 0.0;
    if (linearWeight_kNPerM <= 1.0) return 0.50;
    if (linearWeight_kNPerM <= 2.0) return 0.75;
    if (linearWeight_kNPerM <= 3.0) return 1.00;
    return -1.0; // Tabela 11: nao permitido para peso > 3.0 kN/m
}

WindResult6123 NbrCodeChecks::evaluateWind6123(const WindInput6123& in) {
    WindResult6123 out;
    out.vk_ms = std::max(0.0, in.v0_ms) * in.s1 * in.s2 * in.s3;
    out.q_Pa = 0.613 * out.vk_ms * out.vk_ms;
    out.q_kPa = out.q_Pa * 1e-3;
    return out;
}

double NbrCodeChecks::concreteFctm_MPa(double fck_MPa) {
    const double f = std::clamp(fck_MPa, 15.0, 90.0);
    if (f <= 50.0) return 0.30 * std::pow(f, 2.0 / 3.0);
    return 2.12 * std::log(1.0 + 0.11 * f);
}

double NbrCodeChecks::concreteFctkInf_MPa(double fck_MPa) {
    return 0.70 * concreteFctm_MPa(fck_MPa);
}

double NbrCodeChecks::concreteFctkSup_MPa(double fck_MPa) {
    return 1.30 * concreteFctm_MPa(fck_MPa);
}

double NbrCodeChecks::concreteLambda(double fck_MPa) {
    if (fck_MPa <= 50.0) return 0.80;
    return 0.80 - (std::min(90.0, fck_MPa) - 50.0) / 400.0;
}

double NbrCodeChecks::concreteAlphaC(double fck_MPa) {
    if (fck_MPa <= 50.0) return 0.85;
    return 0.85 * (1.0 - (std::min(90.0, fck_MPa) - 50.0) / 200.0);
}

double NbrCodeChecks::concreteEtaC(double fck_MPa) {
    if (fck_MPa <= 40.0) return 1.0;
    return std::cbrt(40.0 / fck_MPa);
}

double NbrCodeChecks::concreteDuctilityLimitXOverD(double fck_MPa) {
    return (fck_MPa <= 50.0) ? 0.45 : 0.35;
}

double NbrCodeChecks::rebarCharacteristicYield_MPa(RebarGrade7480 grade) {
    switch (grade) {
    case RebarGrade7480::CA25: return 250.0;
    case RebarGrade7480::CA50: return 500.0;
    case RebarGrade7480::CA60: return 600.0;
    }
    return 500.0;
}

double NbrCodeChecks::rebarBondFactorEta1(RebarGrade7480 grade) {
    switch (grade) {
    case RebarGrade7480::CA25: return 1.00;
    case RebarGrade7480::CA50: return 2.25;
    case RebarGrade7480::CA60: return 1.00; // NBR 6118:2023 Tabela 8.2
    }
    return 2.25;
}

double NbrCodeChecks::minFlexuralSteelRatioTab17_3(double fck_MPa) {
    static constexpr std::array<std::pair<double, double>, 15> kTab{{
        {20.0, 0.00150}, {25.0, 0.00150}, {30.0, 0.00150}, {35.0, 0.00164}, {40.0, 0.00179},
        {45.0, 0.00194}, {50.0, 0.00208}, {55.0, 0.00211}, {60.0, 0.00219}, {65.0, 0.00226},
        {70.0, 0.00233}, {75.0, 0.00239}, {80.0, 0.00245}, {85.0, 0.00251}, {90.0, 0.00256}
    }};
    if (fck_MPa <= kTab.front().first) return kTab.front().second;
    if (fck_MPa >= kTab.back().first) return kTab.back().second;
    for (size_t i = 0; i + 1 < kTab.size(); ++i) {
        if (fck_MPa >= kTab[i].first && fck_MPa <= kTab[i + 1].first) {
            const double alpha = (fck_MPa - kTab[i].first) / (kTab[i + 1].first - kTab[i].first);
            return kTab[i].second + alpha * (kTab[i + 1].second - kTab[i].second);
        }
    }
    return 0.00150;
}

double NbrCodeChecks::computeCb8800(double mMax_abs, double mA_abs, double mB_abs, double mC_abs, double Rm) {
    const double Mmax = std::abs(mMax_abs);
    if (Mmax <= 1e-12) return 1.0 * Rm;
    const double denom = 2.5 * Mmax + 3.0 * std::abs(mA_abs) + 4.0 * std::abs(mB_abs) + 3.0 * std::abs(mC_abs);
    if (denom <= 1e-12) return 1.0 * Rm;
    return std::min(3.0, (12.5 * Mmax / denom) * Rm);
}

SteelCheckResult NbrCodeChecks::checkSteelMember8800(const SteelSectionSpec& sec,
                                                     const SteelDesignForces& forces,
                                                     CombinationSituation sit) {
    SteelCheckResult out;
    const auto f = steelFactors(sit);
    const double gammaA1 = f.gamma_a1;
    const double gammaA2 = f.gamma_a2;
    const auto g = computeSteelGeom(sec);

    const double E = std::max(1.0, sec.E_MPa);
    const double G = (sec.G_MPa > 1.0) ? sec.G_MPa : E / (2.0 * 1.3);
    const double fy = std::max(1.0, sec.fy_MPa);
    const double fu = std::max(fy, sec.fu_MPa);
    const double sigmaR = 0.30 * fy; // Anexo D.2.8-e)

    // 1. Tracao resistente (Sec. 5.2.2)
    const double Nt_y_kN = (g.Ag * fy * 1e3) / gammaA1;
    const double Ae = std::clamp(sec.Ct, 0.0, 1.0) * std::clamp(sec.netAreaRatio, 0.0, 1.0) * g.Ag;
    const double Nt_u_kN = (Ae * fu * 1e3) / gammaA2;
    out.Nt_Rd_kN = std::min(Nt_y_kN, Nt_u_kN);

    // 2. Compressao resistente (Sec. 5.3.2 a 5.3.5 + Tabelas 4 e 5)
    const double Lx = std::max(1e-4, sec.Lx_m);
    const double Ly = std::max(1e-4, sec.Ly_m);
    const double Lz = std::max(1e-4, sec.Lz_m);
    const double Lb = std::max(1e-4, sec.Lb_m);

    const double Nex_kN = (kPi * kPi * E * g.Ix * 1e3) / (Lx * Lx);
    const double Ney_kN = (kPi * kPi * E * g.Iy * 1e3) / (Ly * Ly);
    const double Nez_kN = (1.0 / (g.r0 * g.r0)) * ((kPi * kPi * E * g.Cw * 1e3) / (Lz * Lz) + G * g.J * 1e3);
    const double Ne_kN = std::max(1e-6, std::min({Nex_kN, Ney_kN, Nez_kN}));

    const double lambda0 = std::sqrt((g.Ag * fy * 1e3) / Ne_kN);
    const double chi = (lambda0 <= 1.5)
        ? std::pow(0.658, lambda0 * lambda0)
        : (0.877 / (lambda0 * lambda0));
    out.chiReduction = chi;

    double Aef = g.Ag;
    if (sec.kind == SteelSectionKind::IShapeRolled || sec.kind == SteelSectionKind::IShapeWelded) {
        double btFlangeLim = 0.56 * std::sqrt(E / fy);
        if (sec.kind == SteelSectionKind::IShapeWelded) {
            const double hwTw = std::max(1e-4, g.hFlat / g.tw);
            const double kc = std::clamp(4.0 / std::sqrt(hwTw), 0.35, 0.76);
            btFlangeLim = 0.64 * std::sqrt(E / (fy / kc));
        }
        const double bFlangeEf = effectiveWidth5_3_4_2(g.bFlange, g.tf, btFlangeLim, chi, fy, 0.22, 1.49);
        const double btWebLim = 1.49 * std::sqrt(E / fy);
        const double hWebEf = effectiveWidth5_3_4_2(g.hFlat, g.tw, btWebLim, chi, fy, 0.18, 1.31);
        const double dAFlanges = 4.0 * std::max(0.0, g.bFlange - bFlangeEf) * g.tf;
        const double dAWeb = std::max(0.0, g.hFlat - hWebEf) * g.tw;
        Aef = std::max(0.0, g.Ag - dAFlanges - dAWeb);
    } else if (sec.kind == SteelSectionKind::RectangularTube) {
        const double btLim = 1.40 * std::sqrt(E / fy);
        const double bEf = effectiveWidth5_3_4_2(g.bFlange, g.tf, btLim, chi, fy, 0.20, 1.38);
        const double hEf = effectiveWidth5_3_4_2(g.hFlat, g.tw, btLim, chi, fy, 0.20, 1.38);
        const double lost = 2.0 * std::max(0.0, g.bFlange - bEf) * g.tf + 2.0 * std::max(0.0, g.hFlat - hEf) * g.tw;
        Aef = std::max(0.0, g.Ag - lost);
    } else if (sec.kind == SteelSectionKind::CircularTube) {
        const double D_t = sec.d_m / g.tf;
        if (D_t > 0.11 * (E / fy)) {
            const double factor = std::clamp(0.038 * (E / fy) / D_t + 2.0 / 3.0, 0.0, 1.0);
            Aef = factor * g.Ag;
        }
    }
    out.effectiveAreaRatio = (g.Ag > 1e-12) ? (Aef / g.Ag) : 1.0;
    out.Nc_Rd_kN = (chi * Aef * fy * 1e3) / gammaA1;

    // 3. Momento fletor resistente (Sec. 5.4.2, Anexo D, Anexo E)
    const double cb = std::max(1.0, sec.Cb);
    const double Mlim_x_kNm = 1.50 * g.Wx * fy * 1e3 / gammaA1; // 5.4.2.2
    const double Mlim_y_kNm = 1.50 * g.Wy * fy * 1e3 / gammaA1; // 5.4.2.2

    double Mx_Rd_cand;
    double My_Rd_cand;
    std::string governingBend = "Esbeltez/Plastificacao";

    if (sec.kind == SteelSectionKind::IShapeRolled || sec.kind == SteelSectionKind::IShapeWelded) {
        const double Mpl_x_MNm = g.Zx * fy;
        const double Mr_x_MNm = (fy - sigmaR) * g.Wx;

        // FLA (Tabela D.1 ou Anexo E se alma esbelta)
        const double lam_w = g.hFlat / g.tw;
        const double lamP_w = 3.76 * std::sqrt(E / fy);
        const double lamR_w = 5.70 * std::sqrt(E / fy);

        // FLT (Tabela D.1 + D.2.8-a)): lambdaR independente de Cb; resultado multiplicado por Cb e limitado por Mpl
        const double lam_LT = Lb / g.ry;
        const double lamP_LT = 1.76 * std::sqrt(E / fy);
        const double beta1 = (fy - sigmaR) * g.Wx / (E * g.J);
        const double Mcr_LT_MNm = cb * (kPi * kPi * E * g.Iy / (Lb * Lb)) *
            std::sqrt((g.Cw / g.Iy) * (1.0 + 0.039 * g.J * Lb * Lb / std::max(1e-18, g.Cw)));
        const double lamR_LT = (beta1 > 1e-12)
            ? (1.38 * std::sqrt(g.Iy * g.J) / (g.ry * g.J * beta1)) *
                  std::sqrt(1.0 + std::sqrt(1.0 + 27.0 * g.Cw * beta1 * beta1 / g.Iy))
            : 1e9;
        double Mx_FLT_MNm = evalFltBandMNm(
            lam_LT, lamP_LT, lamR_LT, cb, Mpl_x_MNm, Mr_x_MNm, Mcr_LT_MNm, Mpl_x_MNm);

        // FLM (Tabela D.1 + D.2.8-f))
        const double lam_f = g.bFlange / g.tf;
        const double lamP_f = 0.38 * std::sqrt(E / fy);
        double lamR_f = 0.83 * std::sqrt(E / (fy - sigmaR));
        double Mcr_f_MNm = 0.69 * E * g.Wx / (lam_f * lam_f);
        if (sec.kind == SteelSectionKind::IShapeWelded) {
            const double kc = std::clamp(4.0 / std::sqrt(std::max(1e-4, lam_w)), 0.35, 0.76);
            lamR_f = 0.95 * std::sqrt(E / ((fy - sigmaR) / kc));
            Mcr_f_MNm = 0.90 * E * kc * g.Wx / (lam_f * lam_f);
        }
        double Mx_FLM_MNm = evalBendingBandMNm(lam_f, lamP_f, lamR_f, Mpl_x_MNm, Mr_x_MNm, Mcr_f_MNm);

        double Mx_FLA_MNm;
        if (lam_w <= lamR_w) {
            Mx_FLA_MNm = evalBendingBandMNm(lam_w, lamP_w, lamR_w, Mpl_x_MNm, fy * g.Wx, fy * g.Wx);
        } else {
            // Anexo E: viga de alma esbelta (E.6.1, E.6.2)
            const double hc = g.hFlat;
            const double Afc = sec.bf_m * g.tf;
            const double ar = std::min(10.0, (hc * g.tw) / std::max(1e-9, Afc));
            const double kpg = std::min(1.0, 1.0 - (ar / (1200.0 + 300.0 * ar)) * (lam_w - lamR_w));
            const double My_MNm = kpg * fy * g.Wx;
            const double Mr_e_MNm = kpg * (fy - sigmaR) * g.Wx;
            // r_yc: raio de giracao da mesa comprimida + 1/3 da alma comprimida
            const double ycArea = Afc + (hc / 6.0) * g.tw;
            const double ycIy = (g.tf * std::pow(sec.bf_m, 3) + (hc / 6.0) * std::pow(g.tw, 3)) / 12.0;
            const double ryc = std::sqrt(std::max(1e-12, ycIy / std::max(1e-9, ycArea)));
            const double lam_pg = Lb / ryc;
            const double lamP_pg = 1.10 * std::sqrt(E / fy);
            const double lamR_pg = kPi * std::sqrt(E / (fy - sigmaR));
            const double Mcr_pg_MNm = (cb * kPi * kPi * E * kpg * g.Wx) / (lam_pg * lam_pg);
            Mx_FLT_MNm = evalFltBandMNm(
                lam_pg, lamP_pg, lamR_pg, cb, My_MNm, Mr_e_MNm, Mcr_pg_MNm, My_MNm);
            Mx_FLA_MNm = My_MNm;
            governingBend = "Anexo E (Alma Esbelta)";
        }

        Mx_Rd_cand = std::min({Mx_FLT_MNm, Mx_FLM_MNm, Mx_FLA_MNm}) * 1e3 / gammaA1;

        // Eixo menor y (Tabela D.1: apenas FLM para I/H duplamente simetricos)
        const double Mpl_y_MNm = g.Zy * fy;
        const double Mr_y_MNm = (fy - sigmaR) * g.Wy;
        double Mcr_fy_MNm = 0.69 * E * g.Wy / (lam_f * lam_f);
        if (sec.kind == SteelSectionKind::IShapeWelded) {
            const double kc = std::clamp(4.0 / std::sqrt(std::max(1e-4, lam_w)), 0.35, 0.76);
            Mcr_fy_MNm = 0.90 * E * kc * g.Wy / (lam_f * lam_f);
        }
        My_Rd_cand = evalBendingBandMNm(lam_f, lamP_f, lamR_f, Mpl_y_MNm, Mr_y_MNm, Mcr_fy_MNm) * 1e3 / gammaA1;
    } else if (sec.kind == SteelSectionKind::RectangularTube) {
        // D.2.7 + Tabela D.1
        auto evalBoxAxis = [&](double Z_ax, double W_ax, double bComp, double tComp, double hWeb, double tWeb, double ryAxis) {
            const double Mpl = Z_ax * fy;
            const double lamF = bComp / tComp;
            const double lamPf = 1.12 * std::sqrt(E / fy);
            const double lamRf = 1.40 * std::sqrt(E / fy);
            double Mflm = Mpl;
            if (lamF > lamPf && lamF <= lamRf) {
                Mflm = Mpl - (Mpl - fy * W_ax) * (3.57 * lamF * std::sqrt(fy / E) - 4.0);
            } else if (lamF > lamRf) {
                const double bef = effectiveWidth5_3_4_2(bComp, tComp, lamRf, 1.0, fy, 0.20, 1.38);
                const double scaleW = (bComp > 1e-12) ? (1.0 - 0.5 * (bComp - bef) / bComp) : 1.0;
                Mflm = fy * W_ax * std::max(0.2, scaleW);
            }
            const double lamW = hWeb / tWeb;
            const double lamPw = 2.42 * std::sqrt(E / fy);
            const double lamRw = 5.70 * std::sqrt(E / fy);
            double Mfla = Mpl;
            if (lamW > lamPw && lamW <= lamRw) {
                Mfla = std::min(Mpl, Mpl - (Mpl - fy * W_ax) * (0.305 * lamW * std::sqrt(fy / E) - 0.738));
            } else if (lamW > lamRw) {
                Mfla = fy * W_ax; // Fallback conservador dentro dos limites usuais
            }
            double Mflt = Mpl;
            const double lamLT = Lb / std::max(1e-6, ryAxis);
            const double factorJA = std::sqrt(g.J * g.Ag);
            const double lamPLT = 0.13 * E * factorJA / std::max(1e-9, Mpl);
            const double MrLT = (fy - sigmaR) * W_ax;
            const double lamRLT = 2.0 * E * factorJA / std::max(1e-9, MrLT);
            const double McrLT = (2.0 * cb * E * factorJA) / std::max(1e-9, lamLT);
            Mflt = evalFltBandMNm(lamLT, lamPLT, lamRLT, cb, Mpl, MrLT, McrLT, Mpl);
            return std::min({Mflm, Mfla, Mflt}) * 1e3 / gammaA1;
        };
        Mx_Rd_cand = evalBoxAxis(g.Zx, g.Wx, g.bFlange, g.tf, g.hFlat, g.tw, g.ry);
        My_Rd_cand = evalBoxAxis(g.Zy, g.Wy, g.hFlat, g.tw, g.bFlange, g.tf, g.rx);
    } else if (sec.kind == SteelSectionKind::CircularTube) {
        // D.2.6
        const double lam = sec.d_m / g.tf;
        const double lamP = 0.07 * (E / fy);
        const double lamR = 0.31 * (E / fy);
        const double Mpl = g.Zx * fy;
        double MRd_MNm = Mpl;
        if (lam > lamP && lam <= lamR) MRd_MNm = (0.021 * E / lam + fy) * g.Wx;
        else if (lam > lamR) MRd_MNm = (0.33 * E / lam) * g.Wx;
        Mx_Rd_cand = (MRd_MNm * 1e3) / gammaA1;
        My_Rd_cand = Mx_Rd_cand;
    } else if (sec.kind == SteelSectionKind::RectangularSolid) {
        // Tabela D.1 + D.2.5
        const double MplX = g.Zx * fy;
        const double MrX = fy * g.Wx;
        const double factorJA = std::sqrt(g.J * g.Ag);
        const double lamPLT = 0.13 * E * factorJA / std::max(1e-9, MplX);
        const double lamRLT = 2.0 * E * factorJA / std::max(1e-9, MrX);
        const double lamLT = Lb / g.ry;
        const double McrX = (2.0 * cb * E * factorJA) / std::max(1e-9, lamLT);
        Mx_Rd_cand = evalFltBandMNm(lamLT, lamPLT, lamRLT, cb, MplX, MrX, McrX, MplX) * 1e3 / gammaA1;
        My_Rd_cand = (g.Zy * fy * 1e3) / gammaA1;
    } else {
        Mx_Rd_cand = (g.Zx * fy * 1e3) / gammaA1;
        My_Rd_cand = (g.Zy * fy * 1e3) / gammaA1;
    }

    out.Mx_Rd_kNm = std::min(Mlim_x_kNm, Mx_Rd_cand);
    out.My_Rd_kNm = std::min(Mlim_y_kNm, My_Rd_cand);

    // 4. Forca cortante resistente (Sec. 5.4.3)
    if (sec.kind == SteelSectionKind::IShapeRolled || sec.kind == SteelSectionKind::IShapeWelded) {
        // 5.4.3.1 (Vy na alma)
        const double kvY = 5.34;
        const double lamPY = 1.10 * std::sqrt(kvY * E / fy);
        const double lamRY = 1.37 * std::sqrt(kvY * E / fy);
        const double VplY_kN = 0.60 * (sec.d_m * g.tw) * fy * 1e3;
        out.Vy_Rd_kN = evalShearBandkN(g.hFlat / g.tw, lamPY, lamRY, VplY_kN, gammaA1);

        // 5.4.3.5 (Vx nas mesas)
        const double kvX = 1.20;
        const double lamPX = 1.10 * std::sqrt(kvX * E / fy);
        const double lamRX = 1.37 * std::sqrt(kvX * E / fy);
        const double VplX_kN = 0.60 * (2.0 * sec.bf_m * g.tf) * fy * 1e3;
        out.Vx_Rd_kN = evalShearBandkN((0.5 * sec.bf_m) / g.tf, lamPX, lamRX, VplX_kN, gammaA1);
    } else if (sec.kind == SteelSectionKind::RectangularTube) {
        // 5.4.3.2
        const double kv = 5.0;
        const double lamP = 1.10 * std::sqrt(kv * E / fy);
        const double lamR = 1.37 * std::sqrt(kv * E / fy);
        out.Vy_Rd_kN = evalShearBandkN(g.hFlat / g.tw, lamP, lamR, 0.60 * (2.0 * g.hFlat * g.tw) * fy * 1e3, gammaA1);
        out.Vx_Rd_kN = evalShearBandkN(g.bFlange / g.tf, lamP, lamR, 0.60 * (2.0 * g.bFlange * g.tf) * fy * 1e3, gammaA1);
    } else if (sec.kind == SteelSectionKind::CircularTube) {
        // 5.4.3.6
        const double D_t = sec.d_m / g.tf;
        const double Lv = std::max(0.1, 0.5 * Lx);
        const double tau1 = 1.60 * E / (std::sqrt(Lv / sec.d_m) * std::pow(D_t, 1.25));
        const double tau2 = 0.78 * E / std::pow(D_t, 1.5);
        const double tauCr = std::min(0.60 * fy, std::max(tau1, tau2));
        out.Vy_Rd_kN = 0.50 * tauCr * g.Ag * 1e3 / gammaA1;
        out.Vx_Rd_kN = out.Vy_Rd_kN;
    } else {
        out.Vy_Rd_kN = 0.60 * g.Ag * fy * 1e3 / gammaA1;
        out.Vx_Rd_kN = out.Vy_Rd_kN;
    }

    // 5. Utilizacoes individuais e interacao NBR 8800:2024 Sec. 5.5.1.2
    const double N_Sd = forces.N_Sd_kN;
    const double N_Rd = (N_Sd >= 0.0) ? out.Nt_Rd_kN : out.Nc_Rd_kN;
    out.dcrAxial = (N_Rd > 1e-12) ? (std::abs(N_Sd) / N_Rd) : 999.0;
    out.dcrFlexureX = (out.Mx_Rd_kNm > 1e-12) ? (std::abs(forces.Mx_Sd_kNm) / out.Mx_Rd_kNm) : 999.0;
    out.dcrFlexureY = (out.My_Rd_kNm > 1e-12) ? (std::abs(forces.My_Sd_kNm) / out.My_Rd_kNm) : 999.0;
    out.dcrShearY = (out.Vy_Rd_kN > 1e-12) ? (std::abs(forces.Vy_Sd_kN) / out.Vy_Rd_kN) : 999.0;
    out.dcrShearX = (out.Vx_Rd_kN > 1e-12) ? (std::abs(forces.Vx_Sd_kN) / out.Vx_Rd_kN) : 999.0;

    if (out.dcrAxial >= 0.2) {
        out.dcrInteraction = out.dcrAxial + (8.0 / 9.0) * (out.dcrFlexureX + out.dcrFlexureY);
    } else {
        out.dcrInteraction = 0.5 * out.dcrAxial + (out.dcrFlexureX + out.dcrFlexureY);
    }

    out.maxSlenderness = std::max(Lx / g.rx, Ly / g.ry);
    out.slendernessLimit = (N_Sd < -1e-6) ? 200.0 : 300.0; // Sec. 5.3.7 vs 5.2.8
    out.dcrSlenderness = out.maxSlenderness / out.slendernessLimit;

    struct Cand { double v; const char* lbl; };
    const std::array<Cand, 6> cands{{
        {out.dcrInteraction, "Interacao N+Mx+My (NBR 8800 Sec. 5.5.1.2)"},
        {out.dcrShearY, "Cortante Eixo Y (NBR 8800 Sec. 5.4.3)"},
        {out.dcrShearX, "Cortante Eixo X (NBR 8800 Sec. 5.4.3)"},
        {out.dcrFlexureX, "Flexao Eixo X (NBR 8800 Sec. 5.4.2)"},
        {out.dcrAxial, (N_Sd < 0.0) ? "Compressao (NBR 8800 Sec. 5.3)" : "Tracao (NBR 8800 Sec. 5.2)"},
        {out.dcrSlenderness, "Indice de Esbeltez Limite KL/r"}
    }};
    auto best = std::max_element(cands.begin(), cands.end(),
                                 [](const Cand& a, const Cand& b) { return a.v < b.v; });
    out.dcrMax = best->v;
    out.governingLimitState = best->lbl;
    out.passed = (out.dcrMax <= 1.0 + 1e-6);
    return out;
}

ConcreteBeamCheckResult NbrCodeChecks::checkConcreteBeam6118(const ConcreteBeamSpec& beam,
                                                             const ConcreteBeamForces& forces) {
    ConcreteBeamCheckResult out;
    const auto f = concreteFactors(beam.situation);
    const double bw = std::max(0.05, beam.bw_m);
    const double h = std::max(0.05, beam.h_m);
    const double d = std::clamp(beam.d_m, 0.03, h - 0.01);
    const double dp = std::clamp(beam.dp_m, 0.01, 0.4 * d);

    const double fck = std::clamp(beam.fck_MPa, 15.0, 90.0);
    const double fcd = fck / f.gamma_c;
    const double fyk = rebarCharacteristicYield_MPa(beam.rebarGrade);
    const double fyd = fyk / f.gamma_s;
    const double lam = concreteLambda(fck);
    const double alphaC = concreteAlphaC(fck);
    const double etaC = concreteEtaC(fck);
    const double sigmaCd = alphaC * etaC * fcd; // MPa
    const double xlim = concreteDuctilityLimitXOverD(fck);
    out.xOverDLimit = xlim;

    // 1. Momento minimo e armadura minima (17.3.5.2.1 & Tabela 17.3)
    const double W0 = bw * h * h / 6.0;
    const double fctkSup = concreteFctkSup_MPa(fck);
    out.M_d_min_kNm = 0.8 * W0 * fctkSup * 1e3;

    const double Ac_cm2 = (bw * h) * 1e4;
    const double rhoMin = minFlexuralSteelRatioTab17_3(fck);
    out.minAs_cm2 = std::max(0.0015, rhoMin) * Ac_cm2;
    out.maxTotalAs_cm2 = 0.04 * Ac_cm2; // 17.3.5.2.4

    const double MSd_abs = std::max(0.0, std::abs(forces.M_Sd_kNm));
    const double MDesignReq = std::max(MSd_abs, out.M_d_min_kNm);

    // 2. Dimensionamento a flexao simples (bloco retangular 17.2.2) respeitando x/d <= xlim
    const double yLim = lam * xlim * d;
    const double FcLim_kN = sigmaCd * bw * yLim * 1e3;
    const double Mlim_kNm = FcLim_kN * (d - 0.5 * yLim);

    if (MDesignReq <= Mlim_kNm) {
        // Armadura simples
        const double disc = std::max(0.0, 1.0 - (2.0 * MDesignReq * 1e-3) / (sigmaCd * bw * d * d));
        const double y = d * (1.0 - std::sqrt(disc));
        const double Fc_kN = sigmaCd * bw * y * 1e3;
        out.requiredAs_cm2 = (Fc_kN / (fyd * 1e3)) * 1e4;
        out.requiredAsp_cm2 = 0.0;
    } else {
        // Armadura dupla para manter ductilidade em x/d = xlim (17.2.3 & 14.6.4.3)
        const double dM = MDesignReq - Mlim_kNm;
        const double leverS = std::max(0.02, d - dp);
        const double epsC_u = (fck <= 50.0) ? 0.0035 : (0.0026 + 0.035 * std::pow((90.0 - fck) / 100.0, 4));
        const double xMax = xlim * d;
        const double epsSp = epsC_u * (xMax - dp) / std::max(1e-6, xMax);
        const double sigmaSp_MPa = std::min(fyd, 210000.0 * std::max(0.0, epsSp));
        out.requiredAsp_cm2 = (dM / (sigmaSp_MPa * 1e3 * leverS)) * 1e4;
        out.requiredAs_cm2 = ((FcLim_kN + dM / leverS) / (fyd * 1e3)) * 1e4;
    }
    out.requiredAs_cm2 = std::max(out.requiredAs_cm2, out.minAs_cm2);

    // Se providedAs_cm2 foi fornecida, avalia MRd e x/d reais da secao; caso contrario assume As = requiredAs
    const double AsUsed_cm2 = (beam.providedAs_cm2 > 0.0) ? beam.providedAs_cm2 : out.requiredAs_cm2;
    const double AspUsed_cm2 = (beam.providedAs_cm2 > 0.0) ? std::max(0.0, beam.providedAsp_cm2) : out.requiredAsp_cm2;

    const double epsCu = (fck <= 50.0)
        ? 0.0035
        : (0.0026 + 0.035 * std::pow((90.0 - fck) / 100.0, 4));
    const double NSd_MN = forces.N_Sd_kN * 1e-3; // + tracao, - compressao
    auto evalAxialSectionMN = [&](double x) -> std::tuple<double, double, double> {
        const double y = std::min(h, lam * std::max(0.0, x));
        const double Fc = sigmaCd * bw * y; // MN
        const double epsS = (x > 1e-6) ? (epsCu * (d - x) / x) : 0.010;
        const double sigS = std::clamp(210000.0 * epsS, -fyd, fyd);
        const double sigSp = (x > dp) ? std::min(fyd, 210000.0 * epsCu * (x - dp) / std::max(1e-6, x)) : 0.0;
        const double Fs = (AsUsed_cm2 * 1e-4) * sigS;
        const double Fsp = (AspUsed_cm2 * 1e-4) * sigSp;
        const double MRd_MNm = Fc * (0.5 * h - 0.5 * y) + Fsp * (0.5 * h - dp) + Fs * (d - 0.5 * h);
        return {Fc + Fsp - Fs + NSd_MN, MRd_MNm, x / d};
    };

    double xLo = 1e-4, xHi = 0.99 * h;
    for (int it = 0; it < 45; ++it) {
        const double xm = 0.5 * (xLo + xHi);
        const auto [Nres, Mtmp, xdTmp] = evalAxialSectionMN(xm);
        (void)Mtmp;
        (void)xdTmp;
        if (Nres < 0.0) xLo = xm; // precisa de mais compressao
        else xHi = xm;
    }
    const auto [Nbal, MRd_MNm, xdReal] = evalAxialSectionMN(0.5 * (xLo + xHi));
    const double NcapTens_MN = (AsUsed_cm2 + AspUsed_cm2) * 1e-4 * fyd;
    out.M_Rd_kNm = (NSd_MN > NcapTens_MN + 1e-6 || Nbal < -0.05 * std::max(0.1, sigmaCd * bw * h))
        ? 0.0
        : std::max(0.0, MRd_MNm * 1e3);
    out.xOverD = xdReal;

    out.dcrFlexure = (out.M_Rd_kNm > 1e-12) ? (MSd_abs / out.M_Rd_kNm) : 999.0;
    out.dcrDuctility = out.xOverD / out.xOverDLimit;
    out.dcrMinSteel = out.minAs_cm2 / std::max(1e-9, AsUsed_cm2);
    out.dcrMaxSteel = (AsUsed_cm2 + AspUsed_cm2) / out.maxTotalAs_cm2;

    // 3. Cortante Modelo I (NBR 6118:2023 Sec. 17.4.2.2)
    const double alphaV2 = 1.0 - fck / 250.0;
    out.V_Rd2_kN = 0.27 * alphaV2 * fcd * bw * d * 1e3;

    const double fctd = concreteFctkInf_MPa(fck) / f.gamma_c;
    const double Vc0_kN = 0.60 * fctd * bw * d * 1e3;
    if (forces.N_Sd_kN > 1e-6) {
        // Tracao concomitante reduz Vc
        const double nTensMax = concreteFctm_MPa(fck) * bw * h * 1e3;
        out.V_c_kN = std::max(0.0, Vc0_kN * (1.0 - forces.N_Sd_kN / std::max(1.0, nTensMax)));
    } else if (forces.N_Sd_kN < -1e-6) {
        // Compressao longitudinal moderada aumenta Vc ate 2*Vc0
        const double M0 = (-forces.N_Sd_kN) * (h / 6.0);
        const double factor = (MSd_abs > 1e-6) ? std::min(2.0, 1.0 + M0 / MSd_abs) : 1.0;
        out.V_c_kN = Vc0_kN * factor;
    } else {
        out.V_c_kN = Vc0_kN;
    }

    const double fywd_MPa = std::min(beam.stirrupFyk_MPa / f.gamma_s, 435.0);
    const double alphaRad = std::clamp(beam.stirrupAlphaDeg, 45.0, 90.0) * (kPi / 180.0);
    const double trig = std::sin(alphaRad) + std::cos(alphaRad);
    const double VSd_abs = std::max(0.0, std::abs(forces.V_Sd_kN));
    const double VswReq_kN = std::max(0.0, VSd_abs - out.V_c_kN);
    // Vsw [kN] = (Asw/s [m^2/m]) * 0.9 * d [m] * fywd [MPa] * 1000 * trig
    out.requiredAswPerS_cm2PerM = (VswReq_kN / std::max(1e-9, 0.9 * d * fywd_MPa * 1e3 * trig)) * 1e4;
    out.minAswPerS_cm2PerM = 0.20 * (concreteFctm_MPa(fck) / std::max(1.0, beam.stirrupFyk_MPa)) * bw * 1e4;
    out.requiredAswPerS_cm2PerM = std::max(out.requiredAswPerS_cm2PerM, out.minAswPerS_cm2PerM);

    const double AswUsed = (beam.providedAswPerS_cm2PerM > 0.0)
        ? beam.providedAswPerS_cm2PerM
        : out.requiredAswPerS_cm2PerM;
    out.V_sw_kN = (AswUsed * 1e-4) * 0.9 * d * (fywd_MPa * 1e3) * trig;
    out.V_Rd3_kN = out.V_c_kN + out.V_sw_kN;

    out.dcrShearStrut = (out.V_Rd2_kN > 1e-12) ? (VSd_abs / out.V_Rd2_kN) : 999.0;
    out.dcrShearStirrup = (out.V_Rd3_kN > 1e-12) ? (VSd_abs / out.V_Rd3_kN) : 999.0;

    // 4. ELS-W Fissuracao (NBR 6118:2023 Sec. 17.3.3.2 & Tabela 13.4)
    switch (beam.caa) {
    case EnvironmentalAggressivenessClass::CAA_I: out.wkLimit_mm = 0.40; break;
    case EnvironmentalAggressivenessClass::CAA_II:
    case EnvironmentalAggressivenessClass::CAA_III: out.wkLimit_mm = 0.30; break;
    case EnvironmentalAggressivenessClass::CAA_IV: out.wkLimit_mm = 0.20; break;
    }

    const double Mels_abs = std::max(0.0, std::abs(forces.M_Sd_els_kNm));
    if (Mels_abs > 1e-6 && AsUsed_cm2 > 1e-6) {
        constexpr double alphaE = 15.0; // Sec. 17.3.3.2 permite alpha_e = 15 no estadio II
        constexpr double Es_MPa = 210000.0;
        const double As_m2 = AsUsed_cm2 * 1e-4;
        const double Asp_m2 = AspUsed_cm2 * 1e-4;
        // 0.5*bw*x^2 + alphaE*Asp*(x - dp) - alphaE*As*(d - x) = 0
        const double aQuad = 0.5 * bw;
        const double bQuad = alphaE * (As_m2 + Asp_m2);
        const double cQuad = -alphaE * (As_m2 * d + Asp_m2 * dp);
        const double x2 = (-bQuad + std::sqrt(std::max(0.0, bQuad * bQuad - 4.0 * aQuad * cQuad))) / (2.0 * aQuad);
        const double I2 = (bw * std::pow(x2, 3)) / 3.0 + alphaE * Asp_m2 * std::pow(x2 - dp, 2)
                        + alphaE * As_m2 * std::pow(d - x2, 2);
        out.sigmaSi_els_MPa = alphaE * (Mels_abs * 1e-3) * std::max(0.0, d - x2) / std::max(1e-12, I2);

        const double phi_m = std::max(0.005, beam.barDiameter_mm * 1e-3);
        const double eta1 = rebarBondFactorEta1(beam.rebarGrade);
        const double fctm = concreteFctm_MPa(fck);
        const double nBars = static_cast<double>(std::max(1, beam.barsCountForCrackZone));
        const double wStrip = std::min(bw / nBars, 15.0 * phi_m);
        const double hStrip = std::min(15.0 * phi_m, 2.0 * std::max(0.02, h - d) + 7.5 * phi_m);
        const double Acri_m2 = std::max(1e-6, wStrip * hStrip);
        const double rhoRi = std::clamp((As_m2 / nBars) / Acri_m2, 1e-4, 0.25);

        const double termBase = (beam.barDiameter_mm * out.sigmaSi_els_MPa) / (12.5 * eta1 * Es_MPa);
        const double wk1 = termBase * (3.0 * out.sigmaSi_els_MPa / fctm);
        const double wk2 = termBase * (4.0 / rhoRi + 45.0);
        out.wk_mm = std::min(wk1, wk2);
        out.dcrCrackWidth = out.wk_mm / out.wkLimit_mm;
    }

    struct Cand { double v; const char* lbl; };
    const std::array<Cand, 7> cands{{
        {out.dcrFlexure, "Flexao ELU (NBR 6118 Sec. 17.2.2)"},
        {out.dcrDuctility, "Ductilidade x/d (NBR 6118 Sec. 14.6.4.3)"},
        {out.dcrShearStrut, "Biela Comprimida VRd2 (NBR 6118 Sec. 17.4.2.2)"},
        {out.dcrShearStirrup, "Estribos VRd3 (NBR 6118 Sec. 17.4.2.2)"},
        {out.dcrMinSteel, "Armadura Minima (NBR 6118 Tab. 17.3)"},
        {out.dcrMaxSteel, "Armadura Maxima 4% (NBR 6118 Sec. 17.3.5.2.4)"},
        {out.dcrCrackWidth, "Fissuracao ELS-W (NBR 6118 Sec. 17.3.3.2 / Tab. 13.4)"}
    }};
    auto best = std::max_element(cands.begin(), cands.end(),
                                 [](const Cand& a, const Cand& b) { return a.v < b.v; });
    out.dcrMax = best->v;
    out.governingLimitState = best->lbl;
    out.passed = (out.dcrMax <= 1.0 + 1e-6);
    return out;
}

ConcreteColumnCheckResult NbrCodeChecks::checkConcreteColumnBiaxial6118(const ConcreteColumnSpec& col,
                                                                        const ConcreteColumnForces& forces) {
    ConcreteColumnCheckResult out;
    const auto f = concreteFactors(col.situation);
    const double bx = std::max(0.10, col.bx_m);
    const double hy = std::max(0.10, col.hy_m);
    const double dPrime = std::clamp(col.coverToCentroid_m, 0.02, 0.25 * std::min(bx, hy));
    const double fck = std::clamp(col.fck_MPa, 15.0, 90.0);
    const double fcd = fck / f.gamma_c;
    const double fyd = rebarCharacteristicYield_MPa(col.rebarGrade) / f.gamma_s;

    const double Ac_cm2 = (bx * hy) * 1e4;
    out.As_min_cm2 = 0.004 * Ac_cm2; // Sec. 17.3.5.3.1 piso absoluto
    out.As_max_cm2 = 0.080 * Ac_cm2; // Sec. 17.3.5.3.2
    const double AsTot = std::max(1e-6, col.totalAs_cm2);
    out.rhoPercent = 100.0 * (AsTot / Ac_cm2);
    out.dcrSteelMin = out.As_min_cm2 / AsTot;
    out.dcrSteelMax = AsTot / out.As_max_cm2;

    const auto bars = buildPerimeterBars(bx, hy, dPrime, AsTot, col.barsPerSide);
    const auto resPureTens = evalColumnUlsState(bx, hy, dPrime, fck, fcd, fyd, bars, 0.0, 0.0);
    const auto resPureComp = evalColumnUlsState(bx, hy, dPrime, fck, fcd, fyd, bars, 0.0, 1.0);
    out.N_Rd_pureTens_kN = resPureTens.N_kN;
    out.N_Rd_pureComp_kN = std::abs(resPureComp.N_kN);

    const double dcrAxial = (forces.N_Sd_kN >= 0.0)
        ? (forces.N_Sd_kN / std::max(1e-6, out.N_Rd_pureTens_kN))
        : (std::abs(forces.N_Sd_kN) / std::max(1e-6, out.N_Rd_pureComp_kN));

    // Capacidades uniaxiais em Mx e My para N_Sd
    FiberForceResult ux{}, uy{};
    if (solveColumnMomentAtN(bx, hy, dPrime, fck, fcd, fyd, bars, 0.0, forces.N_Sd_kN, ux)) {
        out.Mx_Rd_atNSd_kNm = std::abs(ux.Mx_kNm);
    }
    if (solveColumnMomentAtN(bx, hy, dPrime, fck, fcd, fyd, bars, 0.5 * kPi, forces.N_Sd_kN, uy)) {
        out.My_Rd_atNSd_kNm = std::abs(uy.My_kNm);
    }

    const double mMag = std::hypot(forces.Mx_Sd_kNm, forces.My_Sd_kNm);
    double dcrMomentBiaxial = 0.0;
    if (mMag > 1e-6) {
        if (dcrAxial >= 1.0) {
            dcrMomentBiaxial = dcrAxial;
        } else {
            // Amostra o contorno convexo (Mx(phi), My(phi)) em N = N_Sd para interseccao radial exata
            constexpr int kAngles = 36;
            std::vector<std::pair<double, double>> poly;
            poly.reserve(kAngles);
            for (int k = 0; k < kAngles; ++k) {
                const double phi = (2.0 * kPi * k) / kAngles;
                FiberForceResult rk{};
                if (solveColumnMomentAtN(bx, hy, dPrime, fck, fcd, fyd, bars, phi, forces.N_Sd_kN, rk)) {
                    poly.emplace_back(rk.Mx_kNm, rk.My_kNm);
                }
            }
            double maxRatio = 0.0;
            for (size_t i = 0; i < poly.size(); ++i) {
                const auto [x1, y1] = poly[i];
                const auto [x2, y2] = poly[(i + 1) % poly.size()];
                // Cruzamento do raio s*(Mx_Sd, My_Sd) com o segmento (x1,y1)->(x2,y2)
                const double dx = x2 - x1;
                const double dy = y2 - y1;
                const double det = forces.Mx_Sd_kNm * (-dy) - forces.My_Sd_kNm * (-dx);
                if (std::abs(det) < 1e-12) continue;
                const double s = (x1 * (-dy) - y1 * (-dx)) / det;
                const double u = (forces.Mx_Sd_kNm * y1 - forces.My_Sd_kNm * x1) / det;
                if (s > 1e-9 && u >= -1e-6 && u <= 1.0 + 1e-6) {
                    maxRatio = std::max(maxRatio, 1.0 / s);
                }
            }
            dcrMomentBiaxial = (maxRatio > 0.0) ? maxRatio : 999.0;
        }
    }

    out.dcrPMM = std::max(dcrAxial, dcrMomentBiaxial);

    struct Cand { double v; const char* lbl; };
    const std::array<Cand, 3> cands{{
        {out.dcrPMM, "Interacao Flexo-Compressao Biaxial P-Mx-My (NBR 6118 Sec. 17.2.2)"},
        {out.dcrSteelMin, "Taxa Minima Longitudinal 0.4% (NBR 6118 Sec. 17.3.5.3.1)"},
        {out.dcrSteelMax, "Taxa Maxima Longitudinal 8% (NBR 6118 Sec. 17.3.5.3.2)"}
    }};
    auto best = std::max_element(cands.begin(), cands.end(),
                                 [](const Cand& a, const Cand& b) { return a.v < b.v; });
    out.dcrMax = best->v;
    out.governingLimitState = best->lbl;
    out.passed = (out.dcrMax <= 1.0 + 1e-6);
    return out;
}

ServiceabilityDisplacementResult NbrCodeChecks::checkServiceabilityDeflections6118(
    const ServiceabilityDisplacementInput& in) {
    ServiceabilityDisplacementResult out;
    const double span = std::max(0.1, in.span_m);
    const double H = std::max(0.5, in.buildingHeight_m);
    const double Hi = std::max(0.5, in.storyHeight_m);

    out.dcrTotalDeflection = std::abs(in.midspanDeflectionTotal_m) / (span / 250.0);
    out.dcrVibrationDeflection = std::abs(in.midspanDeflectionVibration_m) / (span / 350.0);
    out.dcrGlobalDrift = std::abs(in.topHorizontalDriftWind_m) / (H / 1700.0);
    out.dcrInterstoryDrift = std::abs(in.interstoryDriftWind_m) / (Hi / 850.0);
    const double partitionLim = std::min(span / 500.0, 0.010);
    out.dcrPartitionDeflection = std::abs(in.partitionSpanDeflection_m) / partitionLim;

    struct Cand { double v; const char* lbl; };
    const std::array<Cand, 5> cands{{
        {out.dcrTotalDeflection, "Aceitabilidade Sensorial/Visual L/250"},
        {out.dcrVibrationDeflection, "Vibracoes L/350"},
        {out.dcrGlobalDrift, "Deslocamento Lateral Global H/1700"},
        {out.dcrInterstoryDrift, "Deslocamento Interpavimento Hi/850"},
        {out.dcrPartitionDeflection, "Alvenarias e Paredes Divisorias min(L/500, 10 mm)"}
    }};
    auto best = std::max_element(cands.begin(), cands.end(),
                                 [](const Cand& a, const Cand& b) { return a.v < b.v; });
    out.dcrMax = best->v;
    out.governingCriterion = best->lbl;
    out.passed = (out.dcrMax <= 1.0 + 1e-6);
    return out;
}

ShallowFoundationResult NbrCodeChecks::checkShallowFoundation6122(const ShallowFoundationInput& in) {
    ShallowFoundationResult out;
    const double B = std::max(0.2, in.B_m);
    const double L = std::max(0.2, in.L_m);
    const double A = B * L;
    const double Wx = B * L * L / 6.0;
    const double Wy = L * B * B / 6.0;

    // NBR 6122:2022 Tab. 1 + Sec. 6.3
    const double windBoost = in.windIsPrincipal ? (in.useDesignMethod ? 1.10 : 1.15) : 1.0;
    if (in.useDesignMethod) {
        const double gammaM = in.hasTwoLoadTests ? 1.40 : 2.15;
        out.safetyFactorUsed = gammaM;
        out.sigmaLimit_kPa = (in.qUlt_kPa / gammaM) * windBoost;
    } else {
        out.safetyFactorUsed = in.hasTwoLoadTests ? 2.00 : 3.00;
        out.sigmaLimit_kPa = in.sigmaAdm_kPa * windBoost;
    }

    const double sigMean = in.N_k_kN / A;
    const double dSigX = std::abs(in.Mx_k_kNm) / Wx;
    const double dSigY = std::abs(in.My_k_kNm) / Wy;
    out.sigmaMax_kPa = sigMean + dSigX + dSigY;
    out.sigmaMin_kPa = sigMean - dSigX - dSigY;
    out.hasUplift = (out.sigmaMin_kPa < -1e-6);

    const double Nabs = std::max(1e-6, in.N_k_kN);
    const double ex = std::abs(in.My_k_kNm) / Nabs;
    const double ey = std::abs(in.Mx_k_kNm) / Nabs;
    out.eccentricityRatioX = ex / (B / 6.0);
    out.eccentricityRatioY = ey / (L / 6.0);

    out.dcrBearing = (out.sigmaLimit_kPa > 1e-6) ? (out.sigmaMax_kPa / out.sigmaLimit_kPa) : 999.0;
    out.passed = (in.N_k_kN > 0.0) && !out.hasUplift && (out.dcrBearing <= 1.0 + 1e-6);
    return out;
}

} // namespace omniframe::kernel::codes
