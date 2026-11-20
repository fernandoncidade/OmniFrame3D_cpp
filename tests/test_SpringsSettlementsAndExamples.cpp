/**
 * @file test_SpringsSettlementsAndExamples.cpp
 * @brief Auditoria de Molas Elasticas, Recalques Prescritos, 3 Exemplos Prontos, Undo/Redo, Nos Isolados e Relatorio Markdown
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include "io/ProjectSerializer.hpp"
#include <QDir>
#include <QFile>
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

void testElasticSpringFoundation() {
    // Viga apoiada exclusivamente sobre molas elasticas nos apoios (sem fixed[d])!
    StructuralModel model;
    Material mat = Material::makeStructuralSteel(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 20x40", 0.20, 0.40);
    MaterialId mid = model.addMaterial(mat);
    SectionId sid = model.addCrossSection(sec);

    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(6.0, 0.0, 0.0);

    auto* p1 = model.findNode(n1);
    auto* p2 = model.findNode(n2);
    // Molas em todos os 6 DOFs sem fixed[d] para validar pure-spring support e reacoes -k*u
    for (int d = 0; d < 6; ++d) {
        p1->support.springStiffness[d] = 10000.0;
        p2->support.springStiffness[d] = 10000.0;
    }

    ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);
    Element* elem = model.findElement(eid);
    elem->distLoad.active = true;
    elem->distLoad.coordSystem = CoordSystem::Global;
    elem->distLoad.w = {0.0, 0.0, -10.0}; // -60 kN total

    ModelValidation val = model.validate();
    TEST_ASSERT(val.valid, "Modelo exclusivamente em molas deve passar na validacao");

    SolverResult res = FemSolver3D::solve(model);
    TEST_ASSERT(res.success, "Solver deve convergir com apoio exclusivamente por molas");
    TEST_ASSERT(res.equilibrium.isBalanced, "Equilibrio global deve fechar com reacoes de molas R=-k*u");

    // Cada apoio recebe metade da carga (-30 kN) -> uZ = -30 / 10000 = -0.003 m, Rz = +30 kN
    assertNear(p1->displacement[2], -0.003, 1e-6, "Deslocamento vertical uZ na mola 1");
    assertNear(p2->displacement[2], -0.003, 1e-6, "Deslocamento vertical uZ na mola 2");
    assertNear(p1->reaction[2], +30.0, 1e-4, "Reacao elastica Rz na mola 1");
    assertNear(p2->reaction[2], +30.0, 1e-4, "Reacao elastica Rz na mola 2");
}

void testPrescribedSettlementPrecedence() {
    StructuralModel model;
    Material mat = Material::makeConcreteC30(10);
    CrossSection sec = CrossSection::makeRectangular(10, "Ret 20x50", 0.20, 0.50);
    MaterialId mid = model.addMaterial(mat);
    SectionId sid = model.addCrossSection(sec);

    const double L = 4.0;
    const double delta = -0.015; // -15 mm recalque vertical
    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(L, 0.0, 0.0);

    model.findNode(n1)->support = SupportRestraint::makeFixed();
    auto* p2 = model.findNode(n2);
    p2->support = SupportRestraint::makeFixed();
    // Mesmo que fixed[2] esteja true, hasPrescribed[2] deve prevalecer!
    p2->support.hasPrescribed[2] = true;
    p2->support.prescribedValue[2] = delta;

    ElementId eid = model.addElement(n1, n2, mid, sid, 0.0);

    SolverResult res = FemSolver3D::solve(model);
    TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Viga com recalque prescrito desequilibrada");
    assertNear(p2->displacement[2], delta, 1e-9, "Deslocamento prescrito uZ nao aplicado");

    // Reacao vertical no apoio esquerdo = -12*E*Iy*delta / L^3 (positiva para cima se delta < 0!)
    double expectedVz = -(12.0 * mat.E * sec.Iy * delta) / std::pow(L, 3);
    assertNear(model.findNode(n1)->reaction[2], +expectedVz, 1e-2, "Reacao Rz por recalque na extremidade oposta");
    assertNear(model.findNode(n2)->reaction[2], -expectedVz, 1e-2, "Reacao Rz no apoio recalcado");
    assertNear(model.findElement(eid)->evaluateAt(0.0, model).Vz, -expectedVz, 1e-2, "Cortante por recalque");
}

void testAllThreeBuiltInExamples() {
    // 1. Edificio 3D (Concreto C30, Pilares 30x30, Vigas 20x50)
    {
        StructuralModel m1 = StructuralModel::createBuildingFrameExample();
        ModelValidation v1 = m1.validate();
        TEST_ASSERT(v1.valid && v1.warnings.empty(), "Exemplo 1 deve validar sem erros nem avisos");

        const Element& col = m1.getElements().front();
        const Material* mat = m1.findMaterial(col.materialId);
        const CrossSection* secCol = m1.findCrossSection(col.sectionId);
        TEST_ASSERT(mat && mat->name.find("Concreto") != std::string::npos, "Exemplo 1 deve usar material Concreto C30");
        TEST_ASSERT(secCol && secCol->name == "Pilar 30x30", "Pilares do Exemplo 1 devem usar Pilar 30x30");

        SolverResult r1 = FemSolver3D::solve(m1);
        TEST_ASSERT(r1.success && r1.equilibrium.isBalanced, "Exemplo 1 deve convergir em equilibrio perfeito");
        TEST_ASSERT(r1.maxNodalDisplacement > 1e-5, "Exemplo 1 deve deformar sob vento + carga distribuida");
    }

    // 2. Portico L Engastado em Balanco
    {
        StructuralModel m2 = StructuralModel::createCantileverLFrameExample();
        ModelValidation v2 = m2.validate();
        TEST_ASSERT(v2.valid && v2.warnings.empty(), "Exemplo 2 deve validar sem erros nem avisos");
        SolverResult r2 = FemSolver3D::solve(m2);
        TEST_ASSERT(r2.success && r2.equilibrium.isBalanced, "Exemplo 2 deve convergir em equilibrio perfeito");
    }

    // 3. Passarela Trelicada Espacial 3D
    {
        StructuralModel m3 = StructuralModel::createSpaceTrussExample();
        ModelValidation v3 = m3.validate();
        TEST_ASSERT(v3.valid && v3.warnings.empty(), "Exemplo 3 deve validar sem erros nem avisos");

        SolverResult r3 = FemSolver3D::solve(m3);
        TEST_ASSERT(r3.success, "Exemplo 3 trelicado espacial nao pode ter matriz singular");
        TEST_ASSERT(r3.equilibrium.isBalanced, "Exemplo 3 trelicado espacial deve estar equilibrado");
        TEST_ASSERT(r3.maxNodalDisplacement > 1e-5 && r3.maxNodalDisplacement < 0.1, "Flecha do Exemplo 3 fora de faixa fisica");

        // Modifica coordenada de um no e re-resolve verificando alteracao real + clearResults + undo
        double oldDisp = r3.maxNodalDisplacement;
        m3.clearResults();
        TEST_ASSERT(!m3.getElements()[0].hasResults, "clearResults deve limpar flag hasResults");

        m3.findNode(15)->position.z = 2.6; // Aumenta altura no topo no meio da ponte
        m3.pushUndo();
        TEST_ASSERT(m3.canUndo(), "Deve permitir Undo apos editar coordenada");

        SolverResult r3mod = FemSolver3D::solve(m3);
        TEST_ASSERT(r3mod.success && r3mod.equilibrium.isBalanced, "Exemplo 3 editado deve convergir");
        TEST_ASSERT(std::abs(r3mod.maxNodalDisplacement - oldDisp) > 1e-6, "Editar geometria deve mudar resposta FEA");

        TEST_ASSERT(m3.undo(), "Undo deve restaurar coordenada original");
        assertNear(m3.findNode(15)->position.z, 2.0, 1e-9, "Coordenada Z restaurada pelo Undo");

        // Exporta relatorio Markdown e valida tabela de esforcos nas barras
        QString mdPath = QDir::tempPath() + "/omniframe_test_report.md";
        TEST_ASSERT(ProjectSerializer::exportTechnicalReportMarkdown(mdPath, m3, r3), "Falha ao exportar relatorio Markdown");
        QFile mdFile(mdPath);
        TEST_ASSERT(mdFile.open(QIODevice::ReadOnly | QIODevice::Text), "Nao leu arquivo Markdown");
        QString content = QString::fromUtf8(mdFile.readAll());
        mdFile.close();
        QFile::remove(mdPath);
        TEST_ASSERT(content.contains("N min/max (kN)"), "Relatorio Markdown deve incluir extremos de esforcos nas barras");
    }
}

void testIsolatedNodeValidation() {
    StructuralModel model;
    NodeId n1 = model.addNode(0.0, 0.0, 0.0);
    NodeId n2 = model.addNode(4.0, 0.0, 0.0);
    model.addNode(2.0, 2.0, 0.0); // No isolado 3
    model.findNode(n1)->support = SupportRestraint::makeFixed();
    model.addElement(n1, n2, 1, 1);

    ModelValidation val = model.validate();
    TEST_ASSERT(!val.valid, "No isolado sem barra conectada deve invalidar modelo");
}

} // namespace

int main() {
    testElasticSpringFoundation();
    testPrescribedSettlementPrecedence();
    testAllThreeBuiltInExamples();
    testIsolatedNodeValidation();
    std::cout << "[SUCESSO] test_SpringsSettlementsAndExamples validado 100%.\n";
    return 0;
}

