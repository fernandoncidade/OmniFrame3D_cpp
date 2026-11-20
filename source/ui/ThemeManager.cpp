#include "ThemeManager.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>
#include <QProxyStyle>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleOption>
#include <QWidget>

#ifdef Q_OS_WIN
#include <QLibrary>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace omniframe {

namespace {

QString themeSettingsFilePath() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(configDir).filePath(QStringLiteral("theme.json"));
}

QPalette buildDarkPalette() {
    QPalette p;
    const QColor windowBg(28, 32, 38);
    const QColor windowText(226, 232, 240);
    const QColor baseBg(20, 23, 28);
    const QColor altBaseBg(35, 40, 48);
    const QColor buttonBg(38, 44, 52);
    const QColor disabledText(118, 128, 144);

    p.setColor(QPalette::Window, windowBg);
    p.setColor(QPalette::WindowText, windowText);
    p.setColor(QPalette::Base, baseBg);
    p.setColor(QPalette::AlternateBase, altBaseBg);
    p.setColor(QPalette::ToolTipBase, QColor(22, 27, 34));
    p.setColor(QPalette::ToolTipText, windowText);
    p.setColor(QPalette::Text, QColor(230, 235, 240));
    p.setColor(QPalette::Button, buttonBg);
    p.setColor(QPalette::ButtonText, QColor(240, 243, 248));
    p.setColor(QPalette::Light, QColor(62, 70, 84));
    p.setColor(QPalette::Midlight, QColor(48, 55, 66));
    p.setColor(QPalette::Mid, QColor(34, 39, 47));
    p.setColor(QPalette::Dark, QColor(18, 21, 26));
    p.setColor(QPalette::Shadow, QColor(10, 12, 16));
    p.setColor(QPalette::BrightText, QColor(248, 113, 113));
    p.setColor(QPalette::Link, QColor(88, 166, 255));
    p.setColor(QPalette::LinkVisited, QColor(167, 139, 250));
    p.setColor(QPalette::Highlight, QColor(43, 108, 176));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(138, 148, 166));

    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(51, 65, 85));
    return p;
}

QPalette buildLightPalette() {
    QPalette p;
    const QColor windowBg(238, 242, 247);
    const QColor windowText(15, 23, 42);
    const QColor baseBg(255, 255, 255);
    const QColor altBaseBg(248, 250, 252);
    const QColor buttonBg(226, 232, 240);
    const QColor disabledText(148, 163, 184);

    p.setColor(QPalette::Window, windowBg);
    p.setColor(QPalette::WindowText, windowText);
    p.setColor(QPalette::Base, baseBg);
    p.setColor(QPalette::AlternateBase, altBaseBg);
    p.setColor(QPalette::ToolTipBase, baseBg);
    p.setColor(QPalette::ToolTipText, windowText);
    p.setColor(QPalette::Text, windowText);
    p.setColor(QPalette::Button, buttonBg);
    p.setColor(QPalette::ButtonText, windowText);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(241, 245, 249));
    p.setColor(QPalette::Mid, QColor(203, 213, 225));
    p.setColor(QPalette::Dark, QColor(148, 163, 184));
    p.setColor(QPalette::Shadow, QColor(100, 116, 139));
    p.setColor(QPalette::BrightText, QColor(185, 28, 28));
    p.setColor(QPalette::Link, QColor(29, 78, 216));
    p.setColor(QPalette::LinkVisited, QColor(109, 40, 217));
    p.setColor(QPalette::Highlight, QColor(37, 99, 235));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(100, 116, 139));

    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(191, 219, 254));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(71, 85, 105));
    return p;
}

