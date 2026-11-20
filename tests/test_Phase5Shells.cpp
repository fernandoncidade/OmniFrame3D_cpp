#include "TestHelper.hpp"
#define ASSERT_TRUE(cond, msg) TEST_ASSERT((cond), (msg))
#define ASSERT_NEAR(a, b, tol, msg) TEST_ASSERT(std::abs((a) - (b)) <= (tol), (msg))

#include "core/StructuralModel.hpp"
#include "kernel/analysis/BucklingSolver.hpp"
#include "kernel/assembly/GlobalAssembler.hpp"
#include "kernel/core/DofManager.hpp"
#include "kernel/elements/ShellElements.hpp"
#include "kernel/materials/LinearElasticMaterial.hpp"
#include "kernel/mesh/MeshGenerator.hpp"
#include "kernel/solver/LinearSolverSuite.hpp"
#include "solver/FemSolver3D.hpp"

#include <Eigen/Eigenvalues>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace omniframe::kernel;

namespace {

constexpr double PI = 3.14159265358979323846;

double navierSquarePlateCenterW(double q, double L, double D) {
    double sum = 0.0;
    for (int m = 1; m <= 25; m += 2) {
        const double sm = (m % 4 == 1) ? 1.0 : -1.0;
        for (int n = 1; n <= 25; n += 2) {
            const double sn = (n % 4 == 1) ? 1.0 : -1.0;
            const double denom = static_cast<double>(m * n) * std::pow((m * m + n * n) / (L * L), 2);
            sum += (sm * sn) / denom;
        }
    }
    return 16.0 * q / (std::pow(PI, 6) * D) * sum;
}

void checkSingleElementSpectral(const ShellElement& elem, const std::vector<Vec3>& coords, const char* label) {
    const MatX K = elem.computeGlobalStiffness();
    const int ndof = elem.localDofCount();
    ASSERT_NEAR((K - K.transpose()).norm(), 0.0, 1e-6 * K.norm(), "Matriz de rigidez da casca deve ser simétrica");

    Eigen::SelfAdjointEigenSolver<MatX> es(K);
    ASSERT_TRUE(es.info() == Eigen::Success, "Autovalores de elemento único devem convergir");
    const VecX evals = es.eigenvalues();
    const double kMax = evals(ndof - 1);

    // Primeiros 6 modos = corpo rígido (energia zero), 7º modo > 0 (sem modos de ampulheta)
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(std::abs(evals(i)) <= 1e-9 * kMax, "Os 6 modos de corpo rígido devem ter autovalor nulo");
    }
    ASSERT_TRUE(evals(6) > 1e-10 * kMax, "Não pode existir modo espúrio (ampulheta / drilling solto)");

    // Teste explícito dos 6 movimentos rígidos em 3D (translações + rotações infinitesimais)
    Vec3 center = Vec3::Zero();
    for (const auto& c : coords) center += c;
    center /= static_cast<double>(coords.size());

    for (int mode = 0; mode < 6; ++mode) {
        VecX u = VecX::Zero(ndof);
        for (size_t i = 0; i < coords.size(); ++i) {
            if (mode < 3) {
                u(6 * i + mode) = 1.0;
            } else {
                Vec3 w = Vec3::Zero();
                w(mode - 3) = 1.0;
                const Vec3 disp = w.cross(coords[i] - center);
                u.segment<3>(6 * i + 0) = disp;
                u.segment<3>(6 * i + 3) = w;
            }
        }
        const double resRatio = (K * u).norm() / (kMax * u.norm());
        ASSERT_TRUE(resRatio < 1e-10, "K * u_rigido deve anular-se em 3D arbitrário");
    }
    std::cout << "[OK] RigidBody + NoHourglass (" << label << ", lambda7/lambdaMax="
              << (evals(6) / kMax) << ")\n";
}

void testRigidBodyAndSpectralRank() {
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "Concrete", 3.0e7, 0.25, 1e-5, 2500.0, 25.0);

    // Tri3 arbitrário inclinado no espaço 3D com espessura variável
    std::vector<Vec3> triCoords{{0.2, 0.1, 0.5}, {1.6, 0.4, 0.9}, {0.5, 1.5, 1.2}};
    ShellElement tri(1, ShellElementType::Tri3MITC3, {1, 2, 3}, triCoords, mat, 0.20, {0.18, 0.22, 0.20});
    checkSingleElementSpectral(tri, triCoords, "Tri3MITC3");

    // Quad4 distorcido e inclinado no espaço 3D
    std::vector<Vec3> quadCoords{{0.0, 0.0, 0.0}, {1.4, 0.2, 0.5}, {1.2, 1.5, 0.8}, {-0.1, 1.1, 0.3}};
    ShellElement quad4(2, ShellElementType::Quad4MITC4, {1, 2, 3, 4}, quadCoords, mat, 0.20, {0.15, 0.20, 0.25, 0.18});
    checkSingleElementSpectral(quad4, quadCoords, "Quad4MITC4");

    // Quad8 Serendipity inclinado em 3D
    std::vector<Vec3> q8(8);
    q8[0] = {0.0, 0.0, 0.0};
    q8[1] = {2.0, 0.0, 0.4};
    q8[2] = {2.2, 2.0, 0.6};
    q8[3] = {0.1, 1.8, 0.2};
    q8[4] = 0.5 * (q8[0] + q8[1]);
    q8[5] = 0.5 * (q8[1] + q8[2]);
    q8[6] = 0.5 * (q8[2] + q8[3]);
    q8[7] = 0.5 * (q8[3] + q8[0]);
    ShellElement quad8(3, ShellElementType::Quad8Serendipity, {1, 2, 3, 4, 5, 6, 7, 8}, q8, mat, 0.18);
    checkSingleElementSpectral(quad8, q8, "Quad8Serendipity");
}

