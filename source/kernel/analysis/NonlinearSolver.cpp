#include "NonlinearSolver.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace omniframe::kernel {

namespace {

/**
 * Constrói matriz esparsa de prolongamento P_free (nFull x nFree) onde
 * U_full_free = P_free * U_free representa exatamente os modos livres sujeitos aos MPCs.
 */
SpMat buildFreeProlongation(const ReducedLinearSystem& sys) {
    const int nFull = sys.fullDofs;
    const int nFree = sys.freeDofs;
    SpMat P(nFull, nFree);
    if (nFull <= 0 || nFree <= 0) return P;

    std::vector<Triplet> trips;
    if (sys.hasMpc && sys.T_mpc.rows() == nFull && sys.T_mpc.cols() == sys.reducedDofs) {
        std::unordered_map<int, int> redToFree;
        redToFree.reserve(nFree * 2 + 1);
        for (int j = 0; j < nFree; ++j) {
            redToFree[sys.freeReducedIndices[j]] = j;
        }
        for (int k = 0; k < sys.T_mpc.outerSize(); ++k) {
            auto fit = redToFree.find(k);
            if (fit == redToFree.end()) continue;
            const int freeCol = fit->second;
            for (SpMat::InnerIterator it(sys.T_mpc, k); it; ++it) {
                if (std::abs(it.value()) > 1e-16) {
                    trips.emplace_back(it.row(), freeCol, it.value());
                }
            }
        }
    } else {
        trips.reserve(nFree);
        for (int j = 0; j < nFree; ++j) {
            const int fullIdx = sys.freeReducedIndices[j];
            if (fullIdx >= 0 && fullIdx < nFull) {
                trips.emplace_back(fullIdx, j, 1.0);
            }
        }
    }
    P.setFromTriplets(trips.begin(), trips.end());
    P.makeCompressed();
    return P;
}

VecX computeReactionsFull(const ReducedLinearSystem& sys,
                          const VecX& Fint_full,
                          double lambda,
                          const VecX& Fref_full)
{
    // Em equilíbrio completo: F_int = F_ext + R_reac  =>  R_reac = F_int - lambda * F_ref nos DOFs restritos
    VecX reac = Fint_full - lambda * Fref_full;
    // Zera ruído numérico em graus puramente livres (sem MPC)
    for (int j = 0; j < sys.freeDofs; ++j) {
        if (!sys.hasMpc) {
            const int fullIdx = sys.freeReducedIndices[j];
            if (fullIdx >= 0 && fullIdx < reac.size()) {
                reac(fullIdx) = 0.0;
            }
        }
    }
    return reac;
}

} // namespace

FrameNonlinearModel::FrameNonlinearModel(DofManager dofMgr,
                                         ReducedLinearSystem baseReducedSys,
                                         VecX referenceLoadFull,
                                         std::vector<Frame1DNonlinearElementSpec> elements,
                                         NonlinearKinematics kinematics)
    : m_dofMgr(std::move(dofMgr))
    , m_baseRed(std::move(baseReducedSys))
    , m_Fref(std::move(referenceLoadFull))
    , m_elements(std::move(elements))
    , m_kinematics(kinematics)
{
    for (auto& spec : m_elements) {
        if (spec.hasPlasticity && spec.fibers.empty() && spec.props.formulation != Beam1DFormulation::Truss3D) {
            spec.fibers = CorotationalBeam3D::buildDefaultFibers(
                spec.props.A, spec.props.Iy, spec.props.Iz, 8, 6);
        }
        spec.props.L = (spec.X2 - spec.X1).norm();
    }

    m_elemDofs.resize(m_elements.size());
    for (size_t i = 0; i < m_elements.size(); ++i) {
        const auto& spec = m_elements[i];
        for (int d = 0; d < 6; ++d) {
            m_elemDofs[i][d] = m_dofMgr.getNodeGlobalDof(spec.nodeStart, d);
            m_elemDofs[i][d + 6] = m_dofMgr.getNodeGlobalDof(spec.nodeEnd, d);
        }
    }

    resetAll();
}

void FrameNonlinearModel::beginStepTrial() {
    m_trialStates = m_committedStates;
}

void FrameNonlinearModel::commitStep() {
    for (size_t i = 0; i < m_elements.size(); ++i) {
        if (i < m_lastEval.size()) {
            m_committedStates[i] = m_lastEval[i].trialState;
        }
    }
    m_trialStates = m_committedStates;
}

void FrameNonlinearModel::revertStepToCommitted() {
    m_trialStates = m_committedStates;
    m_savedIterTrialStates = m_committedStates;
}

void FrameNonlinearModel::saveIterationTrialState() {
    m_savedIterTrialStates = m_trialStates;
}

void FrameNonlinearModel::restoreIterationTrialState() {
    m_trialStates = m_savedIterTrialStates;
}

void FrameNonlinearModel::resetAll() {
    m_committedStates.assign(m_elements.size(), Corotational1DState{});
    m_trialStates = m_committedStates;
    m_savedIterTrialStates = m_committedStates;
    m_lastEval.resize(m_elements.size());
}

