#include "CompileTranslationsGUI.hpp"

#ifndef OMNIFRAME_COMPILE_TRANSLATIONS_TERMINAL_LINKED
#define OMNIFRAME_COMPILE_TRANSLATIONS_NO_MAIN
#include "CompileTranslationsTerminal.cpp"
#else
#include "CompileTranslationsTerminal.hpp"
#endif

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace mocks::compile_translations_gui {

namespace ct = mocks::compile_translations_terminal;

QIcon obter_icone_aplicacao() {
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

    if (!appDir.isEmpty()) {
        QDir walk(appDir);
        for (int i = 0; i < 6; ++i) {
            candidatos << walk.filePath("source/assets/icons/omniframe.ico")
                       << walk.filePath("source/assets/icons/omniframe.png")
                       << walk.filePath("assets/icons/omniframe.ico")
                       << walk.filePath("assets/icons/omniframe.png");
            if (!walk.cdUp()) {
                break;
            }
        }
    }
    const QString raiz = ct::resolver_raiz_projeto();
    if (!raiz.isEmpty()) {
        candidatos << QDir(raiz).filePath("source/assets/icons/omniframe.ico")
                   << QDir(raiz).filePath("source/assets/icons/omniframe.png");
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

CompileTranslationsGuiWindow::CompileTranslationsGuiWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_activeProcess = new QProcess(this);
    m_processTimeoutTimer = new QTimer(this);
    m_processTimeoutTimer->setSingleShot(true);

    connect(m_processTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (!m_compilationRunning || !m_activeProcess || m_currentItemHandled) {
            return;
        }
        m_timedOutCurrent = true;
        if (m_activeProcess->state() != QProcess::NotRunning) {
            m_activeProcess->kill();
        }
    });

    connect(m_activeProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (!m_compilationRunning || m_currentItemHandled) {
            return;
        }
        if (err == QProcess::FailedToStart) {
            m_currentItemHandled = true;
            if (m_processTimeoutTimer) {
                m_processTimeoutTimer->stop();
            }
            ++m_batchErrors;
            registrarLog(QString("[ERRO] %1 -> %2 | %3")
                .arg(QFileInfo(m_activeTsPath).fileName(),
                     QFileInfo(m_activeQmPath).fileName(),
                     trLocal("Falha ao iniciar executavel lrelease", "Failed to start lrelease executable")));
            QTimer::singleShot(0, this, [this]() { processarProximoDaFila(); });
        }
    });

    connect(m_activeProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!m_compilationRunning || m_currentItemHandled) {
            return;
        }
        m_currentItemHandled = true;
        if (m_processTimeoutTimer) {
            m_processTimeoutTimer->stop();
        }

        QString stdoutStr = QString::fromLocal8Bit(m_activeProcess->readAllStandardOutput()).trimmed();
        const QString stderrStr = QString::fromLocal8Bit(m_activeProcess->readAllStandardError()).trimmed();
        const QFileInfo qmInfo(m_activeQmPath);

        if (m_timedOutCurrent) {
            ++m_batchErrors;
            registrarLog(QString("[ERRO] %1 -> %2 | %3")
                .arg(QFileInfo(m_activeTsPath).fileName(),
                     qmInfo.fileName(),
                     trLocal("Timeout ao executar lrelease", "Timed out executing lrelease")));
        } else if (exitStatus == QProcess::NormalExit && exitCode == 0 && qmInfo.exists() && qmInfo.size() > 0) {
            ++m_batchOk;
            const QString msg = stdoutStr.isEmpty()
                ? trLocal("Compilado com sucesso", "Compiled successfully")
                : stdoutStr.replace('\n', ' ').replace('\r', ' ');
            registrarLog(QString("[OK] %1 -> %2 | %3")
                .arg(QFileInfo(m_activeTsPath).fileName(), qmInfo.fileName(), msg));
        } else {
            ++m_batchErrors;
            const QString msg = !stderrStr.isEmpty()
                ? stderrStr
                : trLocal("Falha na compilacao com lrelease", "Compilation failed with lrelease");
            registrarLog(QString("[ERRO] %1 -> %2 | %3")
                .arg(QFileInfo(m_activeTsPath).fileName(), qmInfo.fileName(), msg));
        }

        processarProximoDaFila();
    });

    configurarUi();
    aplicarEstilo();
    aplicarTraducoesUi();
    aoDetectarPadrao();

    const QIcon icon = obter_icone_aplicacao();
    if (!icon.isNull()) {
        setWindowIcon(icon);
    }
    resize(960, 680);
}

