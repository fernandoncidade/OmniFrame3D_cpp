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

} // namespace

int main() {
    testSdoFFreeVibrationAndHarmonicFrf();
    testContinuousCantileverModesConsistent();
    testSeismicAndResponseSpectrumCqcVsSrss();
    std::cout << "\nTodos os testes da Fase 4 (Modal, Rayleigh, Harmônica FRF, Newmark/HHT, Espectro CQC/SRSS) passaram!\n";
    return 0;
}
