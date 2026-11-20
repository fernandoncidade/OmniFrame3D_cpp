#pragma once

#include "../assembly/GlobalAssembler.hpp"
#include "../core/KernelTypes.hpp"
#include "../elements/Corotational1D.hpp"
#include "../solver/LinearSolverSuite.hpp"
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace omniframe::kernel {

enum class NonlinearMethod : std::uint8_t {
    PDelta = 0,
    NewtonRaphson = 1,
    ArcLengthCrisfield = 2
};

enum class NonlinearKinematics : std::uint8_t {
    SmallStrain = 0,
    PDeltaSecondOrder = 1,
    Corotational3D = 2
};

struct NonlinearOptions {
    NonlinearMethod method = NonlinearMethod::NewtonRaphson;
    NonlinearKinematics kinematics = NonlinearKinematics::Corotational3D;

    int numSteps = 20;
    int maxIterationsPerStep = 40;
    int maxSteps = 500;
    double targetLoadFactor = 1.0;

    // Critérios de convergência
    double forceTol = 1e-6;         // ||R|| / max(1, ||F_ref||)
    double dispTol = 1e-7;          // ||dU|| / max(1, ||U||)
    bool fullNewtonTangent = true;  // true = Full NR (atualiza Kt cada iteração), false = Modified NR
    bool enableLineSearch = true;   // Line search com retrocesso para Newton-Raphson

    // Comprimento de arco (Crisfield)
    double initialArcLength = 0.0;  // 0 => calculado automaticamente da norma ||dU_1|| do 1º passo elástico
    double minArcLengthFactor = 1e-4;
    double maxArcLengthFactor = 10.0;
    double psiArcScale = 0.0;       // 0.0 = cilíndrico (recomendado), 1.0 = esférico
    int desiredIterations = 5;      // para controle adaptativo do comprimento de arco

    LinearSolverOptions linearSolver{};
};

struct NonlinearStepRecord {
    int stepIndex = 0;
    double loadFactor = 0.0;
    int iterations = 0;
    double residualNorm = 0.0;
    VecX U_full;
    VecX reactionsFull;
    bool anyYielded = false;
};

struct NonlinearResult {
    bool success = false;
    std::string errorMessage;
    double achievedLoadFactor = 0.0;
    int totalSteps = 0;
    int totalIterations = 0;
    VecX finalU_full;
    VecX finalReactionsFull;
    std::vector<NonlinearStepRecord> history;
};

/**
 * Interface abstrata de modelo não linear para Newton-Raphson, P-Delta e Arc-Length.
 */
class INonlinearModel {
public:
    virtual ~INonlinearModel() = default;

    virtual const DofManager& dofManager() const = 0;
    virtual const ReducedLinearSystem& baseReducedSystem() const = 0;
    virtual const VecX& referenceExternalLoadFull() const = 0;
    virtual VecX initialPrescribedOffsetFull() const { return VecX::Zero(dofManager().totalDofs()); }

    /**
     * Inicia novo incremento de carga a partir do último estado convergido.
     */
    virtual void beginStepTrial() = 0;

    /**
     * Avalia forças internas globais F_int e rigidez tangente Kt_full.
     *
     * @param U_full_total  Deslocamentos totais tentativos no passo atual
     * @param deltaU_step_or_iter Incremento de deslocamento desde o estado anterior de tentativa
     *                            (usado para atualizar rotações incrementais multiplicativamente em SO(3)).
     *                            Para modelos baseados apenas em U total / estado convergido, ambos estão disponíveis.
     */
    virtual bool evaluateTrial(const VecX& U_full_total,
                               const VecX& deltaU_iter,
                               SpMat& outKt_full,
                               VecX& outFint_full,
                               bool& outYielded,
                               std::string* errorMsg = nullptr) = 0;

    virtual void commitStep() = 0;
    virtual void revertStepToCommitted() = 0;
    virtual void resetAll() = 0;
};

/**
 * Especificação de um elemento 1D para modelo não linear de pórticos e treliças 3D.
 */
struct Frame1DNonlinearElementSpec {
    ElementId id = -1;
    NodeId nodeStart = -1;
    NodeId nodeEnd = -1;
    Vec3 X1 = Vec3::Zero();
    Vec3 X2 = Vec3::Zero();
    Mat3 R0 = Mat3::Identity();
    Beam1DProps props;
    std::array<bool, 6> releaseStart{false, false, false, false, false, false};
    std::array<bool, 6> releaseEnd{false, false, false, false, false, false};

    bool hasPlasticity = false;
    J2PlasticityParams plasticParams;
    std::vector<SectionFiber> fibers; // Se vazio quando hasPlasticity==true, gera fibras retangulares equivalentes
};

/**
 * Modelo não linear completo para pórticos e treliças 3D (geométrico P-Delta ou
 * corrotacional 3D exato + plasticidade J2 por fibras ao longo de Gauss-Lobatto).
 */
class FrameNonlinearModel final : public INonlinearModel {
public:
    FrameNonlinearModel(DofManager dofMgr,
                        ReducedLinearSystem baseReducedSys,
                        VecX referenceLoadFull,
                        std::vector<Frame1DNonlinearElementSpec> elements,
                        NonlinearKinematics kinematics);

    const DofManager& dofManager() const override { return m_dofMgr; }
    const ReducedLinearSystem& baseReducedSystem() const override { return m_baseRed; }
    const VecX& referenceExternalLoadFull() const override { return m_Fref; }

    void beginStepTrial() override;

    bool evaluateTrial(const VecX& U_full_total,
                       const VecX& deltaU_iter,
                       SpMat& outKt_full,
                       VecX& outFint_full,
                       bool& outYielded,
                       std::string* errorMsg = nullptr) override;

    void commitStep() override;
    void revertStepToCommitted() override;
    void resetAll() override;

    const std::vector<Corotational1DEvalResult>& lastElementResults() const { return m_lastEval; }

private:
    DofManager m_dofMgr;
    ReducedLinearSystem m_baseRed;
    VecX m_Fref;
    std::vector<Frame1DNonlinearElementSpec> m_elements;
    NonlinearKinematics m_kinematics = NonlinearKinematics::Corotational3D;

    std::vector<Corotational1DState> m_committedStates;
    std::vector<Corotational1DState> m_trialStates;
    std::vector<Corotational1DEvalResult> m_lastEval;
    std::vector<std::array<int, 12>> m_elemDofs;
};

/**
 * Resolvedor não linear estático:
 * - P-Delta iterativo de 2ª ordem
 * - Newton-Raphson incremental (Full ou Modified) com Line Search
 * - Comprimento de Arco de Crisfield (Esférico/Cilíndrico) para snap-through e snap-back
 */
class NonlinearSolver {
public:
    static NonlinearResult solve(INonlinearModel& model, const NonlinearOptions& opts = {});
};

} // namespace omniframe::kernel
