#include "VersionEditor.hpp"

#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStringConverter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mocks::version_editor {

namespace {

void anexarConsolePaiParaCli() {
#ifdef _WIN32
    const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    const DWORD outType = (hOut != nullptr && hOut != INVALID_HANDLE_VALUE) ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;
    const DWORD errType = (hErr != nullptr && hErr != INVALID_HANDLE_VALUE) ? GetFileType(hErr) : FILE_TYPE_UNKNOWN;

    if (outType == FILE_TYPE_UNKNOWN || errType == FILE_TYPE_UNKNOWN) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* dummy = nullptr;
            if (outType == FILE_TYPE_UNKNOWN && freopen_s(&dummy, "CONOUT$", "w", stdout) == 0) {
                std::cout.clear();
            }
            if (errType == FILE_TYPE_UNKNOWN && freopen_s(&dummy, "CONOUT$", "w", stderr) == 0) {
                std::cerr.clear();
            }
            std::ios::sync_with_stdio(true);
        }
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

struct TextoArquivoInfo {
    QString texto;
    bool ok = false;
    bool temCrlf = false;
    bool temBom = false;
};

TextoArquivoInfo lerTextoArquivoUtf8(const QString& caminho) {
    TextoArquivoInfo info;
    QFile file(caminho);
    if (!file.open(QIODevice::ReadOnly)) {
        return info;
    }
    QByteArray bytes = file.readAll();
    info.ok = true;
    static const QByteArray kUtf8Bom("\xEF\xBB\xBF", 3);
    if (bytes.startsWith(kUtf8Bom)) {
        info.temBom = true;
        bytes = bytes.mid(kUtf8Bom.size());
    }
    info.temCrlf = bytes.contains("\r\n");
    info.texto = QString::fromUtf8(bytes);
    if (info.temCrlf) {
        info.texto.replace("\r\n", "\n");
    }
    return info;
}

bool pareceRaizOmniFrame(const QDir& dir) {
    if (!dir.exists()) {
        return false;
    }
    const bool hasCmake = dir.exists("CMakeLists.txt");
    const bool hasSource = dir.exists("source");
    const bool hasLibraryOrAssets = dir.exists(QString::fromLatin1(kLibraryDirName)) || dir.exists("source/assets");
    if (!hasCmake || !hasSource || !hasLibraryOrAssets) {
        return false;
    }
    QFile cmakeFile(dir.filePath("CMakeLists.txt"));
    if (!cmakeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString content = QString::fromUtf8(cmakeFile.readAll());
    return content.contains("OmniFrame", Qt::CaseInsensitive);
}

QString lerTextoUtf8(const QString& caminho, bool* ok = nullptr) {
    const TextoArquivoInfo info = lerTextoArquivoUtf8(caminho);
    if (ok) {
        *ok = info.ok;
    }
    return info.texto;
}

bool caminhosIguaisSistema(const QString& a, const QString& b) {
    const QString normA = QDir::cleanPath(QFileInfo(a).absoluteFilePath());
    const QString normB = QDir::cleanPath(QFileInfo(b).absoluteFilePath());
#ifdef Q_OS_WIN
    return normA.compare(normB, Qt::CaseInsensitive) == 0;
#else
    return normA.compare(normB, Qt::CaseSensitive) == 0;
#endif
}

bool ehArquivoReleaseMarkdownNome(const QString& fileName) {
    return fileName.startsWith(QStringLiteral("release-v"), Qt::CaseInsensitive) &&
           fileName.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive);
}

bool escreverTextoArquivoUtf8(const QString& caminho, const QString& conteudo, const TextoArquivoInfo& meta) {
    QString textoFinal = conteudo;
    textoFinal.replace("\r\n", "\n");
    if (meta.temCrlf) {
        textoFinal.replace('\n', "\r\n");
    }
    QFileInfo info(caminho);
    QDir().mkpath(info.absolutePath());
    QSaveFile file(caminho);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QByteArray encoded = textoFinal.toUtf8();
    if (meta.temBom) {
        encoded.prepend(QByteArray("\xEF\xBB\xBF", 3));
    }
    const qint64 written = file.write(encoded);
    if (written != encoded.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

struct ContagemSubstituicao {
    int encontrados = 0;
    int alterados = 0;

    void adicionar(const ContagemSubstituicao& outro) {
        encontrados += outro.encontrados;
        alterados += outro.alterados;
    }
};

QString expandirSubstituicaoSegura(const QString& templateStr, const QRegularExpressionMatch& match) {
    QString out;
    out.reserve(templateStr.size() + 32);
    for (int i = 0; i < templateStr.size(); ++i) {
        if (templateStr[i] == '\\' && i + 1 < templateStr.size() && templateStr[i + 1].isDigit()) {
            const int groupIdx = templateStr[i + 1].digitValue();
            if (groupIdx >= 0 && groupIdx <= match.lastCapturedIndex()) {
                out += match.captured(groupIdx);
                ++i;
                continue;
            }
        }
        out += templateStr[i];
    }
    return out;
}

ContagemSubstituicao substituirComRegex(
    QString& texto,
    const QString& padrao,
    const QString& substituicao,
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption
) {
    const QRegularExpression rx(padrao, options);
    ContagemSubstituicao cont;
    QString resultado;
    qsizetype lastPos = 0;
    QRegularExpressionMatchIterator it = rx.globalMatch(texto);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        ++cont.encontrados;
        const QString rep = expandirSubstituicaoSegura(substituicao, match);
        if (rep != match.captured(0)) {
            ++cont.alterados;
        }
        resultado += texto.mid(lastPos, match.capturedStart() - lastPos);
        resultado += rep;
        lastPos = match.capturedEnd();
    }
    if (cont.alterados > 0) {
        resultado += texto.mid(lastPos);
        texto = resultado;
    }
    return cont;
}

bool validarCalVerPartes(int ano, int mes, int dia) {
    return ano >= 2000 && ano <= 2099 && QDate(ano, mes, dia).isValid();
}

ContagemSubstituicao substituirVersoesCalVer(QString& texto, const QString& versaoNormalizada) {
    // Atualiza estritamente versoes no formato [v]20XX.M.D.R (nunca 6.11.1, 3.4.0, 10.0.17763.0, 2.1 etc.).
    static const QRegularExpression rxCalVer(
        R"((?<![0-9A-Za-z.])([vV]?)(20\d{2})\.(\d{1,2})\.(\d{1,2})\.(\d+)(?![0-9A-Za-z_]|\.\d))"
    );
    ContagemSubstituicao cont;
    QString resultado;
    qsizetype lastPos = 0;
    QRegularExpressionMatchIterator it = rxCalVer.globalMatch(texto);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int ano = match.captured(2).toInt();
        const int mes = match.captured(3).toInt();
        const int dia = match.captured(4).toInt();
        if (!validarCalVerPartes(ano, mes, dia)) {
            continue;
        }
        ++cont.encontrados;
        const QString novoTrecho = match.captured(1) + versaoNormalizada;
        if (novoTrecho != match.captured(0)) {
            ++cont.alterados;
        }
        resultado += texto.mid(lastPos, match.capturedStart() - lastPos);
        resultado += novoTrecho;
        lastPos = match.capturedEnd();
    }
    if (cont.alterados > 0) {
        resultado += texto.mid(lastPos);
        texto = resultado;
    }
    return cont;
}

QString detectarReleaseMarkdownRelativo(const QDir& rootDir) {
    QDir docsDir(rootDir.filePath("docs"));
    if (docsDir.exists()) {
        const QStringList allMd = docsDir.entryList(QStringList() << "*.md", QDir::Files, QDir::Name);
        QStringList candidates;
        candidates.reserve(allMd.size());
        for (const QString& name : allMd) {
            if (ehArquivoReleaseMarkdownNome(name)) {
                candidates << name;
            }
        }
        if (!candidates.isEmpty()) {
            static const QRegularExpression rxVer(
                R"(^release-v(20\d{2})\.(\d{1,2})\.(\d{1,2})\.(\d+)\.md$)",
                QRegularExpression::CaseInsensitiveOption
            );
            QString bestFile = candidates.first();
            bool bestHasTuple = false;
            QVector<int> bestTuple = {0, 0, 0, 0};

            for (const QString& candidate : candidates) {
                const QRegularExpressionMatch m = rxVer.match(candidate);
                if (m.hasMatch() && validarCalVerPartes(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt())) {
                    const QVector<int> tuple = {
                        m.captured(1).toInt(),
                        m.captured(2).toInt(),
                        m.captured(3).toInt(),
                        m.captured(4).toInt()
                    };
                    bool isNewer = !bestHasTuple;
                    if (!isNewer) {
                        for (int i = 0; i < 4; ++i) {
                            if (tuple[i] != bestTuple[i]) {
                                isNewer = (tuple[i] > bestTuple[i]);
                                break;
                            }
                        }
                    }
                    if (isNewer) {
                        bestHasTuple = true;
                        bestTuple = tuple;
                        bestFile = candidate;
                    }
                } else if (!bestHasTuple && candidate > bestFile) {
                    bestFile = candidate;
                }
            }
            return QString("docs/%1").arg(bestFile);
        }
    }
    return QString("docs/release-v%1.md").arg(kDefaultAppVersion);
}

ContagemSubstituicao substituirDatasLineStartValidas(
    QString& texto,
    const QString& padrao,
    const QString& novaDataFormatada
) {
    const QRegularExpression rx(padrao, QRegularExpression::MultilineOption | QRegularExpression::UseUnicodePropertiesOption);
    ContagemSubstituicao cont;
    QString resultado;
    qsizetype lastPos = 0;
    QRegularExpressionMatchIterator it = rx.globalMatch(texto);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        QDate parsed;
        if (!parsear_data_flexivel(match.captured(2), parsed)) {
            continue;
        }
        ++cont.encontrados;
        const QString rep = match.captured(1) + novaDataFormatada + match.captured(3);
        if (rep != match.captured(0)) {
            ++cont.alterados;
        }
        resultado += texto.mid(lastPos, match.capturedStart() - lastPos);
        resultado += rep;
        lastPos = match.capturedEnd();
    }
    if (cont.alterados > 0) {
        resultado += texto.mid(lastPos);
        texto = resultado;
    }
    return cont;
}