CompileTranslationsGuiWindow::~CompileTranslationsGuiWindow() {
    m_compilationRunning = false;
    if (m_processTimeoutTimer) {
        m_processTimeoutTimer->stop();
    }
    if (m_activeProcess) {
        disconnect(m_activeProcess, nullptr, this, nullptr);
        if (m_activeProcess->state() != QProcess::NotRunning) {
            m_activeProcess->kill();
            m_activeProcess->waitForFinished(3000);
        }
    }
}

void CompileTranslationsGuiWindow::configurarUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* topRow = new QHBoxLayout();
    auto* titlesCol = new QVBoxLayout();
    titlesCol->setSpacing(2);
    m_tituloLabel = new QLabel(central);
    QFont tf = m_tituloLabel->font();
    tf.setPointSize(14);
    tf.setBold(true);
    m_tituloLabel->setFont(tf);
    m_subtituloLabel = new QLabel(central);
    m_subtituloLabel->setObjectName("subtitleLabel");
    titlesCol->addWidget(m_tituloLabel);
    titlesCol->addWidget(m_subtituloLabel);
    topRow->addLayout(titlesCol, 1);

    m_idiomaLabel = new QLabel(central);
    m_idiomaCombo = new QComboBox(central);
    m_idiomaCombo->addItem("Português (Brasil)", false);
    m_idiomaCombo->addItem("English (US)", true);
    connect(m_idiomaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CompileTranslationsGuiWindow::aoAlternarIdioma);
    topRow->addWidget(m_idiomaLabel);
    topRow->addWidget(m_idiomaCombo);
    root->addLayout(topRow);

    m_grupoConfig = new QGroupBox(central);
    auto* grid = new QGridLayout(m_grupoConfig);
    m_dirLabel = new QLabel(m_grupoConfig);
    m_dirEdit = new QLineEdit(m_grupoConfig);
    m_btnProcurarDir = new QPushButton(m_grupoConfig);
    m_btnDetectarPadrao = new QPushButton(m_grupoConfig);
    m_lreleaseLabel = new QLabel(m_grupoConfig);
    m_lreleaseEdit = new QLineEdit(m_grupoConfig);
    m_btnProcurarLrelease = new QPushButton(m_grupoConfig);

    grid->addWidget(m_dirLabel, 0, 0);
    grid->addWidget(m_dirEdit, 0, 1);
    grid->addWidget(m_btnProcurarDir, 0, 2);
    grid->addWidget(m_btnDetectarPadrao, 0, 3);
    grid->addWidget(m_lreleaseLabel, 1, 0);
    grid->addWidget(m_lreleaseEdit, 1, 1);
    grid->addWidget(m_btnProcurarLrelease, 1, 2);
    root->addWidget(m_grupoConfig);

    m_grupoArquivos = new QGroupBox(central);
    auto* listLayout = new QVBoxLayout(m_grupoArquivos);
    m_listaTs = new QListWidget(m_grupoArquivos);
    m_listaTs->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listLayout->addWidget(m_listaTs);
    root->addWidget(m_grupoArquivos, 2);

    auto* btnRow = new QHBoxLayout();
    m_btnCompilarTodas = new QPushButton(central);
    m_btnCompilarTodas->setObjectName("primaryButton");
    m_btnCompilarSelecionadas = new QPushButton(central);
    m_btnVerificar = new QPushButton(central);
    btnRow->addWidget(m_btnCompilarTodas);
    btnRow->addWidget(m_btnCompilarSelecionadas);
    btnRow->addWidget(m_btnVerificar);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    m_grupoLog = new QGroupBox(central);
    auto* logLayout = new QVBoxLayout(m_grupoLog);
    m_logText = new QTextEdit(m_grupoLog);
    m_logText->setReadOnly(true);
    logLayout->addWidget(m_logText);
    root->addWidget(m_grupoLog, 2);

    setCentralWidget(central);

    connect(m_btnProcurarDir, &QPushButton::clicked, this, &CompileTranslationsGuiWindow::aoSelecionarPastaTraducoes);
    connect(m_btnProcurarLrelease, &QPushButton::clicked, this, &CompileTranslationsGuiWindow::aoSelecionarLrelease);
    connect(m_btnDetectarPadrao, &QPushButton::clicked, this, &CompileTranslationsGuiWindow::aoDetectarPadrao);
    connect(m_btnCompilarTodas, &QPushButton::clicked, this, &CompileTranslationsGuiWindow::aoCompilarTodas);
    connect(m_btnCompilarSelecionadas, &QPushButton::clicked, this, &CompileTranslationsGuiWindow::aoCompilarSelecionadas);
    connect(m_btnVerificar, &QPushButton::clicked, this, &CompileTranslationsGuiWindow::aoVerificarStatus);
}

