#pragma once

#include "ResultTableConfigurator.hpp"
#include "ThemeManager.hpp"
#include "core/StructuralModel.hpp"
#include "io/ProjectRecoveryStore.hpp"
#include "solver/FemSolver3D.hpp"
#include "render/Viewport3D.hpp"

#include <QMainWindow>
#include <QActionGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QMessageBox>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QLabel>
#include <QLineEdit>
#include <QDockWidget>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <cstdint>
#include <unordered_map>

class QCloseEvent;
class QDialog;
class QEvent;
class QGroupBox;
class QMenu;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QToolBar;

namespace omniframe {

class AboutDialog;
class AnalysisSuiteDialog;
class DistributedLoadDialog;
class GuidedSessionDialog;
class LoadCombinationsDialog;
class MaterialCatalogDialog;
class MaterialDialog;
class MeshGeneratorDialog;
class NbrComplianceDialog;
class NodalLoadDialog;
class PdfReportOptionsDialog;
class PostProcessControlDialog;
class ReleasesDialog;
class RenderSettingsDialog;
class RollAngleDialog;
class SectionCatalogDialog;
class SectionDialog;
class StartupSessionDialog;
class SupportDialog;
class ThermalLoadDialog;
class WindowZOrderManager;

namespace ui {
class ManualDialog;
class ResultTableConfigurator;
}

class DockTitleBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class GuidedSessionDialog;

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadExampleBuilding();
    void loadExampleCantilever();
    void loadExampleSpaceTruss();
    bool openProjectFileFromCommandLine(const QString& filePath);

    WindowZOrderManager* windowZOrderManager() const { return m_windowManager; }
    ui::ResultTableConfigurator* resultTableConfigurator() const { return m_tableConfigurator; }
    GuidedSessionDialog* guidedSessionDialog() const;

    Q_INVOKABLE void setInspectorDockCollapsed(bool collapsed);
    Q_INVOKABLE void setResultsDockCollapsed(bool collapsed);
    bool isInspectorDockCollapsed() const { return m_inspectorCollapsed; }
    bool isResultsDockCollapsed() const { return m_resultsCollapsed; }

    void setResultsLayoutCount(int count);
    int resultsLayoutCount() const { return m_resultsLayoutCount; }
    void setResultsLayoutTableVisible(int layoutIndex, ui::ResultTableKind kind, bool visible);
    bool isResultsLayoutTableVisible(int layoutIndex, ui::ResultTableKind kind) const;
    void resetResultsLayoutsToDefault();
    void saveResultsPanelSettings() const;
    bool loadResultsPanelSettings();
    static QString appLocalDataDirectory();
    static QString userPreferencesFilePath();
    void saveUserPreferences() const;
    bool loadUserPreferences();
    void restoreAllDefaultSettings();
    bool preferredStartMaximized() const { return m_preferredStartMaximized; }
    QSplitter* resultsSplitter() const { return m_resultsSplitter; }
    QTabWidget* resultsLayoutTabWidget(int layoutIndex) const;
    bool hasUnsavedEdits() const { return m_hasUnsavedEdits; }
    QString currentProjectFilePath() const { return m_currentProjectFilePath; }
    bool saveProjectToPath(const QString& path);
    bool recoverPreviousSession();
    io::StartupSessionBehavior startupSessionBehavior() const { return m_startupSessionBehavior; }
    bool confirmUnsavedOnClose() const { return m_confirmUnsavedOnClose; }
    void setStartupSessionBehavior(io::StartupSessionBehavior behavior);
    void setConfirmUnsavedOnClose(bool enabled);

private slots:
    // Ações de Arquivo
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onRecoverSession();
    void onRequestQuit();
    void onImportDxf();
    void onExportDxf();
    void onImportIfc();
    void onExportIfc();
    void onExportPdfReport();
    void onExportHtmlReport();
    void onExportMarkdownReport();

    // Ações de Edição e Solver
    void onUndo();
    void onRedo();
    void onAutoRecalculateToggled(bool checked);
    void onSolveStructure();

