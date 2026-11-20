#include "CompileTranslationsGUI.hpp"
#include "CompileTranslationsTerminal.hpp"
#include "VersionEditor.hpp"
#include "io/AppLogger.hpp"
#include "io/UserPreferencesStore.hpp"
#include "render/RenderSettings.hpp"
#include "ui/MainWindow.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << '\n';
        ++g_failures;
    }
}

QByteArray readAllBytes(const QString& path, bool* okOut = nullptr) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (okOut) *okOut = false;
        return {};
    }
    if (okOut) *okOut = true;
    return f.readAll();
}

bool writeAllBytes(const QString& path, const QByteArray& bytes) {
    const QFileInfo fi(path);
    if (!QDir().mkpath(fi.absolutePath())) {
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(bytes) == bytes.size();
}

void createMinimalVersionWorkspace(const QString& rootPath, const QByteArray& cmakeContent) {
    QDir root(rootPath);
    require(root.mkpath(QStringLiteral("source/assets")), "Criar source/assets temporario");
    require(root.mkpath(QStringLiteral("docs")), "Criar docs temporario");
    require(writeAllBytes(root.filePath(QStringLiteral("CMakeLists.txt")), cmakeContent), "Gravar CMakeLists.txt");
}

void testVersionEditorMultipleReleaseDocsAndAtomicCrlf() {
    using namespace mocks::version_editor;
    QTemporaryDir tmp;
    require(tmp.isValid(), "QTemporaryDir valido para multiplos release-v*.md");

    const QByteArray cmakeCrlf =
        "cmake_minimum_required(VERSION 3.28)\r\n"
        "project(\r\n"
        "    OmniFrame\r\n"
        "    VERSION 2026.11.18.0\r\n"
        "    LANGUAGES CXX\r\n"
        ")\r\n";
    createMinimalVersionWorkspace(tmp.path(), cmakeCrlf);

    const QDir root(tmp.path());
    const QByteArray oldReleaseBytes =
        "# Release Notes v2026.10.01.0\n"
        "**Versão:** `2026.10.01.0`\n"
        "**Data:** `01 de outubro de 2026`\n";
    const QByteArray activeReleaseBytes =
        "# Release Notes v2026.11.18.0\n"
        "**Versão:** `2026.11.18.0`\n"
        "**Data:** `18 de novembro de 2026`\n";

    const QString oldReleasePath = root.filePath(QStringLiteral("docs/release-v2026.10.01.0.md"));
    const QString activeReleasePath = root.filePath(QStringLiteral("docs/release-v2026.11.18.0.md"));
    const QString targetReleasePath = root.filePath(QStringLiteral("docs/release-v2026.11.20.0.md"));

    require(writeAllBytes(oldReleasePath, oldReleaseBytes), "Gravar release historico");
    require(writeAllBytes(activeReleasePath, activeReleaseBytes), "Gravar release ativo");

    const RelatorioExecucao rel = executar_edicao_versao(
        tmp.path(),
        QStringLiteral("2026.11.20.0"),
        QDate(2026, 11, 20),
        OperacaoModo::Apply,
        IdiomaEditor::Portugues
    );

    require(rel.totalErros == 0, "VersionEditor nao deve registrar erros de gravacao");
    require(!QFileInfo::exists(activeReleasePath), "Release ativo antigo deve ser renomeado");
    require(QFileInfo::exists(targetReleasePath), "Novo arquivo de release ativo deve existir");
    require(QFileInfo::exists(oldReleasePath), "Arquivo historico release-v2026.10.01.0.md deve ser preservado");

    bool okRead = false;
    const QByteArray oldAfter = readAllBytes(oldReleasePath, &okRead);
    require(okRead && oldAfter == oldReleaseBytes, "Arquivo historico deve permanecer byte-a-byte intacto");

    const QByteArray activeAfter = readAllBytes(targetReleasePath, &okRead);
    require(okRead && activeAfter.contains("2026.11.20.0"), "Novo arquivo de release deve conter versao atualizada");

    const QByteArray cmakeAfter = readAllBytes(root.filePath(QStringLiteral("CMakeLists.txt")), &okRead);
    require(okRead && cmakeAfter.contains("VERSION 2026.11.20.0") && cmakeAfter.contains("\r\n"),
            "QSaveFile deve atualizar CMakeLists.txt preservando terminacao CRLF");
}

void testVersionEditorCaseVariationAndCollisionSafety() {
    using namespace mocks::version_editor;

    // Caso 1: Variacao de maiusculas/minusculas no nome (ex.: Release-v2026.11.18.0.md -> 2026.11.18.0)
    {
        QTemporaryDir tmp;
        require(tmp.isValid(), "QTemporaryDir valido para case variation");
        createMinimalVersionWorkspace(
            tmp.path(),
            "cmake_minimum_required(VERSION 3.28)\nproject(OmniFrame VERSION 2026.11.18.0 LANGUAGES CXX)\n"
        );
        const QDir root(tmp.path());
        const QString upperCasePath = root.filePath(QStringLiteral("docs/Release-v2026.11.18.0.md"));
        const QByteArray initialContent =
            "# Release Notes\n"
            "**Versão:** `2026.11.17.0`\n"
            "**Data:** `17 de novembro de 2026`\n";
        require(writeAllBytes(upperCasePath, initialContent), "Gravar Release-v*.md");

        const RelatorioExecucao rel = executar_edicao_versao(
            tmp.path(),
            QStringLiteral("2026.11.18.0"),
            QDate(2026, 11, 18),
            OperacaoModo::Apply,
            IdiomaEditor::Ingles
        );
        require(rel.totalErros == 0, "Atualizacao com variacao de caixa nao deve falhar");

        const QString lowerPath = root.filePath(QStringLiteral("docs/release-v2026.11.18.0.md"));
        const QString survivingPath = QFileInfo::exists(upperCasePath) ? upperCasePath : lowerPath;
        require(QFileInfo::exists(survivingPath), "Arquivo de release nao pode ser apagado em rename case-only");
        bool okRead = false;
        const QByteArray contentAfter = readAllBytes(survivingPath, &okRead);
        require(okRead && contentAfter.contains("2026.11.18.0"), "Conteudo deve estar atualizado apos case-only check");
    }

    // Caso 2: Colisao explicita quando targetReleaseAbs ja existe como outro arquivo no disco
    {
        QTemporaryDir tmp;
        require(tmp.isValid(), "QTemporaryDir valido para colisao de release");
        createMinimalVersionWorkspace(
            tmp.path(),
            "cmake_minimum_required(VERSION 3.28)\nproject(OmniFrame VERSION 2026.11.21.0 LANGUAGES CXX)\n"
        );
        const QDir root(tmp.path());
        const QString existingTarget = root.filePath(QStringLiteral("docs/release-v2026.11.20.0.md"));
        const QString activeNewer = root.filePath(QStringLiteral("docs/release-v2026.11.21.0.md"));
        const QByteArray markerContent = "# Existing Historical Release\nKEEP_EXISTING_20\n";

        require(writeAllBytes(existingTarget, markerContent), "Gravar release-v2026.11.20.0.md existente");
        require(writeAllBytes(activeNewer, "# Active Release\n**Versão:** `2026.11.21.0`\n"), "Gravar release-v2026.11.21.0.md");

        const RelatorioExecucao rel = executar_edicao_versao(
            tmp.path(),
            QStringLiteral("2026.11.20.0"),
            QDate(2026, 11, 20),
            OperacaoModo::Apply,
            IdiomaEditor::Ingles
        );
        require(rel.totalErros >= 1 && !rel.sucesso, "Colisao ao renomear release existente deve reportar erro explicito");
        bool okRead = false;
        const QByteArray targetAfter = readAllBytes(existingTarget, &okRead);
        require(okRead && targetAfter == markerContent, "Arquivo existente de destino jamais deve ser sobrescrito ou deletado");
    }
}

void testVersionEditorLibraryPackagingFiles() {
    using namespace mocks::version_editor;

    QTemporaryDir tmp;
    require(tmp.isValid(), "QTemporaryDir valido para alvos em library/");
    createMinimalVersionWorkspace(
        tmp.path(),
        "cmake_minimum_required(VERSION 3.28)\nproject(OmniFrame VERSION 2026.11.18.0 LANGUAGES CXX)\n"
    );

    const QDir root(tmp.path());
    const QString issPath = root.filePath(QString::fromLatin1(kOmniFrameIssRelPath));
    const QString manifestPath = root.filePath(QString::fromLatin1(kAppxManifestRelPath));
    const QString notesPath = root.filePath(QString::fromLatin1(kOmniFrameNotesRelPath));

    const QByteArray issBytes =
        "#define MyAppName \"OmniFrame 3D\"\n"
        "#define MyAppVersion \"2026.11.18.0\"\n"
        "OutputBaseFilename=OmniFrame3D_msvc_v2026.11.18.0\n";
    const QByteArray manifestBytes =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Package>\n"
        "  <!--Package created by MSIX Packaging Tool version: 1.2024.405.0-->\n"
        "  <Identity Name=\"test\" Publisher=\"CN=Test\" Version=\"2026.11.18.0\" ProcessorArchitecture=\"x64\" />\n"
        "  <Dependencies>\n"
        "    <TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.17763.0\" MaxVersionTested=\"10.0.26100.0\" />\n"
        "    <PackageDependency Name=\"Microsoft.VCLibs.140.00.UWPDesktop\" MinVersion=\"14.0.33728.0\" Publisher=\"CN=Microsoft\" />\n"
        "  </Dependencies>\n"
        "</Package>\n";
    const QByteArray notesBytes =
        "& \"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\signtool.exe\"\n"
        "\"C:\\repo\\docs\\release-v2026.11.18.0.md\";\n"
        "Version: 2026.11.18.0\n"
        "Date: November 18, 2026\n"
        "Versão: 2026.11.18.0\n"
        "Data: 18 de novembro de 2026\n";

    require(writeAllBytes(issPath, issBytes), "Gravar library/OmniFrame.iss temporario");
    require(writeAllBytes(manifestPath, manifestBytes), "Gravar library/AppxManifest.xml temporario");
    require(writeAllBytes(notesPath, notesBytes), "Gravar library/OmniFrame_cpp.txt temporario");

    const QVector<ArquivoAlvoInfo> targets = listar_arquivos_alvo(tmp.path());
    bool hasIss = false;
    bool hasNotes = false;
    bool hasManifest = false;
    bool hasLegacyRepo = false;
    for (const ArquivoAlvoInfo& info : targets) {
        if (info.caminhoRelativo == QString::fromLatin1(kOmniFrameIssRelPath)) hasIss = true;
        if (info.caminhoRelativo == QString::fromLatin1(kOmniFrameNotesRelPath)) hasNotes = true;
        if (info.caminhoRelativo == QString::fromLatin1(kAppxManifestRelPath)) hasManifest = true;
        if (info.caminhoRelativo.startsWith(QStringLiteral("repo/"))) hasLegacyRepo = true;
    }
    require(hasIss && hasNotes && hasManifest, "listar_arquivos_alvo deve incluir os tres arquivos em library/");
    require(!hasLegacyRepo, "listar_arquivos_alvo nao deve mais referenciar o diretorio antigo repo/");

    const RelatorioExecucao relApply = executar_edicao_versao(
        tmp.path(),
        QStringLiteral("2026.12.01.0"),
        QDate(2026, 12, 1),
        OperacaoModo::Apply,
        IdiomaEditor::Portugues
    );
    require(relApply.totalErros == 0, "executar_edicao_versao nao deve falhar ao atualizar arquivos em library/");

    auto statusFor = [](const RelatorioExecucao& rel, const QString& relPath, StatusArquivo* statusOut) -> bool {
        for (const RegistroAlteracao& reg : rel.registros) {
            if (reg.caminhoRelativo == relPath) {
                if (statusOut) *statusOut = reg.status;
                return true;
            }
        }
        return false;
    };

    for (const QString& targetRel : {
             QString::fromLatin1(kOmniFrameIssRelPath),
             QString::fromLatin1(kOmniFrameNotesRelPath),
             QString::fromLatin1(kAppxManifestRelPath)
         }) {
        StatusArquivo st = StatusArquivo::Ausente;
        require(statusFor(relApply, targetRel, &st) && st == StatusArquivo::Atualizado,
                "Arquivo de library/ deve ser reportado como Atualizado no modo Apply");
    }

    bool okRead = false;
    const QByteArray issAfter = readAllBytes(issPath, &okRead);
    require(okRead &&
                issAfter.contains("#define MyAppVersion \"2026.12.01.0\"") &&
                issAfter.contains("OutputBaseFilename=OmniFrame3D_msvc_v2026.12.01.0"),
            "library/OmniFrame.iss deve atualizar versao e OutputBaseFilename");

    const QByteArray manifestAfter = readAllBytes(manifestPath, &okRead);
    require(okRead &&
                manifestAfter.contains("Version=\"2026.12.01.0\"") &&
                manifestAfter.contains("1.2024.405.0") &&
                manifestAfter.contains("10.0.17763.0") &&
                manifestAfter.contains("10.0.26100.0") &&
                manifestAfter.contains("14.0.33728.0"),
            "library/AppxManifest.xml deve atualizar Version preservando SDKs/ferramentas");

    const QByteArray notesAfter = readAllBytes(notesPath, &okRead);
    const QString notesText = QString::fromUtf8(notesAfter);
    require(okRead &&
                notesText.contains(QStringLiteral("release-v2026.12.01.0.md")) &&
                notesText.contains(QStringLiteral("Version: 2026.12.01.0")) &&
                notesText.contains(QStringLiteral("Date: December 1, 2026")) &&
                notesText.contains(QStringLiteral("Versão: 2026.12.01.0")) &&
                notesText.contains(QStringLiteral("Data: 1 de dezembro de 2026")) &&
                notesText.contains(QStringLiteral("10.0.26100.0")),
            "library/OmniFrame_cpp.txt deve atualizar versao/datas PT-BR e EN-US preservando 10.0.26100.0");

    const RelatorioExecucao relCheck = executar_edicao_versao(
        tmp.path(),
        QStringLiteral("2026.12.01.0"),
        QDate(2026, 12, 1),
        OperacaoModo::Check,
        IdiomaEditor::Portugues
    );
    require(relCheck.totalErros == 0, "Segunda passagem Check em library/ nao deve ter erros");
    for (const QString& targetRel : {
             QString::fromLatin1(kOmniFrameIssRelPath),
             QString::fromLatin1(kOmniFrameNotesRelPath),
             QString::fromLatin1(kAppxManifestRelPath)
         }) {
        StatusArquivo st = StatusArquivo::Ausente;
        require(statusFor(relCheck, targetRel, &st) && st == StatusArquivo::SemMudancas,
                "Check apos Apply em library/ deve reportar SemMudancas");
    }
}

void testCompileTranslationsTerminalFixes() {
    using namespace mocks::compile_translations_terminal;

    // 1. QVersionNumber ordenacao decrescente (6.11.1 > 6.10.0 > 6.9.2 > 6.5.3)
    const QStringList sorted = ordenar_versoes_qt_desc({
        QStringLiteral("6.5.3"),
        QStringLiteral("6.9.2"),
        QStringLiteral("6.11.1"),
        QStringLiteral("6.2.4"),
        QStringLiteral("6.10.0")
    });
    const QStringList expected = {
        QStringLiteral("6.11.1"),
        QStringLiteral("6.10.0"),
        QStringLiteral("6.9.2"),
        QStringLiteral("6.5.3"),
        QStringLiteral("6.2.4")
    };
    require(sorted == expected, "ordenar_versoes_qt_desc deve ordenar semanticamente 6.11.1 > 6.10.0 > 6.9.2");

    // 2. --lrelease explicito invalido NUNCA pode fazer fallback silencioso para PATH
    QTemporaryDir tmp;
    require(tmp.isValid(), "QTemporaryDir valido para CompileTranslationsTerminal");
    const QDir dir(tmp.path());
    const QString fakeTs = dir.filePath(QStringLiteral("omniframe_test.ts"));
    const QString fakeQm = dir.filePath(QStringLiteral("omniframe_test.qm"));
    const QByteArray tsPayload =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<TS version=\"2.1\" language=\"en_US\"></TS>\n";
    require(writeAllBytes(fakeTs, tsPayload), "Criar .ts de teste");

    const QString nonexistentLrelease = dir.filePath(QStringLiteral("missing_bin/lrelease_invalido.exe"));
    require(localizar_lrelease(nonexistentLrelease).isEmpty(),
            "localizar_lrelease com override inexistente deve retornar vazio sem fallback para PATH");

    const RelatorioCompilacaoTerminal invalidOverrideRel = executar_compilacao(
        tmp.path(),
        nonexistentLrelease,
        {fakeTs},
        ModoExecucao::CompilarSelecionados,
        true
    );
    require(!invalidOverrideRel.sucesso && invalidOverrideRel.caminhoLrelease.isEmpty(),
            "executar_compilacao deve falhar explicitamente quando lreleaseOverride for invalido");

    // 3. --check --file em qualquer ordem e modo estritamente read-only
    const CliArgsParseResult checkBeforeFile = parse_cli_args({
        QStringLiteral("--check"),
        QStringLiteral("--dir"),
        tmp.path(),
        QStringLiteral("--file"),
        fakeTs
    });
    require(checkBeforeFile.modo == ModoExecucao::VerificarApenas && checkBeforeFile.arquivosArg.size() == 1,
            "parse_cli_args deve manter VerificarApenas com --check seguido de --file");

    const CliArgsParseResult fileBeforeCheck = parse_cli_args({
        QStringLiteral("--file"),
        fakeTs,
        QStringLiteral("--check"),
        QStringLiteral("--dir"),
        tmp.path()
    });
    require(fileBeforeCheck.modo == ModoExecucao::VerificarApenas && fileBeforeCheck.arquivosArg.size() == 1,
            "parse_cli_args deve manter VerificarApenas com --file seguido de --check");

    const CliArgsParseResult compileSelected = parse_cli_args({
        QStringLiteral("--dir"),
        tmp.path(),
        QStringLiteral("--file"),
        fakeTs
    });
    require(compileSelected.modo == ModoExecucao::CompilarSelecionados,
            "parse_cli_args sem --check com --file deve usar CompilarSelecionados");

    const RelatorioCompilacaoTerminal checkMissingQm = executar_compilacao(
        tmp.path(),
        {},
        {fakeTs},
        ModoExecucao::VerificarApenas,
        true
    );
    require(!checkMissingQm.sucesso && !QFileInfo::exists(fakeQm),
            "--check sem .qm deve retornar erro sem criar arquivo .qm no disco");

    // 4. FailedToStart vs timeout quando executavel existe mas nao e executavel valido
    const QString nonExecFile = dir.filePath(QStringLiteral("not_an_executable.txt"));
    require(writeAllBytes(nonExecFile, QByteArray("plain text")), "Criar arquivo nao executavel");
    const RelatorioCompilacaoTerminal failedStartRel = executar_compilacao(
        tmp.path(),
        nonExecFile,
        {fakeTs},
        ModoExecucao::CompilarSelecionados,
        true
    );
    require(!failedStartRel.sucesso && failedStartRel.itens.size() == 1,
            "Arquivo nao executavel deve falhar na execucao");
    if (!failedStartRel.itens.isEmpty()) {
        const QString msg = failedStartRel.itens[0].mensagem;
        require(msg.contains(QStringLiteral("Failed to start")) && !msg.contains(QStringLiteral("Timed out")),
                "Erro de inicio deve ser reportado como FailedToStart, nao Timeout");
    }

    // 5. Quando .qm atualizado existe, --check --file aprova sem modificar o arquivo
    const QByteArray qmMarker("QM_TEST_BYTES");
    require(writeAllBytes(fakeQm, qmMarker), "Criar .qm dummy");
    const RelatorioCompilacaoTerminal checkOk = executar_compilacao(
        tmp.path(),
        {},
        {fakeTs},
        ModoExecucao::VerificarApenas,
        true
    );
    require(checkOk.sucesso && readAllBytes(fakeQm) == qmMarker,
            "--check com .qm atualizado deve aprovar preservando o .qm sem modificacoes");

#ifdef OMNIFRAME_COMPILE_TRANSLATIONS_CLI_PATH
    const QString cliBin = QString::fromUtf8(OMNIFRAME_COMPILE_TRANSLATIONS_CLI_PATH);
    if (QFileInfo::exists(cliBin)) {
        require(QFile::remove(fakeQm), "Remover .qm dummy para testar CLI real");

        QProcess cliCheckProc;
        cliCheckProc.start(cliBin, {
            QStringLiteral("--check"),
            QStringLiteral("--dir"),
            tmp.path(),
            QStringLiteral("--file"),
            fakeTs,
            QStringLiteral("--lang"),
            QStringLiteral("en")
        });
        require(cliCheckProc.waitForFinished(15000), "CompileTranslationsTerminal --check --file deve finalizar rapido");
        require(cliCheckProc.exitStatus() == QProcess::NormalExit && cliCheckProc.exitCode() != 0,
                "CLI --check --file sem .qm deve retornar codigo != 0");
        require(!QFileInfo::exists(fakeQm), "CLI --check --file deve ser estritamente read-only");

        QProcess cliBadLrProc;
        cliBadLrProc.start(cliBin, {
            QStringLiteral("--dir"),
            tmp.path(),
            QStringLiteral("--lrelease"),
            nonexistentLrelease,
            QStringLiteral("--lang"),
            QStringLiteral("en")
        });
        require(cliBadLrProc.waitForFinished(15000), "CompileTranslationsTerminal --lrelease invalido deve finalizar");
        require(cliBadLrProc.exitStatus() == QProcess::NormalExit && cliBadLrProc.exitCode() != 0,
                "CLI com --lrelease invalido deve falhar explicitamente");
    }
#endif
}

void testRenderSettingsClassificationAud032() {
    using namespace omniframe;

    RenderQualitySettings base = RenderQualitySettings::fromPreset(QualityPreset::HighQuality);
    base.refreshPresetClassification();
    require(base.preset == QualityPreset::HighQuality, "Preset base HighQuality deve manter classificacao");

    {
        RenderQualitySettings s = base;
        s.lineSoftness += 0.25f;
        s.refreshPresetClassification();
        require(s.preset == QualityPreset::Custom, "Alterar lineSoftness deve reclassificar para Custom");
    }
    {
        RenderQualitySettings s = base;
        s.sharpenStrength += 0.10f;
        s.refreshPresetClassification();
        require(s.preset == QualityPreset::Custom, "Alterar sharpenStrength deve reclassificar para Custom");
    }
    {
        RenderQualitySettings s = base;
        s.planeGridContrast += 0.20f;
        s.refreshPresetClassification();
        require(s.preset == QualityPreset::Custom, "Alterar planeGridContrast deve reclassificar para Custom");
    }
}

bool containsCaseInsensitiveAsciiOrUtf16Le(const QByteArray& haystack, const QString& needle) {
    if (needle.isEmpty()) {
        return false;
    }
    const QByteArray needleUtf8Lower = needle.toLower().toUtf8();
    const QByteArray haystackLower = haystack.toLower();
    if (!needleUtf8Lower.isEmpty() && haystackLower.contains(needleUtf8Lower)) {
        return true;
    }
    const QString lowerNeedle = needle.toLower();
    const QString upperNeedle = needle.toUpper();
    auto encodeUtf16Le = [](const QString& s) {
        QByteArray out;
        out.reserve(s.size() * 2);
        for (QChar ch : s) {
            const ushort u = ch.unicode();
            out.append(static_cast<char>(u & 0xFF));
            out.append(static_cast<char>((u >> 8) & 0xFF));
        }
        return out;
    };
    const QByteArray utf16Lower = encodeUtf16Le(lowerNeedle);
    const QByteArray utf16Upper = encodeUtf16Le(upperNeedle);
    const QByteArray utf16Orig = encodeUtf16Le(needle);
    return (!utf16Orig.isEmpty() && haystack.contains(utf16Orig)) ||
           (!utf16Lower.isEmpty() && haystack.contains(utf16Lower)) ||
           (!utf16Upper.isEmpty() && haystack.contains(utf16Upper));
}

void testCompileTranslationsGuiAsyncQueue() {
    using namespace mocks::compile_translations_gui;
    using namespace mocks::compile_translations_terminal;

    QTemporaryDir tmpDir;
    require(tmpDir.isValid(), "QTemporaryDir valido para CompileTranslationsGUI");

    const QString ts1 = tmpDir.filePath(QStringLiteral("sample_pt_BR.ts"));
    const QString ts2 = tmpDir.filePath(QStringLiteral("sample_en_US.ts"));
    const QByteArray tsContent = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<!DOCTYPE TS>\n"
        "<TS version=\"2.1\" language=\"pt_BR\">\n"
        "<context><name>AuditCtx</name><message><source>Hello</source><translation>Ola</translation></message></context>\n"
        "</TS>\n");
    for (const QString& p : {ts1, ts2}) {
        QFile f(p);
        require(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Escrita de arquivo .ts de teste");
        f.write(tsContent);
        f.close();
    }

    auto pumpUntil = [](auto&& predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(5);
        }
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        return predicate();
    };

    CompileTranslationsGuiWindow gui;
    gui.setAttribute(Qt::WA_DontShowOnScreen, true);

    // 1) Caminho inexistente de lrelease deve ser rejeitado antes de iniciar fila sem travar GUI
    const QString missingLrelease = tmpDir.filePath(QStringLiteral("nonexistent_lrelease_tool.exe"));
    gui.setTranslationDir(tmpDir.path());
    gui.setLreleasePath(missingLrelease);
    gui.triggerCompileAllAsync();
    require(!gui.isCompilationRunning() && gui.batchTotalCount() == 0 && !gui.logContent().isEmpty(),
            "lrelease inexistente deve ser rejeitado com mensagem no log sem iniciar QProcess");

    // 2) Arquivo existente porem nao executavel deve disparar FailedToStart na fila assincrona QProcess+QTimer
    const QString dummyNonExec = tmpDir.filePath(QStringLiteral("not_an_executable_lrelease.txt"));
    {
        QFile badExe(dummyNonExec);
        require(badExe.open(QIODevice::WriteOnly | QIODevice::Truncate), "Escrita de arquivo nao executavel");
        badExe.write("plain text not executable");
        badExe.close();
    }
    gui.setLreleasePath(dummyNonExec);
    gui.triggerCompileAllAsync();
    const bool badFinished = pumpUntil([&]() { return !gui.isCompilationRunning(); }, 10000);
    require(badFinished, "Fila assincrona CompileTranslationsGUI deve concluir quando QProcess falha ao iniciar");
    require(gui.batchTotalCount() == 2 && gui.batchOkCount() == 0 && gui.batchErrorCount() == 2 &&
                (gui.logContent().contains(QStringLiteral("Falha ao iniciar")) ||
                 gui.logContent().contains(QStringLiteral("Failed to start"))),
            "Arquivo nao executavel deve contabilizar 2 erros FailedToStart e registrar log na GUI");

    // 3) Execucao assincrona real com lrelease.exe gerando arquivos .qm
    const QString realLrelease = localizar_lrelease();
    require(!realLrelease.isEmpty() && QFileInfo::exists(realLrelease),
            "lrelease do Qt deve estar disponivel para validar compilacao assincrona GUI");
    if (!realLrelease.isEmpty() && QFileInfo::exists(realLrelease)) {
        gui.setLreleasePath(realLrelease);
        gui.triggerCompileAllAsync();
        const bool okFinished = pumpUntil([&]() { return !gui.isCompilationRunning(); }, 15000);
        require(okFinished, "Fila assincrona CompileTranslationsGUI deve concluir com lrelease real");
        require(gui.batchTotalCount() == 2 && gui.batchOkCount() == 2 && gui.batchErrorCount() == 0,
                "CompileTranslationsGUI deve compilar os 2 arquivos .ts com sucesso via QProcess+QTimer");
        const QString qm1 = tmpDir.filePath(QStringLiteral("sample_pt_BR.qm"));
        const QString qm2 = tmpDir.filePath(QStringLiteral("sample_en_US.qm"));
        require(QFileInfo(qm1).size() > 0 && QFileInfo(qm2).size() > 0,
                "Arquivos .qm compilados pela fila assincrona da GUI devem existir e ser nao vazios");
    }
    gui.close();
    QApplication::processEvents();
}