QString buildDarkApplicationStyleSheet() {
    return QStringLiteral(
        "QMainWindow, QDialog, QMessageBox, QFileDialog { background-color: #1c2026; color: #e2e8f0; }"
        "QMenuBar { background-color: #20252d; color: #e2e8f0; border-bottom: 1px solid #2d333f; }"
        "QMenuBar::item:selected { background-color: #2b6cb0; color: #ffffff; border-radius: 3px; }"
        "QMenu { background-color: #1e232b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"
        "QMenu::item:selected { background-color: #2b6cb0; color: #ffffff; border-radius: 3px; }"
        "QMenu::separator { height: 1px; background: #2d333f; margin: 4px 8px; }"
        "QToolBar { background-color: #23272f; border: 1px solid #2d333f; spacing: 4px; padding: 3px; }"
        "QToolButton { background-color: #2b313b; color: #e1e6eb; border: 1px solid #3a424f; border-radius: 3px; padding: 4px 8px; }"
        "QToolButton:hover { background-color: #38404d; border-color: #4a5568; }"
        "QToolButton:checked { background-color: #2b6cb0; border-color: #3182ce; color: #ffffff; }"
        "QDockWidget { titlebar-close-icon: url(); titlebar-normal-icon: url(); color: #e1e6eb; font-weight: bold; }"
        "QDockWidget::title { background-color: #23272f; color: #e1e6eb; padding: 6px; border: 1px solid #2d333f; text-align: left; }"
        "QGroupBox { border: 1px solid #334155; border-radius: 5px; margin-top: 10px; padding-top: 10px; color: #e2e8f0; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #93c5fd; }"
        "QTableWidget, QListWidget, QTreeView { background-color: #171a1f; alternate-background-color: #1d2229; color: #d0d7de; gridline-color: #2d333f; border: 1px solid #2d333f; selection-background-color: #2b6cb0; selection-color: #ffffff; }"
        "QHeaderView::section { background-color: #23272f; color: #cbd5e0; padding: 4px; border: 1px solid #2d333f; font-weight: bold; }"
        "QTabWidget::pane { border: 1px solid #2d333f; background-color: #1c2026; }"
        "QTabBar::tab { background-color: #23272f; color: #a0aec0; padding: 6px 14px; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; }"
        "QTabBar::tab:selected { background-color: #2b6cb0; color: #ffffff; font-weight: bold; }"
        "QPushButton { background-color: #2b313b; color: #e2e8f0; border: 1px solid #3a424f; border-radius: 4px; padding: 5px 12px; }"
        "QPushButton:hover { background-color: #38404d; border-color: #4a5568; }"
        "QPushButton:pressed { background-color: #1f252e; }"
        "QPushButton:disabled { color: #64748b; border-color: #2b313b; }"
        "QLineEdit, QTextEdit, QPlainTextEdit { background-color: #14171c; color: #e2e8f0; border: 1px solid #334155; border-radius: 4px; padding: 3px 6px; selection-background-color: #2b6cb0; selection-color: #ffffff; }"
        "QLineEdit:focus { border-color: #3b82f6; }"
        "QSpinBox QLineEdit, QDoubleSpinBox QLineEdit { background: transparent; border: none; padding: 0px; }"
        "QComboBox QAbstractItemView { background-color: #181c23; color: #e2e8f0; selection-background-color: #2b6cb0; selection-color: #ffffff; border: 1px solid #334155; }"
        "QStatusBar { background-color: #1a1e24; color: #cbd5e1; border-top: 1px solid #2d333f; }"
        "QToolTip { background-color: #181d25; color: #e2e8f0; border: 1px solid #475569; padding: 4px 6px; }"
        "QSplitter::handle { background-color: #2d333f; }"
    );
}

