/**
 * @file test_DistributedAndInternalForces.cpp
 * @brief Auditoria de Cargas Distribuídas (Global/Local), Vigas Inclinadas/Verticais e Esforços Internos (N, V, M, T, flecha)
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include <cmath>

using namespace omniframe;

namespace {

void assertNear(double actual, double expected, double tol, const char* msg) {
    if (std::abs(actual - expected) > tol) {
        std::cerr << "\n[ERRO] " << msg
                  << "\n  Esperado: " << expected
                  << "\n  Obtido:   " << actual
                  << "\n  Diferenca: " << std::abs(actual - expected)
                  << " (tol=" << tol << ")" << std::endl;
        std::exit(1);
    }
}

void testClampedBeamGlobalZ() {
    StructuralModel model;
    Material mat = Material::makeStructuralSteel(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 20x40", 0.20, 0.40);
    MaterialId mid = model.addMaterial(mat);
    SectionId sid = model.addCrossSection(sec);

    const double L = 6.0;
    const double qz = -12.0; // kN/m (-Z global)

    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(L, 0.0, 0.0);
    model.findNode(n1)->support = SupportRestraint::makeFixed();
    model.findNode(n2)->support = SupportRestraint::makeFixed();

    ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
    Element* elem = model.findElement(eid);
    elem->distLoad.active = true;
    elem->distLoad.coordSystem = CoordSystem::Global;
    elem->distLoad.w = {0.0, 0.0, qz};

    SolverResult res = FemSolver3D::solve(model);
    TEST_ASSERT(res.success, "Solver falhou em viga biengastada qz");
    TEST_ASSERT(res.equilibrium.isBalanced, "Viga biengastada qz desequilibrada");

    // Reacoes esperadas: Rz = 36 kN em cada apoio; My_end = +/- q*L^2/12 = +/- 36 kNm
    assertNear(model.findNode(n1)->reaction[2], -qz * L / 2.0, 1e-4, "Reacao Rz1 biengastada");
    assertNear(model.findNode(n2)->reaction[2], -qz * L / 2.0, 1e-4, "Reacao Rz2 biengastada");

    InternalForces f0 = elem->evaluateAt(0.0, model);
    InternalForces fMid = elem->evaluateAt(0.5, model);
    InternalForces f1 = elem->evaluateAt(1.0, model);

    double wAbs = std::abs(qz);
    assertNear(f0.Vz, -wAbs * L / 2.0, 1e-4, "Cortante Vz(0) biengastada");
    assertNear(fMid.Vz, 0.0, 1e-4, "Cortante Vz(L/2) biengastada");
    assertNear(f1.Vz, +wAbs * L / 2.0, 1e-4, "Cortante Vz(L) biengastada");

    // Em localEndForces com theta_y = -dw/dx: apoio tem tracao superior (My > 0), meio tem tracao inferior (My < 0)
    assertNear(f0.My, +wAbs * L * L / 12.0, 1e-4, "Momento My(0) biengastada = +qL^2/12");
    assertNear(fMid.My, -wAbs * L * L / 24.0, 1e-4, "Momento My(L/2) biengastada = -qL^2/24");
    assertNear(f1.My, +wAbs * L * L / 12.0, 1e-4, "Momento My(L) biengastada = +qL^2/12");

    // Flecha no meio do vao: w = qz * L^4 / (384 * E * Iy)
    double expectedW = (qz * std::pow(L, 4)) / (384.0 * mat.E * sec.Iy);
    assertNear(fMid.defLocal.z, expectedW, 1e-7, "Flecha no meio do vao biengastada qz");
    assertNear(fMid.defGlobal.z, expectedW, 1e-7, "Flecha global no meio do vao biengastada qz");

    // [AUD-001] elementExtremes deve refletir exatamente elem->evaluateAt(...)
    TEST_ASSERT(res.elementExtremes.size() == 1 && res.elementExtremes[0].elementId == eid,
                "elementExtremes deve conter o elemento da viga biengastada");
    const auto& extBeam = res.elementExtremes[0];
    assertNear(extBeam.My_max, +wAbs * L * L / 12.0, 1e-4, "elementExtremes My_max viga biengastada");
    assertNear(extBeam.My_min, -wAbs * L * L / 24.0, 1e-4, "elementExtremes My_min viga biengastada");
    assertNear(extBeam.Vz_max, +wAbs * L / 2.0, 1e-4, "elementExtremes Vz_max viga biengastada");
    assertNear(extBeam.Vz_min, -wAbs * L / 2.0, 1e-4, "elementExtremes Vz_min viga biengastada");
    assertNear(extBeam.maxDeflection, std::abs(expectedW), 1e-7, "elementExtremes maxDeflection viga biengastada");

    // Re-edicao: dobra qz e verifica nova resposta exata
    elem->distLoad.w.z = 2.0 * qz;
    SolverResult res2 = FemSolver3D::solve(model);
    TEST_ASSERT(res2.success && res2.equilibrium.isBalanced, "Re-calculo apos alterar carga falhou");
    InternalForces fMid2 = elem->evaluateAt(0.5, model);
    assertNear(fMid2.My, 2.0 * fMid.My, 1e-4, "Momento dobrado apos re-edicao de carga distribuida");
    assertNear(fMid2.defLocal.z, 2.0 * expectedW, 1e-7, "Flecha dobrada apos re-edicao de carga distribuida");
}

void testClampedBeamLocalY() {
    StructuralModel model;
    Material mat = Material::makeStructuralSteel(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 20x40", 0.20, 0.40);
    MaterialId mid = model.addMaterial(mat);
    SectionId sid = model.addCrossSection(sec);

    const double L = 6.0;
    const double qy = -12.0; // kN/m (-Y local)

    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(L, 0.0, 0.0);
    model.findNode(n1)->support = SupportRestraint::makeFixed();
    model.findNode(n2)->support = SupportRestraint::makeFixed();

    ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
    Element* elem = model.findElement(eid);
    elem->distLoad.active = true;
    elem->distLoad.coordSystem = CoordSystem::Local;
    elem->distLoad.w = {0.0, qy, 0.0};

    SolverResult res = FemSolver3D::solve(model);
    TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Viga biengastada qy desequilibrada");

    InternalForces f0 = elem->evaluateAt(0.0, model);
    InternalForces fMid = elem->evaluateAt(0.5, model);
    InternalForces f1 = elem->evaluateAt(1.0, model);

    double wAbs = std::abs(qy);
    assertNear(f0.Vy, -wAbs * L / 2.0, 1e-4, "Cortante Vy(0) biengastada");
    assertNear(fMid.Vy, 0.0, 1e-4, "Cortante Vy(L/2) biengastada");
    assertNear(f1.Vy, +wAbs * L / 2.0, 1e-4, "Cortante Vy(L) biengastada");

    assertNear(f0.Mz, -wAbs * L * L / 12.0, 1e-4, "Momento Mz(0) biengastada = -qL^2/12");
    assertNear(fMid.Mz, +wAbs * L * L / 24.0, 1e-4, "Momento Mz(L/2) biengastada = +qL^2/24");
    assertNear(f1.Mz, -wAbs * L * L / 12.0, 1e-4, "Momento Mz(L) biengastada = -qL^2/12");

    double expectedV = (qy * std::pow(L, 4)) / (384.0 * mat.E * sec.Iz);
    assertNear(fMid.defLocal.y, expectedV, 1e-7, "Flecha local v(L/2) biengastada qy");
}

void testSimplySupportedBeam() {
    StructuralModel model;
    Material mat = Material::makeConcreteC30(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 20x50", 0.20, 0.50);
    MaterialId mid = model.addMaterial(mat);
    SectionId sid = model.addCrossSection(sec);

    const double L = 6.0;
    const double qz = -10.0;

    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(L, 0.0, 0.0);
    // Apoio isostatico 3D que libera rotacao em Y (flexao XZ)
    auto* p1 = model.findNode(n1);
    p1->support = SupportRestraint::makePinned(); // uX, uY, uZ = true
    p1->support.fixed[3] = true; // trava torcao rx
    p1->support.fixed[5] = true; // trava rotacao planar rz (deixa ry livre!)

    auto* p2 = model.findNode(n2);
    p2->support = SupportRestraint::makeRoller(2); // uZ = true
    p2->support.fixed[1] = true; // uY = true

    ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
    Element* elem = model.findElement(eid);
    elem->distLoad.active = true;
    elem->distLoad.coordSystem = CoordSystem::Global;
    elem->distLoad.w = {0.0, 0.0, qz};

    SolverResult res = FemSolver3D::solve(model);
    TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Viga biapoiada desequilibrada");

    InternalForces f0 = elem->evaluateAt(0.0, model);
    InternalForces fMid = elem->evaluateAt(0.5, model);
    InternalForces f1 = elem->evaluateAt(1.0, model);

    double wAbs = std::abs(qz);
    assertNear(f0.My, 0.0, 1e-4, "Momento My(0) biapoiada deve ser zero");
    assertNear(fMid.My, -wAbs * L * L / 8.0, 1e-4, "Momento My(L/2) biapoiada = -qL^2/8");
    assertNear(f1.My, 0.0, 1e-4, "Momento My(L) biapoiada deve ser zero");

    double expectedW = (5.0 * qz * std::pow(L, 4)) / (384.0 * mat.E * sec.Iy);
    assertNear(fMid.defGlobal.z, expectedW, 1e-6, "Flecha 5qL^4/(384EI) na viga biapoiada");
}

void testInclinedBeamWithRollAndColumn() {
    // 1. Barra inclinada no espaco 3D com rollAngle = 30 graus e carga global nos 3 eixos
    {
        StructuralModel model;
        Material mat = Material::makeStructuralSteel(10);
        CrossSection sec = CrossSection::makeRectangular(10, "Ret 20x30", 0.20, 0.30);
        MaterialId mid = model.addMaterial(mat);
        SectionId sid = model.addCrossSection(sec);

        NodeId n1 = model.addNode(0.0, 0.0, 0.0);
        NodeId n2 = model.addNode(3.0, 4.0, 0.0); // L = 5.0 m
        model.findNode(n1)->support = SupportRestraint::makeFixed();
        model.findNode(n2)->support = SupportRestraint::makeFixed();

        ElementId eid = model.addElement(n1, n2, mid, sid, 30.0);
        Element* elem = model.findElement(eid);
        elem->distLoad.active = true;
        elem->distLoad.coordSystem = CoordSystem::Global;
        elem->distLoad.w = {3.0, -4.0, -10.0};

        SolverResult res = FemSolver3D::solve(model);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Barra inclinada com roll desequilibrada");
        assertNear(res.equilibrium.residualForces.norm(), 0.0, 1e-4, "Residuo de forcas nulo na barra inclinada");
        assertNear(res.equilibrium.residualMoments.norm(), 0.0, 1e-4, "Residuo de momentos nulo na barra inclinada");
    }

    // 2. Pilar vertical em balanco sob carga horizontal distribuida global qX = +8 kN/m e axial qZ = -10 kN/m
    {
        StructuralModel model;
        Material mat = Material::makeStructuralSteel(10);
        CrossSection sec = CrossSection::makeCircularTube(10, "Tubo D200", 0.20, 0.01);
        MaterialId mid = model.addMaterial(mat);
        SectionId sid = model.addCrossSection(sec);

        const double H = 4.0;
        const double qx = 8.0;
        const double qz = -10.0;

        NodeId n1 = model.addNode(0.0, 0.0, 0.0);
        NodeId n2 = model.addNode(0.0, 0.0, H);
        model.findNode(n1)->support = SupportRestraint::makeFixed();

        ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
        Element* elem = model.findElement(eid);
        elem->distLoad.active = true;
        elem->distLoad.coordSystem = CoordSystem::Global;
        elem->distLoad.w = {qx, 0.0, qz};

        SolverResult res = FemSolver3D::solve(model);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Pilar vertical em balanco desequilibrado");

        assertNear(model.findNode(n1)->reaction[0], -qx * H, 1e-4, "Reacao Rx no pe do pilar");
        assertNear(model.findNode(n1)->reaction[2], -qz * H, 1e-4, "Reacao Rz axial no pe do pilar");
        assertNear(model.findNode(n1)->reaction[4], -qx * H * (H / 2.0), 1e-4, "Momento reativo My no pe do pilar");

        // Deslocamento lateral no topo: qx * H^4 / (8 * E * I)
        double expectedUx = (qx * std::pow(H, 4)) / (8.0 * mat.E * sec.Iy);
        assertNear(model.findNode(n2)->displacement[0], expectedUx, 1e-6, "Deslocamento horizontal no topo do pilar em balanco");

        // [AUD-001] Verificar extremos do pilar vertical em balanco vs evaluateAt
        TEST_ASSERT(res.elementExtremes.size() == 1 && res.elementExtremes[0].elementId == eid,
                    "elementExtremes deve conter o pilar em balanco");
        InternalForces colMin = elem->evaluateAt(0.0, model);
        InternalForces colTop = elem->evaluateAt(1.0, model);
        assertNear(res.elementExtremes[0].N_min, colMin.N, 1e-6, "elementExtremes N_min pilar em balanco");
        assertNear(res.elementExtremes[0].N_max, colTop.N, 1e-6, "elementExtremes N_max topo do pilar em balanco");
        assertNear(res.elementExtremes[0].My_max, colMin.My, 1e-4, "elementExtremes My_max base do pilar em balanco");
        assertNear(res.elementExtremes[0].maxDeflection, colTop.defGlobal.norm(), 1e-6, "elementExtremes deflexao pilar em balanco");
    }
}

void testBuildingFrameElementExtremesConsistency() {
    StructuralModel model = StructuralModel::createBuildingFrameExample();
    SolverResult res = FemSolver3D::solve(model);
    TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Portico exemplo building deve resolver equilibrado");
    TEST_ASSERT(res.elementExtremes.size() == model.getElements().size(),
                "elementExtremes deve ter 1 entrada por elemento 1D");

    int columnsWithCompression = 0;
    for (const auto& ext : res.elementExtremes) {
        const Element* el = model.findElement(ext.elementId);
        TEST_ASSERT(el != nullptr && el->hasResults, "Elemento do portico deve possuir hasResults = true");

        double expectedNMin = 0.0, expectedNMax = 0.0;
        double expectedMyMin = 0.0, expectedMyMax = 0.0;
        double expectedMzMin = 0.0, expectedMzMax = 0.0;
        double expectedMaxDef = 0.0;
        constexpr int Nsamples = 32;
        for (int s = 0; s <= Nsamples; ++s) {
            const double xi = static_cast<double>(s) / static_cast<double>(Nsamples);
            const InternalForces f = el->evaluateAt(xi, model);
            if (s == 0) {
                expectedNMin = expectedNMax = f.N;
                expectedMyMin = expectedMyMax = f.My;
                expectedMzMin = expectedMzMax = f.Mz;
                expectedMaxDef = f.defGlobal.norm();
            } else {
                expectedNMin = std::min(expectedNMin, f.N);
                expectedNMax = std::max(expectedNMax, f.N);
                expectedMyMin = std::min(expectedMyMin, f.My);
                expectedMyMax = std::max(expectedMyMax, f.My);
                expectedMzMin = std::min(expectedMzMin, f.Mz);
                expectedMzMax = std::max(expectedMzMax, f.Mz);
                expectedMaxDef = std::max(expectedMaxDef, f.defGlobal.norm());
            }
        }
        assertNear(ext.N_min, expectedNMin, 1e-9, "Consistencia ext.N_min vs evaluateAt no building");
        assertNear(ext.N_max, expectedNMax, 1e-9, "Consistencia ext.N_max vs evaluateAt no building");
        assertNear(ext.My_min, expectedMyMin, 1e-9, "Consistencia ext.My_min vs evaluateAt no building");
        assertNear(ext.My_max, expectedMyMax, 1e-9, "Consistencia ext.My_max vs evaluateAt no building");
        assertNear(ext.Mz_min, expectedMzMin, 1e-9, "Consistencia ext.Mz_min vs evaluateAt no building");
        assertNear(ext.Mz_max, expectedMzMax, 1e-9, "Consistencia ext.Mz_max vs evaluateAt no building");
        assertNear(ext.maxDeflection, expectedMaxDef, 1e-9, "Consistencia ext.maxDeflection vs evaluateAt no building");

        const Node* n1 = model.findNode(el->nodeStart);
        const Node* n2 = model.findNode(el->nodeEnd);
        TEST_ASSERT(n1 && n2, "Nos do elemento do portico devem existir");
        const Vec3 dp = n2->position - n1->position;
        if (std::abs(dp.z) / std::max(0.05, dp.norm()) > 0.707) {
            TEST_ASSERT(ext.N_min < -1.0, "Pilar do building deve apresentar compressao axial N_min < -1 kN");
            ++columnsWithCompression;
        }
    }
    TEST_ASSERT(columnsWithCompression > 0, "Portico building deve conter pilares comprimidos avaliados");
}

void testAud012LoadCaseSelfWeightInternalForcesAndLine2ConsistentLoads() {
    // 1. Viga biengastada resolvida via solveLoadCase com selfWeight ativo deve incluir q = gamma*A em f0 e evaluateAt(0.5)
    {
        StructuralModel m;
        m.clear();
        const Material* mat = m.findMaterial(1);
        const CrossSection* sec = m.findCrossSection(1);
        TEST_ASSERT(mat && sec, "Material e secao padrao devem existir");

        const double L = 4.0;
        const NodeId n1 = m.addNode(0.0, 0.0, 0.0);
        const NodeId n2 = m.addNode(L, 0.0, 0.0);
        m.findNode(n1)->support = SupportRestraint::makeFixed();
        m.findNode(n2)->support = SupportRestraint::makeFixed();
        const ElementId eid = m.addElement(n1, n2, 1, 1);

        kernel::LoadCase lc;
        lc.id = 1;
        lc.name = "PesoProprio";
        lc.selfWeight.enabled = true;
        lc.selfWeight.direction = kernel::Vec3(0.0, 0.0, -1.0);
        lc.selfWeight.factor = 1.0;

        const SolverResult res = FemSolver3D::solveLoadCase(m, lc);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "solveLoadCase com selfWeight deve fechar equilibrio");

        const double q = mat->gamma * sec->A; // kN/m
        const Element* el = m.findElement(eid);
        TEST_ASSERT(el && el->hasResults, "Elemento deve ter resultados pos-processados em solveLoadCase");
        const InternalForces midF = el->evaluateAt(0.5, m);
        // Em viga biengastada sob carga vertical -q (Z), momento no meio do vao tem magnitude q*L^2/24
        assertNear(std::abs(midF.My), q * L * L / 24.0, 1e-5,
                   "Momento fletor no meio do vao sob peso proprio de LoadCase deve igualar q*L^2/24");
    }

    // 2. Line2MeshKernelElement (horizontal e vertical) com carregamento distribuido consistente (momentos q*L^2/12)
    {
        StructuralModel m;
        m.clear();
        const double L = 2.0;
        const double qx = 12.0;
        kernel::MeshNode mn1, mn2;
        mn1.id = 1;
        mn1.position = kernel::Vec3(0.0, 0.0, 0.0);
        mn1.fixed = {true, true, true, true, true, true};
        mn2.id = 2;
        mn2.position = kernel::Vec3(0.0, 0.0, L); // barra Line2 vertical
        m.surfaceMesh().nodes = {mn1, mn2};

        kernel::MeshElement me;
        me.id = 1;
        me.type = kernel::MeshCellType::Line2;
        me.nodeIds = {1, 2};
        me.materialId = 1;
        me.sectionId = 1;
        me.distributedLoad = kernel::Vec3(qx, 0.0, 0.0);
        m.surfaceMesh().elements = {me};

        const SolverResult res = FemSolver3D::solve(m);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced,
                    "Line2 vertical com carga distribuida transversal deve fechar equilibrio de forcas e momentos");
        // Reacao na base engastada: Rx = -qx*L = -24 kN, My = -qx*L^2/2 = -24 kN.m
        assertNear(m.surfaceMesh().nodes[0].reaction[0], -qx * L, 1e-4, "Reacao Rx na base de Line2 vertical");
        assertNear(std::abs(m.surfaceMesh().nodes[0].reaction[4]), qx * L * L * 0.5, 1e-4,
                   "Momento reativo My na base de Line2 vertical deve incluir momento consistente");
    }
}

} // namespace

int main() {
    testClampedBeamGlobalZ();
    testClampedBeamLocalY();
    testSimplySupportedBeam();
    testInclinedBeamWithRollAndColumn();
    testBuildingFrameElementExtremesConsistency();
    testAud012LoadCaseSelfWeightInternalForcesAndLine2ConsistentLoads();
    std::cout << "[SUCESSO] test_DistributedAndInternalForces validado 100%.\n";
    return 0;
}