ContagemSubstituicao substituirDatasMarkdown(
    QString& texto,
    const QString& dataPtBr,
    const QString& dataEnUs,
    const QString& dataIso
) {
    ContagemSubstituicao cont;
    cont.adicionar(substituirComRegex(
        texto,
        R"((\*\*Data:\*\*\s*`)([^`]+)(`))",
        QString("\\1%1\\3").arg(dataPtBr)
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((\*\*Date:\*\*\s*`)([^`]+)(`))",
        QString("\\1%1\\3").arg(dataEnUs)
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((\*\*Data\s*/\s*Date:\*\*\s*`)([^`]+)(`\s*/\s*`)([^`]+)(`))",
        QString("\\1%1\\3%2\\5").arg(dataPtBr, dataEnUs)
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((^\*\*Data de Lan[cç]amento:\*\*\s*)([^\r\n]*?\S)( *)$)",
        QString("\\1%1\\3").arg(dataPtBr),
        QRegularExpression::MultilineOption
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((^\*\*Release Date:\*\*\s*)([^\r\n]*?\S)( *)$)",
        QString("\\1%1\\3").arg(dataEnUs),
        QRegularExpression::MultilineOption
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((Data\s+t[ée]cnica\s+desta\s+revis[aã]o:\s*)([^<\r\n]+))",
        QString("\\1%1").arg(dataPtBr)
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((Technical\s+revision\s+date:\s*)([^<\r\n]+))",
        QString("\\1%1").arg(dataEnUs)
    ));

    const ContagemSubstituicao obsPtBacktick = substituirComRegex(
        texto,
        R"((Data\s+do\s+snapshot\s+t[ée]cnico:\s*`)([^`]+)(`))",
        QString("\\1%1\\3").arg(dataPtBr)
    );
    cont.adicionar(obsPtBacktick);
    if (obsPtBacktick.encontrados == 0) {
        cont.adicionar(substituirComRegex(
            texto,
            R"((^\s*Data\s+do\s+snapshot\s+t[ée]cnico:\s*)([^\r\n]*?\S)( *)$)",
            QString("\\1%1\\3").arg(dataPtBr),
            QRegularExpression::MultilineOption
        ));
    }

    const ContagemSubstituicao obsEnBacktick = substituirComRegex(
        texto,
        R"((Technical\s+Snapshot\s+Date:\s*`)([^`]+)(`))",
        QString("\\1%1\\3").arg(dataEnUs)
    );
    cont.adicionar(obsEnBacktick);
    if (obsEnBacktick.encontrados == 0) {
        cont.adicionar(substituirComRegex(
            texto,
            R"((^\s*Technical\s+Snapshot\s+Date:\s*)([^\r\n]*?\S)( *)$)",
            QString("\\1%1\\3").arg(dataEnUs),
            QRegularExpression::MultilineOption
        ));
    }

    cont.adicionar(substituirComRegex(
        texto,
        R"(((?:--date|-d)(?:\s+|=))(\d{4}-\d{2}-\d{2}))",
        QString("\\1%1").arg(dataIso)
    ));
    return cont;
}

ContagemSubstituicao substituirDatasTextoAssets(
    QString& texto,
    const QString& dataPtBr,
    const QString& dataEnUs,
    const QString& dataIso
) {
    ContagemSubstituicao cont;
    cont.adicionar(substituirComRegex(
        texto,
        R"((^[ÚU]ltima\s+atualiza[cç][aã]o:\s*)([^\r\n]*?\S)( *)$)",
        QString("\\1%1\\3").arg(dataPtBr),
        QRegularExpression::MultilineOption | QRegularExpression::UseUnicodePropertiesOption
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((^Last\s+Updated:\s*)([^\r\n]*?\S)( *)$)",
        QString("\\1%1\\3").arg(dataEnUs),
        QRegularExpression::MultilineOption
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((^Vers[aã]o\s+[vV]?20\d{2}\.\d{1,2}\.\d{1,2}\.\d+,\s*)([^\r\n]*?\S)( *)$)",
        QString("\\1%1\\3").arg(dataPtBr),
        QRegularExpression::MultilineOption
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((^Version\s+[vV]?20\d{2}\.\d{1,2}\.\d{1,2}\.\d+,\s*)([^\r\n]*?\S)( *)$)",
        QString("\\1%1\\3").arg(dataEnUs),
        QRegularExpression::MultilineOption
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((^Vers[aã]o:\s*[vV]?20\d{2}\.\d{1,2}\.\d{1,2}\.\d+\s*\|\s*Data:\s*)([^|\r\n]*?\S)(\s*(?:\||$)))",
        QString("\\1%1\\3").arg(dataPtBr),
        QRegularExpression::MultilineOption
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"((^Version:\s*[vV]?20\d{2}\.\d{1,2}\.\d{1,2}\.\d+\s*\|\s*Date:\s*)([^|\r\n]*?\S)(\s*(?:\||$)))",
        QString("\\1%1\\3").arg(dataEnUs),
        QRegularExpression::MultilineOption
    ));
    cont.adicionar(substituirDatasLineStartValidas(
        texto,
        R"((^Data:\s*)([^\r\n]*?\S)( *)$)",
        dataPtBr
    ));
    cont.adicionar(substituirDatasLineStartValidas(
        texto,
        R"((^Date:\s*)([^\r\n]*?\S)( *)$)",
        dataEnUs
    ));
    cont.adicionar(substituirComRegex(
        texto,
        R"(((?:--date|-d)(?:\s+|=))(\d{4}-\d{2}-\d{2}))",
        QString("\\1%1").arg(dataIso)
    ));
    return cont;
}

QString resumirTransformacao(
    OperacaoModo modo,
    IdiomaEditor idioma,
    int altVersao,
    int altData,
    int camposMonitorados,
    bool renomeadoRelease
) {
    auto trLocal = [idioma](const QString& pt, const QString& en) -> QString {
        return idioma == IdiomaEditor::Portugues ? pt : en;
    };

    const int totalAlt = altVersao + altData;
    if (totalAlt > 0 || renomeadoRelease) {
        if (modo == OperacaoModo::Check) {
            if (altVersao > 0 && altData > 0) {
                return trLocal(
                    QString("Requer sincronização (%1 versão(ões), %2 data(s))").arg(altVersao).arg(altData),
                    QString("Requires sync (%1 version(s), %2 date(s))").arg(altVersao).arg(altData)
                );
            }
            if (altVersao > 0) {
                return renomeadoRelease
                    ? trLocal(
                        QString("Requer sincronização de versão (%1 ajuste(s)) e renomeação do release").arg(altVersao),
                        QString("Requires version sync (%1 change(s)) and release filename rename").arg(altVersao)
                    )
                    : trLocal(
                        QString("Requer sincronização de versão (%1 ajuste(s))").arg(altVersao),
                        QString("Requires version sync (%1 change(s))").arg(altVersao)
                    );
            }
            if (altData > 0) {
                return trLocal(
                    QString("Requer sincronização de data (%1 ajuste(s))").arg(altData),
                    QString("Requires date sync (%1 change(s))").arg(altData)
                );
            }
            return trLocal(
                "Requer renomeação do documento de lançamento para a versão alvo",
                "Requires release notes filename rename to target version"
            );
        }

        if (altVersao > 0 && altData > 0) {
            return trLocal(
                QString("Versão e data sincronizadas (%1 ajuste(s))").arg(totalAlt),
                QString("Version and date synchronized (%1 change(s))").arg(totalAlt)
            );
        }
        if (altVersao > 0) {
            return renomeadoRelease
                ? trLocal(
                    QString("Versão sincronizada (%1 ajuste(s)) e arquivo de lançamento renomeado").arg(altVersao),
                    QString("Version synchronized (%1 change(s)) and release file renamed").arg(altVersao)
                )
                : trLocal(
                    QString("Versão sincronizada (%1 ajuste(s); arquivo sem alteração de data)").arg(altVersao),
                    QString("Version synchronized (%1 change(s); no date changes needed)").arg(altVersao)
                );
        }
        if (altData > 0) {
            return trLocal(
                QString("Data sincronizada (%1 ajuste(s); versão já alinhada)").arg(altData),
                QString("Date synchronized (%1 change(s); version already aligned)").arg(altData)
            );
        }
        return trLocal(
            "Arquivo de lançamento renomeado para a versão alvo",
            "Release notes file renamed to target version"
        );
    }

    if (camposMonitorados > 0) {
        return trLocal(
            QString("Já sincronizado (%1 campo(s) monitorado(s))").arg(camposMonitorados),
            QString("Already aligned (%1 monitored field(s))").arg(camposMonitorados)
        );
    }

    return trLocal(
        "Auditado (nenhum campo [v]YYYY.M.D.R ou data presente)",
        "Audited (no [v]YYYY.M.D.R or date fields present)"
    );
}

struct ResultadoTransformacao {
    bool mudou = false;
    int alteracoesVersao = 0;
    int alteracoesData = 0;
    int camposMonitorados = 0;
};

ResultadoTransformacao aplicarTransformacaoArquivo(
    const QString& relPath,
    const QString& versao,
    const QString& dataPtBr,
    const QString& dataEnUs,
    const QString& dataIso,
    QString& texto
) {
    const QString textoAntes = texto;
    ContagemSubstituicao contVersao;
    ContagemSubstituicao contData;

    if (relPath == QString::fromLatin1(kAppxManifestRelPath)) {
        contVersao.adicionar(substituirComRegex(
            texto,
            R"((<Identity\b[^>]*\bVersion=")([vV]?)(20\d{2}\.\d{1,2}\.\d{1,2}\.\d+)("))",
            QString("\\1\\2%1\\4").arg(versao)
        ));
    } else if (relPath == "mocks/VersionEditor.hpp") {
        contVersao.adicionar(substituirVersoesCalVer(texto, versao));
        contData.adicionar(substituirComRegex(
            texto,
            R"((kDefaultReleaseDate\s*=\s*")(\d{4}-\d{2}-\d{2})(";))",
            QString("\\1%1\\3").arg(dataIso)
        ));
    } else if (
        relPath == "README.md" ||
        relPath == "MANUAL.md" ||
        relPath == "OBSERVACAO.md" ||
        relPath == "SECURITY.md" ||
        relPath == "CODE_OF_CONDUCT.md" ||
        relPath == "CONTRIBUTING.md" ||
        relPath == ".github/ISSUE_TEMPLATE/bug_report.md" ||
        (relPath.startsWith("docs/") && relPath.endsWith(".md"))
    ) {
        contVersao.adicionar(substituirVersoesCalVer(texto, versao));
        contData.adicionar(substituirDatasMarkdown(texto, dataPtBr, dataEnUs, dataIso));
    } else if (
        relPath == "CMakeLists.txt" ||
        relPath == "source/main.cpp" ||
        relPath == "source/ui/AboutDialog.cpp" ||
        relPath == "source/ui/AboutDialog.hpp" ||
        relPath == "source/ui/ManualDialog.hpp" ||
        relPath == "source/ui/ManualDialog.cpp" ||
        relPath == "mocks/CompileTranslationsTerminal.hpp" ||
        (relPath.startsWith(QString::fromLatin1(kLibraryDirName) + QLatin1Char('/')) && relPath.endsWith(QStringLiteral(".iss")))
    ) {
        contVersao.adicionar(substituirVersoesCalVer(texto, versao));
    } else {
        // mocks/INSTRUCOES_find_VersionEditor.txt, library/OmniFrame_cpp.txt e source/assets/**
        contVersao.adicionar(substituirVersoesCalVer(texto, versao));
        contData.adicionar(substituirDatasTextoAssets(texto, dataPtBr, dataEnUs, dataIso));
    }

    ResultadoTransformacao res;
    res.mudou = (texto != textoAntes);
    res.alteracoesVersao = contVersao.alterados;
    res.alteracoesData = contData.alterados;
    res.camposMonitorados = contVersao.encontrados + contData.encontrados;
    return res;
}

} // namespace

