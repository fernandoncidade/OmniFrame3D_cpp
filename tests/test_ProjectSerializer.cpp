/**
 * @file test_ProjectSerializer.cpp
 * @brief Teste de Serialização e Desserialização de Projetos (.omni JSON)
 */

#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/AppLogger.hpp"
#include "io/ProjectRecoveryStore.hpp"
#include "io/ProjectSerializer.hpp"
#include "io/UserPreferencesStore.hpp"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTextStream>

using namespace omniframe;

namespace {

void testSchema21AndBackwardCompat() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir invalido");

    StructuralModel m;
    m.metadata().projectName = "Torre Corporativa Schema 2.1";
    m.metadata().engineerName = "Eng. Estrutural Silva";
    m.metadata().clientName = "Incorporadora Alfa";
    m.metadata().siteLocation = "Sao Paulo - SP";
    m.metadata().soilSigmaAdm_kPa = 320.0;
    m.metadata().footingB_m = 2.2;
    m.metadata().footingL_m = 2.5;
    m.metadata().windV0_ms = 42.0;

    Material plasticMat = Material::makeStructuralSteel(m.getNextMaterialId());
    plasticMat.name = "Aco J2 S355";
    plasticMat.isPlastic = true;
    plasticMat.sigmaY = 355000.0;
    plasticMat.H_iso = 12000.0;
    plasticMat.H_kin = 4500.0;
    const MaterialId pmid = m.addMaterial(plasticMat);

    CrossSection compSec = CrossSection::makeComposite(
        m.getNextSectionId(),
        "Viga Mista Aco-Concreto",
        {
            {0.0, -0.15, 0.20, 0.30, 1.0},
            {0.0,  0.10, 0.80, 0.12, 0.15}
        });
    const SectionId csid = m.addCrossSection(compSec);

    const NodeId n1 = m.addNode(0.0, 0.0, 0.0);
    const NodeId n2 = m.addNode(5.0, 0.0, 0.0);
    m.findNode(n1)->support = SupportRestraint::makeFixed();
    m.addElement(n1, n2, pmid, csid);

    kernel::LoadCase lc;
    lc.id = 1;
    lc.name = "PP + Permanente";
    lc.category = kernel::LoadCaseCategory::Dead;
    lc.selfWeight.enabled = true;
    lc.selfWeight.factor = 1.0;
    m.addLoadCase(lc);

    kernel::LoadCombination comb;
    comb.id = 1;
    comb.name = "ELU Normal";
    comb.limitState = kernel::LimitStateType::ELU_Normal;
    comb.pDeltaEnabled = true;
    comb.factors.push_back({1, 1.40});
    m.addLoadCombination(comb);

    m.surfaceMesh() = kernel::MeshGenerator::generateStructuredQuadGrid(
        kernel::Vec3(0.0, 0.0, 0.0),
        kernel::Vec3(4.0, 0.0, 0.0),
        kernel::Vec3(0.0, 3.0, 0.0),
        2, 2, false);

