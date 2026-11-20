#pragma once

#include <QIcon>
#include <QMainWindow>
#include <QString>
#include <QStringList>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProcess;
class QPushButton;
class QTextEdit;
class QTimer;

namespace mocks::compile_translations_gui {

QIcon obter_icone_aplicacao();

class CompileTranslationsGuiWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit CompileTranslationsGuiWindow(QWidget* parent = nullptr);
    ~CompileTranslationsGuiWindow() override;

    void setTranslationDir(const QString& dirPath);
    void setLreleasePath(const QString& lreleasePath);
    void triggerCompileAllAsync();
    [[nodiscard]] bool isCompilationRunning() const noexcept { return m_compilationRunning; }
    [[nodiscard]] int batchTotalCount() const noexcept { return m_batchTotal; }
    [[nodiscard]] int batchOkCount() const noexcept { return m_batchOk; }
    [[nodiscard]] int batchErrorCount() const noexcept { return m_batchErrors; }
    [[nodiscard]] QString logContent() const;

private slots:
    void aoSelecionarPastaTraducoes();
    void aoSelecionarLrelease();
    void aoDetectarPadrao();
    void aoCompilarTodas();
    void aoCompilarSelecionadas();
    void aoVerificarStatus();
    void aoAlternarIdioma(int index);

private:
    void configurarUi();
    void aplicarEstilo();
    void aplicarTraducoesUi();
    void atualizarListaArquivos();
    void registrarLog(const QString& linha);
    QString trLocal(const QString& pt, const QString& en) const;
    void iniciarCompilacaoAssincrona(const QStringList& arquivosEspecificos);
    void processarProximoDaFila();
    void finalizarCompilacaoAssincrona();
    void definirControlesHabilitados(bool enabled);

    bool m_idiomaIngles = false;
    bool m_compilationRunning = false;
    bool m_currentItemHandled = false;
    bool m_timedOutCurrent = false;
    int m_batchTotal = 0;
    int m_batchOk = 0;
    int m_batchErrors = 0;
    QString m_activeLreleasePath;
    QString m_activeDirTraducoes;
    QString m_activeTsPath;
    QString m_activeQmPath;
    QStringList m_pendingTsQueue;
    QProcess* m_activeProcess = nullptr;
    QTimer* m_processTimeoutTimer = nullptr;

    QLabel* m_tituloLabel = nullptr;
    QLabel* m_subtituloLabel = nullptr;
    QLabel* m_idiomaLabel = nullptr;
    QComboBox* m_idiomaCombo = nullptr;

    QGroupBox* m_grupoConfig = nullptr;
    QLabel* m_dirLabel = nullptr;
    QLineEdit* m_dirEdit = nullptr;
    QPushButton* m_btnProcurarDir = nullptr;
    QPushButton* m_btnDetectarPadrao = nullptr;

    QLabel* m_lreleaseLabel = nullptr;
    QLineEdit* m_lreleaseEdit = nullptr;
    QPushButton* m_btnProcurarLrelease = nullptr;

    QGroupBox* m_grupoArquivos = nullptr;
    QListWidget* m_listaTs = nullptr;

    QPushButton* m_btnCompilarTodas = nullptr;
    QPushButton* m_btnCompilarSelecionadas = nullptr;
    QPushButton* m_btnVerificar = nullptr;

    QGroupBox* m_grupoLog = nullptr;
    QTextEdit* m_logText = nullptr;
};

int run_gui(int argc, char* argv[]);

} // namespace mocks::compile_translations_gui

