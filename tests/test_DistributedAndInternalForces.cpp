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
    }
}

} // namespace

int main() {
    testClampedBeamGlobalZ();
    testClampedBeamLocalY();
    testSimplySupportedBeam();
    testInclinedBeamWithRollAndColumn();
    std::cout << "[SUCESSO] test_DistributedAndInternalForces validado 100%.\n";
    return 0;
}

