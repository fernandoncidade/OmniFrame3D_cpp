#include "BucklingSolver.hpp"
#include <Eigen/Eigenvalues>
#include <Eigen/SparseCholesky>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stop_token>

namespace omniframe::kernel {

namespace {

int kOrthonormalize(const SpMat& K, MatX& cols, std::vector<VecX>* KcolsOut = nullptr) {
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
                const double dot = v.dot(keptKQ[i]);
                v.noalias() -= dot * keptQ[i];
            }
        }
        VecX Kv = K * v;
        const double normSq = v.dot(Kv);
        if (!std::isfinite(normSq) || normSq <= 1e-24) {
            continue;
        }
        const double norm = std::sqrt(normSq);
        if (keptQ.empty()) maxNorm = norm;
        if (norm <= 1e-11 * std::max(1.0, maxNorm)) {
            continue;
        }
        v /= norm;
        Kv /= norm;
        keptQ.push_back(std::move(v));
        keptKQ.push_back(std::move(Kv));
    }

    const int r = static_cast<int>(keptQ.size());
    cols.resize(n, r);
    if (KcolsOut) KcolsOut->clear();
    for (int j = 0; j < r; ++j) {
        cols.col(j) = keptQ[j];
        if (KcolsOut) KcolsOut->push_back(std::move(keptKQ[j]));
    }
    return r;
}

VecX normalizeModeFull(VecX fullVec) {
    const double scale = fullVec.cwiseAbs().maxCoeff();
    if (scale > 1e-15) {
        fullVec /= scale;
    }
    return fullVec;
}

} // namespace