void runPatchTestForQuad4Distorted() {
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "Steel", 2.1e8, 0.30, 1.2e-5, 7850.0, 78.5);
    const double t = 0.10;

    // Domínio [0, 2] x [0, 2] dividido em 4 quadriláteros fortemente distorcidos com 1 nó interno (id=9)
    std::vector<Vec3> pts(10); // 1-based
    pts[1] = {0.0, 0.0, 0.0};
    pts[2] = {1.0, 0.0, 0.0};
    pts[3] = {2.0, 0.0, 0.0};
    pts[4] = {0.0, 1.0, 0.0};
    pts[5] = {0.82, 1.18, 0.0}; // Nó interior fortemente deslocado
    pts[6] = {2.0, 1.0, 0.0};
    pts[7] = {0.0, 2.0, 0.0};
    pts[8] = {1.0, 2.0, 0.0};
    pts[9] = {2.0, 2.0, 0.0};

    const int conn[4][4] = {{1, 2, 5, 4}, {2, 3, 6, 5}, {4, 5, 8, 7}, {5, 6, 9, 8}};

    DofManager dofMgr;
    const std::array<bool, NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};
    for (int id = 1; id <= 9; ++id) dofMgr.registerNode(id, mask6);

    // Caso 1: Patch Test de Membrana (deformação constante eps_x = 1e-3, eps_y = -0.3e-3, gamma_xy = 0.5e-3)
    const double ex = 1.0e-3, ey = -0.3e-3, gxy = 0.5e-3;
    for (int id = 1; id <= 9; ++id) {
        if (id == 5) {
            dofMgr.setBoundaryCondition(id, 2, true, 0.0, 0.0);
            dofMgr.setBoundaryCondition(id, 3, true, 0.0, 0.0);
            dofMgr.setBoundaryCondition(id, 4, true, 0.0, 0.0);
            continue; // u, v, theta_z livres no nó interior 5
        }
        const double x = pts[id].x(), y = pts[id].y();
        const double u = ex * x + 0.5 * gxy * y;
        const double v = 0.5 * gxy * x + ey * y;
        dofMgr.setBoundaryCondition(id, 0, true, u, 0.0);
        dofMgr.setBoundaryCondition(id, 1, true, v, 0.0);
        dofMgr.setBoundaryCondition(id, 2, true, 0.0, 0.0);
        dofMgr.setBoundaryCondition(id, 3, true, 0.0, 0.0);
        dofMgr.setBoundaryCondition(id, 4, true, 0.0, 0.0);
        dofMgr.setBoundaryCondition(id, 5, true, 0.0, 0.0);
    }
    dofMgr.finalizeNumbering();

    std::vector<std::shared_ptr<IFiniteElement>> elems;
    for (int e = 0; e < 4; ++e) {
        std::vector<NodeId> nids{conn[e][0], conn[e][1], conn[e][2], conn[e][3]};
        std::vector<Vec3> nxyz{pts[conn[e][0]], pts[conn[e][1]], pts[conn[e][2]], pts[conn[e][3]]};
        elems.push_back(std::make_shared<ShellElement>(e + 1, ShellElementType::Quad4MITC4, nids, nxyz, mat, t));
    }
    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    SpMat K; VecX F;
    std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K, F, &err), err);
    const auto cls = dofMgr.classifyDofs(K, F, &err);
    ReducedLinearSystem red;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K, F, cls, red, &err), err);
    LinearSolverStats stats;
    const VecX Uf = LinearSolverSuite::solve(red.K_ff, red.rhs_f, LinearSolverOptions{}, stats);
    ASSERT_TRUE(stats.success, "Membrane patch test deve convergir");
    const VecX U = GlobalAssembler::reconstructFullDisplacements(red, Uf);

    const double u5Exact = ex * pts[5].x() + 0.5 * gxy * pts[5].y();
    const double v5Exact = 0.5 * gxy * pts[5].x() + ey * pts[5].y();
    ASSERT_NEAR(U(dofMgr.getNodeGlobalDof(5, 0)), u5Exact, 1e-11, "Patch test membrana u5 em malha distorcida QM6/MITC4");
    ASSERT_NEAR(U(dofMgr.getNodeGlobalDof(5, 1)), v5Exact, 1e-11, "Patch test membrana v5 em malha distorcida QM6/MITC4");

    // Caso 2: Patch Test de Flexão Pura (curvatura constante w = 0.5*k*(x^2 + 0.4*y^2))
    DofManager dofBend;
    for (int id = 1; id <= 9; ++id) dofBend.registerNode(id, mask6);
    const double kx = 1.0e-3, ky = 0.4e-3;
    for (int id = 1; id <= 9; ++id) {
        if (id == 5) {
            dofBend.setBoundaryCondition(id, 0, true, 0.0, 0.0);
            dofBend.setBoundaryCondition(id, 1, true, 0.0, 0.0);
            dofBend.setBoundaryCondition(id, 5, true, 0.0, 0.0);
            continue; // w, rx, ry livres no nó interior 5
        }
        const double x = pts[id].x(), y = pts[id].y();
        const double w = 0.5 * (kx * x * x + ky * y * y);
        const double rx = +ky * y;  // theta_x = +dw/dy (gamma_yz = dw/dy - theta_x = 0)
        const double ry = -kx * x;  // theta_y = -dw/dx (gamma_xz = dw/dx + theta_y = 0)
        dofBend.setBoundaryCondition(id, 0, true, 0.0, 0.0);
        dofBend.setBoundaryCondition(id, 1, true, 0.0, 0.0);
        dofBend.setBoundaryCondition(id, 2, true, w, 0.0);
        dofBend.setBoundaryCondition(id, 3, true, rx, 0.0);
        dofBend.setBoundaryCondition(id, 4, true, ry, 0.0);
        dofBend.setBoundaryCondition(id, 5, true, 0.0, 0.0);
    }
    dofBend.finalizeNumbering();

    SpMat Kb; VecX Fb;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofBend, elems, elLoads, nodLoads, Kb, Fb, &err), err);
    const auto clsb = dofBend.classifyDofs(Kb, Fb, &err);
    ReducedLinearSystem redb;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofBend, Kb, Fb, clsb, redb, &err), err);
    const VecX Ufb = LinearSolverSuite::solve(redb.K_ff, redb.rhs_f, LinearSolverOptions{}, stats);
    ASSERT_TRUE(stats.success, "Bending patch test deve convergir");
    const VecX Ub = GlobalAssembler::reconstructFullDisplacements(redb, Ufb);

    const double w5Exact = 0.5 * (kx * pts[5].x() * pts[5].x() + ky * pts[5].y() * pts[5].y());
    ASSERT_NEAR(Ub(dofBend.getNodeGlobalDof(5, 2)), w5Exact, 1e-11, "Patch test flexão w5 em malha distorcida MITC4");

    const Mat3 Dps = mat->constitutiveMatrixPlaneStress();
    (void)Dps;
    const double D = mat->youngsModulus() * t * t * t / (12.0 * (1.0 - mat->poissonRatio() * mat->poissonRatio()));
    // Para w(x,y) = +0.5*(kx*x^2 + ky*y^2), theta_y = -dw/dx = -kx*x => kappa_x = d(theta_y)/dx = -kx
    const double MxxExact = -D * (kx + mat->poissonRatio() * ky);
    VecX ue(24);
    for (int i = 0; i < 4; ++i) {
        for (int d = 0; d < 6; ++d) ue(6 * i + d) = Ub(dofBend.getNodeGlobalDof(conn[0][i], d));
    }
    const ShellStressResultant sr = static_cast<const ShellElement&>(*elems[0]).evaluateCenterStress(ue);
    ASSERT_NEAR(sr.Mxx, MxxExact, 1e-6 * std::abs(MxxExact), "Momento Mxx no Patch Test de flexão deve ser exato");
    ASSERT_NEAR(sr.Qxz, 0.0, 1e-8, "Cortante transversal Qxz deve anular-se em flexão pura");
    std::cout << "[OK] PatchTest Quad4MITC4/QM6 (Mxx=" << sr.Mxx << ", exact=" << MxxExact << ")\n";
}

