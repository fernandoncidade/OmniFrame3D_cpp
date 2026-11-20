#pragma once

#include "io/ProjectRecoveryStore.hpp"

#include <QDialog>

class QCheckBox;
class QEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;

namespace omniframe {

/**
 * @brief Diálogo não-bloqueante de configuração de Sessão, Inicialização e
 *        inspeção do sistema de Logs Diários do OmniFrame 3D.
 */
class StartupSessionDialog : public QDialog {
    Q_OBJECT

public:
    explicit StartupSessionDialog(io::StartupSessionBehavior behavior,
                                  bool confirmUnsavedOnClose,
                                  QWidget* parent = nullptr);

    io::StartupSessionBehavior selectedBehavior() const;
    bool confirmUnsavedOnClose() const;

    void syncSettings(io::StartupSessionBehavior behavior, bool confirmUnsavedOnClose);
    void refreshLogSummary();

signals:
    void settingsChanged(omniframe::io::StartupSessionBehavior behavior,
                         bool confirmUnsavedOnClose);
    void restoreDefaultsRequested();
    void openLogDirectoryRequested();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onControlsEdited();
    void onRestoreDefaultsClicked();
    void onOpenLogDirectoryClicked();
    void onRefreshLogClicked();

private:
    void setupUi();
    void applyValuesToControls();
    void retranslateUi();

    io::StartupSessionBehavior m_behavior = io::StartupSessionBehavior::AskOnStartup;
    bool m_confirmUnsavedOnClose = true;
    bool m_updatingControls = false;

    QGroupBox* m_groupStartup = nullptr;
    QLabel* m_lblStartupDesc = nullptr;
    QRadioButton* m_radioAsk = nullptr;
    QRadioButton* m_radioAutoLoad = nullptr;
    QRadioButton* m_radioNewProject = nullptr;
    QCheckBox* m_chkConfirmClose = nullptr;

    QGroupBox* m_groupLogs = nullptr;
    QLabel* m_lblLogDirTitle = nullptr;
    QLineEdit* m_logDirEdit = nullptr;
    QLabel* m_lblLogFileTitle = nullptr;
    QLineEdit* m_logFileEdit = nullptr;
    QLabel* m_lblRetentionInfo = nullptr;
    QLabel* m_lblLogCount = nullptr;
    QPlainTextEdit* m_logPreview = nullptr;
    QPushButton* m_btnOpenLogDir = nullptr;
    QPushButton* m_btnRefreshLog = nullptr;

    QPushButton* m_btnRestoreDefaults = nullptr;
    QPushButton* m_btnClose = nullptr;
};

} // namespace omniframe