    const QString path21 = tmp.filePath("schema21.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(path21, m), "Salvar projeto Schema 2.1.0");

    StructuralModel loaded21;
    TEST_ASSERT(ProjectSerializer::loadFromFile(path21, loaded21), "Carregar projeto Schema 2.1.0");
    TEST_ASSERT(loaded21.metadata().projectName == "Torre Corporativa Schema 2.1",
                "Metadados projectName preservados");
    TEST_ASSERT(std::abs(loaded21.metadata().soilSigmaAdm_kPa - 320.0) < 1e-9 &&
                    std::abs(loaded21.metadata().windV0_ms - 42.0) < 1e-9,
                "Parametros NBR em metadata preservados");
    const Material* lmat = loaded21.findMaterial(pmid);
    TEST_ASSERT(lmat && lmat->isPlastic && std::abs(lmat->sigmaY - 355000.0) < 1e-6 &&
                    std::abs(lmat->H_iso - 12000.0) < 1e-6 && std::abs(lmat->H_kin - 4500.0) < 1e-6,
                "Plasticidade J2 do material preservada no Schema 2.1.0");
    const CrossSection* lsec = loaded21.findCrossSection(csid);
    TEST_ASSERT(lsec && lsec->compositeParts.size() == 2 &&
                    std::abs(lsec->compositeParts[1].modularRatioN - 0.15) < 1e-9,
                "Partes compostas da secao preservadas no Schema 2.1.0");
    TEST_ASSERT(loaded21.surfaceMesh().nodes.size() == 9 && loaded21.surfaceMesh().elements.size() == 4,
                "Malha 2D/3D persistida e recarregada no Schema 2.1.0");
    TEST_ASSERT(loaded21.getLoadCases().size() == 1 && loaded21.getLoadCombinations().size() == 1 &&
                    loaded21.getLoadCombinations()[0].pDeltaEnabled,
                "Casos de carga e combinacoes P-Delta preservados");

    // Compatibilidade retroativa com Schema 1.0 minimo
    const QString path10 = tmp.filePath("legacy10.omni");
    {
        QFile f(path10);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Criar legacy Schema 1.0");
        f.write(R"json({
  "version": "1.0.0",
  "materials": [{"id": 1, "name": "Aco", "E": 200000000.0, "nu": 0.3, "alpha": 1.2e-5, "gamma": 78.5}],
  "sections": [{"id": 1, "name": "Ret", "shape": 0, "A": 0.08, "Iy": 0.001, "Iz": 0.0005, "J": 0.0002}],
  "nodes": [
    {"id": 1, "x": 0.0, "y": 0.0, "z": 0.0, "support": {"fixed": [true,true,true,true,true,true]}},
    {"id": 2, "x": 3.0, "y": 0.0, "z": 0.0}
  ],
  "elements": [{"id": 1, "nodeStart": 1, "nodeEnd": 2, "materialId": 1, "sectionId": 1}],
  "futureExtensionTag": {"experimental": 42}
})json");
    }
    StructuralModel loaded10;
    TEST_ASSERT(ProjectSerializer::loadFromFile(path10, loaded10),
                "Schema 1.0 legado deve continuar carregando sem falha");
    TEST_ASSERT(loaded10.getNodes().size() == 2 && !loaded10.serializationWarnings().empty(),
                "Campos desconhecidos devem gerar aviso e ser preservados");
    std::cout << "  - Schema 2.1.0 + compatibilidade retroativa Schema 1.0 [OK]\n";
}

