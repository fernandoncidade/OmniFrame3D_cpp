#include "ThemeManager.hpp"
#include "io/UserPreferencesStore.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonObject>
#include <QLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>
#include <QStyleOptionButton>
#include <QStyleOptionToolButton>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVector>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace omniframe {

namespace {

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
        "QToolBar QToolButton#qt_toolbar_ext_button { background-color: #28313e; color: #e2e8f0; border: 1px solid #475569; border-radius: 3px; padding: 0px; margin: 0px; min-width: 22px; }"
        "QToolBar QToolButton#qt_toolbar_ext_button:hover { background-color: #364354; border-color: #64748b; }"
        "QToolBar QToolButton#qt_toolbar_ext_button:checked, QToolBar QToolButton#qt_toolbar_ext_button:pressed { background-color: #2b6cb0; border-color: #3b82f6; color: #ffffff; }"
        "QDockWidget { titlebar-close-icon: url(); titlebar-normal-icon: url(); color: #e1e6eb; font-weight: bold; }"
        "QDockWidget::title { background-color: #23272f; color: #e1e6eb; padding: 6px; border: 1px solid #2d333f; text-align: left; }"
        "QFrame#dockTitleBar { background-color: #23272f; border: 1px solid #2d333f; }"
        "QLabel#dockTitleLabel { background: transparent; color: #e1e6eb; font-weight: bold; }"
        "QFrame#dockTitleBar QToolButton { background: transparent; border: none; padding: 0px; }"
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
        "QToolBar QToolButton#qt_toolbar_ext_button { background-color: #e2e8f0; color: #1e293b; border: 1px solid #94a3b8; border-radius: 3px; padding: 0px; margin: 0px; min-width: 22px; }"
        "QToolBar QToolButton#qt_toolbar_ext_button:hover { background-color: #cbd5e1; border-color: #64748b; }"
        "QToolBar QToolButton#qt_toolbar_ext_button:checked, QToolBar QToolButton#qt_toolbar_ext_button:pressed { background-color: #2563eb; border-color: #1d4ed8; color: #ffffff; }"
        "QDockWidget { titlebar-close-icon: url(); titlebar-normal-icon: url(); color: #0f172a; font-weight: bold; }"
        "QDockWidget::title { background-color: #e2e8f0; color: #0f172a; padding: 6px; border: 1px solid #cbd5e1; text-align: left; }"
        "QFrame#dockTitleBar { background-color: #e2e8f0; border: 1px solid #cbd5e1; }"
        "QLabel#dockTitleLabel { background: transparent; color: #0f172a; font-weight: bold; }"
        "QFrame#dockTitleBar QToolButton { background: transparent; border: none; padding: 0px; }"
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
bool isDwmEligibleWindow(const QWidget* widget) {
    if (!widget || !widget->isWindow()) return false;
    const Qt::WindowType type = widget->windowType();
    if (type == Qt::Popup || type == Qt::ToolTip || type == Qt::SplashScreen) {
        return false;
    }
    if (widget->testAttribute(Qt::WA_DontShowOnScreen)) return false;
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) {
        return false;
    }
    return widget->internalWinId() != 0;
}

bool setDwmBoolAttribute(QWidget* widget, DWORD attribute, bool enabled) {
    if (!isDwmEligibleWindow(widget)) return false;

    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSetWindowAttributeFn fn = []() -> DwmSetWindowAttributeFn {
        HMODULE mod = ::GetModuleHandleW(L"dwmapi.dll");
        if (!mod) {
            mod = ::LoadLibraryW(L"dwmapi.dll");
        }
        if (!mod) {
            return nullptr;
        }
        return reinterpret_cast<DwmSetWindowAttributeFn>(::GetProcAddress(mod, "DwmSetWindowAttribute"));
    }();
    if (!fn) return false;

    HWND hwnd = reinterpret_cast<HWND>(widget->internalWinId());
    const BOOL value = enabled ? TRUE : FALSE;
    return SUCCEEDED(fn(hwnd, attribute, &value, sizeof(value)));
}

void setWindowsImmersiveDarkMode(QWidget* widget, bool dark) {
    // Attribute 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 11 / Win10 20H1+), fallback 19 for earlier builds
    constexpr DWORD attrModern = 20;
    constexpr DWORD attrLegacy = 19;
    if (!setDwmBoolAttribute(widget, attrModern, dark)) {
        setDwmBoolAttribute(widget, attrLegacy, dark);
    }
}
#endif

QColor resolveSpinArrowColor(bool lightTheme, bool enabled, bool active) {
    if (!enabled) {
        return lightTheme ? QColor(0x94, 0xa3, 0xb8) : QColor(0x64, 0x74, 0x8b);
    }
    if (active) {
        return Qt::white;
    }
    return lightTheme ? QColor(0x1e, 0x29, 0x3b) : QColor(0xe2, 0xe8, 0xf0);
}

QColor resolveSpinButtonBg(bool lightTheme, bool enabled, bool pressed, bool hovered) {
    if (!enabled) {
        return lightTheme ? QColor(0xf1, 0xf5, 0xf9) : QColor(0x1c, 0x22, 0x2b);
    }
    if (pressed) {
        return lightTheme ? QColor(0x25, 0x63, 0xeb) : QColor(0x2b, 0x6c, 0xb0);
    }
    if (hovered) {
        return lightTheme ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x36, 0x43, 0x54);
    }
    return lightTheme ? QColor(0xe2, 0xe8, 0xf0) : QColor(0x28, 0x31, 0x3e);
}

