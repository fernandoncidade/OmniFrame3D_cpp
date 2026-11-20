#include "TestHelper.hpp"
#define ASSERT_TRUE(cond, msg) TEST_ASSERT((cond), (msg))
#define ASSERT_NEAR(a, b, tol, msg) TEST_ASSERT(std::abs((a) - (b)) <= (tol), (msg))
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include "kernel/analysis/DynamicSolver.hpp"
#include "kernel/assembly/GlobalAssembler.hpp"
#include "kernel/elements/BeamTruss1D.hpp"
#include <cmath>
#include <iostream>
#include <memory>

using namespace omniframe;
using namespace omniframe::kernel;

namespace {

class SimpleBarElem final : public IFiniteElement {
public:
    SimpleBarElem(ElementId eid, NodeId n1, NodeId n2, const Beam1DProps& p,
                  const omniframe::Vec3& p1, const omniframe::Vec3& p2)
        : m_id(eid), m_nodes{n1, n2}, m_props(p), m_p1(p1), m_p2(p2)
    {
        Element e(eid, n1, n2, 1, 1);
        StructuralModel tmp;
        tmp.addNode(Node(n1, p1));
        tmp.addNode(Node(n2, p2));
        m_T = e.computeTransformationMatrix(tmp);
        m_props.L = e.computeLength(tmp);
    }

    ElementId id() const override { return m_id; }
    ElementFamily family() const override { return ElementFamily::Frame1D; }
    std::string typeName() const override { return "EulerBernoulli3D"; }
    MaterialId materialId() const override { return 1; }
    const std::vector<NodeId>& nodeIds() const override { return m_nodes; }
    std::array<bool, NODAL_MAX_DOFS> requiredNodeDofMask() const override {
        return {true, true, true, true, true, true, false};
    }
    int localDofCount() const override { return 12; }

    MatX computeGlobalStiffness() const override {
        const auto kLoc = BeamTruss1D::computeUncondensedLocalStiffness(m_props);
        return m_T.transpose() * kLoc * m_T;
    }
    MatX computeGlobalMass(bool consistent) const override {
        const auto mLoc = consistent
            ? BeamTruss1D::computeConsistentLocalMass(m_props)
            : BeamTruss1D::computeLumpedLocalMass(m_props);
        return m_T.transpose() * mLoc * m_T;
    }
    MatX computeGlobalGeometricStiffness(const VecX&) const override {
        return MatX::Zero(12, 12);
    }
    VecX computeGlobalLoadVector(const ElementLoadInput&) const override {
        return VecX::Zero(12);
    }

private:
    ElementId m_id;
    std::vector<NodeId> m_nodes;
    Beam1DProps m_props;
    omniframe::Vec3 m_p1, m_p2;
    Eigen::Matrix<double, 12, 12> m_T;
};

void testSdoFFreeVibrationAndHarmonicFrf() {
    // Sistema SDOF exato: barra sem massa (EA/L = 4000 kN/m) + massa concentrada m = 10.0 t em X no nó 2
    // => omega_n = sqrt(4000 / 10) = 20.0 rad/s, f_n = 20 / (2*pi) Hz
    DofManager dofMgr;
    const std::array<bool, NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};
    dofMgr.registerNode(1, mask6);
    dofMgr.registerNode(2, mask6);
    for (int d = 0; d < 6; ++d) dofMgr.setBoundaryCondition(1, d, true, 0.0, 0.0);
    // Só DOF X do nó 2 livre
    for (int d = 1; d < 6; ++d) dofMgr.setBoundaryCondition(2, d, true, 0.0, 0.0);
    dofMgr.finalizeNumbering();

    Beam1DProps bp;
    bp.L = 2.0;
    bp.E = 2.0e5; // kN/m^2
    bp.G = bp.E / 2.6;
    bp.A = 0.04;  // EA/L = 2e5 * 0.04 / 2 = 4000 kN/m
    bp.Iy = 1e-4;
    bp.Iz = 1e-4;
    bp.J = 1e-4;
    bp.rhoConsistent = 0.0; // massa da barra = 0; massa concentrada nodal m = 10 t
    bp.gammaKNM3 = 0.0;

    std::vector<std::shared_ptr<IFiniteElement>> elems{
        std::make_shared<SimpleBarElem>(1, 1, 2, bp, omniframe::Vec3{0, 0, 0}, omniframe::Vec3{2, 0, 0})
    };
    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodMass;
    Eigen::Matrix<double, NODAL_MAX_DOFS, 1> m2 = Eigen::Matrix<double, NODAL_MAX_DOFS, 1>::Zero();
    m2(0) = 10.0; // 10 t = 10000 kg em X
    nodMass[2] = m2;