bool FrameNonlinearModel::evaluateTrial(const VecX& U_full_total,
                                        const VecX& deltaU_iter,
                                        SpMat& outKt_full,
                                        VecX& outFint_full,
                                        bool& outYielded,
                                        std::string* errorMsg)
{
    const int nDofs = m_dofMgr.totalDofs();
    if (U_full_total.size() != nDofs || deltaU_iter.size() != nDofs) {
        if (errorMsg) *errorMsg = "Tamanho inválido do vetor U_full no modelo não linear.";
        return false;
    }

    std::vector<Triplet> trips;
    trips.reserve(m_elements.size() * 144 + m_dofMgr.nodeStates().size() * 6);
    outFint_full = VecX::Zero(nDofs);
    outYielded = false;

    const bool pDeltaOnly = (m_kinematics != NonlinearKinematics::Corotational3D);

    for (size_t ei = 0; ei < m_elements.size(); ++ei) {
        const auto& spec = m_elements[ei];
        const auto& dofMap = m_elemDofs[ei];

        Vec3 u1 = Vec3::Zero(), u2 = Vec3::Zero();
        Vec3 th1 = Vec3::Zero(), th2 = Vec3::Zero();

        for (int d = 0; d < 3; ++d) {
            if (dofMap[d] >= 0) u1(d) = U_full_total(dofMap[d]);
            if (dofMap[d + 6] >= 0) u2(d) = U_full_total(dofMap[d + 6]);
            if (pDeltaOnly) {
                if (dofMap[d + 3] >= 0) th1(d) = U_full_total(dofMap[d + 3]);
                if (dofMap[d + 9] >= 0) th2(d) = U_full_total(dofMap[d + 9]);
            } else {
                if (dofMap[d + 3] >= 0) th1(d) = deltaU_iter(dofMap[d + 3]);
                if (dofMap[d + 9] >= 0) th2(d) = deltaU_iter(dofMap[d + 9]);
            }
        }

        // Em passo corrotacional: R_i atualiza com deltaU_iter sobre m_trialStates,
        // mas estado plástico sempre retorna do m_committedStates[ei]!
        Corotational1DState inputState;
        inputState.R1 = m_trialStates[ei].R1;
        inputState.R2 = m_trialStates[ei].R2;
        inputState.gaussStates = m_committedStates[ei].gaussStates;

        const J2PlasticityParams* j2Ptr = spec.hasPlasticity ? &spec.plasticParams : nullptr;
        const std::vector<SectionFiber>* fibPtr = (spec.hasPlasticity && !spec.fibers.empty()) ? &spec.fibers : nullptr;

        Corotational1DEvalResult res = CorotationalBeam3D::evaluate(
            spec.X1, spec.X2, spec.R0, spec.props,
            u1, th1, u2, th2,
            inputState,
            j2Ptr, fibPtr,
            spec.releaseStart, spec.releaseEnd,
            pDeltaOnly);

        if (!res.fIntGlobal.allFinite() || !res.KtGlobal.allFinite()) {
            if (errorMsg) *errorMsg = "Matriz tangente ou força interna não finita no elemento " + std::to_string(spec.id);
            return false;
        }

        // Atualiza rotações de tentativa para a próxima iteração de Newton dentro do passo
        m_trialStates[ei].R1 = res.trialState.R1;
        m_trialStates[ei].R2 = res.trialState.R2;
        if (res.yielded) outYielded = true;
        m_lastEval[ei] = std::move(res);

        const auto& elemRes = m_lastEval[ei];
        for (int r = 0; r < 12; ++r) {
            const int gr = dofMap[r];
            if (gr < 0) continue;
            outFint_full(gr) += elemRes.fIntGlobal(r);
            for (int c = 0; c < 12; ++c) {
                const int gc = dofMap[c];
                if (gc < 0) continue;
                const double val = elemRes.KtGlobal(r, c);
                if (std::abs(val) > 1e-16) {
                    trips.emplace_back(gr, gc, val);
                }
            }
        }
    }

    // Molas nodais (elásticas lineares)
    for (const auto& ns : m_dofMgr.nodeStates()) {
        for (int d = 0; d < NODAL_MAX_DOFS; ++d) {
            const int g = ns.globalDof[d];
            if (g >= 0 && ns.springStiffness[d] > 1e-15) {
                const double k = ns.springStiffness[d];
                trips.emplace_back(g, g, k);
                outFint_full(g) += k * U_full_total(g);
            }
        }
    }

    outKt_full.resize(nDofs, nDofs);
    outKt_full.setFromTriplets(trips.begin(), trips.end());
    outKt_full.makeCompressed();
    return true;
}

