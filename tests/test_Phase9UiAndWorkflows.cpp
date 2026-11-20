#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/AppLogger.hpp"
#include "io/PdfReportGenerator.hpp"
#include "io/ProjectRecoveryStore.hpp"
#include "io/UserPreferencesStore.hpp"
#include "locale/GerenciadorTraducao.hpp"
#include "render/Camera3D.hpp"
#include "render/RenderSettings.hpp"
#include "render/Viewport3D.hpp"
#include "solver/FemSolver3D.hpp"
#include "ui/AboutDialog.hpp"
#include "ui/AsyncAnalysisExecutor.hpp"
#include "ui/Dialogs.hpp"
#include "ui/EngineeringDialogs.hpp"
#include "ui/GuidedSessionDialog.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ManualDialog.hpp"
#include "ui/ResultTableConfigurator.hpp"
#include "ui/ResultTableModel.hpp"
#include "ui/StartupSessionDialog.hpp"
#include "ui/ThemeManager.hpp"
#include "ui/WindowZOrderManager.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDate>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QToolBar>
#include <QToolTip>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextFrame>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace omniframe;

namespace {

void pumpEvents(int ms = 30) {
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 15);
        QThread::msleep(5);
    } while (timer.elapsed() < ms);
}

bool waitUntil(const std::function<bool()>& pred, int timeoutMs = 15000) {
    QElapsedTimer timer;
    timer.start();
    while (!pred()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    return true;
}

template <typename T>
T* findVisibleTopLevel() {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* cast = qobject_cast<T*>(w)) {
            if (cast->isVisible()) return cast;
        }
    }
    return nullptr;
}

bool isAboveInOsZOrder(const QWidget* front, const QWidget* back) {
    if (!front || !back || !front->isVisible() || !back->isVisible()) return false;
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) {
        if (const auto* mw = qobject_cast<const MainWindow*>(back);
            mw != nullptr && mw->windowZOrderManager() != nullptr) {
            const QList<QWidget*> stack = mw->windowZOrderManager()->zOrderStack();
            const qsizetype idxFront = stack.indexOf(const_cast<QWidget*>(front));
            const qsizetype idxBack = stack.indexOf(const_cast<QWidget*>(back));
            return idxFront >= 0 && idxBack >= 0 && idxFront > idxBack;
        }
        return true;
    }
#ifdef Q_OS_WIN
    HWND frontHwnd = reinterpret_cast<HWND>(front->effectiveWinId());
    HWND backHwnd = reinterpret_cast<HWND>(back->effectiveWinId());
    if (!frontHwnd || !backHwnd) return false;
    for (HWND cur = GetWindow(frontHwnd, GW_HWNDNEXT); cur != nullptr; cur = GetWindow(cur, GW_HWNDNEXT)) {
        if (cur == backHwnd) return true;
    }
    return false;
#else
    return true;
#endif
}

QPushButton* findButtonContaining(QWidget* root, const QString& needle) {
    for (auto* btn : root->findChildren<QPushButton*>()) {
        if (btn->text().contains(needle, Qt::CaseInsensitive)) return btn;
    }
    return nullptr;
}

void testSingleClickDialogsAndZOrder() {
    MainWindow mw;
    mw.show();
    pumpEvents(80);

    // 1) Abertura em 1 único clique de todas as 6 novas janelas UI
    const std::vector<const char*> openSlots = {
        "onOpenLoadCombinations",
        "onOpenMeshGenerator",
        "onOpenAnalysisSuite",
        "onOpenPostControls",
        "onOpenNbrCompliance",
        "onExportPdfReport"
    };

    for (const char* slotName : openSlots) {
        const bool invoked = QMetaObject::invokeMethod(&mw, slotName, Qt::DirectConnection);
        TEST_ASSERT(invoked, std::string("Slot deve existir e ser invocavel: ") + slotName);
    }
    pumpEvents(80);

    auto* comboDlg = findVisibleTopLevel<LoadCombinationsDialog>();
    auto* meshDlg = findVisibleTopLevel<MeshGeneratorDialog>();
    auto* suiteDlg = findVisibleTopLevel<AnalysisSuiteDialog>();
    auto* postDlg = findVisibleTopLevel<PostProcessControlDialog>();
    auto* nbrDlg = findVisibleTopLevel<NbrComplianceDialog>();
    auto* pdfDlg = findVisibleTopLevel<PdfReportOptionsDialog>();

    TEST_ASSERT(comboDlg != nullptr, "LoadCombinationsDialog deve abrir visivel no primeiro clique");
    TEST_ASSERT(meshDlg != nullptr, "MeshGeneratorDialog deve abrir visivel no primeiro clique");
    TEST_ASSERT(suiteDlg != nullptr, "AnalysisSuiteDialog deve abrir visivel no primeiro clique");
    TEST_ASSERT(postDlg != nullptr, "PostProcessControlDialog deve abrir visivel no primeiro clique");
    TEST_ASSERT(nbrDlg != nullptr, "NbrComplianceDialog deve abrir visivel no primeiro clique");
    TEST_ASSERT(pdfDlg != nullptr, "PdfReportOptionsDialog deve abrir visivel no primeiro clique");

    TEST_ASSERT(isAboveInOsZOrder(pdfDlg, &mw), "Ultima janela aberta deve estar acima do MainWindow");
    pdfDlg->close();
    pumpEvents(40);

    // 2) resize não pode ser sobrescrito por adjustSize() + updateInspector no pai não pode derrubar Z-order
    comboDlg->resize(912, 624);
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenLoadCombinations", Qt::DirectConnection), "Re-focar LoadCombinationsDialog");
    pumpEvents(60);
    TEST_ASSERT(comboDlg->width() >= 900 && comboDlg->height() >= 610,
                "Tamanho redimensionado da janela filha não pode ser resetado pelo WindowZOrderManager");
    TEST_ASSERT(isAboveInOsZOrder(comboDlg, &mw), "LoadCombinationsDialog deve estar acima do MainWindow");

    // Força reconstrução de filhos no MainWindow (ponto que disparava QEvent::Show na GUI pai)
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "updateInspector", Qt::DirectConnection), "Invocar updateInspector");
    if (auto* pb = mw.findChild<QProgressBar*>()) {
        pb->setVisible(true);
        pumpEvents(40);
        pb->setVisible(false);
    }
    pumpEvents(60);
    TEST_ASSERT(isAboveInOsZOrder(comboDlg, &mw),
                "updateInspector/progressBar no MainWindow não pode colocar janela filha atrás do pai");

    // 3) QComboBox::showPopup em diálogo filho não pode derrubar o diálogo para trás do pai
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenPostControls", Qt::DirectConnection), "Focar PostProcessControlDialog");
    pumpEvents(60);
    TEST_ASSERT(isAboveInOsZOrder(postDlg, &mw), "PostProcessControlDialog deve estar acima do MainWindow");
    auto combos = postDlg->findChildren<QComboBox*>();
    TEST_ASSERT(!combos.isEmpty(), "PostProcessControlDialog deve possuir combos");
    combos.first()->showPopup();
    pumpEvents(50);
    combos.first()->hidePopup();
    pumpEvents(50);
    TEST_ASSERT(isAboveInOsZOrder(postDlg, &mw),
                "Abrir e fechar QComboBox::showPopup não pode enviar PostProcessControlDialog para trás");

    // 4) Subdiálogo em MaterialCatalogDialog deve restaurar o catálogo acima do MainWindow
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onAssignMaterials", Qt::DirectConnection), "Abrir catálogo de materiais");
    pumpEvents(60);
    auto* matCat = findVisibleTopLevel<MaterialCatalogDialog>();
    TEST_ASSERT(matCat != nullptr, "MaterialCatalogDialog deve abrir visivel no primeiro clique");
    TEST_ASSERT(QMetaObject::invokeMethod(matCat, "onNewMaterial", Qt::DirectConnection), "Abrir subdiálogo MaterialDialog");
    pumpEvents(60);
    auto* matSub = findVisibleTopLevel<MaterialDialog>();
    TEST_ASSERT(matSub != nullptr, "MaterialDialog subdiálogo deve abrir visivel");
    matSub->reject();
    pumpEvents(60);
    TEST_ASSERT(isAboveInOsZOrder(matCat, &mw),
                "Ao fechar MaterialDialog, MaterialCatalogDialog deve permanecer acima do MainWindow");

    // 5) Diálogos sem resize explícito no construtor devem abrir pré-dimensionados com chrome nativo pronto
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onAbout", Qt::DirectConnection), "Abrir AboutDialog");
    pumpEvents(40);
    auto* aboutDlg = findVisibleTopLevel<AboutDialog>();
    TEST_ASSERT(aboutDlg != nullptr, "AboutDialog deve abrir visível sem flash fantasma");
    TEST_ASSERT(aboutDlg->testAttribute(Qt::WA_Resized) &&
                    aboutDlg->width() >= aboutDlg->minimumSizeHint().width() &&
                    aboutDlg->height() >= aboutDlg->minimumSizeHint().height(),
                "AboutDialog deve ser dimensionado pelo WindowZOrderManager antes da exibição");
    const QString platform = QGuiApplication::platformName();
    if (platform != QLatin1String("offscreen") && platform != QLatin1String("minimal")) {
        TEST_ASSERT(aboutDlg->internalWinId() != 0, "Dialog filho visível deve possuir HWND/WinId nativo inicializado");
    }
    aboutDlg->close();

    matCat->close();
    comboDlg->close();
    meshDlg->close();
    suiteDlg->close();
    postDlg->close();
    nbrDlg->close();
    mw.close();
    pumpEvents(60);
}

void testMeshBoundingBoxAndUiEnablement() {
    StructuralModel model;
    Viewport3D viewport;
    viewport.setModel(&model);
    int updateCount = 0;

    MeshGeneratorDialog dlg(nullptr, &model, &viewport, [&]() { ++updateCount; });
    dlg.show();
    pumpEvents(40);

    auto* meshCombo = dlg.findChild<QComboBox*>();
    auto doubleSpins = dlg.findChildren<QDoubleSpinBox*>();
    auto intSpins = dlg.findChildren<QSpinBox*>();
    TEST_ASSERT(meshCombo != nullptr && doubleSpins.size() >= 6 && intSpins.size() >= 3,
                "MeshGeneratorDialog deve expor controles de geometria");

    // Modo Laje 2D Quad4 (index 1): nz deve ficar desabilitado; ao trocar para Hex8 (index 5), nz habilita
    meshCombo->setCurrentIndex(1);
    pumpEvents(20);
    TEST_ASSERT(!intSpins[2]->isEnabled(), "Nz deve estar desabilitado para malha de laje 2D");

    // Define dimensões fora da caixa padrão [-5..5] para testar StructuralModel::getBoundingBox()
    QDoubleSpinBox* lxSpin = nullptr;
    QDoubleSpinBox* lySpin = nullptr;
    for (auto* sp : doubleSpins) {
        if (std::abs(sp->value() - 6.0) < 1e-6) lxSpin = sp;
        else if (std::abs(sp->value() - 4.0) < 1e-6) lySpin = sp;
    }
    TEST_ASSERT(lxSpin != nullptr && lySpin != nullptr, "Spinboxes Lx e Ly devem existir no MeshGeneratorDialog");
    lxSpin->setValue(18.0); // Lx = 18 m
    lySpin->setValue(12.0); // Ly = 12 m
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onGenerateMesh", Qt::DirectConnection), "Gerar malha 2D");
    pumpEvents(30);

    TEST_ASSERT(updateCount >= 1, "Gerar malha deve notificar onModelUpdated");
    TEST_ASSERT(!model.surfaceMesh().nodes.empty() && !model.surfaceMesh().elements.empty(),
                "Malha 2D gerada deve popular surfaceMesh()");

    Vec3 bMin, bMax;
    model.getBoundingBox(bMin, bMax);
    TEST_ASSERT(bMax.x > 17.5 && bMax.y > 11.5,
                "getBoundingBox() deve refletir nós da malha 2D gerada em surfaceMesh()");

    meshCombo->setCurrentIndex(5); // Bloco Sólido 3D Hex8
    pumpEvents(20);
    TEST_ASSERT(intSpins[2]->isEnabled(), "Nz deve habilitar para bloco sólido 3D Hex8");

    dlg.close();
}

void testLoadCombinationsAutoCaptureAndSelection() {
    StructuralModel model = StructuralModel::createBuildingFrameExample();
    TEST_ASSERT(model.getLoadCases().empty(), "BuildingFrameExample inicia com cargas no viewport e 0 casos explícitos");

    int modelUpdateCalls = 0;
    LoadCombinationsDialog* dlgPtr = nullptr;
    LoadCombinationsDialog dlg(nullptr, &model, [&]() {
        ++modelUpdateCalls;
        if (dlgPtr) {
            dlgPtr->refreshLists(); // Simula MainWindow::onModelModified()
        }
    });
    dlgPtr = &dlg;
    dlg.show();
    pumpEvents(40);

    // 1) Gerar combinações NBR com casos vazios deve auto-capturar cargas em G1 sem popup modal
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onGenerateNbrCombos", Qt::DirectConnection), "Invocar onGenerateNbrCombos");
    pumpEvents(40);
    TEST_ASSERT(model.getLoadCases().size() == 1, "Auto-captura deve criar 1 LoadCase G1");
    TEST_ASSERT(model.getLoadCombinations().size() == 3, "Deve gerar 3 combinações NBR");

    // 2) Criar segundo LoadCase e verificar que refreshLists() preserva a seleção do índice 1
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onAddLoadCase", Qt::DirectConnection), "Adicionar segundo LoadCase");
    pumpEvents(30);
    auto* tabs = dlg.findChild<QTabWidget*>();
    TEST_ASSERT(tabs != nullptr && tabs->count() >= 2, "LoadCombinationsDialog deve possuir abas");
    auto* caseCombo = tabs->widget(0)->findChild<QComboBox*>();
    TEST_ASSERT(caseCombo != nullptr && caseCombo->count() == 2, "Combo de casos deve listar 2 casos");
    caseCombo->setCurrentIndex(1);
    pumpEvents(20);

    auto* swCheck = tabs->widget(0)->findChild<QCheckBox*>();
    TEST_ASSERT(swCheck != nullptr, "LoadCombinationsDialog deve possuir checkbox de peso próprio");
    swCheck->toggle(); // Dispara m_onModelUpdated -> refreshLists()
    pumpEvents(30);
    TEST_ASSERT(caseCombo->currentIndex() == 1,
                "refreshLists() durante edição não pode resetar o caso selecionado para o índice 0");

    // 3) Excluir o segundo LoadCase e confirmar integridade da UI
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onDeleteLoadCase", Qt::DirectConnection), "Excluir segundo LoadCase");
    pumpEvents(30);
    TEST_ASSERT(model.getLoadCases().size() == 1, "Exclusão deve remover o caso selecionado");
    TEST_ASSERT(modelUpdateCalls >= 3, "Ações de edição devem notificar atualização do modelo");

    dlg.close();
}

void testAnalysisSuiteEndToEnd() {
    StructuralModel model = StructuralModel::createBuildingFrameExample();
    Viewport3D viewport;
    viewport.setModel(&model);
    SolverResult lastStatic = FemSolver3D::solve(model);
    TEST_ASSERT(lastStatic.success, "Solver estático inicial do BuildingFrameExample deve convergir");

    AnalysisSuiteDialog dlg(
        nullptr,
        &model,
        &viewport,
        &lastStatic,
        [](const SolverResult&) {},
        [](const QString&) {});
    dlg.show();
    pumpEvents(40);

    auto* pb = dlg.findChild<QProgressBar*>();
    auto* tabs = dlg.findChild<QTabWidget*>();
    TEST_ASSERT(pb != nullptr && tabs != nullptr && tabs->count() >= 7,
                "AnalysisSuiteDialog precisa de barra de progresso e 7 abas");
    auto* comboTable = tabs->widget(0)->findChild<QTableWidget*>();
    auto* modalTable = tabs->widget(1)->findChild<QTableWidget*>();
    auto* bucklingTable = tabs->widget(2)->findChild<QTableWidget*>();
    auto* harmonicTable = tabs->widget(3)->findChild<QTableWidget*>();
    auto* nlTable = tabs->widget(6)->findChild<QTableWidget*>();
    TEST_ASSERT(comboTable && modalTable && bucklingTable && harmonicTable && nlTable,
                "Todas as 5 tabelas de resultados do AnalysisSuiteDialog devem existir");

    auto runAsyncAndWait = [&](const char* slotName) {
        TEST_ASSERT(QMetaObject::invokeMethod(&dlg, slotName, Qt::DirectConnection),
                    std::string("Invocar ") + slotName);
        TEST_ASSERT(waitUntil([&]() { return pb->maximum() > 0; }, 20000),
                    std::string("Timeout aguardando ") + slotName);
    };

    // Aba 1: Casos & Combinações ("Caso Único" fallback quando não há casos definidos)
    runAsyncAndWait("onRunCombosAsync");
    TEST_ASSERT(comboTable->rowCount() >= 1, "Casos & Combinações deve preencher Caso Único");
    comboTable->selectRow(0);
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onPlotSelectedCombo", Qt::DirectConnection), "Plotar combo");
    pumpEvents(30);
    TEST_ASSERT(viewport.hasPostAnimation() && !viewport.isAnimationPlaying(),
                "Plotar combo estático deve carregar campo deformado sem ativar animação oscilatória");

    // Aba 2: Modal
    runAsyncAndWait("onRunModalAsync");
    TEST_ASSERT(dlg.latestModalResult() != nullptr && !dlg.latestModalResult()->modes.empty() && modalTable->rowCount() >= 1,
                "Análise modal deve produzir autovalores/modos");
    modalTable->selectRow(0);
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onPlotSelectedMode", Qt::DirectConnection), "Animar modo");
    pumpEvents(30);
    TEST_ASSERT(viewport.hasPostAnimation(), "Animar modo deve ativar animação no Viewport3D");

    // Aba 3: Flambagem
    runAsyncAndWait("onRunBucklingAsync");
    TEST_ASSERT(dlg.latestBucklingResult() != nullptr && !dlg.latestBucklingResult()->loadFactors.empty() && bucklingTable->rowCount() >= 1,
                "Flambagem deve produzir fatores críticos");
    bucklingTable->selectRow(0);
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onPlotSelectedBucklingMode", Qt::DirectConnection), "Animar flambagem");
    pumpEvents(30);
    TEST_ASSERT(viewport.hasPostAnimation(), "Animar flambagem deve ativar animação");

    // Aba 4: Harmônica
    runAsyncAndWait("onRunHarmonicAsync");
    TEST_ASSERT(harmonicTable->rowCount() >= 2, "Varredura harmônica deve preencher passos");
    harmonicTable->selectRow(0);
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onPlotSelectedHarmonicStep", Qt::DirectConnection), "Animar harmônica");
    pumpEvents(30);
    TEST_ASSERT(viewport.hasPostAnimation(), "Harmônica deve ativar animação");

    // Aba 5: Espectro de Resposta
    runAsyncAndWait("onRunSpectrumAsync");
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onPlotSpectrumResult", Qt::DirectConnection), "Plotar espectro");
    pumpEvents(30);
    TEST_ASSERT(viewport.hasPostAnimation() && !viewport.isAnimationPlaying(),
                "Espectro deve plotar envoltória deformada no Viewport3D");

    // Aba 6: Sísmica Transiente
    runAsyncAndWait("onRunSeismicAsync");
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onPlaySeismicHistory", Qt::DirectConnection), "Reproduzir sísmica");
    pumpEvents(30);
    TEST_ASSERT(viewport.hasPostAnimation() && viewport.nonlinearStepCount() > 1,
                "Histórico sísmico deve carregar frames sequenciais no Viewport3D");

    // Aba 7: Não Linear
    runAsyncAndWait("onRunNonlinearAsync");
    TEST_ASSERT(dlg.latestNonlinearResult() != nullptr && !dlg.latestNonlinearResult()->history.empty(),
                "Análise não linear deve gerar passos de carga");
    TEST_ASSERT(QMetaObject::invokeMethod(&dlg, "onPlayNonlinearHistory", Qt::DirectConnection), "Animar passos NL");
    pumpEvents(30);
    TEST_ASSERT(viewport.hasPostAnimation() && viewport.nonlinearStepCount() >= 1,
                "Trajetória não linear deve carregar frames no Viewport3D");

    dlg.close();
}

void testPostProcessNbrAndPdfWorkflow() {
    MainWindow mw;
    mw.show();
    pumpEvents(80);
    mw.loadExampleBuilding();
    pumpEvents(60);

    // Garante estado NÃO resolvido (desativa recálculo automático e invalida solução após carregar o exemplo)
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onAutoRecalculateToggled", Qt::DirectConnection, Q_ARG(bool, false)),
                "Desativar recálculo automático para testar estado não resolvido");
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onModelModified", Qt::DirectConnection),
                "Limpar resultados estáticos antes da auditoria NBR");
    pumpEvents(30);

    // Abrir Auditoria NBR antes de resolver (F5) -> mensagem explícita de modelo não resolvido
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenNbrCompliance", Qt::DirectConnection), "Abrir NbrComplianceDialog");
    pumpEvents(60);
    auto* nbrDlg = findVisibleTopLevel<NbrComplianceDialog>();
    TEST_ASSERT(nbrDlg != nullptr, "NbrComplianceDialog deve abrir");

    bool foundUnsolvedNotice = false;
    for (auto* lbl : nbrDlg->findChildren<QLabel*>()) {
        if (lbl->text().contains(QStringLiteral("não resolvido"), Qt::CaseInsensitive) ||
            lbl->text().contains(QStringLiteral("not been statically solved"), Qt::CaseInsensitive)) {
            foundUnsolvedNotice = true;
            break;
        }
    }
    TEST_ASSERT(foundUnsolvedNotice, "NbrComplianceDialog deve avisar quando F5 ainda não foi executado");

    // Clicar em Plotar Mapa DCR no Viewport 3D deve resolver assincronamente e ligar Mapa DCR
    auto* btnPlotDcr = findButtonContaining(nbrDlg, QStringLiteral("DCR"));
    TEST_ASSERT(btnPlotDcr != nullptr, "Botão de plotar Mapa DCR deve existir");
    btnPlotDcr->click();

    auto* vp = mw.findChild<Viewport3D*>();
    TEST_ASSERT(vp != nullptr, "Viewport3D deve existir em MainWindow");
    TEST_ASSERT(waitUntil([&]() {
                    return vp->postContourMode() == Viewport3D::PostContourMode::NbrComplianceDcr &&
                           mw.lastSolverResult().success;
                }, 5000),
                "Botão de Mapa DCR deve resolver assincronamente e ativar NbrComplianceDcr no Viewport3D");

    // Abrir Controles de Pós-Processamento e validar sincronização + troca para Displacement e retorno para DCR
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenPostControls", Qt::DirectConnection), "Abrir PostProcessControlDialog");
    pumpEvents(60);
    auto* postDlg = findVisibleTopLevel<PostProcessControlDialog>();
    TEST_ASSERT(postDlg != nullptr, "PostProcessControlDialog deve abrir");

    QComboBox* contourCombo = nullptr;
    QComboBox* clipCombo = nullptr;
    for (auto* cb : postDlg->findChildren<QComboBox*>()) {
        if (cb->objectName() == QStringLiteral("postContourCombo") &&
            cb->count() == 4 &&
            cb->findData(static_cast<int>(Viewport3D::PostContourMode::NbrComplianceDcr)) >= 0 &&
            cb->findData(static_cast<int>(Viewport3D::PostContourMode::VonMisesStress)) >= 0) {
            contourCombo = cb;
        } else if (cb->objectName() == QStringLiteral("clipAxisCombo") &&
                   cb->count() == 4 &&
                   cb->findData(static_cast<int>(Viewport3D::ClipAxis::Z)) >= 0) {
            clipCombo = cb;
        }
    }
    TEST_ASSERT(contourCombo != nullptr && clipCombo != nullptr,
                "PostProcessControlDialog deve conter combos de contorno (4 modos incl. von Mises) e plano de corte");
    TEST_ASSERT(contourCombo->currentData().toInt() == static_cast<int>(Viewport3D::PostContourMode::NbrComplianceDcr),
                "Combo de contorno deve refletir modo NbrComplianceDcr ativo");

    contourCombo->setCurrentIndex(1); // Displacement
    pumpEvents(30);
    TEST_ASSERT(vp->postContourMode() == Viewport3D::PostContourMode::Displacement,
                "Contorno Displacement deve ser aplicado ao Viewport3D");

    contourCombo->setCurrentIndex(3); // VonMisesStress
    pumpEvents(30);
    TEST_ASSERT(vp->postContourMode() == Viewport3D::PostContourMode::VonMisesStress,
                "Contorno VonMisesStress deve ser aplicado ao Viewport3D");

    contourCombo->setCurrentIndex(2); // NbrComplianceDcr
    pumpEvents(30);
    TEST_ASSERT(vp->postContourMode() == Viewport3D::PostContourMode::NbrComplianceDcr,
                "Contorno NbrComplianceDcr via PostProcessControlDialog deve funcionar diretamente");

    // Plano de corte Z
    clipCombo->setCurrentIndex(3); // Z
    pumpEvents(20);
    TEST_ASSERT(vp->clipAxis() == Viewport3D::ClipAxis::Z, "Plano de corte Z deve sincronizar no Viewport3D");

    // Exportar Memorial PDF real usando modelo resolvido + snapshots reais
    StructuralModel model = StructuralModel::createBuildingFrameExample();
    const SolverResult staticRes = FemSolver3D::solve(model);
    TEST_ASSERT(staticRes.success, "Solver para PDF");
    QTemporaryDir tempDir;
    TEST_ASSERT(tempDir.isValid(), "QTemporaryDir para PDF");
    const QString pdfPath = tempDir.filePath(QStringLiteral("memorial_ui_workflow.pdf"));
    io::PdfReportOptions pdfOpts;
    pdfOpts.projectTitle = QStringLiteral("Pórtico Espacial Regressão UI");
    QString errMsg;
    TEST_ASSERT(io::PdfReportGenerator::generateExecutivePdf(pdfPath, model, staticRes, pdfOpts, &errMsg),
                "Geração de PDF executivo A4 deve suceder");
    QFile pdfFile(pdfPath);
    TEST_ASSERT(pdfFile.exists() && pdfFile.size() > 4096, "PDF gerado deve existir com conteúdo substancial");

    postDlg->close();
    nbrDlg->close();
    mw.close();
    pumpEvents(60);
}

void testExamplesCatalogComboAndReeditingWorkflow() {
    // 1) Catálogo pré-definido nos 3 exemplos em memória
    {
        const auto m1 = StructuralModel::createBuildingFrameExample();
        const auto m2 = StructuralModel::createCantileverLFrameExample();
        const auto m3 = StructuralModel::createSpaceTrussExample();
        TEST_ASSERT(m1.getMaterials().size() >= 7 && m1.getCrossSections().size() >= 11,
                    "Exemplo 1 deve possuir catálogo padrão completo (>=7 materiais, >=11 seções)");
        TEST_ASSERT(m2.getMaterials().size() >= 7 && m2.getCrossSections().size() >= 11,
                    "Exemplo 2 deve possuir catálogo padrão completo (>=7 materiais, >=11 seções)");
        TEST_ASSERT(m3.getMaterials().size() >= 7 && m3.getCrossSections().size() >= 11,
                    "Exemplo 3 deve possuir catálogo padrão completo (>=7 materiais, >=11 seções)");
    }

    // 2) SectionDialog: 10 geometrias e 13 presets + perfil I soldado assimétrico
    {
        SectionDialog secDlg;
        auto combos = secDlg.findChildren<QComboBox*>();
        TEST_ASSERT(combos.size() >= 2, "SectionDialog deve possuir combos de Predefinidos e Geometria");
        QComboBox* presetCombo = combos[0];
        QComboBox* shapeCombo = combos[1];
        TEST_ASSERT(presetCombo->count() == 14, "SectionDialog deve listar Personalizado + 13 presets");
        TEST_ASSERT(shapeCombo->count() == 10, "SectionDialog deve listar todas as 10 geometrias paramétricas");
        for (int i = 1; i <= 13; ++i) {
            presetCombo->setCurrentIndex(i);
            const CrossSection cs = secDlg.getSection(1);
            TEST_ASSERT(cs.A > 0.0 && cs.Iy > 0.0 && cs.Iz > 0.0,
                        "Cada preset do SectionDialog deve calcular propriedades positivas");
        }
        const int weldedIdx = shapeCombo->findData(static_cast<int>(SectionShape::WeldedIShape));
        TEST_ASSERT(weldedIdx >= 0, "SectionDialog deve expor SectionShape::WeldedIShape");
        shapeCombo->setCurrentIndex(weldedIdx);
        const CrossSection welded = secDlg.getSection(99);
        TEST_ASSERT(welded.shape == SectionShape::WeldedIShape && welded.Iy > 0.0,
                    "Perfil I Soldado no SectionDialog deve gerar propriedades válidas");
    }

    MainWindow mw;
    mw.show();
    pumpEvents(60);

    auto* vp = mw.findChild<Viewport3D*>();
    TEST_ASSERT(vp != nullptr && vp->model() != nullptr, "MainWindow deve expor Viewport3D com StructuralModel");
    TEST_ASSERT(vp->model()->getNodes().empty() && vp->model()->getElements().empty() &&
                    vp->model()->surfaceMesh().elements.empty(),
                "MainWindow deve inicializar vazio sem exemplo carregado no startup");
    TEST_ASSERT(vp->workPlane() == WorkPlane::XY_Top,
                "Startup vazio deve usar o plano XY (Planta) por padrão");
    StructuralModel* model = vp->model();

    auto findInspectorGroup = [&]() -> QGroupBox* {
        for (auto* gb : mw.findChildren<QGroupBox*>()) {
            if (gb->title().contains(QStringLiteral("Elemento / Nó Selecionado"), Qt::CaseInsensitive) ||
                gb->title().contains(QStringLiteral("Selected Element"), Qt::CaseInsensitive)) {
                return gb;
            }
        }
        return nullptr;
    };

    auto findInspectorFieldByLabel = [](QGroupBox* gb, const QString& needle) -> QWidget* {
        if (!gb) return nullptr;
        for (auto* form : gb->findChildren<QFormLayout*>()) {
            for (int r = 0; r < form->rowCount(); ++r) {
                auto* lblItem = form->itemAt(r, QFormLayout::LabelRole);
                auto* fldItem = form->itemAt(r, QFormLayout::FieldRole);
                if (!lblItem || !fldItem) continue;
                auto* lbl = qobject_cast<QLabel*>(lblItem->widget());
                if (lbl && lbl->text().contains(needle, Qt::CaseInsensitive)) {
                    return fldItem->widget();
                }
            }
        }
        return nullptr;
    };

    auto findInspectorCombo = [&](QGroupBox* gb, const QString& needle) -> QComboBox* {
        QWidget* w = findInspectorFieldByLabel(gb, needle);
        if (!w) return nullptr;
        if (auto* cb = qobject_cast<QComboBox*>(w)) return cb;
        return w->findChild<QComboBox*>();
    };

    auto waitSolved = [&]() {
        TEST_ASSERT(waitUntil([&]() {
            return !model->getElements().empty() && model->getElements().front().hasResults;
        }, 15000), "Solver automático/manual do MainWindow deve concluir com sucesso");
        pumpEvents(40);
    };

    auto exerciseComboPopupAndActivate = [&](QComboBox* cb, int targetIdx, const char* msg) {
        TEST_ASSERT(cb != nullptr && targetIdx >= 0 && targetIdx < cb->count(), msg);
        cb->showPopup();
        TEST_ASSERT(waitUntil([&]() { return cb->view() && cb->view()->isVisible(); }, 3000),
                    std::string("Popup do ComboBox deve abrir: ") + msg);
        pumpEvents(40);
        TEST_ASSERT(cb->view()->isVisible(),
                    std::string("Popup do ComboBox não pode fechar sozinho (Z-order/inspector): ") + msg);
        QPointer<QComboBox> safeCb(cb);
        cb->hidePopup();
        cb->setCurrentIndex(targetIdx);
        QMetaObject::invokeMethod(cb, "activated", Qt::DirectConnection, Q_ARG(int, targetIdx));
        pumpEvents(40);
        if (safeCb) safeCb->hidePopup();
    };

    // 3) Percorrer todos os 3 exemplos: combos Material/Seção/Vínculo/Rótulas, seleção rápida e reedição
    struct ExampleCase {
        void (MainWindow::*loader)();
        ElementId relElemId;
        int hingePreset;
    };
    const std::vector<ExampleCase> examples = {
        {&MainWindow::loadExampleBuilding, 9, 1},   // Rótula Início na viga 9
        {&MainWindow::loadExampleCantilever, 2, 2}, // Rótula Fim na ponta livre da barra 2 (torção livre liberada)
        {&MainWindow::loadExampleSpaceTruss, 1, 0}  // Bi-Engastada na barra 1 da treliça (giro torsional isolado estabilizado)
    };

    for (const auto& ex : examples) {
        vp->setWorkPlane(WorkPlane::XZ_Frontal);
        (mw.*(ex.loader))();
        waitSolved();
        TEST_ASSERT(vp->workPlane() == WorkPlane::XY_Top,
                    "Cada um dos 3 exemplos pré-definidos deve ativar o plano XY (Planta) por padrão");
        TEST_ASSERT(model->getMaterials().size() >= 7 && model->getCrossSections().size() >= 11,
                    "Modelo carregado no MainWindow deve manter catálogo padrão completo");

        QGroupBox* gb = findInspectorGroup();
        TEST_ASSERT(gb != nullptr, "Grupo Elemento / Nó Selecionado deve existir");

        QPushButton* btnSelBars = findButtonContaining(gb, QStringLiteral("Sel. Barras"));
        TEST_ASSERT(btnSelBars != nullptr, "Botão rápido Sel. Barras deve estar presente");
        btnSelBars->click();
        pumpEvents(40);
        TEST_ASSERT(model->getSelectedElements().size() == model->getElements().size(),
                    "Sel. Barras deve selecionar todas as barras do exemplo");

        QComboBox* matCombo = findInspectorCombo(gb, QStringLiteral("Material:"));
        TEST_ASSERT(matCombo != nullptr && matCombo->count() >= 7,
                    "Combobox Material: deve listar todas as opções pré-definidas (>= 7)");
        const MaterialId origMatId = model->getElements().front().materialId;
        int targetMatIdx = -1;
        for (int i = 0; i < matCombo->count(); ++i) {
            TEST_ASSERT(!matCombo->itemText(i).startsWith(QLatin1Char('#')),
                        "Combobox Material: não deve exibir prefixo '#ID' no texto");
            if (targetMatIdx < 0 && matCombo->itemData(i).toInt() != origMatId) {
                targetMatIdx = i;
            }
        }
        const MaterialId targetMatId = matCombo->itemData(targetMatIdx).toInt();
        exerciseComboPopupAndActivate(matCombo, targetMatIdx, "Selecionar novo material via dropdown");
        waitSolved();
        for (const auto& el : model->getElements()) {
            TEST_ASSERT(el.materialId == targetMatId, "Todas as barras selecionadas devem receber o material escolhido");
        }

        // Selecionar todas as barras novamente e trocar Seção Transversal
        btnSelBars = findButtonContaining(gb, QStringLiteral("Sel. Barras"));
        TEST_ASSERT(btnSelBars != nullptr, "Sel. Barras após alteração de material");
        btnSelBars->click();
        pumpEvents(40);

        QComboBox* secCombo = findInspectorCombo(gb, QStringLiteral("Seção Transversal:"));
        TEST_ASSERT(secCombo != nullptr && secCombo->count() >= 11,
                    "Combobox Seção Transversal: deve listar todas as seções do catálogo (>= 11)");
        const SectionId origSecId = model->getElements().front().sectionId;
        int targetSecIdx = -1;
        for (int i = 0; i < secCombo->count(); ++i) {
            TEST_ASSERT(!secCombo->itemText(i).startsWith(QLatin1Char('#')),
                        "Combobox Seção Transversal: não deve exibir prefixo '#ID' no texto");
            if (targetSecIdx < 0 && secCombo->itemData(i).toInt() != origSecId) {
                targetSecIdx = i;
            }
        }
        const SectionId targetSecId = secCombo->itemData(targetSecIdx).toInt();
        exerciseComboPopupAndActivate(secCombo, targetSecIdx, "Selecionar nova seção via dropdown");
        waitSolved();
        for (const auto& el : model->getElements()) {
            TEST_ASSERT(el.sectionId == targetSecId, "Todas as barras selecionadas devem receber a seção escolhida");
        }

        // Testar Rótulas inline numa barra do exemplo
        model->selectElement(ex.relElemId, true);
        QMetaObject::invokeMethod(&mw, "updateInspector", Qt::DirectConnection);
        pumpEvents(30);
        const ElementReleases origReleases = model->findElement(ex.relElemId)->releases;
        QComboBox* relCombo = findInspectorCombo(gb, QStringLiteral("Rótulas:"));
        TEST_ASSERT(relCombo != nullptr && relCombo->count() >= 4, "Combo Rótulas: deve estar disponível");
        const int targetRelIdx = relCombo->findData(ex.hingePreset);
        exerciseComboPopupAndActivate(relCombo, targetRelIdx, "Alterar rótulas via dropdown inline");
        waitSolved();
        const Element* relEl = model->findElement(ex.relElemId);
        TEST_ASSERT(relEl != nullptr, "Barra testada para rótulas deve existir");
        if (ex.hingePreset == 1) {
            TEST_ASSERT(relEl->releases.start[4], "Rótula Início deve liberar My inicial");
        } else if (ex.hingePreset == 2) {
            TEST_ASSERT(relEl->releases.end[4], "Rótula Fim deve liberar My final");
        } else {
            TEST_ASSERT(!relEl->releases.hasAnyRelease(), "Bi-Engastada deve remover liberações");
        }

        TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onUndo", Qt::DirectConnection),
                    "Undo deve restaurar liberações originais após alteração de rótulas");
        waitSolved();
        relEl = model->findElement(ex.relElemId);
        TEST_ASSERT(relEl != nullptr &&
                    relEl->releases.start == origReleases.start &&
                    relEl->releases.end == origReleases.end,
                    "Undo deve restaurar exatamente o estado original de rótulas da barra");
    }

    // 4) No Exemplo 1 (BuildingFrame): Mesma Seção, seleção mista nó+barra, deslocamento Δ, Vínculo, Combos/NBR/Mesh + Undo/Redo
    mw.loadExampleBuilding();
    waitSolved();
    QGroupBox* gb = findInspectorGroup();

    // Mesma Seção
    model->selectElement(1, true);
    QMetaObject::invokeMethod(&mw, "updateInspector", Qt::DirectConnection);
    pumpEvents(30);
    QPushButton* btnSameSec = findButtonContaining(gb, QStringLiteral("Mesma Seção"));
    TEST_ASSERT(btnSameSec != nullptr, "Botão Mesma Seção deve aparecer quando há barra selecionada");
    btnSameSec->click();
    pumpEvents(30);
    TEST_ASSERT(model->getSelectedElements().size() > 1 && model->getSelectedElements().size() < model->getElements().size(),
                "Mesma Seção deve selecionar exatamente o subgrupo de barras com a seção da barra 1");

    // Seleção mista (Nó 1 + Barra 1 simultaneamente)
    model->selectNode(1, true);
    model->selectElement(1, false);
    QMetaObject::invokeMethod(&mw, "updateInspector", Qt::DirectConnection);
    pumpEvents(30);
    QComboBox* supCombo = findInspectorCombo(gb, QStringLiteral("Vínculo:"));
    QComboBox* mixedMatCombo = findInspectorCombo(gb, QStringLiteral("Material:"));
    TEST_ASSERT(supCombo != nullptr && mixedMatCombo != nullptr,
                "Seleção mista nó+barra deve exibir simultaneamente Vínculo: do nó e Material: da barra");

    // Alterar Vínculo inline para Articulado Fixo e voltar para Engastado
    const int pinnedIdx = supCombo->findData(2);
    exerciseComboPopupAndActivate(supCombo, pinnedIdx, "Alterar Vínculo para Articulado Fixo");
    waitSolved();
    TEST_ASSERT(model->findNode(1)->support.isPinned(), "Nó 1 deve passar a Articulado Fixo");
    supCombo = findInspectorCombo(gb, QStringLiteral("Vínculo:"));
    exerciseComboPopupAndActivate(supCombo, supCombo->findData(1), "Restaurar Vínculo Engastado");
    waitSolved();
    TEST_ASSERT(model->findNode(1)->support.isFixed(), "Nó 1 deve retornar a Engastado");

    // Deslocamento relativo (ΔX, ΔY, ΔZ) de múltiplos nós via Sel. Nós
    QPushButton* btnSelNodes = findButtonContaining(gb, QStringLiteral("Sel. Nós"));
    TEST_ASSERT(btnSelNodes != nullptr, "Botão rápido Sel. Nós deve estar disponível");
    btnSelNodes->click();
    pumpEvents(30);
    const double z1BeforeShift = model->findNode(1)->z();
    auto* spDz = qobject_cast<QDoubleSpinBox*>(findInspectorFieldByLabel(gb, QStringLiteral("Deslocamento ΔZ:")));
    QPushButton* btnShift = findButtonContaining(gb, QStringLiteral("Deslocar Nós Selecionados"));
    TEST_ASSERT(spDz != nullptr && btnShift != nullptr, "Campos de deslocamento ΔZ e botão Deslocar devem existir");
    spDz->setValue(0.50);
    btnShift->click();
    waitSolved();
    TEST_ASSERT(std::abs(model->findNode(1)->z() - (z1BeforeShift + 0.50)) < 1e-6,
                "Deslocar Nós Selecionados deve somar +0.50 m em Z");

    // Undo/Redo restaura coordenadas e estado completo
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onUndo", Qt::DirectConnection), "Desfazer deslocamento Δ");
    waitSolved();
    TEST_ASSERT(std::abs(model->findNode(1)->z() - z1BeforeShift) < 1e-6,
                "Undo deve restaurar Z original dos nós");
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onRedo", Qt::DirectConnection), "Refazer deslocamento Δ");
    waitSolved();
    TEST_ASSERT(std::abs(model->findNode(1)->z() - (z1BeforeShift + 0.50)) < 1e-6,
                "Redo deve reaplicar +0.50 m em Z");

    // Sincronização em LoadCombinationsDialog, NbrComplianceDialog e MeshGeneratorDialog (Ajustar ao Contorno)
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenLoadCombinations", Qt::DirectConnection), "Abrir Combinações");
    pumpEvents(40);
    auto* comboDlg = findVisibleTopLevel<LoadCombinationsDialog>();
    TEST_ASSERT(comboDlg != nullptr, "LoadCombinationsDialog deve abrir");
    TEST_ASSERT(QMetaObject::invokeMethod(comboDlg, "onGenerateNbrCombos", Qt::DirectConnection), "Gerar NBR combos #1");
    waitSolved();
    const size_t comboCountAfter1 = model->getLoadCombinations().size();
    TEST_ASSERT(comboCountAfter1 == 3 && !model->getLoadCases().empty(), "Deve capturar G1 e gerar 3 combos NBR");

    // Editar carga nodal no modelo e regerar NBR sem duplicar combinações e sincronizando G1
    model->findNode(5)->load.force.z -= 25.0;
    model->pushUndo();
    QMetaObject::invokeMethod(&mw, "onModelModified", Qt::DirectConnection);
    waitSolved();
    TEST_ASSERT(QMetaObject::invokeMethod(comboDlg, "onGenerateNbrCombos", Qt::DirectConnection), "Regerar NBR combos #2");
    waitSolved();
    TEST_ASSERT(model->getLoadCombinations().size() == comboCountAfter1,
                "Regerar combinações NBR não deve duplicar combos pré-existentes");

    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenNbrCompliance", Qt::DirectConnection), "Abrir Auditoria NBR");
    pumpEvents(40);
    auto* nbrDlg = findVisibleTopLevel<NbrComplianceDialog>();
    TEST_ASSERT(nbrDlg != nullptr, "NbrComplianceDialog deve abrir e auditar modelo reeditado");

    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenMeshGenerator", Qt::DirectConnection), "Abrir MeshGeneratorDialog");
    pumpEvents(40);
    auto* meshDlg = findVisibleTopLevel<MeshGeneratorDialog>();
    TEST_ASSERT(meshDlg != nullptr, "MeshGeneratorDialog deve abrir");
    QPushButton* btnFitBounds = findButtonContaining(meshDlg, QStringLiteral("Ajustar ao Contorno"));
    QPushButton* btnGenMesh = findButtonContaining(meshDlg, QStringLiteral("Gerar"));
    TEST_ASSERT(btnFitBounds != nullptr && btnGenMesh != nullptr, "Botão Ajustar ao Contorno e Gerar devem existir");
    btnFitBounds->click();
    pumpEvents(20);
    btnGenMesh->click();
    waitSolved();
    TEST_ASSERT(!model->surfaceMesh().elements.empty(), "Malha deve ser gerada ajustada ao modelo");
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onUndo", Qt::DirectConnection), "Undo malha/reedição");
    waitSolved();
    TEST_ASSERT(model->surfaceMesh().elements.empty(), "Undo deve reverter geração de malha no ModelState completo");

    meshDlg->close();
    nbrDlg->close();
    comboDlg->close();
    mw.close();
    pumpEvents(60);
}