QString buildLightApplicationStyleSheet() {
    return QStringLiteral(
        "QMainWindow, QDialog, QMessageBox, QFileDialog { background-color: #eef2f7; color: #0f172a; }"
        "QMenuBar { background-color: #f8fafc; color: #0f172a; border-bottom: 1px solid #cbd5e1; }"
        "QMenuBar::item:selected { background-color: #dbeafe; color: #1e3a8a; border-radius: 3px; }"
        "QMenu { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; padding: 4px; }"
        "QMenu::item:selected { background-color: #2563eb; color: #ffffff; border-radius: 3px; }"
        "QMenu::separator { height: 1px; background: #e2e8f0; margin: 4px 8px; }"
        "QToolBar { background-color: #f8fafc; border: 1px solid #cbd5e1; spacing: 4px; padding: 3px; }"
        "QToolButton { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px 8px; }"
        "QToolButton:hover { background-color: #e2e8f0; border-color: #94a3b8; }"
        "QToolButton:checked { background-color: #2563eb; border-color: #1d4ed8; color: #ffffff; }"
        "QDockWidget { titlebar-close-icon: url(); titlebar-normal-icon: url(); color: #0f172a; font-weight: bold; }"
        "QDockWidget::title { background-color: #e2e8f0; color: #0f172a; padding: 6px; border: 1px solid #cbd5e1; text-align: left; }"
        "QGroupBox { border: 1px solid #cbd5e1; border-radius: 5px; margin-top: 10px; padding-top: 10px; color: #0f172a; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #1d4ed8; }"
        "QTableWidget, QListWidget, QTreeView { background-color: #ffffff; alternate-background-color: #f8fafc; color: #0f172a; gridline-color: #e2e8f0; border: 1px solid #cbd5e1; selection-background-color: #2563eb; selection-color: #ffffff; }"
        "QHeaderView::section { background-color: #e2e8f0; color: #1e293b; padding: 4px; border: 1px solid #cbd5e1; font-weight: bold; }"
        "QTabWidget::pane { border: 1px solid #cbd5e1; background-color: #eef2f7; }"
        "QTabBar::tab { background-color: #e2e8f0; color: #334155; padding: 6px 14px; border: 1px solid #cbd5e1; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; }"
        "QTabBar::tab:selected { background-color: #2563eb; color: #ffffff; border-color: #1d4ed8; font-weight: bold; }"
        "QPushButton { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 5px 12px; }"
        "QPushButton:hover { background-color: #e2e8f0; border-color: #94a3b8; }"
        "QPushButton:pressed { background-color: #cbd5e1; }"
        "QPushButton:disabled { color: #94a3b8; background-color: #f1f5f9; border-color: #e2e8f0; }"
        "QLineEdit, QTextEdit, QPlainTextEdit { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 3px 6px; selection-background-color: #2563eb; selection-color: #ffffff; }"
        "QLineEdit:focus { border-color: #2563eb; }"
        "QSpinBox QLineEdit, QDoubleSpinBox QLineEdit { background: transparent; border: none; padding: 0px; }"
        "QComboBox QAbstractItemView { background-color: #ffffff; color: #0f172a; selection-background-color: #2563eb; selection-color: #ffffff; border: 1px solid #cbd5e1; }"
        "QStatusBar { background-color: #f8fafc; color: #334155; border-top: 1px solid #cbd5e1; }"
        "QToolTip { background-color: #ffffff; color: #0f172a; border: 1px solid #94a3b8; padding: 4px 6px; }"
        "QSplitter::handle { background-color: #cbd5e1; }"
    );
}

#ifdef Q_OS_WIN
void setWindowsImmersiveDarkMode(QWidget* widget, bool dark) {
    if (!widget || !widget->isWindow()) return;
    const WId wid = widget->internalWinId();
    if (wid == 0) return;

    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetWindowAttributeFn fn = []() -> DwmSetWindowAttributeFn {
        QLibrary lib(QStringLiteral("dwmapi"));
        return reinterpret_cast<DwmSetWindowAttributeFn>(lib.resolve("DwmSetWindowAttribute"));
    }();
    if (!fn) return;

    HWND hwnd = reinterpret_cast<HWND>(wid);
    const BOOL value = dark ? TRUE : FALSE;
    // Attribute 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 11 / Win10 20H1+), fallback 19 for earlier builds
    constexpr DWORD attrModern = 20;
    constexpr DWORD attrLegacy = 19;
    if (FAILED(fn(hwnd, attrModern, &value, sizeof(value)))) {
        fn(hwnd, attrLegacy, &value, sizeof(value));
    }
}
#endif

/**
 * Custom Fusion proxy style that draws QComboBox and QSpinBox/QDoubleSpinBox
 * frames, buttons, and arrows directly via QPainter without PNG assets.
 */
class OmniFrameFusionStyle final : public QProxyStyle {
public:
    explicit OmniFrameFusionStyle(bool lightTheme)
        : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
        , m_lightTheme(lightTheme) {}

