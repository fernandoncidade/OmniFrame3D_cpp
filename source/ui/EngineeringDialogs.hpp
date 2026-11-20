#pragma once

#include "core/StructuralModel.hpp"
#include "io/PdfReportGenerator.hpp"
#include "render/Viewport3D.hpp"
#include "solver/FemSolver3D.hpp"

#include <QDialog>
#include <QPointer>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableWidget;

namespace omniframe {

class GuidedSessionDialog;

/**
 * @brief Suite assíncrona (não bloqueante) de análises avançadas:
 * Estática/Combinações, Modal, Flambagem Linear, Harmônica FRF,
 * Espectro de Resposta, Transiente Sísmica e Não Linear (P-Delta / NR / Arc-Length).
 */
class AnalysisSuiteDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    AnalysisSuiteDialog(
        QWidget* parent,
        StructuralModel* model,
        Viewport3D* viewport,
        const SolverResult* lastStaticResult,
        std::function<void(const SolverResult&)> onStaticOrNonlinearCompleted,
        std::function<void(const QString&)> onStatusMsg,
        std::function<void()> onModelMutated = {},
        std::function<void()> onMetadataUpdated = {});

    const kernel::ModalResult* latestModalResult() const { return m_hasModal ? &m_modalRes : nullptr; }
    const kernel::BucklingResult* latestBucklingResult() const { return m_hasBuckling ? &m_bucklingRes : nullptr; }
    const kernel::NonlinearResult* latestNonlinearResult() const { return m_hasNonlinear ? &m_nonlinearRes : nullptr; }
    bool isBusy() const { return m_busy; }

    void refreshContext();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onRunCombosAsync();
    void onPlotSelectedCombo();
    void onRunModalAsync();
    void onPlotSelectedMode();
    void onRunBucklingAsync();
    void onPlotSelectedBucklingMode();
    void onApplyBucklingImperfection();
    void onRunHarmonicAsync();
    void onPlotSelectedHarmonicStep();
    void onRunSpectrumAsync();
    void onPlotSpectrumResult();
    void onRunSeismicAsync();
    void onPlaySeismicHistory();
    void onRunNonlinearAsync();
    void onPlayNonlinearHistory();

private:
    void retranslateUi();
    void setBusyState(bool busy, const QString& statusText = QString());
    void refreshSeismicLoadCases();

    StructuralModel* m_model = nullptr;
    Viewport3D* m_viewport = nullptr;
    const SolverResult* m_lastStatic = nullptr;
    std::function<void(const SolverResult&)> m_onCompleted;
    std::function<void(const QString&)> m_onStatus;
    std::function<void()> m_onModelMutated;
    std::function<void()> m_onMetadataUpdated;

    QTabWidget* m_tabs = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_btnClose = nullptr;
    std::vector<QPushButton*> m_actionButtons;
    bool m_busy = false;

    // Aba 1: Casos & Combinações
    QComboBox* m_linearSolverCombo = nullptr;
    QPushButton* m_btnRunCombos = nullptr;
    QPushButton* m_btnPlotCombos = nullptr;
    QTableWidget* m_combosTable = nullptr;
    std::vector<kernel::CaseDisplacementResult> m_combosResults;
    bool m_hasCombos = false;

    // Aba 2: Modal
    QSpinBox* m_modalModesSpin = nullptr;
    QPushButton* m_btnRunModal = nullptr;
    QPushButton* m_btnPlotModal = nullptr;
    QTableWidget* m_modalTable = nullptr;
    kernel::ModalResult m_modalRes;
    bool m_hasModal = false;

    // Aba 3: Flambagem
    QSpinBox* m_bucklingModesSpin = nullptr;
    QCheckBox* m_bucklingNegCheck = nullptr;
    QDoubleSpinBox* m_impAmpSpin = nullptr;
    QPushButton* m_btnRunBuckling = nullptr;
    QPushButton* m_btnPlotBuckling = nullptr;
    QPushButton* m_btnApplyImperfection = nullptr;
    QTableWidget* m_bucklingTable = nullptr;
    kernel::BucklingResult m_bucklingRes;
    bool m_hasBuckling = false;

    // Aba 4: Harmônica
    QDoubleSpinBox* m_harmFMinSpin = nullptr;
    QDoubleSpinBox* m_harmFMaxSpin = nullptr;
    QSpinBox* m_harmStepsSpin = nullptr;
    QDoubleSpinBox* m_harmEtaSpin = nullptr;
    QPushButton* m_btnRunHarmonic = nullptr;
    QPushButton* m_btnPlotHarmonic = nullptr;
    QTableWidget* m_harmonicTable = nullptr;
    kernel::HarmonicResult m_harmonicRes;
    bool m_hasHarmonic = false;