void testVersionEditorBinaryStringsAud031() {
    const QString srcRoot = QStringLiteral(OMNIFRAME_SOURCE_DIR);
    const QString mocksCmakePath = QDir(srcRoot).filePath(QStringLiteral("mocks/CMakeLists.txt"));
    bool cmakeReadOk = false;
    const QByteArray cmakeBytes = readAllBytes(mocksCmakePath, &cmakeReadOk);
    require(cmakeReadOk, "Leitura de mocks/CMakeLists.txt para auditoria AUD-031");
    const QByteArray forbiddenMacro = QByteArrayLiteral("OMNIFRAME_DEFAULT_") + QByteArrayLiteral("SOURCE_ROOT");
    require(!cmakeBytes.contains(forbiddenMacro),
            "mocks/CMakeLists.txt nao deve definir OMNIFRAME_DEFAULT_SOURCE_ROOT");

    QString exePath;
#ifdef OMNIFRAME_VERSION_EDITOR_EXE_PATH
    exePath = QStringLiteral(OMNIFRAME_VERSION_EDITOR_EXE_PATH);
#endif
    const QString releaseFallback = QDir(srcRoot).filePath(QStringLiteral("build/Version/VersionEditor.exe"));
    if (exePath.isEmpty() || !QFileInfo::exists(exePath)) {
        exePath = releaseFallback;
    }

    require(!exePath.isEmpty() && QFileInfo::exists(exePath),
            "VersionEditor.exe deve existir para auditoria de strings binarias [AUD-031]");

    auto auditBinary = [&](const QString& binPath, bool checkWorkspaceRoot) {
        bool readOk = false;
        const QByteArray raw = readAllBytes(binPath, &readOk);
        require(readOk && raw.size() > 4096, "Leitura binaria de VersionEditor.exe");
        const QString hardcodedLegacy = QStringLiteral("D:") + QStringLiteral("\\MISCELANEAS");
        const QString hardcodedLegacyFwd = QStringLiteral("D:") + QStringLiteral("/MISCELANEAS");
        require(!containsCaseInsensitiveAsciiOrUtf16Le(raw, QString::fromLatin1(forbiddenMacro)),
                "Binario VersionEditor.exe nao pode conter macro OMNIFRAME_DEFAULT_SOURCE_ROOT");
        require(!containsCaseInsensitiveAsciiOrUtf16Le(raw, hardcodedLegacy) &&
                    !containsCaseInsensitiveAsciiOrUtf16Le(raw, hardcodedLegacyFwd),
                "Binario VersionEditor.exe nao pode embutir caminho legado de maquina externa");
        if (checkWorkspaceRoot) {
            const QString nativeSrc = QDir::toNativeSeparators(QDir(srcRoot).absolutePath());
            const QString fwdSrc = QDir::fromNativeSeparators(QDir(srcRoot).absolutePath());
            require(!containsCaseInsensitiveAsciiOrUtf16Le(raw, nativeSrc) &&
                        !containsCaseInsensitiveAsciiOrUtf16Le(raw, fwdSrc),
                    "Binario Release VersionEditor.exe nao pode embutir caminhos locais ou PDB absolutos (/PDBALTPATH:%_PDB%)");
        }
    };

#if defined(__SANITIZE_ADDRESS__)
    auditBinary(exePath, false);
    if (QFileInfo::exists(releaseFallback)) {
        auditBinary(releaseFallback, true);
    }
#else
    auditBinary(exePath, true);
#endif
}