    SpMat K_orig, M_orig;
    VecX F_orig;
    std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K_orig, F_orig, &err), err);
    ASSERT_TRUE(GlobalAssembler::assembleMass(dofMgr, elems, true, nodMass, M_orig, &err), err);

    const auto cls = dofMgr.classifyDofs(K_orig, F_orig, &err);
    ReducedLinearSystem redSys;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K_orig, F_orig, cls, redSys, &err), err);

    SpMat M_ff;
    std::array<VecX, 3> infl;
    ASSERT_TRUE(DynamicSolver::reduceMassAndInfluence(dofMgr, M_orig, redSys, M_ff, infl, &err), err);

    ModalOptions mOpts;
    mOpts.numModes = 2;
    const ModalResult mod = DynamicSolver::solveModal(redSys.K_ff, M_ff, infl, redSys, mOpts);
    ASSERT_TRUE(mod.success && !mod.modes.empty(), "Modal SDOF deve convergir");
    ASSERT_NEAR(mod.modes[0].omega, 20.0, 1e-8, "Frequência circular SDOF exata = 20.0 rad/s");
    ASSERT_NEAR(mod.modes[0].effectiveMassRatio[0], 1.0, 1e-10, "Participação de massa SDOF = 100%");

    // 1. Vibração livre não amortecida (Newmark beta=0.25, gamma=0.5): conservação de energia
    TransientOptions trOpts;
    trOpts.scheme = TransientScheme::NewmarkBeta;
    trOpts.dt = 0.001;
    trOpts.numSteps = 314; // ~ 1 ciclo completo (T = 2*pi/20 = 0.31416 s)
    trOpts.initialDisp_f = VecX::Constant(1, 0.05);
    trOpts.initialVel_f = VecX::Zero(1);
    const TransientResult tr = DynamicSolver::solveTransientForceHistory(
        redSys.K_ff, M_ff, SpMat(1, 1), redSys,
        [](double, int) { return VecX::Zero(1); }, trOpts);
    ASSERT_TRUE(tr.success, "Newmark vibração livre deve suceder");
    const double E0 = tr.history.front().kineticEnergy + tr.history.front().strainEnergy;
    const double Eend = tr.history.back().kineticEnergy + tr.history.back().strainEnergy;
    ASSERT_TRUE(std::abs(Eend - E0) / E0 < 1e-4, "Newmark trapezoidal deve conservar energia mecânica total");
    // Após T=0.314 s, deslocamento volta para ~ +0.05 m
    const int gx2 = dofMgr.getNodeGlobalDof(2, 0);
    ASSERT_NEAR(tr.history.back().U_full(gx2), 0.05, 5e-4, "Deslocamento após 1 período deve retornar a u0");

    // 2. Resposta Harmônica em ressonância exata: zeta = 0.02 => fator de amplificação = 1 / (2*zeta) = 25.0
    const double zeta = 0.02;
    const double omegaN = 20.0;
    const double fnHz = omegaN / (2.0 * PI);
    const auto ray = DynamicSolver::computeRayleighCoefficients(omegaN, zeta, omegaN, zeta);
    const SpMat C_ff = DynamicSolver::assembleRayleighDamping(M_ff, redSys.K_ff, ray);

    // Verifica razão de amortecimento exata em duas frequências distintas também
    const auto ray2 = DynamicSolver::computeRayleighCoefficients(10.0, 0.02, 40.0, 0.05);
    const double z1chk = 0.5 * (ray2.alphaM / 10.0 + ray2.betaK * 10.0);
    const double z2chk = 0.5 * (ray2.alphaM / 40.0 + ray2.betaK * 40.0);
    ASSERT_NEAR(z1chk, 0.02, 1e-10, "Rayleigh reproduz zeta1 exato em omega1");
    ASSERT_NEAR(z2chk, 0.05, 1e-10, "Rayleigh reproduz zeta2 exato em omega2");

    HarmonicOptions hOpts;
    hOpts.frequenciesHz = {0.01, fnHz, 2.0 * fnHz};
    const double Famp = 40.0; // F/k = 40 / 4000 = 0.01 m estático
    const HarmonicResult hRes = DynamicSolver::solveHarmonic(
        redSys.K_ff, M_ff, C_ff, VecX::Constant(1, Famp), redSys, hOpts);
    ASSERT_TRUE(hRes.success && hRes.sweep.size() == 3, "Harmônico FRF deve resolver todas as frequências");
    const double uStatic = Famp / 4000.0;
    const double uResonance = hRes.sweep[1].amplitudeFull(gx2);
    const double phaseRes = hRes.sweep[1].phaseRadFull(gx2);
    ASSERT_NEAR(uResonance, uStatic / (2.0 * zeta), 1e-4, "Amplitude na ressonância exata = u_est / (2*zeta)");
    ASSERT_NEAR(phaseRes, -0.5 * PI, 1e-4, "Fase na ressonância exata = -90 graus (-pi/2)");

    std::cout << "[OK] testSdoFFreeVibrationAndHarmonicFrf\n";
}

