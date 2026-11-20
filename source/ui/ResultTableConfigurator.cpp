#include "ResultTableConfigurator.hpp"

#include "ThemeManager.hpp"
#include "WindowZOrderManager.hpp"
#include "../locale/GerenciadorTraducao.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCollator>
#include <QColorDialog>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace omniframe::ui {

namespace {

constexpr int kHeaderPadding = 40;
constexpr int kMinColumnWidth = 10;
constexpr int kHeaderWidthWrapMargin = 10;
constexpr int kHeaderHeightMargin = 10;

QColor blendColors(const QColor& base, const QColor& overlay, double overlayRatio) {
    const double t = std::clamp(overlayRatio, 0.0, 1.0);
    const double inv = 1.0 - t;
    return QColor(
        std::clamp(static_cast<int>(std::lround(base.red() * inv + overlay.red() * t)), 0, 255),
        std::clamp(static_cast<int>(std::lround(base.green() * inv + overlay.green() * t)), 0, 255),
        std::clamp(static_cast<int>(std::lround(base.blue() * inv + overlay.blue() * t)), 0, 255),
        base.alpha());
}

/**
 * @brief Delegate responsável pela coloração adaptativa por coluna/categoria, contraste sRGB
 * automático e realce suave de hover preservando estilos de seleção e linhas de resumo.
 */
class DelegateCorEstadoVisual final : public QStyledItemDelegate {
public:
    explicit DelegateCorEstadoVisual(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    bool isLightTheme = false;
    bool coresVisiveis = true;
    QList<int> coloredColumns;
    QHash<QString, QColor> categoryColors;
    QString defaultCategory;

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        if (painter == nullptr) {
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const bool hovered = opt.state.testFlag(QStyle::State_MouseOver) &&
                             !opt.state.testFlag(QStyle::State_Selected);
        const bool isSummary = index.data(kResultTableSummaryRowRole).toBool();
        const bool alt = opt.features.testFlag(QStyleOptionViewItem::Alternate);

        const QColor summaryBg =
            (isSummary && opt.backgroundBrush.style() != Qt::NoBrush &&
             opt.backgroundBrush.color().isValid())
                ? opt.backgroundBrush.color()
                : QColor();

        const bool nativeUi = ThemeManager::instance().isNativeUi();
        const QColor defaultText = nativeUi
                                       ? opt.palette.color(QPalette::Text)
                                       : (isLightTheme ? QColor(20, 20, 20) : QColor(235, 235, 235));
        if (!nativeUi) {
            opt.backgroundBrush = QBrush(Qt::NoBrush);
            opt.palette.setBrush(QPalette::Text, QBrush(defaultText));
            opt.palette.setBrush(QPalette::WindowText, QBrush(defaultText));
            opt.palette.setBrush(QPalette::HighlightedText, QBrush(defaultText));
        }

        QColor targetBg;
        bool hasCustomBg = false;

        if (summaryBg.isValid()) {
            targetBg = summaryBg;
            hasCustomBg = true;
        } else if (coresVisiveis && coloredColumns.contains(index.column())) {
            QString catKey = index.data(kResultTableRowCategoryRole).toString();
            if (catKey.isEmpty() && index.column() != 0) {
                catKey = index.siblingAtColumn(0).data(kResultTableRowCategoryRole).toString();
            }
            if (catKey.isEmpty()) {
                catKey = defaultCategory;
            }
            const QColor catColor = categoryColors.value(catKey);
            if (catColor.isValid()) {
                targetBg = catColor;
                hasCustomBg = true;
            }
        }

        if (hasCustomBg && targetBg.isValid()) {
            const QColor textCol = ResultTableConfigurator::idealTextColorForBackground(targetBg);
            opt.palette.setBrush(QPalette::Text, QBrush(textCol));
            opt.palette.setBrush(QPalette::WindowText, QBrush(textCol));
            opt.palette.setBrush(QPalette::HighlightedText, QBrush(textCol));
        }

        if (nativeUi) {
            if (hasCustomBg && targetBg.isValid() && !opt.state.testFlag(QStyle::State_Selected)) {
                painter->save();
                painter->fillRect(opt.rect, targetBg);
                painter->restore();
                opt.backgroundBrush = QBrush(Qt::NoBrush);
            }
            QStyledItemDelegate::paint(painter, opt, index);
            return;
        }

        if (hovered) {
            QColor baseHover = hasCustomBg
                                   ? targetBg
                                   : (alt ? opt.palette.color(QPalette::AlternateBase)
                                          : opt.palette.color(QPalette::Base));
            if (!baseHover.isValid() || baseHover.alpha() == 0) {
                baseHover = isLightTheme
                                ? (alt ? QColor(247, 247, 247) : QColor(255, 255, 255))
                                : (alt ? QColor(44, 44, 46) : QColor(34, 34, 36));
            }
            QColor hoverColor = isLightTheme ? baseHover.lighter(112) : baseHover.lighter(128);
            if (isLightTheme && hoverColor.lightness() >= 248) {
                const QColor highlightCol = opt.palette.highlight().color().isValid()
                                                ? opt.palette.highlight().color()
                                                : QColor(40, 120, 220);
                hoverColor = blendColors(hoverColor, highlightCol.lighter(195), 0.20);
            }
            painter->save();
            painter->fillRect(opt.rect, hoverColor);
            painter->restore();
        } else if (hasCustomBg && targetBg.isValid()) {
            painter->save();
            painter->fillRect(opt.rect, targetBg);
            painter->restore();
        }

        // Remove flag nativa de hover para evitar sobrecarga de desenho do estilo sobre nossa cor.
        opt.state &= ~QStyle::State_MouseOver;
        opt.backgroundBrush = QBrush(Qt::NoBrush);
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

/**
 * @brief Menu de contexto que permanece aberto ao alternar opções marcáveis (checkboxes).
 */
class PersistentTableMenu final : public QMenu {
public:
    using QMenu::QMenu;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        QAction* act = activeAction();
        if (act != nullptr && act->isEnabled() && act->isCheckable() && act->menu() == nullptr) {
            act->trigger();
            event->accept();
            return;
        }
        QMenu::mouseReleaseEvent(event);
    }
};

QPalette buildLinceuTablePalette(bool isLightTheme) {
    QPalette pal;
    auto setGroup = [](QPalette& p,
                       QPalette::ColorGroup g,
                       const QColor& window,
                       const QColor& base,
                       const QColor& altBase,
                       const QColor& text,
                       const QColor& button,
                       const QColor& buttonText,
                       const QColor& mid,
                       const QColor& dark,
                       const QColor& light,
                       const QColor& shadow,
                       const QColor& highlight,
                       const QColor& highlightedText) {
        p.setColor(g, QPalette::Window, window);
        p.setColor(g, QPalette::Base, base);
        p.setColor(g, QPalette::AlternateBase, altBase);
        p.setColor(g, QPalette::Text, text);
        p.setColor(g, QPalette::WindowText, text);
        p.setColor(g, QPalette::Button, button);
        p.setColor(g, QPalette::ButtonText, buttonText);
        p.setColor(g, QPalette::Mid, mid);
        p.setColor(g, QPalette::Dark, dark);
        p.setColor(g, QPalette::Light, light);
        p.setColor(g, QPalette::Shadow, shadow);
        p.setColor(g, QPalette::Highlight, highlight);
        p.setColor(g, QPalette::HighlightedText, highlightedText);
        p.setColor(g, QPalette::ToolTipBase, base);
        p.setColor(g, QPalette::ToolTipText, text);
    };

    if (!isLightTheme) {
        const QColor window(30, 30, 32);
        const QColor base(34, 34, 36);
        const QColor alt(44, 44, 46);
        const QColor text(235, 235, 235);
        const QColor button(48, 48, 52);
        const QColor buttonText(235, 235, 235);
        const QColor mid(96, 96, 102);
        const QColor dark(22, 22, 24);
        const QColor light(78, 78, 84);
        const QColor shadow(12, 12, 14);
        const QColor highlight(82, 133, 255);
        const QColor highlightedText(235, 235, 235);
        setGroup(pal, QPalette::Active, window, base, alt, text, button, buttonText, mid, dark, light, shadow, highlight, highlightedText);
        setGroup(pal, QPalette::Inactive, window, base, alt, text, button, buttonText, mid, dark, light, shadow, highlight, highlightedText);
        setGroup(pal, QPalette::Disabled, window, base, alt, QColor(145, 145, 150), button,
                 QColor(145, 145, 150), mid, dark, light, shadow, QColor(92, 92, 96), QColor(210, 210, 210));
    } else {
        const QColor window(250, 250, 250);
        const QColor base(255, 255, 255);
        const QColor alt(247, 247, 247);
        const QColor text(20, 20, 20);
        const QColor button(243, 243, 243);
        const QColor buttonText(20, 20, 20);
        const QColor mid(191, 195, 201);
        const QColor dark(159, 165, 173);
        const QColor light(255, 255, 255);
        const QColor shadow(115, 121, 129);
        const QColor highlight(40, 120, 220);
        const QColor highlightedText(20, 20, 20);
        setGroup(pal, QPalette::Active, window, base, alt, text, button, buttonText, mid, dark, light, shadow, highlight, highlightedText);
        setGroup(pal, QPalette::Inactive, window, base, alt, text, button, buttonText, mid, dark, light, shadow, highlight, highlightedText);
        setGroup(pal, QPalette::Disabled, window, base, alt, QColor(120, 120, 120), button,
                 QColor(120, 120, 120), mid, dark, light, shadow, QColor(145, 170, 210), QColor(120, 120, 120));
    }
    return pal;
}

QString buildLinceuTableStyleSheet() {
    return QStringLiteral(
        "QTableView::item:hover { background-color: palette(alternate-base); }"
        "QTableView { selection-background-color: transparent; selection-color: palette(text); gridline-color: palette(mid); }"
        "QTableView::item:selected { background-color: transparent; color: palette(text); border: 1px solid palette(highlight);"
        "border-radius: 3px; padding: 1px; }"
        "QTableCornerButton::section { background-color: palette(button); border: 1px solid palette(mid); }"
        "QTableCornerButton::section:pressed { background-color: palette(highlight); }"
        "QHeaderView, QHeaderView::section { font-weight: bold; }");
}

int tableRowCount(const QTableView* table) {
    return (table != nullptr && table->model() != nullptr) ? table->model()->rowCount() : 0;
}

int tableColumnCount(const QTableView* table) {
    return (table != nullptr && table->model() != nullptr) ? table->model()->columnCount() : 0;
}

QVariant cellData(const QTableView* table, int row, int col, int role) {
    if (table == nullptr || table->model() == nullptr) {
        return {};
    }
    const QAbstractItemModel* model = table->model();
    if (row < 0 || row >= model->rowCount() || col < 0 || col >= model->columnCount()) {
        return {};
    }
    return model->data(model->index(row, col), role);
}

QString cellText(const QTableView* table, int row, int col) {
    return cellData(table, row, col, Qt::DisplayRole).toString();
}

QString headerText(const QTableView* table, int col) {
    if (table == nullptr || table->model() == nullptr) {
        return {};
    }
    const QAbstractItemModel* model = table->model();
    if (col < 0 || col >= model->columnCount()) {
        return {};
    }
    return model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
}

ResultTableModel* resolveResultTableModel(QTableView* table) {
    if (table == nullptr || table->model() == nullptr) {
        return nullptr;
    }
    if (auto* direct = qobject_cast<ResultTableModel*>(table->model())) {
        return direct;
    }
    if (auto* proxy = qobject_cast<ResultTableProxyModel*>(table->model())) {
        return qobject_cast<ResultTableModel*>(proxy->sourceModel());
    }
    return nullptr;
}

void applyDefaultTextForeground(QTableView* table, bool isLightTheme) {
    if (table == nullptr) {
        return;
    }
    const bool nativeUi = ThemeManager::instance().isNativeUi();
    if (ResultTableModel* sharedModel = resolveResultTableModel(table)) {
        sharedModel->setThemeState(isLightTheme, nativeUi);
        return;
    }
    auto* tw = qobject_cast<QTableWidget*>(table);
    if (tw == nullptr) {
        return;
    }
    const QBrush fgBrush = nativeUi
                               ? QBrush()
                               : QBrush(isLightTheme ? QColor(20, 20, 20) : QColor(235, 235, 235));
    const QSignalBlocker blocker(tw);
    for (int r = 0; r < tw->rowCount(); ++r) {
        if (const QTableWidgetItem* first = tw->item(r, 0);
            first != nullptr && first->data(kResultTableSummaryRowRole).toBool()) {
            continue;
        }
        for (int c = 0; c < tw->columnCount(); ++c) {
            if (QTableWidgetItem* item = tw->item(r, c)) {
                item->setForeground(fgBrush);
            }
        }
    }
}

void applyWidgetPaletteAndRepolish(QWidget* w, const QPalette& pal, bool clearCustomPalette) {
    if (w == nullptr) {
        return;
    }
    if (QStyle* st = w->style()) {
        st->unpolish(w);
    }
    if (clearCustomPalette) {
        w->setPalette(QPalette());
        w->setAttribute(Qt::WA_SetPalette, false);
    } else {
        w->setPalette(pal);
    }
    if (QStyle* st = w->style()) {
        st->polish(w);
    }
    w->update();
}

QAbstractButton* findCornerButton(QTableView* table) {
    if (table == nullptr) {
        return nullptr;
    }
    return table->findChild<QAbstractButton*>(QString(), Qt::FindDirectChildrenOnly);
}

QString wrapHeaderWords(const QString& original, const QFontMetrics& fm, int columnWidth) {
    QString singleLine = original;
    singleLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
    singleLine = singleLine.simplified();
    if (singleLine.isEmpty()) {
        return singleLine;
    }

    const int avail = std::max(1, columnWidth - kHeaderWidthWrapMargin);
    if (fm.horizontalAdvance(singleLine) <= avail) {
        return singleLine;
    }

    const int avgChar = std::max(1, fm.averageCharWidth());
    const int maxChars = std::max(1, avail / avgChar);
    const QStringList words = singleLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return singleLine;
    }

    QStringList lines;
    QString current;
    for (const QString& word : words) {
        if (current.isEmpty()) {
            current = word;
        } else if (current.size() + 1 + word.size() <= maxChars &&
                   fm.horizontalAdvance(current + QLatin1Char(' ') + word) <= avail) {
            current += QLatin1Char(' ') + word;
        } else {
            lines.append(current);
            current = word;
        }
    }
    if (!current.isEmpty()) {
        lines.append(current);
    }
    return lines.join(QLatin1Char('\n'));
}

struct ParsedSortKey {
    enum class Kind {
        Empty = 0,
        Numeric = 1,
        Interval = 2,
        Text = 3
    };
    Kind kind = Kind::Text;
    double primary = 0.0;
    double secondary = 0.0;
    QString text;
};

ParsedSortKey parseCellSortKey(const QString& raw) {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return ParsedSortKey{ParsedSortKey::Kind::Empty, 0.0, 0.0, trimmed};
    }