void testMainWindowUninstallCliIntegrationFailureAud032() {
    using namespace omniframe;

    MainWindow mw;
    mw.setAttribute(Qt::WA_DontShowOnScreen, true);
    mw.show();
    QApplication::processEvents();

    auto findModelessBoxes = []() {
        QList<QMessageBox*> boxes;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(w)) {
                if (box->isVisible()) {
                    boxes.append(box);
                }
            }
        }
        return boxes;
    };

    // 1) Simular falha de desinstalacao HKCU (deve exibir QMessageBox::Warning + barra de status de falha)
    mw.setUninstallWindowsCliIntegrationHandler([](const QString&, QString* detailsOut) {
        if (detailsOut) {
            *detailsOut = QStringLiteral("Simulated HKCU permission denied");
        }
        return false;
    });
    QMetaObject::invokeMethod(&mw, "onUninstallWindowsCliIntegration", Qt::DirectConnection);
    QApplication::processEvents();

    const QList<QMessageBox*> warnBoxes = findModelessBoxes();
    require(warnBoxes.size() == 1, "Falha de desinstalacao CLI deve abrir exatamente 1 QMessageBox modeless");
    if (!warnBoxes.isEmpty()) {
        QMessageBox* box = warnBoxes.front();
        require(box->icon() == QMessageBox::Warning,
                "Falha ao remover integracao CLI deve usar icone QMessageBox::Warning");
        require(box->text().contains(QStringLiteral("Simulated HKCU permission denied")),
                "QMessageBox de falha deve exibir os detalhes retornados pelo handler");
        const QString statusMsg = mw.statusBar() ? mw.statusBar()->currentMessage() : QString();
        require(statusMsg.contains(QStringLiteral("Falha"), Qt::CaseInsensitive) ||
                    statusMsg.contains(QStringLiteral("Failed"), Qt::CaseInsensitive),
                "Barra de status deve sinalizar falha na remocao HKCU da CLI");
        box->close();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // 2) Simular sucesso na remocao (sem tocar registro real HKCU)
    mw.setUninstallWindowsCliIntegrationHandler([](const QString&, QString* detailsOut) {
        if (detailsOut) {
            *detailsOut = QStringLiteral("Simulated clean removal");
        }
        return true;
    });
    QMetaObject::invokeMethod(&mw, "onUninstallWindowsCliIntegration", Qt::DirectConnection);
    QApplication::processEvents();

    const QList<QMessageBox*> infoBoxes = findModelessBoxes();
    require(infoBoxes.size() == 1, "Sucesso na desinstalacao CLI deve abrir 1 QMessageBox::Information modeless");
    if (!infoBoxes.isEmpty()) {
        QMessageBox* box = infoBoxes.front();
        require(box->icon() == QMessageBox::Information,
                "Sucesso ao remover integracao CLI deve usar icone QMessageBox::Information");
        require(box->text().contains(QStringLiteral("Simulated clean removal")),
                "QMessageBox de sucesso deve incluir detalhes da remocao");
        box->close();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QTemporaryDir isolatedPrefsRoot;
    require(isolatedPrefsRoot.isValid(), "QTemporaryDir invalido para isolamento de preferencias");
    omniframe::io::UserPreferencesStore::ScopedDirectoryOverride prefsOverride(
        isolatedPrefsRoot.filePath(QStringLiteral("OmniFrame")));

    testVersionEditorMultipleReleaseDocsAndAtomicCrlf();
    testVersionEditorCaseVariationAndCollisionSafety();
    testVersionEditorLibraryPackagingFiles();
    testCompileTranslationsTerminalFixes();
    testRenderSettingsClassificationAud032();
    testCompileTranslationsGuiAsyncQueue();
    testVersionEditorBinaryStringsAud031();
    testMainWindowUninstallCliIntegrationFailureAud032();

    omniframe::io::AppLogger::shutdown();
    if (g_failures == 0) {
        std::cout << "[PASS] test_Phase11ToolsAndAudit\n";
        return 0;
    }
    std::cerr << "[FAIL] test_Phase11ToolsAndAudit: " << g_failures << " failure(s)\n";
    return 1;
}
