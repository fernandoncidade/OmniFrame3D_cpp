#include "DynamicSolver.hpp"
#include <Eigen/Eigenvalues>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseLU>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stop_token>

namespace omniframe::kernel {

namespace {

SpMat buildFreeProlongation(const ReducedLinearSystem& sys) {
    const int nFull = sys.fullDofs;
    const int nFree = sys.freeDofs;
    SpMat P(nFull, nFree);
    if (nFull <= 0 || nFree <= 0) return P;

    std::vector<Triplet> trips;
    if (sys.hasMpc && sys.T_mpc.rows() == nFull && sys.T_mpc.cols() == sys.reducedDofs) {
        std::unordered_map<int, int> redToFree;
        for (int j = 0; j < nFree; ++j) redToFree[sys.freeReducedIndices[j]] = j;
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

int kOrthonormalizeDyn(const SpMat& K, MatX& cols) {
    const int n = static_cast<int>(cols.rows());
    const int m = static_cast<int>(cols.cols());
    std::vector<VecX> keptQ;
    std::vector<VecX> keptKQ;
    keptQ.reserve(m);
    keptKQ.reserve(m);

    double maxNorm = 0.0;
    for (int j = 0; j < m; ++j) {
        VecX v = cols.col(j);
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 0; i < keptQ.size(); ++i) {
                v.noalias() -= v.dot(keptKQ[i]) * keptQ[i];
            }
        }
        VecX Kv = K * v;
        const double normSq = v.dot(Kv);
        if (!std::isfinite(normSq) || normSq <= 1e-24) continue;
        const double norm = std::sqrt(normSq);
        if (keptQ.empty()) maxNorm = norm;
        if (norm <= 1e-11 * std::max(1.0, maxNorm)) continue;
        v /= norm;
        Kv /= norm;
        keptQ.push_back(std::move(v));
        keptKQ.push_back(std::move(Kv));
    }

    const int r = static_cast<int>(keptQ.size());
    cols.resize(n, r);
    for (int j = 0; j < r; ++j) cols.col(j) = keptQ[j];
    return r;
}

} // namespace

bool DynamicSolver::reduceMassAndInfluence(
    const DofManager& dofMgr,
    const SpMat& M_full,
    const ReducedLinearSystem& redSys,
    SpMat& outM_ff,
    std::array<VecX, 3>& outInfluence_f,
    std::string* errorMsg)
{
    if (redSys.fullDofs <= 0 || redSys.freeDofs <= 0 || M_full.rows() != redSys.fullDofs) {
        if (errorMsg) *errorMsg = "Dimensões inválidas na redução da matriz de massa dinâmica.";
        return false;
    }

    const SpMat P_free = buildFreeProlongation(redSys);
    outM_ff = (P_free.transpose() * M_full * P_free).eval();
    outM_ff.makeCompressed();

    // [AUD-011] Constrói vetor de influência sísmica de corpo rígido diretamente sobre
    // os DOFs mestres/independentes livres: 1 nos translacionais (0..2) da direção `d`,
    // e 0 nos DOFs rotacionais (3..5), evitando qualquer aceleração rotacional de base espúria.
    std::vector<int> redToFree(redSys.reducedDofs, -1);
    if (static_cast<int>(redSys.reducedToFreeMap.size()) == redSys.reducedDofs) {
        redToFree = redSys.reducedToFreeMap;
    } else {
        for (int j = 0; j < redSys.freeDofs && j < static_cast<int>(redSys.freeReducedIndices.size()); ++j) {
            const int rd = redSys.freeReducedIndices[static_cast<std::size_t>(j)];
            if (rd >= 0 && rd < redSys.reducedDofs) {
                redToFree[static_cast<std::size_t>(rd)] = j;
            }
        }
    }

    for (int d = 0; d < 3; ++d) {
        outInfluence_f[static_cast<std::size_t>(d)] = VecX::Zero(redSys.freeDofs);
    }

    const bool hasFullToRed = (static_cast<int>(redSys.fullToReducedMap.size()) == redSys.fullDofs);
    for (const auto& ns : dofMgr.nodeStates()) {
        for (int d = 0; d < 3; ++d) {
            const int g = ns.globalDof[static_cast<std::size_t>(d)];
            if (g < 0 || g >= redSys.fullDofs || ns.restrained[static_cast<std::size_t>(d)]) {
                continue;
            }
            const int rd = hasFullToRed ? redSys.fullToReducedMap[static_cast<std::size_t>(g)]
                                        : (!redSys.hasMpc ? g : -1);
            if (rd < 0 || rd >= redSys.reducedDofs) {
                continue; // Escravo de MPC é dirigido cinematicamente pelo mestre via T_mpc
            }
            const int freeIdx = redToFree[static_cast<std::size_t>(rd)];
            if (freeIdx >= 0 && freeIdx < redSys.freeDofs) {
                outInfluence_f[static_cast<std::size_t>(d)](freeIdx) = 1.0;
            }
        }
    }

    return true;
}

ModalResult DynamicSolver::solveModal(
    const SpMat& K_ff,
    const SpMat& M_ff,
    const std::array<VecX, 3>& influence_f,
    const ReducedLinearSystem& redSys,
    const ModalOptions& opts,
    const std::stop_token& stopToken)
{
    ModalResult res;
    if (stopToken.stop_requested()) {
        res.errorMessage = "Analise modal cancelada.";
        return res;
    }
    const int n = static_cast<int>(K_ff.rows());
    if (n <= 0 || K_ff.cols() != n || M_ff.rows() != n || M_ff.cols() != n) {
        res.errorMessage = "Dimensões inválidas em K_ff / M_ff na análise modal.";
        return res;
    }

    if (M_ff.nonZeros() == 0) {
        res.errorMessage = "Matriz de massa dinâmica M_ff é nula (verifique massa específica rho dos materiais).";
        return res;
    }

    if (stopToken.stop_requested()) {
        res.errorMessage = "Analise modal cancelada.";
        return res;
    }

    Eigen::SimplicialLDLT<SpMat> ldlt;
    ldlt.compute(K_ff);
    if (stopToken.stop_requested()) {
        res.errorMessage = "Analise modal cancelada.";
        return res;
    }
    if (ldlt.info() != Eigen::Success) {
        res.errorMessage = "Falha na fatoração de K_ff na análise modal.";
        return res;
    }
    const VecX dVec = ldlt.vectorD();
    const double maxD = dVec.maxCoeff();
    if (!std::isfinite(dVec.minCoeff()) || maxD <= 0.0 || dVec.minCoeff() <= 1e-14 * maxD) {
        res.errorMessage = "Matriz K_ff não é estritamente definida positiva para análise modal.";
        return res;
    }

    const int kReq = std::min(n, std::max(1, opts.numModes));
    int q = std::min(n, std::max(kReq + 8, 2 * kReq));
    if (n <= 64) q = n;

    // Inicializa subespaço com preferência para DOFs com massa não nula
    MatX X(n, q);
    std::vector<double> massWeight(n, 0.0);
    for (int k = 0; k < M_ff.outerSize(); ++k) {
        for (SpMat::InnerIterator it(M_ff, k); it; ++it) {
            massWeight[it.row()] += std::abs(it.value());
        }
    }
    std::vector<int> ranked(n);
    std::iota(ranked.begin(), ranked.end(), 0);
    std::sort(ranked.begin(), ranked.end(), [&](int a, int b) { return massWeight[a] > massWeight[b]; });

    uint64_t seed = 0x517cc1b727220a95ULL;
    auto nextRand = [&]() -> double {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(static_cast<int64_t>(seed >> 11) & 0xFFFFFFFFFFFFFLL) /
            static_cast<double>(0x10000000000000LL) - 0.5;
    };

    X.setZero();
    for (int j = 0; j < q; ++j) {
        if (j < n && massWeight[ranked[j]] > 0.0) X(ranked[j], j) = 1.0;
        for (int i = 0; i < n; ++i) {
            if (massWeight[i] > 0.0 || j >= n / 2) X(i, j) += 0.2 * nextRand();
        }
    }

    int r = kOrthonormalizeDyn(K_ff, X);
    if (r == 0) {
        res.errorMessage = "Falha ao inicializar base K-ortonormal na análise modal.";
        return res;
    }

    std::vector<double> prevOmegas;
    std::vector<double> bestOmegas;
    MatX bestPhi;

    for (int iter = 1; iter <= std::max(1, opts.maxIterations); ++iter) {
        if (stopToken.stop_requested()) {
            res.errorMessage = "Analise modal cancelada.";
            return res;
        }
        res.iterationsPerformed = iter;

        const MatX Y = M_ff * X;
        if (Y.cwiseAbs().maxCoeff() <= 1e-20) {
            res.errorMessage = "Subespaço de massa nulo na iteração modal.";
            return res;
        }
        MatX Xhat = ldlt.solve(Y);
        if (ldlt.info() != Eigen::Success || !Xhat.allFinite()) {
            res.errorMessage = "Erro ao resolver K_ff * X = M_ff * X na análise modal.";
            return res;
        }

        r = kOrthonormalizeDyn(K_ff, Xhat);
        if (r == 0) {
            res.errorMessage = "Sistema sem modos dinâmicos com massa não nula.";
            return res;
        }

        MatX MX = M_ff * Xhat;
        MatX Mproj = Xhat.transpose() * MX;
        Mproj = 0.5 * (Mproj + Mproj.transpose());

        Eigen::SelfAdjointEigenSolver<MatX> eig(Mproj);
        if (eig.info() != Eigen::Success) {
            res.errorMessage = "Falha no autossolvedor denso do subespaço modal.";
            return res;
        }

        // Autovalores de M_proj são mu = 1 / omega^2 ordenados em ordem crescente em Eigen;
        // maiores mu correspondem às menores frequências omega = 1 / sqrt(mu)!
        const auto& mu = eig.eigenvalues();
        const auto& Z = eig.eigenvectors();
        const double maxMu = mu.maxCoeff();

        std::vector<std::pair<int, double>> valid;
        for (int j = r - 1; j >= 0; --j) {
            if (mu(j) > 1e-13 * std::max(1e-18, maxMu)) {
                valid.emplace_back(j, 1.0 / std::sqrt(mu(j)));
            }
        }
        if (valid.empty()) {
            res.errorMessage = "Nenhum autovalor dinâmico positivo encontrado.";
            return res;
        }

        // Ordena por omega crescente
        std::sort(valid.begin(), valid.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

        // Reconstrói todo o subespaço ordenado por maior mu (= menor omega) para máxima velocidade de convergência
        MatX Zpow(r, r);
        for (int j = 0; j < r; ++j) {
            Zpow.col(j) = Z.col(r - 1 - j);
        }
        X = Xhat * Zpow;
        kOrthonormalizeDyn(K_ff, X);

        const int nKeep = std::min(kReq, static_cast<int>(valid.size()));
        std::vector<double> currOmegas;
        currOmegas.reserve(nKeep);
        MatX keepZ(r, nKeep);
        for (int j = 0; j < nKeep; ++j) {
            currOmegas.push_back(valid[j].second);
            keepZ.col(j) = Z.col(valid[j].first);
        }
        bestPhi = Xhat * keepZ;
        bestOmegas = currOmegas;

        if (nKeep > 0 && iter >= 2 && prevOmegas.size() == currOmegas.size()) {
            double maxRel = 0.0;
            for (int j = 0; j < nKeep; ++j) {
                const double rel = std::abs(currOmegas[j] - prevOmegas[j]) / std::max(1e-12, currOmegas[j]);
                maxRel = std::max(maxRel, rel);
            }
            if (maxRel <= opts.tolerance) {
                break;
            }
        }
        prevOmegas = currOmegas;
    }

    // Massa ativa total nas 3 direções globais X, Y, Z
    std::array<VecX, 3> M_r;
    for (int d = 0; d < 3; ++d) {
        if (influence_f[d].size() == n) {
            M_r[d] = M_ff * influence_f[d];
            res.totalActiveMass[d] = influence_f[d].dot(M_r[d]);
        } else {
            M_r[d] = VecX::Zero(n);
            res.totalActiveMass[d] = 0.0;
        }
    }

    constexpr double twoPi = 2.0 * 3.14159265358979323846;
    const int nModesOut = static_cast<int>(bestOmegas.size());
    res.modes.reserve(nModesOut);
    res.cumulativeMassRatio = {0.0, 0.0, 0.0};

    if (stopToken.stop_requested()) {
        res.errorMessage = "Analise modal cancelada.";
        return res;
    }

    // Para visualização em DOFs globais e combinação homogênea, zeramos recalques e offsets prescritos
    ReducedLinearSystem zeroOff = redSys;
    zeroOff.U_fixed_red = VecX::Zero(redSys.reducedDofs);
    zeroOff.U_offset = VecX::Zero(redSys.fullDofs);

    for (int m = 0; m < nModesOut; ++m) {
        VecX phi = bestPhi.col(m);
        const double genMass = phi.dot(M_ff * phi);
        if (genMass > 1e-24) {
            phi /= std::sqrt(genMass); // M_ff-ortonormalização: phi^T * M_ff * phi == 1.0
        }

        // Padroniza sinal pelo componente de maior magnitude para reprodutibilidade determinística
        int idxMax = 0;
        phi.cwiseAbs().maxCoeff(&idxMax);
        if (phi(idxMax) < 0.0) phi = -phi;

        ModeInfo info;
        info.modeIndex = m + 1;
        info.omega = bestOmegas[m];
        info.frequencyHz = info.omega / twoPi;
        info.periodSec = (info.frequencyHz > 1e-15) ? (1.0 / info.frequencyHz) : 0.0;
        info.phi_f = phi;
        info.phi_full = GlobalAssembler::reconstructFullDisplacements(zeroOff, phi);

        for (int d = 0; d < 3; ++d) {
            const double gamma = phi.dot(M_r[d]);
            const double mEff = gamma * gamma;
            info.participationFactors[d] = gamma;
            info.effectiveMass[d] = mEff;
            info.effectiveMassRatio[d] = (res.totalActiveMass[d] > 1e-18)
                ? (mEff / res.totalActiveMass[d]) : 0.0;
            res.cumulativeMassRatio[d] += info.effectiveMassRatio[d];
        }
        res.modes.push_back(std::move(info));
    }

    res.success = !res.modes.empty();
    return res;
}

RayleighDampingParams DynamicSolver::computeRayleighCoefficients(
    double omega1, double zeta1, double omega2, double zeta2)
{
    RayleighDampingParams p;
    omega1 = std::max(1e-9, omega1);
    omega2 = std::max(1e-9, omega2);
    zeta1 = std::max(0.0, zeta1);
    zeta2 = std::max(0.0, zeta2);

    if (std::abs(omega2 - omega1) <= 1e-9 * std::max(omega1, omega2)) {
        p.alphaM = zeta1 * omega1;
        p.betaK = zeta1 / omega1;
        return p;
    }
    p.betaK = 2.0 * (omega2 * zeta2 - omega1 * zeta1) / (omega2 * omega2 - omega1 * omega1);
    p.alphaM = 2.0 * omega1 * zeta1 - p.betaK * omega1 * omega1;
    return p;
}

SpMat DynamicSolver::assembleRayleighDamping(
    const SpMat& M_ff, const SpMat& K_ff, const RayleighDampingParams& params)
{
    return (params.alphaM * M_ff + params.betaK * K_ff).eval();
}

HarmonicResult DynamicSolver::solveHarmonic(
    const SpMat& K_ff,
    const SpMat& M_ff,
    const SpMat& C_ff,
    const VecX& forceAmplitude_f,
    const ReducedLinearSystem& redSys,
    const HarmonicOptions& opts,
    const std::stop_token& stopToken)
{
    HarmonicResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Analise harmonica cancelada.";
        return out;
    }
    const int n = static_cast<int>(K_ff.rows());
    if (n <= 0 || M_ff.rows() != n || forceAmplitude_f.size() != n || opts.frequenciesHz.empty()) {
        out.errorMessage = "Parâmetros inválidos na análise harmônica (FRF).";
        return out;
    }

    using Cd = std::complex<double>;
    using SpCMat = Eigen::SparseMatrix<Cd>;
    using VecXC = Eigen::Matrix<Cd, Eigen::Dynamic, 1>;

    VecXC rhsC = forceAmplitude_f.cast<Cd>();

    constexpr double twoPi = 2.0 * 3.14159265358979323846;
    const bool hasExplicitC = (C_ff.rows() == n && C_ff.cols() == n && C_ff.nonZeros() > 0);
    const bool hasRayleigh = (opts.rayleighAlphaM != 0.0 || opts.rayleighBetaK != 0.0);

    out.sweep.reserve(opts.frequenciesHz.size());
    for (double fHz : opts.frequenciesHz) {
        if (stopToken.stop_requested()) {
            out.sweep.clear();
            out.success = false;
            out.errorMessage = "Analise harmonica cancelada.";
            return out;
        }
        const double w = twoPi * std::max(0.0, fHz);
        std::vector<Eigen::Triplet<Cd>> trips;
        trips.reserve(K_ff.nonZeros() + M_ff.nonZeros() + (hasExplicitC ? C_ff.nonZeros() : 0));

        const Cd kScale(1.0, opts.structuralLossEta + w * opts.rayleighBetaK);
        for (int k = 0; k < K_ff.outerSize(); ++k) {
            for (SpMat::InnerIterator it(K_ff, k); it; ++it) {
                trips.emplace_back(it.row(), it.col(), kScale * it.value());
            }
        }
        const Cd mScale(-w * w, hasRayleigh ? (w * opts.rayleighAlphaM) : 0.0);
        for (int k = 0; k < M_ff.outerSize(); ++k) {
            for (SpMat::InnerIterator it(M_ff, k); it; ++it) {
                trips.emplace_back(it.row(), it.col(), mScale * it.value());
            }
        }
        if (hasExplicitC) {
            const Cd cScale(0.0, w);
            for (int k = 0; k < C_ff.outerSize(); ++k) {
                for (SpMat::InnerIterator it(C_ff, k); it; ++it) {
                    trips.emplace_back(it.row(), it.col(), cScale * it.value());
                }
            }
        }

        SpCMat Zd(n, n);
        Zd.setFromTriplets(trips.begin(), trips.end());
        Zd.makeCompressed();

        if (stopToken.stop_requested()) {
            out.sweep.clear();
            out.success = false;
            out.errorMessage = "Analise harmonica cancelada.";
            return out;
        }

        Eigen::SparseLU<SpCMat> lu;
        lu.compute(Zd);
        if (lu.info() != Eigen::Success) {
            out.errorMessage = "Matriz dinâmica complexa singular na frequência f=" + std::to_string(fHz) + " Hz.";
            return out;
        }
        const VecXC Uc = lu.solve(rhsC);
        if (lu.info() != Eigen::Success || !Uc.allFinite()) {
            out.errorMessage = "Falha numérica na solução harmônica em f=" + std::to_string(fHz) + " Hz.";
            return out;
        }

        // [AUD-011] & [AUD-012]: expande o fasor complexo para todos os DOFs antes de abs/arg,
        // mantendo U_fixed_red e U_offset homogêneos (nulos).
        const int nRed = (redSys.reducedDofs > 0) ? redSys.reducedDofs : n;
        VecXC U_red_c = VecXC::Zero(nRed);
        for (int i = 0; i < n && i < static_cast<int>(redSys.freeReducedIndices.size()); ++i) {
            const int rd = redSys.freeReducedIndices[static_cast<std::size_t>(i)];
            if (rd >= 0 && rd < nRed) {
                U_red_c(rd) = Uc(i);
            }
        }
        VecXC U_full_c;
        if (redSys.hasMpc && redSys.T_mpc.rows() == redSys.fullDofs && redSys.T_mpc.cols() == nRed) {
            U_full_c = redSys.T_mpc.cast<Cd>() * U_red_c;
        } else if (redSys.fullDofs == nRed) {
            U_full_c = std::move(U_red_c);
        } else {
            U_full_c = VecXC::Zero(redSys.fullDofs);
            const Eigen::Index copyN = std::min<Eigen::Index>(U_full_c.size(), U_red_c.size());
            U_full_c.head(copyN) = U_red_c.head(copyN);
        }

        HarmonicStepResult step;
        step.frequencyHz = fHz;
        step.omega = w;
        step.U_complex_f = Uc;
        step.amplitudeFull = U_full_c.cwiseAbs();
        step.phaseRadFull = VecX(U_full_c.size());
        for (Eigen::Index i = 0; i < U_full_c.size(); ++i) {
            step.phaseRadFull(i) = std::arg(U_full_c(i));
        }
        out.sweep.push_back(std::move(step));
    }

    out.success = !out.sweep.empty();
    return out;
}

TransientResult DynamicSolver::solveTransientForceHistory(
    const SpMat& K_ff,
    const SpMat& M_ff,
    const SpMat& C_ff,
    const ReducedLinearSystem& redSys,
    const std::function<VecX(double timeSec, int stepIdx)>& reducedForceFn,
    const TransientOptions& opts,
    const std::stop_token& stopToken)
{
    TransientResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Analise transiente cancelada.";
        return out;
    }
    const int n = static_cast<int>(K_ff.rows());
    const double dt = opts.dt;
    if (n <= 0 || M_ff.rows() != n || dt <= 0.0 || opts.numSteps <= 0) {
        out.errorMessage = "Parâmetros inválidos na integração transiente direta.";
        return out;
    }