    // Intervalo de esforços: "[N_min; N_max]" (com vírgula ou ponto decimal)
    static const QRegularExpression kIntervalRe(
        QStringLiteral("^\\[\\s*([+-]?\\d+(?:[.,]\\d+)?(?:[eE][+-]?\\d+)?)\\s*;\\s*"
                       "([+-]?\\d+(?:[.,]\\d+)?(?:[eE][+-]?\\d+)?)\\s*\\]$"));
    const QRegularExpressionMatch intervalMatch = kIntervalRe.match(trimmed);
    if (intervalMatch.hasMatch()) {
        bool okA = false;
        bool okB = false;
        const QString sA = intervalMatch.captured(1).replace(QLatin1Char(','), QLatin1Char('.'));
        const QString sB = intervalMatch.captured(2).replace(QLatin1Char(','), QLatin1Char('.'));
        const double a = QLocale::c().toDouble(sA, &okA);
        const double b = QLocale::c().toDouble(sB, &okB);
        if (okA && okB) {
            return ParsedSortKey{ParsedSortKey::Kind::Interval, a, b, trimmed};
        }
    }

    // Número puro ou número com unidade de engenharia (ex: "125.40 kN/m", "-30.12 kPa")
    static const QRegularExpression kNumericUnitRe(
        QStringLiteral("^([+-]?\\d+(?:[.,]\\d+)?(?:[eE][+-]?\\d+)?)(?:\\s+[A-Za-z%µμ²³°/\\-]+.*)?$"));
    const QRegularExpressionMatch numMatch = kNumericUnitRe.match(trimmed);
    if (numMatch.hasMatch()) {
        bool ok = false;
        const QString numStr = numMatch.captured(1).replace(QLatin1Char(','), QLatin1Char('.'));
        const double val = QLocale::c().toDouble(numStr, &ok);
        if (ok && std::isfinite(val)) {
            return ParsedSortKey{ParsedSortKey::Kind::Numeric, val, 0.0, trimmed};
        }
    }

    return ParsedSortKey{ParsedSortKey::Kind::Text, 0.0, 0.0, trimmed};
}

int compareParsedKeys(const ParsedSortKey& a, const ParsedSortKey& b, const QCollator& collator) {
    if (a.kind != b.kind) {
        return static_cast<int>(a.kind) < static_cast<int>(b.kind) ? -1 : 1;
    }
    switch (a.kind) {
    case ParsedSortKey::Kind::Empty:
        return 0;
    case ParsedSortKey::Kind::Numeric:
        if (a.primary < b.primary) return -1;
        if (a.primary > b.primary) return 1;
        return 0;
    case ParsedSortKey::Kind::Interval:
        if (a.primary < b.primary) return -1;
        if (a.primary > b.primary) return 1;
        if (a.secondary < b.secondary) return -1;
        if (a.secondary > b.secondary) return 1;
        return 0;
    case ParsedSortKey::Kind::Text:
        return collator.compare(a.text, b.text);
    }
    return 0;
}

QIcon createColorSwatchIcon(const QColor& color) {
    QPixmap pix(16, 16);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(color.isValid() ? color : Qt::gray);
    p.setPen(QPen(QColor(120, 120, 128), 1));
    p.drawRoundedRect(1, 1, 13, 13, 3, 3);
    return QIcon(pix);
}

bool isLegitimateNumericLiteral(const QString& s) {
    QString norm = s.trimmed();
    if (norm.isEmpty()) {
        return false;
    }
    norm.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double val = QLocale::c().toDouble(norm, &ok);
    return ok && std::isfinite(val);
}

QString stripIllegalXml10Chars(const QString& value) {
    QString cleaned;
    cleaned.reserve(value.size());
    for (const QChar ch : value) {
        const ushort u = ch.unicode();
        const bool validXml10 = (u == 0x09u) ||
                                (u == 0x0Au) ||
                                (u == 0x0Du) ||
                                (u >= 0x20u && u <= 0xD7FFu) ||
                                (u >= 0xE000u && u <= 0xFFFDu) ||
                                ch.isSurrogate();
        if (validXml10) {
            cleaned.append(ch);
        }
    }
    return cleaned;
}

QString sanitizeTabularCell(const QString& value) {
    const bool startsWithTab = !value.isEmpty() && value.front() == QLatin1Char('\t');
    QString out = stripIllegalXml10Chars(value);
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out.replace(QLatin1Char('\t'), QLatin1Char(' '));
    if (!out.isEmpty()) {
        const QChar c0 = out.front();
        const bool isFormulaTrigger = startsWithTab ||
                                      c0 == QLatin1Char('=') ||
                                      c0 == QLatin1Char('+') ||
                                      c0 == QLatin1Char('-') ||
                                      c0 == QLatin1Char('@');
        if (isFormulaTrigger && !isLegitimateNumericLiteral(out)) {
            out.prepend(QLatin1Char('\''));
        }
    }
    return out;
}

QString escapeCsvField(const QString& value) {
    QString out = sanitizeTabularCell(value);
    const bool needsQuotes = out.contains(QLatin1Char(',')) ||
                             out.contains(QLatin1Char('"')) ||
                             out.contains(QLatin1Char(';'));
    if (out.contains(QLatin1Char('"'))) {
        out.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    }
    return needsQuotes ? (QLatin1Char('"') + out + QLatin1Char('"')) : out;
}

QString escapeXmlText(const QString& value) {
    QString out = stripIllegalXml10Chars(value);
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    out.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
    return out;
}

QString excelColumnName(int zeroBasedCol) {
    QString name;
    int col = zeroBasedCol + 1;
    while (col > 0) {
        const int rem = (col - 1) % 26;
        name.prepend(QChar('A' + rem));
        col = (col - 1) / 26;
    }
    return name;
}

std::uint32_t crc32Bytes(const QByteArray& data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc ^= static_cast<std::uint32_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void appendLe16(QByteArray& out, std::uint16_t v) {
    out.append(static_cast<char>(v & 0xFFu));
    out.append(static_cast<char>((v >> 8) & 0xFFu));
}

void appendLe32(QByteArray& out, std::uint32_t v) {
    out.append(static_cast<char>(v & 0xFFu));
    out.append(static_cast<char>((v >> 8) & 0xFFu));
    out.append(static_cast<char>((v >> 16) & 0xFFu));
    out.append(static_cast<char>((v >> 24) & 0xFFu));
}

struct ZipEntryRecord {
    QByteArray utf8Name;
    QByteArray payload;
    std::uint32_t crc32 = 0;
    std::uint32_t localOffset = 0;
};

QByteArray buildStoreZipArchive(const std::vector<std::pair<QString, QByteArray>>& files) {
    if (files.size() > 0xFFFFu) {
        return {};
    }
    QByteArray archive;
    std::vector<ZipEntryRecord> records;
    records.reserve(files.size());

    for (const auto& [path, data] : files) {
        ZipEntryRecord rec;
        rec.utf8Name = path.toUtf8();
        rec.payload = data;
        if (rec.utf8Name.size() < 0 || rec.utf8Name.size() > 0xFFFF ||
            rec.payload.size() < 0 || static_cast<std::uint64_t>(rec.payload.size()) > 0xFFFFFFFFULL) {
            return {};
        }
        const std::uint64_t entryBytes = 30ULL +
                                         static_cast<std::uint64_t>(rec.utf8Name.size()) +
                                         static_cast<std::uint64_t>(rec.payload.size());
        if (static_cast<std::uint64_t>(archive.size()) + entryBytes > 0xFFFFFFFFULL) {
            return {};
        }
        rec.crc32 = crc32Bytes(data);
        rec.localOffset = static_cast<std::uint32_t>(archive.size());

        // Local file header (PK\x03\x04)
        appendLe32(archive, 0x04034B50u);
        appendLe16(archive, 20);      // version needed
        appendLe16(archive, 0x0800u); // UTF-8 filename flag
        appendLe16(archive, 0);       // compression = STORE
        appendLe16(archive, 0);       // mod time
        appendLe16(archive, 0x5421u); // mod date
        appendLe32(archive, rec.crc32);
        appendLe32(archive, static_cast<std::uint32_t>(rec.payload.size()));
        appendLe32(archive, static_cast<std::uint32_t>(rec.payload.size()));
        appendLe16(archive, static_cast<std::uint16_t>(rec.utf8Name.size()));
        appendLe16(archive, 0); // extra length
        archive.append(rec.utf8Name);
        archive.append(rec.payload);

        records.push_back(std::move(rec));
    }

    if (static_cast<std::uint64_t>(archive.size()) > 0xFFFFFFFFULL) {
        return {};
    }
    const std::uint32_t centralDirOffset = static_cast<std::uint32_t>(archive.size());
    for (const ZipEntryRecord& rec : records) {
        const std::uint64_t cdEntryBytes = 46ULL + static_cast<std::uint64_t>(rec.utf8Name.size());
        if (static_cast<std::uint64_t>(archive.size()) + cdEntryBytes > 0xFFFFFFFFULL) {
            return {};
        }
        appendLe32(archive, 0x02014B50u); // Central directory header
        appendLe16(archive, 20);
        appendLe16(archive, 20);
        appendLe16(archive, 0x0800u);
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe16(archive, 0x5421u);
        appendLe32(archive, rec.crc32);
        appendLe32(archive, static_cast<std::uint32_t>(rec.payload.size()));
        appendLe32(archive, static_cast<std::uint32_t>(rec.payload.size()));
        appendLe16(archive, static_cast<std::uint16_t>(rec.utf8Name.size()));
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe32(archive, 0);
        appendLe32(archive, rec.localOffset);
        archive.append(rec.utf8Name);
    }

    const std::uint32_t centralDirSize = static_cast<std::uint32_t>(archive.size()) - centralDirOffset;
    appendLe32(archive, 0x06054B50u); // End of Central Directory
    appendLe16(archive, 0);
    appendLe16(archive, 0);
    appendLe16(archive, static_cast<std::uint16_t>(records.size()));
    appendLe16(archive, static_cast<std::uint16_t>(records.size()));
    appendLe32(archive, centralDirSize);
    appendLe32(archive, centralDirOffset);
    appendLe16(archive, 0);
    return archive;
}

QByteArray buildXlsxPackage(const QStringList& headers,
                            const QList<QStringList>& matrixRows) {
    QString sheetXml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n"
        "<sheetData>\n");

    auto appendInlineCell = [&](const QString& ref, const QString& text, bool boldStyle) {
        sheetXml += QStringLiteral("<c r=\"%1\" t=\"inlineStr\"%2><is><t xml:space=\"preserve\">%3</t></is></c>")
                        .arg(ref, boldStyle ? QStringLiteral(" s=\"1\"") : QString(), escapeXmlText(text));
    };

    // Cabeçalhos na linha 1
    sheetXml += QStringLiteral("<row r=\"1\">");
    for (int c = 0; c < headers.size(); ++c) {
        appendInlineCell(excelColumnName(c) + QLatin1Char('1'), headers[c], true);
    }
    sheetXml += QStringLiteral("</row>\n");

    // Linhas de dados a partir da linha 2
    for (int r = 0; r < matrixRows.size(); ++r) {
        const int excelRow = r + 2;
        sheetXml += QStringLiteral("<row r=\"%1\">").arg(excelRow);
        const QStringList& rowVals = matrixRows[r];
        for (int c = 0; c < rowVals.size(); ++c) {
            const QString ref = excelColumnName(c) + QString::number(excelRow);
            const QString trimmed = rowVals[c].trimmed();
            bool okNum = false;
            const double numVal = QLocale::c().toDouble(trimmed, &okNum);
            if (okNum && !trimmed.isEmpty() && std::isfinite(numVal)) {
                sheetXml += QStringLiteral("<c r=\"%1\"><v>%2</v></c>")
                                .arg(ref, QString::number(numVal, 'g', 15));
            } else {
                appendInlineCell(ref, rowVals[c], false);
            }
        }
        sheetXml += QStringLiteral("</row>\n");
    }
    sheetXml += QStringLiteral("</sheetData>\n</worksheet>\n");

    const QByteArray contentTypes = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>\n"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>\n"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>\n"
        "</Types>\n");