QString pasta_padrao_icones(const QString& raizProjeto) {
    QStringList candidatos;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidatos << QDir(appDir).filePath("assets/icons");
        candidatos << QDir(appDir).filePath("icons");
        candidatos << QDir(appDir).filePath("source/assets/icons");
        QDir walk(appDir);
        for (int i = 0; i < 6; ++i) {
            candidatos << walk.filePath("source/assets/icons");
            candidatos << walk.filePath("assets/icons");
            if (!walk.cdUp()) {
                break;
            }
        }
    }
    if (!raizProjeto.isEmpty()) {
        candidatos << QDir(raizProjeto).filePath("source/assets/icons");
        candidatos << QDir(raizProjeto).filePath("assets/icons");
    }
    candidatos << QDir::current().filePath("source/assets/icons");
    candidatos << QDir::current().filePath("assets/icons");

    for (const QString& dirPath : candidatos) {
        QDir d(dirPath);
        if (d.exists("omniframe.ico") || d.exists("omniframe.png")) {
            return d.absolutePath();
        }
    }
    return {};
}

QIcon obter_icone_aplicacao(const QString& raizProjeto) {
    QStringList candidatos;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidatos << QDir(appDir).filePath("assets/icons/omniframe.ico")
                   << QDir(appDir).filePath("assets/icons/omniframe.png")
                   << QDir(appDir).filePath("icons/omniframe.ico")
                   << QDir(appDir).filePath("icons/omniframe.png")
                   << QDir(appDir).filePath("../assets/icons/omniframe.ico")
                   << QDir(appDir).filePath("../assets/icons/omniframe.png");
    }
    candidatos << QStringLiteral(":/mocks/icons/omniframe.ico")
               << QStringLiteral(":/mocks/icons/omniframe.png");

    const QString iconDir = pasta_padrao_icones(raizProjeto);
    if (!iconDir.isEmpty()) {
        candidatos << QDir(iconDir).filePath("omniframe.ico")
                   << QDir(iconDir).filePath("omniframe.png");
    }

    for (const QString& cand : candidatos) {
        if (QFileInfo::exists(cand)) {
            QIcon icon(cand);
            if (!icon.isNull() && !icon.pixmap(32, 32).isNull()) {
                return icon;
            }
        }
    }
    return {};
}

QString resolver_pasta_raiz_projeto(const QString& caminhoInicial) {
    QStringList origens;
    const QString caminhoLimpo = caminhoInicial.trimmed();
    if (!caminhoLimpo.isEmpty()) {
        origens << QFileInfo(caminhoLimpo).absoluteFilePath();
    } else {
        origens << QDir::currentPath();
        if (!QCoreApplication::applicationDirPath().isEmpty()) {
            origens << QCoreApplication::applicationDirPath();
        }
    }

    for (const QString& origem : origens) {
        QDir dir(origem);
        if (QFileInfo(origem).isFile()) {
            dir = QFileInfo(origem).dir();
        }
        for (int i = 0; i < 8; ++i) {
            if (pareceRaizOmniFrame(dir)) {
                return QDir::cleanPath(dir.absolutePath());
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }
    return QDir::cleanPath(caminhoLimpo.isEmpty() ? QDir::currentPath() : caminhoLimpo);
}

QString normalizar_versao(const QString& versao) {
    static const QRegularExpression rx(R"(^[vV]?(20\d{2})\.(\d{1,2})\.(\d{1,2})\.(\d+)$)");
    const QString v = versao.trimmed();
    const QRegularExpressionMatch m = rx.match(v);
    if (!m.hasMatch()) {
        return {};
    }
    const int ano = m.captured(1).toInt();
    const int mes = m.captured(2).toInt();
    const int dia = m.captured(3).toInt();
    if (!validarCalVerPartes(ano, mes, dia)) {
        return {};
    }
    return QString("%1.%2.%3.%4")
        .arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4));
}

bool validar_formato_versao(const QString& versao) {
    return !normalizar_versao(versao).isEmpty();
}

QString formatar_data_pt_br(const QDate& data) {
    if (!data.isValid()) {
        return {};
    }
    static const QStringList mesesPt = {
        "",
        "janeiro",
        "fevereiro",
        "março",
        "abril",
        "maio",
        "junho",
        "julho",
        "agosto",
        "setembro",
        "outubro",
        "novembro",
        "dezembro"
    };
    return QString("%1 de %2 de %3")
        .arg(data.day())
        .arg(mesesPt.value(data.month()))
        .arg(data.year());
}

QString formatar_data_en_us(const QDate& data) {
    if (!data.isValid()) {
        return {};
    }
    static const QStringList mesesEn = {
        "",
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December"
    };
    return QString("%1 %2, %3")
        .arg(mesesEn.value(data.month()))
        .arg(data.day())
        .arg(data.year());
}

int mesPorNomePt(const QString& mesLower) {
    static const QStringList mesesCompletos = {
        "", "janeiro", "fevereiro", "março", "abril", "maio", "junho",
        "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"
    };
    int idx = mesesCompletos.indexOf(mesLower);
    if (idx > 0) {
        return idx;
    }
    if (mesLower == "marco") {
        return 3;
    }
    static const QStringList mesesAbrev = {
        "", "jan", "fev", "mar", "abr", "mai", "jun",
        "jul", "ago", "set", "out", "nov", "dez"
    };
    return mesesAbrev.indexOf(mesLower);
}

int mesPorNomeEn(const QString& mesLower) {
    static const QStringList mesesCompletos = {
        "", "january", "february", "march", "april", "may", "june",
        "july", "august", "september", "october", "november", "december"
    };
    const int idx = mesesCompletos.indexOf(mesLower);
    if (idx > 0) {
        return idx;
    }
    if (mesLower == "sept") {
        return 9;
    }
    static const QStringList mesesAbrev = {
        "", "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec"
    };
    return mesesAbrev.indexOf(mesLower);
}