void testGuidedSessionWalkthroughAndTranslations() {
    auto& i18n = locale::GerenciadorTraducao::instance();
    i18n.inicializar();
    TEST_ASSERT(i18n.definir_idioma(QStringLiteral("pt_BR")), "Deve ativar idioma pt_BR inicialmente");

    MainWindow mw;
    mw.resize(1280, 820);
    mw.show();
    pumpEvents(60);

    // Localiza menu Ajuda > Sessão Guiada...
    QAction* actGuided = nullptr;
    for (QAction* topAct : mw.menuBar()->actions()) {
        if (QMenu* m = topAct->menu()) {
            for (QAction* subAct : m->actions()) {
                if (subAct->text().contains(QStringLiteral("Sessão Guiada"), Qt::CaseInsensitive)) {
                    actGuided = subAct;
                    break;
                }
            }
        }
        if (actGuided) break;
    }
    TEST_ASSERT(actGuided != nullptr, "Menu Ajuda deve conter a ação 'Sessão Guiada...'");

    actGuided->trigger();
    pumpEvents(60);
    GuidedSessionDialog* dlg = mw.guidedSessionDialog();
    TEST_ASSERT(dlg != nullptr && dlg->isVisible(), "Ajuda > Sessão Guiada deve abrir GuidedSessionDialog");
    TEST_ASSERT(!dlg->isModal() && dlg->windowModality() == Qt::NonModal,
                "GuidedSessionDialog deve ser estritamente modeless (Qt::NonModal)");
    TEST_ASSERT(dlg->parentWidget() == nullptr,
                "GuidedSessionDialog deve ter parent nullptr para respeitar Z-Order bidirecional entre janelas");

    WindowZOrderManager* zMgr = mw.windowZOrderManager();
    TEST_ASSERT(zMgr != nullptr, "MainWindow deve disponibilizar WindowZOrderManager");
    zMgr->bringToFront(&mw, true);
    pumpEvents(25);
    TEST_ASSERT(zMgr->topWindow() == &mw,
                "Clicar/elevar a janela principal deve colocá-la no topo do Z-Order na frente da Sessão Guiada");
    zMgr->bringToFront(dlg, true);
    pumpEvents(25);
    TEST_ASSERT(zMgr->topWindow() == dlg,
                "Clicar/elevar a janela da Sessão Guiada deve colocá-la novamente no topo do Z-Order");

    auto* exCombo = dlg->findChild<QComboBox*>(QStringLiteral("guided_example_combo"));
    auto* profileCombo = dlg->findChild<QComboBox*>(QStringLiteral("guided_reading_profile_combo"));
    auto* countdownLabel = dlg->findChild<QLabel*>(QStringLiteral("guided_countdown_label"));
    auto* targetBanner = dlg->findChild<QLabel*>(QStringLiteral("guided_target_action_banner"));
    auto* legacyTooltipCard = dlg->findChild<QLabel*>(QStringLiteral("guided_tooltip_card"));
    auto* legacyStepTitle = dlg->findChild<QLabel*>(QStringLiteral("guided_step_title"));
    TEST_ASSERT(exCombo != nullptr && exCombo->count() == 3, "Deve listar os 3 exemplos pré-definidos");
    TEST_ASSERT(profileCombo != nullptr && profileCombo->count() == 4,
                "Deve listar os 4 perfis cognitivos de leitura em PPM (incluindo Dislexia)");
    TEST_ASSERT(countdownLabel != nullptr, "Deve exibir estimativa/cronômetro de leitura por etapa");
    TEST_ASSERT(targetBanner != nullptr && targetBanner->isVisible(),
                "Deve exibir apenas o cartão moderno unificado 'guided_target_action_banner'");
    TEST_ASSERT(legacyTooltipCard != nullptr && !legacyTooltipCard->isVisible() &&
                    legacyStepTitle != nullptr && !legacyStepTitle->isVisible(),
                "Versão antiga do tooltip interno deve permanecer oculta mantendo apenas a versão moderna unificada");
    TEST_ASSERT(!QToolTip::isVisible(),
                "Tooltip nativo legado (QToolTip) não deve ser exibido ao abrir a Sessão Guiada");
    TEST_ASSERT(targetBanner->text().contains(QStringLiteral("Status:")) &&
                    targetBanner->text().contains(QStringLiteral("Controle em Foco:")) &&
                    targetBanner->text().contains(QStringLiteral("Como funciona:")),
                "Cartão moderno inicial deve identificar Status, Controle em Foco e Como funciona");
    TEST_ASSERT(dlg->totalSteps() == 30, "Sessão Guiada deve possuir 30 etapas de reconstrução completa do zero");

    auto verifyGuidedHudNoClipping = [&](const QString& langTag) {
        pumpEvents(30);
        TEST_ASSERT(dlg->width() >= 540,
                    "Sessão Guiada deve possuir largura suficiente (>= 540 px) sem limite restritivo de 500 px");
        TEST_ASSERT(exCombo->width() >= 420 && profileCombo->width() >= 420,
                    "Combos da Sessão Guiada devem ocupar largura ampla sem truncar descrições");
        for (QPushButton* btn : dlg->findChildren<QPushButton*>()) {
            const QSize sh = btn->sizeHint();
            TEST_ASSERT(btn->width() >= sh.width() - 2 && btn->height() >= sh.height() - 2,
                        std::string("Botão da Sessão Guiada sem truncamento (") + langTag.toStdString() +
                            "): " + btn->text().toStdString());
        }
        if (qEnvironmentVariableIsSet("OMNIFRAME_CAPTURE_DIR")) {
            const QString dir = qEnvironmentVariable("OMNIFRAME_CAPTURE_DIR");
            QDir().mkpath(dir);
            dlg->grab().save(QDir(dir).filePath(QStringLiteral("guided_%1.png").arg(langTag)));
        }
    };
    verifyGuidedHudNoClipping(QStringLiteral("pt_BR"));

    // Personalização por fase (1, 2, 3, 4...) da cor aplicada no realce das etapas:
    // a cor da fase permanece invariante entre Tema, Interface do Usuário e Idioma,
    // enquanto o fundo do quadro da janela e do tooltip/callout acompanha o Tema (Claro/Escuro)
    // em tempo de execução independentemente do modo de Interface do Usuário (Customizado/Nativo).
    const UiTheme originalTheme = ThemeManager::instance().currentTheme();
    const UiStyleMode originalUiStyle = ThemeManager::instance().uiStyleMode();
    auto* phaseCombo = dlg->findChild<QComboBox*>(QStringLiteral("guided_highlight_phase_combo"));
    auto* colorCombo = dlg->findChild<QComboBox*>(QStringLiteral("guided_highlight_color_combo"));
    auto* colorSwatch = dlg->findChild<QLabel*>(QStringLiteral("guided_highlight_color_swatch"));
    auto* phaseBadge = dlg->findChild<QLabel*>(QStringLiteral("guided_phase_badge"));
    auto* btnPickColor = dlg->findChild<QPushButton*>(QStringLiteral("guided_btn_pick_highlight_color"));
    auto* btnResetColor = dlg->findChild<QPushButton*>(QStringLiteral("guided_btn_reset_highlight_color"));
    TEST_ASSERT(phaseCombo != nullptr && colorCombo != nullptr && colorSwatch != nullptr &&
                    phaseBadge != nullptr && btnPickColor != nullptr && btnResetColor != nullptr,
                "Controles de personalização de cor por fase do realce devem existir na Sessão Guiada");
    TEST_ASSERT(dlg->maxPhasesCount() == 2 && phaseCombo->count() == 2 &&
                    dlg->phaseHighlightColor(1) == GuidedSessionDialog::defaultPhaseHighlightColor(1) &&
                    dlg->phaseHighlightColor(2) == GuidedSessionDialog::defaultPhaseHighlightColor(2) &&
                    dlg->stepHighlightColor() == GuidedSessionDialog::defaultStepHighlightColor(),
                "Cores iniciais das Fases 1 e 2 devem corresponder aos padrões (#F59E0B e #10B981)");

    auto extractBannerBgColor = [](const QString& qss) -> QColor {
        const int idx = qss.indexOf(QLatin1String("background-color:"));
        if (idx < 0) return {};
        const int start = idx + 17;
        int end = qss.indexOf(QLatin1Char(';'), start);
        if (end < 0) end = qss.indexOf(QLatin1Char('}'), start);
        if (end < 0) end = qss.size();
        const QString val = qss.mid(start, end - start).trimmed();
        if (val.startsWith(QLatin1Char('#'))) {
            return QColor(val);
        }
        if (val.startsWith(QLatin1String("rgb"))) {
            const int openP = val.indexOf(QLatin1Char('('));
            const int closeP = val.indexOf(QLatin1Char(')'));
            if (openP >= 0 && closeP > openP) {
                const QStringList parts = val.mid(openP + 1, closeP - openP - 1).split(QLatin1Char(','));
                if (parts.size() >= 3) {
                    return QColor(parts[0].trimmed().toInt(),
                                  parts[1].trimmed().toInt(),
                                  parts[2].trimmed().toInt());
                }
            }
        }
        return {};
    };

    auto findVisibleOverlay = [&]() -> GuidedTargetOverlay* {
        for (GuidedTargetOverlay* ov : mw.findChildren<GuidedTargetOverlay*>()) {
            if (ov != nullptr && ov->isVisible()) {
                return ov;
            }
        }
        for (QWidget* w : QApplication::allWidgets()) {
            if (auto* ov = qobject_cast<GuidedTargetOverlay*>(w); ov != nullptr && ov->isVisible()) {
                return ov;
            }
        }
        return nullptr;
    };

    // Expande a etapa 1 para 4 fases e testa 4 cores distintas + adaptação de fundo por tema
    dlg->setPhasesForStep(1, 4);
    pumpEvents(25);
    TEST_ASSERT(dlg->phasesForStep(1) == 4 && dlg->maxPhasesCount() == 4 && phaseCombo->count() == 4,
                "Se alguma etapa possui 4 fases, o sistema e a UI devem permitir configurar cores para as 4 fases");

    const QList<QColor> distinctColors = {
        QColor(QStringLiteral("#E11D48")),
        QColor(QStringLiteral("#10B981")),
        QColor(QStringLiteral("#3B82F6")),
        QColor(QStringLiteral("#A855F7"))
    };
    for (int p = 1; p <= 4; ++p) {
        dlg->setPhaseHighlightColor(p, distinctColors[p - 1]);
    }
    dlg->selectHighlightPhase(4);
    pumpEvents(25);
    TEST_ASSERT(dlg->selectedHighlightPhase() == 4 && phaseCombo->currentIndex() == 3 &&
                    dlg->phaseHighlightColor(4) == distinctColors[3] &&
                    colorCombo->count() == 8 && colorCombo->itemText(7).contains(QStringLiteral("#A855F7")),
                "Selecionar fase 4 com cor personalizada deve refletir imediatamente na combobox e prévia");

    auto assertPhaseColorAndAdaptiveBg = [&](const QColor& expectedPhaseColor, bool expectLightTheme, const char* contextTag) {
        const QString expectedHex = expectedPhaseColor.name(QColor::HexRgb).toUpper();
        TEST_ASSERT(dlg->activePhaseHighlightColor() == expectedPhaseColor &&
                        phaseBadge->styleSheet().contains(expectedHex, Qt::CaseInsensitive) &&
                        targetBanner->styleSheet().contains(expectedHex, Qt::CaseInsensitive),
                    std::string("Cor da fase ativa (") + expectedHex.toStdString() +
                        ") deve ser preservada em " + contextTag);
        const QColor bannerBg = extractBannerBgColor(targetBanner->styleSheet());
        TEST_ASSERT(bannerBg.isValid(), std::string("Banner deve possuir background-color válido em ") + contextTag);
        if (expectLightTheme) {
            TEST_ASSERT(bannerBg.lightness() > 200,
                        std::string("Fundo do quadro da janela deve ficar claro no Tema Claro em ") + contextTag);
        } else {
            TEST_ASSERT(bannerBg.lightness() < 70,
                        std::string("Fundo do quadro da janela deve ficar escuro no Tema Escuro em ") + contextTag);
        }
        if (auto* ov = findVisibleOverlay()) {
            ov->repaint();
            pumpEvents(15);
            const QColor ovBg = ov->cardBackgroundColor();
            TEST_ASSERT(ovBg.isValid(), std::string("Overlay/tooltip deve possuir cardBackgroundColor válido em ") + contextTag);
            if (expectLightTheme) {
                TEST_ASSERT(ovBg.lightness() > 200,
                            std::string("Fundo do tooltip/callout deve ficar claro no Tema Claro em ") + contextTag);
            } else {
                TEST_ASSERT(ovBg.lightness() < 70,
                            std::string("Fundo do tooltip/callout deve ficar escuro no Tema Escuro em ") + contextTag);
            }
        }
    };

    assertPhaseColorAndAdaptiveBg(distinctColors[3],
                                  ThemeManager::instance().isLight(),
                                  "Pré-execução Fase 4 Custom Dark pt_BR");
    ThemeManager::instance().setTheme(UiTheme::Light);
    pumpEvents(30);
    assertPhaseColorAndAdaptiveBg(distinctColors[3], true, "Pré-execução Fase 4 Custom Light pt_BR");
    ThemeManager::instance().setUiStyleMode(UiStyleMode::Native);
    pumpEvents(30);
    assertPhaseColorAndAdaptiveBg(distinctColors[3], true, "Pré-execução Fase 4 Native Light pt_BR");
    ThemeManager::instance().setTheme(UiTheme::Dark);
    pumpEvents(30);
    assertPhaseColorAndAdaptiveBg(distinctColors[3], false, "Pré-execução Fase 4 Native Dark pt_BR");
    TEST_ASSERT(i18n.definir_idioma(QStringLiteral("en_US")), "Deve carregar en_US preservando cor da fase");
    pumpEvents(30);
    assertPhaseColorAndAdaptiveBg(distinctColors[3], false, "Pré-execução Fase 4 Native Dark en_US");
    TEST_ASSERT(phaseCombo->itemText(3).contains(QStringLiteral("Phase 4")) &&
                    colorCombo->itemText(7).contains(QStringLiteral("Custom Color")) &&
                    btnPickColor->text() == QStringLiteral("Choose Color...") &&
                    btnResetColor->text() == QStringLiteral("Default Color"),
                "Controles da cor por fase devem traduzir dinamicamente para en_US");

    ThemeManager::instance().setUiStyleMode(originalUiStyle);
    ThemeManager::instance().setTheme(originalTheme);
    TEST_ASSERT(i18n.definir_idioma(QStringLiteral("pt_BR")), "Deve retornar para pt_BR");
    pumpEvents(30);
    TEST_ASSERT(phaseCombo->itemText(3).contains(QStringLiteral("Fase 4")) &&
                    btnPickColor->text() == QStringLiteral("Escolher Cor...") &&
                    btnResetColor->text() == QStringLiteral("Cor Padrão"),
                "Controles da cor por fase devem restaurar rótulos em pt_BR");

    // Testa também atribuição de cores repetidas em pré-execução (Fase 1/3 ciano, Fase 2/4 magenta)
    const QColor repeatA(QStringLiteral("#06B6D4"));
    const QColor repeatB(QStringLiteral("#EC4899"));
    dlg->setPhaseHighlightColor(1, repeatA);
    dlg->setPhaseHighlightColor(2, repeatB);
    dlg->setPhaseHighlightColor(3, repeatA);
    dlg->setPhaseHighlightColor(4, repeatB);
    TEST_ASSERT(dlg->phaseHighlightColor(1) == repeatA && dlg->phaseHighlightColor(3) == repeatA &&
                    dlg->phaseHighlightColor(2) == repeatB && dlg->phaseHighlightColor(4) == repeatB,
                "Fases distintas devem aceitar armazenamento independente de cores repetidas");

    // Restaura configuração padrão de 2 fases e cores originais antes do percurso completo de 30 etapas
    dlg->setPhasesForStep(1, 2);
    dlg->resetAllPhaseHighlightColors();
    pumpEvents(20);
    TEST_ASSERT(dlg->maxPhasesCount() == 2 &&
                    dlg->phaseHighlightColor(1) == GuidedSessionDialog::defaultPhaseHighlightColor(1) &&
                    dlg->phaseHighlightColor(2) == GuidedSessionDialog::defaultPhaseHighlightColor(2),
                "resetAllPhaseHighlightColors deve restaurar as cores padrão das fases");

    // Testa seleção via combobox (preset #10B981) na Fase 1 e restauração
    dlg->selectHighlightPhase(1);
    colorCombo->setCurrentIndex(1);
    pumpEvents(25);
    TEST_ASSERT(dlg->phaseHighlightColor(1) == QColor(QStringLiteral("#10B981")) &&
                    colorSwatch->styleSheet().contains(QStringLiteral("#10B981"), Qt::CaseInsensitive),
                "Seleção na combobox deve atualizar a cor da fase selecionada para Verde Esmeralda (#10B981)");
    dlg->resetAllPhaseHighlightColors();
    pumpEvents(20);

    // Verifica calibração científica de ritmo de leitura em PPM por perfil (Fase 1 e Fase 2)
    dlg->selectReadingProfile(GuidedSessionDialog::ReadingProfile::FluentTypical);
    const int fluentMs = dlg->computePhase1ReadingMs(1);
    const int fluentObsMs = dlg->computePhase2ObservationMs(1);
    dlg->selectReadingProfile(GuidedSessionDialog::ReadingProfile::TechnicalTypical);
    const int techMs = dlg->computePhase1ReadingMs(1);
    const int techObsMs = dlg->computePhase2ObservationMs(1);
    dlg->selectReadingProfile(GuidedSessionDialog::ReadingProfile::ReadAloud);
    const int aloudMs = dlg->computePhase1ReadingMs(1);
    const int aloudObsMs = dlg->computePhase2ObservationMs(1);
    dlg->selectReadingProfile(GuidedSessionDialog::ReadingProfile::DyslexiaAccessible);
    const int dyslexiaMs = dlg->computePhase1ReadingMs(1);
    const int dyslexiaObsMs = dlg->computePhase2ObservationMs(1);

    TEST_ASSERT(fluentMs >= 7000 && techMs >= 10000 && aloudMs >= 11000 && dyslexiaMs >= 15000,
                "Perfis de leitura devem respeitar limites humanos reais (>= 7 s / 10 s / 11 s / 15 s)");
    TEST_ASSERT(fluentMs < techMs && techMs < dyslexiaMs,
                "Perfil de dislexia e leitura técnica devem ampliar o tempo proporcionalmente aos PPM e regressão ocular");
    TEST_ASSERT(fluentObsMs == fluentMs && techObsMs == techMs && aloudObsMs == aloudMs && dyslexiaObsMs == dyslexiaMs,
                "Fase 2 (verde) deve usar o mesmo cálculo científico proporcional às palavras e PPM da etapa");
    TEST_ASSERT(fluentObsMs < techObsMs && techObsMs < dyslexiaObsMs,
                "Tempo de observação da Fase 2 deve escalar entre perfis exatamente na mesma proporção dos PPM");

    // Verifica tradução dinâmica pt_BR -> en_US -> pt_BR antes e durante execução
    TEST_ASSERT(i18n.definir_idioma(QStringLiteral("en_US")), "Deve carregar catálogo en_US");
    pumpEvents(40);
    TEST_ASSERT(actGuided->text() == QStringLiteral("Guided Session..."),
                "Ação do menu Ajuda deve traduzir dinamicamente para 'Guided Session...'");
    TEST_ASSERT(dlg->windowTitle().contains(QStringLiteral("Guided Session")),
                "Título do diálogo deve traduzir para inglês em tempo de execução");
    TEST_ASSERT(exCombo->itemText(0).contains(QStringLiteral("Example 1")),
                "Lista dos 3 exemplos deve traduzir dinamicamente para inglês");
    TEST_ASSERT(profileCombo->itemText(3).contains(QStringLiteral("Dyslexia")),
                "Perfil cognitivo de leitura deve traduzir dinamicamente para inglês");
    for (QGroupBox* gb : dlg->findChildren<QGroupBox*>()) {
        TEST_ASSERT(!gb->title().contains(QLatin1Char('&')),
                    std::string("GroupBox não deve conter '&' simples interpretado como mnemonic: ") +
                        gb->title().toStdString());
    }
    TEST_ASSERT(targetBanner->text().contains(QStringLiteral("Status:")) &&
                    targetBanner->text().contains(QStringLiteral("Control in Focus:")) &&
                    targetBanner->text().contains(QStringLiteral("How it works:")),
                "Cartão moderno inicial deve traduzir todos os identificadores unificados para inglês");
    verifyGuidedHudNoClipping(QStringLiteral("en_US"));

    TEST_ASSERT(i18n.definir_idioma(QStringLiteral("pt_BR")), "Deve retornar para pt_BR em tempo de execução");
    pumpEvents(40);
    TEST_ASSERT(actGuided->text() == QStringLiteral("Sessão Guiada..."),
                "Ação do menu Ajuda deve retornar para 'Sessão Guiada...'");
    TEST_ASSERT(profileCombo->itemText(3).contains(QStringLiteral("Dislexia")),
                "Perfil de leitura em português deve restaurar 'Dislexia'");
    TEST_ASSERT(targetBanner->text().contains(QStringLiteral("Controle em Foco:")) &&
                    targetBanner->text().contains(QStringLiteral("Como funciona:")),
                "Cartão moderno inicial deve restaurar identificadores unificados em pt_BR");

    auto* vp = mw.findChild<Viewport3D*>();
    TEST_ASSERT(vp != nullptr && vp->model() != nullptr, "Viewport 3D e modelo devem estar ativos");

    // Verifica em execução ao vivo (4 fases com overlay ativo) cores distintas, cores repetidas e fundo adaptativo
    dlg->setPhasesForStep(1, 4);
    for (int p = 1; p <= 4; ++p) {
        dlg->setPhaseHighlightColor(p, distinctColors[p - 1]);
    }
    dlg->selectExampleByNumber(1);
    dlg->startSession(60000);
    pumpEvents(30);
    TEST_ASSERT(findVisibleOverlay() != nullptr,
                "Overlay/tooltip da Sessão Guiada deve estar visível ao iniciar execução multi-fase");
    for (int p = 1; p <= 4; ++p) {
        TEST_ASSERT(dlg->currentStep() == 1 && dlg->currentPhase() == p,
                    "Execução deve percorrer sequencialmente todas as fases 1..4 configuradas para a etapa");
        TEST_ASSERT(phaseBadge->text().contains(QStringLiteral("FASE %1/4").arg(p)),
                    "Selo de fase deve exibir dinamicamente FASE p/4");
        assertPhaseColorAndAdaptiveBg(distinctColors[p - 1],
                                      ThemeManager::instance().isLight(),
                                      "Execução 4 fases distintas");
        if (p == 1) {
            ThemeManager::instance().setTheme(UiTheme::Light);
            pumpEvents(35);
            assertPhaseColorAndAdaptiveBg(distinctColors[0], true, "Execução Fase 1 Custom Light");

            ThemeManager::instance().setUiStyleMode(UiStyleMode::Native);
            pumpEvents(35);
            assertPhaseColorAndAdaptiveBg(distinctColors[0], true, "Execução Fase 1 Native Light");

            ThemeManager::instance().setTheme(UiTheme::Dark);
            pumpEvents(35);
            assertPhaseColorAndAdaptiveBg(distinctColors[0], false, "Execução Fase 1 Native Dark");

            TEST_ASSERT(i18n.definir_idioma(QStringLiteral("en_US")), "Deve carregar en_US durante execução multi-fase");
            pumpEvents(30);
            assertPhaseColorAndAdaptiveBg(distinctColors[0], false, "Execução Fase 1 Native Dark en_US");
            TEST_ASSERT(phaseBadge->text().contains(QStringLiteral("PHASE 1/4")),
                        "Selo ativo deve traduzir para PHASE 1/4 em execução");

            ThemeManager::instance().setUiStyleMode(originalUiStyle);
            ThemeManager::instance().setTheme(originalTheme);
            TEST_ASSERT(i18n.definir_idioma(QStringLiteral("pt_BR")), "Deve retornar para pt_BR durante execução");
            pumpEvents(30);
            TEST_ASSERT(phaseBadge->text().contains(QStringLiteral("FASE 1/4")),
                        "Selo ativo deve retornar para FASE 1/4 em pt_BR");
        }
        if (p < 4) {
            dlg->advancePhaseNow();
            pumpEvents(25);
        }
    }
    dlg->stopSession();
    pumpEvents(20);

    dlg->setPhaseHighlightColor(1, repeatA);
    dlg->setPhaseHighlightColor(2, repeatB);
    dlg->setPhaseHighlightColor(3, repeatA);
    dlg->setPhaseHighlightColor(4, repeatB);
    dlg->startSession(60000);
    pumpEvents(25);
    for (int p = 1; p <= 4; ++p) {
        const QColor expected = (p % 2 == 1) ? repeatA : repeatB;
        TEST_ASSERT(dlg->currentPhase() == p && dlg->activePhaseHighlightColor() == expected &&
                        phaseBadge->styleSheet().contains(expected.name(QColor::HexRgb), Qt::CaseInsensitive),
                    "Fases distintas em execução ao vivo devem aplicar corretamente cores repetidas");
        if (p < 4) {
            dlg->advancePhaseNow();
            pumpEvents(25);
        }
    }
    dlg->stopSession();
    dlg->setPhasesForStep(1, 2);
    dlg->resetAllPhaseHighlightColors();
    pumpEvents(20);

    auto runAndVerifyFullExample = [&](int exNumber) {
        dlg->selectExampleByNumber(exNumber);
        TEST_ASSERT(dlg->selectedExampleNumber() == exNumber, "Exemplo escolhido deve estar selecionado");
        dlg->startSession(60000); // Timer longo controlado manualmente por advancePhaseNow()
        pumpEvents(25);

        const size_t expectedBaseNodes = (exNumber == 1) ? 4u : (exNumber == 2) ? 2u : 4u;
        const size_t expectedFinalNodes = (exNumber == 1) ? 12u : (exNumber == 2) ? 3u : 20u;
        const size_t expectedPrimaryBars = (exNumber == 1) ? 8u : (exNumber == 2) ? 1u : 16u;
        const size_t expectedFinalBars = (exNumber == 1) ? 16u : (exNumber == 2) ? 2u : 57u;

        auto verifyCalloutZeroOverlap = [&](const char* phaseTag, int exNum, int stepNum) {
            const QRect calloutRect = dlg->currentCalloutCardGlobalRect();
            TEST_ASSERT(!calloutRect.isEmpty(),
                        "Callout moderno deve possuir geometria válida em tela durante todas as etapas");
            for (QWidget* topWin : QApplication::topLevelWidgets()) {
                if (!topWin || !topWin->isVisible() || topWin == &mw) continue;
                const Qt::WindowType wt = topWin->windowType();
                if (wt == Qt::Popup || wt == Qt::ToolTip) continue;
                if (qobject_cast<GuidedTargetOverlay*>(topWin) != nullptr) continue;
                const QRect winRect = topWin->frameGeometry();
                if (calloutRect.intersects(winRect)) {
                    std::cerr << "[GuidedOverlap] ex=" << exNum << " step=" << stepNum
                              << " " << phaseTag << " callout=(" << calloutRect.x() << "," << calloutRect.y()
                              << " " << calloutRect.width() << "x" << calloutRect.height() << ")"
                              << " overlaps " << topWin->metaObject()->className()
                              << " (" << topWin->objectName().toStdString() << ")"
                              << " rect=(" << winRect.x() << "," << winRect.y()
                              << " " << winRect.width() << "x" << winRect.height() << ")\n";
                }
                TEST_ASSERT(!calloutRect.intersects(winRect),
                            "Callout moderno nunca deve sobrepor o HUD da Sessão Guiada ou janelas filhas abertas");
            }
        };

        for (int step = 1; step <= GuidedSessionDialog::kTotalSteps; ++step) {
            TEST_ASSERT(dlg->isSessionRunning(), "Sessão deve continuar rodando até completar a etapa 30");
            TEST_ASSERT(dlg->currentStep() == step, "Etapa atual deve acompanhar a sequência 1..30");
            TEST_ASSERT(dlg->currentPhase() == 1,
                        "Antes de executar cada etapa, a Fase 1 (tooltip explicativo) deve estar ativa");
            TEST_ASSERT(dlg->currentStepWordCount() >= 25,
                        "Cada etapa deve calcular contagem real de palavras do tooltip para dosar os PPM");
            TEST_ASSERT(dlg->isTargetHighlightVisible() &&
                            dlg->currentHighlightedTarget() != nullptr &&
                            !dlg->currentHighlightedRectInWindow().isEmpty() &&
                            !dlg->currentTargetActionText().isEmpty(),
                        "Fase 1 de cada etapa deve identificar visualmente o controle alvo com mira/callout visível");
            verifyCalloutZeroOverlap("Phase1", exNumber, step);
            const QString tip = dlg->currentTooltipText();
            const QString bannerHtml = targetBanner->text();
            TEST_ASSERT(!legacyTooltipCard->isVisible() && !QToolTip::isVisible(),
                        "Somente a versão moderna unificada deve permanecer visível em todas as etapas");
            TEST_ASSERT(!tip.isEmpty() &&
                            tip.contains(QStringLiteral("Etapa:")) &&
                            tip.contains(QStringLiteral("Caminho na GUI:")) &&
                            tip.contains(QStringLiteral("Controle em Foco:")) &&
                            tip.contains(QStringLiteral("Execução Atual:")) &&
                            tip.contains(QStringLiteral("O que faz:")) &&
                            tip.contains(QStringLiteral("Para que serve:")) &&
                            bannerHtml.contains(QStringLiteral("Etapa:")) &&
                            bannerHtml.contains(QStringLiteral("Caminho na GUI:")) &&
                            bannerHtml.contains(QStringLiteral("O que faz:")) &&
                            bannerHtml.contains(QStringLiteral("Para que serve:")),
                        "Texto unificado deve identificar Etapa, Caminho na GUI, Controle em Foco, Execução Atual, O que faz e Para que serve");

            // Verifica tradução dinâmica ao vivo durante uma etapa em andamento (etapa 13 no Exemplo 1)
            if (exNumber == 1 && step == 13) {
                TEST_ASSERT(i18n.definir_idioma(QStringLiteral("en_US")), "Deve alternar para en_US durante execução");
                pumpEvents(25);
                const QString enTip = dlg->currentTooltipText();
                const QString enBanner = targetBanner->text();
                QWidget* hlTarget = dlg->currentHighlightedTarget();
                auto* overlay = (hlTarget && hlTarget->window())
                    ? hlTarget->window()->findChild<GuidedTargetOverlay*>(QStringLiteral("guided_target_overlay"))
                    : nullptr;
                TEST_ASSERT(overlay != nullptr && overlay->isVisible(),
                            "Overlay callout moderno deve permanecer visível durante troca dinâmica de idioma");
                const QString enOverlay = overlay->unifiedHtmlBody();
                TEST_ASSERT(enTip.contains(QStringLiteral("Step:")) &&
                                enTip.contains(QStringLiteral("GUI Path:")) &&
                                enTip.contains(QStringLiteral("Control in Focus:")) &&
                                enTip.contains(QStringLiteral("Current Execution:")) &&
                                enTip.contains(QStringLiteral("What it does:")) &&
                                enTip.contains(QStringLiteral("What it is for:")) &&
                                enBanner.contains(QStringLiteral("Step:")) &&
                                enBanner.contains(QStringLiteral("What it is for:")) &&
                                enOverlay.contains(QStringLiteral("Step:")) &&
                                enOverlay.contains(QStringLiteral("GUI Path:")) &&
                                enOverlay.contains(QStringLiteral("What it does:")),
                            "Texto unificado do banner e do overlay moderno deve traduzir dinamicamente todos os identificadores para inglês");
                TEST_ASSERT(i18n.definir_idioma(QStringLiteral("pt_BR")), "Deve restaurar pt_BR");
                pumpEvents(25);
            }

            // Verifica invariância da cor escolhida do realce ao alternar Tema (Claro <-> Escuro) durante execução (etapa 14)
            if (exNumber == 1 && step == 14) {
                const QString activeHex = dlg->stepHighlightColor().name(QColor::HexRgb).toUpper();
                ThemeManager::instance().setTheme(UiTheme::Light);
                pumpEvents(25);
                TEST_ASSERT(targetBanner->styleSheet().contains(activeHex, Qt::CaseInsensitive) &&
                                phaseBadge->styleSheet().contains(activeHex, Qt::CaseInsensitive) &&
                                dlg->isTargetHighlightVisible(),
                            "Tema Claro ao vivo deve preservar exatamente a cor escolhida do realce e manter destaque ativo");
                ThemeManager::instance().setTheme(UiTheme::Dark);
                pumpEvents(25);
                TEST_ASSERT(targetBanner->styleSheet().contains(activeHex, Qt::CaseInsensitive) &&
                                phaseBadge->styleSheet().contains(activeHex, Qt::CaseInsensitive) &&
                                dlg->isTargetHighlightVisible(),
                            "Tema Escuro ao vivo deve preservar exatamente a cor escolhida do realce e manter destaque ativo");
                ThemeManager::instance().setTheme(originalTheme);
                pumpEvents(25);
            }

            // Verifica que elevar MainWindow manualmente não tem seu Z-Order roubado pelo avanço da Sessão Guiada
            if (exNumber == 1 && step == 6) {
                zMgr->bringToFront(&mw, true);
                pumpEvents(20);
                TEST_ASSERT(zMgr->topWindow() == &mw, "MainWindow deve permanecer no topo do Z-Order");
            }

            // Avança da Fase 1 (Tooltip) -> Fase 2 (Execução simulada da etapa)
            dlg->advancePhaseNow();
            TEST_ASSERT(waitUntil([&]() { return !dlg->isStepAsyncBusy(); }, 15000),
                        "Etapa assíncrona da Sessão Guiada deve concluir dentro do timeout sem processEvents interno");
            pumpEvents(25);

            if (exNumber == 1 && step == 6) {
                TEST_ASSERT(zMgr->topWindow() == &mw,
                            "Avançar etapa na janela principal não deve roubar o Z-Order de volta para a Sessão Guiada");
                zMgr->bringToFront(dlg, true);
                pumpEvents(15);
            }

            if (step < GuidedSessionDialog::kTotalSteps) {
                TEST_ASSERT(dlg->isTargetHighlightVisible() &&
                                dlg->currentHighlightedTarget() != nullptr &&
                                !dlg->currentHighlightedRectInWindow().isEmpty(),
                            "Fase 2 deve manter identificação visual sobre o controle/diálogo em execução");
                verifyCalloutZeroOverlap("Phase2", exNumber, step);
                if (step == 4 || step == 12 || step == 15) {
                    auto* mainOverlay = mw.findChild<GuidedTargetOverlay*>(QStringLiteral("guided_target_overlay"));
                    QWidget* hlTarget = dlg->currentHighlightedTarget();
                    QWidget* childWin = hlTarget ? hlTarget->window() : nullptr;
                    auto* childOverlay = childWin
                        ? childWin->findChild<GuidedTargetOverlay*>(QStringLiteral("guided_child_target_overlay"))
                        : nullptr;
                    TEST_ASSERT(childWin != nullptr && childWin != &mw,
                                "Controle alvo na Fase 2 desta etapa deve pertencer à janela filha aberta");
                    TEST_ASSERT(mainOverlay != nullptr && mainOverlay->isVisible() &&
                                    mainOverlay->showCalloutCard() && !mainOverlay->showSpotlight(),
                                "Overlay da MainWindow deve exibir apenas o Callout quando uma janela filha estiver ativa");
                    TEST_ASSERT(childOverlay != nullptr && childOverlay->isVisible() &&
                                    !childOverlay->showCalloutCard() && childOverlay->showSpotlight(),
                                "Overlay da janela filha deve exibir apenas o Spotlight, nunca o Callout sobre ela");
                }
            }

            // Verifica marcos da reconstrução do zero e ausência de janelas fantasmas
            if (step == 1) {
                TEST_ASSERT(vp->model()->getNodes().empty() && vp->model()->getElements().empty(),
                            "Etapa 1 deve limpar totalmente o modelo para reconstruir o exemplo escolhido do zero");
            } else if (step == 4) {
                TEST_ASSERT(findVisibleTopLevel<MaterialCatalogDialog>() != nullptr,
                            "Etapa 4 deve manter Catálogo de Materiais aberto durante toda a observação (sem piscada fantasma)");
            } else if (step == 5) {
                TEST_ASSERT(findVisibleTopLevel<MaterialCatalogDialog>() == nullptr &&
                                findVisibleTopLevel<SectionCatalogDialog>() != nullptr,
                            "Etapa 5 deve fechar Materiais e manter Catálogo de Seções visível");
            } else if (step == 6) {
                TEST_ASSERT(findVisibleTopLevel<SectionCatalogDialog>() == nullptr &&
                                vp->model()->getNodes().size() == expectedBaseNodes,
                            "Etapa 6 deve fechar Seções e inserir os primeiros nós da base pelas spinboxes");
            } else if (step == 8) {
                TEST_ASSERT(vp->model()->getNodes().size() == expectedFinalNodes,
                            "Etapa 8 deve concluir exatamente todos os nós do exemplo reconstruído");
            } else if (step == 9) {
                TEST_ASSERT(vp->model()->getElements().size() == expectedPrimaryBars,
                            "Etapa 9 deve montar exatamente as barras primárias");
            } else if (step == 10) {
                TEST_ASSERT(vp->model()->getElements().size() == expectedFinalBars,
                            "Etapa 10 deve completar todas as barras do exemplo escolhido");
            } else if (step == 12) {
                TEST_ASSERT(findVisibleTopLevel<SupportDialog>() != nullptr,
                            "Etapa 12 deve manter diálogo de Condições de Apoio aberto na Fase 2");
            } else if (step == 23) {
                TEST_ASSERT(!vp->model()->getLoadCombinations().empty(),
                            "Etapa 23 da Sessão Guiada deve gerar combinações NBR durante a demonstração");
            } else if (step == 24) {
                TEST_ASSERT(!vp->model()->surfaceMesh().elements.empty(),
                            "Etapa 24 da Sessão Guiada deve gerar malha 2D durante a demonstração");
                bool foundContinuumRows = false;
                for (auto* tw : mw.findChildren<QTabWidget*>()) {
                    if (tw->count() >= 4) {
                        if (auto* tbl = qobject_cast<QTableView*>(tw->widget(3))) {
                            foundContinuumRows = (tbl->model() != nullptr && tbl->model()->rowCount() > 0);
                        }
                    }
                }
                TEST_ASSERT(foundContinuumRows,
                            "Etapa 24 deve resolver E2E e preencher a aba Esforços 2D/3D (Casca/Sólido)");
            } else if (step == 25 || step == 26 || step == 27) {
                auto* suite = findVisibleTopLevel<AnalysisSuiteDialog>();
                TEST_ASSERT(suite != nullptr,
                            "Etapas 25-27 devem reutilizar continuamente a Suíte de Análises sem fechar/reabrir janelas fantasmas");
                if (step == 27) {
                    auto* tabs = suite->findChild<QTabWidget*>();
                    auto* nlTbl = (tabs && tabs->count() >= 7) ? tabs->widget(6)->findChild<QTableWidget*>() : nullptr;
                    TEST_ASSERT(nlTbl != nullptr && nlTbl->rowCount() > 0,
                                "Etapa 27 deve concluir o solver não linear corrotacional 3D com passos convergidos");
                }
            } else if (step == 28) {
                TEST_ASSERT(findVisibleTopLevel<AnalysisSuiteDialog>() == nullptr &&
                                findVisibleTopLevel<PostProcessControlDialog>() != nullptr,
                            "Etapa 28 deve ocultar Suíte e manter Pós-Processamento 3D aberto");
                TEST_ASSERT(vp->postContourMode() == Viewport3D::PostContourMode::VonMisesStress &&
                                vp->showSurfaceMesh(),
                            "Etapa 28 deve exibir malha e contorno de Tensão de von Mises após solução 2D/3D da Etapa 24");
            } else if (step == 29) {
                TEST_ASSERT(findVisibleTopLevel<PostProcessControlDialog>() == nullptr &&
                                findVisibleTopLevel<NbrComplianceDialog>() != nullptr,
                            "Etapa 29 deve exibir Auditoria Normativa ABNT NBR");
                TEST_ASSERT(vp->postContourMode() == Viewport3D::PostContourMode::NbrComplianceDcr,
                            "Etapa 29 da Sessão Guiada deve aplicar Mapa DCR ABNT NBR no Viewport 3D");
            }

            // Se ainda não terminou na última etapa, avança para a Fase 1 da próxima etapa
            if (step < GuidedSessionDialog::kTotalSteps) {
                dlg->advancePhaseNow();
                pumpEvents(20);
            }
        }

        TEST_ASSERT(dlg->isCompleted() && !dlg->isSessionRunning(),
                    "Sessão Guiada deve concluir todas as 30 etapas");
        TEST_ASSERT(vp->model()->getNodes().size() == expectedFinalNodes &&
                        vp->model()->getElements().size() == expectedFinalBars,
                    "Após concluir, o exemplo selecionado deve permanecer com sua topologia inicial restaurada");
        TEST_ASSERT(vp->model()->getLoadCombinations().empty() &&
                        vp->model()->surfaceMesh().elements.empty(),
                    "Após concluir, combinações e malha auxiliares da demonstração devem ser limpas restaurando o estado inicial do exemplo");
        TEST_ASSERT(vp->postContourMode() == Viewport3D::PostContourMode::None &&
                        !vp->showSurfaceMesh() &&
                        vp->clipAxis() == Viewport3D::ClipAxis::None &&
                        vp->workPlane() == WorkPlane::XY_Top,
                    "Após concluir, contornos, malha, plano de corte e plano XY (Planta) devem ser restaurados ao estado inicial");
        TEST_ASSERT(findVisibleTopLevel<NbrComplianceDialog>() == nullptr,
                    "Após etapa 30, diálogo NBR deve fechar limpo deixando o exemplo selecionado restaurado no Viewport 3D");
        TEST_ASSERT(!dlg->isTargetHighlightVisible(),
                    "Overlay de destaque visual deve ocultar-se limpo ao concluir a etapa 30");
        TEST_ASSERT(QToolTip::text().isEmpty() || !dlg->isSessionRunning(),
                    "Sessão concluída corretamente");
    };

    // Executa Sessão Guiada completa para o Exemplo 1 (Pórtico) e valida restauração ao estado inicial
    runAndVerifyFullExample(1);

    // Executa também reconstrução completa do zero + restauração para o Exemplo 2 (Balanço L) e Exemplo 3 (Treliça Espacial)
    runAndVerifyFullExample(2);
    runAndVerifyFullExample(3);

    dlg->resetStepHighlightColor();
    ThemeManager::instance().setUiStyleMode(originalUiStyle);
    ThemeManager::instance().setTheme(originalTheme);
    TEST_ASSERT(i18n.definir_idioma(QStringLiteral("pt_BR")), "Restaura pt_BR ao finalizar teste");
    dlg->close();
    mw.close();
    pumpEvents(60);
}

