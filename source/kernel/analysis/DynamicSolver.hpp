#pragma once

#include "../assembly/GlobalAssembler.hpp"
#include "../core/KernelTypes.hpp"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <array>
#include <complex>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace omniframe::kernel {

struct ModalOptions {
    int numModes = 6;
    int maxIterations = 300;
    double tolerance = 1e-8;
};

struct ModeInfo {
    int modeIndex = 0;
    double omega = 0.0;        // rad/s
    double frequencyHz = 0.0;  // Hz
    double periodSec = 0.0;    // s
    VecX phi_f;                // Modo reduzido ortonormalizado em massa: phi_f^T * M_ff * phi_f = 1
    VecX phi_full;             // Modo expandido para todos os DOFs globais
    std::array<double, 3> participationFactors{0.0, 0.0, 0.0}; // Gamma_x, Gamma_y, Gamma_z
    std::array<double, 3> effectiveMass{0.0, 0.0, 0.0};        // Massa efetiva modal (t)
    std::array<double, 3> effectiveMassRatio{0.0, 0.0, 0.0};   // Razão M_eff / M_total
};

struct ModalResult {
    bool success = false;
    std::string errorMessage;
    std::vector<ModeInfo> modes;
    std::array<double, 3> totalActiveMass{0.0, 0.0, 0.0};
    std::array<double, 3> cumulativeMassRatio{0.0, 0.0, 0.0};
    int iterationsPerformed = 0;
};

struct RayleighDampingParams {
    double alphaM = 0.0;
    double betaK = 0.0;
};

struct HarmonicOptions {
    std::vector<double> frequenciesHz;
    double rayleighAlphaM = 0.0;
    double rayleighBetaK = 0.0;
    double structuralLossEta = 0.0; // Fator de amortecimento histerético complexo: (1 + i*eta)*K
};

struct HarmonicStepResult {
    double frequencyHz = 0.0;
    double omega = 0.0;
    VecX amplitudeFull;
    VecX phaseRadFull;
    Eigen::Matrix<std::complex<double>, Eigen::Dynamic, 1> U_complex_f;
};

struct HarmonicResult {
    bool success = false;
    std::string errorMessage;
    std::vector<HarmonicStepResult> sweep;
};

enum class TransientScheme : std::uint8_t {
    NewmarkBeta = 0,
    HHTAlpha = 1
};

struct TransientOptions {
    TransientScheme scheme = TransientScheme::NewmarkBeta;
    double dt = 0.005;
    int numSteps = 200;
    double newmarkBeta = 0.25;
    double newmarkGamma = 0.50;
    double hhtAlpha = -0.05; // Faixa estável padrão: [-1/3, 0]
    double rayleighAlphaM = 0.0;
    double rayleighBetaK = 0.0;
    VecX initialDisp_f;      // Opcional; se vazio, inicia em zero
    VecX initialVel_f;       // Opcional; se vazio, inicia em zero
};

struct TransientStepRecord {
    int step = 0;
    double time = 0.0;
    VecX U_full;
    VecX V_f;
    VecX A_f;
    double maxDispNorm = 0.0;
    double kineticEnergy = 0.0;
    double strainEnergy = 0.0;
};

struct TransientResult {
    bool success = false;
    std::string errorMessage;
    std::vector<TransientStepRecord> history;
    VecX peakDisplacementFull;
};

enum class ResponseSpectrumCombination : std::uint8_t {
    SRSS = 0,
    CQC = 1,
    ABSSUM = 2
};

struct SpectrumPoint {
    double periodSec = 0.0;
    double Sa = 0.0; // Aceleração espectral em m/s²
};

struct ResponseSpectrumOptions {
    ResponseSpectrumCombination combination = ResponseSpectrumCombination::CQC;
    double modalDampingZeta = 0.05;
    std::vector<double> perModeDampingZeta;
    std::array<double, 3> directionScale{1.0, 0.0, 0.0};
    std::vector<SpectrumPoint> spectrumCurve;
};

struct ResponseSpectrumResult {
    bool success = false;
    std::string errorMessage;
    VecX combinedDisplacementFull;
    VecX combinedReactionFull;
    std::vector<VecX> modalDisplacementsFull;
};

/**
 * Motor de análise dinâmica estrutural (C++23 + Eigen3):
 * - Modal: K_ff * phi = omega^2 * M_ff * phi (subespaço K-ortonormal imune a DOFs sem massa rotacional)
 * - Amortecimento de Rayleigh (2 frequências-alvo)
 * - Resposta em Frequência Harmônica (FRF esparsa complexa)
 * - Integração Direta no Tempo (Newmark-Beta & HHT-Alpha) com histórico de forças ou acelerograma sísmico
 * - Espectro de Resposta (SRSS / CQC com correlação modal de Der Kiureghian / ABSSUM + curva NBR 15421)
 */