QColor resolveSpinSeparatorColor(bool lightTheme, bool enabled, bool pressed, bool hovered) {
    if (!enabled) {
        return lightTheme ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x2d, 0x37, 0x48);
    }
    if (pressed) {
        return lightTheme ? QColor(0x1d, 0x4e, 0xd8) : QColor(0x3b, 0x82, 0xf6);
    }
    if (hovered) {
        return QColor(0x64, 0x74, 0x8b);
    }
    return lightTheme ? QColor(0x94, 0xa3, 0xb8) : QColor(0x47, 0x55, 0x69);
}

void drawSpinTriangle(QPainter* painter, const QRectF& rect, Qt::ArrowType arrowType, const QColor& color) {
    if (!painter || rect.isEmpty() || arrowType == Qt::NoArrow) {
        return;
    }

    const qreal cx = rect.center().x();
    const qreal cy = rect.center().y();
    QPolygonF tri;

    if (arrowType == Qt::LeftArrow || arrowType == Qt::RightArrow) {
        const qreal baseSpan = qBound(6.0, qMin(rect.width(), rect.height()) * 0.45, 9.0);
        const qreal depth = qBound(4.0, rect.width() * 0.35, 5.0);
        if (arrowType == Qt::LeftArrow) {
            tri << QPointF(cx - depth * 0.5, cy)
                << QPointF(cx + depth * 0.5, cy - baseSpan * 0.5)
                << QPointF(cx + depth * 0.5, cy + baseSpan * 0.5);
        } else {
            tri << QPointF(cx - depth * 0.5, cy - baseSpan * 0.5)
                << QPointF(cx + depth * 0.5, cy)
                << QPointF(cx - depth * 0.5, cy + baseSpan * 0.5);
        }
    } else {
        const qreal triW = qBound(6.0, rect.width() * 0.45, 9.0);
        const qreal triH = qBound(4.0, rect.height() * 0.40, 5.0);
        if (arrowType == Qt::UpArrow) {
            tri << QPointF(cx, cy - triH * 0.5)
                << QPointF(cx + triW * 0.5, cy + triH * 0.5)
                << QPointF(cx - triW * 0.5, cy + triH * 0.5);
        } else {
            tri << QPointF(cx - triW * 0.5, cy - triH * 0.5)
                << QPointF(cx + triW * 0.5, cy - triH * 0.5)
                << QPointF(cx, cy + triH * 0.5);
        }
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(tri);
    painter->restore();
}

QColor resolveToolbarExtensionArrowColor(bool lightTheme, bool nativeUi, bool enabled, bool active) {
    if (!enabled) {
        return lightTheme ? QColor(0x94, 0xa3, 0xb8) : QColor(0x64, 0x74, 0x8b);
    }
    if (!nativeUi && active) {
        return Qt::white;
    }
    return lightTheme ? QColor(0x1e, 0x29, 0x3b) : QColor(0xe2, 0xe8, 0xf0);
}

QIcon buildToolbarExtensionArrowIcon(bool lightTheme, bool nativeUi) {
    QIcon icon;
    auto addStatePixmap = [&](Qt::ArrowType arrow, const QColor& color, QIcon::Mode mode, QIcon::State state) {
        for (int dpr : {1, 2}) {
            const int logicalSize = 12;
            QPixmap pm(logicalSize * dpr, logicalSize * dpr);
            pm.setDevicePixelRatio(static_cast<qreal>(dpr));
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            drawSpinTriangle(&p, QRectF(0.0, 0.0, logicalSize, logicalSize), arrow, color);
            icon.addPixmap(pm, mode, state);
        }
    };

    const QColor normalCol = resolveToolbarExtensionArrowColor(lightTheme, nativeUi, true, false);
    const QColor activeCol = resolveToolbarExtensionArrowColor(lightTheme, nativeUi, true, true);
    const QColor disabledCol = resolveToolbarExtensionArrowColor(lightTheme, nativeUi, false, false);

    addStatePixmap(Qt::DownArrow, normalCol, QIcon::Normal, QIcon::Off);
    addStatePixmap(Qt::DownArrow, normalCol, QIcon::Active, QIcon::Off);
    addStatePixmap(Qt::DownArrow, disabledCol, QIcon::Disabled, QIcon::Off);
    addStatePixmap(Qt::UpArrow, activeCol, QIcon::Normal, QIcon::On);
    addStatePixmap(Qt::UpArrow, activeCol, QIcon::Active, QIcon::On);
    addStatePixmap(Qt::UpArrow, disabledCol, QIcon::Disabled, QIcon::On);
    return icon;
}

class OmniFrameFusionStyle final : public QProxyStyle {
public:
    explicit OmniFrameFusionStyle(bool lightTheme)
        : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
        , m_lightTheme(lightTheme) {
        setObjectName(QStringLiteral("fusion"));
    }

    int pixelMetric(PixelMetric metric,
                    const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override {
        const int base = QProxyStyle::pixelMetric(metric, option, widget);
        if (metric == PM_ToolBarExtensionExtent) {
            return qMax(base, 22);
        }
        return base;
    }

    QIcon standardIcon(StandardPixmap standardIcon,
                       const QStyleOption* option = nullptr,
                       const QWidget* widget = nullptr) const override {
        if (standardIcon == SP_ToolBarHorizontalExtensionButton ||
            standardIcon == SP_ToolBarVerticalExtensionButton) {
            return buildToolbarExtensionArrowIcon(m_lightTheme, false);
        }
        return QProxyStyle::standardIcon(standardIcon, option, widget);
    }

    QPixmap standardPixmap(StandardPixmap standardPixmap,
                           const QStyleOption* option = nullptr,
                           const QWidget* widget = nullptr) const override {
        if (standardPixmap == SP_ToolBarHorizontalExtensionButton ||
            standardPixmap == SP_ToolBarVerticalExtensionButton) {
            return buildToolbarExtensionArrowIcon(m_lightTheme, false).pixmap(QSize(12, 12));
        }
        return QProxyStyle::standardPixmap(standardPixmap, option, widget);
    }

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

    QRect subControlRect(ComplexControl control,
                         const QStyleOptionComplex* option,
                         SubControl subControl,
                         const QWidget* widget) const override {
        if (control == CC_SpinBox && option) {
            const auto* sb = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
            if (sb && sb->frame && sb->buttonSymbols != QAbstractSpinBox::NoButtons &&
                sb->rect.width() > 36 && sb->rect.height() >= 16) {
                const QRect inner = sb->rect.adjusted(1, 1, -1, -1);
                const QRect baseUp = QProxyStyle::subControlRect(CC_SpinBox, sb, SC_SpinBoxUp, widget);
                const QRect baseDown = QProxyStyle::subControlRect(CC_SpinBox, sb, SC_SpinBoxDown, widget);
                const int rawLeft = (baseUp.isValid() && baseDown.isValid())
                    ? qMin(baseUp.left(), baseDown.left())
                    : (inner.right() - 18);
                const int btnWidth = qBound(18, inner.right() - rawLeft + 1, 22);
                const int btnLeft = inner.right() - btnWidth + 1;
                const int upHeight = inner.height() / 2;

                if (subControl == SC_SpinBoxUp) {
                    return visualRect(sb->direction, sb->rect,
                                      QRect(btnLeft, inner.top(), btnWidth, upHeight));
                }
                if (subControl == SC_SpinBoxDown) {
                    return visualRect(sb->direction, sb->rect,
                                      QRect(btnLeft, inner.top() + upHeight, btnWidth, inner.height() - upHeight));
                }
                if (subControl == SC_SpinBoxEditField) {
                    QRect editRect = QProxyStyle::subControlRect(CC_SpinBox, sb, SC_SpinBoxEditField, widget);
                    if (editRect.isValid()) {
                        if (sb->direction == Qt::LeftToRight) {
                            editRect.setRight(qMin(editRect.right(), btnLeft - 2));
                        } else {
                            const int mirrorRight = sb->rect.left() + btnWidth + 2;
                            editRect.setLeft(qMax(editRect.left(), mirrorRight));
                        }
                    }
                    return editRect;
                }
            }
        }
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }

    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget) const override {
        if ((element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp ||
             element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight ||
             element == PE_IndicatorSpinDown || element == PE_IndicatorSpinUp) &&
            option && painter && !option->rect.isEmpty()) {
            Qt::ArrowType arrowDir = Qt::DownArrow;
            if (element == PE_IndicatorArrowUp || element == PE_IndicatorSpinUp) {
                arrowDir = Qt::UpArrow;
            } else if (element == PE_IndicatorArrowLeft) {
                arrowDir = Qt::LeftArrow;
            } else if (element == PE_IndicatorArrowRight) {
                arrowDir = Qt::RightArrow;
            }
            const bool enabled = option->state.testFlag(QStyle::State_Enabled);
            const bool active = option->state.testFlag(QStyle::State_Sunken) ||
                                option->state.testFlag(QStyle::State_On);
            drawSpinTriangle(painter, QRectF(option->rect), arrowDir, resolveSpinArrowColor(m_lightTheme, enabled, active));
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
                    const QColor bg = resolveSpinButtonBg(m_lightTheme, enabled, pressed, hovered);
                    const QColor sep = resolveSpinSeparatorColor(m_lightTheme, enabled, pressed, hovered);

                    painter->save();
                    painter->setRenderHint(QPainter::Antialiasing, true);
                    painter->setClipPath(innerControlClipPath(cb->rect));
                    painter->fillRect(arrowRect, bg);
                    painter->setPen(sep);
                    painter->drawLine(QLineF(arrowRect.left() + 0.5, arrowRect.top(),
                                             arrowRect.left() + 0.5, arrowRect.bottom() + 1.0));
                    painter->restore();

                    drawSpinTriangle(painter, QRectF(arrowRect), Qt::DownArrow, resolveSpinArrowColor(m_lightTheme, enabled, pressed));
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
                    painter->setClipPath(innerControlClipPath(sb->rect));

                    if (sb->subControls.testFlag(SC_SpinBoxUp) && upRect.isValid()) {
                        painter->fillRect(upRect, resolveSpinButtonBg(m_lightTheme, upEnabled, upPressed, upHovered));
                        painter->setPen(resolveSpinSeparatorColor(m_lightTheme, upEnabled, upPressed, upHovered));
                        painter->drawLine(QLineF(upRect.left() + 0.5, upRect.top(),
                                                 upRect.left() + 0.5, upRect.bottom() + 1.0));
                    }
                    if (sb->subControls.testFlag(SC_SpinBoxDown) && downRect.isValid()) {
                        painter->fillRect(downRect, resolveSpinButtonBg(m_lightTheme, downEnabled, downPressed, downHovered));
                        painter->setPen(resolveSpinSeparatorColor(m_lightTheme, downEnabled, downPressed, downHovered));
                        painter->drawLine(QLineF(downRect.left() + 0.5, downRect.top(),
                                                 downRect.left() + 0.5, downRect.bottom() + 1.0));
                    }
                    if (upRect.isValid() && downRect.isValid()) {
                        painter->setPen(m_lightTheme ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x3b, 0x47, 0x58));
                        painter->drawLine(QLineF(upRect.left() + 1.0, downRect.top(),
                                                 upRect.right() + 1.0, downRect.top()));
                    }
                    painter->restore();

                    if (sb->subControls.testFlag(SC_SpinBoxUp) && upRect.isValid()) {
                        drawSpinTriangle(painter, QRectF(upRect), Qt::UpArrow, resolveSpinArrowColor(m_lightTheme, upEnabled, upPressed));
                    }
                    if (sb->subControls.testFlag(SC_SpinBoxDown) && downRect.isValid()) {
                        drawSpinTriangle(painter, QRectF(downRect), Qt::DownArrow, resolveSpinArrowColor(m_lightTheme, downEnabled, downPressed));
                    }
                }
                return;
            }
        }

        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }

private:
    static QPainterPath innerControlClipPath(const QRect& rect) {
        QPainterPath path;
        path.addRoundedRect(QRectF(rect).adjusted(1.0, 1.0, -1.0, -1.0), 3.5, 3.5);
        return path;
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

    bool m_lightTheme = false;
};

constexpr int kNativeStandardButtonHeight = 28;

QColor blendNativeColor(const QColor& base, const QColor& overlay, double ratio) {
    const double t = qBound(0.0, ratio, 1.0);
    const double inv = 1.0 - t;
    return QColor(
        qBound(0, static_cast<int>(base.red() * inv + overlay.red() * t + 0.5), 255),
        qBound(0, static_cast<int>(base.green() * inv + overlay.green() * t + 0.5), 255),
        qBound(0, static_cast<int>(base.blue() * inv + overlay.blue() * t + 0.5), 255),
        base.alpha());
}

bool isStandardNativeButton(const QWidget* widget) {
    if (!widget) {
        return false;
    }
    if (qobject_cast<const QPushButton*>(widget) != nullptr) {
        return true;
    }
    if (const auto* tb = qobject_cast<const QToolButton*>(widget)) {
        if (qobject_cast<const QTabBar*>(tb->parentWidget()) != nullptr) {
            return false;
        }
        if (tb->maximumHeight() < kNativeStandardButtonHeight) {
            return false;
        }
        const QString obj = tb->objectName();
        if (obj == QLatin1String("inspectorCollapseButton") ||
            obj == QLatin1String("resultsCollapseButton") ||
            obj == QLatin1String("qt_toolbar_ext_button")) {
            return false;
        }
        if (tb->arrowType() != Qt::NoArrow && tb->text().isEmpty()) {
            return false;
        }
        return true;
    }
    return false;
}

