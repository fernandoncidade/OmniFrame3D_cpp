#include "StartupSessionDialog.hpp"

#include "io/AppLogger.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QShowEvent>
#include <QTextCursor>
#include <QVBoxLayout>

namespace omniframe {

using locale::GerenciadorTraducao;

StartupSessionDialog::StartupSessionDialog(io::StartupSessionBehavior behavior,
                                           bool confirmUnsavedOnClose,
                                           QWidget* parent)
    : QDialog(parent),
      m_behavior(behavior),
      m_confirmUnsavedOnClose(confirmUnsavedOnClose) {
    setupUi();
    applyValuesToControls();
    retranslateUi();
    refreshLogSummary();
}

void StartupSessionDialog::setupUi() {
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);
    setMinimumSize(640, 560);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    // 1. Política de Inicialização e Sessão
    m_groupStartup = new QGroupBox(this);
    auto* startupLay = new QVBoxLayout(m_groupStartup);
    startupLay->setSpacing(8);

    m_lblStartupDesc = new QLabel(m_groupStartup);
    m_lblStartupDesc->setWordWrap(true);
    startupLay->addWidget(m_lblStartupDesc);

    m_radioAsk = new QRadioButton(m_groupStartup);
    m_radioAsk->setObjectName(QStringLiteral("radioStartupAsk"));
    m_radioAutoLoad = new QRadioButton(m_groupStartup);
    m_radioAutoLoad->setObjectName(QStringLiteral("radioStartupAutoLoad"));
    m_radioNewProject = new QRadioButton(m_groupStartup);
    m_radioNewProject->setObjectName(QStringLiteral("radioStartupNewProject"));

    auto* grp = new QButtonGroup(m_groupStartup);
    grp->addButton(m_radioAsk);
    grp->addButton(m_radioAutoLoad);
    grp->addButton(m_radioNewProject);

    startupLay->addWidget(m_radioAsk);
    startupLay->addWidget(m_radioAutoLoad);
    startupLay->addWidget(m_radioNewProject);

    m_chkConfirmClose = new QCheckBox(m_groupStartup);
    m_chkConfirmClose->setObjectName(QStringLiteral("chkConfirmUnsavedOnClose"));
    startupLay->addSpacing(4);
    startupLay->addWidget(m_chkConfirmClose);

    root->addWidget(m_groupStartup);

    // 2. Registro de Logs Diários
    m_groupLogs = new QGroupBox(this);
    auto* logsLay = new QVBoxLayout(m_groupLogs);
    logsLay->setSpacing(6);

    auto* form = new QFormLayout();
    m_lblLogDirTitle = new QLabel(m_groupLogs);
    m_logDirEdit = new QLineEdit(m_groupLogs);
    m_logDirEdit->setObjectName(QStringLiteral("logDirPathEdit"));
    m_logDirEdit->setReadOnly(true);
    form->addRow(m_lblLogDirTitle, m_logDirEdit);

    m_lblLogFileTitle = new QLabel(m_groupLogs);
    m_logFileEdit = new QLineEdit(m_groupLogs);
    m_logFileEdit->setObjectName(QStringLiteral("logCurrentFileEdit"));
    m_logFileEdit->setReadOnly(true);
    form->addRow(m_lblLogFileTitle, m_logFileEdit);
    logsLay->addLayout(form);

    m_lblRetentionInfo = new QLabel(m_groupLogs);
    m_lblRetentionInfo->setWordWrap(true);
    logsLay->addWidget(m_lblRetentionInfo);

    m_lblLogCount = new QLabel(m_groupLogs);
    logsLay->addWidget(m_lblLogCount);

    m_logPreview = new QPlainTextEdit(m_groupLogs);
    m_logPreview->setObjectName(QStringLiteral("logPreviewEdit"));
    m_logPreview->setReadOnly(true);
    m_logPreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logPreview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_logPreview->setMinimumHeight(180);
    logsLay->addWidget(m_logPreview, 1);

