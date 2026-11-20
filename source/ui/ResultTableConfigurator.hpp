#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <Qt>

#include <vector>

class QEvent;
class QMenu;
class QPoint;
class QTableWidget;
class QTableWidgetItem;

namespace omniframe {

class WindowZOrderManager;

namespace ui {

/**
 * @brief Identifica qual tabela de resultados estruturais está sendo configurada.
 */
enum class ResultTableKind {
    Displacements,
    Reactions,
    BarForces,
    ContinuumStress
};

/**
 * @brief Modo de ordenação de inserção das linhas na tabela de resultados.
 */
enum class TableRowOrderMode {
    AppendBottom, ///< Ordem natural de inserção (1..N).
    PrependTop    ///< Ordem invertida (N..1), preservando linha de resumo ao final.
};

/**
 * @brief Papéis de dados customizados armazenados nos itens das tabelas de resultados.
 */
inline constexpr int kResultTableSummaryRowRole = Qt::UserRole + 10;
inline constexpr int kResultTableRowCategoryRole = Qt::UserRole + 11;
inline constexpr int kResultTableEntityIdRole = Qt::UserRole + 12;

/**
 * @brief Gerencia configuração estrutural, seleção, ordenação inteligente, quebra dinâmica de
 * cabeçalhos, coloração adaptativa, menu de contexto persistente, atalhos e exportação das
 * tabelas do painel "Tabelas de Resultados e Diagnóstico Estrutural".
 */
class ResultTableConfigurator final : public QObject {
    Q_OBJECT

public:
    explicit ResultTableConfigurator(WindowZOrderManager* windowManager = nullptr,
                                     QObject* parent = nullptr);
    ~ResultTableConfigurator() override = default;

    /**
     * @brief Configura inicialmente uma QTableWidget com o padrão visual e interativo completo.
     */
    void configureTable(QTableWidget* table,
                        ResultTableKind kind,
                        const QStringList& headers,
                        bool isLightTheme);

    /**
     * @brief Atualiza os rótulos originais do cabeçalho (ex.: após troca de idioma) preservando
     * larguras ajustadas, visibilidade das colunas, quebra de linha dinâmica e ordenação.
     */
    void updateHeaders(QTableWidget* table, const QStringList& headers);

    /**
     * @brief Reaplica ordem das linhas, ordenação ativa (mantendo "Σ Reações" ao fim),
     * larguras ideais iniciais na primeira população com dados e cores/estado visual.
     */
    void afterTablePopulated(QTableWidget* table);

    /**
     * @brief Atualiza paleta, QSS, cores-padrão e viewport de todas as tabelas registradas.
     */
    void applyTheme(bool isLightTheme);

    /**
     * @brief Habilita ou desabilita o modo de ordenação ao clicar no cabeçalho.
     */
    void setSortingModeEnabled(QTableWidget* table, bool enabled);
    [[nodiscard]] bool isSortingModeEnabled(const QTableWidget* table) const;

    /**
     * @brief Executa ordenação inteligente (numérica, intervalos [min; max], unidades e natural)
     * mantendo linhas de resumo estrutural fixadas na última linha.
     */
    void sortTableByColumn(QTableWidget* table, int column, Qt::SortOrder order);

    /**
     * @brief Define o modo de ordem das linhas (Adicionar no Fim vs Adicionar no Topo).
     */
    void setRowOrderMode(QTableWidget* table, TableRowOrderMode mode);
    [[nodiscard]] TableRowOrderMode rowOrderMode(const QTableWidget* table) const;

    /**
     * @brief Inverte fisicamente a ordem atual das linhas exibidas (mantendo linha de resumo no fim).
     */
    void invertTableRows(QTableWidget* table);

    /**
     * @brief Controla visibilidade de colunas individuais e restauração.
     */
    void setColumnVisible(QTableWidget* table, int column, bool visible);
    void showAllColumns(QTableWidget* table);
    void resetColumns(QTableWidget* table);

    /**
     * @brief Controla exibição de cores nas colunas e seleção das colunas coloridas.
     */
    void setShowColumnColors(QTableWidget* table, bool enabled);
    [[nodiscard]] bool showColumnColors(const QTableWidget* table) const;
    void setColumnColored(QTableWidget* table, int column, bool colored);
    [[nodiscard]] bool isColumnColored(const QTableWidget* table, int column) const;
    void setAllColumnsColored(QTableWidget* table, bool colored);
    void resetColumnColors(QTableWidget* table);

    /**
     * @brief Configura e consulta cores semânticas por categoria de linha.
     */
    void setCategoryColor(QTableWidget* table, const QString& categoryKey, const QColor& color);
    [[nodiscard]] QColor categoryColor(const QTableWidget* table, const QString& categoryKey) const;
    void resetCategoryColors(QTableWidget* table);

    /**
     * @brief Calcula cor de texto (preto ou branco) de máximo contraste via luminância relativa sRGB.
     */
    [[nodiscard]] static QColor idealTextColorForBackground(const QColor& background);

    /**
     * @brief Configura opções de filtragem de exportação.
     */
    void setExportOnlyActiveColumns(QTableWidget* table, bool activeOnly);
    [[nodiscard]] bool exportOnlyActiveColumns(const QTableWidget* table) const;
    void setExportOnlySelectedCells(QTableWidget* table, bool selectedOnly);
    [[nodiscard]] bool exportOnlySelectedCells(const QTableWidget* table) const;
    void resetExportOptions(QTableWidget* table);

