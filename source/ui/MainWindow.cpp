#include "MainWindow.hpp"
#include "AboutDialog.hpp"
#include "Dialogs.hpp"
#include "EngineeringDialogs.hpp"
#include "GuidedSessionDialog.hpp"
#include "ManualDialog.hpp"
#include "RenderSettingsDialog.hpp"
#include "ThemeManager.hpp"
#include "WindowZOrderManager.hpp"
#include "io/DxfInterop.hpp"
#include "io/IfcInterop.hpp"
#include "io/PdfReportGenerator.hpp"
#include "io/ProjectSerializer.hpp"
#include "locale/GerenciadorTraducao.hpp"
#include "os/WindowsCliIntegration.hpp"

#include <cmath>
#include <memory>
#include <utility>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QEvent>
#include <QKeySequence>
#include <QProgressBar>
#include <QStyleFactory>
#include <QThread>
#include <QTimer>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>

namespace omniframe {

namespace {

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

template <typename DialogT, typename FactoryFn>
void openManagedInfoDialog(WindowZOrderManager* wm,
                           QPointer<DialogT>& slot,
                           bool resizable,
                           FactoryFn&& createFn) {
    if (slot) {
        wm->showAndRaise(slot.data());
        return;
    }
    DialogT* dlg = createFn();
    slot = dlg;
    wm->prepareModelessDialog(dlg, resizable, true);
    wm->showAndRaise(dlg);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_windowManager(new WindowZOrderManager(this, this)) {
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

    setupMenus();
    setupToolbars();
    setupDocks();
    setupStatusBar();
    retranslateUi();
    syncRenderSettingsMenuState();
    applyTheme();

    m_windowManager->registerDockWidget(m_inspectorDock);
    m_windowManager->registerDockWidget(m_resultsDock);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](UiTheme) {
        applyTheme();
    });

    // Conexões de sinais do Viewport
    connect(m_viewport, &Viewport3D::cursorCoordinatesChanged, this, &MainWindow::updateCursorCoordinates);
    connect(m_viewport, &Viewport3D::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_viewport, &Viewport3D::modelModified, this, &MainWindow::onModelModified);
    connect(m_viewport, &Viewport3D::statusMessage, this, &MainWindow::showStatusMessage);