    const QByteArray rootRels = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>\n"
        "</Relationships>\n");

    const QByteArray workbookXml = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n"
        "<sheets><sheet name=\"Resultados\" sheetId=\"1\" r:id=\"rId1\"/></sheets>\n"
        "</workbook>\n");

    const QByteArray workbookRels = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>\n"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>\n"
        "</Relationships>\n");

    const QByteArray stylesXml = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n"
        "<fonts count=\"2\">\n"
        "<font><sz val=\"11\"/><name val=\"Calibri\"/></font>\n"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font>\n"
        "</fonts>\n"
        "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>\n"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>\n"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>\n"
        "<cellXfs count=\"2\">\n"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>\n"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>\n"
        "</cellXfs>\n"
        "</styleSheet>\n");

    return buildStoreZipArchive({
        {QStringLiteral("[Content_Types].xml"), contentTypes},
        {QStringLiteral("_rels/.rels"), rootRels},
        {QStringLiteral("xl/workbook.xml"), workbookXml},
        {QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRels},
        {QStringLiteral("xl/styles.xml"), stylesXml},
        {QStringLiteral("xl/worksheets/sheet1.xml"), sheetXml.toUtf8()}
    });
}

} // namespace

ResultTableConfigurator::ResultTableConfigurator(WindowZOrderManager* windowManager, QObject* parent)
    : QObject(parent),
      m_windowManager(windowManager) {}

ResultTableConfigurator::TableState* ResultTableConfigurator::findState(QTableView* table) {
    for (TableState& entry : m_tables) {
        if (entry.table == table) {
            return &entry;
        }
    }
    return nullptr;
}

const ResultTableConfigurator::TableState* ResultTableConfigurator::findState(const QTableView* table) const {
    for (const TableState& entry : m_tables) {
        if (entry.table == table) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<ResultTableConfigurator::CategorySpec> ResultTableConfigurator::categoriesForKind(ResultTableKind kind) {
    switch (kind) {
    case ResultTableKind::Displacements:
        return {
            {QStringLiteral("node"), tr("Nós Estruturais"), QColor(34, 82, 136), QColor(202, 226, 252)},
            {QStringLiteral("mesh_node"), tr("Nós de Malha"), QColor(42, 102, 92), QColor(200, 238, 228)}
        };
    case ResultTableKind::Reactions:
        return {
            {QStringLiteral("support"), tr("Apoios Estruturais"), QColor(42, 96, 128), QColor(206, 232, 250)},
            {QStringLiteral("mesh_support"), tr("Apoios de Malha"), QColor(62, 104, 82), QColor(210, 240, 224)},
            {QStringLiteral("reaction_sum"), tr("Soma de Reações"), QColor(40, 74, 112), QColor(204, 222, 248)}
        };
    case ResultTableKind::BarForces:
        return {
            {QStringLiteral("bar"), tr("Barras"), QColor(98, 72, 34), QColor(250, 232, 198)}
        };
    case ResultTableKind::ContinuumStress:
        return {
            {QStringLiteral("shell"), tr("Cascas 2D"), QColor(56, 84, 128), QColor(210, 226, 252)},
            {QStringLiteral("solid"), tr("Sólidos 3D"), QColor(102, 68, 122), QColor(232, 214, 248)}
        };
    }
    return {};
}

QString ResultTableConfigurator::defaultCategoryForKind(ResultTableKind kind) {
    switch (kind) {
    case ResultTableKind::Displacements:
        return QStringLiteral("node");
    case ResultTableKind::Reactions:
        return QStringLiteral("support");
    case ResultTableKind::BarForces:
        return QStringLiteral("bar");
    case ResultTableKind::ContinuumStress:
        return QStringLiteral("shell");
    }
    return QStringLiteral("node");
}

QHash<QString, QColor> ResultTableConfigurator::buildDefaultCategoryColors(ResultTableKind kind, bool isLightTheme) {
    QHash<QString, QColor> result;
    for (const CategorySpec& spec : categoriesForKind(kind)) {
        result.insert(spec.key, isLightTheme ? spec.lightDefault : spec.darkDefault);
    }
    return result;
}

QColor ResultTableConfigurator::idealTextColorForBackground(const QColor& background) {
    if (!background.isValid()) {
        return QColor(Qt::white);
    }
    auto linearize = [](double channel) {
        return (channel <= 0.03928) ? (channel / 12.92) : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    const double r = linearize(background.redF());
    const double g = linearize(background.greenF());
    const double b = linearize(background.blueF());
    const double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return luminance > 0.179 ? QColor(Qt::black) : QColor(Qt::white);
}

void ResultTableConfigurator::configureTable(QTableView* table,
                                             ResultTableKind kind,
                                             const QStringList& headers,
                                             bool isLightTheme) {
    if (table == nullptr) {
        return;
    }

    m_isLightTheme = isLightTheme;

    TableState* state = findState(table);
    const bool isNew = (state == nullptr);
    if (isNew) {
        TableState fresh;
        fresh.table = table;
        fresh.kind = kind;
        fresh.originalHeaders = headers;
        fresh.categoryColors = buildDefaultCategoryColors(kind, isLightTheme);
        m_tables.push_back(std::move(fresh));
        state = &m_tables.back();
    } else {
        state->kind = kind;
        state->originalHeaders = headers;
    }

    applyStructuralConfig(table, headers);
    if (isNew) {
        auto* delegate = new DelegateCorEstadoVisual(table);
        table->setItemDelegate(delegate);
    }
    applyVisualThemeToTable(table, isLightTheme);
    syncColoredDelegateState(*state);
    applyIdealColumnWidths(*state, isNew);
    updateHeaderWordWrap(table);

    if (isNew) {
        connect(table, &QObject::destroyed, this, [this](QObject* obj) {
            std::erase_if(m_tables, [obj](const TableState& st) {
                return st.table.isNull() || static_cast<const QObject*>(st.table.data()) == obj;
            });
        });
        table->installEventFilter(this);
        if (table->viewport() != nullptr) {
            table->viewport()->installEventFilter(this);
        }
        QPointer<QTableView> tablePtr(table);
        if (QHeaderView* hh = table->horizontalHeader()) {
            hh->installEventFilter(this);
            connect(hh, &QHeaderView::sectionResized, this, [this, tablePtr](int, int, int) {
                if (!tablePtr) return;
                updateHeaderWordWrap(tablePtr.data());
                if (TableState* st = findState(tablePtr.data()); st != nullptr && !st->suspendHeaderWrap && !m_applyingSavedState) {
                    emit configurationChanged(tablePtr.data());
                }
            });
            connect(hh, &QHeaderView::sectionMoved, this, [this, tablePtr](int, int, int) {
                if (!tablePtr) return;
                if (TableState* st = findState(tablePtr.data()); st != nullptr && !st->suspendHeaderWrap && !m_applyingSavedState) {
                    emit configurationChanged(tablePtr.data());
                }
            });
            connect(hh, &QHeaderView::sectionClicked, this, [this, tablePtr](int logicalColumn) {
                if (!tablePtr) return;
                if (TableState* st = findState(tablePtr.data())) {
                    handleHeaderSectionClicked(*st, logicalColumn);
                }
            });
            connect(hh, &QWidget::customContextMenuRequested, this, [this, tablePtr](const QPoint& pos) {
                if (!tablePtr || !tablePtr->horizontalHeader() || !tablePtr->horizontalHeader()->viewport()) return;
                if (TableState* st = findState(tablePtr.data())) {
                    showContextMenuAt(*st, tablePtr->horizontalHeader()->viewport()->mapToGlobal(pos), true);
                }
            });
        }

        if (QItemSelectionModel* sm = table->selectionModel()) {
            connect(sm,
                    &QItemSelectionModel::selectionChanged,
                    this,
                    [tablePtr](const QItemSelection&, const QItemSelection&) {
                        if (!tablePtr) return;
                        if (tablePtr->viewport() != nullptr) {
                            tablePtr->viewport()->update();
                        }
                        if (QHeaderView* hh = tablePtr->horizontalHeader(); hh != nullptr && hh->viewport() != nullptr) {
                            hh->viewport()->update();
                        }
                        if (QHeaderView* vh = tablePtr->verticalHeader(); vh != nullptr && vh->viewport() != nullptr) {
                            vh->viewport()->update();
                        }
                    });
        }
        if (auto* proxy = qobject_cast<ResultTableProxyModel*>(table->model())) {
            connect(proxy, &QAbstractItemModel::modelReset, this, [this, tablePtr]() {
                if (!tablePtr) return;
                if (TableState* st = findState(tablePtr.data())) {
                    st->appliedRowOrderMode = TableRowOrderMode::AppendBottom;
                }
            });
        }
        connect(table, &QAbstractItemView::doubleClicked, this, [this, tablePtr](const QModelIndex& idx) {
            if (!tablePtr) return;
            if (TableState* st = findState(tablePtr.data())) {
                handleCellDoubleClicked(*st, idx.row(), idx.column());
            }
        });
        connect(table, &QWidget::customContextMenuRequested, this, [this, tablePtr](const QPoint& pos) {
            if (!tablePtr) return;
            if (TableState* st = findState(tablePtr.data())) {
                const QPoint globalPos = tablePtr->viewport()
                                             ? tablePtr->viewport()->mapToGlobal(pos)
                                             : tablePtr->mapToGlobal(pos);
                showContextMenuAt(*st, globalPos, false);
            }
        });

        if (QAbstractButton* cornerBtn = findCornerButton(table)) {
            if (!cornerBtn->property("_omni_corner_connected").toBool()) {
                QObject::disconnect(cornerBtn, &QAbstractButton::clicked, nullptr, nullptr);
                QObject::disconnect(cornerBtn, &QAbstractButton::pressed, nullptr, nullptr);
                connect(cornerBtn, &QAbstractButton::clicked, this, [this, tablePtr]() {
                    if (tablePtr) selectCornerAll(tablePtr.data());
                });
                cornerBtn->setProperty("_omni_corner_connected", true);
            }
        }
    }

    if (!table->objectName().isEmpty() && m_savedStatesByTableName.contains(table->objectName())) {
        loadTableStateFromJson(table, m_savedStatesByTableName.value(table->objectName()));
    }
}

void ResultTableConfigurator::applyStructuralConfig(QTableView* table, const QStringList& headers) {
    if (auto* tw = qobject_cast<QTableWidget*>(table)) {
        tw->setColumnCount(static_cast<int>(headers.size()));
        tw->setHorizontalHeaderLabels(headers);
    } else if (ResultTableModel* sharedModel = resolveResultTableModel(table)) {
        sharedModel->setHeaders(headers);
    }
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(false); // Ordenação gerenciada pelo manipulador inteligente no cabeçalho.
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    if (!table->isCornerButtonEnabled()) {
        table->setCornerButtonEnabled(true);
    }
    table->setMouseTracking(true);
    if (table->viewport() != nullptr) {
        table->viewport()->setMouseTracking(true);
    }
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    if (QHeaderView* vh = table->verticalHeader()) {
        vh->setVisible(true);
        vh->setDefaultAlignment(Qt::AlignCenter);
        vh->setSectionResizeMode(QHeaderView::Fixed);
        vh->setSectionsClickable(true);
        vh->setHighlightSections(true);
    }

    if (QHeaderView* hh = table->horizontalHeader()) {
        hh->setSectionsMovable(true);
        hh->setSectionsClickable(true);
        hh->setHighlightSections(true);
        hh->setDefaultAlignment(Qt::AlignCenter);
        hh->setTextElideMode(Qt::ElideNone);
        hh->setStretchLastSection(false);
        hh->setMinimumSectionSize(kMinColumnWidth);
        hh->setContextMenuPolicy(Qt::CustomContextMenu);

        QFont boldFont = hh->font();
        boldFont.setBold(true);
        hh->setFont(boldFont);

        const int colCount = tableColumnCount(table);
        auto* tw = qobject_cast<QTableWidget*>(table);
        auto* proxy = qobject_cast<ResultTableProxyModel*>(table->model());
        for (int c = 0; c < colCount; ++c) {
            hh->setSectionResizeMode(c, QHeaderView::Interactive);
            const QString hdr = (c < headers.size()) ? headers[c] : QString();
            if (tw != nullptr) {
                QTableWidgetItem* hItem = tw->horizontalHeaderItem(c);
                if (hItem == nullptr) {
                    hItem = new QTableWidgetItem(hdr);
                    tw->setHorizontalHeaderItem(c, hItem);
                }
                hItem->setTextAlignment(Qt::AlignCenter);
                if (c < headers.size()) {
                    hItem->setToolTip(hdr);
                }
            } else if (proxy != nullptr && c < headers.size()) {
                QString singleLine = hdr;
                singleLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
                proxy->setWrappedHeader(c, hdr, singleLine.simplified());
            }
        }
    }
}

void ResultTableConfigurator::applyVisualThemeToTable(QTableView* table, bool isLightTheme) {
    if (table == nullptr) {
        return;
    }
    const bool nativeUi = ThemeManager::instance().isNativeUi();
    if (!table->styleSheet().isEmpty()) {
        table->setStyleSheet(QString());
    }
    const QPalette pal = nativeUi ? QPalette() : buildLinceuTablePalette(isLightTheme);
    applyWidgetPaletteAndRepolish(table, pal, nativeUi);
    applyWidgetPaletteAndRepolish(table->viewport(), pal, nativeUi);
    if (QHeaderView* hh = table->horizontalHeader()) {
        applyWidgetPaletteAndRepolish(hh, pal, nativeUi);
        applyWidgetPaletteAndRepolish(hh->viewport(), pal, nativeUi);
    }
    if (QHeaderView* vh = table->verticalHeader()) {
        applyWidgetPaletteAndRepolish(vh, pal, nativeUi);
        applyWidgetPaletteAndRepolish(vh->viewport(), pal, nativeUi);
    }
    applyWidgetPaletteAndRepolish(table->horizontalScrollBar(), pal, nativeUi);
    applyWidgetPaletteAndRepolish(table->verticalScrollBar(), pal, nativeUi);
    applyWidgetPaletteAndRepolish(findCornerButton(table), pal, nativeUi);

    if (!nativeUi) {
        table->setStyleSheet(buildLinceuTableStyleSheet());
    }
    applyDefaultTextForeground(table, isLightTheme);

    if (QHeaderView* hh = table->horizontalHeader()) {
        QFont boldFont = hh->font();
        boldFont.setBold(true);
        hh->setFont(boldFont);
        if (auto* tw = qobject_cast<QTableWidget*>(table)) {
            for (int c = 0; c < tw->columnCount(); ++c) {
                if (QTableWidgetItem* hItem = tw->horizontalHeaderItem(c)) {
                    QFont itemFont = hItem->font();
                    itemFont.setBold(true);
                    hItem->setFont(itemFont);
                }
            }
        }
    }
    if (table->viewport() != nullptr) {
        table->viewport()->update();
    }
}

void ResultTableConfigurator::applyIdealColumnWidths(TableState& state, bool forceResetWidths) {
    QTableView* table = state.table;
    if (table == nullptr || table->horizontalHeader() == nullptr) {
        return;
    }
    QHeaderView* hh = table->horizontalHeader();
    const QFontMetrics fm(hh->font());

    state.suspendHeaderWrap = true;
    const int colCount = tableColumnCount(table);
    auto* tw = qobject_cast<QTableWidget*>(table);
    for (int c = 0; c < colCount; ++c) {
        QString orig = (c < state.originalHeaders.size()) ? state.originalHeaders[c] : QString();
        orig.replace(QLatin1Char('\n'), QLatin1Char(' '));
        orig = orig.simplified();

        if (tw != nullptr) {
            if (QTableWidgetItem* hItem = tw->horizontalHeaderItem(c)) {
                hItem->setToolTip(orig);
                hItem->setTextAlignment(Qt::AlignCenter);
            }
        }

        if (forceResetWidths) {
            const int idealWidth = std::max(kMinColumnWidth, fm.horizontalAdvance(orig) + kHeaderPadding);
            table->setColumnWidth(c, idealWidth);
        }
    }
    state.suspendHeaderWrap = false;
    state.hasInitialFit = true;
}

void ResultTableConfigurator::syncColoredDelegateState(const TableState& state) {
    if (state.table == nullptr) {
        return;
    }
    if (auto* delegate = dynamic_cast<DelegateCorEstadoVisual*>(state.table->itemDelegate())) {
        delegate->isLightTheme = m_isLightTheme;
        delegate->coresVisiveis = state.showColumnColors;
        delegate->coloredColumns = state.coloredColumns;
        delegate->categoryColors = state.categoryColors;
        delegate->defaultCategory = defaultCategoryForKind(state.kind);
    }
    if (state.table->viewport() != nullptr) {
        state.table->viewport()->update();
    }
}

void ResultTableConfigurator::updateHeaders(QTableView* table, const QStringList& headers) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return;
    }

    const int oldColCount = tableColumnCount(table);
    QList<bool> hiddenState;
    hiddenState.reserve(oldColCount);
    for (int c = 0; c < oldColCount; ++c) {
        hiddenState.append(table->isColumnHidden(c));
    }

    state->originalHeaders = headers;
    state->suspendHeaderWrap = true;
    const int headerCount = static_cast<int>(headers.size());
    if (auto* tw = qobject_cast<QTableWidget*>(table)) {
        if (tw->columnCount() != headerCount) {
            tw->setColumnCount(headerCount);
        }
        for (int c = 0; c < headerCount; ++c) {
            QTableWidgetItem* hItem = tw->horizontalHeaderItem(c);
            if (hItem == nullptr) {
                hItem = new QTableWidgetItem();
                tw->setHorizontalHeaderItem(c, hItem);
            }
            hItem->setText(headers[c]);
            hItem->setToolTip(headers[c]);
            hItem->setTextAlignment(Qt::AlignCenter);
            if (c < hiddenState.size()) {
                tw->setColumnHidden(c, hiddenState[c]);
            }
        }
    } else {
        if (ResultTableModel* sharedModel = resolveResultTableModel(table)) {
            sharedModel->setHeaders(headers);
        }
        const int newColCount = tableColumnCount(table);
        for (int c = 0; c < newColCount && c < hiddenState.size(); ++c) {
            table->setColumnHidden(c, hiddenState[c]);
        }
    }
    state->suspendHeaderWrap = false;
    updateHeaderWordWrap(table);

    if (QHeaderView* hh = table->horizontalHeader()) {
        if (state->sortingEnabled && state->sortColumn >= 0 && state->sortColumn < tableColumnCount(table)) {
            hh->setSortIndicatorShown(true);
            hh->setSortIndicator(state->sortColumn, state->sortOrder);
        } else {
            hh->setSortIndicatorShown(false);
        }
    }
}

void ResultTableConfigurator::afterTablePopulated(QTableView* table) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return;
    }

    const bool prevApplying = m_applyingSavedState;
    m_applyingSavedState = true;

    if (qobject_cast<QTableWidget*>(table) != nullptr) {
        state->appliedRowOrderMode = TableRowOrderMode::AppendBottom;
    }
    if (state->appliedRowOrderMode != state->rowOrderMode && tableRowCount(table) > 1) {
        reverseTableRowsInPlace(table, state->kind == ResultTableKind::Reactions);
        state->appliedRowOrderMode = state->rowOrderMode;
    }

    if (state->sortingEnabled && state->sortColumn >= 0 && state->sortColumn < tableColumnCount(table)) {
        sortTableByColumn(table, state->sortColumn, state->sortOrder);
    }

    if (!state->hasInitialFit) {
        applyIdealColumnWidths(*state, true);
    }
    applyDefaultTextForeground(table, m_isLightTheme);
    updateHeaderWordWrap(table);
    syncColoredDelegateState(*state);

    m_applyingSavedState = prevApplying;
}