    auto* logBtnRow = new QHBoxLayout();
    m_btnOpenLogDir = new QPushButton(m_groupLogs);
    m_btnOpenLogDir->setObjectName(QStringLiteral("btnOpenLogDir"));
    m_btnRefreshLog = new QPushButton(m_groupLogs);
    m_btnRefreshLog->setObjectName(QStringLiteral("btnRefreshLog"));
    logBtnRow->addWidget(m_btnOpenLogDir);
    logBtnRow->addWidget(m_btnRefreshLog);
    logBtnRow->addStretch(1);
    logsLay->addLayout(logBtnRow);

    root->addWidget(m_groupLogs, 1);

    // 3. Rodapé
    auto* footer = new QHBoxLayout();
    m_btnRestoreDefaults = new QPushButton(this);
    m_btnRestoreDefaults->setObjectName(QStringLiteral("btnStartupRestoreDefaults"));
    m_btnClose = new QPushButton(this);
    m_btnClose->setObjectName(QStringLiteral("btnStartupClose"));
    m_btnClose->setDefault(true);
    footer->addWidget(m_btnRestoreDefaults);
    footer->addStretch(1);
    footer->addWidget(m_btnClose);
    root->addLayout(footer);

    connect(m_radioAsk, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) onControlsEdited();
    });
    connect(m_radioAutoLoad, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) onControlsEdited();
    });
    connect(m_radioNewProject, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) onControlsEdited();
    });
    connect(m_chkConfirmClose, &QCheckBox::toggled, this, [this](bool) {
        onControlsEdited();
    });
    connect(m_btnRestoreDefaults, &QPushButton::clicked, this, &StartupSessionDialog::onRestoreDefaultsClicked);
    connect(m_btnOpenLogDir, &QPushButton::clicked, this, &StartupSessionDialog::onOpenLogDirectoryClicked);
    connect(m_btnRefreshLog, &QPushButton::clicked, this, &StartupSessionDialog::onRefreshLogClicked);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::close);
}

io::StartupSessionBehavior StartupSessionDialog::selectedBehavior() const {
    if (m_radioAutoLoad && m_radioAutoLoad->isChecked()) {
        return io::StartupSessionBehavior::AutoLoadPrevious;
    }
    if (m_radioNewProject && m_radioNewProject->isChecked()) {
        return io::StartupSessionBehavior::AlwaysStartNew;
    }
    return io::StartupSessionBehavior::AskOnStartup;
}

bool StartupSessionDialog::confirmUnsavedOnClose() const {
    return m_chkConfirmClose ? m_chkConfirmClose->isChecked() : true;
}

void StartupSessionDialog::syncSettings(io::StartupSessionBehavior behavior,
                                        bool confirmUnsavedOnClose) {
    m_behavior = behavior;
    m_confirmUnsavedOnClose = confirmUnsavedOnClose;
    applyValuesToControls();
}

void StartupSessionDialog::applyValuesToControls() {
    m_updatingControls = true;
    if (m_radioAsk) {
        m_radioAsk->setChecked(m_behavior == io::StartupSessionBehavior::AskOnStartup);
    }
    if (m_radioAutoLoad) {
        m_radioAutoLoad->setChecked(m_behavior == io::StartupSessionBehavior::AutoLoadPrevious);
    }
    if (m_radioNewProject) {
        m_radioNewProject->setChecked(m_behavior == io::StartupSessionBehavior::AlwaysStartNew);
    }
    if (m_chkConfirmClose) {
        m_chkConfirmClose->setChecked(m_confirmUnsavedOnClose);
    }
    m_updatingControls = false;
}