NonlinearResult NonlinearSolver::solve(INonlinearModel& model,
                                       const NonlinearOptions& opts,
                                       std::stop_token stopToken) {
    NonlinearResult out;
    model.resetAll();

    auto returnCancelled = [&](const char* msg) -> NonlinearResult {
        model.revertStepToCommitted();
        out.success = false;
        out.errorMessage = msg;
        out.convergenceDiagnostic = msg;
        return out;
    };

    if (stopToken.stop_requested()) {
        return returnCancelled("Analise nao linear cancelada.");
    }

    const ReducedLinearSystem& baseSys = model.baseReducedSystem();
    const int nFull = baseSys.fullDofs;
    const int nFree = baseSys.freeDofs;
    if (nFull <= 0 || nFree <= 0) {
        out.errorMessage = "Modelo sem graus de liberdade livres para análise não linear.";
        return out;
    }

    const VecX& Fref_full = model.referenceExternalLoadFull();
    const SpMat P_free = buildFreeProlongation(baseSys);
    const SpMat Pt = P_free.transpose();
    const VecX Fref_f = Pt * Fref_full;
    const double refForceNorm = std::max(1.0, Fref_f.norm());

    // [AUD-012] Deslocamento unitário imposto em apoios e vínculos (recalques em U_fixed_red + offsets U_offset)
    VecX U_prescribed_unit = VecX::Zero(nFull);
    if (baseSys.reducedDofs > 0 && baseSys.U_fixed_red.size() == baseSys.reducedDofs) {
        VecX uRedUnit = baseSys.U_fixed_red;
        for (int j = 0; j < nFree && j < static_cast<int>(baseSys.freeReducedIndices.size()); ++j) {
            const int rd = baseSys.freeReducedIndices[static_cast<std::size_t>(j)];
            if (rd >= 0 && rd < baseSys.reducedDofs) {
                uRedUnit(rd) = 0.0;
            }
        }
        if (baseSys.hasMpc && baseSys.T_mpc.rows() == nFull && baseSys.T_mpc.cols() == baseSys.reducedDofs) {
            U_prescribed_unit += baseSys.T_mpc * uRedUnit;
        } else if (uRedUnit.size() == nFull) {
            U_prescribed_unit += uRedUnit;
        }
    }
    if (baseSys.U_offset.size() == nFull && baseSys.U_offset.norm() > 0.0) {
        U_prescribed_unit += baseSys.U_offset;
    }

    auto solveLinearTangent = [&](const SpMat& K_ff, const VecX& rhs, LinearSolverStats& st) -> VecX {
        return LinearSolverSuite::solve(K_ff, rhs, opts.linearSolver, st, nullptr, stopToken);
    };

    if (opts.method == NonlinearMethod::PDelta) {
        // Método iterativo direto de 2ª ordem P-Delta em carga alvo total
        const double lambda = opts.targetLoadFactor;
        VecX U_free = VecX::Zero(nFree);
        VecX deltaU_iter = lambda * U_prescribed_unit;
        VecX U_full = deltaU_iter;

        model.beginStepTrial();
        SpMat Kt_pre;
        VecX Fint_pre;
        bool hasPreEval = false;
        bool yielded_pre = false;
        bool yielded = false;
        double refEnergy = -1.0;
        VecX prevDU_free = VecX::Zero(nFree);
        double lastEnergyRatio = 0.0;
        double lastRelRes = 0.0;

        const int maxIters = std::max(5, opts.maxIterationsPerStep);
        for (int iter = 1; iter <= maxIters; ++iter) {
            if (stopToken.stop_requested()) {
                return returnCancelled("Analise P-Delta cancelada.");
            }
            out.totalIterations = iter;
            SpMat Kt_full;
            VecX Fint_full;
            if (hasPreEval) {
                Kt_full.swap(Kt_pre);
                Fint_full.swap(Fint_pre);
                yielded = yielded_pre;
                hasPreEval = false;
            } else {
                if (!model.evaluateTrial(U_full, deltaU_iter, Kt_full, Fint_full, yielded, &out.errorMessage)) {
                    out.convergenceDiagnostic = "P-Delta falhou na avaliacao do elemento: " + out.errorMessage;
                    return out;
                }
            }
            const VecX R_f = Pt * (lambda * Fref_full - Fint_full);
            const SpMat Kt_ff = Pt * Kt_full * P_free;
            const double relR = R_f.norm() / refForceNorm;
            lastRelRes = relR;
            if (iter > 1 && refEnergy > 0.0) {
                lastEnergyRatio = std::abs(prevDU_free.dot(R_f)) / refEnergy;
            }

            if (iter > 1 && relR <= opts.forceTol && (opts.energyTol <= 0.0 || lastEnergyRatio <= opts.energyTol)) {
                model.commitStep();
                out.success = true;
                out.achievedLoadFactor = lambda;
                out.totalSteps = 1;
                out.finalU_full = U_full;
                out.finalReactionsFull = computeReactionsFull(baseSys, Fint_full, lambda, Fref_full);
                out.history.push_back({1, lambda, iter, R_f.norm(), U_full, out.finalReactionsFull, yielded, lastEnergyRatio});
                out.convergenceDiagnostic = "P-Delta convergiu (iter=" + std::to_string(iter)
                    + ", relR=" + std::to_string(relR) + ", relE=" + std::to_string(lastEnergyRatio) + ").";
                return out;
            }

            LinearSolverStats st;
            VecX dU_free = solveLinearTangent(Kt_ff, R_f, st);
            if (stopToken.stop_requested()) {
                return returnCancelled("Analise P-Delta cancelada.");
            }
            if (!st.success || !dU_free.allFinite()) {
                out.errorMessage = "Falha ou instabilidade (flambagem/singularidade) na análise P-Delta: " + st.message;
                out.convergenceDiagnostic = out.errorMessage;
                return out;
            }

            if (iter == 1 || refEnergy <= 0.0) {
                refEnergy = std::max(1e-12, std::abs(dU_free.dot(R_f)));
            }
            prevDU_free = dU_free;

            // [AUD-010] Atualiza deltaU_iter com incremento real P_free * dU_free para que
            // P-Delta combinado com Corotational3D atualize rotações nodais em SO(3).
            const double relDisp = dU_free.norm() / std::max(1.0, U_free.norm());
            U_free += dU_free;
            deltaU_iter = P_free * dU_free;
            U_full = P_free * U_free + lambda * U_prescribed_unit;

            if (relDisp <= opts.dispTol || iter == maxIters) {
                if (!model.evaluateTrial(U_full, deltaU_iter, Kt_pre, Fint_pre, yielded_pre, &out.errorMessage)) {
                    out.convergenceDiagnostic = out.errorMessage;
                    return out;
                }
                const VecX R2 = Pt * (lambda * Fref_full - Fint_pre);
                const double relR2 = R2.norm() / refForceNorm;
                const double relE2 = (refEnergy > 0.0) ? (std::abs(dU_free.dot(R2)) / refEnergy) : 0.0;
                lastRelRes = relR2;
                lastEnergyRatio = relE2;
                if (relR2 <= opts.forceTol && (opts.energyTol <= 0.0 || relE2 <= opts.energyTol)) {
                    model.commitStep();
                    out.success = true;
                    out.achievedLoadFactor = lambda;
                    out.totalSteps = 1;
                    out.finalU_full = U_full;
                    out.finalReactionsFull = computeReactionsFull(baseSys, Fint_pre, lambda, Fref_full);
                    out.history.push_back({1, lambda, iter, R2.norm(), U_full, out.finalReactionsFull, yielded_pre, relE2});
                    out.convergenceDiagnostic = "P-Delta convergiu (relDisp=" + std::to_string(relDisp)
                        + ", relR=" + std::to_string(relR2) + ", relE=" + std::to_string(relE2) + ").";
                    return out;
                }
                hasPreEval = true;
            }
        }
        out.errorMessage = "Análise P-Delta não convergiu no número máximo de iterações (possível carga próxima à crítica).";
        out.convergenceDiagnostic = out.errorMessage + " Ultimo relR=" + std::to_string(lastRelRes)
            + ", relE=" + std::to_string(lastEnergyRatio) + ".";
        return out;
    }

    if (opts.method == NonlinearMethod::NewtonRaphson) {
        const int nSteps = std::max(1, opts.numSteps);
        const double dLambdaBase = opts.targetLoadFactor / static_cast<double>(nSteps);

        VecX U_free_conv = VecX::Zero(nFree);
        VecX U_full_conv = VecX::Zero(nFull);
        double lambdaConv = 0.0;

        // Estado inicial em t=0
        SpMat Kt0_full;
        VecX Fint0_full;
        bool y0 = false;
        model.beginStepTrial();
        if (!model.evaluateTrial(U_full_conv, VecX::Zero(nFull), Kt0_full, Fint0_full, y0, &out.errorMessage)) {
            return out;
        }
        SpMat Kt_ff_cached = Pt * Kt0_full * P_free;
        model.commitStep();

        int stepIdx = 0;
        double dLambdaCurr = dLambdaBase;
        int cutbacks = 0;

        while (std::abs(opts.targetLoadFactor - lambdaConv) > 1e-12 * std::max(1.0, std::abs(opts.targetLoadFactor))
               && stepIdx < opts.maxSteps)
        {
            if (stopToken.stop_requested()) {
                return returnCancelled("Analise Newton-Raphson cancelada.");
            }
            const double remaining = opts.targetLoadFactor - lambdaConv;
            const double dLam = (std::abs(dLambdaCurr) > std::abs(remaining)) ? remaining : dLambdaCurr;
            const double lambdaTarget = lambdaConv + dLam;

            model.beginStepTrial();
            VecX U_free = U_free_conv;
            VecX deltaU_iter = (lambdaTarget - lambdaConv) * U_prescribed_unit;
            VecX U_full = U_full_conv + deltaU_iter;

            bool stepConverged = false;
            int stepIters = 0;
            double lastResNorm = 0.0;
            double refEnergy = -1.0;
            VecX prevDU_free = VecX::Zero(nFree);
            double lastEnergyRatio = 0.0;
            bool stepYielded = false;
            VecX Fint_final;

            bool hasPreEval = false;
            SpMat Kt_pre;
            VecX Fint_pre;
            bool yielded_pre = false;

            const int maxStepIters = std::max(1, opts.maxIterationsPerStep);
            for (int iter = 1; iter <= maxStepIters; ++iter) {
                if (stopToken.stop_requested()) {
                    return returnCancelled("Analise Newton-Raphson cancelada.");
                }
                stepIters = iter;
                ++out.totalIterations;

                SpMat Kt_full;
                VecX Fint_full;
                if (hasPreEval) {
                    Kt_full.swap(Kt_pre);
                    Fint_full.swap(Fint_pre);
                    stepYielded = yielded_pre;
                    hasPreEval = false;
                } else {
                    if (!model.evaluateTrial(U_full, deltaU_iter, Kt_full, Fint_full, stepYielded, &out.errorMessage)) {
                        break;
                    }
                }
                Fint_final = Fint_full;
                const VecX R_f = Pt * (lambdaTarget * Fref_full - Fint_full);
                lastResNorm = R_f.norm();
                const double relRes = lastResNorm / refForceNorm;
                if (iter > 1 && refEnergy > 0.0) {
                    lastEnergyRatio = std::abs(prevDU_free.dot(R_f)) / refEnergy;
                }

                if (iter > 1 && relRes <= opts.forceTol && (opts.energyTol <= 0.0 || lastEnergyRatio <= opts.energyTol)) {
                    stepConverged = true;
                    break;
                }

                if (opts.fullNewtonTangent || iter == 1) {
                    Kt_ff_cached = Pt * Kt_full * P_free;
                }

                LinearSolverStats st;
                VecX dU_free = solveLinearTangent(Kt_ff_cached, R_f, st);
                if (stopToken.stop_requested()) {
                    return returnCancelled("Analise Newton-Raphson cancelada.");
                }
                if (!st.success || !dU_free.allFinite()) {
                    break;
                }

                if (iter == 1 || refEnergy <= 0.0) {
                    refEnergy = std::max(1e-12, std::abs(dU_free.dot(R_f)));
                }
                prevDU_free = dU_free;

                double alpha = 1.0;
                const VecX dU_full_step = P_free * dU_free;

                if (opts.enableLineSearch && dU_free.norm() > 0.0) {
                    // [AUD-010] Backtracking line search real (Armijo + resíduo de energia direcional)
                    // com snapshot/rollback do estado SO(3) por iteração.
                    model.saveIterationTrialState();
                    const double rNormBefore = R_f.norm();
                    const double g0 = dU_free.dot(R_f);

                    bool hasBest = false;
                    double bestAlpha = 1.0;
                    double bestRNorm = std::numeric_limits<double>::infinity();
                    SpMat Kt_best;
                    VecX Fint_best;
                    bool yielded_best = false;
                    bool acceptedEarly = false;
                    int lastEvaluatedLs = -1;
                    int bestLs = -1;

                    constexpr int kMaxLineSearchSteps = 7;
                    for (int ls = 0; ls < kMaxLineSearchSteps; ++ls) {
                        if (stopToken.stop_requested()) {
                            return returnCancelled("Analise Newton-Raphson cancelada.");
                        }
                        if (ls > 0) {
                            model.restoreIterationTrialState();
                        }
                        lastEvaluatedLs = ls;
                        const VecX dU_cand = alpha * dU_full_step;
                        SpMat Kt_cand;
                        VecX Fint_cand;
                        bool yielded_cand = false;
                        std::string lsErr;
                        if (!model.evaluateTrial(U_full + dU_cand, dU_cand, Kt_cand, Fint_cand, yielded_cand, &lsErr)) {
                            alpha *= 0.5;
                            continue;
                        }
                        const VecX R_cand = Pt * (lambdaTarget * Fref_full - Fint_cand);
                        const double rNormCand = R_cand.norm();
                        const double gCand = dU_free.dot(R_cand);
                        if (!hasBest || rNormCand < bestRNorm) {
                            hasBest = true;
                            bestAlpha = alpha;
                            bestRNorm = rNormCand;
                            Kt_best.swap(Kt_cand);
                            Fint_best.swap(Fint_cand);
                            yielded_best = yielded_cand;
                            bestLs = ls;
                        }

                        const bool convergedTol = (rNormCand / refForceNorm <= opts.forceTol);
                        const bool armijoDecrease = (rNormCand <= (1.0 - 1e-4 * alpha) * rNormBefore);
                        const bool slackEnergy = (std::abs(g0) > 1e-18 &&
                                                  std::abs(gCand) <= 0.5 * std::abs(g0) &&
                                                  rNormCand <= 1.25 * rNormBefore);
                        if (convergedTol || armijoDecrease || slackEnergy) {
                            acceptedEarly = true;
                            break;
                        }
                        alpha *= 0.5;
                    }

                    if (!hasBest) {
                        model.restoreIterationTrialState();
                        break;
                    }
                    if (bestLs != lastEvaluatedLs) {
                        model.restoreIterationTrialState();
                        const VecX dU_best = bestAlpha * dU_full_step;
                        if (!model.evaluateTrial(U_full + dU_best, dU_best, Kt_best, Fint_best, yielded_best, &out.errorMessage)) {
                            break;
                        }
                    }
                    (void)acceptedEarly;
                    alpha = bestAlpha;
                    if (alpha < 1.0 - 1e-12) {
                        ++out.lineSearchBacktrackCount;
                        out.minLineSearchAlpha = std::min(out.minLineSearchAlpha, alpha);
                    }
                    const VecX dU_applied = alpha * dU_full_step;
                    U_free += alpha * dU_free;
                    U_full += dU_applied;
                    deltaU_iter = dU_applied;

                    Fint_final = Fint_best;
                    const VecX R_chk = Pt * (lambdaTarget * Fref_full - Fint_best);
                    lastResNorm = R_chk.norm();
                    lastEnergyRatio = (refEnergy > 0.0) ? (std::abs(alpha * dU_free.dot(R_chk)) / refEnergy) : 0.0;
                    stepYielded = yielded_best;

                    if ((lastResNorm / refForceNorm <= opts.forceTol) &&
                        (opts.energyTol <= 0.0 || lastEnergyRatio <= opts.energyTol)) {
                        stepConverged = true;
                        break;
                    }

                    Kt_pre.swap(Kt_best);
                    Fint_pre.swap(Fint_best);
                    yielded_pre = yielded_best;
                    hasPreEval = true;
                } else {
                    const VecX& dU_applied = dU_full_step;
                    U_free += dU_free;
                    U_full += dU_applied;
                    deltaU_iter = dU_applied;

                    const double relDisp = dU_free.norm() / std::max(1.0, U_free.norm());
                    if (relDisp <= opts.dispTol || iter == maxStepIters) {
                        // Confirma equilíbrio real de forças no ponto atualizado (sem aceitar apenas relDisp)
                        if (!model.evaluateTrial(U_full, deltaU_iter, Kt_pre, Fint_pre, yielded_pre, &out.errorMessage)) {
                            break;
                        }
                        Fint_final = Fint_pre;
                        stepYielded = yielded_pre;
                        const VecX R_chk = Pt * (lambdaTarget * Fref_full - Fint_pre);
                        lastResNorm = R_chk.norm();
                        lastEnergyRatio = (refEnergy > 0.0) ? (std::abs(dU_free.dot(R_chk)) / refEnergy) : 0.0;
                        if ((lastResNorm / refForceNorm <= opts.forceTol) &&
                            (opts.energyTol <= 0.0 || lastEnergyRatio <= opts.energyTol)) {
                            stepConverged = true;
                            break;
                        }
                        hasPreEval = true;
                    }
                }
            }

            if (stepConverged) {
                ++stepIdx;
                model.commitStep();
                U_free_conv = U_free;
                U_full_conv = U_full;
                lambdaConv = lambdaTarget;
                cutbacks = 0;

                VecX reac = computeReactionsFull(baseSys, Fint_final, lambdaConv, Fref_full);
                out.history.push_back({stepIdx, lambdaConv, stepIters, lastResNorm, U_full_conv, reac, stepYielded, lastEnergyRatio});

                if (stepIters <= 4 && std::abs(dLambdaCurr) < std::abs(dLambdaBase)) {
                    dLambdaCurr *= 1.5;
                    if (std::abs(dLambdaCurr) > std::abs(dLambdaBase)) dLambdaCurr = dLambdaBase;
                }
            } else {
                model.revertStepToCommitted();
                ++cutbacks;
                dLambdaCurr *= 0.5;
                if (cutbacks > 10 || std::abs(dLambdaCurr) < 1e-5 * std::abs(dLambdaBase)) {
                    out.errorMessage = "Newton-Raphson não convergiu mesmo após subdivisão de passo (possível ponto limite / flambagem).";
                    out.convergenceDiagnostic = out.errorMessage + " Passo=" + std::to_string(stepIdx)
                        + ", lambda=" + std::to_string(lambdaConv)
                        + ", relE=" + std::to_string(lastEnergyRatio) + ".";
                    out.achievedLoadFactor = lambdaConv;
                    out.totalSteps = stepIdx;
                    out.finalU_full = U_full_conv;
                    if (!out.history.empty()) out.finalReactionsFull = out.history.back().reactionsFull;
                    return out;
                }
            }
        }

        out.success = (stepIdx > 0 && std::abs(opts.targetLoadFactor - lambdaConv) <= 1e-6 * std::max(1.0, std::abs(opts.targetLoadFactor)));
        out.achievedLoadFactor = lambdaConv;
        out.totalSteps = stepIdx;
        out.finalU_full = U_full_conv;
        if (!out.history.empty()) out.finalReactionsFull = out.history.back().reactionsFull;
        if (!out.success && out.errorMessage.empty()) {
            out.errorMessage = "Número máximo de passos atingido antes do fator de carga alvo.";
        }
        out.convergenceDiagnostic = out.success
            ? ("Newton-Raphson convergiu em " + std::to_string(stepIdx) + " passos (" + std::to_string(out.totalIterations) + " iters total).")
            : out.errorMessage;
        return out;
    }

    // --- Método de Comprimento de Arco de Crisfield (Esférico / Cilíndrico) ---
    {
        VecX U_free_conv = VecX::Zero(nFree);
        VecX U_full_conv = VecX::Zero(nFull);
        double lambdaConv = 0.0;

        model.beginStepTrial();
        SpMat Kt0_full;
        VecX Fint0_full;
        bool y0 = false;
        if (!model.evaluateTrial(U_full_conv, VecX::Zero(nFull), Kt0_full, Fint0_full, y0, &out.errorMessage)) {
            return out;
        }
        model.commitStep();

        // Calcula comprimento de arco inicial automático a partir da norma do passo elástico linear inicial
        double ds = opts.initialArcLength;
        const double FrefNorm2 = Fref_f.squaredNorm();
        const double psi2 = opts.psiArcScale * opts.psiArcScale;
        if (ds <= 0.0) {
            SpMat Kt0_ff = Pt * Kt0_full * P_free;
            LinearSolverStats st0;
            const double dLam0 = opts.targetLoadFactor / static_cast<double>(std::max(1, opts.numSteps));
            VecX dUt0 = solveLinearTangent(Kt0_ff, Fref_f, st0);
            if (!st0.success || !dUt0.allFinite()) {
                out.errorMessage = "Matriz tangente inicial singular no método Arc-Length.";
                return out;
            }
            ds = std::max(1e-8, std::abs(dLam0) * std::sqrt(dUt0.squaredNorm() + psi2 * FrefNorm2));
        }
        const double dsMin = ds * opts.minArcLengthFactor;
        const double dsMax = ds * opts.maxArcLengthFactor;

        VecX dU_prev_step = VecX::Zero(nFree);
        double dLam_prev_step = 0.0;
        double loadSign = (opts.targetLoadFactor >= 0.0) ? 1.0 : -1.0;

        int stepIdx = 0;
        int cutbacks = 0;

        while (stepIdx < opts.maxSteps) {
            if (stopToken.stop_requested()) {
                return returnCancelled("Analise Arc-Length cancelada.");
            }
            // Verifica se atingimos ou ultrapassamos targetLoadFactor na mesma direção inicial
            if (stepIdx > 0 && ((opts.targetLoadFactor >= 0.0 && lambdaConv >= opts.targetLoadFactor - 1e-10)
                                || (opts.targetLoadFactor < 0.0 && lambdaConv <= opts.targetLoadFactor + 1e-10))) {
                break;
            }

            model.beginStepTrial();
            SpMat Kt_full;
            VecX Fint_full;
            bool stepYielded = false;
            if (!model.evaluateTrial(U_full_conv, VecX::Zero(nFull), Kt_full, Fint_full, stepYielded, &out.errorMessage)) {
                return out;
            }
            SpMat Kt_ff = Pt * Kt_full * P_free;

            LinearSolverStats stT;
            VecX dU_t = solveLinearTangent(Kt_ff, Fref_f, stT);
            if (!stT.success || !dU_t.allFinite()) {
                model.revertStepToCommitted();
                ds *= 0.5;
                ++cutbacks;
                if (ds < dsMin || cutbacks > 12) {
                    out.errorMessage = "Falha na resolução da tangente no preditor Arc-Length.";
                    break;
                }
                continue;
            }

            // Preditor tangente + orientação consistente pelo trabalho incremental dU_prev^T dU_t
            const double denomDs = std::sqrt(dU_t.squaredNorm() + psi2 * FrefNorm2);
            double dLamStep = (denomDs > 1e-18) ? (ds / denomDs) : 0.0;
            if (stepIdx == 0) {
                dLamStep *= loadSign;
            } else {
                const double dotPrev = dU_prev_step.dot(dU_t) + psi2 * dLam_prev_step * FrefNorm2;
                if (dotPrev < 0.0) dLamStep = -dLamStep;
            }

            VecX dU_step = dLamStep * dU_t;
            VecX U_free = U_free_conv + dU_step;
            VecX deltaU_iter = P_free * dU_step + dLamStep * U_prescribed_unit;
            VecX U_full = U_full_conv + deltaU_iter;
            double lambdaCurr = lambdaConv + dLamStep;

            bool stepConverged = false;
            int stepIters = 0;
            double lastResNorm = 0.0;
            double refEnergy = -1.0;
            VecX prevDU_free = VecX::Zero(nFree);
            double lastEnergyRatio = 0.0;
            VecX Fint_final;

            for (int iter = 1; iter <= opts.maxIterationsPerStep; ++iter) {
                if (stopToken.stop_requested()) {
                    return returnCancelled("Analise Arc-Length cancelada.");
                }
                stepIters = iter;
                ++out.totalIterations;

                if (!model.evaluateTrial(U_full, deltaU_iter, Kt_full, Fint_full, stepYielded, &out.errorMessage)) {
                    break;
                }
                Fint_final = Fint_full;
                const VecX R_f = Pt * (lambdaCurr * Fref_full - Fint_full);
                lastResNorm = R_f.norm();
                if (iter > 1 && refEnergy > 0.0) {
                    lastEnergyRatio = std::abs(prevDU_free.dot(R_f)) / refEnergy;
                }
                if (lastResNorm / refForceNorm <= opts.forceTol && (opts.energyTol <= 0.0 || lastEnergyRatio <= opts.energyTol || iter == 1)) {
                    stepConverged = true;
                    break;
                }

                if (opts.fullNewtonTangent) {
                    Kt_ff = Pt * Kt_full * P_free;
                }

                LinearSolverStats stR, stF;
                FactorizationCache cache;
                VecX dU_I = LinearSolverSuite::solve(Kt_ff, R_f, opts.linearSolver, stR, &cache, stopToken);
                VecX dU_F = LinearSolverSuite::solve(Kt_ff, Fref_f, opts.linearSolver, stF, &cache, stopToken);
                if (stopToken.stop_requested()) {
                    return returnCancelled("Analise Arc-Length cancelada.");
                }
                if (!stR.success || !stF.success || !dU_I.allFinite() || !dU_F.allFinite()) {
                    break;
                }

                // Equação quadrática de Crisfield: a1 * dLam^2 + a2 * dLam + a3 = 0
                const VecX w1 = dU_step + dU_I;
                const double a1 = dU_F.squaredNorm() + psi2 * FrefNorm2;
                const double a2 = 2.0 * (w1.dot(dU_F) + psi2 * dLamStep * FrefNorm2);
                const double a3 = w1.squaredNorm() + psi2 * dLamStep * dLamStep * FrefNorm2 - ds * ds;

                double ddLam = 0.0;
                const double disc = a2 * a2 - 4.0 * a1 * a3;
                if (a1 <= 1e-24 || disc < 0.0) {
                    // Sem raiz real na superfície esférica/cilíndrica: fallback linearizado normal ao preditor
                    const double denomLin = dU_step.dot(dU_F) + psi2 * dLamStep * FrefNorm2;
                    if (std::abs(denomLin) > 1e-18) {
                        ddLam = -dU_step.dot(dU_I) / denomLin;
                    } else {
                        break;
                    }
                } else {
                    const double sqrtDisc = std::sqrt(disc);
                    const double r1 = (-a2 + sqrtDisc) / (2.0 * a1);
                    const double r2 = (-a2 - sqrtDisc) / (2.0 * a1);
                    const VecX cand1 = w1 + r1 * dU_F;
                    const VecX cand2 = w1 + r2 * dU_F;
                    const double dot1 = cand1.dot(dU_step) + psi2 * (dLamStep + r1) * dLamStep * FrefNorm2;
                    const double dot2 = cand2.dot(dU_step) + psi2 * (dLamStep + r2) * dLamStep * FrefNorm2;
                    ddLam = (dot1 >= dot2) ? r1 : r2;
                }

                const VecX ddU_free = dU_I + ddLam * dU_F;
                if (iter == 1 || refEnergy <= 0.0) {
                    refEnergy = std::max(1e-12, std::abs(ddU_free.dot(R_f)));
                }
                prevDU_free = ddU_free;
                dU_step += ddU_free;
                dLamStep += ddLam;
                U_free = U_free_conv + dU_step;
                lambdaCurr = lambdaConv + dLamStep;

                deltaU_iter = P_free * ddU_free + ddLam * U_prescribed_unit;
                U_full += deltaU_iter;
            }

            if (stepConverged) {
                ++stepIdx;
                model.commitStep();
                U_free_conv = U_free;
                U_full_conv = U_full;
                lambdaConv = lambdaCurr;
                dU_prev_step = dU_step;
                dLam_prev_step = dLamStep;
                cutbacks = 0;

                VecX reac = computeReactionsFull(baseSys, Fint_final, lambdaConv, Fref_full);
                out.history.push_back({stepIdx, lambdaConv, stepIters, lastResNorm, U_full_conv, reac, stepYielded, lastEnergyRatio});

                // Adaptação suave do raio do arco
                const double ratio = std::sqrt(static_cast<double>(std::max(1, opts.desiredIterations))
                    / static_cast<double>(std::max(1, stepIters)));
                ds = std::clamp(ds * std::clamp(ratio, 0.5, 1.5), dsMin, dsMax);
            } else {
                model.revertStepToCommitted();
                ds *= 0.5;
                ++cutbacks;
                if (ds < dsMin || cutbacks > 12) {
                    out.errorMessage = "Arc-Length não convergiu mesmo após redução de arco.";
                    break;
                }
            }
        }

        out.achievedLoadFactor = lambdaConv;
        out.totalSteps = stepIdx;
        out.finalU_full = U_full_conv;
        if (!out.history.empty()) out.finalReactionsFull = out.history.back().reactionsFull;
        // Arc-length é considerado bem-sucedido se percorreu passos e atingiu alvo ou completou histórico solicitado
        out.success = (stepIdx > 0 && out.errorMessage.empty());
        out.convergenceDiagnostic = out.success
            ? ("Arc-Length convergiu em " + std::to_string(stepIdx) + " passos (lambda=" + std::to_string(lambdaConv) + ").")
            : out.errorMessage;
        return out;
    }
}

} // namespace omniframe::kernel