void testContinuousCantileverModesConsistent() {
    // Viga em balanço contínua 12 elementos: f_n = (beta_n L)^2 / (2*pi*L^2) * sqrt(EI / (rho*A))
    // Verifica separação explícita entre massa física rho=7850 kg/m^3 e peso estático gamma=0
    const double L = 8.0;
    const int nElem = 12;
    const double E = 2.0e8;
    const double rhoKgM3 = 7850.0;
    const double rhoCons = rhoKgM3 * 1e-3; // 7.85 t/m^3

    CrossSection sec = CrossSection::makeRectangular(1, "Ret 0.20x0.40", 0.20, 0.40);
    Beam1DProps bp;
    bp.E = E;
    bp.G = E / 2.6;
    bp.A = sec.A;
    bp.Iy = sec.Iy;
    bp.Iz = sec.Iz;
    bp.J = sec.J;
    bp.rhoConsistent = rhoCons;
    bp.gammaKNM3 = 0.0; // Zera gamma intencionalmente para provar independência da dinâmica!

    DofManager dofMgr;
    const std::array<bool, NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};
    for (int i = 0; i <= nElem; ++i) {
        dofMgr.registerNode(i + 1, mask6);
        if (i == 0) {
            for (int d = 0; d < 6; ++d) dofMgr.setBoundaryCondition(1, d, true, 0.0, 0.0);
        }
    }
    dofMgr.finalizeNumbering();

    std::vector<std::shared_ptr<IFiniteElement>> elems;
    for (int i = 0; i < nElem; ++i) {
        omniframe::Vec3 p1{(L * i) / nElem, 0.0, 0.0};
        omniframe::Vec3 p2{(L * (i + 1)) / nElem, 0.0, 0.0};
        elems.push_back(std::make_shared<SimpleBarElem>(i + 1, i + 1, i + 2, bp, p1, p2));
    }

    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodMass;

    SpMat K_orig, M_orig;
    VecX F_orig;
    std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K_orig, F_orig, &err), err);
    ASSERT_TRUE(GlobalAssembler::assembleMass(dofMgr, elems, true, nodMass, M_orig, &err), err);

    const auto cls = dofMgr.classifyDofs(K_orig, F_orig, &err);
    ReducedLinearSystem redSys;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K_orig, F_orig, cls, redSys, &err), err);

    SpMat M_ff;
    std::array<VecX, 3> infl;
    ASSERT_TRUE(DynamicSolver::reduceMassAndInfluence(dofMgr, M_orig, redSys, M_ff, infl, &err), err);

    ModalOptions mOpts;
    mOpts.numModes = 6;
    mOpts.tolerance = 1e-9;
    const ModalResult mod = DynamicSolver::solveModal(redSys.K_ff, M_ff, infl, redSys, mOpts);
    ASSERT_TRUE(mod.success && mod.modes.size() == 6, "Modal viga contínua deve extrair 6 modos");

    const double mu = rhoCons * sec.A; // t/m
    const double Imin = std::min(sec.Iy, sec.Iz);
    const double b1L = 1.87510407;
    const double f1_exact = (b1L * b1L) / (2.0 * PI * L * L) * std::sqrt((E * Imin) / mu);
    const double relErr1 = std::abs(mod.modes[0].frequencyHz - f1_exact) / f1_exact;
    ASSERT_TRUE(relErr1 < 0.003, "1ª frequência de flexão consistente deve casar dentro de 0.3% da solução contínua");

    // Participação acumulada nos 6 modos iniciais deve ser substancial nas direções transversais Y e Z
    ASSERT_TRUE(mod.cumulativeMassRatio[1] > 0.80 && mod.cumulativeMassRatio[2] > 0.80,
                "Participação acumulada de massa em flexão nos primeiros modos > 80%");

    std::cout << "[OK] testContinuousCantileverModesConsistent (f1 calc="
              << mod.modes[0].frequencyHz << " Hz, exato=" << f1_exact << " Hz)\n";
}

void testSeismicAndResponseSpectrumCqcVsSrss() {
    // Sistema quase simétrico em 2 direções ortogonais X e Y (frequências próximas f_x ~ 2.00 Hz, f_y ~ 2.04 Hz)
    // Demonstra que CQC captura correlação cruzada Der Kiureghian (rho ~ 0.96) sob excitação diagonal 45°
    DofManager dofMgr;
    const std::array<bool, NODAL_MAX_DOFS> mask6{true, true, true, true, true, true, false};
    dofMgr.registerNode(1, mask6);
    dofMgr.registerNode(2, mask6);
    for (int d = 0; d < 6; ++d) dofMgr.setBoundaryCondition(1, d, true, 0.0, 0.0);
    // Nó 2: somente translações X e Y livres (massa = 10 t)
    for (int d : {2, 3, 4, 5}) dofMgr.setBoundaryCondition(2, d, true, 0.0, 0.0);
    dofMgr.finalizeNumbering();

    // Duas barras ortogonais ou seção levemente assimétrica em coluna vertical Z
    Beam1DProps bp;
    bp.L = 4.0;
    bp.E = 3.0e7;
    bp.G = bp.E / 2.4;
    bp.A = 0.16;
    bp.Iy = 0.00200; // rigidez em X
    bp.Iz = 0.00208; // rigidez 4% maior em Y -> f_y ~= 1.02 * f_x!
    bp.J = 0.002;
    bp.rhoConsistent = 0.0; // massa concentrada explícita

    std::vector<std::shared_ptr<IFiniteElement>> elems{
        std::make_shared<SimpleBarElem>(1, 1, 2, bp, omniframe::Vec3{0, 0, 0}, omniframe::Vec3{0, 0, 4.0})
    };
    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodMass;
    Eigen::Matrix<double, NODAL_MAX_DOFS, 1> m2 = Eigen::Matrix<double, NODAL_MAX_DOFS, 1>::Zero();
    m2(0) = 10.0; m2(1) = 10.0;
    nodMass[2] = m2;

    SpMat K_orig, M_orig;
    VecX F_orig;
    std::string err;
    ASSERT_TRUE(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, elems, elLoads, nodLoads, K_orig, F_orig, &err), err);
    ASSERT_TRUE(GlobalAssembler::assembleMass(dofMgr, elems, true, nodMass, M_orig, &err), err);

    const auto cls = dofMgr.classifyDofs(K_orig, F_orig, &err);
    ReducedLinearSystem redSys;
    ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K_orig, F_orig, cls, redSys, &err), err);

    SpMat M_ff;
    std::array<VecX, 3> infl;
    ASSERT_TRUE(DynamicSolver::reduceMassAndInfluence(dofMgr, M_orig, redSys, M_ff, infl, &err), err);

    ModalOptions mOpts;
    mOpts.numModes = 2;
    const ModalResult mod = DynamicSolver::solveModal(redSys.K_ff, M_ff, infl, redSys, mOpts);
    ASSERT_TRUE(mod.success && mod.modes.size() == 2, "Modos próximos devem convergir");

    // Coeficiente de Der Kiureghian entre dois modos próximos com amortecimento 5% deve ser alto
    const double rho12 = DynamicSolver::derKiureghianCorrelation(
        mod.modes[0].omega, 0.05, mod.modes[1].omega, 0.05);
    ASSERT_TRUE(rho12 > 0.90, "Modos muito próximos (razão ~1.02) têm forte correlação CQC de Der Kiureghian");

    // Espectro NBR 15421 (ag_s = 0.15 g, Ca = 1.2, Cv = 1.6)
    const auto curve = DynamicSolver::buildNbr15421SpectrumCurve(0.15 * 9.80665, 1.2, 1.6);
    ResponseSpectrumOptions rsCqc;
    rsCqc.combination = ResponseSpectrumCombination::CQC;
    rsCqc.modalDampingZeta = 0.05;
    rsCqc.directionScale = {0.70710678, 0.70710678, 0.0};
    rsCqc.spectrumCurve = curve;
    const auto resCqc = DynamicSolver::solveResponseSpectrum(K_orig, mod, rsCqc);
    ASSERT_TRUE(resCqc.success, "CQC espectral deve executar");

    ResponseSpectrumOptions rsSrss = rsCqc;
    rsSrss.combination = ResponseSpectrumCombination::SRSS;
    const auto resSrss = DynamicSolver::solveResponseSpectrum(K_orig, mod, rsSrss);
    ASSERT_TRUE(resSrss.success, "SRSS espectral deve executar");

    const int gx2 = dofMgr.getNodeGlobalDof(2, 0);
    ASSERT_TRUE(resCqc.combinedDisplacementFull(gx2) > 0.0, "Deslocamento sísmico combinado deve ser positivo");

    // Acelerograma sísmico transiente
    TransientOptions sOpts;
    sOpts.scheme = TransientScheme::HHTAlpha;
    sOpts.hhtAlpha = -0.05;
    sOpts.dt = 0.005;
    sOpts.numSteps = 100;
    const double rayA = 0.05 * 2.0 * mod.modes[0].omega;
    const auto seisRes = DynamicSolver::solveSeismicTimeHistory(
        redSys.K_ff, M_ff, (rayA * M_ff).eval(), infl, redSys,
        [](double t, int) -> omniframe::kernel::Vec3 { return {1.5 * std::sin(15.0 * t), 0.0, 0.0}; },
        sOpts);
    ASSERT_TRUE(seisRes.success && seisRes.peakDisplacementFull(gx2) > 1e-4,
                "Integração sísmica HHT-Alpha na base deve excitar resposta em X");

    std::cout << "[OK] testSeismicAndResponseSpectrumCqcVsSrss (rho12=" << rho12
              << ", CQC uX=" << resCqc.combinedDisplacementFull(gx2)
              << ", SRSS uX=" << resSrss.combinedDisplacementFull(gx2) << ")\n";
}

