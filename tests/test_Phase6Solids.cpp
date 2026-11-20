#include "TestHelper.hpp"
#define ASSERT_TRUE(cond, msg) TEST_ASSERT((cond), (msg))
#define ASSERT_NEAR(a, b, tol, msg) TEST_ASSERT(std::abs((a) - (b)) <= (tol), (msg))

#include "kernel/assembly/GlobalAssembler.hpp"
#include "kernel/core/DofManager.hpp"
#include "kernel/elements/SolidElements.hpp"
#include "kernel/materials/LinearElasticMaterial.hpp"
#include "kernel/solver/LinearSolverSuite.hpp"

#include <Eigen/Eigenvalues>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace omniframe::kernel;

namespace {

constexpr double PI = 3.14159265358979323846;

void checkSingleSolidSpectral(const SolidElement& elem, const std::vector<Vec3>& coords, const char* label) {
    const MatX K = elem.computeGlobalStiffness();
    const int ndof = elem.localDofCount();
    ASSERT_NEAR((K - K.transpose()).norm(), 0.0, 1e-6 * K.norm(), "K sólido deve ser simétrica");

    Eigen::SelfAdjointEigenSolver<MatX> es(K);
    ASSERT_TRUE(es.info() == Eigen::Success, "Eigen de sólido único deve convergir");
    const VecX ev = es.eigenvalues();
    const double kMax = ev(ndof - 1);

    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(std::abs(ev(i)) <= 1e-9 * kMax, "Os 6 modos de corpo rígido devem ter autovalor zero");
    }
    ASSERT_TRUE(ev(6) > 1e-9 * kMax, "Elemento sólido não pode ter modos de ampulheta");

    Vec3 c = Vec3::Zero();
    for (const auto& p : coords) c += p;
    c /= static_cast<double>(coords.size());

    for (int m = 0; m < 6; ++m) {
        VecX u = VecX::Zero(ndof);
        for (size_t i = 0; i < coords.size(); ++i) {
            if (m < 3) {
                u(3 * i + m) = 1.0;
            } else {
                Vec3 w = Vec3::Zero();
                w(m - 3) = 1.0;
                u.segment<3>(3 * i) = w.cross(coords[i] - c);
            }
        }
        ASSERT_TRUE((K * u).norm() / (kMax * u.norm()) < 1e-10, "K * u_rigido = 0 em 3D");
    }
    std::cout << "[OK] SolidSpectral (" << label << ", V=" << elem.computeVolume() << ")\n";
}

void testSpectralAndInvertedJacobianRejection() {
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "Concrete", 3.0e7, 0.25, 1e-5, 2400.0, 24.0);

    std::vector<Vec3> tet4c{{0, 0, 0}, {1.2, 0.1, 0}, {0.2, 1.3, 0.1}, {0.3, 0.4, 1.1}};
    SolidElement tet4(1, SolidElementType::Tet4, {1, 2, 3, 4}, tet4c, mat);
    checkSingleSolidSpectral(tet4, tet4c, "Tet4");

    // Tet10 com nós médios nos pontos médios das arestas
    auto mid = [&](int a, int b) { return 0.5 * (tet4c[a] + tet4c[b]); };
    std::vector<Vec3> tet10c{
        tet4c[0], tet4c[1], tet4c[2], tet4c[3],
        mid(0, 1), mid(1, 2), mid(0, 2),
        mid(0, 3), mid(1, 3), mid(2, 3)
    };
    SolidElement tet10(2, SolidElementType::Tet10, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, tet10c, mat);
    checkSingleSolidSpectral(tet10, tet10c, "Tet10");

    std::vector<Vec3> hex8c{
        {0, 0, 0}, {1.1, 0.05, 0}, {1.0, 1.2, 0.05}, {0.0, 1.0, 0.0},
        {0.05, 0.05, 1.0}, {1.2, 0.1, 1.1}, {1.05, 1.1, 1.15}, {0.0, 1.05, 1.05}
    };
    SolidElement hex8(3, SolidElementType::Hex8BBar, {1, 2, 3, 4, 5, 6, 7, 8}, hex8c, mat);
    checkSingleSolidSpectral(hex8, hex8c, "Hex8BBar");

    std::vector<Vec3> hex20c(20);
    for (int i = 0; i < 8; ++i) hex20c[i] = hex8c[i];
    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    for (int m = 0; m < 12; ++m) hex20c[8 + m] = 0.5 * (hex8c[edges[m][0]] + hex8c[edges[m][1]]);
    SolidElement hex20(4, SolidElementType::Hex20Serendipity,
                       {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20}, hex20c, mat);
    checkSingleSolidSpectral(hex20, hex20c, "Hex20");

    // Rejeição de tetraedro invertido (nós 2 e 3 trocados => detJ < 0)
    bool threw = false;
    try {
        std::vector<Vec3> badTet{tet4c[0], tet4c[1], tet4c[3], tet4c[2]};
        SolidElement bad(99, SolidElementType::Tet4, {1, 2, 3, 4}, badTet, mat);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT_TRUE(threw, "SolidElement deve rejeitar Jacobiano invertido");
}

