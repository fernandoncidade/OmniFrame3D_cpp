#include "MainWindow.hpp"
#include "AboutDialog.hpp"
#include "Dialogs.hpp"
#include "EngineeringDialogs.hpp"
#include "GuidedSessionDialog.hpp"
#include "ManualDialog.hpp"
#include "RenderSettingsDialog.hpp"
#include "ResultTableConfigurator.hpp"
#include "StartupSessionDialog.hpp"
#include "ThemeManager.hpp"
#include "WindowZOrderManager.hpp"
#include "io/AppLogger.hpp"
#include "io/DxfInterop.hpp"
#include "io/IfcInterop.hpp"
#include "io/PdfReportGenerator.hpp"
#include "io/ProjectRecoveryStore.hpp"
#include "io/ProjectSerializer.hpp"
#include "io/UserPreferencesStore.hpp"
#include "locale/GerenciadorTraducao.hpp"
#include "os/WindowsCliIntegration.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QProgressBar>
#include <QScreen>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QBoxLayout>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFrame>
#include <QMessageBox>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QDialog>
#include <QStyleOptionToolButton>
#include <QToolButton>

namespace omniframe {

namespace {

void updateToolbarExtensionToolTips(QToolBar* tb1, QToolBar* tb2) {
    const QString extTip = locale::GerenciadorTraducao::trText("Expandir ou recolher botões ocultos da barra");
    for (QToolBar* tb : {tb1, tb2}) {
        if (!tb) continue;
        if (auto* extBtn = tb->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"))) {
            extBtn->setToolTip(extTip);
        }
    }
}

QStringList resultTableHeaders(ui::ResultTableKind kind) {
    using locale::GerenciadorTraducao;
    switch (kind) {
    case ui::ResultTableKind::Displacements:
        return {
            GerenciadorTraducao::trText("Nó"),
            QStringLiteral("uX (mm)"),
            QStringLiteral("uY (mm)"),
            QStringLiteral("uZ (mm)"),
            QStringLiteral("θX (mrad)"),
            QStringLiteral("θY (mrad)"),
            QStringLiteral("θZ (mrad)")
        };
    case ui::ResultTableKind::Reactions:
        return {
            GerenciadorTraducao::trText("Nó"),
            QStringLiteral("Rx (kN)"),
            QStringLiteral("Ry (kN)"),
            QStringLiteral("Rz (kN)"),
            QStringLiteral("Mx (kNm)"),
            QStringLiteral("My (kNm)"),
            QStringLiteral("Mz (kNm)")
        };
    case ui::ResultTableKind::BarForces:
        return {
            GerenciadorTraducao::trText("Barra"),
            QStringLiteral("N min/max (kN)"),
            QStringLiteral("Vy min/max (kN)"),
            QStringLiteral("Vz min/max (kN)"),
            QStringLiteral("Tx min/max (kNm)"),
            QStringLiteral("My min/max (kNm)"),
            QStringLiteral("Mz min/max (kNm)"),
            GerenciadorTraducao::trText("Flecha (mm)")
        };
    case ui::ResultTableKind::ContinuumStress:
        return {
            GerenciadorTraducao::trText("Elemento"),
            GerenciadorTraducao::trText("Tipo"),
            QStringLiteral("σ_vM (kPa)"),
            QStringLiteral("σ_1 (kPa)"),
            QStringLiteral("σ_3 (kPa)"),
            QStringLiteral("Nxx / σxx"),
            QStringLiteral("Nyy / σyy"),
            QStringLiteral("Mxx / τxz")
        };
    }
    return {};
}

QString resultTableTabTitle(ui::ResultTableKind kind) {
    using locale::GerenciadorTraducao;
    switch (kind) {
    case ui::ResultTableKind::Displacements:
        return GerenciadorTraducao::trText("Deslocamentos Nodais");
    case ui::ResultTableKind::Reactions:
        return GerenciadorTraducao::trText("Reações de Apoio");
    case ui::ResultTableKind::BarForces:
        return GerenciadorTraducao::trText("Esforços nas Barras");
    case ui::ResultTableKind::ContinuumStress:
        return GerenciadorTraducao::trText("Esforços 2D/3D (Casca/Sólido)");
    }
    return {};
}

class PersistentLayoutTableMenu final : public QMenu {
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

/// Botão para recolher/expandir painéis dock (nativo Qt6 ou vetorial customizado conforme modo da UI).
class SpinArrowButton final : public QToolButton {
public:
    explicit SpinArrowButton(const QString& objectName, QWidget* parent = nullptr)
        : QToolButton(parent) {
        setObjectName(objectName);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setArrowType(Qt::DownArrow);
        syncUiMode();
    }

    void syncUiMode() {
        if (ThemeManager::instance().isNativeUi()) {
            setAutoRaise(true);
            setToolButtonStyle(Qt::ToolButtonIconOnly);
            setFixedSize(22, 22);
            setAttribute(Qt::WA_Hover, false);
        } else {
            setAutoRaise(false);
            setToolButtonStyle(Qt::ToolButtonIconOnly);
            setFixedSize(24, 20);
            setAttribute(Qt::WA_Hover, true);
        }
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (ThemeManager::instance().isNativeUi()) {
            QToolButton::paintEvent(event);
            return;
        }
        Q_UNUSED(event);
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        const bool enabled = isEnabled();
        const bool hovered = enabled && (opt.state.testFlag(QStyle::State_MouseOver) || underMouse());
        const bool pressed = enabled && (opt.state.testFlag(QStyle::State_Sunken) || isDown());

        QPainter painter(this);
        ThemeManager::instance().paintSpinArrowButton(
            &painter, rect(), arrowType(), enabled, hovered, pressed);
    }
};

struct ScopedBoolFlag final {
    explicit ScopedBoolFlag(bool& target)
        : m_target(target)
        , m_previous(target) {
        m_target = true;
    }
    ~ScopedBoolFlag() { m_target = m_previous; }
    bool& m_target;
    bool m_previous;
};

template <typename DialogT, typename FactoryFn, typename RefreshFn, typename AcceptedFn>
void openManagedModelessDialog(QObject* receiver,
                               WindowZOrderManager* wm,
                               QPointer<DialogT>& slot,
                               bool resizable,
                               FactoryFn&& createFn,
                               RefreshFn&& refreshFn,
                               AcceptedFn&& onAccepted) {
    if (slot) {
        refreshFn(slot.data());
        wm->showAndRaise(slot.data());
        return;
    }
    DialogT* dlg = createFn();
    slot = dlg;
    wm->prepareModelessDialog(dlg, resizable, true);
    QObject::connect(dlg, &QDialog::accepted, receiver,
                     [cb = std::forward<AcceptedFn>(onAccepted), ptr = QPointer<DialogT>(dlg)]() {
                         if (ptr) {
                             cb(ptr.data());
                         }
                     });
    wm->showAndRaise(dlg);
}

template <typename DialogT, typename FactoryFn, typename AfterCreateFn>
void openManagedInfoDialog(WindowZOrderManager* wm,
                           QPointer<DialogT>& slot,
                           bool resizable,
                           FactoryFn&& createFn,
                           AfterCreateFn&& onCreated) {
    if (slot) {
        wm->showAndRaise(slot.data());
        return;
    }
    DialogT* dlg = createFn();
    slot = dlg;
    wm->prepareModelessDialog(dlg, resizable, true);
    onCreated(dlg);
    wm->showAndRaise(dlg);
}

} // namespace

/// Rótulo de título de dock com suporte a rotação de 90° no sentido anti-horário quando recolhido lateralmente.
class RotatableTitleLabel final : public QLabel {
public:
    explicit RotatableTitleLabel(const QString& text, QWidget* parent = nullptr)
        : QLabel(text, parent) {
        setObjectName(QStringLiteral("dockTitleLabel"));
        QFont f = font();
        f.setBold(true);
        setFont(f);
        setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    void setVerticalRotated(bool vertical) {
        if (m_verticalRotated == vertical) {
            return;
        }
        m_verticalRotated = vertical;
        setSizePolicy(QSizePolicy::Preferred,
                      vertical ? QSizePolicy::Expanding : QSizePolicy::Preferred);
        updateGeometry();
        update();
    }

    QSize sizeHint() const override {
        const QSize base = QLabel::sizeHint();
        return m_verticalRotated ? QSize(qMax(20, base.height()), base.width()) : base;
    }

    QSize minimumSizeHint() const override {
        if (!m_verticalRotated) {
            return QLabel::minimumSizeHint();
        }
        const QSize base = QLabel::sizeHint();
        return QSize(qMax(20, base.height()), 60);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (!m_verticalRotated) {
            QLabel::paintEvent(event);
            return;
        }
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(font());
        painter.setPen(palette().color(foregroundRole()));

        const int availLen = qMax(10, height() - 8);
        const QString drawn = QFontMetrics(font()).elidedText(text(), Qt::ElideRight, availLen);

        // Rotaciona 90° no sentido anti-horário: leitura de baixo para cima alinhada próxima ao botão superior.
        painter.translate(0, height());
        painter.rotate(-90.0);
        const QRect textRect(4, 0, availLen, width());
        painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, drawn);
    }

private:
    bool m_verticalRotated = false;
};

/// Barra de título customizada para QDockWidget com botão spin button de recolhimento/expansão.
class DockTitleBar final : public QFrame {
public:
    DockTitleBar(const QString& title,
                 const QString& buttonObjectName,
                 Qt::ArrowType expandedArrow,
                 Qt::ArrowType collapsedArrow,
                 QWidget* parent = nullptr)
        : QFrame(parent)
        , m_expandedArrow(expandedArrow)
        , m_collapsedArrow(collapsedArrow) {
        setObjectName(QStringLiteral("dockTitleBar"));
        setFrameShape(QFrame::NoFrame);

        m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
        m_layout->setContentsMargins(8, 4, 6, 4);
        m_layout->setSpacing(6);

        m_titleLabel = new RotatableTitleLabel(title, this);
        m_collapseButton = new SpinArrowButton(buttonObjectName, this);

        m_layout->addWidget(m_titleLabel, 1);
        m_layout->addWidget(m_collapseButton, 0, Qt::AlignVCenter);

        setCollapsed(false);
    }

    void setTitle(const QString& title) {
        if (m_titleLabel) {
            m_titleLabel->setText(title);
            m_titleLabel->updateGeometry();
            m_titleLabel->update();
        }
    }

    void setCollapsed(bool collapsed) {
        m_collapsed = collapsed;
        if (m_collapseButton) {
            m_collapseButton->setArrowType(collapsed ? m_collapsedArrow : m_expandedArrow);
            retranslateToolTip();
            m_collapseButton->update();
        }
    }

    void setVertical(bool vertical) {
        if (m_vertical == vertical || !m_layout) {
            return;
        }
        m_vertical = vertical;
        m_layout->removeWidget(m_titleLabel);
        m_layout->removeWidget(m_collapseButton);

        if (vertical) {
            m_titleLabel->setVerticalRotated(true);
            m_layout->setDirection(QBoxLayout::TopToBottom);
            m_layout->setContentsMargins(4, 6, 4, 8);
            m_layout->setSpacing(8);
            m_layout->addWidget(m_collapseButton, 0, Qt::AlignHCenter);
            m_layout->addWidget(m_titleLabel, 1);
        } else {
            m_titleLabel->setVerticalRotated(false);
            m_layout->setDirection(QBoxLayout::LeftToRight);
            m_layout->setContentsMargins(8, 4, 6, 4);
            m_layout->setSpacing(6);
            m_layout->addWidget(m_titleLabel, 1);
            m_layout->addWidget(m_collapseButton, 0, Qt::AlignVCenter);
        }
        updateGeometry();
        update();
    }

    void retranslateToolTip() {
        if (m_collapseButton) {
            m_collapseButton->setToolTip(locale::GerenciadorTraducao::trText(
                m_collapsed ? "Expandir painel" : "Recolher painel"));
        }
    }

    void syncUiMode() {
        if (m_collapseButton) {
            m_collapseButton->syncUiMode();
        }
        updateGeometry();
        update();
    }

    QToolButton* collapseButton() const { return m_collapseButton; }

    int collapsedDockHeight(const QDockWidget* dock) const {
        const int titleH = qMax(28, qMax(minimumSizeHint().height(), sizeHint().height()));
        const QMargins margins = dock ? dock->contentsMargins() : QMargins();
        return titleH + margins.top() + margins.bottom() + 2;
    }

    int collapsedDockWidth(const QDockWidget* dock) const {
        const int titleW = qMax(30, qMax(minimumSizeHint().width(), sizeHint().width()));
        const QMargins margins = dock ? dock->contentsMargins() : QMargins();
        return titleW + margins.left() + margins.right() + 2;
    }

private:
    QBoxLayout* m_layout = nullptr;
    RotatableTitleLabel* m_titleLabel = nullptr;
    SpinArrowButton* m_collapseButton = nullptr;
    bool m_collapsed = false;
    bool m_vertical = false;
    Qt::ArrowType m_expandedArrow = Qt::DownArrow;
    Qt::ArrowType m_collapsedArrow = Qt::UpArrow;
};

namespace {

enum class DockCollapseAxis {
    Vertical,
    Horizontal
};

template <typename ToggleFn, typename TopLevelFn>
DockTitleBar* attachCollapsibleTitleBar(QMainWindow* owner,
                                        QDockWidget* dock,
                                        const QString& buttonObjectName,
                                        Qt::ArrowType expandedArrow,
                                        Qt::ArrowType collapsedArrow,
                                        ToggleFn&& onToggle,
                                        TopLevelFn&& onTopLevelChanged) {
    auto* titleBar = new DockTitleBar(
        dock->windowTitle(), buttonObjectName, expandedArrow, collapsedArrow, dock);
    dock->setTitleBarWidget(titleBar);
    QObject::connect(dock, &QDockWidget::windowTitleChanged, titleBar, &DockTitleBar::setTitle);
    QObject::connect(titleBar->collapseButton(), &QToolButton::clicked, owner, std::forward<ToggleFn>(onToggle));
    QObject::connect(dock, &QDockWidget::topLevelChanged, owner, std::forward<TopLevelFn>(onTopLevelChanged));
    return titleBar;
}

QWidget* createDockContentHost(QWidget* content, QWidget* parent) {
    auto* host = new QWidget(parent);
    host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(content);
    return host;
}

void setDockHostCollapsed(QDockWidget* dock, QWidget* content, DockCollapseAxis axis, bool collapsed) {
    if (!dock || !content) {
        return;
    }
    QWidget* host = dock->widget();
    if (host && host != content) {
        if (collapsed) {
            content->setVisible(false);
            if (axis == DockCollapseAxis::Horizontal) {
                // Mantém o host ativo com largura 0 e expansão vertical para preencher toda a lateral recolhida.
                host->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
                host->setMinimumSize(0, 0);
                host->setMaximumSize(0, QWIDGETSIZE_MAX);
            } else {
                // Mantém o host ativo com altura 0 e expansão horizontal para preservar largura total do cabeçalho.
                host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                host->setMinimumSize(0, 0);
                host->setMaximumSize(QWIDGETSIZE_MAX, 0);
            }
        } else {
            host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            host->setMinimumSize(0, 0);
            host->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            content->setVisible(true);
        }
        host->updateGeometry();
    } else {
        content->setVisible(!collapsed);
    }
}

void clearCollapsedDockBounds(QDockWidget* dock) {
    dock->setMinimumWidth(0);
    dock->setMaximumWidth(QWIDGETSIZE_MAX);
    dock->setMinimumHeight(0);
    dock->setMaximumHeight(QWIDGETSIZE_MAX);
}

void applyCollapsedHeightClamp(QDockWidget* dock, const DockTitleBar* titleBar, int& outCollapsedHeight) {
    outCollapsedHeight = titleBar->collapsedDockHeight(dock);
    clearCollapsedDockBounds(dock);
    dock->setMaximumHeight(outCollapsedHeight);
    dock->updateGeometry();
}

void repaintMainWindowSeparatorsNow(QMainWindow* window, QSplitter* splitter) {
    if (!window) {
        return;
    }
    window->update();
    if (splitter) {
        splitter->update();
        for (int i = 1; i < splitter->count(); ++i) {
            if (QWidget* handle = splitter->handle(i)) {
                handle->update();
            }
        }
    }
}

/// Reativa o layout do QMainWindow e repinta os separadores após recolher/expandir painéis dock.
void scheduleDockSeparatorRepaint(QMainWindow* window, QSplitter* splitter) {
    if (!window) {
        return;
    }
    if (QLayout* rootLayout = window->layout()) {
        rootLayout->invalidate();
        rootLayout->activate();
    }
    repaintMainWindowSeparatorsNow(window, splitter);
    QTimer::singleShot(0, window, [winPtr = QPointer<QMainWindow>(window),
                                    splitPtr = QPointer<QSplitter>(splitter)]() {
        repaintMainWindowSeparatorsNow(winPtr.data(), splitPtr.data());
    });
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_windowManager(new WindowZOrderManager(this, this)),
      m_analysisExecutor(new ui::AsyncAnalysisExecutor([this]() { return m_modelRevision; }, this)) {
    resize(1400, 900);

    const QIcon icon = locale::GerenciadorTraducao::appIcon();
    if (!icon.isNull()) {
        setWindowIcon(icon);
    }

    m_viewport = new Viewport3D(this);
    setCentralWidget(m_viewport);
    m_viewport->setModel(&m_model);
    if (m_viewport->renderQualitySettings().planeTheme != ActivePlaneTheme::CleanLight) {
        m_lastDarkPlaneTheme = m_viewport->renderQualitySettings().planeTheme;
    }

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(350);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::flushRecoverySnapshotIfDirty);

    m_prefsSaveTimer = new QTimer(this);
    m_prefsSaveTimer->setSingleShot(true);
    m_prefsSaveTimer->setInterval(500);
    connect(m_prefsSaveTimer, &QTimer::timeout, this, [this]() {
        flushUserPreferencesSync();
    });

    connect(m_analysisExecutor, &ui::AsyncAnalysisExecutor::jobCompleted,
            this, [this](std::uint64_t /*jobId*/, bool /*succeeded*/) {
                if (m_solveInProgress || !m_pendingResolve) {
                    return;
                }
                if (m_analysisExecutor && m_analysisExecutor->isBusy()) {
                    return;
                }
                const bool rerunInteractive = m_pendingResolveInteractive;
                m_pendingResolve = false;
                m_pendingResolveInteractive = false;
                const bool needSolveForContinuations = !m_staticSolveContinuations.empty();
                if (needSolveForContinuations || (m_autoRecalculate && m_hasSolvedContext) || rerunInteractive) {
                    if (m_model.validate().valid) {
                        if (runSolver(rerunInteractive)) {
                            return;
                        }
                    }
                }
                if (!m_staticSolveContinuations.empty()) {
                    auto callbacks = std::move(m_staticSolveContinuations);
                    m_staticSolveContinuations.clear();
                    const bool ok = hasValidStaticResultForCurrentRevision();
                    for (const auto& cb : callbacks) {
                        if (cb) {
                            cb(ok);
                        }
                    }
                }
            });

    {
        const ScopedBoolFlag startupGuard(m_rebuildingOrLoadingResultsSettings);
        const ScopedBoolFlag initialModelGuard(m_loadingOrTemplateModel);
        initDefaultResultsLayoutAssignments();
        setupMenus();
        setupToolbars();
        setupDocks();
        setupStatusBar();
        syncResultsLayoutsMenuState();
        retranslateUi();
        syncRenderSettingsMenuState();
        applyTheme();

        loadResultsPanelSettings();

        m_windowManager->registerDockWidget(m_inspectorDock);
        m_windowManager->registerDockWidget(m_resultsDock);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](UiTheme) {
            applyTheme();
        });
        connect(&ThemeManager::instance(), &ThemeManager::uiStyleModeChanged, this, [this](UiStyleMode) {
            applyTheme();
        });

        // Conexões de sinais do Viewport
        connect(m_viewport, &Viewport3D::cursorCoordinatesChanged, this, &MainWindow::updateCursorCoordinates);
        connect(m_viewport, &Viewport3D::selectionChanged, this, &MainWindow::onSelectionChanged);
        connect(m_viewport, &Viewport3D::modelModified, this, &MainWindow::onModelModified);
        connect(m_viewport, &Viewport3D::statusMessage, this, &MainWindow::showStatusMessage);

        // Inicializa completamente vazio, sem projeto ou exemplo carregado na viewport ou área de plotagem
        m_hasSolvedContext = false;
        m_lastSolverResult = SolverResult();
        m_model.clearUndoHistory();
        m_model.pushUndo();
        refreshAfterModelLoad(false, false);
    }
    const bool loadedPrefs = loadUserPreferences();
    const bool hasAllSections = io::UserPreferencesStore::hasAllSections({
        QStringLiteral("app"),
        QStringLiteral("graphics"),
        QStringLiteral("window"),
        QStringLiteral("workspace"),
        QStringLiteral("resultsPanel")});
    if (!loadedPrefs || !hasAllSections) {
        flushUserPreferencesSync();
    }
    if (!io::AppLogger::isInitialized()) {
        io::AppLogger::initialize(false, true);
    }
    io::AppLogger::info(
        QStringLiteral("MainWindow"),
        QStringLiteral("Janela principal inicializada vazia | startupBehavior=%1 | confirmUnsavedOnClose=%2")
            .arg(io::startupSessionBehaviorToKey(m_startupSessionBehavior),
                 m_confirmUnsavedOnClose ? QStringLiteral("true") : QStringLiteral("false")));
    if (m_viewport) {
        m_viewport->clearElementDcrMap();
        m_viewport->clearPostAnimation();
        m_viewport->fitAll();
    }
    updateWindowTitleAndFileActions();
    QTimer::singleShot(0, this, &MainWindow::checkAndOfferStartupRecovery);
}

MainWindow::~MainWindow() {
    io::AppLogger::info(QStringLiteral("MainWindow"), QStringLiteral("Encerrando MainWindow e persistindo estado da sessao."));
    m_staticSolveContinuations.clear();
    if (m_analysisExecutor) {
        m_analysisExecutor->cancelAndWait(5000, false);
    }
    if (m_activeAutoSaveThread && m_activeAutoSaveThread->isRunning()) {
        m_activeAutoSaveThread->wait(5000);
    }
    if (m_prefsSaveTimer && m_prefsSaveTimer->isActive()) {
        flushUserPreferencesSync();
    }
    if (!m_sessionStatePersistedOnClose) {
        persistCurrentSessionState();
        m_sessionStatePersistedOnClose = true;
    }
    if (m_windowManager) {
        m_windowManager->closeAllChildWindows();
    }
    if (m_viewport) {
        m_viewport->setModel(nullptr);
    }
}

void MainWindow::applyTheme() {
    const bool nativeUi = ThemeManager::instance().isNativeUi();
    const bool isLight = ThemeManager::instance().isLight();
    if (m_btnSolve) {
        if (nativeUi) {
            if (!m_btnSolve->styleSheet().isEmpty()) {
                m_btnSolve->setStyleSheet(QString());
            }
            QFont solveFont = m_btnSolve->font();
            solveFont.setBold(true);
            m_btnSolve->setFont(solveFont);
        } else {
            m_btnSolve->setFont(QFont());
            m_btnSolve->setAttribute(Qt::WA_SetFont, false);
            if (isLight) {
                m_btnSolve->setStyleSheet(QStringLiteral(
                    "QPushButton { background-color: #15803d; color: #ffffff; border: 1px solid #166534; font-weight: bold; padding: 5px 12px; border-radius: 3px; font-size: 13px; }"
                    "QPushButton:hover { background-color: #166534; }"
                    "QPushButton:pressed { background-color: #14532d; }"
                ));
            } else {
                m_btnSolve->setStyleSheet(QStringLiteral(
                    "QPushButton { background-color: #2e7d32; color: #ffffff; border: 1px solid #1b5e20; font-weight: bold; padding: 5px 12px; border-radius: 3px; font-size: 13px; }"
                    "QPushButton:hover { background-color: #388e3c; }"
                    "QPushButton:pressed { background-color: #1b5e20; }"
                ));
            }
        }
    }

    const QString customTabScrollerCss = QStringLiteral(
        "QTabBar::scroller { width: 0px; height: 0px; }\n"
        "QTabBar QToolButton { width: 0px; height: 0px; min-width: 0px; max-width: 0px;"
        " min-height: 0px; max-height: 0px; border: none; background: transparent; padding: 0px; margin: 0px; }");
    for (QTabWidget* tabW : m_resultsLayoutTabWidgets) {
        if (!tabW) continue;
        if (nativeUi) {
            if (!tabW->styleSheet().isEmpty()) {
                tabW->setStyleSheet(QString());
            }
        } else if (tabW->styleSheet() != customTabScrollerCss) {
            tabW->setStyleSheet(customTabScrollerCss);
        }
    }

    if (m_inspectorTitleBar) {
        m_inspectorTitleBar->syncUiMode();
        if (m_inspectorCollapsed && m_inspectorDock && !m_inspectorDock->isFloating()) {
            const int collapsedW = m_inspectorTitleBar->collapsedDockWidth(m_inspectorDock);
            m_inspectorDock->setMinimumWidth(collapsedW);
            m_inspectorDock->setMaximumWidth(collapsedW);
        }
    }
    if (m_resultsTitleBar) {
        m_resultsTitleBar->syncUiMode();
        if (m_resultsCollapsed && m_resultsDock) {
            int collapsedH = 0;
            applyCollapsedHeightClamp(m_resultsDock, m_resultsTitleBar, collapsedH);
        }
    }

    if (m_tableConfigurator) {
        m_tableConfigurator->applyTheme(isLight);
    }
    ThemeManager::instance().refreshToolbarExtensionButtons();
    refreshReactionSummaryRowTheme();
    syncThemeMenuState();
    syncUiStyleMenuState();
    ThemeManager::instance().applyWindowChrome(this);
    if (m_tb1) {
        m_tb1->updateGeometry();
        m_tb1->update();
    }
    if (m_tb2) {
        m_tb2->updateGeometry();
        m_tb2->update();
    }
    if (m_inspectorContent) {
        m_inspectorContent->updateGeometry();
        m_inspectorContent->update();
    }
    if (m_resultsDock) {
        m_resultsDock->updateGeometry();
        m_resultsDock->update();
    }
    if (QLayout* rootLayout = layout()) {
        rootLayout->invalidate();
        rootLayout->activate();
    }
    if (m_viewport) {
        m_viewport->update();
    }
    update();
}

void MainWindow::syncThemeMenuState() {
    const bool isLight = ThemeManager::instance().isLight();
    if (m_actGuiThemeDark) {
        QSignalBlocker b(m_actGuiThemeDark);
        m_actGuiThemeDark->setChecked(!isLight);
    }
    if (m_actGuiThemeLight) {
        QSignalBlocker b(m_actGuiThemeLight);
        m_actGuiThemeLight->setChecked(isLight);
    }
}

void MainWindow::syncUiStyleMenuState() {
    const bool isNative = ThemeManager::instance().isNativeUi();
    if (m_actUiStyleNative) {
        QSignalBlocker b(m_actUiStyleNative);
        m_actUiStyleNative->setChecked(isNative);
    }
    if (m_actUiStyleCustom) {
        QSignalBlocker b(m_actUiStyleCustom);
        m_actUiStyleCustom->setChecked(!isNative);
    }
}

void MainWindow::refreshReactionSummaryRowTheme() {
    const bool nativeUi = ThemeManager::instance().isNativeUi();
    const bool isLight = ThemeManager::instance().isLight();
    for (ui::ResultTableModel* model : m_resultTableModels) {
        if (model != nullptr) {
            model->setThemeState(isLight, nativeUi);
        }
    }
}

void MainWindow::setupMenus() {
    // Menu Arquivo
    m_fileMenu = menuBar()->addMenu("&Arquivo");
    m_actNewProject = m_fileMenu->addAction("Novo Projeto", QKeySequence::New, this, &MainWindow::onNewProject);
    m_actOpenProject = m_fileMenu->addAction("Abrir (.omniframe)...", QKeySequence::Open, this, &MainWindow::onOpenProject);
    m_actSaveProject = m_fileMenu->addAction("Salvar (.omniframe)", QKeySequence::Save, this, &MainWindow::onSaveProject);
    m_actSaveProjectAs = m_fileMenu->addAction("Salvar Como (.omniframe)...", QKeySequence::SaveAs, this, &MainWindow::onSaveProjectAs);
    m_actRecoverSession = m_fileMenu->addAction("Recuperar Sessão Anterior...", this, &MainWindow::onRecoverSession);
    m_fileMenu->addSeparator();
    m_actImportDxf = m_fileMenu->addAction("Importar DXF...", this, &MainWindow::onImportDxf);
    m_actExportDxf = m_fileMenu->addAction("Exportar DXF...", this, &MainWindow::onExportDxf);
    m_actImportIfc = m_fileMenu->addAction("Importar BIM IFC4...", this, &MainWindow::onImportIfc);
    m_actExportIfc = m_fileMenu->addAction("Exportar BIM IFC4...", this, &MainWindow::onExportIfc);
    m_fileMenu->addSeparator();
    m_actExportPdf = m_fileMenu->addAction("Exportar Memorial Executivo (PDF A4)...", this, &MainWindow::onExportPdfReport);
    m_actExportHtml = m_fileMenu->addAction("Exportar Relatório Técnico (HTML)...", this, &MainWindow::onExportHtmlReport);
    m_actExportMd = m_fileMenu->addAction("Exportar Relatório Técnico (Markdown)...", this, &MainWindow::onExportMarkdownReport);
    m_fileMenu->addSeparator();
    m_actQuit = m_fileMenu->addAction("Sair", QKeySequence(Qt::CTRL | Qt::Key_Q), this, &MainWindow::onRequestQuit);

    // Menu Editar
    m_editMenu = menuBar()->addMenu("&Editar");
    m_actUndo = m_editMenu->addAction("Desfazer", QKeySequence::Undo, this, &MainWindow::onUndo);
    m_actRedo = m_editMenu->addAction("Refazer", QKeySequence::Redo, this, &MainWindow::onRedo);
    updateUndoRedoActions();

    // Menu Exemplos Prontos
    m_exMenu = menuBar()->addMenu("&Exemplos");
    m_actEx1 = m_exMenu->addAction("Exemplo 1: Pórtico Espacial de Edifício (2 Pavimentos, Vento e Sobrecarga)", this, &MainWindow::loadExampleBuilding);
    m_actEx2 = m_exMenu->addAction("Exemplo 2: Balanço Espacial em L Sujeito a Flexo-Torção", this, &MainWindow::loadExampleCantilever);
    m_actEx3 = m_exMenu->addAction("Exemplo 3: Treliça Espacial / Passarela Metálica 3D", this, &MainWindow::loadExampleSpaceTruss);

    // Menu Estrutura
    m_structMenu = menuBar()->addMenu("&Estrutura");
    m_actMaterials = m_structMenu->addAction("Materiais...", this, &MainWindow::onAssignMaterials);
    m_actSections = m_structMenu->addAction("Seções Transversais...", this, &MainWindow::onAssignSections);
    m_actSupports = m_structMenu->addAction("Condições de Apoio...", this, &MainWindow::onAssignSupports);
    m_actReleases = m_structMenu->addAction("Rótulas / Liberações...", this, &MainWindow::onAssignReleases);
    m_actNodalLoads = m_structMenu->addAction("Cargas Nodais Concentradas...", this, &MainWindow::onAssignNodalLoads);
    m_actDistLoads = m_structMenu->addAction("Cargas Uniformemente Distribuídas...", this, &MainWindow::onAssignDistributedLoads);
    m_actThermalLoads = m_structMenu->addAction("Cargas Térmicas (ΔT)...", this, &MainWindow::onAssignThermalLoads);
    m_actRollAngle = m_structMenu->addAction("Ângulo de Roll da Seção (γ)...", this, &MainWindow::onAssignRollAngle);
    m_structMenu->addSeparator();
    m_actLoadCombos = m_structMenu->addAction("Casos de Carga, Combinações NBR & Vento...", this, &MainWindow::onOpenLoadCombinations);
    m_actMeshGen = m_structMenu->addAction("Gerador de Malha 1D/2D/3D...", this, &MainWindow::onOpenMeshGenerator);
    m_structMenu->addSeparator();
    m_actAnalysisSuite = m_structMenu->addAction("Suite de Análises (Modal / Flambagem / Não Linear / Sismo)...", this, &MainWindow::onOpenAnalysisSuite);
    m_actPostControls = m_structMenu->addAction("Controles 3D (Contornos / Corte / Animação)...", this, &MainWindow::onOpenPostControls);
    m_actNbrAudit = m_structMenu->addAction("Auditoria Normativa ABNT NBR (6118 / 8800 / 6122)...", this, &MainWindow::onOpenNbrCompliance);
    m_structMenu->addSeparator();
    m_actAutoRecalc = m_structMenu->addAction("Recálculo Automático após Edição");
    m_actAutoRecalc->setCheckable(true);
    m_actAutoRecalc->setChecked(m_autoRecalculate);
    connect(m_actAutoRecalc, &QAction::toggled, this, &MainWindow::onAutoRecalculateToggled);
    m_actSolveMenu = m_structMenu->addAction("CALCULAR ESTRUTURA", QKeySequence(Qt::Key_F5), this, &MainWindow::onSolveStructure);

    // Menu Exibir
    m_viewMenu = menuBar()->addMenu("&Exibir");
    m_actViewIsoMenu = m_viewMenu->addAction("Vista 3D Isométrica", m_viewport, &Viewport3D::viewIsometric);
    m_actViewFrontMenu = m_viewMenu->addAction("Vista Frontal (XZ)", m_viewport, &Viewport3D::viewFrontalXZ);
    m_actViewTopMenu = m_viewMenu->addAction("Vista de Planta (XY)", m_viewport, &Viewport3D::viewTopXY);
    m_actViewSideMenu = m_viewMenu->addAction("Vista Lateral (YZ)", m_viewport, &Viewport3D::viewSideYZ);
    m_actViewFitMenu = m_viewMenu->addAction("Enquadrar Tudo (F)", m_viewport, &Viewport3D::fitAll);

    // Menu Configurações (Resolução, Definição de Imagem, Plano Ativo e Idioma)
    m_settingsMenu = menuBar()->addMenu("&Configurações");

    m_qualityPresetMenu = m_settingsMenu->addMenu("Perfil de Qualidade Gráfica");
    m_presetActionGroup = new QActionGroup(this);
    m_presetActionGroup->setExclusive(true);
    auto addPresetAct = [&](const char* label, QualityPreset preset) -> QAction* {
        QAction* act = m_qualityPresetMenu->addAction(label);
        act->setCheckable(true);
        m_presetActionGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, preset]() { onApplyRenderPreset(preset); });
        return act;
    };
    m_actPresetAutoMax  = addPresetAct("Automático (Máxima Qualidade)", QualityPreset::AutoMax);
    m_actPresetUltra    = addPresetAct("Ultra (SSAA 200% + MSAA Máx)", QualityPreset::UltraQuality);
    m_actPresetHigh     = addPresetAct("Alta (SSAA 150% + MSAA 8×)", QualityPreset::HighQuality);
    m_actPresetBalanced = addPresetAct("Equilibrada (Nativa 100% + MSAA 4×)", QualityPreset::Balanced);
    m_actPresetPerf     = addPresetAct("Desempenho (Nativa 100% + Shader AA)", QualityPreset::Performance);

    m_resScaleMenu = m_settingsMenu->addMenu("Escala de Resolução Interna (SSAA)");
    m_resScaleActionGroup = new QActionGroup(this);
    m_resScaleActionGroup->setExclusive(true);
    auto addScaleAct = [&](const char* label, bool isAuto, float scale) -> QAction* {
        QAction* act = m_resScaleMenu->addAction(label);
        act->setCheckable(true);
        m_resScaleActionGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, isAuto, scale]() {
            RenderQualitySettings s = m_viewport->renderQualitySettings();
            s.autoMaxResolution = isAuto;
            s.resolutionScale = scale;
            s.refreshPresetClassification();
            applyAndSaveRenderSettings(s);
        });
        return act;
    };
    m_actScaleAuto = addScaleAct("Automática (Máxima Suportada)", true, 2.0f);
    m_actScale75   = addScaleAct("75% (Desempenho)", false, 0.75f);
    m_actScale100  = addScaleAct("100% (Resolução Nativa)", false, 1.00f);
    m_actScale125  = addScaleAct("125% (Alta Definição)", false, 1.25f);
    m_actScale150  = addScaleAct("150% (Superamostragem)", false, 1.50f);
    m_actScale200  = addScaleAct("200% (Ultra Definição)", false, 2.00f);

    m_msaaMenu = m_settingsMenu->addMenu("Suavização por Hardware (MSAA)");
    m_msaaActionGroup = new QActionGroup(this);
    m_msaaActionGroup->setExclusive(true);
    auto addMsaaAct = [&](const char* label, bool isAuto, int samples) -> QAction* {
        QAction* act = m_msaaMenu->addAction(label);
        act->setCheckable(true);
        m_msaaActionGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, isAuto, samples]() {
            RenderQualitySettings s = m_viewport->renderQualitySettings();
            s.autoMsaa = isAuto;
            s.requestedMsaa = samples;
            s.refreshPresetClassification();
            applyAndSaveRenderSettings(s);
        });
        return act;
    };
    m_actMsaaAuto = addMsaaAct("Automático (Máximo da GPU)", true, 16);
    m_actMsaaOff  = addMsaaAct("Desligado", false, 0);
    m_actMsaa2x   = addMsaaAct("2× MSAA", false, 2);
    m_actMsaa4x   = addMsaaAct("4× MSAA", false, 4);
    m_actMsaa8x   = addMsaaAct("8× MSAA", false, 8);
    m_actMsaa16x  = addMsaaAct("16× MSAA", false, 16);

    m_definitionMenu = m_settingsMenu->addMenu("Definição de Linhas e Imagem");
    m_actShaderLineAa = m_definitionMenu->addAction("Suavização Analítica de Linhas (Shader SDF)");
    m_actShaderLineAa->setCheckable(true);
    connect(m_actShaderLineAa, &QAction::toggled, this, [this](bool checked) {
        RenderQualitySettings s = m_viewport->renderQualitySettings();
        s.shaderLineAA = checked;
        s.refreshPresetClassification();
        applyAndSaveRenderSettings(s);
    });

    m_definitionMenu->addSeparator();
    m_filterActionGroup = new QActionGroup(this);
    m_filterActionGroup->setExclusive(true);
    auto addFilterAct = [&](const char* label, DownsampleFilter filter) -> QAction* {
        QAction* act = m_definitionMenu->addAction(label);
        act->setCheckable(true);
        m_filterActionGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, filter]() {
            RenderQualitySettings s = m_viewport->renderQualitySettings();
            s.downsampleFilter = filter;
            s.refreshPresetClassification();
            applyAndSaveRenderSettings(s);
        });
        return act;
    };
    m_actFilterCatmullSharpen = addFilterAct("Filtro Catmull-Rom + Nitidez (Máximo)", DownsampleFilter::CatmullRomSharpen);
    m_actFilterBicubic        = addFilterAct("Filtro Bicúbico Suave", DownsampleFilter::BicubicSmooth);
    m_actFilterBilinear       = addFilterAct("Filtro Bilinear Padrão", DownsampleFilter::BilinearHardware);

    m_definitionMenu->addSeparator();
    m_lineWidthActionGroup = new QActionGroup(this);
    m_lineWidthActionGroup->setExclusive(true);
    auto addLineWidthAct = [&](const char* label, float mult) -> QAction* {
        QAction* act = m_definitionMenu->addAction(label);
        act->setCheckable(true);
        m_lineWidthActionGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, mult]() {
            RenderQualitySettings s = m_viewport->renderQualitySettings();
            s.lineWidthMultiplier = mult;
            s.refreshPresetClassification();
            applyAndSaveRenderSettings(s);
        });
        return act;
    };
    m_actLineThin     = addLineWidthAct("Espessura Fina (0,85×)", 0.85f);
    m_actLineBalanced = addLineWidthAct("Espessura Equilibrada (1,00×)", 1.00f);
    m_actLineCrisp    = addLineWidthAct("Espessura Nítida (1,15×)", 1.15f);
    m_actLineBold     = addLineWidthAct("Espessura Destacada (1,50×)", 1.50f);

    m_activePlaneAppearanceMenu = m_settingsMenu->addMenu("Aparência do Plano Ativo");
    m_planeThemeActionGroup = new QActionGroup(this);
    m_planeThemeActionGroup->setExclusive(true);
    auto addThemeAct = [&](const char* label, ActivePlaneTheme theme) -> QAction* {
        QAction* act = m_activePlaneAppearanceMenu->addAction(label);
        act->setCheckable(true);
        m_planeThemeActionGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, theme]() {
            RenderQualitySettings s = m_viewport->renderQualitySettings();
            s.planeTheme = theme;
            applyAndSaveRenderSettings(s);
        });
        return act;
    };
    m_actThemeObsidian   = addThemeAct("Fundo Obsidiana Escuro (Padrão)", ActivePlaneTheme::ObsidianDark);
    m_actThemePureNight  = addThemeAct("Fundo Noite Profunda (Ultra Escuro)", ActivePlaneTheme::PureNight);
    m_actThemeGraphite   = addThemeAct("Fundo Grafite Escuro", ActivePlaneTheme::GraphiteDark);
    m_actThemeCleanLight = addThemeAct("Fundo Claro Técnico", ActivePlaneTheme::CleanLight);

    m_activePlaneAppearanceMenu->addSeparator();
    m_actPlaneSurface = m_activePlaneAppearanceMenu->addAction("Superfície e Moldura do Plano Ativo");
    m_actPlaneSurface->setCheckable(true);
    connect(m_actPlaneSurface, &QAction::toggled, this, [this](bool checked) {
        RenderQualitySettings s = m_viewport->renderQualitySettings();
        s.showActivePlaneSurface = checked;
        applyAndSaveRenderSettings(s);
    });

    m_actResolutionHud = m_activePlaneAppearanceMenu->addAction("Telemetria de Resolução no Viewport");
    m_actResolutionHud->setCheckable(true);
    connect(m_actResolutionHud, &QAction::toggled, this, [this](bool checked) {
        RenderQualitySettings s = m_viewport->renderQualitySettings();
        s.showResolutionHud = checked;
        applyAndSaveRenderSettings(s);
    });

    m_settingsMenu->addSeparator();
    m_actVsync = m_settingsMenu->addAction("Sincronização Vertical (VSync)");
    m_actVsync->setCheckable(true);
    connect(m_actVsync, &QAction::toggled, this, [this](bool checked) {
        RenderQualitySettings s = m_viewport->renderQualitySettings();
        s.vsyncEnabled = checked;
        applyAndSaveRenderSettings(s);
    });

    m_actAdvancedRenderSettings = m_settingsMenu->addAction("Configurações Avançadas de Resolução e Imagem...",
                                                            this, &MainWindow::onOpenRenderSettingsDialog);
    m_actRestoreRenderDefaults = m_settingsMenu->addAction("Restaurar Padrões de Máxima Qualidade",
                                                           this, &MainWindow::onRestoreDefaultRenderSettings);

    // Submenu Layouts do Painel de Tabelas de Resultados (Configurações > Layouts...)
    m_settingsMenu->addSeparator();
    m_resultsLayoutsMenu = m_settingsMenu->addMenu("Layouts do Painel de Tabelas de Resultados");
    m_resultsLayoutsMenu->setObjectName(QStringLiteral("resultsLayoutsMenu"));
    m_resultsLayoutCountGroup = new QActionGroup(this);
    m_resultsLayoutCountGroup->setExclusive(true);

    const char* countLabels[4] = {
        "1 Layout (Todas as Tabelas)",
        "2 Layouts",
        "3 Layouts",
        "4 Layouts"
    };
    for (int i = 0; i < 4; ++i) {
        QAction* actCount = m_resultsLayoutsMenu->addAction(countLabels[i]);
        actCount->setCheckable(true);
        m_resultsLayoutCountGroup->addAction(actCount);
        m_actResultsLayoutCount[static_cast<std::size_t>(i)] = actCount;
        connect(actCount, &QAction::triggered, this, [this, layoutCnt = i + 1]() {
            setResultsLayoutCount(layoutCnt);
        });
    }

    m_resultsLayoutsMenu->addSeparator();
    const char* layoutMenuTitles[4] = {
        "Tabelas do 1º Layout",
        "Tabelas do 2º Layout",
        "Tabelas do 3º Layout",
        "Tabelas do 4º Layout"
    };
    for (int L = 0; L < 4; ++L) {
        auto* subMenu = new PersistentLayoutTableMenu(layoutMenuTitles[L], m_resultsLayoutsMenu);
        m_resultsLayoutsMenu->addMenu(subMenu);
        m_resultsLayoutConfigMenus[static_cast<std::size_t>(L)] = subMenu;

        for (int k = 0; k < 4; ++k) {
            const auto kind = static_cast<ui::ResultTableKind>(k);
            QAction* actTbl = subMenu->addAction(resultTableTabTitle(kind));
            actTbl->setCheckable(true);
            m_actResultsLayoutTables[static_cast<std::size_t>(L)][static_cast<std::size_t>(k)] = actTbl;
            connect(actTbl, &QAction::toggled, this, [this, L, kind](bool checked) {
                setResultsLayoutTableVisible(L, kind, checked);
            });
        }
        subMenu->addSeparator();
        QAction* actAll = subMenu->addAction("Selecionar Todas as Tabelas");
        m_actResultsLayoutSelectAll[static_cast<std::size_t>(L)] = actAll;
        connect(actAll, &QAction::triggered, this, [this, L]() {
            for (int k = 0; k < 4; ++k) {
                m_resultsModeTableAssign[static_cast<std::size_t>(m_resultsLayoutCount - 1)]
                                        [static_cast<std::size_t>(L)]
                                        [static_cast<std::size_t>(k)] = true;
            }
            rebuildResultsLayoutsUi();
            syncResultsLayoutsMenuState();
            if (!m_rebuildingOrLoadingResultsSettings) {
                saveUserPreferences();
            }
        });
    }

    m_resultsLayoutsMenu->addSeparator();
    m_actResetResultsLayouts = m_resultsLayoutsMenu->addAction(
        "Restaurar Layouts Padrão", this, &MainWindow::resetResultsLayoutsToDefault);

    // Submenu Persistência de Configurações e Preferências
    m_settingsMenu->addSeparator();
    m_preferencesPersistenceMenu = m_settingsMenu->addMenu("Persistência de Configurações e Preferências");
    m_preferencesPersistenceMenu->setObjectName(QStringLiteral("preferencesPersistenceMenu"));
    m_actSaveAllPreferences = m_preferencesPersistenceMenu->addAction(
        "Salvar Todas as Configurações Agora", this, &MainWindow::onSaveAllSettingsNow);
    m_actReloadSavedPreferences = m_preferencesPersistenceMenu->addAction(
        "Recarregar Configurações Salvas", this, &MainWindow::onReloadSavedSettings);
    m_preferencesPersistenceMenu->addSeparator();
    m_actRestoreAllDefaults = m_preferencesPersistenceMenu->addAction(
        "Restaurar Todas as Configurações Padrão", this, &MainWindow::onRestoreAllDefaultSettings);

    // Submenu Sessão, Inicialização e Logs dentro de Configurações
    m_startupSessionMenu = m_settingsMenu->addMenu("Sessão, Inicialização e Logs");
    m_startupSessionMenu->setObjectName(QStringLiteral("startupSessionMenu"));
    m_startupBehaviorGroup = new QActionGroup(this);
    m_startupBehaviorGroup->setExclusive(true);

    m_actStartupAsk = m_startupSessionMenu->addAction("Perguntar ao iniciar (Exibir diálogo)");
    m_actStartupAsk->setObjectName(QStringLiteral("actStartupAsk"));
    m_actStartupAsk->setCheckable(true);
    m_startupBehaviorGroup->addAction(m_actStartupAsk);
    connect(m_actStartupAsk, &QAction::triggered, this, [this]() {
        onChangeStartupSessionBehavior(io::StartupSessionBehavior::AskOnStartup);
    });

    m_actStartupAutoLoad = m_startupSessionMenu->addAction("Carregar automaticamente projeto ou exemplo anterior");
    m_actStartupAutoLoad->setObjectName(QStringLiteral("actStartupAutoLoad"));
    m_actStartupAutoLoad->setCheckable(true);
    m_startupBehaviorGroup->addAction(m_actStartupAutoLoad);
    connect(m_actStartupAutoLoad, &QAction::triggered, this, [this]() {
        onChangeStartupSessionBehavior(io::StartupSessionBehavior::AutoLoadPrevious);
    });

    m_actStartupNewProject = m_startupSessionMenu->addAction("Sempre iniciar um novo projeto (Vazio)");
    m_actStartupNewProject->setObjectName(QStringLiteral("actStartupNewProject"));
    m_actStartupNewProject->setCheckable(true);
    m_startupBehaviorGroup->addAction(m_actStartupNewProject);
    connect(m_actStartupNewProject, &QAction::triggered, this, [this]() {
        onChangeStartupSessionBehavior(io::StartupSessionBehavior::AlwaysStartNew);
    });

    m_startupSessionMenu->addSeparator();
    m_actConfirmUnsavedOnClose = m_startupSessionMenu->addAction("Exibir confirmação de alterações não salvas ao sair");
    m_actConfirmUnsavedOnClose->setObjectName(QStringLiteral("actConfirmUnsavedOnClose"));
    m_actConfirmUnsavedOnClose->setCheckable(true);
    m_actConfirmUnsavedOnClose->setChecked(m_confirmUnsavedOnClose);
    connect(m_actConfirmUnsavedOnClose, &QAction::toggled, this, &MainWindow::onToggleConfirmUnsavedOnClose);

    m_startupSessionMenu->addSeparator();
    m_actOpenStartupSessionDialog = m_startupSessionMenu->addAction(
        "Configurar Inicialização, Sessão e Logs...",
        this, &MainWindow::onOpenStartupSessionDialog);
    m_actOpenStartupSessionDialog->setObjectName(QStringLiteral("actOpenStartupSessionDialog"));
    m_actOpenLogDirectory = m_startupSessionMenu->addAction(
        "Abrir Pasta de Logs...",
        this, &MainWindow::onOpenLogDirectory);
    m_actOpenLogDirectory->setObjectName(QStringLiteral("actOpenLogDirectory"));
    syncStartupSessionMenuState();

    // Submenu Tema dentro de Configurações (Configurações > Tema)
    m_settingsMenu->addSeparator();
    m_themeMenu = m_settingsMenu->addMenu("&Tema");
    m_guiThemeActionGroup = new QActionGroup(this);
    m_guiThemeActionGroup->setExclusive(true);
    m_actGuiThemeDark = m_themeMenu->addAction("Escuro");
    m_actGuiThemeDark->setCheckable(true);
    m_guiThemeActionGroup->addAction(m_actGuiThemeDark);
    m_actGuiThemeLight = m_themeMenu->addAction("Claro");
    m_actGuiThemeLight->setCheckable(true);
    m_guiThemeActionGroup->addAction(m_actGuiThemeLight);
    connect(m_actGuiThemeDark, &QAction::triggered, this, [this]() { onChangeGuiTheme(UiTheme::Dark); });
    connect(m_actGuiThemeLight, &QAction::triggered, this, [this]() { onChangeGuiTheme(UiTheme::Light); });
    syncThemeMenuState();

    // Submenu Interface do Usuário dentro de Configurações (Configurações > Interface do Usuário)
    m_uiStyleMenu = m_settingsMenu->addMenu("Interface do Usuário");
    m_uiStyleMenu->setObjectName(QStringLiteral("uiStyleMenu"));
    m_uiStyleActionGroup = new QActionGroup(this);
    m_uiStyleActionGroup->setExclusive(true);
    m_actUiStyleNative = m_uiStyleMenu->addAction("Nativo");
    m_actUiStyleNative->setObjectName(QStringLiteral("actUiStyleNative"));
    m_actUiStyleNative->setCheckable(true);
    m_uiStyleActionGroup->addAction(m_actUiStyleNative);
    m_actUiStyleCustom = m_uiStyleMenu->addAction("Customizado");
    m_actUiStyleCustom->setObjectName(QStringLiteral("actUiStyleCustom"));
    m_actUiStyleCustom->setCheckable(true);
    m_uiStyleActionGroup->addAction(m_actUiStyleCustom);
    connect(m_actUiStyleNative, &QAction::triggered, this, [this]() { onChangeUiStyleMode(UiStyleMode::Native); });
    connect(m_actUiStyleCustom, &QAction::triggered, this, [this]() { onChangeUiStyleMode(UiStyleMode::Custom); });
    syncUiStyleMenuState();

    // Submenu Idioma dentro de Configurações (Configurações > Idioma)
    m_langMenu = m_settingsMenu->addMenu("&Idioma");
    m_langActionGroup = new QActionGroup(this);
    m_langActionGroup->setExclusive(true);
    m_actLangPtBr = m_langMenu->addAction("Português (Brasil)");
    m_actLangPtBr->setCheckable(true);
    m_langActionGroup->addAction(m_actLangPtBr);
    m_actLangEnUs = m_langMenu->addAction("English (US)");
    m_actLangEnUs->setCheckable(true);
    m_langActionGroup->addAction(m_actLangEnUs);
    const bool isEn = (locale::GerenciadorTraducao::instance().idiomaAtual() == locale::Idioma::InglesEstadosUnidos);
    m_actLangPtBr->setChecked(!isEn);
    m_actLangEnUs->setChecked(isEn);
    connect(m_actLangPtBr, &QAction::triggered, this, [this]() { onChangeLanguage("pt_BR"); });
    connect(m_actLangEnUs, &QAction::triggered, this, [this]() { onChangeLanguage("en_US"); });

    // Submenu CLI Windows (CMD / PowerShell) dentro de Configurações
    m_settingsMenu->addSeparator();
    m_cliWindowsMenu = m_settingsMenu->addMenu("CLI Windows (CMD / PowerShell)");
    m_actCliInstallWin = m_cliWindowsMenu->addAction(
        "Registrar omniframe-cli no Windows (PATH e App Paths)...",
        this, &MainWindow::onInstallWindowsCliIntegration);
    m_actCliUninstallWin = m_cliWindowsMenu->addAction(
        "Remover Registro da CLI no Windows...",
        this, &MainWindow::onUninstallWindowsCliIntegration);
    m_cliWindowsMenu->addSeparator();
    m_actCliOpenCmd = m_cliWindowsMenu->addAction(
        "Abrir Prompt de Comando (CMD) com CLI...",
        this, &MainWindow::onOpenCliTerminalCmd);
    m_actCliOpenPowerShell = m_cliWindowsMenu->addAction(
        "Abrir PowerShell com CLI...",
        this, &MainWindow::onOpenCliTerminalPowerShell);
    m_cliWindowsMenu->addSeparator();
    m_actCliCheckWin = m_cliWindowsMenu->addAction(
        "Verificar Status da CLI no Windows...",
        this, &MainWindow::onCheckWindowsCliIntegration);

    syncRenderSettingsMenuState();

    // Menu Ajuda
    m_helpMenu = menuBar()->addMenu("&Ajuda");
    m_actManual = m_helpMenu->addAction("Manual", QKeySequence(Qt::Key_F1), this, &MainWindow::onManual);
    m_actGuidedSession = m_helpMenu->addAction("Sessão Guiada...", QKeySequence(Qt::Key_F2), this, &MainWindow::onGuidedSession);
    m_helpMenu->addSeparator();
    m_actAbout = m_helpMenu->addAction("Sobre o OmniFrame 3D", this, &MainWindow::onAbout);
}