void testUnifiedUserPreferencesStoreAndMigration() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir invalido para teste de UserPreferencesStore");

    {
        io::UserPreferencesStore::ScopedDirectoryOverride dirScope(tmp.path());

        // 1) Cria os 4 arquivos legados + user_preferences.json v1
        auto writeText = [&](const QString& name, const QByteArray& bytes) {
            QFile f(tmp.filePath(name));
            TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Falha ao criar arquivo de teste");
            TEST_ASSERT(f.write(bytes) == bytes.size(), "Falha ao escrever arquivo de teste");
        };

        writeText(QStringLiteral("graphics.json"), R"json({
  "preset": 2,
  "autoMaxResolution": false,
  "resolutionScale": 1.5,
  "autoMsaa": false,
  "requestedMsaa": 8,
  "vsyncEnabled": false
})json");
        writeText(QStringLiteral("language.json"), R"json({"language": "en_US"})json");
        writeText(QStringLiteral("theme.json"), R"json({"theme": "light"})json");
        writeText(QStringLiteral("results_tables.json"), R"json({"version": 1, "layoutCount": 3})json");
        writeText(QStringLiteral("user_preferences.json"), R"json({
  "version": 1,
  "app": {
    "lastDarkPlaneTheme": 2,
    "lastFileDialogDir": "C:/Projetos"
  },
  "workspace": {
    "workPlane": 2,
    "gridStep": 1.25
  }
})json");

        bool ok = false;
        const QJsonObject migrated = io::UserPreferencesStore::readRootObject(&ok);
        TEST_ASSERT(ok, "Migracao unificada deve suceder");
        TEST_ASSERT(migrated.value(QStringLiteral("version")).toInt() ==
                        io::UserPreferencesStore::kCurrentSchemaVersion,
                    "Esquema deve ser promovido para v2");
        TEST_ASSERT(migrated.value(QStringLiteral("graphics")).toObject().value(QStringLiteral("requestedMsaa")).toInt() == 8 &&
                        !migrated.value(QStringLiteral("graphics")).toObject().value(QStringLiteral("vsyncEnabled")).toBool(true),
                    "graphics.json legado deve migrar para secao graphics");
        TEST_ASSERT(migrated.value(QStringLiteral("app")).toObject().value(QStringLiteral("language")).toString() ==
                        QStringLiteral("en_US"),
                    "language.json legado deve migrar para app.language");
        TEST_ASSERT(migrated.value(QStringLiteral("app")).toObject().value(QStringLiteral("theme")).toString() ==
                        QStringLiteral("light"),
                    "theme.json legado deve migrar para app.theme");
        TEST_ASSERT(migrated.value(QStringLiteral("resultsPanel")).toObject().value(QStringLiteral("layoutCount")).toInt() == 3,
                    "results_tables.json legado deve migrar para resultsPanel");
        TEST_ASSERT(migrated.value(QStringLiteral("workspace")).toObject().value(QStringLiteral("workPlane")).toInt() == 2,
                    "Secao workspace existente de v1 deve ser preservada");

        for (const QString& legacyName : io::UserPreferencesStore::legacyPreferenceFileNames()) {
            TEST_ASSERT(!QFile::exists(tmp.filePath(legacyName)),
                        "Arquivo legado deve ser removido apos migracao atomica bem-sucedida");
        }
        TEST_ASSERT(QFile::exists(io::UserPreferencesStore::userPreferencesFilePath()),
                    "Apenas user_preferences.json deve permanecer");

        // 2) Atualizacao parcial nao pode apagar outras secoes
        TEST_ASSERT(io::UserPreferencesStore::updateSectionKey(
                        QStringLiteral("app"), QStringLiteral("language"), QStringLiteral("pt_BR")),
                    "Atualizacao parcial de app.language deve suceder");
        const QJsonObject afterPartial = io::UserPreferencesStore::readRootObject(&ok);
        TEST_ASSERT(ok &&
                        afterPartial.value(QStringLiteral("app")).toObject().value(QStringLiteral("language")).toString() ==
                            QStringLiteral("pt_BR") &&
                        afterPartial.value(QStringLiteral("app")).toObject().value(QStringLiteral("theme")).toString() ==
                            QStringLiteral("light") &&
                        afterPartial.value(QStringLiteral("graphics")).toObject().value(QStringLiteral("requestedMsaa")).toInt() == 8 &&
                        afterPartial.value(QStringLiteral("resultsPanel")).toObject().value(QStringLiteral("layoutCount")).toInt() == 3,
                    "Atualizacao parcial deve preservar todas as outras secoes e chaves");

        // 3) Recuperacao segura de JSON corrompido com backup .corrupt.bak
        writeText(QStringLiteral("user_preferences.json"), "{ invalid json !!!");
        bool corruptOk = true;
        const QJsonObject corruptRead = io::UserPreferencesStore::readRootObject(&corruptOk);
        TEST_ASSERT(!corruptOk && corruptRead.isEmpty(),
                    "Leitura de JSON corrompido deve falhar graciosamente sem lancar excecao");
        TEST_ASSERT(io::UserPreferencesStore::updateSectionKey(
                        QStringLiteral("app"), QStringLiteral("theme"), QStringLiteral("dark")),
                    "Escrita apos arquivo corrompido deve restaurar persistencia valida");
        TEST_ASSERT(QFile::exists(tmp.filePath(QStringLiteral("user_preferences.json.corrupt.bak"))),
                    "Arquivo corrompido deve ser preservado em .corrupt.bak antes da substituicao");
    }

    // 4) Migracao automatica do subdiretorio legado <dir>/OmniFrame/ -> <dir>/user_preferences.json
    {
        QTemporaryDir nestedTmp;
        TEST_ASSERT(nestedTmp.isValid(), "QTemporaryDir invalido para teste de migracao aninhada");
        io::UserPreferencesStore::ScopedDirectoryOverride dirScope(nestedTmp.path());

        const QString legacySubDir = io::UserPreferencesStore::legacyNestedPreferencesDirectory();
        TEST_ASSERT(QDir().mkpath(legacySubDir), "Criar subdiretorio legado OmniFrame");
        auto writeNested = [&](const QString& name, const QByteArray& bytes) {
            QFile f(QDir(legacySubDir).filePath(name));
            TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Falha ao criar legado aninhado");
            TEST_ASSERT(f.write(bytes) == bytes.size(), "Falha ao escrever legado aninhado");
        };
        writeNested(QStringLiteral("graphics.json"), R"json({"preset": 1, "requestedMsaa": 16})json");
        writeNested(QStringLiteral("language.json"), R"json({"language": "en_US"})json");
        writeNested(QStringLiteral("theme.json"), R"json({"theme": "light"})json");
        writeNested(QStringLiteral("results_tables.json"), R"json({"version": 1, "layoutCount": 2})json");
        writeNested(QStringLiteral("user_preferences.json"), R"json({
  "version": 1,
  "workspace": {"workPlane": 0, "gridStep": 0.75}
})json");

        bool nestedOk = false;
        const QJsonObject migratedNested = io::UserPreferencesStore::readRootObject(&nestedOk);
        TEST_ASSERT(nestedOk, "Migracao do subdiretorio legado OmniFrame deve suceder");
        TEST_ASSERT(QFile::exists(nestedTmp.filePath(QStringLiteral("user_preferences.json"))),
                    "Arquivo consolidado deve ser criado na raiz AppData/Local/OmniFrame");
        TEST_ASSERT(!QFileInfo::exists(legacySubDir),
                    "Subdiretorio legado vazio OmniFrame/OmniFrame deve ser removido apos migracao");
        TEST_ASSERT(migratedNested.value(QStringLiteral("app")).toObject().value(QStringLiteral("language")).toString() ==
                        QStringLiteral("en_US") &&
                        migratedNested.value(QStringLiteral("graphics")).toObject().value(QStringLiteral("preset")).toInt() == 1 &&
                        migratedNested.value(QStringLiteral("resultsPanel")).toObject().value(QStringLiteral("layoutCount")).toInt() == 2 &&
                        std::abs(migratedNested.value(QStringLiteral("workspace")).toObject().value(QStringLiteral("gridStep")).toDouble() - 0.75) < 1e-9,
                    "Todos os dados do subdiretorio legado devem ser preservados em user_preferences.json v2");
    }

    std::cout << "  - UserPreferencesStore (migracao, atomicidade e recuperacao) [OK]\n";
}

