#pragma once

#include "kernel/mesh/MeshGenerator.hpp"

#include <QColor>
#include <QDialog>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

class QCloseEvent;
class QComboBox;
class QEvent;
class QGroupBox;
class QLabel;
class QMoveEvent;
class QObject;
class QPaintEvent;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QTimer;

namespace omniframe {

class MainWindow;

/**
 * @brief Sobreposição visual transparente a eventos de mouse que acompanha a janela-alvo
 *        ativa (Janela Principal ou diálogo filho aberto) durante a Sessão Guiada.
 *
 * Desenha quadro de mira pulsante, seta direcional e cartão callout vinculado diretamente
 * ao botão/combobox/spinbox/menu em execução, adaptando-se dinamicamente aos temas
 * Claro e Escuro sem bloquear cliques nem interferir no Z-Order das janelas.
 */
class GuidedTargetOverlay : public QWidget {
    Q_OBJECT

public:
    explicit GuidedTargetOverlay(QWidget* hostWindow = nullptr);
    ~GuidedTargetOverlay() override;

    void setShowCalloutCard(bool show);
    bool showCalloutCard() const { return m_showCalloutCard; }
    void setShowSpotlight(bool show);
    bool showSpotlight() const { return m_showSpotlight; }
    void setHighlightColor(const QColor& color);
    QColor highlightColor() const { return m_highlightColor; }
    QColor cardBackgroundColor() const { return m_lastCardBgColor; }

    void setHighlight(QWidget* hostWindow,
                      QWidget* targetWidget,
                      const QRect& targetRectInTarget,
                      int phase,
                      const QString& badgeText,
                      const QString& unifiedHtmlBody);
    void clearHighlight();
    void refreshTheme();
    void advancePulse();
    void syncAndUpdate();

    QRect targetRectInHost() const { return m_targetRectInHost; }
    QRect lastCalloutCardRectInHost() const { return m_calloutRectInHost; }
    QRect lastCalloutCardGlobalRect() const;
    QWidget* hostWindow() const { return m_hostWindow.data(); }
    QWidget* targetWidget() const { return m_targetWidget.data(); }
    QString badgeText() const { return m_badgeText; }
    QString unifiedHtmlBody() const { return m_unifiedHtmlBody; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void syncGeometryFromHost();
    void computeCalloutLayout(QRect& outCardRect,
                              int& outHeaderH,
                              int& outPadY,
                              int& outFontPt,
                              int& outInnerW,
                              int& outBadgeH) const;

    QPointer<QWidget> m_hostWindow;
    QPointer<QWidget> m_targetWidget;
    QRect m_targetRectInTarget;
    QRect m_targetRectInHost;
    QRect m_calloutRectInHost;
    int m_phase = 0;
    int m_pulseTick = 0;
    bool m_showCalloutCard = true;
    bool m_showSpotlight = true;
    QString m_badgeText;
    QString m_unifiedHtmlBody;
    QColor m_highlightColor{QStringLiteral("#F59E0B")};
    mutable QColor m_lastCardBgColor;
};

/**
 * @brief Diálogo/controlador modeless da Sessão Guiada Interativa (Ajuda > Sessão Guiada...).
 *
 * Solicita ao usuário que escolha um dos três exemplos pré-definidos e o perfil
 * científico de velocidade de leitura (PPM), reconstruindo o exemplo do zero em
 * 30 etapas enquanto identifica visualmente (Spotlight + Callout + Tooltip sincronizado)
 * qual widget e execução estão ativos no momento, respeitando integralmente Z-Order,
 * janelas não-modais, temas claro/escuro e tradução dinâmica pt_BR / en_US.
 */
class GuidedSessionDialog : public QDialog {
    Q_OBJECT

public:
    enum class ReadingProfile {
        FluentTypical = 0,      // Leitor típico silencioso: 200-300 PPM (ref. 250 PPM)
        TechnicalTypical = 1,   // Leitura técnica / engenharia: 100-200 PPM (ref. 150 PPM)
        ReadAloud = 2,          // Leitura em voz alta: 130-180 PPM (ref. 155 PPM)
        DyslexiaAccessible = 3  // Dislexia / acessível: 100-160 PPM (ref. 130 PPM + fator regressão 1,30x)
    };

