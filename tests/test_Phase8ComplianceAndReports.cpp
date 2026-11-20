#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/PdfReportGenerator.hpp"
#include "io/ProjectSerializer.hpp"
#include "kernel/analysis/NonlinearSolver.hpp"
#include "kernel/codes/NbrCodeChecks.hpp"
#include "kernel/mesh/MeshGenerator.hpp"
#include "kernel/solver/LinearSolverSuite.hpp"
#include "solver/FemSolver3D.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace omniframe;
using namespace omniframe::kernel::codes;

namespace {

bool containsCodeStatus(const std::vector<StandardCoverageInfo>& matrix,
                        const std::string& codeSubstr,
                        ComplianceScopeStatus expectedStatus) {
    for (const auto& entry : matrix) {
        if (entry.codeId.find(codeSubstr) != std::string::npos) {
            return entry.status == expectedStatus;
        }
    }
    return false;
}

void testCoverageMatrixAndFactors() {
    const auto matrix = NbrCodeChecks::getStandardCoverageMatrix();
    TEST_ASSERT(matrix.size() >= 11, "Matriz de cobertura NBR deve listar normas locais e ausentes");

    TEST_ASSERT(containsCodeStatus(matrix, "6118", ComplianceScopeStatus::LocalPdfVerified),
                "NBR 6118:2023 deve estar marcada como LocalPdfVerified");
    TEST_ASSERT(containsCodeStatus(matrix, "8800", ComplianceScopeStatus::LocalPdfVerified),
                "NBR 8800:2024 deve estar marcada como LocalPdfVerified");
    TEST_ASSERT(containsCodeStatus(matrix, "6120", ComplianceScopeStatus::LocalPdfVerified),
                "NBR 6120:2019 deve estar marcada como LocalPdfVerified");
    TEST_ASSERT(containsCodeStatus(matrix, "6122", ComplianceScopeStatus::LocalPdfVerified),
                "NBR 6122:2022 deve estar marcada como LocalPdfVerified");
    TEST_ASSERT(containsCodeStatus(matrix, "6123", ComplianceScopeStatus::LocalPdfVerified),
                "NBR 6123:2023 deve estar marcada como LocalPdfVerified");
    TEST_ASSERT(containsCodeStatus(matrix, "7480", ComplianceScopeStatus::LocalPdfVerified),
                "NBR 7480:2022 deve estar marcada como LocalPdfVerified");

    TEST_ASSERT(containsCodeStatus(matrix, "8681", ComplianceScopeStatus::OutOfLocalPdfScope),
                "NBR 8681 ausente deve ser marcada explicitamente como OutOfLocalPdfScope");
    TEST_ASSERT(containsCodeStatus(matrix, "14762", ComplianceScopeStatus::OutOfLocalPdfScope),
                "NBR 14762 ausente deve ser marcada explicitamente como OutOfLocalPdfScope");
    TEST_ASSERT(containsCodeStatus(matrix, "15421", ComplianceScopeStatus::OutOfLocalPdfScope),
                "NBR 15421 ausente deve ser marcada explicitamente como OutOfLocalPdfScope");
    TEST_ASSERT(containsCodeStatus(matrix, "AISC", ComplianceScopeStatus::OutOfLocalPdfScope),
                "AISC 360 deve ser marcado como OutOfLocalPdfScope");

    const auto cNorm = NbrCodeChecks::concreteFactors(CombinationSituation::Normal);
    TEST_ASSERT(std::abs(cNorm.gamma_g_unfav - 1.40) < 1e-9 &&
                    std::abs(cNorm.gamma_q - 1.40) < 1e-9 &&
                    std::abs(cNorm.gamma_c - 1.40) < 1e-9 &&
                    std::abs(cNorm.gamma_s - 1.15) < 1e-9,
                "Coeficientes normais da NBR 6118 Tab. 11.1/12.1");

    const auto cExc = NbrCodeChecks::concreteFactors(CombinationSituation::Exceptional);
    TEST_ASSERT(std::abs(cExc.gamma_c - 1.20) < 1e-9 && std::abs(cExc.gamma_s - 1.00) < 1e-9,
                "Coeficientes excepcionais da NBR 6118 Tab. 12.1");

    const auto sCast = NbrCodeChecks::steelFactors(CombinationSituation::Normal, DeadLoadClass8800::CastInPlaceConcrete);
    TEST_ASSERT(std::abs(sCast.gamma_g_unfav - 1.35) < 1e-9 &&
                    std::abs(sCast.gamma_q - 1.50) < 1e-9 &&
                    std::abs(sCast.gamma_w - 1.40) < 1e-9 &&
                    std::abs(sCast.gamma_a1 - 1.10) < 1e-9 &&
                    std::abs(sCast.gamma_a2 - 1.35) < 1e-9,
                "Coeficientes normais da NBR 8800 Tab. 1/3");

    const auto psiRes = NbrCodeChecks::occupancyPsi(OccupancyCategory::Residential);
    TEST_ASSERT(std::abs(psiRes.psi0 - 0.5) < 1e-9 &&
                    std::abs(psiRes.psi1 - 0.4) < 1e-9 &&
                    std::abs(psiRes.psi2 - 0.3) < 1e-9,
                "Fatores psi residenciais (Tab. 11.2)");

    TEST_ASSERT(std::abs(NbrCodeChecks::unpositionedPartitionSurcharge6120_kPa(1.0) - 0.50) < 1e-9 &&
                    std::abs(NbrCodeChecks::unpositionedPartitionSurcharge6120_kPa(2.0) - 0.75) < 1e-9 &&
                    std::abs(NbrCodeChecks::unpositionedPartitionSurcharge6120_kPa(3.0) - 1.00) < 1e-9 &&
                    NbrCodeChecks::unpositionedPartitionSurcharge6120_kPa(3.5) < 0.0,
                "Divisorias sem posicao definida NBR 6120 Tab. 11");

    const auto wind = NbrCodeChecks::evaluateWind6123({40.0, 1.0, 1.0, 1.0});
    TEST_ASSERT(std::abs(wind.vk_ms - 40.0) < 1e-9 && std::abs(wind.q_Pa - 980.8) < 1e-6 &&
                    std::abs(wind.q_kPa - 0.9808) < 1e-6,
                "Pressao dinamica NBR 6123 q = 0.613 * Vk^2");

    TEST_ASSERT(std::abs(NbrCodeChecks::rebarCharacteristicYield_MPa(RebarGrade7480::CA50) - 500.0) < 1e-9 &&
                    std::abs(NbrCodeChecks::rebarBondFactorEta1(RebarGrade7480::CA50) - 2.25) < 1e-9 &&
                    std::abs(NbrCodeChecks::concreteDuctilityLimitXOverD(30.0) - 0.45) < 1e-9 &&
                    std::abs(NbrCodeChecks::concreteDuctilityLimitXOverD(60.0) - 0.35) < 1e-9,
                "Parametros NBR 7480 e limites x/d NBR 6118 Sec. 14.6.4.3");

    std::cout << "[OK] testCoverageMatrixAndFactors\n";
}

void testSteelMemberNbr8800() {
    SteelSectionSpec w310;
    w310.kind = SteelSectionKind::IShapeRolled;
    w310.d_m = 0.310;
    w310.bf_m = 0.165;
    w310.tf_m = 0.0118;
    w310.tw_m = 0.0079;
    w310.fillet_m = 0.010;
    w310.fy_MPa = 345.0;
    w310.fu_MPa = 450.0;
    w310.E_MPa = 200000.0;
    w310.G_MPa = 76923.0769;
    w310.Lx_m = 3.0;
    w310.Ly_m = 3.0;
    w310.Lz_m = 3.0;
    w310.Lb_m = 3.0;
    w310.Cb = 1.0;

    SteelDesignForces okForces;
    okForces.N_Sd_kN = -350.0;
    okForces.Mx_Sd_kNm = 80.0;
    okForces.My_Sd_kNm = 12.0;
    okForces.Vy_Sd_kN = 90.0;

    const auto okRes = NbrCodeChecks::checkSteelMember8800(w310, okForces);
    TEST_ASSERT(okRes.passed && okRes.dcrMax > 0.2 && okRes.dcrMax < 1.0,
                "W310x38.7 equivalente sob carga moderada deve passar na NBR 8800:2024");
    TEST_ASSERT(okRes.Nc_Rd_kN > 500.0 && okRes.Mx_Rd_kNm > 120.0 && okRes.Vy_Rd_kN > 300.0,
                "Resistencias de calculo NBR 8800 devem estar na ordem fisica esperada");
    TEST_ASSERT(okRes.chiReduction > 0.3 && okRes.chiReduction <= 1.0,
                "Fator de reducao chi da curva de flambagem NBR 8800 deve estar em (0.3, 1.0]");

    SteelDesignForces overForces = okForces;
    overForces.N_Sd_kN = -3500.0;
    overForces.Mx_Sd_kNm = 350.0;
    const auto failRes = NbrCodeChecks::checkSteelMember8800(w310, overForces);
    TEST_ASSERT(!failRes.passed && failRes.dcrMax > 1.0 && failRes.dcrInteraction > 1.0,
                "Perfil de aco sobrecarregado deve falhar na interacao NBR 8800 5.5.1.2");

    SteelSectionSpec slenderSpec = w310;
    slenderSpec.Lx_m = 12.0;
    slenderSpec.Ly_m = 12.0;
    slenderSpec.Lz_m = 12.0;
    SteelDesignForces lightComp;
    lightComp.N_Sd_kN = -50.0;
    const auto slenderRes = NbrCodeChecks::checkSteelMember8800(slenderSpec, lightComp);
    TEST_ASSERT(!slenderRes.passed && slenderRes.dcrSlenderness > 1.0 &&
                    slenderRes.maxSlenderness > slenderRes.slendernessLimit,
                "Barra comprimida com lambda > 200 deve acusar esbeltez excessiva (NBR 8800 5.3.4)");

    // Tracao com ligacao reduzida (Ct=0.85, An/Ag=0.80)
    SteelSectionSpec tensSpec = w310;
    tensSpec.Ct = 0.85;
    tensSpec.netAreaRatio = 0.80;
    SteelDesignForces tensForce;
    tensForce.N_Sd_kN = 800.0;
    const auto tensRes = NbrCodeChecks::checkSteelMember8800(tensSpec, tensForce);
    TEST_ASSERT(tensRes.Nt_Rd_kN > 900.0 && tensRes.Nt_Rd_kN < 1800.0 && tensRes.passed,
                "Tracao NBR 8800 Sec. 5.2 deve combinar escoamento da secao bruta e ruptura da secao liquida");

    std::cout << "[OK] testSteelMemberNbr8800 (DCR ok=" << okRes.dcrMax
              << ", Nc_Rd=" << okRes.Nc_Rd_kN << " kN, Mx_Rd=" << okRes.Mx_Rd_kNm << " kNm)\n";
}

void testNbr8800FltMonotonicityAndCapWithCb() {
    auto verifyFltSweep = [](const SteelSectionSpec& baseSpec, double lbMin, double lbMax, const char* label) {
        SteelDesignForces zeroF;
        // Teto M_cap = Mx_Rd no limite plastico (Lb -> 0)
        SteelSectionSpec capSpec = baseSpec;
        capSpec.Lb_m = 1e-4;
        capSpec.Cb = 1.0;
        const double mCap_kNm = NbrCodeChecks::checkSteelMember8800(capSpec, zeroF).Mx_Rd_kNm;
        TEST_ASSERT(mCap_kNm > 1.0, "Capacidade plastica M_cap deve ser positiva");

        constexpr int N = 40;
        for (double cb : {1.0, 1.67, 2.5}) {
            double prevMx = 1e30;
            double maxObserved = 0.0;
            for (int i = 0; i <= N; ++i) {
                const double t = static_cast<double>(i) / static_cast<double>(N);
                SteelSectionSpec sp = baseSpec;
                sp.Lb_m = lbMin + t * (lbMax - lbMin);
                sp.Cb = cb;
                const auto res = NbrCodeChecks::checkSteelMember8800(sp, zeroF);
                const double mx = res.Mx_Rd_kNm;
                TEST_ASSERT(std::isfinite(mx) && mx > 0.0, label);
                TEST_ASSERT(mx <= mCap_kNm + 1e-9,
                            "NBR 8800 FLT: M_n nao pode exceder o teto M_cap mesmo para Cb > 1 ([AUD-004])");
                TEST_ASSERT(mx <= prevMx + 1e-9,
                            "NBR 8800 FLT: M_n deve ser monotonicamente nao crescente com lambda/Lb ([AUD-004])");
                prevMx = mx;
                maxObserved = std::max(maxObserved, mx);
            }
            TEST_ASSERT(maxObserved <= mCap_kNm + 1e-9, label);
        }
    };

    // 1. IShapeRolled compacto
    {
        SteelSectionSpec sp;
        sp.kind = SteelSectionKind::IShapeRolled;
        sp.d_m = 0.400; sp.bf_m = 0.200; sp.tf_m = 0.020; sp.tw_m = 0.010; sp.fillet_m = 0.012;
        sp.fy_MPa = 345.0; sp.fu_MPa = 450.0; sp.E_MPa = 200000.0; sp.G_MPa = 76923.0769;
        sp.Lx_m = 1.0; sp.Ly_m = 1.0; sp.Lz_m = 1.0;
        // ry ~= 0.048m; lamP ~= 42 -> Lb_P ~= 2.0m; lamR ~= 140 -> Lb_R ~= 6.7m
        verifyFltSweep(sp, 0.5 * 2.0, 2.0 * 7.0, "IShapeRolled FLT sweep");
    }

    // 2. IShapeWelded de alma esbelta (Anexo E)
    {
        SteelSectionSpec sp;
        sp.kind = SteelSectionKind::IShapeWelded;
        sp.d_m = 1.200; sp.bf_m = 0.300; sp.tf_m = 0.025; sp.tw_m = 0.006; sp.fillet_m = 0.0;
        sp.fy_MPa = 345.0; sp.fu_MPa = 450.0; sp.E_MPa = 200000.0; sp.G_MPa = 76923.0769;
        sp.Lx_m = 1.0; sp.Ly_m = 1.0; sp.Lz_m = 1.0;
        // ryc ~= 0.085m; lamP_pg ~= 26.5 -> 2.2m; lamR_pg ~= 90 -> 7.7m
        verifyFltSweep(sp, 0.5 * 2.2, 2.0 * 8.0, "IShapeWelded Anexo E FLT sweep");
    }

    // 3. RectangularTube (BoxRHS)
    {
        SteelSectionSpec sp;
        sp.kind = SteelSectionKind::RectangularTube;
        sp.d_m = 0.300; sp.bf_m = 0.150; sp.tf_m = 0.016; sp.tw_m = 0.016; sp.fillet_m = 0.0;
        sp.fy_MPa = 345.0; sp.fu_MPa = 450.0; sp.E_MPa = 200000.0; sp.G_MPa = 76923.0769;
        sp.Lx_m = 1.0; sp.Ly_m = 1.0; sp.Lz_m = 1.0;
        // Faixa ampla para cobrir [0.5*lamPLT, 2.0*lamRLT] em tubo fechado
        verifyFltSweep(sp, 2.0, 150.0, "RectangularTube FLT sweep");
    }

    // 4. RectangularSolid
    {
        SteelSectionSpec sp;
        sp.kind = SteelSectionKind::RectangularSolid;
        sp.d_m = 0.250; sp.bf_m = 0.040; sp.tf_m = 0.040; sp.tw_m = 0.040; sp.fillet_m = 0.0;
        sp.fy_MPa = 250.0; sp.fu_MPa = 400.0; sp.E_MPa = 200000.0; sp.G_MPa = 76923.0769;
        sp.Lx_m = 1.0; sp.Ly_m = 1.0; sp.Lz_m = 1.0;
        verifyFltSweep(sp, 0.20, 24.0, "RectangularSolid FLT sweep");
    }

    std::cout << "[OK] testNbr8800FltMonotonicityAndCapWithCb\n";
}

void testConcreteBeamAndColumnNbr6118() {
    ConcreteBeamSpec beam;
    beam.bw_m = 0.20;
    beam.h_m = 0.50;
    beam.d_m = 0.45;
    beam.dp_m = 0.05;
    beam.fck_MPa = 30.0;
    beam.rebarGrade = RebarGrade7480::CA50;
    beam.caa = EnvironmentalAggressivenessClass::CAA_II;

    ConcreteBeamForces beamDesignF;
    beamDesignF.M_Sd_kNm = 110.0;
    beamDesignF.V_Sd_kN = 85.0;
    beamDesignF.M_Sd_els_kNm = 75.0;

    // 1. Modo dimensionamento automatico
    const auto designRes = NbrCodeChecks::checkConcreteBeam6118(beam, beamDesignF);
    TEST_ASSERT(designRes.passed && designRes.requiredAs_cm2 > designRes.minAs_cm2,
                "Dimensionamento de viga NBR 6118 deve calcular As > As,min e passar");
    TEST_ASSERT(designRes.xOverD > 0.0 && designRes.xOverD <= 0.45,
                "Linha neutra x/d deve respeitar limite de ductilidade 0.45 para fck <= 50 MPa");
    TEST_ASSERT(designRes.V_Rd2_kN > beamDesignF.V_Sd_kN && designRes.requiredAswPerS_cm2PerM > 0.0,
                "Verificacao de biela e estribos (Modelo I) deve convergir com V_Rd2 > V_Sd");
    TEST_ASSERT(designRes.wk_mm > 0.0 && designRes.wk_mm <= designRes.wkLimit_mm + 1e-9,
                "Abertura de fissura wk (Sec. 17.3.3.2) deve ser calculada e respeitar CAA II (0.3 mm)");

    // 2. Modo verificacao com armadura insuficiente
    ConcreteBeamSpec underBeam = beam;
    underBeam.providedAs_cm2 = 2.0;
    underBeam.providedAswPerS_cm2PerM = 2.5;
    const auto underRes = NbrCodeChecks::checkConcreteBeam6118(underBeam, beamDesignF);
    TEST_ASSERT(!underRes.passed && underRes.dcrFlexure > 1.0,
                "Viga com As fornecida insuficiente deve falhar na verificacao a flexao");

    // 3. Pilar retangular biaxial P-Mx-My
    ConcreteColumnSpec col;
    col.bx_m = 0.30;
    col.hy_m = 0.40;
    col.coverToCentroid_m = 0.04;
    col.fck_MPa = 30.0;
    col.totalAs_cm2 = 20.0;
    col.barsPerSide = 3;

    ConcreteColumnForces safeColF;
    safeColF.N_Sd_kN = -900.0;
    safeColF.Mx_Sd_kNm = 55.0;
    safeColF.My_Sd_kNm = 35.0;

    const auto colSafeRes = NbrCodeChecks::checkConcreteColumnBiaxial6118(col, safeColF);
    TEST_ASSERT(colSafeRes.passed && colSafeRes.dcrPMM > 0.3 && colSafeRes.dcrPMM < 1.0,
                "Pilar 30x40 com 20 cm2 sob N=-900 kN e momentos moderados deve passar na superficie P-Mx-My");
    TEST_ASSERT(colSafeRes.N_Rd_pureComp_kN > 2000.0 && colSafeRes.Mx_Rd_atNSd_kNm > 100.0,
                "Capacidade axial e momentos resistentes do pilar devem ser consistentes");

    // Simetria biaxial ao permutar eixos x/y
    ConcreteColumnSpec swappedCol = col;
    swappedCol.bx_m = col.hy_m;
    swappedCol.hy_m = col.bx_m;
    ConcreteColumnForces swappedF = safeColF;
    swappedF.Mx_Sd_kNm = safeColF.My_Sd_kNm;
    swappedF.My_Sd_kNm = safeColF.Mx_Sd_kNm;
    const auto colSwappedRes = NbrCodeChecks::checkConcreteColumnBiaxial6118(swappedCol, swappedF);
    TEST_ASSERT(std::abs(colSwappedRes.dcrPMM - colSafeRes.dcrPMM) < 1e-4,
                "Integracao por fibras P-Mx-My deve ser simetrica ao permutar eixos");

    ConcreteColumnForces overloadColF;
    overloadColF.N_Sd_kN = -3200.0;
    overloadColF.Mx_Sd_kNm = 200.0;
    overloadColF.My_Sd_kNm = 120.0;
    const auto colFailRes = NbrCodeChecks::checkConcreteColumnBiaxial6118(col, overloadColF);
    TEST_ASSERT(!colFailRes.passed && colFailRes.dcrPMM > 1.0,
                "Pilar sobrecarregado deve falhar no nucleo de interacao P-Mx-My");

    std::cout << "[OK] testConcreteBeamAndColumnNbr6118 (As_req=" << designRes.requiredAs_cm2
              << " cm2, wk=" << designRes.wk_mm << " mm, col DCR=" << colSafeRes.dcrPMM << ")\n";
}

void testServiceability6118AndFoundation6122() {
    ServiceabilityDisplacementInput elsIn;
    elsIn.span_m = 6.0;
    elsIn.midspanDeflectionTotal_m = 0.015;     // L/250 = 0.024 -> ok
    elsIn.midspanDeflectionVibration_m = 0.008; // L/350 = 0.0171 -> ok
    elsIn.buildingHeight_m = 15.0;
    elsIn.topHorizontalDriftWind_m = 0.005;     // H/1700 = 0.0088 -> ok
    elsIn.storyHeight_m = 3.0;
    elsIn.interstoryDriftWind_m = 0.002;        // Hi/850 = 0.00353 -> ok
    elsIn.partitionSpanDeflection_m = 0.006;    // min(6/500=0.012, 0.010)=0.010 -> ok

    const auto elsOk = NbrCodeChecks::checkServiceabilityDeflections6118(elsIn);
    TEST_ASSERT(elsOk.passed && elsOk.dcrMax < 1.0, "ELS Tab. 13.3 dentro dos limites deve passar");

    elsIn.midspanDeflectionTotal_m = 0.030;
    const auto elsFail = NbrCodeChecks::checkServiceabilityDeflections6118(elsIn);
    TEST_ASSERT(!elsFail.passed && elsFail.dcrTotalDeflection > 1.0,
                "Flecha > L/250 deve falhar no ELS NBR 6118 Tab. 13.3");

    ShallowFoundationInput footOk;
    footOk.B_m = 2.0;
    footOk.L_m = 2.0;
    footOk.N_k_kN = 400.0;
    footOk.Mx_k_kNm = 20.0;
    footOk.sigmaAdm_kPa = 200.0;
    const auto fResOk = NbrCodeChecks::checkShallowFoundation6122(footOk);
    TEST_ASSERT(fResOk.passed && !fResOk.hasUplift && fResOk.dcrBearing < 1.0,
                "Sapata centrada 2x2m com 400 kN e momento pequeno deve passar sem descolamento");

    ShallowFoundationInput footUplift = footOk;
    footUplift.Mx_k_kNm = 200.0;
    const auto fResUp = NbrCodeChecks::checkShallowFoundation6122(footUplift);
    TEST_ASSERT(fResUp.hasUplift && fResUp.sigmaMin_kPa < 1e-6,
                "Excentricidade e > L/6 deve sinalizar levantamento/descolamento na base (NBR 6122)");

    std::cout << "[OK] testServiceability6118AndFoundation6122\n";
}

bool verifyPdfMagicAndSize(const QString& path, qint64 minBytes) {
    QFile f(path);
    if (!f.exists() || f.size() < minBytes) {
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray hdr = f.read(8);
    return hdr.startsWith("%PDF-1.");
}

void testExecutivePdfAndCliEndToEnd() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "Diretorio temporario deve ser valido");