    // Aba 5: Espectro de Resposta
    QComboBox* m_specComboRule = nullptr;
    QDoubleSpinBox* m_specZetaSpin = nullptr;
    QDoubleSpinBox* m_specAgSpin = nullptr;
    QDoubleSpinBox* m_specCaSpin = nullptr;
    QDoubleSpinBox* m_specCvSpin = nullptr;
    QDoubleSpinBox* m_specDirX = nullptr;
    QDoubleSpinBox* m_specDirY = nullptr;
    QDoubleSpinBox* m_specDirZ = nullptr;
    QLabel* m_specNoteLabel = nullptr;
    QPushButton* m_btnRunSpec = nullptr;
    QPushButton* m_btnPlotSpec = nullptr;
    QLabel* m_specResultLabel = nullptr;
    kernel::ResponseSpectrumResult m_specRes;
    bool m_hasSpec = false;

    // Aba 6: Sísmica / Transiente
    QComboBox* m_seisModeCombo = nullptr;
    QComboBox* m_seisLoadCaseCombo = nullptr;
    QComboBox* m_seisSchemeCombo = nullptr;
    QDoubleSpinBox* m_seisDtSpin = nullptr;
    QSpinBox* m_seisStepsSpin = nullptr;
    QLabel* m_seisAmpLabel = nullptr;
    QDoubleSpinBox* m_seisAmpSpin = nullptr;
    QDoubleSpinBox* m_seisFreqSpin = nullptr;
    QComboBox* m_seisDirCombo = nullptr;
    QPushButton* m_btnRunSeis = nullptr;
    QPushButton* m_btnPlaySeis = nullptr;
    QLabel* m_seisResultLabel = nullptr;
    kernel::TransientResult m_seisRes;
    bool m_hasSeis = false;

    // Aba 7: Não Linear
    QComboBox* m_nlMethodCombo = nullptr;
    QComboBox* m_nlKinCombo = nullptr;
    QSpinBox* m_nlStepsSpin = nullptr;
    QDoubleSpinBox* m_nlFactorSpin = nullptr;
    QCheckBox* m_nlFullNewtonCheck = nullptr;
    QCheckBox* m_nlLineSearchCheck = nullptr;
    QPushButton* m_btnRunNl = nullptr;
    QPushButton* m_btnPlayNl = nullptr;
    QTableWidget* m_nlTable = nullptr;
    kernel::NonlinearResult m_nonlinearRes;
    bool m_hasNonlinear = false;
};

/**
 * @brief Gestão de Casos de Carga, Combinações NBR 6118 / NBR 8800 e Assistentes NBR 6123 / NBR 6120.
 */
class LoadCombinationsDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    LoadCombinationsDialog(
        QWidget* parent,
        StructuralModel* model,
        std::function<void()> onModelUpdated,
        std::function<void()> onMetadataUpdated = {});

    void refreshLists();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onAddLoadCase();
    void onDeleteLoadCase();
    void onCaptureModelLoadsToCase();
    void onLoadCaseToModel();
    void onAddCombination();
    void onDeleteCombination();
    void onGenerateNbrCombos();
    void onAddComboFactorRow();
    void onClearComboFactors();
    void onComputeWind6123();
    void onApplyWindToSelectedNodes();
    void onComputePartition6120();