void CompileTranslationsGuiWindow::aplicarEstilo() {
    setStyleSheet(
        "QMainWindow, QWidget { background-color: #12161f; color: #e6ecf5; font-family: 'Segoe UI', sans-serif; font-size: 9.5pt; }"
        "QLabel#subtitleLabel { color: #8ea0b8; }"
        "QGroupBox { border: 1px solid #2b364a; border-radius: 6px; margin-top: 10px; padding-top: 10px; font-weight: 600; color: #b7c7de; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; }"
        "QLineEdit, QComboBox, QListWidget, QTextEdit { background-color: #19202e; border: 1px solid #314059; border-radius: 4px; padding: 5px; color: #e6ecf5; }"
        "QPushButton { background-color: #243044; border: 1px solid #384a68; border-radius: 4px; padding: 6px 14px; font-weight: 600; color: #e6ecf5; }"
        "QPushButton:hover { background-color: #2e3e58; }"
        "QPushButton#primaryButton { background-color: #1f6feb; border-color: #388bfd; color: #ffffff; }"
        "QPushButton#primaryButton:hover { background-color: #2c7cf6; }"
    );
}

QString CompileTranslationsGuiWindow::trLocal(const QString& pt, const QString& en) const {
    return m_idiomaIngles ? en : pt;
}

void CompileTranslationsGuiWindow::aplicarTraducoesUi() {
    setWindowTitle(trLocal("OmniFrame 3D - Compilador de Traducoes (.ts -> .qm)", "OmniFrame 3D - Translations Compiler (.ts -> .qm)"));
    m_tituloLabel->setText("OmniFrame 3D - CompileTranslationsGUI");
    m_subtituloLabel->setText(trLocal(
        "Geracao e verificacao de arquivos binarios Qt .qm a partir de source/locale/language",
        "Generation and verification of binary Qt .qm files from source/locale/language"
    ));
    m_idiomaLabel->setText(trLocal("Idioma:", "Language:"));

    m_grupoConfig->setTitle(trLocal("Caminhos e Ferramenta lrelease", "Paths & lrelease Tool"));
    m_dirLabel->setText(trLocal("Pasta de Traducoes:", "Translations Folder:"));
    m_btnProcurarDir->setText(trLocal("Procurar...", "Browse..."));
    m_btnDetectarPadrao->setText(trLocal("Detectar Padrao", "Detect Defaults"));
    m_lreleaseLabel->setText(trLocal("Executavel lrelease:", "lrelease Executable:"));
    m_btnProcurarLrelease->setText(trLocal("Selecionar...", "Select..."));

    m_grupoArquivos->setTitle(trLocal("Arquivos de Traducao (.ts)", "Translation Source Files (.ts)"));
    m_btnCompilarTodas->setText(trLocal("Compilar Todas (.ts -> .qm)", "Compile All (.ts -> .qm)"));
    m_btnCompilarSelecionadas->setText(trLocal("Compilar Selecionadas", "Compile Selected"));
    m_btnVerificar->setText(trLocal("Verificar Status .qm", "Check .qm Status"));
    m_grupoLog->setTitle(trLocal("Saida do Compilador", "Compiler Output"));
    atualizarListaArquivos();
}