    // 1. Modelo exemplo de concreto armado (portico espacial)
    StructuralModel rcModel = StructuralModel::createBuildingFrameExample();
    SolverResult rcRes = FemSolver3D::solve(rcModel);
    TEST_ASSERT(rcRes.success, "Portico exemplo de concreto deve resolver estaticamente");

    // Snapshot sintetico simulando captura do Viewport3D
    QImage snap(360, 220, QImage::Format_ARGB32);
    snap.fill(QColor(22, 27, 34));
    {
        QPainter sp(&snap);
        sp.setPen(QPen(QColor(56, 189, 248), 2.5));
        sp.drawRect(60, 40, 240, 140);
        sp.drawLine(60, 110, 300, 110);
    }

    io::PdfReportOptions opts;
    opts.projectTitle = "Edificio Comercial OmniFrame - Auditoria Fase 8";
    opts.engineerName = "Eng. Teste Automatizado CREA-SP";
    opts.clientName = "OmniFrame Structural Validation";
    opts.viewportSnapshot = snap;

    const auto audit = io::PdfReportGenerator::evaluateModelNbrCompliance(rcModel, rcRes, opts);
    TEST_ASSERT(!audit.coverageMatrix.empty() && !audit.foundations.empty(),
                "Auditoria NBR deve cobrir fundacoes e matriz normativa");
    TEST_ASSERT(audit.concreteBeams.size() + audit.concreteColumns.size() > 0,
                "Auditoria NBR do portico de concreto deve classificar vigas e/ou pilares");