    // Ferramentas Avançadas de Engenharia e Pós-Processamento
    void onOpenLoadCombinations();
    void onOpenMeshGenerator();
    void onOpenAnalysisSuite();
    void onOpenPostControls();
    void onOpenNbrCompliance();

    // Ferramentas e Modos
    void onInteractionModeChanged(QAction* action);
    void onWorkPlaneChanged(int index);
    void onPlaneOffsetChanged(double val);
    void onSnapToggled(bool checked);
    void onGridStepChanged(double val);
    void onShowLocalAxesToggled(bool checked);
    void onShowReactionsToggled(bool checked);
    void onDiagramChanged(int index);
    void onDiagramScaleChanged(int val);

    // Diálogos de Atribuição de Engenharia
    void onAssignMaterials();
    void onAssignSections();
    void onAssignSupports();
    void onAssignReleases();
    void onAssignNodalLoads();
    void onAssignDistributedLoads();
    void onAssignThermalLoads();
    void onAssignRollAngle();

    // Entrada Rápida de Coordenadas
    void onQuickAddNode();

    // Sincronização de UI
    void onSelectionChanged();
    void onModelModified();
    void onProjectMetadataOrOptionsModified();
    void updateInspector();
    void updateResultsTables();
    void updateCursorCoordinates(double x, double y, double z);
    void showStatusMessage(const QString& msg);
    void onManual();
    void onGuidedSession();
    void onAbout();
    void onApplyRenderPreset(QualityPreset preset);
    void onOpenRenderSettingsDialog();
    void onRestoreDefaultRenderSettings();
    void onSaveAllSettingsNow();
    void onReloadSavedSettings();
    void onRestoreAllDefaultSettings();
    void onChangeGuiTheme(UiTheme theme);
    void onChangeUiStyleMode(UiStyleMode mode);
    void onChangeLanguage(const QString& langCode);
    void onChangeStartupSessionBehavior(io::StartupSessionBehavior behavior);
    void onToggleConfirmUnsavedOnClose(bool checked);
    void onOpenStartupSessionDialog();
    void onOpenLogDirectory();
    void onInstallWindowsCliIntegration();
    void onUninstallWindowsCliIntegration();
    void onCheckWindowsCliIntegration();
    void onOpenCliTerminalCmd();
    void onOpenCliTerminalPowerShell();
    void retranslateUi();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void applyTheme();
    void syncThemeMenuState();
    void syncUiStyleMenuState();
    void syncStartupSessionMenuState();
    void refreshReactionSummaryRowTheme();
    bool runSolver(bool interactive);
    bool ensureStaticSolvedSync();
    std::unordered_map<ElementId, double> computeModelDcrMap() const;
    void updateUndoRedoActions();
    void setupMenus();
    void applyAndSaveRenderSettings(const RenderQualitySettings& settings);
    void syncRenderSettingsMenuState();
    void setupToolbars();
    void setupDocks();
    void applyInspectorCollapsedState();
    void handleInspectorDockTopLevelChanged(bool floating);
    void handleResultsDockTopLevelChanged(bool floating);
    void initDefaultResultsLayoutAssignments();
    QJsonObject serializeResultsPanelJson() const;
    bool deserializeResultsPanelJson(const QJsonObject& root);
    void rebuildResultsLayoutsUi();
    void syncResultsLayoutsMenuState();
    void syncExtraLayoutTables();
    void setupStatusBar();
    void applyDefaultExampleWorkPlane();
    void translateComboPreservingData();
    void bindPersistentDialog(const QString& dialogKey, QDialog* dlg, bool persistControls = true);
    void capturePersistentDialogState(const QString& dialogKey, const QDialog* dlg, bool persistControls = true) const;
    void restorePersistentDialogState(const QString& dialogKey, QDialog* dlg, bool persistControls = true);
    void captureAllOpenDialogsState() const;
    QString initialFileDialogPath(const QString& defaultFileName = QString()) const;
    void rememberFileDialogPath(const QString& filePath) const;
    void markProjectDirty();
    void clearDirtyState(bool clearRecoverySnapshot = true);
    void persistCurrentSessionState();
    void flushRecoverySnapshotIfDirty();
    void updateWindowTitleAndFileActions();
    void checkAndOfferStartupRecovery();
    void updateRecoveryPromptBoxTexts();
    void promptUnsavedChangesBeforeClose();
    void updateCloseConfirmBoxTexts();
    QString formatRecoveryMessage(const io::ProjectRecoveryInfo& info,
                                  QString& outAcceptButtonText,
                                  QString& outRejectButtonText) const;
    void loadPredefinedExample(io::SessionSourceKind kind, StructuralModel model, const QString& statusMsg);
    void refreshAfterModelLoad(bool markDirty, bool fitViewport);

