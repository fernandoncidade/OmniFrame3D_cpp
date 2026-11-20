#include "CompileTranslationsTerminal.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <iostream>

namespace mocks::compile_translations_terminal {

namespace {

bool pareceRaizProjeto(const QDir& dir) {
    return dir.exists("CMakeLists.txt") && dir.exists("source/locale/language");
}

QStringList candidatosQtBin() {
    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        dirs << appDir;
    }

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* varName : {"QTDIR", "QT_HOST_PATH", "Qt6_DIR"}) {
        const QString val = env.value(QLatin1String(varName)).trimmed();
        if (!val.isEmpty()) {
            dirs << QDir(val).filePath("bin")
                 << QDir(val).filePath("../bin")
                 << QDir(val).filePath("../../../bin");
        }
    }
    const QString cmakePrefix = env.value("CMAKE_PREFIX_PATH");
    for (const QString& prefix : cmakePrefix.split(';', Qt::SkipEmptyParts)) {
        dirs << QDir(prefix.trimmed()).filePath("bin");
    }

    const QString qtBins = QLibraryInfo::path(QLibraryInfo::BinariesPath);
    if (!qtBins.isEmpty()) {
        dirs << qtBins;
    }

    const QStringList qtRootBases = {
        QStringLiteral("C:/Qt"),
        QStringLiteral("D:/Qt"),
        QStringLiteral("E:/Qt"),
        QDir::home().filePath("Qt")
    };
    for (const QString& rootBase : qtRootBases) {
        QDir qtRoot(rootBase);
        if (!qtRoot.exists()) {
            continue;
        }
        const QStringList versions = qtRoot.entryList(QStringList() << "6.*", QDir::Dirs | QDir::NoDotAndDotDot, QDir::Reversed);
        for (const QString& ver : versions) {
            const QDir verDir(qtRoot.filePath(ver));
            const QStringList kits = verDir.entryList(QStringList() << "msvc*" << "*", QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& kit : kits) {
                if (kit.contains("msvc", Qt::CaseInsensitive)) {
                    dirs << verDir.filePath(kit + "/bin");
                }
            }
            for (const QString& kit : kits) {
                dirs << verDir.filePath(kit + "/bin");
            }
        }
    }
    return dirs;
}

} // namespace