void CompileTranslationsGuiWindow::atualizarListaArquivos() {
    m_listaTs->clear();
    const QStringList tsFiles = ct::listar_arquivos_ts(m_dirEdit->text());
    for (const QString& tsPath : tsFiles) {
        QFileInfo tsInfo(tsPath);
        const QString qmPath = tsInfo.dir().filePath(tsInfo.completeBaseName() + ".qm");
        const QFileInfo qmInfo(qmPath);
        const bool qmOk = qmInfo.exists() && qmInfo.size() > 0 && qmInfo.lastModified() >= tsInfo.lastModified();
        const QString badge = qmOk ? trLocal("[QM Atualizado]", "[QM Up to Date]") : trLocal("[Pendente]", "[Pending]");

        auto* item = new QListWidgetItem(QString("%1  %2  (%3)").arg(tsInfo.fileName(), badge, tsPath), m_listaTs);
        item->setData(Qt::UserRole, tsPath);
        if (qmOk) {
            item->setForeground(QColor("#3fb950"));
        } else {
            item->setForeground(QColor("#d29922"));
        }
    }
}

void CompileTranslationsGuiWindow::registrarLog(const QString& linha) {
    m_logText->append(linha);
}

void CompileTranslationsGuiWindow::aoSelecionarPastaTraducoes() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        trLocal("Selecionar Pasta de Traducoes (.ts)", "Select Translations Folder (.ts)"),
        m_dirEdit->text()
    );
    if (!dir.isEmpty()) {
        m_dirEdit->setText(QDir::cleanPath(dir));
        atualizarListaArquivos();
    }
}

void CompileTranslationsGuiWindow::aoSelecionarLrelease() {
    const QString file = QFileDialog::getOpenFileName(
        this,
        trLocal("Selecionar lrelease.exe", "Select lrelease.exe"),
        m_lreleaseEdit->text(),
        "Executables (*.exe);;All Files (*)"
    );
    if (!file.isEmpty()) {
        m_lreleaseEdit->setText(QDir::cleanPath(file));
    }
}

void CompileTranslationsGuiWindow::aoDetectarPadrao() {
    const QString raiz = ct::resolver_raiz_projeto();
    const QString dirTs = ct::resolver_diretorio_traducoes(raiz);
    const QString lrelease = ct::localizar_lrelease();

    m_dirEdit->setText(dirTs);
    m_lreleaseEdit->setText(lrelease);
    atualizarListaArquivos();
    statusBar()->showMessage(trLocal("Caminhos detectados em: ", "Detected paths in: ") + dirTs);
}

void CompileTranslationsGuiWindow::definirControlesHabilitados(bool enabled) {
    if (m_btnCompilarTodas) m_btnCompilarTodas->setEnabled(enabled);
    if (m_btnCompilarSelecionadas) m_btnCompilarSelecionadas->setEnabled(enabled);
    if (m_btnVerificar) m_btnVerificar->setEnabled(enabled);
    if (m_btnProcurarDir) m_btnProcurarDir->setEnabled(enabled);
    if (m_btnDetectarPadrao) m_btnDetectarPadrao->setEnabled(enabled);
    if (m_btnProcurarLrelease) m_btnProcurarLrelease->setEnabled(enabled);
    if (m_dirEdit) m_dirEdit->setEnabled(enabled);
    if (m_lreleaseEdit) m_lreleaseEdit->setEnabled(enabled);
}

