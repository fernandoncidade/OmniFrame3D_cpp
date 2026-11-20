#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include "io/ProjectSerializer.hpp"
#include "kernel/materials/LinearElasticMaterial.hpp"
#include "kernel/postprocessing/PostProcessor.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

using namespace omniframe;
using namespace omniframe::kernel;

namespace {

void testConstitutiveAndPostProcessor() {
    auto steel = LinearElasticIsotropic::steelAstmA36(1);
    TEST_ASSERT(steel.isValid(), "Material isotropico valido");
    TEST_ASSERT(std::abs(steel.densityKgM3() - 7850.0) < 1e-6, "rho em kg/m3 distinto de gamma");
    TEST_ASSERT(std::abs(steel.specificWeightKNM3() - 78.5) < 1e-6, "gamma em kN/m3 distinto de rho");
    TEST_ASSERT(std::abs(steel.consistentMassDensity() - 7.85) < 1e-9, "rho consistente em t/m3 = kN*s2/m4");

    const auto D3 = steel.constitutiveMatrix3D();
    TEST_ASSERT(D3.isApprox(D3.transpose(), 1e-9), "Tensor 3D deve ser simetrico");

    LinearElasticOrthotropic ortho(2, "CFRP", 1.4e8, 1.0e7, 1.0e7, 0.3, 0.3, 0.3, 5.0e6, 3.5e6, 5.0e6,
                                   1e-6, 3e-5, 3e-5, 1600.0, 15.7);
    TEST_ASSERT(ortho.isValid(), "Material ortotropico fisicamente consistente deve validar");

    // Invariantes de tensão: tração uniaxial sigma_x = 100
    Eigen::Matrix<double, 6, 1> s;
    s << 100.0, 0.0, 0.0, 0.0, 0.0, 0.0;
    const auto inv = PostProcessor::computeStressInvariants(s);
    TEST_ASSERT(std::abs(inv.sigma1 - 100.0) < 1e-9 && std::abs(inv.vonMises - 100.0) < 1e-9,
                "Invariantes uniaxiais corretos");
}

void testAllLinearSolversConsistency() {
    const std::array<LinearSolverType, 6> types{
        LinearSolverType::Auto,
        LinearSolverType::SparseLU,
        LinearSolverType::SimplicialLDLT,
        LinearSolverType::SimplicialLLT,
        LinearSolverType::ConjugateGradient,
        LinearSolverType::BiCGSTAB
    };

    double refDisp = -1.0;
    for (auto st : types) {
        StructuralModel m = StructuralModel::createBuildingFrameExample();
        LinearSolverOptions opts;
        opts.type = st;
        opts.tolerance = 1e-12;
        SolverResult res = FemSolver3D::solveWithOptions(m, opts);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Todos os solvers lineares devem convergir no portico 3D");
        TEST_ASSERT(res.solverStats.relativeResidual < 1e-8, "Residuo relativo do solver deve ser pequeno");

        if (refDisp < 0.0) refDisp = res.maxNodalDisplacement;
        else {
            TEST_ASSERT(std::abs(res.maxNodalDisplacement - refDisp) / refDisp < 1e-6,
                        "Todos os solvers devem concordar entre si");
        }
    }

    // Treliça com todos os solvers simétricos/iterativos também
    for (auto st : {LinearSolverType::SimplicialLDLT, LinearSolverType::SimplicialLLT, LinearSolverType::ConjugateGradient}) {
        StructuralModel truss = StructuralModel::createSpaceTrussExample();
        LinearSolverOptions opts;
        opts.type = st;
        opts.tolerance = 1e-12;
        SolverResult res = FemSolver3D::solveWithOptions(truss, opts);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced, "Solvers SPD devem resolver trelica 3D com DOFs inativos removidos");
    }
}

void testMpcRigidLinkAndDiaphragm() {
    // Pórtico com dois pilares idênticos em x=0 e x=4 conectados por diafragma rígido no nível Z=3 (t1 e t2)
    StructuralModel m;
    m.clear();
    m.addCrossSection(CrossSection::makeGeneric(10, "Col", 0.09, 2.0e-4, 2.0e-4, 1.0e-4));
    NodeId b1 = m.addNode(0.0, 0.0, 0.0);
    NodeId t1 = m.addNode(0.0, 0.0, 3.0);
    NodeId b2 = m.addNode(4.0, 0.0, 0.0);
    NodeId t2 = m.addNode(4.0, 0.0, 3.0);
    m.findNode(b1)->support.fixed = {true, true, true, true, true, true};
    m.findNode(b2)->support.fixed = {true, true, true, true, true, true};

    m.addElement(b1, t1, 1, 10);
    m.addElement(b2, t2, 1, 10);

    // Carga horizontal aplicada APENAS em t1 (mestre do diafragma)
    m.findNode(t1)->load.force.x = 20.0;
    RigidDiaphragmSpec dia;
    dia.masterNode = t1;
    dia.slaveNodes = {t2};
    dia.normalAxis = 2; // plano XY
    m.getRigidDiaphragms().push_back(dia);

    SolverResult res = FemSolver3D::solve(m);
    TEST_ASSERT(res.success, "Diafragma rigido entre dois pilares deve resolver sem rigidez artificial");

    const double ux1 = m.findNode(t1)->displacement[0];
    const double ux2 = m.findNode(t2)->displacement[0];
    TEST_ASSERT(std::abs(ux1 - ux2) < 1e-10, "Diafragma rigido deve igualar uX do escravo ao mestre");
    TEST_ASSERT(std::abs(m.findNode(b1)->reaction[0] + m.findNode(b2)->reaction[0] + 20.0) < 1e-6,
                "Dois pilares devem dividir exatamente a forca horizontal aplicada no diafragma");
    TEST_ASSERT(std::abs(m.findNode(b1)->reaction[0] - m.findNode(b2)->reaction[0]) < 1e-6,
                "Pilares identicos ligados por diafragma rigido devem absorver exatamente 50%/50% da carga lateral");
}