void ResultTableConfigurator::applyTheme(bool isLightTheme) {
    const bool previousTheme = m_isLightTheme;
    m_isLightTheme = isLightTheme;

    for (TableState& state : m_tables) {
        if (state.table == nullptr) {
            continue;
        }
        const QHash<QString, QColor> oldDefaults = buildDefaultCategoryColors(state.kind, previousTheme);
        const QHash<QString, QColor> newDefaults = buildDefaultCategoryColors(state.kind, isLightTheme);
        for (auto it = newDefaults.constBegin(); it != newDefaults.constEnd(); ++it) {
            const QString& key = it.key();
            if (!state.categoryColors.contains(key) ||
                state.categoryColors.value(key) == oldDefaults.value(key)) {
                state.categoryColors.insert(key, it.value());
            }
        }
        applyVisualThemeToTable(state.table, isLightTheme);
        syncColoredDelegateState(state);
    }
}

void ResultTableConfigurator::setSortingModeEnabled(QTableView* table, bool enabled) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return;
    }
    state->sortingEnabled = enabled;
    table->setSortingEnabled(false); // Sempre gerenciado por clique para manter multisseleção + resumo fixo.
    if (QHeaderView* hh = table->horizontalHeader()) {
        if (enabled && state->sortColumn >= 0 && state->sortColumn < tableColumnCount(table)) {
            hh->setSortIndicatorShown(true);
            hh->setSortIndicator(state->sortColumn, state->sortOrder);
        } else {
            hh->setSortIndicatorShown(false);
        }
    }
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

bool ResultTableConfigurator::isSortingModeEnabled(const QTableView* table) const {
    const TableState* state = findState(table);
    return state != nullptr && state->sortingEnabled;
}

bool ResultTableConfigurator::isSummaryRow(const QTableView* table, int row) const {
    if (table == nullptr || row < 0 || row >= tableRowCount(table)) {
        return false;
    }
    return cellData(table, row, 0, kResultTableSummaryRowRole).toBool();
}

void ResultTableConfigurator::sortTableByColumn(QTableView* table, int column, Qt::SortOrder order) {
    TableState* state = findState(table);
    const int colCount = tableColumnCount(table);
    if (table == nullptr || column < 0 || column >= colCount) {
        return;
    }

    const int rowCount = tableRowCount(table);
    if (rowCount <= 1 || colCount <= 0) {
        if (state != nullptr) {
            state->sortColumn = column;
            state->sortOrder = order;
            if (!m_applyingSavedState) {
                emit configurationChanged(table);
            }
        }
        return;
    }

    if (auto* proxy = qobject_cast<ResultTableProxyModel*>(table->model())) {
        struct ProxySortBucket {
            int originalProxyRow = 0;
            int sourceRow = 0;
            bool isSummary = false;
            ParsedSortKey key;
        };

        std::vector<ProxySortBucket> buckets;
        buckets.reserve(static_cast<std::size_t>(rowCount));
        for (int r = 0; r < rowCount; ++r) {
            ProxySortBucket bucket;
            bucket.originalProxyRow = r;
            bucket.sourceRow = proxy->sourceRowAt(r);
            bucket.isSummary = isSummaryRow(table, r);
            bucket.key = parseCellSortKey(cellText(table, r, column));
            buckets.push_back(std::move(bucket));
        }

        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);

        std::stable_sort(buckets.begin(), buckets.end(), [&](const ProxySortBucket& lhs, const ProxySortBucket& rhs) {
            if (lhs.isSummary != rhs.isSummary) {
                return !lhs.isSummary;
            }
            if (lhs.isSummary && rhs.isSummary) {
                return lhs.originalProxyRow < rhs.originalProxyRow;
            }
            const int cmp = compareParsedKeys(lhs.key, rhs.key, collator);
            if (cmp == 0) {
                return lhs.originalProxyRow < rhs.originalProxyRow;
            }
            return (order == Qt::AscendingOrder) ? (cmp < 0) : (cmp > 0);
        });

        std::vector<int> newOrder;
        newOrder.reserve(buckets.size());
        for (const ProxySortBucket& b : buckets) {
            newOrder.push_back(b.sourceRow);
        }
        proxy->setRowOrder(std::move(newOrder));
    } else if (auto* tw = qobject_cast<QTableWidget*>(table)) {
        struct RowBucket {
            int originalRow = 0;
            bool isSummary = false;
            ParsedSortKey key;
            std::vector<QTableWidgetItem*> items;
        };

        std::vector<RowBucket> buckets;
        buckets.reserve(static_cast<std::size_t>(rowCount));

        const QSignalBlocker blocker(tw);
        for (int r = 0; r < rowCount; ++r) {
            RowBucket bucket;
            bucket.originalRow = r;
            bucket.isSummary = isSummaryRow(tw, r);
            const QTableWidgetItem* keyItem = tw->item(r, column);
            bucket.key = parseCellSortKey(keyItem != nullptr ? keyItem->text() : QString());
            bucket.items.reserve(static_cast<std::size_t>(colCount));
            for (int c = 0; c < colCount; ++c) {
                bucket.items.push_back(tw->takeItem(r, c));
            }
            buckets.push_back(std::move(bucket));
        }

        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);

        std::stable_sort(buckets.begin(), buckets.end(), [&](const RowBucket& lhs, const RowBucket& rhs) {
            if (lhs.isSummary != rhs.isSummary) {
                return !lhs.isSummary; // Linha de resumo permanece sempre ao final.
            }
            if (lhs.isSummary && rhs.isSummary) {
                return lhs.originalRow < rhs.originalRow;
            }
            const int cmp = compareParsedKeys(lhs.key, rhs.key, collator);
            if (cmp == 0) {
                return lhs.originalRow < rhs.originalRow;
            }
            return (order == Qt::AscendingOrder) ? (cmp < 0) : (cmp > 0);
        });

        for (int r = 0; r < rowCount; ++r) {
            RowBucket& bucket = buckets[static_cast<std::size_t>(r)];
            for (int c = 0; c < colCount; ++c) {
                if (bucket.items[static_cast<std::size_t>(c)] != nullptr) {
                    tw->setItem(r, c, bucket.items[static_cast<std::size_t>(c)]);
                }
            }
        }
    }

    if (state != nullptr) {
        state->sortColumn = column;
        state->sortOrder = order;
        if (QHeaderView* hh = table->horizontalHeader()) {
            hh->setSortIndicatorShown(true);
            hh->setSortIndicator(column, order);
        }
        syncColoredDelegateState(*state);
        if (!m_applyingSavedState) {
            emit configurationChanged(table);
        }
    }
}