QString resolver_raiz_projeto(const QString& origem) {
    QStringList starts;
    if (!origem.trimmed().isEmpty()) {
        starts << QFileInfo(origem).absoluteFilePath();
    } else {
        starts << QDir::currentPath();
        if (!QCoreApplication::applicationDirPath().isEmpty()) {
            starts << QCoreApplication::applicationDirPath();
        }
    }

    for (const QString& s : starts) {
        QDir dir(s);
        if (QFileInfo(s).isFile()) {
            dir = QFileInfo(s).dir();
        }
        for (int i = 0; i < 8; ++i) {
            if (pareceRaizProjeto(dir)) {
                return QDir::cleanPath(dir.absolutePath());
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }
    return QDir::cleanPath(origem.isEmpty() ? QDir::currentPath() : origem);
}

QString resolver_diretorio_traducoes(const QString& raizProjeto, const QString& overrideDir) {
    if (!overrideDir.trimmed().isEmpty()) {
        return QDir::cleanPath(QFileInfo(overrideDir).absoluteFilePath());
    }
    const QString raiz = resolver_raiz_projeto(raizProjeto);
    const QString padrao = QDir(raiz).filePath("source/locale/language");
    if (QDir(padrao).exists()) {
        return QDir::cleanPath(padrao);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QStringList rels = {
            "translations",
            "locale/language",
            "../translations",
            "../../translations",
            "../source/locale/language",
            "../../source/locale/language"
        };
        for (const QString& rel : rels) {
            const QString cand = QDir(appDir).filePath(rel);
            if (QDir(cand).exists()) {
                return QDir::cleanPath(cand);
            }
        }
    }
    return QDir::cleanPath(padrao);
}

QString localizar_lrelease(const QString& overridePath) {
    if (!overridePath.trimmed().isEmpty()) {
        QFileInfo fi(overridePath.trimmed());
        if (fi.exists() && fi.isFile()) {
            return QDir::cleanPath(fi.absoluteFilePath());
        }
    }

    const QString inPath = QStandardPaths::findExecutable("lrelease");
    if (!inPath.isEmpty()) {
        return QDir::cleanPath(inPath);
    }
    const QString inPathQt6 = QStandardPaths::findExecutable("lrelease-qt6");
    if (!inPathQt6.isEmpty()) {
        return QDir::cleanPath(inPathQt6);
    }

    const QString exeSuffix =
#ifdef Q_OS_WIN
        ".exe";
#else
        "";
#endif

    for (const QString& binDir : candidatosQtBin()) {
        const QString cand = QDir(binDir).filePath(QString("lrelease%1").arg(exeSuffix));
        if (QFileInfo::exists(cand)) {
            return QDir::cleanPath(cand);
        }
    }
    return {};
}

QStringList listar_arquivos_ts(const QString& diretorioTraducoes) {
    QDir dir(diretorioTraducoes);
    if (!dir.exists()) {
        return {};
    }
    QStringList files = dir.entryList(QStringList() << "omniframe_*.ts" << "*.ts", QDir::Files, QDir::Name);
    QStringList result;
    for (const QString& f : files) {
        const QString abs = QDir::cleanPath(dir.filePath(f));
        if (!result.contains(abs)) {
            result << abs;
        }
    }
    return result;
}

RelatorioCompilacaoTerminal executar_compilacao(
    const QString& dirTraducoes,
    const QString& lreleaseOverride,
    const QStringList& arquivosEspecificos,
    ModoExecucao modo,
    bool idiomaIngles
) {
    auto trLocal = [idiomaIngles](const QString& pt, const QString& en) -> QString {
        return idiomaIngles ? en : pt;
    };

    RelatorioCompilacaoTerminal rel;
    rel.raizProjeto = resolver_raiz_projeto(dirTraducoes);
    rel.diretorioTraducoes = resolver_diretorio_traducoes(rel.raizProjeto, dirTraducoes);
    rel.caminhoLrelease = localizar_lrelease(lreleaseOverride);

    if (modo != ModoExecucao::VerificarApenas && rel.caminhoLrelease.isEmpty()) {
        rel.sucesso = false;
        rel.mensagemGeral = trLocal(
            "Executavel lrelease.exe do Qt6 nao encontrado. Informe --lrelease ou configure QTDIR.",
            "Qt6 lrelease.exe executable was not found. Pass --lrelease or set QTDIR."
        );
        return rel;
    }

    QStringList alvos = arquivosEspecificos;
    if (alvos.isEmpty()) {
        alvos = listar_arquivos_ts(rel.diretorioTraducoes);
    }
    rel.totalEncontrados = alvos.size();

    if (alvos.isEmpty()) {
        rel.sucesso = false;
        rel.mensagemGeral = trLocal(
            QString("Nenhum arquivo .ts encontrado em: %1").arg(rel.diretorioTraducoes),
            QString("No .ts files found in: %1").arg(rel.diretorioTraducoes)
        );
        return rel;
    }

    for (const QString& tsPathRaw : alvos) {
        QFileInfo tsInfo(tsPathRaw);
        if (!tsInfo.isAbsolute()) {
            tsInfo.setFile(QDir(rel.diretorioTraducoes).filePath(tsPathRaw));
        }

        ResultadoArquivoTraducao item;
        item.arquivoTs = QDir::cleanPath(tsInfo.absoluteFilePath());
        item.arquivoQm = QDir::cleanPath(tsInfo.dir().filePath(tsInfo.completeBaseName() + ".qm"));

        if (!tsInfo.exists()) {
            item.sucesso = false;
            item.mensagem = trLocal("Arquivo .ts nao encontrado", ".ts file not found");
            ++rel.totalErros;
            rel.itens.push_back(item);
            continue;
        }

        if (modo == ModoExecucao::VerificarApenas) {
            const QFileInfo qmInfo(item.arquivoQm);
            if (qmInfo.exists() && qmInfo.size() > 0 && qmInfo.lastModified() >= tsInfo.lastModified()) {
                item.sucesso = true;
                item.atualizado = true;
                item.mensagem = trLocal("QM atualizado e valido", "QM up to date and valid");
                ++rel.totalCompilados;
            } else {
                item.sucesso = false;
                item.atualizado = false;
                item.mensagem = trLocal("QM ausente ou desatualizado", "QM missing or outdated");
                ++rel.totalErros;
            }
            rel.itens.push_back(item);
            continue;
        }

        QProcess proc;
        proc.start(rel.caminhoLrelease, QStringList() << item.arquivoTs << "-qm" << item.arquivoQm);
        if (!proc.waitForFinished(60000)) {
            item.sucesso = false;
            item.mensagem = trLocal("Timeout ao executar lrelease", "Timed out executing lrelease");
            ++rel.totalErros;
            rel.itens.push_back(item);
            continue;
        }

        QString stdoutStr = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
        const QString stderrStr = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
        const QFileInfo qmInfo(item.arquivoQm);

        if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && qmInfo.exists() && qmInfo.size() > 0) {
            item.sucesso = true;
            item.atualizado = true;
            item.mensagem = stdoutStr.isEmpty()
                ? trLocal("Compilado com sucesso", "Compiled successfully")
                : stdoutStr.replace('\n', ' ').replace('\r', ' ');
            ++rel.totalCompilados;
        } else {
            item.sucesso = false;
            item.mensagem = !stderrStr.isEmpty()
                ? stderrStr
                : trLocal("Falha na compilacao com lrelease", "Compilation failed with lrelease");
            ++rel.totalErros;
        }
        rel.itens.push_back(item);
    }

    rel.sucesso = (rel.totalErros == 0 && rel.totalCompilados > 0);
    rel.mensagemGeral = trLocal(
        QString("Traducoes processadas: %1 OK, %2 erro(s) de %3 arquivo(s).")
            .arg(rel.totalCompilados)
            .arg(rel.totalErros)
            .arg(rel.totalEncontrados),
        QString("Translations processed: %1 OK, %2 error(s) out of %3 file(s).")
            .arg(rel.totalCompilados)
            .arg(rel.totalErros)
            .arg(rel.totalEncontrados)
    );
    return rel;
}

static void imprimirAjuda() {
    std::cout << "OmniFrame 3D - CompileTranslationsTerminal (" << kFallbackAppVersion << ")\n"
              << "Uso / Usage:\n"
              << "  CompileTranslationsTerminal [opcoes]\n"
              << "  --all                  Compila todos os arquivos .ts de source/locale/language (padrao)\n"
              << "  --check                Verifica se os arquivos .qm estao presentes e atualizados\n"
              << "  --dir <caminho>        Define diretorio alternativo de traducoes\n"
              << "  --file <arquivo.ts>    Compila apenas o arquivo .ts informado (repetivel)\n"
              << "  --lrelease <caminho>   Caminho explicito para o lrelease.exe\n"
              << "  --lang <pt|en>         Idioma das mensagens de terminal\n"
              << "  --help                 Exibe esta ajuda\n";
}

int run_terminal(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("CompileTranslationsTerminal");
    QCoreApplication::setApplicationVersion(kFallbackAppVersion);

    ModoExecucao modo = ModoExecucao::CompilarTodos;
    QString dirArg;
    QString lreleaseArg;
    QStringList arquivosArg;
    bool ingles = false;

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--help" || arg == "-h") {
            imprimirAjuda();
            return 0;
        } else if (arg == "--all") {
            modo = ModoExecucao::CompilarTodos;
        } else if (arg == "--check") {
            modo = ModoExecucao::VerificarApenas;
        } else if ((arg == "--dir" || arg == "-d") && i + 1 < argc) {
            dirArg = QString::fromLocal8Bit(argv[++i]);
        } else if ((arg == "--file" || arg == "-f") && i + 1 < argc) {
            arquivosArg << QString::fromLocal8Bit(argv[++i]);
            modo = ModoExecucao::CompilarSelecionados;
        } else if (arg == "--lrelease" && i + 1 < argc) {
            lreleaseArg = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--lang" && i + 1 < argc) {
            ingles = QString::fromLocal8Bit(argv[++i]).toLower().startsWith("en");
        }
    }

    const RelatorioCompilacaoTerminal rel = executar_compilacao(dirArg, lreleaseArg, arquivosArg, modo, ingles);
    std::cout << "[CompileTranslationsTerminal] Dir: " << rel.diretorioTraducoes.toStdString() << '\n';
    if (!rel.caminhoLrelease.isEmpty()) {
        std::cout << "[CompileTranslationsTerminal] lrelease: " << rel.caminhoLrelease.toStdString() << '\n';
    }
    for (const ResultadoArquivoTraducao& item : rel.itens) {
        std::cout << "  [" << (item.sucesso ? "OK" : "ERROR") << "] "
                  << QFileInfo(item.arquivoTs).fileName().toStdString()
                  << " -> " << QFileInfo(item.arquivoQm).fileName().toStdString()
                  << " | " << item.mensagem.toStdString() << '\n';
    }
    std::cout << "[CompileTranslationsTerminal] " << rel.mensagemGeral.toStdString() << '\n';
    return rel.sucesso ? 0 : 1;
}

} // namespace mocks::compile_translations_terminal

#ifndef OMNIFRAME_COMPILE_TRANSLATIONS_NO_MAIN
int main(int argc, char* argv[]) {
    return mocks::compile_translations_terminal::run_terminal(argc, argv);
}
#endif