void testFemSolver3DRayleighNotDoubled() {
    // Modelo SDOF 100% controlado via FemSolver3D: barra axial alongada em X (no 1 engastado, no 2 apenas uX livre)
    StructuralModel model;
    model.getMaterials().clear();
    model.getCrossSections().clear();

    Material steel = Material::makeStructuralSteel(1);
    const MaterialId mid = model.addMaterial(steel);
    CrossSection sec = CrossSection::makeRectangular(1, "Ret 20x20", 0.20, 0.20);
    const SectionId sid = model.addCrossSection(sec);

    const NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    const NodeId n2 = model.addNode(5.0, 0.0, 0.0);
    model.findNode(n1)->support = SupportRestraint::makeFixed();
    auto* p2 = model.findNode(n2);
    p2->support = SupportRestraint::makeFixed();
    p2->support.fixed[0] = false; // Apenas translação axial X livre (SDOF exato)
    p2->load.force = {100.0, 0.0, 0.0};
    model.addElement(n1, n2, mid, sid, 0.0);

    const SolverResult staticRes = FemSolver3D::solve(model);
    ASSERT_TRUE(staticRes.success, "SDOF estático deve convergir");
    const double uStat = std::abs(model.findNode(n2)->displacement[0]);
    ASSERT_TRUE(uStat > 0.0, "Deslocamento estático SDOF positivo");

    ModalOptions mOpts;
    mOpts.numModes = 1;
    const ModalResult mod = FemSolver3D::solveModal(model, mOpts);
    ASSERT_TRUE(mod.success && mod.modes.size() == 1, "Modal SDOF deve convergir com 1 modo");
    const double omega1 = mod.modes[0].omega;
    constexpr double kPi = 3.14159265358979323846;
    const double f1Hz = omega1 / (2.0 * kPi);
    const double T1Sec = 1.0 / f1Hz;

    constexpr double zeta = 0.02;
    // Usa alphaM e betaK simultaneamente para verificar que nenhum dos dois termos Rayleigh é duplicado
    const double alphaM = zeta * omega1;
    const double betaK = zeta / omega1;

    // (a) Ressonância harmônica via FemSolver3D::solveHarmonic: amplitude u_res = u_est / (2 * zeta)
    HarmonicOptions hOpts;
    hOpts.frequenciesHz = {f1Hz};
    hOpts.structuralLossEta = 0.0;
    hOpts.rayleighAlphaM = alphaM;
    hOpts.rayleighBetaK = betaK;
    const HarmonicResult hRes = FemSolver3D::solveHarmonic(model, hOpts);
    ASSERT_TRUE(hRes.success && hRes.sweep.size() == 1, "Harmônico SDOF via FemSolver3D deve resolver");
    //DOF livre 0 do nó 2: em um SDOF com nó 1 todo fixo e nó 2 apenas uX livre, maxCoeff = uX(n2)
    const double uRes = hRes.sweep[0].amplitudeFull.cwiseAbs().maxCoeff();
    const double uExpected = uStat / (2.0 * zeta);
    ASSERT_TRUE(std::abs(uRes - uExpected) / uExpected < 1e-4,
                "Amplitude de ressonância harmônica deve igualar u_est/(2*zeta) sem duplicação 2C ([AUD-002])");

    // (b) Decremento logarítmico em vibração livre amortecida via FemSolver3D::solveTransient
    StructuralModel freeModel = model;
    freeModel.findNode(n2)->load.force = {0.0, 0.0, 0.0};
    TransientOptions tOpts;
    tOpts.scheme = TransientScheme::NewmarkBeta;
    tOpts.newmarkBeta = 0.25;
    tOpts.newmarkGamma = 0.50;
    tOpts.rayleighAlphaM = alphaM;
    tOpts.rayleighBetaK = betaK;
    constexpr int stepsPerCycle = 200;
    tOpts.dt = T1Sec / static_cast<double>(stepsPerCycle);
    tOpts.numSteps = stepsPerCycle * 3;
    tOpts.initialDisp_f = kernel::VecX::Constant(1, 0.010);
    tOpts.initialVel_f = kernel::VecX::Zero(1);

    const TransientResult tRes = FemSolver3D::solveTransient(
        freeModel, [](double, int) { return 0.0; }, 0, tOpts);
    ASSERT_TRUE(tRes.success && tRes.history.size() > static_cast<std::size_t>(stepsPerCycle * 2),
                "Transiente livre SDOF deve concluir");

    auto extractPeaksAfter = [](const TransientResult& tr, int dofIdx, double tMin) {
        std::vector<double> peaks;
        for (std::size_t i = 1; i + 1 < tr.history.size(); ++i) {
            if (tr.history[i].time < tMin) continue;
            const double prev = tr.history[i - 1].U_full(dofIdx);
            const double curr = tr.history[i].U_full(dofIdx);
            const double next = tr.history[i + 1].U_full(dofIdx);
            if (curr > 0.0 && curr >= prev && curr > next) {
                peaks.push_back(curr);
            }
        }
        return peaks;
    };

    // Identifica índice global do DOF uX do nó 2 pelo valor inicial não-nulo em step 0
    int dofX2 = -1;
    for (int i = 0; i < tRes.history[0].U_full.size(); ++i) {
        if (std::abs(tRes.history[0].U_full(i)) > 1e-6) {
            dofX2 = i;
            break;
        }
    }
    ASSERT_TRUE(dofX2 >= 0, "Índice de DOF axial do SDOF deve ser encontrado");

    const auto peaksT = extractPeaksAfter(tRes, dofX2, 0.5 * T1Sec);
    ASSERT_TRUE(peaksT.size() >= 2, "Transiente livre deve capturar pelo menos 2 picos positivos");
    const double deltaT = std::log(peaksT[0] / peaksT[1]);
    const double zetaMeasuredT = deltaT / std::sqrt(4.0 * kPi * kPi + deltaT * deltaT);
    ASSERT_NEAR(zetaMeasuredT, zeta, 5e-4,
                "Decremento logarítmico no FemSolver3D::solveTransient deve medir zeta = 0.02 ([AUD-002])");

    // (c) Decremento logarítmico em vibração livre pós-pulso sísmico via FemSolver3D::solveSeismic
    TransientOptions sOpts = tOpts;
    sOpts.initialDisp_f.resize(0);
    sOpts.initialVel_f.resize(0);
    sOpts.numSteps = stepsPerCycle * 4;
    const double pulseDur = 0.25 * T1Sec;
    const TransientResult sRes = FemSolver3D::solveSeismic(
        freeModel,
        [pulseDur](double t, int) -> omniframe::kernel::Vec3 {
            return (t <= pulseDur) ? omniframe::kernel::Vec3(5.0, 0.0, 0.0) : omniframe::kernel::Vec3(0.0, 0.0, 0.0);
        },
        sOpts);
    ASSERT_TRUE(sRes.success, "Sísmico pós-pulso via FemSolver3D::solveSeismic deve concluir");
    const auto peaksS = extractPeaksAfter(sRes, dofX2, 0.4 * T1Sec);
    ASSERT_TRUE(peaksS.size() >= 2, "Sísmico pós-pulso deve capturar pelo menos 2 picos positivos em vibração livre");
    const double deltaS = std::log(peaksS[0] / peaksS[1]);
    const double zetaMeasuredS = deltaS / std::sqrt(4.0 * kPi * kPi + deltaS * deltaS);
    ASSERT_NEAR(zetaMeasuredS, zeta, 5e-4,
                "Decremento logarítmico pós-pulso no FemSolver3D::solveSeismic deve medir zeta = 0.02 ([AUD-002])");

    std::cout << "[OK] testFemSolver3DRayleighNotDoubled (uRes=" << uRes << ", expected=" << uExpected
              << ", zetaT=" << zetaMeasuredT << ", zetaS=" << zetaMeasuredS << ")\n";
}