    SpMat C_effMat(n, n);
    if (C_ff.rows() == n && C_ff.cols() == n) {
        C_effMat = C_ff;
    } else {
        C_effMat.setZero();
    }
    if (opts.rayleighAlphaM != 0.0 || opts.rayleighBetaK != 0.0) {
        C_effMat = (C_effMat + opts.rayleighAlphaM * M_ff + opts.rayleighBetaK * K_ff).eval();
    }

    double alpha = 0.0;
    double beta = opts.newmarkBeta;
    double gamma = opts.newmarkGamma;
    if (opts.scheme == TransientScheme::HHTAlpha) {
        alpha = std::clamp(opts.hhtAlpha, -1.0 / 3.0, 0.0);
        gamma = 0.5 * (1.0 - 2.0 * alpha);
        beta = 0.25 * (1.0 - alpha) * (1.0 - alpha);
    }

    // Constantes de integração de Newmark / HHT-Alpha
    const double a0 = 1.0 / (beta * dt * dt);
    const double a1 = gamma / (beta * dt);
    const double a2 = 1.0 / (beta * dt);
    const double a3 = 1.0 / (2.0 * beta) - 1.0;
    const double a4 = gamma / beta - 1.0;
    const double a5 = (dt / 2.0) * (gamma / beta - 2.0);
    const double a6 = dt * (1.0 - gamma);
    const double a7 = gamma * dt;