    int styleHint(StyleHint hint,
                  const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override {
        if (hint == SH_ComboBox_Popup) {
            return 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    QSize sizeFromContents(ContentsType type,
                           const QStyleOption* option,
                           const QSize& size,
                           const QWidget* widget) const override {
        QSize sz = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_ComboBox || type == CT_SpinBox) {
            sz.setHeight(qMax(sz.height(), 28));
        }
        return sz;
    }

    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget) const override {
        if ((element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp ||
             element == PE_IndicatorSpinDown || element == PE_IndicatorSpinUp) &&
            option && painter && !option->rect.isEmpty()) {
            const bool pointUp = (element == PE_IndicatorArrowUp || element == PE_IndicatorSpinUp);
            const bool enabled = option->state.testFlag(QStyle::State_Enabled);
            const bool active = option->state.testFlag(QStyle::State_Sunken) ||
                                option->state.testFlag(QStyle::State_On);
            drawTriangle(painter, QRectF(option->rect), pointUp, resolveArrowColor(enabled, active));
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawComplexControl(ComplexControl control,
                            const QStyleOptionComplex* option,
                            QPainter* painter,
                            const QWidget* widget) const override {
        if (control == CC_ComboBox && option && painter) {
            const auto* cb = qstyleoption_cast<const QStyleOptionComboBox*>(option);
            if (cb && cb->frame) {
                const bool enabled = cb->state.testFlag(State_Enabled);
                const bool focusedOrOpen = enabled && (cb->state.testFlag(State_HasFocus) || cb->state.testFlag(State_On));
                const bool hovered = enabled && cb->state.testFlag(State_MouseOver);
                const bool pressed = enabled && (cb->state.testFlag(State_Sunken) || cb->state.testFlag(State_On));

                drawControlFrame(painter, cb->rect, enabled, focusedOrOpen, hovered);

                if (cb->subControls.testFlag(SC_ComboBoxArrow)) {
                    QRect arrowRect = subControlRect(CC_ComboBox, cb, SC_ComboBoxArrow, widget);
                    if (arrowRect.width() < 20 && cb->rect.width() > 36) {
                        arrowRect = QRect(cb->rect.right() - 21, cb->rect.top() + 1, 21, cb->rect.height() - 2);
                    }
                    const QColor bg = resolveButtonBg(enabled, pressed, hovered);
                    const QColor sep = resolveSeparatorColor(enabled, pressed, hovered);

                    painter->save();
                    painter->setRenderHint(QPainter::Antialiasing, true);
                    QPainterPath clipPath;
                    clipPath.addRoundedRect(QRectF(cb->rect).adjusted(1.0, 1.0, -1.0, -1.0), 3.0, 3.0);
                    painter->setClipPath(clipPath);
                    painter->fillRect(arrowRect, bg);
                    painter->setPen(sep);
                    painter->drawLine(arrowRect.topLeft(), arrowRect.bottomLeft());
                    painter->restore();

                    drawTriangle(painter, QRectF(arrowRect), false, resolveArrowColor(enabled, pressed));
                }
                return;
            }
        }

        if (control == CC_SpinBox && option && painter) {
            const auto* sb = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
            if (sb && sb->frame) {
                const bool enabled = sb->state.testFlag(State_Enabled);
                const bool focused = enabled && sb->state.testFlag(State_HasFocus);
                const bool hovered = enabled && sb->state.testFlag(State_MouseOver);

                drawControlFrame(painter, sb->rect, enabled, focused, hovered);

                if (sb->buttonSymbols != QAbstractSpinBox::NoButtons) {
                    const QRect upRect = subControlRect(CC_SpinBox, sb, SC_SpinBoxUp, widget);
                    const QRect downRect = subControlRect(CC_SpinBox, sb, SC_SpinBoxDown, widget);

                    const bool upEnabled = enabled && sb->stepEnabled.testFlag(QAbstractSpinBox::StepUpEnabled);
                    const bool downEnabled = enabled && sb->stepEnabled.testFlag(QAbstractSpinBox::StepDownEnabled);
                    const bool upHot = upEnabled && sb->activeSubControls.testFlag(SC_SpinBoxUp);
                    const bool downHot = downEnabled && sb->activeSubControls.testFlag(SC_SpinBoxDown);
                    const bool upPressed = upHot && sb->state.testFlag(State_Sunken);
                    const bool downPressed = downHot && sb->state.testFlag(State_Sunken);
                    const bool upHovered = upHot && sb->state.testFlag(State_MouseOver);
                    const bool downHovered = downHot && sb->state.testFlag(State_MouseOver);

                    painter->save();
                    painter->setRenderHint(QPainter::Antialiasing, true);
                    QPainterPath clipPath;
                    clipPath.addRoundedRect(QRectF(sb->rect).adjusted(1.0, 1.0, -1.0, -1.0), 3.0, 3.0);
                    painter->setClipPath(clipPath);

                    if (sb->subControls.testFlag(SC_SpinBoxUp) && upRect.isValid()) {
                        painter->fillRect(upRect, resolveButtonBg(upEnabled, upPressed, upHovered));
                        painter->setPen(resolveSeparatorColor(upEnabled, upPressed, upHovered));
                        painter->drawLine(upRect.topLeft(), upRect.bottomLeft());
                    }
                    if (sb->subControls.testFlag(SC_SpinBoxDown) && downRect.isValid()) {
                        painter->fillRect(downRect, resolveButtonBg(downEnabled, downPressed, downHovered));
                        painter->setPen(resolveSeparatorColor(downEnabled, downPressed, downHovered));
                        painter->drawLine(downRect.topLeft(), downRect.bottomLeft());
                    }
                    if (upRect.isValid() && downRect.isValid()) {
                        painter->setPen(m_lightTheme ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x3b, 0x47, 0x58));
                        painter->drawLine(upRect.bottomLeft(), upRect.bottomRight());
                    }
                    painter->restore();

                    if (sb->subControls.testFlag(SC_SpinBoxUp) && upRect.isValid()) {
                        drawTriangle(painter, QRectF(upRect), true, resolveArrowColor(upEnabled, upPressed));
                    }
                    if (sb->subControls.testFlag(SC_SpinBoxDown) && downRect.isValid()) {
                        drawTriangle(painter, QRectF(downRect), false, resolveArrowColor(downEnabled, downPressed));
                    }
                }
                return;
            }
        }

        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }

private:
    QColor resolveArrowColor(bool enabled, bool active) const {
        if (!enabled) {
            return m_lightTheme ? QColor(0x94, 0xa3, 0xb8) : QColor(0x64, 0x74, 0x8b);
        }
        if (active) {
            return Qt::white;
        }
        return m_lightTheme ? QColor(0x1e, 0x29, 0x3b) : QColor(0xe2, 0xe8, 0xf0);
    }

    QColor resolveButtonBg(bool enabled, bool pressed, bool hovered) const {
        if (!enabled) {
            return m_lightTheme ? QColor(0xf1, 0xf5, 0xf9) : QColor(0x1c, 0x22, 0x2b);
        }
        if (pressed) {
            return m_lightTheme ? QColor(0x25, 0x63, 0xeb) : QColor(0x2b, 0x6c, 0xb0);
        }
        if (hovered) {
            return m_lightTheme ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x36, 0x43, 0x54);
        }
        return m_lightTheme ? QColor(0xe2, 0xe8, 0xf0) : QColor(0x28, 0x31, 0x3e);
    }

    QColor resolveSeparatorColor(bool enabled, bool pressed, bool hovered) const {
        if (!enabled) {
            return m_lightTheme ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x2d, 0x37, 0x48);
        }
        if (pressed) {
            return m_lightTheme ? QColor(0x1d, 0x4e, 0xd8) : QColor(0x3b, 0x82, 0xf6);
        }
        if (hovered) {
            return QColor(0x64, 0x74, 0x8b);
        }
        return m_lightTheme ? QColor(0x94, 0xa3, 0xb8) : QColor(0x47, 0x55, 0x69);
    }