void ResultTableConfigurator::reverseTableRowsInPlace(QTableView* table, bool keepSummaryPinnedLast) {
    if (table == nullptr) {
        return;
    }
    if (auto* proxy = qobject_cast<ResultTableProxyModel*>(table->model())) {
        proxy->reverseRows(keepSummaryPinnedLast);
        return;
    }
    auto* tw = qobject_cast<QTableWidget*>(table);
    if (tw == nullptr) {
        return;
    }
    const int rowCount = tw->rowCount();
    const int colCount = tw->columnCount();
    if (rowCount <= 1 || colCount <= 0) {
        return;
    }

    int sortableRows = rowCount;
    if (keepSummaryPinnedLast && isSummaryRow(tw, rowCount - 1)) {
        sortableRows = rowCount - 1;
    }
    if (sortableRows <= 1) {
        return;
    }

    const QSignalBlocker blocker(tw);
    std::vector<std::vector<QTableWidgetItem*>> taken(static_cast<std::size_t>(sortableRows));
    for (int r = 0; r < sortableRows; ++r) {
        taken[static_cast<std::size_t>(r)].reserve(static_cast<std::size_t>(colCount));
        for (int c = 0; c < colCount; ++c) {
            taken[static_cast<std::size_t>(r)].push_back(tw->takeItem(r, c));
        }
    }

    for (int r = 0; r < sortableRows; ++r) {
        const int srcRow = sortableRows - 1 - r;
        auto& rowItems = taken[static_cast<std::size_t>(srcRow)];
        for (int c = 0; c < colCount; ++c) {
            if (rowItems[static_cast<std::size_t>(c)] != nullptr) {
                tw->setItem(r, c, rowItems[static_cast<std::size_t>(c)]);
            }
        }
    }
}

void ResultTableConfigurator::setRowOrderMode(QTableView* table, TableRowOrderMode mode) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return;
    }
    state->rowOrderMode = mode;
    if (state->appliedRowOrderMode != mode && tableRowCount(table) > 1) {
        reverseTableRowsInPlace(table, state->kind == ResultTableKind::Reactions);
        state->appliedRowOrderMode = mode;
    }
    syncColoredDelegateState(*state);
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

TableRowOrderMode ResultTableConfigurator::rowOrderMode(const QTableView* table) const {
    const TableState* state = findState(table);
    return (state != nullptr) ? state->rowOrderMode : TableRowOrderMode::AppendBottom;
}

void ResultTableConfigurator::invertTableRows(QTableView* table) {
    TableState* state = findState(table);
    if (table == nullptr) {
        return;
    }
    const int rows = tableRowCount(table);
    const bool keepSummary = (state != nullptr && state->kind == ResultTableKind::Reactions) ||
                             isSummaryRow(table, rows - 1);
    reverseTableRowsInPlace(table, keepSummary);
    if (state != nullptr) {
        state->appliedRowOrderMode = (state->appliedRowOrderMode == TableRowOrderMode::AppendBottom)
                                         ? TableRowOrderMode::PrependTop
                                         : TableRowOrderMode::AppendBottom;
        syncColoredDelegateState(*state);
        if (!m_applyingSavedState) {
            emit configurationChanged(table);
        }
    }
}

void ResultTableConfigurator::setColumnVisible(QTableView* table, int column, bool visible) {
    if (table == nullptr || column < 0 || column >= tableColumnCount(table)) {
        return;
    }
    table->setColumnHidden(column, !visible);
    updateHeaderWordWrap(table);
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

void ResultTableConfigurator::showAllColumns(QTableView* table) {
    if (table == nullptr) {
        return;
    }
    const int colCount = tableColumnCount(table);
    for (int c = 0; c < colCount; ++c) {
        table->setColumnHidden(c, false);
    }
    updateHeaderWordWrap(table);
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

void ResultTableConfigurator::resetColumns(QTableView* table) {
    TableState* state = findState(table);
    if (table == nullptr || state == nullptr) {
        return;
    }
    const bool prevApplying = m_applyingSavedState;
    m_applyingSavedState = true;
    const int colCount = tableColumnCount(table);
    for (int c = 0; c < colCount; ++c) {
        table->setColumnHidden(c, false);
        if (QHeaderView* hh = table->horizontalHeader()) {
            const int visual = hh->visualIndex(c);
            if (visual >= 0 && visual != c) {
                hh->moveSection(visual, c);
            }
        }
    }
    applyIdealColumnWidths(*state, true);
    updateHeaderWordWrap(table);
    m_applyingSavedState = prevApplying;
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

void ResultTableConfigurator::setShowColumnColors(QTableView* table, bool enabled) {
    TableState* state = findState(table);
    if (state == nullptr) {
        return;
    }
    state->showColumnColors = enabled;
    syncColoredDelegateState(*state);
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

bool ResultTableConfigurator::showColumnColors(const QTableView* table) const {
    const TableState* state = findState(table);
    return state == nullptr || state->showColumnColors;
}

void ResultTableConfigurator::setColumnColored(QTableView* table, int column, bool colored) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr || column < 0 || column >= tableColumnCount(table)) {
        return;
    }
    if (colored) {
        if (!state->coloredColumns.contains(column)) {
            state->coloredColumns.append(column);
        }
    } else {
        state->coloredColumns.removeAll(column);
    }
    syncColoredDelegateState(*state);
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

bool ResultTableConfigurator::isColumnColored(const QTableView* table, int column) const {
    const TableState* state = findState(table);
    return state != nullptr && state->coloredColumns.contains(column);
}

void ResultTableConfigurator::setAllColumnsColored(QTableView* table, bool colored) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return;
    }
    state->coloredColumns.clear();
    if (colored) {
        const int colCount = tableColumnCount(table);
        for (int c = 0; c < colCount; ++c) {
            state->coloredColumns.append(c);
        }
    }
    syncColoredDelegateState(*state);
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

void ResultTableConfigurator::resetColumnColors(QTableView* table) {
    setAllColumnsColored(table, false);
}

void ResultTableConfigurator::setCategoryColor(QTableView* table, const QString& categoryKey, const QColor& color) {
    TableState* state = findState(table);
    if (state == nullptr || !color.isValid()) {
        return;
    }
    state->categoryColors.insert(categoryKey, color);
    syncColoredDelegateState(*state);
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

QColor ResultTableConfigurator::categoryColor(const QTableView* table, const QString& categoryKey) const {
    const TableState* state = findState(table);
    return (state != nullptr) ? state->categoryColors.value(categoryKey) : QColor();
}

void ResultTableConfigurator::resetCategoryColors(QTableView* table) {
    TableState* state = findState(table);
    if (state == nullptr) {
        return;
    }
    state->categoryColors = buildDefaultCategoryColors(state->kind, m_isLightTheme);
    syncColoredDelegateState(*state);
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

void ResultTableConfigurator::setExportOnlyActiveColumns(QTableView* table, bool activeOnly) {
    if (TableState* state = findState(table)) {
        state->exportActiveColumnsOnly = activeOnly;
        if (!m_applyingSavedState) {
            emit configurationChanged(table);
        }
    }
}

bool ResultTableConfigurator::exportOnlyActiveColumns(const QTableView* table) const {
    const TableState* state = findState(table);
    return state != nullptr && state->exportActiveColumnsOnly;
}

void ResultTableConfigurator::setExportOnlySelectedCells(QTableView* table, bool selectedOnly) {
    if (TableState* state = findState(table)) {
        state->exportSelectedCellsOnly = selectedOnly;
        if (!m_applyingSavedState) {
            emit configurationChanged(table);
        }
    }
}

bool ResultTableConfigurator::exportOnlySelectedCells(const QTableView* table) const {
    const TableState* state = findState(table);
    return state != nullptr && state->exportSelectedCellsOnly;
}

void ResultTableConfigurator::resetExportOptions(QTableView* table) {
    if (TableState* state = findState(table)) {
        state->exportActiveColumnsOnly = false;
        state->exportSelectedCellsOnly = false;
        if (!m_applyingSavedState) {
            emit configurationChanged(table);
        }
    }
}

void ResultTableConfigurator::updateHeaderWordWrap(QTableView* table) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr || state->suspendHeaderWrap) {
        return;
    }
    QHeaderView* hh = table->horizontalHeader();
    if (hh == nullptr) {
        return;
    }

    state->suspendHeaderWrap = true;
    const QFontMetrics fm(hh->font());
    int maxLines = 1;
    const int colCount = tableColumnCount(table);
    auto* tw = qobject_cast<QTableWidget*>(table);
    auto* proxy = qobject_cast<ResultTableProxyModel*>(table->model());

    for (int c = 0; c < colCount; ++c) {
        if (table->isColumnHidden(c)) {
            continue;
        }
        QString orig = (c < state->originalHeaders.size()) ? state->originalHeaders[c] : QString();
        if (orig.isEmpty()) {
            orig = headerText(table, c);
        }
        const QString wrapped = wrapHeaderWords(orig, fm, hh->sectionSize(c));
        maxLines = std::max(maxLines, static_cast<int>(wrapped.count(QLatin1Char('\n'))) + 1);

        QString singleLine = orig;
        singleLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
        const QString tooltip = singleLine.simplified();

        if (tw != nullptr) {
            QTableWidgetItem* hItem = tw->horizontalHeaderItem(c);
            if (hItem == nullptr) {
                hItem = new QTableWidgetItem();
                tw->setHorizontalHeaderItem(c, hItem);
            }
            if (hItem->text() != wrapped) {
                hItem->setText(wrapped);
            }
            hItem->setToolTip(tooltip);
            hItem->setTextAlignment(Qt::AlignCenter);
        } else if (proxy != nullptr) {
            proxy->setWrappedHeader(c, wrapped, tooltip);
        }
    }

    const int targetHeight = maxLines * fm.height() + kHeaderHeightMargin;
    hh->setMinimumHeight(targetHeight);
    hh->setMaximumHeight(targetHeight);
    hh->updateGeometry();
    state->suspendHeaderWrap = false;
}

void ResultTableConfigurator::selectCornerAll(QTableView* table) {
    if (table == nullptr) {
        return;
    }

    QItemSelectionModel* sm = table->selectionModel();
    const int rows = tableRowCount(table);
    const int cols = tableColumnCount(table);
    bool hasVisibleCells = false;
    bool allSelected = false;
    if (sm != nullptr && table->model() != nullptr) {
        allSelected = true;
        for (int r = 0; r < rows && allSelected; ++r) {
            if (table->isRowHidden(r)) {
                continue;
            }
            for (int c = 0; c < cols; ++c) {
                if (table->isColumnHidden(c)) {
                    continue;
                }
                hasVisibleCells = true;
                if (!sm->isSelected(table->model()->index(r, c))) {
                    allSelected = false;
                    break;
                }
            }
        }
    }

    if (hasVisibleCells && allSelected) {
        table->clearSelection();
    } else {
        table->selectAll();
    }

    if (table->viewport() != nullptr) {
        table->viewport()->update();
    }
    if (QHeaderView* hh = table->horizontalHeader(); hh != nullptr && hh->viewport() != nullptr) {
        hh->viewport()->update();
    }
    if (QHeaderView* vh = table->verticalHeader(); vh != nullptr && vh->viewport() != nullptr) {
        vh->viewport()->update();
    }
}

void ResultTableConfigurator::resetAllDefaults(QTableView* table) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return;
    }
    const bool prevApplying = m_applyingSavedState;
    m_applyingSavedState = true;
    state->sortingEnabled = false;
    state->sortColumn = -1;
    state->sortOrder = Qt::AscendingOrder;
    state->headerAnchorColumn = -1;
    setRowOrderMode(table, TableRowOrderMode::AppendBottom);
    state->showColumnColors = true;
    state->coloredColumns.clear();
    state->exportActiveColumnsOnly = false;
    state->exportSelectedCellsOnly = false;
    resetCategoryColors(table);
    resetColumns(table);
    if (QHeaderView* hh = table->horizontalHeader()) {
        hh->setSortIndicatorShown(false);
    }
    syncColoredDelegateState(*state);
    if (!table->objectName().isEmpty()) {
        m_savedStatesByTableName.remove(table->objectName());
    }
    m_applyingSavedState = prevApplying;
    if (!m_applyingSavedState) {
        emit configurationChanged(table);
    }
}

