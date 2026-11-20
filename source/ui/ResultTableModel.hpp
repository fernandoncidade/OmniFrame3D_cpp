#pragma once

#include <QAbstractProxyModel>
#include <QAbstractTableModel>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <Qt>

#include <vector>

namespace omniframe::ui {

inline constexpr int kResultTableSummaryRowRole = Qt::UserRole + 10;
inline constexpr int kResultTableRowCategoryRole = Qt::UserRole + 11;
inline constexpr int kResultTableEntityIdRole = Qt::UserRole + 12;

struct ResultTableRow {
    QStringList cells;
    QString category;
    quint64 entityId = 0;
    bool hasEntityId = false;
    bool isSummary = false;
};

/**
 * @brief Modelo de tabela compartilhado (zero clonagem celular entre múltiplos layouts).
 */
class ResultTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit ResultTableModel(QObject* parent = nullptr);
    ~ResultTableModel() override = default;

    void setHeaders(const QStringList& headers);
    [[nodiscard]] const QStringList& headers() const noexcept { return m_headers; }

    void setRows(std::vector<ResultTableRow> rows);
    void clearRows();
    void updateSummaryLabel(const QString& label);
    void setThemeState(bool isLightTheme, bool nativeUi);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    QStringList m_headers;
    std::vector<ResultTableRow> m_rows;
    bool m_isLightTheme = false;
    bool m_nativeUi = false;
};

/**
 * @brief Proxy leve por layout que mantém apenas a permutação local de linhas e
 * quebra de cabeçalhos sem duplicar nenhum item de dados.
 */
class ResultTableProxyModel final : public QAbstractProxyModel {
    Q_OBJECT

public:
    explicit ResultTableProxyModel(QObject* parent = nullptr);
    ~ResultTableProxyModel() override = default;

    void setSourceModel(QAbstractItemModel* sourceModel) override;

    [[nodiscard]] QModelIndex index(int row,
                                    int column,
                                    const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
    [[nodiscard]] QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
    [[nodiscard]] QVariant data(const QModelIndex& proxyIndex, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& proxyIndex) const override;

    [[nodiscard]] int sourceRowAt(int proxyRow) const noexcept;
    void setRowOrder(std::vector<int> order);
    void resetRowOrder();
    void reverseRows(bool keepSummaryPinnedLast);
    void setWrappedHeader(int section, const QString& wrappedText, const QString& tooltip);

private:
    void rebuildIdentityRowOrder();
    void ensureHeaderCacheMatchesSource();

    std::vector<int> m_rowOrder;
    QStringList m_wrappedHeaders;
    QStringList m_headerTooltips;
    std::vector<QMetaObject::Connection> m_sourceConnections;
};

} // namespace omniframe::ui