void testProjectRecoveryStoreAndSolverUndo() {
    QTemporaryDir recTmp;
    TEST_ASSERT(recTmp.isValid(), "QTemporaryDir para ProjectRecoveryStore");
    omniframe::io::UserPreferencesStore::ScopedDirectoryOverride dirOverride(recTmp.path());

    TEST_ASSERT(!omniframe::io::ProjectRecoveryStore::inspectSnapshot().available,
                "Snapshot de recuperacao nao deve existir inicialmente");

    StructuralModel edited;
    edited.solverOptions().type = kernel::LinearSolverType::Auto;
    edited.pushUndo();
    edited.solverOptions().type = kernel::LinearSolverType::SparseLU;
    edited.solverOptions().maxIterations = 1234;
    edited.metadata().projectName = "Edificio Recuperado";
    edited.metadata().windV0_ms = 45.0;
    const NodeId n1 = edited.addNode(0.0, 0.0, 0.0);
    const NodeId n2 = edited.addNode(5.0, 0.0, 3.0);
    if (Node* np1 = edited.findNode(n1)) {
        np1->support = SupportRestraint::makeFixed();
        np1->support.springStiffness[2] = 5000.0;
        np1->support.prescribedValue[2] = -0.005;
        np1->support.hasPrescribed[2] = true;
    }
    if (Node* np2 = edited.findNode(n2)) {
        np2->load.force = {12.0, -3.5, -40.0};
    }
    const ElementId e1 = edited.addElement(n1, n2, 1, 2, 0.25);
    if (Element* ep1 = edited.findElement(e1)) {
        ep1->releases.end[4] = true;
        ep1->distLoad.active = true;
        ep1->distLoad.w = {0.0, 0.0, -18.5};
        ep1->thermalLoad.active = true;
        ep1->thermalLoad.deltaT = 22.0;
    }
    edited.pushUndo();

    TEST_ASSERT(edited.undo(), "Undo deve reverter estado anterior");
    TEST_ASSERT(edited.solverOptions().type == kernel::LinearSolverType::SparseLU,
                "Undo intermediario mantem solverOptions do passo");
    // Desfaz ate o estado inicial antes da alteracao do solver
    while (edited.canUndo()) {
        edited.undo();
    }
    TEST_ASSERT(edited.solverOptions().type == kernel::LinearSolverType::Auto,
                "Undo completo deve restaurar solverOptions inicial");
    while (edited.canRedo()) {
        edited.redo();
    }
    TEST_ASSERT(edited.solverOptions().type == kernel::LinearSolverType::SparseLU &&
                    edited.solverOptions().maxIterations == 1234,
                "Redo deve restaurar solverOptions e edicoes posteriores");
    while (edited.canRedo()) {
        edited.redo();
    }

    const QString originPath = recTmp.filePath(QStringLiteral("projeto_original.omniframe"));
    TEST_ASSERT(omniframe::io::ProjectRecoveryStore::saveSnapshot(edited, originPath),
                "saveSnapshot deve gravar copia de recuperacao atomica");

    const omniframe::io::ProjectRecoveryInfo info = omniframe::io::ProjectRecoveryStore::inspectSnapshot();
    TEST_ASSERT(info.available && info.nodeCount == 2 && info.elementCount == 1 &&
                    info.projectFilePath == originPath,
                "inspectSnapshot deve reportar metadados exatos da sessao nao salva");

    StructuralModel recovered;
    omniframe::io::ProjectRecoveryInfo loadedInfo;
    TEST_ASSERT(omniframe::io::ProjectRecoveryStore::loadSnapshot(recovered, &loadedInfo),
                "loadSnapshot deve restaurar o modelo completo");
    TEST_ASSERT(recovered.getNodes().size() == 2 && recovered.getElements().size() == 1 &&
                    recovered.solverOptions().type == kernel::LinearSolverType::SparseLU &&
                    recovered.solverOptions().maxIterations == 1234 &&
                    recovered.metadata().projectName == "Edificio Recuperado" &&
                    std::abs(recovered.metadata().windV0_ms - 45.0) < 1e-6 &&
                    recovered.findNode(n1)->support.hasPrescribed[2] &&
                    std::abs(recovered.findNode(n1)->support.springStiffness[2] - 5000.0) < 1e-6 &&
                    recovered.findElement(e1)->thermalLoad.active &&
                    std::abs(recovered.findElement(e1)->thermalLoad.deltaT - 22.0) < 1e-6,
                "Todos os campos estruturais devem sobreviver a recuperacao de snapshot");

    omniframe::io::ProjectRecoveryStore::clearSnapshot();
    TEST_ASSERT(!omniframe::io::ProjectRecoveryStore::inspectSnapshot().available &&
                    !omniframe::io::ProjectRecoveryStore::inspectPreviousSession().available,
                "clearSnapshot deve remover arquivos de recuperacao e estado de sessao");

    // Testar continuidade de sessao limpa: Projeto Salvo e Exemplos pre-definidos
    StructuralModel exModel = StructuralModel::createCantileverLFrameExample();
    TEST_ASSERT(omniframe::io::ProjectRecoveryStore::saveCleanSessionState(
                    exModel, QString(), omniframe::io::SessionSourceKind::ExampleCantilever),
                "saveCleanSessionState deve registrar exemplo pre-definido sem rascunho de alteracoes");
    TEST_ASSERT(!omniframe::io::ProjectRecoveryStore::inspectSnapshot().available,
                "Exemplo limpo nao deve criar snapshot de alteracoes nao salvas");
    const omniframe::io::ProjectRecoveryInfo exSession =
        omniframe::io::ProjectRecoveryStore::inspectPreviousSession();
    TEST_ASSERT(exSession.available && !exSession.hasUnsavedChanges &&
                    exSession.sourceKind == omniframe::io::SessionSourceKind::ExampleCantilever &&
                    exSession.nodeCount == static_cast<int>(exModel.getNodes().size()) &&
                    exSession.elementCount == static_cast<int>(exModel.getElements().size()),
                "inspectPreviousSession deve identificar exemplo pre-definido aberto na sessao anterior");

    const QString savedProjPath = recTmp.filePath(QStringLiteral("portico_salvo.omniframe"));
    TEST_ASSERT(ProjectSerializer::saveToFile(savedProjPath, edited),
                "Salvar projeto de referencia para teste de continuidade de sessao");
    TEST_ASSERT(omniframe::io::ProjectRecoveryStore::saveCleanSessionState(
                    edited, savedProjPath, omniframe::io::SessionSourceKind::SavedProject),
                "saveCleanSessionState deve registrar projeto salvo ativo");
    const omniframe::io::ProjectRecoveryInfo projSession =
        omniframe::io::ProjectRecoveryStore::inspectPreviousSession();
    TEST_ASSERT(projSession.available && !projSession.hasUnsavedChanges &&
                    projSession.sourceKind == omniframe::io::SessionSourceKind::SavedProject &&
                    projSession.projectFilePath == savedProjPath,
                "inspectPreviousSession deve identificar projeto salvo da sessao anterior");

    TEST_ASSERT(omniframe::io::ProjectRecoveryStore::saveCleanSessionState(
                    StructuralModel(), QString(), omniframe::io::SessionSourceKind::Empty),
                "saveCleanSessionState(Empty) deve limpar continuidade de sessao");
    TEST_ASSERT(!omniframe::io::ProjectRecoveryStore::inspectPreviousSession().available,
                "Sessao vazia nao deve reportar continuidade pendente");

    omniframe::io::ProjectRecoveryStore::clearSnapshot();

    std::cout << "  - ProjectRecoveryStore & SolverOptions Undo/Redo [OK]\n";
}