QJsonObject ResultTableConfigurator::saveTableStateToJson(const QTableView* table) const {
    const TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return {};
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("sortingEnabled"), state->sortingEnabled);
    obj.insert(QStringLiteral("sortColumn"), state->sortColumn);
    obj.insert(QStringLiteral("sortOrder"), static_cast<int>(state->sortOrder));
    obj.insert(QStringLiteral("rowOrderMode"), static_cast<int>(state->rowOrderMode));
    obj.insert(QStringLiteral("showColumnColors"), state->showColumnColors);
    obj.insert(QStringLiteral("exportActiveColumnsOnly"), state->exportActiveColumnsOnly);
    obj.insert(QStringLiteral("exportSelectedCellsOnly"), state->exportSelectedCellsOnly);

    QJsonArray coloredArr;
    for (int col : state->coloredColumns) {
        coloredArr.append(col);
    }
    obj.insert(QStringLiteral("coloredColumns"), coloredArr);

    QJsonObject catColorsObj;
    QJsonObject customCatColorsObj;
    const std::vector<CategorySpec> specs = categoriesForKind(state->kind);
    for (auto it = state->categoryColors.constBegin(); it != state->categoryColors.constEnd(); ++it) {
        if (!it.value().isValid()) {
            continue;
        }
        const QString hex = it.value().name(QColor::HexArgb);
        catColorsObj.insert(it.key(), hex);
        bool matchesBuiltinDefault = false;
        for (const CategorySpec& spec : specs) {
            if (spec.key == it.key() &&
                (it.value() == spec.darkDefault || it.value() == spec.lightDefault)) {
                matchesBuiltinDefault = true;
                break;
            }
        }
        if (!matchesBuiltinDefault) {
            customCatColorsObj.insert(it.key(), hex);
        }
    }
    obj.insert(QStringLiteral("categoryColors"), catColorsObj);
    obj.insert(QStringLiteral("customCategoryColors"), customCatColorsObj);

    QJsonArray colHiddenArr;
    QJsonArray colWidthArr;
    QJsonArray colVisualOrderArr;
    const QHeaderView* hh = table->horizontalHeader();
    const int colCount = tableColumnCount(table);
    for (int c = 0; c < colCount; ++c) {
        colHiddenArr.append(table->isColumnHidden(c));
        colWidthArr.append(table->columnWidth(c));
        colVisualOrderArr.append(hh != nullptr ? hh->logicalIndex(c) : c);
    }
    obj.insert(QStringLiteral("columnHidden"), colHiddenArr);
    obj.insert(QStringLiteral("columnWidths"), colWidthArr);
    obj.insert(QStringLiteral("columnVisualOrder"), colVisualOrderArr);
    return obj;
}

void ResultTableConfigurator::loadTableStateFromJson(QTableView* table, const QJsonObject& obj) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return;
    }

    const bool prevApplying = m_applyingSavedState;
    m_applyingSavedState = true;

    const int colCount = tableColumnCount(table);
    state->sortingEnabled = obj.value(QStringLiteral("sortingEnabled")).toBool(false);
    const int savedSortCol = obj.value(QStringLiteral("sortColumn")).toInt(-1);
    state->sortColumn = (savedSortCol >= 0 && savedSortCol < colCount) ? savedSortCol : -1;
    const int savedOrder = obj.value(QStringLiteral("sortOrder"))
                               .toInt(static_cast<int>(Qt::AscendingOrder));
    state->sortOrder = (savedOrder == static_cast<int>(Qt::DescendingOrder))
                           ? Qt::DescendingOrder
                           : Qt::AscendingOrder;

    const int rowOrderInt = obj.value(QStringLiteral("rowOrderMode"))
                                .toInt(static_cast<int>(TableRowOrderMode::AppendBottom));
    state->rowOrderMode = (rowOrderInt == static_cast<int>(TableRowOrderMode::PrependTop))
                              ? TableRowOrderMode::PrependTop
                              : TableRowOrderMode::AppendBottom;

    state->showColumnColors = obj.value(QStringLiteral("showColumnColors")).toBool(true);
    state->exportActiveColumnsOnly = obj.value(QStringLiteral("exportActiveColumnsOnly")).toBool(false);
    state->exportSelectedCellsOnly = obj.value(QStringLiteral("exportSelectedCellsOnly")).toBool(false);

    state->coloredColumns.clear();
    const QJsonArray coloredArr = obj.value(QStringLiteral("coloredColumns")).toArray();
    for (const auto val : coloredArr) {
        const int c = val.toInt(-1);
        if (c >= 0 && c < colCount && !state->coloredColumns.contains(c)) {
            state->coloredColumns.append(c);
        }
    }

    state->categoryColors = buildDefaultCategoryColors(state->kind, m_isLightTheme);
    const std::vector<CategorySpec> specs = categoriesForKind(state->kind);
    const QJsonObject customCatObj = obj.value(QStringLiteral("customCategoryColors")).toObject();
    for (auto it = customCatObj.constBegin(); it != customCatObj.constEnd(); ++it) {
        const QColor parsed(it.value().toString());
        if (parsed.isValid()) {
            state->categoryColors.insert(it.key(), parsed);
        }
    }
    if (customCatObj.isEmpty()) {
        const QJsonObject catObj = obj.value(QStringLiteral("categoryColors")).toObject();
        for (auto it = catObj.constBegin(); it != catObj.constEnd(); ++it) {
            const QColor parsed(it.value().toString());
            if (!parsed.isValid()) {
                continue;
            }
            bool matchesBuiltinDefault = false;
            for (const CategorySpec& spec : specs) {
                if (spec.key == it.key() &&
                    (parsed == spec.darkDefault || parsed == spec.lightDefault)) {
                    matchesBuiltinDefault = true;
                    break;
                }
            }
            if (!matchesBuiltinDefault) {
                state->categoryColors.insert(it.key(), parsed);
            }
        }
    }

    state->suspendHeaderWrap = true;
    const QJsonArray colWidthArr = obj.value(QStringLiteral("columnWidths")).toArray();
    bool hasValidWidth = false;
    for (int c = 0; c < colCount && c < colWidthArr.size(); ++c) {
        const int w = colWidthArr.at(c).toInt(-1);
        if (w >= kMinColumnWidth) {
            table->setColumnWidth(c, w);
            hasValidWidth = true;
        }
    }
    if (hasValidWidth) {
        state->hasInitialFit = true;
    }

    const QJsonArray colHiddenArr = obj.value(QStringLiteral("columnHidden")).toArray();
    for (int c = 0; c < colCount && c < colHiddenArr.size(); ++c) {
        table->setColumnHidden(c, colHiddenArr.at(c).toBool(false));
    }

    const QJsonArray colVisualArr = obj.value(QStringLiteral("columnVisualOrder")).toArray();
    if (QHeaderView* hh = table->horizontalHeader(); hh != nullptr && colVisualArr.size() == colCount) {
        const QSignalBlocker hhBlocker(hh);
        for (int v = 0; v < colCount; ++v) {
            const int desiredLogical = colVisualArr.at(v).toInt(-1);
            if (desiredLogical >= 0 && desiredLogical < colCount) {
                const int curVis = hh->visualIndex(desiredLogical);
                if (curVis >= 0 && curVis != v) {
                    hh->moveSection(curVis, v);
                }
            }
        }
    }
    state->suspendHeaderWrap = false;

    if (QHeaderView* hh = table->horizontalHeader()) {
        const bool showSort = state->sortingEnabled && state->sortColumn >= 0 && state->sortColumn < colCount;
        hh->setSortIndicatorShown(showSort);
        if (showSort) {
            hh->setSortIndicator(state->sortColumn, state->sortOrder);
        }
    }

    if (tableRowCount(table) > 1) {
        if (state->appliedRowOrderMode != state->rowOrderMode) {
            reverseTableRowsInPlace(table, state->kind == ResultTableKind::Reactions);
            state->appliedRowOrderMode = state->rowOrderMode;
        }
        if (state->sortingEnabled && state->sortColumn >= 0 && state->sortColumn < colCount) {
            sortTableByColumn(table, state->sortColumn, state->sortOrder);
        }
    }

    updateHeaderWordWrap(table);
    syncColoredDelegateState(*state);

    m_applyingSavedState = prevApplying;
}

QJsonObject ResultTableConfigurator::saveAllTablesToJson() const {
    QJsonObject result;
    for (auto it = m_savedStatesByTableName.constBegin(); it != m_savedStatesByTableName.constEnd(); ++it) {
        if (!it.key().isEmpty()) {
            result.insert(it.key(), it.value());
        }
    }
    for (const TableState& st : m_tables) {
        if (st.table != nullptr && !st.table->objectName().isEmpty()) {
            result.insert(st.table->objectName(), saveTableStateToJson(st.table));
        }
    }
    return result;
}

void ResultTableConfigurator::loadAllTablesFromJson(const QJsonObject& tablesObj) {
    m_savedStatesByTableName.clear();
    for (auto it = tablesObj.constBegin(); it != tablesObj.constEnd(); ++it) {
        if (!it.key().isEmpty() && it.value().isObject()) {
            m_savedStatesByTableName.insert(it.key(), it.value().toObject());
        }
    }
    for (TableState& st : m_tables) {
        if (st.table != nullptr && !st.table->objectName().isEmpty() &&
            m_savedStatesByTableName.contains(st.table->objectName())) {
            loadTableStateFromJson(st.table, m_savedStatesByTableName.value(st.table->objectName()));
        }
    }
}

void ResultTableConfigurator::handleHeaderSectionClicked(TableState& state, int logicalColumn) {
    QTableView* table = state.table;
    const int colCount = tableColumnCount(table);
    const int rowCount = tableRowCount(table);
    if (table == nullptr || logicalColumn < 0 || logicalColumn >= colCount) {
        return;
    }

    const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    const bool ctrl = mods.testFlag(Qt::ControlModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);

    if (state.sortingEnabled && !ctrl && !shift) {
        if (state.sortColumn == logicalColumn) {
            state.sortOrder = (state.sortOrder == Qt::AscendingOrder)
                                  ? Qt::DescendingOrder
                                  : Qt::AscendingOrder;
        } else {
            state.sortColumn = logicalColumn;
            state.sortOrder = Qt::AscendingOrder;
        }
        sortTableByColumn(table, state.sortColumn, state.sortOrder);
        return;
    }

    if (rowCount <= 0) {
        state.headerAnchorColumn = logicalColumn;
        return;
    }

    if (shift) {
        const int anchor = (state.headerAnchorColumn >= 0 && state.headerAnchorColumn < colCount)
                               ? state.headerAnchorColumn
                               : logicalColumn;
        const int startCol = std::min(anchor, logicalColumn);
        const int endCol = std::max(anchor, logicalColumn);
        selectHeaderColumnRange(state, startCol, endCol, ctrl);
        if (state.headerAnchorColumn < 0) {
            state.headerAnchorColumn = logicalColumn;
        }
    } else if (ctrl) {
        QItemSelectionModel* sm = table->selectionModel();
        if (sm != nullptr && table->model() != nullptr) {
            const QModelIndex top = table->model()->index(0, logicalColumn);
            const QModelIndex bot = table->model()->index(rowCount - 1, logicalColumn);
            const QItemSelection colSel(top, bot);
            const auto flags = sm->isColumnSelected(logicalColumn, QModelIndex())
                                   ? (QItemSelectionModel::Deselect | QItemSelectionModel::Columns)
                                   : (QItemSelectionModel::Select | QItemSelectionModel::Columns);
            sm->select(colSel, flags);
        }
        state.headerAnchorColumn = logicalColumn;
    } else {
        selectHeaderColumnRange(state, logicalColumn, logicalColumn, false);
        state.headerAnchorColumn = logicalColumn;
    }

    if (table->viewport() != nullptr) {
        table->viewport()->update();
    }
    if (QHeaderView* hh = table->horizontalHeader(); hh != nullptr && hh->viewport() != nullptr) {
        hh->viewport()->update();
    }
}