bool hasDistinctArrowTriangle(const QImage& img, const QRect& box, const QColor& expectedArrow, Qt::ArrowType expectedDir) {
    if (img.isNull() || box.isEmpty() || expectedDir == Qt::NoArrow) return false;
    const QRect clipped = box.intersected(img.rect()).adjusted(2, 2, -2, -2);
    if (!clipped.isValid()) return false;

    const QColor centerBg = img.pixelColor(std::min(clipped.right(), clipped.left() + 2),
                                           std::min(clipped.bottom(), clipped.top() + 2));
    const bool darkArrowOnLightBg = expectedArrow.lightness() < 80 && centerBg.lightness() > 140;
    const bool lightArrowOnDarkBg = expectedArrow.lightness() > 180 && centerBg.lightness() < 110;

    auto isArrowPixel = [&](int x, int y) {
        const QColor px = img.pixelColor(x, y);
        if (std::abs(px.red() - expectedArrow.red()) <= 75 &&
            std::abs(px.green() - expectedArrow.green()) <= 75 &&
            std::abs(px.blue() - expectedArrow.blue()) <= 75) {
            return true;
        }
        if (darkArrowOnLightBg && px.lightness() <= centerBg.lightness() - 60) {
            return true;
        }
        if (lightArrowOnDarkBg && px.lightness() >= centerBg.lightness() + 60) {
            return true;
        }
        return false;
    };

    std::vector<int> sliceSpans;
    int totalMatching = 0;
    if (expectedDir == Qt::LeftArrow || expectedDir == Qt::RightArrow) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            int minY = clipped.bottom() + 1;
            int maxY = clipped.top() - 1;
            for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
                if (isArrowPixel(x, y)) {
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                    ++totalMatching;
                }
            }
            if (maxY >= minY) {
                sliceSpans.push_back(maxY - minY + 1);
            }
        }
        if (totalMatching < 5 || sliceSpans.size() < 3) return false;
        return (expectedDir == Qt::LeftArrow)
            ? (sliceSpans.back() > sliceSpans.front())
            : (sliceSpans.front() > sliceSpans.back());
    }

    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        int minX = clipped.right() + 1;
        int maxX = clipped.left() - 1;
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            if (isArrowPixel(x, y)) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                ++totalMatching;
            }
        }
        if (maxX >= minX) {
            sliceSpans.push_back(maxX - minX + 1);
        }
    }
    if (totalMatching < 5 || sliceSpans.size() < 3) return false;
    return (expectedDir == Qt::UpArrow)
        ? (sliceSpans.back() > sliceSpans.front())
        : (sliceSpans.front() > sliceSpans.back());
}

bool hasDistinctArrowTriangle(const QImage& img, const QRect& box, const QColor& expectedArrow, bool expectUp) {
    return hasDistinctArrowTriangle(img, box, expectedArrow, expectUp ? Qt::UpArrow : Qt::DownArrow);
}

void testThemedComboAndSpinArrowsRenderWithoutPngAssets() {
    for (const char* resPath : {
             ":/omniframe/assets/icons/spin_up_dark.png",
             ":/omniframe/assets/icons/spin_down_dark.png",
             ":/omniframe/assets/icons/spin_up_light.png",
             ":/omniframe/assets/icons/spin_down_light.png",
             ":/omniframe/assets/icons/spin_up_disabled_dark.png",
             ":/omniframe/assets/icons/spin_down_disabled_dark.png",
             ":/omniframe/assets/icons/spin_up_disabled_light.png",
             ":/omniframe/assets/icons/spin_down_disabled_light.png",
         }) {
        TEST_ASSERT(!QFile::exists(QString::fromLatin1(resPath)),
                    std::string("Recurso legado nao deve estar embutido: ") + resPath);
    }

    const UiTheme originalTheme = ThemeManager::instance().currentTheme();
    const QString shotDir = qEnvironmentVariable("OMNIFRAME_UI_SHOT_DIR");

    for (const UiTheme theme : {UiTheme::Dark, UiTheme::Light}) {
        ThemeManager::instance().setTheme(theme);

        QWidget host;
        host.setAttribute(Qt::WA_DontShowOnScreen, true);
        auto* layout = new QVBoxLayout(&host);
        auto* combo = new QComboBox(&host);
        combo->addItems({QStringLiteral("Item A"), QStringLiteral("Item B")});
        combo->setMinimumSize(180, 28);
        auto* comboDis = new QComboBox(&host);
        comboDis->addItem(QStringLiteral("Disabled"));
        comboDis->setEnabled(false);
        comboDis->setMinimumSize(180, 28);

        auto* spin = new QSpinBox(&host);
        spin->setValue(12);
        spin->setMinimumSize(180, 28);
        auto* dspinDis = new QDoubleSpinBox(&host);
        dspinDis->setValue(3.14);
        dspinDis->setEnabled(false);
        dspinDis->setMinimumSize(180, 28);

        layout->addWidget(combo);
        layout->addWidget(comboDis);
        layout->addWidget(spin);
        layout->addWidget(dspinDis);
        host.resize(220, 160);
        host.show();
        pumpEvents(30);

        if (!shotDir.isEmpty()) {
            const QString tag = (theme == UiTheme::Light) ? QStringLiteral("light") : QStringLiteral("dark");
            host.grab().save(shotDir + QStringLiteral("/widgets_") + tag + QStringLiteral(".png"));
        }

        const QColor normalArrow = (theme == UiTheme::Light) ? QColor(0x1e, 0x29, 0x3b) : QColor(0xe2, 0xe8, 0xf0);
        const QColor disabledArrow = (theme == UiTheme::Light) ? QColor(0x94, 0xa3, 0xb8) : QColor(0x64, 0x74, 0x8b);

        const QImage comboImg = combo->grab().toImage();
        const QRect comboArrowRect(comboImg.width() - 22, 2, 20, comboImg.height() - 4);
        TEST_ASSERT(hasDistinctArrowTriangle(comboImg, comboArrowRect, normalArrow, false),
                    "QComboBox deve renderizar triangulo inferior nitido sem PNGs externos");

        const QImage comboDisImg = comboDis->grab().toImage();
        const QRect comboDisRect(comboDisImg.width() - 22, 2, 20, comboDisImg.height() - 4);
        TEST_ASSERT(hasDistinctArrowTriangle(comboDisImg, comboDisRect, disabledArrow, false),
                    "QComboBox desabilitado deve renderizar triangulo inferior atenuado sem PNGs externos");

        const QImage spinImg = spin->grab().toImage();
        const int spinMidY = spinImg.height() / 2;
        const QRect spinUpRect(spinImg.width() - 20, 1, 18, spinMidY - 1);
        const QRect spinDownRect(spinImg.width() - 20, spinMidY, 18, spinImg.height() - spinMidY - 1);
        TEST_ASSERT(hasDistinctArrowTriangle(spinImg, spinUpRect, normalArrow, true),
                    "QSpinBox deve renderizar triangulo superior nitido sem PNGs externos");
        TEST_ASSERT(hasDistinctArrowTriangle(spinImg, spinDownRect, normalArrow, false),
                    "QSpinBox deve renderizar triangulo inferior nitido sem PNGs externos");

        auto verifyFlushRoundedSpinCorners = [&](const QImage& sImg, bool isEnabledSpin, const char* label) {
            const QColor hostBg = host.palette().color(QPalette::Window);
            const QColor expectedBtnBg = isEnabledSpin
                ? ((theme == UiTheme::Light) ? QColor(0xe2, 0xe8, 0xf0) : QColor(0x28, 0x31, 0x3e))
                : ((theme == UiTheme::Light) ? QColor(0xf1, 0xf5, 0xf9) : QColor(0x1c, 0x22, 0x2b));
            auto matchesColor = [](const QColor& a, const QColor& b, int tol) {
                return std::abs(a.red() - b.red()) <= tol &&
                       std::abs(a.green() - b.green()) <= tol &&
                       std::abs(a.blue() - b.blue()) <= tol;
            };
            // O preenchimento dos botões up/down deve tocar rente as bordas superior, direita e inferior (sem vão retangular).
            TEST_ASSERT(matchesColor(sImg.pixelColor(sImg.width() - 5, 1), expectedBtnBg, 8),
                        std::string(label) + ": topo do spin button superior deve tocar rente a borda arredondada");
            TEST_ASSERT(matchesColor(sImg.pixelColor(sImg.width() - 2, 4), expectedBtnBg, 8),
                        std::string(label) + ": lateral direita do spin button superior deve tocar rente a borda");
            TEST_ASSERT(matchesColor(sImg.pixelColor(sImg.width() - 5, sImg.height() - 2), expectedBtnBg, 8),
                        std::string(label) + ": base do spin button inferior deve tocar rente a borda arredondada");
            TEST_ASSERT(matchesColor(sImg.pixelColor(sImg.width() - 2, sImg.height() - 5), expectedBtnBg, 8),
                        std::string(label) + ": lateral direita do spin button inferior deve tocar rente a borda");
            // Os vértices externos (cantos extremos da caixa) devem preservar o fundo externo fora do raio arredondado.
            TEST_ASSERT(matchesColor(sImg.pixelColor(sImg.width() - 1, 0), hostBg, 6),
                        std::string(label) + ": canto extremo superior direito deve permanecer recortado pelo raio da moldura");
            TEST_ASSERT(matchesColor(sImg.pixelColor(sImg.width() - 1, sImg.height() - 1), hostBg, 6),
                        std::string(label) + ": canto extremo inferior direito deve permanecer recortado pelo raio da moldura");
            if (isEnabledSpin) {
                TEST_ASSERT(!matchesColor(sImg.pixelColor(sImg.width() - 1, 0), expectedBtnBg, 6),
                            std::string(label) + ": preenchimento do botao superior nao deve vazar para o canto extremo");
                TEST_ASSERT(!matchesColor(sImg.pixelColor(sImg.width() - 1, sImg.height() - 1), expectedBtnBg, 6),
                            std::string(label) + ": preenchimento do botao inferior nao deve vazar para o canto extremo");
            }
        };
        verifyFlushRoundedSpinCorners(spinImg, true, "QSpinBox");

        const QImage dspinDisImg = dspinDis->grab().toImage();
        const int dspinMidY = dspinDisImg.height() / 2;
        TEST_ASSERT(hasDistinctArrowTriangle(dspinDisImg,
                                             QRect(dspinDisImg.width() - 20, 1, 18, dspinMidY - 1),
                                             disabledArrow,
                                             true),
                    "QDoubleSpinBox desabilitado deve renderizar triangulo superior sem PNGs externos");
        TEST_ASSERT(hasDistinctArrowTriangle(dspinDisImg,
                                             QRect(dspinDisImg.width() - 20, dspinMidY, 18, dspinDisImg.height() - dspinMidY - 1),
                                             disabledArrow,
                                             false),
                    "QDoubleSpinBox desabilitado deve renderizar triangulo inferior sem PNGs externos");
        verifyFlushRoundedSpinCorners(dspinDisImg, false, "QDoubleSpinBox desabilitado");

        AboutDialog about;
        about.setAttribute(Qt::WA_DontShowOnScreen, true);
        about.show();
        if (auto* aboutCombo = about.findChild<QComboBox*>()) {
            if (auto* tabs = about.findChild<QTabWidget*>()) {
                tabs->setCurrentWidget(aboutCombo->parentWidget());
            }
            pumpEvents(30);
            if (!shotDir.isEmpty()) {
                const QString tag = (theme == UiTheme::Light) ? QStringLiteral("light") : QStringLiteral("dark");
                aboutCombo->grab().save(shotDir + QStringLiteral("/about_combo_") + tag + QStringLiteral(".png"));
            }
            const QImage aboutComboImg = aboutCombo->grab().toImage();
            const QRect aboutArrowRect(aboutComboImg.width() - 23, 2, 21, aboutComboImg.height() - 4);
            TEST_ASSERT(hasDistinctArrowTriangle(aboutComboImg, aboutArrowRect, normalArrow, false),
                        "Combo do AboutDialog deve renderizar triangulo inferior sem PNGs externos");
        }
        about.close();
        host.close();
    }

    ThemeManager::instance().setTheme(originalTheme);
}

void testCollapsibleDocksWithSpinArrowButtons() {
    auto& traducao = omniframe::locale::GerenciadorTraducao::instance();
    const QString originalLang = traducao.codigoIdiomaAtual();
    const UiTheme originalTheme = ThemeManager::instance().currentTheme();
    const QString shotDir = qEnvironmentVariable("OMNIFRAME_UI_SHOT_DIR");

    TEST_ASSERT(traducao.definir_idioma(QStringLiteral("pt_BR")), "Deve ativar pt_BR para teste de docks");
    ThemeManager::instance().setTheme(UiTheme::Dark);

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1360, 860);
    w.show();
    pumpEvents(50);

    auto* inspectorBtn = w.findChild<QToolButton*>(QStringLiteral("inspectorCollapseButton"));
    auto* resultsBtn = w.findChild<QToolButton*>(QStringLiteral("resultsCollapseButton"));
    TEST_ASSERT(inspectorBtn != nullptr, "Botao spin arrow do Inspetor deve existir");
    TEST_ASSERT(resultsBtn != nullptr, "Botao spin arrow das Tabelas de Resultados deve existir");

    auto* inspectorDock = qobject_cast<QDockWidget*>(inspectorBtn->parentWidget() ? inspectorBtn->parentWidget()->parentWidget() : nullptr);
    auto* resultsDock = qobject_cast<QDockWidget*>(resultsBtn->parentWidget() ? resultsBtn->parentWidget()->parentWidget() : nullptr);
    TEST_ASSERT(inspectorDock != nullptr, "Inspector button deve estar hospedado na barra de titulo de seu QDockWidget");
    TEST_ASSERT(resultsDock != nullptr, "Results button deve estar hospedado na barra de titulo de seu QDockWidget");

    auto* inspectorScroll = inspectorDock->findChild<QScrollArea*>();
    auto* resultsTabs = resultsDock->findChild<QTabWidget*>();
    auto* viewport = w.findChild<Viewport3D*>();
    TEST_ASSERT(inspectorScroll != nullptr, "Conteudo ScrollArea do Inspetor deve existir");
    TEST_ASSERT(resultsTabs != nullptr, "QTabWidget das Tabelas de Resultados deve existir");
    TEST_ASSERT(viewport != nullptr, "Viewport3D deve existir");

    auto verifySpinButtonPixels = [&](QToolButton* btn, UiTheme theme, Qt::ArrowType expectedDir, const char* context) {
        const QColor arrowColor = (theme == UiTheme::Light) ? QColor(0x1e, 0x29, 0x3b) : QColor(0xe2, 0xe8, 0xf0);
        const QImage img = btn->grab().toImage();
        TEST_ASSERT(hasDistinctArrowTriangle(img, img.rect(), arrowColor, expectedDir), context);
    };

    // Estado inicial expandido: Inspetor aponta para esquerda (◀) e Resultados para baixo (▼).
    TEST_ASSERT(!w.isInspectorDockCollapsed(), "Inspetor deve iniciar expandido");
    TEST_ASSERT(!w.isResultsDockCollapsed(), "Tabelas de Resultados devem iniciar expandidas");
    TEST_ASSERT(inspectorDock->isVisible() && inspectorScroll->isVisible(), "Conteudo do Inspetor deve estar visivel inicialmente");
    TEST_ASSERT(resultsDock->isVisible() && resultsTabs->isVisible(), "Conteudo das Tabelas de Resultados deve estar visivel inicialmente");
    TEST_ASSERT(inspectorBtn->arrowType() == Qt::LeftArrow, "Seta inicial do Inspetor deve apontar para esquerda");
    TEST_ASSERT(resultsBtn->arrowType() == Qt::DownArrow, "Seta inicial de Resultados deve apontar para baixo");
    TEST_ASSERT(inspectorBtn->toolTip() == QStringLiteral("Recolher painel"), "Tooltip inicial do Inspetor deve ser Recolher painel");
    TEST_ASSERT(resultsBtn->toolTip() == QStringLiteral("Recolher painel"), "Tooltip inicial de Resultados deve ser Recolher painel");
    TEST_ASSERT(w.dockWidgetArea(inspectorDock) == Qt::LeftDockWidgetArea, "Inspetor inicia ancorado a esquerda");
    TEST_ASSERT(w.dockWidgetArea(resultsDock) == Qt::BottomDockWidgetArea, "Resultados inicia ancorado abaixo");

    verifySpinButtonPixels(inspectorBtn, UiTheme::Dark, Qt::LeftArrow, "Seta inicial do Inspetor (Dark) deve renderizar triangulo para esquerda");
    verifySpinButtonPixels(resultsBtn, UiTheme::Dark, Qt::DownArrow, "Seta inicial de Resultados (Dark) deve renderizar triangulo inferior");

    const int initialViewportWidth = viewport->width();
    const int initialViewportHeight = viewport->height();
    const int initialResultsHeight = resultsDock->height();

    if (!shotDir.isEmpty()) {
        w.grab().save(shotDir + QStringLiteral("/docks_expanded_dark.png"));
    }

    // 1) Recolhe Tabelas de Resultados via clique no botão spin arrow.
    resultsBtn->click();
    pumpEvents(50);

    TEST_ASSERT(w.isResultsDockCollapsed(), "Resultados deve estar recolhido apos clique");
    TEST_ASSERT(resultsBtn->arrowType() == Qt::UpArrow, "Seta de Resultados recolhido deve apontar para cima");
    TEST_ASSERT(resultsBtn->toolTip() == QStringLiteral("Expandir painel"), "Tooltip recolhido de Resultados deve ser Expandir painel");
    TEST_ASSERT(!resultsTabs->isVisible(), "QTabWidget deve ficar oculto ao recolher Resultados");
    TEST_ASSERT(resultsDock->isVisible() && resultsBtn->isVisible(), "Barra de titulo e botao devem continuar visiveis para expandir Resultados");
    TEST_ASSERT(resultsDock->height() < initialResultsHeight && resultsDock->height() <= 45,
                "Dock de Resultados deve encolher para a altura da barra de titulo");
    TEST_ASSERT(viewport->height() > initialViewportHeight, "Viewport3D deve ganhar altura vertical ao recolher Resultados");
    verifySpinButtonPixels(resultsBtn, UiTheme::Dark, Qt::UpArrow, "Seta recolhida de Resultados (Dark) deve renderizar triangulo superior");

    // 2) Recolhe Inspetor via clique no botão spin arrow.
    inspectorBtn->click();
    pumpEvents(50);

    TEST_ASSERT(w.isInspectorDockCollapsed(), "Inspetor deve estar recolhido apos clique");
    TEST_ASSERT(inspectorBtn->arrowType() == Qt::RightArrow, "Seta do Inspetor recolhido deve apontar para direita");
    TEST_ASSERT(inspectorBtn->toolTip() == QStringLiteral("Expandir painel"), "Tooltip recolhido do Inspetor deve ser Expandir painel");
    TEST_ASSERT(!inspectorScroll->isVisible(), "ScrollArea do Inspetor deve ficar oculta ao recolher");
    TEST_ASSERT(inspectorDock->isVisible() && inspectorBtn->isVisible(), "Barra de titulo e botao do Inspetor devem permanecer visiveis");
    TEST_ASSERT(w.dockWidgetArea(inspectorDock) == Qt::LeftDockWidgetArea,
                "Inspetor recolhido deve permanecer na lateral esquerda (LeftDockWidgetArea)");
    TEST_ASSERT(inspectorDock->features().testFlag(QDockWidget::DockWidgetVerticalTitleBar),
                "Inspetor recolhido na lateral deve ativar DockWidgetVerticalTitleBar");
    TEST_ASSERT(inspectorDock->width() <= 45 && inspectorDock->height() >= 400,
                "Inspetor recolhido deve formar faixa vertical compacta na lateral esquerda");
    TEST_ASSERT(viewport->width() > initialViewportWidth + 200,
                "Viewport3D deve recuperar a largura lateral (~285px) ao recolher o Inspetor");
    verifySpinButtonPixels(inspectorBtn, UiTheme::Dark, Qt::RightArrow, "Seta recolhida do Inspetor (Dark) deve renderizar triangulo para direita");

    auto assertFullCollapsedHeaderReadable = [&](UiTheme theme, const char* themeTag) {
        auto* inspLbl = inspectorDock->titleBarWidget()->findChild<QLabel*>(QStringLiteral("dockTitleLabel"));
        auto* resLbl = resultsDock->titleBarWidget()->findChild<QLabel*>(QStringLiteral("dockTitleLabel"));
        TEST_ASSERT(inspectorDock->titleBarWidget()->height() >= 400 && inspLbl &&
                        inspLbl->height() >= 200 && inspLbl->height() > inspLbl->width(),
                    std::string("Cabecalho recolhido do Inspetor deve ocupar a faixa vertical com rotulo rotacionado no tema ") + themeTag);
        // Confirma que há pixels do texto rotacionado pintados na metade superior do rótulo vertical.
        const QImage lblImg = inspLbl->grab().toImage();
        const QColor expectedText = (theme == UiTheme::Light) ? QColor(0x0f, 0x17, 0x2a) : QColor(0xe1, 0xe6, 0xeb);
        int textPixels = 0;
        const int scanH = std::min(lblImg.height(), 220);
        for (int y = 0; y < scanH; ++y) {
            for (int x = 0; x < lblImg.width(); ++x) {
                const QColor px = lblImg.pixelColor(x, y);
                if (std::abs(px.red() - expectedText.red()) <= 50 &&
                    std::abs(px.green() - expectedText.green()) <= 50 &&
                    std::abs(px.blue() - expectedText.blue()) <= 50) {
                    ++textPixels;
                }
            }
        }
        TEST_ASSERT(textPixels >= 40,
                    std::string("Rotulo rotacionado -90 graus do Inspetor deve renderizar texto visivel no tema ") + themeTag);
        TEST_ASSERT(resultsDock->titleBarWidget()->width() >= 900 && resLbl && resLbl->width() >= 140,
                    std::string("Cabecalho recolhido de Resultados deve ocupar toda a largura e exibir titulo no tema ") + themeTag);
    };
    assertFullCollapsedHeaderReadable(UiTheme::Dark, "Dark");

    if (!shotDir.isEmpty()) {
        w.grab().save(shotDir + QStringLiteral("/docks_collapsed_dark.png"));
    }

    // Verifica tema claro com painéis recolhidos.
    ThemeManager::instance().setTheme(UiTheme::Light);
    pumpEvents(40);
    assertFullCollapsedHeaderReadable(UiTheme::Light, "Light");
    verifySpinButtonPixels(inspectorBtn, UiTheme::Light, Qt::RightArrow, "Seta recolhida do Inspetor (Light) deve renderizar triangulo para direita");
    verifySpinButtonPixels(resultsBtn, UiTheme::Light, Qt::UpArrow, "Seta recolhida de Resultados (Light) deve renderizar triangulo superior");

    if (!shotDir.isEmpty()) {
        w.grab().save(shotDir + QStringLiteral("/docks_collapsed_light.png"));
    }

    // 3) Expande Inspetor novamente via clique.
    inspectorBtn->click();
    pumpEvents(50);
    TEST_ASSERT(!w.isInspectorDockCollapsed(), "Inspetor deve expandir ao clicar na seta para direita");
    TEST_ASSERT(inspectorBtn->arrowType() == Qt::LeftArrow, "Seta do Inspetor expandido deve voltar a apontar para esquerda");
    TEST_ASSERT(!inspectorDock->features().testFlag(QDockWidget::DockWidgetVerticalTitleBar),
                "Inspetor expandido deve retornar para barra de titulo horizontal");
    TEST_ASSERT(inspectorScroll->isVisible(), "Conteudo do Inspetor deve voltar a ficar visivel");
    TEST_ASSERT(w.dockWidgetArea(inspectorDock) == Qt::LeftDockWidgetArea, "Inspetor expandido deve permanecer em LeftDockWidgetArea");
    TEST_ASSERT(inspectorDock->width() >= 320, "Inspetor expandido deve restaurar largura minima de 320px");

    // 4) Expande Tabelas de Resultados novamente via clique.
    resultsBtn->click();
    pumpEvents(50);
    TEST_ASSERT(!w.isResultsDockCollapsed(), "Resultados deve expandir ao clicar na seta para cima");
    TEST_ASSERT(resultsBtn->arrowType() == Qt::DownArrow, "Seta de Resultados expandido deve voltar a apontar para baixo");
    TEST_ASSERT(resultsTabs->isVisible(), "QTabWidget deve voltar a ficar visivel");
    TEST_ASSERT(resultsDock->height() >= 140, "Dock de Resultados deve restaurar altura util");
    verifySpinButtonPixels(inspectorBtn, UiTheme::Light, Qt::LeftArrow, "Seta expandida do Inspetor (Light) deve renderizar triangulo para esquerda");
    verifySpinButtonPixels(resultsBtn, UiTheme::Light, Qt::DownArrow, "Seta expandida de Resultados (Light) deve renderizar triangulo inferior");

    // 5) Recolhimento e expansão com dock flutuante.
    resultsDock->setFloating(true);
    pumpEvents(40);
    resultsBtn->click();
    pumpEvents(40);
    TEST_ASSERT(w.isResultsDockCollapsed() && resultsDock->isFloating(), "Resultados flutuante deve permanecer flutuante ao recolher");
    TEST_ASSERT(!resultsTabs->isVisible() && resultsDock->height() <= 55, "Resultados flutuante recolhido deve ficar compacto");
    resultsBtn->click();
    pumpEvents(40);
    TEST_ASSERT(!w.isResultsDockCollapsed() && resultsTabs->isVisible() && resultsDock->height() >= 140,
                "Resultados flutuante expandido deve restaurar altura e conteudo");
    resultsDock->setFloating(false);
    pumpEvents(40);

    inspectorDock->setFloating(true);
    pumpEvents(40);
    inspectorBtn->click();
    pumpEvents(40);
    TEST_ASSERT(w.isInspectorDockCollapsed() && inspectorDock->isFloating(), "Inspetor flutuante deve permanecer flutuante ao recolher");
    TEST_ASSERT(!inspectorScroll->isVisible() && inspectorDock->height() <= 55, "Inspetor flutuante recolhido deve ficar compacto");
    // Reacopla enquanto ainda está recolhido -> deve voltar como faixa vertical na lateral esquerda.
    inspectorDock->setFloating(false);
    pumpEvents(50);
    TEST_ASSERT(w.isInspectorDockCollapsed() && w.dockWidgetArea(inspectorDock) == Qt::LeftDockWidgetArea &&
                inspectorDock->width() <= 45 && inspectorDock->height() >= 400,
                "Inspetor reacoplado enquanto recolhido deve manter faixa vertical na lateral esquerda");
    inspectorBtn->click();
    pumpEvents(50);
    TEST_ASSERT(!w.isInspectorDockCollapsed() && w.dockWidgetArea(inspectorDock) == Qt::LeftDockWidgetArea &&
                inspectorDock->width() >= 320,
                "Inspetor expandido apos reacoplamento deve voltar para area esquerda");

    // 6) Tradução em tempo real (pt_BR <-> en_US) para títulos e tooltips.
    TEST_ASSERT(traducao.definir_idioma(QStringLiteral("en_US")), "Deve alternar idioma para en_US");
    pumpEvents(20);
    auto* inspectorTitleLbl = inspectorDock->titleBarWidget()->findChild<QLabel*>(QStringLiteral("dockTitleLabel"));
    auto* resultsTitleLbl = resultsDock->titleBarWidget()->findChild<QLabel*>(QStringLiteral("dockTitleLabel"));
    TEST_ASSERT(inspectorTitleLbl && inspectorTitleLbl->text() == QStringLiteral("Inspector & Quick Input"),
                "Titulo customizado do Inspetor deve atualizar para Ingles");
    TEST_ASSERT(resultsTitleLbl && resultsTitleLbl->text() == QStringLiteral("Result Tables & Structural Diagnostics"),
                "Titulo customizado de Resultados deve atualizar para Ingles");
    TEST_ASSERT(inspectorBtn->toolTip() == QStringLiteral("Collapse panel"),
                "Tooltip do botao expandido deve atualizar para Ingles");
    resultsBtn->click();
    pumpEvents(30);
    TEST_ASSERT(resultsBtn->toolTip() == QStringLiteral("Expand panel"),
                "Tooltip do botao recolhido deve atualizar para Ingles");

    // GuidedSessionDialog::startSession deve expandir painéis quando recolhidos.
    w.setInspectorDockCollapsed(true);
    w.setResultsDockCollapsed(true);
    pumpEvents(30);
    TEST_ASSERT(w.isInspectorDockCollapsed() && w.isResultsDockCollapsed(), "Ambos os docks recolhidos antes do Tour");
    TEST_ASSERT(QMetaObject::invokeMethod(&w, "onGuidedSession"), "onGuidedSession deve abrir o dialog guiado");
    pumpEvents(40);
    auto* guided = w.guidedSessionDialog();
    TEST_ASSERT(guided != nullptr, "guidedSessionDialog deve existir apos onGuidedSession");
    guided->startSession(120);
    pumpEvents(40);
    TEST_ASSERT(!w.isInspectorDockCollapsed() && !w.isResultsDockCollapsed(),
                "GuidedSessionDialog::startSession deve expandir docks recolhidos");
    guided->stopSession();
    guided->close();

    w.close();
    pumpEvents(40);
    ThemeManager::instance().setTheme(originalTheme);
    traducao.definir_idioma(originalLang);
}

