#pragma once

#include <array>
#include <string>
#include <vector>

namespace omniframe::kernel::codes {

/// Classificacao explicita de rastreabilidade frente aos PDFs locais em `repo/Normas_Tecnicas_NBRs`.
enum class ComplianceScopeStatus {
    LocalPdfVerified,     ///< Verificado integralmente com PDF presente na biblioteca local
    PartialReferenceOnly, ///< PDF presente na biblioteca local, mas escopo parcial/parametrizado
    OutOfLocalPdfScope    ///< PDF ausente na biblioteca local (marcador explicito de limite de escopo)
};

struct StandardCoverageInfo {
    std::string codeId;
    std::string title;
    ComplianceScopeStatus status = ComplianceScopeStatus::OutOfLocalPdfScope;
    std::string localPdfFile;
    std::string scopeNote;
};

/// Situacao de combinacao segundo ABNT NBR 6118:2023 (Tab. 11.1/12.1) e ABNT NBR 8800:2024 (Tab. 1/3).
enum class CombinationSituation {
    Normal,
    SpecialOrConstruction,
    Exceptional
};

/// Categoria de ocupacao para fatores psi (NBR 6118:2023 Tab. 11.2 / NBR 8800:2024 Tab. 2).
enum class OccupancyCategory {
    Residential,             ///< psi0=0.5, psi1=0.4, psi2=0.3
    CommercialOrPublic,      ///< psi0=0.7, psi1=0.6, psi2=0.4
    LibraryArchiveGarageRoof ///< psi0=0.8, psi1=0.7, psi2=0.6
};

/// Tipo de carga permanente para ponderacao na ABNT NBR 8800:2024 Tabela 1.
enum class DeadLoadClass8800 {
    StructuralSteel,
    PrecastOrTimber,
    CastInPlaceConcrete,
    NonStructuralMasonryOrFinish,
    GroupedPermanent
};

/// Classe de Agressividade Ambiental (CAA) - ABNT NBR 6118:2023 Tabela 13.4.
enum class EnvironmentalAggressivenessClass {
    CAA_I,   ///< wk <= 0.4 mm
    CAA_II,  ///< wk <= 0.3 mm
    CAA_III, ///< wk <= 0.3 mm
    CAA_IV   ///< wk <= 0.2 mm
};

/// Categoria de aco de armadura passiva conforme ABNT NBR 7480:2022 e NBR 6118:2023 Tab. 8.2.
enum class RebarGrade7480 {
    CA25, ///< fyk = 250 MPa, eta1 = 1.00 (liso)
    CA50, ///< fyk = 500 MPa, eta1 = 2.25 (nervurado)
    CA60  ///< fyk = 600 MPa, eta1 = 1.00 (fios Tab. 8.2 NBR 6118)
};

/// Coeficientes parciais de combinacao e de materiais.
struct CombinationFactors {
    double gamma_g_unfav = 1.40;
    double gamma_g_fav = 1.00;
    double gamma_q = 1.40;
    double gamma_w = 1.40;
    double gamma_t = 1.20;
    double gamma_c = 1.40;
    double gamma_s = 1.15;
    double gamma_a1 = 1.10;
    double gamma_a2 = 1.35;
};

struct PsiFactors {
    double psi0 = 0.5;
    double psi1 = 0.4;
    double psi2 = 0.3;
};

/// Entrada simples para vento caracteristico segundo ABNT NBR 6123:2023 Secao 4.2.
struct WindInput6123 {
    double v0_ms = 35.0;
    double s1 = 1.0;
    double s2 = 1.0;
    double s3 = 1.0;
};

struct WindResult6123 {
    double vk_ms = 0.0;       ///< Vk = V0 * S1 * S2 * S3 [m/s]
    double q_Pa = 0.0;        ///< q = 0.613 * Vk^2 [N/m^2]
    double q_kPa = 0.0;       ///< q * 1e-3 [kN/m^2]
    std::string referenceClause = "ABNT NBR 6123:2023 Sec. 4.2";
};

/// Perfil transversal de aco para verificacoes pela ABNT NBR 8800:2024.
enum class SteelSectionKind {
    IShapeRolled,
    IShapeWelded,
    RectangularTube,
    CircularTube,
    RectangularSolid,
    CircularSolid
};

struct SteelSectionSpec {
    SteelSectionKind kind = SteelSectionKind::IShapeRolled;
    // Dimensoes em metros [m]
    double d_m = 0.30;    ///< Altura total (h ou diametro externo D)
    double bf_m = 0.15;   ///< Largura da mesa (ou largura b da secao)
    double tf_m = 0.012;  ///< Espessura da mesa (ou parede t em tubos)
    double tw_m = 0.008;  ///< Espessura da alma
    double fillet_m = 0.0;///< Raio de concordancia r (para altura plana da alma h)
    // Material em MPa
    double fy_MPa = 250.0;
    double fu_MPa = 400.0;
    double E_MPa = 200000.0;
    double G_MPa = 76923.0769;
    // Comprimentos de flambagem [m]
    double Lx_m = 3.0; ///< Flambagem por flexao em torno do eixo x (maior inercia)
    double Ly_m = 3.0; ///< Flambagem por flexao em torno do eixo y (menor inercia)
    double Lz_m = 3.0; ///< Flambagem por torcao
    double Lb_m = 3.0; ///< Comprimento destravado para FLT
    double Cb = 1.0;   ///< Fator Cb (5.4.2.3)
    // Ligacao/area liquida em tracao
    double Ct = 1.0;
    double netAreaRatio = 1.0; ///< An / Ag em (0, 1]
};

struct SteelDesignForces {
    double N_Sd_kN = 0.0;  ///< + tracao, - compressao (kN)
    double Vx_Sd_kN = 0.0; ///< Cortante paralelo ao eixo x (atua nas mesas em perfis I, ou largura RHS)
    double Vy_Sd_kN = 0.0; ///< Cortante paralelo ao eixo y (atua na alma de perfis I)
    double Mx_Sd_kNm = 0.0;///< Momento fletor em torno do eixo forte x (kN.m)
    double My_Sd_kNm = 0.0;///< Momento fletor em torno do eixo fraco y (kN.m)
};

struct SteelCheckResult {
    bool passed = false;
    double dcrMax = 0.0;
    double dcrAxial = 0.0;
    double dcrFlexureX = 0.0;
    double dcrFlexureY = 0.0;
    double dcrShearY = 0.0;
    double dcrShearX = 0.0;
    double dcrInteraction = 0.0;
    double dcrSlenderness = 0.0;

