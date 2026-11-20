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
class QPushButton;
class QTextEdit;

namespace mocks::compile_translations_gui {

QIcon obter_icone_aplicacao();

class CompileTranslationsGuiWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit CompileTranslationsGuiWindow(QWidget* parent = nullptr);
    ~CompileTranslationsGuiWindow() override = default;

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

    bool m_idiomaIngles = false;

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