    QString translateCoreMessage(const QString& rawMsg) const;

    StructuralModel m_model;
    SolverResult m_lastSolverResult;
    bool m_hasSolvedContext = false;
    bool m_autoRecalculate = true;

    Viewport3D* m_viewport = nullptr;

    // Ações de Modo
    QActionGroup* m_modeActionGroup = nullptr;
    QAction* m_actSelect = nullptr;
    QAction* m_actAddNode = nullptr;
    QAction* m_actAddElement = nullptr;
    QAction* m_actMeasure = nullptr;
    QAction* m_actDelete = nullptr;

    // Controles de Barra de Ferramentas
    QComboBox* m_planeCombo = nullptr;
    QDoubleSpinBox* m_planeOffsetSpin = nullptr;
    QAction* m_actSnap = nullptr;
    QDoubleSpinBox* m_gridStepSpin = nullptr;
    QAction* m_actLocalAxes = nullptr;
    QAction* m_actReactions = nullptr;
    QComboBox* m_diagramCombo = nullptr;
    QSlider* m_scaleSlider = nullptr;

    // Dock Esquerdo (Inspetor)
    QDockWidget* m_inspectorDock = nullptr;
    DockTitleBar* m_inspectorTitleBar = nullptr;
    QScrollArea* m_inspectorScroll = nullptr;
    bool m_inspectorCollapsed = false;
    int m_inspectorSavedWidth = 320;
    int m_inspectorSavedFloatingHeight = 520;
    QGroupBox* m_entryGroup = nullptr;
    QGroupBox* m_inspGroup = nullptr;
    QPushButton* m_btnAddNode = nullptr;
    QPushButton* m_btnSolve = nullptr;
    QMenu* m_fileMenu = nullptr;
    QMenu* m_editMenu = nullptr;
    QMenu* m_exMenu = nullptr;
    QMenu* m_structMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QMenu* m_settingsMenu = nullptr;
    QMenu* m_qualityPresetMenu = nullptr;
    QMenu* m_resScaleMenu = nullptr;
    QMenu* m_msaaMenu = nullptr;
    QMenu* m_definitionMenu = nullptr;
    QMenu* m_activePlaneAppearanceMenu = nullptr;
    QMenu* m_themeMenu = nullptr;
    QMenu* m_uiStyleMenu = nullptr;
    QMenu* m_langMenu = nullptr;
    QMenu* m_cliWindowsMenu = nullptr;
    QMenu* m_helpMenu = nullptr;
    QToolBar* m_tb1 = nullptr;
    QToolBar* m_tb2 = nullptr;
    QLabel* m_planeLabel = nullptr;
    QLabel* m_offsetLabel = nullptr;
    QLabel* m_stepLabel = nullptr;
    QLabel* m_diagLabel = nullptr;
    QLabel* m_scaleLabel = nullptr;
    QAction* m_actNewProject = nullptr;
    QAction* m_actOpenProject = nullptr;
    QAction* m_actSaveProject = nullptr;
    QAction* m_actSaveProjectAs = nullptr;
    QAction* m_actRecoverSession = nullptr;
    QAction* m_actImportDxf = nullptr;
    QAction* m_actExportDxf = nullptr;
    QAction* m_actImportIfc = nullptr;
    QAction* m_actExportIfc = nullptr;
    QAction* m_actExportPdf = nullptr;
    QAction* m_actExportHtml = nullptr;
    QAction* m_actExportMd = nullptr;
    QAction* m_actQuit = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QAction* m_actEx1 = nullptr;
    QAction* m_actEx2 = nullptr;
    QAction* m_actEx3 = nullptr;
    QAction* m_actMaterials = nullptr;
    QAction* m_actSections = nullptr;
    QAction* m_actSupports = nullptr;
    QAction* m_actReleases = nullptr;
    QAction* m_actNodalLoads = nullptr;
    QAction* m_actDistLoads = nullptr;
    QAction* m_actThermalLoads = nullptr;
    QAction* m_actRollAngle = nullptr;
    QAction* m_actLoadCombos = nullptr;
    QAction* m_actMeshGen = nullptr;
    QAction* m_actAnalysisSuite = nullptr;
    QAction* m_actPostControls = nullptr;
    QAction* m_actNbrAudit = nullptr;
    QAction* m_actAutoRecalc = nullptr;
    QAction* m_actSolveMenu = nullptr;
    QAction* m_actViewIsoMenu = nullptr;
    QAction* m_actViewFrontMenu = nullptr;
    QAction* m_actViewTopMenu = nullptr;
    QAction* m_actViewSideMenu = nullptr;
    QAction* m_actViewFitMenu = nullptr;
    QAction* m_actManual = nullptr;
    QAction* m_actGuidedSession = nullptr;
    QAction* m_actAbout = nullptr;
    QAction* m_actCliInstallWin = nullptr;
    QAction* m_actCliUninstallWin = nullptr;
    QAction* m_actCliCheckWin = nullptr;
    QAction* m_actCliOpenCmd = nullptr;
    QAction* m_actCliOpenPowerShell = nullptr;