void testResultTablesLinceuConfigAndFeatures() {
    auto& traducao = locale::GerenciadorTraducao::instance();
    const QString originalLang = traducao.obter_idioma_atual();
    const UiTheme originalTheme = ThemeManager::instance().currentTheme();

    traducao.definir_idioma(QStringLiteral("pt_BR"));
    ThemeManager::instance().setTheme(UiTheme::Dark);
    pumpEvents(30);

    MainWindow w;
    w.show();
    w.loadExampleBuilding();
    pumpEvents(60);

    auto* dispTable = w.findChild<QTableView*>(QStringLiteral("dispTable"));
    auto* reacTable = w.findChild<QTableView*>(QStringLiteral("reacTable"));
    auto* forcesTable = w.findChild<QTableView*>(QStringLiteral("forcesTable"));
    auto* continuumTable = w.findChild<QTableView*>(QStringLiteral("continuumTable"));
    ui::ResultTableConfigurator* cfg = w.resultTableConfigurator();

    TEST_ASSERT(dispTable && reacTable && forcesTable && continuumTable && cfg &&
                    dispTable->model() && reacTable->model() && forcesTable->model() && continuumTable->model(),
                "Todas as 4 tabelas de resultados e o configurador devem existir");

    TEST_ASSERT(waitUntil([&]() {
        return dispTable->model()->rowCount() > 2 && reacTable->model()->rowCount() > 2 &&
               forcesTable->model()->rowCount() > 2;
    }, 15000), "Tabelas de resultados devem ser populadas após resolução do exemplo");
    pumpEvents(40);

    // Popula tabela de contínuos com dados representativos (cascas + sólido) para cobertura integral das 4 abas
    struct ContSample { int id; QString tipo; QString vm; QString p1; QString p3; QString s5; QString s6; QString s7; QString cat; };
    const ContSample samples[3] = {
        {10, QStringLiteral("Q4"),   QStringLiteral("240.00"), QStringLiteral("260.00"), QStringLiteral("-40.00"),
         QStringLiteral("125.40 kN/m"), QStringLiteral("30.00 kN/m"), QStringLiteral("2.150 kNm/m"), QStringLiteral("shell")},
        {2,  QStringLiteral("Q4"),   QStringLiteral("85.00"),  QStringLiteral("90.00"),  QStringLiteral("-12.00"),
         QStringLiteral("9.50 kN/m"),   QStringLiteral("4.20 kN/m"),  QStringLiteral("0.420 kNm/m"), QStringLiteral("shell")},
        {5,  QStringLiteral("C3D8"), QStringLiteral("510.00"), QStringLiteral("540.00"), QStringLiteral("-80.00"),
         QStringLiteral("310.20 kPa"),  QStringLiteral("110.00 kPa"), QStringLiteral("45.00 kPa"),   QStringLiteral("solid")}
    };
    if (auto* contModel = w.resultTableModel(ui::ResultTableKind::ContinuumStress)) {
        std::vector<ui::ResultTableRow> contRows;
        contRows.reserve(3);
        for (int r = 0; r < 3; ++r) {
            ui::ResultTableRow row;
            row.cells = {QString::number(samples[r].id), samples[r].tipo, samples[r].vm, samples[r].p1,
                         samples[r].p3, samples[r].s5, samples[r].s6, samples[r].s7};
            row.category = samples[r].cat;
            row.entityId = static_cast<quint64>(samples[r].id);
            row.hasEntityId = true;
            contRows.push_back(std::move(row));
        }
        contModel->setRows(std::move(contRows));
    }
    cfg->afterTablePopulated(continuumTable);

    // 1) Configuração estrutural aplicada uniformemente às 4 tabelas
    auto* resultsTabs = qobject_cast<QTabWidget*>(dispTable->parentWidget() ? dispTable->parentWidget()->parentWidget() : nullptr);
    const QList<QTableView*> allTables = {dispTable, reacTable, forcesTable, continuumTable};
    for (QTableView* tbl : allTables) {
        if (resultsTabs != nullptr) {
            resultsTabs->setCurrentWidget(tbl);
            pumpEvents(15);
        }
        TEST_ASSERT(tbl->alternatingRowColors(), "alternatingRowColors ativo");
        TEST_ASSERT(!tbl->isSortingEnabled(), "sorting Qt nativo desligado em favor do sort inteligente");
        TEST_ASSERT(tbl->selectionBehavior() == QAbstractItemView::SelectItems, "SelectItems");
        TEST_ASSERT(tbl->selectionMode() == QAbstractItemView::ExtendedSelection, "ExtendedSelection");
        TEST_ASSERT(tbl->editTriggers() == QAbstractItemView::NoEditTriggers, "NoEditTriggers");
        TEST_ASSERT(tbl->isCornerButtonEnabled(), "Corner button habilitado");
        TEST_ASSERT(!tbl->verticalHeader()->isHidden(), "Vertical header configurado como visível");
        if (resultsTabs != nullptr) {
            TEST_ASSERT(tbl->verticalHeader()->isVisible(), "Vertical header visível na aba ativa");
        }
        TEST_ASSERT(tbl->horizontalHeader()->sectionsMovable(), "Colunas movíveis");
        TEST_ASSERT(!tbl->horizontalHeader()->stretchLastSection(), "StretchLastSection desabilitado");
        TEST_ASSERT(tbl->horizontalHeader()->minimumSectionSize() == 10, "Largura mínima de seção 10px");
        TEST_ASSERT(tbl->horizontalHeader()->font().bold(), "Fonte negrito no cabeçalho");
        TEST_ASSERT(tbl->itemDelegate() != nullptr && !tbl->styleSheet().isEmpty(), "Delegate e QSS configurados");
    }
    if (resultsTabs != nullptr) {
        resultsTabs->setCurrentWidget(dispTable);
        pumpEvents(15);
    }

    // 2) Corner button -> toggle selecionar tudo / desmarcar tudo; clique direto em células, cabeçalho de linha e cabeçalho de coluna
    if (auto* corner = dispTable->findChild<QAbstractButton*>(QString(), Qt::FindDirectChildrenOnly)) {
        corner->click();
        pumpEvents(20);
        TEST_ASSERT(dispTable->selectionModel() != nullptr &&
                        dispTable->selectionModel()->selectedIndexes().size() ==
                            dispTable->model()->rowCount() * dispTable->model()->columnCount(),
                    "Primeiro clique no botão de canto deve selecionar todas as células da tabela");
        corner->click();
        pumpEvents(20);
        TEST_ASSERT(dispTable->selectionModel() != nullptr &&
                        dispTable->selectionModel()->selectedIndexes().isEmpty(),
                    "Segundo clique no botão de canto deve desmarcar todas as células da tabela");
    }

    auto sendMouse = [](QWidget* target, QEvent::Type type, const QPointF& pos,
                        Qt::MouseButton btn, Qt::MouseButtons buttons) {
        const QPointF globalPos = target->mapToGlobal(pos);
        QMouseEvent ev(type, pos, globalPos, btn, buttons, Qt::NoModifier);
        QApplication::sendEvent(target, &ev);
    };

    // Seleção direta por arraste em múltiplas células
    dispTable->clearSelection();
    dispTable->scrollToTop();
    pumpEvents(10);
    {
        const QPointF p0 = dispTable->visualRect(dispTable->model()->index(0, 0)).center();
        const QPointF p1 = dispTable->visualRect(dispTable->model()->index(1, 2)).center();
        sendMouse(dispTable->viewport(), QEvent::MouseButtonPress, p0, Qt::LeftButton, Qt::LeftButton);
        sendMouse(dispTable->viewport(), QEvent::MouseMove, p1, Qt::NoButton, Qt::LeftButton);
        sendMouse(dispTable->viewport(), QEvent::MouseButtonRelease, p1, Qt::LeftButton, Qt::NoButton);
        pumpEvents(15);
        TEST_ASSERT(dispTable->selectionModel()->selectedIndexes().size() == 6,
                    "Arraste direto entre (0,0) e (1,2) deve selecionar 6 células");
    }

    // Seleção de linha via cabeçalho vertical
    dispTable->clearSelection();
    if (QHeaderView* vh = dispTable->verticalHeader(); vh != nullptr && vh->viewport() != nullptr) {
        const QPointF rowHdrPt(vh->viewport()->width() * 0.5,
                               dispTable->rowViewportPosition(1) + dispTable->rowHeight(1) * 0.5);
        sendMouse(vh->viewport(), QEvent::MouseButtonPress, rowHdrPt, Qt::LeftButton, Qt::LeftButton);
        sendMouse(vh->viewport(), QEvent::MouseButtonRelease, rowHdrPt, Qt::LeftButton, Qt::NoButton);
        pumpEvents(15);
        TEST_ASSERT(dispTable->selectionModel()->isRowSelected(1, QModelIndex()),
                    "Clique no cabeçalho de linha deve selecionar toda a linha 1");
    }

    // Seleção de coluna via cabeçalho horizontal
    dispTable->clearSelection();
    emit dispTable->horizontalHeader()->sectionClicked(2);
    pumpEvents(15);
    TEST_ASSERT(dispTable->selectionModel()->isColumnSelected(2, QModelIndex()),
                "Clique no cabeçalho com ordenação desligada deve selecionar a coluna 2");
    TEST_ASSERT(!dispTable->selectionModel()->isColumnSelected(0, QModelIndex()),
                "Coluna 0 não deve permanecer selecionada após clique simples na coluna 2");

    // 3) Quebra dinâmica de cabeçalho e ajuste de altura ao reduzir largura da coluna
    const int initialHeaderH = forcesTable->horizontalHeader()->minimumHeight();
    forcesTable->setColumnWidth(1, 52); // "N min/max (kN)" precisa quebrar em múltiplas linhas
    pumpEvents(20);
    TEST_ASSERT(forcesTable->model()->headerData(1, Qt::Horizontal, Qt::DisplayRole).toString().contains(QLatin1Char('\n')),
                "Cabeçalho estreito deve quebrar em múltiplas linhas");
    TEST_ASSERT(forcesTable->horizontalHeader()->minimumHeight() > initialHeaderH,
                "Altura do cabeçalho deve crescer conforme número máximo de linhas");
    cfg->resetColumns(forcesTable);

    // 4) Ordenação inteligente + preservação fixa da linha "Σ Reações" ao fim em Ascending e Descending
    cfg->setSortingModeEnabled(reacTable, true);
    cfg->sortTableByColumn(reacTable, 0, Qt::AscendingOrder);
    TEST_ASSERT(reacTable->model()->index(reacTable->model()->rowCount() - 1, 0).data().toString() == QStringLiteral("Σ Reações"),
                "Linha Σ Reações deve permanecer na última linha após ordenação ascendente");
    cfg->sortTableByColumn(reacTable, 0, Qt::DescendingOrder);
    TEST_ASSERT(reacTable->model()->index(reacTable->model()->rowCount() - 1, 0).data().toString() == QStringLiteral("Σ Reações"),
                "Linha Σ Reações deve permanecer na última linha após ordenação descendente");
    cfg->setSortingModeEnabled(reacTable, false);

    // Intervalo [min; max] em Esforços nas Barras
    cfg->setSortingModeEnabled(forcesTable, true);
    cfg->sortTableByColumn(forcesTable, 1, Qt::AscendingOrder);
    auto extractIntervalMin = [](const QString& s) {
        QString inner = s.trimmed();
        inner.remove(QLatin1Char('['));
        inner.remove(QLatin1Char(']'));
        return inner.split(QLatin1Char(';')).value(0).trimmed().toDouble();
    };
    TEST_ASSERT(extractIntervalMin(forcesTable->model()->index(0, 1).data().toString()) <=
                    extractIntervalMin(forcesTable->model()->index(forcesTable->model()->rowCount() - 1, 1).data().toString()),
                "Ordenação de intervalos [min; max] deve ordenar pelo valor numérico inicial");
    cfg->setSortingModeEnabled(forcesTable, false);

    // Número com unidades na tabela de contínuos ("9.50 kN/m" < "125.40 kN/m" < "310.20 kPa")
    cfg->sortTableByColumn(continuumTable, 5, Qt::AscendingOrder);
    TEST_ASSERT(continuumTable->model()->index(0, 5).data().toString().startsWith(QStringLiteral("9.50")),
                "Ordenação numérica com unidades deve colocar 9.50 kN/m antes de 125.40 kN/m");

    // 5) Ordem das linhas (PrependTop / Inverter Linhas) mantendo Σ Reações no final
    const QString firstDispBefore = dispTable->model()->index(0, 0).data().toString();
    const QString lastDispBefore = dispTable->model()->index(dispTable->model()->rowCount() - 1, 0).data().toString();
    cfg->setRowOrderMode(dispTable, ui::TableRowOrderMode::PrependTop);
    TEST_ASSERT(dispTable->model()->index(0, 0).data().toString() == lastDispBefore,
                "Modo Adicionar no Topo deve exibir último nó no topo");
    cfg->setRowOrderMode(dispTable, ui::TableRowOrderMode::AppendBottom);
    TEST_ASSERT(dispTable->model()->index(0, 0).data().toString() == firstDispBefore,
                "Voltar para Adicionar no Fim deve restaurar primeiro nó");
    cfg->invertTableRows(reacTable);
    TEST_ASSERT(reacTable->model()->index(reacTable->model()->rowCount() - 1, 0).data().toString() == QStringLiteral("Σ Reações"),
                "Inverter linhas na tabela de reações deve preservar Σ Reações no final");

    // 6) Visibilidade de colunas, atalho Ctrl+Shift+A, cores de categoria, contraste e renderização
    cfg->setColumnVisible(dispTable, 3, false);
    TEST_ASSERT(dispTable->isColumnHidden(3), "Coluna 3 deve ficar oculta");
    cfg->showAllColumns(dispTable);
    TEST_ASSERT(!dispTable->isColumnHidden(3), "showAllColumns deve reexibir coluna 3");

    QKeyEvent colorAllEvent(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::sendEvent(dispTable->viewport(), &colorAllEvent);
    TEST_ASSERT(cfg->isColumnColored(dispTable, 0) && cfg->showColumnColors(dispTable),
                "Atalho Ctrl+Shift+A deve colorir todas as colunas");

    cfg->setCategoryColor(dispTable, QStringLiteral("node"), QColor(250, 210, 80));
    TEST_ASSERT(cfg->categoryColor(dispTable, QStringLiteral("node")) == QColor(250, 210, 80),
                "Cor customizada por categoria deve persistir no estado da tabela");

    TEST_ASSERT(ui::ResultTableConfigurator::idealTextColorForBackground(QColor(240, 240, 240)) == QColor(Qt::black),
                "Fundo claro deve gerar texto preto");
    TEST_ASSERT(ui::ResultTableConfigurator::idealTextColorForBackground(QColor(15, 25, 45)) == QColor(Qt::white),
                "Fundo escuro deve gerar texto branco");

    // Render offscreen para validar delegate em tempo de pintura
    QPixmap paintCheck(dispTable->viewport()->size().expandedTo(QSize(200, 120)));
    dispTable->viewport()->render(&paintCheck);

    // 7) Duplo clique em nó na tabela de deslocamentos seleciona o nó no modelo
    bool nodeSignalFired = false;
    unsigned int clickedNodeId = 0;
    const QMetaObject::Connection conn = QObject::connect(
        cfg, &ui::ResultTableConfigurator::nodeDoubleClicked, &w, [&](unsigned int id) {
            nodeSignalFired = true;
            clickedNodeId = id;
        });
    const unsigned int expectedNodeId = dispTable->model()->index(0, 0).data().toUInt();
    emit dispTable->doubleClicked(dispTable->model()->index(0, 0));
    QObject::disconnect(conn);
    TEST_ASSERT(nodeSignalFired && clickedNodeId == expectedNodeId,
                "Duplo clique na linha deve emitir nodeDoubleClicked com o ID correto");

    // 8) Atalho Ctrl+Shift+C (copiar com cabeçalhos), cópia integral e menu de contexto completo
    dispTable->clearSelection();
    dispTable->setCurrentIndex(dispTable->model()->index(0, 0));
    dispTable->selectionModel()->select(dispTable->model()->index(0, 0), QItemSelectionModel::Select);
    QKeyEvent copyHdrEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::sendEvent(dispTable->viewport(), &copyHdrEvent);
    if (QClipboard* cb = QApplication::clipboard()) {
        const QString copied = cb->text();
        TEST_ASSERT(copied.contains(QStringLiteral("Nó")) &&
                        copied.contains(dispTable->model()->index(0, 0).data().toString()),
                    "Ctrl+Shift+C deve copiar cabeçalho e valor selecionado");
    }

    QMenu* ctxMenu = cfg->createTableContextMenu(forcesTable, &w, false);
    TEST_ASSERT(ctxMenu != nullptr && ctxMenu->actions().size() >= 10,
                "Menu de contexto deve conter opções completas de configuração, cores, ordem, cópia e exportação");
    delete ctxMenu;

    // 9) Exportação CSV, TXT, JSON, XML e XLSX (+ filtros de colunas ativas e seleção)
    QTemporaryDir tmpDir;
    TEST_ASSERT(tmpDir.isValid(), "QTemporaryDir válido para testes de exportação");
    for (const QString& ext : {QStringLiteral("csv"), QStringLiteral("txt"),
                               QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("xlsx")}) {
        const QString outPath = tmpDir.filePath(QStringLiteral("export_test.") + ext);
        QString err;
        TEST_ASSERT(cfg->exportTableToFile(continuumTable, outPath, &err),
                    std::string("Exportação deve suceder para .") + ext.toStdString());
        QFile f(outPath);
        TEST_ASSERT(f.open(QIODevice::ReadOnly) && f.size() > 0, "Arquivo exportado não pode estar vazio");
        if (ext == QStringLiteral("xlsx")) {
            const QByteArray bytes = f.readAll();
            TEST_ASSERT(bytes.startsWith("PK\x03\x04") && bytes.contains("xl/worksheets/sheet1.xml"),
                        "Arquivo .xlsx deve conter assinatura ZIP e planilha OpenXML válida");
        }
    }

    // Exportação filtrada (apenas colunas ativas + apenas células selecionadas)
    cfg->setColumnVisible(continuumTable, 2, false);
    cfg->setExportOnlyActiveColumns(continuumTable, true);
    continuumTable->clearSelection();
    continuumTable->selectionModel()->select(continuumTable->model()->index(0, 0), QItemSelectionModel::Select);
    continuumTable->selectionModel()->select(continuumTable->model()->index(0, 5), QItemSelectionModel::Select);
    cfg->setExportOnlySelectedCells(continuumTable, true);
    const QString filteredJson = tmpDir.filePath(QStringLiteral("filtered.json"));
    TEST_ASSERT(cfg->exportTableToFile(continuumTable, filteredJson),
                "Exportação filtrada para JSON deve suceder");
    {
        QFile jf(filteredJson);
        TEST_ASSERT(jf.open(QIODevice::ReadOnly), "Leitura JSON filtrado");
        const QJsonDocument doc = QJsonDocument::fromJson(jf.readAll());
        TEST_ASSERT(doc.isArray() && doc.array().size() == 1,
                    "Exportar seleção deve gerar exatamente 1 linha no JSON");
    }

    // 10) Alternância de tema e idioma mantendo cabeçalhos, seleção e paleta sincronizados
    cfg->setAllColumnsColored(dispTable, false);
    if (resultsTabs != nullptr) {
        resultsTabs->setCurrentWidget(dispTable);
    }
    ThemeManager::instance().setTheme(UiTheme::Light);
    pumpEvents(25);
    TEST_ASSERT(dispTable->palette().color(QPalette::Base) == QColor(255, 255, 255),
                "Tema claro deve aplicar paleta clara na tabela");
    TEST_ASSERT(dispTable->palette().color(QPalette::HighlightedText).lightness() < 50,
                "HighlightedText no tema claro deve ser escuro para legibilidade da seleção");

    // Verifica renderização dinâmica de fonte escura em grupo de células selecionadas no tema claro
    dispTable->clearSelection();
    dispTable->scrollToTop();
    dispTable->selectionModel()->select(
        QItemSelection(dispTable->model()->index(0, 0), dispTable->model()->index(1, 2)),
        QItemSelectionModel::Select);
    pumpEvents(25);
    {
        QImage vpImg(dispTable->viewport()->size().expandedTo(QSize(200, 120)), QImage::Format_ARGB32);
        vpImg.fill(Qt::white);
        dispTable->viewport()->render(&vpImg);

        auto hasDarkGlyphsLightBgAndBlueBorder = [&](int row, int col) {
            const QRect fullRect = dispTable->visualRect(dispTable->model()->index(row, col))
                                       .intersected(vpImg.rect());
            const QRect innerRect = fullRect.adjusted(3, 3, -3, -3);
            if (!fullRect.isValid() || !innerRect.isValid()) {
                return false;
            }
            int darkPixels = 0;
            int lightPixels = 0;
            for (int y = innerRect.top(); y <= innerRect.bottom(); ++y) {
                for (int x = innerRect.left(); x <= innerRect.right(); ++x) {
                    const int gray = qGray(vpImg.pixel(x, y));
                    if (gray < 80) {
                        ++darkPixels;
                    } else if (gray > 220) {
                        ++lightPixels;
                    }
                }
            }
            int blueBorderPixels = 0;
            for (int y = fullRect.top(); y <= fullRect.bottom(); ++y) {
                for (int x = fullRect.left(); x <= fullRect.right(); ++x) {
                    if (innerRect.contains(x, y)) {
                        continue;
                    }
                    const QColor px = vpImg.pixelColor(x, y);
                    if (px.blue() > px.red() + 40) {
                        ++blueBorderPixels;
                    }
                }
            }
            return darkPixels >= 6 && lightPixels >= 10 && blueBorderPixels >= 12;
        };
        TEST_ASSERT(hasDarkGlyphsLightBgAndBlueBorder(0, 0) && hasDarkGlyphsLightBgAndBlueBorder(1, 1),
                    "Células selecionadas no tema claro devem renderizar texto escuro e borda azul de seleção dinamicamente");
    }

    // 11) Layouts múltiplos do painel de resultados (1, 2, 3 e 4 layouts no menu Configurações)
    QMenu* layoutsMenu = w.findChild<QMenu*>(QStringLiteral("resultsLayoutsMenu"));
    TEST_ASSERT(layoutsMenu != nullptr && w.resultsLayoutCount() == 1,
                "Submenu de layouts deve existir no menu Configurações iniciando em 1 layout");
    TEST_ASSERT(w.resultsLayoutTabWidget(0) != nullptr && w.resultsLayoutTabWidget(0)->count() == 4,
                "Com 1 layout todas as 4 tabelas devem estar no primeiro layout");

    w.setResultsLayoutCount(2);
    pumpEvents(20);
    TEST_ASSERT(w.resultsLayoutCount() == 2 &&
                    w.resultsLayoutTabWidget(0)->isVisible() &&
                    w.resultsLayoutTabWidget(1)->isVisible() &&
                    !w.resultsLayoutTabWidget(2)->isVisible() &&
                    w.resultsLayoutTabWidget(0)->count() == 2 &&
                    w.resultsLayoutTabWidget(1)->count() == 2,
                "Com 2 layouts dois conjuntos de abas devem ficar visíveis com distribuição padrão 2+2");

    // Disponibilizar mesma tabela simultaneamente nos 2 layouts e verificar sincronização de linhas
    w.setResultsLayoutTableVisible(1, ui::ResultTableKind::Displacements, true);
    pumpEvents(20);
    TEST_ASSERT(w.resultsLayoutTabWidget(1)->count() == 3,
                "Personalização de tabelas no 2º layout deve incluir Deslocamentos Nodais sem remover do 1º");
    auto* extraDisp = qobject_cast<QTableView*>(w.resultsLayoutTabWidget(1)->widget(0));
    TEST_ASSERT(extraDisp != nullptr && extraDisp != dispTable &&
                    extraDisp->model() != nullptr && dispTable->model() != nullptr &&
                    extraDisp->model()->rowCount() == dispTable->model()->rowCount(),
                "Cópia de tabela em layout adicional deve sincronizar os resultados carregados");
    {
        auto* dispProxy = qobject_cast<ui::ResultTableProxyModel*>(dispTable->model());
        auto* extraProxy = qobject_cast<ui::ResultTableProxyModel*>(extraDisp->model());
        auto* sharedModel = w.resultTableModel(ui::ResultTableKind::Displacements);
        TEST_ASSERT(dispProxy != nullptr && extraProxy != nullptr && sharedModel != nullptr &&
                        dispProxy != extraProxy &&
                        dispProxy->sourceModel() == sharedModel &&
                        extraProxy->sourceModel() == sharedModel,
                    "Múltiplos layouts devem compartilhar o mesmo ResultTableModel com proxies independentes");
        const QString mainFirstBeforeSort = dispTable->model()->index(0, 0).data().toString();
        cfg->setSortingModeEnabled(extraDisp, true);
        cfg->sortTableByColumn(extraDisp, 0, Qt::DescendingOrder);
        TEST_ASSERT(dispTable->model()->index(0, 0).data().toString() == mainFirstBeforeSort,
                    "Ordenar tabela no layout adicional via proxy não deve alterar a ordem da tabela principal");
    }

    auto verifyNoTabScrollRectangles = [&](int activeLayouts, const QList<int>& splitSizes) {
        w.setResultsLayoutCount(activeLayouts);
        pumpEvents(20);
        if (w.resultsSplitter() != nullptr) {
            w.resultsSplitter()->setSizes(splitSizes);
            pumpEvents(25);
        }
        for (int L = 0; L < activeLayouts; ++L) {
            QTabWidget* tabW = w.resultsLayoutTabWidget(L);
            TEST_ASSERT(tabW != nullptr && tabW->isVisible(),
                        "Container do layout ativo deve permanecer visível");
            TEST_ASSERT(!tabW->usesScrollButtons(),
                        "QTabWidget de resultados não deve usar botões de rolagem");
            TEST_ASSERT(tabW->elideMode() == Qt::ElideRight,
                        "QTabWidget de resultados deve elidir textos à direita ao estreitar o painel");
            QTabBar* tb = tabW->tabBar();
            TEST_ASSERT(tb != nullptr && !tb->usesScrollButtons() && tb->elideMode() == Qt::ElideRight,
                        "QTabBar não deve usar botões de rolagem mesmo após reduzir a largura do splitter");
            for (QToolButton* scrollerBtn : tb->findChildren<QToolButton*>()) {
                TEST_ASSERT(!scrollerBtn->isVisible() || scrollerBtn->width() <= 0 || scrollerBtn->height() <= 0,
                            "QTabBar não deve exibir retângulos/botões de rolagem ao mover o splitter");
            }
            for (int t = 0; t < tabW->count(); ++t) {
                TEST_ASSERT(!tabW->tabToolTip(t).isEmpty(),
                            "Cada aba deve possuir tooltip completo para modo elidido");
            }
        }
    };

    verifyNoTabScrollRectangles(2, {900, 90});
    verifyNoTabScrollRectangles(3, {500, 500, 90});
    TEST_ASSERT(w.resultsLayoutTabWidget(0)->isVisible() &&
                    w.resultsLayoutTabWidget(1)->isVisible() &&
                    w.resultsLayoutTabWidget(2)->isVisible() &&
                    !w.resultsLayoutTabWidget(3)->isVisible(),
                "Com 3 layouts exatamente três containers de tabelas devem ficar visíveis");

    verifyNoTabScrollRectangles(4, {350, 350, 350, 90});
    for (int L = 0; L < 4; ++L) {
        TEST_ASSERT(w.resultsLayoutTabWidget(L)->isVisible() &&
                        w.resultsLayoutTabWidget(L)->count() == 1,
                    "Com 4 layouts cada um dos quatro painéis deve estar visível com sua tabela padrão");
    }

    traducao.definir_idioma(QStringLiteral("en_US"));
    pumpEvents(25);
    TEST_ASSERT(dispTable->model()->headerData(0, Qt::Horizontal, Qt::DisplayRole).toString().simplified() ==
                    QStringLiteral("Node"),
                "Troca de idioma deve atualizar cabeçalhos das tabelas via configurador");
    TEST_ASSERT(layoutsMenu->title() == QStringLiteral("Result Tables Panel Layouts"),
                "Troca de idioma deve retraduzir o menu de layouts das tabelas");
    TEST_ASSERT(w.resultsLayoutTabWidget(0)->tabToolTip(0) == QStringLiteral("Nodal Displacements") &&
                    w.resultsLayoutTabWidget(1)->tabToolTip(0) == QStringLiteral("Support Reactions") &&
                    w.resultsLayoutTabWidget(2)->tabToolTip(0) == QStringLiteral("Member Forces") &&
                    w.resultsLayoutTabWidget(3)->tabToolTip(0) == QStringLiteral("2D/3D Forces (Shell/Solid)"),
                "Troca de idioma deve atualizar dinamicamente os tooltips das abas dos layouts");

    w.resetResultsLayoutsToDefault();
    pumpEvents(15);
    TEST_ASSERT(w.resultsLayoutCount() == 1 &&
                    w.resultsLayoutTabWidget(0)->count() == 4 &&
                    !w.resultsLayoutTabWidget(1)->isVisible(),
                "resetResultsLayoutsToDefault deve restaurar 1 layout com todas as 4 tabelas");

    for (QTableView* tbl : allTables) {
        cfg->resetAllDefaults(tbl);
    }

    // 12) Persistência universal unificada e recarga automática em AppData/Local/OmniFrame/user_preferences.json
    traducao.definir_idioma(QStringLiteral("pt_BR"));
    pumpEvents(20);
    const QString userPrefsPath = MainWindow::userPreferencesFilePath();
    TEST_ASSERT(QDir::fromNativeSeparators(userPrefsPath).endsWith(
                    QStringLiteral("/OmniFrame/user_preferences.json")) &&
                    !QDir::fromNativeSeparators(userPrefsPath).endsWith(
                        QStringLiteral("/OmniFrame/OmniFrame/user_preferences.json")),
                "Caminho do arquivo universal de preferências deve apontar para AppData/Local/OmniFrame/user_preferences.json");
    TEST_ASSERT(userPrefsPath == io::UserPreferencesStore::userPreferencesFilePath(),
                "MainWindow::userPreferencesFilePath deve delegar ao UserPreferencesStore unificado");

    w.setResultsLayoutCount(3);
    pumpEvents(20);
    w.setResultsLayoutTableVisible(0, ui::ResultTableKind::ContinuumStress, true);
    pumpEvents(20);
    w.resultsLayoutTabWidget(0)->setCurrentIndex(1);
    if (w.resultsSplitter() != nullptr) {
        w.resultsSplitter()->setSizes({480, 320, 200});
    }
    pumpEvents(20);

    cfg->setColumnVisible(dispTable, 2, false);
    dispTable->setColumnWidth(1, 148);
    if (QHeaderView* hh = dispTable->horizontalHeader()) {
        hh->moveSection(3, 1);
    }
    pumpEvents(15);
    cfg->setSortingModeEnabled(dispTable, true);
    cfg->sortTableByColumn(dispTable, 0, Qt::DescendingOrder);
    cfg->setRowOrderMode(reacTable, ui::TableRowOrderMode::PrependTop);
    cfg->setColumnColored(dispTable, 0, true);
    cfg->setColumnColored(dispTable, 1, true);
    cfg->setCategoryColor(dispTable, QStringLiteral("node"), QColor(123, 45, 210));
    cfg->setExportOnlyActiveColumns(dispTable, true);
    cfg->setExportOnlySelectedCells(dispTable, true);

    // Personalizar qualidade gráfica (seção graphics em user_preferences.json) via menu Configurações
    QAction* actHighPreset = nullptr;
    QAction* actVsync = nullptr;
    for (QAction* act : w.findChildren<QAction*>()) {
        if (act->text() == QStringLiteral("Alta (SSAA 150% + MSAA 8×)")) {
            actHighPreset = act;
        } else if (act->text() == QStringLiteral("Sincronização Vertical (VSync)")) {
            actVsync = act;
        }
    }
    TEST_ASSERT(actHighPreset != nullptr && actVsync != nullptr,
                "Ações de qualidade gráfica e VSync devem existir no menu Configurações");
    actHighPreset->trigger();
    if (actVsync->isChecked()) {
        actVsync->trigger();
    }
    pumpEvents(20);

    // Personalizar Viewport3D, Barras de Ferramentas, Entrada Rápida e Pós-Processamento 3D
    const QList<QToolBar*> toolbars = w.findChildren<QToolBar*>();
    TEST_ASSERT(toolbars.size() >= 2, "MainWindow deve possuir as 2 toolbars principais");
    auto* planeCombo = toolbars[0]->findChild<QComboBox*>();
    const auto tb1Spins = toolbars[0]->findChildren<QDoubleSpinBox*>();
    auto* diagramCombo = toolbars[1]->findChild<QComboBox*>();
    auto* scaleSlider = toolbars[1]->findChild<QSlider*>();
    TEST_ASSERT(planeCombo && tb1Spins.size() >= 2 && diagramCombo && scaleSlider,
                "Controles das toolbars devem existir para personalização");

    const int xzIdx = planeCombo->findData(static_cast<int>(WorkPlane::XZ_Frontal));
    TEST_ASSERT(xzIdx >= 0, "WorkPlane::XZ_Frontal deve existir no combo de plano");
    planeCombo->setCurrentIndex(xzIdx);
    tb1Spins[0]->setValue(4.25);
    tb1Spins[1]->setValue(1.25);
    for (QAction* act : toolbars[0]->actions()) {
        if (act->text().contains(QStringLiteral("Snap Grid"), Qt::CaseInsensitive)) {
            act->setChecked(false);
        } else if (act->text().contains(QStringLiteral("Eixos Locais"), Qt::CaseInsensitive)) {
            act->setChecked(true);
        }
    }
    const int mzIdx = diagramCombo->findData(static_cast<int>(DiagramType::Moment_Mz));
    TEST_ASSERT(mzIdx >= 0, "DiagramType::Moment_Mz deve existir no combo de diagramas");
    diagramCombo->setCurrentIndex(mzIdx);
    scaleSlider->setValue(65);
    for (QAction* act : toolbars[1]->actions()) {
        if (act->text().contains(QStringLiteral("Reações"), Qt::CaseInsensitive)) {
            act->setChecked(false);
        }
    }

    QGroupBox* entryGroup = nullptr;
    for (QGroupBox* gb : w.findChildren<QGroupBox*>()) {
        if (gb->title().contains(QStringLiteral("Adicionar Nó"), Qt::CaseInsensitive)) {
            entryGroup = gb;
            break;
        }
    }
    TEST_ASSERT(entryGroup != nullptr, "Grupo Adicionar Nó por Coordenadas deve existir");
    const auto quickSpins = entryGroup->findChildren<QDoubleSpinBox*>();
    TEST_ASSERT(quickSpins.size() >= 3, "Spinboxes X, Y, Z de entrada rápida devem existir");
    quickSpins[0]->setValue(1.5);
    quickSpins[1]->setValue(-2.5);
    quickSpins[2]->setValue(6.0);

    auto* vp = w.findChild<Viewport3D*>();
    TEST_ASSERT(vp != nullptr, "Viewport3D deve existir em MainWindow");
    vp->setPostContourMode(Viewport3D::PostContourMode::VonMisesStress);
    vp->setShowSurfaceMesh(false);
    vp->setClipPlane(Viewport3D::ClipAxis::Y, 2.5, true);
    vp->setAnimationSpeed(2.0);
    vp->setAnimationAmplitude(1.75);

    // Personalizar diálogo secundário (MeshGeneratorDialog) e verificar persistência de seus parâmetros
    TEST_ASSERT(QMetaObject::invokeMethod(&w, "onOpenMeshGenerator", Qt::DirectConnection),
                "Abrir MeshGeneratorDialog para registrar preferência do usuário");
    pumpEvents(30);
    auto* meshDlg = findVisibleTopLevel<MeshGeneratorDialog>();
    TEST_ASSERT(meshDlg != nullptr, "MeshGeneratorDialog deve abrir");
    QDoubleSpinBox* meshLxSpin = nullptr;
    for (auto* sp : meshDlg->findChildren<QDoubleSpinBox*>()) {
        if (std::abs(sp->value() - 6.0) < 1e-6) {
            meshLxSpin = sp;
            break;
        }
    }
    TEST_ASSERT(meshLxSpin != nullptr, "SpinBox Lx do MeshGeneratorDialog deve existir");
    meshLxSpin->setValue(14.5);
    pumpEvents(20);
    meshDlg->close();
    pumpEvents(30);

    TEST_ASSERT(QMetaObject::invokeMethod(&w, "onSaveAllSettingsNow", Qt::DirectConnection),
                "Salvar todas as preferências via ação do menu Configurações");
    pumpEvents(15);
    TEST_ASSERT(QFile::exists(userPrefsPath),
                "Arquivo único user_preferences.json deve existir em AppData/Local/OmniFrame");
    for (const QString& legacyPath : io::UserPreferencesStore::legacyPreferenceFilePaths()) {
        TEST_ASSERT(!QFile::exists(legacyPath),
                    "Arquivos legados (graphics/language/results_tables/theme.json) não devem ser gerados");
    }

    {
        QFile checkFile(userPrefsPath);
        TEST_ASSERT(checkFile.open(QIODevice::ReadOnly), "Abrir user_preferences.json para inspeção estrutural");
        const QJsonDocument doc = QJsonDocument::fromJson(checkFile.readAll());
        TEST_ASSERT(doc.isObject(), "user_preferences.json deve conter objeto JSON válido");
        const QJsonObject rootObj = doc.object();
        TEST_ASSERT(rootObj.value(QStringLiteral("version")).toInt() ==
                        io::UserPreferencesStore::kCurrentSchemaVersion,
                    "Esquema unificado deve estar na versão 2");
        TEST_ASSERT(rootObj.value(QStringLiteral("app")).isObject() &&
                        rootObj.value(QStringLiteral("graphics")).isObject() &&
                        rootObj.value(QStringLiteral("window")).isObject() &&
                        rootObj.value(QStringLiteral("workspace")).isObject() &&
                        rootObj.value(QStringLiteral("dialogs")).isObject() &&
                        rootObj.value(QStringLiteral("resultsPanel")).isObject(),
                    "user_preferences.json deve consolidar todas as seções (app, graphics, window, workspace, dialogs, resultsPanel)");
    }

    w.close();
    pumpEvents(40);

    {
        MainWindow wReloaded;
        wReloaded.show();
        pumpEvents(40);

        auto* reloadedDisp = wReloaded.findChild<QTableView*>(QStringLiteral("dispTable"));
        auto* reloadedReac = wReloaded.findChild<QTableView*>(QStringLiteral("reacTable"));
        auto* reloadedForces = wReloaded.findChild<QTableView*>(QStringLiteral("forcesTable"));
        auto* reloadedCont = wReloaded.findChild<QTableView*>(QStringLiteral("continuumTable"));
        ui::ResultTableConfigurator* reloadedCfg = wReloaded.resultTableConfigurator();
        auto* reloadedVp = wReloaded.findChild<Viewport3D*>();

        TEST_ASSERT(reloadedDisp && reloadedReac && reloadedForces && reloadedCont && reloadedCfg && reloadedVp,
                    "Todas as tabelas, viewport e configurador devem existir após reiniciar");
        TEST_ASSERT(reloadedVp->model() != nullptr && reloadedVp->model()->getNodes().empty() &&
                        reloadedDisp->model() != nullptr && reloadedReac->model() != nullptr &&
                        reloadedDisp->model()->rowCount() == 0 && reloadedReac->model()->rowCount() == 0,
                    "Reinicialização sem projeto aberto deve iniciar vazia antes de carregar modelo");

        // Verificar recarga universal de Viewport3D / Toolbars / Pós-Processamento 3D antes de carregar exemplo
        TEST_ASSERT(reloadedVp->workPlane() == WorkPlane::XZ_Frontal &&
                        std::abs(reloadedVp->planeOffset() - 4.25) < 1e-6 &&
                        !reloadedVp->snapToGrid() &&
                        std::abs(reloadedVp->gridStep() - 1.25) < 1e-6 &&
                        reloadedVp->showLocalAxes() &&
                        !reloadedVp->showReactions() &&
                        reloadedVp->activeDiagram() == DiagramType::Moment_Mz &&
                        std::abs(reloadedVp->diagramScale() - (65.0 / 30.0)) < 1e-5,
                    "Estado do Viewport3D e toolbars deve ser restaurado na reinicialização");
        TEST_ASSERT(reloadedVp->postContourMode() == Viewport3D::PostContourMode::VonMisesStress &&
                        !reloadedVp->showSurfaceMesh() &&
                        reloadedVp->clipAxis() == Viewport3D::ClipAxis::Y &&
                        std::abs(reloadedVp->clipOffset() - 2.5) < 1e-6 &&
                        reloadedVp->clipInverted() &&
                        std::abs(reloadedVp->animationSpeed() - 2.0) < 1e-6 &&
                        std::abs(reloadedVp->animationAmplitude() - 1.75) < 1e-6,
                    "Preferências de pós-processamento 3D devem ser restauradas na reinicialização");

        wReloaded.loadExampleBuilding();
        TEST_ASSERT(reloadedVp->clipAxis() == Viewport3D::ClipAxis::None,
                    "Carregar exemplo deve limpar plano de corte anterior para exibir a estrutura integralmente");
        TEST_ASSERT(waitUntil([&]() {
            return reloadedDisp->model()->rowCount() > 2 && reloadedReac->model()->rowCount() > 2;
        }, 15000), "Tabelas recarregadas devem ser populadas após resolução do exemplo carregado");
        pumpEvents(40);

        TEST_ASSERT(wReloaded.resultsLayoutCount() == 3,
                    "Quantidade de layouts (3) deve ser restaurada na reinicialização");
        TEST_ASSERT(wReloaded.isResultsLayoutTableVisible(0, ui::ResultTableKind::ContinuumStress),
                    "Atribuição personalizada de tabela no 1º layout deve ser restaurada");
        TEST_ASSERT(wReloaded.resultsLayoutTabWidget(0) != nullptr &&
                        wReloaded.resultsLayoutTabWidget(0)->currentIndex() == 1,
                    "Aba ativa selecionada pelo usuário deve ser restaurada");
        TEST_ASSERT(wReloaded.resultsSplitter() != nullptr &&
                        wReloaded.resultsSplitter()->count() == 3,
                    "Splitter deve conter os 3 layouts restaurados");
        TEST_ASSERT(reloadedDisp->isColumnHidden(2),
                    "Visibilidade oculta de coluna deve ser restaurada");
        TEST_ASSERT(reloadedDisp->columnWidth(1) == 148,
                    "Largura ajustada de coluna deve ser restaurada");
        TEST_ASSERT(reloadedDisp->horizontalHeader() != nullptr &&
                        reloadedDisp->horizontalHeader()->logicalIndex(1) == 3,
                    "Ordem visual reordenada das colunas deve ser restaurada");
        TEST_ASSERT(reloadedCfg->isSortingModeEnabled(reloadedDisp),
                    "Modo de ordenação habilitado deve ser restaurado");
        TEST_ASSERT(reloadedDisp->model()->rowCount() > 1 &&
                        reloadedDisp->model()->index(0, 0).data().toString().toInt() >=
                            reloadedDisp->model()->index(reloadedDisp->model()->rowCount() - 1, 0).data().toString().toInt(),
                    "Direção de ordenação decrescente na coluna 0 deve ser reaplicada");
        TEST_ASSERT(reloadedCfg->rowOrderMode(reloadedReac) == ui::TableRowOrderMode::PrependTop,
                    "Ordem das linhas (PrependTop) deve ser restaurada");
        TEST_ASSERT(reloadedCfg->isColumnColored(reloadedDisp, 0) &&
                        reloadedCfg->isColumnColored(reloadedDisp, 1),
                    "Colunas marcadas para coloração devem ser restauradas");
        TEST_ASSERT(reloadedCfg->categoryColor(reloadedDisp, QStringLiteral("node")) == QColor(123, 45, 210),
                    "Cor personalizada de categoria deve ser restaurada");
        TEST_ASSERT(reloadedCfg->exportOnlyActiveColumns(reloadedDisp) &&
                        reloadedCfg->exportOnlySelectedCells(reloadedDisp),
                    "Opções de exportação devem ser restauradas");

        // Verificar recarga das preferências gráficas consolidadas em user_preferences.json
        const RenderQualitySettings reloadedRender = reloadedVp->renderQualitySettings();
        TEST_ASSERT(reloadedRender.preset == QualityPreset::HighQuality &&
                        !reloadedRender.autoMaxResolution &&
                        std::abs(reloadedRender.resolutionScale - 1.5f) < 0.02f &&
                        !reloadedRender.autoMsaa &&
                        reloadedRender.requestedMsaa == 8 &&
                        !reloadedRender.vsyncEnabled,
                    "Configurações gráficas (preset HighQuality, SSAA 1.5x, MSAA 8x, VSync) devem ser restauradas via user_preferences.json");

        QGroupBox* reloadedEntryGroup = nullptr;
        for (QGroupBox* gb : wReloaded.findChildren<QGroupBox*>()) {
            if (gb->title().contains(QStringLiteral("Adicionar Nó"), Qt::CaseInsensitive)) {
                reloadedEntryGroup = gb;
                break;
            }
        }
        TEST_ASSERT(reloadedEntryGroup != nullptr, "Grupo Adicionar Nó deve existir após reiniciar");
        const auto reloadedQuickSpins = reloadedEntryGroup->findChildren<QDoubleSpinBox*>();
        TEST_ASSERT(reloadedQuickSpins.size() >= 3 &&
                        std::abs(reloadedQuickSpins[0]->value() - 1.5) < 1e-6 &&
                        std::abs(reloadedQuickSpins[1]->value() - (-2.5)) < 1e-6 &&
                        std::abs(reloadedQuickSpins[2]->value() - 6.0) < 1e-6,
                    "Coordenadas de entrada rápida (X/Y/Z) devem ser restauradas na reinicialização");

        // Verificar recarga de parâmetros persistidos no MeshGeneratorDialog
        TEST_ASSERT(QMetaObject::invokeMethod(&wReloaded, "onOpenMeshGenerator", Qt::DirectConnection),
                    "Reabrir MeshGeneratorDialog após reiniciar");
        pumpEvents(30);
        auto* reloadedMeshDlg = findVisibleTopLevel<MeshGeneratorDialog>();
        TEST_ASSERT(reloadedMeshDlg != nullptr, "MeshGeneratorDialog deve abrir após reiniciar");
        bool foundSavedLx = false;
        for (auto* sp : reloadedMeshDlg->findChildren<QDoubleSpinBox*>()) {
            if (std::abs(sp->value() - 14.5) < 1e-6) {
                foundSavedLx = true;
                break;
            }
        }
        TEST_ASSERT(foundSavedLx, "Valor Lx = 14.5 definido no MeshGeneratorDialog deve ser restaurado");
        reloadedMeshDlg->close();
        pumpEvents(25);

        // Verificar recarga ao vivo via onReloadSavedSettings após edição externa em user_preferences.json
        {
            bool readOk = false;
            QJsonObject diskRoot = io::UserPreferencesStore::readRootObject(&readOk);
            TEST_ASSERT(readOk, "Leitura para teste de recarga ao vivo");
            QJsonObject appObj = diskRoot.value(QStringLiteral("app")).toObject();
            appObj.insert(QStringLiteral("language"), QStringLiteral("en_US"));
            diskRoot.insert(QStringLiteral("app"), appObj);
            QJsonObject gfxObj = diskRoot.value(QStringLiteral("graphics")).toObject();
            gfxObj.insert(QStringLiteral("vsyncEnabled"), true);
            diskRoot.insert(QStringLiteral("graphics"), gfxObj);
            QJsonObject wsObj = diskRoot.value(QStringLiteral("workspace")).toObject();
            wsObj.insert(QStringLiteral("gridStep"), 2.25);
            diskRoot.insert(QStringLiteral("workspace"), wsObj);
            TEST_ASSERT(io::UserPreferencesStore::writeRootObject(diskRoot),
                        "Gravação de edição externa em user_preferences.json");
        }
        TEST_ASSERT(QMetaObject::invokeMethod(&wReloaded, "onReloadSavedSettings", Qt::DirectConnection),
                    "Recarregar todas as configurações via ação do menu");
        pumpEvents(25);
        QMenu* persistMenu = wReloaded.findChild<QMenu*>(QStringLiteral("preferencesPersistenceMenu"));
        TEST_ASSERT(persistMenu != nullptr &&
                        persistMenu->title() == QStringLiteral("Settings and Preferences Persistence") &&
                        !persistMenu->title().contains(QLatin1Char('_')) &&
                        !persistMenu->title().contains(QLatin1Char('&')) &&
                        traducao.codigoIdiomaAtual() == QStringLiteral("en_US") &&
                        reloadedVp->renderQualitySettings().vsyncEnabled &&
                        std::abs(reloadedVp->gridStep() - 2.25) < 1e-6,
                    "onReloadSavedSettings deve recarregar idioma, gráficos e workspace do user_preferences.json");

        // Verificar restauração global de padrões
        wReloaded.restoreAllDefaultSettings();
        pumpEvents(25);
        TEST_ASSERT(wReloaded.resultsLayoutCount() == 1 &&
                        reloadedVp->workPlane() == WorkPlane::XY_Top &&
                        reloadedVp->snapToGrid() &&
                        std::abs(reloadedVp->gridStep() - 0.5) < 1e-6 &&
                        !reloadedVp->showLocalAxes() &&
                        reloadedVp->showReactions() &&
                        reloadedVp->postContourMode() == Viewport3D::PostContourMode::None &&
                        reloadedVp->showSurfaceMesh() &&
                        reloadedVp->clipAxis() == Viewport3D::ClipAxis::None &&
                        reloadedVp->renderQualitySettings().preset == QualityPreset::AutoMax &&
                        reloadedVp->renderQualitySettings().autoMsaa,
                    "restoreAllDefaultSettings deve restaurar todas as preferências, gráficos e o viewport ao padrão");

        for (const QString& legacyPath : io::UserPreferencesStore::legacyPreferenceFilePaths()) {
            TEST_ASSERT(!QFile::exists(legacyPath),
                        "Nenhum arquivo legado deve existir após restauração de padrões");
        }

        wReloaded.close();
        pumpEvents(40);
    }

    ThemeManager::instance().setTheme(originalTheme);
    traducao.definir_idioma(originalLang);
}

void testDualUiModeNativeAndCustomSwitchingAndPersistence() {
    auto& traducao = omniframe::locale::GerenciadorTraducao::instance();
    const QString originalLang = traducao.codigoIdiomaAtual();
    const UiTheme originalTheme = ThemeManager::instance().currentTheme();
    const UiStyleMode originalMode = ThemeManager::instance().uiStyleMode();

    TEST_ASSERT(traducao.definir_idioma(QStringLiteral("pt_BR")),
                "Deve ativar pt_BR para teste de UI Nativo e Customizado");
    ThemeManager::instance().setUiStyleMode(UiStyleMode::Custom);
    ThemeManager::instance().setTheme(UiTheme::Dark);
    pumpEvents(30);

    auto findUiStyleControls = [](MainWindow& win,
                                  QMenu*& outUiMenu,
                                  QAction*& outNativeAct,
                                  QAction*& outCustomAct) {
        outUiMenu = win.findChild<QMenu*>(QStringLiteral("uiStyleMenu"));
        outNativeAct = win.findChild<QAction*>(QStringLiteral("actUiStyleNative"));
        outCustomAct = win.findChild<QAction*>(QStringLiteral("actUiStyleCustom"));
        bool foundInSettings = false;
        if (QMenuBar* mb = win.menuBar()) {
            for (QAction* topAct : mb->actions()) {
                QMenu* sub = topAct ? topAct->menu() : nullptr;
                if (!sub) continue;
                const QString clean = topAct->text().remove(QLatin1Char('&'));
                if (clean == QStringLiteral("Configurações") || clean == QStringLiteral("Settings")) {
                    for (QAction* itemAct : sub->actions()) {
                        if (itemAct && itemAct->menu() == outUiMenu) {
                            foundInSettings = true;
                            break;
                        }
                    }
                }
            }
        }
        return outUiMenu != nullptr && outNativeAct != nullptr && outCustomAct != nullptr && foundInSettings;
    };

    {
        MainWindow w;
        w.setAttribute(Qt::WA_DontShowOnScreen, true);
        w.resize(1360, 860);
        w.show();
        pumpEvents(50);

        QMenu* uiMenu = nullptr;
        QAction* actNative = nullptr;
        QAction* actCustom = nullptr;
        TEST_ASSERT(findUiStyleControls(w, uiMenu, actNative, actCustom),
                    "Menu Configurações > Interface do Usuário (Nativo / Customizado) deve existir");
        TEST_ASSERT(uiMenu->title() == QStringLiteral("Interface do Usuário") &&
                        actNative->text() == QStringLiteral("Nativo") &&
                        actCustom->text() == QStringLiteral("Customizado"),
                    "Rótulos em pt_BR de Interface do Usuário devem estar corretos");
        TEST_ASSERT(ThemeManager::instance().isCustomUi() &&
                        actCustom->isChecked() && !actNative->isChecked() &&
                        !qApp->styleSheet().isEmpty() &&
                        QApplication::style() != nullptr &&
                        QApplication::style()->objectName().compare(QStringLiteral("windows11"), Qt::CaseInsensitive) != 0 &&
                        QApplication::style()->objectName().compare(QStringLiteral("windowsvista"), Qt::CaseInsensitive) != 0,
                    "Estado inicial padrão deve ser Customizado com OmniFrameFusionStyle e stylesheet global");

        auto* inspBtn = w.findChild<QToolButton*>(QStringLiteral("inspectorCollapseButton"));
        auto* resBtn = w.findChild<QToolButton*>(QStringLiteral("resultsCollapseButton"));
        auto* dispTable = w.findChild<QTableView*>(QStringLiteral("dispTable"));
        auto* reacTable = w.findChild<QTableView*>(QStringLiteral("reacTable"));
        TEST_ASSERT(inspBtn && resBtn && dispTable && reacTable,
                    "Botões dos docks e tabelas de resultados devem existir");
        TEST_ASSERT(!inspBtn->autoRaise() && inspBtn->size() == QSize(24, 20) &&
                        !dispTable->styleSheet().isEmpty(),
                    "Modo Customizado deve aplicar estilo customizado nos botões de dock e tabelas");

        auto assertChildHierarchyTracksTheme = [&](bool expectedLight, const char* phaseTag) {
            const int winLightness = QApplication::palette().color(QPalette::Window).lightness();
            TEST_ASSERT(expectedLight ? (winLightness > 150) : (winLightness < 80),
                        std::string("Paleta da aplicacao deve acompanhar tema em ") + phaseTag);
            QGroupBox* entryGrp = w.findChild<QGroupBox*>();
            QDoubleSpinBox* anySpin = w.findChild<QDoubleSpinBox*>();
            QMenu* persistMenu = w.findChild<QMenu*>(QStringLiteral("preferencesPersistenceMenu"));
            TEST_ASSERT(entryGrp != nullptr && anySpin != nullptr && persistMenu != nullptr,
                        std::string("Controles internos devem existir em ") + phaseTag);
            const int grpL = entryGrp->palette().color(QPalette::Window).lightness();
            const int spinL = anySpin->palette().color(QPalette::Base).lightness();
            const int menuL = persistMenu->palette().color(QPalette::Window).lightness();
            const bool ok = expectedLight ? (grpL > 150 && spinL > 150 && menuL > 150)
                                          : (grpL < 85 && spinL < 85 && menuL < 85);
            if (!ok) {
                std::cerr << "[DIAG " << phaseTag << "] winL=" << winLightness
                          << " wPalL=" << w.palette().color(QPalette::Window).lightness()
                          << " grpL=" << grpL << " (WA=" << entryGrp->testAttribute(Qt::WA_SetPalette) << ", vis=" << entryGrp->isVisible() << ")"
                          << " spinL=" << spinL << " (WA=" << anySpin->testAttribute(Qt::WA_SetPalette) << ")"
                          << " menuL=" << menuL << " (WA=" << persistMenu->testAttribute(Qt::WA_SetPalette) << ")"
                          << "\n  grp ancestors:";
                for (QWidget* anc = entryGrp; anc != nullptr; anc = anc->parentWidget()) {
                    std::cerr << " -> " << anc->metaObject()->className()
                              << "(" << anc->objectName().toStdString()
                              << ",L=" << anc->palette().color(QPalette::Window).lightness()
                              << ",WA=" << anc->testAttribute(Qt::WA_SetPalette)
                              << ",qss=" << !anc->styleSheet().isEmpty() << ")";
                }
                std::cerr << std::endl;
            }
            TEST_ASSERT(ok,
                        std::string("Widgets filhos em QScrollArea, SpinBox e Menus devem adaptar paleta ao vivo em ") + phaseTag);
        };

        auto assertNativeFramedButtonsUniformHeight = [&](const char* phaseTag) {
            QList<QPushButton*> pushButtons = w.findChildren<QPushButton*>();
            TEST_ASSERT(!pushButtons.isEmpty(), std::string("MainWindow deve conter QPushButtons em ") + phaseTag);
            for (QPushButton* pb : pushButtons) {
                if (!pb || !pb->isVisible()) continue;
                TEST_ASSERT(pb->height() == 28 && pb->sizeHint().height() == 28,
                            std::string("QPushButton deve ter altura vertical padronizada de 28px no modo Nativo em ") + phaseTag);
                const QImage btnImg = pb->grab().toImage();
                TEST_ASSERT(!btnImg.isNull() && btnImg.height() == 28,
                            std::string("QPushButton deve renderizar em 28px no modo Nativo em ") + phaseTag);
                const QColor topEdge = btnImg.pixelColor(btnImg.width() / 2, 0);
                const QColor centerPx = btnImg.pixelColor(btnImg.width() / 2, btnImg.height() / 2);
                const QColor parentBg = pb->parentWidget()
                    ? pb->parentWidget()->palette().color(pb->parentWidget()->backgroundRole())
                    : pb->palette().color(QPalette::Window);
                const bool edgeDiffers = (topEdge != parentBg) || (topEdge != centerPx);
                TEST_ASSERT(edgeDiffers,
                            std::string("QPushButton deve exibir borda/moldura visivel no modo Nativo em ") + phaseTag);
            }
            for (QToolBar* tb : w.findChildren<QToolBar*>()) {
                if (!tb || !tb->isVisible()) continue;
                for (QToolButton* tbtn : tb->findChildren<QToolButton*>()) {
                    if (!tbtn || !tbtn->isVisible() || tbtn->objectName() == QLatin1String("qt_toolbar_ext_button")) {
                        continue;
                    }
                    TEST_ASSERT(tbtn->height() == 28 && tbtn->sizeHint().height() == 28,
                                std::string("QToolButton de toolbar deve ter altura padronizada de 28px no modo Nativo em ") + phaseTag);
                    const QImage tbImg = tbtn->grab().toImage();
                    TEST_ASSERT(!tbImg.isNull() && tbImg.height() == 28,
                                std::string("QToolButton de toolbar deve renderizar em 28px no modo Nativo em ") + phaseTag);
                }
            }
        };

        assertChildHierarchyTracksTheme(false, "Custom Dark inicial");

        // Alterna para Custom Light antes de ir para Nativo para garantir transição limpa entre temas/modos
        ThemeManager::instance().setTheme(UiTheme::Light);
        pumpEvents(40);
        assertChildHierarchyTracksTheme(true, "Custom Light");

        ThemeManager::instance().setTheme(UiTheme::Dark);
        pumpEvents(40);
        assertChildHierarchyTracksTheme(false, "Custom Dark");

        // Alterna para o modo Nativo via menu "Configurações > Interface do Usuário > Nativo"
        actNative->trigger();
        pumpEvents(50);

        TEST_ASSERT(ThemeManager::instance().isNativeUi() &&
                        actNative->isChecked() && !actCustom->isChecked(),
                    "Ação Nativo deve ativar modo Nativo e atualizar exclusividade do menu");
        TEST_ASSERT(qApp->styleSheet().isEmpty() &&
                        QApplication::style() != nullptr &&
                        QApplication::style()->objectName().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0,
                    "Modo Nativo deve remover stylesheet global customizado e restaurar QStyle nativo do Qt6");
        TEST_ASSERT(inspBtn->autoRaise() && inspBtn->styleSheet().isEmpty() &&
                        inspBtn->size() == QSize(22, 22) && !inspBtn->grab().toImage().isNull() &&
                        resBtn->autoRaise() && resBtn->styleSheet().isEmpty() &&
                        resBtn->size() == QSize(22, 22) && !resBtn->grab().toImage().isNull(),
                    "Modo Nativo deve alternar botões de dock para QToolButton nativo com autoRaise");
        TEST_ASSERT(dispTable->styleSheet().isEmpty() && reacTable->styleSheet().isEmpty(),
                    "Modo Nativo deve limpar QSS customizado das tabelas de resultados");
        assertChildHierarchyTracksTheme(false, "Native Dark");
        assertNativeFramedButtonsUniformHeight("Native Dark");

        if (const QStyleHints* hints = QGuiApplication::styleHints()) {
            TEST_ASSERT(hints->colorScheme() == Qt::ColorScheme::Dark,
                        "QStyleHints::colorScheme deve refletir Dark no modo Nativo");
        }

        // Mantem dialogos abertos e verifica adaptacao ao vivo entre Native Dark <-> Native Light <-> Custom
        {
            AboutDialog about;
            about.setAttribute(Qt::WA_DontShowOnScreen, true);
            about.show();

            ui::ManualDialog manual;
            manual.setAttribute(Qt::WA_DontShowOnScreen, true);
            manual.show();

            GuidedSessionDialog guided(&w);
            guided.setAttribute(Qt::WA_DontShowOnScreen, true);
            guided.show();
            pumpEvents(35);

            TEST_ASSERT(about.styleSheet().isEmpty(),
                        "AboutDialog deve operar sem stylesheet customizado no modo Nativo");
            TEST_ASSERT(manual.styleSheet().isEmpty(),
                        "ManualDialog deve operar sem stylesheet customizado no modo Nativo");

            auto* manualViewer = manual.findChild<QTextBrowser*>();
            TEST_ASSERT(manualViewer != nullptr && manualViewer->document() != nullptr,
                        "ManualDialog deve conter QTextBrowser inicializado");
            {
                const QString manualHtml = manualViewer->toHtml();
                TEST_ASSERT(!manualHtml.contains(QStringLiteral("#f5f5f5"), Qt::CaseInsensitive),
                            "HTML do ManualDialog no modo Nativo Escuro nao deve conter fundo claro #f5f5f5 (AlternateBase)");

                auto checkFrameNoLightBg = [&](auto& self, QTextFrame* frame) -> void {
                    if (!frame) return;
                    const QTextFrameFormat ff = frame->frameFormat();
                    if (ff.background().style() != Qt::NoBrush && ff.background().color().alpha() > 0) {
                        TEST_ASSERT(ff.background().color().lightness() < 110,
                                    "Nenhum bloco/quadro do ManualDialog deve possuir fundo claro no modo Nativo Escuro");
                    }
                    for (auto it = frame->begin(); !it.atEnd(); ++it) {
                        if (QTextFrame* childFrame = it.currentFrame()) {
                            self(self, childFrame);
                        } else {
                            const QTextBlock blk = it.currentBlock();
                            if (blk.isValid()) {
                                const QBrush bb = blk.blockFormat().background();
                                if (bb.style() != Qt::NoBrush && bb.color().alpha() > 0) {
                                    TEST_ASSERT(bb.color().lightness() < 110,
                                                "Nenhuma linha/paragrafo do ManualDialog deve possuir fundo claro no modo Nativo Escuro");
                                }
                            }
                        }
                    }
                };
                checkFrameNoLightBg(checkFrameNoLightBg, manualViewer->document()->rootFrame());

                const QImage manualImg = manualViewer->viewport()->grab().toImage();
                TEST_ASSERT(!manualImg.isNull() && manualImg.width() > 40 && manualImg.height() > 60,
                            "Viewport do ManualDialog deve renderizar imagem valida");
                // Verifica a margem interna esquerda do banner superior (x=18, y=16..42):
                // nenhuma faixa com fundo claro deve aparecer atras das strings do cabeçalho
                for (int y = 16; y <= std::min(42, manualImg.height() - 4); y += 6) {
                    const QColor px = manualImg.pixelColor(18, y);
                    TEST_ASSERT(px.lightness() < 90,
                                "Pixels de fundo dos blocos do ManualDialog no modo Nativo Escuro devem permanecer escuros");
                }
            }

            guided.setPhaseHighlightColor(1, QColor(QStringLiteral("#3B82F6")));
            guided.setPhaseHighlightColor(2, QColor(QStringLiteral("#EC4899")));
            guided.selectHighlightPhase(1);
            pumpEvents(25);
            TEST_ASSERT(guided.phaseHighlightColor(2) == QColor(QStringLiteral("#EC4899")),
                        "Fase 2 deve manter cor independente (#EC4899) ao retornar foco para Fase 1 (#3B82F6)");
            auto* banner = guided.findChild<QLabel*>(QStringLiteral("guided_target_action_banner"));
            auto* badge = guided.findChild<QLabel*>(QStringLiteral("guided_phase_badge"));
            auto* swatch = guided.findChild<QLabel*>(QStringLiteral("guided_highlight_color_swatch"));
            auto extractDualBg = [](const QString& qss) -> QColor {
                const int idx = qss.indexOf(QLatin1String("background-color:"));
                if (idx < 0) return {};
                const int start = idx + 17;
                int end = qss.indexOf(QLatin1Char(';'), start);
                if (end < 0) end = qss.indexOf(QLatin1Char('}'), start);
                if (end < 0) end = qss.size();
                const QString val = qss.mid(start, end - start).trimmed();
                if (val.startsWith(QLatin1Char('#'))) {
                    return QColor(val);
                }
                if (val.startsWith(QLatin1String("rgb"))) {
                    const int openP = val.indexOf(QLatin1Char('('));
                    const int closeP = val.indexOf(QLatin1Char(')'));
                    if (openP >= 0 && closeP > openP) {
                        const QStringList parts = val.mid(openP + 1, closeP - openP - 1).split(QLatin1Char(','));
                        if (parts.size() >= 3) {
                            return QColor(parts[0].trimmed().toInt(),
                                          parts[1].trimmed().toInt(),
                                          parts[2].trimmed().toInt());
                        }
                    }
                }
                return {};
            };
            TEST_ASSERT(banner != nullptr && badge != nullptr && swatch != nullptr &&
                            guided.styleSheet().isEmpty() &&
                            banner->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            badge->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            swatch->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            extractDualBg(banner->styleSheet()).lightness() < 70,
                        "GuidedSessionDialog deve usar janela nativa mantendo cor definida por fase e fundo escuro em Native Dark");

            for (QPushButton* dlgBtn : about.findChildren<QPushButton*>()) {
                if (dlgBtn && dlgBtn->isVisible()) {
                    TEST_ASSERT(dlgBtn->height() == 28,
                                "Botões do AboutDialog no modo Nativo devem ter altura padronizada de 28px");
                }
            }
            for (QPushButton* dlgBtn : manual.findChildren<QPushButton*>()) {
                if (dlgBtn && dlgBtn->isVisible()) {
                    TEST_ASSERT(dlgBtn->height() == 28,
                                "Botões do ManualDialog no modo Nativo devem ter altura padronizada de 28px");
                }
            }

            // Troca ao vivo Native Dark -> Native Light com dialogos abertos
            ThemeManager::instance().setTheme(UiTheme::Light);
            pumpEvents(45);
            if (const QStyleHints* hints = QGuiApplication::styleHints()) {
                TEST_ASSERT(hints->colorScheme() == Qt::ColorScheme::Light,
                            "QStyleHints::colorScheme deve acompanhar alteração ao vivo para Light no modo Nativo");
            }
            assertChildHierarchyTracksTheme(true, "Native Light");
            assertNativeFramedButtonsUniformHeight("Native Light");
            TEST_ASSERT(about.palette().color(QPalette::Window).lightness() > 150 &&
                            manual.palette().color(QPalette::Window).lightness() > 150 &&
                            guided.palette().color(QPalette::Window).lightness() > 150,
                        "Diálogos abertos devem adaptar paleta ao vivo para Light no modo Nativo");
            TEST_ASSERT(banner->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            badge->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            extractDualBg(banner->styleSheet()).lightness() > 200,
                        "Cor do realce em GuidedSessionDialog deve permanecer inalterada e fundo deve ficar claro em Native Light");

            // Troca ao vivo Native Light -> Custom Light -> Custom Dark com dialogos abertos
            actCustom->trigger();
            pumpEvents(45);
            TEST_ASSERT(!about.styleSheet().isEmpty() && !manual.styleSheet().isEmpty() &&
                            banner->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            badge->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            extractDualBg(banner->styleSheet()).lightness() > 200,
                        "Diálogos abertos devem assumir QSS Custom Light e GuidedSessionDialog deve manter cor da fase e fundo claro");
            assertChildHierarchyTracksTheme(true, "Custom Light apos Nativo");

            ThemeManager::instance().setTheme(UiTheme::Dark);
            pumpEvents(45);
            assertChildHierarchyTracksTheme(false, "Custom Dark apos Custom Light");
            TEST_ASSERT(banner->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            badge->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            extractDualBg(banner->styleSheet()).lightness() < 70,
                        "GuidedSessionDialog aberto deve preservar cor definida por fase e voltar para fundo escuro em Custom Dark");

            // Volta para Nativo Dark e confirma sincronia
            actNative->trigger();
            pumpEvents(45);
            assertChildHierarchyTracksTheme(false, "Native Dark final com dialogos abertos");
            TEST_ASSERT(about.styleSheet().isEmpty() && manual.styleSheet().isEmpty() &&
                            guided.styleSheet().isEmpty() &&
                            banner->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            badge->styleSheet().contains(QStringLiteral("#3B82F6"), Qt::CaseInsensitive) &&
                            extractDualBg(banner->styleSheet()).lightness() < 70,
                        "Diálogos abertos devem retornar para Native Dark mantendo cor por fase e fundo escuro");

            // Verifica restauracao e persistencia das cores por fase no MainWindow / GuidedSessionDialog
            guided.resetAllPhaseHighlightColors();
            pumpEvents(20);
            TEST_ASSERT(guided.stepHighlightColor() == GuidedSessionDialog::defaultStepHighlightColor() &&
                            guided.phaseHighlightColor(1) == GuidedSessionDialog::defaultPhaseHighlightColor(1) &&
                            guided.phaseHighlightColor(2) == GuidedSessionDialog::defaultPhaseHighlightColor(2),
                        "resetAllPhaseHighlightColors deve restaurar as cores padrão das fases (#F59E0B / #10B981)");

            ThemeManager::instance().setTheme(UiTheme::Light);
            pumpEvents(35);

            guided.close();
            manual.close();
            about.close();
        }

        // Regressao de alternancias consecutivas apos resolver modelo e reconstruir inspetor (updateInspector)
        {
            QPushButton* solveBtn = nullptr;
            for (QPushButton* pb : w.findChildren<QPushButton*>()) {
                if (pb && pb->text().contains(QStringLiteral("CALCULAR"), Qt::CaseInsensitive)) {
                    solveBtn = pb;
                    break;
                }
            }
            if (solveBtn) {
                solveBtn->click();
                pumpEvents(120);
            }
            QAction* actThemeDark = nullptr;
            QAction* actThemeLight = nullptr;
            for (QAction* act : w.findChildren<QAction*>()) {
                if (!act) continue;
                if (act->text() == QStringLiteral("Escuro") || act->text() == QStringLiteral("Dark")) {
                    actThemeDark = act;
                } else if (act->text() == QStringLiteral("Claro") || act->text() == QStringLiteral("Light")) {
                    actThemeLight = act;
                }
            }
            TEST_ASSERT(actThemeDark != nullptr && actThemeLight != nullptr,
                        "Acoes Escuro/Claro devem existir para teste consecutivo");
            for (int cycle = 0; cycle < 12; ++cycle) {
                (cycle % 2 == 0 ? actCustom : actNative)->trigger();
                pumpEvents(15);
                (cycle % 2 == 0 ? actThemeDark : actThemeLight)->trigger();
                pumpEvents(15);
            }
            // Garante retorno final em Nativo + Light antes das verificacoes seguintes
            actNative->trigger();
            actThemeLight->trigger();
            pumpEvents(35);
            assertChildHierarchyTracksTheme(true, "Native Light apos 12 alternancias consecutivas");
            assertNativeFramedButtonsUniformHeight("Native Light apos 12 alternancias consecutivas");
        }

        // Verifica tradução dinâmica en_US <-> pt_BR para todas as novas strings e Persistência
        TEST_ASSERT(traducao.definir_idioma(QStringLiteral("en_US")),
                    "Deve ativar en_US para validar tradução dinâmica do menu Interface do Usuário");
        pumpEvents(30);
        QMenu* persistMenuEn = w.findChild<QMenu*>(QStringLiteral("preferencesPersistenceMenu"));
        TEST_ASSERT(persistMenuEn != nullptr &&
                        persistMenuEn->title() == QStringLiteral("Settings and Preferences Persistence") &&
                        !persistMenuEn->title().contains(QLatin1Char('_')) &&
                        !persistMenuEn->title().contains(QLatin1Char('&')),
                    "Menu Persistência em inglês deve traduzir como 'Settings and Preferences Persistence' sem mnemônico '_'");
        TEST_ASSERT(uiMenu->title() == QStringLiteral("User Interface") &&
                        actNative->text() == QStringLiteral("Native") &&
                        actCustom->text() == QStringLiteral("Custom") &&
                        traducao.trText("Modo de interface nativo (Qt6 padrão) aplicado.") ==
                            QStringLiteral("Native (standard Qt6) user interface mode applied.") &&
                        traducao.trText("Modo de interface customizado aplicado.") ==
                            QStringLiteral("Custom user interface mode applied."),
                    "Todas as novas strings de Interface do Usuário devem traduzir dinamicamente em en_US");

        TEST_ASSERT(traducao.definir_idioma(QStringLiteral("pt_BR")),
                    "Deve retornar para pt_BR dinamicamente");
        pumpEvents(30);
        TEST_ASSERT(uiMenu->title() == QStringLiteral("Interface do Usuário") &&
                        actNative->text() == QStringLiteral("Nativo") &&
                        actCustom->text() == QStringLiteral("Customizado"),
                    "Tradução dinâmica para pt_BR deve restaurar rótulos em português");

        w.close();
        pumpEvents(40);
    }

    // Verifica persistência de uiStyleMode = "native" em user_preferences.json entre reinicializações
    {
        bool readOk = false;
        const QJsonObject root = io::UserPreferencesStore::readRootObject(&readOk);
        TEST_ASSERT(readOk &&
                        root.value(QStringLiteral("app")).toObject().value(QStringLiteral("uiStyleMode")).toString() ==
                            QStringLiteral("native"),
                    "Modo de UI Nativo deve ser persistido em user_preferences.json");
    }

    // Simula nova instância após reinicialização em modo Nativo e retorno ao vivo para Customizado
    {
        TEST_ASSERT(ThemeManager::instance().reloadSavedTheme(),
                    "ThemeManager::reloadSavedTheme deve carregar uiStyleMode salvo");
        MainWindow wReloaded;
        wReloaded.setAttribute(Qt::WA_DontShowOnScreen, true);
        wReloaded.resize(1360, 860);
        wReloaded.show();
        pumpEvents(50);

        QMenu* uiMenu = nullptr;
        QAction* actNative = nullptr;
        QAction* actCustom = nullptr;
        TEST_ASSERT(findUiStyleControls(wReloaded, uiMenu, actNative, actCustom) &&
                        ThemeManager::instance().isNativeUi() &&
                        actNative->isChecked() && !actCustom->isChecked() &&
                        qApp->styleSheet().isEmpty() &&
                        QApplication::style()->objectName().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0,
                    "Nova instância do MainWindow deve restaurar modo Nativo automaticamente");

        // Alterna de volta para Customizado pelo menu em tempo de execução
        actCustom->trigger();
        pumpEvents(50);

        auto* inspBtn = wReloaded.findChild<QToolButton*>(QStringLiteral("inspectorCollapseButton"));
        auto* dispTable = wReloaded.findChild<QTableView*>(QStringLiteral("dispTable"));
        TEST_ASSERT(ThemeManager::instance().isCustomUi() &&
                        actCustom->isChecked() && !actNative->isChecked() &&
                        !qApp->styleSheet().isEmpty() &&
                        QApplication::style() != nullptr &&
                        QApplication::style()->objectName().compare(QStringLiteral("windows11"), Qt::CaseInsensitive) != 0 &&
                        QApplication::style()->objectName().compare(QStringLiteral("windowsvista"), Qt::CaseInsensitive) != 0 &&
                        inspBtn != nullptr && !inspBtn->autoRaise() && inspBtn->size() == QSize(24, 20) &&
                        dispTable != nullptr && !dispTable->styleSheet().isEmpty(),
                    "Ação Customizado deve restaurar integralmente a UI customizada e tabelas Linceu ao vivo");
        {
            AboutDialog aboutCustom;
            aboutCustom.setAttribute(Qt::WA_DontShowOnScreen, true);
            aboutCustom.show();
            pumpEvents(20);
            TEST_ASSERT(!aboutCustom.styleSheet().isEmpty(),
                        "AboutDialog deve restaurar QSS customizado após alternar de volta para Customizado");
            aboutCustom.close();
        }

        // Verifica restoreAllDefaultSettings restaurando Customizado quando estiver em Nativo
        actNative->trigger();
        pumpEvents(30);
        TEST_ASSERT(ThemeManager::instance().isNativeUi(), "Pre-condicao Nativo antes de restaurar padroes");
        wReloaded.restoreAllDefaultSettings();
        pumpEvents(40);
        TEST_ASSERT(ThemeManager::instance().isCustomUi() && actCustom->isChecked() &&
                        !qApp->styleSheet().isEmpty(),
                    "restoreAllDefaultSettings deve restaurar o modo de interface padrão Customizado");

        wReloaded.close();
        pumpEvents(40);
    }

    ThemeManager::instance().setUiStyleMode(originalMode);
    ThemeManager::instance().setTheme(originalTheme);
    traducao.definir_idioma(originalLang);
}

void testEmptyStartupAndInadvertentCloseProjectPreservation() {
    auto& traducao = locale::GerenciadorTraducao::instance();
    const QString originalLang = traducao.obter_idioma_atual();
    traducao.definir_idioma(QStringLiteral("pt_BR"));

    // 1) Startup inicial completamente vazio (sem projeto estrutural, sem diagramas, sem tabelas preenchidas)
    {
        MainWindow wEmpty;
        wEmpty.setAttribute(Qt::WA_DontShowOnScreen, true);
        wEmpty.resize(1280, 820);
        wEmpty.show();
        pumpEvents(60);

        auto* vp = wEmpty.findChild<Viewport3D*>();
        TEST_ASSERT(vp != nullptr && vp->model() != nullptr, "Viewport3D deve existir no startup");
        TEST_ASSERT(vp->model()->getNodes().empty() &&
                        vp->model()->getElements().empty() &&
                        vp->model()->surfaceMesh().elements.empty(),
                    "Startup deve iniciar com modelo completamente vazio (0 nos, 0 barras, 0 malha 2D/3D)");
        TEST_ASSERT(!vp->hasPostAnimation() && vp->postContourMode() == Viewport3D::PostContourMode::None,
                    "Startup vazio nao deve exibir pos-processamento/contornos");
        TEST_ASSERT(!wEmpty.hasUnsavedEdits(), "Startup vazio nao deve iniciar com alterações não salvas");
        for (const char* tblName : {"dispTable", "reacTable", "forcesTable", "continuumTable"}) {
            auto* tbl = wEmpty.findChild<QTableView*>(QString::fromLatin1(tblName));
            TEST_ASSERT(tbl != nullptr && tbl->model() != nullptr && tbl->model()->rowCount() == 0,
                        std::string("Tabela ") + tblName + " deve iniciar vazia (0 linhas)");
        }

        // Verifica traducao dinamica dos novos itens do menu Arquivo e barra de status
        QAction* actSave = nullptr;
        QAction* actSaveAs = nullptr;
        QAction* actRecover = nullptr;
        if (QMenuBar* mb = wEmpty.menuBar()) {
            if (!mb->actions().isEmpty() && mb->actions().first()->menu()) {
                for (QAction* act : mb->actions().first()->menu()->actions()) {
                    if (act->text().contains(QStringLiteral("Salvar (.omniframe)"))) actSave = act;
                    if (act->text().contains(QStringLiteral("Salvar Como"))) actSaveAs = act;
                    if (act->text().contains(QStringLiteral("Recuperar"))) actRecover = act;
                }
            }
        }
        TEST_ASSERT(actSave != nullptr && actSaveAs != nullptr && actRecover != nullptr,
                    "Menu Arquivo deve expor Salvar, Salvar Como e Recuperar Sessão Anterior");
        TEST_ASSERT(!actRecover->isEnabled(),
                    "Recuperar Sessão Anterior deve ficar desabilitado quando não há rascunho anterior");

        TEST_ASSERT(traducao.definir_idioma(QStringLiteral("en_US")), "Alternar para en_US no startup vazio");
        pumpEvents(40);
        TEST_ASSERT(actSave->text() == QStringLiteral("Save (.omniframe)") &&
                        actSaveAs->text() == QStringLiteral("Save As (.omniframe)...") &&
                        actRecover->text() == QStringLiteral("Recover Previous Session..."),
                    "Novas ações do menu Arquivo devem traduzir dinamicamente para inglês");

        bool foundReadyToModel = false;
        for (auto* lbl : wEmpty.findChildren<QLabel*>()) {
            if (lbl->text() == QStringLiteral("Ready to model.")) {
                foundReadyToModel = true;
                break;
            }
        }
        TEST_ASSERT(foundReadyToModel, "Status bar inicial vazio deve traduzir dinamicamente para 'Ready to model.'");
        TEST_ASSERT(traducao.definir_idioma(QStringLiteral("pt_BR")), "Restaurar pt_BR");
        pumpEvents(30);

        wEmpty.close();
        pumpEvents(40);
    }

    // 2) Edições estruturais completas + proteção contra fechamento inadvertido + recuperação automática
    {
        MainWindow wWork;
        wWork.setAttribute(Qt::WA_DontShowOnScreen, true);
        wWork.resize(1280, 820);
        wWork.show();
        pumpEvents(60);

        auto* vp = wWork.findChild<Viewport3D*>();
        TEST_ASSERT(vp != nullptr && vp->model() != nullptr, "Viewport ativo");
        StructuralModel* m = vp->model();

        const NodeId n1 = m->addNode(0.0, 0.0, 0.0);
        const NodeId n2 = m->addNode(4.5, 0.0, 3.0);
        if (Node* np1 = m->findNode(n1)) {
            np1->support = SupportRestraint::makeFixed();
            np1->support.springStiffness[2] = 4200.0;
        }
        if (Node* np2 = m->findNode(n2)) {
            np2->load.force = {15.0, 0.0, -35.0};
        }
        const ElementId e1 = m->addElement(n1, n2, 1, 2, 0.15);
        if (Element* el = m->findElement(e1)) {
            el->releases.end[4] = true;
            el->distLoad.active = true;
            el->distLoad.w = {0.0, 0.0, -12.0};
            el->thermalLoad.active = true;
            el->thermalLoad.deltaT = 18.0;
        }
        m->metadata().projectName = "Galpão Industrial Protegido";
        m->metadata().windV0_ms = 44.0;
        m->solverOptions().type = kernel::LinearSolverType::SparseLU;
        m->pushUndo();
        TEST_ASSERT(QMetaObject::invokeMethod(&wWork, "onModelModified", Qt::DirectConnection),
                    "Notificar modificação estrutural");
        pumpEvents(50);

        TEST_ASSERT(wWork.hasUnsavedEdits() && wWork.windowTitle().contains(QStringLiteral("[*]")),
                    "Edição estrutural deve marcar projeto com alterações não salvas");

        // Simula tentativa de saída pelo menu Arquivo > Sair: deve pausar e perguntar antes de fechar
        QAction* actQuit = nullptr;
        if (QMenuBar* mb = wWork.menuBar()) {
            if (!mb->actions().isEmpty() && mb->actions().first()->menu()) {
                for (QAction* act : mb->actions().first()->menu()->actions()) {
                    if (act->text() == QStringLiteral("Sair") || act->text() == QStringLiteral("Quit") ||
                        act->text() == QStringLiteral("Exit")) {
                        actQuit = act;
                    }
                }
            }
        }
        TEST_ASSERT(actQuit != nullptr, "Ação Sair deve existir no menu Arquivo");
        actQuit->trigger();
        pumpEvents(50);

        auto* closeMsg = findVisibleTopLevel<QMessageBox>();
        TEST_ASSERT(wWork.isVisible() && closeMsg != nullptr,
                    "Tentativa interativa de fechar projeto não salvo deve exibir confirmação e manter GUI aberta");
        QAbstractButton* cancelBtn = nullptr;
        for (QAbstractButton* b : closeMsg->buttons()) {
            if (b->text() == QStringLiteral("Cancelar")) cancelBtn = b;
        }
        TEST_ASSERT(cancelBtn != nullptr, "Diálogo de fechamento deve conter botão Cancelar");
        cancelBtn->click();
        pumpEvents(50);
        TEST_ASSERT(wWork.isVisible() && vp->model()->getNodes().size() == 2,
                    "Cancelar fechamento deve manter janela e todas as edições intactas");

        // Simula fechamento da janela: deve gravar snapshot atômico de recuperação em disco
        wWork.close();
        pumpEvents(50);
    }

    TEST_ASSERT(omniframe::io::ProjectRecoveryStore::inspectSnapshot().available,
                "Snapshot de recuperação automática deve existir em disco após fechar sem salvar");

    // 3) Reabertura após fechamento inadvertido: inicia VAZIO na viewport e oferece recuperação integral
    QTemporaryDir saveTmp;
    TEST_ASSERT(saveTmp.isValid(), "QTemporaryDir para salvamento final");
    const QString finalProjectPath = saveTmp.filePath(QStringLiteral("galpao_final.omniframe"));

    {
        MainWindow wRecovered;
        wRecovered.setAttribute(Qt::WA_DontShowOnScreen, true);
        wRecovered.resize(1280, 820);
        wRecovered.show();
        pumpEvents(80);

        auto* vp = wRecovered.findChild<Viewport3D*>();
        TEST_ASSERT(vp != nullptr && vp->model() != nullptr &&
                        vp->model()->getNodes().empty() && vp->model()->getElements().empty(),
                    "Mesmo havendo sessão recuperável, a reabertura deve iniciar com viewport vazia");

        auto* recBox = findVisibleTopLevel<QMessageBox>();
        TEST_ASSERT(recBox != nullptr,
                    "Ao existir sessão não salva anterior, a abertura deve oferecer recuperação não-bloqueante");
        TEST_ASSERT(recBox->windowTitle() == QStringLiteral("Retomar Sessão Anterior") &&
                        recBox->text().contains(QStringLiteral("Galpão Industrial Protegido")),
                    "Prompt inicial deve identificar o nome do projeto não salvo em português");

        // Valida tradução dinâmica em tempo de execução no diálogo aberto de retomada de sessão
        traducao.definir_idioma("en_US");
        pumpEvents(40);
        TEST_ASSERT(recBox->windowTitle() == QStringLiteral("Resume Previous Session") &&
                        recBox->text().contains(QStringLiteral("Galpão Industrial Protegido")) &&
                        recBox->text().contains(QStringLiteral("Would you like to load the previous edit or start a new project?")),
                    "Prompt aberto de retomada deve traduzir dinamicamente para inglês em tempo de execução");
        QAbstractButton* btnRecoverEn = nullptr;
        QAbstractButton* btnNewEn = nullptr;
        for (QAbstractButton* b : recBox->buttons()) {
            if (b->text() == QStringLiteral("Load Previous Edit")) btnRecoverEn = b;
            if (b->text() == QStringLiteral("Start New Project")) btnNewEn = b;
        }
        TEST_ASSERT(btnRecoverEn != nullptr && btnNewEn != nullptr,
                    "Botões do prompt de retomada devem traduzir para inglês dinamicamente");

        traducao.definir_idioma("pt_BR");
        pumpEvents(40);
        QAbstractButton* btnRecover = nullptr;
        QAbstractButton* btnStartNew = nullptr;
        for (QAbstractButton* b : recBox->buttons()) {
            if (b->text() == QStringLiteral("Carregar Edição Anterior")) btnRecover = b;
            if (b->text() == QStringLiteral("Iniciar Novo Projeto")) btnStartNew = b;
        }
        TEST_ASSERT(btnRecover != nullptr && btnStartNew != nullptr,
                    "Prompt de recuperação deve possuir botões 'Carregar Edição Anterior' e 'Iniciar Novo Projeto'");
        btnRecover->click();
        pumpEvents(60);

        const StructuralModel* restored = vp->model();
        TEST_ASSERT(restored->getNodes().size() == 2 && restored->getElements().size() == 1 &&
                        restored->metadata().projectName == "Galpão Industrial Protegido" &&
                        std::abs(restored->metadata().windV0_ms - 44.0) < 1e-6 &&
                        restored->solverOptions().type == kernel::LinearSolverType::SparseLU &&
                        std::abs(restored->findNode(1)->support.springStiffness[2] - 4200.0) < 1e-6 &&
                        restored->findElement(1)->thermalLoad.active &&
                        std::abs(restored->findElement(1)->thermalLoad.deltaT - 18.0) < 1e-6 &&
                        wRecovered.hasUnsavedEdits(),
                    "Recuperação deve restaurar integralmente toda a produção estrutural editada");

        TEST_ASSERT(wRecovered.saveProjectToPath(finalProjectPath),
                    "Salvar projeto recuperado deve gravar arquivo .omniframe final");
        TEST_ASSERT(!wRecovered.hasUnsavedEdits() &&
                        !omniframe::io::ProjectRecoveryStore::inspectSnapshot().available,
                    "Salvar no arquivo final deve limpar pendência e remover rascunho temporário de recuperação");
        const omniframe::io::ProjectRecoveryInfo savedSessionInfo =
            omniframe::io::ProjectRecoveryStore::inspectPreviousSession();
        TEST_ASSERT(savedSessionInfo.available && !savedSessionInfo.hasUnsavedChanges &&
                        savedSessionInfo.sourceKind == omniframe::io::SessionSourceKind::SavedProject &&
                        savedSessionInfo.projectFilePath == finalProjectPath,
                    "Após salvar projeto, estado de sessão deve registrar o projeto salvo ativo");
        TEST_ASSERT(wRecovered.currentProjectFilePath() == finalProjectPath,
                    "Caminho ativo do projeto deve ser atualizado após salvar");

        wRecovered.close();
        pumpEvents(40);
    }

    // 3b) Reinício com projeto salvo "x" limpo aberto ao sair: inicia vazio, identifica "x" e recarrega se aceito
    {
        MainWindow wResumeProj;
        wResumeProj.setAttribute(Qt::WA_DontShowOnScreen, true);
        wResumeProj.resize(1280, 820);
        wResumeProj.show();
        pumpEvents(80);

        auto* vp = wResumeProj.findChild<Viewport3D*>();
        TEST_ASSERT(vp != nullptr && vp->model() != nullptr &&
                        vp->model()->getNodes().empty() && vp->model()->getElements().empty(),
                    "Mesmo havendo projeto salvo anterior, app deve iniciar com viewport vazia");

        auto* recBox = findVisibleTopLevel<QMessageBox>();
        TEST_ASSERT(recBox != nullptr &&
                        recBox->text().contains(QFileInfo(finalProjectPath).fileName()),
                    "Prompt de inicialização deve identificar o projeto anterior aberto na tela");

        traducao.definir_idioma("en_US");
        pumpEvents(40);
        QAbstractButton* btnLoadProjEn = nullptr;
        for (QAbstractButton* b : recBox->buttons()) {
            if (b->text() == QStringLiteral("Load Previous Project")) btnLoadProjEn = b;
        }
        TEST_ASSERT(btnLoadProjEn != nullptr &&
                        recBox->text().contains(QStringLiteral("project")) &&
                        recBox->text().contains(QFileInfo(finalProjectPath).fileName()),
                    "Prompt de projeto anterior deve traduzir para inglês preservando nome do projeto");
        traducao.definir_idioma("pt_BR");
        pumpEvents(40);

        QAbstractButton* btnLoadProj = nullptr;
        for (QAbstractButton* b : recBox->buttons()) {
            if (b->text() == QStringLiteral("Carregar Projeto Anterior")) btnLoadProj = b;
        }
        TEST_ASSERT(btnLoadProj != nullptr, "Botão 'Carregar Projeto Anterior' deve estar presente");
        btnLoadProj->click();
        pumpEvents(60);

        TEST_ASSERT(vp->model()->getNodes().size() == 2 && vp->model()->getElements().size() == 1 &&
                        !wResumeProj.hasUnsavedEdits() &&
                        wResumeProj.currentProjectFilePath() == finalProjectPath,
                    "Aceitar 'Carregar Projeto Anterior' deve reabrir o projeto salvo x intacto");

        // Agora abre um exemplo pré-definido ("Exemplo 2: Balanço Espacial em L") e fecha o app
        wResumeProj.loadExampleCantilever();
        pumpEvents(40);
        TEST_ASSERT(vp->model()->getNodes().size() == 3 && !wResumeProj.hasUnsavedEdits(),
                    "Exemplo 2 deve carregar limpo sem alterações pendentes");
        wResumeProj.close();
        pumpEvents(40);
    }

    // 3c) Reinício após fechar com exemplo pré-definido aberto: identifica exemplo "y", traduz em tempo real e recarrega
    {
        MainWindow wResumeEx;
        wResumeEx.setAttribute(Qt::WA_DontShowOnScreen, true);
        wResumeEx.resize(1280, 820);
        wResumeEx.show();
        pumpEvents(80);

        auto* vp = wResumeEx.findChild<Viewport3D*>();
        TEST_ASSERT(vp != nullptr && vp->model() != nullptr &&
                        vp->model()->getNodes().empty() && vp->model()->getElements().empty(),
                    "Mesmo tendo deixado exemplo aberto na sessão anterior, reinício deve começar vazio");

        auto* recBox = findVisibleTopLevel<QMessageBox>();
        TEST_ASSERT(recBox != nullptr &&
                        recBox->text().contains(QStringLiteral("Exemplo 2: Balanço Espacial em L")),
                    "Prompt deve identificar o exemplo estrutural pré-definido aberto na sessão anterior");

        traducao.definir_idioma("en_US");
        pumpEvents(40);
        QAbstractButton* btnLoadExEn = nullptr;
        for (QAbstractButton* b : recBox->buttons()) {
            if (b->text() == QStringLiteral("Load Previous Example")) btnLoadExEn = b;
        }
        TEST_ASSERT(btnLoadExEn != nullptr &&
                        recBox->text().contains(QStringLiteral("Example 2: 3D L-Shaped Cantilever")),
                    "Nome do exemplo e botões do prompt devem traduzir em tempo de execução para inglês");
        btnLoadExEn->click();
        pumpEvents(60);

        TEST_ASSERT(vp->model()->getNodes().size() == 3 && vp->model()->getElements().size() == 2 &&
                        !wResumeEx.hasUnsavedEdits(),
                    "Aceitar carregamento deve restaurar o Exemplo 2 completo");

        // Edita o exemplo e encerra para testar prioridade de "Carregar Edição Anterior" sobre exemplo base
        traducao.definir_idioma("pt_BR");
        pumpEvents(20);
        vp->model()->addNode(9.0, 9.0, 4.0);
        vp->model()->pushUndo();
        TEST_ASSERT(QMetaObject::invokeMethod(&wResumeEx, "onModelModified", Qt::DirectConnection),
                    "Notificar modificação no Exemplo 2");
        pumpEvents(50);
        wResumeEx.close();
        pumpEvents(50);
    }

    // 3d) Reinício após editar exemplo: restaura edição anterior, depois valida "Iniciar Novo Projeto" limpando sessão
    {
        MainWindow wResumeEditedEx;
        wResumeEditedEx.setAttribute(Qt::WA_DontShowOnScreen, true);
        wResumeEditedEx.resize(1280, 820);
        wResumeEditedEx.show();
        pumpEvents(80);

        auto* vp = wResumeEditedEx.findChild<Viewport3D*>();
        TEST_ASSERT(vp != nullptr && vp->model() != nullptr && vp->model()->getNodes().empty(),
                    "Reinício com exemplo editado também deve iniciar vazio na viewport");

        auto* recBox = findVisibleTopLevel<QMessageBox>();
        TEST_ASSERT(recBox != nullptr &&
                        recBox->text().contains(QStringLiteral("Exemplo 2: Balanço Espacial em L")),
                    "Prompt deve identificar que as edições não salvas pertencem ao Exemplo 2");
        QAbstractButton* btnLoadEdit = nullptr;
        for (QAbstractButton* b : recBox->buttons()) {
            if (b->text() == QStringLiteral("Carregar Edição Anterior")) btnLoadEdit = b;
        }
        TEST_ASSERT(btnLoadEdit != nullptr,
                    "Exemplo com edições não salvas deve oferecer 'Carregar Edição Anterior'");
        btnLoadEdit->click();
        pumpEvents(60);

        TEST_ASSERT(vp->model()->getNodes().size() == 4 && wResumeEditedEx.hasUnsavedEdits(),
                    "Carregar Edição Anterior deve preservar os nós editados além dos 3 originais do Exemplo 2");
        wResumeEditedEx.close();
        pumpEvents(50);
    }

    {
        MainWindow wDeclineSession;
        wDeclineSession.setAttribute(Qt::WA_DontShowOnScreen, true);
        wDeclineSession.resize(1280, 820);
        wDeclineSession.show();
        pumpEvents(80);

        auto* vp = wDeclineSession.findChild<Viewport3D*>();
        auto* recBox = findVisibleTopLevel<QMessageBox>();
        TEST_ASSERT(recBox != nullptr, "Prompt de retomada deve aparecer antes de optar por novo projeto");
        QAbstractButton* btnNew = nullptr;
        for (QAbstractButton* b : recBox->buttons()) {
            if (b->text() == QStringLiteral("Iniciar Novo Projeto")) btnNew = b;
        }
        TEST_ASSERT(btnNew != nullptr, "Botão 'Iniciar Novo Projeto' deve estar disponível");
        btnNew->click();
        pumpEvents(50);

        TEST_ASSERT(vp->model()->getNodes().empty() && vp->model()->getElements().empty() &&
                        !omniframe::io::ProjectRecoveryStore::inspectPreviousSession().available &&
                        !omniframe::io::ProjectRecoveryStore::inspectSnapshot().available,
                    "Escolher 'Iniciar Novo Projeto' deve manter viewport vazia e limpar sessão anterior");
        wDeclineSession.close();
        pumpEvents(40);
    }

    // 4) Planos de trabalho (Planta XY e Lateral YZ) e exemplos nunca podem ser cortados ao meio por ClipPlane residual
    {
        MainWindow wClip;
        wClip.resize(1000, 720);
        wClip.show();
        pumpEvents(60);

        auto* vp = wClip.findChild<Viewport3D*>();
        TEST_ASSERT(vp != nullptr, "Viewport3D deve existir para teste de corte e planos");
        vp->setClipPlane(Viewport3D::ClipAxis::Y, 2.5, true);
        vp->setGridStep(0.5);
        pumpEvents(30);

        // Na vista em Planta (XY) e Lateral (YZ), ambas as metades (superior e inferior do grid) devem renderizar linhas do plano
        auto verifyGridSymmetricCoverage = [&](WorkPlane wp, const char* tag) {
            vp->setWorkPlane(wp);
            if (wp == WorkPlane::XY_Top) {
                vp->viewTopXY();
            } else {
                vp->viewSideYZ();
            }
            vp->fitAll();
            vp->update();
            pumpEvents(60);
            const QImage frame = vp->grabFramebuffer();
            TEST_ASSERT(!frame.isNull() && frame.width() > 200 && frame.height() > 200,
                        std::string("Captura do Viewport deve ser valida em ") + tag);
            const QColor bg = frame.pixelColor(frame.width() - 4, 4);
            auto countNonBgInBand = [&](int y0, int y1) {
                int count = 0;
                for (int y = y0; y < y1; y += 2) {
                    for (int x = frame.width() / 4; x < (frame.width() * 3) / 4; x += 2) {
                        const QColor px = frame.pixelColor(x, y);
                        const int diff = std::abs(px.red() - bg.red()) +
                                         std::abs(px.green() - bg.green()) +
                                         std::abs(px.blue() - bg.blue());
                        if (diff > 24) {
                            ++count;
                        }
                    }
                }
                return count;
            };
            const int topHalfPixels = countNonBgInBand(frame.height() / 5, frame.height() / 2 - 10);
            const int botHalfPixels = countNonBgInBand(frame.height() / 2 + 10, (frame.height() * 4) / 5);
            TEST_ASSERT(topHalfPixels > 20 && botHalfPixels > 20,
                        std::string("Plano de trabalho ") + tag + " deve renderizar ambas as metades sem corte pela metade");
        };

        verifyGridSymmetricCoverage(WorkPlane::XY_Top, "Planta (XY)");
        verifyGridSymmetricCoverage(WorkPlane::YZ_Side, "Lateral (YZ)");

        // Carregar qualquer dos 3 exemplos deve limpar o ClipAxis residual para não cortar a estrutura
        wClip.loadExampleBuilding();
        pumpEvents(40);
        TEST_ASSERT(vp->clipAxis() == Viewport3D::ClipAxis::None &&
                        vp->model()->getNodes().size() == 12,
                    "loadExampleBuilding deve limpar plano de corte residual e carregar todos os 12 nós");
        wClip.loadExampleCantilever();
        pumpEvents(40);
        TEST_ASSERT(vp->clipAxis() == Viewport3D::ClipAxis::None,
                    "loadExampleCantilever deve limpar plano de corte residual");
        wClip.loadExampleSpaceTruss();
        pumpEvents(40);
        TEST_ASSERT(vp->clipAxis() == Viewport3D::ClipAxis::None,
                    "loadExampleSpaceTruss deve limpar plano de corte residual");

        wClip.close();
        pumpEvents(40);
    }

    traducao.definir_idioma(originalLang);
}

void testStartupPolicyConfigurationAndLogsIntegration() {
    auto& traducao = locale::GerenciadorTraducao::instance();
    const QString originalLang = traducao.obter_idioma_atual();
    const UiTheme originalTheme = ThemeManager::instance().currentTheme();
    const UiStyleMode originalMode = ThemeManager::instance().uiStyleMode();
    traducao.definir_idioma(QStringLiteral("pt_BR"));

    omniframe::io::AppLogger::initialize(true, false);

    // 1) Verifica menu "Sessão, Inicialização e Logs", diálogo não-bloqueante, temas e tradução dinâmica
    {
        MainWindow wCfg;
        wCfg.setAttribute(Qt::WA_DontShowOnScreen, true);
        wCfg.resize(1280, 820);
        wCfg.show();
        pumpEvents(60);

        TEST_ASSERT(wCfg.startupSessionBehavior() == omniframe::io::StartupSessionBehavior::AskOnStartup &&
                        wCfg.confirmUnsavedOnClose(),
                    "Configuração inicial padrão deve perguntar ao iniciar e confirmar ao sair");

        QMenu* startupMenu = wCfg.findChild<QMenu*>(QStringLiteral("startupSessionMenu"));
        QAction* actAsk = wCfg.findChild<QAction*>(QStringLiteral("actStartupAsk"));
        QAction* actAutoLoad = wCfg.findChild<QAction*>(QStringLiteral("actStartupAutoLoad"));
        QAction* actNewProj = wCfg.findChild<QAction*>(QStringLiteral("actStartupNewProject"));
        QAction* actConfirmClose = wCfg.findChild<QAction*>(QStringLiteral("actConfirmUnsavedOnClose"));
        QAction* actOpenDlg = wCfg.findChild<QAction*>(QStringLiteral("actOpenStartupSessionDialog"));
        QAction* actOpenLogs = wCfg.findChild<QAction*>(QStringLiteral("actOpenLogDirectory"));
        TEST_ASSERT(startupMenu && actAsk && actAutoLoad && actNewProj &&
                        actConfirmClose && actOpenDlg && actOpenLogs,
                    "Todas as ações do submenu Sessão, Inicialização e Logs devem existir");
        TEST_ASSERT(startupMenu->title() == QStringLiteral("Sessão, Inicialização e Logs") &&
                        actAsk->isChecked() && actConfirmClose->isChecked(),
                    "Estado inicial e títulos do submenu devem refletir padrão em português");

        actOpenDlg->trigger();
        pumpEvents(50);
        StartupSessionDialog* dlg = nullptr;
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (auto* sd = qobject_cast<StartupSessionDialog*>(top); sd && sd->isVisible()) {
                dlg = sd;
                break;
            }
        }
        TEST_ASSERT(dlg != nullptr && !dlg->isModal(),
                    "StartupSessionDialog deve abrir de forma não-bloqueante");

        TEST_ASSERT(traducao.definir_idioma(QStringLiteral("en_US")), "Alternar para en_US");
        pumpEvents(40);
        TEST_ASSERT(startupMenu->title() == QStringLiteral("Session, Startup and Logs") &&
                        actAsk->text() == QStringLiteral("Ask on startup (Show dialog)") &&
                        actAutoLoad->text() == QStringLiteral("Automatically load previous project or example") &&
                        actNewProj->text() == QStringLiteral("Always start a new project (Empty)") &&
                        actOpenDlg->text() == QStringLiteral("Configure Startup, Session and Logs...") &&
                        actOpenLogs->text() == QStringLiteral("Open Logs Folder...") &&
                        dlg->windowTitle() == QStringLiteral("Session, Startup and Log Settings"),
                    "Submenu e StartupSessionDialog devem traduzir dinamicamente para inglês em tempo de execução");

        TEST_ASSERT(traducao.definir_idioma(QStringLiteral("pt_BR")), "Retornar para pt_BR");
        pumpEvents(40);
        TEST_ASSERT(dlg->windowTitle() == QStringLiteral("Configurações de Sessão, Inicialização e Logs"),
                    "StartupSessionDialog deve retornar ao título em português em tempo de execução");

        auto* logDirEdit = dlg->findChild<QLineEdit*>(QStringLiteral("logDirPathEdit"));
        auto* logFileEdit = dlg->findChild<QLineEdit*>(QStringLiteral("logCurrentFileEdit"));
        auto* logPreview = dlg->findChild<QPlainTextEdit*>(QStringLiteral("logPreviewEdit"));
        TEST_ASSERT(logDirEdit && logFileEdit && logPreview &&
                        QDir::fromNativeSeparators(logDirEdit->text()).endsWith(QStringLiteral("/log")) &&
                        QFileInfo(logFileEdit->text()).fileName() ==
                            omniframe::io::AppLogger::logFileNameForDate(QDate::currentDate()) &&
                        !logPreview->toPlainText().isEmpty(),
                    "Diálogo deve exibir diretório, nome omniframe_aaaa-mm-dd.log e conteúdo gravado");

        ThemeManager::instance().setUiStyleMode(UiStyleMode::Native);
        ThemeManager::instance().setTheme(UiTheme::Light);
        pumpEvents(25);
        ThemeManager::instance().setUiStyleMode(UiStyleMode::Custom);
        ThemeManager::instance().setTheme(UiTheme::Dark);
        pumpEvents(25);

        auto* radioAuto = dlg->findChild<QRadioButton*>(QStringLiteral("radioStartupAutoLoad"));
        auto* chkClose = dlg->findChild<QCheckBox*>(QStringLiteral("chkConfirmUnsavedOnClose"));
        TEST_ASSERT(radioAuto && chkClose, "Controles radio/checkbox do StartupSessionDialog devem existir");
        radioAuto->click();
        chkClose->click();
        pumpEvents(40);

        TEST_ASSERT(wCfg.startupSessionBehavior() == omniframe::io::StartupSessionBehavior::AutoLoadPrevious &&
                        !wCfg.confirmUnsavedOnClose() &&
                        actAutoLoad->isChecked() && !actConfirmClose->isChecked(),
                    "Alterações no StartupSessionDialog devem sincronizar menu e MainWindow em tempo real");

        actOpenLogs->trigger();
        pumpEvents(30);
        dlg->close();
        pumpEvents(30);

        wCfg.loadExampleCantilever();
        pumpEvents(40);
        auto* vp = wCfg.findChild<Viewport3D*>();
        TEST_ASSERT(vp && vp->model() && vp->model()->getNodes().size() == 3,
                    "Exemplo 2 carregado com 3 nós");
        vp->model()->addNode(7.0, 6.0, 2.0);
        vp->model()->pushUndo();
        TEST_ASSERT(QMetaObject::invokeMethod(&wCfg, "onModelModified", Qt::DirectConnection),
                    "Notificar modificação estrutural");
        pumpEvents(40);

        QAction* actQuit = nullptr;
        if (QMenuBar* mb = wCfg.menuBar()) {
            if (!mb->actions().isEmpty() && mb->actions().first()->menu()) {
                for (QAction* act : mb->actions().first()->menu()->actions()) {
                    if (act->text() == QStringLiteral("Sair") || act->text() == QStringLiteral("Quit")) {
                        actQuit = act;
                    }
                }
            }
        }
        TEST_ASSERT(actQuit != nullptr, "Ação Sair deve existir no menu Arquivo");
        actQuit->trigger();
        pumpEvents(50);

        TEST_ASSERT(findVisibleTopLevel<QMessageBox>() == nullptr && !wCfg.isVisible(),
                    "Com confirmUnsavedOnClose=false, Sair deve fechar imediatamente preservando rascunho de recuperação");
    }

    // Confirma persistência de startupSessionBehavior = "auto_load" em user_preferences.json
    {
        bool ok = false;
        const QJsonObject root = omniframe::io::UserPreferencesStore::readRootObject(&ok);
        const QJsonObject appObj = root.value(QStringLiteral("app")).toObject();
        TEST_ASSERT(ok &&
                        appObj.value(QStringLiteral("startupSessionBehavior")).toString() ==
                            QStringLiteral("auto_load") &&
                        !appObj.value(QStringLiteral("confirmUnsavedOnClose")).toBool(true),
                    "Preferências de inicialização devem estar persistidas em user_preferences.json");
    }

    // 2) Reinício com AutoLoadPrevious: carrega automaticamente o exemplo/edição anterior sem exibir diálogo
    {
        MainWindow wAutoResume;
        wAutoResume.setAttribute(Qt::WA_DontShowOnScreen, true);
        wAutoResume.resize(1280, 820);
        wAutoResume.show();
        pumpEvents(90);

        auto* vp = wAutoResume.findChild<Viewport3D*>();
        TEST_ASSERT(findVisibleTopLevel<QMessageBox>() == nullptr &&
                        vp != nullptr && vp->model() != nullptr &&
                        vp->model()->getNodes().size() == 4 &&
                        wAutoResume.hasUnsavedEdits(),
                    "Com AutoLoadPrevious configurado, app deve carregar automaticamente o exemplo editado sem emitir diálogo");

        QAction* actNewProj = wAutoResume.findChild<QAction*>(QStringLiteral("actStartupNewProject"));
        TEST_ASSERT(actNewProj != nullptr, "Ação Sempre Iniciar Novo Projeto deve existir");
        actNewProj->trigger();
        pumpEvents(30);
        wAutoResume.loadExampleSpaceTruss();
        pumpEvents(50);
        TEST_ASSERT(!vp->model()->getNodes().empty(), "Exemplo 3 carregado antes de encerrar");

        wAutoResume.close();
        pumpEvents(50);
    }

    // 3) Reinício com AlwaysStartNew: inicia sempre vazio sem exibir diálogo e limpa estado anterior
    {
        MainWindow wAlwaysNew;
        wAlwaysNew.setAttribute(Qt::WA_DontShowOnScreen, true);
        wAlwaysNew.resize(1280, 820);
        wAlwaysNew.show();
        pumpEvents(90);

        auto* vp = wAlwaysNew.findChild<Viewport3D*>();
        TEST_ASSERT(findVisibleTopLevel<QMessageBox>() == nullptr &&
                        vp != nullptr && vp->model() != nullptr &&
                        vp->model()->getNodes().empty() && vp->model()->getElements().empty() &&
                        !omniframe::io::ProjectRecoveryStore::inspectPreviousSession().available,
                    "Com AlwaysStartNew configurado, app deve iniciar vazio sem prompt e limpar sessão anterior");

        if (auto* actOpenDlg = wAlwaysNew.findChild<QAction*>(QStringLiteral("actOpenStartupSessionDialog"))) {
            actOpenDlg->trigger();
            pumpEvents(40);
            StartupSessionDialog* dlg = nullptr;
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (auto* sd = qobject_cast<StartupSessionDialog*>(top); sd && sd->isVisible()) {
                    dlg = sd;
                    break;
                }
            }
            TEST_ASSERT(dlg != nullptr, "StartupSessionDialog deve abrir no teste de restauração");
            auto* btnRestore = dlg->findChild<QPushButton*>(QStringLiteral("btnStartupRestoreDefaults"));
            TEST_ASSERT(btnRestore != nullptr, "Botão Restaurar Padrão deve existir");
            btnRestore->click();
            pumpEvents(30);
            TEST_ASSERT(wAlwaysNew.startupSessionBehavior() == omniframe::io::StartupSessionBehavior::AskOnStartup &&
                            wAlwaysNew.confirmUnsavedOnClose(),
                        "Restaurar Padrão deve restabelecer AskOnStartup e confirmUnsavedOnClose=true");
            dlg->close();
        }
        wAlwaysNew.close();
        pumpEvents(40);
    }

    // 4) Confirma registros completos no arquivo omniframe_aaaa-mm-dd.log no diretório isolado
    {
        omniframe::io::AppLogger::flush();
        const QString logFile = omniframe::io::AppLogger::currentLogFilePath();
        QFile f(logFile);
        TEST_ASSERT(QFile::exists(logFile) && f.open(QIODevice::ReadOnly | QIODevice::Text),
                    "Arquivo de log diario do fluxo UI deve existir");
        const QString contents = QString::fromUtf8(f.readAll());
        TEST_ASSERT(contents.contains(QStringLiteral("AutoLoadPrevious")) &&
                        contents.contains(QStringLiteral("AlwaysStartNew")) &&
                        contents.contains(QStringLiteral("[MainWindow]")) &&
                        contents.contains(QStringLiteral("[Settings]")),
                    "Arquivo de log da GUI deve registrar eventos de inicializacao, politicas e configuracoes");
        TEST_ASSERT(omniframe::io::AppLogger::existingLogFiles().size() <= omniframe::io::AppLogger::kMaxLogFiles,
                    "Quantidade de arquivos de log na pasta nao deve ultrapassar 10");
    }

    ThemeManager::instance().setUiStyleMode(originalMode);
    ThemeManager::instance().setTheme(originalTheme);
    traducao.definir_idioma(originalLang);
}