    const double onePlusA = 1.0 + alpha;
    // Keff * u_{n+1} = F_eff (LDLT único para passo dt constante!)
    SpMat Keff = (a0 * M_ff + (onePlusA * a1) * C_effMat + onePlusA * K_ff).eval();
    Keff.makeCompressed();

    Eigen::SimplicialLDLT<SpMat> ldltKeff;
    ldltKeff.compute(Keff);
    if (ldltKeff.info() != Eigen::Success) {
        out.errorMessage = "Falha na fatoração LDLT da matriz efetiva de Newmark/HHT.";
        return out;
    }

    VecX u = (opts.initialDisp_f.size() == n) ? opts.initialDisp_f : VecX::Zero(n);
    VecX v = (opts.initialVel_f.size() == n) ? opts.initialVel_f : VecX::Zero(n);
    VecX F_prev = reducedForceFn(0.0, 0);
    if (F_prev.size() != n) F_prev = VecX::Zero(n);

    // Aceleração inicial: M * a0_init = F(0) - C*v(0) - K*u(0)
    // Se M tiver DOFs rotacionais sem massa (massa=0), regularizamos diagonal mínima só para a_0 consistente
    VecX a = VecX::Zero(n);
    {
        Eigen::SimplicialLDLT<SpMat> ldltM;
        SpMat Mreg = M_ff;
        const double maxMdiag = M_ff.diagonal().cwiseAbs().maxCoeff();
        if (M_ff.diagonal().minCoeff() <= 1e-18 * std::max(1.0, maxMdiag)) {
            std::vector<Triplet> diagShift;
            diagShift.reserve(n);
            const double epsM = 1e-12 * std::max(1.0, maxMdiag);
            for (int i = 0; i < n; ++i) diagShift.emplace_back(i, i, epsM);
            SpMat shift(n, n);
            shift.setFromTriplets(diagShift.begin(), diagShift.end());
            Mreg = (M_ff + shift).eval();
        }
        ldltM.compute(Mreg);
        if (ldltM.info() == Eigen::Success) {
            VecX rhs0 = F_prev - C_effMat * v - K_ff * u;
            a = ldltM.solve(rhs0);
            if (!a.allFinite()) a.setZero();
        }
    }