bool parsear_data_flexivel(const QString& texto, QDate& dataOut) {
    const QString clean = texto.trimmed();
    if (clean.isEmpty()) {
        return false;
    }

    QDate d = QDate::fromString(clean, Qt::ISODate);
    if (d.isValid()) {
        dataOut = d;
        return true;
    }
    for (const char* fmt : {"dd/MM/yyyy", "d/M/yyyy", "MM/dd/yyyy", "M/d/yyyy"}) {
        d = QDate::fromString(clean, fmt);
        if (d.isValid()) {
            dataOut = d;
            return true;
        }
    }

    static const QRegularExpression rxPt(
        R"(^(\d{1,2})\s+de\s+([\p{L}]+)\s+de\s+(\d{4})$)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption
    );
    const QRegularExpressionMatch mPt = rxPt.match(clean);
    if (mPt.hasMatch()) {
        const int mesIdx = mesPorNomePt(mPt.captured(2).toLower());
        if (mesIdx > 0) {
            d = QDate(mPt.captured(3).toInt(), mesIdx, mPt.captured(1).toInt());
            if (d.isValid()) {
                dataOut = d;
                return true;
            }
        }
    }

    static const QRegularExpression rxEnMonthFirst(
        R"(^([A-Za-z]+)\.?\s+(\d{1,2}),?\s+(\d{4})$)",
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpressionMatch mEn = rxEnMonthFirst.match(clean);
    if (mEn.hasMatch()) {
        const int mesIdx = mesPorNomeEn(mEn.captured(1).toLower());
        if (mesIdx > 0) {
            d = QDate(mEn.captured(3).toInt(), mesIdx, mEn.captured(2).toInt());
            if (d.isValid()) {
                dataOut = d;
                return true;
            }
        }
    }

    static const QRegularExpression rxEnDayFirst(
        R"(^(\d{1,2})\s+([A-Za-z]+)\.?\s+(\d{4})$)",
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpressionMatch mEnDay = rxEnDayFirst.match(clean);
    if (mEnDay.hasMatch()) {
        const int mesIdx = mesPorNomeEn(mEnDay.captured(2).toLower());
        if (mesIdx > 0) {
            d = QDate(mEnDay.captured(3).toInt(), mesIdx, mEnDay.captured(1).toInt());
            if (d.isValid()) {
                dataOut = d;
                return true;
            }
        }
    }

    return false;
}

QString ler_versao_atual(const QString& raizProjeto) {
    QDir rootDir(resolver_pasta_raiz_projeto(raizProjeto));
    bool ok = false;
    const QString cmakeText = lerTextoUtf8(rootDir.filePath("CMakeLists.txt"), &ok);
    if (ok) {
        static const QRegularExpression rx(R"(project\s*\(\s*OmniFrame\s+VERSION\s+([vV]?20\d{2}\.\d{1,2}\.\d{1,2}\.\d+))");
        const QRegularExpressionMatch match = rx.match(cmakeText);
        if (match.hasMatch()) {
            const QString norm = normalizar_versao(match.captured(1));
            if (!norm.isEmpty()) {
                return norm;
            }
        }
    }
    const QString aboutText = lerTextoUtf8(rootDir.filePath("source/ui/AboutDialog.cpp"), &ok);
    if (ok) {
        static const QRegularExpression rxAbout(R"re(kFallbackAppVersion\s*=\s*"([vV]?20\d{2}\.\d{1,2}\.\d{1,2}\.\d+)")re");
        const QRegularExpressionMatch match = rxAbout.match(aboutText);
        if (match.hasMatch()) {
            const QString norm = normalizar_versao(match.captured(1));
            if (!norm.isEmpty()) {
                return norm;
            }
        }
    }
    return QString::fromLatin1(kDefaultAppVersion);
}

QDate ler_data_atual(const QString& raizProjeto) {
    QDir rootDir(resolver_pasta_raiz_projeto(raizProjeto));
    struct FonteData {
        const char* caminhoRelativo;
        const char* padrao;
    };
    static constexpr FonteData kFontesData[] = {
        {"source/assets/PRIVACY_POLICY/Privacy_Policy_en_US.txt", R"(^Last\s+Updated:\s*([^\r\n]+))"},
        {"source/assets/PRIVACY_POLICY/Privacy_Policy_pt_BR.txt", R"(^[ÚU]ltima\s+atualiza[cç][aã]o:\s*([^\r\n]+))"},
        {"source/assets/EULA/EULA_en_US.txt", R"(^Version\s+[vV]?20\d{2}\.\d{1,2}\.\d{1,2}\.\d+,\s*([^\r\n]+))"},
        {"source/assets/MANUAL/MANUAL_en_US.txt", R"(^Version:\s*[vV]?20\d{2}\.\d{1,2}\.\d{1,2}\.\d+\s*\|\s*Date:\s*([^|\r\n]+))"},
        {"MANUAL.md", R"(\*\*Data / Date:\*\*\s*`([^`]+)`)"},
        {"OBSERVACAO.md", R"(Technical Snapshot Date:\s*`([^`]+)`)"},
        {"docs/architecture.md", R"(\*\*Date:\*\*\s*`([^`]+)`)"}
    };

    for (const FonteData& fonte : kFontesData) {
        bool ok = false;
        const QString conteudo = lerTextoUtf8(rootDir.filePath(QString::fromUtf8(fonte.caminhoRelativo)), &ok);
        if (!ok) {
            continue;
        }
        const QRegularExpression rx(
            QString::fromUtf8(fonte.padrao),
            QRegularExpression::MultilineOption | QRegularExpression::UseUnicodePropertiesOption
        );
        const QRegularExpressionMatch m = rx.match(conteudo);
        QDate parsed;
        if (m.hasMatch() && parsear_data_flexivel(m.captured(1), parsed)) {
            return parsed;
        }
    }

    const QDate fallback = QDate::fromString(QString::fromLatin1(kDefaultReleaseDate), Qt::ISODate);
    return fallback.isValid() ? fallback : QDate(2026, 11, 13);
}

QVector<ArquivoAlvoInfo> listar_arquivos_alvo(const QString& raizProjeto) {
    const QDir rootDir(resolver_pasta_raiz_projeto(raizProjeto));
    const QString releaseDoc = detectarReleaseMarkdownRelativo(rootDir);

    QVector<ArquivoAlvoInfo> lista;
    QSet<QString> vistos;

    auto adicionarUnico = [&lista, &vistos](const ArquivoAlvoInfo& info) {
        const QString caminhoLimpo = QDir::cleanPath(info.caminhoRelativo);
#ifdef Q_OS_WIN
        const QString chave = caminhoLimpo.toLower();
#else
        const QString chave = caminhoLimpo;
#endif
        if (vistos.contains(chave)) {
            return;
        }
        vistos.insert(chave);
        ArquivoAlvoInfo copia = info;
        copia.caminhoRelativo = caminhoLimpo;
        lista.push_back(copia);
    };

    auto adicionarDiretorio = [&rootDir, &adicionarUnico](
        const QString& dirRelativo,
        const QStringList& extensoes,
        const QString& catPt,
        const QString& catEn,
        const QString& descPt,
        const QString& descEn,
        const QStringList& padroesFallback = {},
        bool ignorarReleasesHistoricos = false
    ) {
        const QDir subDir(rootDir.filePath(dirRelativo));
        if (subDir.exists()) {
            const QStringList arquivos = subDir.entryList(extensoes, QDir::Files, QDir::Name);
            for (const QString& nome : arquivos) {
                if (ignorarReleasesHistoricos && ehArquivoReleaseMarkdownNome(nome)) {
                    continue;
                }
                adicionarUnico({
                    QString("%1/%2").arg(dirRelativo, nome),
                    catPt,
                    catEn,
                    descPt,
                    descEn
                });
            }
            if (!arquivos.isEmpty()) {
                return;
            }
        }
        for (const QString& fallbackRel : padroesFallback) {
            adicionarUnico({fallbackRel, catPt, catEn, descPt, descEn});
        }
    };

    // Build / CMake e código-fonte C++
    adicionarUnico({"CMakeLists.txt", "Build / CMake", "Build / CMake", "Versão principal do projeto (project OmniFrame VERSION)", "Root project version (project OmniFrame VERSION)"});
    adicionarUnico({"source/main.cpp", "Código-fonte C++", "C++ Source", "Metadado QCoreApplication::setApplicationVersion", "QCoreApplication::setApplicationVersion metadata"});
    adicionarUnico({"source/ui/AboutDialog.hpp", "Interface C++", "C++ UI Source", "Cabeçalho do diálogo Sobre (auditoria de versão)", "About dialog header (version audit)"});
    adicionarUnico({"source/ui/AboutDialog.cpp", "Interface C++", "C++ UI Source", "Versão de fallback do diálogo Sobre", "About dialog fallback version"});
    adicionarUnico({"source/ui/ManualDialog.hpp", "Interface C++", "C++ UI Source", "Cabeçalho do diálogo Manual (auditoria de versão)", "Manual dialog header (version audit)"});
    adicionarUnico({"source/ui/ManualDialog.cpp", "Interface C++", "C++ UI Source", "Implementação do diálogo Manual (auditoria de versão)", "Manual dialog implementation (version audit)"});

    // Mocks / Ferramentas auxiliares
    adicionarUnico({"mocks/VersionEditor.hpp", "Mocks / Ferramentas", "Mocks / Tools", "Constantes padrão de versão e data do VersionEditor", "VersionEditor default version and date constants"});
    adicionarUnico({"mocks/CompileTranslationsTerminal.hpp", "Mocks / Ferramentas", "Mocks / Tools", "Versão de fallback do compilador de traduções", "Translations compiler fallback version"});
    adicionarUnico({"mocks/INSTRUCOES_find_VersionEditor.txt", "Mocks / Ferramentas", "Mocks / Tools", "Instruções CLI com exemplos de versão e data", "CLI instructions with version and date examples"});

    // Documentação raiz e políticas do repositório
    adicionarUnico({"README.md", "Docs / Raiz", "Docs / Root", "Badges de release, links e notas de versão (PT-BR / EN-US)", "Release badges, links, and version notes (PT-BR / EN-US)"});
    adicionarUnico({"MANUAL.md", "Docs / Raiz", "Docs / Root", "Cabeçalho bilíngue de versão e data do manual", "Bilingual manual version and date header"});
    adicionarUnico({"OBSERVACAO.md", "Docs / Raiz", "Docs / Root", "Snapshot técnico e notas de versão (PT-BR / EN-US)", "Technical snapshot and release reference (PT-BR / EN-US)"});
    adicionarUnico({"SECURITY.md", "Docs / Raiz", "Docs / Root", "Política de segurança (atualiza apenas [v]YYYY.M.D.R)", "Security policy (updates only [v]YYYY.M.D.R)"});
    adicionarUnico({"CODE_OF_CONDUCT.md", "Docs / Raiz", "Docs / Root", "Código de Conduta (auditoria [v]YYYY.M.D.R)", "Code of Conduct ([v]YYYY.M.D.R audit)"});
    adicionarUnico({"CONTRIBUTING.md", "Docs / Raiz", "Docs / Root", "Guia de Contribuição (auditoria [v]YYYY.M.D.R)", "Contributing Guide ([v]YYYY.M.D.R audit)"});

    // Diretório docs/ completo (todos os *.md, garantindo prioridade descritiva ao documento de release)
    adicionarUnico({"docs/architecture.md", "Docs / Técnicos", "Docs / Technical", "Metadados de versão e data (PT-BR / EN-US)", "Version and date metadata (PT-BR / EN-US)"});
    adicionarUnico({"docs/cli-reference.md", "Docs / Técnicos", "Docs / Technical", "Metadados e exemplos CLI de versão e data (PT-BR / EN-US)", "CLI reference version, date metadata, and examples (PT-BR / EN-US)"});
    adicionarUnico({"docs/compliance.md", "Docs / Técnicos", "Docs / Technical", "Metadados de versão e data (PT-BR / EN-US)", "Version and date metadata (PT-BR / EN-US)"});
    adicionarUnico({"docs/licensing.md", "Docs / Técnicos", "Docs / Technical", "Metadados de versão e data (PT-BR / EN-US)", "Version and date metadata (PT-BR / EN-US)"});
    adicionarUnico({"docs/privacy-policy.md", "Docs / Técnicos", "Docs / Technical", "Metadados de versão e data da política de privacidade (PT-BR / EN-US)", "Privacy policy version and date metadata (PT-BR / EN-US)"});
    adicionarUnico({"docs/security-model.md", "Docs / Técnicos", "Docs / Technical", "Metadados de versão e data (PT-BR / EN-US)", "Version and date metadata (PT-BR / EN-US)"});
    adicionarUnico({"docs/suggested-issues.md", "Docs / Técnicos", "Docs / Technical", "Sugestões e issues técnicas (auditoria [v]YYYY.M.D.R)", "Technical issues template ([v]YYYY.M.D.R audit)"});
    adicionarUnico({"docs/user-guide.md", "Docs / Técnicos", "Docs / Technical", "Metadados de versão e data (PT-BR / EN-US)", "Version and date metadata (PT-BR / EN-US)"});
    adicionarUnico({releaseDoc, "Docs / Lançamento", "Docs / Release", "Documento de lançamento da versão atual", "Current version release notes markdown"});
    adicionarDiretorio(
        "docs",
        {"*.md"},
        "Docs / Técnicos",
        "Docs / Technical",
        "Documentação técnica em docs/ (versão e data)",
        "Technical documentation under docs/ (version and date)",
        {},
        true
    );

    // Templates GitHub
    adicionarUnico({".github/ISSUE_TEMPLATE/bug_report.md", "GitHub / Templates", "GitHub / Templates", "Exemplo de versão no template de bug (PT-BR / EN-US)", "Version example in bug report template (PT-BR / EN-US)"});

    // Empacotamento e metadados library/
    adicionarUnico({QString::fromLatin1(kOmniFrameIssRelPath), "Empacotamento / Inno", "Packaging / Inno", "Versão do script e OutputBaseFilename no Inno Setup", "Inno Setup version define and OutputBaseFilename"});
    if (rootDir.exists(QString::fromLatin1(kOmniFrameMsvcIssRelPath))) {
        adicionarUnico({QString::fromLatin1(kOmniFrameMsvcIssRelPath), "Empacotamento / Inno", "Packaging / Inno", "Versão do instalador Inno Setup MSVC", "MSVC Inno Setup installer version"});
    }
    if (rootDir.exists(QString::fromLatin1(kOmniFrameMingwIssRelPath))) {
        adicionarUnico({QString::fromLatin1(kOmniFrameMingwIssRelPath), "Empacotamento / Inno", "Packaging / Inno", "Versão do instalador Inno Setup MinGW", "MinGW Inno Setup installer version"});
    }
    adicionarUnico({QString::fromLatin1(kOmniFrameNotesRelPath), "Empacotamento / Library", "Packaging / Library", "Referência de release, versão e data em library/", "Library release link, version, and date reference"});
    adicionarUnico({QString::fromLatin1(kAppxManifestRelPath), "Empacotamento / MSIX", "Packaging / MSIX", "Atributo Version da identidade Microsoft Store MSIX", "Microsoft Store MSIX Identity Version attribute"});

    // Assets de conformidade, documentação e manual (source/assets/RELEASE é preservado intocado)
    adicionarDiretorio(
        "source/assets/ABOUT",
        {"*.txt"},
        "Assets / Sobre",
        "Assets / About",
        "Texto Sobre e histórico do aplicativo",
        "About text and application history assets",
        {"source/assets/ABOUT/ABOUT_pt_BR.txt", "source/assets/ABOUT/ABOUT_en_US.txt",
         "source/assets/ABOUT/History_APP_pt_BR.txt", "source/assets/ABOUT/History_APP_en_US.txt"}
    );
    adicionarDiretorio(
        "source/assets/CLC",
        {"*.txt"},
        "Assets / CLC",
        "Assets / CLC",
        "Esclarecimento comercial CLC (versão e data)",
        "Commercial licensing clarification CLC (version and date)",
        {"source/assets/CLC/CLC_pt_BR.txt", "source/assets/CLC/CLC_en_US.txt"}
    );
    adicionarDiretorio(
        "source/assets/COPYRIGHT",
        {"*.txt"},
        "Assets / Copyright",
        "Assets / Copyright",
        "Aviso de copyright e marca registrada",
        "Copyright and trademark notice asset",
        {"source/assets/COPYRIGHT/AVISO DE COPYRIGHT E MARCA REGISTRADA_pt_BR.txt",
         "source/assets/COPYRIGHT/COPYRIGHT AND TRADEMARK NOTICE_en_US.txt"}
    );
    adicionarDiretorio(
        "source/assets/EULA",
        {"*.txt"},
        "Assets / EULA",
        "Assets / EULA",
        "Licença EULA (versão e data)",
        "End User License Agreement EULA (version and date)",
        {"source/assets/EULA/EULA_pt_BR.txt", "source/assets/EULA/EULA_en_US.txt"}
    );
    adicionarDiretorio(
        "source/assets/MANUAL",
        {"*.txt"},
        "Assets / Manual",
        "Assets / Manual",
        "Manual integrado do aplicativo (versão e data)",
        "Built-in application manual (version and date)",
        {"source/assets/MANUAL/MANUAL_pt_BR.txt", "source/assets/MANUAL/MANUAL_en_US.txt"}
    );
    adicionarDiretorio(
        "source/assets/NOTICES",
        {"*.txt"},
        "Assets / Avisos",
        "Assets / Notices",
        "Avisos e atribuições de terceiros (preserva versões de libs)",
        "Third-party notices and attributions (preserves lib versions)",
        {"source/assets/NOTICES/NOTICE_pt_BR.txt", "source/assets/NOTICES/NOTICE_en_US.txt"}
    );
    adicionarDiretorio(
        "source/assets/PRIVACY_POLICY",
        {"*.txt"},
        "Assets / Privacidade",
        "Assets / Privacy",
        "Política de Privacidade (versão e data)",
        "Privacy Policy (version and date)",
        {"source/assets/PRIVACY_POLICY/Privacy_Policy_pt_BR.txt",
         "source/assets/PRIVACY_POLICY/Privacy_Policy_en_US.txt"}
    );

    return lista;
}

RelatorioExecucao executar_edicao_versao(
    const QString& raizProjeto,
    const QString& novaVersao,
    const QDate& novaData,
    OperacaoModo modo,
    IdiomaEditor idioma
) {
    RelatorioExecucao rel;
    rel.modo = modo;
    rel.raizProjeto = resolver_pasta_raiz_projeto(raizProjeto);
    rel.versaoAnterior = ler_versao_atual(rel.raizProjeto);
    const QString versaoNormalizada = normalizar_versao(novaVersao);
    rel.versaoAlvo = versaoNormalizada.isEmpty() ? novaVersao.trimmed() : versaoNormalizada;

    auto trLocal = [idioma](const QString& pt, const QString& en) -> QString {
        return idioma == IdiomaEditor::Portugues ? pt : en;
    };

    if (versaoNormalizada.isEmpty()) {
        rel.sucesso = false;
        rel.mensagemGeral = trLocal(
            QString("Formato de versão inválido. Utilize YYYY.M.D.R ou vYYYY.M.D.R (ex.: %1 ou v%1).").arg(kDefaultAppVersion),
            QString("Invalid version format. Expected YYYY.M.D.R or vYYYY.M.D.R pattern (e.g., %1 or v%1).").arg(kDefaultAppVersion)
        );
        return rel;
    }

    const QDate dataValida = novaData.isValid() ? novaData : ler_data_atual(rel.raizProjeto);
    rel.dataPtBr = formatar_data_pt_br(dataValida);
    rel.dataEnUs = formatar_data_en_us(dataValida);
    rel.dataIso = dataValida.toString(Qt::ISODate);

    const QDir rootDir(rel.raizProjeto);
    if (!rootDir.exists("CMakeLists.txt")) {
        rel.sucesso = false;
        rel.mensagemGeral = trLocal(
            "Diretório raiz do OmniFrame 3D não encontrado ou inválido.",
            "OmniFrame 3D root directory not found or invalid."
        );
        return rel;
    }

    const QVector<ArquivoAlvoInfo> alvos = listar_arquivos_alvo(rel.raizProjeto);
    rel.totalArquivos = alvos.size();
    const QString activeReleaseRel = detectarReleaseMarkdownRelativo(rootDir);
    const QString activeReleaseAbs = rootDir.filePath(activeReleaseRel);

    for (const ArquivoAlvoInfo& alvo : alvos) {
        RegistroAlteracao reg;
        reg.caminhoRelativo = alvo.caminhoRelativo;
        reg.categoriaPt = alvo.categoriaPt;
        reg.categoriaEn = alvo.categoriaEn;
        reg.categoria = trLocal(alvo.categoriaPt, alvo.categoriaEn);

        const QString absPath = rootDir.filePath(alvo.caminhoRelativo);
        if (!QFileInfo::exists(absPath)) {
            reg.status = StatusArquivo::Ausente;
            reg.statusTexto = trLocal("AUSENTE", "MISSING");
            reg.resumo = trLocal("Arquivo não encontrado neste workspace", "File not found in workspace");
            ++rel.totalAusentes;
            rel.registros.push_back(reg);
            continue;
        }

        const TextoArquivoInfo fileInfo = lerTextoArquivoUtf8(absPath);
        if (!fileInfo.ok) {
            reg.status = StatusArquivo::Erro;
            reg.statusTexto = trLocal("ERRO", "ERROR");
            reg.resumo = trLocal("Falha ao abrir arquivo para leitura", "Failed to open file for reading");
            ++rel.totalErros;
            rel.registros.push_back(reg);
            continue;
        }

        QString conteudoNovo = fileInfo.texto;
        const ResultadoTransformacao transf = aplicarTransformacaoArquivo(
            alvo.caminhoRelativo,
            versaoNormalizada,
            rel.dataPtBr,
            rel.dataEnUs,
            rel.dataIso,
            conteudoNovo
        );

        const QString targetReleaseRel = QString("docs/release-v%1.md").arg(versaoNormalizada);
        const bool ehReleaseAtivo = caminhosIguaisSistema(absPath, activeReleaseAbs);
        const QString targetReleaseAbs = rootDir.filePath(targetReleaseRel);
        const bool precisaRenomearRelease = ehReleaseAtivo
            && !caminhosIguaisSistema(absPath, targetReleaseAbs);

        reg.alteracoesVersao = transf.alteracoesVersao;
        reg.alteracoesData = transf.alteracoesData;
        reg.camposMonitorados = transf.camposMonitorados;
        reg.renomeadoRelease = precisaRenomearRelease;
        reg.resumo = resumirTransformacao(
            modo,
            idioma,
            reg.alteracoesVersao,
            reg.alteracoesData,
            reg.camposMonitorados,
            reg.renomeadoRelease
        );

        if (!transf.mudou && !precisaRenomearRelease) {
            reg.status = StatusArquivo::SemMudancas;
            reg.statusTexto = "OK";
            ++rel.totalSemMudancas;
            rel.registros.push_back(reg);
            continue;
        }

        rel.houveAlteracoes = true;
        if (modo == OperacaoModo::Apply) {
            if (transf.mudou && !escreverTextoArquivoUtf8(absPath, conteudoNovo, fileInfo)) {
                reg.status = StatusArquivo::Erro;
                reg.statusTexto = trLocal("ERRO", "ERROR");
                reg.resumo = trLocal("Falha ao gravar alterações no disco", "Failed to write changes to disk");
                ++rel.totalErros;
                rel.registros.push_back(reg);
                continue;
            }
            if (precisaRenomearRelease) {
                if (QFileInfo::exists(targetReleaseAbs)) {
                    reg.status = StatusArquivo::Erro;
                    reg.statusTexto = trLocal("ERRO", "ERROR");
                    reg.resumo = trLocal(
                        "Falha ao renomear: o arquivo de lançamento de destino já existe",
                        "Failed to rename: target release document already exists"
                    );
                    ++rel.totalErros;
                    rel.registros.push_back(reg);
                    continue;
                }
                if (!QFile::rename(absPath, targetReleaseAbs)) {
                    reg.status = StatusArquivo::Erro;
                    reg.statusTexto = trLocal("ERRO", "ERROR");
                    reg.resumo = trLocal("Falha ao renomear documento de lançamento", "Failed to rename release notes document");
                    ++rel.totalErros;
                    rel.registros.push_back(reg);
                    continue;
                }
                reg.caminhoRelativo = targetReleaseRel;
            }
            reg.status = StatusArquivo::Atualizado;
            reg.statusTexto = trLocal("ATUALIZADO", "UPDATED");
            ++rel.totalAtualizados;
        } else {
            reg.status = StatusArquivo::Atualizado;
            reg.statusTexto = trLocal("PENDENTE", "PENDING");
            ++rel.totalAtualizados;
        }

        rel.registros.push_back(reg);
    }

    rel.sucesso = (rel.totalErros == 0 && rel.totalAusentes == 0);
    if (modo == OperacaoModo::Check) {
        if (rel.houveAlteracoes || !rel.sucesso) {
            rel.mensagemGeral = trLocal(
                QString("Verificação concluída: %1 pendente(s), %2 OK, %3 ausente(s), %4 erro(s).")
                    .arg(rel.totalAtualizados)
                    .arg(rel.totalSemMudancas)
                    .arg(rel.totalAusentes)
                    .arg(rel.totalErros),
                QString("Check finished: %1 pending, %2 OK, %3 missing, %4 error(s).")
                    .arg(rel.totalAtualizados)
                    .arg(rel.totalSemMudancas)
                    .arg(rel.totalAusentes)
                    .arg(rel.totalErros)
            );
        } else {
            rel.mensagemGeral = trLocal(
                QString("Todos os %1 arquivo(s) rastreados estão sincronizados com a versão %2.")
                    .arg(rel.totalArquivos)
                    .arg(rel.versaoAlvo),
                QString("All %1 tracked file(s) are synchronized with version %2.")
                    .arg(rel.totalArquivos)
                    .arg(rel.versaoAlvo)
            );
        }
    } else {
        rel.mensagemGeral = trLocal(
            QString("Aplicação concluída: %1 atualizado(s), %2 sem mudanças, %3 ausente(s), %4 erro(s).")
                .arg(rel.totalAtualizados)
                .arg(rel.totalSemMudancas)
                .arg(rel.totalAusentes)
                .arg(rel.totalErros),
            QString("Apply finished: %1 updated, %2 unchanged, %3 missing, %4 error(s).")
                .arg(rel.totalAtualizados)
                .arg(rel.totalSemMudancas)
                .arg(rel.totalAusentes)
                .arg(rel.totalErros)
        );
    }

    return rel;
}

VersionEditorWidget::VersionEditorWidget(
    const QString& raizInicial,
    IdiomaEditor idiomaInicial,
    QWidget* parent
)
    : QMainWindow(parent)
    , m_idioma(idiomaInicial)
    , m_raizProjeto(resolver_pasta_raiz_projeto(raizInicial))
{
    configurarUi();
    aplicarEstilo();
    aplicarTraducoesUi();
    carregarContextoProjeto(m_raizProjeto);

    const QIcon icon = obter_icone_aplicacao(m_raizProjeto);
    if (!icon.isNull()) {
        setWindowIcon(icon);
    }
    resize(1060, 740);
}

void VersionEditorWidget::configurarUi() {
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    auto* headerLayout = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);

    m_tituloBannerLabel = new QLabel(central);
    QFont titleFont = m_tituloBannerLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    m_tituloBannerLabel->setFont(titleFont);

    m_subtituloBannerLabel = new QLabel(central);
    m_subtituloBannerLabel->setObjectName("subtitleLabel");
    titleCol->addWidget(m_tituloBannerLabel);
    titleCol->addWidget(m_subtituloBannerLabel);

    headerLayout->addLayout(titleCol, 1);

    m_idiomaLabel = new QLabel(central);
    m_idiomaCombo = new QComboBox(central);
    m_idiomaCombo->addItem("Português (Brasil)", static_cast<int>(IdiomaEditor::Portugues));
    m_idiomaCombo->addItem("English (US)", static_cast<int>(IdiomaEditor::Ingles));
    m_idiomaCombo->setCurrentIndex(m_idioma == IdiomaEditor::Portugues ? 0 : 1);
    connect(m_idiomaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VersionEditorWidget::aoAlternarIdioma);

    headerLayout->addWidget(m_idiomaLabel);
    headerLayout->addWidget(m_idiomaCombo);
    rootLayout->addLayout(headerLayout);

    m_grupoProjeto = new QGroupBox(central);
    auto* gridProjeto = new QGridLayout(m_grupoProjeto);
    m_raizLabel = new QLabel(m_grupoProjeto);
    m_raizEdit = new QLineEdit(m_grupoProjeto);
    m_btnProcurarRaiz = new QPushButton(m_grupoProjeto);
    m_btnDetectarAtual = new QPushButton(m_grupoProjeto);
    m_versaoAtualBadge = new QLabel(m_grupoProjeto);
    m_dataAtualBadge = new QLabel(m_grupoProjeto);
    m_versaoAtualBadge->setObjectName("badgeLabel");
    m_dataAtualBadge->setObjectName("badgeLabel");

    gridProjeto->addWidget(m_raizLabel, 0, 0);
    gridProjeto->addWidget(m_raizEdit, 0, 1);
    gridProjeto->addWidget(m_btnProcurarRaiz, 0, 2);
    gridProjeto->addWidget(m_btnDetectarAtual, 0, 3);

    auto* badgesRow = new QHBoxLayout();
    badgesRow->addWidget(m_versaoAtualBadge);
    badgesRow->addWidget(m_dataAtualBadge);
    badgesRow->addStretch(1);
    gridProjeto->addLayout(badgesRow, 1, 1, 1, 3);
    rootLayout->addWidget(m_grupoProjeto);

    m_grupoParametros = new QGroupBox(central);
    auto* gridParams = new QGridLayout(m_grupoParametros);
    gridParams->setHorizontalSpacing(14);
    gridParams->setVerticalSpacing(10);

    m_novaVersaoLabel = new QLabel(m_grupoParametros);
    m_novaVersaoEdit = new QLineEdit(m_grupoParametros);
    m_novaVersaoEdit->setMinimumWidth(180);
    m_novaVersaoEdit->setMaximumWidth(260);
    m_novaVersaoEdit->setMinimumHeight(34);

    m_novaDataLabel = new QLabel(m_grupoParametros);
    m_novaDataEdit = new QDateEdit(m_grupoParametros);
    m_novaDataEdit->setCalendarPopup(true);
    m_novaDataEdit->setDisplayFormat("yyyy-MM-dd");
    m_novaDataEdit->setMinimumWidth(200);
    m_novaDataEdit->setMinimumHeight(34);
    m_novaDataEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (QCalendarWidget* cal = m_novaDataEdit->calendarWidget()) {
        cal->setMinimumSize(380, 270);
        cal->setGridVisible(true);
        cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    }

    m_btnDataHoje = new QPushButton(m_grupoParametros);
    m_btnDataHoje->setMinimumHeight(34);
    m_checkIncluirReleaseDoc = new QCheckBox(m_grupoParametros);
    m_checkIncluirReleaseDoc->setChecked(true);
    m_checkIncluirReleaseDoc->setEnabled(false);

    gridParams->addWidget(m_novaVersaoLabel, 0, 0);
    gridParams->addWidget(m_novaVersaoEdit, 0, 1);
    gridParams->addWidget(m_novaDataLabel, 0, 2);
    gridParams->addWidget(m_novaDataEdit, 0, 3);
    gridParams->addWidget(m_btnDataHoje, 0, 4);
    gridParams->setColumnMinimumWidth(3, 200);
    gridParams->setColumnStretch(5, 1);
    gridParams->addWidget(m_checkIncluirReleaseDoc, 1, 0, 1, 6);
    rootLayout->addWidget(m_grupoParametros);

    auto* actionRow = new QHBoxLayout();
    m_btnVerificar = new QPushButton(central);
    m_btnAplicar = new QPushButton(central);
    m_btnAplicar->setObjectName("primaryButton");
    m_btnSobre = new QPushButton(central);
    actionRow->addWidget(m_btnVerificar);
    actionRow->addWidget(m_btnAplicar);
    actionRow->addStretch(1);
    actionRow->addWidget(m_btnSobre);
    rootLayout->addLayout(actionRow);

    m_grupoArquivos = new QGroupBox(central);
    auto* filesLayout = new QVBoxLayout(m_grupoArquivos);
    m_tabelaArquivos = new QTableWidget(m_grupoArquivos);
    m_tabelaArquivos->setColumnCount(4);
    m_tabelaArquivos->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tabelaArquivos->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabelaArquivos->setAlternatingRowColors(true);
    m_tabelaArquivos->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tabelaArquivos->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tabelaArquivos->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tabelaArquivos->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    filesLayout->addWidget(m_tabelaArquivos);
    rootLayout->addWidget(m_grupoArquivos, 3);

    m_grupoLog = new QGroupBox(central);
    auto* logLayout = new QVBoxLayout(m_grupoLog);
    m_logTextEdit = new QTextEdit(m_grupoLog);
    m_logTextEdit->setReadOnly(true);
    logLayout->addWidget(m_logTextEdit);
    rootLayout->addWidget(m_grupoLog, 1);

    setCentralWidget(central);

    connect(m_btnProcurarRaiz, &QPushButton::clicked, this, &VersionEditorWidget::aoSelecionarPastaRaiz);
    connect(m_btnDetectarAtual, &QPushButton::clicked, this, &VersionEditorWidget::aoDetectarVersaoAtual);
    connect(m_btnVerificar, &QPushButton::clicked, this, &VersionEditorWidget::aoVerificarSemAlterar);
    connect(m_btnAplicar, &QPushButton::clicked, this, &VersionEditorWidget::aoAplicarAtualizacao);
    connect(m_btnSobre, &QPushButton::clicked, this, &VersionEditorWidget::aoMostrarSobre);
    connect(m_btnDataHoje, &QPushButton::clicked, this, [this]() {
        m_novaDataEdit->setDate(QDate::currentDate());
    });
}

void VersionEditorWidget::aplicarEstilo() {
    setStyleSheet(
        "QMainWindow, QWidget { background-color: #12161f; color: #e6ecf5; font-family: 'Segoe UI', sans-serif; font-size: 9.5pt; }"
        "QLabel#subtitleLabel { color: #8ea0b8; }"
        "QLabel#badgeLabel { background-color: #1d2536; border: 1px solid #30415e; border-radius: 4px; padding: 4px 10px; color: #7cc4fa; font-weight: 600; }"
        "QGroupBox { border: 1px solid #2b364a; border-radius: 6px; margin-top: 10px; padding-top: 10px; font-weight: 600; color: #b7c7de; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; }"
        "QLineEdit, QComboBox, QTextEdit, QTableWidget { background-color: #19202e; border: 1px solid #314059; border-radius: 4px; padding: 5px; color: #e6ecf5; }"
        "QDateEdit { background-color: #19202e; border: 1px solid #314059; border-radius: 4px; padding: 0px 4px 0px 8px; color: #e6ecf5; font-size: 10pt; min-height: 32px; }"
        "QDateEdit:focus, QLineEdit:focus { border-color: #388bfd; }"
        "QDateEdit QLineEdit { background: transparent; border: none; padding: 4px 2px; margin: 0px; color: #e6ecf5; }"
        "QCalendarWidget QWidget { alternate-background-color: #1c2434; background-color: #12161f; color: #e6ecf5; }"
        "QCalendarWidget QToolButton { background-color: #20293a; color: #e6ecf5; border: 1px solid #314059; border-radius: 4px; padding: 4px 16px 4px 10px; font-weight: 600; }"
        "QCalendarWidget QToolButton#qt_calendar_monthbutton { padding: 4px 24px 4px 12px; }"
        "QCalendarWidget QToolButton#qt_calendar_monthbutton::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; right: 6px; }"
        "QCalendarWidget QToolButton:hover { background-color: #2e3e58; }"
        "QCalendarWidget QMenu { background-color: #19202e; color: #e6ecf5; border: 1px solid #314059; }"
        "QCalendarWidget QSpinBox { background-color: #19202e; color: #e6ecf5; border: 1px solid #314059; border-radius: 3px; padding: 2px 6px; }"
        "QCalendarWidget QAbstractItemView:enabled { background-color: #19202e; color: #e6ecf5; selection-background-color: #1f6feb; selection-color: #ffffff; gridline-color: #2b364a; }"
        "QCalendarWidget QAbstractItemView:disabled { color: #56647a; }"
        "QHeaderView::section { background-color: #20293a; color: #c7d5ea; border: 1px solid #2e3b52; padding: 5px; font-weight: 600; }"
        "QPushButton { background-color: #243044; border: 1px solid #384a68; border-radius: 4px; padding: 6px 14px; font-weight: 600; color: #e6ecf5; }"
        "QPushButton:hover { background-color: #2e3e58; }"
        "QPushButton#primaryButton { background-color: #1f6feb; border-color: #388bfd; color: #ffffff; }"
        "QPushButton#primaryButton:hover { background-color: #2c7cf6; }"
    );
}

QString VersionEditorWidget::trLocal(const QString& pt, const QString& en) const {
    return m_idioma == IdiomaEditor::Portugues ? pt : en;
}

QString VersionEditorWidget::textoStatus(StatusArquivo status, OperacaoModo modo, IdiomaEditor idioma) {
    const bool pt = (idioma == IdiomaEditor::Portugues);
    switch (status) {
        case StatusArquivo::Atualizado:
            return (modo == OperacaoModo::Apply)
                ? (pt ? "ATUALIZADO" : "UPDATED")
                : (pt ? "PENDENTE" : "PENDING");
        case StatusArquivo::SemMudancas: return "OK";
        case StatusArquivo::Ausente: return pt ? "AUSENTE" : "MISSING";
        case StatusArquivo::Erro: return pt ? "ERRO" : "ERROR";
    }
    return {};
}

void VersionEditorWidget::aplicarTraducoesUi() {
    setWindowTitle(trLocal("OmniFrame 3D - Editor de Versão e Metadados", "OmniFrame 3D - Version and Metadata Editor"));
    m_tituloBannerLabel->setText("OmniFrame 3D - VersionEditor");
    m_subtituloBannerLabel->setText(trLocal(
        "Sincronização centralizada de versão ([v]2026.11.12.0) e data entre CMake, C++, docs/, políticas, library/ e source/assets/",
        "Centralized synchronization of version ([v]2026.11.12.0) and date across CMake, C++, docs/, policy files, library/, and source/assets/"
    ));
    m_idiomaLabel->setText(trLocal("Idioma:", "Language:"));

    m_grupoProjeto->setTitle(trLocal("Projeto Alvo", "Target Project"));
    m_raizLabel->setText(trLocal("Diretório Raiz:", "Root Directory:"));
    m_btnProcurarRaiz->setText(trLocal("Procurar...", "Browse..."));
    m_btnDetectarAtual->setText(trLocal("Ler Versão Atual", "Read Current"));

    m_grupoParametros->setTitle(trLocal("Parâmetros de Sincronização", "Synchronization Parameters"));
    m_novaVersaoLabel->setText(trLocal("Nova Versão:", "Target Version:"));
    m_novaVersaoEdit->setPlaceholderText(trLocal("Ex.: 2026.11.12.0 ou v2026.11.12.0", "E.g.: 2026.11.12.0 or v2026.11.12.0"));
    m_novaVersaoEdit->setToolTip(trLocal(
        "Somente versões no formato 2026.11.12.0 ou v2026.11.12.0 são atualizadas. Outras versões (Qt, Eigen, Windows SDK) são preservadas.",
        "Only versions in 2026.11.12.0 or v2026.11.12.0 format are updated. Other versions (Qt, Eigen, Windows SDK) remain preserved."
    ));
    m_novaDataLabel->setText(trLocal("Data de Lançamento:", "Release Date:"));
    m_btnDataHoje->setText(trLocal("Hoje", "Today"));
    m_checkIncluirReleaseDoc->setText(trLocal(
        "Sincronizar automaticamente docs/, source/assets/ (exceto RELEASE), UI C++, README, políticas, library/ e CMakeLists.txt",
        "Automatically synchronize docs/, source/assets/ (except RELEASE), C++ UI, README, policy files, library/, and CMakeLists.txt"
    ));

    if (m_novaDataEdit) {
        const QLocale loc = (m_idioma == IdiomaEditor::Portugues)
            ? QLocale(QLocale::Portuguese, QLocale::Brazil)
            : QLocale(QLocale::English, QLocale::UnitedStates);
        m_novaDataEdit->setLocale(loc);
        if (QCalendarWidget* cal = m_novaDataEdit->calendarWidget()) {
            cal->setLocale(loc);
        }
    }

    m_btnVerificar->setText(trLocal("Verificar Consistência (--check)", "Check Consistency (--check)"));
    m_btnAplicar->setText(trLocal("Aplicar Versão (--apply)", "Apply Version (--apply)"));
    m_btnSobre->setText(trLocal("Sobre", "About"));

    m_grupoArquivos->setTitle(trLocal("Arquivos Rastreados no Repositório", "Tracked Repository Files"));
    m_tabelaArquivos->setHorizontalHeaderLabels({
        trLocal("Arquivo", "File"),
        trLocal("Categoria", "Category"),
        trLocal("Status", "Status"),
        trLocal("Detalhes", "Details")
    });
    m_grupoLog->setTitle(trLocal("Registro de Operações", "Operation Log"));
    atualizarBadgesProjeto();
    if (m_temUltimoRelatorio) {
        retraduzirRelatorioEmMemoria();
    } else {
        preencherTabelaArquivosAlvo();
    }
}

void VersionEditorWidget::atualizarBadgesProjeto() {
    if (m_versaoAtualDetectada.isEmpty()) {
        return;
    }
    m_versaoAtualBadge->setText(trLocal("Versão atual: ", "Current version: ") + m_versaoAtualDetectada);
    const QString textoData = (m_idioma == IdiomaEditor::Portugues)
        ? formatar_data_pt_br(m_dataAtualDetectada)
        : formatar_data_en_us(m_dataAtualDetectada);
    m_dataAtualBadge->setText(trLocal("Data detectada: ", "Detected date: ") + textoData);
}

void VersionEditorWidget::carregarContextoProjeto(const QString& raiz) {
    m_temUltimoRelatorio = false;
    m_raizProjeto = resolver_pasta_raiz_projeto(raiz);
    m_raizEdit->setText(m_raizProjeto);

    m_versaoAtualDetectada = ler_versao_atual(m_raizProjeto);
    m_dataAtualDetectada = ler_data_atual(m_raizProjeto);

    m_novaVersaoEdit->setText(m_versaoAtualDetectada);
    m_novaDataEdit->setDate(m_dataAtualDetectada.isValid() ? m_dataAtualDetectada : QDate::currentDate());

    atualizarBadgesProjeto();
    preencherTabelaArquivosAlvo();
    statusBar()->showMessage(trLocal("Projeto carregado: ", "Loaded project: ") + m_raizProjeto);
}

void VersionEditorWidget::preencherTabelaArquivosAlvo() {
    const QVector<ArquivoAlvoInfo> lista = listar_arquivos_alvo(m_raizProjeto);
    const QDir rootDir(m_raizProjeto);
    m_tabelaArquivos->setRowCount(lista.size());
    for (int i = 0; i < lista.size(); ++i) {
        const ArquivoAlvoInfo& info = lista[i];
        const bool existe = QFileInfo::exists(rootDir.filePath(info.caminhoRelativo));
        m_tabelaArquivos->setItem(i, 0, new QTableWidgetItem(info.caminhoRelativo));
        m_tabelaArquivos->setItem(i, 1, new QTableWidgetItem(m_idioma == IdiomaEditor::Portugues ? info.categoriaPt : info.categoriaEn));
        m_tabelaArquivos->setItem(i, 2, new QTableWidgetItem(existe ? trLocal("DISPONÍVEL", "AVAILABLE") : trLocal("AUSENTE", "MISSING")));
        m_tabelaArquivos->setItem(i, 3, new QTableWidgetItem(m_idioma == IdiomaEditor::Portugues ? info.descricaoPt : info.descricaoEn));
    }
}

void VersionEditorWidget::atualizarTabelaComRelatorio(const RelatorioExecucao& relatorio) {
    m_tabelaArquivos->setRowCount(relatorio.registros.size());
    for (int i = 0; i < relatorio.registros.size(); ++i) {
        const RegistroAlteracao& r = relatorio.registros[i];
        auto* itemFile = new QTableWidgetItem(r.caminhoRelativo);
        auto* itemCat = new QTableWidgetItem(r.categoria);
        auto* itemStatus = new QTableWidgetItem(r.statusTexto);
        auto* itemRes = new QTableWidgetItem(r.resumo);

        if (r.status == StatusArquivo::Erro || r.status == StatusArquivo::Ausente) {
            itemStatus->setForeground(QColor("#ff7b72"));
        } else if (r.status == StatusArquivo::Atualizado) {
            itemStatus->setForeground(QColor("#d29922"));
        } else {
            itemStatus->setForeground(QColor("#3fb950"));
        }

        m_tabelaArquivos->setItem(i, 0, itemFile);
        m_tabelaArquivos->setItem(i, 1, itemCat);
        m_tabelaArquivos->setItem(i, 2, itemStatus);
        m_tabelaArquivos->setItem(i, 3, itemRes);
    }
}

void VersionEditorWidget::retraduzirRelatorioEmMemoria() {
    if (!m_temUltimoRelatorio) {
        preencherTabelaArquivosAlvo();
        return;
    }

    for (RegistroAlteracao& reg : m_ultimoRelatorio.registros) {
        reg.categoria = trLocal(reg.categoriaPt, reg.categoriaEn);
        reg.statusTexto = textoStatus(reg.status, m_ultimoRelatorio.modo, m_idioma);
        if (reg.status == StatusArquivo::Ausente) {
            reg.resumo = trLocal("Arquivo não encontrado neste workspace", "File not found in workspace");
        } else if (reg.status != StatusArquivo::Erro) {
            reg.resumo = resumirTransformacao(
                m_ultimoRelatorio.modo,
                m_idioma,
                reg.alteracoesVersao,
                reg.alteracoesData,
                reg.camposMonitorados,
                reg.renomeadoRelease
            );
        }
    }

    if (m_ultimoRelatorio.modo == OperacaoModo::Check) {
        if (m_ultimoRelatorio.houveAlteracoes || !m_ultimoRelatorio.sucesso) {
            m_ultimoRelatorio.mensagemGeral = trLocal(
                QString("Verificação concluída: %1 pendente(s), %2 OK, %3 ausente(s), %4 erro(s).")
                    .arg(m_ultimoRelatorio.totalAtualizados)
                    .arg(m_ultimoRelatorio.totalSemMudancas)
                    .arg(m_ultimoRelatorio.totalAusentes)
                    .arg(m_ultimoRelatorio.totalErros),
                QString("Check finished: %1 pending, %2 OK, %3 missing, %4 error(s).")
                    .arg(m_ultimoRelatorio.totalAtualizados)
                    .arg(m_ultimoRelatorio.totalSemMudancas)
                    .arg(m_ultimoRelatorio.totalAusentes)
                    .arg(m_ultimoRelatorio.totalErros)
            );
        } else {
            m_ultimoRelatorio.mensagemGeral = trLocal(
                QString("Todos os %1 arquivo(s) rastreados estão sincronizados com a versão %2.")
                    .arg(m_ultimoRelatorio.totalArquivos)
                    .arg(m_ultimoRelatorio.versaoAlvo),
                QString("All %1 tracked file(s) are synchronized with version %2.")
                    .arg(m_ultimoRelatorio.totalArquivos)
                    .arg(m_ultimoRelatorio.versaoAlvo)
            );
        }
    } else {
        m_ultimoRelatorio.mensagemGeral = trLocal(
            QString("Aplicação concluída: %1 atualizado(s), %2 sem mudanças, %3 ausente(s), %4 erro(s).")
                .arg(m_ultimoRelatorio.totalAtualizados)
                .arg(m_ultimoRelatorio.totalSemMudancas)
                .arg(m_ultimoRelatorio.totalAusentes)
                .arg(m_ultimoRelatorio.totalErros),
            QString("Apply finished: %1 updated, %2 unchanged, %3 missing, %4 error(s).")
                .arg(m_ultimoRelatorio.totalAtualizados)
                .arg(m_ultimoRelatorio.totalSemMudancas)
                .arg(m_ultimoRelatorio.totalAusentes)
                .arg(m_ultimoRelatorio.totalErros)
        );
    }

    atualizarTabelaComRelatorio(m_ultimoRelatorio);
    statusBar()->showMessage(m_ultimoRelatorio.mensagemGeral);
}

void VersionEditorWidget::adicionarLog(const QString& texto) {
    m_logTextEdit->append(texto);
}

void VersionEditorWidget::aoSelecionarPastaRaiz() {
    const QString escolhido = QFileDialog::getExistingDirectory(
        this,
        trLocal("Selecionar Diretório Raiz do OmniFrame 3D", "Select OmniFrame 3D Root Directory"),
        m_raizProjeto
    );
    if (!escolhido.isEmpty()) {
        carregarContextoProjeto(escolhido);
        adicionarLog(trLocal("Workspace alterado para: ", "Workspace changed to: ") + m_raizProjeto);
    }
}

void VersionEditorWidget::aoDetectarVersaoAtual() {
    carregarContextoProjeto(m_raizEdit->text());
    adicionarLog(trLocal("Versão e data relidas a partir de CMakeLists.txt e assets.", "Version and date reloaded from CMakeLists.txt and assets."));
}

void VersionEditorWidget::aoVerificarSemAlterar() {
    const RelatorioExecucao rel = executar_edicao_versao(
        m_raizEdit->text(),
        m_novaVersaoEdit->text(),
        m_novaDataEdit->date(),
        OperacaoModo::Check,
        m_idioma
    );
    if (!rel.sucesso && rel.registros.isEmpty()) {
        QMessageBox::warning(
            this,
            trLocal("Verificação bloqueada", "Check Blocked"),
            rel.mensagemGeral
        );
        adicionarLog("[CHECK] " + rel.mensagemGeral);
        statusBar()->showMessage(rel.mensagemGeral);
        return;
    }
    m_ultimoRelatorio = rel;
    m_temUltimoRelatorio = true;
    atualizarTabelaComRelatorio(m_ultimoRelatorio);
    adicionarLog("[CHECK] " + m_ultimoRelatorio.mensagemGeral);
    statusBar()->showMessage(m_ultimoRelatorio.mensagemGeral);
}

void VersionEditorWidget::aoAplicarAtualizacao() {
    const QString versaoNormalizada = normalizar_versao(m_novaVersaoEdit->text());
    if (versaoNormalizada.isEmpty()) {
        QMessageBox::warning(
            this,
            trLocal("Versão inválida", "Invalid Version"),
            trLocal(
                QString("Informe uma versão válida no formato YYYY.M.D.R ou vYYYY.M.D.R (ex.: %1 ou v%1).").arg(kDefaultAppVersion),
                QString("Enter a valid version in YYYY.M.D.R or vYYYY.M.D.R format (e.g., %1 or v%1).").arg(kDefaultAppVersion)
            )
        );
        return;
    }

    m_novaVersaoEdit->setText(versaoNormalizada);
    const RelatorioExecucao rel = executar_edicao_versao(
        m_raizEdit->text(),
        versaoNormalizada,
        m_novaDataEdit->date(),
        OperacaoModo::Apply,
        m_idioma
    );
    carregarContextoProjeto(m_raizEdit->text());
    m_ultimoRelatorio = rel;
    m_temUltimoRelatorio = true;
    atualizarTabelaComRelatorio(m_ultimoRelatorio);
    adicionarLog("[APPLY] " + m_ultimoRelatorio.mensagemGeral);
    statusBar()->showMessage(m_ultimoRelatorio.mensagemGeral);
}

void VersionEditorWidget::aoAlternarIdioma(int index) {
    const int val = m_idiomaCombo->itemData(index).toInt();
    m_idioma = (val == static_cast<int>(IdiomaEditor::Ingles)) ? IdiomaEditor::Ingles : IdiomaEditor::Portugues;
    aplicarTraducoesUi();
    if (!m_temUltimoRelatorio) {
        statusBar()->showMessage(trLocal("Idioma alterado para Português (BR).", "Language switched to English (US)."));
    }
}

void VersionEditorWidget::aoMostrarSobre() {
    const QString verExibida = m_versaoAtualDetectada.isEmpty()
        ? QString::fromLatin1(kDefaultAppVersion)
        : m_versaoAtualDetectada;
    QMessageBox::information(
        this,
        trLocal("Sobre o VersionEditor", "About VersionEditor"),
        trLocal(
            QString("OmniFrame 3D — Editor de Versão e Metadados (%1)\n\n"
                    "Autor: Fernando Nillsson Cidade\n"
                    "Sincroniza estritamente versões no formato [v]YYYY.M.D.R (ex.: v2026.11.12.0 / 2026.11.12.0) e datas de lançamento em:\n"
                    "• docs/ (todos os documentos Markdown e release-v*.md)\n"
                    "• source/assets/{ABOUT,CLC,COPYRIGHT,EULA,MANUAL,NOTICES,PRIVACY_POLICY}/\n"
                    "• source/ui/{AboutDialog,ManualDialog}.{hpp,cpp}\n"
                    "• README.md, MANUAL.md, OBSERVACAO.md, SECURITY.md, CODE_OF_CONDUCT.md, CONTRIBUTING.md\n"
                    "• library/{OmniFrame*.iss,OmniFrame_cpp.txt,AppxManifest.xml}, CMakeLists.txt e mocks/\n\n"
                    "Preserva intactos source/assets/RELEASE/ e versões de bibliotecas/SDKs (Qt 6.11.1, Eigen 3.4.0, Windows SDK).")
                .arg(verExibida),
            QString("OmniFrame 3D — Version & Metadata Editor (%1)\n\n"
                    "Author: Fernando Nillsson Cidade\n"
                    "Strictly synchronizes versions matching [v]YYYY.M.D.R (e.g., v2026.11.12.0 / 2026.11.12.0) and release dates across:\n"
                    "• docs/ (all Markdown guides and release-v*.md)\n"
                    "• source/assets/{ABOUT,CLC,COPYRIGHT,EULA,MANUAL,NOTICES,PRIVACY_POLICY}/\n"
                    "• source/ui/{AboutDialog,ManualDialog}.{hpp,cpp}\n"
                    "• README.md, MANUAL.md, OBSERVACAO.md, SECURITY.md, CODE_OF_CONDUCT.md, CONTRIBUTING.md\n"
                    "• library/{OmniFrame*.iss,OmniFrame_cpp.txt,AppxManifest.xml}, CMakeLists.txt, and mocks/\n\n"
                    "Preserves source/assets/RELEASE/ and third-party/SDK versions (Qt 6.11.1, Eigen 3.4.0, Windows SDK) untouched.")
                .arg(verExibida)
        )
    );
}

static void imprimirAjudaTerminal(IdiomaEditor idioma) {
    if (idioma == IdiomaEditor::Ingles) {
        std::cout << "OmniFrame 3D - VersionEditor CLI / GUI\n"
                  << "Usage:\n"
                  << "  VersionEditor                                   Open graphical user interface (GUI)\n"
                  << "  VersionEditor --check [root]                    Check version & date consistency without writing\n"
                  << "  VersionEditor --apply [root]                    Apply target version & date across tracked files\n"
                  << "  VersionEditor --version, -v <[v]2026.11.12.0>   Set target version ([v]YYYY.M.D.R only)\n"
                  << "  VersionEditor --date, -d <YYYY-MM-DD>           Set target release date (ISO, PT-BR, or EN-US)\n"
                  << "  VersionEditor --root, -r <dir>                  Set project root directory\n"
                  << "  VersionEditor --lang <pt|en>                    Set interface and output language\n"
                  << "  VersionEditor --help, -h                        Show this help message\n";
        return;
    }

    std::cout << "OmniFrame 3D - VersionEditor CLI / GUI\n"
              << "Uso:\n"
              << "  VersionEditor                                   Abre a interface gráfica (GUI)\n"
              << "  VersionEditor --check [raiz]                    Verifica consistência de versão e data sem gravar\n"
              << "  VersionEditor --apply [raiz]                    Aplica versão e data em todos os arquivos rastreados\n"
              << "  VersionEditor --version, -v <[v]2026.11.12.0>   Define a versão alvo (apenas [v]YYYY.M.D.R)\n"
              << "  VersionEditor --date, -d <YYYY-MM-DD>           Define a data alvo (ISO, PT-BR ou EN-US)\n"
              << "  VersionEditor --root, -r <dir>                  Define o diretório raiz do projeto\n"
              << "  VersionEditor --lang <pt|en>                    Define idioma da interface e saída\n"
              << "  VersionEditor --help, -h                        Exibe esta ajuda\n";
}

int run_cli_or_gui(int argc, char* argv[]) {
    bool modoCli = false;
    bool ajudaSolicitada = false;
    OperacaoModo modo = OperacaoModo::Check;
    IdiomaEditor idioma = IdiomaEditor::Portugues;
    QString raizArg;
    QString versaoArg;
    QString dataArg;

    auto parseLang = [](const QString& val) {
        return val.trimmed().toLower().startsWith("en") ? IdiomaEditor::Ingles : IdiomaEditor::Portugues;
    };

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--help" || arg == "-h") {
            ajudaSolicitada = true;
        } else if (arg == "--check") {
            modoCli = true;
            modo = OperacaoModo::Check;
        } else if (arg == "--apply") {
            modoCli = true;
            modo = OperacaoModo::Apply;
        } else if ((arg == "--version" || arg == "-v") && i + 1 < argc) {
            versaoArg = QString::fromLocal8Bit(argv[++i]);
        } else if (arg.startsWith("--version=")) {
            versaoArg = arg.mid(QString("--version=").size());
        } else if ((arg == "--date" || arg == "-d") && i + 1 < argc) {
            dataArg = QString::fromLocal8Bit(argv[++i]);
        } else if (arg.startsWith("--date=")) {
            dataArg = arg.mid(QString("--date=").size());
        } else if ((arg == "--root" || arg == "-r") && i + 1 < argc) {
            raizArg = QString::fromLocal8Bit(argv[++i]);
        } else if (arg.startsWith("--root=")) {
            raizArg = arg.mid(QString("--root=").size());
        } else if (arg == "--lang" && i + 1 < argc) {
            idioma = parseLang(QString::fromLocal8Bit(argv[++i]));
        } else if (arg.startsWith("--lang=")) {
            idioma = parseLang(arg.mid(QString("--lang=").size()));
        } else if (!arg.startsWith('-') && raizArg.isEmpty()) {
            raizArg = arg;
        }
    }

    if (ajudaSolicitada) {
        anexarConsolePaiParaCli();
        imprimirAjudaTerminal(idioma);
        return 0;
    }

    if (modoCli) {
        anexarConsolePaiParaCli();
        QCoreApplication app(argc, argv);
        const QString raiz = resolver_pasta_raiz_projeto(raizArg);
        const QString versao = versaoArg.isEmpty() ? ler_versao_atual(raiz) : versaoArg;
        QDate releaseDate = ler_data_atual(raiz);
        if (!dataArg.isEmpty()) {
            QDate parsed;
            if (!parsear_data_flexivel(dataArg, parsed)) {
                if (idioma == IdiomaEditor::Ingles) {
                    std::cerr << "[VersionEditor] Error: invalid date format '"
                              << dataArg.toStdString()
                              << "'. Use YYYY-MM-DD, DD/MM/YYYY, MM/DD/YYYY, 'Month D, YYYY', or 'D de mes de YYYY'.\n";
                } else {
                    std::cerr << "[VersionEditor] Erro: formato de data inválido '"
                              << dataArg.toStdString()
                              << "'. Utilize YYYY-MM-DD, DD/MM/YYYY, MM/DD/YYYY, 'D de mês de YYYY' ou 'Month D, YYYY'.\n";
                }
                return 2;
            }
            releaseDate = parsed;
        }

        const RelatorioExecucao rel = executar_edicao_versao(raiz, versao, releaseDate, modo, idioma);
        const bool en = (idioma == IdiomaEditor::Ingles);
        std::cout << "[VersionEditor] " << (en ? "Root: " : "Raiz: ") << rel.raizProjeto.toStdString() << '\n';
        if (rel.versaoAlvo.isEmpty() || !validar_formato_versao(rel.versaoAlvo)) {
            std::cout << "[VersionEditor] " << rel.mensagemGeral.toStdString() << '\n';
            return 2;
        }
        std::cout << "[VersionEditor] " << (en ? "Version: " : "Versão: ") << rel.versaoAlvo.toStdString()
                  << " | PT-BR: " << rel.dataPtBr.toStdString()
                  << " | EN-US: " << rel.dataEnUs.toStdString() << '\n';
        for (const RegistroAlteracao& reg : rel.registros) {
            std::cout << "  [" << reg.statusTexto.toStdString() << "] "
                      << reg.caminhoRelativo.toStdString() << " - "
                      << reg.resumo.toStdString() << '\n';
        }
        std::cout << "[VersionEditor] " << rel.mensagemGeral.toStdString() << '\n';

        if (!rel.sucesso) {
            return 2;
        }
        if (modo == OperacaoModo::Check && rel.houveAlteracoes) {
            return 1;
        }
        return 0;
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("VersionEditor");
    QCoreApplication::setApplicationVersion(kDefaultAppVersion);
    VersionEditorWidget window(raizArg, idioma);
    window.show();
    return app.exec();
}

} // namespace mocks::version_editor

#ifndef OMNIFRAME_VERSION_EDITOR_NO_MAIN
int main(int argc, char* argv[]) {
    return mocks::version_editor::run_cli_or_gui(argc, argv);
}
#endif