    WindowZOrderManager* m_windowManager = nullptr;
    RenderQualitySettings m_renderSettingsSnapshot;
    QPointer<ui::ManualDialog> m_manualDialog;
    QPointer<GuidedSessionDialog> m_guidedSessionDialog;
    QPointer<AboutDialog> m_aboutDialog;
    QPointer<RenderSettingsDialog> m_renderSettingsDialog;
    QPointer<MaterialCatalogDialog> m_materialCatalogDialog;
    QPointer<SectionCatalogDialog> m_sectionCatalogDialog;
    QPointer<SupportDialog> m_supportDialog;
    QPointer<ReleasesDialog> m_releasesDialog;
    QPointer<NodalLoadDialog> m_nodalLoadDialog;
    QPointer<DistributedLoadDialog> m_distLoadDialog;
    QPointer<ThermalLoadDialog> m_thermalLoadDialog;
    QPointer<RollAngleDialog> m_rollAngleDialog;
    QPointer<AnalysisSuiteDialog> m_analysisSuiteDialog;
    QPointer<LoadCombinationsDialog> m_loadCombinationsDialog;
    QPointer<MeshGeneratorDialog> m_meshGeneratorDialog;
    QPointer<PostProcessControlDialog> m_postProcessControlDialog;
    QPointer<NbrComplianceDialog> m_nbrComplianceDialog;
    QPointer<PdfReportOptionsDialog> m_pdfOptionsDialog;
    QPointer<StartupSessionDialog> m_startupSessionDialog;

    // Ações do menu Configurações (Resolução, Qualidade, Plano Ativo e Idioma)
    QActionGroup* m_presetActionGroup = nullptr;
    QAction* m_actPresetAutoMax = nullptr;
    QAction* m_actPresetUltra = nullptr;
    QAction* m_actPresetHigh = nullptr;
    QAction* m_actPresetBalanced = nullptr;
    QAction* m_actPresetPerf = nullptr;

    QActionGroup* m_resScaleActionGroup = nullptr;
    QAction* m_actScaleAuto = nullptr;
    QAction* m_actScale75 = nullptr;
    QAction* m_actScale100 = nullptr;
    QAction* m_actScale125 = nullptr;
    QAction* m_actScale150 = nullptr;
    QAction* m_actScale200 = nullptr;