    explicit GuidedSessionDialog(MainWindow* mainWindow, QWidget* parent = nullptr);
    ~GuidedSessionDialog() override;

    void positionHudNearMainWindow();

    void selectExampleByNumber(int exampleNum);
    int selectedExampleNumber() const;

    void selectReadingProfile(ReadingProfile profile);
    ReadingProfile selectedReadingProfile() const;

    int phasesForStep(int step) const;
    void setPhasesForStep(int step, int phaseCount);
    int maxPhasesCount() const;
    void selectHighlightPhase(int phase);
    int selectedHighlightPhase() const { return m_selectedHighlightPhase; }
    void setPhaseHighlightColor(int phase, const QColor& color);
    QColor phaseHighlightColor(int phase) const;
    QColor activePhaseHighlightColor() const;
    void resetAllPhaseHighlightColors();
    static QColor defaultPhaseHighlightColor(int phase);

    void setStepHighlightColor(const QColor& color);
    QColor stepHighlightColor() const { return activePhaseHighlightColor(); }
    void resetStepHighlightColor();
    static QColor defaultStepHighlightColor() { return defaultPhaseHighlightColor(1); }

    int currentStepWordCount() const;
    int computePhase1ReadingMs(int step) const;
    int computePhase2ObservationMs(int step = -1) const;

    void startSession(int customPhaseIntervalMs = -1);
    void pauseOrResumeSession();
    void stopSession();
    void advancePhaseNow();

    bool isSessionRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }
    bool isCompleted() const { return m_completed; }
    bool isStepAsyncBusy() const { return m_pendingAsyncStep != 0; }
    int currentStep() const { return m_currentStep; }
    int totalSteps() const { return kTotalSteps; }
    int currentPhase() const { return m_currentPhase; }
    QString currentTooltipText() const { return m_currentTooltipText; }
    QString currentStepTitle() const { return m_currentStepTitle; }

    QWidget* currentHighlightedTarget() const;
    QRect currentHighlightedRectInWindow() const;
    QRect currentCalloutCardGlobalRect() const;
    bool isTargetHighlightVisible() const;
    QString currentTargetActionText() const { return m_currentTargetActionText; }

    static constexpr int kTotalSteps = 30;

signals:
    void stepHighlightColorChanged(const QColor& color);

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTimerTick();
    void onCountdownTick();
    void onStartClicked();
    void onPauseResumeClicked();
    void onNextStepClicked();
    void onStopClicked();
    void onPickHighlightColorClicked();
    void onResetHighlightColorClicked();

private:
    struct StepSpec {
        QString titlePtBr;
        QString whatDoesPtBr;
        QString purposePtBr;
        QString menuPathPtBr;
        QString targetWidgetNamePtBr;
        QString actionSummaryPtBr;
    };

    struct ReadingProfileSpec {
        int ppm = 150;
        double cognitiveMultiplier = 1.10;
        int minPhase1Ms = 10000;
        int maxPhase1Ms = 45000;
        int phase2ObserveMs = 4500;
    };

    ReadingProfileSpec profileSpec(ReadingProfile profile) const;
    int wordCountForStep(int step) const;
    int computeReadingDurationMsForStep(int step) const;
    void schedulePhaseTimer(int durationMs);
    void updateCountdownDisplay();

