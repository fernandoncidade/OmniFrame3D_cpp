#include "ResultTableModel.hpp"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPalette>

#include <algorithm>
#include <utility>

namespace omniframe::ui {

ResultTableModel::ResultTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void ResultTableModel::setHeaders(const QStringList& headers) {
    if (m_headers == headers) {
        return;
    }
    const bool colCountChanged = (m_headers.size() != headers.size());
    if (colCountChanged) {
        beginResetModel();
        m_headers = headers;
        endResetModel();
        return;
    }
    m_headers = headers;
    if (!m_headers.isEmpty()) {
        emit headerDataChanged(Qt::Horizontal, 0, static_cast<int>(m_headers.size()) - 1);
    }
}

void ResultTableModel::setRows(std::vector<ResultTableRow> rows) {
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

void ResultTableModel::clearRows() {
    if (m_rows.empty()) {
        return;
    }
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void ResultTableModel::updateSummaryLabel(const QString& label) {
    for (int r = 0; r < static_cast<int>(m_rows.size()); ++r) {
        ResultTableRow& row = m_rows[static_cast<std::size_t>(r)];
        if (row.isSummary && !row.cells.isEmpty() && row.cells[0] != label) {
            row.cells[0] = label;
            const QModelIndex idx = index(r, 0);
            emit dataChanged(idx, idx, {Qt::DisplayRole});
        }
    }
}

void ResultTableModel::setThemeState(bool isLightTheme, bool nativeUi) {
    if (m_isLightTheme == isLightTheme && m_nativeUi == nativeUi) {
        return;
    }
    m_isLightTheme = isLightTheme;
    m_nativeUi = nativeUi;
    if (!m_rows.empty() && !m_headers.isEmpty()) {
        emit dataChanged(
            index(0, 0),
            index(static_cast<int>(m_rows.size()) - 1, static_cast<int>(m_headers.size()) - 1),
            {Qt::BackgroundRole, Qt::ForegroundRole, Qt::FontRole});
    }
}

int ResultTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_rows.size());
}

int ResultTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_headers.size());
}

QVariant ResultTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(m_rows.size()) ||
        index.column() < 0 ||
        index.column() >= static_cast<int>(m_headers.size())) {
        return {};
    }

    const ResultTableRow& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
        return (index.column() < row.cells.size()) ? row.cells[index.column()] : QString();
    case kResultTableSummaryRowRole:
        return row.isSummary;
    case kResultTableRowCategoryRole:
        return row.category;
    case kResultTableEntityIdRole:
        return row.hasEntityId ? QVariant(row.entityId) : QVariant();
    case Qt::BackgroundRole:
        if (row.isSummary && index.column() == 0) {
            if (m_nativeUi) {
                return QApplication::palette().brush(QPalette::AlternateBase);
            }
            return QBrush(m_isLightTheme ? QColor(219, 234, 254) : QColor(43, 108, 176));
        }
        return {};
    case Qt::ForegroundRole:
        if (row.isSummary && index.column() == 0) {
            if (m_nativeUi) {
                return QApplication::palette().brush(QPalette::WindowText);
            }
            return QBrush(m_isLightTheme ? QColor(30, 58, 138) : QColor(Qt::white));
        }
        if (!m_nativeUi) {
            return QBrush(m_isLightTheme ? QColor(20, 20, 20) : QColor(235, 235, 235));
        }
        return {};
    case Qt::FontRole:
        if (row.isSummary && index.column() == 0) {
            QFont boldFont;
            boldFont.setBold(true);
            return boldFont;
        }
        return {};
    default:
        return {};
    }
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal) {
        if (section < 0 || section >= static_cast<int>(m_headers.size())) {
            return {};
        }
        if (role == Qt::DisplayRole) {
            return m_headers[section];
        }
        if (role == Qt::ToolTipRole) {
            QString singleLine = m_headers[section];
            singleLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
            return singleLine.simplified();
        }
        if (role == Qt::TextAlignmentRole) {
            return static_cast<int>(Qt::AlignCenter);
        }
        if (role == Qt::FontRole) {
            QFont boldFont;
            boldFont.setBold(true);
            return boldFont;
        }
        return {};
    }
    if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
        return section + 1;
    }
    return {};
}

Qt::ItemFlags ResultTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(m_rows.size()) ||
        index.column() < 0 ||
        index.column() >= static_cast<int>(m_headers.size())) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

ResultTableProxyModel::ResultTableProxyModel(QObject* parent)
    : QAbstractProxyModel(parent) {}