class DynamicSolver {
public:
    /**
     * Constrói matriz esparsa M_ff e os 3 vetores de influência de corpo rígido r_f[x, y, z]
     * no mesmo espaço reduzido de `redSys`.
     */
    static bool reduceMassAndInfluence(const DofManager& dofMgr,
                                         const SpMat& M_full,
                                         const ReducedLinearSystem& redSys,
                                         SpMat& outM_ff,
                                         std::array<VecX, 3>& outInfluence_f,
                                         std::string* errorMsg = nullptr);

    static ModalResult solveModal(const SpMat& K_ff,
                                  const SpMat& M_ff,
                                  const std::array<VecX, 3>& influence_f,
                                  const ReducedLinearSystem& redSys,
                                  const ModalOptions& opts = {},
                                  const std::stop_token& stopToken = {});

    static RayleighDampingParams computeRayleighCoefficients(double omega1, double zeta1,
                                                             double omega2, double zeta2);

    static SpMat assembleRayleighDamping(const SpMat& M_ff, const SpMat& K_ff,
                                         const RayleighDampingParams& params);

    /**
     * Resposta em frequência harmônica (FRF).
     * Contrato de amortecimento: `C_ff` deve conter EXCLUSIVAMENTE amortecedores discretos
     * (nodais ou de elemento). O amortecimento de Rayleigh (`opts.rayleighAlphaM * M_ff +
     * opts.rayleighBetaK * K_ff`) e a perda estrutural (`opts.structuralLossEta`) são
     * aplicados internamente por `DynamicSolver`. Chamadores sem amortecedores discretos
     * devem passar `C_ff` vazia (`SpMat(n, n)`) para evitar duplicação (2C).
     */
    static HarmonicResult solveHarmonic(const SpMat& K_ff,
                                        const SpMat& M_ff,
                                        const SpMat& C_ff,
                                        const VecX& forceAmplitude_f,
                                        const ReducedLinearSystem& redSys,
                                        const HarmonicOptions& opts,
                                        const std::stop_token& stopToken = {});

    /**
     * Integração direta no tempo por histórico de forças.
     * Contrato de amortecimento: `C_ff` deve conter EXCLUSIVAMENTE amortecedores discretos;
     * o amortecimento de Rayleigh (`opts.rayleighAlphaM * M_ff + opts.rayleighBetaK * K_ff`)
     * é adicionado internamente por `DynamicSolver`.
     */
    static TransientResult solveTransientForceHistory(
        const SpMat& K_ff,
        const SpMat& M_ff,
        const SpMat& C_ff,
        const ReducedLinearSystem& redSys,
        const std::function<VecX(double timeSec, int stepIdx)>& reducedForceFn,
        const TransientOptions& opts,
        const std::stop_token& stopToken = {});

    /**
     * Integração direta sísmica por acelerograma na base.
     * Contrato de amortecimento: `C_ff` deve conter EXCLUSIVAMENTE amortecedores discretos;
     * o amortecimento de Rayleigh (`opts.rayleighAlphaM`, `opts.rayleighBetaK`) é adicionado
     * internamente por `DynamicSolver::solveTransientForceHistory`.
     */
    static TransientResult solveSeismicTimeHistory(
        const SpMat& K_ff,
        const SpMat& M_ff,
        const SpMat& C_ff,
        const std::array<VecX, 3>& influence_f,
        const ReducedLinearSystem& redSys,
        const std::function<Vec3(double timeSec, int stepIdx)>& groundAccelMS2Fn,
        const TransientOptions& opts,
        const std::stop_token& stopToken = {});

    static double interpolateSpectrum(const std::vector<SpectrumPoint>& curve, double periodSec);

    static double derKiureghianCorrelation(double omegaI, double zetaI, double omegaK, double zetaK);

    /**
     * Espectro elástico horizontal da ABNT NBR 15421 (aceleração característica ag_s em m/s²,
     * fatores de amplificação sísmica de solo Ca para T=0.2s e Cv para T=1.0s).
     * Para amortecimento `dampingZeta != 0.05`, aplica o fator de correção `eta = max(0.55, sqrt(10 / (5 + 100*zeta)))`
     * sobre os trechos de amplificação dinâmica (mantendo `Sa(0) = Ca * ag_s`).
     */
    static std::vector<SpectrumPoint> buildNbr15421SpectrumCurve(double ag_s_ms2, double Ca, double Cv,
                                                                 double Tmax = 4.0, int numPoints = 100,
                                                                 double dampingZeta = 0.05);

    static ResponseSpectrumResult solveResponseSpectrum(const SpMat& K_orig,
                                                        const ModalResult& modal,
                                                        const ResponseSpectrumOptions& opts,
                                                        const std::stop_token& stopToken = {});
};

} // namespace omniframe::kernel