    const QString rcPdfPath = tmp.filePath("memorial_concreto_fase8.pdf");
    QString errMsg;
    const bool rcPdfOk = io::PdfReportGenerator::generateExecutivePdf(rcPdfPath, rcModel, rcRes, opts, &errMsg);
    TEST_ASSERT(rcPdfOk && errMsg.isEmpty() && verifyPdfMagicAndSize(rcPdfPath, 15000),
                "Geracao de PDF executivo (concreto 24 secoes) deve produzir PDF valido > 15 KB");

    // 2. Modelo portico em aco ASTM A572 Gr50 (NBR 8800:2024)
    StructuralModel steelModel;
    Material steelMat(1, "Aco ASTM A572 Gr50", 200.0e6, 0.30, 1.2e-5, 77.0, 7850.0);
    steelMat.sigmaY = 345000.0;
    steelModel.addMaterial(steelMat);
    steelModel.addCrossSection(CrossSection::makeIShape(1, "W310x38.7", 0.310, 0.165, 0.0118, 0.0079));
    const NodeId s1 = steelModel.addNode(0.0, 0.0, 0.0);
    const NodeId s2 = steelModel.addNode(6.0, 0.0, 0.0);
    const NodeId s3 = steelModel.addNode(0.0, 0.0, 4.0);
    const NodeId s4 = steelModel.addNode(6.0, 0.0, 4.0);
    steelModel.findNode(s1)->support = SupportRestraint::makeFixed();
    steelModel.findNode(s2)->support = SupportRestraint::makeFixed();
    steelModel.addElement(s1, s3, 1, 1);
    steelModel.addElement(s2, s4, 1, 1);
    Element girder{3, s3, s4, 1, 1};
    girder.distLoad.active = true;
    girder.distLoad.coordSystem = CoordSystem::Global;
    girder.distLoad.w = {0.0, 0.0, -25.0};
    steelModel.addElement(girder);
    if (auto* nd3 = steelModel.findNode(s3)) {
        nd3->load.force.x = 12.0;
        nd3->load.force.z = -30.0;
    }