    ReducedLinearSystem zeroOff = redSys;
    zeroOff.U_fixed_red = VecX::Zero(redSys.reducedDofs);
    zeroOff.U_offset = VecX::Zero(redSys.fullDofs);

    out.history.reserve(static_cast<size_t>(opts.numSteps) + 1);
    out.peakDisplacementFull = VecX::Zero(redSys.fullDofs);

    auto recordStep = [&](int stepIdx, double t) {
        VecX uFull = GlobalAssembler::reconstructFullDisplacements(zeroOff, u);
        for (int i = 0; i < uFull.size(); ++i) {
            out.peakDisplacementFull(i) = std::max(out.peakDisplacementFull(i), std::abs(uFull(i)));
        }
        TransientStepRecord rec;
        rec.step = stepIdx;
        rec.time = t;
        rec.U_full = std::move(uFull);
        rec.V_f = v;
        rec.A_f = a;
        rec.maxDispNorm = u.cwiseAbs().maxCoeff();
        rec.kineticEnergy = 0.5 * v.dot(M_ff * v);
        rec.strainEnergy = 0.5 * u.dot(K_ff * u);
        out.history.push_back(std::move(rec));
    };

    recordStep(0, 0.0);

    for (int s = 1; s <= opts.numSteps; ++s) {
        if (stopToken.stop_requested()) {
            out.history.clear();
            out.peakDisplacementFull.resize(0);
            out.success = false;
            out.errorMessage = "Analise transiente cancelada.";
            return out;
        }
        const double t = s * dt;
        VecX F_curr = reducedForceFn(t, s);
        if (F_curr.size() != n) F_curr = VecX::Zero(n);

        // Formulação unificada HHT-alpha (reduz exatamente para Newmark-Beta quando alpha == 0)
        const VecX uPred = a0 * u + a2 * v + a3 * a;
        const VecX vPred = a1 * u + a4 * v + a5 * a;

        VecX rhs = onePlusA * F_curr - alpha * F_prev
            + M_ff * uPred
            + C_effMat * (onePlusA * vPred + alpha * v)
            + alpha * (K_ff * u);

        const VecX uNew = ldltKeff.solve(rhs);
        if (ldltKeff.info() != Eigen::Success || !uNew.allFinite()) {
            out.errorMessage = "Falha numérica na integração transiente no passo " + std::to_string(s);
            return out;
        }

        const VecX aNew = a0 * (uNew - u) - a2 * v - a3 * a;
        const VecX vNew = v + a6 * a + a7 * aNew;

        u = uNew;
        v = vNew;
        a = aNew;
        F_prev = F_curr;

        recordStep(s, t);
    }