void testAppLoggerDailyRotationAndLevels() {
    using omniframe::io::AppLogger;

    // Contrato de caminho padrão real e padrão omniframe_aaaa-mm-dd.log
    {
        const QString realLogDir = QDir::fromNativeSeparators(AppLogger::logDirectory());
        TEST_ASSERT(realLogDir.endsWith(QStringLiteral("/OmniFrame/log")),
                    "Diretorio padrao real de logs deve apontar para AppData/Local/OmniFrame/log");
        TEST_ASSERT(AppLogger::logFileNameForDate(QDate(2026, 11, 17)) ==
                        QStringLiteral("omniframe_2026-11-17.log"),
                    "Nome do arquivo de log deve seguir o padrao omniframe_aaaa-mm-dd.log");
    }

    QTemporaryDir logTmp;
    TEST_ASSERT(logTmp.isValid(), "QTemporaryDir invalido para AppLogger");
    AppLogger::ScopedDirectoryOverride dirGuard(logTmp.path());

    // 1) Todos os niveis de log (AppLogger + Qt message handler)
    {
        AppLogger::ScopedDateOverride fixedDate(QDate(2026, 11, 17));
        AppLogger::initialize(true, false);

        AppLogger::trace(QStringLiteral("UnitTest"), QStringLiteral("msg_trace_01"));
        AppLogger::debug(QStringLiteral("UnitTest"), QStringLiteral("msg_debug_02"));
        AppLogger::info(QStringLiteral("UnitTest"), QStringLiteral("msg_info_03"));
        AppLogger::warning(QStringLiteral("UnitTest"), QStringLiteral("msg_warn_04"));
        AppLogger::critical(QStringLiteral("UnitTest"), QStringLiteral("msg_crit_05"));
        AppLogger::fatal(QStringLiteral("UnitTest"), QStringLiteral("msg_fatal_06"));

        qDebug() << "qt_debug_captured";
        qInfo() << "qt_info_captured";
        qWarning() << "qt_warning_captured";
        qCritical() << "qt_critical_captured";
        AppLogger::flush();

        const QString logPath = AppLogger::currentLogFilePath();
        TEST_ASSERT(QFileInfo(logPath).fileName() == QStringLiteral("omniframe_2026-11-17.log") &&
                        QFile::exists(logPath),
                    "Arquivo diario omniframe_2026-11-17.log deve existir");

        QFile in(logPath);
        TEST_ASSERT(in.open(QIODevice::ReadOnly | QIODevice::Text), "Leitura do arquivo de log");
        QTextStream ts(&in);
        ts.setEncoding(QStringConverter::Utf8);
        const QString content = ts.readAll();
        TEST_ASSERT(content.contains(QStringLiteral("[TRACE   ]")) &&
                        content.contains(QStringLiteral("[DEBUG   ]")) &&
                        content.contains(QStringLiteral("[INFO    ]")) &&
                        content.contains(QStringLiteral("[WARNING ]")) &&
                        content.contains(QStringLiteral("[CRITICAL]")) &&
                        content.contains(QStringLiteral("[FATAL   ]")) &&
                        content.contains(QStringLiteral("msg_trace_01")) &&
                        content.contains(QStringLiteral("msg_fatal_06")) &&
                        content.contains(QStringLiteral("qt_debug_captured")) &&
                        content.contains(QStringLiteral("qt_info_captured")) &&
                        content.contains(QStringLiteral("qt_warning_captured")) &&
                        content.contains(QStringLiteral("qt_critical_captured")),
                    "Log diario deve registrar todos os niveis explicitos e mensagens do Qt");

        TEST_ASSERT(!AppLogger::readRecentLines(20).isEmpty(),
                    "readRecentLines deve retornar conteudo recente gravado");
        AppLogger::shutdown();
    }

    // 2) Retencao maxima de 10 arquivos de log: a cada novo recente gerado, o mais antigo e excluido
    QTemporaryDir rotTmp;
    TEST_ASSERT(rotTmp.isValid(), "QTemporaryDir invalido para rotacao");
    AppLogger::ScopedDirectoryOverride rotGuard(rotTmp.path());
    {
        AppLogger::ScopedDateOverride initDay(QDate(2026, 11, 1));
        AppLogger::initialize(false, false);

        for (int day = 1; day <= 13; ++day) {
            AppLogger::ScopedDateOverride d(QDate(2026, 11, day));
            AppLogger::info(QStringLiteral("Retention"), QStringLiteral("Log do dia %1").arg(day));
            AppLogger::flush();
            const QStringList files = AppLogger::existingLogFiles();
            TEST_ASSERT(files.size() <= AppLogger::kMaxLogFiles,
                        "Diretorio de logs nunca deve exceder 10 arquivos");
        }
    }

    const QStringList remaining = AppLogger::existingLogFiles();
    TEST_ASSERT(remaining.size() == 10,
                "Apos gerar 13 arquivos diarios, devem permanecer exatamente os 10 mais recentes");
    TEST_ASSERT(QFileInfo(remaining.first()).fileName() == QStringLiteral("omniframe_2026-11-13.log") &&
                    QFileInfo(remaining.last()).fileName() == QStringLiteral("omniframe_2026-11-04.log"),
                "Os arquivos mais antigos (dias 01 a 03) devem ser removidos, restando 04 a 13");
    TEST_ASSERT(!QFile::exists(QDir(rotTmp.path()).filePath(QStringLiteral("omniframe_2026-11-01.log"))) &&
                    !QFile::exists(QDir(rotTmp.path()).filePath(QStringLiteral("omniframe_2026-11-02.log"))) &&
                    !QFile::exists(QDir(rotTmp.path()).filePath(QStringLiteral("omniframe_2026-11-03.log"))),
                "Arquivos de log antigos excedentes devem ter sido excluidos do disco");

    AppLogger::shutdown();
    std::cout << "  - AppLogger (niveis completos, formato diario e retencao max 10) [OK]\n";
}

} // namespace