void testShearLockingThinNavierPlate() {
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "PlateMat", 3.0e7, 0.30, 1e-5, 2500.0, 25.0);
    const double L = 10.0;
    const double t = 0.01; // L/t = 1000 (limite extremamente esbelto -> trava sem MITC!)
    const double q = -1.0; // -1 kN/m^2 em +Z
    const double D = mat->youngsModulus() * t * t * t / (12.0 * (1.0 - 0.3 * 0.3));
    const double wExact = navierSquarePlateCenterW(std::abs(q), L, D);

    auto solveSquarePlate = [&](ShellElementType stype, int nx) -> double {
        DofManager dofMgr;
        const std::array<bool, NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};

        auto nodeId = [&](int ix, int iy) { return static_cast<NodeId>(iy * (2 * nx + 1) + ix + 1); };
        std::vector<Vec3> nodeCoords(static_cast<size_t>((2 * nx + 1) * (2 * nx + 1) + 1));

        if (stype == ShellElementType::Quad8Serendipity) {
            for (int iy = 0; iy <= 2 * nx; ++iy) {
                for (int ix = 0; ix <= 2 * nx; ++ix) {
                    if ((ix % 2 == 1) && (iy % 2 == 1)) continue;
                    const NodeId id = nodeId(ix, iy);
                    dofMgr.registerNode(id, mask6);
                    nodeCoords[id] = {L * ix / (2 * nx), L * iy / (2 * nx), 0.0};
                    if (ix == 0 || ix == 2 * nx || iy == 0 || iy == 2 * nx) {
                        dofMgr.setBoundaryCondition(id, 2, true, 0.0, 0.0);
                    }
                }
            }
        } else {
            for (int iy = 0; iy <= nx; ++iy) {
                for (int ix = 0; ix <= nx; ++ix) {
                    const NodeId id = static_cast<NodeId>(iy * (nx + 1) + ix + 1);
                    dofMgr.registerNode(id, mask6);
                    nodeCoords[id] = {L * ix / nx, L * iy / nx, 0.0};
                    if (ix == 0 || ix == nx || iy == 0 || iy == nx) {
                        dofMgr.setBoundaryCondition(id, 2, true, 0.0, 0.0);
                    }
                }
            }
        }
        // Prende translações no plano e drilling num nó e em Y numa borda para suprimir corpo rígido de membrana
        dofMgr.setBoundaryCondition(1, 0, true, 0.0, 0.0);
        dofMgr.setBoundaryCondition(1, 1, true, 0.0, 0.0);
        const int sideIdx = (stype == ShellElementType::Quad8Serendipity) ? (2 * nx + 1) : (nx + 1);
        dofMgr.setBoundaryCondition(sideIdx, 1, true, 0.0, 0.0);
        dofMgr.finalizeNumbering();

        std::vector<std::shared_ptr<IFiniteElement>> elems;
        std::unordered_map<ElementId, ElementLoadInput> elLoads;
        ElementId eid = 1;

        if (stype == ShellElementType::Quad8Serendipity) {
            for (int ey = 0; ey < nx; ++ey) {
                for (int ex = 0; ex < nx; ++ex) {
                    const int ix = 2 * ex, iy = 2 * ey;
                    std::vector<NodeId> nids{
                        nodeId(ix, iy), nodeId(ix + 2, iy), nodeId(ix + 2, iy + 2), nodeId(ix, iy + 2),
                        nodeId(ix + 1, iy), nodeId(ix + 2, iy + 1), nodeId(ix + 1, iy + 2), nodeId(ix, iy + 1)
                    };
                    std::vector<Vec3> xyz;
                    for (NodeId nid : nids) xyz.push_back(nodeCoords[nid]);
                    elems.push_back(std::make_shared<ShellElement>(eid, stype, nids, xyz, mat, t));
                    ElementLoadInput ld;
                    ld.normalPressure = q;
                    elLoads[eid++] = ld;
                }
            }
        } else {
            auto nid1 = [&](int ix, int iy) { return static_cast<NodeId>(iy * (nx + 1) + ix + 1); };
            for (int ey = 0; ey < nx; ++ey) {
                for (int ex = 0; ex < nx; ++ex) {
                    const NodeId n00 = nid1(ex, ey);
                    const NodeId n10 = nid1(ex + 1, ey);
                    const NodeId n11 = nid1(ex + 1, ey + 1);
                    const NodeId n01 = nid1(ex, ey + 1);
                    ElementLoadInput ld;
                    ld.normalPressure = q;
                    if (stype == ShellElementType::Quad4MITC4) {
                        std::vector<NodeId> nids{n00, n10, n11, n01};
                        std::vector<Vec3> xyz{nodeCoords[n00], nodeCoords[n10], nodeCoords[n11], nodeCoords[n01]};
                        elems.push_back(std::make_shared<ShellElement>(eid, stype, nids, xyz, mat, t));
                        elLoads[eid++] = ld;
                    } else {
                        std::vector<NodeId> t1{n00, n10, n11}, t2{n00, n11, n01};
                        elems.push_back(std::make_shared<ShellElement>(eid, stype, t1,
                            std::vector<Vec3>{nodeCoords[n00], nodeCoords[n10], nodeCoords[n11]}, mat, t));
                        elLoads[eid++] = ld;
                        elems.push_back(std::make_shared<ShellElement>(eid, stype, t2,
                            std::vector<Vec3>{nodeCoords[n00], nodeCoords[n11], nodeCoords[n01]}, mat, t));
                        elLoads[eid++] = ld;
                    }
                }
            }
        }

        std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
        SpMat K; VecX F; std::string err;
        ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K, F, &err), err);
        const auto cls = dofMgr.classifyDofs(K, F, &err);
        ReducedLinearSystem red;
        ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K, F, cls, red, &err), err);
        LinearSolverStats stats;
        const VecX Uf = LinearSolverSuite::solve(red.K_ff, red.rhs_f, LinearSolverOptions{}, stats);
        ASSERT_TRUE(stats.success, "Navier thin plate deve resolver");
        const VecX U = GlobalAssembler::reconstructFullDisplacements(red, Uf);

        const NodeId centerNode = (stype == ShellElementType::Quad8Serendipity)
            ? nodeId(nx, nx)
            : static_cast<NodeId>((nx / 2) * (nx + 1) + (nx / 2) + 1);
        return std::abs(U(dofMgr.getNodeGlobalDof(centerNode, 2)));
    };

    const double wQ4 = solveSquarePlate(ShellElementType::Quad4MITC4, 10);
    const double wT3 = solveSquarePlate(ShellElementType::Tri3MITC3, 12);
    const double wQ8 = solveSquarePlate(ShellElementType::Quad8Serendipity, 6);

    std::cout << "[INFO] ThinPlateLockingFree L/t=1000 (Navier=" << wExact
              << ", Q4=" << wQ4 << ", T3=" << wT3 << ", Q8=" << wQ8 << ")\n";
    ASSERT_NEAR(wQ4, wExact, 0.025 * wExact, "Quad4MITC4 (L/t=1000) livre de shear locking (<2.5% erro)");
    ASSERT_NEAR(wT3, wExact, 0.035 * wExact, "Tri3DST (L/t=1000) livre de shear locking (<3.5% erro)");
    ASSERT_NEAR(wQ8, wExact, 0.020 * wExact, "Quad8Serendipity seletivo (L/t=1000) (<2% erro)");
}