void CompileTranslationsGuiWindow::iniciarCompilacaoAssincrona(const QStringList& arquivosEspecificos) {
    if (m_compilationRunning) {
        return;
    }

    const QString lreleaseOverride = m_lreleaseEdit->text().trimmed();
    m_activeLreleasePath = ct::localizar_lrelease(lreleaseOverride);
    if (!lreleaseOverride.isEmpty() && m_activeLreleasePath.isEmpty()) {
        const QString msg = trLocal(
            QString("Caminho informado para lrelease invalido ou inexistente: %1").arg(lreleaseOverride),
            QString("Specified lrelease path is invalid or does not exist: %1").arg(lreleaseOverride)
        );
        registrarLog("==> " + msg);
        statusBar()->showMessage(msg);
        return;
    }
    if (m_activeLreleasePath.isEmpty()) {
        const QString msg = trLocal(
            "Executavel lrelease.exe do Qt6 nao encontrado. Selecione o executavel ou configure QTDIR.",
            "Qt6 lrelease.exe executable was not found. Select the executable or set QTDIR."
        );
        registrarLog("==> " + msg);
        statusBar()->showMessage(msg);
        return;
    }

    const QString raiz = ct::resolver_raiz_projeto(m_dirEdit->text());
    m_activeDirTraducoes = ct::resolver_diretorio_traducoes(raiz, m_dirEdit->text());
    QStringList alvos = arquivosEspecificos;
    if (alvos.isEmpty()) {
        alvos = ct::listar_arquivos_ts(m_activeDirTraducoes);
    }
    if (alvos.isEmpty()) {
        const QString msg = trLocal(
            QString("Nenhum arquivo .ts encontrado em: %1").arg(m_activeDirTraducoes),
            QString("No .ts files found in: %1").arg(m_activeDirTraducoes)
        );
        registrarLog("==> " + msg);
        statusBar()->showMessage(msg);
        return;
    }

    m_pendingTsQueue = alvos;
    m_batchTotal = static_cast<int>(alvos.size());
    m_batchOk = 0;
    m_batchErrors = 0;
    m_compilationRunning = true;
    definirControlesHabilitados(false);
    statusBar()->showMessage(trLocal("Compilando traducoes (.ts -> .qm)...", "Compiling translations (.ts -> .qm)..."));
    processarProximoDaFila();
}

void CompileTranslationsGuiWindow::processarProximoDaFila() {
    if (!m_compilationRunning) {
        return;
    }
    while (!m_pendingTsQueue.isEmpty()) {
        const QString tsPathRaw = m_pendingTsQueue.takeFirst();
        QFileInfo tsInfo(tsPathRaw);
        if (!tsInfo.isAbsolute()) {
            tsInfo.setFile(QDir(m_activeDirTraducoes).filePath(tsPathRaw));
        }

        m_activeTsPath = QDir::cleanPath(tsInfo.absoluteFilePath());
        m_activeQmPath = QDir::cleanPath(tsInfo.dir().filePath(tsInfo.completeBaseName() + ".qm"));

        if (!tsInfo.exists()) {
            ++m_batchErrors;
            registrarLog(QString("[ERRO] %1 -> %2 | %3")
                .arg(QFileInfo(m_activeTsPath).fileName(),
                     QFileInfo(m_activeQmPath).fileName(),
                     trLocal("Arquivo .ts nao encontrado", ".ts file not found")));
            continue;
        }

        m_currentItemHandled = false;
        m_timedOutCurrent = false;
        m_processTimeoutTimer->start(60000);
        m_activeProcess->start(m_activeLreleasePath, QStringList() << m_activeTsPath << QStringLiteral("-qm") << m_activeQmPath);
        return;
    }

    finalizarCompilacaoAssincrona();
}