void test3DPatchTestDistortedHexAndTet() {
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "PatchMat", 2.0e8, 0.30, 1e-5, 7850.0, 78.5);
    const std::array<bool, NODAL_MAX_DOFS> mask3{true, true, true, false, false, false, false};

    // Bloco [0, 2]^3 dividido em 2x2x2 hexaedros com nó central (1,1,1) fortemente distorcido para (0.76, 1.24, 0.82)
    auto nid = [](int ix, int iy, int iz) { return static_cast<NodeId>(iz * 9 + iy * 3 + ix + 1); };
    std::vector<Vec3> pts(28);
    DofManager dofMgr;

    auto linearDisp = [](const Vec3& p) -> Vec3 {
        return {1.0e-3 * (p.x() + 0.2 * p.y() - 0.1 * p.z()),
                1.0e-3 * (-0.3 * p.x() + 0.5 * p.y() + 0.4 * p.z()),
                1.0e-3 * (0.2 * p.x() - 0.1 * p.y() - 0.4 * p.z())};
    };

    for (int iz = 0; iz <= 2; ++iz) {
        for (int iy = 0; iy <= 2; ++iy) {
            for (int ix = 0; ix <= 2; ++ix) {
                const NodeId id = nid(ix, iy, iz);
                Vec3 p{static_cast<double>(ix), static_cast<double>(iy), static_cast<double>(iz)};
                if (ix == 1 && iy == 1 && iz == 1) p = {0.76, 1.24, 0.82};
                pts[id] = p;
                dofMgr.registerNode(id, mask3);
                if (!(ix == 1 && iy == 1 && iz == 1)) {
                    const Vec3 u = linearDisp(p);
                    for (int d = 0; d < 3; ++d) dofMgr.setBoundaryCondition(id, d, true, u(d), 0.0);
                }
            }
        }
    }
    dofMgr.finalizeNumbering();

    std::vector<std::shared_ptr<IFiniteElement>> hexElems;
    ElementId eid = 1;
    for (int iz = 0; iz < 2; ++iz) {
        for (int iy = 0; iy < 2; ++iy) {
            for (int ix = 0; ix < 2; ++ix) {
                std::vector<NodeId> nids{
                    nid(ix, iy, iz), nid(ix + 1, iy, iz), nid(ix + 1, iy + 1, iz), nid(ix, iy + 1, iz),
                    nid(ix, iy, iz + 1), nid(ix + 1, iy, iz + 1), nid(ix + 1, iy + 1, iz + 1), nid(ix, iy + 1, iz + 1)
                };
                std::vector<Vec3> c;
                for (NodeId i : nids) c.push_back(pts[i]);
                hexElems.push_back(std::make_shared<SolidElement>(eid++, SolidElementType::Hex8BBar, nids, c, mat));
            }
        }
    }

    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    SpMat K; VecX F; std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, hexElems, elLoads, nodLoads, K, F, &err), err);
    const auto cls = dofMgr.classifyDofs(K, F, &err);
    ReducedLinearSystem red;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K, F, cls, red, &err), err);
    LinearSolverStats stats;
    const VecX Uf = LinearSolverSuite::solve(red.K_ff, red.rhs_f, LinearSolverOptions{}, stats);
    ASSERT_TRUE(stats.success, "Patch test 3D Hex8BBar deve convergir");
    const VecX U = GlobalAssembler::reconstructFullDisplacements(red, Uf);

    const NodeId midId = nid(1, 1, 1);
    const Vec3 uMidExact = linearDisp(pts[midId]);
    for (int d = 0; d < 3; ++d) {
        ASSERT_NEAR(U(dofMgr.getNodeGlobalDof(midId, d)), uMidExact(d), 1e-12, "Patch test 3D nó interior distorcido");
    }
    std::cout << "[OK] Solid3DPatchTestDistorted (Hex8BBar err < 1e-12)\n";
}