    void retranslateUi();
    void updateReadingInfoLabel();
    void ensurePhaseColorCapacity(int minPhases);
    void syncHighlightColorControls();
    void loadSavedHighlightColor();
    void saveHighlightColorPreference() const;
    void applyCardTheme();
    void updateControlsState();
    void refreshCurrentStepTexts();
    StepSpec specForStep(int step) const;
    void positionChildDialogForGuidedSession(QWidget* childDlg) const;
    QWidget* resolveTargetWidgetForStep(int step,
                                        int phase,
                                        QRect* subRectInTarget = nullptr,
                                        QPoint* customGlobalPos = nullptr) const;
    void updateVisualHighlight();
    void clearVisualHighlight();
    void showPreActionTooltipForStep(int step);
    void executeStepAction(int step);
    void commitPendingStepEdits(int completedStep);
    void closeAuxiliaryInputDialogs();
    void restoreSelectedExampleToInitialState();
    void finishSession();
    void cancelPendingAsyncStep();
    void onAsyncSuiteStepFinished(bool ok);
    void runGuidedStaticAsyncStep(int step, std::function<void(bool)> afterSolved);

    QPointer<MainWindow> m_mainWindow;
    QTimer* m_timer = nullptr;
    QTimer* m_countdownTimer = nullptr;
    QElapsedTimer m_phaseElapsed;
    QPointer<GuidedTargetOverlay> m_overlay;
    QPointer<GuidedTargetOverlay> m_childOverlay;
    QPointer<QWidget> m_currentHighlightedWidget;
    QPointer<QDialog> m_colorPickerDialog;
    QRect m_currentHighlightRect;

    QLabel* m_introLabel = nullptr;
    QGroupBox* m_setupGroup = nullptr;
    QLabel* m_exampleLabel = nullptr;
    QComboBox* m_exampleCombo = nullptr;
    QLabel* m_readingProfileLabel = nullptr;
    QComboBox* m_readingProfileCombo = nullptr;
    QLabel* m_readingInfoLabel = nullptr;
    QLabel* m_highlightColorLabel = nullptr;
    QComboBox* m_highlightPhaseCombo = nullptr;
    QLabel* m_highlightColorSwatch = nullptr;
    QComboBox* m_highlightColorCombo = nullptr;
    QPushButton* m_btnPickHighlightColor = nullptr;
    QPushButton* m_btnResetHighlightColor = nullptr;
    std::array<int, kTotalSteps> m_stepPhaseCounts{};
    std::vector<QColor> m_phaseHighlightColors;
    int m_selectedHighlightPhase = 1;

    QGroupBox* m_progressGroup = nullptr;
    QLabel* m_phaseBadgeLabel = nullptr;
    QScrollArea* m_bannerScroll = nullptr;
    QLabel* m_targetActionBannerLabel = nullptr;
    QLabel* m_timerCountdownLabel = nullptr;
    QLabel* m_stepTitleLabel = nullptr;
    QLabel* m_tooltipCardLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;

    QPushButton* m_btnStart = nullptr;
    QPushButton* m_btnPauseResume = nullptr;
    QPushButton* m_btnNextStep = nullptr;
    QPushButton* m_btnStop = nullptr;
    QPushButton* m_btnClose = nullptr;

    ReadingProfile m_readingProfile = ReadingProfile::TechnicalTypical;
    bool m_running = false;
    bool m_paused = false;
    bool m_completed = false;
    bool m_executingStep = false;
    bool m_inStartOrStop = false;
    int m_currentStep = 0;   // 1..kTotalSteps quando ativo/concluído
    int m_currentPhase = 0;  // 0 = Ocioso/Concluído, 1 = Tooltip pré-ação, 2 = Ação executada / observação
    int m_customPhaseIntervalMs = -1;
    int m_currentPhaseDurationMs = 4500;
    int m_pausedRemainingMs = 0;
    int m_activeDialogStep = 0;
    int m_pendingAsyncStep = 0;
    int m_pendingAsyncStage = 0;
    std::uint64_t m_staticAsyncToken = 0;
    QMetaObject::Connection m_suiteAsyncConn;
    kernel::MeshContainer m_savedStep27SurfaceMesh;
    QString m_currentStepTitle;
    QString m_currentTooltipText;
    QString m_currentTargetActionText;
    QString m_currentUnifiedHtml;
};

} // namespace omniframe