void CompileTranslationsGuiWindow::finalizarCompilacaoAssincrona() {
    if (m_processTimeoutTimer) {
        m_processTimeoutTimer->stop();
    }
    m_compilationRunning = false;
    definirControlesHabilitados(true);

    const QString resumo = trLocal(
        QString("Traducoes processadas: %1 OK, %2 erro(s) de %3 arquivo(s).")
            .arg(m_batchOk)
            .arg(m_batchErrors)
            .arg(m_batchTotal),
        QString("Translations processed: %1 OK, %2 error(s) out of %3 file(s).")
            .arg(m_batchOk)
            .arg(m_batchErrors)
            .arg(m_batchTotal)
    );
    registrarLog("==> " + resumo);
    atualizarListaArquivos();
    statusBar()->showMessage(resumo);
}

void CompileTranslationsGuiWindow::aoCompilarTodas() {
    iniciarCompilacaoAssincrona({});
}

void CompileTranslationsGuiWindow::aoCompilarSelecionadas() {
    if (m_compilationRunning) {
        return;
    }
    QStringList selecionados;
    for (QListWidgetItem* item : m_listaTs->selectedItems()) {
        selecionados << item->data(Qt::UserRole).toString();
    }
    if (selecionados.isEmpty()) {
        QMessageBox::information(
            this,
            trLocal("Selecao vazia", "Empty Selection"),
            trLocal("Selecione pelo menos um arquivo .ts na lista.", "Select at least one .ts file from the list.")
        );
        return;
    }

    iniciarCompilacaoAssincrona(selecionados);
}

void CompileTranslationsGuiWindow::aoVerificarStatus() {
    const ct::RelatorioCompilacaoTerminal rel = ct::executar_compilacao(
        m_dirEdit->text(),
        m_lreleaseEdit->text(),
        {},
        ct::ModoExecucao::VerificarApenas,
        m_idiomaIngles
    );
    for (const auto& item : rel.itens) {
        registrarLog(QString("[CHECK %1] %2 | %3")
            .arg(item.sucesso ? "OK" : "PENDENTE")
            .arg(QFileInfo(item.arquivoTs).fileName())
            .arg(item.mensagem));
    }
    registrarLog("==> " + rel.mensagemGeral);
    atualizarListaArquivos();
    statusBar()->showMessage(rel.mensagemGeral);
}

void CompileTranslationsGuiWindow::aoAlternarIdioma(int index) {
    m_idiomaIngles = m_idiomaCombo->itemData(index).toBool();
    aplicarTraducoesUi();
}

void CompileTranslationsGuiWindow::setTranslationDir(const QString& dirPath) {
    if (m_dirEdit) {
        m_dirEdit->setText(dirPath);
    }
    atualizarListaArquivos();
}

void CompileTranslationsGuiWindow::setLreleasePath(const QString& lreleasePath) {
    if (m_lreleaseEdit) {
        m_lreleaseEdit->setText(lreleasePath);
    }
}

void CompileTranslationsGuiWindow::triggerCompileAllAsync() {
    iniciarCompilacaoAssincrona({});
}

QString CompileTranslationsGuiWindow::logContent() const {
    return m_logText ? m_logText->toPlainText() : QString();
}

int run_gui(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("CompileTranslationsGUI");
    QCoreApplication::setApplicationVersion(ct::kFallbackAppVersion);
    CompileTranslationsGuiWindow window;
    window.show();
    return app.exec();
}

} // namespace mocks::compile_translations_gui

#ifndef OMNIFRAME_COMPILE_TRANSLATIONS_GUI_NO_MAIN
int main(int argc, char* argv[]) {
    return mocks::compile_translations_gui::run_gui(argc, argv);
}
#endif