void MainWindow::setupToolbars() {
    // -------------------------------------------------------------
    // Toolbar 1: Modos de Interação e Plano de Trabalho
    // -------------------------------------------------------------
    m_tb1 = addToolBar("Interação e Plano de Trabalho");
    m_tb1->setMovable(false);

    m_modeActionGroup = new QActionGroup(this);

    m_actSelect = m_tb1->addAction("Selecionar (S)");
    m_actSelect->setCheckable(true);
    m_actSelect->setChecked(true);
    m_actSelect->setShortcut(Qt::Key_S);
    m_modeActionGroup->addAction(m_actSelect);

    m_actAddNode = m_tb1->addAction("Inserir Nó (N)");
    m_actAddNode->setCheckable(true);
    m_actAddNode->setShortcut(Qt::Key_N);
    m_modeActionGroup->addAction(m_actAddNode);

    m_actAddElement = m_tb1->addAction("Inserir Barra (B)");
    m_actAddElement->setCheckable(true);
    m_actAddElement->setShortcut(Qt::Key_B);
    m_modeActionGroup->addAction(m_actAddElement);

    m_actMeasure = m_tb1->addAction("Medir (M)");
    m_actMeasure->setCheckable(true);
    m_actMeasure->setShortcut(Qt::Key_M);
    m_modeActionGroup->addAction(m_actMeasure);

    m_actDelete = m_tb1->addAction("Excluir (Del)");
    m_actDelete->setCheckable(true);
    m_actDelete->setShortcut(Qt::Key_Delete);
    m_modeActionGroup->addAction(m_actDelete);

    connect(m_modeActionGroup, &QActionGroup::triggered, this, &MainWindow::onInteractionModeChanged);

    m_tb1->addSeparator();

    // Controles do Plano de Trabalho
    m_planeLabel = new QLabel(" Plano: ");
    m_tb1->addWidget(m_planeLabel);
    m_planeCombo = new QComboBox();
    m_planeCombo->addItem("XZ (Frontal)", static_cast<int>(WorkPlane::XZ_Frontal));
    m_planeCombo->addItem("XY (Planta)", static_cast<int>(WorkPlane::XY_Top));
    m_planeCombo->addItem("YZ (Lateral)", static_cast<int>(WorkPlane::YZ_Side));
    connect(m_planeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onWorkPlaneChanged);
    if (const int xyIdx = m_planeCombo->findData(static_cast<int>(WorkPlane::XY_Top)); xyIdx >= 0) {
        m_planeCombo->setCurrentIndex(xyIdx);
    }
    m_tb1->addWidget(m_planeCombo);

    m_offsetLabel = new QLabel(" Cota: ");
    m_tb1->addWidget(m_offsetLabel);
    m_planeOffsetSpin = new QDoubleSpinBox();
    m_planeOffsetSpin->setRange(-1000.0, 1000.0);
    m_planeOffsetSpin->setDecimals(2);
    m_planeOffsetSpin->setValue(0.0);
    m_planeOffsetSpin->setSuffix(" m");
    connect(m_planeOffsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onPlaneOffsetChanged);
    m_tb1->addWidget(m_planeOffsetSpin);

    m_actSnap = m_tb1->addAction("Snap Grid");
    m_actSnap->setCheckable(true);
    m_actSnap->setChecked(true);
    connect(m_actSnap, &QAction::toggled, this, &MainWindow::onSnapToggled);

    m_stepLabel = new QLabel(" Passo: ");
    m_tb1->addWidget(m_stepLabel);
    m_gridStepSpin = new QDoubleSpinBox();
    m_gridStepSpin->setRange(0.05, 50.0);
    m_gridStepSpin->setSingleStep(0.5);
    m_gridStepSpin->setValue(0.5);
    m_gridStepSpin->setSuffix(" m");
    connect(m_gridStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onGridStepChanged);
    m_tb1->addWidget(m_gridStepSpin);

    m_actLocalAxes = m_tb1->addAction("Eixos Locais");
    m_actLocalAxes->setCheckable(true);
    m_actLocalAxes->setChecked(false);
    connect(m_actLocalAxes, &QAction::toggled, this, &MainWindow::onShowLocalAxesToggled);

    m_tb1->addSeparator();

    // Botões de Câmera Rápida
    m_actViewIsoTb = m_tb1->addAction("3D Iso");
    connect(m_actViewIsoTb, &QAction::triggered, m_viewport, &Viewport3D::viewIsometric);
    m_actViewFrontTb = m_tb1->addAction("Frontal");
    connect(m_actViewFrontTb, &QAction::triggered, m_viewport, &Viewport3D::viewFrontalXZ);
    m_actViewTopTb = m_tb1->addAction("Planta");
    connect(m_actViewTopTb, &QAction::triggered, m_viewport, &Viewport3D::viewTopXY);
    m_actViewSideTb = m_tb1->addAction("Lateral");
    connect(m_actViewSideTb, &QAction::triggered, m_viewport, &Viewport3D::viewSideYZ);
    m_actViewFitTb = m_tb1->addAction("Enquadrar (F)");
    m_actViewFitTb->setShortcut(QKeySequence(Qt::Key_F));
    connect(m_actViewFitTb, &QAction::triggered, m_viewport, &Viewport3D::fitAll);

    // -------------------------------------------------------------
    // Toolbar 2: Atribuições de Engenharia e Pós-Processamento
    // -------------------------------------------------------------
    addToolBarBreak();
    m_tb2 = addToolBar("Engenharia e Resultados");
    m_tb2->setMovable(false);

    m_actTbMaterials = m_tb2->addAction("Materiais...", this, &MainWindow::onAssignMaterials);
    m_actTbSections = m_tb2->addAction("Seções...", this, &MainWindow::onAssignSections);
    m_actTbSupports = m_tb2->addAction("Apoios...", this, &MainWindow::onAssignSupports);
    m_actTbReleases = m_tb2->addAction("Rótulas...", this, &MainWindow::onAssignReleases);
    m_actTbNodalLoads = m_tb2->addAction("Carga Nodal...", this, &MainWindow::onAssignNodalLoads);
    m_actTbDistLoads = m_tb2->addAction("Carga Distr....", this, &MainWindow::onAssignDistributedLoads);
    m_actTbThermalLoads = m_tb2->addAction("Térmica (ΔT)...", this, &MainWindow::onAssignThermalLoads);
    m_actTbRollAngle = m_tb2->addAction("Roll (γ)...", this, &MainWindow::onAssignRollAngle);
    m_actTbCombos = m_tb2->addAction("Combinações...", this, &MainWindow::onOpenLoadCombinations);
    m_actTbMesh = m_tb2->addAction("Malha 2D/3D...", this, &MainWindow::onOpenMeshGenerator);

    m_tb2->addSeparator();

    // Botão de Destaque Verde para Cálculo
    m_btnSolve = new QPushButton(" ▶ CALCULAR ESTRUTURA (F5)");
    connect(m_btnSolve, &QPushButton::clicked, this, &MainWindow::onSolveStructure);
    m_tb2->addWidget(m_btnSolve);

    m_actTbAdvAnalysis = m_tb2->addAction("Avançadas...", this, &MainWindow::onOpenAnalysisSuite);
    m_actTbPostCtrl = m_tb2->addAction("Corte/Contornos...", this, &MainWindow::onOpenPostControls);
    m_actTbNbrAudit = m_tb2->addAction("Auditoria NBR...", this, &MainWindow::onOpenNbrCompliance);

    m_tb2->addSeparator();

    m_diagLabel = new QLabel(" Diagrama: ");
    m_tb2->addWidget(m_diagLabel);
    m_diagramCombo = new QComboBox();
    m_diagramCombo->addItem("Nenhum (Geometria)", static_cast<int>(DiagramType::None));
    m_diagramCombo->addItem("Deformada 3D", static_cast<int>(DiagramType::Deformed));
    m_diagramCombo->addItem("Normal (N)", static_cast<int>(DiagramType::Normal_N));
    m_diagramCombo->addItem("Cortante (Vy)", static_cast<int>(DiagramType::Shear_Vy));
    m_diagramCombo->addItem("Cortante (Vz)", static_cast<int>(DiagramType::Shear_Vz));
    m_diagramCombo->addItem("Torsor (Tx)", static_cast<int>(DiagramType::Torsion_Tx));
    m_diagramCombo->addItem("Fletor (My)", static_cast<int>(DiagramType::Moment_My));
    m_diagramCombo->addItem("Fletor (Mz)", static_cast<int>(DiagramType::Moment_Mz));
    connect(m_diagramCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDiagramChanged);
    m_tb2->addWidget(m_diagramCombo);

    m_actReactions = m_tb2->addAction("Reações");
    m_actReactions->setCheckable(true);
    m_actReactions->setChecked(true);
    connect(m_actReactions, &QAction::toggled, this, &MainWindow::onShowReactionsToggled);

    m_scaleLabel = new QLabel(" Escala: ");
    m_tb2->addWidget(m_scaleLabel);
    m_scaleSlider = new QSlider(Qt::Horizontal);
    m_scaleSlider->setRange(1, 100);
    m_scaleSlider->setValue(30);
    m_scaleSlider->setMaximumWidth(120);
    connect(m_scaleSlider, &QSlider::valueChanged, this, &MainWindow::onDiagramScaleChanged);
    m_tb2->addWidget(m_scaleSlider);

    ThemeManager::instance().refreshToolbarExtensionButtons();
    updateToolbarExtensionToolTips(m_tb1, m_tb2);
}

void MainWindow::setupDocks() {
    // -------------------------------------------------------------
    // Dock Esquerdo: Inspetor de Propriedades e Entrada Numérica
    // -------------------------------------------------------------
    m_inspectorDock = new QDockWidget(locale::GerenciadorTraducao::trText("Inspetor e Entrada Rápida"), this);
    m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* leftContainer = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftContainer);

    // Bloco de Entrada Numérica de Coordenadas
    m_entryGroup = new QGroupBox("Adicionar Nó por Coordenadas");
    auto* entryLayout = new QFormLayout(m_entryGroup);

    m_quickXSpin = new QDoubleSpinBox(); m_quickXSpin->setRange(-1000.0, 1000.0); m_quickXSpin->setDecimals(2); m_quickXSpin->setSuffix(" m");
    m_quickYSpin = new QDoubleSpinBox(); m_quickYSpin->setRange(-1000.0, 1000.0); m_quickYSpin->setDecimals(2); m_quickYSpin->setSuffix(" m");
    m_quickZSpin = new QDoubleSpinBox(); m_quickZSpin->setRange(-1000.0, 1000.0); m_quickZSpin->setDecimals(2); m_quickZSpin->setSuffix(" m");
    auto onQuickCoordChanged = [this](double) {
        if (!m_rebuildingOrLoadingResultsSettings) {
            saveUserPreferences();
        }
    };
    connect(m_quickXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onQuickCoordChanged);
    connect(m_quickYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onQuickCoordChanged);
    connect(m_quickZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onQuickCoordChanged);

    entryLayout->addRow("X:", m_quickXSpin);
    entryLayout->addRow("Y:", m_quickYSpin);
    entryLayout->addRow("Z:", m_quickZSpin);

    m_btnAddNode = new QPushButton("Adicionar Nó");
    connect(m_btnAddNode, &QPushButton::clicked, this, &MainWindow::onQuickAddNode);
    entryLayout->addRow("", m_btnAddNode);

    leftLayout->addWidget(m_entryGroup);

    // Bloco de Conteúdo do Inspetor Dinâmico
    m_inspGroup = new QGroupBox("Elemento / Nó Selecionado");
    m_inspectorLayout = new QVBoxLayout(m_inspGroup);
    m_inspectorLayout->setContentsMargins(4, 4, 4, 4);
    m_inspectorContent = new QWidget();
    m_inspectorLayout->addWidget(m_inspectorContent);

    leftLayout->addWidget(m_inspGroup);
    leftLayout->addStretch();

    leftContainer->setMinimumWidth(300);
    m_inspectorScroll = new QScrollArea(this);
    m_inspectorScroll->setWidgetResizable(true);
    m_inspectorScroll->setFrameShape(QFrame::NoFrame);
    m_inspectorScroll->setWidget(leftContainer);
    m_inspectorDock->setMinimumWidth(320);
    m_inspectorDock->setWidget(createDockContentHost(m_inspectorScroll, m_inspectorDock));
    addDockWidget(Qt::LeftDockWidgetArea, m_inspectorDock);
    m_inspectorTitleBar = attachCollapsibleTitleBar(
        this,
        m_inspectorDock,
        QStringLiteral("inspectorCollapseButton"),
        Qt::LeftArrow,
        Qt::RightArrow,
        [this]() { setInspectorDockCollapsed(!m_inspectorCollapsed); },
        [this](bool floating) { handleInspectorDockTopLevelChanged(floating); });

    // -------------------------------------------------------------
    // Dock Inferior: Tabelas de Resultados e Diagnóstico
    // -------------------------------------------------------------
    m_resultsDock = new QDockWidget(locale::GerenciadorTraducao::trText("Tabelas de Resultados e Diagnóstico Estrutural"), this);
    m_resultsDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    m_resultsSplitter = new QSplitter(Qt::Horizontal, m_resultsDock);
    m_resultsSplitter->setObjectName(QStringLiteral("resultsSplitter"));
    m_resultsSplitter->setChildrenCollapsible(false);
    connect(m_resultsSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_rebuildingOrLoadingResultsSettings || m_resultsSplitter == nullptr) {
            return;
        }
        const int modeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
        const QList<int> sz = m_resultsSplitter->sizes();
        if (sz.size() == m_resultsLayoutCount) {
            int total = 0;
            for (int s : sz) {
                total += s;
            }
            if (total > 0) {
                m_resultsSplitterSizesByMode[static_cast<std::size_t>(modeIdx)] = sz;
            }
        }
        saveUserPreferences();
    });

    m_hiddenTablesHost = new QWidget(this);
    m_hiddenTablesHost->hide();

    for (int i = 0; i < 4; ++i) {
        auto* tabW = new QTabWidget(m_hiddenTablesHost);
        tabW->setObjectName(QStringLiteral("resultsLayoutTabs_%1").arg(i));
        tabW->setUsesScrollButtons(false);
        tabW->setElideMode(Qt::ElideRight);
        if (QTabBar* tb = tabW->tabBar()) {
            tb->setUsesScrollButtons(false);
            tb->setElideMode(Qt::ElideRight);
            tb->setExpanding(false);
            const QList<QToolButton*> tabButtons = tb->findChildren<QToolButton*>();
            for (QToolButton* btn : tabButtons) {
                btn->hide();
                btn->setEnabled(false);
                btn->setFixedSize(0, 0);
            }
        }
        tabW->setStyleSheet(QStringLiteral(
            "QTabBar::scroller { width: 0px; height: 0px; }\n"
            "QTabBar QToolButton { width: 0px; height: 0px; min-width: 0px; max-width: 0px;"
            " min-height: 0px; max-height: 0px; border: none; background: transparent; padding: 0px; margin: 0px; }"));
        connect(tabW, &QTabWidget::currentChanged, this, [this, i](int idx) {
            if (m_rebuildingOrLoadingResultsSettings || i >= m_resultsLayoutCount || idx < 0) {
                return;
            }
            const int modeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
            m_resultsActiveTabByMode[static_cast<std::size_t>(modeIdx)][static_cast<std::size_t>(i)] = idx;
            saveUserPreferences();
        });
        m_resultsLayoutTabWidgets[static_cast<std::size_t>(i)] = tabW;
    }
    m_resultsTabs = m_resultsLayoutTabWidgets[0];

    m_tableConfigurator = new ui::ResultTableConfigurator(m_windowManager, this);
    connect(m_tableConfigurator, &ui::ResultTableConfigurator::nodeDoubleClicked, this, [this](unsigned int nodeId) {
        m_model.selectNode(static_cast<NodeId>(nodeId), true);
        onSelectionChanged();
        if (m_viewport) {
            m_viewport->update();
        }
    });
    connect(m_tableConfigurator, &ui::ResultTableConfigurator::elementDoubleClicked, this, [this](unsigned int elementId) {
        m_model.selectElement(static_cast<ElementId>(elementId), true);
        onSelectionChanged();
        if (m_viewport) {
            m_viewport->update();
        }
    });
    connect(m_tableConfigurator, &ui::ResultTableConfigurator::statusMessageRequested, this, [this](const QString& msg, int) {
        showStatusMessage(msg);
    });
    connect(m_tableConfigurator, &ui::ResultTableConfigurator::configurationChanged, this, [this](QTableView*) {
        if (!m_rebuildingOrLoadingResultsSettings) {
            saveUserPreferences();
        }
    });

    const bool isLight = ThemeManager::instance().isLight();
    const bool nativeUi = ThemeManager::instance().isNativeUi();
    auto createSharedResultView = [&](ui::ResultTableKind kind, const QString& objName) -> QTableView* {
        const std::size_t idx = static_cast<std::size_t>(kind);
        const QStringList hdrs = resultTableHeaders(kind);
        if (m_resultTableModels[idx] == nullptr) {
            auto* sharedModel = new ui::ResultTableModel(this);
            sharedModel->setThemeState(isLight, nativeUi);
            sharedModel->setHeaders(hdrs);
            m_resultTableModels[idx] = sharedModel;
        }
        auto* view = new QTableView(m_hiddenTablesHost);
        view->setObjectName(objName);
        auto* proxy = new ui::ResultTableProxyModel(view);
        proxy->setSourceModel(m_resultTableModels[idx]);
        view->setModel(proxy);
        m_tableConfigurator->configureTable(view, kind, hdrs, isLight);
        return view;
    };

    // Aba 1: Deslocamentos Nodais
    m_dispTable = createSharedResultView(ui::ResultTableKind::Displacements, QStringLiteral("dispTable"));

    // Aba 2: Reações de Apoio
    m_reacTable = createSharedResultView(ui::ResultTableKind::Reactions, QStringLiteral("reacTable"));

    // Aba 3: Esforços nas Barras
    m_forcesTable = createSharedResultView(ui::ResultTableKind::BarForces, QStringLiteral("forcesTable"));

    // Aba 4: Esforços e Tensões 2D/3D (Casca/Sólido)
    m_continuumTable = createSharedResultView(ui::ResultTableKind::ContinuumStress, QStringLiteral("continuumTable"));

    rebuildResultsLayoutsUi();
    m_resultsSplitter->setSizes({1000});

    m_resultsDock->setWidget(createDockContentHost(m_resultsSplitter, m_resultsDock));
    addDockWidget(Qt::BottomDockWidgetArea, m_resultsDock);
    m_resultsTitleBar = attachCollapsibleTitleBar(
        this,
        m_resultsDock,
        QStringLiteral("resultsCollapseButton"),
        Qt::DownArrow,
        Qt::UpArrow,
        [this]() { setResultsDockCollapsed(!m_resultsCollapsed); },
        [this](bool floating) { handleResultsDockTopLevelChanged(floating); });
}

void MainWindow::applyInspectorCollapsedState() {
    if (m_inspectorDock->isFloating()) {
        m_inspectorTitleBar->setVertical(false);
        m_inspectorDock->setFeatures(m_inspectorDock->features() & ~QDockWidget::DockWidgetVerticalTitleBar);
        setDockHostCollapsed(m_inspectorDock, m_inspectorScroll, DockCollapseAxis::Vertical, true);

        int collapsedH = 0;
        applyCollapsedHeightClamp(m_inspectorDock, m_inspectorTitleBar, collapsedH);
        m_inspectorDock->resize(qMax(320, m_inspectorSavedWidth), collapsedH);
        return;
    }

    // Recolhe para a lateral esquerda (ou direita) com barra vertical e rótulo rotacionado -90°.
    m_inspectorTitleBar->setVertical(true);
    m_inspectorDock->setFeatures(m_inspectorDock->features() | QDockWidget::DockWidgetVerticalTitleBar);
    setDockHostCollapsed(m_inspectorDock, m_inspectorScroll, DockCollapseAxis::Horizontal, true);

    const int collapsedW = m_inspectorTitleBar->collapsedDockWidth(m_inspectorDock);
    clearCollapsedDockBounds(m_inspectorDock);
    m_inspectorDock->setMaximumWidth(collapsedW);
    m_inspectorDock->updateGeometry();
    resizeDocks({m_inspectorDock}, {collapsedW}, Qt::Horizontal);
    scheduleDockSeparatorRepaint(this, m_resultsSplitter);
}