    double Nt_Rd_kN = 0.0;
    double Nc_Rd_kN = 0.0;
    double Mx_Rd_kNm = 0.0;
    double My_Rd_kNm = 0.0;
    double Vy_Rd_kN = 0.0;
    double Vx_Rd_kN = 0.0;
    double maxSlenderness = 0.0;
    double slendernessLimit = 200.0;
    double chiReduction = 1.0;
    double effectiveAreaRatio = 1.0;
    std::string governingLimitState;
    std::string referenceClause = "ABNT NBR 8800:2024 Sec. 5.2, 5.3, 5.4.2, 5.4.3, 5.5.1.2, Anexos D/E";
};

/// Entrada para verificacao/dimensionamento de viga de concreto armado (ABNT NBR 6118:2023).
struct ConcreteBeamSpec {
    double bw_m = 0.20;
    double h_m = 0.50;
    double d_m = 0.45;
    double dp_m = 0.05;
    double fck_MPa = 30.0;
    RebarGrade7480 rebarGrade = RebarGrade7480::CA50;
    double stirrupFyk_MPa = 500.0;
    CombinationSituation situation = CombinationSituation::Normal;
    EnvironmentalAggressivenessClass caa = EnvironmentalAggressivenessClass::CAA_II;
    // Armadura informada pelo usuario (opcional: se <= 0, calcula a necessaria; se > 0, calcula Rd e DCR)
    double providedAs_cm2 = 0.0;
    double providedAsp_cm2 = 0.0;
    double providedAswPerS_cm2PerM = 0.0;
    double barDiameter_mm = 16.0;
    double stirrupAlphaDeg = 90.0;
    int barsCountForCrackZone = 3;
};

struct ConcreteBeamForces {
    double M_Sd_kNm = 0.0;
    double V_Sd_kN = 0.0;
    double N_Sd_kN = 0.0;      ///< Esforco normal concomitante (+ tracao, - compressao)
    double M_Sd_els_kNm = 0.0; ///< Momento em combinacao frequente de servico (para ELS-W)
};

struct ConcreteBeamCheckResult {
    bool passed = false;
    double dcrMax = 0.0;
    double dcrFlexure = 0.0;
    double dcrShearStrut = 0.0;
    double dcrShearStirrup = 0.0;
    double dcrMinSteel = 0.0;
    double dcrMaxSteel = 0.0;
    double dcrDuctility = 0.0;
    double dcrCrackWidth = 0.0;

    double M_Rd_kNm = 0.0;
    double M_d_min_kNm = 0.0;
    double requiredAs_cm2 = 0.0;
    double requiredAsp_cm2 = 0.0;
    double minAs_cm2 = 0.0;
    double maxTotalAs_cm2 = 0.0;
    double xOverD = 0.0;
    double xOverDLimit = 0.45;