void testToolbarOverflowExtensionArrowsAllModes() {
    auto& traducao = locale::GerenciadorTraducao::instance();
    const QString originalLang = traducao.obter_idioma_atual();
    const UiTheme originalTheme = ThemeManager::instance().currentTheme();
    const UiStyleMode originalMode = ThemeManager::instance().uiStyleMode();

    traducao.definir_idioma(QStringLiteral("pt_BR"));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1200, 760);
    w.show();
    pumpEvents(80);

    // Parte A: No modo Nativo (Escuro e Claro), validar moldura nos botões de dock + pontinhos nas barras de arraste
    auto* inspBtn = w.findChild<QToolButton*>(QStringLiteral("inspectorCollapseButton"));
    auto* resBtn = w.findChild<QToolButton*>(QStringLiteral("resultsCollapseButton"));
    auto* viewport = w.findChild<Viewport3D*>();
    TEST_ASSERT(inspBtn && resBtn && viewport, "Botões de dock e viewport devem existir");
    auto* inspDock = qobject_cast<QDockWidget*>(inspBtn->parentWidget() ? inspBtn->parentWidget()->parentWidget() : nullptr);
    auto* resDock = qobject_cast<QDockWidget*>(resBtn->parentWidget() ? resBtn->parentWidget()->parentWidget() : nullptr);
    TEST_ASSERT(inspDock && resDock, "Docks Inspetor e Tabelas de Resultados devem existir");

    for (const UiTheme nativeTheme : {UiTheme::Dark, UiTheme::Light}) {
        ThemeManager::instance().setUiStyleMode(UiStyleMode::Native);
        ThemeManager::instance().setTheme(nativeTheme);
        w.resize(1200, 760);
        pumpEvents(70);

        const char* tag = (nativeTheme == UiTheme::Light) ? "Nativo Claro" : "Nativo Escuro";

        // 1) Moldura visível nos botões de expandir/recolher dos dois painéis dock
        for (QToolButton* dBtn : {inspBtn, resBtn}) {
            TEST_ASSERT(dBtn->autoRaise() && dBtn->styleSheet().isEmpty() && dBtn->size() == QSize(22, 22),
                        std::string("Botão de dock deve manter propriedades nativas em ") + tag);
            const QImage btnImg = dBtn->grab().toImage();
            TEST_ASSERT(!btnImg.isNull(), std::string("Captura do botão de dock inválida em ") + tag);
            const QColor titleBg = dBtn->parentWidget()
                ? dBtn->parentWidget()->palette().color(dBtn->parentWidget()->backgroundRole())
                : dBtn->palette().color(QPalette::Window);
            const QColor borderPx = btnImg.pixelColor(btnImg.width() / 2, 0);
            const QColor cornerInnerPx = btnImg.pixelColor(btnImg.width() / 2, 2);
            const int edgeDiff = std::abs(borderPx.red() - titleBg.red()) +
                                 std::abs(borderPx.green() - titleBg.green()) +
                                 std::abs(borderPx.blue() - titleBg.blue());
            const int innerDiff = std::abs(borderPx.red() - cornerInnerPx.red()) +
                                  std::abs(borderPx.green() - cornerInnerPx.green()) +
                                  std::abs(borderPx.blue() - cornerInnerPx.blue());
            TEST_ASSERT(edgeDiff >= 12 || innerDiff >= 12,
                        std::string("Botão de dock deve exibir borda/moldura visível no modo ") + tag);
        }

        // 2) Seta nativa visível no botão de cada painel
        const QColor expectedNativeArrow = (nativeTheme == UiTheme::Light)
            ? QColor(0x1e, 0x29, 0x3b)
            : QColor(0xe2, 0xe8, 0xf0);
        TEST_ASSERT(hasDistinctArrowTriangle(
                        inspBtn->grab().toImage(), inspBtn->rect(), expectedNativeArrow, Qt::LeftArrow),
                    std::string("Seta esquerda do Inspetor deve estar nítida no modo ") + tag);
        TEST_ASSERT(hasDistinctArrowTriangle(
                        resBtn->grab().toImage(), resBtn->rect(), expectedNativeArrow, Qt::DownArrow),
                    std::string("Seta inferior da Tabela de Resultados deve estar nítida no modo ") + tag);

        // 3) Pontinhos nas barras de arraste: vertical do Inspetor e horizontal da Tabela de Resultados
        const QImage winImg = w.grab().toImage();
        TEST_ASSERT(!winImg.isNull(), std::string("Captura da janela principal inválida em ") + tag);

        const QRect inspGeo = inspDock->geometry();
        const QRect resGeo = resDock->geometry();
        const QRect vpGeo = viewport->geometry();
        const int vSepLeft = inspGeo.right() + 1;
        const int vSepRight = vpGeo.left() - 1;
        const int hSepTop = vpGeo.bottom() + 1;
        const int hSepBottom = resGeo.top() - 1;
        TEST_ASSERT(vSepRight >= vSepLeft + 3 && hSepBottom >= hSepTop + 3,
                    std::string("Barras de arraste vertical e horizontal devem ter espessura mínima para pontinhos em ") + tag);

        auto countContrastDotsInBand = [&](int x0, int x1, int y0, int y1, const QColor& baseBg) {
            int count = 0;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const QColor px = winImg.pixelColor(x, y);
                    const int d = std::abs(px.red() - baseBg.red()) +
                                  std::abs(px.green() - baseBg.green()) +
                                  std::abs(px.blue() - baseBg.blue());
                    if (d >= 36) {
                        ++count;
                    }
                }
            }
            return count;
        };

        const QColor winBg = w.palette().color(QPalette::Window);
        const int vMidY = (inspGeo.top() + inspGeo.bottom()) / 2;
        const int vCenterDots = countContrastDotsInBand(
            vSepLeft, vSepRight, vMidY - 18, vMidY + 18, winBg);
        const int vCornerDots = countContrastDotsInBand(
            vSepLeft, vSepRight, inspGeo.top() + 8, inspGeo.top() + 28, winBg);
        TEST_ASSERT(vCenterDots >= 6 && vCenterDots > vCornerDots,
                    std::string("Barra de arraste vertical do Inspetor deve exibir pontinhos centralizados no modo ") + tag);

        const int hMidX = (resGeo.left() + resGeo.right()) / 2;
        const int hCenterDots = countContrastDotsInBand(
            hMidX - 18, hMidX + 18, hSepTop, hSepBottom, winBg);
        const int hCornerDots = countContrastDotsInBand(
            resGeo.left() + 8, resGeo.left() + 28, hSepTop, hSepBottom, winBg);
        TEST_ASSERT(hCenterDots >= 6 && hCenterDots > hCornerDots,
                    std::string("Barra de arraste horizontal da Tabela de Resultados deve exibir pontinhos centralizados no modo ") + tag);

        // 4) Pontinhos da barra vertical (e horizontal) devem permanecer após recolher/expandir
        //    Tabela de Resultados e Inspetor nos modos Nativo e Customizado
        auto countSepContrast = [&](const QImage& img, int x0, int x1, int y0, int y1, const QColor& baseBg) {
            int count = 0;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const QColor px = img.pixelColor(x, y);
                    const int d = std::abs(px.red() - baseBg.red()) +
                                  std::abs(px.green() - baseBg.green()) +
                                  std::abs(px.blue() - baseBg.blue());
                    if (d >= 18) {
                        ++count;
                    }
                }
            }
            return count;
        };

        auto assertVerticalBarDots = [&](const std::string& phaseLabel) {
            const QImage img = w.grab().toImage();
            const QRect ig = inspDock->geometry();
            const QRect vg = viewport->geometry();
            const int vl = ig.right() + 1;
            const int vr = vg.left() - 1;
            TEST_ASSERT(vr >= vl + 3,
                        "Barra vertical do Inspetor deve manter largura para pontinhos (" + phaseLabel + ")");
            TEST_ASSERT(inspDock->maximumWidth() > 1000,
                        "Inspetor expandido nao pode ficar travado com maximumWidth==320 (" + phaseLabel + ")");
            const QColor bg = w.palette().color(QPalette::Window);
            const int vMid = (ig.top() + ig.bottom()) / 2;
            const int center = countSepContrast(img, vl, vr, vMid - 18, vMid + 18, bg);
            const int corner = countSepContrast(img, vl, vr, ig.top() + 8, ig.top() + 28, bg);
            TEST_ASSERT(center >= 6 && center > corner,
                        "Pontinhos da barra vertical devem permanecer visiveis (" + phaseLabel + ")");
        };

        auto assertHorizontalBarDots = [&](const std::string& phaseLabel) {
            const QImage img = w.grab().toImage();
            const QRect rg = resDock->geometry();
            const QRect vg = viewport->geometry();
            const int ht = vg.bottom() + 1;
            const int hb = rg.top() - 1;
            TEST_ASSERT(hb >= ht + 3,
                        "Barra horizontal da Tabela de Resultados deve manter altura para pontinhos (" + phaseLabel + ")");
            TEST_ASSERT(resDock->maximumHeight() > 1000,
                        "Tabela de Resultados expandida nao pode ficar travada em height fixo (" + phaseLabel + ")");
            const QColor bg = w.palette().color(QPalette::Window);
            const int hMid = (rg.left() + rg.right()) / 2;
            const int center = countSepContrast(img, hMid - 18, hMid + 18, ht, hb, bg);
            const int corner = countSepContrast(img, rg.left() + 8, rg.left() + 28, ht, hb, bg);
            TEST_ASSERT(center >= 6 && center > corner,
                        "Pontinhos da barra horizontal devem permanecer visiveis (" + phaseLabel + ")");
        };

        for (const UiStyleMode checkMode : {UiStyleMode::Native, UiStyleMode::Custom}) {
            ThemeManager::instance().setUiStyleMode(checkMode);
            ThemeManager::instance().setTheme(nativeTheme);
            w.resize(1200, 760);
            pumpEvents(60);
            const std::string mLabel = std::string(tag) + "/" +
                (checkMode == UiStyleMode::Native ? "Nativo" : "Customizado");

            resBtn->click();
            pumpEvents(80);
            TEST_ASSERT(w.isResultsDockCollapsed(),
                        "Tabela de Resultados deve estar recolhida (" + mLabel + ")");
            assertVerticalBarDots(mLabel + " apos recolher Resultados");

            resBtn->click();
            pumpEvents(80);
            TEST_ASSERT(!w.isResultsDockCollapsed(),
                        "Tabela de Resultados deve expandir ao clicar novamente (" + mLabel + ")");
            assertVerticalBarDots(mLabel + " apos expandir Resultados");
            assertHorizontalBarDots(mLabel + " apos expandir Resultados");

            inspBtn->click();
            pumpEvents(80);
            TEST_ASSERT(w.isInspectorDockCollapsed(),
                        "Inspetor deve estar recolhido (" + mLabel + ")");
            inspBtn->click();
            pumpEvents(80);
            TEST_ASSERT(!w.isInspectorDockCollapsed(),
                        "Inspetor deve expandir ao clicar novamente (" + mLabel + ")");
            assertVerticalBarDots(mLabel + " apos recolher/expandir Inspetor");
            assertHorizontalBarDots(mLabel + " apos recolher/expandir Inspetor");

            if (checkMode == UiStyleMode::Native) {
                w.setResultsLayoutCount(2);
                pumpEvents(70);
                resBtn->click();
                pumpEvents(70);
                resBtn->click();
                pumpEvents(70);
                if (QSplitter* sp = w.resultsSplitter(); sp && sp->count() >= 2 && sp->handle(1)) {
                    QSplitterHandle* sh = sp->handle(1);
                    const QImage himg = sh->grab().toImage();
                    const QColor hbg = sh->palette().color(QPalette::Window);
                    const int sMid = sh->height() / 2;
                    const int sc = countSepContrast(himg, 0, sh->width() - 1, sMid - 18, sMid + 18, hbg);
                    const int sCorner = countSepContrast(himg, 0, sh->width() - 1, 4, 24, hbg);
                    TEST_ASSERT(sc >= 6 && sc > sCorner,
                                "Pontinhos verticais do splitter da Tabela de Resultados devem permanecer apos recolher/expandir (" + mLabel + ")");
                }
                w.setResultsLayoutCount(1);
                pumpEvents(40);
            }
        }
    }

    // Parte B: Largura estreita força ambas as barras superiores a exibirem o botão de extensão de overflow
    w.resize(560, 720);
    w.show();
    pumpEvents(80);

    const QList<QToolBar*> bars = w.findChildren<QToolBar*>();
    TEST_ASSERT(bars.size() >= 2, "MainWindow deve possuir pelo menos duas QToolBars superiores");

    auto findVisibleExtButtons = [&]() {
        std::vector<QToolButton*> buttons;
        for (QToolBar* tb : bars) {
            if (!tb) continue;
            if (auto* ext = tb->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"))) {
                if (ext->isVisible()) {
                    buttons.push_back(ext);
                }
            }
        }
        return buttons;
    };

    const std::vector<QToolButton*> initialExts = findVisibleExtButtons();
    TEST_ASSERT(initialExts.size() >= 2,
                "Em janela estreita, ambos os botoes de extensao das toolbars devem ficar visiveis");

    // Verifica tradução dinâmica do tooltip dos botões de extensão
    for (QToolButton* ext : initialExts) {
        TEST_ASSERT(ext->toolTip() == QStringLiteral("Expandir ou recolher botões ocultos da barra"),
                    "Tooltip do botão de extensão deve iniciar em português");
    }
    TEST_ASSERT(traducao.definir_idioma(QStringLiteral("en_US")), "Alternar para en_US");
    pumpEvents(40);
    for (QToolButton* ext : initialExts) {
        TEST_ASSERT(ext->toolTip() == QStringLiteral("Expand or collapse hidden toolbar buttons"),
                    "Tooltip do botão de extensão deve traduzir dinamicamente para inglês");
    }
    TEST_ASSERT(traducao.definir_idioma(QStringLiteral("pt_BR")), "Retornar para pt_BR");
    pumpEvents(40);

    // Valida seta apontada para BAIXO nitida e largura adequada em todos os 4 modos visuais
    struct VisualCombo {
        UiStyleMode mode;
        UiTheme theme;
        const char* tag;
    };
    const VisualCombo combos[] = {
        {UiStyleMode::Custom, UiTheme::Dark, "Customizado Escuro"},
        {UiStyleMode::Custom, UiTheme::Light, "Customizado Claro"},
        {UiStyleMode::Native, UiTheme::Dark, "Nativo Escuro"},
        {UiStyleMode::Native, UiTheme::Light, "Nativo Claro"},
    };

    for (const VisualCombo& c : combos) {
        ThemeManager::instance().setUiStyleMode(c.mode);
        ThemeManager::instance().setTheme(c.theme);
        w.resize(560, 720);
        pumpEvents(70);

        const std::vector<QToolButton*> exts = findVisibleExtButtons();
        TEST_ASSERT(exts.size() >= 2,
                    std::string("Botoes de extensao visiveis em ") + c.tag);

        const QColor expectedArrow = (c.theme == UiTheme::Light)
            ? QColor(0x1e, 0x29, 0x3b)
            : QColor(0xe2, 0xe8, 0xf0);

        for (QToolButton* ext : exts) {
            TEST_ASSERT(ext->width() >= 22 && ext->height() >= 18,
                        std::string("Largura do botao de extensao suficiente para seta unificada em ") + c.tag);
            const QImage img = ext->grab().toImage();
            TEST_ASSERT(!img.isNull() &&
                            hasDistinctArrowTriangle(img, QRect(0, 0, img.width(), img.height()),
                                                     expectedArrow, Qt::DownArrow),
                        std::string("Botao de extensao da toolbar deve renderizar seta apontada para baixo com o mesmo design dos paineis em ") + c.tag);
        }
    }

    // Valida clique para expandir/colapsar a barra temporária em Customizado e Nativo
    for (const UiStyleMode mode : {UiStyleMode::Custom, UiStyleMode::Native}) {
        ThemeManager::instance().setUiStyleMode(mode);
        ThemeManager::instance().setTheme(UiTheme::Dark);
        w.resize(560, 720);
        pumpEvents(60);

        const std::vector<QToolButton*> exts = findVisibleExtButtons();
        TEST_ASSERT(!exts.empty(), "Botao de extensao disponivel para teste de clique");
        QToolButton* ext = exts.front();
        ext->click();
        pumpEvents(50);

        const bool expandedVisible = (QApplication::activePopupWidget() != nullptr) || ext->isChecked();
        TEST_ASSERT(expandedVisible,
                    "Clicar no botao de extensao deve expandir a barra temporaria com widgets ocultos");
        if (ext->isChecked()) {
            const QImage expandedImg = ext->grab().toImage();
            const QColor expandedArrow = (mode == UiStyleMode::Custom)
                ? QColor(Qt::white)
                : QColor(0xe2, 0xe8, 0xf0);
            TEST_ASSERT(hasDistinctArrowTriangle(expandedImg,
                                                 QRect(0, 0, expandedImg.width(), expandedImg.height()),
                                                 expandedArrow,
                                                 Qt::UpArrow),
                        "Quando expandido, botao de extensao deve apontar para cima indicando colapso");
        }
        if (QWidget* pop = QApplication::activePopupWidget()) {
            pop->close();
        } else if (ext->isChecked()) {
            ext->click();
        }
        pumpEvents(40);
    }

    w.close();
    pumpEvents(40);
    ThemeManager::instance().setUiStyleMode(originalMode);
    ThemeManager::instance().setTheme(originalTheme);
    traducao.definir_idioma(originalLang);
}