void testScordelisLoCylindricalRoof() {
    // Benchmark clássico MacNeal-Harder: Scordelis-Lo Cylindrical Roof
    // R = 25, L = 50 (simetria: 0..25), phi0 = 40 deg, t = 0.25, E = 4.32e8, nu = 0.0, g_z = -90 (t*gamma = -90 => gamma = -360)
    const double R = 25.0;
    const double halfL = 25.0;
    const double phiMax = 40.0 * PI / 180.0;
    const double t = 0.25;
    // t = 0.25 -> força superficial desejada 90 kN/m² => gamma = 90 / 0.25 = 360 kN/m³, g_z = -1
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "RoofMat", 4.32e8, 0.0, 0.0, 1000.0, 360.0);

    const int nPhi = 12;
    const int nY = 12;
    auto nid = [&](int ip, int iy) { return static_cast<NodeId>(iy * (nPhi + 1) + ip + 1); };
    std::vector<Vec3> coords((nPhi + 1) * (nY + 1) + 1);

    DofManager dofMgr;
    const std::array<bool, NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};

    for (int iy = 0; iy <= nY; ++iy) {
        const double y = halfL * iy / nY;
        for (int ip = 0; ip <= nPhi; ++ip) {
            const double phi = phiMax * ip / nPhi;
            const NodeId id = nid(ip, iy);
            coords[id] = {R * std::sin(phi), y, R * std::cos(phi)};
            dofMgr.registerNode(id, mask6);

            // iy == 0: diafragma rígido no plano XZ -> u_x = 0, u_z = 0, r_y = 0
            if (iy == 0) {
                dofMgr.setBoundaryCondition(id, 0, true, 0.0, 0.0);
                dofMgr.setBoundaryCondition(id, 2, true, 0.0, 0.0);
                dofMgr.setBoundaryCondition(id, 4, true, 0.0, 0.0);
            }
            // iy == nY: plano médio de simetria (Y = L/2) -> u_y = 0, r_x = 0, r_z = 0
            if (iy == nY) {
                dofMgr.setBoundaryCondition(id, 1, true, 0.0, 0.0);
                dofMgr.setBoundaryCondition(id, 3, true, 0.0, 0.0);
                dofMgr.setBoundaryCondition(id, 5, true, 0.0, 0.0);
            }
            // ip == 0: plano de simetria na crista (X = 0) -> u_x = 0, r_y = 0, r_z = 0
            if (ip == 0) {
                dofMgr.setBoundaryCondition(id, 0, true, 0.0, 0.0);
                dofMgr.setBoundaryCondition(id, 4, true, 0.0, 0.0);
                dofMgr.setBoundaryCondition(id, 5, true, 0.0, 0.0);
            }
        }
    }
    dofMgr.finalizeNumbering();

    std::vector<std::shared_ptr<IFiniteElement>> elems;
    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    ElementId eid = 1;
    for (int iy = 0; iy < nY; ++iy) {
        for (int ip = 0; ip < nPhi; ++ip) {
            const NodeId n00 = nid(ip, iy);
            const NodeId n10 = nid(ip + 1, iy);
            const NodeId n11 = nid(ip + 1, iy + 1);
            const NodeId n01 = nid(ip, iy + 1);
            elems.push_back(std::make_shared<ShellElement>(
                eid, ShellElementType::Quad4MITC4,
                std::vector<NodeId>{n00, n10, n11, n01},
                std::vector<Vec3>{coords[n00], coords[n10], coords[n11], coords[n01]},
                mat, t));
            ElementLoadInput ld;
            ld.hasBodyGravity = true;
            ld.gravityAccel = {0.0, 0.0, -1.0};
            elLoads[eid++] = ld;
        }
    }

    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    SpMat K; VecX F; std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K, F, &err), err);
    const auto cls = dofMgr.classifyDofs(K, F, &err);
    ReducedLinearSystem red;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K, F, cls, red, &err), err);
    LinearSolverStats stats;
    const VecX Uf = LinearSolverSuite::solve(red.K_ff, red.rhs_f, LinearSolverOptions{}, stats);
    ASSERT_TRUE(stats.success, "Scordelis-Lo deve convergir");
    const VecX U = GlobalAssembler::reconstructFullDisplacements(red, Uf);

    const double wMidFree = std::abs(U(dofMgr.getNodeGlobalDof(nid(nPhi, nY), 2)));
    // Referência MacNeal-Harder casca rasa: 0.3024 (casca profunda ~0.3006-0.3086)
    ASSERT_TRUE(wMidFree > 0.285 && wMidFree < 0.320, "Scordelis-Lo Quad4MITC4 deve convergir próximo a 0.3024");
    std::cout << "[OK] ScordelisLoRoof Quad4MITC4 (12x12 quarter w_edge=" << wMidFree << ", ref=0.3024)\n";
}