void testNbr15421SpectrumAndSignInvariance() {
    // 1. Validar ordenadas exatas e inserção explícita dos pontos T=0, TB e TC da NBR 15421
    const double ag_s = 0.15 * 9.80665;
    const double Ca = 1.2;
    const double Cv = 1.6;
    const double a_gs0 = Ca * ag_s;
    const double a_gs1 = Cv * ag_s;
    const double TB = 0.08 * (Cv / Ca);
    const double TC = 0.40 * (Cv / Ca);

    const auto curve = DynamicSolver::buildNbr15421SpectrumCurve(ag_s, Ca, Cv, 4.0, 100);
    ASSERT_TRUE(!curve.empty() && std::abs(curve.front().periodSec - 0.0) < 1e-12,
                "Curva NBR 15421 deve iniciar em T = 0");
    ASSERT_NEAR(curve.front().Sa, a_gs0, 1e-9, "Sa(0) deve ser igual a Ca * ag_s");

    bool foundTB = false;
    bool foundTC = false;
    for (std::size_t i = 0; i < curve.size(); ++i) {
        if (i > 0) {
            ASSERT_TRUE(curve[i].periodSec > curve[i - 1].periodSec,
                        "Tabela espectral NBR 15421 deve ser estritamente crescente");
        }
        if (std::abs(curve[i].periodSec - TB) < 1e-10) {
            foundTB = true;
            ASSERT_NEAR(curve[i].Sa, 2.5 * a_gs0, 1e-9, "Sa(TB) deve igualar 2.5 * Ca * ag_s");
        }
        if (std::abs(curve[i].periodSec - TC) < 1e-10) {
            foundTC = true;
            ASSERT_NEAR(curve[i].Sa, 2.5 * a_gs0, 1e-9, "Sa(TC) deve igualar 2.5 * Ca * ag_s");
            ASSERT_NEAR(curve[i].Sa, a_gs1 / TC, 1e-9, "Continuidade exata a_gs1/TC == 2.5*a_gs0");
        }
        if (curve[i].periodSec > TC + 1e-9) {
            ASSERT_NEAR(curve[i].Sa, a_gs1 / curve[i].periodSec, 1e-9,
                        "Trecho T > TC deve obedecer Sa(T) = Cv * ag_s / T");
        }
    }
    ASSERT_TRUE(foundTB && foundTC, "Pontos exatos TB e TC devem estar inseridos na tabela ordenada");

    // Verificar correção de amortecimento eta quando zeta != 5%
    const auto curveZ2 = DynamicSolver::buildNbr15421SpectrumCurve(ag_s, Ca, Cv, 4.0, 100, 0.02);
    ASSERT_NEAR(curveZ2.front().Sa, a_gs0, 1e-9, "Sa(0) permanece a_gs0 independente de zeta");
    ASSERT_TRUE(DynamicSolver::interpolateSpectrum(curveZ2, TC) > 2.5 * a_gs0 * 1.15,
                "Amortecimento de 2% deve ampliar patamar espectral em relacao a 5%");

    // 2. Verificar combinação direcional independente e invariância a inversão de sinal de autovetores
    ModalResult modal;
    modal.success = true;
    modal.modes.resize(2);
    modal.modes[0].modeIndex = 0;
    modal.modes[0].omega = 12.0;
    modal.modes[0].frequencyHz = 12.0 / (2.0 * 3.14159265358979323846);
    modal.modes[0].periodSec = 1.0 / modal.modes[0].frequencyHz;
    modal.modes[0].participationFactors = {1.5, -1.5, 0.2};
    modal.modes[0].phi_full = (VecX(4) << 0.6, -0.4, 0.5, 0.3).finished();

    modal.modes[1].modeIndex = 1;
    modal.modes[1].omega = 13.5;
    modal.modes[1].frequencyHz = 13.5 / (2.0 * 3.14159265358979323846);
    modal.modes[1].periodSec = 1.0 / modal.modes[1].frequencyHz;
    modal.modes[1].participationFactors = {-0.8, 0.9, 0.4};
    modal.modes[1].phi_full = (VecX(4) << -0.3, 0.7, 0.2, 0.5).finished();

    SpMat K_id(4, 4);
    K_id.setIdentity();
    ResponseSpectrumOptions opts;
    opts.spectrumCurve = curve;
    opts.directionScale = {1.0, 1.0, 0.30};

    for (auto rule : {ResponseSpectrumCombination::ABSSUM,
                      ResponseSpectrumCombination::SRSS,
                      ResponseSpectrumCombination::CQC}) {
        opts.combination = rule;
        const auto baseRes = DynamicSolver::solveResponseSpectrum(K_id, modal, opts);
        ASSERT_TRUE(baseRes.success && baseRes.combinedDisplacementFull.norm() > 0.0,
                    "Resposta espectral multi-direcional nao pode se anular por soma algebrica");

        // Inverter sinal de cada autovetor individualmente e simultaneamente
        for (int mask = 1; mask <= 3; ++mask) {
            ModalResult flipped = modal;
            if (mask & 1) {
                flipped.modes[0].phi_full = -flipped.modes[0].phi_full;
                for (double& g : flipped.modes[0].participationFactors) g = -g;
            }
            if (mask & 2) {
                flipped.modes[1].phi_full = -flipped.modes[1].phi_full;
                for (double& g : flipped.modes[1].participationFactors) g = -g;
            }
            const auto flipRes = DynamicSolver::solveResponseSpectrum(K_id, flipped, opts);
            ASSERT_TRUE(flipRes.success, "Espectro com autovetor invertido deve convergir");
            ASSERT_TRUE((flipRes.combinedDisplacementFull - baseRes.combinedDisplacementFull).norm() < 1e-12,
                        "Deslocamento combinado deve ser invariante a inversao phi_k -> -phi_k");
            ASSERT_TRUE((flipRes.combinedReactionFull - baseRes.combinedReactionFull).norm() < 1e-12,
                        "Reacao combinada deve ser invariante a inversao phi_k -> -phi_k");
        }
    }

    std::cout << "[OK] testNbr15421SpectrumAndSignInvariance\n";
}