BucklingResult BucklingSolver::solve(const SpMat& K_ff,
                                     const SpMat& Kg_ff,
                                     const ReducedLinearSystem& redSys,
                                     const BucklingOptions& opts,
                                     const std::stop_token& stopToken)
{
    BucklingResult res;
    if (stopToken.stop_requested()) {
        res.errorMessage = "Analise de flambagem cancelada.";
        return res;
    }
    const int n = static_cast<int>(K_ff.rows());
    if (n <= 0 || K_ff.cols() != n || Kg_ff.rows() != n || Kg_ff.cols() != n) {
        res.errorMessage = "Dimensões inválidas das matrizes K_ff / Kg_ff para análise de flambagem.";
        return res;
    }

    if (stopToken.stop_requested()) {
        res.errorMessage = "Analise de flambagem cancelada.";
        return res;
    }

    Eigen::SimplicialLDLT<SpMat> ldlt;
    ldlt.compute(K_ff);
    if (stopToken.stop_requested()) {
        res.errorMessage = "Analise de flambagem cancelada.";
        return res;
    }
    if (ldlt.info() != Eigen::Success) {
        res.errorMessage = "Falha na fatoração LDLT de K_ff (sistema possivelmente singular ou hipostático).";
        return res;
    }
    const VecX dVec = ldlt.vectorD();
    const double maxD = dVec.maxCoeff();
    const double minD = dVec.minCoeff();
    if (!std::isfinite(minD) || maxD <= 0.0 || minD <= 1e-14 * maxD) {
        res.errorMessage = "Matriz K_ff não é estritamente definida positiva para análise de flambagem.";
        return res;
    }

    const SpMat B = (-Kg_ff).eval(); // (-Kg) * phi = mu * K * phi, onde lambda = 1 / mu
    if (B.nonZeros() == 0) {
        res.errorMessage = "Matriz de rigidez geométrica nula (não há esforço axial para flambagem).";
        return res;
    }

    const int kReq = std::min(n, std::max(1, opts.numModes));
    int q = std::min(n, std::max(kReq + 8, 2 * kReq));
    if (n <= 64) q = n; // Para modelos pequenos, subespaço completo converge em 1-2 passos com precisão de máquina

    // Semente determinística combinando base canônica em colunas ativas de B e mistura pseudo-aleatória
    MatX X(n, q);
    std::vector<double> colScore(n, 0.0);
    for (int outer = 0; outer < B.outerSize(); ++outer) {
        for (SpMat::InnerIterator it(B, outer); it; ++it) {
            colScore[it.row()] += std::abs(it.value());
        }
    }
    std::vector<int> ranked(n);
    std::iota(ranked.begin(), ranked.end(), 0);
    std::sort(ranked.begin(), ranked.end(), [&](int a, int b) { return colScore[a] > colScore[b]; });

    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    auto nextRand = [&]() -> double {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(static_cast<int64_t>(seed >> 11) & 0xFFFFFFFFFFFFFLL) /
            static_cast<double>(0x10000000000000LL) - 0.5;
    };

    X.setZero();
    for (int j = 0; j < q; ++j) {
        if (j < n && colScore[ranked[j]] > 0.0) {
            X(ranked[j], j) = 1.0;
        }
        for (int i = 0; i < n; ++i) {
            X(i, j) += 0.25 * nextRand();
        }
    }

    int r = kOrthonormalize(K_ff, X);
    if (r == 0) {
        res.errorMessage = "Não foi possível construir base inicial K-ortonormal válida.";
        return res;
    }

    std::vector<double> prevLambdas;
    MatX eigVecsBest;
    std::vector<double> bestLambdas;
    MatX Qbest;

    for (int iter = 1; iter <= std::max(1, opts.maxIterations); ++iter) {
        if (stopToken.stop_requested()) {
            res.errorMessage = "Analise de flambagem cancelada.";
            return res;
        }
        res.iterationsPerformed = iter;

        MatX Y = B * X;
        // Se algum vetor caiu no núcleo de B, filtramos na ortogonalização após resolver K*Xhat = Y
        MatX Xhat = ldlt.solve(Y);
        if (ldlt.info() != Eigen::Success || !Xhat.allFinite()) {
            res.errorMessage = "Erro numérico ao resolver K_ff * X = (-Kg_ff) * X.";
            return res;
        }

        // Mantém subespaço K-ortonormalizado com posto revelado (elimina vetores no núcleo nulo de Kg)
        std::vector<VecX> KQ;
        r = kOrthonormalize(K_ff, Xhat, &KQ);
        if (r == 0) {
            res.errorMessage = "Matriz geométrica sem modos de flambagem não triviais.";
            return res;
        }

        MatX BQ = B * Xhat;
        MatX Bproj = Xhat.transpose() * BQ;
        Bproj = 0.5 * (Bproj + Bproj.transpose());

        Eigen::SelfAdjointEigenSolver<MatX> eig(Bproj);
        if (eig.info() != Eigen::Success) {
            res.errorMessage = "Falha no autossolvedor simétrico denso do subespaço reduzido.";
            return res;
        }

        const auto& mu = eig.eigenvalues();
        const auto& Z = eig.eigenvectors();

        // Ordena pares válidos: positivos primeiro em ordem crescente de lambda = 1/mu (ou por |mu|)
        struct ModePair {
            int colIdx = 0;
            double muVal = 0.0;
            double lambdaVal = 0.0;
        };
        std::vector<ModePair> candidates;
        double maxAbsMu = mu.cwiseAbs().maxCoeff();
        for (int j = 0; j < r; ++j) {
            const double m = mu(j);
            if (std::abs(m) <= 1e-12 * std::max(1.0, maxAbsMu)) continue;
            if (!opts.includeNegativeFactors && m <= 0.0) continue;
            candidates.push_back({j, m, 1.0 / m});
        }
        std::sort(candidates.begin(), candidates.end(), [](const ModePair& a, const ModePair& b) {
            if (a.lambdaVal > 0.0 && b.lambdaVal <= 0.0) return true;
            if (a.lambdaVal <= 0.0 && b.lambdaVal > 0.0) return false;
            return std::abs(a.lambdaVal) < std::abs(b.lambdaVal);
        });

        // Constrói próximo X mantendo direções de maior |mu| para aceleração máxima da iteração de potência
        std::vector<int> powOrder(r);
        std::iota(powOrder.begin(), powOrder.end(), 0);
        std::sort(powOrder.begin(), powOrder.end(), [&](int a, int b) {
            return std::abs(mu(a)) > std::abs(mu(b));
        });
        MatX Zsorted(r, r);
        for (int j = 0; j < r; ++j) Zsorted.col(j) = Z.col(powOrder[j]);
        X = Xhat * Zsorted;
        kOrthonormalize(K_ff, X);

        const int nKeep = std::min(kReq, static_cast<int>(candidates.size()));
        std::vector<double> currLambdas;
        currLambdas.reserve(nKeep);
        MatX bestZ(r, nKeep);
        for (int j = 0; j < nKeep; ++j) {
            currLambdas.push_back(candidates[j].lambdaVal);
            bestZ.col(j) = Z.col(candidates[j].colIdx);
        }
        Qbest = Xhat * bestZ;
        bestLambdas = currLambdas;

        if (nKeep > 0 && iter >= 2 && prevLambdas.size() == currLambdas.size()) {
            double maxRelErr = 0.0;
            for (int j = 0; j < nKeep; ++j) {
                const double rel = std::abs(currLambdas[j] - prevLambdas[j]) / std::max(1e-12, std::abs(currLambdas[j]));
                maxRelErr = std::max(maxRelErr, rel);
            }
            if (maxRelErr <= opts.tolerance) {
                break;
            }
        }
        prevLambdas = currLambdas;
    }

    if (bestLambdas.empty()) {
        res.errorMessage = "Nenhum modo crítico de flambagem positivo encontrado para o carregamento informado.";
        return res;
    }

    if (stopToken.stop_requested()) {
        res.errorMessage = "Analise de flambagem cancelada.";
        return res;
    }

    res.loadFactors = std::move(bestLambdas);
    res.modeShapesFull.reserve(res.loadFactors.size());
    ReducedLinearSystem zeroSys = redSys;
    zeroSys.U_fixed_red = VecX::Zero(redSys.reducedDofs);
    zeroSys.U_offset = VecX::Zero(redSys.fullDofs);
    for (int j = 0; j < static_cast<int>(res.loadFactors.size()); ++j) {
        VecX phi_f = Qbest.col(j);
        VecX phi_full = GlobalAssembler::reconstructFullDisplacements(zeroSys, phi_f);
        res.modeShapesFull.push_back(normalizeModeFull(std::move(phi_full)));
    }
    res.success = true;
    return res;
}