    out.success = true;
    return out;
}

TransientResult DynamicSolver::solveSeismicTimeHistory(
    const SpMat& K_ff,
    const SpMat& M_ff,
    const SpMat& C_ff,
    const std::array<VecX, 3>& influence_f,
    const ReducedLinearSystem& redSys,
    const std::function<Vec3(double timeSec, int stepIdx)>& groundAccelMS2Fn,
    const TransientOptions& opts,
    const std::stop_token& stopToken)
{
    if (stopToken.stop_requested()) {
        TransientResult cancelled;
        cancelled.errorMessage = "Analise sismica cancelada.";
        return cancelled;
    }
    const int n = static_cast<int>(K_ff.rows());
    std::array<VecX, 3> M_r;
    for (int d = 0; d < 3; ++d) {
        M_r[d] = (influence_f[d].size() == n) ? (M_ff * influence_f[d]).eval() : VecX::Zero(n);
    }
    auto forceFn = [&](double t, int step) -> VecX {
        const Vec3 ag = groundAccelMS2Fn(t, step);
        return -(M_r[0] * ag.x() + M_r[1] * ag.y() + M_r[2] * ag.z());
    };
    return solveTransientForceHistory(K_ff, M_ff, C_ff, redSys, forceFn, opts, stopToken);
}