int main() {
    std::cout << "============================================================\n";
    std::cout << "  TESTE: SERIALIZACAO E DESSERIALIZACAO (.omni JSON)        \n";
    std::cout << "============================================================\n";

    Material steel = Material::makeStructuralSteel(1);
    CrossSection rect = CrossSection::makeRectangular(1, "Retangular 20x40", 0.20, 0.40);

    StructuralModel originalModel;
    originalModel.addMaterial(steel);
    originalModel.addCrossSection(rect);

    NodeId n1 = originalModel.addNode(0.0, 0.0, 0.0);
    NodeId n2 = originalModel.addNode(4.0, 0.0, 0.0);
    NodeId n3 = originalModel.addNode(4.0, 3.0, 0.0);

    Node* node1 = originalModel.findNode(n1);
    TEST_ASSERT(node1 != nullptr, "No 1 nao encontrado");
    node1->support = SupportRestraint::makeFixed();

    Node* node3 = originalModel.findNode(n3);
    TEST_ASSERT(node3 != nullptr, "No 3 nao encontrado");
    node3->load.force.y = -50.0;
    node3->load.force.z = 12.5;

    originalModel.addElement(n1, n2, 1, 1, 0.0);
    originalModel.addElement(n2, n3, 1, 1, 90.0);

    // Salvar em arquivo temporário .omni
    QString testFilePath = "teste_serializacao.omni";
    bool saved = ProjectSerializer::saveToFile(testFilePath, originalModel);
    TEST_ASSERT(saved, "Falha ao serializar projeto estrutural para disco (.omni)");
    std::cout << "  - Projeto serializado em: " << testFilePath.toStdString() << " [OK]\n";

    // Recarregar em um novo modelo
    StructuralModel loadedModel;
    bool loaded = ProjectSerializer::loadFromFile(testFilePath, loadedModel);
    TEST_ASSERT(loaded, "Falha ao carregar projeto desserializado (.omni)");
    std::cout << "  - Projeto recarregado do disco [OK]\n";

    // Validar fidelidade dimensional e topológica
    TEST_ASSERT(loadedModel.getNodes().size() == originalModel.getNodes().size(),
                "Numero de nos desserializados diverge do modelo original");
    TEST_ASSERT(loadedModel.getElements().size() == originalModel.getElements().size(),
                "Numero de elementos desserializados diverge do modelo original");
    TEST_ASSERT(loadedModel.getMaterials().size() == originalModel.getMaterials().size(),
                "Numero de materiais diverge");
    TEST_ASSERT(loadedModel.getCrossSections().size() == originalModel.getCrossSections().size(),
                "Numero de secoes transversais diverge");

    const Node* loadedNode1 = loadedModel.findNode(n1);
    TEST_ASSERT(loadedNode1 != nullptr, "No 1 carregado nao encontrado");
    TEST_ASSERT(loadedNode1->support.fixed[0] && loadedNode1->support.fixed[1] && loadedNode1->support.fixed[2],
                "Condicoes de contorno de apoio nao preservadas na desserializacao");

    const Node* loadedNode3 = loadedModel.findNode(n3);
    TEST_ASSERT(loadedNode3 != nullptr, "No 3 carregado nao encontrado");
    TEST_ASSERT(std::abs(loadedNode3->load.force.y - (-50.0)) < 1e-6,
                "Forca nodal Y nao preservada na desserializacao");
    TEST_ASSERT(std::abs(loadedNode3->load.force.z - 12.5) < 1e-6,
                "Forca nodal Z nao preservada na desserializacao");

    std::cout << "  - Elementos originais: " << originalModel.getElements().size()
              << " | Elementos carregados: " << loadedModel.getElements().size() << " [OK]\n";

    testSchema21AndBackwardCompat();
    testUnifiedUserPreferencesStoreAndMigration();
    testProjectRecoveryStoreAndSolverUndo();
    testAppLoggerDailyRotationAndLevels();

    std::cout << "\n[SUCESSO] Teste de Serialização .omni (Schemas 1.0 / 2.0 / 2.1) concluido com 100% de integridade!\n";
    return 0;
}