void drawNativeButtonFrame(QPainter* painter, const QStyleOption* option, bool isToolButton) {
    if (!painter || !option || option->rect.width() < 4 || option->rect.height() < 4) {
        return;
    }
    const QPalette& pal = option->palette;
    const bool enabled = option->state.testFlag(QStyle::State_Enabled);
    const bool checked = option->state.testFlag(QStyle::State_On);
    const bool sunken = option->state.testFlag(QStyle::State_Sunken);
    const bool hovered = enabled && option->state.testFlag(QStyle::State_MouseOver);
    const bool focused = enabled && option->state.testFlag(QStyle::State_HasFocus);

    const QPalette::ColorGroup cg = enabled ? QPalette::Active : QPalette::Disabled;
    const QColor baseBg = pal.color(cg, isToolButton ? QPalette::Window : QPalette::Button);
    const QColor fg = pal.color(cg, isToolButton ? QPalette::WindowText : QPalette::ButtonText);
    const QColor accent = pal.color(QPalette::Active, QPalette::Highlight);

    QColor borderColor;
    qreal penWidth = 1.0;
    if (!enabled) {
        borderColor = blendNativeColor(baseBg, fg, 0.18);
    } else if (sunken || checked) {
        borderColor = accent;
        penWidth = 1.25;
    } else if (focused || hovered) {
        borderColor = blendNativeColor(fg, accent, 0.72);
        penWidth = focused ? 1.25 : 1.0;
    } else {
        borderColor = blendNativeColor(baseBg, fg, 0.30);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(borderColor, penWidth));
    const qreal inset = 0.5 + (penWidth - 1.0) * 0.5;
    painter->drawRoundedRect(
        QRectF(option->rect).adjusted(inset, inset, -inset, -inset), 4.0, 4.0);
    painter->restore();
}