void testLoadCasesCombinationsCacheAndSchemaV2() {
    StructuralModel m = StructuralModel::createBuildingFrameExample();

    LoadCase deadCase;
    deadCase.id = 1;
    deadCase.name = "G1 - Peso Proprio + Permanente";
    deadCase.category = LoadCaseCategory::Dead;
    deadCase.selfWeight.enabled = true;
    deadCase.selfWeight.factor = 1.0;
    deadCase.selfWeight.direction = {0.0, 0.0, -1.0};
    m.addLoadCase(deadCase);

    LoadCase windCase;
    windCase.id = 2;
    windCase.name = "W1 - Vento X";
    windCase.category = LoadCaseCategory::Wind;
    windCase.nodalLoads.push_back({5, {20.0, 0, 0, 0, 0, 0}});
    windCase.nodalLoads.push_back({8, {20.0, 0, 0, 0, 0, 0}});
    windCase.nodalLoads.push_back({9, {30.0, 0, 0, 0, 0, 0}});
    windCase.nodalLoads.push_back({12, {30.0, 0, 0, 0, 0, 0}});
    m.addLoadCase(windCase);

    LoadCombination elu;
    elu.id = 1;
    elu.name = "ELU Normal (1.4G + 1.4W)";
    elu.limitState = LimitStateType::ELU_Normal;
    elu.factors = {{1, 1.4}, {2, 1.4}};
    m.addLoadCombination(elu);

    LoadCombination els;
    els.id = 2;
    els.name = "ELS Rara (1.0G + 0.6W)";
    els.limitState = LimitStateType::ELS_Rare;
    els.factors = {{1, 1.0}, {2, 0.6}};
    m.addLoadCombination(els);

    m.solverOptions().type = LinearSolverType::SimplicialLDLT;

    std::vector<CaseDisplacementResult> caseRes, comboRes;
    EnvelopeExtremes env;
    std::string err;
    TEST_ASSERT(FemSolver3D::solveLoadCasesAndCombinations(m, m.solverOptions(), caseRes, comboRes, env, &err),
                "Multi-caso e multi-combinacao deve resolver");
    TEST_ASSERT(caseRes.size() == 2 && comboRes.size() == 2, "Deve produzir 2 casos e 2 combinacoes");
    TEST_ASSERT(comboRes[0].solverStats.reusedFactorization, "Combinacao deve reutilizar fatoracao esparsa cacheada");

    // Superposição exata: U_elu == 1.4*U_1 + 1.4*U_2
    const VecX uExpected = 1.4 * caseRes[0].U_full + 1.4 * caseRes[1].U_full;
    const double diff = (comboRes[0].U_full - uExpected).norm();
    TEST_ASSERT(diff < 1e-9, "Combinacao linear usando cache de fatoracao deve igualar superposicao exata");

    // Envoltória deve conter extremos
    const double minZ = env.minNodeDisp[9][2];
    const double maxZ = env.maxNodeDisp[9][2];
    TEST_ASSERT(minZ <= maxZ, "Envoltoria deve respeitar min <= max");

    // Serialização Schema 2.0.0 + Preservação de campos desconhecidos
    QTemporaryDir tmp;
    const QString pathV2 = tmp.filePath("schema2.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(pathV2, m), "Save Schema 2.0.0 deve funcionar");

    StructuralModel reloaded;
    TEST_ASSERT(ProjectSerializer::loadFromFile(pathV2, reloaded), "Load Schema 2.0.0 deve funcionar");
    TEST_ASSERT(reloaded.getLoadCases().size() == 2 && reloaded.getLoadCombinations().size() == 2,
                "Roundtrip Schema 2.0.0 deve preservar casos e combinacoes");
    TEST_ASSERT(reloaded.solverOptions().type == LinearSolverType::SimplicialLDLT,
                "Roundtrip Schema 2.0.0 deve preservar opcoes de solver");

    // Injeta campo desconhecido futuro para verificar preservação sem perda
    QFile rf(pathV2);
    TEST_ASSERT(rf.open(QIODevice::ReadOnly), "Abrir schema2.omniframe");
    QJsonObject obj = QJsonDocument::fromJson(rf.readAll()).object();
    rf.close();
    QJsonObject futureObj;
    futureObj["futureTag"] = "phase9-experiment";
    obj["unknownFutureBlock"] = futureObj;

    const QString pathFuture = tmp.filePath("future.omniframe");
    QFile wf(pathFuture);
    TEST_ASSERT(wf.open(QIODevice::WriteOnly), "Escrever future.omniframe");
    wf.write(QJsonDocument(obj).toJson());
    wf.close();

    StructuralModel mFuture;
    TEST_ASSERT(ProjectSerializer::loadFromFile(pathFuture, mFuture), "Arquivo com campos futuros desconhecidos deve carregar");
    TEST_ASSERT(!mFuture.serializationWarnings().empty(), "Deve emitir aviso sobre campos desconhecidos preservados");

    const QString pathResaved = tmp.filePath("future_resaved.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(pathResaved, mFuture), "Resave deve funcionar");
    QFile checkF(pathResaved);
    TEST_ASSERT(checkF.open(QIODevice::ReadOnly), "Reabrir future_resaved");
    QJsonObject checkObj = QJsonDocument::fromJson(checkF.readAll()).object();
    TEST_ASSERT(checkObj.contains("unknownFutureBlock"), "Campo desconhecido futuro deve permanecer preservado apos salvar novamente");
}

void testAud011MpcSupportConflictAndExternalReactions() {
    // 1. Conflito entre apoio Dirichlet e DOF escravo de MPC deve ser rejeitado na validacao e no solver
    {
        StructuralModel m;
        m.clear();
        const NodeId n1 = m.addNode(0.0, 0.0, 0.0);
        const NodeId n2 = m.addNode(2.0, 0.0, 0.0);
        m.findNode(n1)->support = SupportRestraint::makeFixed();
        m.findNode(n2)->support.fixed[0] = true; // Conflito com RigidLink no DOF 0 escravo
        m.addElement(n1, n2, 1, 1);
        m.getRigidLinks().push_back({n1, n2, true});

        const ModelValidation val = m.validate();
        TEST_ASSERT(!val.valid, "Modelo com apoio Dirichlet em no escravo de RigidLink deve ser invalido");
        bool foundConflictCode = false;
        for (const auto& iss : val.issues) {
            if (iss.code == "ERR_MPC_SUPPORT_CONFLICT") foundConflictCode = true;
        }
        TEST_ASSERT(foundConflictCode, "validate() deve emitir ERR_MPC_SUPPORT_CONFLICT");
    }

    // 2. Reacoes externas com MPC (T_mpc^T * R_full): escravos livres devem ter reacao externa zero e mestre fixo recebe total
    {
        StructuralModel m;
        m.clear();
        const NodeId nBase = m.addNode(0.0, 0.0, 0.0);
        const NodeId nMaster = m.addNode(0.0, 0.0, 3.0);
        const NodeId nSlave = m.addNode(2.0, 0.0, 3.0);
        m.findNode(nBase)->support = SupportRestraint::makeFixed();
        m.findNode(nMaster)->support = SupportRestraint::makeFixed();
        m.findNode(nSlave)->load.force.x = 25.0;
        m.findNode(nSlave)->load.force.y = -40.0;
        m.addElement(nBase, nMaster, 1, 1);
        m.addElement(nBase, nSlave, 1, 1);
        m.getRigidDiaphragms().push_back({nMaster, {nSlave}, 2});

        const SolverResult res = FemSolver3D::solve(m);
        TEST_ASSERT(res.success && res.equilibrium.isBalanced,
                    "Modelo com diafragma rigido e mestre engastado deve fechar equilibrio estatico global");
        for (int d = 0; d < 6; ++d) {
            TEST_ASSERT(std::abs(m.findNode(nSlave)->reaction[d]) < 1e-12,
                        "No escravo livre de MPC nao deve reportar forca interna de vinculo como reacao de apoio");
        }
        const double totalRx = m.findNode(nBase)->reaction[0] + m.findNode(nMaster)->reaction[0];
        const double totalRy = m.findNode(nBase)->reaction[1] + m.findNode(nMaster)->reaction[1];
        TEST_ASSERT(std::abs(totalRx + 25.0) < 1e-5, "Somatorio de Rx nos apoios reais deve equilibrar Fx aplicado no escravo");
        TEST_ASSERT(std::abs(totalRy - 40.0) < 1e-5, "Somatorio de Ry nos apoios reais deve equilibrar Fy aplicado no escravo");
    }
}

} // namespace

int main() {
    testConstitutiveAndPostProcessor();
    testAllLinearSolversConsistency();
    testMpcRigidLinkAndDiaphragm();
    testLoadCasesCombinationsCacheAndSchemaV2();
    testAud011MpcSupportConflictAndExternalReactions();

    std::cout << "[OK] test_Phase1KernelAndSolvers passou com sucesso." << std::endl;
    return 0;
}