    double V_Rd2_kN = 0.0;
    double V_c_kN = 0.0;
    double V_sw_kN = 0.0;
    double V_Rd3_kN = 0.0;
    double requiredAswPerS_cm2PerM = 0.0;
    double minAswPerS_cm2PerM = 0.0;

    double wk_mm = 0.0;
    double wkLimit_mm = 0.30;
    double sigmaSi_els_MPa = 0.0;

    std::string governingLimitState;
    std::string referenceClause = "ABNT NBR 6118:2023 Sec. 13.4, 14.6.4.3, 17.2.2, 17.3.3.2, 17.3.5.2, 17.4.2.2";
};

/// Entrada para verificacao P-Mx-My de pilar retangular de concreto armado (ABNT NBR 6118:2023).
struct ConcreteColumnSpec {
    double bx_m = 0.30; ///< Dimensao paralela ao eixo x (eixo de flexao My)
    double hy_m = 0.40; ///< Dimensao paralela ao eixo y (eixo de flexao Mx)
    double coverToCentroid_m = 0.04;
    double fck_MPa = 30.0;
    RebarGrade7480 rebarGrade = RebarGrade7480::CA50;
    CombinationSituation situation = CombinationSituation::Normal;
    double totalAs_cm2 = 16.0; ///< Armadura simetrica distribuida nas 4 faces/cantos
    int barsPerSide = 3;       ///< >= 2
};

struct ConcreteColumnForces {
    double N_Sd_kN = 0.0;  ///< + tracao, - compressao (kN)
    double Mx_Sd_kNm = 0.0;///< Momento em torno do eixo x (solicitando altura hy_m)
    double My_Sd_kNm = 0.0;///< Momento em torno do eixo y (solicitando largura bx_m)
};

struct ConcreteColumnCheckResult {
    bool passed = false;
    double dcrPMM = 0.0;      ///< Fator inverso de capacidade radial (Sd / Rd) sobre superficie P-Mx-My
    double dcrSteelMin = 0.0;
    double dcrSteelMax = 0.0;
    double dcrMax = 0.0;
    double N_Rd_pureComp_kN = 0.0;
    double N_Rd_pureTens_kN = 0.0;
    double Mx_Rd_atNSd_kNm = 0.0;
    double My_Rd_atNSd_kNm = 0.0;
    double As_min_cm2 = 0.0;
    double As_max_cm2 = 0.0;
    double rhoPercent = 0.0;
    std::string governingLimitState;
    std::string referenceClause = "ABNT NBR 6118:2023 Sec. 17.2.2, 17.3.5.3 (Modelo Biaxial por Fibras)";
};

/// Verificacao de estado-limite de servico de deslocamentos (ABNT NBR 6118:2023 Tabela 13.3).
struct ServiceabilityDisplacementInput {
    double span_m = 5.0;
    double midspanDeflectionTotal_m = 0.0;
    double midspanDeflectionVibration_m = 0.0;
    double buildingHeight_m = 12.0;
    double topHorizontalDriftWind_m = 0.0;
    double storyHeight_m = 3.0;
    double interstoryDriftWind_m = 0.0;
    double partitionSpanDeflection_m = 0.0;
};

struct ServiceabilityDisplacementResult {
    bool passed = true;
    double dcrMax = 0.0;
    double dcrTotalDeflection = 0.0;     ///< L / 250
    double dcrVibrationDeflection = 0.0; ///< L / 350
    double dcrGlobalDrift = 0.0;         ///< H / 1700
    double dcrInterstoryDrift = 0.0;     ///< Hi / 850
    double dcrPartitionDeflection = 0.0; ///< min(L/500, 10 mm)
    std::string governingCriterion;
    std::string referenceClause = "ABNT NBR 6118:2023 Tabela 13.3";
};

/// Verificacao simplificada de fundacao rasa sob tensao admissivel / resistente (ABNT NBR 6122:2022 Tab. 1).
struct ShallowFoundationInput {
    double B_m = 1.50;
    double L_m = 1.50;
    double N_k_kN = 300.0;      ///< Compressao vertical (+ compressao para baixo, kN)
    double Mx_k_kNm = 0.0;
    double My_k_kNm = 0.0;
    double sigmaAdm_kPa = 200.0;///< Tensao admissivel (valores caracteristicos, FSg = 3.0)
    bool useDesignMethod = false;
    bool windIsPrincipal = false; ///< Sec. 6.3.2 (+15% adm) / 6.3.3 (+10% Rd)
    bool hasTwoLoadTests = false; ///< Tab. 1 (gamma_m 1.40/FSg 2.00 vs 2.15/3.00)
    double qUlt_kPa = 600.0;
};

struct ShallowFoundationResult {
    bool passed = false;
    bool hasUplift = false;
    double sigmaMax_kPa = 0.0;
    double sigmaMin_kPa = 0.0;
    double sigmaLimit_kPa = 0.0;
    double eccentricityRatioX = 0.0; ///< |e_x| / (B/6)
    double eccentricityRatioY = 0.0; ///< |e_y| / (L/6)
    double dcrBearing = 0.0;
    double safetyFactorUsed = 3.0;
    std::string referenceClause = "ABNT NBR 6122:2022 Sec. 6.2.1.1.1 Tab. 1 e Sec. 6.3";
};

class NbrCodeChecks {
public:
    /// Matriz explicita de rastreabilidade normativa (PDFs presentes vs ausentes).
    static std::vector<StandardCoverageInfo> getStandardCoverageMatrix();