void MainWindow::setInspectorDockCollapsed(bool collapsed) {
    if (!m_inspectorDock || !m_inspectorScroll || !m_inspectorTitleBar || m_inspectorCollapsed == collapsed) {
        return;
    }

    const ScopedBoolFlag guard(m_updatingDockCollapse);
    m_inspectorCollapsed = collapsed;
    m_inspectorTitleBar->setCollapsed(collapsed);

    if (collapsed) {
        if (m_inspectorDock->isVisible()) {
            if (m_inspectorDock->width() >= 200) {
                m_inspectorSavedWidth = qMax(320, m_inspectorDock->width());
            }
            if (m_inspectorDock->isFloating() && m_inspectorDock->height() >= 120) {
                m_inspectorSavedFloatingHeight = qMax(260, m_inspectorDock->height());
            }
        }
        applyInspectorCollapsedState();
        if (!m_rebuildingOrLoadingResultsSettings) {
            saveUserPreferences();
        }
        return;
    }

    m_inspectorTitleBar->setVertical(false);
    m_inspectorDock->setFeatures(m_inspectorDock->features() & ~QDockWidget::DockWidgetVerticalTitleBar);
    // Restaura primeiro os limites máximos do host interno antes de aplicar minWidth(320) no dock,
    // evitando que QDockWidgetLayout trave maximumWidth()==320 e oculte a barra vertical de arraste.
    setDockHostCollapsed(m_inspectorDock, m_inspectorScroll, DockCollapseAxis::Horizontal, false);
    clearCollapsedDockBounds(m_inspectorDock);
    m_inspectorDock->setMinimumWidth(320);
    m_inspectorDock->updateGeometry();

    if (m_inspectorDock->isFloating()) {
        m_inspectorDock->resize(qMax(320, m_inspectorDock->width()), qMax(260, m_inspectorSavedFloatingHeight));
    } else {
        resizeDocks({m_inspectorDock}, {qMax(320, m_inspectorSavedWidth)}, Qt::Horizontal);
    }
    m_inspectorDock->setMaximumWidth(QWIDGETSIZE_MAX);
    scheduleDockSeparatorRepaint(this, m_resultsSplitter);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::setResultsDockCollapsed(bool collapsed) {
    if (!m_resultsDock || !m_resultsSplitter || !m_resultsTitleBar || m_resultsCollapsed == collapsed) {
        return;
    }

    const ScopedBoolFlag guard(m_updatingDockCollapse);
    m_resultsCollapsed = collapsed;
    m_resultsTitleBar->setCollapsed(collapsed);

    if (collapsed) {
        if (m_resultsDock->isVisible() && m_resultsDock->height() >= 100) {
            if (m_resultsDock->isFloating()) {
                m_resultsSavedFloatingHeight = qMax(180, m_resultsDock->height());
            } else {
                m_resultsSavedDockedHeight = qMax(140, m_resultsDock->height());
            }
        }

        setDockHostCollapsed(m_resultsDock, m_resultsSplitter, DockCollapseAxis::Vertical, true);
        int collapsedH = 0;
        applyCollapsedHeightClamp(m_resultsDock, m_resultsTitleBar, collapsedH);

        if (m_resultsDock->isFloating()) {
            m_resultsDock->resize(m_resultsDock->width(), collapsedH);
        } else {
            resizeDocks({m_resultsDock}, {collapsedH}, Qt::Vertical);
        }
        scheduleDockSeparatorRepaint(this, m_resultsSplitter);
        if (!m_rebuildingOrLoadingResultsSettings) {
            saveUserPreferences();
        }
        return;
    }

    setDockHostCollapsed(m_resultsDock, m_resultsSplitter, DockCollapseAxis::Vertical, false);
    clearCollapsedDockBounds(m_resultsDock);
    for (int i = 0; i < 4; ++i) {
        if (m_resultsLayoutTabWidgets[static_cast<std::size_t>(i)]) {
            m_resultsLayoutTabWidgets[static_cast<std::size_t>(i)]->setVisible(i < m_resultsLayoutCount);
        }
    }
    m_resultsDock->updateGeometry();

    if (m_resultsDock->isFloating()) {
        m_resultsDock->resize(m_resultsDock->width(), qMax(180, m_resultsSavedFloatingHeight));
    } else {
        resizeDocks({m_resultsDock}, {qMax(140, m_resultsSavedDockedHeight)}, Qt::Vertical);
    }
    m_resultsDock->setMaximumHeight(QWIDGETSIZE_MAX);
    scheduleDockSeparatorRepaint(this, m_resultsSplitter);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::handleInspectorDockTopLevelChanged(bool floating) {
    Q_UNUSED(floating);
    if (m_updatingDockCollapse || !m_inspectorCollapsed || !m_inspectorDock || !m_inspectorTitleBar) {
        scheduleDockSeparatorRepaint(this, m_resultsSplitter);
        return;
    }

    const ScopedBoolFlag guard(m_updatingDockCollapse);
    applyInspectorCollapsedState();
}

void MainWindow::handleResultsDockTopLevelChanged(bool floating) {
    if (m_updatingDockCollapse || !m_resultsCollapsed || !m_resultsDock || !m_resultsTitleBar) {
        scheduleDockSeparatorRepaint(this, m_resultsSplitter);
        return;
    }

    const ScopedBoolFlag guard(m_updatingDockCollapse);
    int collapsedH = 0;
    applyCollapsedHeightClamp(m_resultsDock, m_resultsTitleBar, collapsedH);

    if (floating) {
        m_resultsDock->resize(qMax(360, m_resultsDock->width()), collapsedH);
    } else {
        resizeDocks({m_resultsDock}, {collapsedH}, Qt::Vertical);
    }
    scheduleDockSeparatorRepaint(this, m_resultsSplitter);
}

void MainWindow::setupStatusBar() {
    m_cursorCoordLabel = new QLabel("X: 0.00 m | Y: 0.00 m | Z: 0.00 m");
    m_cursorCoordLabel->setMinimumWidth(260);

    m_modelStatsLabel = new QLabel(locale::GerenciadorTraducao::trText("Nós: 0 | Barras: 0"));
    m_modelStatsLabel->setMinimumWidth(180);

    m_solverStatusLabel = new QLabel(locale::GerenciadorTraducao::trText("Pronto para calcular."));
    m_statusProgress = new QProgressBar(this);
    m_statusProgress->setRange(0, 1);
    m_statusProgress->setValue(0);
    m_statusProgress->setMaximumWidth(140);
    m_statusProgress->setTextVisible(false);
    m_statusProgress->setVisible(false);

    statusBar()->addWidget(m_cursorCoordLabel);
    statusBar()->addWidget(m_modelStatsLabel);
    statusBar()->addWidget(m_solverStatusLabel, 1);
    statusBar()->addPermanentWidget(m_statusProgress);
}

void MainWindow::applyDefaultExampleWorkPlane() {
    if (m_planeCombo) {
        const int xyIdx = m_planeCombo->findData(static_cast<int>(WorkPlane::XY_Top));
        if (xyIdx >= 0) {
            m_planeCombo->setCurrentIndex(xyIdx);
        }
    }
    if (m_planeOffsetSpin) {
        m_planeOffsetSpin->setValue(0.0);
    }
    if (m_viewport) {
        m_viewport->setWorkPlane(WorkPlane::XY_Top);
        m_viewport->setPlaneOffset(0.0);
    }
}

// Exemplos Prontos
void MainWindow::loadPredefinedExample(io::SessionSourceKind kind,
                                       StructuralModel model,
                                       const QString& statusMsg) {
    m_currentProjectFilePath.clear();
    m_sessionSourceKind = kind;
    m_model = std::move(model);
    applyDefaultExampleWorkPlane();
    refreshAfterModelLoad(false, true);
    onSolveStructure();
    persistCurrentSessionState();
    updateWindowTitleAndFileActions();
    showStatusMessage(statusMsg);
}

void MainWindow::loadExampleBuilding() {
    loadPredefinedExample(
        io::SessionSourceKind::ExampleBuilding,
        StructuralModel::createBuildingFrameExample(),
        locale::GerenciadorTraducao::trText("Exemplo 1 carregado: Pórtico Espacial de Edifício (2 Pavimentos)."));
}

void MainWindow::loadExampleCantilever() {
    loadPredefinedExample(
        io::SessionSourceKind::ExampleCantilever,
        StructuralModel::createCantileverLFrameExample(),
        locale::GerenciadorTraducao::trText("Exemplo 2 carregado: Balanço Espacial em L com Flexo-Torção."));
}

void MainWindow::loadExampleSpaceTruss() {
    loadPredefinedExample(
        io::SessionSourceKind::ExampleSpaceTruss,
        StructuralModel::createSpaceTrussExample(),
        locale::GerenciadorTraducao::trText("Exemplo 3 carregado: Passarela Treliçada Espacial 3D."));
}

bool MainWindow::openProjectFileFromCommandLine(const QString& filePath) {
    if (filePath.trimmed().isEmpty()) {
        return false;
    }
    m_openedFromCli = true;
    const QString lower = filePath.toLower();
    if (lower.endsWith(QStringLiteral(".dxf"))) {
        StructuralModel importedModel;
        kernel::MeshContainer importedMesh;
        io::DxfInteropReport rep;
        if (!io::DxfInterop::importFromFile(filePath.toUtf8().toStdString(), importedModel, &importedMesh, io::DxfImportOptions{}, &rep) || !rep.success) {
            showStatusMessage(locale::GerenciadorTraducao::trText("Falha ao importar arquivo DXF: %1")
                .arg(QString::fromStdString(rep.errorMessage)));
            return false;
        }
        if (!importedMesh.nodes.empty() || !importedMesh.elements.empty()) {
            importedModel.surfaceMesh() = std::move(importedMesh);
        }
        m_currentProjectFilePath.clear();
        m_sessionSourceKind = io::SessionSourceKind::UnsavedUntitled;
        m_model = std::move(importedModel);
        refreshAfterModelLoad(true, true);
        showStatusMessage(locale::GerenciadorTraducao::trText("Arquivo carregado com sucesso: %1").arg(filePath));
        return true;
    }
    if (lower.endsWith(QStringLiteral(".ifc"))) {
        StructuralModel importedModel;
        kernel::MeshContainer importedMesh;
        io::IfcGuidMap guidMap;
        io::IfcInteropReport rep;
        if (!io::IfcInterop::importFromFile(filePath.toUtf8().toStdString(), importedModel, &importedMesh, &guidMap, &rep) || !rep.success) {
            showStatusMessage(locale::GerenciadorTraducao::trText("Não foi possível carregar o arquivo especificado."));
            return false;
        }
        if (!importedMesh.nodes.empty() || !importedMesh.elements.empty()) {
            importedModel.surfaceMesh() = std::move(importedMesh);
        }
        m_currentProjectFilePath.clear();
        m_sessionSourceKind = io::SessionSourceKind::UnsavedUntitled;
        m_model = std::move(importedModel);
        refreshAfterModelLoad(true, true);
        showStatusMessage(locale::GerenciadorTraducao::trText("Arquivo carregado com sucesso: %1").arg(filePath));
        return true;
    }

    StructuralModel loadedModel;
    if (!ProjectSerializer::loadFromFile(filePath, loadedModel)) {
        showStatusMessage(locale::GerenciadorTraducao::trText("Não foi possível carregar o arquivo especificado."));
        return false;
    }
    m_model = std::move(loadedModel);
    m_currentProjectFilePath = filePath;
    m_sessionSourceKind = io::SessionSourceKind::SavedProject;
    clearDirtyState(false);
    refreshAfterModelLoad(false, true);
    persistCurrentSessionState();
    updateWindowTitleAndFileActions();
    showStatusMessage(locale::GerenciadorTraducao::trText("Arquivo carregado com sucesso: %1").arg(filePath));
    return true;
}

void MainWindow::refreshAfterModelLoad(bool markDirty, bool fitViewport) {
    if (m_recoveryPromptBox) {
        m_recoveryPromptBtnAccept.clear();
        m_recoveryPromptBtnReject.clear();
        m_recoveryPromptBox->close();
    }
    m_pendingResolve = false;
    m_pendingResolveInteractive = false;
    if (!m_staticSolveContinuations.empty()) {
        auto oldContinuations = std::move(m_staticSolveContinuations);
        m_staticSolveContinuations.clear();
        for (const auto& cb : oldContinuations) {
            if (cb) {
                cb(false);
            }
        }
    }
    {
        const ScopedBoolFlag loadGuard(m_loadingOrTemplateModel);
        m_hasSolvedContext = false;
        m_lastSolverResult = SolverResult();
        if (m_viewport) {
            m_viewport->setModel(&m_model);
            m_viewport->setClipPlane(Viewport3D::ClipAxis::None, 0.0, false);
        }
        if (m_postProcessControlDialog) {
            m_postProcessControlDialog->syncFromViewport();
        }
        onModelModified();
        if (fitViewport && m_viewport) {
            m_viewport->fitAll();
        }
    }
    if (markDirty) {
        markProjectDirty();
    } else {
        m_hasUnsavedEdits = false;
        if (m_autoSaveTimer) {
            m_autoSaveTimer->stop();
        }
        updateWindowTitleAndFileActions();
    }
}

void MainWindow::markProjectDirty() {
    m_sessionStatePersistedOnClose = false;
    m_hasUnsavedEdits = true;
    if (m_sessionSourceKind == io::SessionSourceKind::Empty) {
        m_sessionSourceKind = m_currentProjectFilePath.trimmed().isEmpty()
            ? io::SessionSourceKind::UnsavedUntitled
            : io::SessionSourceKind::SavedProject;
    }
    if (m_autoSaveTimer) {
        m_autoSaveTimer->start();
    }
    updateWindowTitleAndFileActions();
}

void MainWindow::clearDirtyState(bool clearRecoverySnapshot) {
    m_sessionStatePersistedOnClose = false;
    m_hasUnsavedEdits = false;
    if (m_autoSaveTimer) {
        m_autoSaveTimer->stop();
    }
    if (m_activeAutoSaveThread && m_activeAutoSaveThread->isRunning()) {
        m_activeAutoSaveThread->wait(5000);
    }
    if (clearRecoverySnapshot) {
        io::ProjectRecoveryStore::clearSnapshot();
    }
    updateWindowTitleAndFileActions();
}

void MainWindow::persistCurrentSessionState() {
    if (m_autoSaveTimer) {
        m_autoSaveTimer->stop();
    }
    if (m_activeAutoSaveThread && m_activeAutoSaveThread->isRunning()) {
        m_activeAutoSaveThread->wait(5000);
    }
    if (m_hasUnsavedEdits) {
        const io::SessionSourceKind effectiveKind =
            (m_sessionSourceKind == io::SessionSourceKind::Empty)
                ? (m_currentProjectFilePath.trimmed().isEmpty()
                       ? io::SessionSourceKind::UnsavedUntitled
                       : io::SessionSourceKind::SavedProject)
                : m_sessionSourceKind;
        const StructuralModel snapshot = m_model.cloneWithoutHistory();
        io::ProjectRecoveryStore::saveSnapshot(snapshot, m_currentProjectFilePath, effectiveKind);
    } else if (m_sessionSourceKind != io::SessionSourceKind::Empty) {
        io::ProjectRecoveryStore::saveCleanSessionState(m_model, m_currentProjectFilePath, m_sessionSourceKind);
    } else {
        io::ProjectRecoveryStore::clearSnapshot();
    }
}

void MainWindow::flushRecoverySnapshotIfDirty() {
    if (m_autoSaveTimer) {
        m_autoSaveTimer->stop();
    }
    if (!m_hasUnsavedEdits) {
        persistCurrentSessionState();
        if (m_actRecoverSession) {
            m_actRecoverSession->setEnabled(io::ProjectRecoveryStore::inspectPreviousSession().available);
        }
        return;
    }
    if (m_activeAutoSaveThread && m_activeAutoSaveThread->isRunning()) {
        if (m_autoSaveTimer) {
            m_autoSaveTimer->start();
        }
        return;
    }

    const io::SessionSourceKind effectiveKind =
        (m_sessionSourceKind == io::SessionSourceKind::Empty)
            ? (m_currentProjectFilePath.trimmed().isEmpty()
                   ? io::SessionSourceKind::UnsavedUntitled
                   : io::SessionSourceKind::SavedProject)
            : m_sessionSourceKind;
    auto snapshotPtr = std::make_shared<StructuralModel>(m_model.cloneWithoutHistory());
    const QString projectPath = m_currentProjectFilePath;
    const QString sessionId = io::ProjectRecoveryStore::currentSessionId();

    QThread* worker = QThread::create([snapshotPtr, projectPath, effectiveKind, sessionId]() {
        try {
            io::ProjectRecoveryStore::saveSnapshot(*snapshotPtr, projectPath, effectiveKind, sessionId);
        } catch (const std::exception& ex) {
            io::AppLogger::warning(QStringLiteral("Recovery"),
                                   QStringLiteral("Falha ao gravar snapshot de recuperação: %1")
                                       .arg(QString::fromUtf8(ex.what())));
        } catch (...) {
            io::AppLogger::warning(QStringLiteral("Recovery"),
                                   QStringLiteral("Exceção desconhecida ao gravar snapshot de recuperação."));
        }
    });
    m_activeAutoSaveThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, worker]() {
        if (m_activeAutoSaveThread == worker) {
            m_activeAutoSaveThread = nullptr;
        }
        if (m_actRecoverSession) {
            m_actRecoverSession->setEnabled(io::ProjectRecoveryStore::inspectPreviousSession().available);
        }
    });
    worker->start();
}

void MainWindow::updateWindowTitleAndFileActions() {
    const QString baseTitle = locale::GerenciadorTraducao::trText("OmniFrame 3D - Análise Estrutural Espacial");
    QString title = baseTitle;
    if (!m_currentProjectFilePath.isEmpty()) {
        title = QStringLiteral("%1 - %2").arg(QFileInfo(m_currentProjectFilePath).fileName(), baseTitle);
    }
    if (m_hasUnsavedEdits) {
        title += QStringLiteral(" [*]");
    }
    setWindowModified(m_hasUnsavedEdits);
    setWindowTitle(title);

    if (m_actRecoverSession) {
        m_actRecoverSession->setEnabled(io::ProjectRecoveryStore::inspectPreviousSession().available);
    }
}

QString MainWindow::formatRecoveryMessage(const io::ProjectRecoveryInfo& info,
                                          QString& outAcceptButtonText,
                                          QString& outRejectButtonText) const {
    using locale::GerenciadorTraducao;
    outRejectButtonText = GerenciadorTraducao::trText("Iniciar Novo Projeto");

    auto exampleDisplayName = [](io::SessionSourceKind kind) -> QString {
        switch (kind) {
            case io::SessionSourceKind::ExampleBuilding:
                return GerenciadorTraducao::trText("Exemplo 1: Pórtico Espacial de Edifício");
            case io::SessionSourceKind::ExampleCantilever:
                return GerenciadorTraducao::trText("Exemplo 2: Balanço Espacial em L");
            case io::SessionSourceKind::ExampleSpaceTruss:
                return GerenciadorTraducao::trText("Exemplo 3: Treliça Espacial / Passarela Metálica 3D");
            default:
                return QString();
        }
    };

    const QString fileBaseName = !info.projectFilePath.trimmed().isEmpty()
        ? QFileInfo(info.projectFilePath).fileName()
        : QString();
    const bool isExample =
        info.sourceKind == io::SessionSourceKind::ExampleBuilding ||
        info.sourceKind == io::SessionSourceKind::ExampleCantilever ||
        info.sourceKind == io::SessionSourceKind::ExampleSpaceTruss;

    if (isExample) {
        const QString exName = exampleDisplayName(info.sourceKind);
        if (info.hasUnsavedChanges) {
            outAcceptButtonText = GerenciadorTraducao::trText("Carregar Edição Anterior");
            return GerenciadorTraducao::trText(
                       "Foi encontrada uma edição não salva da sessão anterior no exemplo \"%1\" (%2 nós, %3 barras, %4 elementos 2D/3D).\n\nDeseja carregar a edição anterior ou iniciar um novo projeto?")
                .arg(exName,
                     QString::number(info.nodeCount),
                     QString::number(info.elementCount),
                     QString::number(info.meshElementCount));
        }
        outAcceptButtonText = GerenciadorTraducao::trText("Carregar Exemplo Anterior");
        return GerenciadorTraducao::trText(
                   "Na sessão anterior, o exemplo estrutural \"%1\" estava aberto (%2 nós, %3 barras, %4 elementos 2D/3D).\n\nDeseja carregar o exemplo anterior ou iniciar um novo projeto?")
            .arg(exName,
                 QString::number(info.nodeCount),
                 QString::number(info.elementCount),
                 QString::number(info.meshElementCount));
    }

    const QString projLabel = !fileBaseName.isEmpty()
        ? fileBaseName
        : (!info.projectName.trimmed().isEmpty()
               ? info.projectName.trimmed()
               : GerenciadorTraducao::trText("Projeto Sem Título"));

    if (info.hasUnsavedChanges) {
        outAcceptButtonText = GerenciadorTraducao::trText("Carregar Edição Anterior");
        return GerenciadorTraducao::trText(
                   "Foi encontrada uma edição não salva da sessão anterior no projeto \"%1\" (%2 nós, %3 barras, %4 elementos 2D/3D).\n\nDeseja carregar a edição anterior ou iniciar um novo projeto?")
            .arg(projLabel,
                 QString::number(info.nodeCount),
                 QString::number(info.elementCount),
                 QString::number(info.meshElementCount));
    }

    outAcceptButtonText = GerenciadorTraducao::trText("Carregar Projeto Anterior");
    return GerenciadorTraducao::trText(
               "Na sessão anterior, o projeto \"%1\" estava aberto (%2 nós, %3 barras, %4 elementos 2D/3D).\n\nDeseja carregar o projeto anterior ou iniciar um novo projeto?")
        .arg(projLabel,
             QString::number(info.nodeCount),
             QString::number(info.elementCount),
             QString::number(info.meshElementCount));
}

void MainWindow::updateRecoveryPromptBoxTexts() {
    if (!m_recoveryPromptBox) {
        return;
    }
    QString acceptText;
    QString rejectText;
    const QString msg = formatRecoveryMessage(m_recoveryPromptInfo, acceptText, rejectText);
    m_recoveryPromptBox->setWindowTitle(locale::GerenciadorTraducao::trText("Retomar Sessão Anterior"));
    m_recoveryPromptBox->setText(msg);
    if (m_recoveryPromptBtnAccept) {
        m_recoveryPromptBtnAccept->setText(acceptText);
    }
    if (m_recoveryPromptBtnReject) {
        m_recoveryPromptBtnReject->setText(rejectText);
    }
}

void MainWindow::updateCloseConfirmBoxTexts() {
    if (!m_closeConfirmBox) {
        return;
    }
    m_closeConfirmBox->setWindowTitle(locale::GerenciadorTraducao::trText("Alterações Não Salvas"));
    m_closeConfirmBox->setText(locale::GerenciadorTraducao::trText(
        "Existem alterações não salvas no projeto estrutural (uma cópia de recuperação automática foi gravada em disco).\n\nDeseja salvar em arquivo antes de fechar?"));
    if (m_closeBtnSave) {
        m_closeBtnSave->setText(locale::GerenciadorTraducao::trText("Salvar"));
    }
    if (m_closeBtnExitKeep) {
        m_closeBtnExitKeep->setText(locale::GerenciadorTraducao::trText("Sair e Manter Recuperação"));
    }
    if (m_closeBtnCancel) {
        m_closeBtnCancel->setText(locale::GerenciadorTraducao::trText("Cancelar"));
    }
}

void MainWindow::checkAndOfferStartupRecovery() {
    if (m_openedFromCli ||
        !m_model.getNodes().empty() ||
        !m_model.getElements().empty() ||
        !m_model.surfaceMesh().elements.empty()) {
        io::AppLogger::info(
            QStringLiteral("Startup"),
            QStringLiteral("Inicializacao com modelo ja carregado (CLI ou manual); retomada automatica ignorada."));
        return;
    }
    const io::ProjectRecoveryInfo info = io::ProjectRecoveryStore::inspectPreviousSession();
    if (!info.available) {
        io::AppLogger::info(
            QStringLiteral("Startup"),
            QStringLiteral("Nenhuma sessao anterior disponivel; aplicativo iniciado vazio."));
        return;
    }

    if (m_startupSessionBehavior == io::StartupSessionBehavior::AlwaysStartNew) {
        io::AppLogger::info(
            QStringLiteral("Startup"),
            QStringLiteral("Politica AlwaysStartNew: descartando sessao anterior e iniciando novo projeto vazio."));
        io::ProjectRecoveryStore::clearSnapshot();
        updateWindowTitleAndFileActions();
        showStatusMessage(locale::GerenciadorTraducao::trText(
            "Novo projeto vazio iniciado automaticamente conforme configuração de inicialização."));
        return;
    }

    if (m_startupSessionBehavior == io::StartupSessionBehavior::AutoLoadPrevious) {
        io::AppLogger::info(
            QStringLiteral("Startup"),
            QStringLiteral("Politica AutoLoadPrevious: carregando automaticamente sessao anterior (sourceKind=%1, unsaved=%2).")
                .arg(static_cast<int>(info.sourceKind))
                .arg(info.hasUnsavedChanges ? QStringLiteral("true") : QStringLiteral("false")));
        if (!recoverPreviousSession()) {
            io::AppLogger::warning(
                QStringLiteral("Startup"),
                QStringLiteral("Falha ao recarregar automaticamente sessao anterior; mantendo novo projeto vazio."));
            io::ProjectRecoveryStore::clearSnapshot();
            updateWindowTitleAndFileActions();
            showStatusMessage(locale::GerenciadorTraducao::trText(
                "Não foi possível carregar automaticamente a sessão anterior; novo projeto vazio iniciado."));
        }
        return;
    }

    io::AppLogger::info(
        QStringLiteral("Startup"),
        QStringLiteral("Politica AskOnStartup: exibindo dialogo de retomada (sourceKind=%1, unsaved=%2, file=%3).")
            .arg(QString::number(static_cast<int>(info.sourceKind)),
                 info.hasUnsavedChanges ? QStringLiteral("true") : QStringLiteral("false"),
                 info.projectFilePath));

    m_recoveryPromptInfo = info;
    if (m_recoveryPromptBox) {
        updateRecoveryPromptBoxTexts();
        m_windowManager->showAndRaise(m_recoveryPromptBox.data());
        return;
    }
    auto* box = new QMessageBox(nullptr);
    m_recoveryPromptBox = box;
    box->setIcon(QMessageBox::Question);
    QPushButton* btnAccept = box->addButton(QString(), QMessageBox::AcceptRole);
    QPushButton* btnReject = box->addButton(QString(), QMessageBox::RejectRole);
    m_recoveryPromptBtnAccept = btnAccept;
    m_recoveryPromptBtnReject = btnReject;
    updateRecoveryPromptBoxTexts();
    box->setDefaultButton(btnAccept);
    m_windowManager->prepareModelessDialog(box, false, true);
    connect(box, &QMessageBox::finished, this, [this, boxPtr = QPointer<QMessageBox>(box)](int) {
        if (!boxPtr) return;
        QAbstractButton* clicked = boxPtr->clickedButton();
        if (clicked != nullptr && clicked == m_recoveryPromptBtnAccept) {
            io::AppLogger::info(QStringLiteral("Startup"), QStringLiteral("Usuario escolheu retomar a sessao anterior."));
            recoverPreviousSession();
        } else if (clicked != nullptr && clicked == m_recoveryPromptBtnReject) {
            io::AppLogger::info(QStringLiteral("Startup"), QStringLiteral("Usuario escolheu iniciar um novo projeto vazio."));
            io::ProjectRecoveryStore::clearSnapshot();
            updateWindowTitleAndFileActions();
        }
    });
    m_windowManager->showAndRaise(box);
}

void MainWindow::promptUnsavedChangesBeforeClose() {
    if (m_closeConfirmBox) {
        updateCloseConfirmBoxTexts();
        m_windowManager->showAndRaise(m_closeConfirmBox.data());
        return;
    }
    auto* box = new QMessageBox(nullptr);
    m_closeConfirmBox = box;
    box->setIcon(QMessageBox::Warning);
    m_closeBtnSave = box->addButton(QString(), QMessageBox::AcceptRole);
    m_closeBtnExitKeep = box->addButton(QString(), QMessageBox::DestructiveRole);
    m_closeBtnCancel = box->addButton(QString(), QMessageBox::RejectRole);
    updateCloseConfirmBoxTexts();
    box->setDefaultButton(m_closeBtnSave);
    m_windowManager->prepareModelessDialog(box, false, true);
    connect(box, &QMessageBox::finished, this, [this, boxPtr = QPointer<QMessageBox>(box)](int) {
        if (!boxPtr) return;
        QAbstractButton* clicked = boxPtr->clickedButton();
        if (clicked != nullptr && clicked == m_closeBtnSave) {
            m_closePendingAfterSave = true;
            onSaveProject();
        } else if (clicked != nullptr && clicked == m_closeBtnExitKeep) {
            persistCurrentSessionState();
            m_closingConfirmed = true;
            close();
        }
    });
    m_windowManager->showAndRaise(box);
}

// Ações de Arquivo
void MainWindow::onNewProject() {
    m_windowManager->showModelessMessage(
        QMessageBox::Question,
        locale::GerenciadorTraducao::trText("Novo Projeto"),
        locale::GerenciadorTraducao::trText("Deseja limpar o modelo atual e iniciar um novo projeto?"),
        QMessageBox::Yes | QMessageBox::No,
        [this](int reply) {
            if (reply == QMessageBox::Yes) {
                m_currentProjectFilePath.clear();
                m_sessionSourceKind = io::SessionSourceKind::Empty;
                m_model.clear();
                clearDirtyState(true);
                refreshAfterModelLoad(false, true);
                if (m_viewport) m_viewport->update();
                showStatusMessage(locale::GerenciadorTraducao::trText("Novo projeto iniciado."));
            }
        }
    );
}

void MainWindow::onOpenProject() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptOpen,
        QFileDialog::ExistingFile,
        locale::GerenciadorTraducao::trText("Abrir Arquivo OmniFrame"),
        initialFileDialogPath(),
        locale::GerenciadorTraducao::trText("Arquivos OmniFrame (*.omniframe);;Todos os Arquivos (*.*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            rememberFileDialogPath(path);
            StructuralModel loadedModel;
            if (ProjectSerializer::loadFromFile(path, loadedModel)) {
                m_model = std::move(loadedModel);
                m_currentProjectFilePath = path;
                m_sessionSourceKind = io::SessionSourceKind::SavedProject;
                clearDirtyState(false);
                refreshAfterModelLoad(false, true);
                persistCurrentSessionState();
                updateWindowTitleAndFileActions();
                showStatusMessage(locale::GerenciadorTraducao::trText("Arquivo carregado com sucesso: %1").arg(path));
            } else {
                io::AppLogger::critical(
                    QStringLiteral("Project"),
                    QStringLiteral("Falha ao abrir arquivo de projeto: %1").arg(path));
                m_windowManager->showModelessMessage(
                    QMessageBox::Critical,
                    locale::GerenciadorTraducao::trText("Erro"),
                    locale::GerenciadorTraducao::trText("Não foi possível carregar o arquivo especificado.")
                );
            }
        }
    );
}

bool MainWindow::saveProjectToPath(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    rememberFileDialogPath(path);
    if (ProjectSerializer::saveToFile(path, m_model)) {
        m_currentProjectFilePath = path;
        m_sessionSourceKind = io::SessionSourceKind::SavedProject;
        clearDirtyState(false);
        persistCurrentSessionState();
        updateWindowTitleAndFileActions();
        showStatusMessage(locale::GerenciadorTraducao::trText("Projeto salvo com sucesso: %1").arg(path));
        if (m_closePendingAfterSave) {
            m_closePendingAfterSave = false;
            m_closingConfirmed = true;
            close();
        }
        return true;
    }
    m_closePendingAfterSave = false;
    io::AppLogger::critical(
        QStringLiteral("Project"),
        QStringLiteral("Falha ao salvar arquivo de projeto: %1").arg(path));
    m_windowManager->showModelessMessage(
        QMessageBox::Critical,
        locale::GerenciadorTraducao::trText("Erro"),
        locale::GerenciadorTraducao::trText("Não foi possível salvar o arquivo.")
    );
    return false;
}

void MainWindow::onSaveProject() {
    if (!m_currentProjectFilePath.isEmpty()) {
        saveProjectToPath(m_currentProjectFilePath);
        return;
    }
    onSaveProjectAs();
}

void MainWindow::onSaveProjectAs() {
    const QString suggestedName = m_currentProjectFilePath.isEmpty()
        ? QStringLiteral("modelo.omniframe")
        : QFileInfo(m_currentProjectFilePath).fileName();
    QFileDialog* dlg = m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptSave,
        QFileDialog::AnyFile,
        locale::GerenciadorTraducao::trText("Salvar Arquivo OmniFrame"),
        initialFileDialogPath(suggestedName),
        locale::GerenciadorTraducao::trText("Arquivos OmniFrame (*.omniframe)"),
        [this](const QString& path) {
            if (path.isEmpty()) {
                m_closePendingAfterSave = false;
                return;
            }
            saveProjectToPath(path);
        }
    );
    if (dlg) {
        connect(dlg, &QDialog::rejected, this, [this]() {
            m_closePendingAfterSave = false;
        });
    }
}

bool MainWindow::recoverPreviousSession() {
    const io::ProjectRecoveryInfo info = io::ProjectRecoveryStore::inspectPreviousSession();
    if (!info.available) {
        return false;
    }

    if (info.hasUnsavedChanges) {
        StructuralModel recoveredModel;
        io::ProjectRecoveryInfo loadedInfo;
        if (!io::ProjectRecoveryStore::loadSnapshot(recoveredModel, &loadedInfo)) {
            return false;
        }
        m_model = std::move(recoveredModel);
        m_currentProjectFilePath = loadedInfo.projectFilePath;
        m_sessionSourceKind = loadedInfo.sourceKind;
        refreshAfterModelLoad(true, true);
        showStatusMessage(
            locale::GerenciadorTraducao::trText("Sessão anterior recuperada: %1 nós, %2 barras.")
                .arg(m_model.getNodes().size())
                .arg(m_model.getElements().size()));
        return true;
    }

    if (info.sourceKind == io::SessionSourceKind::SavedProject && !info.projectFilePath.trimmed().isEmpty()) {
        StructuralModel loadedModel;
        if (!ProjectSerializer::loadFromFile(info.projectFilePath, loadedModel)) {
            return false;
        }
        m_model = std::move(loadedModel);
        m_currentProjectFilePath = info.projectFilePath;
        m_sessionSourceKind = io::SessionSourceKind::SavedProject;
        clearDirtyState(false);
        refreshAfterModelLoad(false, true);
        persistCurrentSessionState();
        updateWindowTitleAndFileActions();
        showStatusMessage(locale::GerenciadorTraducao::trText("Arquivo carregado com sucesso: %1").arg(info.projectFilePath));
        return true;
    }

    switch (info.sourceKind) {
        case io::SessionSourceKind::ExampleBuilding:
            loadExampleBuilding();
            return true;
        case io::SessionSourceKind::ExampleCantilever:
            loadExampleCantilever();
            return true;
        case io::SessionSourceKind::ExampleSpaceTruss:
            loadExampleSpaceTruss();
            return true;
        default:
            return false;
    }
}

void MainWindow::onRecoverSession() {
    if (!recoverPreviousSession()) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            locale::GerenciadorTraducao::trText("Recuperação de Sessão"),
            locale::GerenciadorTraducao::trText("Não há projeto ou edição anterior disponível para carregar.")
        );
    }
}

void MainWindow::onRequestQuit() {
    const ScopedBoolFlag quitGuard(m_userRequestedQuit);
    close();
}

void MainWindow::onExportHtmlReport() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptSave,
        QFileDialog::AnyFile,
        locale::GerenciadorTraducao::trText("Exportar Relatório HTML"),
        initialFileDialogPath(QStringLiteral("Relatorio_Estrutural.html")),
        locale::GerenciadorTraducao::trText("Documento HTML (*.html)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            rememberFileDialogPath(path);
            if (ProjectSerializer::exportTechnicalReportHtml(path, m_model, m_lastSolverResult)) {
                showStatusMessage(locale::GerenciadorTraducao::trText("Relatório HTML exportado com sucesso: %1").arg(path));
                m_windowManager->showModelessMessage(
                    QMessageBox::Information,
                    locale::GerenciadorTraducao::trText("Sucesso"),
                    locale::GerenciadorTraducao::trText("Relatório técnico HTML exportado com sucesso!")
                );
            } else {
                m_windowManager->showModelessMessage(
                    QMessageBox::Critical,
                    locale::GerenciadorTraducao::trText("Erro"),
                    locale::GerenciadorTraducao::trText("Falha ao exportar relatório HTML.")
                );
            }
        }
    );
}

void MainWindow::onExportMarkdownReport() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptSave,
        QFileDialog::AnyFile,
        locale::GerenciadorTraducao::trText("Exportar Relatório Markdown"),
        initialFileDialogPath(QStringLiteral("Relatorio_Estrutural.md")),
        locale::GerenciadorTraducao::trText("Documento Markdown (*.md)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            rememberFileDialogPath(path);
            if (ProjectSerializer::exportTechnicalReportMarkdown(path, m_model, m_lastSolverResult)) {
                showStatusMessage(locale::GerenciadorTraducao::trText("Relatório Markdown exportado com sucesso: %1").arg(path));
                m_windowManager->showModelessMessage(
                    QMessageBox::Information,
                    locale::GerenciadorTraducao::trText("Sucesso"),
                    locale::GerenciadorTraducao::trText("Relatório técnico Markdown exportado com sucesso!")
                );
            } else {
                m_windowManager->showModelessMessage(
                    QMessageBox::Critical,
                    locale::GerenciadorTraducao::trText("Erro"),
                    locale::GerenciadorTraducao::trText("Falha ao exportar relatório Markdown.")
                );
            }
        }
    );
}

void MainWindow::onImportDxf() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptOpen,
        QFileDialog::ExistingFile,
        locale::GerenciadorTraducao::trText("Importar CAD DXF"),
        initialFileDialogPath(),
        locale::GerenciadorTraducao::trText("Desenhos CAD DXF (*.dxf);;Todos (*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            rememberFileDialogPath(path);
            StructuralModel importedModel;
            kernel::MeshContainer importedMesh;
            io::DxfImportOptions opts;
            io::DxfInteropReport rep;
            if (!io::DxfInterop::importFromFile(path.toStdString(), importedModel, &importedMesh, opts, &rep) || !rep.success) {
                const QString err = QString::fromStdString(rep.errorMessage);
                m_windowManager->showModelessMessage(
                    QMessageBox::Warning,
                    locale::GerenciadorTraducao::trText("Erro ao Importar DXF"),
                    locale::GerenciadorTraducao::trText("Falha ao importar arquivo DXF: %1").arg(err)
                );
                return;
            }
            if (!importedMesh.nodes.empty() || !importedMesh.elements.empty()) {
                importedModel.surfaceMesh() = std::move(importedMesh);
            }
            m_currentProjectFilePath.clear();
            m_sessionSourceKind = io::SessionSourceKind::UnsavedUntitled;
            m_model = std::move(importedModel);
            refreshAfterModelLoad(true, true);
            showStatusMessage(
                locale::GerenciadorTraducao::trText("DXF importado: %1 barras, %2 faces (%3 nós fundidos).")
                    .arg(rep.lineCount)
                    .arg(rep.faceCount)
                    .arg(rep.mergedNodes));
        }
    );
}

void MainWindow::onExportDxf() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptSave,
        QFileDialog::AnyFile,
        locale::GerenciadorTraducao::trText("Exportar CAD DXF"),
        initialFileDialogPath(QStringLiteral("Modelo_OmniFrame.dxf")),
        locale::GerenciadorTraducao::trText("Desenhos CAD DXF (*.dxf);;Todos (*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            rememberFileDialogPath(path);
            io::DxfInteropReport rep;
            const kernel::MeshContainer* meshPtr =
                m_model.surfaceMesh().elements.empty() ? nullptr : &m_model.surfaceMesh();
            if (io::DxfInterop::exportToFile(path.toStdString(), m_model, meshPtr, &rep) && rep.success) {
                showStatusMessage(
                    locale::GerenciadorTraducao::trText("DXF exportado: %1 linhas e %2 faces.")
                        .arg(rep.lineCount)
                        .arg(rep.faceCount));
            } else {
                m_windowManager->showModelessMessage(
                    QMessageBox::Critical,
                    locale::GerenciadorTraducao::trText("Erro ao Exportar DXF"),
                    QString::fromStdString(rep.errorMessage));
            }
        }
    );
}

void MainWindow::onImportIfc() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptOpen,
        QFileDialog::ExistingFile,
        locale::GerenciadorTraducao::trText("Importar Modelo BIM IFC4"),
        initialFileDialogPath(),
        locale::GerenciadorTraducao::trText("Modelos BIM IFC (*.ifc);;Todos (*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            rememberFileDialogPath(path);
            StructuralModel importedModel;
            kernel::MeshContainer importedMesh;
            io::IfcGuidMap guidMap;
            io::IfcInteropReport rep;
            if (!io::IfcInterop::importFromFile(path.toStdString(), importedModel, &importedMesh, &guidMap, &rep) || !rep.success) {
                m_windowManager->showModelessMessage(
                    QMessageBox::Warning,
                    locale::GerenciadorTraducao::trText("Erro ao Importar IFC4"),
                    QString::fromStdString(rep.errorMessage));
                return;
            }
            if (!importedMesh.nodes.empty() || !importedMesh.elements.empty()) {
                importedModel.surfaceMesh() = std::move(importedMesh);
            }
            m_currentProjectFilePath.clear();
            m_sessionSourceKind = io::SessionSourceKind::UnsavedUntitled;
            m_model = std::move(importedModel);
            refreshAfterModelLoad(true, true);
            showStatusMessage(
                locale::GerenciadorTraducao::trText("IFC4 importado: %1 nós estruturais, %2 barras, %3 superfícies.")
                    .arg(rep.structuralNodesImported)
                    .arg(rep.curveMembersImported)
                    .arg(rep.surfaceMembersImported));
        }
    );
}

