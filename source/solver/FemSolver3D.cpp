#include "FemSolver3D.hpp"
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace omniframe {

SolverResult FemSolver3D::solve(StructuralModel& model) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolverResult result;
    model.clearResults();

    // 1. Validação prévia
    ModelValidation val = model.validate();
    if (!val.valid) {
        result.success = false;
        result.errorMessage = val.errors.empty() ? "Erro de validação desconhecido." : val.errors[0];
        return result;
    }

    auto& nodes = model.getNodes();
    auto& elements = model.getElements();

    int nNodes = static_cast<int>(nodes.size());
    int totalDofs = nNodes * 6;
    result.totalNodes = nNodes;
    result.totalElements = static_cast<int>(elements.size());
    result.totalDofs = totalDofs;

    // Mapeamento de NodeId para índice [0, nNodes - 1]
    std::unordered_map<NodeId, int> nodeIndex;
    for (int i = 0; i < nNodes; ++i) {
        nodeIndex[nodes[i].id] = i;
    }

    // 2. Montagem dos Triplets da Matriz de Rigidez Global K
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(elements.size() * 144 + nNodes * 6);

    for (const auto& elem : elements) {
        auto it1 = nodeIndex.find(elem.nodeStart);
        auto it2 = nodeIndex.find(elem.nodeEnd);
        if (it1 == nodeIndex.end() || it2 == nodeIndex.end()) continue;

        int i1 = it1->second;
        int i2 = it2->second;

        std::array<int, 12> dofMap;
        for (int k = 0; k < 6; ++k) dofMap[k] = i1 * 6 + k;
        for (int k = 0; k < 6; ++k) dofMap[k + 6] = i2 * 6 + k;

        Eigen::Matrix<double, 12, 12> Ke = elem.computeGlobalStiffnessMatrix(model);

        for (int r = 0; r < 12; ++r) {
            for (int c = 0; c < 12; ++c) {
                double entryVal = Ke(r, c);
                if (std::abs(entryVal) > 1e-15) {
                    triplets.emplace_back(dofMap[r], dofMap[c], entryVal);
                }
            }
        }
    }

    // Adiciona rigidez de molas nos apoios
    for (int i = 0; i < nNodes; ++i) {
        const auto& sup = nodes[i].support;
        for (int d = 0; d < 6; ++d) {
            if (sup.springStiffness[d] > 1e-9) {
                triplets.emplace_back(i * 6 + d, i * 6 + d, sup.springStiffness[d]);
            }
        }
    }

    Eigen::SparseMatrix<double> K_orig(totalDofs, totalDofs);
    K_orig.setFromTriplets(triplets.begin(), triplets.end());

    // 3. Montagem do Vetor de Cargas Globais F
    Eigen::VectorXd F_orig = Eigen::VectorXd::Zero(totalDofs);

    // Cargas nodais
    for (int i = 0; i < nNodes; ++i) {
        const auto& ld = nodes[i].load;
        int base = i * 6;
        F_orig(base + 0) += ld.force.x;
        F_orig(base + 1) += ld.force.y;
        F_orig(base + 2) += ld.force.z;
        F_orig(base + 3) += ld.moment.x;
        F_orig(base + 4) += ld.moment.y;
        F_orig(base + 5) += ld.moment.z;
    }

    // Forças equivalentes de engastamento perfeito por cargas distribuídas e térmicas (-T^T * f_fef)
    for (const auto& elem : elements) {
        auto it1 = nodeIndex.find(elem.nodeStart);
        auto it2 = nodeIndex.find(elem.nodeEnd);
        if (it1 == nodeIndex.end() || it2 == nodeIndex.end()) continue;

        int i1 = it1->second;
        int i2 = it2->second;
        std::array<int, 12> dofMap;
        for (int k = 0; k < 6; ++k) dofMap[k] = i1 * 6 + k;
        for (int k = 0; k < 6; ++k) dofMap[k + 6] = i2 * 6 + k;

        Eigen::Matrix<double, 12, 1> fGlobal = elem.computeGlobalFixedEndForces(model);
        for (int k = 0; k < 12; ++k) {
            F_orig(dofMap[k]) -= fGlobal(k);
        }
    }

    // 4. Identificação e Aplicação de Condições de Contorno (Apoios e Deslocamentos Prescritos)
    std::vector<bool> isRestrained(totalDofs, false);
    std::vector<double> prescribedVal(totalDofs, 0.0);
    int restrainedCount = 0;
    int springCount = 0;

    for (int i = 0; i < nNodes; ++i) {
        const auto& sup = nodes[i].support;
        int base = i * 6;
        for (int d = 0; d < 6; ++d) {
            int gDof = base + d;
            if (sup.hasPrescribed[d]) {
                isRestrained[gDof] = true;
                prescribedVal[gDof] = sup.prescribedValue[d];
                restrainedCount++;
            } else if (sup.fixed[d]) {
                isRestrained[gDof] = true;
                prescribedVal[gDof] = 0.0;
                restrainedCount++;
            } else if (sup.springStiffness[d] > 1e-9) {
                springCount++;
            }
        }
    }
    result.restrainedDofs = restrainedCount;

    if (restrainedCount == 0 && springCount == 0) {
        result.success = false;
        result.errorMessage = "Estrutura hipostática: nenhum grau de liberdade restringido.";
        return result;
    }

    // Ajusta vetor de forças para deslocamentos prescritos não-nulos
    Eigen::VectorXd F_mod = F_orig;
    for (int g = 0; g < totalDofs; ++g) {
        if (isRestrained[g] && std::abs(prescribedVal[g]) > 1e-12) {
            for (Eigen::SparseMatrix<double>::InnerIterator it(K_orig, g); it; ++it) {
                int row = static_cast<int>(it.row());
                if (!isRestrained[row]) {
                    F_mod(row) -= it.value() * prescribedVal[g];
                }
            }
        }
    }

    // Cria matriz modificada zerando linhas e colunas dos DOFs restritos
    std::vector<Eigen::Triplet<double>> modTriplets;
    modTriplets.reserve(triplets.size());

    for (const auto& trip : triplets) {
        int r = trip.row();
        int c = trip.col();
        if (!isRestrained[r] && !isRestrained[c]) {
            modTriplets.push_back(trip);
        }
    }

    // Coloca 1.0 na diagonal dos DOFs restritos
    for (int g = 0; g < totalDofs; ++g) {
        if (isRestrained[g]) {
            modTriplets.emplace_back(g, g, 1.0);
            F_mod(g) = prescribedVal[g];
        }
    }

    Eigen::SparseMatrix<double> K_mod(totalDofs, totalDofs);
    K_mod.setFromTriplets(modTriplets.begin(), modTriplets.end());

    // 5. Resolução do Sistema K_mod * U = F_mod via SparseLU
    Eigen::SparseLU<Eigen::SparseMatrix<double>> solver;
    solver.compute(K_mod);

    if (solver.info() != Eigen::Success) {
        result.success = false;
        result.errorMessage = "Falha na fatoração da matriz de rigidez global (possível mecanismo ou matriz singular).";
        return result;
    }

    Eigen::VectorXd U = solver.solve(F_mod);
    if (solver.info() != Eigen::Success) {
        result.success = false;
        result.errorMessage = "Falha ao resolver o sistema linear esparso K*U=F.";
        return result;
    }

    // 6. Cálculo das Reações de Apoio (Vínculos Rígidos e Molas Elásticas) e Atribuição aos Nós
    Eigen::VectorXd R_full = K_orig * U - F_orig;

    double maxDisp = 0.0;
    for (int i = 0; i < nNodes; ++i) {
        int base = i * 6;
        const auto& sup = nodes[i].support;
        for (int d = 0; d < 6; ++d) {
            nodes[i].displacement[d] = U(base + d);
            if (isRestrained[base + d]) {
                nodes[i].reaction[d] = R_full(base + d);
            } else if (sup.springStiffness[d] > 1e-9) {
                // Reação elástica da mola sobre o nó: R = -k * u
                nodes[i].reaction[d] = -sup.springStiffness[d] * U(base + d);
            } else {
                nodes[i].reaction[d] = 0.0;
            }
        }
        maxDisp = std::max(maxDisp, nodes[i].displacementNorm());
    }
    result.maxNodalDisplacement = maxDisp;

    // 7. Pós-Processamento dos Elementos (Esforços Locais, Deslocamentos Recuperados e Diagramas)
    result.elementExtremes.clear();
    result.elementExtremes.reserve(elements.size());

    for (auto& elem : elements) {
        int i1 = nodeIndex[elem.nodeStart];
        int i2 = nodeIndex[elem.nodeEnd];

        Eigen::Matrix<double, 12, 1> uGlob = Eigen::Matrix<double, 12, 1>::Zero();
        for (int k = 0; k < 6; ++k) uGlob(k) = U(i1 * 6 + k);
        for (int k = 0; k < 6; ++k) uGlob(k + 6) = U(i2 * 6 + k);

        elem.globalDisplacements = uGlob;

        Eigen::Matrix<double, 12, 12> T = elem.computeTransformationMatrix(model);
        Eigen::Matrix<double, 12, 1> uLocNodal = T * uGlob;
        elem.localDisplacements = elem.recoverLocalElementDisplacements(model, uLocNodal);

        Eigen::Matrix<double, 12, 12> k0 = elem.computeUncondensedLocalStiffnessMatrix(model);
        Eigen::Matrix<double, 12, 1> f0 = elem.computeUncondensedLocalFixedEndForces(model);
        elem.localEndForces = k0 * elem.localDisplacements + f0;

        for (int k = 0; k < 6; ++k) {
            if (elem.releases.start[k]) elem.localEndForces(k) = 0.0;
            if (elem.releases.end[k]) elem.localEndForces(k + 6) = 0.0;
        }
        elem.hasResults = true;

        // Amostragem em N = 32 subdivisões para extremos
        ElementExtremes ext;
        ext.elementId = elem.id;

        int Nsamples = 32;
        for (int s = 0; s <= Nsamples; ++s) {
            double xi = static_cast<double>(s) / static_cast<double>(Nsamples);
            InternalForces forces = elem.evaluateAt(xi, model);

            if (s == 0) {
                ext.N_min = ext.N_max = forces.N;
                ext.Vy_min = ext.Vy_max = forces.Vy;
                ext.Vz_min = ext.Vz_max = forces.Vz;
                ext.Tx_min = ext.Tx_max = forces.Tx;
                ext.My_min = ext.My_max = forces.My;
                ext.Mz_min = ext.Mz_max = forces.Mz;
                ext.maxDeflection = forces.defGlobal.norm();
            } else {
                ext.N_min = std::min(ext.N_min, forces.N);
                ext.N_max = std::max(ext.N_max, forces.N);
                ext.Vy_min = std::min(ext.Vy_min, forces.Vy);
                ext.Vy_max = std::max(ext.Vy_max, forces.Vy);
                ext.Vz_min = std::min(ext.Vz_min, forces.Vz);
                ext.Vz_max = std::max(ext.Vz_max, forces.Vz);
                ext.Tx_min = std::min(ext.Tx_min, forces.Tx);
                ext.Tx_max = std::max(ext.Tx_max, forces.Tx);
                ext.My_min = std::min(ext.My_min, forces.My);
                ext.My_max = std::max(ext.My_max, forces.My);
                ext.Mz_min = std::min(ext.Mz_min, forces.Mz);
                ext.Mz_max = std::max(ext.Mz_max, forces.Mz);
                ext.maxDeflection = std::max(ext.maxDeflection, forces.defGlobal.norm());
            }
        }
        result.elementExtremes.push_back(ext);
    }

    // 8. Verificação do Equilíbrio Estático Global (Soma de Forças e Momentos = 0)
    Vec3 sumAppliedF{0.0, 0.0, 0.0};
    Vec3 sumAppliedM{0.0, 0.0, 0.0};
    Vec3 sumReacF{0.0, 0.0, 0.0};
    Vec3 sumReacM{0.0, 0.0, 0.0};

    for (const auto& n : nodes) {
        // Cargas nodais aplicadas
        sumAppliedF += n.load.force;
        sumAppliedM += n.load.moment + n.position.cross(n.load.force);

        // Reações nodais
        Vec3 rF{n.reaction[0], n.reaction[1], n.reaction[2]};
        Vec3 rM{n.reaction[3], n.reaction[4], n.reaction[5]};
        sumReacF += rF;
        sumReacM += rM + n.position.cross(rF);
    }

    // Adiciona resultante de cargas distribuídas
    for (const auto& elem : elements) {
        if (!elem.distLoad.active) continue;
        double L = elem.computeLength(model);
        Vec3 qGlob{0.0, 0.0, 0.0};
        if (elem.distLoad.coordSystem == CoordSystem::Global) {
            qGlob = elem.distLoad.w;
        } else {
            Eigen::Matrix3d R = elem.computeRotationMatrix3x3(model);
            Eigen::Vector3d ql(elem.distLoad.w.x, elem.distLoad.w.y, elem.distLoad.w.z);
            Eigen::Vector3d qg = R.transpose() * ql;
            qGlob = {qg.x(), qg.y(), qg.z()};
        }
        Vec3 totalF = qGlob * L;
        sumAppliedF += totalF;

        const Node* n1 = model.findNode(elem.nodeStart);
        const Node* n2 = model.findNode(elem.nodeEnd);
        if (n1 && n2) {
            Vec3 midPos = (n1->position + n2->position) * 0.5;
            sumAppliedM += midPos.cross(totalF);
        }
    }

    result.equilibrium.sumAppliedForces = sumAppliedF;
    result.equilibrium.sumReactions = sumReacF;
    result.equilibrium.sumAppliedMoments = sumAppliedM;
    result.equilibrium.sumReactionMoments = sumReacM;

    // Em equilíbrio: Reações + Forças Aplicadas = 0
    result.equilibrium.residualForces = sumReacF + sumAppliedF;
    result.equilibrium.residualMoments = sumReacM + sumAppliedM;

    double resFNorm = result.equilibrium.residualForces.norm();
    double resMNorm = result.equilibrium.residualMoments.norm();
    result.equilibrium.isBalanced = (resFNorm < 1e-2) && (resMNorm < 1e-2);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    result.success = true;

    return result;
}

} // namespace omniframe