    SolverResult steelRes = FemSolver3D::solve(steelModel);
    TEST_ASSERT(steelRes.success, "Portico metalico NBR 8800 deve resolver estaticamente");
    const auto steelAudit = io::PdfReportGenerator::evaluateModelNbrCompliance(steelModel, steelRes);
    TEST_ASSERT(steelAudit.steelMembers.size() == 3,
                "Auditoria do portico metalico deve avaliar todas as 3 barras pela NBR 8800:2024");

    // 2b. [AUD-001] Barra metalica W200x35.9 (ASTM A36) sob tracao de 5000 kN nao pode dar falso positivo
    {
        StructuralModel tensBarModel;
        tensBarModel.getMaterials().clear();
        tensBarModel.getCrossSections().clear();
        Material a36 = Material::makeStructuralSteel(1);
        a36.sigmaY = 250000.0; // 250 MPa em kN/m^2
        const MaterialId tMid = tensBarModel.addMaterial(a36);
        const SectionId tSid = tensBarModel.addCrossSection(
            CrossSection::makeIShape(1, "W200x35.9", 0.201, 0.165, 0.0102, 0.0062));
        const NodeId t1 = tensBarModel.addNode(0.0, 0.0, 0.0);
        const NodeId t2 = tensBarModel.addNode(4.0, 0.0, 0.0);
        tensBarModel.findNode(t1)->support = SupportRestraint::makeFixed();
        tensBarModel.findNode(t2)->load.force.x = 5000.0; // +5000 kN tracao
        tensBarModel.addElement(t1, t2, tMid, tSid);

        const SolverResult tensSolve = FemSolver3D::solve(tensBarModel);
        TEST_ASSERT(tensSolve.success, "Barra W200x35.9 tracionada deve resolver estaticamente");
        const auto tensAudit = io::PdfReportGenerator::evaluateModelNbrCompliance(tensBarModel, tensSolve);
        TEST_ASSERT(tensAudit.steelMembers.size() == 1 && !tensAudit.passedAll &&
                        tensAudit.steelMembers[0].result.dcrAxial > 4.5,
                    "W200x35.9 A36 sob +5000 kN deve reprovar com dcrAxial > 4.5 ([AUD-001])");
    }

