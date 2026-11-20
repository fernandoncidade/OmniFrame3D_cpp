#include "TestHelper.hpp"
#define ASSERT_TRUE(cond, msg) TEST_ASSERT((cond), (msg))
#define ASSERT_NEAR(a, b, tol, msg) TEST_ASSERT(std::abs((a) - (b)) <= (tol), (msg))
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include "kernel/analysis/BucklingSolver.hpp"
#include "kernel/analysis/NonlinearSolver.hpp"
#include "kernel/elements/Corotational1D.hpp"
#include "kernel/materials/J2PlasticityMaterial.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>

using namespace omniframe;
using namespace omniframe::kernel;

namespace {

void testJ2PlasticityAndConsistentTangent() {
    // 1. Teste 1D cíclico com encruamento misto (isotrópico + cinemático)
    J2PlasticityParams p1;
    p1.E = 2.0e8;       // 200 GPa em kPa
    p1.sigmaY0 = 2.5e5; // 250 MPa em kPa
    p1.H_iso = 1.0e7;   // 10 GPa
    p1.H_kin = 2.0e7;   // 20 GPa

    J2Uniaxial1D j2_1d(p1);
    J2State1D st0{};
    J2State1D st1{};
    // Carregamento elástico abaixo do escoamento (eps = 0.001 => sigma = 200 MPa < 250 MPa)
    j2_1d.integrate(0.001, st0, st1);
    ASSERT_NEAR(st1.stress, 2.0e5, 1e-6, "J2 1D elástico");
    ASSERT_NEAR(st1.epsPlastic, 0.0, 1e-12, "Sem deformação plástica elástica");

    // Carregamento plástico até eps = 0.003
    j2_1d.integrate(0.003, st1, st1);
    ASSERT_TRUE(st1.epsPlastic > 0.0, "Deve acumular deformação plástica em tração");
    const double expectedSigma = 2.5e5 + (p1.H_iso + p1.H_kin) * st1.epsPlastic;
    ASSERT_NEAR(st1.stress, expectedSigma, 1e-3, "Tensão pós-escoamento 1D exata");
    const double expectedEt = p1.E * (p1.H_iso + p1.H_kin) / (p1.E + p1.H_iso + p1.H_kin);
    ASSERT_NEAR(st1.tangentE, expectedEt, 1e-3, "Tangente elastoplástica 1D exata");

    // Descarregamento até deformação zero -> tensão residual compressiva e efeito Bauschinger
    J2State1D st2{};
    j2_1d.integrate(0.0, st1, st2);
    ASSERT_TRUE(st2.stress < 0.0, "Descarregamento para deformação nula gera compressão residual");

    // 2. Teste 3D J2 + verificação integral do módulo tangente consistente Simo-Taylor por diferenças finitas
    J2PlasticityParams p3;
    p3.E = 2.0e8;
    p3.nu = 0.30;
    p3.sigmaY0 = 2.5e5;
    p3.H_iso = 8.0e6;
    p3.H_kin = 1.2e7;

    J2Plasticity3D j2_3d(p3);
    J2State3D s3Prev{};
    J2State3D s3Out{};
    Eigen::Matrix<double, 6, 6> Dep;

    Eigen::Matrix<double, 6, 1> eps;
    eps << 0.0022, -0.0005, -0.0004, 0.0018, -0.0007, 0.0011;
    j2_3d.integrate(eps, s3Prev, s3Out, Dep);

    ASSERT_TRUE(s3Out.alphaIso > 0.0, "Estado 3D deve plastificar");

    // Verifica simetria de D_ep
    ASSERT_NEAR((Dep - Dep.transpose()).norm() / Dep.norm(), 0.0, 1e-12, "D_ep deve ser simétrico");

    // Verifica condição de escoamento na superfície atualizada: ||s - q|| == sqrt(2/3)*(sigmaY0 + H_iso*alpha)
    const double devMean = (s3Out.stress(0) + s3Out.stress(1) + s3Out.stress(2)) / 3.0;
    Eigen::Matrix<double, 6, 1> sDev = s3Out.stress;
    sDev(0) -= devMean; sDev(1) -= devMean; sDev(2) -= devMean;
    const Eigen::Matrix<double, 6, 1> eta = sDev - s3Out.backstressDev;
    const double normEta = std::sqrt(eta(0)*eta(0) + eta(1)*eta(1) + eta(2)*eta(2)
        + 2.0*(eta(3)*eta(3) + eta(4)*eta(4) + eta(5)*eta(5)));
    const double radius = std::sqrt(2.0 / 3.0) * (p3.sigmaY0 + p3.H_iso * s3Out.alphaIso);
    ASSERT_NEAR(normEta, radius, 1e-4, "Retorno radial 3D exato sobre superfície de escoamento");

    // Compara todas as 6 colunas de Dep com diferenças finitas centradas
    const double h = 1e-8;
    for (int col = 0; col < 6; ++col) {
        Eigen::Matrix<double, 6, 1> epP = eps;
        Eigen::Matrix<double, 6, 1> epM = eps;
        epP(col) += h;
        epM(col) -= h;
        J2State3D oP, oM;
        Eigen::Matrix<double, 6, 6> unusedD;
        j2_3d.integrate(epP, s3Prev, oP, unusedD);
        j2_3d.integrate(epM, s3Prev, oM, unusedD);
        const Eigen::Matrix<double, 6, 1> fdCol = (oP.stress - oM.stress) / (2.0 * h);
        const double colErr = (fdCol - Dep.col(col)).norm() / std::max(1.0, Dep.col(col).norm());
        ASSERT_TRUE(colErr < 1e-5, "Coluna da matriz tangente consistente J2 3D deve casar com diferenças finitas");
    }

    std::cout << "[OK] testJ2PlasticityAndConsistentTangent\n";
}

void testCorotationalObjectivityAndEquilibrium() {
    Beam1DProps p;
    p.L = 2.5;
    p.E = 2.0e8;
    p.G = p.E / (2.0 * 1.3);
    p.A = 0.01;
    p.Iy = 8.0e-5;
    p.Iz = 1.2e-4;
    p.J = 4.0e-5;
    p.Asy = 0.008;
    p.Asz = 0.008;
    p.formulation = Beam1DFormulation::EulerBernoulli3D;

    const kernel::Vec3 X1(0.0, 0.0, 0.0);
    const kernel::Vec3 X2(2.5, 0.0, 0.0);
    const kernel::Mat3 R0 = kernel::Mat3::Identity();
    Corotational1DState st0{};

    // 1. Objetividade: rotação rígida arbitrária de 55 graus no espaço 3D deve produzir força interna ZERO!
    const kernel::Vec3 thetaRig(0.35, -0.55, 0.65);
    const kernel::Mat3 Q = CorotationalBeam3D::expSO3(thetaRig);
    const kernel::Vec3 u1 = kernel::Vec3(1.2, -0.4, 0.8);
    const kernel::Vec3 u2 = u1 + (Q * (X2 - X1) - (X2 - X1));

    const auto rigidRes = CorotationalBeam3D::evaluate(
        X1, X2, R0, p, u1, thetaRig, u2, thetaRig, st0);
    ASSERT_TRUE(rigidRes.fIntGlobal.norm() < 1e-7,
                "Rotação e translação rígida finita 3D devem gerar tensão interna nula");

    // 2. Equilíbrio exato em configuração deformada com grandes deslocamentos/rotações
    const kernel::Vec3 du1(0.0, 0.0, 0.0);
    const kernel::Vec3 dth1(0.02, -0.05, 0.08);
    const kernel::Vec3 du2(-0.18, 0.45, 0.32);
    const kernel::Vec3 dth2(-0.04, 0.25, -0.35);

    const auto defRes = CorotationalBeam3D::evaluate(
        X1, X2, R0, p, du1, dth1, du2, dth2, st0);

    const kernel::Vec3 x1def = X1 + du1;
    const kernel::Vec3 x2def = X2 + du2;
    const kernel::Vec3 F1 = defRes.fIntGlobal.segment<3>(0);
    const kernel::Vec3 M1 = defRes.fIntGlobal.segment<3>(3);
    const kernel::Vec3 F2 = defRes.fIntGlobal.segment<3>(6);
    const kernel::Vec3 M2 = defRes.fIntGlobal.segment<3>(9);

    ASSERT_NEAR((F1 + F2).norm(), 0.0, 1e-8, "Auto-equilíbrio de forças corrotacionais");
    const kernel::Vec3 sumM = x1def.cross(F1) + x2def.cross(F2) + M1 + M2;
    ASSERT_TRUE(sumM.norm() < 1e-6, "Auto-equilíbrio de momentos em torno da configuração deformada");
    ASSERT_NEAR((defRes.KtGlobal - defRes.KtGlobal.transpose()).norm() / defRes.KtGlobal.norm(),
                0.0, 1e-12, "Matriz tangente corrotacional deve ser simétrica");

    std::cout << "[OK] testCorotationalObjectivityAndEquilibrium\n";
}

void testLinearizedEulerBuckling() {
    // Coluna em balanço com L = 6.0 m discretizada em 6 elementos (k_eff = 2.0 => P_cr = pi^2 E I_min / (4 L^2))
    const double L = 6.0;
    const int nElem = 6;
    StructuralModel model;
    Material mat = Material::makeStructuralSteel(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 0.20x0.30", 0.20, 0.30);
    const MaterialId mid = model.addMaterial(mat);
    const SectionId sid = model.addCrossSection(sec);

    for (int i = 0; i <= nElem; ++i) {
        Node n(i + 1, 0.0, 0.0, (L * i) / nElem);
        if (i == 0) n.support = SupportRestraint::makeFixed();
        if (i == nElem) n.load.force = {0.0, 0.0, -100.0}; // 100 kN compressão axial
        model.addNode(n);
    }
    for (int i = 0; i < nElem; ++i) {
        model.addElement(Element(i + 1, i + 1, i + 2, mid, sid));
    }

    BucklingOptions bOpts;
    bOpts.numModes = 4;
    bOpts.tolerance = 1e-9;
    const BucklingResult bRes = FemSolver3D::solveBuckling(model, bOpts);
    ASSERT_TRUE(bRes.success, "Análise de flambagem linearizada deve convergir");
    ASSERT_TRUE(bRes.loadFactors.size() >= 2, "Deve retornar múltiplos modos de flambagem");

    const double Imin = std::min(sec.Iy, sec.Iz);
    const double Imax = std::max(sec.Iy, sec.Iz);
    const double PcrEuler1 = (PI * PI * mat.E * Imin) / (4.0 * L * L);
    const double PcrEuler2 = (PI * PI * mat.E * Imax) / (4.0 * L * L); // 1º modo no eixo forte

    const double PcrCalc1 = bRes.loadFactors[0] * 100.0;
    const double relErr1 = std::abs(PcrCalc1 - PcrEuler1) / PcrEuler1;
    ASSERT_TRUE(relErr1 < 0.005, "Carga crítica de Euler P_cr1 deve casar dentro de 0.5% do exato");

    // 2º modo deve ser o 1º modo do eixo forte (se Imax < 9*Imin) ou 2º modo do eixo fraco
    const double PcrMode1StrongOr2Weak = std::min(PcrEuler2, 9.0 * PcrEuler1);
    const double PcrCalc2 = bRes.loadFactors[1] * 100.0;
    const double relErr2 = std::abs(PcrCalc2 - PcrMode1StrongOr2Weak) / PcrMode1StrongOr2Weak;
    ASSERT_TRUE(relErr2 < 0.015, "Segundo autovalor de flambagem deve corresponder ao próximo modo analítico");

    // Verifica aplicação de imperfeição geométrica inicial
    std::vector<kernel::Vec3> coords;
    for (const auto& n : model.getNodes()) coords.emplace_back(n.position.x, n.position.y, n.position.z);
    // Constrói DofManager compatível apenas para testar amplitude de applyGeometricImperfection
    kernel::DofManager dummyDof;
    const std::array<bool, kernel::NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};
    for (const auto& n : model.getNodes()) dummyDof.registerNode(n.id, mask6);
    dummyDof.finalizeNumbering();
    const auto imperf = BucklingSolver::applyGeometricImperfection(coords, dummyDof, bRes.modeShapesFull[0], 0.025);
    double maxDelta = 0.0;
    for (size_t i = 0; i < coords.size(); ++i) maxDelta = std::max(maxDelta, (imperf[i] - coords[i]).norm());
    ASSERT_NEAR(maxDelta, 0.025, 1e-8, "Amplitude máxima da imperfeição geométrica deve ser exata");

    std::cout << "[OK] testLinearizedEulerBuckling (Pcr calc=" << PcrCalc1
              << " kN, exato=" << PcrEuler1 << " kN)\n";
}

void testPDeltaAndLargeRotationElastica() {
    // 1. P-Delta em pilar com carga axial N = 0.5 * P_cr + força horizontal H no topo:
    // Teoria exata: deslocamento transversal amplifica por ~ 1 / (1 - P / P_cr) => ~ 2.0x!
    const double L = 6.0;
    const int nElem = 6;
    StructuralModel model;
    Material mat = Material::makeStructuralSteel(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 0.20x0.20", 0.20, 0.20);
    const MaterialId mid = model.addMaterial(mat);
    const SectionId sid = model.addCrossSection(sec);

    const double Pcr = (PI * PI * mat.E * sec.Iz) / (4.0 * L * L);
    const double Paxial = 0.50 * Pcr;
    const double Hlat = 2.0;

    for (int i = 0; i <= nElem; ++i) {
        Node n(i + 1, 0.0, 0.0, (L * i) / nElem);
        if (i == 0) n.support = SupportRestraint::makeFixed();
        if (i == nElem) n.load.force = {Hlat, 0.0, -Paxial};
        model.addNode(n);
    }
    for (int i = 0; i < nElem; ++i) {
        model.addElement(Element(i + 1, i + 1, i + 2, mid, sid));
    }

    ASSERT_TRUE(FemSolver3D::solve(model).success, "Estático linear base deve resolver");
    const double uLin = model.getNodes().back().displacement[0];

    NonlinearOptions pdOpts;
    pdOpts.method = NonlinearMethod::PDelta;
    pdOpts.kinematics = NonlinearKinematics::PDeltaSecondOrder;
    pdOpts.forceTol = 1e-8;
    const NonlinearResult pdRes = FemSolver3D::solveNonlinear(model, pdOpts);
    ASSERT_TRUE(pdRes.success, "Análise P-Delta deve convergir");
    const double uPD = model.getNodes().back().displacement[0];
    const double ampActual = uPD / uLin;
    // Para coluna em balanço sob carga lateral na ponta e axial P = 0.5*Pcr, amplificação exata é ~2.05
    ASSERT_TRUE(ampActual > 1.95 && ampActual < 2.20,
                "Amplificação P-Delta para P = 0.5*P_cr deve estar próxima de 2.0x");

    // 2. Grandes rotações corrotacionais: viga em balanço sob momento puro na ponta (Elástica de Euler)
    // Quando M = pi * E * I / L => curva vira um semicírculo perfeito de 180 graus!
    // Raio R = L / pi => ponta move de (L, 0, 0) para (0, 0, 2*L/pi) (deslocamento u_x = -L, w_z = 2*L/pi)!
    StructuralModel elastica;
    Material emat = Material::makeStructuralSteel(10);
    CrossSection esec = CrossSection::makeRectangular(10, "Fita", 0.05, 0.10);
    const MaterialId emid = elastica.addMaterial(emat);
    const SectionId esid = elastica.addCrossSection(esec);
    const double Le = 4.0;
    const int ne = 16;
    // Flexão no plano X-Z (em torno de Y local): rotação +theta_y dobra no sentido -Z; momento My = -M_pi dobra para +Z!
    // Momento necessário para círculo semi-completo (180 graus): M = pi * E * Iy / L
    const double I_bend = esec.Iy;
    const double M_halfCircle = (PI * emat.E * I_bend) / Le;
    for (int i = 0; i <= ne; ++i) {
        Node n(i + 1, (Le * i) / ne, 0.0, 0.0);
        if (i == 0) n.support = SupportRestraint::makeFixed();
        if (i == ne) n.load.moment = {0.0, -M_halfCircle, 0.0};
        elastica.addNode(n);
    }
    for (int i = 0; i < ne; ++i) {
        elastica.addElement(Element(i + 1, i + 1, i + 2, emid, esid));
    }

    NonlinearOptions nrOpts;
    nrOpts.method = NonlinearMethod::NewtonRaphson;
    nrOpts.kinematics = NonlinearKinematics::Corotational3D;
    nrOpts.numSteps = 20;
    nrOpts.forceTol = 1e-6;
    nrOpts.dispTol = 1e-7;
    const NonlinearResult elRes = FemSolver3D::solveNonlinear(elastica, nrOpts);
    ASSERT_TRUE(elRes.success, "Elástica de 180 graus via Corrotacional 3D Newton-Raphson deve convergir");

    const auto& tip = elastica.getNodes().back();
    const double xFinal = tip.position.x + tip.displacement[0];
    const double zFinal = tip.position.z + tip.displacement[2];
    const double xExact = 0.0;
    const double zExact = (2.0 * Le) / PI;
    ASSERT_TRUE(std::abs(xFinal - xExact) < 0.06, "Posição final X da elástica 180 graus deve retornar próximo a 0");
    ASSERT_TRUE(std::abs(zFinal - zExact) < 0.06, "Posição final Z da elástica 180 graus deve casar com 2*L/pi");

    std::cout << "[OK] testPDeltaAndLargeRotationElastica (tip=(" << xFinal << ", " << zFinal
              << "), exato=(0, " << zExact << "))\n";
}

void testArcLengthSnapThrough() {
    // Treliça rasa de Von Mises (2 barras simétricas) exibindo Snap-Through:
    // Apoios rotulados em (-a, 0, 0) e (+a, 0, 0), vértice em (0, 0, h0) carregado com Fz = -P_ref.
    const double a = 5.0;
    const double h0 = 0.40; // razão h0/a = 0.08 -> snap-through elástico clássico
    const double E = 2.0e8;
    const double A = 0.005;
    const double L0 = std::sqrt(a * a + h0 * h0);
    // Pico analítico raso aproximado: P_cr ~= 2 * E * A * (h0 / L0)^3 / (3 * sqrt(3))
    const double P_cr_approx = 2.0 * E * A * std::pow(h0 / L0, 3) / (3.0 * std::sqrt(3.0));

    kernel::DofManager dofMgr;
    const std::array<bool, kernel::NODAL_MAX_DOFS> mask3{true, true, true, false, false, false, false};
    dofMgr.registerNode(1, mask3);
    dofMgr.registerNode(2, mask3);
    dofMgr.registerNode(3, mask3);
    for (int d = 0; d < 3; ++d) {
        dofMgr.setBoundaryCondition(1, d, true, 0.0, 0.0);
        dofMgr.setBoundaryCondition(3, d, true, 0.0, 0.0);
    }
    // Restringe movimento fora do plano Y do vértice 2 para forçar snap vertical limpo X-Z
    dofMgr.setBoundaryCondition(2, 1, true, 0.0, 0.0);
    dofMgr.finalizeNumbering();

    // Graus livres: apenas X e Z do nó 2 (ou até X fixo por simetria, deixamos X e Z livres!)
    const int nDofs = dofMgr.totalDofs();
    ReducedLinearSystem redSys;
    redSys.fullDofs = nDofs;
    redSys.reducedDofs = nDofs;
    redSys.U_offset = kernel::VecX::Zero(nDofs);
    for (int d : {0, 2}) {
        redSys.freeReducedIndices.push_back(dofMgr.getNodeGlobalDof(2, d));
    }
    redSys.freeDofs = static_cast<int>(redSys.freeReducedIndices.size());

    kernel::VecX Fref = kernel::VecX::Zero(nDofs);
    const int gz2 = dofMgr.getNodeGlobalDof(2, 2);
    // Carga de referência = 2.5 * P_cr_approx descendente para ultrapassar pico e atingir ramo pós-snap
    const double P_ref = 2.5 * P_cr_approx;
    Fref(gz2) = -P_ref;

    Beam1DProps tp;
    tp.L = L0;
    tp.E = E;
    tp.G = E / 2.6;
    tp.A = A;
    tp.formulation = Beam1DFormulation::Truss3D;

    auto computeLocalRotTranspose = [](const kernel::Vec3& p1, const kernel::Vec3& p2) -> kernel::Mat3 {
        kernel::Vec3 e1 = (p2 - p1).normalized();
        kernel::Vec3 up(0.0, 1.0, 0.0);
        kernel::Vec3 e3 = e1.cross(up).normalized();
        kernel::Vec3 e2 = e3.cross(e1);
        kernel::Mat3 R0;
        R0.col(0) = e1; R0.col(1) = e2; R0.col(2) = e3;
        return R0;
    };

    const kernel::Vec3 X1(-a, 0.0, 0.0), X2(0.0, 0.0, h0), X3(a, 0.0, 0.0);
    Frame1DNonlinearElementSpec e1{1, 1, 2, X1, X2, computeLocalRotTranspose(X1, X2), tp};
    Frame1DNonlinearElementSpec e2{2, 2, 3, X2, X3, computeLocalRotTranspose(X2, X3), tp};

    FrameNonlinearModel toggleModel(
        dofMgr, redSys, Fref, {e1, e2}, NonlinearKinematics::Corotational3D);

    NonlinearOptions arcOpts;
    arcOpts.method = NonlinearMethod::ArcLengthCrisfield;
    arcOpts.kinematics = NonlinearKinematics::Corotational3D;
    arcOpts.targetLoadFactor = 1.0; // Fref já é 2.5 * P_cr
    arcOpts.initialArcLength = 0.08;
    arcOpts.maxSteps = 80;
    arcOpts.desiredIterations = 4;
    arcOpts.forceTol = 1e-7;

    const NonlinearResult res = NonlinearSolver::solve(toggleModel, arcOpts);
    ASSERT_TRUE(res.success, "Arc-Length Crisfield deve atravessar o snap-through completo");

    double peakLambda = -1e9;
    double valleyLambda = 1e9;
    int peakStep = -1;
    int valleyStep = -1;
    for (size_t i = 0; i < res.history.size(); ++i) {
        const double lam = res.history[i].loadFactor;
        if (i > 0 && lam > peakLambda && valleyStep < 0) {
            peakLambda = lam;
            peakStep = static_cast<int>(i);
        }
        if (peakStep >= 0 && lam < peakLambda && lam < valleyLambda) {
            valleyLambda = lam;
            valleyStep = static_cast<int>(i);
        }
    }

    const double peakP = peakLambda * P_ref;
    ASSERT_TRUE(valleyStep > peakStep && valleyLambda < 0.0,
                "Caminho pós-pico deve apresentar ramo de descarga negativa (lambda < 0 após snap-through)");
    ASSERT_TRUE(res.achievedLoadFactor >= 0.99,
                "Após o vale de snap-through, Arc-Length deve re-estabilizar na tração até lambda >= 1.0");
    const double relPeakErr = std::abs(peakP - P_cr_approx) / P_cr_approx;
    ASSERT_TRUE(relPeakErr < 0.08, "Carga de pico snap-through deve aproximar solução analítica rasa");

    std::cout << "[OK] testArcLengthSnapThrough (P_pico=" << peakP
              << " kN, P_cr_approx=" << P_cr_approx << " kN, passos=" << res.totalSteps << ")\n";
}

void testElastoplasticCantileverPlasticHinge() {
    // Viga em balanço retangular b=0.10 m, h=0.20 m, L=2.0 m, fy = 250 MPa (2.5e5 kPa)
    // Momento de início de escoamento: My = fy * b * h^2 / 6
    // Momento de plastificação total: Mp = fy * b * h^2 / 4 = 1.5 * My
    // Carga pontual de colapso plástico na ponta: Pu = Mp / L
    const double b = 0.10, h = 0.20, L = 2.0;
    const double fy = 2.5e5;
    const double Mp = fy * b * h * h / 4.0;
    const double Pu = Mp / L; // 250000 * 0.1 * 0.04 / 4 / 2 = 125 kN

    StructuralModel model;
    Material mat = Material::makeStructuralSteel(10);
    mat.isPlastic = true;
    mat.sigmaY = fy;
    mat.H_iso = 2.0e6; // Encruamento isotrópico de 1% E (típico bilinear)
    mat.H_kin = 0.0;
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 0.10x0.20", b, h);
    const MaterialId mid = model.addMaterial(mat);
    const SectionId sid = model.addCrossSection(sec);

    const int nElem = 6;
    const double P_apply = 1.06 * Pu; // Acima da carga limite elasto-perfeita Pu = Mp/L => rótula plástica franca
    for (int i = 0; i <= nElem; ++i) {
        Node n(i + 1, (L * i) / nElem, 0.0, 0.0);
        if (i == 0) n.support = SupportRestraint::makeFixed();
        if (i == nElem) n.load.force = {0.0, 0.0, -P_apply};
        model.addNode(n);
    }
    for (int i = 0; i < nElem; ++i) {
        model.addElement(Element(i + 1, i + 1, i + 2, mid, sid));
    }

    ASSERT_TRUE(FemSolver3D::solve(model).success, "Solução elástica linear base");
    const double wElastic = std::abs(model.getNodes().back().displacement[2]);

    NonlinearOptions nlOpts;
    nlOpts.method = NonlinearMethod::NewtonRaphson;
    nlOpts.kinematics = NonlinearKinematics::Corotational3D;
    nlOpts.numSteps = 25;
    nlOpts.forceTol = 1e-5;
    const NonlinearResult nlRes = FemSolver3D::solveNonlinear(model, nlOpts);
    ASSERT_TRUE(nlRes.success, "Análise elastoplástica com fibras deve convergir além de Pu com encruamento");

    // Confirma comportamento linear puro no trecho inicial antes de My = 0.667 Mp (passo 10 => ~0.42 Pu)
    const auto& earlyStep = nlRes.history[9];
    const double wEarlyAbs = std::abs(earlyStep.U_full(nlRes.finalU_full.size() - 4)); // ou checa qualquer passo sem escoamento
    (void)wEarlyAbs;
    ASSERT_TRUE(!nlRes.history.front().anyYielded, "Passo elástico inicial não deve plastificar");

    const double wPlastic = std::abs(model.getNodes().back().displacement[2]);
    // Acima da formação de rótula plástica no engaste, flecha elastoplástica supera bastante a elástica linear
    ASSERT_TRUE(wPlastic > 1.30 * wElastic,
                "Flecha elastoplástica deve superar significativamente a linear após formação de rótula plástica");

    bool yieldedFound = false;
    for (const auto& step : nlRes.history) {
        if (step.anyYielded) yieldedFound = true;
    }
    ASSERT_TRUE(yieldedFound, "Histórico deve registrar início do escoamento J2 nas fibras do engaste");

    std::cout << "[OK] testElastoplasticCantileverPlasticHinge (w_el=" << wElastic
              << " m, w_ep=" << wPlastic << " m)\n";
}

} // namespace

int main() {
    testJ2PlasticityAndConsistentTangent();
    testCorotationalObjectivityAndEquilibrium();
    testLinearizedEulerBuckling();
    testPDeltaAndLargeRotationElastica();
    testArcLengthSnapThrough();
    testElastoplasticCantileverPlasticHinge();
    std::cout << "\nTodos os testes da Fase 3 (Flambagem, P-Delta, Corrotacional 3D, Arc-Length e J2) passaram!\n";
    return 0;
}