private:
    void retranslateUi();
    void setStatusFeedback(const QString& msg);
    void populateCaseFields();
    void populateComboFactorsTable();

    StructuralModel* m_model = nullptr;
    std::function<void()> m_onModelUpdated;
    std::function<void()> m_onMetadataUpdated;
    bool m_refreshingLists = false;

    QTabWidget* m_tabs = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_btnClose = nullptr;

    // Casos de Carga
    QComboBox* m_caseListCombo = nullptr;
    QLineEdit* m_caseNameEdit = nullptr;
    QComboBox* m_caseCatCombo = nullptr;
    QCheckBox* m_caseSelfWeightCheck = nullptr;
    QDoubleSpinBox* m_caseSwFactorSpin = nullptr;
    QLabel* m_caseSummaryLabel = nullptr;
    QPushButton* m_btnCaseAdd = nullptr;
    QPushButton* m_btnCaseDel = nullptr;
    QPushButton* m_btnCaseCapture = nullptr;
    QPushButton* m_btnCaseApplyModel = nullptr;

    // Combinações
    QComboBox* m_comboListCombo = nullptr;
    QLineEdit* m_comboNameEdit = nullptr;
    QComboBox* m_comboLimitCombo = nullptr;
    QCheckBox* m_comboPDeltaCheck = nullptr;
    QTableWidget* m_comboFactorsTable = nullptr;
    QComboBox* m_factorCaseCombo = nullptr;
    QDoubleSpinBox* m_factorGammaSpin = nullptr;
    QComboBox* m_autoMaterialCombo = nullptr;
    QComboBox* m_autoOccCombo = nullptr;
    QPushButton* m_btnComboAdd = nullptr;
    QPushButton* m_btnComboDel = nullptr;
    QPushButton* m_btnFactorAdd = nullptr;
    QPushButton* m_btnFactorClear = nullptr;
    QPushButton* m_btnAutoNbr = nullptr;

    // Assistente NBR 6123 & 6120
    QDoubleSpinBox* m_v0Spin = nullptr;
    QDoubleSpinBox* m_s1Spin = nullptr;
    QDoubleSpinBox* m_s2Spin = nullptr;
    QDoubleSpinBox* m_s3Spin = nullptr;
    QDoubleSpinBox* m_windAreaSpin = nullptr;
    QDoubleSpinBox* m_windCpSpin = nullptr;
    QComboBox* m_windDirCombo = nullptr;
    QLabel* m_windResultLabel = nullptr;
    QPushButton* m_btnCalcWind = nullptr;
    QPushButton* m_btnApplyWind = nullptr;

    QDoubleSpinBox* m_partWeightSpin = nullptr;
    QLabel* m_partResultLabel = nullptr;
    QPushButton* m_btnCalcPart = nullptr;
};

/**
 * @brief Gerador de Malha 1D/2D/3D (Line2, Tri3, Quad4/8, Hex8/20, Tet4, Extrusão) + Métricas de Qualidade.
 */
class MeshGeneratorDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    MeshGeneratorDialog(
        QWidget* parent,
        StructuralModel* model,
        Viewport3D* viewport,
        std::function<void()> onModelUpdated);

    void refreshFromModel();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onGenerateMesh();
    void onClearMesh();
    void onFitToModelBounds();

private:
    void retranslateUi();
    void updateQualityReport();
    void updateParameterEnablement();
    void refreshMaterialCombo();

    StructuralModel* m_model = nullptr;
    Viewport3D* m_viewport = nullptr;
    std::function<void()> m_onModelUpdated;

    QComboBox* m_meshTypeCombo = nullptr;
    QComboBox* m_matCombo = nullptr;
    QComboBox* m_bcPresetCombo = nullptr;
    QDoubleSpinBox* m_thicknessSpin = nullptr;
    QDoubleSpinBox* m_pressureSpin = nullptr;
    QDoubleSpinBox* m_origXSpin = nullptr;
    QDoubleSpinBox* m_origYSpin = nullptr;
    QDoubleSpinBox* m_origZSpin = nullptr;
    QDoubleSpinBox* m_lxSpin = nullptr;
    QDoubleSpinBox* m_lySpin = nullptr;
    QDoubleSpinBox* m_lzSpin = nullptr;
    QSpinBox* m_nxSpin = nullptr;
    QSpinBox* m_nySpin = nullptr;
    QSpinBox* m_nzSpin = nullptr;
    QDoubleSpinBox* m_delaunayEdgeSpin = nullptr;
    QLabel* m_qualityLabel = nullptr;
    QPushButton* m_btnFitBounds = nullptr;
    QPushButton* m_btnGenerate = nullptr;
    QPushButton* m_btnClear = nullptr;
    QPushButton* m_btnClose = nullptr;
};

/**
 * @brief Controles interativos de Pós-Processamento 3D: Contornos, Plano de Corte e Animação.
 */
class PostProcessControlDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    PostProcessControlDialog(
        QWidget* parent,
        Viewport3D* viewport,
        std::function<bool()> onEnsureDcrMap = {});

    void syncFromViewport();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onContourChanged(int idx);
    void onMeshVisibilityToggled(bool checked);
    void onClipChanged();
    void onPlayPauseToggled(bool checked);
    void onStepSliderMoved(int val);
    void onSpeedChanged(double val);
    void onAmpChanged(double val);
    void onClearAnimation();

