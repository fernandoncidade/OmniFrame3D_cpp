#pragma once

#include <QDate>
#include <QIcon>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

namespace mocks::version_editor {

inline constexpr const char* kDefaultAppVersion = "2026.11.17.0";
inline constexpr const char* kDefaultReleaseDate = "2026-11-17";

enum class OperacaoModo {
    Check,
    Apply
};

enum class IdiomaEditor {
    Portugues,
    Ingles
};

enum class StatusArquivo {
    Atualizado,
    SemMudancas,
    Ausente,
    Erro
};

struct ArquivoAlvoInfo {
    QString caminhoRelativo;
    QString categoriaPt;
    QString categoriaEn;
    QString descricaoPt;
    QString descricaoEn;
};

struct RegistroAlteracao {
    QString caminhoRelativo;
    QString categoriaPt;
    QString categoriaEn;
    QString categoria;
    StatusArquivo status = StatusArquivo::SemMudancas;
    QString statusTexto;
    QString resumo;
    int alteracoesVersao = 0;
    int alteracoesData = 0;
    int camposMonitorados = 0;
    bool renomeadoRelease = false;
};

struct RelatorioExecucao {
    bool sucesso = false;
    bool houveAlteracoes = false;
    OperacaoModo modo = OperacaoModo::Check;
    QString raizProjeto;
    QString versaoAnterior;
    QString versaoAlvo;
    QString dataPtBr;
    QString dataEnUs;
    QString dataIso;
    int totalArquivos = 0;
    int totalAtualizados = 0;
    int totalSemMudancas = 0;
    int totalAusentes = 0;
    int totalErros = 0;
    QString mensagemGeral;
    QVector<RegistroAlteracao> registros;
};

QString pasta_padrao_icones(const QString& raizProjeto = QString());
QIcon obter_icone_aplicacao(const QString& raizProjeto = QString());
QString resolver_pasta_raiz_projeto(const QString& caminhoInicial = QString());

QString normalizar_versao(const QString& versao);
bool validar_formato_versao(const QString& versao);
QString formatar_data_pt_br(const QDate& data);
QString formatar_data_en_us(const QDate& data);
bool parsear_data_flexivel(const QString& texto, QDate& dataOut);

QString ler_versao_atual(const QString& raizProjeto);
QDate ler_data_atual(const QString& raizProjeto);
QVector<ArquivoAlvoInfo> listar_arquivos_alvo(const QString& raizProjeto = QString());

RelatorioExecucao executar_edicao_versao(
    const QString& raizProjeto,
    const QString& novaVersao,
    const QDate& novaData,
    OperacaoModo modo,
    IdiomaEditor idioma = IdiomaEditor::Portugues
);

class VersionEditorWidget final : public QMainWindow {
    Q_OBJECT

public:
    explicit VersionEditorWidget(
        const QString& raizInicial = QString(),
        IdiomaEditor idiomaInicial = IdiomaEditor::Portugues,
        QWidget* parent = nullptr
    );
    ~VersionEditorWidget() override = default;

private slots:
    void aoSelecionarPastaRaiz();
    void aoDetectarVersaoAtual();
    void aoVerificarSemAlterar();
    void aoAplicarAtualizacao();
    void aoAlternarIdioma(int index);
    void aoMostrarSobre();

private:
    void configurarUi();
    void aplicarEstilo();
    void aplicarTraducoesUi();
    void carregarContextoProjeto(const QString& raiz);
    void atualizarBadgesProjeto();
    void preencherTabelaArquivosAlvo();
    void atualizarTabelaComRelatorio(const RelatorioExecucao& relatorio);
    void retraduzirRelatorioEmMemoria();
    void adicionarLog(const QString& texto);
    QString trLocal(const QString& pt, const QString& en) const;
    static QString textoStatus(StatusArquivo status, OperacaoModo modo, IdiomaEditor idioma);

    IdiomaEditor m_idioma = IdiomaEditor::Portugues;
    QString m_raizProjeto;
    QString m_versaoAtualDetectada;
    QDate m_dataAtualDetectada;
    bool m_temUltimoRelatorio = false;
    RelatorioExecucao m_ultimoRelatorio;

    QLabel* m_tituloBannerLabel = nullptr;
    QLabel* m_subtituloBannerLabel = nullptr;
    QLabel* m_idiomaLabel = nullptr;
    QComboBox* m_idiomaCombo = nullptr;

    QGroupBox* m_grupoProjeto = nullptr;
    QLabel* m_raizLabel = nullptr;
    QLineEdit* m_raizEdit = nullptr;
    QPushButton* m_btnProcurarRaiz = nullptr;
    QPushButton* m_btnDetectarAtual = nullptr;
    QLabel* m_versaoAtualBadge = nullptr;
    QLabel* m_dataAtualBadge = nullptr;

    QGroupBox* m_grupoParametros = nullptr;
    QLabel* m_novaVersaoLabel = nullptr;
    QLineEdit* m_novaVersaoEdit = nullptr;
    QLabel* m_novaDataLabel = nullptr;
    QDateEdit* m_novaDataEdit = nullptr;
    QPushButton* m_btnDataHoje = nullptr;
    QCheckBox* m_checkIncluirReleaseDoc = nullptr;

    QPushButton* m_btnVerificar = nullptr;
    QPushButton* m_btnAplicar = nullptr;
    QPushButton* m_btnSobre = nullptr;

    QGroupBox* m_grupoArquivos = nullptr;
    QTableWidget* m_tabelaArquivos = nullptr;

    QGroupBox* m_grupoLog = nullptr;
    QTextEdit* m_logTextEdit = nullptr;
};

int run_cli_or_gui(int argc, char* argv[]);

} // namespace mocks::version_editor