double DynamicSolver::interpolateSpectrum(const std::vector<SpectrumPoint>& curve, double periodSec) {
    if (curve.empty()) return 0.0;
    if (curve.size() == 1 || periodSec <= curve.front().periodSec) return curve.front().Sa;
    if (periodSec >= curve.back().periodSec) return curve.back().Sa;

    for (size_t i = 0; i + 1 < curve.size(); ++i) {
        const double t0 = curve[i].periodSec;
        const double t1 = curve[i + 1].periodSec;
        if (periodSec >= t0 && periodSec <= t1) {
            const double span = t1 - t0;
            if (span <= 1e-15) return curve[i].Sa;
            const double alpha = (periodSec - t0) / span;
            return curve[i].Sa + alpha * (curve[i + 1].Sa - curve[i].Sa);
        }
    }
    return curve.back().Sa;
}

double DynamicSolver::derKiureghianCorrelation(double omegaI, double zetaI, double omegaK, double zetaK) {
    if (omegaI <= 1e-15 || omegaK <= 1e-15) return 0.0;
    const double zi = std::max(1e-7, zetaI);
    const double zk = std::max(1e-7, zetaK);
    double r = omegaK / omegaI;
    double z1 = zi, z2 = zk;
    if (r > 1.0) {
        r = omegaI / omegaK;
        z1 = zk;
        z2 = zi;
    }
    const double num = 8.0 * std::sqrt(z1 * z2) * (z1 + r * z2) * std::pow(r, 1.5);
    const double oneMinusR2 = 1.0 - r * r;
    const double den = oneMinusR2 * oneMinusR2
        + 4.0 * z1 * z2 * r * (1.0 + r * r)
        + 4.0 * (z1 * z1 + z2 * z2) * r * r;
    if (den <= 1e-24) return 1.0;
    return std::clamp(num / den, 0.0, 1.0);
}

