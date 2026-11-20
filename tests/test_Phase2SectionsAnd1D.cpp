#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include "io/ProjectSerializer.hpp"
#include <QTemporaryDir>
#include <cmath>

using namespace omniframe;

namespace {

void testExtendedCrossSections() {
    // 1. Box RHS: comparar com retângulo vazado exato
    const double h = 0.30, b = 0.20, tf = 0.012, tw = 0.010;
    CrossSection rhs = CrossSection::makeBoxRHS(10, "RHS 300x200", h, b, tf, tw);
    const double bi = b - 2.0 * tw;
    const double hi = h - 2.0 * tf;
    const double expectedA = b * h - bi * hi;
    const double expectedIy = (b * std::pow(h, 3) - bi * std::pow(hi, 3)) / 12.0;
    TEST_ASSERT(std::abs(rhs.A - expectedA) < 1e-9, "Area RHS exata");
    TEST_ASSERT(std::abs(rhs.Iy - expectedIy) < 1e-10, "Iy RHS exato");
    TEST_ASSERT(rhs.J > 0.0 && rhs.Asy > 0.0 && rhs.Asz > 0.0, "Torcao Bredt e cisalhamento positivos");

    // 2. Perfil C / U, T, I soldado assimétrico e Cantoneira L
    CrossSection chan = CrossSection::makeChannelC(11, "U200", 0.20, 0.08, 0.012, 0.008);
    TEST_ASSERT(chan.A > 0.0 && chan.Iy > chan.Iz && chan.Cw > 0.0, "Canal C com empenamento positivo");

    CrossSection tee = CrossSection::makeTShape(12, "T200", 0.20, 0.15, 0.014, 0.010);
    TEST_ASSERT(tee.A > 0.0 && tee.Iy > 0.0, "Perfil T valido");

    CrossSection welded = CrossSection::makeWeldedIShape(13, "VS400", 0.40, 0.22, 0.016, 0.18, 0.012, 0.008);
    TEST_ASSERT(welded.A > 0.0 && welded.Iy > 0.0 && welded.Cw > 0.0, "Perfil I soldado assimetrico valido");

    CrossSection ang = CrossSection::makeAngleL(14, "L100x100x10", 0.10, 0.10, 0.01);
    TEST_ASSERT(std::abs(ang.Iyz) > 1e-9, "Cantoneira L possui produto de inercia nao nulo");
    TEST_ASSERT(ang.I1 > ang.I2 && ang.I2 > 0.0, "Momentos principais I1 > I2 positivos na cantoneira L");

    // 3. Polígono fechado com furo (tubo quadrado 0.20x0.20 furo 0.16x0.16 via Teorema de Green)
    kernel::PolygonLoop2D outer{{{-0.10, -0.10}, {0.10, -0.10}, {0.10, 0.10}, {-0.10, 0.10}}, false};
    kernel::PolygonLoop2D hole{{{-0.08, -0.08}, {0.08, -0.08}, {0.08, 0.08}, {-0.08, 0.08}}, true};
    CrossSection poly = CrossSection::makePolygon(15, "Caixa Poligonal", {outer, hole});
    const double expPolyA = 0.20 * 0.20 - 0.16 * 0.16;
    const double expPolyI = (std::pow(0.20, 4) - std::pow(0.16, 4)) / 12.0;
    TEST_ASSERT(std::abs(poly.A - expPolyA) < 1e-9, "Area poligonal com furo exata via Teorema de Green");
    TEST_ASSERT(std::abs(poly.Iy - expPolyI) < 1e-10 && std::abs(poly.Iz - expPolyI) < 1e-10,
                "Inercia poligonal com furo exata");

    // 4. Seção mista/composta (Viga + Laje com razão modular n=1)
    CrossSection comp = CrossSection::makeComposite(16, "Mista", {
        {0.0, 0.0, 0.20, 0.40, 1.0},
        {0.0, 0.25, 1.00, 0.10, 0.5}
    });
    TEST_ASSERT(comp.A > 0.20 * 0.40 && comp.Iy > (0.20 * std::pow(0.40, 3) / 12.0),
                "Secao mista aumenta A e Iy pelo Teorema de Steiner");
}

void testTimoshenkoDeepAndSlenderLockingFree() {
    // 1. Viga curta/alta (L = 1.0 m, b = 0.2 m, h = 0.4 m -> L/h = 2.5!) biapoiada UDL ou ponta em balanço
    // Em balanço L=1.0m, carga na ponta Pz = -100 kN:
    // Deflexão analítica exata de Timoshenko:
    // w = P*L^3/(3*E*Iy) + P*L/(G*Asz)
    const double L = 1.0, b = 0.20, h = 0.40, P = -100.0;
    StructuralModel m;
    m.clear();
    m.addCrossSection(CrossSection::makeRectangular(10, "Viga Alta 20x40", b, h));

    NodeId n1 = m.addNode(0.0, 0.0, 0.0);
    NodeId n2 = m.addNode(L, 0.0, 0.0);
    m.findNode(n1)->support.fixed = {true, true, true, true, true, true};
    m.findNode(n2)->load.force.z = P;

    ElementId eid = m.addElement(n1, n2, 1, 10);
    m.findElement(eid)->formulation = ElementFormulation::Timoshenko3D;

    SolverResult resTimo = FemSolver3D::solve(m);
    TEST_ASSERT(resTimo.success && resTimo.equilibrium.isBalanced, "Timoshenko viga alta deve convergir");

    const Material* mat = m.findMaterial(1);
    const CrossSection* sec = m.findCrossSection(10);
    const double wBend = (std::abs(P) * std::pow(L, 3)) / (3.0 * mat->E * sec->Iy);
    const double wShear = (std::abs(P) * L) / (mat->G() * sec->Asz);
    const double wExactTimo = wBend + wShear;
    const double wCalc = std::abs(m.findNode(n2)->displacement[2]);
    TEST_ASSERT(std::abs(wCalc - wExactTimo) / wExactTimo < 1e-8,
                "Deflexao exata de Timoshenko (flexao + cortante) em 1 elemento");
    TEST_ASSERT(wCalc > wBend * 1.12, "Parcela de cisalhamento deve ser > 12% em viga L/h=2.5");

    // 2. Viga biapoiada com UDL em Timoshenko (1 elemento unico!) -> verificar interpolação interna no meio do vão xi=0.5!
    StructuralModel mUdl;
    mUdl.clear();
    mUdl.addCrossSection(CrossSection::makeRectangular(10, "Viga Alta", b, h));
    NodeId u1 = mUdl.addNode(0.0, 0.0, 0.0);
    NodeId u2 = mUdl.addNode(1.0, 0.0, 0.0);
    mUdl.findNode(u1)->support.fixed = {true, true, true, true, false, true};
    mUdl.findNode(u2)->support.fixed = {false, true, true, false, false, true};
    ElementId eu = mUdl.addElement(u1, u2, 1, 10);
    auto* elU = mUdl.findElement(eu);
    elU->formulation = ElementFormulation::Timoshenko3D;
    elU->distLoad.active = true;
    elU->distLoad.coordSystem = CoordSystem::Global;
    elU->distLoad.w = {0.0, 0.0, -50.0};

    SolverResult rUdl = FemSolver3D::solve(mUdl);
    TEST_ASSERT(rUdl.success, "Timoshenko biapoiada UDL");
    const double wMidExact = (5.0 * 50.0 * std::pow(1.0, 4)) / (384.0 * mat->E * sec->Iy) +
                             (50.0 * 1.0 * 1.0) / (8.0 * mat->G() * sec->Asz);
    const double wMidCalc = std::abs(mUdl.findElement(eu)->evaluateAt(0.5, mUdl).defLocal.z);
    TEST_ASSERT(std::abs(wMidCalc - wMidExact) / wMidExact < 1e-8,
                "Bolha de cisalhamento UDL em 1 elemento Timoshenko deve dar flecha exata no centro");

    // 3. Teste anti-shear-locking: viga ultra-esbelta L/h = 10000 (h = 0.001 m, L = 10 m) em Timoshenko
    StructuralModel mThin;
    mThin.clear();
    mThin.addCrossSection(CrossSection::makeRectangular(11, "Fina", 0.01, 0.001));
    NodeId t1 = mThin.addNode(0.0, 0.0, 0.0);
    NodeId t2 = mThin.addNode(10.0, 0.0, 0.0);
    mThin.findNode(t1)->support.fixed = {true, true, true, true, true, true};
    mThin.findNode(t2)->load.force.z = -1e-4;
    ElementId et = mThin.addElement(t1, t2, 1, 11);
    mThin.findElement(et)->formulation = ElementFormulation::Timoshenko3D;
    SolverResult rThin = FemSolver3D::solve(mThin);
    TEST_ASSERT(rThin.success, "Viga ultra-esbelta Timoshenko deve convergir");

    const CrossSection* secThin = mThin.findCrossSection(11);
    const double wThinEB = (1e-4 * std::pow(10.0, 3)) / (3.0 * mat->E * secThin->Iy);
    const double wThinCalc = std::abs(mThin.findNode(t2)->displacement[2]);
    TEST_ASSERT(std::abs(wThinCalc - wThinEB) / wThinEB < 1e-4,
                "Timoshenko deve estar 100% livre de shear locking para L/h=10000");
}

void testTruss3DAndSerialization() {
    StructuralModel m;
    m.clear();
    NodeId n1 = m.addNode(0.0, 0.0, 0.0);
    NodeId n2 = m.addNode(3.0, 0.0, 0.0);
    NodeId n3 = m.addNode(1.5, 0.0, 2.0);
    m.findNode(n1)->support.fixed = {true, true, true, false, false, false};
    m.findNode(n2)->support.fixed = {false, true, true, false, false, false};
    m.findNode(n3)->support.fixed = {false, true, false, false, false, false};
    m.findNode(n3)->load.force.z = -40.0;

    for (auto [a, b] : std::array<std::pair<NodeId, NodeId>, 3>{{{n1, n2}, {n1, n3}, {n2, n3}}}) {
        ElementId eid = m.addElement(a, b, 1, 3);
        m.findElement(eid)->formulation = ElementFormulation::Truss3D;
    }

    SolverResult res = FemSolver3D::solve(m);
    TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Elemento Truss3D nativo deve resolver trelica sem releases artificiais");

    // Roundtrip JSON para formulação e novas seções
    m.addCrossSection(CrossSection::makeBoxRHS(20, "RHS", 0.25, 0.15, 0.01, 0.008));
    QTemporaryDir tmp;
    const QString path = tmp.filePath("phase2.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(path, m), "Save Fase 2 deve funcionar");

    StructuralModel reloaded;
    TEST_ASSERT(ProjectSerializer::loadFromFile(path, reloaded), "Load Fase 2 deve funcionar");
    TEST_ASSERT(reloaded.findElement(1)->formulation == ElementFormulation::Truss3D, "Formulacao Truss3D preservada");
    TEST_ASSERT(reloaded.findCrossSection(20)->shape == SectionShape::BoxRHS, "Forma BoxRHS preservada");
}

} // namespace

int main() {
    testExtendedCrossSections();
    testTimoshenkoDeepAndSlenderLockingFree();
    testTruss3DAndSerialization();

    std::cout << "[OK] test_Phase2SectionsAnd1D passou com sucesso." << std::endl;
    return 0;
}