void testAud011HarmonicMpcPhasorAndRotationalInfluence() {
    // 1. Vetor de influencia sismica nao deve excitar DOFs rotacionais diretamente
    {
        DofManager dofMgr;
        dofMgr.registerNode(1);
        dofMgr.registerNode(2);
        for (int d = 0; d < 6; ++d) {
            dofMgr.setBoundaryCondition(1, d, true);
        }
        dofMgr.finalizeNumbering();

        SpMat K(12, 12), M(12, 12);
        K.setIdentity();
        M.setIdentity();
        VecX F = VecX::Zero(12);
        const DofClassification cls = dofMgr.classifyDofs(K, F);
        ReducedLinearSystem redSys;
        ASSERT_TRUE(GlobalAssembler::buildReducedSystem(dofMgr, K, F, cls, redSys), "buildReducedSystem");

        SpMat M_ff;
        std::array<VecX, 3> infl;
        ASSERT_TRUE(DynamicSolver::reduceMassAndInfluence(dofMgr, M, redSys, M_ff, infl), "reduceMassAndInfluence");
        // No 2 livre tem 6 DOFs livres: 0..2 translacionais, 3..5 rotacionais
        ASSERT_TRUE(infl[0].size() == 6, "6 DOFs livres no no 2");
        for (int dir = 0; dir < 3; ++dir) {
            for (int t = 0; t < 3; ++t) {
                ASSERT_NEAR(infl[dir](t), (t == dir) ? 1.0 : 0.0, 1e-12,
                            "Apenas a translacao na direcao sismica dir deve receber coeficiente 1.0");
            }
            for (int r = 3; r < 6; ++r) {
                ASSERT_NEAR(infl[dir](r), 0.0, 1e-12,
                            "DOFs rotacionais nunca devem receber aceleracao translacional unitaria direta");
            }
        }
    }

    // 2. Expansao de fasor complexo harmonico com MPC (T_mpc * U_red_c antes de cwiseAbs)
    {
        StructuralModel m;
        m.clear();
        const NodeId n1 = m.addNode(0.0, 0.0, 0.0);
        const NodeId n2 = m.addNode(0.0, 0.0, 3.0);
        const NodeId n3 = m.addNode(2.0, 0.0, 3.0);
        m.findNode(n1)->support = SupportRestraint::makeFixed();
        m.findNode(n2)->load.force.y = 10.0;
        m.findNode(n2)->load.moment.z = -10.0; // produz rotacao theta_z em oposicao de fase parcial com u_y
        m.addElement(n1, n2, 1, 1);
        m.getRigidLinks().push_back({n2, n3, true});

        HarmonicOptions hOpts;
        hOpts.frequenciesHz = {2.0};
        hOpts.rayleighAlphaM = 0.25;
        hOpts.rayleighBetaK = 0.002;
        const HarmonicResult hRes = FemSolver3D::solveHarmonic(m, hOpts);
        ASSERT_TRUE(hRes.success && hRes.sweep.size() == 1, "Harmonic com RigidLink deve convergir");

        const auto& pt = hRes.sweep[0];
        // No 2 (mestre): uy = indice 7, rz = indice 11; No 3 (escravo a dx=+2): uy_s = uy_m + 2 * rz_m
        const std::complex<double> uy_m = std::polar(pt.amplitudeFull(7), pt.phaseRadFull(7));
        const std::complex<double> rz_m = std::polar(pt.amplitudeFull(11), pt.phaseRadFull(11));
        const std::complex<double> uy_s_exact = uy_m + 2.0 * rz_m;
        ASSERT_NEAR(pt.amplitudeFull(13), std::abs(uy_s_exact), 1e-9,
                    "Amplitude harmonica no escravo MPC deve igualar o modulo do fasor complexo combinado");
    }

    std::cout << "[OK] testAud011HarmonicMpcPhasorAndRotationalInfluence\n";
}