void testFileExitShortcutAndNoDuplicatesAllModes() {
    using namespace omniframe;

    auto& traducao = locale::GerenciadorTraducao::instance();
    const QString originalLang = traducao.obter_idioma_atual();
    const UiTheme originalTheme = ThemeManager::instance().currentTheme();
    const UiStyleMode originalMode = ThemeManager::instance().uiStyleMode();

    traducao.definir_idioma(QStringLiteral("pt_BR"));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1100, 720);
    w.show();
    pumpEvents(60);

    QMenuBar* bar = w.menuBar();
    TEST_ASSERT(bar != nullptr && !bar->actions().isEmpty(), "Barra de menus deve existir");
    QMenu* fileMenu = bar->actions().front()->menu();
    TEST_ASSERT(fileMenu != nullptr, "Menu Arquivo deve existir");

    QAction* quitAct = nullptr;
    for (QAction* act : fileMenu->actions()) {
        if (act && !act->isSeparator() && act->text() == QStringLiteral("Sair")) {
            quitAct = act;
            break;
        }
    }
    TEST_ASSERT(quitAct != nullptr, "Acao Arquivo > Sair deve existir no menu Arquivo");

    struct Combo {
        const char* lang;
        const char* expectedMenu;
        const char* expectedText;
        UiStyleMode mode;
        UiTheme theme;
    };
    const Combo matrix[] = {
        {"pt_BR", "&Arquivo", "Sair", UiStyleMode::Custom, UiTheme::Dark},
        {"pt_BR", "&Arquivo", "Sair", UiStyleMode::Custom, UiTheme::Light},
        {"pt_BR", "&Arquivo", "Sair", UiStyleMode::Native, UiTheme::Dark},
        {"pt_BR", "&Arquivo", "Sair", UiStyleMode::Native, UiTheme::Light},
        {"en_US", "&File",    "Exit", UiStyleMode::Custom, UiTheme::Dark},
        {"en_US", "&File",    "Exit", UiStyleMode::Custom, UiTheme::Light},
        {"en_US", "&File",    "Exit", UiStyleMode::Native, UiTheme::Dark},
        {"en_US", "&File",    "Exit", UiStyleMode::Native, UiTheme::Light},
    };

    for (const Combo& c : matrix) {
        traducao.definir_idioma(QString::fromUtf8(c.lang));
        ThemeManager::instance().setUiStyleMode(c.mode);
        ThemeManager::instance().setTheme(c.theme);
        pumpEvents(40);

        TEST_ASSERT(fileMenu->title() == QString::fromUtf8(c.expectedMenu),
                    "Titulo do menu Arquivo/File deve traduzir dinamicamente");
        TEST_ASSERT(quitAct->text() == QString::fromUtf8(c.expectedText),
                    "Texto da acao Sair/Exit deve traduzir dinamicamente");

        const QKeySequence expectedSeq(Qt::CTRL | Qt::Key_Q);
        TEST_ASSERT(quitAct->shortcut() == expectedSeq,
                    "Acao Arquivo > Sair deve possuir o atalho Ctrl+Q");

        const QString shortcutNative = quitAct->shortcut().toString(QKeySequence::NativeText);
        const QString shortcutPortable = quitAct->shortcut().toString(QKeySequence::PortableText);
        TEST_ASSERT(shortcutNative == QStringLiteral("Ctrl+Q") &&
                        shortcutPortable == QStringLiteral("Ctrl+Q"),
                    "Atalho de Arquivo > Sair deve ser exibido como Ctrl+Q em ambas as traducoes/UIs/temas");
        TEST_ASSERT(shortcutNative != quitAct->text() &&
                        !shortcutNative.contains(QStringLiteral("Sair"), Qt::CaseInsensitive) &&
                        !shortcutNative.contains(QStringLiteral("Exit"), Qt::CaseInsensitive),
                    "Menu Arquivo > Sair nunca deve duplicar texto como Sair\\tSair ou Exit\\tExit");

        // Garante unicidade dos atalhos no menu e ausencia de duplicacao rotulo == atalho
        QSet<QString> seenShortcuts;
        for (QAction* topAct : bar->actions()) {
            if (!topAct || !topAct->menu()) continue;
            for (QAction* itemAct : topAct->menu()->actions()) {
                if (!itemAct || itemAct->isSeparator() || itemAct->shortcut().isEmpty()) continue;
                const QString sc = itemAct->shortcut().toString(QKeySequence::PortableText);
                TEST_ASSERT(sc != itemAct->text(),
                            "Nenhum item de menu pode repetir seu proprio rotulo no atalho");
                TEST_ASSERT(!seenShortcuts.contains(sc),
                            "Atalho de menu nao pode colidir com outro atalho ativo");
                seenShortcuts.insert(sc);
            }
        }
    }

    // Valida que acionar Arquivo > Sair (Ctrl+Q) fecha janela limpa normalmente
    TEST_ASSERT(w.isVisible(), "Janela deve estar visivel antes de acionar Arquivo > Sair");
    quitAct->trigger();
    pumpEvents(50);
    TEST_ASSERT(!w.isVisible(), "Acionar Arquivo > Sair (Ctrl+Q) deve encerrar janela limpa");

    ThemeManager::instance().setUiStyleMode(originalMode);
    ThemeManager::instance().setTheme(originalTheme);
    traducao.definir_idioma(originalLang);
}