void testNearIncompressibilityHex8BBar() {
    // Viga engastada quase incompressível nu = 0.499 (sem B-bar trava gravemente!)
    const double E = 1.0e5;
    const double nu = 0.499;
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "RubberLike", E, nu, 0.0, 1200.0, 12.0);
    const double L = 6.0, H = 1.0, B = 1.0;
    const int nx = 12, ny = 1, nz = 2;

    DofManager dofMgr;
    const std::array<bool, NODAL_MAX_DOFS> mask3{true, true, true, false, false, false, false};
    auto nid = [&](int ix, int iy, int iz) {
        return static_cast<NodeId>((iz * (ny + 1) + iy) * (nx + 1) + ix + 1);
    };
    std::vector<Vec3> pts((nx + 1) * (ny + 1) * (nz + 1) + 1);

    for (int iz = 0; iz <= nz; ++iz) {
        for (int iy = 0; iy <= ny; ++iy) {
            for (int ix = 0; ix <= nx; ++ix) {
                const NodeId id = nid(ix, iy, iz);
                pts[id] = {L * ix / nx, B * iy / ny, H * iz / nz};
                dofMgr.registerNode(id, mask3);
                if (ix == 0) {
                    for (int d = 0; d < 3; ++d) dofMgr.setBoundaryCondition(id, d, true, 0.0, 0.0);
                }
            }
        }
    }
    dofMgr.finalizeNumbering();

    std::vector<std::shared_ptr<IFiniteElement>> elems;
    ElementId eid = 1;
    for (int iz = 0; iz < nz; ++iz) {
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                std::vector<NodeId> nids{
                    nid(ix, iy, iz), nid(ix + 1, iy, iz), nid(ix + 1, iy + 1, iz), nid(ix, iy + 1, iz),
                    nid(ix, iy, iz + 1), nid(ix + 1, iy, iz + 1), nid(ix + 1, iy + 1, iz + 1), nid(ix, iy + 1, iz + 1)
                };
                std::vector<Vec3> c;
                for (NodeId i : nids) c.push_back(pts[i]);
                elems.push_back(std::make_shared<SolidElement>(eid++, SolidElementType::Hex8BBar, nids, c, mat));
            }
        }
    }

    // Carga total Fz = -100 kN distribuída na face da ponta x = L
    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    const double P = -100.0;
    const int nTip = (ny + 1) * (nz + 1);
    for (int iz = 0; iz <= nz; ++iz) {
        for (int iy = 0; iy <= ny; ++iy) {
            Eigen::Matrix<double, NODAL_MAX_DOFS, 1> f = Eigen::Matrix<double, NODAL_MAX_DOFS, 1>::Zero();
            f(2) = P / nTip;
            nodLoads[nid(nx, iy, iz)] = f;
        }
    }

    SpMat K; VecX F; std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K, F, &err), err);
    const auto cls = dofMgr.classifyDofs(K, F, &err);
    ReducedLinearSystem red;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K, F, cls, red, &err), err);
    LinearSolverStats stats;
    const VecX Uf = LinearSolverSuite::solve(red.K_ff, red.rhs_f, LinearSolverOptions{}, stats);
    ASSERT_TRUE(stats.success, "B-bar nu=0.499 deve resolver");
    const VecX U = GlobalAssembler::reconstructFullDisplacements(red, Uf);

    const double wTip = std::abs(U(dofMgr.getNodeGlobalDof(nid(nx, 0, nz), 2)));
    const double I = B * H * H * H / 12.0;
    const double G = E / (2.0 * (1.0 + nu));
    const double wTimo = std::abs(P) * L * L * L / (3.0 * E * I) + std::abs(P) * L / ((5.0 / 6.0) * G * (B * H));
    ASSERT_TRUE(wTip > 0.70 * wTimo && wTip < 1.15 * wTimo,
                "Hex8BBar em nu=0.499 não pode travar volumetricamente (>70% de Timoshenko)");
    std::cout << "[OK] IncompressibilityBBar nu=0.499 (wTip=" << wTip << ", Timoshenko=" << wTimo << ")\n";
}