    void drawControlFrame(QPainter* painter, const QRect& rect, bool enabled, bool focused, bool hovered) const {
        QColor bg;
        QColor border;
        if (!enabled) {
            bg = m_lightTheme ? QColor(0xf8, 0xfa, 0xfc) : QColor(0x18, 0x1c, 0x23);
            border = m_lightTheme ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x2d, 0x37, 0x48);
        } else {
            bg = m_lightTheme ? QColor(0xff, 0xff, 0xff) : QColor(0x14, 0x17, 0x1c);
            if (focused) {
                border = m_lightTheme ? QColor(0x25, 0x63, 0xeb) : QColor(0x3b, 0x82, 0xf6);
            } else if (hovered) {
                border = QColor(0x64, 0x74, 0x8b);
            } else {
                border = m_lightTheme ? QColor(0x94, 0xa3, 0xb8) : QColor(0x47, 0x55, 0x69);
            }
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(border);
        painter->setBrush(bg);
        painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
        painter->restore();
    }

    static void drawTriangle(QPainter* painter, const QRectF& rect, bool pointUp, const QColor& color) {
        const qreal triW = qBound(6.0, rect.width() * 0.45, 9.0);
        const qreal triH = qBound(4.0, rect.height() * 0.40, 5.0);
        const qreal cx = rect.center().x();
        const qreal cy = rect.center().y();

        QPolygonF tri;
        if (pointUp) {
            tri << QPointF(cx, cy - triH * 0.5)
                << QPointF(cx + triW * 0.5, cy + triH * 0.5)
                << QPointF(cx - triW * 0.5, cy + triH * 0.5);
        } else {
            tri << QPointF(cx - triW * 0.5, cy - triH * 0.5)
                << QPointF(cx + triW * 0.5, cy - triH * 0.5)
                << QPointF(cx, cy + triH * 0.5);
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawPolygon(tri);
        painter->restore();
    }

    bool m_lightTheme = false;
};

} // namespace