void MainWindow::onExportIfc() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptSave,
        QFileDialog::AnyFile,
        locale::GerenciadorTraducao::trText("Exportar Modelo BIM IFC4"),
        initialFileDialogPath(QStringLiteral("Modelo_OmniFrame.ifc")),
        locale::GerenciadorTraducao::trText("Modelos BIM IFC (*.ifc);;Todos (*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            rememberFileDialogPath(path);
            io::IfcGuidMap guidMap;
            io::IfcInteropReport rep;
            const kernel::MeshContainer* meshPtr =
                m_model.surfaceMesh().elements.empty() ? nullptr : &m_model.surfaceMesh();
            if (io::IfcInterop::exportToFile(path.toStdString(), m_model, meshPtr, &guidMap, &rep) && rep.success) {
                showStatusMessage(
                    locale::GerenciadorTraducao::trText("IFC4 exportado (%1 entidades STEP): %2")
                        .arg(QString::number(rep.entitiesExported), path));
            } else {
                m_windowManager->showModelessMessage(
                    QMessageBox::Critical,
                    locale::GerenciadorTraducao::trText("Erro ao Exportar IFC4"),
                    QString::fromStdString(rep.errorMessage));
            }
        }
    );
}

bool MainWindow::hasValidStaticResultForCurrentRevision() const noexcept {
    return m_lastSolverResult.success && m_lastSolvedRevision == m_modelRevision;
}

void MainWindow::completeBackgroundStaticSolve(bool ok) {
    emit backgroundSolverFinished(ok);
    if (!m_staticSolveContinuations.empty()) {
        auto callbacks = std::move(m_staticSolveContinuations);
        m_staticSolveContinuations.clear();
        for (const auto& cb : callbacks) {
            if (cb) {
                cb(ok);
            }
        }
    }
}

void MainWindow::ensureStaticSolvedAsync(std::function<void(bool)> onFinished) {
    const bool hasAnyElements = !m_model.getElements().empty() || !m_model.surfaceMesh().elements.empty();
    if (!hasAnyElements) {
        if (onFinished) {
            onFinished(false);
        }
        return;
    }
    if (hasValidStaticResultForCurrentRevision()) {
        if (onFinished) {
            onFinished(true);
        }
        return;
    }
    const ModelValidation val = m_model.validate();
    if (!val.valid) {
        if (onFinished) {
            onFinished(false);
        }
        return;
    }
    if (onFinished) {
        m_staticSolveContinuations.push_back(std::move(onFinished));
    }
    if (m_solveInProgress && m_activeStaticRevision == m_modelRevision) {
        return;
    }
    if (!runSolver(false)) {
        m_pendingResolve = true;
    }
}

std::unordered_map<ElementId, double> MainWindow::computeModelDcrMap() const {
    std::unordered_map<ElementId, double> dcrMap;
    if (m_model.getElements().empty() && m_model.surfaceMesh().elements.empty()) {
        return dcrMap;
    }
    io::PdfReportOptions opts;
    if (m_nbrComplianceDialog) {
        opts = m_nbrComplianceDialog->buildOptions();
    } else {
        const auto& md = m_model.metadata();
        opts.soilSigmaAdm_kPa = md.soilSigmaAdm_kPa;
        opts.footingB_m = md.footingB_m;
        opts.footingL_m = md.footingL_m;
        opts.windV0_ms = md.windV0_ms;
        opts.windS1 = md.windS1;
        opts.windS2 = md.windS2;
        opts.windS3 = md.windS3;
    }
    const SolverResult emptyStatic;
    const SolverResult& staticRef = m_lastSolverResult.success ? m_lastSolverResult : emptyStatic;
    const io::ModelNbrAudit audit = io::PdfReportGenerator::evaluateModelNbrCompliance(m_model, staticRef, opts);
    for (const auto& s : audit.steelMembers) dcrMap[s.elementId] = s.result.dcrMax;
    for (const auto& b : audit.concreteBeams) dcrMap[b.elementId] = b.result.dcrMax;
    for (const auto& c : audit.concreteColumns) dcrMap[c.elementId] = c.result.dcrMax;
    return dcrMap;
}

void MainWindow::onExportPdfReport() {
    if (m_pdfOptionsDialog) {
        m_windowManager->showAndRaise(m_pdfOptionsDialog.data());
        return;
    }
    auto* dlg = new PdfReportOptionsDialog(nullptr, &m_model);
    m_pdfOptionsDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, false, true);
    bindPersistentDialog(QStringLiteral("PdfReportOptionsDialog"), dlg, true);
    connect(dlg, &QDialog::accepted, this, [this, dlgPtr = QPointer<PdfReportOptionsDialog>(dlg)]() {
        if (!dlgPtr) return;
        io::PdfReportOptions opts = dlgPtr->collectOptionsAndSaveMetadata();
        m_model.pushUndo();
        onProjectMetadataOrOptionsModified();
        if (dlgPtr->includeViewportCapture() && m_viewport) {
            opts.viewportSnapshot = m_viewport->grabFramebuffer();
        }
        if (m_analysisSuiteDialog) {
            if (const auto* mRes = m_analysisSuiteDialog->latestModalResult()) {
                opts.modalResult = std::make_shared<const kernel::ModalResult>(*mRes);
            }
            if (const auto* bRes = m_analysisSuiteDialog->latestBucklingResult()) {
                opts.bucklingResult = std::make_shared<const kernel::BucklingResult>(*bRes);
            }
            if (const auto* nRes = m_analysisSuiteDialog->latestNonlinearResult()) {
                opts.nonlinearResult = std::make_shared<const kernel::NonlinearResult>(*nRes);
            }
        }
        QPointer<MainWindow> self(this);
        m_windowManager->showModelessFileDialog(
            QFileDialog::AcceptSave,
            QFileDialog::AnyFile,
            locale::GerenciadorTraducao::trText("Exportar Memorial Executivo (PDF A4)"),
            initialFileDialogPath(QStringLiteral("Memorial_Executivo_OmniFrame.pdf")),
            locale::GerenciadorTraducao::trText("Documentos PDF (*.pdf);;Todos (*)"),
            [self, opts](const QString& path) {
                if (!self || path.isEmpty()) return;
                self->rememberFileDialogPath(path);
                QString errMsg;
                QApplication::setOverrideCursor(Qt::WaitCursor);
                const bool ok = io::PdfReportGenerator::generateExecutivePdf(
                    path, self->m_model, self->m_lastSolverResult, opts, &errMsg);
                QApplication::restoreOverrideCursor();
                if (ok) {
                    self->showStatusMessage(locale::GerenciadorTraducao::trText("Memorial executivo PDF exportado: %1").arg(path));
                    self->m_windowManager->showModelessMessage(
                        QMessageBox::Information,
                        locale::GerenciadorTraducao::trText("Sucesso"),
                        locale::GerenciadorTraducao::trText("Memorial Executivo A4 PDF gerado com sucesso!"));
                } else {
                    self->m_windowManager->showModelessMessage(
                        QMessageBox::Critical,
                        locale::GerenciadorTraducao::trText("Erro ao Gerar PDF"),
                        errMsg);
                }
            }
        );
    });
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onOpenLoadCombinations() {
    if (m_loadCombinationsDialog) {
        m_loadCombinationsDialog->refreshLists();
        m_windowManager->showAndRaise(m_loadCombinationsDialog.data());
        return;
    }
    auto* dlg = new LoadCombinationsDialog(
        nullptr,
        &m_model,
        [this]() { onModelModified(); },
        [this]() { onProjectMetadataOrOptionsModified(); });
    m_loadCombinationsDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("LoadCombinationsDialog"), dlg, true);
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onOpenMeshGenerator() {
    if (m_meshGeneratorDialog) {
        m_windowManager->showAndRaise(m_meshGeneratorDialog.data());
        return;
    }
    auto* dlg = new MeshGeneratorDialog(nullptr, &m_model, m_viewport, [this]() {
        onModelModified();
    });
    m_meshGeneratorDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("MeshGeneratorDialog"), dlg, true);
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onOpenAnalysisSuite() {
    if (m_analysisSuiteDialog) {
        m_windowManager->showAndRaise(m_analysisSuiteDialog.data());
        return;
    }
    auto* dlg = new AnalysisSuiteDialog(
        nullptr,
        &m_model,
        m_viewport,
        &m_lastSolverResult,
        [this](const SolverResult& res) {
            m_lastSolverResult = res;
            m_hasSolvedContext = res.success;
            if (m_viewport && m_viewport->postContourMode() == Viewport3D::PostContourMode::NbrComplianceDcr) {
                m_viewport->setElementDcrMap(computeModelDcrMap());
            }
            updateResultsTables();
            updateInspector();
            if (m_viewport) m_viewport->update();
            if (m_nbrComplianceDialog) m_nbrComplianceDialog->runAudit();
            if (m_postProcessControlDialog) m_postProcessControlDialog->syncFromViewport();
        },
        [this](const QString& msg) { showStatusMessage(msg); },
        [this]() { onModelModified(); },
        [this]() { onProjectMetadataOrOptionsModified(); },
        [this]() -> std::uint64_t { return m_modelRevision; },
        m_analysisExecutor);
    m_analysisSuiteDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("AnalysisSuiteDialog"), dlg, true);
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onOpenPostControls() {
    if (m_postProcessControlDialog) {
        m_postProcessControlDialog->syncFromViewport();
        m_windowManager->showAndRaise(m_postProcessControlDialog.data());
        return;
    }
    auto* dlg = new PostProcessControlDialog(nullptr, m_viewport, [this](std::function<void(bool)> doneCb) {
        ensureStaticSolvedAsync([this, doneCb = std::move(doneCb)](bool solved) {
            const auto dcrMap = computeModelDcrMap();
            if (m_viewport) {
                m_viewport->setElementDcrMap(dcrMap);
            }
            if (m_nbrComplianceDialog) {
                m_nbrComplianceDialog->runAudit();
            }
            if (doneCb) {
                doneCb(solved && !dcrMap.empty());
            }
        });
    });
    m_postProcessControlDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("PostProcessControlDialog"), dlg, false);
    const QList<QComboBox*> comboChildren = dlg->findChildren<QComboBox*>();
    for (auto* cb : comboChildren) {
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (!m_rebuildingOrLoadingResultsSettings) {
                saveUserPreferences();
            }
        });
    }
    const QList<QCheckBox*> checkChildren = dlg->findChildren<QCheckBox*>();
    for (auto* chk : checkChildren) {
        connect(chk, &QCheckBox::toggled, this, [this](bool) {
            if (!m_rebuildingOrLoadingResultsSettings) {
                saveUserPreferences();
            }
        });
    }
    const QList<QDoubleSpinBox*> dspinChildren = dlg->findChildren<QDoubleSpinBox*>();
    for (auto* dsp : dspinChildren) {
        connect(dsp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            if (!m_rebuildingOrLoadingResultsSettings) {
                saveUserPreferences();
            }
        });
    }
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onOpenNbrCompliance() {
    if (m_nbrComplianceDialog) {
        m_nbrComplianceDialog->runAudit();
        m_windowManager->showAndRaise(m_nbrComplianceDialog.data());
        return;
    }
    auto* dlg = new NbrComplianceDialog(
        nullptr,
        &m_model,
        m_viewport,
        &m_lastSolverResult,
        [this]() { onExportPdfReport(); },
        [this](std::function<void(bool)> doneCb) { ensureStaticSolvedAsync(std::move(doneCb)); });
    m_nbrComplianceDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("NbrComplianceDialog"), dlg, true);
    m_windowManager->showAndRaise(dlg);
}

// Ações de Edição e Solver MEF 3D
void MainWindow::updateUndoRedoActions() {
    if (m_actUndo) m_actUndo->setEnabled(m_model.canUndo());
    if (m_actRedo) m_actRedo->setEnabled(m_model.canRedo());
}

void MainWindow::onUndo() {
    if (m_model.undo()) {
        onSelectionChanged();
        onModelModified();
        showStatusMessage(locale::GerenciadorTraducao::trText("Alteração desfeita."));
    }
}

void MainWindow::onRedo() {
    if (m_model.redo()) {
        onSelectionChanged();
        onModelModified();
        showStatusMessage(locale::GerenciadorTraducao::trText("Alteração refeita."));
    }
}

void MainWindow::onAutoRecalculateToggled(bool checked) {
    m_autoRecalculate = checked;
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
    if (m_autoRecalculate && m_hasSolvedContext) {
        onModelModified();
    }
}

void MainWindow::onSolveStructure() {
    if (!m_rebuildingOrLoadingResultsSettings && sender() != nullptr) {
        m_hasLoadedCustomDiagram = false;
    }
    m_hasSolvedContext = true;
    runSolver(true);
}

bool MainWindow::runSolver(bool interactive) {
    if (m_solveInProgress || (m_analysisExecutor && m_analysisExecutor->isBusy())) {
        m_pendingResolve = true;
        if (interactive) {
            m_pendingResolveInteractive = true;
            showStatusMessage(locale::GerenciadorTraducao::trText("Cálculo já está em andamento; aguarde a conclusão."));
        }
        return false;
    }

    if (!m_analysisExecutor) {
        return false;
    }

    m_solveInProgress = true;
    m_activeStaticRevision = m_modelRevision;
    m_pendingResolve = false;
    m_pendingResolveInteractive = false;
    if (m_btnSolve) m_btnSolve->setEnabled(false);
    if (m_statusProgress) {
        m_statusProgress->setRange(0, 0);
        m_statusProgress->setVisible(true);
    }
    m_solverStatusLabel->setText(locale::GerenciadorTraducao::trText("Calculando matriz de rigidez global esparsa..."));

    struct AsyncSolvePayload {
        std::shared_ptr<StructuralModel> snapshot;
        SolverResult result;
    };
    auto snapshot = std::make_shared<StructuralModel>(m_model.cloneWithoutHistory());

    auto finishFailureUi = [this, interactive](const QString& rawErrMsg) {
        const QString translatedErr = translateCoreMessage(rawErrMsg);
        io::AppLogger::warning(
            QStringLiteral("Solver"),
            QStringLiteral("Erro no calculo MEF: %1").arg(translatedErr));
        m_solverStatusLabel->setText(locale::GerenciadorTraducao::trText("Erro no cálculo: %1").arg(translatedErr));
        updateResultsTables();
        if (QApplication::activePopupWidget() != nullptr) {
            QTimer::singleShot(120, this, &MainWindow::updateInspector);
        } else {
            updateInspector();
        }
        if (m_viewport) {
            m_viewport->markGeometryDirty();
            m_viewport->update();
        }
        if (interactive) {
            m_windowManager->showModelessMessage(
                QMessageBox::Warning,
                locale::GerenciadorTraducao::trText("Erro no Solver"),
                translatedErr
            );
        }
        completeBackgroundStaticSolve(false);
    };

    const bool submitted = m_analysisExecutor->submit<AsyncSolvePayload>(
        this,
        [snapshot](std::stop_token stopToken) -> AsyncSolvePayload {
            AsyncSolvePayload out;
            out.snapshot = snapshot;
            if (snapshot) {
                out.result = FemSolver3D::solveWithOptions(*snapshot, snapshot->solverOptions(), stopToken);
            }
            return out;
        },
        [this, interactive, finishFailureUi](const AsyncSolvePayload& payload,
                                              std::uint64_t startRev,
                                              bool wasStopped) {
            m_solveInProgress = false;
            m_activeStaticJobId = 0;
            m_activeStaticRevision = ~std::uint64_t{0};
            if (m_btnSolve) m_btnSolve->setEnabled(true);
            if (m_statusProgress) {
                m_statusProgress->setVisible(false);
                m_statusProgress->setRange(0, 1);
                m_statusProgress->setValue(0);
            }

            if (wasStopped && m_lastSolverResult.success &&
                startRev == m_modelRevision && !m_pendingResolve) {
                completeBackgroundStaticSolve(true);
                return;
            }

            if (m_pendingResolve || startRev != m_modelRevision) {
                const bool rerunInteractive = m_pendingResolveInteractive;
                m_pendingResolve = false;
                m_pendingResolveInteractive = false;
                if ((m_autoRecalculate && m_hasSolvedContext) || rerunInteractive || !m_staticSolveContinuations.empty()) {
                    if (m_model.validate().valid) {
                        if (runSolver(rerunInteractive)) {
                            return;
                        }
                    }
                }
                if (startRev != m_modelRevision) {
                    showStatusMessage(locale::GerenciadorTraducao::trText("Modelo modificado durante o cálculo em segundo plano; execute F5 novamente."));
                    updateResultsTables();
                    if (QApplication::activePopupWidget() != nullptr) {
                        QTimer::singleShot(120, this, &MainWindow::updateInspector);
                    } else {
                        updateInspector();
                    }
                    if (m_viewport) {
                        m_viewport->update();
                    }
                    completeBackgroundStaticSolve(false);
                    return;
                }
            }

            if (!payload.snapshot) {
                finishFailureUi(QStringLiteral("Snapshot de modelo nulo durante análise estática."));
                return;
            }

            const auto& srcNodes = payload.snapshot->getNodes();
            const auto& srcElems = payload.snapshot->getElements();
            const auto& srcMesh = payload.snapshot->surfaceMesh();
            auto& dstNodes = m_model.getNodes();
            auto& dstElems = m_model.getElements();
            auto& dstMesh = m_model.surfaceMesh();
            bool sameTopology = (srcNodes.size() == dstNodes.size())
                             && (srcElems.size() == dstElems.size())
                             && (srcMesh.nodes.size() == dstMesh.nodes.size())
                             && (srcMesh.elements.size() == dstMesh.elements.size());
            if (sameTopology) {
                for (size_t i = 0; i < srcNodes.size(); ++i) {
                    if (srcNodes[i].id != dstNodes[i].id ||
                        (srcNodes[i].position - dstNodes[i].position).norm() > 1e-9) {
                        sameTopology = false;
                        break;
                    }
                }
            }
            if (sameTopology) {
                for (size_t i = 0; i < srcElems.size(); ++i) {
                    if (srcElems[i].id != dstElems[i].id ||
                        srcElems[i].nodeStart != dstElems[i].nodeStart ||
                        srcElems[i].nodeEnd != dstElems[i].nodeEnd) {
                        sameTopology = false;
                        break;
                    }
                }
            }
            if (sameTopology) {
                for (size_t i = 0; i < srcMesh.nodes.size(); ++i) {
                    if (srcMesh.nodes[i].id != dstMesh.nodes[i].id ||
                        (srcMesh.nodes[i].position - dstMesh.nodes[i].position).norm() > 1e-9) {
                        sameTopology = false;
                        break;
                    }
                }
            }

            if (!sameTopology) {
                showStatusMessage(locale::GerenciadorTraducao::trText("Modelo modificado durante o cálculo em segundo plano; execute F5 novamente."));
                updateResultsTables();
                updateInspector();
                if (m_viewport) {
                    m_viewport->update();
                }
                completeBackgroundStaticSolve(false);
                return;
            }

            for (size_t i = 0; i < srcNodes.size(); ++i) {
                dstNodes[i].displacement = srcNodes[i].displacement;
                dstNodes[i].reaction = srcNodes[i].reaction;
            }
            for (size_t i = 0; i < srcElems.size(); ++i) {
                dstElems[i].globalDisplacements = srcElems[i].globalDisplacements;
                dstElems[i].localDisplacements = srcElems[i].localDisplacements;
                dstElems[i].localEndForces = srcElems[i].localEndForces;
                dstElems[i].hasResults = srcElems[i].hasResults;
            }
            for (size_t i = 0; i < srcMesh.nodes.size(); ++i) {
                dstMesh.nodes[i].hasResults = srcMesh.nodes[i].hasResults;
                dstMesh.nodes[i].displacement = srcMesh.nodes[i].displacement;
                dstMesh.nodes[i].reaction = srcMesh.nodes[i].reaction;
            }
            for (size_t i = 0; i < srcMesh.elements.size(); ++i) {
                dstMesh.elements[i].hasResults = srcMesh.elements[i].hasResults;
                dstMesh.elements[i].vonMisesStress = srcMesh.elements[i].vonMisesStress;
                dstMesh.elements[i].maxPrincipalStress = srcMesh.elements[i].maxPrincipalStress;
                dstMesh.elements[i].minPrincipalStress = srcMesh.elements[i].minPrincipalStress;
                dstMesh.elements[i].membraneResultants = srcMesh.elements[i].membraneResultants;
                dstMesh.elements[i].bendingResultants = srcMesh.elements[i].bendingResultants;
                dstMesh.elements[i].transverseShear = srcMesh.elements[i].transverseShear;
                dstMesh.elements[i].stressVoigt = srcMesh.elements[i].stressVoigt;
            }
            m_lastSolverResult = payload.result;

            if (m_lastSolverResult.success) {
                m_lastSolvedRevision = startRev;
                m_model.bumpRevision();
                QString msg = locale::GerenciadorTraducao::trText("Cálculo MEF concluído com sucesso em %1 ms (%2 nós, %3 barras, %4 DOFs). Equilíbrio: %5")
                              .arg(QString::number(m_lastSolverResult.executionTimeMs, 'f', 1),
                                   QString::number(m_lastSolverResult.totalNodes),
                                   QString::number(m_lastSolverResult.totalElements),
                                   QString::number(m_lastSolverResult.totalDofs),
                                   m_lastSolverResult.equilibrium.isBalanced
                                       ? locale::GerenciadorTraducao::trText("OK (ΣF=0)")
                                       : locale::GerenciadorTraducao::trText("Alerta de Desequilíbrio"));
                if (m_lastSolverResult.totalSurfaceElements > 0) {
                    msg += QStringLiteral(" | 2D/3D: +%1n / +%2e")
                               .arg(m_lastSolverResult.totalSurfaceNodes)
                               .arg(m_lastSolverResult.totalSurfaceElements);
                }

                m_solverStatusLabel->setText(msg);
                showStatusMessage(msg);

                // Se nenhum diagrama estiver ativo após cálculo manual, ativa a Deformada 3D
                if (interactive && !m_hasLoadedCustomDiagram && m_viewport->activeDiagram() == DiagramType::None) {
                    m_diagramCombo->setCurrentIndex(1); // Deformada 3D
                }

                if (m_viewport && m_viewport->postContourMode() == Viewport3D::PostContourMode::NbrComplianceDcr) {
                    m_viewport->setElementDcrMap(computeModelDcrMap());
                }
                updateResultsTables();
                if (QApplication::activePopupWidget() != nullptr) {
                    QTimer::singleShot(120, this, &MainWindow::updateInspector);
                } else {
                    updateInspector();
                }
                if (m_viewport) {
                    m_viewport->markGeometryDirty();
                    m_viewport->update();
                }
                if (m_nbrComplianceDialog) m_nbrComplianceDialog->runAudit();
                if (m_postProcessControlDialog) m_postProcessControlDialog->syncFromViewport();
                completeBackgroundStaticSolve(true);
                return;
            }

            finishFailureUi(QString::fromStdString(m_lastSolverResult.errorMessage));
        },
        [this, finishFailureUi](const QString& errMsg) {
            m_solveInProgress = false;
            m_activeStaticJobId = 0;
            m_activeStaticRevision = ~std::uint64_t{0};
            if (m_btnSolve) m_btnSolve->setEnabled(true);
            if (m_statusProgress) {
                m_statusProgress->setVisible(false);
                m_statusProgress->setRange(0, 1);
                m_statusProgress->setValue(0);
            }
            m_lastSolverResult = SolverResult{};
            m_lastSolverResult.success = false;
            m_lastSolverResult.errorMessage = errMsg.toStdString();
            finishFailureUi(errMsg);
        },
        &m_activeStaticJobId);

    if (!submitted) {
        m_solveInProgress = false;
        m_activeStaticJobId = 0;
        m_activeStaticRevision = ~std::uint64_t{0};
        if (m_btnSolve) m_btnSolve->setEnabled(true);
        if (m_statusProgress) {
            m_statusProgress->setVisible(false);
            m_statusProgress->setRange(0, 1);
            m_statusProgress->setValue(0);
        }
        return false;
    }
    return true;
}

// Modos e Ferramentas
void MainWindow::onInteractionModeChanged(QAction* action) {
    if (action == m_actSelect) m_viewport->setInteractionMode(InteractionMode::Select);
    else if (action == m_actAddNode) m_viewport->setInteractionMode(InteractionMode::AddNode);
    else if (action == m_actAddElement) m_viewport->setInteractionMode(InteractionMode::AddElement);
    else if (action == m_actMeasure) m_viewport->setInteractionMode(InteractionMode::Measure);
    else if (action == m_actDelete) m_viewport->setInteractionMode(InteractionMode::Delete);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onWorkPlaneChanged(int index) {
    auto plane = static_cast<WorkPlane>(m_planeCombo->itemData(index).toInt());
    m_viewport->setWorkPlane(plane);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onPlaneOffsetChanged(double val) {
    m_viewport->setPlaneOffset(val);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onSnapToggled(bool checked) {
    m_viewport->setSnapToGrid(checked);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onGridStepChanged(double val) {
    m_viewport->setGridStep(val);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onShowLocalAxesToggled(bool checked) {
    m_viewport->setShowLocalAxes(checked);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onShowReactionsToggled(bool checked) {
    m_viewport->setShowReactions(checked);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onDiagramChanged(int index) {
    auto diag = static_cast<DiagramType>(m_diagramCombo->itemData(index).toInt());
    m_viewport->setActiveDiagram(diag);
    if (!m_rebuildingOrLoadingResultsSettings) {
        m_hasLoadedCustomDiagram = true;
        saveUserPreferences();
    }
}

void MainWindow::onDiagramScaleChanged(int val) {
    m_viewport->setDiagramScale(val / 30.0);
    m_viewport->setDeformedScale(val * 1.5);
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

// Diálogos de Atribuição e Edição
void MainWindow::onAssignMaterials() {
    if (m_materialCatalogDialog) {
        m_materialCatalogDialog->refreshList();
        m_windowManager->showAndRaise(m_materialCatalogDialog.data());
        return;
    }
    auto* dlg = new MaterialCatalogDialog(nullptr, &m_model, m_windowManager, [this]() {
        onModelModified();
    });
    m_materialCatalogDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("MaterialCatalogDialog"), dlg, false);
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onAssignSections() {
    if (m_sectionCatalogDialog) {
        m_sectionCatalogDialog->refreshList();
        m_windowManager->showAndRaise(m_sectionCatalogDialog.data());
        return;
    }
    auto* dlg = new SectionCatalogDialog(nullptr, &m_model, m_windowManager, [this]() {
        onModelModified();
    });
    m_sectionCatalogDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("SectionCatalogDialog"), dlg, false);
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onAssignSupports() {
    if (m_model.getSelectedNodes().empty()) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            locale::GerenciadorTraducao::trText("Seleção Necessária"),
            locale::GerenciadorTraducao::trText("Selecione ao menos um nó no Viewport 3D para atribuir ou editar apoios."));
        return;
    }

    const Node* firstNode = m_model.findNode(*m_model.getSelectedNodes().begin());
    const SupportRestraint initialSup = firstNode ? firstNode->support : SupportRestraint();

    openManagedModelessDialog(
        this, m_windowManager, m_supportDialog, false,
        [initialSup]() { return new SupportDialog(nullptr, &initialSup); },
        [initialSup](SupportDialog* dlg) { dlg->setSupport(initialSup); },
        [this](SupportDialog* dlg) {
            if (m_model.getSelectedNodes().empty()) return;
            const SupportRestraint sup = dlg->getSupport();
            for (NodeId nid : m_model.getSelectedNodes()) {
                if (Node* n = m_model.findNode(nid)) n->support = sup;
            }
            m_model.pushUndo();
            onModelModified();
        }
    );
}

void MainWindow::onAssignReleases() {
    if (m_model.getSelectedElements().empty()) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            locale::GerenciadorTraducao::trText("Seleção Necessária"),
            locale::GerenciadorTraducao::trText("Selecione ao menos uma barra no Viewport 3D para atribuir ou editar rótulas."));
        return;
    }

    const Element* firstElem = m_model.findElement(*m_model.getSelectedElements().begin());
    const ElementReleases initialRel = firstElem ? firstElem->releases : ElementReleases();

    openManagedModelessDialog(
        this, m_windowManager, m_releasesDialog, false,
        [initialRel]() { return new ReleasesDialog(nullptr, &initialRel); },
        [initialRel](ReleasesDialog* dlg) { dlg->setReleases(initialRel); },
        [this](ReleasesDialog* dlg) {
            if (m_model.getSelectedElements().empty()) return;
            const ElementReleases rel = dlg->getReleases();
            for (ElementId eid : m_model.getSelectedElements()) {
                if (Element* e = m_model.findElement(eid)) e->releases = rel;
            }
            m_model.pushUndo();
            onModelModified();
        }
    );
}

void MainWindow::onAssignNodalLoads() {
    if (m_model.getSelectedNodes().empty()) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            locale::GerenciadorTraducao::trText("Seleção Necessária"),
            locale::GerenciadorTraducao::trText("Selecione ao menos um nó no Viewport 3D para atribuir ou editar cargas nodais."));
        return;
    }

    const Node* firstNode = m_model.findNode(*m_model.getSelectedNodes().begin());
    const NodalLoad initialLoad = firstNode ? firstNode->load : NodalLoad();

    openManagedModelessDialog(
        this, m_windowManager, m_nodalLoadDialog, false,
        [initialLoad]() { return new NodalLoadDialog(nullptr, &initialLoad); },
        [initialLoad](NodalLoadDialog* dlg) { dlg->setLoad(initialLoad); },
        [this](NodalLoadDialog* dlg) {
            if (m_model.getSelectedNodes().empty()) return;
            const NodalLoad ld = dlg->getLoad();
            for (NodeId nid : m_model.getSelectedNodes()) {
                if (Node* n = m_model.findNode(nid)) n->load = ld;
            }
            m_model.pushUndo();
            onModelModified();
        }
    );
}

void MainWindow::onAssignDistributedLoads() {
    if (m_model.getSelectedElements().empty()) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            locale::GerenciadorTraducao::trText("Seleção Necessária"),
            locale::GerenciadorTraducao::trText("Selecione ao menos uma barra no Viewport 3D para atribuir ou editar cargas distribuídas."));
        return;
    }

    const Element* firstElem = m_model.findElement(*m_model.getSelectedElements().begin());
    DistributedLoad initialDist;
    if (firstElem && (firstElem->distLoad.active || firstElem->distLoad.w.normSq() > 1e-12)) {
        initialDist = firstElem->distLoad;
    } else {
        initialDist.active = true;
        initialDist.coordSystem = CoordSystem::Global;
        initialDist.w = {0.0, 0.0, -10.0};
    }

    openManagedModelessDialog(
        this, m_windowManager, m_distLoadDialog, false,
        [initialDist]() { return new DistributedLoadDialog(nullptr, &initialDist); },
        [initialDist](DistributedLoadDialog* dlg) { dlg->setLoad(initialDist); },
        [this](DistributedLoadDialog* dlg) {
            if (m_model.getSelectedElements().empty()) return;
            const DistributedLoad q = dlg->getLoad();
            for (ElementId eid : m_model.getSelectedElements()) {
                if (Element* e = m_model.findElement(eid)) e->distLoad = q;
            }
            m_model.pushUndo();
            onModelModified();
        }
    );
}

void MainWindow::onAssignThermalLoads() {
    if (m_model.getSelectedElements().empty()) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            locale::GerenciadorTraducao::trText("Seleção Necessária"),
            locale::GerenciadorTraducao::trText("Selecione ao menos uma barra no Viewport 3D para atribuir ou editar variação térmica."));
        return;
    }

    const Element* firstElem = m_model.findElement(*m_model.getSelectedElements().begin());
    ThermalLoad initialThermal;
    if (firstElem && (firstElem->thermalLoad.active || std::abs(firstElem->thermalLoad.deltaT) > 1e-9)) {
        initialThermal = firstElem->thermalLoad;
    } else {
        initialThermal.active = true;
        initialThermal.deltaT = 20.0;
    }

    openManagedModelessDialog(
        this, m_windowManager, m_thermalLoadDialog, false,
        [initialThermal]() { return new ThermalLoadDialog(nullptr, &initialThermal); },
        [initialThermal](ThermalLoadDialog* dlg) { dlg->setThermalLoad(initialThermal); },
        [this](ThermalLoadDialog* dlg) {
            if (m_model.getSelectedElements().empty()) return;
            const ThermalLoad th = dlg->getThermalLoad();
            for (ElementId eid : m_model.getSelectedElements()) {
                if (Element* e = m_model.findElement(eid)) e->thermalLoad = th;
            }
            m_model.pushUndo();
            onModelModified();
        }
    );
}

void MainWindow::onAssignRollAngle() {
    if (m_model.getSelectedElements().empty()) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            locale::GerenciadorTraducao::trText("Seleção Necessária"),
            locale::GerenciadorTraducao::trText("Selecione ao menos uma barra no Viewport 3D para ajustar o ângulo de Roll."));
        return;
    }

    ElementId eid = *m_model.getSelectedElements().begin();
    const Element* e = m_model.findElement(eid);
    const double currAngle = e ? (e->rollAngle * 180.0 / PI) : 0.0;

    openManagedModelessDialog(
        this, m_windowManager, m_rollAngleDialog, false,
        [currAngle]() { return new RollAngleDialog(nullptr, currAngle); },
        [currAngle](RollAngleDialog* dlg) { dlg->setAngleDegrees(currAngle); },
        [this](RollAngleDialog* dlg) {
            if (m_model.getSelectedElements().empty()) return;
            double rollRad = dlg->getAngleRadians();
            for (ElementId id : m_model.getSelectedElements()) {
                if (Element* el = m_model.findElement(id)) el->rollAngle = rollRad;
            }
            m_model.pushUndo();
            onModelModified();
        }
    );
}

void MainWindow::onQuickAddNode() {
    m_model.addNode(m_quickXSpin->value(), m_quickYSpin->value(), m_quickZSpin->value());
    onModelModified();
    showStatusMessage(locale::GerenciadorTraducao::trText("Nó adicionado em (%1, %2, %3).")
                      .arg(m_quickXSpin->value(), 0, 'f', 2)
                      .arg(m_quickYSpin->value(), 0, 'f', 2)
                      .arg(m_quickZSpin->value(), 0, 'f', 2));
}

// Sincronização de UI
void MainWindow::onSelectionChanged() {
    if (m_materialCatalogDialog) m_materialCatalogDialog->refreshList();
    if (m_sectionCatalogDialog) m_sectionCatalogDialog->refreshList();
    if (m_viewport) {
        m_viewport->markGeometryDirty();
        m_viewport->update();
    }
    updateInspector();
}

void MainWindow::onProjectMetadataOrOptionsModified() {
    ++m_modelRevision;
    updateUndoRedoActions();
    if (!m_loadingOrTemplateModel) {
        markProjectDirty();
    }
}

void MainWindow::onModelModified() {
    ++m_modelRevision;
    m_lastSolvedRevision = ~std::uint64_t{0};
    if (m_solveInProgress && m_analysisExecutor && m_activeStaticJobId != 0) {
        m_analysisExecutor->requestCancelForJob(m_activeStaticJobId);
    }
    m_model.clearResults();
    m_lastSolverResult = SolverResult();
    if (m_viewport) {
        m_viewport->clearElementDcrMap();
        m_viewport->clearPostAnimation();
        m_viewport->markGeometryDirty();
    }

    QString statsMsg = locale::GerenciadorTraducao::trText("Nós: %1 | Barras: %2")
                           .arg(m_model.getNodes().size())
                           .arg(m_model.getElements().size());
    if (!m_model.surfaceMesh().elements.empty()) {
        statsMsg += QStringLiteral(" | 2D/3D: %1n / %2e")
                        .arg(m_model.surfaceMesh().nodes.size())
                        .arg(m_model.surfaceMesh().elements.size());
    }
    m_modelStatsLabel->setText(statsMsg);
    updateUndoRedoActions();
    m_model.syncAutoCapturedViewportLoadCase();
    if (m_materialCatalogDialog) m_materialCatalogDialog->refreshList();
    if (m_sectionCatalogDialog) m_sectionCatalogDialog->refreshList();
    if (m_loadCombinationsDialog) m_loadCombinationsDialog->refreshLists();
    if (m_meshGeneratorDialog) m_meshGeneratorDialog->refreshFromModel();
    if (m_analysisSuiteDialog) m_analysisSuiteDialog->refreshContext();
    if (m_postProcessControlDialog) m_postProcessControlDialog->syncFromViewport();
    if (m_nbrComplianceDialog) m_nbrComplianceDialog->runAudit();

    const bool isEmptyModel = m_model.getNodes().empty() &&
                              m_model.getElements().empty() &&
                              m_model.surfaceMesh().elements.empty();

    if (m_autoRecalculate && m_hasSolvedContext && !isEmptyModel) {
        if (m_solveInProgress || (m_analysisExecutor && m_analysisExecutor->isBusy())) {
            m_pendingResolve = true;
            m_solverStatusLabel->setText(
                locale::GerenciadorTraducao::trText("Cálculo em andamento; aguarde ou recalcule após concluir (F5)."));
        } else {
            const ModelValidation val = m_model.validate();
            if (val.valid) {
                runSolver(false);
                if (!m_loadingOrTemplateModel) {
                    markProjectDirty();
                }
                return;
            }
            const QString err = val.errors.empty()
                ? locale::GerenciadorTraducao::trText("Erro de validação desconhecido.")
                : translateCoreMessage(QString::fromStdString(val.errors.front()));
            m_solverStatusLabel->setText(
                locale::GerenciadorTraducao::trText("Recálculo pendente após edição: %1").arg(err));
        }
    } else if (m_hasSolvedContext) {
        m_solverStatusLabel->setText(
            locale::GerenciadorTraducao::trText("Modelo modificado — clique em CALCULAR ESTRUTURA (F5) para atualizar os resultados."));
    } else if (isEmptyModel) {
        m_solverStatusLabel->setText(locale::GerenciadorTraducao::trText("Pronto para modelar."));
    } else {
        m_solverStatusLabel->setText(locale::GerenciadorTraducao::trText("Pronto para calcular."));
    }

    if (!m_loadingOrTemplateModel) {
        markProjectDirty();
    }

    updateResultsTables();
    updateInspector();
    if (m_viewport) m_viewport->update();
}