void ResultTableProxyModel::setSourceModel(QAbstractItemModel* src) {
    if (sourceModel() == src) {
        return;
    }
    for (const QMetaObject::Connection& conn : m_sourceConnections) {
        disconnect(conn);
    }
    m_sourceConnections.clear();

    beginResetModel();
    QAbstractProxyModel::setSourceModel(src);
    rebuildIdentityRowOrder();
    m_wrappedHeaders.clear();
    m_headerTooltips.clear();
    ensureHeaderCacheMatchesSource();

    if (src != nullptr) {
        m_sourceConnections.push_back(
            connect(src, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
                beginResetModel();
            }));
        m_sourceConnections.push_back(
            connect(src, &QAbstractItemModel::modelReset, this, [this]() {
                rebuildIdentityRowOrder();
                ensureHeaderCacheMatchesSource();
                endResetModel();
            }));
        m_sourceConnections.push_back(
            connect(src,
                    &QAbstractItemModel::dataChanged,
                    this,
                    [this](const QModelIndex& topLeft,
                           const QModelIndex& bottomRight,
                           const QList<int>& roles) {
                        if (rowCount() <= 0 || columnCount() <= 0) {
                            return;
                        }
                        if (topLeft.row() == bottomRight.row()) {
                            const QModelIndex pTopLeft = mapFromSource(topLeft);
                            const QModelIndex pBottomRight = mapFromSource(bottomRight);
                            if (pTopLeft.isValid() && pBottomRight.isValid()) {
                                emit dataChanged(pTopLeft, pBottomRight, roles);
                            }
                        } else {
                            const int colMin = std::clamp(topLeft.column(), 0, columnCount() - 1);
                            const int colMax = std::clamp(bottomRight.column(), colMin, columnCount() - 1);
                            emit dataChanged(index(0, colMin), index(rowCount() - 1, colMax), roles);
                        }
                    }));
        m_sourceConnections.push_back(
            connect(src,
                    &QAbstractItemModel::headerDataChanged,
                    this,
                    [this](Qt::Orientation orientation, int first, int last) {
                        if (orientation == Qt::Horizontal) {
                            ensureHeaderCacheMatchesSource();
                        }
                        emit headerDataChanged(orientation, first, last);
                    }));
    }
    endResetModel();
}

void ResultTableProxyModel::rebuildIdentityRowOrder() {
    const int srcRows = sourceModel() ? sourceModel()->rowCount() : 0;
    m_rowOrder.resize(static_cast<std::size_t>(std::max(0, srcRows)));
    for (int r = 0; r < srcRows; ++r) {
        m_rowOrder[static_cast<std::size_t>(r)] = r;
    }
}

void ResultTableProxyModel::ensureHeaderCacheMatchesSource() {
    const int cols = sourceModel() ? std::max(0, sourceModel()->columnCount()) : 0;
    if (m_wrappedHeaders.size() != cols) {
        m_wrappedHeaders = QStringList(cols);
    }
    if (m_headerTooltips.size() != cols) {
        m_headerTooltips = QStringList(cols);
    }
}

QModelIndex ResultTableProxyModel::index(int row, int column, const QModelIndex& parent) const {
    if (parent.isValid() ||
        sourceModel() == nullptr ||
        row < 0 ||
        row >= static_cast<int>(m_rowOrder.size()) ||
        column < 0 ||
        column >= columnCount()) {
        return {};
    }
    return createIndex(row, column);
}

QModelIndex ResultTableProxyModel::parent(const QModelIndex&) const {
    return {};
}

int ResultTableProxyModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || sourceModel() == nullptr) {
        return 0;
    }
    return static_cast<int>(m_rowOrder.size());
}

int ResultTableProxyModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid() || sourceModel() == nullptr) {
        return 0;
    }
    return sourceModel()->columnCount();
}

QModelIndex ResultTableProxyModel::mapToSource(const QModelIndex& proxyIndex) const {
    if (!proxyIndex.isValid() || sourceModel() == nullptr) {
        return {};
    }
    const int pr = proxyIndex.row();
    if (pr < 0 || pr >= static_cast<int>(m_rowOrder.size())) {
        return {};
    }
    return sourceModel()->index(m_rowOrder[static_cast<std::size_t>(pr)], proxyIndex.column());
}

QModelIndex ResultTableProxyModel::mapFromSource(const QModelIndex& sourceIndex) const {
    if (!sourceIndex.isValid() || sourceModel() == nullptr) {
        return {};
    }
    const int sr = sourceIndex.row();
    if (sr >= 0 &&
        sr < static_cast<int>(m_rowOrder.size()) &&
        m_rowOrder[static_cast<std::size_t>(sr)] == sr) {
        return index(sr, sourceIndex.column());
    }
    const auto it = std::find(m_rowOrder.begin(), m_rowOrder.end(), sr);
    if (it == m_rowOrder.end()) {
        return {};
    }
    return index(static_cast<int>(std::distance(m_rowOrder.begin(), it)), sourceIndex.column());
}

QVariant ResultTableProxyModel::data(const QModelIndex& proxyIndex, int role) const {
    if (!proxyIndex.isValid() || sourceModel() == nullptr) {
        return {};
    }
    return sourceModel()->data(mapToSource(proxyIndex), role);
}