ThemeManager& ThemeManager::instance() {
    static ThemeManager mgr;
    return mgr;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent) {
}

void ThemeManager::inicializar() {
    if (!m_initialized) {
        loadSavedTheme();
        m_initialized = true;
    }
    applyApplicationStyle();
    applyAllTopLevelWindowChrome();
}

void ThemeManager::setTheme(UiTheme theme) {
    const bool changed = (!m_initialized || m_theme != theme);
    m_theme = theme;
    m_initialized = true;
    saveTheme();

    applyApplicationStyle();
    applyAllTopLevelWindowChrome();

    if (changed) {
        emit themeChanged(m_theme);
    }
}

void ThemeManager::applyWindowChrome(QWidget* widget) const {
#ifdef Q_OS_WIN
    setWindowsImmersiveDarkMode(widget, isDark());
#else
    Q_UNUSED(widget);
#endif
}

void ThemeManager::applyAllTopLevelWindowChrome() const {
    if (!qApp) return;
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* w : topLevels) {
        applyWindowChrome(w);
    }
}

void ThemeManager::loadSavedTheme() {
    const QString path = themeSettingsFilePath();
    if (!QFile::exists(path)) {
        m_theme = UiTheme::Dark;
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_theme = UiTheme::Dark;
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        m_theme = UiTheme::Dark;
        return;
    }

    const QString value = doc.object().value(QStringLiteral("theme")).toString().trimmed().toLower();
    m_theme = (value == QStringLiteral("light")) ? UiTheme::Light : UiTheme::Dark;
}

void ThemeManager::saveTheme() const {
    const QString path = themeSettingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject obj;
    obj.insert(QStringLiteral("theme"), isLight() ? QStringLiteral("light") : QStringLiteral("dark"));

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

void ThemeManager::applyApplicationStyle() const {
    if (!qApp) return;
    QApplication::setStyle(new OmniFrameFusionStyle(isLight()));
    QApplication::setPalette(isLight() ? buildLightPalette() : buildDarkPalette());
    qApp->setStyleSheet(isLight() ? buildLightApplicationStyleSheet() : buildDarkApplicationStyleSheet());
}

} // namespace omniframe