void testLameThickCylinderHex20() {
    // Cilindro espesso sob pressão interna pi (Lamé analítico): a = 1.0, b = 2.0, H = 0.25, quarto de simetria
    const double a = 1.0, b = 2.0, H = 0.25, pi = 1000.0;
    const double E = 2.0e8, nu = 0.30;
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "Steel", E, nu, 0.0, 7850.0, 78.5);

    const int nr = 4, nt = 4; // malha 4x4 de Hex20 quadráticos (8 divisões geométricas por arco)
    const int nR = 2 * nr + 1;
    const int nT = 2 * nt + 1;
    const int nZ = 3;

    auto nid = [&](int ir, int it, int iz) {
        return static_cast<NodeId>((iz * nT + it) * nR + ir + 1);
    };
    std::vector<Vec3> pts(nZ * nT * nR + 1);
    DofManager dofMgr;
    const std::array<bool, NODAL_MAX_DOFS> mask3{true, true, true, false, false, false, false};

    for (int iz = 0; iz < nZ; ++iz) {
        const double z = H * iz / (nZ - 1);
        for (int it = 0; it < nT; ++it) {
            const double theta = (0.5 * PI) * it / (nT - 1);
            for (int ir = 0; ir < nR; ++ir) {
                if ((ir % 2 == 1 && it % 2 == 1) || (iz == 1 && (ir % 2 == 1 || it % 2 == 1))) continue;
                const double r = a + (b - a) * ir / (nR - 1);
                const NodeId id = nid(ir, it, iz);
                pts[id] = {r * std::cos(theta), r * std::sin(theta), z};
                dofMgr.registerNode(id, mask3);

                // Estado plano de deformação axial: w(z=0)=w(z=H)=0
                dofMgr.setBoundaryCondition(id, 2, true, 0.0, 0.0);
                if (it == 0) dofMgr.setBoundaryCondition(id, 1, true, 0.0, 0.0);      // y=0 -> u_y=0
                if (it == nT - 1) dofMgr.setBoundaryCondition(id, 0, true, 0.0, 0.0); // x=0 -> u_x=0
            }
        }
    }
    dofMgr.finalizeNumbering();

    std::vector<std::shared_ptr<IFiniteElement>> elems;
    ElementId eid = 1;
    for (int et = 0; et < nt; ++et) {
        for (int er = 0; er < nr; ++er) {
            const int ir = 2 * er, it = 2 * et;
            std::vector<NodeId> nids{
                nid(ir, it, 0), nid(ir + 2, it, 0), nid(ir + 2, it + 2, 0), nid(ir, it + 2, 0),
                nid(ir, it, 2), nid(ir + 2, it, 2), nid(ir + 2, it + 2, 2), nid(ir, it + 2, 2),
                nid(ir + 1, it, 0), nid(ir + 2, it + 1, 0), nid(ir + 1, it + 2, 0), nid(ir, it + 1, 0),
                nid(ir + 1, it, 2), nid(ir + 2, it + 1, 2), nid(ir + 1, it + 2, 2), nid(ir, it + 1, 2),
                nid(ir, it, 1), nid(ir + 2, it, 1), nid(ir + 2, it + 2, 1), nid(ir, it + 2, 1)
            };
            std::vector<Vec3> c;
            for (NodeId id : nids) c.push_back(pts[id]);
            elems.push_back(std::make_shared<SolidElement>(eid++, SolidElementType::Hex20Serendipity, nids, c, mat));
        }
    }

    // Força nodal equivalente exata para pressão uniforme pi na face cilíndrica r=a (integração Q8 2D)
    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    const double dTheta = (0.5 * PI) / nt;
    const double gPts[3] = {-std::sqrt(3.0 / 5.0), 0.0, std::sqrt(3.0 / 5.0)};
    const double gWts[3] = {5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0};

    for (int et = 0; et < nt; ++et) {
        const int it = 2 * et;
        const NodeId faceNids[8] = {
            nid(0, it, 0), nid(0, it + 2, 0), nid(0, it + 2, 2), nid(0, it, 2),
            nid(0, it + 1, 0), nid(0, it + 2, 1), nid(0, it + 1, 2), nid(0, it, 1)
        };
        const double th0 = dTheta * et;
        const double cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
        for (int i1 = 0; i1 < 3; ++i1) {
            for (int i2 = 0; i2 < 3; ++i2) {
                const double xi = gPts[i1], eta = gPts[i2];
                const double wt = gWts[i1] * gWts[i2];
                double N[8];
                for (int k = 0; k < 4; ++k) {
                    N[k] = 0.25 * (1 + cx[k] * xi) * (1 + cy[k] * eta) * (cx[k] * xi + cy[k] * eta - 1);
                }
                N[4] = 0.5 * (1 - xi * xi) * (1 - eta);
                N[5] = 0.5 * (1 + xi) * (1 - eta * eta);
                N[6] = 0.5 * (1 - xi * xi) * (1 + eta);
                N[7] = 0.5 * (1 - xi) * (1 - eta * eta);
                const double th = th0 + 0.5 * (xi + 1.0) * dTheta;
                const double dArea = (0.5 * a * dTheta) * (0.5 * H) * wt;
                const Vec3 pVec(pi * std::cos(th) * dArea, pi * std::sin(th) * dArea, 0.0);
                for (int k = 0; k < 8; ++k) {
                    auto [kv, inserted] = nodLoads.try_emplace(
                        faceNids[k], Eigen::Matrix<double, NODAL_MAX_DOFS, 1>::Zero());
                    kv->second(0) += N[k] * pVec.x();
                    kv->second(1) += N[k] * pVec.y();
                }
            }
        }
    }

    SpMat K; VecX F; std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K, F, &err), err);
    const auto cls = dofMgr.classifyDofs(K, F, &err);
    ReducedLinearSystem red;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K, F, cls, red, &err), err);
    LinearSolverStats stats;
    const VecX Uf = LinearSolverSuite::solve(red.K_ff, red.rhs_f, LinearSolverOptions{}, stats);
    ASSERT_TRUE(stats.success, "Lamé Hex20 deve convergir");
    const VecX U = GlobalAssembler::reconstructFullDisplacements(red, Uf);

    const double urInnerCalc = U(dofMgr.getNodeGlobalDof(nid(0, 0, 0), 0));
    const double urOuterCalc = U(dofMgr.getNodeGlobalDof(nid(nR - 1, 0, 0), 0));
    const double coef = (1.0 + nu) * a * a * pi / (E * (b * b - a * a));
    const double urInnerExact = coef * ((1.0 - 2.0 * nu) * a + b * b / a);
    const double urOuterExact = coef * ((1.0 - 2.0 * nu) * b + b * b / b);

    ASSERT_NEAR(urInnerCalc, urInnerExact, 0.01 * urInnerExact, "Deslocamento radial interno Lamé (<1% erro)");
    ASSERT_NEAR(urOuterCalc, urOuterExact, 0.01 * urOuterExact, "Deslocamento radial externo Lamé (<1% erro)");

    // Checa tensão média no 1º anel (r ~ 1.125) vs Lamé analítico
    const auto& el0 = static_cast<const SolidElement&>(*elems[0]);
    VecX ue(el0.localDofCount());
    const auto& el0Nids = el0.nodeIds();
    for (size_t i = 0; i < el0Nids.size(); ++i) {
        for (int d = 0; d < 3; ++d) ue(3 * i + d) = U(dofMgr.getNodeGlobalDof(el0Nids[i], d));
    }
    const SolidStressState st0 = el0.evaluateCenterStress(ue);
    const double rMid = a + 0.5 * (b - a) / nr;
    const double srExact = a * a * pi / (b * b - a * a) * (1.0 - b * b / (rMid * rMid));
    const double stExact = a * a * pi / (b * b - a * a) * (1.0 + b * b / (rMid * rMid));
    const double cTh = std::cos(0.5 * dTheta), sTh = std::sin(0.5 * dTheta);
    const double srCalc = st0.sxx * cTh * cTh + st0.syy * sTh * sTh + 2.0 * st0.txy * cTh * sTh;
    const double stCalc = st0.sxx * sTh * sTh + st0.syy * cTh * cTh - 2.0 * st0.txy * cTh * sTh;
    ASSERT_NEAR(srCalc, srExact, 0.05 * std::abs(srExact), "Tensão radial Lamé próxima ao analítico");
    ASSERT_NEAR(stCalc, stExact, 0.03 * std::abs(stExact), "Tensão circunferencial Lamé próxima ao analítico");

    std::cout << "[OK] LameCylinder Hex20 (ur_a=" << urInnerCalc << " vs exact=" << urInnerExact
              << ", s_theta=" << stCalc << " vs " << stExact << ")\n";
}