void testPlateBucklingTimoshenkoAndConsistentMass() {
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "Steel", 2.0e8, 0.30, 0.0, 7850.0, 0.0);
    const double L = 2.0;
    const double t = 0.02;
    const double D = mat->youngsModulus() * t * t * t / (12.0 * (1.0 - 0.3 * 0.3));
    const double NcrExact = 4.0 * PI * PI * D / (L * L); // Compressão uniaxial placa quadrada simplesmente apoiada

    const int n = 8;
    auto nid = [&](int ix, int iy) { return static_cast<NodeId>(iy * (n + 1) + ix + 1); };
    std::vector<Vec3> coords((n + 1) * (n + 1) + 1);

    DofManager dofMgr;
    const std::array<bool, NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};
    for (int iy = 0; iy <= n; ++iy) {
        for (int ix = 0; ix <= n; ++ix) {
            const NodeId id = nid(ix, iy);
            coords[id] = {L * ix / n, L * iy / n, 0.0};
            dofMgr.registerNode(id, mask6);
            if (ix == 0 || ix == n || iy == 0 || iy == n) {
                dofMgr.setBoundaryCondition(id, 2, true, 0.0, 0.0);
            }
            if (ix == 0) dofMgr.setBoundaryCondition(id, 0, true, 0.0, 0.0);
            if (iy == 0 && ix == 0) dofMgr.setBoundaryCondition(id, 1, true, 0.0, 0.0);
        }
    }
    dofMgr.finalizeNumbering();

    std::vector<std::shared_ptr<IFiniteElement>> elems;
    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    ElementId eid = 1;
    for (int iy = 0; iy < n; ++iy) {
        for (int ix = 0; ix < n; ++ix) {
            const NodeId n00 = nid(ix, iy);
            const NodeId n10 = nid(ix + 1, iy);
            const NodeId n11 = nid(ix + 1, iy + 1);
            const NodeId n01 = nid(ix, iy + 1);
            elems.push_back(std::make_shared<ShellElement>(
                eid++, ShellElementType::Quad4MITC4,
                std::vector<NodeId>{n00, n10, n11, n01},
                std::vector<Vec3>{coords[n00], coords[n10], coords[n11], coords[n01]},
                mat, t));
        }
    }

    // Carga uniforme Nxx = -1 kN/m na borda x = L
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    const double dy = L / n;
    for (int iy = 0; iy <= n; ++iy) {
        const double w = (iy == 0 || iy == n) ? 0.5 : 1.0;
        Eigen::Matrix<double, NODAL_MAX_DOFS, 1> f = Eigen::Matrix<double, NODAL_MAX_DOFS, 1>::Zero();
        f(0) = -1.0 * dy * w;
        nodLoads[nid(n, iy)] = f;
    }

    SpMat K_orig, Kg_orig, M_orig;
    VecX F_orig;
    std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K_orig, F_orig, &err), err);
    const auto cls = dofMgr.classifyDofs(K_orig, F_orig, &err);
    ReducedLinearSystem red;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K_orig, F_orig, cls, red, &err), err);

    LinearSolverStats stats;
    const VecX Uf = LinearSolverSuite::solve(red.K_ff, red.rhs_f, LinearSolverOptions{}, stats);
    ASSERT_TRUE(stats.success, "Estado de pré-flambagem da placa deve convergir");
    const VecX Ufull = GlobalAssembler::reconstructFullDisplacements(red, Uf);

    ASSERT_TRUE(GlobalAssembler::assembleGeometricStiffness(dofMgr, elems, Ufull, Kg_orig, &err), err);
    std::vector<Triplet> pTrips;
    for (int j = 0; j < red.freeDofs; ++j) pTrips.emplace_back(red.freeReducedIndices[j], j, 1.0);
    SpMat P(red.fullDofs, red.freeDofs);
    P.setFromTriplets(pTrips.begin(), pTrips.end());
    P.makeCompressed();
    const SpMat Kg_ff = (P.transpose() * Kg_orig * P).eval();

    BucklingOptions bOpts;
    bOpts.numModes = 2;
    const BucklingResult bRes = BucklingSolver::solve(red.K_ff, Kg_ff, red, bOpts);
    ASSERT_TRUE(bRes.success && !bRes.loadFactors.empty(), "Flambagem de placa deve convergir");
    const double NcrCalc = bRes.loadFactors[0];
    ASSERT_NEAR(NcrCalc, NcrExact, 0.03 * NcrExact, "Flambagem crítica de placa Timoshenko deve bater em <3%");

    // Verificação explícita: massa consistente depende exclusivamente de rho=7850 (mesmo com gamma=0!)
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> noNodalMass;
    ASSERT_TRUE(GlobalAssembler::assembleMass(dofMgr, elems, true, noNodalMass, M_orig, &err), err);
    const double expectedTotalMassT = L * L * t * 7850.0 * 1e-3; // 0.628 t
    const VecX onesZ = [&]() {
        VecX v = VecX::Zero(dofMgr.totalDofs());
        for (int iy = 0; iy <= n; ++iy) {
            for (int ix = 0; ix <= n; ++ix) v(dofMgr.getNodeGlobalDof(nid(ix, iy), 2)) = 1.0;
        }
        return v;
    }();
    const double totalMassComputed = onesZ.dot(M_orig * onesZ);
    ASSERT_NEAR(totalMassComputed, expectedTotalMassT, 1e-10, "Massa consistente de casca deve usar rho (com gamma=0)");

    std::cout << "[OK] PlateBuckling & ShellMass (Ncr=" << NcrCalc << " vs Timoshenko=" << NcrExact
              << ", Mass=" << totalMassComputed << " t)\n";
}

