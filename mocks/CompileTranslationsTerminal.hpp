#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace mocks::compile_translations_terminal {

inline constexpr const char* kFallbackAppVersion = "2026.11.17.0";

enum class ModoExecucao {
    CompilarTodos,
    CompilarSelecionados,
    VerificarApenas
};

struct ResultadoArquivoTraducao {
    QString arquivoTs;
    QString arquivoQm;
    bool sucesso = false;
    bool atualizado = false;
    QString mensagem;
};

struct RelatorioCompilacaoTerminal {
    bool sucesso = false;
    QString raizProjeto;
    QString diretorioTraducoes;
    QString caminhoLrelease;
    int totalEncontrados = 0;
    int totalCompilados = 0;
    int totalErros = 0;
    QString mensagemGeral;
    QVector<ResultadoArquivoTraducao> itens;
};

QString resolver_raiz_projeto(const QString& origem = QString());
QString resolver_diretorio_traducoes(const QString& raizProjeto = QString(), const QString& overrideDir = QString());
QString localizar_lrelease(const QString& overridePath = QString());
QStringList listar_arquivos_ts(const QString& diretorioTraducoes);

RelatorioCompilacaoTerminal executar_compilacao(
    const QString& dirTraducoes,
    const QString& lreleaseOverride = QString(),
    const QStringList& arquivosEspecificos = {},
    ModoExecucao modo = ModoExecucao::CompilarTodos,
    bool idiomaIngles = false
);

int run_terminal(int argc, char* argv[]);

} // namespace mocks::compile_translations_terminal