void drawNativeResizeHandleDots(const QStyleOption* option, QPainter* painter, bool lightTheme) {
    if (!option || !painter || option->rect.width() < 2 || option->rect.height() < 2) {
        return;
    }
    const QPalette& pal = option->palette;
    const bool enabled = option->state.testFlag(QStyle::State_Enabled);
    const bool hovered = enabled && option->state.testFlag(QStyle::State_MouseOver);
    const bool sunken = enabled && option->state.testFlag(QStyle::State_Sunken);

    const QColor winBg = pal.color(QPalette::Window);
    const QColor fg = pal.color(QPalette::WindowText);
    const QColor accent = pal.color(QPalette::Highlight);

    painter->save();
    if (sunken || hovered) {
        painter->fillRect(option->rect, blendNativeColor(winBg, accent, sunken ? 0.24 : 0.13));
    } else {
        painter->fillRect(option->rect, winBg);
    }

    const int lumContrast = std::abs(fg.lightness() - winBg.lightness());
    const QColor safeFg = (lumContrast >= 60)
        ? fg
        : (lightTheme ? QColor(0x1e, 0x29, 0x3b) : QColor(0xe2, 0xe8, 0xf0));
    const QColor dotColor = (sunken || hovered)
        ? blendNativeColor(safeFg, accent, 0.45)
        : blendNativeColor(winBg, safeFg, lightTheme ? 0.46 : 0.52);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dotColor);

    const bool horizontalBar = (option->rect.width() >= option->rect.height());
    const int span = horizontalBar ? option->rect.width() : option->rect.height();
    const qreal cx = option->rect.center().x() + 0.5;
    const qreal cy = option->rect.center().y() + 0.5;
    const int dotCount = (span >= 36) ? 5 : ((span >= 20) ? 3 : 1);
    const qreal step = 5.0;
    const qreal radius = 1.2;
    const qreal startOffset = -0.5 * (dotCount - 1) * step;

    for (int i = 0; i < dotCount; ++i) {
        const qreal offset = startOffset + i * step;
        const QPointF center = horizontalBar
            ? QPointF(cx + offset, cy)
            : QPointF(cx, cy + offset);
        painter->drawEllipse(center, radius, radius);
    }
    painter->restore();
}

class OmniFrameNativeStyle final : public QProxyStyle {
public:
    explicit OmniFrameNativeStyle(const QString& baseStyleName, bool lightTheme)
        : QProxyStyle(QStyleFactory::create(baseStyleName))
        , m_lightTheme(lightTheme) {
        if (baseStyle()) {
            setObjectName(baseStyle()->objectName());
        } else {
            setObjectName(baseStyleName);
        }
    }