    const QString steelPdfPath = tmp.filePath("memorial_aco_fase8.pdf");
    TEST_ASSERT(io::PdfReportGenerator::generateExecutivePdf(steelPdfPath, steelModel, steelRes) &&
                    verifyPdfMagicAndSize(steelPdfPath, 12000),
                "PDF executivo do portico metalico deve gerar arquivo %PDF valido");

#ifdef OMNIFRAME_CLI_PATH
    // 3. CLI end-to-end --report pdf
    const QString modelPath = tmp.filePath("cli_frame_p8.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(modelPath, rcModel), "Salvar .omniframe para CLI");

    const QString cliPdfPath = tmp.filePath("cli_memorial_p8.pdf");
    QProcess proc;
    proc.start(QString::fromUtf8(OMNIFRAME_CLI_PATH),
               {"--analyze", modelPath, "--analysis", "static", "--report", "pdf", cliPdfPath, "--json"});
    TEST_ASSERT(proc.waitForStarted(10000) && proc.waitForFinished(30000),
                "omniframe-cli --report pdf deve concluir dentro do timeout");
    TEST_ASSERT(proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0,
                "omniframe-cli --report pdf deve sair com codigo 0");

    const QByteArray stdoutJson = proc.readAllStandardOutput();
    const QJsonDocument cliDoc = QJsonDocument::fromJson(stdoutJson);
    TEST_ASSERT(cliDoc.isObject() && cliDoc.object()["success"].toBool(),
                "omniframe-cli --json deve confirmar success=true");
    TEST_ASSERT(verifyPdfMagicAndSize(cliPdfPath, 15000),
                "PDF gerado pelo CLI headless deve existir, ser > 15 KB e iniciar com %PDF-1.");
#endif

    std::cout << "[OK] testExecutivePdfAndCliEndToEnd (RC beams=" << audit.concreteBeams.size()
              << ", RC cols=" << audit.concreteColumns.size()
              << ", Steel=" << steelAudit.steelMembers.size() << ")\n";
}

void testValidationIssuesFallbackEnergyAndSection9Reports() {
    // 1. ModelValidation::issues estruturado em modelo desconexo e sem apoios
    {
        StructuralModel badModel;
        const int matId = badModel.addMaterial(Material::makeStructuralSteel(1));
        const int secId = badModel.addCrossSection(CrossSection::makeRectangular(1, "Viga", 0.2, 0.4));
        const int n0 = badModel.addNode(0.0, 0.0, 0.0);
        const int n1 = badModel.addNode(2.0, 0.0, 0.0);
        const int n2 = badModel.addNode(10.0, 0.0, 0.0);
        const int n3 = badModel.addNode(12.0, 0.0, 0.0);
        badModel.addElement(n0, n1, matId, secId);
        badModel.addElement(n2, n3, matId, secId);

        const ModelValidation val = badModel.validate();
        TEST_ASSERT(!val.valid && !val.errors.empty() && !val.issues.empty(),
                    "Modelo sem apoios e desconexo deve gerar ModelValidation::issues");
        bool hasStructuredError = false;
        for (const auto& iss : val.issues) {
            if (iss.severity == IssueSeverity::Error && !iss.code.empty() && !iss.category.empty()) {
                hasStructuredError = true;
                break;
            }
        }
        TEST_ASSERT(hasStructuredError, "ValidationIssue deve conter severidade Error, code e category");
    }

    // 2. Fallback explicito e rastreado no LinearSolverSuite (LLT em matriz indefinida)
    {
        omniframe::kernel::SpMat A(2, 2);
        A.insert(0, 0) = 2.0;
        A.insert(1, 1) = -3.0;
        A.makeCompressed();
        omniframe::kernel::VecX b(2);
        b << 4.0, 6.0;

        omniframe::kernel::LinearSolverOptions fbOpts;
        fbOpts.type = omniframe::kernel::LinearSolverType::SimplicialLLT;
        fbOpts.allowFallback = true;
        omniframe::kernel::LinearSolverStats fbStats;
        const omniframe::kernel::VecX x = omniframe::kernel::LinearSolverSuite::solve(A, b, fbOpts, fbStats);
        TEST_ASSERT(fbStats.success &&
                    fbStats.solverUsed == omniframe::kernel::LinearSolverType::SimplicialLDLT &&
                    fbStats.message.find("fallback") != std::string::npos &&
                    std::abs(x(0) - 2.0) < 1e-10 && std::abs(x(1) + 2.0) < 1e-10,
                    "LLT com allowFallback=true deve recorrer ao LDLT e registrar fallback em stats.message");

        fbOpts.allowFallback = false;
        omniframe::kernel::LinearSolverStats noFbStats;
        omniframe::kernel::LinearSolverSuite::solve(A, b, fbOpts, noFbStats);
        TEST_ASSERT(!noFbStats.success && noFbStats.errorCode == "NOT_SPD",
                    "LLT com allowFallback=false deve falhar explicitamente em matriz indefinida");
    }

    // 3. NonlinearOptions::energyTol e NonlinearResult::convergenceDiagnostic
    {
        StructuralModel cantModel;
        cantModel.getMaterials().clear();
        cantModel.getCrossSections().clear();
        const int matId = cantModel.addMaterial(Material::makeStructuralSteel(1));
        const int secId = cantModel.addCrossSection(CrossSection::makeRectangular(1, "B 20x40", 0.20, 0.40));
        const int n0 = cantModel.addNode(0.0, 0.0, 0.0);
        const int n1 = cantModel.addNode(3.0, 0.0, 0.0);
        cantModel.findNode(n0)->support = SupportRestraint::makeFixed();
        cantModel.findNode(n1)->load.force = {0.0, 0.0, -20.0};
        cantModel.addElement(n0, n1, matId, secId);

        omniframe::kernel::NonlinearOptions nlo;
        nlo.numSteps = 5;
        nlo.energyTol = 1e-10;
        const auto nlRes = FemSolver3D::solveNonlinear(cantModel, nlo);
        TEST_ASSERT(nlRes.success && !nlRes.history.empty() && !nlRes.convergenceDiagnostic.empty(),
                    "solveNonlinear deve preencher convergenceDiagnostic e energyRatio");
        TEST_ASSERT(nlRes.history.back().energyRatio <= 1e-6,
                    "Criterio energetico no ultimo passo deve convergir abaixo de 1e-6");
    }

    // 4. Relatorios Secao 9 (HTML, Markdown e PDF A4) em modelo 1D + 2D continuo
    {
        StructuralModel coupled;
        coupled.getMaterials().clear();
        coupled.getCrossSections().clear();
        coupled.metadata().projectName = "Laje+Portico Secao 9";
        const int matId = coupled.addMaterial(Material::makeConcreteC30(1));
        const int secId = coupled.addCrossSection(CrossSection::makeRectangular(1, "Pilar 30x30", 0.30, 0.30));

        const std::array<std::pair<double, double>, 4> corners = {
            std::make_pair(0.0, 0.0), std::make_pair(4.0, 0.0),
            std::make_pair(4.0, 4.0), std::make_pair(0.0, 4.0)
        };
        for (const auto& c : corners) {
            const int b = coupled.addNode(c.first, c.second, 0.0);
            const int t = coupled.addNode(c.first, c.second, 3.0);
            coupled.findNode(b)->support = SupportRestraint::makeFixed();
            coupled.addElement(b, t, matId, secId);
        }
        coupled.surfaceMesh() = omniframe::kernel::MeshGenerator::generateStructuredQuadGrid(
            omniframe::kernel::Vec3(0.0, 0.0, 3.0),
            omniframe::kernel::Vec3(4.0, 0.0, 0.0),
            omniframe::kernel::Vec3(0.0, 4.0, 0.0),
            2, 2, false, "Laje2D");
        for (auto& me : coupled.surfaceMesh().elements) {
            me.materialId = matId;
            me.thickness = 0.20;
            me.normalPressure = -10.0;
        }

        const SolverResult cRes = FemSolver3D::solve(coupled);
        TEST_ASSERT(cRes.success && cRes.totalSurfaceElements == 4,
                    "Modelo 1D+2D deve resolver antes da exportacao de relatorios");

        QTemporaryDir tmp;
        TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser criado para relatorios Secao 9");
        const QString htmlPath = tmp.filePath("sec9_report.html");
        const QString mdPath = tmp.filePath("sec9_report.md");
        const QString pdfPath = tmp.filePath("sec9_report.pdf");

        TEST_ASSERT(ProjectSerializer::exportTechnicalReportHtml(htmlPath, coupled, cRes),
                    "Exportar HTML Secao 9 deve suceder");
        TEST_ASSERT(ProjectSerializer::exportTechnicalReportMarkdown(mdPath, coupled, cRes),
                    "Exportar Markdown Secao 9 deve suceder");

        QFile hf(htmlPath);
        TEST_ASSERT(hf.open(QIODevice::ReadOnly | QIODevice::Text), "Abrir HTML gerado");
        const QString htmlStr = QString::fromUtf8(hf.readAll());
        TEST_ASSERT(htmlStr.contains(QStringLiteral("Parâmetros do Solver")) &&
                    htmlStr.contains(QStringLiteral("Fallback ativado")) &&
                    htmlStr.contains(QStringLiteral("2D/3D")),
                    "HTML Secao 9 deve conter metadados do solver, fallback e resultados 2D/3D");

        QFile mf(mdPath);
        TEST_ASSERT(mf.open(QIODevice::ReadOnly | QIODevice::Text), "Abrir Markdown gerado");
        const QString mdStr = QString::fromUtf8(mf.readAll());
        TEST_ASSERT(mdStr.contains(QStringLiteral("Identificação da Execução")) &&
                    mdStr.contains(QStringLiteral("Fallback permitido")) &&
                    mdStr.contains(QStringLiteral("Malha 2D/3D")),
                    "Markdown Secao 9 deve conter timestamp, fallback e resumo da malha 2D/3D");

        io::PdfReportOptions pdfOpts;
        pdfOpts.projectTitle = QStringLiteral("Auditoria Secao 9 1D+2D");
        QString pdfErr;
        TEST_ASSERT(io::PdfReportGenerator::generateExecutivePdf(pdfPath, coupled, cRes, pdfOpts, &pdfErr) &&
                    pdfErr.isEmpty() && verifyPdfMagicAndSize(pdfPath, 15000),
                    "PDF Executivo A4 de modelo 1D+2D deve ser gerado com > 15 KB");
    }

    std::cout << "[OK] ValidationIssues + SolverFallback + EnergyTol + Section9 Reports (HTML/MD/PDF)\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);

    testCoverageMatrixAndFactors();
    testSteelMemberNbr8800();
    testNbr8800FltMonotonicityAndCapWithCb();
    testConcreteBeamAndColumnNbr6118();
    testServiceability6118AndFoundation6122();
    testExecutivePdfAndCliEndToEnd();
    testValidationIssuesFallbackEnergyAndSection9Reports();

    std::cout << "\nTodos os testes da Fase 8 (Conformidade ABNT NBR + Relatorio PDF Executivo + CLI) passaram!\n";
    return 0;
}
