#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

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
    bool waitForBackgroundSolvers(int timeoutMs = 15000);
    void pumpUiEvents(int ms = 25);

    MainWindow* m_mainWindow = nullptr;
    QTimer* m_timer = nullptr;
    QTimer* m_countdownTimer = nullptr;
    QElapsedTimer m_phaseElapsed;
    QPointer<GuidedTargetOverlay> m_overlay;
    QPointer<GuidedTargetOverlay> m_childOverlay;
    QPointer<QWidget> m_currentHighlightedWidget;
    QRect m_currentHighlightRect;

    QLabel* m_introLabel = nullptr;
    QGroupBox* m_setupGroup = nullptr;
    QLabel* m_exampleLabel = nullptr;
    QComboBox* m_exampleCombo = nullptr;
    QLabel* m_readingProfileLabel = nullptr;
    QComboBox* m_readingProfileCombo = nullptr;
    QLabel* m_readingInfoLabel = nullptr;

    QGroupBox* m_progressGroup = nullptr;
    QLabel* m_phaseBadgeLabel = nullptr;
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
    int m_currentStep = 0;   // 1..kTotalSteps quando ativo/concluído
    int m_currentPhase = 0;  // 0 = Ocioso/Concluído, 1 = Tooltip pré-ação, 2 = Ação executada / observação
    int m_customPhaseIntervalMs = -1;
    int m_currentPhaseDurationMs = 4500;
    int m_pausedRemainingMs = 0;
    int m_activeDialogStep = 0;
    QString m_currentStepTitle;
    QString m_currentTooltipText;
    QString m_currentTargetActionText;
    QString m_currentUnifiedHtml;
};

} // namespace omniframe