    int pixelMetric(PixelMetric metric,
                    const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override {
        const int base = QProxyStyle::pixelMetric(metric, option, widget);
        if (metric == PM_ToolBarExtensionExtent) {
            return qMax(base, 22);
        }
        if (metric == PM_DockWidgetSeparatorExtent || metric == PM_SplitterWidth) {
            return qMax(base, 6);
        }
        return base;
    }

    QIcon standardIcon(StandardPixmap standardIcon,
                       const QStyleOption* option = nullptr,
                       const QWidget* widget = nullptr) const override {
        if (standardIcon == SP_ToolBarHorizontalExtensionButton ||
            standardIcon == SP_ToolBarVerticalExtensionButton) {
            return buildToolbarExtensionArrowIcon(m_lightTheme, true);
        }
        return QProxyStyle::standardIcon(standardIcon, option, widget);
    }

    QPixmap standardPixmap(StandardPixmap standardPixmap,
                           const QStyleOption* option = nullptr,
                           const QWidget* widget = nullptr) const override {
        if (standardPixmap == SP_ToolBarHorizontalExtensionButton ||
            standardPixmap == SP_ToolBarVerticalExtensionButton) {
            return buildToolbarExtensionArrowIcon(m_lightTheme, true).pixmap(QSize(12, 12));
        }
        return QProxyStyle::standardPixmap(standardPixmap, option, widget);
    }

    QSize sizeFromContents(ContentsType type,
                           const QStyleOption* option,
                           const QSize& size,
                           const QWidget* widget) const override {
        QSize sz = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_ToolButton && widget &&
            widget->objectName() == QLatin1String("qt_toolbar_ext_button")) {
            sz.setWidth(qMax(sz.width(), 22));
            sz.setHeight(qMax(sz.height(), 24));
            return sz;
        }
        if ((type == CT_PushButton || type == CT_ToolButton) && isStandardNativeButton(widget)) {
            bool multiline = false;
            if (const auto* ab = qobject_cast<const QAbstractButton*>(widget)) {
                multiline = ab->text().contains(QLatin1Char('\n'));
            }
            sz.setHeight(multiline ? qMax(sz.height(), kNativeStandardButtonHeight)
                                   : kNativeStandardButtonHeight);
        }
        return sz;
    }

    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget) const override {
        if (element == PE_IndicatorDockWidgetResizeHandle && option && painter) {
            drawNativeResizeHandleDots(option, painter, m_lightTheme);
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawControl(ControlElement element,
                     const QStyleOption* option,
                     QPainter* painter,
                     const QWidget* widget) const override {
        if (element == CE_Splitter && option && painter) {
            drawNativeResizeHandleDots(option, painter, m_lightTheme);
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
        if (element == CE_PushButtonBevel && option && painter && isStandardNativeButton(widget)) {
            drawNativeButtonFrame(painter, option, false);
        }
    }

    void drawComplexControl(ComplexControl control,
                            const QStyleOptionComplex* option,
                            QPainter* painter,
                            const QWidget* widget) const override {
        if (control == CC_ToolButton && option && painter && widget) {
            const QString obj = widget->objectName();
            const bool isToolbarExt = (obj == QLatin1String("qt_toolbar_ext_button"));
            const bool isDockCollapse = (obj == QLatin1String("inspectorCollapseButton") ||
                                         obj == QLatin1String("resultsCollapseButton"));
            if (isToolbarExt) {
                if (const auto* tb = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
                    QStyleOptionToolButton cleanOpt(*tb);
                    cleanOpt.icon = QIcon();
                    cleanOpt.text.clear();
                    cleanOpt.subControls = SC_ToolButton;
                    cleanOpt.features = QStyleOptionToolButton::Arrow;
                    cleanOpt.arrowType = tb->state.testFlag(State_On) ? Qt::UpArrow : Qt::DownArrow;
                    cleanOpt.toolButtonStyle = Qt::ToolButtonIconOnly;
                    cleanOpt.iconSize = QSize(16, 16);
                    QProxyStyle::drawComplexControl(control, &cleanOpt, painter, widget);
                    drawNativeButtonFrame(painter, option, true);
                    return;
                }
            }
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            if (isDockCollapse || isStandardNativeButton(widget)) {
                drawNativeButtonFrame(painter, option, true);
            }
            return;
        }

        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }

private:
    bool m_lightTheme = false;
};

class ToolbarExtensionPaintFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event && event->type() == QEvent::Paint &&
            !ThemeManager::instance().isNativeUi()) {
            if (auto* ext = qobject_cast<QToolButton*>(watched)) {
                QStyleOptionToolButton opt;
                opt.initFrom(ext);
                const bool enabled = ext->isEnabled();
                const bool hovered = enabled && (opt.state.testFlag(QStyle::State_MouseOver) || ext->underMouse());
                const bool pressed = enabled && (opt.state.testFlag(QStyle::State_Sunken) || ext->isDown() || ext->isChecked());
                const Qt::ArrowType arrow = ext->isChecked() ? Qt::UpArrow : Qt::DownArrow;
                QPainter painter(ext);
                ThemeManager::instance().paintSpinArrowButton(
                    &painter, ext->rect(), arrow, enabled, hovered, pressed);
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

ToolbarExtensionPaintFilter* toolbarExtensionPaintFilter() {
    static ToolbarExtensionPaintFilter filter;
    return &filter;
}

QVector<QPointer<QWidget>> collectWidgetsTopDown() {
    QVector<QPointer<QWidget>> safeWidgets;
    if (!qApp) {
        return safeWidgets;
    }
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* tlw : topLevels) {
        if (!tlw || tlw->windowType() == Qt::ToolTip) continue;
        safeWidgets.push_back(tlw);
        const auto children = tlw->findChildren<QWidget*>();
        for (QWidget* child : children) {
            if (child) {
                safeWidgets.push_back(child);
            }
        }
    }
    return safeWidgets;
}

void clearExplicitWidgetPalettes() {
    const QVector<QPointer<QWidget>> safeWidgets = collectWidgetsTopDown();
    for (const QPointer<QWidget>& w : safeWidgets) {
        if (!w) continue;
        w->setPalette(QPalette());
        w->setAttribute(Qt::WA_SetPalette, false);
    }
}

void syncToolbarExtensionButtons(const QVector<QPointer<QWidget>>& widgets, bool lightTheme, bool nativeUi) {
    const QIcon extIcon = buildToolbarExtensionArrowIcon(lightTheme, nativeUi);
    for (const QPointer<QWidget>& w : widgets) {
        if (!w) continue;
        if (auto* ext = qobject_cast<QToolButton*>(w.data());
            ext != nullptr && ext->objectName() == QLatin1String("qt_toolbar_ext_button")) {
            ext->setAttribute(Qt::WA_Hover, true);
            ext->setCursor(Qt::PointingHandCursor);
            ext->setIconSize(QSize(16, 16));
            ext->setMinimumWidth(22);
            ext->setIcon(extIcon);
            if (!ext->property("_omniframeExtPaintFilter").toBool()) {
                ext->setProperty("_omniframeExtPaintFilter", true);
                ext->installEventFilter(toolbarExtensionPaintFilter());
            }
            if (QWidget* tb = ext->parentWidget()) {
                if (QLayout* lay = tb->layout()) {
                    lay->invalidate();
                    lay->activate();
                }
            }
            ext->update();
        }
    }
}

void refreshLayoutsAndWidgetsAfterStyleChange(bool lightTheme, bool nativeUi) {
    const QVector<QPointer<QWidget>> safeWidgets = collectWidgetsTopDown();
    // Resolve palettes strictly parent-before-child against the final QApplication::palette()
    // before activating layouts so nested containers/scroll areas cannot hold stale scheme colors.
    for (const QPointer<QWidget>& w : safeWidgets) {
        if (!w) continue;
        w->setPalette(QPalette());
        w->setAttribute(Qt::WA_SetPalette, false);
    }
    syncToolbarExtensionButtons(safeWidgets, lightTheme, nativeUi);
    for (const QPointer<QWidget>& w : safeWidgets) {
        if (!w) continue;
        const bool isPopupOrMenu =
            (w->windowType() == Qt::Popup || qobject_cast<const QMenu*>(w.data()) != nullptr);
        if (!isPopupOrMenu) {
            if (QLayout* lay = w->layout()) {
                lay->invalidate();
                lay->activate();
            }
            w->updateGeometry();
        }
        w->update();
    }
}

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

void ThemeManager::setUiStyleMode(UiStyleMode mode) {
    const bool changed = (!m_initialized || m_uiStyleMode != mode);
    m_uiStyleMode = mode;
    m_initialized = true;
    saveTheme();

    applyApplicationStyle();
    applyAllTopLevelWindowChrome();

    if (changed) {
        emit uiStyleModeChanged(m_uiStyleMode);
    }
}

void ThemeManager::applyWindowChrome(QWidget* widget) const {
#ifdef Q_OS_WIN
    setWindowsImmersiveDarkMode(widget, isDark());
#else
    Q_UNUSED(widget);
#endif
}

void ThemeManager::setWindowTransitionsDisabled(QWidget* widget, bool disabled) const {
#ifdef Q_OS_WIN
    // Attribute 3 = DWMWA_TRANSITIONS_FORCEDISABLED
    setDwmBoolAttribute(widget, 3, disabled);
#else
    Q_UNUSED(widget);
    Q_UNUSED(disabled);
#endif
}

void ThemeManager::setWindowCloaked(QWidget* widget, bool cloaked) const {
#ifdef Q_OS_WIN
    // Attribute 13 = DWMWA_CLOAK
    setDwmBoolAttribute(widget, 13, cloaked);
#else
    Q_UNUSED(widget);
    Q_UNUSED(cloaked);
#endif
}

void ThemeManager::applyAllTopLevelWindowChrome() const {
    if (!qApp) return;
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* w : topLevels) {
        applyWindowChrome(w);
    }
}

void ThemeManager::refreshToolbarExtensionButtons() const {
    if (!qApp) return;
    syncToolbarExtensionButtons(collectWidgetsTopDown(), isLight(), isNativeUi());
}

bool ThemeManager::reloadSavedTheme() {
    bool exists = false;
    const QJsonObject appObj = io::UserPreferencesStore::readSection(QStringLiteral("app"), &exists);
    if (!exists || (!appObj.contains(QStringLiteral("theme")) &&
                    !appObj.contains(QStringLiteral("uiStyleMode")))) {
        return false;
    }

    UiTheme nextTheme = m_theme;
    bool hasValidTheme = false;
    if (appObj.contains(QStringLiteral("theme"))) {
        const QString value = appObj.value(QStringLiteral("theme")).toString().trimmed().toLower();
        if (value == QLatin1String("light")) {
            nextTheme = UiTheme::Light;
            hasValidTheme = true;
        } else if (value == QLatin1String("dark")) {
            nextTheme = UiTheme::Dark;
            hasValidTheme = true;
        }
    }

    UiStyleMode nextMode = m_uiStyleMode;
    bool hasValidMode = false;
    if (appObj.contains(QStringLiteral("uiStyleMode"))) {
        const QString modeVal = appObj.value(QStringLiteral("uiStyleMode")).toString().trimmed().toLower();
        if (modeVal == QLatin1String("native")) {
            nextMode = UiStyleMode::Native;
            hasValidMode = true;
        } else if (modeVal == QLatin1String("custom")) {
            nextMode = UiStyleMode::Custom;
            hasValidMode = true;
        }
    }

    if (!hasValidTheme && !hasValidMode) {
        return false;
    }

    const bool themeDidChange = (!m_initialized || (hasValidTheme && m_theme != nextTheme));
    const bool modeDidChange = (!m_initialized || (hasValidMode && m_uiStyleMode != nextMode));
    if (hasValidTheme) {
        m_theme = nextTheme;
    }
    if (hasValidMode) {
        m_uiStyleMode = nextMode;
    }
    m_initialized = true;

    if (themeDidChange || modeDidChange) {
        applyApplicationStyle();
        applyAllTopLevelWindowChrome();
        if (modeDidChange) {
            emit uiStyleModeChanged(m_uiStyleMode);
        }
        if (themeDidChange) {
            emit themeChanged(m_theme);
        }
    }
    return true;
}

void ThemeManager::loadSavedTheme() {
    bool exists = false;
    const QJsonObject appObj = io::UserPreferencesStore::readSection(QStringLiteral("app"), &exists);
    if (!exists) {
        m_theme = UiTheme::Dark;
        m_uiStyleMode = UiStyleMode::Custom;
        return;
    }

    const QString value = appObj.value(QStringLiteral("theme")).toString().trimmed().toLower();
    m_theme = (value == QStringLiteral("light")) ? UiTheme::Light : UiTheme::Dark;

    const QString modeVal = appObj.value(QStringLiteral("uiStyleMode")).toString().trimmed().toLower();
    m_uiStyleMode = (modeVal == QStringLiteral("native")) ? UiStyleMode::Native : UiStyleMode::Custom;
}

void ThemeManager::saveTheme() const {
    QJsonObject appObj = io::UserPreferencesStore::readSection(QStringLiteral("app"));
    appObj.insert(QStringLiteral("theme"), isLight() ? QStringLiteral("light") : QStringLiteral("dark"));
    appObj.insert(QStringLiteral("uiStyleMode"), isNativeUi() ? QStringLiteral("native") : QStringLiteral("custom"));
    io::UserPreferencesStore::updateSection(QStringLiteral("app"), appObj);
}

void ThemeManager::applyApplicationStyle() const {
    if (!qApp || m_applyingStyle) return;
    struct ApplyStyleGuard {
        bool& flag;
        explicit ApplyStyleGuard(bool& f) : flag(f) { flag = true; }
        ~ApplyStyleGuard() { flag = false; }
    } guard(m_applyingStyle);

    if (m_nativeStyleName.isEmpty() && !m_customStyleApplied && qApp->styleSheet().isEmpty() &&
        QApplication::style() &&
        dynamic_cast<const OmniFrameFusionStyle*>(QApplication::style()) == nullptr) {
        const QString currentName = QApplication::style()->objectName();
        if (!currentName.isEmpty() &&
            currentName.compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0) {
            m_nativeStyleName = currentName;
        }
    }

    if (isNativeUi()) {
        if (!qApp->styleSheet().isEmpty()) {
            qApp->setStyleSheet(QString());
        }
        clearExplicitWidgetPalettes();

        const QStringList keys = QStyleFactory::keys();
        QString targetStyle = m_nativeStyleName;
        if (targetStyle.isEmpty() || !keys.contains(targetStyle, Qt::CaseInsensitive)) {
            for (const QString& candidate : {QStringLiteral("windows11"), QStringLiteral("windowsvista"), QStringLiteral("windows")}) {
                if (keys.contains(candidate, Qt::CaseInsensitive)) {
                    targetStyle = candidate;
                    break;
                }
            }
        }
        if (!targetStyle.isEmpty()) {
            QApplication::setStyle(new OmniFrameNativeStyle(targetStyle, isLight()));
        }
        m_customStyleApplied = false;

        if (QStyleHints* hints = QGuiApplication::styleHints()) {
            const Qt::ColorScheme targetScheme =
                isLight() ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark;
            if (hints->colorScheme() == targetScheme) {
                hints->unsetColorScheme();
            }
            hints->setColorScheme(targetScheme);
        }

        QApplication::setPalette(QPalette());
        m_customPaletteApplied = false;

        refreshLayoutsAndWidgetsAfterStyleChange(isLight(), true);
        return;
    }

    if (!qApp->styleSheet().isEmpty()) {
        qApp->setStyleSheet(QString());
    }
    clearExplicitWidgetPalettes();
    if (QStyleHints* hints = QGuiApplication::styleHints()) {
        hints->setColorScheme(isLight() ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    }
    QApplication::setStyle(new OmniFrameFusionStyle(isLight()));
    m_customStyleApplied = true;
    QApplication::setPalette(isLight() ? buildLightPalette() : buildDarkPalette());
    m_customPaletteApplied = true;
    qApp->setStyleSheet(isLight() ? buildLightApplicationStyleSheet() : buildDarkApplicationStyleSheet());
    refreshLayoutsAndWidgetsAfterStyleChange(isLight(), false);
}

void ThemeManager::paintSpinArrowButton(QPainter* painter,
                                        const QRect& rect,
                                        Qt::ArrowType arrowType,
                                        bool enabled,
                                        bool hovered,
                                        bool pressed) const {
    if (!painter || rect.isEmpty()) {
        return;
    }

    if (isNativeUi()) {
        if (!qApp || !QApplication::style()) {
            return;
        }
        QStyleOptionToolButton opt;
        opt.rect = rect;
        opt.palette = QApplication::palette();
        opt.state = QStyle::State_AutoRaise;
        if (enabled) opt.state |= QStyle::State_Enabled;
        if (hovered) opt.state |= QStyle::State_MouseOver;
        if (pressed) opt.state |= QStyle::State_Sunken;
        opt.subControls = QStyle::SC_ToolButton;
        opt.features = QStyleOptionToolButton::Arrow;
        opt.arrowType = arrowType;
        opt.toolButtonStyle = Qt::ToolButtonIconOnly;

        QApplication::style()->drawComplexControl(QStyle::CC_ToolButton, &opt, painter);
        return;
    }

    const bool light = isLight();
    const QColor bg = resolveSpinButtonBg(light, enabled, pressed, hovered);
    const QColor border = resolveSpinSeparatorColor(light, enabled, pressed, hovered);
    const QColor arrow = resolveSpinArrowColor(light, enabled, pressed);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(border);
    painter->setBrush(bg);
    painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
    painter->restore();

    drawSpinTriangle(painter, QRectF(rect), arrowType, arrow);
}

} // namespace omniframe