void ResultTableConfigurator::selectHeaderColumnRange(TableState& state,
                                                      int startCol,
                                                      int endCol,
                                                      bool preserveExisting) {
    QTableView* table = state.table;
    const int rowCount = tableRowCount(table);
    const int colCount = tableColumnCount(table);
    if (table == nullptr || table->model() == nullptr || rowCount <= 0 || colCount <= 0) {
        return;
    }
    QItemSelectionModel* sm = table->selectionModel();
    if (sm == nullptr) {
        return;
    }

    const int lo = std::clamp(std::min(startCol, endCol), 0, colCount - 1);
    const int hi = std::clamp(std::max(startCol, endCol), 0, colCount - 1);
    const QModelIndex topLeft = table->model()->index(0, lo);
    const QModelIndex bottomRight = table->model()->index(rowCount - 1, hi);
    const QItemSelection rangeSel(topLeft, bottomRight);
    const QItemSelectionModel::SelectionFlags flags =
        (preserveExisting ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect) |
        QItemSelectionModel::Columns;

    sm->select(rangeSel, flags);
}

void ResultTableConfigurator::handleCellDoubleClicked(const TableState& state, int row, int) {
    const QTableView* table = state.table;
    if (table == nullptr || row < 0 || row >= tableRowCount(table)) {
        return;
    }
    if (isSummaryRow(table, row)) {
        return;
    }

    bool okId = false;
    unsigned int id = 0;
    const QVariant idVar = cellData(table, row, 0, kResultTableEntityIdRole);
    if (idVar.isValid()) {
        id = idVar.toUInt(&okId);
    }
    if (!okId) {
        const QString txt = cellText(table, row, 0).trimmed();
        id = txt.toUInt(&okId);
    }
    if (!okId || id == 0) {
        return;
    }

    switch (state.kind) {
    case ResultTableKind::Displacements:
    case ResultTableKind::Reactions:
        emit nodeDoubleClicked(id);
        break;
    case ResultTableKind::BarForces:
        emit elementDoubleClicked(id);
        break;
    case ResultTableKind::ContinuumStress: {
        const QString typeName = (tableColumnCount(table) > 1) ? cellText(table, row, 1) : QString();
        emit continuumElementDoubleClicked(id, typeName);
        emit statusMessageRequested(
            tr("Elemento %1 (%2) — inspeção na tabela de contínuos.").arg(id).arg(typeName),
            4000);
        break;
    }
    }
}

QList<int> ResultTableConfigurator::columnsToProcess(const TableState& state, bool applyExportFilter) const {
    const QTableView* table = state.table;
    QList<int> cols;
    if (table == nullptr) {
        return cols;
    }

    const int colCount = tableColumnCount(table);
    const QHeaderView* hh = table->horizontalHeader();

    QList<int> visualOrder;
    visualOrder.reserve(colCount);
    for (int v = 0; v < colCount; ++v) {
        const int logical = (hh != nullptr) ? hh->logicalIndex(v) : v;
        if (logical >= 0 && logical < colCount) {
            visualOrder.append(logical);
        }
    }

    QList<int> selectedCols;
    if (applyExportFilter && state.exportSelectedCellsOnly) {
        if (table->selectionModel() == nullptr) {
            return cols;
        }
        const QModelIndexList selected = table->selectionModel()->selectedIndexes();
        if (selected.isEmpty()) {
            return cols;
        }
        for (const QModelIndex& idx : selected) {
            if (!selectedCols.contains(idx.column())) {
                selectedCols.append(idx.column());
            }
        }
    }

    for (int logical : visualOrder) {
        if (applyExportFilter && state.exportActiveColumnsOnly && table->isColumnHidden(logical)) {
            continue;
        }
        if (applyExportFilter && state.exportSelectedCellsOnly && !selectedCols.contains(logical)) {
            continue;
        }
        cols.append(logical);
    }
    return cols;
}

QList<int> ResultTableConfigurator::rowsToProcess(const TableState& state, bool applyExportFilter) const {
    const QTableView* table = state.table;
    QList<int> rows;
    if (table == nullptr) {
        return rows;
    }

    if (applyExportFilter && state.exportSelectedCellsOnly) {
        if (table->selectionModel() == nullptr) {
            return rows;
        }
        const QModelIndexList selected = table->selectionModel()->selectedIndexes();
        if (selected.isEmpty()) {
            return rows;
        }
        for (const QModelIndex& idx : selected) {
            if (!rows.contains(idx.row())) {
                rows.append(idx.row());
            }
        }
        std::sort(rows.begin(), rows.end());
        return rows;
    }

    const int rowCount = tableRowCount(table);
    rows.reserve(rowCount);
    for (int r = 0; r < rowCount; ++r) {
        rows.append(r);
    }
    return rows;
}

QString ResultTableConfigurator::buildTsvText(const TableState& state,
                                              const QList<int>& rows,
                                              const QList<int>& cols,
                                              bool includeHeaders) const {
    const QTableView* table = state.table;
    if (table == nullptr || cols.isEmpty()) {
        return {};
    }

    QStringList lines;
    if (includeHeaders) {
        QStringList hdrs;
        for (int c : cols) {
            QString h = (c < state.originalHeaders.size()) ? state.originalHeaders[c] : QString();
            h.replace(QLatin1Char('\n'), QLatin1Char(' '));
            hdrs.append(sanitizeTabularCell(h.simplified()));
        }
        lines.append(hdrs.join(QLatin1Char('\t')));
    }

    for (int r : rows) {
        QStringList cells;
        for (int c : cols) {
            cells.append(sanitizeTabularCell(cellText(table, r, c)));
        }
        lines.append(cells.join(QLatin1Char('\t')));
    }
    return lines.join(QLatin1Char('\n'));
}

void ResultTableConfigurator::copySelectionToClipboard(QTableView* table, bool includeHeaders) const {
    const TableState* state = findState(table);
    if (state == nullptr || table == nullptr || table->selectionModel() == nullptr) {
        return;
    }

    const QModelIndexList selected = table->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        return;
    }

    QList<int> selRows;
    QList<int> selCols;
    QHash<qint64, QString> selectedTextMap;
    for (const QModelIndex& idx : selected) {
        if (!selRows.contains(idx.row())) {
            selRows.append(idx.row());
        }
        if (!selCols.contains(idx.column())) {
            selCols.append(idx.column());
        }
        const qint64 key = (static_cast<qint64>(idx.row()) << 32) | static_cast<quint32>(idx.column());
        selectedTextMap.insert(key, sanitizeTabularCell(cellText(table, idx.row(), idx.column())));
    }
    std::sort(selRows.begin(), selRows.end());
    std::sort(selCols.begin(), selCols.end());

    QStringList lines;
    if (includeHeaders) {
        QStringList hdrs;
        for (int c : selCols) {
            QString h = (c < state->originalHeaders.size()) ? state->originalHeaders[c] : QString();
            h.replace(QLatin1Char('\n'), QLatin1Char(' '));
            hdrs.append(sanitizeTabularCell(h.simplified()));
        }
        lines.append(hdrs.join(QLatin1Char('\t')));
    }

    for (int r : selRows) {
        QStringList cells;
        for (int c : selCols) {
            const qint64 key = (static_cast<qint64>(r) << 32) | static_cast<quint32>(c);
            cells.append(selectedTextMap.value(key));
        }
        lines.append(cells.join(QLatin1Char('\t')));
    }

    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(lines.join(QLatin1Char('\n')));
    }
}

void ResultTableConfigurator::copyWholeTableToClipboard(QTableView* table) const {
    const TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return;
    }
    const QList<int> rows = rowsToProcess(*state, false);
    const QList<int> cols = columnsToProcess(*state, false);
    const QString text = buildTsvText(*state, rows, cols, true);
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
}