QVariant ResultTableProxyModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (sourceModel() == nullptr) {
        return {};
    }
    if (orientation == Qt::Horizontal && section >= 0 && section < columnCount()) {
        if (role == Qt::DisplayRole &&
            section < m_wrappedHeaders.size() &&
            !m_wrappedHeaders[section].isEmpty()) {
            return m_wrappedHeaders[section];
        }
        if (role == Qt::ToolTipRole &&
            section < m_headerTooltips.size() &&
            !m_headerTooltips[section].isEmpty()) {
            return m_headerTooltips[section];
        }
        if (role == Qt::TextAlignmentRole) {
            return static_cast<int>(Qt::AlignCenter);
        }
        if (role == Qt::FontRole) {
            QFont boldFont;
            boldFont.setBold(true);
            return boldFont;
        }
    }
    return sourceModel()->headerData(section, orientation, role);
}

Qt::ItemFlags ResultTableProxyModel::flags(const QModelIndex& proxyIndex) const {
    if (!proxyIndex.isValid() || sourceModel() == nullptr) {
        return Qt::NoItemFlags;
    }
    return sourceModel()->flags(mapToSource(proxyIndex));
}

int ResultTableProxyModel::sourceRowAt(int proxyRow) const noexcept {
    if (proxyRow < 0 || proxyRow >= static_cast<int>(m_rowOrder.size())) {
        return -1;
    }
    return m_rowOrder[static_cast<std::size_t>(proxyRow)];
}

void ResultTableProxyModel::setRowOrder(std::vector<int> order) {
    if (order.size() != m_rowOrder.size() || order == m_rowOrder) {
        return;
    }
    emit layoutAboutToBeChanged();
    const QModelIndexList persistent = persistentIndexList();
    std::vector<int> oldSourceRows;
    oldSourceRows.reserve(static_cast<std::size_t>(persistent.size()));
    for (const QModelIndex& pIdx : persistent) {
        if (pIdx.isValid() && pIdx.row() >= 0 && pIdx.row() < static_cast<int>(m_rowOrder.size())) {
            oldSourceRows.push_back(m_rowOrder[static_cast<std::size_t>(pIdx.row())]);
        } else {
            oldSourceRows.push_back(-1);
        }
    }

    m_rowOrder = std::move(order);

    std::vector<int> sourceToNewProxy(m_rowOrder.size(), -1);
    for (int pr = 0; pr < static_cast<int>(m_rowOrder.size()); ++pr) {
        const int sr = m_rowOrder[static_cast<std::size_t>(pr)];
        if (sr >= 0 && sr < static_cast<int>(sourceToNewProxy.size())) {
            sourceToNewProxy[static_cast<std::size_t>(sr)] = pr;
        }
    }

    QModelIndexList newPersistent;
    newPersistent.reserve(persistent.size());
    for (int i = 0; i < persistent.size(); ++i) {
        const int sr = oldSourceRows[static_cast<std::size_t>(i)];
        const int newPr = (sr >= 0 && sr < static_cast<int>(sourceToNewProxy.size()))
                              ? sourceToNewProxy[static_cast<std::size_t>(sr)]
                              : -1;
        newPersistent.append(newPr >= 0 ? index(newPr, persistent[i].column()) : QModelIndex());
    }
    changePersistentIndexList(persistent, newPersistent);
    emit layoutChanged();
}

void ResultTableProxyModel::resetRowOrder() {
    const int n = sourceModel() ? sourceModel()->rowCount() : 0;
    std::vector<int> identity(static_cast<std::size_t>(std::max(0, n)));
    for (int r = 0; r < n; ++r) {
        identity[static_cast<std::size_t>(r)] = r;
    }
    setRowOrder(std::move(identity));
}

void ResultTableProxyModel::reverseRows(bool keepSummaryPinnedLast) {
    const int total = static_cast<int>(m_rowOrder.size());
    if (total <= 1) {
        return;
    }
    int sortableRows = total;
    if (keepSummaryPinnedLast &&
        data(index(total - 1, 0), kResultTableSummaryRowRole).toBool()) {
        sortableRows = total - 1;
    }
    if (sortableRows <= 1) {
        return;
    }
    std::vector<int> reversed = m_rowOrder;
    std::reverse(reversed.begin(), reversed.begin() + sortableRows);
    setRowOrder(std::move(reversed));
}

void ResultTableProxyModel::setWrappedHeader(int section, const QString& wrappedText, const QString& tooltip) {
    ensureHeaderCacheMatchesSource();
    if (section < 0 || section >= m_wrappedHeaders.size()) {
        return;
    }
    const bool changed = (m_wrappedHeaders[section] != wrappedText) || (m_headerTooltips[section] != tooltip);
    m_wrappedHeaders[section] = wrappedText;
    m_headerTooltips[section] = tooltip;
    if (changed) {
        emit headerDataChanged(Qt::Horizontal, section, section);
    }
}

} // namespace omniframe::ui