    /// Coeficientes parciais para estruturas de concreto armado (ABNT NBR 6118:2023 Tab. 11.1 e 12.1).
    static CombinationFactors concreteFactors(CombinationSituation sit);

    /// Coeficientes parciais para estruturas de aco (ABNT NBR 8800:2024 Tab. 1 e 3).
    static CombinationFactors steelFactors(CombinationSituation sit,
                                           DeadLoadClass8800 deadClass = DeadLoadClass8800::GroupedPermanent);

    /// Fatores psi0, psi1, psi2 (NBR 6118:2023 Tab. 11.2 / NBR 8800:2024 Tab. 2).
    static PsiFactors occupancyPsi(OccupancyCategory cat);
    static PsiFactors windPsi();
    static PsiFactors temperaturePsi();

    /// Sobrecarga equivalente de divisorias sem posicao definida (ABNT NBR 6120:2019 Tabela 11).
    /// Retorna -1.0 quando peso > 3.0 kN/m (nao permitido como carga distribuida uniforme).
    static double unpositionedPartitionSurcharge6120_kPa(double linearWeight_kNPerM);

    /// Calculo de velocidade caracteristica e pressao dinamica de vento (ABNT NBR 6123:2023 Sec. 4.2).
    static WindResult6123 evaluateWind6123(const WindInput6123& in);

    /// Resistencias de calculo e parametros de ductilidade do concreto (ABNT NBR 6118:2023 Sec. 8.2 e 17.2.2).
    static double concreteFctm_MPa(double fck_MPa);
    static double concreteFctkInf_MPa(double fck_MPa);
    static double concreteFctkSup_MPa(double fck_MPa);
    static double concreteLambda(double fck_MPa);
    static double concreteAlphaC(double fck_MPa);
    static double concreteEtaC(double fck_MPa);
    static double concreteDuctilityLimitXOverD(double fck_MPa);
    static double rebarCharacteristicYield_MPa(RebarGrade7480 grade);
    static double rebarBondFactorEta1(RebarGrade7480 grade);
    static double minFlexuralSteelRatioTab17_3(double fck_MPa);

    /// Coeficiente Cb de momento fletor nao uniforme (ABNT NBR 8800:2024 Eq. 5.4.2.3-a).
    static double computeCb8800(double mMax_abs, double mA_abs, double mB_abs, double mC_abs, double Rm = 1.0);

    /// Verificacao completa de barra de aco (ABNT NBR 8800:2024 Sec. 5.2, 5.3, 5.4, 5.5.1.2, Anexos D/E).
    static SteelCheckResult checkSteelMember8800(const SteelSectionSpec& sec,
                                                 const SteelDesignForces& forces,
                                                 CombinationSituation sit = CombinationSituation::Normal);

    /// Dimensionamento/verificacao de viga de concreto armado (ABNT NBR 6118:2023 Sec. 17.2, 17.3, 17.4).
    static ConcreteBeamCheckResult checkConcreteBeam6118(const ConcreteBeamSpec& beam,
                                                         const ConcreteBeamForces& forces);

    /// Verificacao biaxial P-Mx-My de pilar retangular por integracao de fibras (ABNT NBR 6118:2023 Sec. 17.2.2/17.3.5.3).
    static ConcreteColumnCheckResult checkConcreteColumnBiaxial6118(const ConcreteColumnSpec& col,
                                                                    const ConcreteColumnForces& forces);

    /// Verificacao de deslocamentos e fissuracao ELS (ABNT NBR 6118:2023 Tab. 13.3).
    static ServiceabilityDisplacementResult checkServiceabilityDeflections6118(
        const ServiceabilityDisplacementInput& in);

    /// Verificacao de fundacao rasa (ABNT NBR 6122:2022 Tab. 1 e Sec. 6.3).
    static ShallowFoundationResult checkShallowFoundation6122(const ShallowFoundationInput& in);
};

} // namespace omniframe::kernel::codes