    QActionGroup* m_msaaActionGroup = nullptr;
    QAction* m_actMsaaAuto = nullptr;
    QAction* m_actMsaaOff = nullptr;
    QAction* m_actMsaa2x = nullptr;
    QAction* m_actMsaa4x = nullptr;
    QAction* m_actMsaa8x = nullptr;
    QAction* m_actMsaa16x = nullptr;

    QAction* m_actShaderLineAa = nullptr;
    QActionGroup* m_filterActionGroup = nullptr;
    QAction* m_actFilterCatmullSharpen = nullptr;
    QAction* m_actFilterBicubic = nullptr;
    QAction* m_actFilterBilinear = nullptr;

    QActionGroup* m_lineWidthActionGroup = nullptr;
    QAction* m_actLineThin = nullptr;
    QAction* m_actLineBalanced = nullptr;
    QAction* m_actLineCrisp = nullptr;
    QAction* m_actLineBold = nullptr;

    QActionGroup* m_planeThemeActionGroup = nullptr;
    QAction* m_actThemeObsidian = nullptr;
    QAction* m_actThemePureNight = nullptr;
    QAction* m_actThemeGraphite = nullptr;
    QAction* m_actThemeCleanLight = nullptr;
    QAction* m_actPlaneSurface = nullptr;
    QAction* m_actResolutionHud = nullptr;

    QAction* m_actVsync = nullptr;
    QAction* m_actAdvancedRenderSettings = nullptr;
    QAction* m_actRestoreRenderDefaults = nullptr;

    ActivePlaneTheme m_lastDarkPlaneTheme = ActivePlaneTheme::ObsidianDark;
    QActionGroup* m_guiThemeActionGroup = nullptr;
    QAction* m_actGuiThemeDark = nullptr;
    QAction* m_actGuiThemeLight = nullptr;

    QActionGroup* m_uiStyleActionGroup = nullptr;
    QAction* m_actUiStyleNative = nullptr;
    QAction* m_actUiStyleCustom = nullptr;

    QActionGroup* m_langActionGroup = nullptr;
    QAction* m_actLangPtBr = nullptr;
    QAction* m_actLangEnUs = nullptr;
    QAction* m_actTbMaterials = nullptr;
    QAction* m_actTbSections = nullptr;
    QAction* m_actTbSupports = nullptr;
    QAction* m_actTbReleases = nullptr;
    QAction* m_actTbNodalLoads = nullptr;
    QAction* m_actTbDistLoads = nullptr;
    QAction* m_actTbThermalLoads = nullptr;
    QAction* m_actTbRollAngle = nullptr;
    QAction* m_actTbCombos = nullptr;
    QAction* m_actTbMesh = nullptr;
    QAction* m_actTbAdvAnalysis = nullptr;
    QAction* m_actTbPostCtrl = nullptr;
    QAction* m_actTbNbrAudit = nullptr;
    QAction* m_actViewIsoTb = nullptr;
    QAction* m_actViewFrontTb = nullptr;
    QAction* m_actViewTopTb = nullptr;
    QAction* m_actViewSideTb = nullptr;
    QAction* m_actViewFitTb = nullptr;
    QDoubleSpinBox* m_quickXSpin = nullptr;
    QDoubleSpinBox* m_quickYSpin = nullptr;
    QDoubleSpinBox* m_quickZSpin = nullptr;
    QPointer<QWidget> m_inspectorContent;
    QVBoxLayout* m_inspectorLayout = nullptr;

    // Dock Inferior (Tabelas de Resultados)
    QDockWidget* m_resultsDock = nullptr;
    DockTitleBar* m_resultsTitleBar = nullptr;
    bool m_resultsCollapsed = false;
    int m_resultsSavedDockedHeight = 200;
    int m_resultsSavedFloatingHeight = 240;
    QTabWidget* m_resultsTabs = nullptr;
    QTableWidget* m_dispTable = nullptr;
    QTableWidget* m_reacTable = nullptr;
    QTableWidget* m_forcesTable = nullptr;
    QTableWidget* m_continuumTable = nullptr;
    ui::ResultTableConfigurator* m_tableConfigurator = nullptr;