void StartupSessionDialog::refreshLogSummary() {
    if (m_logDirEdit) {
        m_logDirEdit->setText(QDir::toNativeSeparators(io::AppLogger::logDirectory()));
    }
    if (m_logFileEdit) {
        m_logFileEdit->setText(QDir::toNativeSeparators(io::AppLogger::currentLogFilePath()));
    }
    const QStringList files = io::AppLogger::existingLogFiles();
    if (m_lblLogCount) {
        m_lblLogCount->setText(
            GerenciadorTraducao::trText("Arquivos de log no diretório: %1 / %2 máximo")
                .arg(files.size())
                .arg(io::AppLogger::kMaxLogFiles));
    }
    if (m_logPreview) {
        const QString recent = io::AppLogger::readRecentLines(250);
        m_logPreview->setPlainText(
            recent.isEmpty()
                ? GerenciadorTraducao::trText("Nenhum registro gravado ainda neste arquivo de log.")
                : recent);
        QTextCursor cursor = m_logPreview->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_logPreview->setTextCursor(cursor);
    }
}

void StartupSessionDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Configurações de Sessão, Inicialização e Logs"));
    if (m_groupStartup) {
        m_groupStartup->setTitle(GerenciadorTraducao::trText("Comportamento ao Iniciar o Aplicativo"));
    }
    if (m_lblStartupDesc) {
        m_lblStartupDesc->setText(GerenciadorTraducao::trText(
            "Defina se o aplicativo deve perguntar a cada inicialização ou executar automaticamente uma ação padrão quando existir projeto, exemplo ou edição da sessão anterior:"));
    }
    if (m_radioAsk) {
        m_radioAsk->setText(GerenciadorTraducao::trText(
            "Perguntar a cada nova inicialização (emitir diálogo de escolha)"));
    }
    if (m_radioAutoLoad) {
        m_radioAutoLoad->setText(GerenciadorTraducao::trText(
            "Carregar automaticamente o projeto, exemplo ou edição deixado em aberto"));
    }
    if (m_radioNewProject) {
        m_radioNewProject->setText(GerenciadorTraducao::trText(
            "Sempre inicializar um novo projeto vazio"));
    }
    if (m_chkConfirmClose) {
        m_chkConfirmClose->setText(GerenciadorTraducao::trText(
            "Exibir diálogo de confirmação para alterações não salvas ao sair"));
    }

    if (m_groupLogs) {
        m_groupLogs->setTitle(GerenciadorTraducao::trText("Registro Diário de Logs"));
    }
    if (m_lblLogDirTitle) {
        m_lblLogDirTitle->setText(GerenciadorTraducao::trText("Diretório de Logs:"));
    }
    if (m_lblLogFileTitle) {
        m_lblLogFileTitle->setText(GerenciadorTraducao::trText("Arquivo Atual:"));
    }
    if (m_lblRetentionInfo) {
        m_lblRetentionInfo->setText(GerenciadorTraducao::trText(
            "Registra todos os níveis de mensagens (Trace, Debug, Info, Warning, Critical e Fatal) no padrão omniframe_aaaa-mm-dd.log, mantendo no máximo 10 arquivos (o mais antigo é excluído ao gerar um mais recente)."));
    }
    if (m_btnOpenLogDir) {
        m_btnOpenLogDir->setText(GerenciadorTraducao::trText("Abrir Pasta de Logs"));
    }
    if (m_btnRefreshLog) {
        m_btnRefreshLog->setText(GerenciadorTraducao::trText("Atualizar Log"));
    }
    if (m_btnRestoreDefaults) {
        m_btnRestoreDefaults->setText(GerenciadorTraducao::trText("Restaurar Padrão"));
    }
    if (m_btnClose) {
        m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
    }
    refreshLogSummary();
}

void StartupSessionDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void StartupSessionDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    refreshLogSummary();
}

void StartupSessionDialog::onControlsEdited() {
    if (m_updatingControls) {
        return;
    }
    m_behavior = selectedBehavior();
    m_confirmUnsavedOnClose = confirmUnsavedOnClose();
    emit settingsChanged(m_behavior, m_confirmUnsavedOnClose);
    refreshLogSummary();
}

void StartupSessionDialog::onRestoreDefaultsClicked() {
    emit restoreDefaultsRequested();
    refreshLogSummary();
}

void StartupSessionDialog::onOpenLogDirectoryClicked() {
    emit openLogDirectoryRequested();
}

void StartupSessionDialog::onRefreshLogClicked() {
    refreshLogSummary();
}

} // namespace omniframe