void testAud011EccentricRigidDiaphragmModalMassParticipation() {
    StructuralModel m;
    m.clear();

    const NodeId b1 = m.addNode(0.0, 0.0, 0.0);
    const NodeId b2 = m.addNode(6.0, 0.0, 0.0);
    const NodeId b3 = m.addNode(6.0, 4.0, 0.0);
    const NodeId b4 = m.addNode(0.0, 4.0, 0.0);
    for (NodeId bid : {b1, b2, b3, b4}) {
        m.findNode(bid)->support = SupportRestraint::makeFixed();
    }

    const NodeId t1 = m.addNode(0.0, 0.0, 3.5);
    const NodeId t2 = m.addNode(6.0, 0.0, 3.5);
    const NodeId t3 = m.addNode(6.0, 4.0, 3.5);
    const NodeId t4 = m.addNode(0.0, 4.0, 3.5);
    // No mestre excentrico (nao coincide com o centroide geometrico (3.0, 2.0, 3.5))
    const NodeId master = m.addNode(4.5, 2.8, 3.5);

    // 4 pilares
    m.addElement(b1, t1, 1, 1);
    m.addElement(b2, t2, 1, 1);
    m.addElement(b3, t3, 1, 1);
    m.addElement(b4, t4, 1, 1);
    // 4 vigas de borda do pavimento
    m.addElement(t1, t2, 1, 1);
    m.addElement(t2, t3, 1, 1);
    m.addElement(t3, t4, 1, 1);
    m.addElement(t4, t1, 1, 1);
    // Conexoes do no mestre excentrico para rigidez fora do plano (uz, rx, ry) e massa distribuida
    m.addElement(t1, master, 1, 1);
    m.addElement(t2, master, 1, 1);
    m.addElement(t3, master, 1, 1);
    m.addElement(t4, master, 1, 1);

    RigidDiaphragmSpec dia;
    dia.masterNode = master;
    dia.slaveNodes = {t1, t2, t3, t4};
    dia.normalAxis = 2; // plano XY
    m.getRigidDiaphragms().push_back(dia);

    ModalOptions opts;
    opts.numModes = 24;
    opts.tolerance = 1e-10;
    const ModalResult res = FemSolver3D::solveModal(m, opts);
    ASSERT_TRUE(res.success && !res.modes.empty(),
                "Analise modal de portico 3D com diafragma rigido excentrico deve convergir");
    ASSERT_TRUE(res.totalActiveMass[0] > 0.0 && res.totalActiveMass[1] > 0.0 && res.totalActiveMass[2] > 0.0,
                "Massa ativa total deve ser estritamente positiva nas direcoes X, Y, Z");
    ASSERT_NEAR(res.cumulativeMassRatio[0], 1.0, 1e-3,
                "Participacao acumulada de massa sismica em X deve somar 100%");
    ASSERT_NEAR(res.cumulativeMassRatio[1], 1.0, 1e-3,
                "Participacao acumulada de massa sismica em Y deve somar 100%");
    ASSERT_NEAR(res.cumulativeMassRatio[2], 1.0, 1e-3,
                "Participacao acumulada de massa sismica em Z deve somar 100%");

    // Verifica que nenhum apoio de base possui componente modal espúria (translacional ou rotacional)
    // Os 4 nos de base sao indices 0..3 -> DOFs globais 0..23
    for (const auto& mode : res.modes) {
        ASSERT_TRUE(mode.phi_full.size() >= 24, "phi_full deve incluir todos os DOFs globais");
        for (int g = 0; g < 24; ++g) {
            ASSERT_NEAR(mode.phi_full(g), 0.0, 1e-12,
                        "DOFs translacionais e rotacionais engastados na base devem permanecer nulos nos autovetores");
        }
    }

    std::cout << "[OK] testAud011EccentricRigidDiaphragmModalMassParticipation (modes="
              << res.modes.size() << ", cumX=" << res.cumulativeMassRatio[0]
              << ", cumY=" << res.cumulativeMassRatio[1] << ")\n";
}