    // Carrega o Exemplo 1 (Pórtico Espacial de Edifício) por padrão conforme diretriz do Master Prompt
    loadExampleBuilding();
}

MainWindow::~MainWindow() {
    if (m_activeSolverThread && m_activeSolverThread->isRunning()) {
        m_activeSolverThread->wait(5000);
    }
}

void MainWindow::applyTheme() {
    const bool isLight = ThemeManager::instance().isLight();
    if (m_btnSolve) {
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
    refreshReactionSummaryRowTheme();
    syncThemeMenuState();
    if (m_viewport) {
        m_viewport->update();
    }
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

void MainWindow::refreshReactionSummaryRowTheme() {
    if (!m_reacTable || m_reacTable->rowCount() <= 0) {
        return;
    }
    const int sumRow = m_reacTable->rowCount() - 1;
    QTableWidgetItem* itemSum = m_reacTable->item(sumRow, 0);
    if (!itemSum) {
        return;
    }
    const bool isLight = ThemeManager::instance().isLight();
    const QColor bg = isLight ? QColor(219, 234, 254) : QColor(43, 108, 176);
    const QColor fg = isLight ? QColor(30, 58, 138) : Qt::white;
    itemSum->setBackground(bg);
    itemSum->setForeground(fg);
    QFont f = itemSum->font();
    f.setBold(true);
    itemSum->setFont(f);
}

void MainWindow::setupMenus() {
    // Menu Arquivo
    m_fileMenu = menuBar()->addMenu("&Arquivo");
    m_actNewProject = m_fileMenu->addAction("Novo Projeto", QKeySequence::New, this, &MainWindow::onNewProject);
    m_actOpenProject = m_fileMenu->addAction("Abrir (.omniframe)...", QKeySequence::Open, this, &MainWindow::onOpenProject);
    m_actSaveProject = m_fileMenu->addAction("Salvar (.omniframe)...", QKeySequence::Save, this, &MainWindow::onSaveProject);
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
    m_actQuit = m_fileMenu->addAction("Sair", QKeySequence::Quit, this, &QWidget::close);

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
}

void MainWindow::setupDocks() {
    // -------------------------------------------------------------
    // Dock Esquerdo: Inspetor de Propriedades e Entrada Numérica
    // -------------------------------------------------------------
    m_inspectorDock = new QDockWidget("Inspetor e Entrada Rápida", this);
    m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* leftContainer = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftContainer);

    // Bloco de Entrada Numérica de Coordenadas
    m_entryGroup = new QGroupBox("Adicionar Nó por Coordenadas");
    auto* entryLayout = new QFormLayout(m_entryGroup);

    m_quickXSpin = new QDoubleSpinBox(); m_quickXSpin->setRange(-1000.0, 1000.0); m_quickXSpin->setDecimals(2); m_quickXSpin->setSuffix(" m");
    m_quickYSpin = new QDoubleSpinBox(); m_quickYSpin->setRange(-1000.0, 1000.0); m_quickYSpin->setDecimals(2); m_quickYSpin->setSuffix(" m");
    m_quickZSpin = new QDoubleSpinBox(); m_quickZSpin->setRange(-1000.0, 1000.0); m_quickZSpin->setDecimals(2); m_quickZSpin->setSuffix(" m");

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
    auto* inspScroll = new QScrollArea(this);
    inspScroll->setWidgetResizable(true);
    inspScroll->setFrameShape(QFrame::NoFrame);
    inspScroll->setWidget(leftContainer);
    m_inspectorDock->setMinimumWidth(320);
    m_inspectorDock->setWidget(inspScroll);
    addDockWidget(Qt::LeftDockWidgetArea, m_inspectorDock);

    // -------------------------------------------------------------
    // Dock Inferior: Tabelas de Resultados e Diagnóstico
    // -------------------------------------------------------------
    m_resultsDock = new QDockWidget(locale::GerenciadorTraducao::trText("Tabelas de Resultados e Diagnóstico Estrutural"), this);
    m_resultsDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    m_resultsTabs = new QTabWidget();

    // Aba 1: Deslocamentos Nodais
    m_dispTable = new QTableWidget();
    m_dispTable->setColumnCount(7);
    m_dispTable->setHorizontalHeaderLabels({locale::GerenciadorTraducao::trText("Nó"), "uX (mm)", "uY (mm)", "uZ (mm)", "θX (mrad)", "θY (mrad)", "θZ (mrad)"});
    m_dispTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_dispTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTabs->addTab(m_dispTable, locale::GerenciadorTraducao::trText("Deslocamentos Nodais"));

    // Aba 2: Reações de Apoio
    m_reacTable = new QTableWidget();
    m_reacTable->setColumnCount(7);
    m_reacTable->setHorizontalHeaderLabels({locale::GerenciadorTraducao::trText("Nó"), "Rx (kN)", "Ry (kN)", "Rz (kN)", "Mx (kNm)", "My (kNm)", "Mz (kNm)"});
    m_reacTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_reacTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTabs->addTab(m_reacTable, locale::GerenciadorTraducao::trText("Reações de Apoio"));

    // Aba 3: Esforços nas Barras
    m_forcesTable = new QTableWidget();
    m_forcesTable->setColumnCount(8);
    m_forcesTable->setHorizontalHeaderLabels({locale::GerenciadorTraducao::trText("Barra"), "N min/max (kN)", "Vy min/max (kN)", "Vz min/max (kN)", "Tx min/max (kNm)", "My min/max (kNm)", "Mz min/max (kNm)", locale::GerenciadorTraducao::trText("Flecha (mm)")});
    m_forcesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_forcesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTabs->addTab(m_forcesTable, locale::GerenciadorTraducao::trText("Esforços nas Barras"));

    // Aba 4: Esforços e Tensões 2D/3D (Casca/Sólido)
    m_continuumTable = new QTableWidget();
    m_continuumTable->setColumnCount(8);
    m_continuumTable->setHorizontalHeaderLabels({
        locale::GerenciadorTraducao::trText("Elemento"),
        locale::GerenciadorTraducao::trText("Tipo"),
        QStringLiteral("σ_vM (kPa)"),
        QStringLiteral("σ_1 (kPa)"),
        QStringLiteral("σ_3 (kPa)"),
        QStringLiteral("Nxx / σxx"),
        QStringLiteral("Nyy / σyy"),
        QStringLiteral("Mxx / τxz")
    });
    m_continuumTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_continuumTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTabs->addTab(m_continuumTable, locale::GerenciadorTraducao::trText("Esforços 2D/3D (Casca/Sólido)"));

    m_resultsDock->setWidget(m_resultsTabs);
    addDockWidget(Qt::BottomDockWidgetArea, m_resultsDock);
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
void MainWindow::loadExampleBuilding() {
    m_hasSolvedContext = false;
    m_model = StructuralModel::createBuildingFrameExample();
    applyDefaultExampleWorkPlane();
    m_viewport->setModel(&m_model);
    onModelModified();
    m_viewport->fitAll();
    onSolveStructure();
    showStatusMessage(locale::GerenciadorTraducao::trText("Exemplo 1 carregado: Pórtico Espacial de Edifício (2 Pavimentos)."));
}

void MainWindow::loadExampleCantilever() {
    m_hasSolvedContext = false;
    m_model = StructuralModel::createCantileverLFrameExample();
    applyDefaultExampleWorkPlane();
    m_viewport->setModel(&m_model);
    onModelModified();
    m_viewport->fitAll();
    onSolveStructure();
    showStatusMessage(locale::GerenciadorTraducao::trText("Exemplo 2 carregado: Balanço Espacial em L com Flexo-Torção."));
}

void MainWindow::loadExampleSpaceTruss() {
    m_hasSolvedContext = false;
    m_model = StructuralModel::createSpaceTrussExample();
    applyDefaultExampleWorkPlane();
    m_viewport->setModel(&m_model);
    onModelModified();
    m_viewport->fitAll();
    onSolveStructure();
    showStatusMessage(locale::GerenciadorTraducao::trText("Exemplo 3 carregado: Passarela Treliçada Espacial 3D."));
}

bool MainWindow::openProjectFileFromCommandLine(const QString& filePath) {
    if (filePath.trimmed().isEmpty()) {
        return false;
    }
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
        m_hasSolvedContext = false;
        m_model = std::move(importedModel);
        m_viewport->setModel(&m_model);
        onModelModified();
        m_viewport->fitAll();
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
        m_hasSolvedContext = false;
        m_model = std::move(importedModel);
        m_viewport->setModel(&m_model);
        onModelModified();
        m_viewport->fitAll();
        showStatusMessage(locale::GerenciadorTraducao::trText("Arquivo carregado com sucesso: %1").arg(filePath));
        return true;
    }

    StructuralModel loadedModel;
    if (!ProjectSerializer::loadFromFile(filePath, loadedModel)) {
        showStatusMessage(locale::GerenciadorTraducao::trText("Não foi possível carregar o arquivo especificado."));
        return false;
    }
    m_hasSolvedContext = false;
    m_model = std::move(loadedModel);
    m_viewport->setModel(&m_model);
    m_lastSolverResult = SolverResult();
    onModelModified();
    m_viewport->fitAll();
    showStatusMessage(locale::GerenciadorTraducao::trText("Arquivo carregado com sucesso: %1").arg(filePath));
    return true;
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
                m_hasSolvedContext = false;
                m_model.clear();
                m_lastSolverResult = SolverResult();
                onModelModified();
                m_viewport->update();
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
        QString(),
        locale::GerenciadorTraducao::trText("Arquivos OmniFrame (*.omniframe);;Todos os Arquivos (*.*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            if (ProjectSerializer::loadFromFile(path, m_model)) {
                m_hasSolvedContext = false;
                m_lastSolverResult = SolverResult();
                onModelModified();
                m_viewport->fitAll();
                showStatusMessage(locale::GerenciadorTraducao::trText("Arquivo carregado com sucesso: %1").arg(path));
            } else {
                m_windowManager->showModelessMessage(
                    QMessageBox::Critical,
                    locale::GerenciadorTraducao::trText("Erro"),
                    locale::GerenciadorTraducao::trText("Não foi possível carregar o arquivo especificado.")
                );
            }
        }
    );
}