void MainWindow::updateInspector() {
    using locale::GerenciadorTraducao;
    // Limpa conteúdo anterior do inspetor de forma segura para sinais em execução
    m_inspectorContent.clear();
    QLayoutItem* item = nullptr;
    while ((item = m_inspectorLayout->takeAt(0)) != nullptr) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    auto* contentWidget = new QWidget(m_inspectorLayout ? m_inspectorLayout->parentWidget() : nullptr);
    m_inspectorContent = contentWidget;
    auto* formLayout = new QFormLayout(contentWidget);
    formLayout->setContentsMargins(2, 2, 2, 2);

    auto configureInspectorCombo = [](QComboBox* cb) {
        cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cb->setMinimumContentsLength(10);
        if (cb->view()) {
            cb->view()->setMinimumWidth(260);
        }
    };

    if (!m_model.getNodes().empty() || !m_model.getElements().empty()) {
        auto* quickSelWidget = new QWidget(contentWidget);
        auto* quickSelLay = new QHBoxLayout(quickSelWidget);
        quickSelLay->setContentsMargins(0, 0, 0, 0);
        quickSelLay->setSpacing(4);

        auto* btnSelBars = new QPushButton(GerenciadorTraducao::trText("Sel. Barras"), quickSelWidget);
        btnSelBars->setEnabled(!m_model.getElements().empty());
        connect(btnSelBars, &QPushButton::clicked, this, [this]() {
            m_model.clearSelection();
            for (const auto& el : m_model.getElements()) {
                m_model.getSelectedElements().insert(el.id);
            }
            updateInspector();
            if (m_viewport) m_viewport->update();
        });
        quickSelLay->addWidget(btnSelBars);

        auto* btnSelNodes = new QPushButton(GerenciadorTraducao::trText("Sel. Nós"), quickSelWidget);
        btnSelNodes->setEnabled(!m_model.getNodes().empty());
        connect(btnSelNodes, &QPushButton::clicked, this, [this]() {
            m_model.clearSelection();
            for (const auto& nd : m_model.getNodes()) {
                m_model.getSelectedNodes().insert(nd.id);
            }
            updateInspector();
            if (m_viewport) m_viewport->update();
        });
        quickSelLay->addWidget(btnSelNodes);

        if (!m_model.getSelectedElements().empty()) {
            auto* btnSameSec = new QPushButton(GerenciadorTraducao::trText("Mesma Seção"), quickSelWidget);
            connect(btnSameSec, &QPushButton::clicked, this, [this]() {
                const Element* refEl = m_model.findElement(*m_model.getSelectedElements().begin());
                if (!refEl) return;
                const SectionId targetSec = refEl->sectionId;
                m_model.getSelectedElements().clear();
                for (const auto& el : m_model.getElements()) {
                    if (el.sectionId == targetSec) {
                        m_model.getSelectedElements().insert(el.id);
                    }
                }
                updateInspector();
                if (m_viewport) m_viewport->update();
            });
            quickSelLay->addWidget(btnSameSec);
        }

        formLayout->addRow(quickSelWidget);
    }

    auto addRowWithEditBtn = [&](const QString& labelText, const QString& valText, auto slotPtr) {
        auto* rowWidget = new QWidget(contentWidget);
        auto* hLay = new QHBoxLayout(rowWidget);
        hLay->setContentsMargins(0, 0, 0, 0);
        auto* valLbl = new QLabel(valText, rowWidget);
        valLbl->setWordWrap(true);
        auto* editBtn = new QPushButton(GerenciadorTraducao::trText("Editar..."), rowWidget);
        editBtn->setMaximumWidth(86);
        connect(editBtn, &QPushButton::clicked, this, slotPtr);
        hLay->addWidget(valLbl, 1);
        hLay->addWidget(editBtn);
        formLayout->addRow(labelText, rowWidget);
    };

    if (!m_model.getSelectedNodes().empty()) {
        NodeId nid = *m_model.getSelectedNodes().begin();
        Node* node = m_model.findNode(nid);
        if (node) {
            const size_t selCount = m_model.getSelectedNodes().size();
            const QString idText = selCount > 1
                ? GerenciadorTraducao::trText("Nó ID %1 (+%2)").arg(node->id).arg(selCount - 1)
                : GerenciadorTraducao::trText("Nó ID %1").arg(node->id);
            formLayout->addRow(QString("<b>%1</b>").arg(GerenciadorTraducao::trText("Tipo:")), new QLabel(idText, contentWidget));

            auto makeCoordSpin = [contentWidget](double v) {
                auto* sp = new QDoubleSpinBox(contentWidget);
                sp->setRange(-10000.0, 10000.0);
                sp->setDecimals(3);
                sp->setSuffix(QStringLiteral(" m"));
                sp->setValue(v);
                return sp;
            };
            auto* spX = makeCoordSpin(node->x());
            spX->setObjectName(QStringLiteral("insp_coord_x"));
            auto* spY = makeCoordSpin(node->y());
            spY->setObjectName(QStringLiteral("insp_coord_y"));
            auto* spZ = makeCoordSpin(node->z());
            spZ->setObjectName(QStringLiteral("insp_coord_z"));
            formLayout->addRow(GerenciadorTraducao::trText("Coordenada X:"), spX);
            formLayout->addRow(GerenciadorTraducao::trText("Coordenada Y:"), spY);
            formLayout->addRow(GerenciadorTraducao::trText("Coordenada Z:"), spZ);

            auto* btnApplyCoords = new QPushButton(GerenciadorTraducao::trText("Aplicar Coordenadas"), contentWidget);
            btnApplyCoords->setObjectName(QStringLiteral("insp_btn_apply_coords"));
            connect(btnApplyCoords, &QPushButton::clicked, this, [this, nid, spX, spY, spZ]() {
                Node* n = m_model.findNode(nid);
                if (!n) return;
                const Vec3 newPos{spX->value(), spY->value(), spZ->value()};
                if ((newPos - n->position).norm() < 1e-9) return;
                for (const auto& elem : m_model.getElements()) {
                    if (elem.nodeStart == nid || elem.nodeEnd == nid) {
                        const NodeId otherId = (elem.nodeStart == nid) ? elem.nodeEnd : elem.nodeStart;
                        const Node* other = m_model.findNode(otherId);
                        if (other && (other->position - newPos).norm() < 1e-4) {
                            m_windowManager->showModelessMessage(
                                QMessageBox::Warning,
                                GerenciadorTraducao::trText("Coordenada Inválida"),
                                GerenciadorTraducao::trText("A nova posição coincidiria com o Nó %1 conectado pela Barra %2.")
                                    .arg(otherId).arg(elem.id));
                            return;
                        }
                    }
                }
                n->position = newPos;
                m_model.pushUndo();
                onModelModified();
                showStatusMessage(GerenciadorTraducao::trText("Coordenadas do Nó %1 atualizadas.").arg(nid));
            });
            formLayout->addRow(QString(), btnApplyCoords);

            if (selCount > 1) {
                auto* spDx = makeCoordSpin(0.0);
                auto* spDy = makeCoordSpin(0.0);
                auto* spDz = makeCoordSpin(0.0);
                formLayout->addRow(GerenciadorTraducao::trText("Deslocamento ΔX:"), spDx);
                formLayout->addRow(GerenciadorTraducao::trText("Deslocamento ΔY:"), spDy);
                formLayout->addRow(GerenciadorTraducao::trText("Deslocamento ΔZ:"), spDz);

                auto* btnShiftNodes = new QPushButton(GerenciadorTraducao::trText("Deslocar Nós Selecionados (Δ)"), contentWidget);
                connect(btnShiftNodes, &QPushButton::clicked, this, [this, spDx, spDy, spDz, selCount]() {
                    const Vec3 delta{spDx->value(), spDy->value(), spDz->value()};
                    if (delta.norm() < 1e-9) return;

                    const auto& selNodes = m_model.getSelectedNodes();
                    for (const auto& elem : m_model.getElements()) {
                        const Node* na = m_model.findNode(elem.nodeStart);
                        const Node* nb = m_model.findNode(elem.nodeEnd);
                        if (!na || !nb) continue;
                        Vec3 pa = na->position;
                        Vec3 pb = nb->position;
                        if (selNodes.count(elem.nodeStart)) pa += delta;
                        if (selNodes.count(elem.nodeEnd)) pb += delta;
                        if ((pb - pa).norm() < 1e-4) {
                            m_windowManager->showModelessMessage(
                                QMessageBox::Warning,
                                GerenciadorTraducao::trText("Coordenada Inválida"),
                                GerenciadorTraducao::trText("O deslocamento Δ resultaria em comprimento nulo para a Barra %1.")
                                    .arg(elem.id));
                            return;
                        }
                    }

                    for (NodeId id : selNodes) {
                        if (Node* n = m_model.findNode(id)) {
                            n->position += delta;
                        }
                    }
                    m_model.pushUndo();
                    onModelModified();
                    showStatusMessage(GerenciadorTraducao::trText("%1 nós deslocados.").arg(selCount));
                });
                formLayout->addRow(QString(), btnShiftNodes);
            }

            QStringList supParts;
            if (node->support.isFixed()) {
                supParts << GerenciadorTraducao::trText("Engastado");
            } else if (node->support.isPinned()) {
                supParts << GerenciadorTraducao::trText("Articulado Fixo");
            } else if (node->support.hasFixedDofs()) {
                supParts << GerenciadorTraducao::trText("Fixações Parciais");
            }
            if (node->support.hasSprings()) {
                supParts << GerenciadorTraducao::trText("Molas Elásticas");
            }
            if (node->support.hasPrescribedDisplacements()) {
                supParts << GerenciadorTraducao::trText("Desloc. Prescritos");
            }
            const QString supStr = supParts.isEmpty() ? GerenciadorTraducao::trText("Livre") : supParts.join(QStringLiteral(" + "));

            auto* supCombo = new QComboBox(contentWidget);
            supCombo->setObjectName(QStringLiteral("insp_sup_combo"));
            configureInspectorCombo(supCombo);
            supCombo->addItem(GerenciadorTraducao::trText("Livre"), 0);
            supCombo->addItem(GerenciadorTraducao::trText("Engastado"), 1);
            supCombo->addItem(GerenciadorTraducao::trText("Articulado Fixo"), 2);
            supCombo->addItem(GerenciadorTraducao::trText("Apoio Móvel (Z)"), 3);

            int supIdx = 0;
            const bool hasExtra = node->support.hasSprings() || node->support.hasPrescribedDisplacements();
            if (!node->support.isRestrained()) {
                supIdx = 0;
            } else if (!hasExtra && node->support.isFixed()) {
                supIdx = 1;
            } else if (!hasExtra && node->support.isPinned()) {
                supIdx = 2;
            } else if (!hasExtra && node->support.isRollerZ()) {
                supIdx = 3;
            } else {
                supCombo->addItem(supStr, 4);
                supIdx = 4;
            }
            supCombo->setCurrentIndex(supIdx);

            auto* supRow = new QWidget(contentWidget);
            auto* supLay = new QHBoxLayout(supRow);
            supLay->setContentsMargins(0, 0, 0, 0);
            auto* supEditBtn = new QPushButton(QStringLiteral("..."), supRow);
            supEditBtn->setMaximumWidth(34);
            supEditBtn->setToolTip(GerenciadorTraducao::trText("Editar Condições de Apoio"));
            supLay->addWidget(supCombo, 1);
            supLay->addWidget(supEditBtn);
            formLayout->addRow(GerenciadorTraducao::trText("Vínculo:"), supRow);
            connect(supEditBtn, &QPushButton::clicked, this, &MainWindow::onAssignSupports);
            connect(supCombo, QOverload<int>::of(&QComboBox::activated), this, [this, supCombo](int idx) {
                const int preset = supCombo->itemData(idx).toInt();
                SupportRestraint restraint;
                if (preset == 0) {
                    restraint = SupportRestraint{};
                } else if (preset == 1) {
                    restraint = SupportRestraint::makeFixed();
                } else if (preset == 2) {
                    restraint = SupportRestraint::makePinned();
                } else if (preset == 3) {
                    restraint = SupportRestraint::makeRoller(2);
                } else {
                    return;
                }
                for (NodeId id : m_model.getSelectedNodes()) {
                    if (Node* n = m_model.findNode(id)) n->support = restraint;
                }
                m_model.pushUndo();
                onModelModified();
            });

            QString ldStr = GerenciadorTraducao::trText("Nenhuma");
            if (node->load.isActive()) {
                ldStr = QString("F=(%1, %2, %3) kN\nM=(%4, %5, %6) kNm")
                            .arg(node->load.force.x, 0, 'f', 1)
                            .arg(node->load.force.y, 0, 'f', 1)
                            .arg(node->load.force.z, 0, 'f', 1)
                            .arg(node->load.moment.x, 0, 'f', 1)
                            .arg(node->load.moment.y, 0, 'f', 1)
                            .arg(node->load.moment.z, 0, 'f', 1);
            }
            addRowWithEditBtn(GerenciadorTraducao::trText("Carga Nodal:"), ldStr, &MainWindow::onAssignNodalLoads);

            if (m_lastSolverResult.success) {
                formLayout->addRow(QStringLiteral("---"), new QLabel(QString("<b>%1</b>").arg(GerenciadorTraducao::trText("Resultados do Solver:")), contentWidget));
                formLayout->addRow(QStringLiteral("uX / uY / uZ:"),
                    new QLabel(QString("%1 / %2 / %3 mm")
                                   .arg(node->displacement[0] * 1000.0, 0, 'f', 3)
                                   .arg(node->displacement[1] * 1000.0, 0, 'f', 3)
                                   .arg(node->displacement[2] * 1000.0, 0, 'f', 3), contentWidget));
                formLayout->addRow(QStringLiteral("θX / θY / θZ:"),
                    new QLabel(QString("%1 / %2 / %3 mrad")
                                   .arg(node->displacement[3] * 1000.0, 0, 'f', 3)
                                   .arg(node->displacement[4] * 1000.0, 0, 'f', 3)
                                   .arg(node->displacement[5] * 1000.0, 0, 'f', 3), contentWidget));
                if (node->isSupported()) {
                    formLayout->addRow(QStringLiteral("Rx / Ry / Rz:"),
                        new QLabel(QString("%1 / %2 / %3 kN")
                                       .arg(node->reaction[0], 0, 'f', 2)
                                       .arg(node->reaction[1], 0, 'f', 2)
                                       .arg(node->reaction[2], 0, 'f', 2), contentWidget));
                    formLayout->addRow(QStringLiteral("Mx / My / Mz:"),
                        new QLabel(QString("%1 / %2 / %3 kNm")
                                       .arg(node->reaction[3], 0, 'f', 2)
                                       .arg(node->reaction[4], 0, 'f', 2)
                                       .arg(node->reaction[5], 0, 'f', 2), contentWidget));
                }
            }
        }
    }

    if (!m_model.getSelectedElements().empty()) {
        if (!m_model.getSelectedNodes().empty()) {
            formLayout->addRow(QStringLiteral("---"), new QLabel(QString("<b>%1</b>").arg(GerenciadorTraducao::trText("Barra Selecionada:")), contentWidget));
        }
        ElementId eid = *m_model.getSelectedElements().begin();
        Element* elem = m_model.findElement(eid);
        if (elem) {
            const size_t selCount = m_model.getSelectedElements().size();
            const QString idText = selCount > 1
                ? GerenciadorTraducao::trText("Barra ID %1 (+%2)").arg(elem->id).arg(selCount - 1)
                : GerenciadorTraducao::trText("Barra ID %1").arg(elem->id);
            formLayout->addRow(QString("<b>%1</b>").arg(GerenciadorTraducao::trText("Tipo:")), new QLabel(idText, contentWidget));

            // Conectividade editável (Nó Início -> Nó Fim)
            auto* startCombo = new QComboBox(contentWidget);
            auto* endCombo = new QComboBox(contentWidget);
            configureInspectorCombo(startCombo);
            configureInspectorCombo(endCombo);
            int sIdx = 0;
            int eIdx = 0;
            const auto& allNodes = m_model.getNodes();
            for (int i = 0; i < static_cast<int>(allNodes.size()); ++i) {
                const QString nLbl = GerenciadorTraducao::trText("Nó %1").arg(allNodes[i].id);
                startCombo->addItem(nLbl, allNodes[i].id);
                endCombo->addItem(nLbl, allNodes[i].id);
                if (allNodes[i].id == elem->nodeStart) sIdx = i;
                if (allNodes[i].id == elem->nodeEnd) eIdx = i;
            }
            startCombo->setCurrentIndex(sIdx);
            endCombo->setCurrentIndex(eIdx);

            auto* connRow = new QWidget(contentWidget);
            auto* connLay = new QHBoxLayout(connRow);
            connLay->setContentsMargins(0, 0, 0, 0);
            auto* arrowLbl = new QLabel(QStringLiteral("➔"), connRow);
            auto* btnApplyConn = new QPushButton(GerenciadorTraducao::trText("Aplicar"), connRow);
            btnApplyConn->setMaximumWidth(72);
            connLay->addWidget(startCombo, 1);
            connLay->addWidget(arrowLbl);
            connLay->addWidget(endCombo, 1);
            connLay->addWidget(btnApplyConn);
            formLayout->addRow(GerenciadorTraducao::trText("Conectividade:"), connRow);

            connect(btnApplyConn, &QPushButton::clicked, this, [this, eid, startCombo, endCombo]() {
                Element* el = m_model.findElement(eid);
                if (!el) return;
                const NodeId n1Id = startCombo->currentData().toInt();
                const NodeId n2Id = endCombo->currentData().toInt();
                if (n1Id == n2Id) {
                    m_windowManager->showModelessMessage(
                        QMessageBox::Warning,
                        GerenciadorTraducao::trText("Conectividade Inválida"),
                        GerenciadorTraducao::trText("O nó inicial e o nó final de uma barra devem ser distintos."));
                    return;
                }
                const Node* na = m_model.findNode(n1Id);
                const Node* nb = m_model.findNode(n2Id);
                if (!na || !nb || (na->position - nb->position).norm() < 1e-4) {
                    m_windowManager->showModelessMessage(
                        QMessageBox::Warning,
                        GerenciadorTraducao::trText("Conectividade Inválida"),
                        GerenciadorTraducao::trText("Os nós selecionados possuem comprimento nulo."));
                    return;
                }
                if (el->nodeStart == n1Id && el->nodeEnd == n2Id) return;
                el->nodeStart = n1Id;
                el->nodeEnd = n2Id;
                m_model.pushUndo();
                onModelModified();
            });

            formLayout->addRow(GerenciadorTraducao::trText("Comprimento (L):"), new QLabel(QString("%1 m").arg(elem->computeLength(m_model), 0, 'f', 3), contentWidget));

            // Material editável inline + botão Catálogo
            auto* matCombo = new QComboBox(contentWidget);
            matCombo->setObjectName(QStringLiteral("insp_mat_combo"));
            configureInspectorCombo(matCombo);
            int mIdx = 0;
            const auto& mats = m_model.getMaterials();
            for (int i = 0; i < static_cast<int>(mats.size()); ++i) {
                matCombo->addItem(GerenciadorTraducao::trText(QString::fromStdString(mats[i].name)), mats[i].id);
                if (mats[i].id == elem->materialId) mIdx = i;
            }
            matCombo->setCurrentIndex(mIdx);
            auto* matRow = new QWidget(contentWidget);
            auto* matLay = new QHBoxLayout(matRow);
            matLay->setContentsMargins(0, 0, 0, 0);
            auto* matEditBtn = new QPushButton(QStringLiteral("..."), matRow);
            matEditBtn->setMaximumWidth(34);
            matEditBtn->setToolTip(GerenciadorTraducao::trText("Catálogo de Materiais"));
            matLay->addWidget(matCombo, 1);
            matLay->addWidget(matEditBtn);
            formLayout->addRow(GerenciadorTraducao::trText("Material:"), matRow);
            connect(matEditBtn, &QPushButton::clicked, this, &MainWindow::onAssignMaterials);
            connect(matCombo, QOverload<int>::of(&QComboBox::activated), this, [this, matCombo](int idx) {
                const MaterialId mid = matCombo->itemData(idx).toInt();
                for (ElementId id : m_model.getSelectedElements()) {
                    if (Element* el = m_model.findElement(id)) el->materialId = mid;
                }
                m_model.pushUndo();
                onModelModified();
            });

            // Seção Transversal editável inline + botão Catálogo
            auto* secCombo = new QComboBox(contentWidget);
            secCombo->setObjectName(QStringLiteral("insp_sec_combo"));
            configureInspectorCombo(secCombo);
            int sSecIdx = 0;
            const auto& secs = m_model.getCrossSections();
            for (int i = 0; i < static_cast<int>(secs.size()); ++i) {
                secCombo->addItem(GerenciadorTraducao::trText(QString::fromStdString(secs[i].name)), secs[i].id);
                if (secs[i].id == elem->sectionId) sSecIdx = i;
            }
            secCombo->setCurrentIndex(sSecIdx);
            auto* secRow = new QWidget(contentWidget);
            auto* secLay = new QHBoxLayout(secRow);
            secLay->setContentsMargins(0, 0, 0, 0);
            auto* secEditBtn = new QPushButton(QStringLiteral("..."), secRow);
            secEditBtn->setMaximumWidth(34);
            secEditBtn->setToolTip(GerenciadorTraducao::trText("Catálogo de Seções Transversais"));
            secLay->addWidget(secCombo, 1);
            secLay->addWidget(secEditBtn);
            formLayout->addRow(GerenciadorTraducao::trText("Seção Transversal:"), secRow);
            connect(secEditBtn, &QPushButton::clicked, this, &MainWindow::onAssignSections);
            connect(secCombo, QOverload<int>::of(&QComboBox::activated), this, [this, secCombo](int idx) {
                const SectionId sid = secCombo->itemData(idx).toInt();
                for (ElementId id : m_model.getSelectedElements()) {
                    if (Element* el = m_model.findElement(id)) el->sectionId = sid;
                }
                m_model.pushUndo();
                onModelModified();
            });

            addRowWithEditBtn(
                GerenciadorTraducao::trText("Ângulo de Roll (γ):"),
                QString("%1 °").arg(elem->rollAngle * 180.0 / PI, 0, 'f', 1),
                &MainWindow::onAssignRollAngle);

            // Rótulas / Liberações editável inline + botão Editar
            auto* relCombo = new QComboBox(contentWidget);
            relCombo->setObjectName(QStringLiteral("insp_rel_combo"));
            configureInspectorCombo(relCombo);
            relCombo->addItem(GerenciadorTraducao::trText("Bi-Engastada"), 0);
            relCombo->addItem(GerenciadorTraducao::trText("Rótula Início"), 1);
            relCombo->addItem(GerenciadorTraducao::trText("Rótula Fim"), 2);
            relCombo->addItem(GerenciadorTraducao::trText("Barra Treliça"), 3);

            ElementReleases startRel;
            startRel.start[4] = startRel.start[5] = true;
            ElementReleases endRel;
            endRel.end[4] = endRel.end[5] = true;
            const ElementReleases trussRel = ElementReleases::makeTrussReleases();

            int relIdx = 4;
            if (!elem->releases.hasAnyRelease()) {
                relIdx = 0;
            } else if (elem->releases.start == startRel.start && elem->releases.end == startRel.end) {
                relIdx = 1;
            } else if (elem->releases.start == endRel.start && elem->releases.end == endRel.end) {
                relIdx = 2;
            } else if (elem->releases.start == trussRel.start && elem->releases.end == trussRel.end) {
                relIdx = 3;
            } else {
                relCombo->addItem(GerenciadorTraducao::trText("Liberações Ativas"), 4);
            }
            relCombo->setCurrentIndex(relIdx);

            auto* relRow = new QWidget(contentWidget);
            auto* relLay = new QHBoxLayout(relRow);
            relLay->setContentsMargins(0, 0, 0, 0);
            auto* relEditBtn = new QPushButton(QStringLiteral("..."), relRow);
            relEditBtn->setMaximumWidth(34);
            relEditBtn->setToolTip(GerenciadorTraducao::trText("Editar Liberações e Rótulas"));
            relLay->addWidget(relCombo, 1);
            relLay->addWidget(relEditBtn);
            formLayout->addRow(GerenciadorTraducao::trText("Rótulas:"), relRow);
            connect(relEditBtn, &QPushButton::clicked, this, &MainWindow::onAssignReleases);
            connect(relCombo, QOverload<int>::of(&QComboBox::activated), this, [this, relCombo, startRel, endRel, trussRel](int idx) {
                const int preset = relCombo->itemData(idx).toInt();
                ElementReleases rel;
                if (preset == 0) {
                    rel = ElementReleases{};
                } else if (preset == 1) {
                    rel = startRel;
                } else if (preset == 2) {
                    rel = endRel;
                } else if (preset == 3) {
                    rel = trussRel;
                } else {
                    return;
                }
                for (ElementId id : m_model.getSelectedElements()) {
                    if (Element* el = m_model.findElement(id)) el->releases = rel;
                }
                m_model.pushUndo();
                onModelModified();
            });

            QString qStr = GerenciadorTraducao::trText("Inativa");
            if (elem->distLoad.active) {
                const QString csStr = (elem->distLoad.coordSystem == CoordSystem::Global)
                    ? GerenciadorTraducao::trText("Global")
                    : GerenciadorTraducao::trText("Local");
                qStr = QString("(%1) (%2, %3, %4) kN/m")
                           .arg(csStr,
                                QString::number(elem->distLoad.w.x, 'f', 1),
                                QString::number(elem->distLoad.w.y, 'f', 1),
                                QString::number(elem->distLoad.w.z, 'f', 1));
            }
            addRowWithEditBtn(GerenciadorTraducao::trText("Carga Distr.:"), qStr, &MainWindow::onAssignDistributedLoads);

            QString thStr = elem->thermalLoad.active
                ? QString("ΔT = %1 °C").arg(elem->thermalLoad.deltaT, 0, 'f', 1)
                : GerenciadorTraducao::trText("Inativa");
            addRowWithEditBtn(GerenciadorTraducao::trText("Carga Térmica:"), thStr, &MainWindow::onAssignThermalLoads);

            if (elem->hasResults) {
                formLayout->addRow(QStringLiteral("---"), new QLabel(QString("<b>%1</b>").arg(GerenciadorTraducao::trText("Esforços Internos Extremos:")), contentWidget));
                InternalForces f0 = elem->evaluateAt(0.0, m_model);
                InternalForces fMid = elem->evaluateAt(0.5, m_model);
                InternalForces f1 = elem->evaluateAt(1.0, m_model);

                formLayout->addRow(GerenciadorTraducao::trText("Normal N (kN):"), new QLabel(GerenciadorTraducao::trText("Início: %1 | Fim: %2").arg(f0.N, 0, 'f', 1).arg(f1.N, 0, 'f', 1), contentWidget));
                formLayout->addRow(GerenciadorTraducao::trText("Cortante Vy (kN):"), new QLabel(GerenciadorTraducao::trText("Início: %1 | Fim: %2").arg(f0.Vy, 0, 'f', 1).arg(f1.Vy, 0, 'f', 1), contentWidget));
                formLayout->addRow(GerenciadorTraducao::trText("Cortante Vz (kN):"), new QLabel(GerenciadorTraducao::trText("Início: %1 | Fim: %2").arg(f0.Vz, 0, 'f', 1).arg(f1.Vz, 0, 'f', 1), contentWidget));
                formLayout->addRow(GerenciadorTraducao::trText("Fletor Mz (kNm):"), new QLabel(GerenciadorTraducao::trText("Início: %1 | Meio: %2 | Fim: %3").arg(f0.Mz, 0, 'f', 1).arg(fMid.Mz, 0, 'f', 1).arg(f1.Mz, 0, 'f', 1), contentWidget));
                formLayout->addRow(GerenciadorTraducao::trText("Fletor My (kNm):"), new QLabel(GerenciadorTraducao::trText("Início: %1 | Meio: %2 | Fim: %3").arg(f0.My, 0, 'f', 1).arg(fMid.My, 0, 'f', 1).arg(f1.My, 0, 'f', 1), contentWidget));
                formLayout->addRow(GerenciadorTraducao::trText("Torsor Tx (kNm):"), new QLabel(QString("%1").arg(f0.Tx, 0, 'f', 1), contentWidget));
            }
        }
    }

    if (m_model.getSelectedNodes().empty() && m_model.getSelectedElements().empty()) {
        auto* hintLbl = new QLabel(QString("<i>%1</i>").arg(GerenciadorTraducao::trText("Nenhum nó ou barra selecionado. Clique em um elemento no Viewport 3D para inspecionar.")), contentWidget);
        hintLbl->setWordWrap(true);
        formLayout->addRow(hintLbl);
    }

    m_inspectorLayout->addWidget(contentWidget);
}

void MainWindow::updateResultsTables() {
    auto* dispModel = m_resultTableModels[static_cast<std::size_t>(ui::ResultTableKind::Displacements)];
    auto* reacModel = m_resultTableModels[static_cast<std::size_t>(ui::ResultTableKind::Reactions)];
    auto* forcesModel = m_resultTableModels[static_cast<std::size_t>(ui::ResultTableKind::BarForces)];
    auto* contModel = m_resultTableModels[static_cast<std::size_t>(ui::ResultTableKind::ContinuumStress)];

    if (!m_lastSolverResult.success) {
        for (ui::ResultTableModel* model : m_resultTableModels) {
            if (model != nullptr) {
                model->clearRows();
            }
        }
        syncExtraLayoutTables();
        return;
    }

    const auto& nodes = m_model.getNodes();
    const auto& mesh = m_model.surfaceMesh();

    // Evita duplicar linhas de nós coincidentes entre barras 1D e malha 2D/3D acoplada (busca O(1))
    SpatialPointHash frameHash(1e-4);
    frameHash.reserve(nodes.size());
    for (std::size_t idx = 0; idx < nodes.size(); ++idx) {
        const auto& fn = nodes[idx];
        frameHash.insert(fn.x(), fn.y(), fn.z(), idx);
    }

    std::vector<const kernel::MeshNode*> uniqueMeshNodes;
    uniqueMeshNodes.reserve(mesh.nodes.size());
    for (const auto& mn : mesh.nodes) {
        if (!frameHash.findNearestWithinSq(mn.position.x(), mn.position.y(), mn.position.z(), 1e-12)) {
            uniqueMeshNodes.push_back(&mn);
        }
    }

    // 1. Tabela de Deslocamentos Nodais (modelo compartilhado)
    if (dispModel != nullptr) {
        std::vector<ui::ResultTableRow> dispRows;
        dispRows.reserve(nodes.size() + uniqueMeshNodes.size());
        for (const auto& n : nodes) {
            ui::ResultTableRow row;
            row.category = QStringLiteral("node");
            row.entityId = static_cast<quint64>(n.id);
            row.hasEntityId = true;
            row.cells = {
                QString::number(n.id),
                QString::number(n.displacement[0] * 1000.0, 'f', 3),
                QString::number(n.displacement[1] * 1000.0, 'f', 3),
                QString::number(n.displacement[2] * 1000.0, 'f', 3),
                QString::number(n.displacement[3] * 1000.0, 'f', 3),
                QString::number(n.displacement[4] * 1000.0, 'f', 3),
                QString::number(n.displacement[5] * 1000.0, 'f', 3)
            };
            dispRows.push_back(std::move(row));
        }
        for (const auto* mn : uniqueMeshNodes) {
            ui::ResultTableRow row;
            row.category = QStringLiteral("mesh_node");
            const QString idLabel = nodes.empty() ? QString::number(mn->id) : QStringLiteral("M%1").arg(mn->id);
            row.cells.reserve(7);
            row.cells.append(idLabel);
            for (int d = 0; d < 6; ++d) {
                row.cells.append(QString::number(mn->displacement[d] * 1000.0, 'f', 3));
            }
            dispRows.push_back(std::move(row));
        }
        dispModel->setRows(std::move(dispRows));
    }
    if (m_tableConfigurator) {
        m_tableConfigurator->afterTablePopulated(m_dispTable);
    }

    // 2. Tabela de Reações de Apoio (modelo compartilhado)
    if (reacModel != nullptr) {
        std::vector<ui::ResultTableRow> reacRows;
        reacRows.reserve(nodes.size() + uniqueMeshNodes.size() + 1);
        for (const auto& n : nodes) {
            if (!n.isSupported()) continue;
            ui::ResultTableRow row;
            row.category = QStringLiteral("support");
            row.entityId = static_cast<quint64>(n.id);
            row.hasEntityId = true;
            row.cells = {
                QString::number(n.id),
                QString::number(n.reaction[0], 'f', 2),
                QString::number(n.reaction[1], 'f', 2),
                QString::number(n.reaction[2], 'f', 2),
                QString::number(n.reaction[3], 'f', 2),
                QString::number(n.reaction[4], 'f', 2),
                QString::number(n.reaction[5], 'f', 2)
            };
            reacRows.push_back(std::move(row));
        }
        for (const auto* mn : uniqueMeshNodes) {
            if (!mn->isSupported()) continue;
            ui::ResultTableRow row;
            row.category = QStringLiteral("mesh_support");
            const QString idLabel = nodes.empty() ? QString::number(mn->id) : QStringLiteral("M%1").arg(mn->id);
            row.cells.reserve(7);
            row.cells.append(idLabel);
            for (int d = 0; d < 6; ++d) {
                row.cells.append(QString::number(mn->reaction[d], 'f', 2));
            }
            reacRows.push_back(std::move(row));
        }

        ui::ResultTableRow sumRow;
        sumRow.category = QStringLiteral("reaction_sum");
        sumRow.isSummary = true;
        sumRow.cells = {
            locale::GerenciadorTraducao::trText("Σ Reações"),
            QString::number(m_lastSolverResult.equilibrium.sumReactions.x, 'f', 2),
            QString::number(m_lastSolverResult.equilibrium.sumReactions.y, 'f', 2),
            QString::number(m_lastSolverResult.equilibrium.sumReactions.z, 'f', 2),
            QString::number(m_lastSolverResult.equilibrium.sumReactionMoments.x, 'f', 2),
            QString::number(m_lastSolverResult.equilibrium.sumReactionMoments.y, 'f', 2),
            QString::number(m_lastSolverResult.equilibrium.sumReactionMoments.z, 'f', 2)
        };
        reacRows.push_back(std::move(sumRow));
        reacModel->setRows(std::move(reacRows));
    }
    refreshReactionSummaryRowTheme();
    if (m_tableConfigurator) {
        m_tableConfigurator->afterTablePopulated(m_reacTable);
        refreshReactionSummaryRowTheme();
    }

    // 3. Tabela de Esforços nas Barras (modelo compartilhado)
    if (forcesModel != nullptr) {
        std::vector<ui::ResultTableRow> forceRows;
        forceRows.reserve(m_lastSolverResult.elementExtremes.size());
        for (const auto& ext : m_lastSolverResult.elementExtremes) {
            ui::ResultTableRow row;
            row.category = QStringLiteral("bar");
            row.entityId = static_cast<quint64>(ext.elementId);
            row.hasEntityId = true;
            row.cells = {
                QString::number(ext.elementId),
                QString("[%1; %2]").arg(ext.N_min, 0, 'f', 1).arg(ext.N_max, 0, 'f', 1),
                QString("[%1; %2]").arg(ext.Vy_min, 0, 'f', 1).arg(ext.Vy_max, 0, 'f', 1),
                QString("[%1; %2]").arg(ext.Vz_min, 0, 'f', 1).arg(ext.Vz_max, 0, 'f', 1),
                QString("[%1; %2]").arg(ext.Tx_min, 0, 'f', 1).arg(ext.Tx_max, 0, 'f', 1),
                QString("[%1; %2]").arg(ext.My_min, 0, 'f', 1).arg(ext.My_max, 0, 'f', 1),
                QString("[%1; %2]").arg(ext.Mz_min, 0, 'f', 1).arg(ext.Mz_max, 0, 'f', 1),
                QString::number(ext.maxDeflection * 1000.0, 'f', 2)
            };
            forceRows.push_back(std::move(row));
        }
        forcesModel->setRows(std::move(forceRows));
    }
    if (m_tableConfigurator) {
        m_tableConfigurator->afterTablePopulated(m_forcesTable);
    }

    // 4. Tabela de Esforços e Tensões 2D/3D (Casca / Sólido - modelo compartilhado)
    if (m_continuumTable && contModel != nullptr) {
        const auto& cont = m_lastSolverResult.continuumElementResults;
        std::vector<ui::ResultTableRow> contRows;
        contRows.reserve(cont.size());
        for (const auto& ce : cont) {
            ui::ResultTableRow row;
            row.category = ce.isShell ? QStringLiteral("shell") : QStringLiteral("solid");
            row.entityId = static_cast<quint64>(ce.elementId);
            row.hasEntityId = true;
            if (ce.isShell) {
                row.cells = {
                    QString::number(ce.elementId),
                    QString::fromStdString(ce.cellTypeName),
                    QString::number(ce.vonMisesStress, 'f', 2),
                    QString::number(ce.maxPrincipalStress, 'f', 2),
                    QString::number(ce.minPrincipalStress, 'f', 2),
                    QString::number(ce.membraneResultants[0], 'f', 2) + QStringLiteral(" kN/m"),
                    QString::number(ce.membraneResultants[1], 'f', 2) + QStringLiteral(" kN/m"),
                    QString::number(ce.bendingResultants[0], 'f', 3) + QStringLiteral(" kNm/m")
                };
            } else {
                row.cells = {
                    QString::number(ce.elementId),
                    QString::fromStdString(ce.cellTypeName),
                    QString::number(ce.vonMisesStress, 'f', 2),
                    QString::number(ce.maxPrincipalStress, 'f', 2),
                    QString::number(ce.minPrincipalStress, 'f', 2),
                    QString::number(ce.stressVoigt[0], 'f', 2) + QStringLiteral(" kPa"),
                    QString::number(ce.stressVoigt[1], 'f', 2) + QStringLiteral(" kPa"),
                    QString::number(ce.stressVoigt[4], 'f', 2) + QStringLiteral(" kPa")
                };
            }
            contRows.push_back(std::move(row));
        }
        contModel->setRows(std::move(contRows));
        if (m_tableConfigurator) {
            m_tableConfigurator->afterTablePopulated(m_continuumTable);
        }
    }

    syncExtraLayoutTables();
}

void MainWindow::initDefaultResultsLayoutAssignments() {
    m_resultsModeTableAssign = {};
    // 1 Layout: all 4 tables in Layout 1
    m_resultsModeTableAssign[0][0] = {true, true, true, true};
    // 2 Layouts: Layout 1 (Displacements, Reactions), Layout 2 (BarForces, ContinuumStress)
    m_resultsModeTableAssign[1][0] = {true, true, false, false};
    m_resultsModeTableAssign[1][1] = {false, false, true, true};
    // 3 Layouts: Layout 1 (Displacements), Layout 2 (Reactions), Layout 3 (BarForces, ContinuumStress)
    m_resultsModeTableAssign[2][0] = {true, false, false, false};
    m_resultsModeTableAssign[2][1] = {false, true, false, false};
    m_resultsModeTableAssign[2][2] = {false, false, true, true};
    // 4 Layouts: one table per layout
    m_resultsModeTableAssign[3][0] = {true, false, false, false};
    m_resultsModeTableAssign[3][1] = {false, true, false, false};
    m_resultsModeTableAssign[3][2] = {false, false, true, false};
    m_resultsModeTableAssign[3][3] = {false, false, false, true};

    m_resultsSplitterSizesByMode[0] = {1000};
    m_resultsSplitterSizesByMode[1] = {1000, 1000};
    m_resultsSplitterSizesByMode[2] = {1000, 1000, 1000};
    m_resultsSplitterSizesByMode[3] = {1000, 1000, 1000, 1000};
    m_resultsActiveTabByMode = {};
}

QString MainWindow::appLocalDataDirectory() {
    return io::UserPreferencesStore::preferencesDirectory();
}

QString MainWindow::userPreferencesFilePath() {
    return io::UserPreferencesStore::userPreferencesFilePath();
}

QString MainWindow::initialFileDialogPath(const QString& defaultFileName) const {
    const bool hasDir = !m_lastFileDialogDir.isEmpty() && QDir(m_lastFileDialogDir).exists();
    if (!hasDir) {
        return defaultFileName;
    }
    return defaultFileName.isEmpty()
               ? m_lastFileDialogDir
               : QDir(m_lastFileDialogDir).filePath(defaultFileName);
}