void testAud012ModalWithPrescribedSettlement() {
    auto buildCantileverColumn = [](bool withSettlement) {
        StructuralModel col;
        col.clear();
        const NodeId n1 = col.addNode(0.0, 0.0, 0.0);
        const NodeId n2 = col.addNode(0.0, 0.0, 2.0);
        const NodeId n3 = col.addNode(0.0, 0.0, 4.0);
        col.findNode(n1)->support = SupportRestraint::makeFixed();
        if (withSettlement) {
            col.findNode(n1)->support.hasPrescribed[0] = true;
            col.findNode(n1)->support.prescribedValue[0] = 0.010;
            col.findNode(n1)->support.hasPrescribed[2] = true;
            col.findNode(n1)->support.prescribedValue[2] = -0.025;
        }
        col.addElement(n1, n2, 1, 1);
        col.addElement(n2, n3, 1, 1);
        return col;
    };

    StructuralModel refModel = buildCantileverColumn(false);
    StructuralModel settledModel = buildCantileverColumn(true);

    ModalOptions opts;
    opts.numModes = 6;
    opts.tolerance = 1e-10;
    const ModalResult refRes = FemSolver3D::solveModal(refModel, opts);
    const ModalResult setRes = FemSolver3D::solveModal(settledModel, opts);

    ASSERT_TRUE(refRes.success && setRes.success && !setRes.modes.empty(),
                "Analise modal com recalque prescrito nao nulo deve convergir");
    ASSERT_TRUE(refRes.modes.size() == setRes.modes.size(),
                "Numero de modos com recalque prescrito deve coincidir com referencia");

    for (std::size_t i = 0; i < setRes.modes.size(); ++i) {
        const auto& mode = setRes.modes[i];
        ASSERT_NEAR(mode.phi_full(0), 0.0, 1e-12,
                    "Autovetor modal deve ser nulo no DOF X com recalque prescrito");
        ASSERT_NEAR(mode.phi_full(2), 0.0, 1e-12,
                    "Autovetor modal deve ser nulo no DOF Z com recalque prescrito");
        ASSERT_NEAR(mode.frequencyHz, refRes.modes[i].frequencyHz, 1e-9,
                    "Frequencia modal deve ser invariante ao valor do recalque estatico prescrito");
    }

    std::cout << "[OK] testAud012ModalWithPrescribedSettlement (f1=" << setRes.modes[0].frequencyHz << " Hz)\n";
}

} // namespace

int main() {
    testSdoFFreeVibrationAndHarmonicFrf();
    testContinuousCantileverModesConsistent();
    testSeismicAndResponseSpectrumCqcVsSrss();
    testFemSolver3DRayleighNotDoubled();
    testNbr15421SpectrumAndSignInvariance();
    testAud011HarmonicMpcPhasorAndRotationalInfluence();
    testAud011EccentricRigidDiaphragmModalMassParticipation();
    testAud012ModalWithPrescribedSettlement();
    std::cout << "\nTodos os testes da Fase 4 (Modal, Rayleigh, Harmônica FRF, Newmark/HHT, Espectro CQC/SRSS) passaram!\n";
    return 0;
}