void MainWindow::onSaveProject() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptSave,
        QFileDialog::AnyFile,
        locale::GerenciadorTraducao::trText("Salvar Arquivo OmniFrame"),
        QStringLiteral("modelo.omniframe"),
        locale::GerenciadorTraducao::trText("Arquivos OmniFrame (*.omniframe)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            if (ProjectSerializer::saveToFile(path, m_model)) {
                showStatusMessage(locale::GerenciadorTraducao::trText("Projeto salvo com sucesso: %1").arg(path));
            } else {
                m_windowManager->showModelessMessage(
                    QMessageBox::Critical,
                    locale::GerenciadorTraducao::trText("Erro"),
                    locale::GerenciadorTraducao::trText("Não foi possível salvar o arquivo.")
                );
            }
        }
    );
}

void MainWindow::onExportHtmlReport() {
    m_windowManager->showModelessFileDialog(
        QFileDialog::AcceptSave,
        QFileDialog::AnyFile,
        locale::GerenciadorTraducao::trText("Exportar Relatório HTML"),
        QStringLiteral("Relatorio_Estrutural.html"),
        locale::GerenciadorTraducao::trText("Documento HTML (*.html)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
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
        QStringLiteral("Relatorio_Estrutural.md"),
        locale::GerenciadorTraducao::trText("Documento Markdown (*.md)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
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
        QString(),
        locale::GerenciadorTraducao::trText("Desenhos CAD DXF (*.dxf);;Todos (*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
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
            m_hasSolvedContext = false;
            m_model = std::move(importedModel);
            m_viewport->setModel(&m_model);
            onModelModified();
            m_viewport->fitAll();
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
        QStringLiteral("Modelo_OmniFrame.dxf"),
        locale::GerenciadorTraducao::trText("Desenhos CAD DXF (*.dxf);;Todos (*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
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
        QString(),
        locale::GerenciadorTraducao::trText("Modelos BIM IFC (*.ifc);;Todos (*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
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
            m_hasSolvedContext = false;
            m_model = std::move(importedModel);
            m_viewport->setModel(&m_model);
            onModelModified();
            m_viewport->fitAll();
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
        QStringLiteral("Modelo_OmniFrame.ifc"),
        locale::GerenciadorTraducao::trText("Modelos BIM IFC (*.ifc);;Todos (*)"),
        [this](const QString& path) {
            if (path.isEmpty()) return;
            io::IfcGuidMap guidMap;
            io::IfcInteropReport rep;
            const kernel::MeshContainer* meshPtr =
                m_model.surfaceMesh().elements.empty() ? nullptr : &m_model.surfaceMesh();
            if (io::IfcInterop::exportToFile(path.toStdString(), m_model, meshPtr, &guidMap, &rep) && rep.success) {
                showStatusMessage(
                    locale::GerenciadorTraducao::trText("IFC4 exportado (%1 entidades STEP): %2")
                        .arg(rep.entitiesExported)
                        .arg(path));
            } else {
                m_windowManager->showModelessMessage(
                    QMessageBox::Critical,
                    locale::GerenciadorTraducao::trText("Erro ao Exportar IFC4"),
                    QString::fromStdString(rep.errorMessage));
            }
        }
    );
}

bool MainWindow::ensureStaticSolvedSync() {
    const bool hasAnyElements = !m_model.getElements().empty() || !m_model.surfaceMesh().elements.empty();
    if (m_lastSolverResult.success && hasAnyElements) {
        return true;
    }
    if (m_solveInProgress || !hasAnyElements) {
        return false;
    }
    const ModelValidation val = m_model.validate();
    if (!val.valid) {
        return false;
    }
    SolverResult res = FemSolver3D::solve(m_model);
    if (!res.success) {
        return false;
    }
    m_lastSolverResult = std::move(res);
    m_hasSolvedContext = true;
    updateResultsTables();
    updateInspector();
    if (m_viewport) m_viewport->update();
    if (m_analysisSuiteDialog) m_analysisSuiteDialog->refreshContext();
    return true;
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
    connect(dlg, &QDialog::accepted, this, [this, dlgPtr = QPointer<PdfReportOptionsDialog>(dlg)]() {
        if (!dlgPtr) return;
        io::PdfReportOptions opts = dlgPtr->collectOptionsAndSaveMetadata();
        if (dlgPtr->includeViewportCapture() && m_viewport) {
            opts.viewportSnapshot = m_viewport->grabFramebuffer();
        }
        if (m_analysisSuiteDialog) {
            opts.modalResult = m_analysisSuiteDialog->latestModalResult();
            opts.bucklingResult = m_analysisSuiteDialog->latestBucklingResult();
            opts.nonlinearResult = m_analysisSuiteDialog->latestNonlinearResult();
        }
        m_windowManager->showModelessFileDialog(
            QFileDialog::AcceptSave,
            QFileDialog::AnyFile,
            locale::GerenciadorTraducao::trText("Exportar Memorial Executivo (PDF A4)"),
            QStringLiteral("Memorial_Executivo_OmniFrame.pdf"),
            locale::GerenciadorTraducao::trText("Documentos PDF (*.pdf);;Todos (*)"),
            [this, opts](const QString& path) {
                if (path.isEmpty()) return;
                QString errMsg;
                QApplication::setOverrideCursor(Qt::WaitCursor);
                const bool ok = io::PdfReportGenerator::generateExecutivePdf(
                    path, m_model, m_lastSolverResult, opts, &errMsg);
                QApplication::restoreOverrideCursor();
                if (ok) {
                    showStatusMessage(locale::GerenciadorTraducao::trText("Memorial executivo PDF exportado: %1").arg(path));
                    m_windowManager->showModelessMessage(
                        QMessageBox::Information,
                        locale::GerenciadorTraducao::trText("Sucesso"),
                        locale::GerenciadorTraducao::trText("Memorial Executivo A4 PDF gerado com sucesso!"));
                } else {
                    m_windowManager->showModelessMessage(
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
    auto* dlg = new LoadCombinationsDialog(nullptr, &m_model, [this]() {
        onModelModified();
    });
    m_loadCombinationsDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
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
        [this]() { onModelModified(); });
    m_analysisSuiteDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onOpenPostControls() {
    if (m_postProcessControlDialog) {
        m_postProcessControlDialog->syncFromViewport();
        m_windowManager->showAndRaise(m_postProcessControlDialog.data());
        return;
    }
    auto* dlg = new PostProcessControlDialog(nullptr, m_viewport, [this]() -> bool {
        const bool solved = ensureStaticSolvedSync();
        const auto dcrMap = computeModelDcrMap();
        if (m_viewport) {
            m_viewport->setElementDcrMap(dcrMap);
        }
        if (m_nbrComplianceDialog) {
            m_nbrComplianceDialog->runAudit();
        }
        return solved && !dcrMap.empty();
    });
    m_postProcessControlDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
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
        [this]() { return ensureStaticSolvedSync(); });
    m_nbrComplianceDialog = dlg;
    m_windowManager->prepareModelessDialog(dlg, true, true);
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
    if (m_autoRecalculate && m_hasSolvedContext) {
        onModelModified();
    }
}

void MainWindow::onSolveStructure() {
    m_hasSolvedContext = true;
    runSolver(true);
}

bool MainWindow::runSolver(bool interactive) {
    if (m_solveInProgress) {
        m_pendingResolve = true;
        if (interactive) {
            m_pendingResolveInteractive = true;
            showStatusMessage(locale::GerenciadorTraducao::trText("Cálculo já está em andamento; aguarde a conclusão."));
        }
        return false;
    }

    m_solveInProgress = true;
    m_pendingResolve = false;
    m_pendingResolveInteractive = false;
    if (m_btnSolve) m_btnSolve->setEnabled(false);
    if (m_statusProgress) {
        m_statusProgress->setRange(0, 0);
        m_statusProgress->setVisible(true);
    }
    m_solverStatusLabel->setText(locale::GerenciadorTraducao::trText("Calculando matriz de rigidez global esparsa..."));

    struct AsyncSolvePayload {
        StructuralModel snapshot;
        SolverResult result;
        std::uint64_t revision = 0;
    };
    auto payload = std::make_shared<AsyncSolvePayload>();
    payload->snapshot = m_model;
    payload->revision = m_modelRevision;

    QThread* worker = QThread::create([payload]() {
        payload->result = FemSolver3D::solve(payload->snapshot);
    });
    m_activeSolverThread = worker;
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, payload, worker, interactive]() {
        m_solveInProgress = false;
        if (m_activeSolverThread == worker) {
            m_activeSolverThread = nullptr;
        }
        if (m_btnSolve) m_btnSolve->setEnabled(true);
        if (m_statusProgress) {
            m_statusProgress->setVisible(false);
            m_statusProgress->setRange(0, 1);
            m_statusProgress->setValue(0);
        }

        if (m_pendingResolve || payload->revision != m_modelRevision) {
            const bool rerunInteractive = m_pendingResolveInteractive;
            m_pendingResolve = false;
            m_pendingResolveInteractive = false;
            if ((m_autoRecalculate && m_hasSolvedContext) || rerunInteractive) {
                if (m_model.validate().valid) {
                    runSolver(rerunInteractive);
                    return;
                }
            }
            if (payload->revision != m_modelRevision) {
                showStatusMessage(locale::GerenciadorTraducao::trText("Modelo modificado durante o cálculo em segundo plano; execute F5 novamente."));
                updateResultsTables();
                if (QApplication::activePopupWidget() != nullptr) {
                    QTimer::singleShot(120, this, &MainWindow::updateInspector);
                } else {
                    updateInspector();
                }
                m_viewport->update();
                return;
            }
        }

        const auto& srcNodes = payload->snapshot.getNodes();
        const auto& srcElems = payload->snapshot.getElements();
        const auto& srcMesh = payload->snapshot.surfaceMesh();
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
            m_viewport->update();
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
        m_lastSolverResult = payload->result;

        if (m_lastSolverResult.success) {
            QString msg = locale::GerenciadorTraducao::trText("Cálculo MEF concluído com sucesso em %1 ms (%2 nós, %3 barras, %4 DOFs). Equilíbrio: %5")
                          .arg(m_lastSolverResult.executionTimeMs, 0, 'f', 1)
                          .arg(m_lastSolverResult.totalNodes)
                          .arg(m_lastSolverResult.totalElements)
                          .arg(m_lastSolverResult.totalDofs)
                          .arg(m_lastSolverResult.equilibrium.isBalanced
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
            if (interactive && m_viewport->activeDiagram() == DiagramType::None) {
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
            m_viewport->update();
            if (m_nbrComplianceDialog) m_nbrComplianceDialog->runAudit();
            if (m_postProcessControlDialog) m_postProcessControlDialog->syncFromViewport();
            return;
        }

        const QString translatedErr = translateCoreMessage(QString::fromStdString(m_lastSolverResult.errorMessage));
        m_solverStatusLabel->setText(locale::GerenciadorTraducao::trText("Erro no cálculo: %1").arg(translatedErr));
        updateResultsTables();
        if (QApplication::activePopupWidget() != nullptr) {
            QTimer::singleShot(120, this, &MainWindow::updateInspector);
        } else {
            updateInspector();
        }
        m_viewport->update();

        if (interactive) {
            m_windowManager->showModelessMessage(
                QMessageBox::Warning,
                locale::GerenciadorTraducao::trText("Erro no Solver"),
                translatedErr
            );
        }
    });
    worker->start();
    return true;
}

// Modos e Ferramentas
void MainWindow::onInteractionModeChanged(QAction* action) {
    if (action == m_actSelect) m_viewport->setInteractionMode(InteractionMode::Select);
    else if (action == m_actAddNode) m_viewport->setInteractionMode(InteractionMode::AddNode);
    else if (action == m_actAddElement) m_viewport->setInteractionMode(InteractionMode::AddElement);
    else if (action == m_actMeasure) m_viewport->setInteractionMode(InteractionMode::Measure);
    else if (action == m_actDelete) m_viewport->setInteractionMode(InteractionMode::Delete);
}

void MainWindow::onWorkPlaneChanged(int index) {
    auto plane = static_cast<WorkPlane>(m_planeCombo->itemData(index).toInt());
    m_viewport->setWorkPlane(plane);
}

void MainWindow::onPlaneOffsetChanged(double val) {
    m_viewport->setPlaneOffset(val);
}

void MainWindow::onSnapToggled(bool checked) {
    m_viewport->setSnapToGrid(checked);
}

void MainWindow::onGridStepChanged(double val) {
    m_viewport->setGridStep(val);
}

void MainWindow::onShowLocalAxesToggled(bool checked) {
    m_viewport->setShowLocalAxes(checked);
}

void MainWindow::onShowReactionsToggled(bool checked) {
    m_viewport->setShowReactions(checked);
}

void MainWindow::onDiagramChanged(int index) {
    auto diag = static_cast<DiagramType>(m_diagramCombo->itemData(index).toInt());
    m_viewport->setActiveDiagram(diag);
}

void MainWindow::onDiagramScaleChanged(int val) {
    m_viewport->setDiagramScale(val / 30.0);
    m_viewport->setDeformedScale(val * 1.5);
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
    updateInspector();
}

void MainWindow::onModelModified() {
    ++m_modelRevision;
    m_model.clearResults();
    m_lastSolverResult = SolverResult();
    if (m_viewport) {
        m_viewport->clearElementDcrMap();
        m_viewport->clearPostAnimation();
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

    if (m_autoRecalculate && m_hasSolvedContext) {
        if (m_solveInProgress) {
            m_pendingResolve = true;
            m_solverStatusLabel->setText(
                locale::GerenciadorTraducao::trText("Cálculo em andamento; aguarde ou recalcule após concluir (F5)."));
        } else {
            const ModelValidation val = m_model.validate();
            if (val.valid) {
                runSolver(false);
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
    } else {
        m_solverStatusLabel->setText(locale::GerenciadorTraducao::trText("Pronto para calcular."));
    }

    updateResultsTables();
    updateInspector();
    m_viewport->update();
}

void MainWindow::updateInspector() {
    using locale::GerenciadorTraducao;
    // Limpa conteúdo anterior do inspetor de forma segura para sinais em execução
    QLayoutItem* item = nullptr;
    while ((item = m_inspectorLayout->takeAt(0)) != nullptr) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    auto* contentWidget = new QWidget(m_inspectorLayout ? m_inspectorLayout->parentWidget() : nullptr);
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
                           .arg(csStr)
                           .arg(elem->distLoad.w.x, 0, 'f', 1)
                           .arg(elem->distLoad.w.y, 0, 'f', 1)
                           .arg(elem->distLoad.w.z, 0, 'f', 1);
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
    m_dispTable->setRowCount(0);
    m_reacTable->setRowCount(0);
    m_forcesTable->setRowCount(0);
    if (m_continuumTable) {
        m_continuumTable->setRowCount(0);
    }

    if (!m_lastSolverResult.success) {
        return;
    }

    const auto& nodes = m_model.getNodes();
    const auto& mesh = m_model.surfaceMesh();

    // Evita duplicar linhas de nós coincidentes entre barras 1D e malha 2D/3D acoplada
    std::vector<const kernel::MeshNode*> uniqueMeshNodes;
    uniqueMeshNodes.reserve(mesh.nodes.size());
    for (const auto& mn : mesh.nodes) {
        bool coincidesWithFrame = false;
        for (const auto& fn : nodes) {
            const double dx = fn.x() - mn.position.x();
            const double dy = fn.y() - mn.position.y();
            const double dz = fn.z() - mn.position.z();
            if ((dx * dx + dy * dy + dz * dz) <= 1e-12) {
                coincidesWithFrame = true;
                break;
            }
        }
        if (!coincidesWithFrame) {
            uniqueMeshNodes.push_back(&mn);
        }
    }

    // 1. Tabela de Deslocamentos Nodais
    m_dispTable->setRowCount(static_cast<int>(nodes.size() + uniqueMeshNodes.size()));

    for (int r = 0; r < static_cast<int>(nodes.size()); ++r) {
        const auto& n = nodes[r];
        m_dispTable->setItem(r, 0, new QTableWidgetItem(QString::number(n.id)));
        m_dispTable->setItem(r, 1, new QTableWidgetItem(QString::number(n.displacement[0] * 1000.0, 'f', 3)));
        m_dispTable->setItem(r, 2, new QTableWidgetItem(QString::number(n.displacement[1] * 1000.0, 'f', 3)));
        m_dispTable->setItem(r, 3, new QTableWidgetItem(QString::number(n.displacement[2] * 1000.0, 'f', 3)));
        m_dispTable->setItem(r, 4, new QTableWidgetItem(QString::number(n.displacement[3] * 1000.0, 'f', 3)));
        m_dispTable->setItem(r, 5, new QTableWidgetItem(QString::number(n.displacement[4] * 1000.0, 'f', 3)));
        m_dispTable->setItem(r, 6, new QTableWidgetItem(QString::number(n.displacement[5] * 1000.0, 'f', 3)));
    }
    for (int m = 0; m < static_cast<int>(uniqueMeshNodes.size()); ++m) {
        const int r = static_cast<int>(nodes.size()) + m;
        const auto* mn = uniqueMeshNodes[static_cast<std::size_t>(m)];
        const QString idLabel = nodes.empty() ? QString::number(mn->id) : QStringLiteral("M%1").arg(mn->id);
        m_dispTable->setItem(r, 0, new QTableWidgetItem(idLabel));
        for (int d = 0; d < 6; ++d) {
            m_dispTable->setItem(r, d + 1, new QTableWidgetItem(QString::number(mn->displacement[d] * 1000.0, 'f', 3)));
        }
    }

    // 2. Tabela de Reações de Apoio
    m_reacTable->setRowCount(0);
    int supportedCount = 0;
    for (const auto& n : nodes) {
        if (n.isSupported()) supportedCount++;
    }
    for (const auto* mn : uniqueMeshNodes) {
        if (mn->isSupported()) supportedCount++;
    }

    m_reacTable->setRowCount(supportedCount + 1); // +1 para linha de soma de equilíbrio
    int row = 0;
    for (const auto& n : nodes) {
        if (!n.isSupported()) continue;
        m_reacTable->setItem(row, 0, new QTableWidgetItem(QString::number(n.id)));
        m_reacTable->setItem(row, 1, new QTableWidgetItem(QString::number(n.reaction[0], 'f', 2)));
        m_reacTable->setItem(row, 2, new QTableWidgetItem(QString::number(n.reaction[1], 'f', 2)));
        m_reacTable->setItem(row, 3, new QTableWidgetItem(QString::number(n.reaction[2], 'f', 2)));
        m_reacTable->setItem(row, 4, new QTableWidgetItem(QString::number(n.reaction[3], 'f', 2)));
        m_reacTable->setItem(row, 5, new QTableWidgetItem(QString::number(n.reaction[4], 'f', 2)));
        m_reacTable->setItem(row, 6, new QTableWidgetItem(QString::number(n.reaction[5], 'f', 2)));
        row++;
    }
    for (const auto* mn : uniqueMeshNodes) {
        if (!mn->isSupported()) continue;
        const QString idLabel = nodes.empty() ? QString::number(mn->id) : QStringLiteral("M%1").arg(mn->id);
        m_reacTable->setItem(row, 0, new QTableWidgetItem(idLabel));
        for (int d = 0; d < 6; ++d) {
            m_reacTable->setItem(row, d + 1, new QTableWidgetItem(QString::number(mn->reaction[d], 'f', 2)));
        }
        row++;
    }

    // Linha de Soma Total
    auto* itemSum = new QTableWidgetItem(locale::GerenciadorTraducao::trText("Σ Reações"));
    m_reacTable->setItem(row, 0, itemSum);
    m_reacTable->setItem(row, 1, new QTableWidgetItem(QString::number(m_lastSolverResult.equilibrium.sumReactions.x, 'f', 2)));
    m_reacTable->setItem(row, 2, new QTableWidgetItem(QString::number(m_lastSolverResult.equilibrium.sumReactions.y, 'f', 2)));
    m_reacTable->setItem(row, 3, new QTableWidgetItem(QString::number(m_lastSolverResult.equilibrium.sumReactions.z, 'f', 2)));
    m_reacTable->setItem(row, 4, new QTableWidgetItem(QString::number(m_lastSolverResult.equilibrium.sumReactionMoments.x, 'f', 2)));
    m_reacTable->setItem(row, 5, new QTableWidgetItem(QString::number(m_lastSolverResult.equilibrium.sumReactionMoments.y, 'f', 2)));
    m_reacTable->setItem(row, 6, new QTableWidgetItem(QString::number(m_lastSolverResult.equilibrium.sumReactionMoments.z, 'f', 2)));
    refreshReactionSummaryRowTheme();

    // 3. Tabela de Esforços nas Barras
    m_forcesTable->setRowCount(0);
    m_forcesTable->setRowCount(static_cast<int>(m_lastSolverResult.elementExtremes.size()));
    for (int r = 0; r < static_cast<int>(m_lastSolverResult.elementExtremes.size()); ++r) {
        const auto& ext = m_lastSolverResult.elementExtremes[r];
        m_forcesTable->setItem(r, 0, new QTableWidgetItem(QString::number(ext.elementId)));
        m_forcesTable->setItem(r, 1, new QTableWidgetItem(QString("[%1; %2]").arg(ext.N_min, 0, 'f', 1).arg(ext.N_max, 0, 'f', 1)));
        m_forcesTable->setItem(r, 2, new QTableWidgetItem(QString("[%1; %2]").arg(ext.Vy_min, 0, 'f', 1).arg(ext.Vy_max, 0, 'f', 1)));
        m_forcesTable->setItem(r, 3, new QTableWidgetItem(QString("[%1; %2]").arg(ext.Vz_min, 0, 'f', 1).arg(ext.Vz_max, 0, 'f', 1)));
        m_forcesTable->setItem(r, 4, new QTableWidgetItem(QString("[%1; %2]").arg(ext.Tx_min, 0, 'f', 1).arg(ext.Tx_max, 0, 'f', 1)));
        m_forcesTable->setItem(r, 5, new QTableWidgetItem(QString("[%1; %2]").arg(ext.My_min, 0, 'f', 1).arg(ext.My_max, 0, 'f', 1)));
        m_forcesTable->setItem(r, 6, new QTableWidgetItem(QString("[%1; %2]").arg(ext.Mz_min, 0, 'f', 1).arg(ext.Mz_max, 0, 'f', 1)));
        m_forcesTable->setItem(r, 7, new QTableWidgetItem(QString::number(ext.maxDeflection * 1000.0, 'f', 2)));
    }

    // 4. Tabela de Esforços e Tensões 2D/3D (Casca / Sólido)
    if (m_continuumTable) {
        const auto& cont = m_lastSolverResult.continuumElementResults;
        m_continuumTable->setRowCount(static_cast<int>(cont.size()));
        for (int r = 0; r < static_cast<int>(cont.size()); ++r) {
            const auto& ce = cont[static_cast<std::size_t>(r)];
            m_continuumTable->setItem(r, 0, new QTableWidgetItem(QString::number(ce.elementId)));
            m_continuumTable->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(ce.cellTypeName)));
            m_continuumTable->setItem(r, 2, new QTableWidgetItem(QString::number(ce.vonMisesStress, 'f', 2)));
            m_continuumTable->setItem(r, 3, new QTableWidgetItem(QString::number(ce.maxPrincipalStress, 'f', 2)));
            m_continuumTable->setItem(r, 4, new QTableWidgetItem(QString::number(ce.minPrincipalStress, 'f', 2)));
            if (ce.isShell) {
                m_continuumTable->setItem(r, 5, new QTableWidgetItem(QString::number(ce.membraneResultants[0], 'f', 2) + QStringLiteral(" kN/m")));
                m_continuumTable->setItem(r, 6, new QTableWidgetItem(QString::number(ce.membraneResultants[1], 'f', 2) + QStringLiteral(" kN/m")));
                m_continuumTable->setItem(r, 7, new QTableWidgetItem(QString::number(ce.bendingResultants[0], 'f', 3) + QStringLiteral(" kNm/m")));
            } else {
                m_continuumTable->setItem(r, 5, new QTableWidgetItem(QString::number(ce.stressVoigt[0], 'f', 2) + QStringLiteral(" kPa")));
                m_continuumTable->setItem(r, 6, new QTableWidgetItem(QString::number(ce.stressVoigt[1], 'f', 2) + QStringLiteral(" kPa")));
                m_continuumTable->setItem(r, 7, new QTableWidgetItem(QString::number(ce.stressVoigt[4], 'f', 2) + QStringLiteral(" kPa")));
            }
        }
    }
}

void MainWindow::updateCursorCoordinates(double x, double y, double z) {
    m_cursorCoordLabel->setText(QString("X: %1 m | Y: %2 m | Z: %3 m")
                                .arg(x, 6, 'f', 2)
                                .arg(y, 6, 'f', 2)
                                .arg(z, 6, 'f', 2));
}

void MainWindow::showStatusMessage(const QString& msg) {
    statusBar()->showMessage(msg, 5000);
}

void MainWindow::onManual() {
    openManagedInfoDialog(
        m_windowManager, m_manualDialog, true,
        []() { return new ui::ManualDialog(nullptr); }
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
    dlg->positionHudNearMainWindow();
    m_windowManager->showAndRaise(dlg);
}

void MainWindow::onAbout() {
    openManagedInfoDialog(
        m_windowManager, m_aboutDialog, true,
        []() { return new AboutDialog(nullptr); }
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

void MainWindow::onUninstallWindowsCliIntegration() {
    using locale::GerenciadorTraducao;
    QString details;
    os::uninstallWindowsCliIntegration(QString(), &details);
    m_windowManager->showModelessMessage(
        QMessageBox::Information,
        GerenciadorTraducao::trText("Integração CLI Removida"),
        GerenciadorTraducao::trText("As entradas de usuário (HKCU PATH, App Paths e associação .omniframe) deste diretório foram removidas com segurança.\n\n%1")
            .arg(details));
    showStatusMessage(GerenciadorTraducao::trText("Registro HKCU da CLI removido."));
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
        .arg(st.installDirectory)
        .arg(st.cliBinaryExists ? yesStr : noStr)
        .arg(st.installDirInUserPath ? yesStr : noStr)
        .arg(st.installDirInSystemPath ? yesStr : noStr)
        .arg(st.appPathsCliRegistered ? yesStr : noStr)
        .arg(st.fileAssocRegistered ? yesStr : noStr)
        .arg(st.isReadyForCmdAndPowerShell() ? yesStr : noStr);

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
    saveRenderSettings(settings);
    QSurfaceFormat::setDefaultFormat(buildPreferredSurfaceFormat(settings));
    m_renderSettingsSnapshot = settings;
    if (m_viewport) {
        m_viewport->setRenderQualitySettings(settings);
    }
    syncRenderSettingsMenuState();
    if (m_renderSettingsDialog && m_viewport) {
        m_renderSettingsDialog->syncSettings(settings, m_viewport->graphicsCapabilities());
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
    if (m_renderSettingsDialog && m_viewport) {
        m_renderSettingsDialog->syncSettings(m_viewport->renderQualitySettings(), m_viewport->graphicsCapabilities());
    }
    showStatusMessage(theme == UiTheme::Light
        ? locale::GerenciadorTraducao::trText("Tema claro aplicado em toda a interface.")
        : locale::GerenciadorTraducao::trText("Tema escuro aplicado em toda a interface."));
}

void MainWindow::onChangeLanguage(const QString& langCode) {
    locale::GerenciadorTraducao::instance().definir_idioma(langCode);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
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
    setWindowTitle(GerenciadorTraducao::trText("OmniFrame 3D - Análise Estrutural Espacial"));

    // Menus
    if (m_fileMenu) m_fileMenu->setTitle(GerenciadorTraducao::trText("&Arquivo"));
    if (m_actNewProject) m_actNewProject->setText(GerenciadorTraducao::trText("Novo Projeto"));
    if (m_actOpenProject) m_actOpenProject->setText(GerenciadorTraducao::trText("Abrir (.omniframe)..."));
    if (m_actSaveProject) m_actSaveProject->setText(GerenciadorTraducao::trText("Salvar (.omniframe)..."));
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

    if (m_themeMenu) m_themeMenu->setTitle(GerenciadorTraducao::trText("&Tema"));
    if (m_actGuiThemeDark) m_actGuiThemeDark->setText(GerenciadorTraducao::trText("Escuro"));
    if (m_actGuiThemeLight) m_actGuiThemeLight->setText(GerenciadorTraducao::trText("Claro"));
    syncThemeMenuState();

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
    if (m_entryGroup) m_entryGroup->setTitle(GerenciadorTraducao::trText("Adicionar Nó por Coordenadas"));
    if (m_btnAddNode) m_btnAddNode->setText(GerenciadorTraducao::trText("Adicionar Nó"));
    if (m_inspGroup) m_inspGroup->setTitle(GerenciadorTraducao::trText("Elemento / Nó Selecionado"));

    if (m_resultsDock) m_resultsDock->setWindowTitle(GerenciadorTraducao::trText("Tabelas de Resultados e Diagnóstico Estrutural"));
    if (m_resultsTabs) {
        m_resultsTabs->setTabText(0, GerenciadorTraducao::trText("Deslocamentos Nodais"));
        m_resultsTabs->setTabText(1, GerenciadorTraducao::trText("Reações de Apoio"));
        m_resultsTabs->setTabText(2, GerenciadorTraducao::trText("Esforços nas Barras"));
        if (m_resultsTabs->count() > 3) {
            m_resultsTabs->setTabText(3, GerenciadorTraducao::trText("Esforços 2D/3D (Casca/Sólido)"));
        }
    }
    if (m_dispTable) {
        m_dispTable->setHorizontalHeaderLabels({GerenciadorTraducao::trText("Nó"), "uX (mm)", "uY (mm)", "uZ (mm)", "θX (mrad)", "θY (mrad)", "θZ (mrad)"});
    }
    if (m_reacTable) {
        m_reacTable->setHorizontalHeaderLabels({GerenciadorTraducao::trText("Nó"), "Rx (kN)", "Ry (kN)", "Rz (kN)", "Mx (kNm)", "My (kNm)", "Mz (kNm)"});
    }
    if (m_forcesTable) {
        m_forcesTable->setHorizontalHeaderLabels({GerenciadorTraducao::trText("Barra"), "N min/max (kN)", "Vy min/max (kN)", "Vz min/max (kN)", "Tx min/max (kNm)", "My min/max (kNm)", "Mz min/max (kNm)", GerenciadorTraducao::trText("Flecha (mm)")});
    }
    if (m_continuumTable) {
        m_continuumTable->setHorizontalHeaderLabels({
            GerenciadorTraducao::trText("Elemento"),
            GerenciadorTraducao::trText("Tipo"),
            QStringLiteral("σ_vM (kPa)"),
            QStringLiteral("σ_1 (kPa)"),
            QStringLiteral("σ_3 (kPa)"),
            QStringLiteral("Nxx / σxx"),
            QStringLiteral("Nyy / σyy"),
            QStringLiteral("Mxx / τxz")
        });
    }

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
                .arg(m_lastSolverResult.executionTimeMs, 0, 'f', 1)
                .arg(m_lastSolverResult.totalNodes)
                .arg(m_lastSolverResult.totalElements)
                .arg(m_lastSolverResult.totalDofs)
                .arg(m_lastSolverResult.equilibrium.isBalanced
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
        } else {
            m_solverStatusLabel->setText(GerenciadorTraducao::trText("Pronto para calcular."));
        }
    }

    if (m_reacTable && m_reacTable->rowCount() > 0 && !m_lastSolverResult.success) {
        // Keep idle cleanly
    } else if (m_reacTable && m_reacTable->rowCount() > 0) {
        const int lastRow = m_reacTable->rowCount() - 1;
        if (auto* item = m_reacTable->item(lastRow, 0)) {
            item->setText(GerenciadorTraducao::trText("Σ Reações"));
        }
    }

    updateInspector();
    if (m_viewport) {
        m_viewport->update();
    }
}

} // namespace omniframe