private:
    void retranslateUi();

    Viewport3D* m_viewport = nullptr;
    std::function<bool()> m_onEnsureDcrMap;

    QComboBox* m_contourCombo = nullptr;
    QCheckBox* m_showMeshCheck = nullptr;
    QLabel* m_statusLabel = nullptr;

    QComboBox* m_clipAxisCombo = nullptr;
    QDoubleSpinBox* m_clipOffsetSpin = nullptr;
    QCheckBox* m_clipInvertCheck = nullptr;

    QLabel* m_animInfoLabel = nullptr;
    QPushButton* m_btnPlayPause = nullptr;
    QPushButton* m_btnClearAnim = nullptr;
    QSlider* m_stepSlider = nullptr;
    QDoubleSpinBox* m_speedSpin = nullptr;
    QDoubleSpinBox* m_ampSpin = nullptr;
    QPushButton* m_btnClose = nullptr;
};

/**
 * @brief Auditoria Normativa ABNT NBR (6118, 6120, 6122, 6123, 7480, 8800) com rastreabilidade explícita de PDFs locais.
 */
class NbrComplianceDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    NbrComplianceDialog(
        QWidget* parent,
        StructuralModel* model,
        Viewport3D* viewport,
        const SolverResult* lastSolverResult,
        std::function<void()> onRequestPdfExport,
        std::function<bool()> onRequestStaticSolve = {});

    void runAudit();
    io::PdfReportOptions buildOptions() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();

    StructuralModel* m_model = nullptr;
    Viewport3D* m_viewport = nullptr;
    const SolverResult* m_lastSolver = nullptr;
    std::function<void()> m_onPdfExport;
    std::function<bool()> m_onRequestStaticSolve;

    QComboBox* m_caaCombo = nullptr;
    QComboBox* m_rebarCombo = nullptr;
    QComboBox* m_sitCombo = nullptr;
    QDoubleSpinBox* m_sigmaAdmSpin = nullptr;
    QDoubleSpinBox* m_footBSpin = nullptr;
    QDoubleSpinBox* m_footLSpin = nullptr;
    QDoubleSpinBox* m_v0Spin = nullptr;

    QLabel* m_summaryLabel = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_coverageTable = nullptr;
    QTableWidget* m_steelTable = nullptr;
    QTableWidget* m_beamTable = nullptr;
    QTableWidget* m_colTable = nullptr;
    QTableWidget* m_foundTable = nullptr;
    QTableWidget* m_elsWindTable = nullptr;

    QPushButton* m_btnReevaluate = nullptr;
    QPushButton* m_btnApplyDcrViewport = nullptr;
    QPushButton* m_btnExportPdf = nullptr;
    QPushButton* m_btnClose = nullptr;

    io::ModelNbrAudit m_lastAudit;
};

/**
 * @brief Configuração de Metadados do Projeto e Exportação de Memorial Executivo A4 PDF.
 */
class PdfReportOptionsDialog : public QDialog {
    Q_OBJECT
public:
    PdfReportOptionsDialog(QWidget* parent, StructuralModel* model);

    io::PdfReportOptions collectOptionsAndSaveMetadata();
    bool includeViewportCapture() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();

    StructuralModel* m_model = nullptr;

    QLineEdit* m_projectEdit = nullptr;
    QLineEdit* m_engineerEdit = nullptr;
    QLineEdit* m_clientEdit = nullptr;
    QLineEdit* m_locationEdit = nullptr;
    QComboBox* m_caaCombo = nullptr;
    QComboBox* m_rebarCombo = nullptr;
    QComboBox* m_sitCombo = nullptr;
    QDoubleSpinBox* m_sigmaAdmSpin = nullptr;
    QDoubleSpinBox* m_footBSpin = nullptr;
    QDoubleSpinBox* m_footLSpin = nullptr;
    QDoubleSpinBox* m_v0Spin = nullptr;
    QDoubleSpinBox* m_s1Spin = nullptr;
    QDoubleSpinBox* m_s2Spin = nullptr;
    QDoubleSpinBox* m_s3Spin = nullptr;
    QCheckBox* m_includeDiagCheck = nullptr;
    QCheckBox* m_includeNbrCheck = nullptr;
    QCheckBox* m_captureViewportCheck = nullptr;
    QPushButton* m_btnGenerate = nullptr;
    QPushButton* m_btnCancel = nullptr;
};

} // namespace omniframe