void testUnifiedEndToEndShellPlateAndCoupledFrame() {
    // 1) Fluxo integrado via StructuralModel::surfaceMesh() + FemSolver3D::solve() em placa Navier 4x4m
    {
        omniframe::StructuralModel plateModel;
        plateModel.getMaterials().clear();
        const int matId = plateModel.addMaterial(omniframe::Material::makeConcreteC30(1));
        plateModel.surfaceMesh() = MeshGenerator::generateStructuredQuadGrid(
            Vec3(0.0, 0.0, 0.0), Vec3(4.0, 0.0, 0.0), Vec3(0.0, 4.0, 0.0), 4, 4, false, "PlateQuad");

        for (auto& mn : plateModel.surfaceMesh().nodes) {
            const bool onEdge = (std::abs(mn.position.x()) < 1e-6 || std::abs(mn.position.x() - 4.0) < 1e-6 ||
                                 std::abs(mn.position.y()) < 1e-6 || std::abs(mn.position.y() - 4.0) < 1e-6);
            if (onEdge) {
                mn.fixed = {true, true, true, false, false, false};
            }
        }
        for (auto& me : plateModel.surfaceMesh().elements) {
            me.materialId = matId;
            me.thickness = 0.20;
            me.normalPressure = -10.0; // 10 kPa em -Z
        }

        const omniframe::SolverResult res = omniframe::FemSolver3D::solve(plateModel);
        ASSERT_TRUE(res.success, "Solver unificado deve resolver placa Quad4 isolada");
        ASSERT_TRUE(res.totalSurfaceNodes == 25 && res.totalSurfaceElements == 16,
                    "Contadores de superficie 2D devem refletir malha 4x4");
        ASSERT_TRUE(res.maxNodalDisplacement > 0.0 && res.maxShellVonMises > 0.0,
                    "Deslocamento maximo e tensao von Mises 2D devem ser positivos");
        ASSERT_TRUE(res.continuumElementResults.size() == 16,
                    "Resultados por elemento continuo devem estar preenchidos");
        ASSERT_TRUE(res.equilibrium.isBalanced, "Placa 2D deve satisfazer equilibrio estatico global");

        double centerW = 0.0;
        for (const auto& mn : plateModel.surfaceMesh().nodes) {
            ASSERT_TRUE(mn.hasResults, "Cada MeshNode deve receber displacement/reaction");
            if (std::abs(mn.position.x() - 2.0) < 1e-6 && std::abs(mn.position.y() - 2.0) < 1e-6) {
                centerW = std::abs(mn.displacement[2]);
            }
        }
        const double E = 3.0e7, nu = 0.20, t = 0.20;
        const double D = E * std::pow(t, 3) / (12.0 * (1.0 - nu * nu));
        const double wNavier = navierSquarePlateCenterW(10.0, 4.0, D);
        ASSERT_TRUE(centerW > 0.80 * wNavier && centerW < 1.25 * wNavier,
                    "Flecha central E2E Quad4 deve acompanhar solucao analitica de Navier");
    }

    // 2) Acoplamento automatico 1D (4 pilares) + 2D (laje Quad4 no topo z=3m)
    {
        omniframe::StructuralModel coupledModel;
        coupledModel.getMaterials().clear();
        coupledModel.getCrossSections().clear();
        const int matId = coupledModel.addMaterial(omniframe::Material::makeConcreteC30(1));
        const int secId = coupledModel.addCrossSection(
            omniframe::CrossSection::makeRectangular(1, "Pilar 30x30", 0.30, 0.30));

        const std::array<std::pair<double, double>, 4> corners = {
            std::make_pair(0.0, 0.0), std::make_pair(4.0, 0.0),
            std::make_pair(4.0, 4.0), std::make_pair(0.0, 4.0)
        };
        std::vector<int> baseIds;
        std::vector<int> topIds;
        for (const auto& c : corners) {
            const int b = coupledModel.addNode(c.first, c.second, 0.0);
            const int t = coupledModel.addNode(c.first, c.second, 3.0);
            if (auto* nb = coupledModel.findNode(b)) {
                nb->support = omniframe::SupportRestraint::makeFixed();
            }
            coupledModel.addElement(b, t, matId, secId);
            baseIds.push_back(b);
            topIds.push_back(t);
        }

        coupledModel.surfaceMesh() = MeshGenerator::generateStructuredQuadGrid(
            Vec3(0.0, 0.0, 3.0), Vec3(4.0, 0.0, 0.0), Vec3(0.0, 4.0, 0.0), 4, 4, false, "RoofSlab");
        for (auto& me : coupledModel.surfaceMesh().elements) {
            me.materialId = matId;
            me.thickness = 0.20;
            me.normalPressure = -12.0; // -12 kPa * 16 m2 = -192 kN total
        }

        const omniframe::SolverResult res = omniframe::FemSolver3D::solve(coupledModel);
        ASSERT_TRUE(res.success, "Modelo acoplado 1D+2D deve convergir");
        ASSERT_TRUE(res.totalNodes == 8 && res.totalElements == 4, "Pilares 1D devem permanecer contabilizados");
        ASSERT_TRUE(res.totalSurfaceNodes == 25 && res.totalSurfaceElements == 16,
                    "Laje 2D acoplada deve estar contabilizada");
        ASSERT_TRUE(res.equilibrium.isBalanced, "Acoplamento 1D+2D deve fechar equilibrio estatico global");
        ASSERT_TRUE(res.maxShellVonMises > 0.0, "Tensão von Mises da laje acoplada deve ser positiva");

        double sumBaseRz = 0.0;
        for (int b : baseIds) {
            const auto* nb = coupledModel.findNode(b);
            ASSERT_TRUE(nb != nullptr, "No da base deve existir");
            sumBaseRz += nb->reaction[2];
        }
        ASSERT_NEAR(sumBaseRz, 192.0, 0.1,
                    "Somatorio das reacoes verticais na base dos pilares deve equilibrar integralmente a pressao da laje");

        const auto* top0 = coupledModel.findNode(topIds[0]);
        ASSERT_TRUE(top0 != nullptr && top0->displacement[2] < 0.0,
                    "Topo do pilar deve sofrer recalque axial compativel com a carga da laje");
        for (const auto& mn : coupledModel.surfaceMesh().nodes) {
            if ((mn.position - Vec3(0.0, 0.0, 3.0)).norm() < 1e-6) {
                ASSERT_NEAR(mn.displacement[2], top0->displacement[2], 1e-10,
                            "No coincidente da casca 2D deve compartilhar DOF exato com o topo do pilar 1D");
            }
        }
    }

    std::cout << "[OK] UnifiedE2E ShellPlate & CoupledFrame1D+2D\n";
}

} // namespace

int main() {
    testRigidBodyAndSpectralRank();
    runPatchTestForQuad4Distorted();
    testShearLockingThinNavierPlate();
    testScordelisLoCylindricalRoof();
    testPlateBucklingTimoshenkoAndConsistentMass();
    testUnifiedEndToEndShellPlateAndCoupledFrame();
    std::cout << "\nTodos os testes da Fase 5 (Cascas TRI3-MITC3, QUAD4-MITC4/QM6, QUAD8 + Fluxo E2E) passaram!\n";
    return 0;
}