std::vector<SpectrumPoint> DynamicSolver::buildNbr15421SpectrumCurve(
    double ag_s_ms2, double Ca, double Cv, double Tmax, int numPoints, double dampingZeta)
{
    // Expressões da ABNT NBR 15421:2006 (Seção 6.2 - Espectro de Resposta de Projeto):
    // a_gs0 = Ca * ag_s; a_gs1 = Cv * ag_s
    // TB = 0.08 * (a_gs1 / a_gs0) = 0.08 * (Cv / Ca); TC = 0.40 * (a_gs1 / a_gs0) = 0.40 * (Cv / Ca)
    // Trecho 1 (0 <= T <= TB): Sa(T) = a_gs0 + (2.5 * eta * a_gs0 - a_gs0) * (T / TB)
    // Trecho 2 (TB < T <= TC): Sa(T) = 2.5 * eta * a_gs0
    // Trecho 3 (T > TC): Sa(T) = eta * a_gs1 / T  (contínuo em TC pois a_gs1 / TC = 2.5 * a_gs0)
    // Correção de amortecimento eta = 1.0 para zeta = 5%; para outros zetas, eta = max(0.55, sqrt(10 / (5 + 100*zeta))).
    const double safeAg = std::isfinite(ag_s_ms2) ? std::max(0.0, ag_s_ms2) : 0.0;
    const double safeCa = (std::isfinite(Ca) && Ca > 1e-6) ? Ca : 1e-6;
    const double safeCv = (std::isfinite(Cv) && Cv > 1e-6) ? Cv : 1e-6;
    const double safeTmax = (std::isfinite(Tmax) && Tmax > 1e-4) ? Tmax : 4.0;
    const double safeZeta = std::isfinite(dampingZeta) ? std::clamp(dampingZeta, 0.0, 0.50) : 0.05;
    const double eta = (std::abs(safeZeta - 0.05) <= 1e-12)
        ? 1.0
        : std::max(0.55, std::sqrt(10.0 / (5.0 + 100.0 * safeZeta)));

    const double a_gs0 = safeCa * safeAg;
    const double a_gs1 = safeCv * safeAg;
    const double TC = 0.40 * (safeCv / safeCa);
    const double TB = 0.20 * TC;

    auto evalSa = [&](double T) -> double {
        if (T <= 0.0) return a_gs0;
        const double plateau = 2.5 * eta * a_gs0;
        if (T < TB) {
            return a_gs0 + (plateau - a_gs0) * (T / std::max(1e-12, TB));
        }
        if (T <= TC) return plateau;
        return (eta * a_gs1) / T;
    };

    numPoints = std::max(10, numPoints);
    std::vector<double> periods;
    periods.reserve(static_cast<std::size_t>(numPoints) + 4);
    for (int i = 0; i <= numPoints; ++i) {
        periods.push_back((safeTmax * static_cast<double>(i)) / static_cast<double>(numPoints));
    }
    if (TB > 0.0 && TB <= safeTmax) periods.push_back(TB);
    if (TC > 0.0 && TC <= safeTmax) periods.push_back(TC);
    std::sort(periods.begin(), periods.end());

    std::vector<SpectrumPoint> pts;
    pts.reserve(periods.size());
    for (double T : periods) {
        if (!pts.empty() && std::abs(T - pts.back().periodSec) <= 1e-12) {
            continue;
        }
        pts.push_back({T, evalSa(T)});
    }
    return pts;
}