void MainWindow::rememberFileDialogPath(const QString& filePath) const {
    if (filePath.isEmpty()) {
        return;
    }
    const QString dirPath = QFileInfo(filePath).absolutePath();
    if (dirPath.isEmpty() || m_lastFileDialogDir == dirPath) {
        return;
    }
    m_lastFileDialogDir = dirPath;
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

QJsonObject MainWindow::serializeResultsPanelJson() const {
    const int modeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
    if (m_resultsSplitter != nullptr && m_resultsSplitter->isVisible()) {
        const QList<int> curSizes = m_resultsSplitter->sizes();
        if (curSizes.size() == m_resultsLayoutCount) {
            int total = 0;
            for (int s : curSizes) {
                total += s;
            }
            if (total > 0) {
                m_resultsSplitterSizesByMode[static_cast<std::size_t>(modeIdx)] = curSizes;
            }
        }
    }
    for (int L = 0; L < m_resultsLayoutCount; ++L) {
        const QTabWidget* tabW = m_resultsLayoutTabWidgets[static_cast<std::size_t>(L)];
        if (tabW != nullptr && tabW->count() > 0 && tabW->currentIndex() >= 0) {
            m_resultsActiveTabByMode[static_cast<std::size_t>(modeIdx)][static_cast<std::size_t>(L)] =
                tabW->currentIndex();
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("layoutCount"), m_resultsLayoutCount);

    QJsonArray modesArr;
    for (int m = 0; m < 4; ++m) {
        QJsonArray layoutsArr;
        for (int L = 0; L < 4; ++L) {
            QJsonArray tablesArr;
            for (int k = 0; k < 4; ++k) {
                tablesArr.append(m_resultsModeTableAssign[static_cast<std::size_t>(m)]
                                                         [static_cast<std::size_t>(L)]
                                                         [static_cast<std::size_t>(k)]);
            }
            layoutsArr.append(tablesArr);
        }
        modesArr.append(layoutsArr);
    }
    root.insert(QStringLiteral("modeTableAssignments"), modesArr);

    QJsonArray sizesByModeArr;
    for (int m = 0; m < 4; ++m) {
        QJsonArray szArr;
        for (int s : std::as_const(m_resultsSplitterSizesByMode[static_cast<std::size_t>(m)])) {
            szArr.append(s);
        }
        sizesByModeArr.append(szArr);
    }
    root.insert(QStringLiteral("splitterSizesByMode"), sizesByModeArr);

    QJsonArray activeTabByModeArr;
    for (int m = 0; m < 4; ++m) {
        QJsonArray tabsArr;
        for (int L = 0; L < 4; ++L) {
            tabsArr.append(m_resultsActiveTabByMode[static_cast<std::size_t>(m)][static_cast<std::size_t>(L)]);
        }
        activeTabByModeArr.append(tabsArr);
    }
    root.insert(QStringLiteral("activeTabByMode"), activeTabByModeArr);

    root.insert(QStringLiteral("tables"),
                m_tableConfigurator ? m_tableConfigurator->saveAllTablesToJson() : QJsonObject{});
    return root;
}

bool MainWindow::deserializeResultsPanelJson(const QJsonObject& root) {
    if (root.isEmpty()) {
        return false;
    }

    const ScopedBoolFlag guard(m_rebuildingOrLoadingResultsSettings);

    const QJsonArray modesArr = root.value(QStringLiteral("modeTableAssignments")).toArray();
    if (modesArr.size() == 4) {
        for (int m = 0; m < 4; ++m) {
            const QJsonArray layoutsArr = modesArr.at(m).toArray();
            if (layoutsArr.size() != 4) {
                continue;
            }
            for (int L = 0; L < 4; ++L) {
                const QJsonArray tablesArr = layoutsArr.at(L).toArray();
                if (tablesArr.size() != 4) {
                    continue;
                }
                for (int k = 0; k < 4; ++k) {
                    m_resultsModeTableAssign[static_cast<std::size_t>(m)]
                                            [static_cast<std::size_t>(L)]
                                            [static_cast<std::size_t>(k)] = tablesArr.at(k).toBool(false);
                }
            }
        }
    }

    const QJsonArray sizesByModeArr = root.value(QStringLiteral("splitterSizesByMode")).toArray();
    if (sizesByModeArr.size() == 4) {
        for (int m = 0; m < 4; ++m) {
            const QJsonArray szArr = sizesByModeArr.at(m).toArray();
            if (szArr.size() == m + 1) {
                QList<int> loadedSizes;
                loadedSizes.reserve(szArr.size());
                int total = 0;
                for (int i = 0; i < szArr.size(); ++i) {
                    const int s = std::max(0, szArr.at(i).toInt(0));
                    loadedSizes.append(s);
                    total += s;
                }
                if (total > 0) {
                    m_resultsSplitterSizesByMode[static_cast<std::size_t>(m)] = loadedSizes;
                }
            }
        }
    }

    const QJsonArray activeTabByModeArr = root.value(QStringLiteral("activeTabByMode")).toArray();
    if (activeTabByModeArr.size() == 4) {
        for (int m = 0; m < 4; ++m) {
            const QJsonArray tabsArr = activeTabByModeArr.at(m).toArray();
            if (tabsArr.size() == 4) {
                for (int L = 0; L < 4; ++L) {
                    m_resultsActiveTabByMode[static_cast<std::size_t>(m)][static_cast<std::size_t>(L)] =
                        std::max(0, tabsArr.at(L).toInt(0));
                }
            }
        }
    }

    m_resultsLayoutCount = std::clamp(root.value(QStringLiteral("layoutCount")).toInt(1), 1, 4);

    if (m_tableConfigurator != nullptr && root.value(QStringLiteral("tables")).isObject()) {
        m_tableConfigurator->loadAllTablesFromJson(root.value(QStringLiteral("tables")).toObject());
    }

    rebuildResultsLayoutsUi();
    if (m_resultsSplitter != nullptr) {
        const int modeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
        QList<int> sizes = m_resultsSplitterSizesByMode[static_cast<std::size_t>(modeIdx)];
        if (sizes.size() != m_resultsSplitter->count()) {
            sizes.clear();
            sizes.reserve(m_resultsSplitter->count());
            for (int i = 0; i < m_resultsSplitter->count(); ++i) {
                sizes.append(1000);
            }
        }
        m_resultsSplitter->setSizes(sizes);
    }
    syncResultsLayoutsMenuState();
    return true;
}

void MainWindow::saveResultsPanelSettings() const {
    io::UserPreferencesStore::updateSection(QStringLiteral("resultsPanel"), serializeResultsPanelJson());
}

bool MainWindow::loadResultsPanelSettings() {
    bool exists = false;
    const QJsonObject resultsObj = io::UserPreferencesStore::readSection(QStringLiteral("resultsPanel"), &exists);
    if (!exists || resultsObj.isEmpty()) {
        return false;
    }
    return deserializeResultsPanelJson(resultsObj);
}

namespace {

template <typename WidgetT>
QList<WidgetT*> collectPersistableWidgets(const QDialog* dlg) {
    QList<WidgetT*> result;
    if (dlg == nullptr) {
        return result;
    }
    for (WidgetT* w : dlg->findChildren<WidgetT*>()) {
        if (w == nullptr || w->objectName().startsWith(QLatin1String("qt_"))) {
            continue;
        }
        if constexpr (std::is_same_v<WidgetT, QLineEdit>) {
            if (qobject_cast<QAbstractSpinBox*>(w->parentWidget()) != nullptr ||
                qobject_cast<QComboBox*>(w->parentWidget()) != nullptr) {
                continue;
            }
        }
        result.append(w);
    }
    return result;
}

QString dialogControlKey(const char* prefix, int idx, const QWidget* w) {
    const QString obj = w != nullptr ? w->objectName() : QString();
    return obj.isEmpty()
               ? QStringLiteral("%1_%2").arg(QLatin1String(prefix), QString::number(idx))
               : QStringLiteral("%1_%2_%3").arg(QLatin1String(prefix), QString::number(idx), obj);
}

bool shouldSkipLoadComboWidget(const QString& dialogKey,
                               const char* prefix,
                               int idx,
                               const QWidget* widget = nullptr) {
    if (widget != nullptr &&
        (widget->objectName() == QLatin1String("guided_highlight_color_combo") ||
         widget->objectName() == QLatin1String("guided_highlight_phase_combo"))) {
        return true;
    }
    if (dialogKey != QLatin1String("LoadCombinationsDialog")) {
        return false;
    }
    if (std::strcmp(prefix, "edit") == 0 || std::strcmp(prefix, "chk") == 0) {
        return true;
    }
    if (std::strcmp(prefix, "combo") == 0 && idx < 5) {
        return true;
    }
    if (std::strcmp(prefix, "dspin") == 0 && idx == 0) {
        return true;
    }
    return false;
}

} // namespace

void MainWindow::capturePersistentDialogState(const QString& dialogKey,
                                              const QDialog* dlg,
                                              bool persistControls) const {
    if (dlg == nullptr || dialogKey.isEmpty()) {
        return;
    }
    QJsonObject dlgObj = m_savedDialogsStateJson.value(dialogKey).toObject();
    if (dlg->width() >= 200 && dlg->height() >= 120) {
        dlgObj.insert(QStringLiteral("width"), dlg->width());
        dlgObj.insert(QStringLiteral("height"), dlg->height());
    }

    if (persistControls) {
        QJsonObject controlsObj;
        const auto tabs = collectPersistableWidgets<QTabWidget>(dlg);
        for (int i = 0; i < tabs.size(); ++i) {
            controlsObj.insert(dialogControlKey("tab", i, tabs[i]), tabs[i]->currentIndex());
        }
        const auto spins = collectPersistableWidgets<QSpinBox>(dlg);
        for (int i = 0; i < spins.size(); ++i) {
            if (!shouldSkipLoadComboWidget(dialogKey, "spin", i, spins[i])) {
                controlsObj.insert(dialogControlKey("spin", i, spins[i]), spins[i]->value());
            }
        }
        const auto dspins = collectPersistableWidgets<QDoubleSpinBox>(dlg);
        for (int i = 0; i < dspins.size(); ++i) {
            if (!shouldSkipLoadComboWidget(dialogKey, "dspin", i, dspins[i])) {
                controlsObj.insert(dialogControlKey("dspin", i, dspins[i]), dspins[i]->value());
            }
        }
        const auto checks = collectPersistableWidgets<QCheckBox>(dlg);
        for (int i = 0; i < checks.size(); ++i) {
            if (!shouldSkipLoadComboWidget(dialogKey, "chk", i, checks[i])) {
                controlsObj.insert(dialogControlKey("chk", i, checks[i]), checks[i]->isChecked());
            }
        }
        const auto sliders = collectPersistableWidgets<QSlider>(dlg);
        for (int i = 0; i < sliders.size(); ++i) {
            controlsObj.insert(dialogControlKey("slider", i, sliders[i]), sliders[i]->value());
        }
        const auto combos = collectPersistableWidgets<QComboBox>(dlg);
        for (int i = 0; i < combos.size(); ++i) {
            if (!shouldSkipLoadComboWidget(dialogKey, "combo", i, combos[i])) {
                controlsObj.insert(dialogControlKey("combo", i, combos[i]), combos[i]->currentIndex());
            }
        }
        const auto edits = collectPersistableWidgets<QLineEdit>(dlg);
        for (int i = 0; i < edits.size(); ++i) {
            if (!shouldSkipLoadComboWidget(dialogKey, "edit", i, edits[i])) {
                controlsObj.insert(dialogControlKey("edit", i, edits[i]), edits[i]->text());
            }
        }
        if (const auto* guidedDlg = qobject_cast<const GuidedSessionDialog*>(dlg)) {
            controlsObj.insert(
                QStringLiteral("stepHighlightColor"),
                guidedDlg->stepHighlightColor().name(QColor::HexRgb).toUpper());
            QJsonArray phaseColorsArr;
            const int phaseCount = guidedDlg->maxPhasesCount();
            for (int p = 1; p <= phaseCount; ++p) {
                phaseColorsArr.append(guidedDlg->phaseHighlightColor(p).name(QColor::HexRgb).toUpper());
            }
            controlsObj.insert(QStringLiteral("phaseHighlightColors"), phaseColorsArr);
        }
        dlgObj.insert(QStringLiteral("controls"), controlsObj);
    }

    m_savedDialogsStateJson.insert(dialogKey, dlgObj);
}

void MainWindow::restorePersistentDialogState(const QString& dialogKey,
                                              QDialog* dlg,
                                              bool persistControls) {
    if (dlg == nullptr || dialogKey.isEmpty() || !m_savedDialogsStateJson.contains(dialogKey)) {
        return;
    }

    const ScopedBoolFlag guard(m_rebuildingOrLoadingResultsSettings);
    const QJsonObject dlgObj = m_savedDialogsStateJson.value(dialogKey).toObject();

    const int w = dlgObj.value(QStringLiteral("width")).toInt(0);
    const int h = dlgObj.value(QStringLiteral("height")).toInt(0);
    const QSize minHint = dlg->minimumSizeHint();
    if (w >= std::max(200, minHint.width()) && h >= std::max(120, minHint.height())) {
        dlg->resize(w, h);
    }

    if (!persistControls || !dlgObj.value(QStringLiteral("controls")).isObject()) {
        return;
    }

    const QJsonObject controlsObj = dlgObj.value(QStringLiteral("controls")).toObject();

    const auto tabs = collectPersistableWidgets<QTabWidget>(dlg);
    for (int i = 0; i < tabs.size(); ++i) {
        const QString key = dialogControlKey("tab", i, tabs[i]);
        if (controlsObj.contains(key)) {
            const int idx = controlsObj.value(key).toInt(-1);
            if (idx >= 0 && idx < tabs[i]->count()) {
                tabs[i]->setCurrentIndex(idx);
            }
        }
    }
    const auto combos = collectPersistableWidgets<QComboBox>(dlg);
    for (int i = 0; i < combos.size(); ++i) {
        if (shouldSkipLoadComboWidget(dialogKey, "combo", i, combos[i])) {
            continue;
        }
        const QString key = dialogControlKey("combo", i, combos[i]);
        if (controlsObj.contains(key)) {
            const int idx = controlsObj.value(key).toInt(-1);
            if (idx >= 0 && idx < combos[i]->count()) {
                combos[i]->setCurrentIndex(idx);
            }
        }
    }
    const auto checks = collectPersistableWidgets<QCheckBox>(dlg);
    for (int i = 0; i < checks.size(); ++i) {
        if (shouldSkipLoadComboWidget(dialogKey, "chk", i, checks[i])) {
            continue;
        }
        const QString key = dialogControlKey("chk", i, checks[i]);
        if (controlsObj.contains(key)) {
            checks[i]->setChecked(controlsObj.value(key).toBool(checks[i]->isChecked()));
        }
    }
    const auto spins = collectPersistableWidgets<QSpinBox>(dlg);
    for (int i = 0; i < spins.size(); ++i) {
        if (shouldSkipLoadComboWidget(dialogKey, "spin", i, spins[i])) {
            continue;
        }
        const QString key = dialogControlKey("spin", i, spins[i]);
        if (controlsObj.contains(key)) {
            spins[i]->setValue(controlsObj.value(key).toInt(spins[i]->value()));
        }
    }
    const auto dspins = collectPersistableWidgets<QDoubleSpinBox>(dlg);
    for (int i = 0; i < dspins.size(); ++i) {
        if (shouldSkipLoadComboWidget(dialogKey, "dspin", i, dspins[i])) {
            continue;
        }
        const QString key = dialogControlKey("dspin", i, dspins[i]);
        if (controlsObj.contains(key)) {
            dspins[i]->setValue(controlsObj.value(key).toDouble(dspins[i]->value()));
        }
    }
    const auto sliders = collectPersistableWidgets<QSlider>(dlg);
    for (int i = 0; i < sliders.size(); ++i) {
        const QString key = dialogControlKey("slider", i, sliders[i]);
        if (controlsObj.contains(key)) {
            sliders[i]->setValue(controlsObj.value(key).toInt(sliders[i]->value()));
        }
    }
    const auto edits = collectPersistableWidgets<QLineEdit>(dlg);
    for (int i = 0; i < edits.size(); ++i) {
        if (shouldSkipLoadComboWidget(dialogKey, "edit", i, edits[i])) {
            continue;
        }
        const QString key = dialogControlKey("edit", i, edits[i]);
        if (controlsObj.contains(key)) {
            const QString txt = controlsObj.value(key).toString();
            if (!txt.isEmpty()) {
                edits[i]->setText(txt);
            }
        }
    }
    if (auto* guidedDlg = qobject_cast<GuidedSessionDialog*>(dlg)) {
        if (controlsObj.contains(QStringLiteral("phaseHighlightColors"))) {
            const QJsonArray arr = controlsObj.value(QStringLiteral("phaseHighlightColors")).toArray();
            for (int i = 0; i < arr.size(); ++i) {
                const QColor c(arr.at(i).toString().trimmed());
                if (c.isValid()) {
                    guidedDlg->setPhaseHighlightColor(i + 1, c);
                }
            }
        } else if (controlsObj.contains(QStringLiteral("stepHighlightColor"))) {
            const QColor savedColor(controlsObj.value(QStringLiteral("stepHighlightColor")).toString().trimmed());
            if (savedColor.isValid()) {
                guidedDlg->setStepHighlightColor(savedColor);
            }
        }
    }
}

void MainWindow::bindPersistentDialog(const QString& dialogKey, QDialog* dlg, bool persistControls) {
    if (dlg == nullptr || dialogKey.isEmpty()) {
        return;
    }

    restorePersistentDialogState(dialogKey, dlg, persistControls);

    QPointer<QDialog> dlgPtr(dlg);
    auto onControlChanged = [this, dialogKey, dlgPtr, persistControls]() {
        if (!m_rebuildingOrLoadingResultsSettings && dlgPtr) {
            capturePersistentDialogState(dialogKey, dlgPtr.data(), persistControls);
            saveUserPreferences();
        }
    };

    connect(dlg, &QDialog::finished, this, [onControlChanged](int) {
        onControlChanged();
    });

    if (!persistControls) {
        return;
    }

    const QList<QTabWidget*> tabWidgets = collectPersistableWidgets<QTabWidget>(dlg);
    for (QTabWidget* tw : tabWidgets) {
        connect(tw, &QTabWidget::currentChanged, this, [onControlChanged](int) { onControlChanged(); });
    }
    const auto spins = collectPersistableWidgets<QSpinBox>(dlg);
    for (int i = 0; i < spins.size(); ++i) {
        if (!shouldSkipLoadComboWidget(dialogKey, "spin", i)) {
            connect(spins[i], QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [onControlChanged](int) { onControlChanged(); });
        }
    }
    const auto dspins = collectPersistableWidgets<QDoubleSpinBox>(dlg);
    for (int i = 0; i < dspins.size(); ++i) {
        if (!shouldSkipLoadComboWidget(dialogKey, "dspin", i)) {
            connect(dspins[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [onControlChanged](double) { onControlChanged(); });
        }
    }
    const auto checks = collectPersistableWidgets<QCheckBox>(dlg);
    for (int i = 0; i < checks.size(); ++i) {
        if (!shouldSkipLoadComboWidget(dialogKey, "chk", i)) {
            connect(checks[i], &QCheckBox::toggled, this, [onControlChanged](bool) { onControlChanged(); });
        }
    }
    const QList<QSlider*> sliders = collectPersistableWidgets<QSlider>(dlg);
    for (QSlider* sl : sliders) {
        connect(sl, &QSlider::valueChanged, this, [onControlChanged](int) { onControlChanged(); });
    }
    const auto combos = collectPersistableWidgets<QComboBox>(dlg);
    for (int i = 0; i < combos.size(); ++i) {
        if (!shouldSkipLoadComboWidget(dialogKey, "combo", i, combos[i])) {
            connect(combos[i], QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [onControlChanged](int) { onControlChanged(); });
        }
    }
    const auto edits = collectPersistableWidgets<QLineEdit>(dlg);
    for (int i = 0; i < edits.size(); ++i) {
        if (!shouldSkipLoadComboWidget(dialogKey, "edit", i)) {
            connect(edits[i], &QLineEdit::editingFinished, this, [onControlChanged]() { onControlChanged(); });
        }
    }
}

void MainWindow::captureAllOpenDialogsState() const {
    capturePersistentDialogState(QStringLiteral("LoadCombinationsDialog"), m_loadCombinationsDialog.data(), true);
    capturePersistentDialogState(QStringLiteral("MeshGeneratorDialog"), m_meshGeneratorDialog.data(), true);
    capturePersistentDialogState(QStringLiteral("AnalysisSuiteDialog"), m_analysisSuiteDialog.data(), true);
    capturePersistentDialogState(QStringLiteral("PostProcessControlDialog"), m_postProcessControlDialog.data(), false);
    capturePersistentDialogState(QStringLiteral("NbrComplianceDialog"), m_nbrComplianceDialog.data(), true);
    capturePersistentDialogState(QStringLiteral("PdfReportOptionsDialog"), m_pdfOptionsDialog.data(), true);
    capturePersistentDialogState(QStringLiteral("GuidedSessionDialog"), m_guidedSessionDialog.data(), true);
    capturePersistentDialogState(QStringLiteral("ManualDialog"), m_manualDialog.data(), true);
    capturePersistentDialogState(QStringLiteral("AboutDialog"), m_aboutDialog.data(), true);
    capturePersistentDialogState(QStringLiteral("MaterialCatalogDialog"), m_materialCatalogDialog.data(), false);
    capturePersistentDialogState(QStringLiteral("SectionCatalogDialog"), m_sectionCatalogDialog.data(), false);
    capturePersistentDialogState(QStringLiteral("RenderSettingsDialog"), m_renderSettingsDialog.data(), false);
    capturePersistentDialogState(QStringLiteral("StartupSessionDialog"), m_startupSessionDialog.data(), false);
}

void MainWindow::saveUserPreferences() const {
    if (m_prefsSaveTimer != nullptr) {
        m_prefsSaveTimer->start(500);
        return;
    }
    flushUserPreferencesSync();
}

void MainWindow::flushUserPreferencesSync() const {
    if (m_prefsSaveTimer != nullptr && m_prefsSaveTimer->isActive()) {
        m_prefsSaveTimer->stop();
    }
    const QJsonObject resultsPanelObj = serializeResultsPanelJson();

    captureAllOpenDialogsState();

    QJsonObject appObj;
    appObj.insert(QStringLiteral("theme"),
                  ThemeManager::instance().isLight() ? QStringLiteral("light") : QStringLiteral("dark"));
    appObj.insert(QStringLiteral("uiStyleMode"),
                  ThemeManager::instance().isNativeUi() ? QStringLiteral("native") : QStringLiteral("custom"));
    appObj.insert(QStringLiteral("language"),
                  locale::GerenciadorTraducao::instance().codigoIdiomaAtual());
    appObj.insert(QStringLiteral("lastDarkPlaneTheme"), static_cast<int>(m_lastDarkPlaneTheme));
    appObj.insert(QStringLiteral("lastFileDialogDir"), m_lastFileDialogDir);
    appObj.insert(QStringLiteral("startupSessionBehavior"),
                  io::startupSessionBehaviorToKey(m_startupSessionBehavior));
    appObj.insert(QStringLiteral("confirmUnsavedOnClose"), m_confirmUnsavedOnClose);

    const RenderQualitySettings activeRender =
        m_viewport ? m_viewport->renderQualitySettings() : loadRenderSettings();
    const QJsonObject graphicsObj = renderSettingsToJson(activeRender);

    const int persistedInspWidth =
        (!m_inspectorCollapsed && m_inspectorDock && !m_inspectorDock->isFloating() &&
         m_inspectorDock->isVisible() && m_inspectorDock->width() >= 200)
            ? qMax(320, m_inspectorDock->width())
            : m_inspectorSavedWidth;
    const int persistedInspFloatH =
        (!m_inspectorCollapsed && m_inspectorDock && m_inspectorDock->isFloating() &&
         m_inspectorDock->isVisible() && m_inspectorDock->height() >= 120)
            ? qMax(260, m_inspectorDock->height())
            : m_inspectorSavedFloatingHeight;

    QJsonObject inspDockObj;
    inspDockObj.insert(QStringLiteral("collapsed"), m_inspectorCollapsed);
    inspDockObj.insert(QStringLiteral("savedWidth"), persistedInspWidth);
    inspDockObj.insert(QStringLiteral("savedFloatingHeight"), persistedInspFloatH);
    inspDockObj.insert(QStringLiteral("floating"), m_inspectorDock ? m_inspectorDock->isFloating() : false);

    const int persistedResDockedH =
        (!m_resultsCollapsed && m_resultsDock && !m_resultsDock->isFloating() &&
         m_resultsDock->isVisible() && m_resultsDock->height() >= 100)
            ? qMax(140, m_resultsDock->height())
            : m_resultsSavedDockedHeight;
    const int persistedResFloatH =
        (!m_resultsCollapsed && m_resultsDock && m_resultsDock->isFloating() &&
         m_resultsDock->isVisible() && m_resultsDock->height() >= 120)
            ? qMax(180, m_resultsDock->height())
            : m_resultsSavedFloatingHeight;

    QJsonObject resDockObj;
    resDockObj.insert(QStringLiteral("collapsed"), m_resultsCollapsed);
    resDockObj.insert(QStringLiteral("savedDockedHeight"), persistedResDockedH);
    resDockObj.insert(QStringLiteral("savedFloatingHeight"), persistedResFloatH);
    resDockObj.insert(QStringLiteral("floating"), m_resultsDock ? m_resultsDock->isFloating() : false);

    const bool persistedMaximized = isVisible() ? isMaximized() : m_preferredStartMaximized;
    const QSize normalSz =
        (persistedMaximized && normalGeometry().width() >= 640 && normalGeometry().height() >= 480)
            ? normalGeometry().size()
            : size();

    QJsonObject windowObj;
    windowObj.insert(QStringLiteral("width"), normalSz.width());
    windowObj.insert(QStringLiteral("height"), normalSz.height());
    windowObj.insert(QStringLiteral("maximized"), persistedMaximized);
    windowObj.insert(QStringLiteral("inspectorDock"), inspDockObj);
    windowObj.insert(QStringLiteral("resultsDock"), resDockObj);

    QJsonObject quickObj;
    quickObj.insert(QStringLiteral("x"), m_quickXSpin ? m_quickXSpin->value() : 0.0);
    quickObj.insert(QStringLiteral("y"), m_quickYSpin ? m_quickYSpin->value() : 0.0);
    quickObj.insert(QStringLiteral("z"), m_quickZSpin ? m_quickZSpin->value() : 0.0);

    QJsonObject post3dObj;
    post3dObj.insert(QStringLiteral("contourMode"),
                     m_viewport ? static_cast<int>(m_viewport->postContourMode()) : 0);
    post3dObj.insert(QStringLiteral("showSurfaceMesh"),
                     m_viewport ? m_viewport->showSurfaceMesh() : true);
    post3dObj.insert(QStringLiteral("clipAxis"),
                     m_viewport ? static_cast<int>(m_viewport->clipAxis()) : 0);
    post3dObj.insert(QStringLiteral("clipOffset"),
                     m_viewport ? m_viewport->clipOffset() : 0.0);
    post3dObj.insert(QStringLiteral("clipInverted"),
                     m_viewport ? m_viewport->clipInverted() : false);
    post3dObj.insert(QStringLiteral("animationSpeed"),
                     m_viewport ? m_viewport->animationSpeed() : 1.0);
    post3dObj.insert(QStringLiteral("animationAmplitude"),
                     m_viewport ? m_viewport->animationAmplitude() : 1.0);

    QJsonObject workspaceObj;
    workspaceObj.insert(QStringLiteral("autoRecalculate"), m_autoRecalculate);
    workspaceObj.insert(QStringLiteral("interactionMode"),
                        m_viewport ? static_cast<int>(m_viewport->interactionMode()) : 0);
    workspaceObj.insert(QStringLiteral("workPlane"),
                        m_viewport ? static_cast<int>(m_viewport->workPlane())
                                   : static_cast<int>(WorkPlane::XY_Top));
    workspaceObj.insert(QStringLiteral("planeOffset"),
                        m_viewport ? m_viewport->planeOffset() : 0.0);
    workspaceObj.insert(QStringLiteral("snapToGrid"),
                        m_viewport ? m_viewport->snapToGrid() : true);
    workspaceObj.insert(QStringLiteral("gridStep"),
                        m_viewport ? m_viewport->gridStep() : 0.5);
    workspaceObj.insert(QStringLiteral("showLocalAxes"),
                        m_viewport ? m_viewport->showLocalAxes() : false);
    workspaceObj.insert(QStringLiteral("showReactions"),
                        m_viewport ? m_viewport->showReactions() : true);
    workspaceObj.insert(QStringLiteral("activeDiagram"),
                        m_viewport ? static_cast<int>(m_viewport->activeDiagram()) : 0);
    workspaceObj.insert(QStringLiteral("scaleSliderValue"),
                        m_scaleSlider ? m_scaleSlider->value() : 30);
    workspaceObj.insert(QStringLiteral("quickNodeInput"), quickObj);
    workspaceObj.insert(QStringLiteral("postProcessing3D"), post3dObj);

    QJsonObject guidedSessionObj = io::UserPreferencesStore::readSection(QStringLiteral("guidedSession"));
    if (m_guidedSessionDialog) {
        guidedSessionObj.insert(
            QStringLiteral("stepHighlightColor"),
            m_guidedSessionDialog->stepHighlightColor().name(QColor::HexRgb).toUpper());
        QJsonArray phaseColorsArr;
        const int phaseCount = m_guidedSessionDialog->maxPhasesCount();
        for (int p = 1; p <= phaseCount; ++p) {
            phaseColorsArr.append(m_guidedSessionDialog->phaseHighlightColor(p).name(QColor::HexRgb).toUpper());
        }
        guidedSessionObj.insert(QStringLiteral("phaseHighlightColors"), phaseColorsArr);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), io::UserPreferencesStore::kCurrentSchemaVersion);
    root.insert(QStringLiteral("app"), appObj);
    root.insert(QStringLiteral("graphics"), graphicsObj);
    root.insert(QStringLiteral("window"), windowObj);
    root.insert(QStringLiteral("workspace"), workspaceObj);
    root.insert(QStringLiteral("dialogs"), m_savedDialogsStateJson);
    root.insert(QStringLiteral("resultsPanel"), resultsPanelObj);
    if (!guidedSessionObj.isEmpty()) {
        root.insert(QStringLiteral("guidedSession"), guidedSessionObj);
    }

    io::UserPreferencesStore::writeRootObject(root);
}

bool MainWindow::loadUserPreferences() {
    bool loadedOk = false;
    const QJsonObject root = io::UserPreferencesStore::readRootObject(&loadedOk);
    if (!loadedOk || root.isEmpty()) {
        return false;
    }

    const ScopedBoolFlag guard(m_rebuildingOrLoadingResultsSettings);

    if (const QJsonObject resultsObj = root.value(QStringLiteral("resultsPanel")).toObject();
        !resultsObj.isEmpty()) {
        deserializeResultsPanelJson(resultsObj);
    }

    if (const QJsonObject graphicsObj = root.value(QStringLiteral("graphics")).toObject();
        !graphicsObj.isEmpty()) {
        const RenderQualitySettings loadedRender = renderSettingsFromJson(graphicsObj);
        m_renderSettingsSnapshot = loadedRender;
        if (!QOpenGLContext::globalShareContext()) {
            QSurfaceFormat::setDefaultFormat(buildPreferredSurfaceFormat(loadedRender));
        }
        if (m_viewport) {
            m_viewport->setRenderQualitySettings(loadedRender);
        }
        if (loadedRender.planeTheme != ActivePlaneTheme::CleanLight) {
            m_lastDarkPlaneTheme = loadedRender.planeTheme;
        }
        syncRenderSettingsMenuState();
        if (m_renderSettingsDialog && m_viewport) {
            m_renderSettingsDialog->syncSettings(loadedRender, m_viewport->graphicsCapabilities());
        }
    }

    if (const QJsonObject appObj = root.value(QStringLiteral("app")).toObject(); !appObj.isEmpty()) {
        const QString prevLang = locale::GerenciadorTraducao::instance().codigoIdiomaAtual();
        if (locale::GerenciadorTraducao::instance().reloadSavedLanguage()) {
            if (prevLang != locale::GerenciadorTraducao::instance().codigoIdiomaAtual()) {
                retranslateUi();
            }
        }
        (void)ThemeManager::instance().reloadSavedTheme();
        syncThemeMenuState();
        syncUiStyleMenuState();

        const int darkPlaneInt = appObj.value(QStringLiteral("lastDarkPlaneTheme"))
                                     .toInt(static_cast<int>(m_lastDarkPlaneTheme));
        if (darkPlaneInt >= static_cast<int>(ActivePlaneTheme::ObsidianDark) &&
            darkPlaneInt <= static_cast<int>(ActivePlaneTheme::GraphiteDark)) {
            m_lastDarkPlaneTheme = static_cast<ActivePlaneTheme>(darkPlaneInt);
        }
        if (appObj.contains(QStringLiteral("lastFileDialogDir"))) {
            m_lastFileDialogDir = appObj.value(QStringLiteral("lastFileDialogDir")).toString();
        }
        const QJsonValue startupVal = appObj.value(QStringLiteral("startupSessionBehavior"));
        if (startupVal.isString()) {
            m_startupSessionBehavior = io::startupSessionBehaviorFromKey(startupVal.toString(), 0);
        } else if (startupVal.isDouble()) {
            m_startupSessionBehavior = io::startupSessionBehaviorFromKey(QString(), startupVal.toInt(0));
        } else {
            m_startupSessionBehavior = io::StartupSessionBehavior::AskOnStartup;
        }
        m_confirmUnsavedOnClose = appObj.value(QStringLiteral("confirmUnsavedOnClose")).toBool(true);
        syncStartupSessionMenuState();
        if (m_startupSessionDialog) {
            m_startupSessionDialog->syncSettings(m_startupSessionBehavior, m_confirmUnsavedOnClose);
        }
    }

    if (const QJsonObject wsObj = root.value(QStringLiteral("workspace")).toObject(); !wsObj.isEmpty()) {
        m_autoRecalculate = wsObj.value(QStringLiteral("autoRecalculate")).toBool(true);
        if (m_actAutoRecalc) {
            const QSignalBlocker b(m_actAutoRecalc);
            m_actAutoRecalc->setChecked(m_autoRecalculate);
        }

        if (m_viewport) {
            const int modeInt = std::clamp(
                wsObj.value(QStringLiteral("interactionMode")).toInt(static_cast<int>(InteractionMode::Select)),
                static_cast<int>(InteractionMode::Select),
                static_cast<int>(InteractionMode::Delete));
            const auto mode = static_cast<InteractionMode>(modeInt);
            m_viewport->setInteractionMode(mode);
            auto checkModeAct = [](QAction* act, bool on) {
                if (act) {
                    const QSignalBlocker b(act);
                    act->setChecked(on);
                }
            };
            checkModeAct(m_actSelect, mode == InteractionMode::Select);
            checkModeAct(m_actAddNode, mode == InteractionMode::AddNode);
            checkModeAct(m_actAddElement, mode == InteractionMode::AddElement);
            checkModeAct(m_actMeasure, mode == InteractionMode::Measure);
            checkModeAct(m_actDelete, mode == InteractionMode::Delete);

            const int wpInt = std::clamp(
                wsObj.value(QStringLiteral("workPlane")).toInt(static_cast<int>(WorkPlane::XY_Top)),
                0, 2);
            const auto wp = static_cast<WorkPlane>(wpInt);
            m_viewport->setWorkPlane(wp);
            if (m_planeCombo) {
                const QSignalBlocker b(m_planeCombo);
                const int idx = m_planeCombo->findData(wpInt);
                if (idx >= 0) {
                    m_planeCombo->setCurrentIndex(idx);
                }
            }

            const double planeOffset = std::clamp(wsObj.value(QStringLiteral("planeOffset")).toDouble(0.0),
                                                  -1000.0, 1000.0);
            m_viewport->setPlaneOffset(planeOffset);
            if (m_planeOffsetSpin) {
                const QSignalBlocker b(m_planeOffsetSpin);
                m_planeOffsetSpin->setValue(planeOffset);
            }

            const bool snap = wsObj.value(QStringLiteral("snapToGrid")).toBool(true);
            m_viewport->setSnapToGrid(snap);
            if (m_actSnap) {
                const QSignalBlocker b(m_actSnap);
                m_actSnap->setChecked(snap);
            }

            const double gridStep = std::clamp(wsObj.value(QStringLiteral("gridStep")).toDouble(0.5), 0.05, 50.0);
            m_viewport->setGridStep(gridStep);
            if (m_gridStepSpin) {
                const QSignalBlocker b(m_gridStepSpin);
                m_gridStepSpin->setValue(gridStep);
            }

            const bool showLocalAxes = wsObj.value(QStringLiteral("showLocalAxes")).toBool(false);
            m_viewport->setShowLocalAxes(showLocalAxes);
            if (m_actLocalAxes) {
                const QSignalBlocker b(m_actLocalAxes);
                m_actLocalAxes->setChecked(showLocalAxes);
            }

            const bool showReactions = wsObj.value(QStringLiteral("showReactions")).toBool(true);
            m_viewport->setShowReactions(showReactions);
            if (m_actReactions) {
                const QSignalBlocker b(m_actReactions);
                m_actReactions->setChecked(showReactions);
            }

            const int scaleVal = std::clamp(wsObj.value(QStringLiteral("scaleSliderValue")).toInt(30), 1, 100);
            if (m_scaleSlider) {
                const QSignalBlocker b(m_scaleSlider);
                m_scaleSlider->setValue(scaleVal);
            }
            m_viewport->setDiagramScale(scaleVal / 30.0);
            m_viewport->setDeformedScale(scaleVal * 1.5);

            if (wsObj.contains(QStringLiteral("activeDiagram"))) {
                const int diagInt = std::clamp(wsObj.value(QStringLiteral("activeDiagram")).toInt(0), 0, 7);
                const auto diag = static_cast<DiagramType>(diagInt);
                m_viewport->setActiveDiagram(diag);
                if (m_diagramCombo) {
                    const QSignalBlocker b(m_diagramCombo);
                    const int idx = m_diagramCombo->findData(diagInt);
                    if (idx >= 0) {
                        m_diagramCombo->setCurrentIndex(idx);
                    }
                }
                m_hasLoadedCustomDiagram = true;
            }

            if (const QJsonObject ppObj = wsObj.value(QStringLiteral("postProcessing3D")).toObject();
                !ppObj.isEmpty()) {
                const int cmInt = std::clamp(ppObj.value(QStringLiteral("contourMode")).toInt(0), 0, 3);
                const auto cm = static_cast<Viewport3D::PostContourMode>(cmInt);
                if (cm == Viewport3D::PostContourMode::NbrComplianceDcr) {
                    m_viewport->setElementDcrMap(computeModelDcrMap());
                }
                m_viewport->setPostContourMode(cm);
                m_viewport->setShowSurfaceMesh(ppObj.value(QStringLiteral("showSurfaceMesh")).toBool(true));

                const int clipAxisInt = std::clamp(ppObj.value(QStringLiteral("clipAxis")).toInt(0), 0, 3);
                const double clipOffset = ppObj.value(QStringLiteral("clipOffset")).toDouble(0.0);
                const bool clipInv = ppObj.value(QStringLiteral("clipInverted")).toBool(false);
                m_viewport->setClipPlane(static_cast<Viewport3D::ClipAxis>(clipAxisInt), clipOffset, clipInv);

                const double animSpeed = std::clamp(ppObj.value(QStringLiteral("animationSpeed")).toDouble(1.0), 0.2, 5.0);
                const double animAmp = std::clamp(ppObj.value(QStringLiteral("animationAmplitude")).toDouble(1.0), 0.1, 10.0);
                m_viewport->setAnimationSpeed(animSpeed);
                m_viewport->setAnimationAmplitude(animAmp);

                if (m_postProcessControlDialog) {
                    m_postProcessControlDialog->syncFromViewport();
                }
            }
        }

        if (const QJsonObject quickObj = wsObj.value(QStringLiteral("quickNodeInput")).toObject();
            !quickObj.isEmpty()) {
            if (m_quickXSpin) {
                const QSignalBlocker b(m_quickXSpin);
                m_quickXSpin->setValue(quickObj.value(QStringLiteral("x")).toDouble(0.0));
            }
            if (m_quickYSpin) {
                const QSignalBlocker b(m_quickYSpin);
                m_quickYSpin->setValue(quickObj.value(QStringLiteral("y")).toDouble(0.0));
            }
            if (m_quickZSpin) {
                const QSignalBlocker b(m_quickZSpin);
                m_quickZSpin->setValue(quickObj.value(QStringLiteral("z")).toDouble(0.0));
            }
        }
    }

    if (const QJsonObject winObj = root.value(QStringLiteral("window")).toObject(); !winObj.isEmpty()) {
        m_preferredStartMaximized = winObj.value(QStringLiteral("maximized")).toBool(true);
        if (!m_preferredStartMaximized) {
            const int winW = winObj.value(QStringLiteral("width")).toInt(0);
            const int winH = winObj.value(QStringLiteral("height")).toInt(0);
            if (winW >= 640 && winH >= 480) {
                QSize targetSize(winW, winH);
                if (const QScreen* scr = screen() ? screen() : QGuiApplication::primaryScreen()) {
                    const QRect avail = scr->availableGeometry();
                    if (avail.isValid()) {
                        targetSize.setWidth(std::min(targetSize.width(), avail.width()));
                        targetSize.setHeight(std::min(targetSize.height(), avail.height()));
                    }
                }
                resize(targetSize);
            }
        }

        if (const QJsonObject inspObj = winObj.value(QStringLiteral("inspectorDock")).toObject();
            !inspObj.isEmpty()) {
            m_inspectorSavedWidth = std::max(320, inspObj.value(QStringLiteral("savedWidth")).toInt(320));
            m_inspectorSavedFloatingHeight = std::max(260, inspObj.value(QStringLiteral("savedFloatingHeight")).toInt(520));
            const bool inspFloating = inspObj.value(QStringLiteral("floating")).toBool(false);
            if (m_inspectorDock && m_inspectorDock->isFloating() != inspFloating) {
                m_inspectorDock->setFloating(inspFloating);
            }
            const bool inspCollapsed = inspObj.value(QStringLiteral("collapsed")).toBool(false);
            if (m_inspectorCollapsed != inspCollapsed) {
                setInspectorDockCollapsed(inspCollapsed);
            } else if (!inspCollapsed && m_inspectorDock && !m_inspectorDock->isFloating()) {
                resizeDocks({m_inspectorDock}, {m_inspectorSavedWidth}, Qt::Horizontal);
                m_inspectorDock->setMaximumWidth(QWIDGETSIZE_MAX);
                scheduleDockSeparatorRepaint(this, m_resultsSplitter);
            }
        }
        if (const QJsonObject resObj = winObj.value(QStringLiteral("resultsDock")).toObject();
            !resObj.isEmpty()) {
            m_resultsSavedDockedHeight = std::max(140, resObj.value(QStringLiteral("savedDockedHeight")).toInt(200));
            m_resultsSavedFloatingHeight = std::max(180, resObj.value(QStringLiteral("savedFloatingHeight")).toInt(240));
            const bool resFloating = resObj.value(QStringLiteral("floating")).toBool(false);
            if (m_resultsDock && m_resultsDock->isFloating() != resFloating) {
                m_resultsDock->setFloating(resFloating);
            }
            const bool resCollapsed = resObj.value(QStringLiteral("collapsed")).toBool(false);
            if (m_resultsCollapsed != resCollapsed) {
                setResultsDockCollapsed(resCollapsed);
            } else if (!resCollapsed && m_resultsDock && !m_resultsDock->isFloating()) {
                resizeDocks({m_resultsDock}, {m_resultsSavedDockedHeight}, Qt::Vertical);
                m_resultsDock->setMaximumHeight(QWIDGETSIZE_MAX);
                scheduleDockSeparatorRepaint(this, m_resultsSplitter);
            }
        }
    }

    m_savedDialogsStateJson = root.value(QStringLiteral("dialogs")).toObject();
    if (m_loadCombinationsDialog) restorePersistentDialogState(QStringLiteral("LoadCombinationsDialog"), m_loadCombinationsDialog.data(), true);
    if (m_meshGeneratorDialog) restorePersistentDialogState(QStringLiteral("MeshGeneratorDialog"), m_meshGeneratorDialog.data(), true);
    if (m_analysisSuiteDialog) restorePersistentDialogState(QStringLiteral("AnalysisSuiteDialog"), m_analysisSuiteDialog.data(), true);
    if (m_postProcessControlDialog) restorePersistentDialogState(QStringLiteral("PostProcessControlDialog"), m_postProcessControlDialog.data(), false);
    if (m_nbrComplianceDialog) restorePersistentDialogState(QStringLiteral("NbrComplianceDialog"), m_nbrComplianceDialog.data(), true);
    if (m_pdfOptionsDialog) restorePersistentDialogState(QStringLiteral("PdfReportOptionsDialog"), m_pdfOptionsDialog.data(), true);
    if (m_guidedSessionDialog) {
        restorePersistentDialogState(QStringLiteral("GuidedSessionDialog"), m_guidedSessionDialog.data(), true);
        const QJsonObject guidedObj = root.value(QStringLiteral("guidedSession")).toObject();
        if (guidedObj.contains(QStringLiteral("phaseHighlightColors"))) {
            const QJsonArray arr = guidedObj.value(QStringLiteral("phaseHighlightColors")).toArray();
            for (int i = 0; i < arr.size(); ++i) {
                const QColor c(arr.at(i).toString().trimmed());
                if (c.isValid()) {
                    m_guidedSessionDialog->setPhaseHighlightColor(i + 1, c);
                }
            }
        } else if (guidedObj.contains(QStringLiteral("stepHighlightColor"))) {
            const QColor savedColor(guidedObj.value(QStringLiteral("stepHighlightColor")).toString().trimmed());
            if (savedColor.isValid()) {
                m_guidedSessionDialog->setStepHighlightColor(savedColor);
            }
        }
    }
    if (m_manualDialog) restorePersistentDialogState(QStringLiteral("ManualDialog"), m_manualDialog.data(), true);
    if (m_aboutDialog) restorePersistentDialogState(QStringLiteral("AboutDialog"), m_aboutDialog.data(), true);
    if (m_materialCatalogDialog) restorePersistentDialogState(QStringLiteral("MaterialCatalogDialog"), m_materialCatalogDialog.data(), false);
    if (m_sectionCatalogDialog) restorePersistentDialogState(QStringLiteral("SectionCatalogDialog"), m_sectionCatalogDialog.data(), false);
    if (m_renderSettingsDialog) restorePersistentDialogState(QStringLiteral("RenderSettingsDialog"), m_renderSettingsDialog.data(), false);
    if (m_startupSessionDialog) restorePersistentDialogState(QStringLiteral("StartupSessionDialog"), m_startupSessionDialog.data(), false);

    if (m_viewport) {
        m_viewport->update();
    }
    return true;
}

void MainWindow::restoreAllDefaultSettings() {
    {
        const ScopedBoolFlag guard(m_rebuildingOrLoadingResultsSettings);

        m_autoRecalculate = true;
        if (m_actAutoRecalc) {
            const QSignalBlocker b(m_actAutoRecalc);
            m_actAutoRecalc->setChecked(true);
        }
        m_hasLoadedCustomDiagram = false;

        if (m_actSelect) {
            const QSignalBlocker b(m_actSelect);
            m_actSelect->setChecked(true);
        }

        if (m_viewport) {
            m_viewport->setInteractionMode(InteractionMode::Select);
            applyDefaultExampleWorkPlane();

            if (m_actSnap) {
                const QSignalBlocker b(m_actSnap);
                m_actSnap->setChecked(true);
            }
            m_viewport->setSnapToGrid(true);

            if (m_gridStepSpin) {
                const QSignalBlocker b(m_gridStepSpin);
                m_gridStepSpin->setValue(0.5);
            }
            m_viewport->setGridStep(0.5);

            if (m_actLocalAxes) {
                const QSignalBlocker b(m_actLocalAxes);
                m_actLocalAxes->setChecked(false);
            }
            m_viewport->setShowLocalAxes(false);

            if (m_actReactions) {
                const QSignalBlocker b(m_actReactions);
                m_actReactions->setChecked(true);
            }
            m_viewport->setShowReactions(true);

            const DiagramType defDiag = m_lastSolverResult.success ? DiagramType::Deformed : DiagramType::None;
            if (m_diagramCombo) {
                const QSignalBlocker b(m_diagramCombo);
                const int idx = m_diagramCombo->findData(static_cast<int>(defDiag));
                if (idx >= 0) {
                    m_diagramCombo->setCurrentIndex(idx);
                }
            }
            m_viewport->setActiveDiagram(defDiag);

            if (m_scaleSlider) {
                const QSignalBlocker b(m_scaleSlider);
                m_scaleSlider->setValue(30);
            }
            m_viewport->setDiagramScale(1.0);
            m_viewport->setDeformedScale(45.0);

            m_viewport->clearElementDcrMap();
            m_viewport->setPostContourMode(Viewport3D::PostContourMode::None);
            m_viewport->setShowSurfaceMesh(true);
            m_viewport->setClipPlane(Viewport3D::ClipAxis::None, 0.0, false);
            m_viewport->clearPostAnimation();
            m_viewport->setAnimationSpeed(1.0);
            m_viewport->setAnimationAmplitude(1.0);
            if (m_postProcessControlDialog) {
                m_postProcessControlDialog->syncFromViewport();
            }
        }

        if (m_quickXSpin) {
            const QSignalBlocker b(m_quickXSpin);
            m_quickXSpin->setValue(0.0);
        }
        if (m_quickYSpin) {
            const QSignalBlocker b(m_quickYSpin);
            m_quickYSpin->setValue(0.0);
        }
        if (m_quickZSpin) {
            const QSignalBlocker b(m_quickZSpin);
            m_quickZSpin->setValue(0.0);
        }

        m_inspectorSavedWidth = 320;
        m_inspectorSavedFloatingHeight = 520;
        m_resultsSavedDockedHeight = 200;
        m_resultsSavedFloatingHeight = 240;
        m_preferredStartMaximized = true;
        m_lastDarkPlaneTheme = ActivePlaneTheme::ObsidianDark;
        if (m_inspectorCollapsed) {
            setInspectorDockCollapsed(false);
        } else if (m_inspectorDock && !m_inspectorDock->isFloating()) {
            resizeDocks({m_inspectorDock}, {m_inspectorSavedWidth}, Qt::Horizontal);
            m_inspectorDock->setMaximumWidth(QWIDGETSIZE_MAX);
            scheduleDockSeparatorRepaint(this, m_resultsSplitter);
        }
        if (m_resultsCollapsed) {
            setResultsDockCollapsed(false);
        } else if (m_resultsDock && !m_resultsDock->isFloating()) {
            resizeDocks({m_resultsDock}, {m_resultsSavedDockedHeight}, Qt::Vertical);
            m_resultsDock->setMaximumHeight(QWIDGETSIZE_MAX);
            scheduleDockSeparatorRepaint(this, m_resultsSplitter);
        }

        m_savedDialogsStateJson = QJsonObject{};
        if (m_guidedSessionDialog) {
            m_guidedSessionDialog->resetAllPhaseHighlightColors();
        }
        io::UserPreferencesStore::updateSection(QStringLiteral("guidedSession"), QJsonObject{});
        m_lastFileDialogDir.clear();
        m_startupSessionBehavior = io::StartupSessionBehavior::AskOnStartup;
        m_confirmUnsavedOnClose = true;
        syncStartupSessionMenuState();
        if (m_startupSessionDialog) {
            m_startupSessionDialog->syncSettings(m_startupSessionBehavior, m_confirmUnsavedOnClose);
        }

        resetResultsLayoutsToDefault();
        if (m_tableConfigurator) {
            for (QTableView* tbl : {m_dispTable, m_reacTable, m_forcesTable, m_continuumTable}) {
                if (tbl != nullptr) {
                    m_tableConfigurator->resetAllDefaults(tbl);
                }
            }
        }

        ThemeManager::instance().setUiStyleMode(UiStyleMode::Custom);
        onRestoreDefaultRenderSettings();
    }

    flushUserPreferencesSync();
    if (m_viewport) {
        m_viewport->update();
    }
}

void MainWindow::onSaveAllSettingsNow() {
    flushUserPreferencesSync();
    showStatusMessage(locale::GerenciadorTraducao::trText(
        "Todas as configurações e preferências foram salvas em AppData/Local/OmniFrame/user_preferences.json."));
}

void MainWindow::onReloadSavedSettings() {
    loadUserPreferences();
    showStatusMessage(locale::GerenciadorTraducao::trText(
        "Configurações e preferências recarregadas com sucesso."));
}

void MainWindow::onRestoreAllDefaultSettings() {
    restoreAllDefaultSettings();
    showStatusMessage(locale::GerenciadorTraducao::trText(
        "Todas as configurações e preferências foram restauradas para o padrão."));
}

void MainWindow::setResultsLayoutCount(int count) {
    const int clamped = std::clamp(count, 1, 4);
    if (m_resultsLayoutCount == clamped && m_resultsSplitter != nullptr) {
        syncResultsLayoutsMenuState();
        return;
    }
    const int oldModeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
    if (m_resultsSplitter != nullptr && m_resultsSplitter->isVisible()) {
        const QList<int> prevSizes = m_resultsSplitter->sizes();
        if (prevSizes.size() == m_resultsLayoutCount) {
            int total = 0;
            for (int s : prevSizes) {
                total += s;
            }
            if (total > 0) {
                m_resultsSplitterSizesByMode[static_cast<std::size_t>(oldModeIdx)] = prevSizes;
            }
        }
    }
    m_resultsLayoutCount = clamped;
    rebuildResultsLayoutsUi();
    if (m_resultsSplitter != nullptr) {
        const int newModeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
        QList<int> sizes = m_resultsSplitterSizesByMode[static_cast<std::size_t>(newModeIdx)];
        if (sizes.size() != m_resultsSplitter->count()) {
            sizes.clear();
            sizes.reserve(m_resultsSplitter->count());
            for (int i = 0; i < m_resultsSplitter->count(); ++i) {
                sizes.append(1000);
            }
            m_resultsSplitterSizesByMode[static_cast<std::size_t>(newModeIdx)] = sizes;
        }
        m_resultsSplitter->setSizes(sizes);
    }
    syncResultsLayoutsMenuState();
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::setResultsLayoutTableVisible(int layoutIndex, ui::ResultTableKind kind, bool visible) {
    if (layoutIndex < 0 || layoutIndex >= 4) {
        return;
    }
    const int k = static_cast<int>(kind);
    if (k < 0 || k >= 4) {
        return;
    }
    const int modeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
    if (m_resultsModeTableAssign[static_cast<std::size_t>(modeIdx)][static_cast<std::size_t>(layoutIndex)][static_cast<std::size_t>(k)] == visible) {
        return;
    }
    m_resultsModeTableAssign[static_cast<std::size_t>(modeIdx)][static_cast<std::size_t>(layoutIndex)][static_cast<std::size_t>(k)] = visible;
    rebuildResultsLayoutsUi();
    syncResultsLayoutsMenuState();
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

bool MainWindow::isResultsLayoutTableVisible(int layoutIndex, ui::ResultTableKind kind) const {
    if (layoutIndex < 0 || layoutIndex >= 4) {
        return false;
    }
    const int k = static_cast<int>(kind);
    if (k < 0 || k >= 4) {
        return false;
    }
    const int modeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
    return m_resultsModeTableAssign[static_cast<std::size_t>(modeIdx)][static_cast<std::size_t>(layoutIndex)][static_cast<std::size_t>(k)];
}

void MainWindow::resetResultsLayoutsToDefault() {
    initDefaultResultsLayoutAssignments();
    m_resultsLayoutCount = 1;
    rebuildResultsLayoutsUi();
    if (m_resultsSplitter != nullptr) {
        m_resultsSplitter->setSizes({1000});
    }
    syncResultsLayoutsMenuState();
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

QTabWidget* MainWindow::resultsLayoutTabWidget(int layoutIndex) const {
    if (layoutIndex < 0 || layoutIndex >= 4) {
        return nullptr;
    }
    return m_resultsLayoutTabWidgets[static_cast<std::size_t>(layoutIndex)];
}

void MainWindow::rebuildResultsLayoutsUi() {
    if (!m_resultsSplitter || !m_hiddenTablesHost) {
        return;
    }

    const bool prevGuard = m_rebuildingOrLoadingResultsSettings;
    m_rebuildingOrLoadingResultsSettings = true;

    auto primaryForKind = [this](int k) -> QTableView* {
        switch (k) {
        case 0: return m_dispTable;
        case 1: return m_reacTable;
        case 2: return m_forcesTable;
        case 3: return m_continuumTable;
        default: return nullptr;
        }
    };

    // Detach inactive layout containers into hidden host and active tables safely
    for (int L = 0; L < 4; ++L) {
        if (QTabWidget* tabW = m_resultsLayoutTabWidgets[static_cast<std::size_t>(L)]) {
            while (tabW->count() > 0) {
                tabW->removeTab(0);
            }
            if (L >= m_resultsLayoutCount) {
                tabW->hide();
                tabW->setParent(m_hiddenTablesHost);
            }
        }
    }

    auto stashHiddenTable = [this](QTableView* tbl) {
        if (tbl != nullptr) {
            tbl->hide();
            tbl->setParent(m_hiddenTablesHost);
        }
    };
    for (int k = 0; k < 4; ++k) {
        stashHiddenTable(primaryForKind(k));
        for (int L = 0; L < 4; ++L) {
            stashHiddenTable(m_extraLayoutTables[static_cast<std::size_t>(L)][static_cast<std::size_t>(k)]);
        }
    }

    std::array<bool, 4> primaryPlaced{};
    const int modeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
    const bool isLight = ThemeManager::instance().isLight();

    for (int L = 0; L < m_resultsLayoutCount; ++L) {
        QTabWidget* tabW = m_resultsLayoutTabWidgets[static_cast<std::size_t>(L)];
        if (tabW == nullptr) {
            continue;
        }
        for (int k = 0; k < 4; ++k) {
            if (!m_resultsModeTableAssign[static_cast<std::size_t>(modeIdx)][static_cast<std::size_t>(L)][static_cast<std::size_t>(k)]) {
                continue;
            }
            const auto kind = static_cast<ui::ResultTableKind>(k);
            QTableView* tblToInsert = nullptr;
            if (!primaryPlaced[static_cast<std::size_t>(k)]) {
                primaryPlaced[static_cast<std::size_t>(k)] = true;
                tblToInsert = primaryForKind(k);
            } else {
                QTableView*& cloneRef = m_extraLayoutTables[static_cast<std::size_t>(L)][static_cast<std::size_t>(k)];
                if (cloneRef == nullptr) {
                    cloneRef = new QTableView(m_hiddenTablesHost);
                    cloneRef->setObjectName(QStringLiteral("extraResultsTable_L%1_K%2").arg(L).arg(k));
                    auto* proxy = new ui::ResultTableProxyModel(cloneRef);
                    proxy->setSourceModel(m_resultTableModels[static_cast<std::size_t>(k)]);
                    cloneRef->setModel(proxy);
                    if (m_tableConfigurator) {
                        m_tableConfigurator->configureTable(
                            cloneRef, kind, resultTableHeaders(kind), isLight);
                    }
                }
                tblToInsert = cloneRef;
            }
            if (tblToInsert != nullptr) {
                const QString tabTitle = resultTableTabTitle(kind);
                const int tabIdx = tabW->addTab(tblToInsert, tabTitle);
                tabW->setTabToolTip(tabIdx, tabTitle);
                tblToInsert->show();
            }
        }
        const int savedTab = m_resultsActiveTabByMode[static_cast<std::size_t>(modeIdx)][static_cast<std::size_t>(L)];
        if (savedTab >= 0 && savedTab < tabW->count()) {
            tabW->setCurrentIndex(savedTab);
        }
        if (tabW->parentWidget() != m_resultsSplitter) {
            m_resultsSplitter->insertWidget(L, tabW);
        }
        tabW->show();
    }

    syncExtraLayoutTables();
    m_rebuildingOrLoadingResultsSettings = prevGuard;
}

void MainWindow::syncResultsLayoutsMenuState() {
    for (int i = 0; i < 4; ++i) {
        if (QAction* actCnt = m_actResultsLayoutCount[static_cast<std::size_t>(i)]) {
            const QSignalBlocker b(actCnt);
            actCnt->setChecked((i + 1) == m_resultsLayoutCount);
        }
    }

    const int modeIdx = std::clamp(m_resultsLayoutCount - 1, 0, 3);
    for (int L = 0; L < 4; ++L) {
        if (QMenu* subMenu = m_resultsLayoutConfigMenus[static_cast<std::size_t>(L)]) {
            subMenu->setEnabled(L < m_resultsLayoutCount);
        }
        for (int k = 0; k < 4; ++k) {
            if (QAction* actTbl = m_actResultsLayoutTables[static_cast<std::size_t>(L)][static_cast<std::size_t>(k)]) {
                const QSignalBlocker b(actTbl);
                actTbl->setChecked(
                    m_resultsModeTableAssign[static_cast<std::size_t>(modeIdx)]
                                            [static_cast<std::size_t>(L)]
                                            [static_cast<std::size_t>(k)]);
            }
        }
    }
}

void MainWindow::syncExtraLayoutTables() {
    if (m_resultsLayoutCount <= 1) {
        return;
    }

    for (int L = 0; L < 4; ++L) {
        for (int k = 0; k < 4; ++k) {
            QTableView* dst = m_extraLayoutTables[static_cast<std::size_t>(L)][static_cast<std::size_t>(k)];
            if (dst == nullptr || dst->parentWidget() == m_hiddenTablesHost) {
                continue;
            }
            if (m_tableConfigurator) {
                m_tableConfigurator->afterTablePopulated(dst);
            }
        }
    }
    refreshReactionSummaryRowTheme();
}

void MainWindow::updateCursorCoordinates(double x, double y, double z) {
    m_cursorCoordLabel->setText(QString("X: %1 m | Y: %2 m | Z: %3 m")
                                .arg(x, 6, 'f', 2)
                                .arg(y, 6, 'f', 2)
                                .arg(z, 6, 'f', 2));
}

void MainWindow::showStatusMessage(const QString& msg) {
    io::AppLogger::info(QStringLiteral("Status"), msg);
    statusBar()->showMessage(msg, 5000);
}

void MainWindow::onManual() {
    openManagedInfoDialog(
        m_windowManager, m_manualDialog, true,
        []() { return new ui::ManualDialog(nullptr); },
        [this](ui::ManualDialog* dlg) {
            bindPersistentDialog(QStringLiteral("ManualDialog"), dlg, true);
        }
    );
}

GuidedSessionDialog* MainWindow::guidedSessionDialog() const {
    return m_guidedSessionDialog.data();
}

void MainWindow::onGuidedSession() {
    if (m_guidedSessionDialog) {
        m_windowManager->showAndRaise(m_guidedSessionDialog.data());
        return;
    }
    auto* dlg = new GuidedSessionDialog(this, nullptr);
    m_guidedSessionDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("GuidedSessionDialog"), dlg, true);
    connect(dlg, &GuidedSessionDialog::stepHighlightColorChanged, this, [this](const QColor&) {
        if (!m_rebuildingOrLoadingResultsSettings) {
            saveUserPreferences();
        }
    });
    dlg->positionHudNearMainWindow();
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onAbout() {
    openManagedInfoDialog(
        m_windowManager, m_aboutDialog, true,
        []() { return new AboutDialog(nullptr); },
        [this](AboutDialog* dlg) {
            bindPersistentDialog(QStringLiteral("AboutDialog"), dlg, true);
        }
    );
}

void MainWindow::onInstallWindowsCliIntegration() {
    using locale::GerenciadorTraducao;
    QString details;
    const bool ok = os::installWindowsCliIntegration(QString(), &details);
    const os::CliIntegrationStatus st = os::queryWindowsCliIntegration();
    if (ok) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            GerenciadorTraducao::trText("Integração CLI Windows Concluída"),
            GerenciadorTraducao::trText("O binário omniframe-cli.exe foi registrado no Windows 10/11 (HKCU Environment PATH, App Paths e extensão .omniframe).\n\nDiretório: %1\n\nNovos terminais CMD e PowerShell já podem executar 'omniframe-cli --help' diretamente.")
                .arg(st.installDirectory));
        showStatusMessage(GerenciadorTraducao::trText("CLI integrada ao PATH do Windows (CMD e PowerShell)."));
    } else {
        m_windowManager->showModelessMessage(
            QMessageBox::Warning,
            GerenciadorTraducao::trText("Falha na Integração CLI Windows"),
            GerenciadorTraducao::trText("Não foi possível concluir o registro completo da CLI no Windows.\n\nDetalhes: %1")
                .arg(details));
    }
}

void MainWindow::setUninstallWindowsCliIntegrationHandler(std::function<bool(const QString&, QString*)> handler) {
    m_uninstallCliHandler = std::move(handler);
}

void MainWindow::onUninstallWindowsCliIntegration() {
    using locale::GerenciadorTraducao;
    QString details;
    const bool ok = m_uninstallCliHandler
        ? m_uninstallCliHandler(QString(), &details)
        : os::uninstallWindowsCliIntegration(QString(), &details);
    if (ok) {
        m_windowManager->showModelessMessage(
            QMessageBox::Information,
            GerenciadorTraducao::trText("Integração CLI Removida"),
            GerenciadorTraducao::trText("As entradas de usuário (HKCU PATH, App Paths e associação .omniframe) deste diretório foram removidas com segurança.\n\n%1")
                .arg(details));
        showStatusMessage(GerenciadorTraducao::trText("Registro HKCU da CLI removido."));
    } else {
        m_windowManager->showModelessMessage(
            QMessageBox::Warning,
            GerenciadorTraducao::trText("Falha ao Remover Integração CLI"),
            GerenciadorTraducao::trText("Não foi possível remover integralmente as entradas da CLI no registro HKCU do Windows.\n\nDetalhes: %1")
                .arg(details));
        showStatusMessage(GerenciadorTraducao::trText("Falha ao remover registro HKCU da CLI."));
    }
}

void MainWindow::onCheckWindowsCliIntegration() {
    using locale::GerenciadorTraducao;
    const os::CliIntegrationStatus st = os::queryWindowsCliIntegration();
    const QString yesStr = GerenciadorTraducao::trText("Sim");
    const QString noStr = GerenciadorTraducao::trText("Não");
    const QString summary = GerenciadorTraducao::trText(
        "Status da Integração omniframe-cli.exe no Windows 10/11:\n\n"
        "• Diretório ativo: %1\n"
        "• omniframe-cli.exe encontrado: %2\n"
        "• PATH do Usuário (HKCU): %3\n"
        "• PATH do Sistema (HKLM): %4\n"
        "• App Paths (omniframe-cli.exe): %5\n"
        "• Associação .omniframe: %6\n"
        "• Pronto para CMD e PowerShell: %7")
        .arg(st.installDirectory,
             st.cliBinaryExists ? yesStr : noStr,
             st.installDirInUserPath ? yesStr : noStr,
             st.installDirInSystemPath ? yesStr : noStr,
             st.appPathsCliRegistered ? yesStr : noStr,
             st.fileAssocRegistered ? yesStr : noStr,
             st.isReadyForCmdAndPowerShell() ? yesStr : noStr);

    m_windowManager->showModelessMessage(
        QMessageBox::Information,
        GerenciadorTraducao::trText("Status da CLI Windows (CMD / PowerShell)"),
        summary);
}

void MainWindow::onOpenCliTerminalCmd() {
    using locale::GerenciadorTraducao;
    QString err;
    if (!os::openInteractiveCliTerminal(false, QString(), QString(), &err)) {
        m_windowManager->showModelessMessage(
            QMessageBox::Warning,
            GerenciadorTraducao::trText("Erro ao Abrir CMD"),
            GerenciadorTraducao::trText("Não foi possível iniciar o Prompt de Comando com a CLI.\n\nDetalhes: %1").arg(err));
        return;
    }
    showStatusMessage(GerenciadorTraducao::trText("Prompt de Comando (CMD) iniciado com omniframe-cli ativo."));
}

void MainWindow::onOpenCliTerminalPowerShell() {
    using locale::GerenciadorTraducao;
    QString err;
    if (!os::openInteractiveCliTerminal(true, QString(), QString(), &err)) {
        m_windowManager->showModelessMessage(
            QMessageBox::Warning,
            GerenciadorTraducao::trText("Erro ao Abrir PowerShell"),
            GerenciadorTraducao::trText("Não foi possível iniciar o PowerShell com a CLI.\n\nDetalhes: %1").arg(err));
        return;
    }
    showStatusMessage(GerenciadorTraducao::trText("PowerShell iniciado com omniframe-cli ativo."));
}

void MainWindow::applyAndSaveRenderSettings(const RenderQualitySettings& settings) {
    if (settings.planeTheme != ActivePlaneTheme::CleanLight) {
        m_lastDarkPlaneTheme = settings.planeTheme;
    }
    if (!QOpenGLContext::globalShareContext()) {
        QSurfaceFormat::setDefaultFormat(buildPreferredSurfaceFormat(settings));
    }
    m_renderSettingsSnapshot = settings;
    if (m_viewport) {
        m_viewport->setRenderQualitySettings(settings);
    }
    syncRenderSettingsMenuState();
    if (m_renderSettingsDialog && m_viewport) {
        m_renderSettingsDialog->syncSettings(settings, m_viewport->graphicsCapabilities());
    }
    if (!m_rebuildingOrLoadingResultsSettings) {
        if (m_viewport) {
            saveUserPreferences();
        } else {
            saveRenderSettings(settings);
        }
    }
}

void MainWindow::syncRenderSettingsMenuState() {
    if (!m_viewport) return;
    const RenderQualitySettings& s = m_viewport->renderQualitySettings();

    auto setCheckedSilent = [](QAction* act, bool val) {
        if (!act) return;
        const bool old = act->blockSignals(true);
        act->setChecked(val);
        act->blockSignals(old);
    };

    if (m_presetActionGroup) {
        setCheckedSilent(m_actPresetAutoMax,  s.preset == QualityPreset::AutoMax);
        setCheckedSilent(m_actPresetUltra,    s.preset == QualityPreset::UltraQuality);
        setCheckedSilent(m_actPresetHigh,     s.preset == QualityPreset::HighQuality);
        setCheckedSilent(m_actPresetBalanced, s.preset == QualityPreset::Balanced);
        setCheckedSilent(m_actPresetPerf,     s.preset == QualityPreset::Performance);
        if (s.preset == QualityPreset::Custom && m_presetActionGroup->checkedAction()) {
            const bool old = m_presetActionGroup->blockSignals(true);
            m_presetActionGroup->checkedAction()->setChecked(false);
            m_presetActionGroup->blockSignals(old);
        }
    }

    auto nearEq = [](float a, float b) { return std::abs(a - b) < 0.03f; };
    setCheckedSilent(m_actScaleAuto, s.autoMaxResolution);
    setCheckedSilent(m_actScale75,   !s.autoMaxResolution && nearEq(s.resolutionScale, 0.75f));
    setCheckedSilent(m_actScale100,  !s.autoMaxResolution && nearEq(s.resolutionScale, 1.00f));
    setCheckedSilent(m_actScale125,  !s.autoMaxResolution && nearEq(s.resolutionScale, 1.25f));
    setCheckedSilent(m_actScale150,  !s.autoMaxResolution && nearEq(s.resolutionScale, 1.50f));
    setCheckedSilent(m_actScale200,  !s.autoMaxResolution && nearEq(s.resolutionScale, 2.00f));

    setCheckedSilent(m_actMsaaAuto, s.autoMsaa);
    setCheckedSilent(m_actMsaaOff,  !s.autoMsaa && s.requestedMsaa == 0);
    setCheckedSilent(m_actMsaa2x,   !s.autoMsaa && s.requestedMsaa == 2);
    setCheckedSilent(m_actMsaa4x,   !s.autoMsaa && s.requestedMsaa == 4);
    setCheckedSilent(m_actMsaa8x,   !s.autoMsaa && s.requestedMsaa == 8);
    setCheckedSilent(m_actMsaa16x,  !s.autoMsaa && s.requestedMsaa == 16);

    setCheckedSilent(m_actShaderLineAa, s.shaderLineAA);
    setCheckedSilent(m_actFilterCatmullSharpen, s.downsampleFilter == DownsampleFilter::CatmullRomSharpen);
    setCheckedSilent(m_actFilterBicubic,        s.downsampleFilter == DownsampleFilter::BicubicSmooth);
    setCheckedSilent(m_actFilterBilinear,       s.downsampleFilter == DownsampleFilter::BilinearHardware);

    setCheckedSilent(m_actLineThin,     nearEq(s.lineWidthMultiplier, 0.85f));
    setCheckedSilent(m_actLineBalanced, nearEq(s.lineWidthMultiplier, 1.00f));
    setCheckedSilent(m_actLineCrisp,    nearEq(s.lineWidthMultiplier, 1.15f));
    setCheckedSilent(m_actLineBold,     nearEq(s.lineWidthMultiplier, 1.50f));

    setCheckedSilent(m_actThemeObsidian,   s.planeTheme == ActivePlaneTheme::ObsidianDark);
    setCheckedSilent(m_actThemePureNight,  s.planeTheme == ActivePlaneTheme::PureNight);
    setCheckedSilent(m_actThemeGraphite,   s.planeTheme == ActivePlaneTheme::GraphiteDark);
    setCheckedSilent(m_actThemeCleanLight, s.planeTheme == ActivePlaneTheme::CleanLight);
    setCheckedSilent(m_actPlaneSurface,    s.showActivePlaneSurface);
    setCheckedSilent(m_actResolutionHud,   s.showResolutionHud);
    setCheckedSilent(m_actVsync,           s.vsyncEnabled);
    syncThemeMenuState();
}

void MainWindow::onApplyRenderPreset(QualityPreset preset) {
    if (!m_viewport) return;
    const RenderQualitySettings& cur = m_viewport->renderQualitySettings();
    const RenderQualitySettings next = RenderQualitySettings::fromPreset(
        preset, cur.planeTheme, cur.showActivePlaneSurface, cur.vsyncEnabled, cur.showResolutionHud);
    applyAndSaveRenderSettings(next);
}

void MainWindow::onOpenRenderSettingsDialog() {
    if (!m_viewport) return;
    if (m_renderSettingsDialog) {
        m_windowManager->showAndRaise(m_renderSettingsDialog.data());
        return;
    }

    m_renderSettingsSnapshot = m_viewport->renderQualitySettings();
    auto* dlg = new RenderSettingsDialog(m_renderSettingsSnapshot, m_viewport->graphicsCapabilities(), nullptr);
    m_renderSettingsDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, false, true);
    bindPersistentDialog(QStringLiteral("RenderSettingsDialog"), dlg, false);

    connect(dlg, &RenderSettingsDialog::settingsPreviewChanged, this,
            [this, dlgPtr = QPointer<RenderSettingsDialog>(dlg)](const RenderQualitySettings& preview) {
                if (!m_viewport || !dlgPtr) return;
                m_viewport->setRenderQualitySettings(preview);
                m_viewport->repaint();
                dlgPtr->updateHardwareInfo(m_viewport->graphicsCapabilities());
                syncRenderSettingsMenuState();
            });

    connect(dlg, &RenderSettingsDialog::settingsApplied, this,
            [this](const RenderQualitySettings& applied) {
                applyAndSaveRenderSettings(applied);
            });

    connect(dlg, &QDialog::accepted, this,
            [this, dlgPtr = QPointer<RenderSettingsDialog>(dlg)]() {
                if (dlgPtr) {
                    applyAndSaveRenderSettings(dlgPtr->selectedSettings());
                }
            });

    connect(dlg, &QDialog::rejected, this, [this]() {
        if (m_viewport) {
            m_viewport->setRenderQualitySettings(m_renderSettingsSnapshot);
            m_viewport->update();
        }
        syncRenderSettingsMenuState();
    });

    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onRestoreDefaultRenderSettings() {
    const ActivePlaneTheme defaultTheme = ThemeManager::instance().isLight()
        ? ActivePlaneTheme::CleanLight
        : ActivePlaneTheme::ObsidianDark;
    applyAndSaveRenderSettings(RenderQualitySettings::fromPreset(QualityPreset::AutoMax, defaultTheme));
}

void MainWindow::onChangeGuiTheme(UiTheme theme) {
    {
        const ScopedBoolFlag batchGuard(m_rebuildingOrLoadingResultsSettings);
        if (m_viewport) {
            RenderQualitySettings s = m_viewport->renderQualitySettings();
            if (theme == UiTheme::Light) {
                if (s.planeTheme != ActivePlaneTheme::CleanLight) {
                    m_lastDarkPlaneTheme = s.planeTheme;
                }
                s.planeTheme = ActivePlaneTheme::CleanLight;
                applyAndSaveRenderSettings(s);
            } else if (s.planeTheme == ActivePlaneTheme::CleanLight) {
                s.planeTheme = m_lastDarkPlaneTheme;
                applyAndSaveRenderSettings(s);
            }
        }
        ThemeManager::instance().setTheme(theme);
    }

    if (m_renderSettingsDialog && m_viewport) {
        m_renderSettingsDialog->syncSettings(m_viewport->renderQualitySettings(), m_viewport->graphicsCapabilities());
    }
    showStatusMessage(theme == UiTheme::Light
        ? locale::GerenciadorTraducao::trText("Tema claro aplicado em toda a interface.")
        : locale::GerenciadorTraducao::trText("Tema escuro aplicado em toda a interface."));
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onChangeUiStyleMode(UiStyleMode mode) {
    {
        const ScopedBoolFlag batchGuard(m_rebuildingOrLoadingResultsSettings);
        ThemeManager::instance().setUiStyleMode(mode);
    }

    syncUiStyleMenuState();
    showStatusMessage(mode == UiStyleMode::Native
        ? locale::GerenciadorTraducao::trText("Modo de interface nativo (Qt6 padrão) aplicado.")
        : locale::GerenciadorTraducao::trText("Modo de interface customizado aplicado."));
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onChangeLanguage(const QString& langCode) {
    locale::GerenciadorTraducao::instance().definir_idioma(langCode);
    io::AppLogger::info(QStringLiteral("Settings"), QStringLiteral("Idioma alterado para: %1").arg(langCode));
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::syncStartupSessionMenuState() {
    if (m_actStartupAsk) {
        const QSignalBlocker b(m_actStartupAsk);
        m_actStartupAsk->setChecked(m_startupSessionBehavior == io::StartupSessionBehavior::AskOnStartup);
    }
    if (m_actStartupAutoLoad) {
        const QSignalBlocker b(m_actStartupAutoLoad);
        m_actStartupAutoLoad->setChecked(m_startupSessionBehavior == io::StartupSessionBehavior::AutoLoadPrevious);
    }
    if (m_actStartupNewProject) {
        const QSignalBlocker b(m_actStartupNewProject);
        m_actStartupNewProject->setChecked(m_startupSessionBehavior == io::StartupSessionBehavior::AlwaysStartNew);
    }
    if (m_actConfirmUnsavedOnClose) {
        const QSignalBlocker b(m_actConfirmUnsavedOnClose);
        m_actConfirmUnsavedOnClose->setChecked(m_confirmUnsavedOnClose);
    }
}

void MainWindow::setStartupSessionBehavior(io::StartupSessionBehavior behavior) {
    m_startupSessionBehavior = behavior;
    syncStartupSessionMenuState();
    if (m_startupSessionDialog) {
        m_startupSessionDialog->syncSettings(m_startupSessionBehavior, m_confirmUnsavedOnClose);
    }
    io::AppLogger::info(
        QStringLiteral("Settings"),
        QStringLiteral("Politica de inicializacao definida para: %1")
            .arg(io::startupSessionBehaviorToKey(behavior)));
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::setConfirmUnsavedOnClose(bool enabled) {
    m_confirmUnsavedOnClose = enabled;
    syncStartupSessionMenuState();
    if (m_startupSessionDialog) {
        m_startupSessionDialog->syncSettings(m_startupSessionBehavior, m_confirmUnsavedOnClose);
    }
    io::AppLogger::info(
        QStringLiteral("Settings"),
        QStringLiteral("Confirmacao de alteracoes nao salvas ao sair: %1")
            .arg(enabled ? QStringLiteral("true") : QStringLiteral("false")));
    if (!m_rebuildingOrLoadingResultsSettings) {
        saveUserPreferences();
    }
}

void MainWindow::onChangeStartupSessionBehavior(io::StartupSessionBehavior behavior) {
    setStartupSessionBehavior(behavior);
    switch (behavior) {
        case io::StartupSessionBehavior::AutoLoadPrevious:
            showStatusMessage(locale::GerenciadorTraducao::trText(
                "Inicialização configurada: carregar automaticamente projeto ou exemplo anterior."));
            break;
        case io::StartupSessionBehavior::AlwaysStartNew:
            showStatusMessage(locale::GerenciadorTraducao::trText(
                "Inicialização configurada: sempre iniciar um novo projeto vazio."));
            break;
        case io::StartupSessionBehavior::AskOnStartup:
        default:
            showStatusMessage(locale::GerenciadorTraducao::trText(
                "Inicialização configurada: perguntar a cada nova inicialização."));
            break;
    }
}

void MainWindow::onToggleConfirmUnsavedOnClose(bool checked) {
    setConfirmUnsavedOnClose(checked);
    showStatusMessage(checked
        ? locale::GerenciadorTraducao::trText("Confirmação de alterações não salvas ao sair ativada.")
        : locale::GerenciadorTraducao::trText("Confirmação de alterações não salvas ao sair desativada (recuperação automática mantida)."));
}

void MainWindow::onOpenStartupSessionDialog() {
    if (m_startupSessionDialog) {
        m_startupSessionDialog->syncSettings(m_startupSessionBehavior, m_confirmUnsavedOnClose);
        m_startupSessionDialog->refreshLogSummary();
        m_windowManager->showAndRaise(m_startupSessionDialog.data());
        return;
    }

    auto* dlg = new StartupSessionDialog(m_startupSessionBehavior, m_confirmUnsavedOnClose, nullptr);
    m_startupSessionDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    bindPersistentDialog(QStringLiteral("StartupSessionDialog"), dlg, false);

    connect(dlg, &StartupSessionDialog::settingsChanged, this,
            [this](io::StartupSessionBehavior behavior, bool confirmClose) {
                const bool behaviorChanged = (m_startupSessionBehavior != behavior);
                const bool confirmChanged = (m_confirmUnsavedOnClose != confirmClose);
                if (behaviorChanged) {
                    onChangeStartupSessionBehavior(behavior);
                }
                if (confirmChanged) {
                    onToggleConfirmUnsavedOnClose(confirmClose);
                }
            });
    connect(dlg, &StartupSessionDialog::restoreDefaultsRequested, this, [this, dlg]() {
        setStartupSessionBehavior(io::StartupSessionBehavior::AskOnStartup);
        setConfirmUnsavedOnClose(true);
        if (dlg) {
            dlg->syncSettings(m_startupSessionBehavior, m_confirmUnsavedOnClose);
        }
        showStatusMessage(locale::GerenciadorTraducao::trText(
            "Preferências de inicialização restauradas para o padrão."));
    });
    connect(dlg, &StartupSessionDialog::openLogDirectoryRequested, this, &MainWindow::onOpenLogDirectory);

    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onOpenLogDirectory() {
    const QString logDir = io::AppLogger::logDirectory();
    QDir().mkpath(logDir);
    io::AppLogger::info(QStringLiteral("Settings"),
                        QStringLiteral("Pasta de logs acessada: %1").arg(logDir));
    if (!testAttribute(Qt::WA_DontShowOnScreen)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
    }
    showStatusMessage(locale::GerenciadorTraducao::trText("Diretório de logs: %1")
                          .arg(QDir::toNativeSeparators(logDir)));
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!m_closingConfirmed && m_confirmUnsavedOnClose && m_hasUnsavedEdits && event &&
        (event->spontaneous() || m_userRequestedQuit)) {
        persistCurrentSessionState();
        if (m_actRecoverSession) {
            m_actRecoverSession->setEnabled(io::ProjectRecoveryStore::inspectPreviousSession().available);
        }
        event->ignore();
        promptUnsavedChangesBeforeClose();
        return;
    }
    m_staticSolveContinuations.clear();
    if (m_analysisExecutor) {
        m_analysisExecutor->cancelAndWait(5000, false);
    }
    persistCurrentSessionState();
    m_sessionStatePersistedOnClose = true;
    flushUserPreferencesSync();
    if (m_windowManager) {
        m_windowManager->closeAllChildWindows();
    }
    QMainWindow::closeEvent(event);
}

QString MainWindow::translateCoreMessage(const QString& rawMsg) const {
    using locale::GerenciadorTraducao;
    if (rawMsg == "Erro de validação desconhecido.") {
        return GerenciadorTraducao::trText("Erro de validação desconhecido.");
    }
    if (rawMsg == "Estrutura hipostática: nenhum grau de liberdade restringido."
        || rawMsg == "Estrutura hipostática: nenhum grau de liberdade restrito pelos apoios.") {
        return GerenciadorTraducao::trText("Estrutura hipostática: nenhum grau de liberdade restringido.");
    }
    if (rawMsg == "Falha ao resolver o sistema linear esparso K*U=F.") {
        return GerenciadorTraducao::trText("Falha ao resolver o sistema linear esparso K*U=F.");
    }
    if (rawMsg == "O modelo precisa ter pelo menos 2 nós.") {
        return GerenciadorTraducao::trText("O modelo precisa ter pelo menos 2 nós.");
    }
    if (rawMsg == "O modelo precisa ter pelo menos 1 barra.") {
        return GerenciadorTraducao::trText("O modelo precisa ter pelo menos 1 barra.");
    }
    if (rawMsg == "Nenhum apoio definido no modelo. A estrutura terá movimento de corpo rígido.") {
        return GerenciadorTraducao::trText("Nenhum apoio definido no modelo. A estrutura terá movimento de corpo rígido.");
    }

    static const QRegularExpression rxInvalidNode(R"(^Barra (\d+) conecta a nó inexistente\.$)");
    if (const auto m = rxInvalidNode.match(rawMsg); m.hasMatch()) {
        return GerenciadorTraducao::trText("Barra %1 conecta a nó inexistente.").arg(m.captured(1));
    }
    static const QRegularExpression rxZeroLen(R"(^Barra (\d+) tem comprimento nulo(?: ou inválido)?\.$)");
    if (const auto m = rxZeroLen.match(rawMsg); m.hasMatch()) {
        return GerenciadorTraducao::trText("Barra %1 tem comprimento nulo.").arg(m.captured(1));
    }
    static const QRegularExpression rxIsolatedNode(R"(^Nó (\d+) está isolado sem nenhuma barra conectada\.$)");
    if (const auto m = rxIsolatedNode.match(rawMsg); m.hasMatch()) {
        return GerenciadorTraducao::trText("Nó %1 está isolado sem nenhuma barra conectada.").arg(m.captured(1));
    }
    return rawMsg;
}

void MainWindow::translateComboPreservingData() {
    using locale::GerenciadorTraducao;

    if (m_planeCombo) {
        const int idx = m_planeCombo->currentIndex();
        m_planeCombo->blockSignals(true);
        if (m_planeCombo->count() >= 3) {
            m_planeCombo->setItemText(0, GerenciadorTraducao::trText("XZ (Frontal)"));
            m_planeCombo->setItemText(1, GerenciadorTraducao::trText("XY (Planta)"));
            m_planeCombo->setItemText(2, GerenciadorTraducao::trText("YZ (Lateral)"));
        }
        m_planeCombo->setCurrentIndex(idx);
        m_planeCombo->blockSignals(false);
    }

    if (m_diagramCombo) {
        const int idx = m_diagramCombo->currentIndex();
        m_diagramCombo->blockSignals(true);
        if (m_diagramCombo->count() >= 8) {
            m_diagramCombo->setItemText(0, GerenciadorTraducao::trText("Nenhum (Geometria)"));
            m_diagramCombo->setItemText(1, GerenciadorTraducao::trText("Deformada 3D"));
            m_diagramCombo->setItemText(2, GerenciadorTraducao::trText("Normal (N)"));
            m_diagramCombo->setItemText(3, GerenciadorTraducao::trText("Cortante (Vy)"));
            m_diagramCombo->setItemText(4, GerenciadorTraducao::trText("Cortante (Vz)"));
            m_diagramCombo->setItemText(5, GerenciadorTraducao::trText("Torsor (Tx)"));
            m_diagramCombo->setItemText(6, GerenciadorTraducao::trText("Fletor (My)"));
            m_diagramCombo->setItemText(7, GerenciadorTraducao::trText("Fletor (Mz)"));
        }
        m_diagramCombo->setCurrentIndex(idx);
        m_diagramCombo->blockSignals(false);
    }
}

void MainWindow::retranslateUi() {
    using locale::GerenciadorTraducao;
    updateWindowTitleAndFileActions();
    updateRecoveryPromptBoxTexts();
    updateCloseConfirmBoxTexts();

    // Menus
    if (m_fileMenu) m_fileMenu->setTitle(GerenciadorTraducao::trText("&Arquivo"));
    if (m_actNewProject) m_actNewProject->setText(GerenciadorTraducao::trText("Novo Projeto"));
    if (m_actOpenProject) m_actOpenProject->setText(GerenciadorTraducao::trText("Abrir (.omniframe)..."));
    if (m_actSaveProject) m_actSaveProject->setText(GerenciadorTraducao::trText("Salvar (.omniframe)"));
    if (m_actSaveProjectAs) m_actSaveProjectAs->setText(GerenciadorTraducao::trText("Salvar Como (.omniframe)..."));
    if (m_actRecoverSession) m_actRecoverSession->setText(GerenciadorTraducao::trText("Recuperar Sessão Anterior..."));
    if (m_actImportDxf) m_actImportDxf->setText(GerenciadorTraducao::trText("Importar DXF..."));
    if (m_actExportDxf) m_actExportDxf->setText(GerenciadorTraducao::trText("Exportar DXF..."));
    if (m_actImportIfc) m_actImportIfc->setText(GerenciadorTraducao::trText("Importar BIM IFC4..."));
    if (m_actExportIfc) m_actExportIfc->setText(GerenciadorTraducao::trText("Exportar BIM IFC4..."));
    if (m_actExportPdf) m_actExportPdf->setText(GerenciadorTraducao::trText("Exportar Memorial Executivo (PDF A4)..."));
    if (m_actExportHtml) m_actExportHtml->setText(GerenciadorTraducao::trText("Exportar Relatório Técnico (HTML)..."));
    if (m_actExportMd) m_actExportMd->setText(GerenciadorTraducao::trText("Exportar Relatório Técnico (Markdown)..."));
    if (m_actQuit) m_actQuit->setText(GerenciadorTraducao::trText("Sair"));

    if (m_editMenu) m_editMenu->setTitle(GerenciadorTraducao::trText("&Editar"));
    if (m_actUndo) m_actUndo->setText(GerenciadorTraducao::trText("Desfazer"));
    if (m_actRedo) m_actRedo->setText(GerenciadorTraducao::trText("Refazer"));

    if (m_exMenu) m_exMenu->setTitle(GerenciadorTraducao::trText("&Exemplos"));
    if (m_actEx1) m_actEx1->setText(GerenciadorTraducao::trText("Exemplo 1: Pórtico Espacial de Edifício (2 Pavimentos, Vento e Sobrecarga)"));
    if (m_actEx2) m_actEx2->setText(GerenciadorTraducao::trText("Exemplo 2: Balanço Espacial em L Sujeito a Flexo-Torção"));
    if (m_actEx3) m_actEx3->setText(GerenciadorTraducao::trText("Exemplo 3: Treliça Espacial / Passarela Metálica 3D"));

    if (m_structMenu) m_structMenu->setTitle(GerenciadorTraducao::trText("&Estrutura"));
    if (m_actMaterials) m_actMaterials->setText(GerenciadorTraducao::trText("Materiais..."));
    if (m_actSections) m_actSections->setText(GerenciadorTraducao::trText("Seções Transversais..."));
    if (m_actSupports) m_actSupports->setText(GerenciadorTraducao::trText("Condições de Apoio..."));
    if (m_actReleases) m_actReleases->setText(GerenciadorTraducao::trText("Rótulas / Liberações..."));
    if (m_actNodalLoads) m_actNodalLoads->setText(GerenciadorTraducao::trText("Cargas Nodais Concentradas..."));
    if (m_actDistLoads) m_actDistLoads->setText(GerenciadorTraducao::trText("Cargas Uniformemente Distribuídas..."));
    if (m_actThermalLoads) m_actThermalLoads->setText(GerenciadorTraducao::trText("Cargas Térmicas (ΔT)..."));
    if (m_actRollAngle) m_actRollAngle->setText(GerenciadorTraducao::trText("Ângulo de Roll da Seção (γ)..."));
    if (m_actLoadCombos) m_actLoadCombos->setText(GerenciadorTraducao::trText("Casos de Carga, Combinações NBR & Vento..."));
    if (m_actMeshGen) m_actMeshGen->setText(GerenciadorTraducao::trText("Gerador de Malha 1D/2D/3D..."));
    if (m_actAutoRecalc) m_actAutoRecalc->setText(GerenciadorTraducao::trText("Recálculo Automático após Edição"));
    if (m_actSolveMenu) m_actSolveMenu->setText(GerenciadorTraducao::trText("CALCULAR ESTRUTURA"));
    if (m_actAnalysisSuite) m_actAnalysisSuite->setText(GerenciadorTraducao::trText("Suite de Análises (Modal / Flambagem / Não Linear / Sismo)..."));
    if (m_actPostControls) m_actPostControls->setText(GerenciadorTraducao::trText("Controles 3D (Contornos / Corte / Animação)..."));
    if (m_actNbrAudit) m_actNbrAudit->setText(GerenciadorTraducao::trText("Auditoria Normativa ABNT NBR (6118 / 8800 / 6122)..."));

    if (m_viewMenu) m_viewMenu->setTitle(GerenciadorTraducao::trText("&Exibir"));
    if (m_actViewIsoMenu) m_actViewIsoMenu->setText(GerenciadorTraducao::trText("Vista 3D Isométrica"));
    if (m_actViewFrontMenu) m_actViewFrontMenu->setText(GerenciadorTraducao::trText("Vista Frontal (XZ)"));
    if (m_actViewTopMenu) m_actViewTopMenu->setText(GerenciadorTraducao::trText("Vista de Planta (XY)"));
    if (m_actViewSideMenu) m_actViewSideMenu->setText(GerenciadorTraducao::trText("Vista Lateral (YZ)"));
    if (m_actViewFitMenu) m_actViewFitMenu->setText(GerenciadorTraducao::trText("Enquadrar Tudo (F)"));

    if (m_settingsMenu) m_settingsMenu->setTitle(GerenciadorTraducao::trText("&Configurações"));
    if (m_qualityPresetMenu) m_qualityPresetMenu->setTitle(GerenciadorTraducao::trText("Perfil de Qualidade Gráfica"));
    if (m_actPresetAutoMax) m_actPresetAutoMax->setText(GerenciadorTraducao::trText("Automático (Máxima Qualidade)"));
    if (m_actPresetUltra) m_actPresetUltra->setText(GerenciadorTraducao::trText("Ultra (SSAA 200% + MSAA Máx)"));
    if (m_actPresetHigh) m_actPresetHigh->setText(GerenciadorTraducao::trText("Alta (SSAA 150% + MSAA 8×)"));
    if (m_actPresetBalanced) m_actPresetBalanced->setText(GerenciadorTraducao::trText("Equilibrada (Nativa 100% + MSAA 4×)"));
    if (m_actPresetPerf) m_actPresetPerf->setText(GerenciadorTraducao::trText("Desempenho (Nativa 100% + Shader AA)"));

    if (m_resScaleMenu) m_resScaleMenu->setTitle(GerenciadorTraducao::trText("Escala de Resolução Interna (SSAA)"));
    if (m_actScaleAuto) m_actScaleAuto->setText(GerenciadorTraducao::trText("Automática (Máxima Suportada)"));
    if (m_actScale75) m_actScale75->setText(GerenciadorTraducao::trText("75% (Desempenho)"));
    if (m_actScale100) m_actScale100->setText(GerenciadorTraducao::trText("100% (Resolução Nativa)"));
    if (m_actScale125) m_actScale125->setText(GerenciadorTraducao::trText("125% (Alta Definição)"));
    if (m_actScale150) m_actScale150->setText(GerenciadorTraducao::trText("150% (Superamostragem)"));
    if (m_actScale200) m_actScale200->setText(GerenciadorTraducao::trText("200% (Ultra Definição)"));

    if (m_msaaMenu) m_msaaMenu->setTitle(GerenciadorTraducao::trText("Suavização por Hardware (MSAA)"));
    if (m_actMsaaAuto) m_actMsaaAuto->setText(GerenciadorTraducao::trText("Automático (Máximo da GPU)"));
    if (m_actMsaaOff) m_actMsaaOff->setText(GerenciadorTraducao::trText("Desligado"));
    if (m_actMsaa2x) m_actMsaa2x->setText(QStringLiteral("2× MSAA"));
    if (m_actMsaa4x) m_actMsaa4x->setText(QStringLiteral("4× MSAA"));
    if (m_actMsaa8x) m_actMsaa8x->setText(QStringLiteral("8× MSAA"));
    if (m_actMsaa16x) m_actMsaa16x->setText(QStringLiteral("16× MSAA"));

    if (m_definitionMenu) m_definitionMenu->setTitle(GerenciadorTraducao::trText("Definição de Linhas e Imagem"));
    if (m_actShaderLineAa) m_actShaderLineAa->setText(GerenciadorTraducao::trText("Suavização Analítica de Linhas (Shader SDF)"));
    if (m_actFilterCatmullSharpen) m_actFilterCatmullSharpen->setText(GerenciadorTraducao::trText("Filtro Catmull-Rom + Nitidez (Máximo)"));
    if (m_actFilterBicubic) m_actFilterBicubic->setText(GerenciadorTraducao::trText("Filtro Bicúbico Suave"));
    if (m_actFilterBilinear) m_actFilterBilinear->setText(GerenciadorTraducao::trText("Filtro Bilinear Padrão"));
    if (m_actLineThin) m_actLineThin->setText(GerenciadorTraducao::trText("Espessura Fina (0,85×)"));
    if (m_actLineBalanced) m_actLineBalanced->setText(GerenciadorTraducao::trText("Espessura Equilibrada (1,00×)"));
    if (m_actLineCrisp) m_actLineCrisp->setText(GerenciadorTraducao::trText("Espessura Nítida (1,15×)"));
    if (m_actLineBold) m_actLineBold->setText(GerenciadorTraducao::trText("Espessura Destacada (1,50×)"));

    if (m_activePlaneAppearanceMenu) m_activePlaneAppearanceMenu->setTitle(GerenciadorTraducao::trText("Aparência do Plano Ativo"));
    if (m_actThemeObsidian) m_actThemeObsidian->setText(GerenciadorTraducao::trText("Fundo Obsidiana Escuro (Padrão)"));
    if (m_actThemePureNight) m_actThemePureNight->setText(GerenciadorTraducao::trText("Fundo Noite Profunda (Ultra Escuro)"));
    if (m_actThemeGraphite) m_actThemeGraphite->setText(GerenciadorTraducao::trText("Fundo Grafite Escuro"));
    if (m_actThemeCleanLight) m_actThemeCleanLight->setText(GerenciadorTraducao::trText("Fundo Claro Técnico"));
    if (m_actPlaneSurface) m_actPlaneSurface->setText(GerenciadorTraducao::trText("Superfície e Moldura do Plano Ativo"));
    if (m_actResolutionHud) m_actResolutionHud->setText(GerenciadorTraducao::trText("Telemetria de Resolução no Viewport"));

    if (m_actVsync) m_actVsync->setText(GerenciadorTraducao::trText("Sincronização Vertical (VSync)"));
    if (m_actAdvancedRenderSettings) m_actAdvancedRenderSettings->setText(GerenciadorTraducao::trText("Configurações Avançadas de Resolução e Imagem..."));
    if (m_actRestoreRenderDefaults) m_actRestoreRenderDefaults->setText(GerenciadorTraducao::trText("Restaurar Padrões de Máxima Qualidade"));

    if (m_resultsLayoutsMenu) {
        m_resultsLayoutsMenu->setTitle(GerenciadorTraducao::trText("Layouts do Painel de Tabelas de Resultados"));
        const char* countLabels[4] = {
            "1 Layout (Todas as Tabelas)",
            "2 Layouts",
            "3 Layouts",
            "4 Layouts"
        };
        const char* layoutMenuTitles[4] = {
            "Tabelas do 1º Layout",
            "Tabelas do 2º Layout",
            "Tabelas do 3º Layout",
            "Tabelas do 4º Layout"
        };
        for (int i = 0; i < 4; ++i) {
            if (m_actResultsLayoutCount[static_cast<std::size_t>(i)]) {
                m_actResultsLayoutCount[static_cast<std::size_t>(i)]->setText(
                    GerenciadorTraducao::trText(countLabels[i]));
            }
            if (m_resultsLayoutConfigMenus[static_cast<std::size_t>(i)]) {
                m_resultsLayoutConfigMenus[static_cast<std::size_t>(i)]->setTitle(
                    GerenciadorTraducao::trText(layoutMenuTitles[i]));
            }
            for (int k = 0; k < 4; ++k) {
                if (m_actResultsLayoutTables[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)]) {
                    m_actResultsLayoutTables[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)]->setText(
                        resultTableTabTitle(static_cast<ui::ResultTableKind>(k)));
                }
            }
            if (m_actResultsLayoutSelectAll[static_cast<std::size_t>(i)]) {
                m_actResultsLayoutSelectAll[static_cast<std::size_t>(i)]->setText(
                    GerenciadorTraducao::trText("Selecionar Todas as Tabelas"));
            }
        }
        if (m_actResetResultsLayouts) {
            m_actResetResultsLayouts->setText(GerenciadorTraducao::trText("Restaurar Layouts Padrão"));
        }
    }

    if (m_preferencesPersistenceMenu) {
        m_preferencesPersistenceMenu->setTitle(
            GerenciadorTraducao::trText("Persistência de Configurações e Preferências"));
    }
    if (m_actSaveAllPreferences) {
        m_actSaveAllPreferences->setText(
            GerenciadorTraducao::trText("Salvar Todas as Configurações Agora"));
    }
    if (m_actReloadSavedPreferences) {
        m_actReloadSavedPreferences->setText(
            GerenciadorTraducao::trText("Recarregar Configurações Salvas"));
    }
    if (m_actRestoreAllDefaults) {
        m_actRestoreAllDefaults->setText(
            GerenciadorTraducao::trText("Restaurar Todas as Configurações Padrão"));
    }

    if (m_startupSessionMenu) {
        m_startupSessionMenu->setTitle(GerenciadorTraducao::trText("Sessão, Inicialização e Logs"));
    }
    if (m_actStartupAsk) {
        m_actStartupAsk->setText(GerenciadorTraducao::trText("Perguntar ao iniciar (Exibir diálogo)"));
    }
    if (m_actStartupAutoLoad) {
        m_actStartupAutoLoad->setText(
            GerenciadorTraducao::trText("Carregar automaticamente projeto ou exemplo anterior"));
    }
    if (m_actStartupNewProject) {
        m_actStartupNewProject->setText(
            GerenciadorTraducao::trText("Sempre iniciar um novo projeto (Vazio)"));
    }
    if (m_actConfirmUnsavedOnClose) {
        m_actConfirmUnsavedOnClose->setText(
            GerenciadorTraducao::trText("Exibir confirmação de alterações não salvas ao sair"));
    }
    if (m_actOpenStartupSessionDialog) {
        m_actOpenStartupSessionDialog->setText(
            GerenciadorTraducao::trText("Configurar Inicialização, Sessão e Logs..."));
    }
    if (m_actOpenLogDirectory) {
        m_actOpenLogDirectory->setText(GerenciadorTraducao::trText("Abrir Pasta de Logs..."));
    }
    syncStartupSessionMenuState();

    if (m_themeMenu) m_themeMenu->setTitle(GerenciadorTraducao::trText("&Tema"));
    if (m_actGuiThemeDark) m_actGuiThemeDark->setText(GerenciadorTraducao::trText("Escuro"));
    if (m_actGuiThemeLight) m_actGuiThemeLight->setText(GerenciadorTraducao::trText("Claro"));
    syncThemeMenuState();

    if (m_uiStyleMenu) m_uiStyleMenu->setTitle(GerenciadorTraducao::trText("Interface do Usuário"));
    if (m_actUiStyleNative) m_actUiStyleNative->setText(GerenciadorTraducao::trText("Nativo"));
    if (m_actUiStyleCustom) m_actUiStyleCustom->setText(GerenciadorTraducao::trText("Customizado"));
    syncUiStyleMenuState();

    if (m_langMenu) m_langMenu->setTitle(GerenciadorTraducao::trText("&Idioma"));
    const bool isEn = (GerenciadorTraducao::instance().idiomaAtual() == locale::Idioma::InglesEstadosUnidos);
    if (m_actLangPtBr) m_actLangPtBr->setChecked(!isEn);
    if (m_actLangEnUs) m_actLangEnUs->setChecked(isEn);

    if (m_cliWindowsMenu) m_cliWindowsMenu->setTitle(GerenciadorTraducao::trText("CLI Windows (CMD / PowerShell)"));
    if (m_actCliInstallWin) m_actCliInstallWin->setText(GerenciadorTraducao::trText("Registrar omniframe-cli no Windows (PATH e App Paths)..."));
    if (m_actCliUninstallWin) m_actCliUninstallWin->setText(GerenciadorTraducao::trText("Remover Registro da CLI no Windows..."));
    if (m_actCliOpenCmd) m_actCliOpenCmd->setText(GerenciadorTraducao::trText("Abrir Prompt de Comando (CMD) com CLI..."));
    if (m_actCliOpenPowerShell) m_actCliOpenPowerShell->setText(GerenciadorTraducao::trText("Abrir PowerShell com CLI..."));
    if (m_actCliCheckWin) m_actCliCheckWin->setText(GerenciadorTraducao::trText("Verificar Status da CLI no Windows..."));

    if (m_helpMenu) m_helpMenu->setTitle(GerenciadorTraducao::trText("&Ajuda"));
    if (m_actManual) m_actManual->setText(GerenciadorTraducao::trText("Manual"));
    if (m_actGuidedSession) m_actGuidedSession->setText(GerenciadorTraducao::trText("Sessão Guiada..."));
    if (m_actAbout) m_actAbout->setText(GerenciadorTraducao::trText("Sobre o OmniFrame 3D"));

    // Toolbars
    if (m_tb1) m_tb1->setWindowTitle(GerenciadorTraducao::trText("Interação e Plano de Trabalho"));
    if (m_actSelect) m_actSelect->setText(GerenciadorTraducao::trText("Selecionar (S)"));
    if (m_actAddNode) m_actAddNode->setText(GerenciadorTraducao::trText("Inserir Nó (N)"));
    if (m_actAddElement) m_actAddElement->setText(GerenciadorTraducao::trText("Inserir Barra (B)"));
    if (m_actMeasure) m_actMeasure->setText(GerenciadorTraducao::trText("Medir (M)"));
    if (m_actDelete) m_actDelete->setText(GerenciadorTraducao::trText("Excluir (Del)"));
    if (m_planeLabel) m_planeLabel->setText(GerenciadorTraducao::trText(" Plano: "));
    if (m_offsetLabel) m_offsetLabel->setText(GerenciadorTraducao::trText(" Cota: "));
    if (m_actSnap) m_actSnap->setText(GerenciadorTraducao::trText("Snap Grid"));
    if (m_stepLabel) m_stepLabel->setText(GerenciadorTraducao::trText(" Passo: "));
    if (m_actLocalAxes) m_actLocalAxes->setText(GerenciadorTraducao::trText("Eixos Locais"));
    if (m_actViewIsoTb) m_actViewIsoTb->setText(GerenciadorTraducao::trText("3D Iso"));
    if (m_actViewFrontTb) m_actViewFrontTb->setText(GerenciadorTraducao::trText("Frontal"));
    if (m_actViewTopTb) m_actViewTopTb->setText(GerenciadorTraducao::trText("Planta"));
    if (m_actViewSideTb) m_actViewSideTb->setText(GerenciadorTraducao::trText("Lateral"));
    if (m_actViewFitTb) m_actViewFitTb->setText(GerenciadorTraducao::trText("Enquadrar (F)"));

    if (m_tb2) m_tb2->setWindowTitle(GerenciadorTraducao::trText("Engenharia e Resultados"));
    updateToolbarExtensionToolTips(m_tb1, m_tb2);
    if (m_actTbMaterials) m_actTbMaterials->setText(GerenciadorTraducao::trText("Materiais..."));
    if (m_actTbSections) m_actTbSections->setText(GerenciadorTraducao::trText("Seções..."));
    if (m_actTbSupports) m_actTbSupports->setText(GerenciadorTraducao::trText("Apoios..."));
    if (m_actTbReleases) m_actTbReleases->setText(GerenciadorTraducao::trText("Rótulas..."));
    if (m_actTbNodalLoads) m_actTbNodalLoads->setText(GerenciadorTraducao::trText("Carga Nodal..."));
    if (m_actTbDistLoads) m_actTbDistLoads->setText(GerenciadorTraducao::trText("Carga Distr...."));
    if (m_actTbThermalLoads) m_actTbThermalLoads->setText(GerenciadorTraducao::trText("Térmica (ΔT)..."));
    if (m_actTbRollAngle) m_actTbRollAngle->setText(GerenciadorTraducao::trText("Roll (γ)..."));
    if (m_actTbCombos) m_actTbCombos->setText(GerenciadorTraducao::trText("Combinações..."));
    if (m_actTbMesh) m_actTbMesh->setText(GerenciadorTraducao::trText("Malha 2D/3D..."));
    if (m_btnSolve) m_btnSolve->setText(GerenciadorTraducao::trText(" ▶ CALCULAR ESTRUTURA (F5)"));
    if (m_actTbAdvAnalysis) m_actTbAdvAnalysis->setText(GerenciadorTraducao::trText("Avançadas..."));
    if (m_actTbPostCtrl) m_actTbPostCtrl->setText(GerenciadorTraducao::trText("Corte/Contornos..."));
    if (m_actTbNbrAudit) m_actTbNbrAudit->setText(GerenciadorTraducao::trText("Auditoria NBR..."));
    if (m_diagLabel) m_diagLabel->setText(GerenciadorTraducao::trText(" Diagrama: "));
    if (m_actReactions) m_actReactions->setText(GerenciadorTraducao::trText("Reações"));
    if (m_scaleLabel) m_scaleLabel->setText(GerenciadorTraducao::trText(" Escala: "));

    translateComboPreservingData();

    // Docks & Tables
    if (m_inspectorDock) m_inspectorDock->setWindowTitle(GerenciadorTraducao::trText("Inspetor e Entrada Rápida"));
    if (m_inspectorTitleBar) m_inspectorTitleBar->retranslateToolTip();
    if (m_entryGroup) m_entryGroup->setTitle(GerenciadorTraducao::trText("Adicionar Nó por Coordenadas"));
    if (m_btnAddNode) m_btnAddNode->setText(GerenciadorTraducao::trText("Adicionar Nó"));
    if (m_inspGroup) m_inspGroup->setTitle(GerenciadorTraducao::trText("Elemento / Nó Selecionado"));

    if (m_resultsDock) m_resultsDock->setWindowTitle(GerenciadorTraducao::trText("Tabelas de Resultados e Diagnóstico Estrutural"));
    if (m_resultsTitleBar) m_resultsTitleBar->retranslateToolTip();

    for (int L = 0; L < 4; ++L) {
        QTabWidget* tabW = m_resultsLayoutTabWidgets[static_cast<std::size_t>(L)];
        if (tabW == nullptr) {
            continue;
        }
        auto updateTabTitle = [&](int tabIndex, ui::ResultTableKind kind) {
            const QString title = resultTableTabTitle(kind);
            tabW->setTabText(tabIndex, title);
            tabW->setTabToolTip(tabIndex, title);
        };
        for (int t = 0; t < tabW->count(); ++t) {
            const QWidget* w = tabW->widget(t);
            if (w == m_dispTable || w == m_extraLayoutTables[static_cast<std::size_t>(L)][0]) {
                updateTabTitle(t, ui::ResultTableKind::Displacements);
            } else if (w == m_reacTable || w == m_extraLayoutTables[static_cast<std::size_t>(L)][1]) {
                updateTabTitle(t, ui::ResultTableKind::Reactions);
            } else if (w == m_forcesTable || w == m_extraLayoutTables[static_cast<std::size_t>(L)][2]) {
                updateTabTitle(t, ui::ResultTableKind::BarForces);
            } else if (w == m_continuumTable || w == m_extraLayoutTables[static_cast<std::size_t>(L)][3]) {
                updateTabTitle(t, ui::ResultTableKind::ContinuumStress);
            }
        }
    }

    auto retranslateTable = [this](QTableView* table, ui::ResultTableKind kind) {
        if (!table) {
            return;
        }
        const QStringList hdrs = resultTableHeaders(kind);
        if (m_tableConfigurator) {
            m_tableConfigurator->updateHeaders(table, hdrs);
        }
    };
    for (int k = 0; k < 4; ++k) {
        if (m_resultTableModels[static_cast<std::size_t>(k)] != nullptr) {
            m_resultTableModels[static_cast<std::size_t>(k)]->setHeaders(
                resultTableHeaders(static_cast<ui::ResultTableKind>(k)));
        }
    }
    retranslateTable(m_dispTable, ui::ResultTableKind::Displacements);
    retranslateTable(m_reacTable, ui::ResultTableKind::Reactions);
    retranslateTable(m_forcesTable, ui::ResultTableKind::BarForces);
    retranslateTable(m_continuumTable, ui::ResultTableKind::ContinuumStress);
    for (int L = 0; L < 4; ++L) {
        for (int k = 0; k < 4; ++k) {
            retranslateTable(
                m_extraLayoutTables[static_cast<std::size_t>(L)][static_cast<std::size_t>(k)],
                static_cast<ui::ResultTableKind>(k));
        }
    }
    if (auto* reacModel = m_resultTableModels[static_cast<std::size_t>(ui::ResultTableKind::Reactions)]) {
        reacModel->updateSummaryLabel(GerenciadorTraducao::trText("Σ Reações"));
        refreshReactionSummaryRowTheme();
    }
    syncExtraLayoutTables();

    if (m_modelStatsLabel) {
        QString statsMsg = GerenciadorTraducao::trText("Nós: %1 | Barras: %2")
                               .arg(m_model.getNodes().size())
                               .arg(m_model.getElements().size());
        if (!m_model.surfaceMesh().elements.empty()) {
            statsMsg += QStringLiteral(" | 2D/3D: %1n / %2e")
                            .arg(m_model.surfaceMesh().nodes.size())
                            .arg(m_model.surfaceMesh().elements.size());
        }
        m_modelStatsLabel->setText(statsMsg);
    }
    if (m_solverStatusLabel) {
        if (m_lastSolverResult.success) {
            QString okMsg =
                GerenciadorTraducao::trText("Cálculo MEF concluído com sucesso em %1 ms (%2 nós, %3 barras, %4 DOFs). Equilíbrio: %5")
                .arg(QString::number(m_lastSolverResult.executionTimeMs, 'f', 1),
                     QString::number(m_lastSolverResult.totalNodes),
                     QString::number(m_lastSolverResult.totalElements),
                     QString::number(m_lastSolverResult.totalDofs),
                     m_lastSolverResult.equilibrium.isBalanced
                         ? GerenciadorTraducao::trText("OK (ΣF=0)")
                         : GerenciadorTraducao::trText("Alerta de Desequilíbrio"));
            if (m_lastSolverResult.totalSurfaceElements > 0) {
                okMsg += QStringLiteral(" | 2D/3D: +%1n / +%2e")
                             .arg(m_lastSolverResult.totalSurfaceNodes)
                             .arg(m_lastSolverResult.totalSurfaceElements);
            }
            m_solverStatusLabel->setText(okMsg);
        } else if (!m_lastSolverResult.errorMessage.empty()) {
            m_solverStatusLabel->setText(
                GerenciadorTraducao::trText("Erro no cálculo: %1")
                .arg(translateCoreMessage(QString::fromStdString(m_lastSolverResult.errorMessage))));
        } else if (m_hasSolvedContext && !m_autoRecalculate) {
            m_solverStatusLabel->setText(
                GerenciadorTraducao::trText("Modelo modificado — clique em CALCULAR ESTRUTURA (F5) para atualizar os resultados."));
        } else if (m_model.getNodes().empty() &&
                   m_model.getElements().empty() &&
                   m_model.surfaceMesh().elements.empty()) {
            m_solverStatusLabel->setText(GerenciadorTraducao::trText("Pronto para modelar."));
        } else {
            m_solverStatusLabel->setText(GerenciadorTraducao::trText("Pronto para calcular."));
        }
    }

    if (auto* reacModel = m_resultTableModels[static_cast<std::size_t>(ui::ResultTableKind::Reactions)];
        reacModel != nullptr && reacModel->rowCount() > 0 && m_lastSolverResult.success) {
        reacModel->updateSummaryLabel(GerenciadorTraducao::trText("Σ Reações"));
    }

    updateInspector();
    if (m_viewport) {
        m_viewport->update();
    }
}

} // namespace omniframe