    // Labels da Barra de Status
    QLabel* m_cursorCoordLabel = nullptr;
    QLabel* m_modelStatsLabel = nullptr;
    QLabel* m_solverStatusLabel = nullptr;
    QProgressBar* m_statusProgress = nullptr;
    bool m_updatingDockCollapse = false;
    bool m_solveInProgress = false;
    std::uint64_t m_modelRevision = 0;
    bool m_pendingResolve = false;
    bool m_pendingResolveInteractive = false;
    QPointer<QThread> m_activeSolverThread;

    // Layouts múltiplos do Painel de Tabelas de Resultados
    QMenu* m_resultsLayoutsMenu = nullptr;
    QActionGroup* m_resultsLayoutCountGroup = nullptr;
    std::array<QAction*, 4> m_actResultsLayoutCount{};
    std::array<QMenu*, 4> m_resultsLayoutConfigMenus{};
    std::array<std::array<QAction*, 4>, 4> m_actResultsLayoutTables{};
    std::array<QAction*, 4> m_actResultsLayoutSelectAll{};
    QAction* m_actResetResultsLayouts = nullptr;
    QSplitter* m_resultsSplitter = nullptr;
    QWidget* m_hiddenTablesHost = nullptr;
    std::array<QTabWidget*, 4> m_resultsLayoutTabWidgets{};
    std::array<std::array<QTableWidget*, 4>, 4> m_extraLayoutTables{};
    int m_resultsLayoutCount = 1;
    std::array<std::array<std::array<bool, 4>, 4>, 4> m_resultsModeTableAssign{};
    mutable std::array<QList<int>, 4> m_resultsSplitterSizesByMode{};
    mutable std::array<std::array<int, 4>, 4> m_resultsActiveTabByMode{};
    bool m_rebuildingOrLoadingResultsSettings = false;

    // Persistência universal de preferências e janelas auxiliares
    QMenu* m_preferencesPersistenceMenu = nullptr;
    QAction* m_actSaveAllPreferences = nullptr;
    QAction* m_actReloadSavedPreferences = nullptr;
    QAction* m_actRestoreAllDefaults = nullptr;
    QMenu* m_startupSessionMenu = nullptr;
    QActionGroup* m_startupBehaviorGroup = nullptr;
    QAction* m_actStartupAsk = nullptr;
    QAction* m_actStartupAutoLoad = nullptr;
    QAction* m_actStartupNewProject = nullptr;
    QAction* m_actConfirmUnsavedOnClose = nullptr;
    QAction* m_actOpenStartupSessionDialog = nullptr;
    QAction* m_actOpenLogDirectory = nullptr;
    io::StartupSessionBehavior m_startupSessionBehavior = io::StartupSessionBehavior::AskOnStartup;
    bool m_confirmUnsavedOnClose = true;
    mutable QJsonObject m_savedDialogsStateJson;
    mutable QString m_lastFileDialogDir;
    bool m_hasLoadedCustomDiagram = false;
    bool m_preferredStartMaximized = true;

    // Proteção de edições do projeto e recuperação contra fechamento inadvertido
    io::SessionSourceKind m_sessionSourceKind = io::SessionSourceKind::Empty;
    QString m_currentProjectFilePath;
    bool m_hasUnsavedEdits = false;
    bool m_loadingOrTemplateModel = false;
    bool m_openedFromCli = false;
    bool m_userRequestedQuit = false;
    bool m_closingConfirmed = false;
    bool m_closePendingAfterSave = false;
    QTimer* m_autoSaveTimer = nullptr;
    io::ProjectRecoveryInfo m_recoveryPromptInfo;
    QPointer<QMessageBox> m_recoveryPromptBox;
    QPointer<QPushButton> m_recoveryPromptBtnAccept;
    QPointer<QPushButton> m_recoveryPromptBtnReject;
    QPointer<QMessageBox> m_closeConfirmBox;
    QPointer<QPushButton> m_closeBtnSave;
    QPointer<QPushButton> m_closeBtnExitKeep;
    QPointer<QPushButton> m_closeBtnCancel;
};

} // namespace omniframe