bool ResultTableConfigurator::exportTableToFile(QTableView* table,
                                                const QString& filePath,
                                                QString* errorMessage) const {
    const TableState* state = findState(table);
    if (state == nullptr || table == nullptr || filePath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Erro ao exportar tabela.");
        }
        return false;
    }

    QList<int> cols = columnsToProcess(*state, true);
    const QList<int> rows = rowsToProcess(*state, true);

    QHash<qint64, bool> selectedKeys;
    if (state->exportSelectedCellsOnly) {
        if (table->selectionModel() != nullptr) {
            const QModelIndexList selIndices = table->selectionModel()->selectedIndexes();
            for (const QModelIndex& idx : selIndices) {
                const qint64 key = (static_cast<qint64>(idx.row()) << 32) | static_cast<quint32>(idx.column());
                selectedKeys.insert(key, true);
            }
        }
        if (selectedKeys.isEmpty() && cols.isEmpty()) {
            cols = columnsToProcess(*state, false);
            if (state->exportActiveColumnsOnly) {
                cols.erase(std::remove_if(cols.begin(), cols.end(), [table](int c) {
                    return table->isColumnHidden(c);
                }), cols.end());
            }
        }
    }

    QStringList headers;
    headers.reserve(cols.size());
    for (int c : std::as_const(cols)) {
        QString h = (c < state->originalHeaders.size()) ? state->originalHeaders[c] : QString();
        h.replace(QLatin1Char('\n'), QLatin1Char(' '));
        headers.append(h.simplified());
    }

    QList<QStringList> matrix;
    matrix.reserve(rows.size());
    for (int r : rows) {
        QStringList rowVals;
        rowVals.reserve(cols.size());
        for (int c : std::as_const(cols)) {
            if (state->exportSelectedCellsOnly) {
                const qint64 key = (static_cast<qint64>(r) << 32) | static_cast<quint32>(c);
                if (!selectedKeys.contains(key)) {
                    rowVals.append(QString());
                    continue;
                }
            }
            rowVals.append(cellText(table, r, c));
        }
        matrix.append(rowVals);
    }

    const QString ext = QFileInfo(filePath).suffix().trimmed().toLower();
    QByteArray payload;

    if (ext == QStringLiteral("csv")) {
        QStringList lines;
        QStringList escapedHeaders;
        for (const QString& h : headers) {
            escapedHeaders.append(escapeCsvField(h));
        }
        lines.append(escapedHeaders.join(QLatin1Char(',')));
        for (const QStringList& rowVals : matrix) {
            QStringList escapedRow;
            for (const QString& v : rowVals) {
                escapedRow.append(escapeCsvField(v));
            }
            lines.append(escapedRow.join(QLatin1Char(',')));
        }
        payload = QByteArray("\xEF\xBB\xBF") + lines.join(QLatin1Char('\n')).toUtf8();
    } else if (ext == QStringLiteral("txt") || ext == QStringLiteral("tsv")) {
        QStringList lines;
        QStringList safeHeaders;
        for (const QString& h : headers) {
            safeHeaders.append(sanitizeTabularCell(h));
        }
        lines.append(safeHeaders.join(QLatin1Char('\t')));
        for (const QStringList& rowVals : matrix) {
            QStringList safeRow;
            for (const QString& v : rowVals) {
                safeRow.append(sanitizeTabularCell(v));
            }
            lines.append(safeRow.join(QLatin1Char('\t')));
        }
        payload = lines.join(QLatin1Char('\n')).toUtf8();
    } else if (ext == QStringLiteral("json")) {
        QJsonArray arr;
        for (const QStringList& rowVals : matrix) {
            QJsonObject obj;
            for (int i = 0; i < headers.size() && i < rowVals.size(); ++i) {
                obj.insert(headers[i], rowVals[i]);
            }
            arr.append(obj);
        }
        payload = QJsonDocument(arr).toJson(QJsonDocument::Indented);
    } else if (ext == QStringLiteral("xml")) {
        QXmlStreamWriter xml(&payload);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeStartElement(QStringLiteral("ResultsTable"));
        for (const QStringList& rowVals : matrix) {
            xml.writeStartElement(QStringLiteral("Row"));
            for (int i = 0; i < headers.size() && i < rowVals.size(); ++i) {
                xml.writeStartElement(QStringLiteral("Cell"));
                xml.writeAttribute(QStringLiteral("column"), stripIllegalXml10Chars(headers[i]));
                xml.writeCharacters(stripIllegalXml10Chars(rowVals[i]));
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndDocument();
    } else if (ext == QStringLiteral("xlsx")) {
        payload = buildXlsxPackage(headers, matrix);
        if (payload.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("Erro ao exportar tabela.");
            }
            return false;
        }
    } else {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Erro ao exportar tabela.");
        }
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

void ResultTableConfigurator::showExportDialog(QTableView* table) {
    if (table == nullptr) {
        return;
    }
    QString initialDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (initialDir.isEmpty()) {
        initialDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    const QString initialPath = initialDir + QStringLiteral("/resultados_estruturais.xlsx");
    const QString filter = tr("Planilha Excel (*.xlsx);;CSV UTF-8 (*.csv);;Texto Tabulado (*.txt);;JSON (*.json);;XML (*.xml)");
    const QString title = tr("Exportar Dados da Tabela");

    QPointer<ResultTableConfigurator> self(this);
    QPointer<QTableView> tablePtr(table);
    auto finishExport = [self, tablePtr](const QString& selectedPath) {
        if (!self || !tablePtr || selectedPath.trimmed().isEmpty()) {
            return;
        }
        QString errMsg;
        if (self->exportTableToFile(tablePtr.data(), selectedPath, &errMsg)) {
            emit self->statusMessageRequested(tr("Tabela exportada com sucesso."), 4000);
        } else {
            emit self->statusMessageRequested(errMsg.isEmpty() ? tr("Erro ao exportar tabela.") : errMsg, 5000);
        }
    };

    if (m_windowManager != nullptr) {
        m_windowManager->showModelessFileDialog(
            QFileDialog::AcceptSave,
            QFileDialog::AnyFile,
            title,
            initialPath,
            filter,
            finishExport);
    } else {
        const QString selected = QFileDialog::getSaveFileName(table, title, initialPath, filter);
        finishExport(selected);
    }
}

void ResultTableConfigurator::pickCategoryColor(TableState& state, const QString& categoryKey) {
    const QColor current = state.categoryColors.value(categoryKey, QColor(80, 120, 200));
    auto* dlg = new QColorDialog(current, state.table);
    dlg->setWindowTitle(tr("Selecionar Cor"));
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    QPointer<QTableView> tablePtr(state.table);
    connect(dlg, &QColorDialog::colorSelected, this, [this, tablePtr, categoryKey](const QColor& chosen) {
        if (tablePtr) {
            setCategoryColor(tablePtr.data(), categoryKey, chosen);
        }
    });
    if (m_windowManager != nullptr) {
        m_windowManager->prepareModelessDialog(dlg, false, true);
        m_windowManager->showAndRaise(dlg);
    } else {
        dlg->setWindowModality(Qt::NonModal);
        dlg->show();
    }
}

QMenu* ResultTableConfigurator::createTableContextMenu(QTableView* table,
                                                       QWidget* parent,
                                                       bool) {
    TableState* state = findState(table);
    if (state == nullptr || table == nullptr) {
        return nullptr;
    }

    QMenu* menu = new PersistentTableMenu(parent != nullptr ? parent : table);
    const bool hasSelection = table->selectionModel() != nullptr &&
                              !table->selectionModel()->selectedIndexes().isEmpty();
    const int colCount = tableColumnCount(table);

    // 1. Configurar colunas
    QMenu* colsMenu = new PersistentTableMenu(tr("Configurar colunas"), menu);
    for (int c = 0; c < colCount; ++c) {
        QString colLabel = (c < state->originalHeaders.size()) ? state->originalHeaders[c] : QString();
        colLabel.replace(QLatin1Char('\n'), QLatin1Char(' '));
        QAction* actCol = colsMenu->addAction(colLabel.simplified());
        actCol->setCheckable(true);
        actCol->setChecked(!table->isColumnHidden(c));
        connect(actCol, &QAction::toggled, this, [this, table, c](bool visible) {
            setColumnVisible(table, c, visible);
        });
    }
    colsMenu->addSeparator();
    QAction* actShowAllCols = colsMenu->addAction(tr("Selecionar todas colunas"));
    connect(actShowAllCols, &QAction::triggered, this, [this, table, colsMenu]() {
        showAllColumns(table);
        const QList<QAction*> acts = colsMenu->actions();
        const int cols = tableColumnCount(table);
        for (int i = 0; i < cols && i < acts.size(); ++i) {
            const QSignalBlocker b(acts[i]);
            acts[i]->setChecked(true);
        }
    });
    QAction* actResetCols = colsMenu->addAction(tr("Resetar colunas"));
    connect(actResetCols, &QAction::triggered, this, [this, table]() {
        resetColumns(table);
    });
    menu->addMenu(colsMenu);

    // 2. Ordenar linhas (habilita ordenação inteligente pelo cabeçalho)
    QAction* actSort = menu->addAction(tr("Ordenar linhas"));
    actSort->setCheckable(true);
    actSort->setChecked(state->sortingEnabled);
    connect(actSort, &QAction::toggled, this, [this, table](bool enabled) {
        setSortingModeEnabled(table, enabled);
    });

    // 3. Ordem das Linhas da Tabela
    QMenu* orderMenu = new PersistentTableMenu(tr("Ordem das Linhas da Tabela"), menu);
    QAction* actAppend = orderMenu->addAction(tr("Adicionar no Fim"));
    actAppend->setCheckable(true);
    actAppend->setChecked(state->rowOrderMode == TableRowOrderMode::AppendBottom);
    QAction* actPrepend = orderMenu->addAction(tr("Adicionar no Topo"));
    actPrepend->setCheckable(true);
    actPrepend->setChecked(state->rowOrderMode == TableRowOrderMode::PrependTop);
    connect(actAppend, &QAction::triggered, this, [this, table, actAppend, actPrepend]() {
        setRowOrderMode(table, TableRowOrderMode::AppendBottom);
        actAppend->setChecked(true);
        actPrepend->setChecked(false);
    });
    connect(actPrepend, &QAction::triggered, this, [this, table, actAppend, actPrepend]() {
        setRowOrderMode(table, TableRowOrderMode::PrependTop);
        actAppend->setChecked(false);
        actPrepend->setChecked(true);
    });
    orderMenu->addSeparator();
    QAction* actInvertRows = orderMenu->addAction(tr("Inverter Linhas Atuais da Tabela"));
    connect(actInvertRows, &QAction::triggered, this, [this, table]() {
        invertTableRows(table);
    });
    menu->addMenu(orderMenu);

    menu->addSeparator();

    // 4. Mostrar cores nas colunas
    QAction* actShowColors = menu->addAction(tr("Mostrar cores nas colunas"));
    actShowColors->setCheckable(true);
    actShowColors->setChecked(state->showColumnColors);
    connect(actShowColors, &QAction::toggled, this, [this, table](bool enabled) {
        setShowColumnColors(table, enabled);
    });

    // 5. Colorir colunas
    QMenu* colorColsMenu = new PersistentTableMenu(tr("Colorir colunas"), menu);
    for (int c = 0; c < colCount; ++c) {
        QString colLabel = (c < state->originalHeaders.size()) ? state->originalHeaders[c] : QString();
        colLabel.replace(QLatin1Char('\n'), QLatin1Char(' '));
        QAction* actColColor = colorColsMenu->addAction(colLabel.simplified());
        actColColor->setCheckable(true);
        actColColor->setChecked(state->coloredColumns.contains(c));
        connect(actColColor, &QAction::toggled, this, [this, table, c](bool colored) {
            setColumnColored(table, c, colored);
        });
    }
    colorColsMenu->addSeparator();
    QAction* actColorAll = colorColsMenu->addAction(tr("Selecionar todas colunas"));
    connect(actColorAll, &QAction::triggered, this, [this, table, colorColsMenu]() {
        setAllColumnsColored(table, true);
        const QList<QAction*> acts = colorColsMenu->actions();
        const int cols = tableColumnCount(table);
        for (int i = 0; i < cols && i < acts.size(); ++i) {
            const QSignalBlocker b(acts[i]);
            acts[i]->setChecked(true);
        }
    });
    QAction* actResetColColors = colorColsMenu->addAction(tr("Resetar cores de colunas"));
    connect(actResetColColors, &QAction::triggered, this, [this, table, colorColsMenu]() {
        resetColumnColors(table);
        const QList<QAction*> acts = colorColsMenu->actions();
        const int cols = tableColumnCount(table);
        for (int i = 0; i < cols && i < acts.size(); ++i) {
            const QSignalBlocker b(acts[i]);
            acts[i]->setChecked(false);
        }
    });
    menu->addMenu(colorColsMenu);

    // 6. Configurar cores
    QMenu* catColorsMenu = new QMenu(tr("Configurar cores"), menu);
    for (const CategorySpec& spec : categoriesForKind(state->kind)) {
        const QColor col = state->categoryColors.value(spec.key, m_isLightTheme ? spec.lightDefault : spec.darkDefault);
        QAction* actCat = catColorsMenu->addAction(createColorSwatchIcon(col), spec.label);
        connect(actCat, &QAction::triggered, this, [this, table, key = spec.key]() {
            if (TableState* st = findState(table)) {
                pickCategoryColor(*st, key);
            }
        });
    }
    catColorsMenu->addSeparator();
    QAction* actResetCatColors = catColorsMenu->addAction(tr("Resetar cores"));
    connect(actResetCatColors, &QAction::triggered, this, [this, table]() {
        resetCategoryColors(table);
    });
    menu->addMenu(catColorsMenu);

    menu->addSeparator();

    // 7. Cópia para área de transferência
    QAction* actCopySel = menu->addAction(tr("Copiar Seleção"));
    actCopySel->setEnabled(hasSelection);
    connect(actCopySel, &QAction::triggered, this, [this, table]() {
        copySelectionToClipboard(table, false);
    });

    QAction* actCopyHdr = menu->addAction(tr("Copiar com Cabeçalhos"));
    actCopyHdr->setEnabled(hasSelection);
    connect(actCopyHdr, &QAction::triggered, this, [this, table]() {
        copySelectionToClipboard(table, true);
    });

    QAction* actCopyAll = menu->addAction(tr("Copiar Tabela Inteira"));
    actCopyAll->setEnabled(tableRowCount(table) > 0 && colCount > 0);
    connect(actCopyAll, &QAction::triggered, this, [this, table]() {
        copyWholeTableToClipboard(table);
    });

    menu->addSeparator();

    // 8. Opções de exportação
    QMenu* exportOptsMenu = new PersistentTableMenu(tr("Opções de exportação"), menu);
    QAction* actExpActive = exportOptsMenu->addAction(tr("Exportar colunas ativas"));
    actExpActive->setCheckable(true);
    actExpActive->setChecked(state->exportActiveColumnsOnly);
    connect(actExpActive, &QAction::toggled, this, [this, table](bool activeOnly) {
        setExportOnlyActiveColumns(table, activeOnly);
    });

    QAction* actExpSel = exportOptsMenu->addAction(tr("Exportar seleção"));
    actExpSel->setCheckable(true);
    actExpSel->setChecked(state->exportSelectedCellsOnly);
    connect(actExpSel, &QAction::toggled, this, [this, table](bool selectedOnly) {
        setExportOnlySelectedCells(table, selectedOnly);
    });

    exportOptsMenu->addSeparator();
    QAction* actResetExp = exportOptsMenu->addAction(tr("Resetar opções de exportação"));
    connect(actResetExp, &QAction::triggered, this, [this, table, actExpActive, actExpSel]() {
        resetExportOptions(table);
        const QSignalBlocker b1(actExpActive);
        const QSignalBlocker b2(actExpSel);
        actExpActive->setChecked(false);
        actExpSel->setChecked(false);
    });
    menu->addMenu(exportOptsMenu);

    // 9. Exportar Tabela
    QAction* actExport = menu->addAction(tr("Exportar Tabela"));
    actExport->setEnabled(colCount > 0);
    connect(actExport, &QAction::triggered, this, [this, table]() {
        showExportDialog(table);
    });

    return menu;
}

void ResultTableConfigurator::showContextMenuAt(TableState& state, const QPoint& globalPos, bool fromHeader) {
    QMenu* menu = createTableContextMenu(state.table, state.table, fromHeader);
    if (menu == nullptr) {
        return;
    }
    menu->setAttribute(Qt::WA_DeleteOnClose, true);
    menu->popup(globalPos);
}

bool ResultTableConfigurator::eventFilter(QObject* watched, QEvent* event) {
    if (event != nullptr && event->type() == QEvent::KeyPress) {
        QTableView* targetTable = nullptr;
        for (const TableState& st : m_tables) {
            if (st.table == watched || (st.table != nullptr && st.table->viewport() == watched)) {
                targetTable = st.table;
                break;
            }
        }
        if (targetTable != nullptr) {
            auto* ke = static_cast<QKeyEvent*>(event);
            const Qt::KeyboardModifiers mods = ke->modifiers();
            const bool ctrl = mods.testFlag(Qt::ControlModifier);
            const bool shift = mods.testFlag(Qt::ShiftModifier);
            if (ctrl && shift && ke->key() == Qt::Key_C) {
                copySelectionToClipboard(targetTable, true);
                return true;
            }
            if (ctrl && !shift && ke->key() == Qt::Key_C) {
                copySelectionToClipboard(targetTable, false);
                return true;
            }
            if (ctrl && shift && ke->key() == Qt::Key_A) {
                setAllColumnsColored(targetTable, true);
                setShowColumnColors(targetTable, true);
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace omniframe::ui