void testSolidMassThermalAndGravity() {
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "Conc", 3.0e7, 0.20, 1.0e-5, 2500.0, 0.0);
    std::vector<Vec3> cube{
        {0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}
    };
    SolidElement hex(1, SolidElementType::Hex8BBar, {1,2,3,4,5,6,7,8}, cube, mat);

    // Massa consistente usa rho = 2500 kg/m³ -> 2.5 t, mesmo com gamma = 0
    const MatX M = hex.computeGlobalMass(true);
    VecX unitZ = VecX::Zero(24);
    for (int i = 0; i < 8; ++i) unitZ(3 * i + 2) = 1.0;
    ASSERT_NEAR(unitZ.dot(M * unitZ), 2.5, 1e-12, "Massa de sólido 1m³ rho=2500 deve ser 2.5 t");

    // Bloqueado em todos os DOFs + deltaT = +50 °C -> compressão hidrostática exata -E*alpha*dT/(1-2*nu)
    const double dT = 50.0;
    const SolidStressState stTh = hex.evaluateCenterStress(VecX::Zero(24), dT);
    const double sigExact = -3.0e7 * 1.0e-5 * dT / (1.0 - 2.0 * 0.20);
    ASSERT_NEAR(stTh.sxx, sigExact, 1e-6, "Tensão térmica confinada 3D sxx exata");
    ASSERT_NEAR(stTh.syy, sigExact, 1e-6, "Tensão térmica confinada 3D syy exata");
    ASSERT_NEAR(stTh.szz, sigExact, 1e-6, "Tensão térmica confinada 3D szz exata");
    std::cout << "[OK] SolidMassThermal (M=" << unitZ.dot(M * unitZ) << " t, s_th=" << stTh.sxx << " kPa)\n";
}

} // namespace

int main() {
    testSpectralAndInvertedJacobianRejection();
    test3DPatchTestDistortedHexAndTet();
    testNearIncompressibilityHex8BBar();
    testLameThickCylinderHex20();
    testSolidMassThermalAndGravity();
    std::cout << "\nTodos os testes da Fase 6 (Sólidos TET4, TET10, HEX8-Bbar, HEX20) passaram!\n";
    return 0;
}