void testModelessPdfExportSurvivesAnalysisSuiteDestruction() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser valido para teste de UAF em PDF");
    const QString outPdfPath = tmp.filePath(QStringLiteral("memorial_uaf_aud005.pdf"));

    MainWindow mw;
    mw.show();
    pumpEvents(60);

    mw.loadExampleBuilding();
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onSolveStructure", Qt::DirectConnection),
                "Solver estatico deve executar antes da exportacao PDF");
    pumpEvents(50);

    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenAnalysisSuite", Qt::DirectConnection),
                "AnalysisSuiteDialog deve abrir");
    pumpEvents(50);
    auto* suiteDlg = findVisibleTopLevel<AnalysisSuiteDialog>();
    TEST_ASSERT(suiteDlg != nullptr, "AnalysisSuiteDialog deve estar visivel");
    QPointer<AnalysisSuiteDialog> suitePtr(suiteDlg);

    TEST_ASSERT(QMetaObject::invokeMethod(suiteDlg, "onRunModalAsync", Qt::DirectConnection),
                "Analise modal deve iniciar no AnalysisSuiteDialog");
    TEST_ASSERT(waitUntil([&]() {
                    return suitePtr && suitePtr->latestModalResult() != nullptr &&
                           suitePtr->latestModalResult()->success;
                }, 20000),
                "Analise modal deve concluir antes da exportacao PDF");

    // Inicia fluxo nao modal de exportacao PDF enquanto AnalysisSuiteDialog ainda possui resultados
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onExportPdfReport", Qt::DirectConnection),
                "PdfReportOptionsDialog deve abrir");
    pumpEvents(50);
    auto* pdfOptDlg = findVisibleTopLevel<PdfReportOptionsDialog>();
    TEST_ASSERT(pdfOptDlg != nullptr, "PdfReportOptionsDialog deve estar visivel");

    // Aceitar opcoes captura o snapshot seguro e abre QFileDialog nao modal
    pdfOptDlg->accept();
    pumpEvents(60);

    auto* fileDlg = findVisibleTopLevel<QFileDialog>();
    TEST_ASSERT(fileDlg != nullptr, "QFileDialog nao modal de exportacao PDF deve estar visivel");

    // Fecha e destroi AnalysisSuiteDialog ANTES de selecionar o arquivo de destino ([AUD-005])
    suiteDlg->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    pumpEvents(30);
    TEST_ASSERT(waitUntil([&]() { return suitePtr.isNull(); }, 5000),
                "AnalysisSuiteDialog deve ser completamente destruido antes de concluir salvamento");

    // Conclui o salvamento no QFileDialog (acionando leitura do snapshot modalResult na secao 22 do PDF)
    fileDlg->setDirectory(tmp.path());
    pumpEvents(40);
    if (auto* fnEdit = fileDlg->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
        fnEdit->setText(QDir::toNativeSeparators(outPdfPath));
    }
    fileDlg->selectFile(QFileInfo(outPdfPath).fileName());
    pumpEvents(40);
    if (auto* bb = fileDlg->findChild<QDialogButtonBox*>()) {
        if (auto* acceptBtn = bb->button(QDialogButtonBox::Open)) {
            acceptBtn->click();
        } else if (auto* saveBtn = bb->button(QDialogButtonBox::Save)) {
            saveBtn->click();
        } else {
            static_cast<QDialog*>(fileDlg)->accept();
        }
    } else {
        static_cast<QDialog*>(fileDlg)->accept();
    }
    pumpEvents(80);
    TEST_ASSERT(waitUntil([&]() { return QFile::exists(outPdfPath); }, 10000),
                "Arquivo PDF deve ser gerado apos destruicao de AnalysisSuiteDialog");

    QFile pdfFile(outPdfPath);
    TEST_ASSERT(pdfFile.open(QIODevice::ReadOnly) && pdfFile.size() > 15000 &&
                    pdfFile.read(8).startsWith("%PDF-1."),
                "PDF exportado apos destruicao do AnalysisSuiteDialog deve ser valido (> 15 KB)");

    // Fecha quaisquer caixas de mensagem ou dialogos residuais antes de encerrar MainWindow
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* mb = qobject_cast<QMessageBox*>(w)) {
            mb->close();
        }
    }
    pumpEvents(40);
    mw.close();
    pumpEvents(40);
}