std::vector<Vec3> BucklingSolver::applyGeometricImperfection(const std::vector<Vec3>& baseNodeCoords,
                                                             const DofManager& dofMgr,
                                                             const VecX& modeFull,
                                                             double amplitude)
{
    std::vector<Vec3> out = baseNodeCoords;
    if (out.empty() || !std::isfinite(amplitude) || amplitude == 0.0) {
        return out;
    }

    double maxDispNorm = 0.0;
    const auto& states = dofMgr.nodeStates();
    const size_t nNodes = std::min(out.size(), states.size());
    std::vector<Vec3> modalTrans(nNodes, Vec3::Zero());
    for (size_t i = 0; i < nNodes; ++i) {
        for (int d = 0; d < 3; ++d) {
            const int g = states[i].globalDof[d];
            if (g >= 0 && g < modeFull.size()) {
                modalTrans[i](d) = modeFull(g);
            }
        }
        maxDispNorm = std::max(maxDispNorm, modalTrans[i].norm());
    }

    if (maxDispNorm <= 1e-15) {
        return out;
    }
    const double factor = amplitude / maxDispNorm;
    for (size_t i = 0; i < nNodes; ++i) {
        out[i] += factor * modalTrans[i];
    }
    return out;
}

} // namespace omniframe::kernel