    /**
     * @brief Copia seleção (com ou sem cabeçalhos) ou tabela inteira para a área de transferência em TSV.
     */
    void copySelectionToClipboard(QTableWidget* table, bool includeHeaders) const;
    void copyWholeTableToClipboard(QTableWidget* table) const;

    /**
     * @brief Exporta os dados respeitando opções ativas para .xlsx, .csv, .txt, .json ou .xml.
     */
    [[nodiscard]] bool exportTableToFile(QTableWidget* table,
                                         const QString& filePath,
                                         QString* errorMessage = nullptr) const;

    /**
     * @brief Abre diálogo de salvamento não bloqueante quando gerenciado por WindowZOrderManager.
     */
    void showExportDialog(QTableWidget* table);

    /**
     * @brief Constrói o menu de contexto completo para a tabela ou seu cabeçalho horizontal.
     */
    [[nodiscard]] QMenu* createTableContextMenu(QTableWidget* table,
                                                QWidget* parent = nullptr,
                                                bool triggeredFromHeader = false);

    /**
     * @brief Recalcula a quebra dinâmica em palavras dos cabeçalhos e altura do QHeaderView.
     */
    void updateHeaderWordWrap(QTableWidget* table);

    /**
     * @brief Seleciona todas as células e colunas (equivalente ao botão de canto superior esquerdo).
     */
    void selectCornerAll(QTableWidget* table);

    /**
     * @brief Restaura todas as opções configuráveis da tabela para seus padrões iniciais.
     */
    void resetAllDefaults(QTableWidget* table);

    /**
     * @brief Serializa e restaura o estado configurável de uma tabela ou de todas as tabelas.
     */
    [[nodiscard]] QJsonObject saveTableStateToJson(const QTableWidget* table) const;
    void loadTableStateFromJson(QTableWidget* table, const QJsonObject& obj);
    [[nodiscard]] QJsonObject saveAllTablesToJson() const;
    void loadAllTablesFromJson(const QJsonObject& tablesObj);

signals:
    void configurationChanged(QTableWidget* table);
    void nodeDoubleClicked(unsigned int nodeId);
    void elementDoubleClicked(unsigned int elementId);
    void continuumElementDoubleClicked(unsigned int elementId, const QString& typeName);
    void statusMessageRequested(const QString& message, int timeoutMs = 4000);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct TableState {
        QTableWidget* table = nullptr;
        ResultTableKind kind = ResultTableKind::Displacements;
        QStringList originalHeaders;
        QHash<QString, QColor> categoryColors;
        QList<int> coloredColumns;
        bool sortingEnabled = false;
        int sortColumn = -1;
        Qt::SortOrder sortOrder = Qt::AscendingOrder;
        int headerAnchorColumn = -1;
        TableRowOrderMode rowOrderMode = TableRowOrderMode::AppendBottom;
        TableRowOrderMode appliedRowOrderMode = TableRowOrderMode::AppendBottom;
        bool showColumnColors = true;
        bool exportActiveColumnsOnly = false;
        bool exportSelectedCellsOnly = false;
        bool suspendHeaderWrap = false;
        bool hasInitialFit = false;
    };

    struct CategorySpec {
        QString key;
        QString label;
        QColor darkDefault;
        QColor lightDefault;
    };

    TableState* findState(QTableWidget* table);
    const TableState* findState(const QTableWidget* table) const;

    static std::vector<CategorySpec> categoriesForKind(ResultTableKind kind);
    static QString defaultCategoryForKind(ResultTableKind kind);
    static QHash<QString, QColor> buildDefaultCategoryColors(ResultTableKind kind, bool isLightTheme);

    void applyStructuralConfig(QTableWidget* table, const QStringList& headers);
    void applyVisualThemeToTable(QTableWidget* table, bool isLightTheme);
    void applyIdealColumnWidths(TableState& state, bool forceResetWidths);
    void syncColoredDelegateState(const TableState& state);
    void handleHeaderSectionClicked(TableState& state, int logicalColumn);
    void selectHeaderColumnRange(TableState& state, int startCol, int endCol, bool preserveExisting);
    void handleCellDoubleClicked(const TableState& state, int row, int column);
    void reverseTableRowsInPlace(QTableWidget* table, bool keepSummaryPinnedLast);
    void showContextMenuAt(TableState& state, const QPoint& globalPos, bool fromHeader);
    void pickCategoryColor(TableState& state, const QString& categoryKey);

    [[nodiscard]] bool isSummaryRow(const QTableWidget* table, int row) const;
    [[nodiscard]] QList<int> columnsToProcess(const TableState& state, bool applyExportFilter) const;
    [[nodiscard]] QList<int> rowsToProcess(const TableState& state, bool applyExportFilter) const;
    [[nodiscard]] QString buildTsvText(const TableState& state,
                                       const QList<int>& rows,
                                       const QList<int>& cols,
                                       bool includeHeaders) const;

    WindowZOrderManager* m_windowManager = nullptr;
    bool m_isLightTheme = false;
    bool m_applyingSavedState = false;
    std::vector<TableState> m_tables;
    QHash<QString, QJsonObject> m_savedStatesByTableName;
};

} // namespace ui
} // namespace omniframe