void testNonFiniteRenderingSafety() {
    const double qNaN = std::numeric_limits<double>::quiet_NaN();
    const double pInf = std::numeric_limits<double>::infinity();
    const double nInf = -std::numeric_limits<double>::infinity();

    auto assertFiniteColor = [](const ColorRGB& c, const char* msg) {
        TEST_ASSERT(std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a) &&
                        c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f && c.b >= 0.0f && c.b <= 1.0f,
                    msg);
    };

    for (double bad : {qNaN, pInf, nInf, -1e9, 1e9}) {
        assertFiniteColor(ColorRGB::jet(bad), "ColorRGB::jet com entrada nao finita deve ser finito e em [0,1]");
        assertFiniteColor(ColorRGB::jetThemed(bad, false), "ColorRGB::jetThemed(escuro) deve ser finito e seguro");
        assertFiniteColor(ColorRGB::jetThemed(bad, true), "ColorRGB::jetThemed(claro) deve evitar OOB com NaN ([AUD-009])");
        assertFiniteColor(ColorRGB::viridis(bad), "ColorRGB::viridis com entrada nao finita deve ser finito e em [0,1]");
    }

    Camera3D cam;
    const float initYaw = cam.yaw();
    const float initPitch = cam.pitch();
    const float initDist = cam.distance();
    const float fNaN = std::numeric_limits<float>::quiet_NaN();
    const float fInf = std::numeric_limits<float>::infinity();

    cam.rotate(fNaN, 10.0f);
    cam.rotate(10.0f, fInf);
    cam.rotate(-fInf, -fInf);
    TEST_ASSERT(std::isfinite(cam.yaw()) && cam.yaw() == initYaw && cam.pitch() == initPitch,
                "Camera3D::rotate deve ignorar incrementos nao finitos sem loop/NaN");
    cam.rotate(725.0f, 15.0f);
    TEST_ASSERT(cam.yaw() >= 0.0f && cam.yaw() < 360.0f, "Camera3D::rotate deve normalizar via std::fmod");

    cam.setDistance(fNaN);
    cam.setYaw(fInf);
    cam.setPitch(-fInf);
    cam.pan(fNaN, fInf);
    cam.zoom(fNaN);
    TEST_ASSERT(std::isfinite(cam.distance()) && cam.distance() == initDist &&
                    std::isfinite(cam.yaw()) && std::isfinite(cam.pitch()),
                "Setters/pan/zoom de Camera3D devem rejeitar NaN/Inf");

    cam.fitToBoundingBox({qNaN, 0.0, 0.0}, {1.0, pInf, 2.0});
    const QMatrix4x4 vp = cam.viewProjectionMatrix(fNaN);
    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT(std::isfinite(vp.constData()[i]), "Matriz viewProjection deve permanecer finita");
    }

    Viewport3D vpWidget;
    vpWidget.setGridStep(2.5);
    vpWidget.setGridStep(qNaN);
    vpWidget.setGridStep(pInf);
    TEST_ASSERT(std::abs(vpWidget.gridStep() - 2.5) < 1e-9,
                "Viewport3D::setGridStep deve ignorar NaN/Inf ([AUD-009])");

    vpWidget.setDeformedScale(15.0);
    vpWidget.setDeformedScale(qNaN);
    vpWidget.setDeformedScale(nInf);
    TEST_ASSERT(std::abs(vpWidget.deformedScale() - 15.0) < 1e-9,
                "Viewport3D::setDeformedScale deve ignorar NaN/Inf");

    vpWidget.setAnimationSpeed(2.0);
    vpWidget.setAnimationSpeed(qNaN);
    TEST_ASSERT(std::abs(vpWidget.animationSpeed() - 2.0) < 1e-9,
                "Viewport3D::setAnimationSpeed deve ignorar NaN");

    RenderQualitySettings rqs;
    rqs.resolutionScale = fNaN;
    rqs.lineWidthMultiplier = fInf;
    rqs.sharpenStrength = -fInf;
    const QJsonObject rjson = renderSettingsToJson(rqs);
    const RenderQualitySettings rSanitized = renderSettingsFromJson(rjson);
    TEST_ASSERT(std::isfinite(rjson["resolutionScale"].toDouble()) &&
                    std::isfinite(rSanitized.resolutionScale) &&
                    std::isfinite(rSanitized.lineWidthMultiplier) &&
                    std::isfinite(rSanitized.sharpenStrength),
                "renderSettingsToJson/FromJson deve sanitizar NaN/Inf para valores finitos");
}

void testPhase2AuditHardeningAndConcurrency() {
    // 1) [AUD-018] Sanitizacao CSV/TSV/XLSX contra Formula Injection, XML 1.0 invalido, selecao esparsa e XSS PlainText
    {
        const QString dirtyXmlCell = QStringLiteral("linha\x01\tcom\nquebra");
        QTableWidget table(3, 2);
        const QStringList headers = {QStringLiteral("Expressao"), QStringLiteral("Valor")};
        ui::ResultTableConfigurator cfg(nullptr, nullptr);
        cfg.configureTable(&table, ui::ResultTableKind::Displacements, headers, false);
        table.setRowCount(3);
        table.setItem(0, 0, new QTableWidgetItem(QStringLiteral("=CMD|' /C calc'!A0")));
        table.setItem(0, 1, new QTableWidgetItem(QStringLiteral("-12.50")));
        table.setItem(1, 0, new QTableWidgetItem(dirtyXmlCell));
        table.setItem(1, 1, new QTableWidgetItem(QStringLiteral("@IMPORT")));
        table.setItem(2, 0, new QTableWidgetItem(QStringLiteral("+SUM(A1:A2)")));
        table.setItem(2, 1, new QTableWidgetItem(QStringLiteral("42.0")));

        QTemporaryDir exportDir;
        TEST_ASSERT(exportDir.isValid(), "QTemporaryDir para exportacao [AUD-018]");
        const QString csvPath = exportDir.filePath(QStringLiteral("audit.csv"));
        const QString txtPath = exportDir.filePath(QStringLiteral("audit.txt"));
        const QString xlsxPath = exportDir.filePath(QStringLiteral("audit.xlsx"));
        const QString xmlPath = exportDir.filePath(QStringLiteral("audit.xml"));

        QString err;
        TEST_ASSERT(cfg.exportTableToFile(&table, csvPath, &err), "Exportar CSV sanitizado [AUD-018]");
        TEST_ASSERT(cfg.exportTableToFile(&table, txtPath, &err), "Exportar TXT/TSV sanitizado [AUD-018]");
        TEST_ASSERT(cfg.exportTableToFile(&table, xlsxPath, &err), "Exportar XLSX sanitizado [AUD-018]");
        TEST_ASSERT(cfg.exportTableToFile(&table, xmlPath, &err), "Exportar XML sanitizado [AUD-018]");

        QFile fCsv(csvPath);
        TEST_ASSERT(fCsv.open(QIODevice::ReadOnly), "Ler CSV exportado");
        const QString csvContent = QString::fromUtf8(fCsv.readAll());
        TEST_ASSERT(csvContent.contains(QStringLiteral("'=CMD|' /C calc'!A0")) &&
                        csvContent.contains(QStringLiteral("'+SUM(A1:A2)")) &&
                        csvContent.contains(QStringLiteral("-12.50")) &&
                        csvContent.contains(QStringLiteral("'@IMPORT")) &&
                        !csvContent.contains(QChar(0x01)),
                    "CSV exportado deve neutralizar gatilhos de formula (=, +, @) e preservar -12.50");

        QFile fXml(xmlPath);
        TEST_ASSERT(fXml.open(QIODevice::ReadOnly), "Ler XML exportado");
        const QByteArray xmlBytes = fXml.readAll();
        TEST_ASSERT(!xmlBytes.contains('\x01'),
                    "XML 1.0 exportado nao pode conter bytes de controle invalidos (< 0x20)");

        // Selecao vazia com exportOnlySelectedCells = true deve exportar apenas cabecalho (0 linhas de dados)
        cfg.setExportOnlySelectedCells(&table, true);
        table.clearSelection();
        const QString emptySelCsv = exportDir.filePath(QStringLiteral("empty_sel.csv"));
        TEST_ASSERT(cfg.exportTableToFile(&table, emptySelCsv, &err), "Exportar CSV com selecao vazia");
        QFile fEmpty(emptySelCsv);
        TEST_ASSERT(fEmpty.open(QIODevice::ReadOnly), "Ler empty_sel.csv");
        const QStringList emptyLines = QString::fromUtf8(fEmpty.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        TEST_ASSERT(emptyLines.size() == 1, "Selecao vazia com exportOnlySelectedCells=true nao deve exportar todas as linhas");

        // Selecao nao contigua (canto superior esquerdo (0,0) e canto inferior direito (2,1), sem linha 1)
        table.item(0, 0)->setSelected(true);
        table.item(2, 1)->setSelected(true);
        const QString sparseSelCsv = exportDir.filePath(QStringLiteral("sparse_sel.csv"));
        TEST_ASSERT(cfg.exportTableToFile(&table, sparseSelCsv, &err), "Exportar CSV com selecao esparsa");
        QFile fSparse(sparseSelCsv);
        TEST_ASSERT(fSparse.open(QIODevice::ReadOnly), "Ler sparse_sel.csv");
        const QString sparseContent = QString::fromUtf8(fSparse.readAll());
        TEST_ASSERT(!sparseContent.contains(QStringLiteral("@IMPORT")) &&
                        !sparseContent.contains(QStringLiteral("-12.50")) &&
                        sparseContent.contains(QStringLiteral("42.0")),
                    "Selecao nao contigua deve exportar apenas as celulas efetivamente selecionadas");

        // WindowZOrderManager::showModelessMessage deve usar Qt::PlainText
        QWidget dummyMain;
        WindowZOrderManager zMgr(&dummyMain);
        QMessageBox* msgBox = zMgr.showModelessMessage(
            QMessageBox::Warning,
            QStringLiteral("Teste PlainText"),
            QStringLiteral("<img src=x onerror=alert(1)>"));
        TEST_ASSERT(msgBox != nullptr && msgBox->textFormat() == Qt::PlainText,
                    "WindowZOrderManager::showModelessMessage deve forcar Qt::PlainText contra injecao HTML");
        msgBox->close();
    }

    // 2) [AUD-014] SectionDialog preserva Polygon/Composite e valida limites geometricos
    {
        kernel::PolygonLoop2D hexLoop;
        hexLoop.isHole = false;
        hexLoop.vertices = {
            {0.0, 0.0}, {0.20, 0.0}, {0.25, 0.15}, {0.20, 0.30}, {0.0, 0.30}, {-0.05, 0.15}};
        const CrossSection polySec = CrossSection::makePolygon(77, "Poligono Custom", {hexLoop});
        SectionDialog polyDlg(nullptr, &polySec);
        const CrossSection polyOut = polyDlg.getSection(77);
        TEST_ASSERT(polyOut.shape == SectionShape::Polygon &&
                        polyOut.polygonLoops.size() == 1 &&
                        polyOut.polygonLoops.front().vertices.size() == hexLoop.vertices.size() &&
                        std::abs(polyOut.A - polySec.A) < 1e-9,
                    "SectionDialog deve preservar SectionShape::Polygon e seus vertices/propriedades");

        CrossSection badTube = CrossSection::makeCircularTube(78, "Tubo Invalido", 0.20, 0.15); // t >= D/2 -> clamped
        SectionDialog badTubeDlg(nullptr, &badTube);
        const CrossSection clampedTube = badTubeDlg.getSection(78);
        TEST_ASSERT(badTube.dim2 < badTube.dim1 * 0.5 && clampedTube.dim2 < clampedTube.dim1 * 0.5,
                    "CrossSection::makeCircularTube e SectionDialog::getSection devem impor limite fisico t < D/2");
    }

    // 3) [AUD-014] LoadCombinationsDialog psi2=0 para Sismo em ELS Quase-Permanente,
    //    MeshGeneratorDialog 3D Solid (kN/m3 e engaste somente na base z=z_min em DOFs 0..2),
    //    e MaterialCatalogDialog bloqueando exclusao de material usado pela malha 2D/3D
    {
        MainWindow mw;
        mw.setAttribute(Qt::WA_DontShowOnScreen, true);
        mw.show();
        pumpEvents(20);
        auto* vp = mw.findChild<Viewport3D*>();
        TEST_ASSERT(vp != nullptr && vp->model() != nullptr, "Viewport3D e modelo devem existir na MainWindow");

        // Adiciona caso Sismico e gera combinacoes NBR
        kernel::LoadCase seisCase;
        seisCase.id = 99;
        seisCase.name = "Sismo Horizontal";
        seisCase.category = kernel::LoadCaseCategory::Seismic;
        vp->model()->addLoadCase(seisCase);

        TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenLoadCombinations", Qt::DirectConnection),
                    "Invocar onOpenLoadCombinations");
        pumpEvents(15);
        auto* lcDlg = findVisibleTopLevel<LoadCombinationsDialog>();
        TEST_ASSERT(lcDlg != nullptr, "LoadCombinationsDialog deve abrir");
        for (auto* btn : lcDlg->findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Gerar Combina"), Qt::CaseInsensitive)) {
                btn->click();
                break;
            }
        }
        for (const auto& comb : vp->model()->getLoadCombinations()) {
            if (comb.limitState == kernel::LimitStateType::ELS_QuasiPermanent) {
                for (const auto& term : comb.factors) {
                    if (term.loadCaseId == 99) {
                        TEST_ASSERT(std::abs(term.gamma) <= 1e-12,
                                    "Combinacao ELS Quase-Permanente deve atribuir fator psi2 = 0.0 para Sismo (NBR 8681)");
                    }
                }
            }
        }
        lcDlg->close();

        // MeshGeneratorDialog: malha solida 3D Tet4
        TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenMeshGenerator", Qt::DirectConnection),
                    "Invocar onOpenMeshGenerator");
        pumpEvents(15);
        auto* meshDlg = findVisibleTopLevel<MeshGeneratorDialog>();
        TEST_ASSERT(meshDlg != nullptr, "MeshGeneratorDialog deve abrir");
        auto combos = meshDlg->findChildren<QComboBox*>();
        TEST_ASSERT(!combos.isEmpty(), "MeshGeneratorDialog deve conter QComboBox de tipo");
        QComboBox* typeCombo = combos.first();
        TEST_ASSERT(typeCombo->count() >= 8, "MeshGeneratorDialog deve ter opcoes 3D (Hex8/Hex20/Tet4)");
        typeCombo->setCurrentIndex(5); // 3D Solid Hex8
        pumpEvents(10);

        bool foundKnPerM3 = false;
        for (auto* dspin : meshDlg->findChildren<QDoubleSpinBox*>()) {
            if (dspin->suffix().contains(QStringLiteral("kN/m³"))) {
                foundKnPerM3 = true;
                break;
            }
        }
        TEST_ASSERT(foundKnPerM3, "MeshGeneratorDialog deve exibir sufixo kN/m³ para malha solida 3D");

        for (auto* btn : meshDlg->findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Gerar"), Qt::CaseInsensitive)) {
                btn->click();
                break;
            }
        }
        pumpEvents(20);
        const auto& sm = vp->model()->surfaceMesh();
        TEST_ASSERT(!sm.elements.empty() && !sm.nodes.empty(), "Malha solida 3D Tet4 deve ser gerada");
        double zMin = sm.nodes.front().position.z();
        double zMax = sm.nodes.front().position.z();
        for (const auto& mn : sm.nodes) {
            if (mn.position.z() < zMin) zMin = mn.position.z();
            if (mn.position.z() > zMax) zMax = mn.position.z();
        }
        for (const auto& mn : sm.nodes) {
            if (std::abs(mn.position.z() - zMin) <= 1e-6) {
                TEST_ASSERT(mn.fixed[0] && mn.fixed[1] && mn.fixed[2] &&
                                !mn.fixed[3] && !mn.fixed[4] && !mn.fixed[5],
                            "Preset 1 de solido 3D deve restringir apenas translacoes (0..2) na base z = z_min");
            } else if (std::abs(mn.position.z() - zMax) <= 1e-6) {
                TEST_ASSERT(!mn.fixed[0] && !mn.fixed[1] && !mn.fixed[2],
                            "Preset 1 de solido 3D nao deve bloquear todos os nos do topo z = z_max");
            }
        }
        meshDlg->close();

        // MaterialCatalogDialog: adiciona um material exclusivo da malha 2D/3D e tenta excluir
        StructuralModel* mdl = vp->model();
        Material meshOnlyMat = Material::makeConcreteC30(0);
        meshOnlyMat.name = "Material Exclusivo Malha 3D";
        const MaterialId meshMatId = mdl->addMaterial(meshOnlyMat);
        mdl->surfaceMesh().elements.front().materialId = meshMatId;
        const std::size_t matCountBefore = mdl->getMaterials().size();

        TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onAssignMaterials", Qt::DirectConnection),
                    "Invocar onAssignMaterials");
        pumpEvents(15);
        auto* matDlg = findVisibleTopLevel<MaterialCatalogDialog>();
        TEST_ASSERT(matDlg != nullptr, "MaterialCatalogDialog deve abrir");
        if (auto* lw = matDlg->findChild<QListWidget*>()) {
            for (int i = 0; i < lw->count(); ++i) {
                if (lw->item(i)->data(Qt::UserRole).toInt() == static_cast<int>(meshMatId)) {
                    lw->setCurrentRow(i);
                    break;
                }
            }
        }
        for (auto* btn : matDlg->findChildren<QPushButton*>()) {
            if (btn->text().contains(QStringLiteral("Excluir"), Qt::CaseInsensitive) ||
                btn->text().contains(QStringLiteral("Delete"), Qt::CaseInsensitive)) {
                btn->click();
                break;
            }
        }
        pumpEvents(15);
        TEST_ASSERT(mdl->getMaterials().size() == matCountBefore && mdl->findMaterial(meshMatId) != nullptr,
                    "MaterialCatalogDialog deve impedir exclusao de material referenciado por surfaceMesh().elements");
        matDlg->close();

        // 4) [AUD-015] Excecao lancada em worker thread do AnalysisSuiteDialog nao causa std::terminate,
        //    e fechamento imediato do AnalysisSuiteDialog com job em voo nao causa UAF
        TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenAnalysisSuite", Qt::DirectConnection),
                    "Invocar onOpenAnalysisSuite");
        pumpEvents(15);
        auto* suiteDlg = findVisibleTopLevel<AnalysisSuiteDialog>();
        TEST_ASSERT(suiteDlg != nullptr, "AnalysisSuiteDialog deve abrir para teste [AUD-015]");
        bool caughtFinishedSignal = false;
        bool jobSucceeded = true;
        QObject::connect(suiteDlg, &AnalysisSuiteDialog::asyncJobFinished, suiteDlg,
                         [&](bool ok) {
                             caughtFinishedSignal = true;
                             jobSucceeded = ok;
                         });
        suiteDlg->runCustomAsyncTaskForTest([]() {
            throw std::runtime_error("Falha simulada na thread worker [AUD-015]");
        });
        TEST_ASSERT(waitUntil([&]() { return caughtFinishedSignal && !suiteDlg->isBusy(); }, 5000) && !jobSucceeded,
                    "AnalysisSuiteDialog deve capturar std::exception na worker thread e sinalizar asyncJobFinished(false)");

        // Inicia job assincrono e fecha imediatamente o dialogo enquanto a thread roda
        suiteDlg->runCustomAsyncTaskForTest([]() {
            QThread::msleep(40);
        });
        suiteDlg->close();
        pumpEvents(60);

        // 5) [AUD-006] Iniciar GuidedSessionDialog, avancar ate etapa de analise e fechar dialogos no meio da execucao
        TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onGuidedSession", Qt::DirectConnection),
                    "Invocar onGuidedSession");
        pumpEvents(15);
        QPointer<GuidedSessionDialog> guided = mw.guidedSessionDialog();
        TEST_ASSERT(guided != nullptr, "GuidedSessionDialog deve abrir para teste [AUD-006]");
        guided->startSession(60000);
        // Reentrancia startSession durante execucao deve ser ignorada com seguranca
        guided->startSession(60000);
        TEST_ASSERT(guided->isSessionRunning() && guided->currentStep() == 1,
                    "GuidedSessionDialog deve iniciar no passo 1");
        guided->close();
        pumpEvents(20);
        TEST_ASSERT(guided.isNull() || !guided->isSessionRunning(),
                    "Fechar GuidedSessionDialog deve encerrar e destruir a sessao de forma limpa");

        // 6) [AUD-028] Debounce de saveUserPreferences: 50 eventos rapidos -> 0 escritas imediatas e 1 escrita no flush
        omniframe::io::UserPreferencesStore::resetDiskWriteCount();
        for (int i = 0; i < 50; ++i) {
            mw.saveUserPreferences();
        }
        TEST_ASSERT(omniframe::io::UserPreferencesStore::diskWriteCount() == 0,
                    "50 chamadas rapidas a MainWindow::saveUserPreferences() devem ser agrupadas pelo timer de debounce (0 escritas imediatas)");
        mw.flushUserPreferencesSync();
        TEST_ASSERT(omniframe::io::UserPreferencesStore::diskWriteCount() == 1,
                    "MainWindow::flushUserPreferencesSync() deve consolidar as 50 atualizacoes em exatamente 1 escrita em disco");
    }
}

void testAud015StaleRevisionAndMutualExclusion() {
    MainWindow mw;
    mw.setAttribute(Qt::WA_DontShowOnScreen, true);
    mw.show();
    pumpEvents(20);
    mw.loadExampleBuilding();

    TEST_ASSERT(waitUntil([&]() {
                    return mw.hasValidStaticResultForCurrentRevision() &&
                           !mw.isSolveInProgress() &&
                           !(mw.analysisExecutor() && mw.analysisExecutor()->isBusy());
                }, 15000),
                "Modelo de exemplo deve concluir resolucao inicial antes de [AUD-015]");

    QMetaObject::invokeMethod(&mw, "onOpenAnalysisSuite", Qt::DirectConnection);
    pumpEvents(25);

    QPointer<AnalysisSuiteDialog> suiteDlg;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* dlg = qobject_cast<AnalysisSuiteDialog*>(w)) {
            if (dlg->isVisible()) {
                suiteDlg = dlg;
                break;
            }
        }
    }
    TEST_ASSERT(!suiteDlg.isNull(), "AnalysisSuiteDialog modeless deve abrir para teste [AUD-015]");

    bool finishedFired = false;
    bool lastOk = true;
    const QMetaObject::Connection conn = QObject::connect(
        suiteDlg.data(), &AnalysisSuiteDialog::asyncJobFinished, &mw,
        [&](bool ok) {
            finishedFired = true;
            lastOk = ok;
        });

    // 1) Tarefa longa da suite + solicitacao concorrente do solver estatico + mutacao do modelo (revisao obsoleta)
    suiteDlg->runStaleRevisionCheckTaskForTest([]() { QThread::msleep(150); });
    TEST_ASSERT(mw.analysisExecutor() != nullptr && mw.analysisExecutor()->isBusy(),
                "AsyncAnalysisExecutor compartilhado deve marcar isBusy() durante tarefa da suite");

    QMetaObject::invokeMethod(&mw, "onSolveStructure", Qt::DirectConnection);
    TEST_ASSERT(!mw.isSolveInProgress(),
                "Mutual exclusion: solver estatico nao deve iniciar enquanto executor compartilhado estiver ocupado");

    QMetaObject::invokeMethod(&mw, "onModelModified", Qt::DirectConnection);

    TEST_ASSERT(waitUntil([&]() { return finishedFired; }, 10000),
                "Tarefa em background da suite deve emitir asyncJobFinished");
    QObject::disconnect(conn);
    TEST_ASSERT(!lastOk,
                "Resultado da suite cuja revisao ficou obsoleta durante execucao deve ser descartado (success=false)");

    TEST_ASSERT(waitUntil([&]() {
                    return !mw.isSolveInProgress() &&
                           !(mw.analysisExecutor() && mw.analysisExecutor()->isBusy()) &&
                           mw.hasValidStaticResultForCurrentRevision();
                }, 15000),
                "Calculo estatico enfileirado durante bloqueio mutuo deve executar e validar revisao atual");

    // 2) Com recalculo automatico desligado, mutacao antes da entrega do worker estatico invalida revisao final
    QMetaObject::invokeMethod(&mw, "onAutoRecalculateToggled", Qt::DirectConnection, Q_ARG(bool, false));
    QMetaObject::invokeMethod(&mw, "onSolveStructure", Qt::DirectConnection);
    QMetaObject::invokeMethod(&mw, "onModelModified", Qt::DirectConnection);

    TEST_ASSERT(waitUntil([&]() {
                    return !mw.isSolveInProgress() &&
                           !(mw.analysisExecutor() && mw.analysisExecutor()->isBusy());
                }, 15000),
                "Executor deve ficar ocioso apos cancelamento/descarte de calculo estatico obsoleto");
    TEST_ASSERT(!mw.hasValidStaticResultForCurrentRevision(),
                "Resultado estatico de revisao obsoleta com recalculo automatico desligado nao pode ser validado");
}

void testAud024MainWindowDirectDestruction() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    pumpEvents(30);

    QPointer<QDialog> matDlg;
    QPointer<QDialog> secDlg;
    QPointer<QDialog> comboDlg;
    QPointer<QDialog> meshDlg;
    QPointer<QDialog> suiteDlg;
    QPointer<QDialog> nbrDlg;

    {
        auto* mwDestroy = new MainWindow();
        mwDestroy->setAttribute(Qt::WA_DontShowOnScreen, true);
        mwDestroy->show();
        pumpEvents(20);
        mwDestroy->loadExampleBuilding();
        TEST_ASSERT(waitUntil([&]() {
                        return !mwDestroy->isSolveInProgress() &&
                               !(mwDestroy->analysisExecutor() && mwDestroy->analysisExecutor()->isBusy());
                    }, 15000),
                    "Exemplo deve concluir antes de abrir os 6 dialogos modeless [AUD-024]");

        TEST_ASSERT(QMetaObject::invokeMethod(mwDestroy, "onAssignMaterials", Qt::DirectConnection), "onAssignMaterials");
        TEST_ASSERT(QMetaObject::invokeMethod(mwDestroy, "onAssignSections", Qt::DirectConnection), "onAssignSections");
        TEST_ASSERT(QMetaObject::invokeMethod(mwDestroy, "onOpenLoadCombinations", Qt::DirectConnection), "onOpenLoadCombinations");
        TEST_ASSERT(QMetaObject::invokeMethod(mwDestroy, "onOpenMeshGenerator", Qt::DirectConnection), "onOpenMeshGenerator");
        TEST_ASSERT(QMetaObject::invokeMethod(mwDestroy, "onOpenAnalysisSuite", Qt::DirectConnection), "onOpenAnalysisSuite");
        TEST_ASSERT(QMetaObject::invokeMethod(mwDestroy, "onOpenNbrCompliance", Qt::DirectConnection), "onOpenNbrCompliance");
        pumpEvents(60);

        matDlg = findVisibleTopLevel<MaterialCatalogDialog>();
        secDlg = findVisibleTopLevel<SectionCatalogDialog>();
        comboDlg = findVisibleTopLevel<LoadCombinationsDialog>();
        meshDlg = findVisibleTopLevel<MeshGeneratorDialog>();
        suiteDlg = findVisibleTopLevel<AnalysisSuiteDialog>();
        nbrDlg = findVisibleTopLevel<NbrComplianceDialog>();

        TEST_ASSERT(matDlg && matDlg->isVisible() &&
                        secDlg && secDlg->isVisible() &&
                        comboDlg && comboDlg->isVisible() &&
                        meshDlg && meshDlg->isVisible() &&
                        suiteDlg && suiteDlg->isVisible() &&
                        nbrDlg && nbrDlg->isVisible(),
                    "Todos os 6 dialogos modeless devem estar abertos e visiveis antes da destruicao direta");

        delete mwDestroy;
        mwDestroy = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        pumpEvents(50);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        pumpEvents(20);
    }

    TEST_ASSERT(matDlg.isNull() && secDlg.isNull() && comboDlg.isNull() &&
                    meshDlg.isNull() && suiteDlg.isNull() && nbrDlg.isNull(),
                "Destruicao direta de MainWindow deve fechar e destruir todos os 6 dialogos modeless sem UAF/vazamento");

    bool anyVisibleDialogOrMain = false;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w && w->isVisible() && (qobject_cast<QDialog*>(w) != nullptr || qobject_cast<MainWindow*>(w) != nullptr)) {
            anyVisibleDialogOrMain = true;
        }
    }
    TEST_ASSERT(!anyVisibleDialogOrMain,
                "Nenhum QDialog ou MainWindow visivel pode restar apos destruicao direta da MainWindow");
}

} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    QApplication app(argc, argv);

    // Garante contrato de caminho real AppData/Local/OmniFrame/user_preferences.json antes do isolamento dos testes
    {
        const QString realPath = QDir::fromNativeSeparators(omniframe::MainWindow::userPreferencesFilePath());
        TEST_ASSERT(realPath.endsWith(QStringLiteral("/OmniFrame/user_preferences.json")) &&
                        !realPath.endsWith(QStringLiteral("/OmniFrame/OmniFrame/user_preferences.json")),
                    "Caminho padrão real de preferências deve apontar para AppData/Local/OmniFrame/user_preferences.json");
    }

    // Isola todos os testes em diretório temporário ".../OmniFrame" para nunca corromper user_preferences.json do usuário em caso de falha
    QTemporaryDir isolatedPrefsRoot;
    TEST_ASSERT(isolatedPrefsRoot.isValid(), "QTemporaryDir para isolamento de preferências inválido");
    const QString isolatedOmniFrameDir = isolatedPrefsRoot.filePath(QStringLiteral("OmniFrame"));
    QDir().mkpath(isolatedOmniFrameDir);
    omniframe::io::UserPreferencesStore::ScopedDirectoryOverride testPrefsIsolation(isolatedOmniFrameDir);

    const QString userPrefsPath = omniframe::MainWindow::userPreferencesFilePath();
    const QStringList legacyPaths = omniframe::io::UserPreferencesStore::legacyPreferenceFilePaths();
    const QString legacyNestedDir = omniframe::io::UserPreferencesStore::legacyNestedPreferencesDirectory();

    struct FileBackup {
        QString path;
        bool existed = false;
        QByteArray bytes;
    };
    std::vector<FileBackup> backups;
    auto captureBackup = [&](const QString& path) {
        FileBackup b;
        b.path = path;
        b.existed = QFile::exists(path);
        if (b.existed) {
            QFile in(path);
            if (in.open(QIODevice::ReadOnly)) {
                b.bytes = in.readAll();
            }
        }
        backups.push_back(std::move(b));
    };
    captureBackup(userPrefsPath);
    captureBackup(omniframe::io::ProjectRecoveryStore::recoveryFilePath());
    captureBackup(omniframe::io::ProjectRecoveryStore::recoveryMetaPath());
    for (const QString& lp : legacyPaths) {
        captureBackup(lp);
    }
    captureBackup(QDir(legacyNestedDir).filePath(QStringLiteral("user_preferences.json")));
    for (const QString& name : omniframe::io::UserPreferencesStore::legacyPreferenceFileNames()) {
        captureBackup(QDir(legacyNestedDir).filePath(name));
    }

    auto cleanTestPrefs = [&]() {
        omniframe::io::AppLogger::shutdown();
        QFile::remove(userPrefsPath);
        QFile::remove(userPrefsPath + QStringLiteral(".corrupt.bak"));
        omniframe::io::ProjectRecoveryStore::clearSnapshot();
        {
            const QDir isolatedLogDir(omniframe::io::AppLogger::logDirectory());
            if (isolatedLogDir.exists()) {
                for (const QString& logPath : omniframe::io::AppLogger::existingLogFiles()) {
                    QFile::remove(logPath);
                }
            }
        }
        for (const QString& lp : legacyPaths) {
            QFile::remove(lp);
        }
        if (QFileInfo::exists(legacyNestedDir)) {
            QFile::remove(QDir(legacyNestedDir).filePath(QStringLiteral("user_preferences.json")));
            QFile::remove(QDir(legacyNestedDir).filePath(QStringLiteral("user_preferences.json.corrupt.bak")));
            for (const QString& name : omniframe::io::UserPreferencesStore::legacyPreferenceFileNames()) {
                QFile::remove(QDir(legacyNestedDir).filePath(name));
            }
            QDir(omniframe::io::UserPreferencesStore::preferencesDirectory()).rmdir(QStringLiteral("OmniFrame"));
        }
    };

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 1/11 Janelas UI (1-clique), resize e Z-Order...\n";
    testSingleClickDialogsAndZOrder();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 2/11 Malha 2D/3D e BoundingBox...\n";
    testMeshBoundingBoxAndUiEnablement();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 3/11 Casos & Combinacoes NBR e preservacao de selecao...\n";
    testLoadCombinationsAutoCaptureAndSelection();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 4/11 AnalysisSuiteDialog (Combos/Modal/Flamb/Harm/Espec/Sism/NL)...\n";
    testAnalysisSuiteEndToEnd();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 5/11 PostProcess, Auditoria ABNT NBR, DCR e PDF...\n";
    testPostProcessNbrAndPdfWorkflow();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 6/11 Catalogos dos 3 Exemplos, Comboboxes do Inspetor e Reedicao Completa...\n";
    testExamplesCatalogComboAndReeditingWorkflow();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 7/11 Sessao Guiada (Ajuda > Sessao Guiada) nos 3 Exemplos e Traducao Dinamica...\n";
    testGuidedSessionWalkthroughAndTranslations();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 8/11 Renderizacao de setas QComboBox/QSpinBox sem spin_*.png...\n";
    testThemedComboAndSpinArrowsRenderWithoutPngAssets();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 9/11 Paineis dock recolhiveis com seta estilo spin button...\n";
    testCollapsibleDocksWithSpinArrowButtons();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 10/11 Configuracao Linceu nas 4 tabelas de resultados (selecao, ordenacao, quebra, cores, exportacao)...\n";
    testResultTablesLinceuConfigAndFeatures();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 11/12 Interface do Usuario dupla (Nativo Qt6 <-> Customizado), persistencia e traducao dinamica...\n";
    testDualUiModeNativeAndCustomSwitchingAndPersistence();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 12/13 Startup vazio e preservacao integral de edicoes contra fechamento inadvertido...\n";
    testEmptyStartupAndInadvertentCloseProjectPreservation();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 13/14 Politicas configuraveis de inicializacao e sistema de log diario...\n";
    testStartupPolicyConfigurationAndLogsIntegration();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 14/15 Molduras e pontinhos de docks (Nativo) e setas unificadas das toolbars...\n";
    testToolbarOverflowExtensionArrowsAllModes();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 15/16 Atalho Ctrl+Q em Arquivo > Sair sem duplicacao em idiomas/UIs/temas...\n";
    testFileExitShortcutAndNoDuplicatesAllModes();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 16/17 Exportacao PDF nao modal sobrevive a destruicao do AnalysisSuiteDialog ([AUD-005])...\n";
    testModelessPdfExportSurvivesAnalysisSuiteDestruction();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 17/18 Protecao contra NaN/Inf e OOB em renderizacao ([AUD-009])...\n";
    testNonFiniteRenderingSafety();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 18/20 Concorrencia Qt6, sanitizacao CSV/XLSX, debounce e integridade UI ([AUD-006/014/015/018/028])...\n";
    testPhase2AuditHardeningAndConcurrency();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 19/20 Cancelamento por revisao obsoleta e exclusao mutua solver/suite ([AUD-015])...\n";
    testAud015StaleRevisionAndMutualExclusion();

    cleanTestPrefs();
    std::cout << "[Test Phase 9] 20/20 Destruicao direta de MainWindow com 6 dialogos modeless abertos ([AUD-024])...\n";
    testAud024MainWindowDirectDestruction();

    cleanTestPrefs();
    bool restoredAny = false;
    for (const FileBackup& b : backups) {
        if (b.existed) {
            QDir().mkpath(QFileInfo(b.path).absolutePath());
            QFile out(b.path);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.write(b.bytes);
                restoredAny = true;
            }
        }
    }
    if (!restoredAny && !QFile::exists(userPrefsPath)) {
        // Nenhuma acao extra necessaria no diretorio temporario isolado
    }

    std::cout << "[OK] Todos os testes de UI e workflows passaram com sucesso!\n";
    return 0;
}
