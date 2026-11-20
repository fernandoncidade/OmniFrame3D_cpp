/**
 * @file test_ReleasesAndThermal.cpp
 * @brief Auditoria de Condensacao Estatica de Cargas Distribuidas e Termicas com Rotulas + Recuperacao da Deformada
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

void testDoubleHingedBeamWithDistributedLoad() {
    // Viga com nos biengastados, mas barra rotulada em ambos os extremos em Y (start[4]=true, end[4]=true)
    StructuralModel model;
    Material mat = Material::makeConcreteC30(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 20x50", 0.20, 0.50);
    MaterialId mid = model.addMaterial(mat);
    SectionId sid = model.addCrossSection(sec);

    const double L = 6.0;
    const double qz = -10.0;

    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(L, 0.0, 0.0);
    model.findNode(n1)->support = SupportRestraint::makeFixed();
    model.findNode(n2)->support = SupportRestraint::makeFixed();

    ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
    Element* elem = model.findElement(eid);
    elem->releases.start[4] = true; // Rotula em ry no inicio
    elem->releases.end[4]   = true; // Rotula em ry no fim
    elem->distLoad.active = true;
    elem->distLoad.coordSystem = CoordSystem::Global;
    elem->distLoad.w = {0.0, 0.0, qz};

    SolverResult res = FemSolver3D::solve(model);
    TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Viga com rotulas duplas desequilibrada");

    // Reacoes de momento nos apoios engastados devem ser rigorosamente NULAS por causa das rotulas internas!
    assertNear(model.findNode(n1)->reaction[4], 0.0, 1e-4, "Reacao My1 deve ser nula com rotula interna");
    assertNear(model.findNode(n2)->reaction[4], 0.0, 1e-4, "Reacao My2 deve ser nula com rotula interna");
    assertNear(model.findNode(n1)->reaction[2], +std::abs(qz) * L / 2.0, 1e-4, "Reacao Rz1 = qL/2");
    assertNear(model.findNode(n2)->reaction[2], +std::abs(qz) * L / 2.0, 1e-4, "Reacao Rz2 = qL/2");

    // Momento no centro deve ser qL^2/8 (nao qL^2/24!) e flecha no centro 5qL^4/(384EI)!
    InternalForces f0 = elem->evaluateAt(0.0, model);
    InternalForces fMid = elem->evaluateAt(0.5, model);
    InternalForces f1 = elem->evaluateAt(1.0, model);

    assertNear(f0.My, 0.0, 1e-4, "My(0) nulo na rotula");
    assertNear(fMid.My, -std::abs(qz) * L * L / 8.0, 1e-4, "My(L/2) = -qL^2/8 na viga bi-rotulada");
    assertNear(f1.My, 0.0, 1e-4, "My(L) nulo na rotula");

    double expectedW = (5.0 * qz * std::pow(L, 4)) / (384.0 * mat.E * sec.Iy);
    assertNear(fMid.defGlobal.z, expectedW, 1e-6, "Deformada recuperada na barra com rotulas deve ser 5qL^4/(384EI)");
}

void testProppedCantileverWithEndHinge() {
    // Viga engastada na esquerda (no 1) e rotulada na barra junto ao apoio direito engastado (no 2)
    StructuralModel model;
    Material mat = Material::makeStructuralSteel(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 20x40", 0.20, 0.40);
    MaterialId mid = model.addMaterial(mat);
    SectionId sid = model.addCrossSection(sec);

    const double L = 4.0;
    const double qz = -16.0;

    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(L, 0.0, 0.0);
    model.findNode(n1)->support = SupportRestraint::makeFixed();
    model.findNode(n2)->support = SupportRestraint::makeFixed();

    ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
    Element* elem = model.findElement(eid);
    elem->releases.end[4] = true; // Rotula em ry no extremo final
    elem->distLoad.active = true;
    elem->distLoad.coordSystem = CoordSystem::Global;
    elem->distLoad.w = {0.0, 0.0, qz};

    SolverResult res = FemSolver3D::solve(model);
    TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Viga engastada-rotulada desequilibrada");

    double wAbs = std::abs(qz);
    assertNear(model.findNode(n1)->reaction[2], 5.0 * wAbs * L / 8.0, 1e-4, "Reacao esquerda = 5/8 qL");
    assertNear(model.findNode(n2)->reaction[2], 3.0 * wAbs * L / 8.0, 1e-4, "Reacao direita rotulada = 3/8 qL");
    assertNear(model.findNode(n2)->reaction[4], 0.0, 1e-4, "Momento reativo nulo na extremidade rotulada");

    InternalForces f0 = elem->evaluateAt(0.0, model);
    assertNear(f0.My, +wAbs * L * L / 8.0, 1e-4, "Momento no engaste esquerdo = +qL^2/8");
}

void testThermalLoadClampedAndAxiallyReleased() {
    // 1. Barra biengastada sob +60 C -> compressao axial N = -E * A * alpha * deltaT
    {
        StructuralModel model;
        Material mat = Material::makeStructuralSteel(10);
        CrossSection sec = CrossSection::makeRectangular(10, "Ret 10x10", 0.10, 0.10);
        MaterialId mid = model.addMaterial(mat);
        SectionId sid = model.addCrossSection(sec);

        NodeId n1 = model.addNode(0.0, 0.0, 0.0);
        NodeId n2 = model.addNode(5.0, 0.0, 0.0);
        model.findNode(n1)->support = SupportRestraint::makeFixed();
        model.findNode(n2)->support = SupportRestraint::makeFixed();

        ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
        Element* elem = model.findElement(eid);
        elem->thermalLoad.active = true;
        elem->thermalLoad.deltaT = 60.0;

        SolverResult res = FemSolver3D::solve(model);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Barra termica biengastada falhou");

        double Nth = mat.E * sec.A * mat.alpha * 60.0;
        assertNear(elem->evaluateAt(0.5, model).N, -Nth, 1e-3, "Forca axial termica sob restricao");
        assertNear(model.findNode(n1)->reaction[0], +Nth, 1e-3, "Reacao termica Rx1");
        assertNear(model.findNode(n2)->reaction[0], -Nth, 1e-3, "Reacao termica Rx2");
    }

    // 2. Barra com liberacao axial no no inicial (releases.start[0] = true) sob +60 C: expansao livre sem esforco!
    {
        StructuralModel model;
        Material mat = Material::makeStructuralSteel(10);
        CrossSection sec = CrossSection::makeRectangular(10, "Ret 10x10", 0.10, 0.10);
        MaterialId mid = model.addMaterial(mat);
        SectionId sid = model.addCrossSection(sec);

        const double L = 5.0;
        const double dT = 60.0;
        NodeId n1 = model.addNode(0.0, 0.0, 0.0);
        NodeId n2 = model.addNode(L, 0.0, 0.0);
        model.findNode(n1)->support = SupportRestraint::makeFixed();
        model.findNode(n2)->support = SupportRestraint::makeFixed();

        ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
        Element* elem = model.findElement(eid);
        elem->releases.start[0] = true; // Liberacao axial no inicio
        elem->thermalLoad.active = true;
        elem->thermalLoad.deltaT = dT;

        SolverResult res = FemSolver3D::solve(model);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Barra termica com liberacao axial falhou");

        assertNear(elem->evaluateAt(0.5, model).N, 0.0, 1e-4, "Forca axial deve ser zero com liberacao axial");
        assertNear(model.findNode(n1)->reaction[0], 0.0, 1e-4, "Reacao Rx1 deve ser zero com liberacao axial");
        assertNear(model.findNode(n2)->reaction[0], 0.0, 1e-4, "Reacao Rx2 deve ser zero com liberacao axial");
        assertNear(elem->localDisplacements(0), -mat.alpha * dT * L, 1e-7, "Expansao termica livre recuperada na extremidade liberada");
    }
}

} // namespace

int main() {
    testDoubleHingedBeamWithDistributedLoad();
    testProppedCantileverWithEndHinge();
    testThermalLoadClampedAndAxiallyReleased();
    std::cout << "[SUCESSO] test_ReleasesAndThermal validado 100%.\n";
    return 0;
}