ResponseSpectrumResult DynamicSolver::solveResponseSpectrum(
    const SpMat& K_orig,
    const ModalResult& modal,
    const ResponseSpectrumOptions& opts,
    const std::stop_token& stopToken)
{
    ResponseSpectrumResult out;
    if (stopToken.stop_requested()) {
        out.errorMessage = "Analise espectral cancelada.";
        return out;
    }
    if (!modal.success || modal.modes.empty() || opts.spectrumCurve.empty()) {
        out.errorMessage = "Análise modal prévia e curva espectral válidas são obrigatórias para Espectro de Resposta.";
        return out;
    }

    const int nFull = static_cast<int>(modal.modes[0].phi_full.size());
    const int nModes = static_cast<int>(modal.modes.size());
    const bool hasK = (K_orig.rows() == nFull && K_orig.cols() == nFull);

    auto getZeta = [&](int idx) -> double {
        if (idx < static_cast<int>(opts.perModeDampingZeta.size())) {
            return opts.perModeDampingZeta[static_cast<std::size_t>(idx)];
        }
        return opts.modalDampingZeta;
    };

    // Pré-computa Sa / omega^2 para cada modo
    std::vector<double> modalSpectralScale(static_cast<std::size_t>(nModes), 0.0);
    for (int k = 0; k < nModes; ++k) {
        const auto& m = modal.modes[static_cast<std::size_t>(k)];
        const double Sa = interpolateSpectrum(opts.spectrumCurve, m.periodSec);
        const double invW2 = (m.omega > 1e-12) ? (1.0 / (m.omega * m.omega)) : 0.0;
        modalSpectralScale[static_cast<std::size_t>(k)] = Sa * invW2;
    }

    // Pré-computa C_rho[i][k] apenas uma vez caso combinação seja CQC
    std::vector<double> cqcRho;
    if (opts.combination == ResponseSpectrumCombination::CQC) {
        cqcRho.resize(static_cast<std::size_t>(nModes) * static_cast<std::size_t>(nModes), 0.0);
        for (int i = 0; i < nModes; ++i) {
            for (int k = 0; k < nModes; ++k) {
                cqcRho[static_cast<std::size_t>(i) * static_cast<std::size_t>(nModes) + static_cast<std::size_t>(k)] =
                    derKiureghianCorrelation(
                        modal.modes[static_cast<std::size_t>(i)].omega, getZeta(i),
                        modal.modes[static_cast<std::size_t>(k)].omega, getZeta(k));
            }
        }
    }

    out.modalDisplacementsFull.assign(static_cast<std::size_t>(nModes), VecX::Zero(nFull));
    VecX totalDispSq = VecX::Zero(nFull);
    VecX totalReactSq = VecX::Zero(nFull);

    int activeDirections = 0;
    int lastActiveDir = -1;
    std::vector<VecX> lastSingleDirModalD;

    // Combinação modal INDEPENDENTE por direção ortogonal (X, Y, Z), seguida de combinação direcional SRSS.
    // Como Gamma_{k,d} * phi_k é invariante à inversão phi_k -> -phi_k e as direções são combinadas
    // por SRSS entre si, elimina cancelamento espúrio entre eixos e garante invariância a sinal de autovetor.
    for (int d = 0; d < 3; ++d) {
        if (stopToken.stop_requested()) {
            out.success = false;
            out.errorMessage = "Analise espectral cancelada.";
            return out;
        }
        const double dirScale = opts.directionScale[static_cast<std::size_t>(d)];
        if (std::abs(dirScale) <= 1e-15) continue;

        ++activeDirections;
        lastActiveDir = d;

        std::vector<VecX> dirModalD(static_cast<std::size_t>(nModes));
        std::vector<VecX> dirModalR(static_cast<std::size_t>(nModes));
        for (int k = 0; k < nModes; ++k) {
            const auto& m = modal.modes[static_cast<std::size_t>(k)];
            const double coef = dirScale * m.participationFactors[static_cast<std::size_t>(d)] *
                                modalSpectralScale[static_cast<std::size_t>(k)];
            dirModalD[static_cast<std::size_t>(k)] = coef * m.phi_full;
            dirModalR[static_cast<std::size_t>(k)] = hasK
                ? (K_orig * dirModalD[static_cast<std::size_t>(k)]).eval()
                : VecX::Zero(nFull);
            out.modalDisplacementsFull[static_cast<std::size_t>(k)] += dirModalD[static_cast<std::size_t>(k)].cwiseAbs2();
        }
        if (activeDirections == 1) {
            lastSingleDirModalD = dirModalD;
        }

        VecX dirDisp = VecX::Zero(nFull);
        VecX dirReact = VecX::Zero(nFull);

        if (opts.combination == ResponseSpectrumCombination::ABSSUM) {
            for (int k = 0; k < nModes; ++k) {
                dirDisp += dirModalD[static_cast<std::size_t>(k)].cwiseAbs();
                dirReact += dirModalR[static_cast<std::size_t>(k)].cwiseAbs();
            }
        } else if (opts.combination == ResponseSpectrumCombination::SRSS) {
            VecX sumSqD = VecX::Zero(nFull);
            VecX sumSqR = VecX::Zero(nFull);
            for (int k = 0; k < nModes; ++k) {
                sumSqD += dirModalD[static_cast<std::size_t>(k)].cwiseAbs2();
                sumSqR += dirModalR[static_cast<std::size_t>(k)].cwiseAbs2();
            }
            dirDisp = sumSqD.cwiseSqrt();
            dirReact = sumSqR.cwiseSqrt();
        } else {
            VecX cqcSqD = VecX::Zero(nFull);
            VecX cqcSqR = VecX::Zero(nFull);
            for (int i = 0; i < nModes; ++i) {
                for (int k = 0; k < nModes; ++k) {
                    const double rhoIK = cqcRho[static_cast<std::size_t>(i) * static_cast<std::size_t>(nModes) +
                                                static_cast<std::size_t>(k)];
                    cqcSqD += rhoIK * dirModalD[static_cast<std::size_t>(i)].cwiseProduct(
                                          dirModalD[static_cast<std::size_t>(k)]);
                    cqcSqR += rhoIK * dirModalR[static_cast<std::size_t>(i)].cwiseProduct(
                                          dirModalR[static_cast<std::size_t>(k)]);
                }
            }
            dirDisp = cqcSqD.cwiseMax(0.0).cwiseSqrt();
            dirReact = cqcSqR.cwiseMax(0.0).cwiseSqrt();
        }

        totalDispSq += dirDisp.cwiseAbs2();
        totalReactSq += dirReact.cwiseAbs2();
    }
    (void)lastActiveDir;

    if (activeDirections == 1) {
        out.modalDisplacementsFull = std::move(lastSingleDirModalD);
    } else if (activeDirections > 1) {
        for (int k = 0; k < nModes; ++k) {
            out.modalDisplacementsFull[static_cast<std::size_t>(k)] =
                out.modalDisplacementsFull[static_cast<std::size_t>(k)].cwiseSqrt();
        }
    }

    out.combinedDisplacementFull = totalDispSq.cwiseSqrt();
    out.combinedReactionFull = totalReactSq.cwiseSqrt();
    out.success = true;
    return out;
}

} // namespace omniframe::kernel
