#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/PdfReportGenerator.hpp"
#include "locale/GerenciadorTraducao.hpp"
#include "render/Viewport3D.hpp"
#include "solver/FemSolver3D.hpp"
#include "ui/Dialogs.hpp"
#include "ui/EngineeringDialogs.hpp"
#include "ui/GuidedSessionDialog.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ThemeManager.hpp"
#include "ui/WindowZOrderManager.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QToolTip>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cmath>
#include <iostream>
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
    LoadCombinationsDialog dlg(nullptr, &model, [&]() {
        ++modelUpdateCalls;
        dlg.refreshLists(); // Simula MainWindow::onModelModified()
    });
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

    // Garante estado NÃO resolvido (loadExampleBuilding() no startup dispara F5 automático)
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

    // Clicar em Plotar Mapa DCR no Viewport 3D deve resolver sincronamente e ligar Mapa DCR
    auto* btnPlotDcr = findButtonContaining(nbrDlg, QStringLiteral("DCR"));
    TEST_ASSERT(btnPlotDcr != nullptr, "Botão de plotar Mapa DCR deve existir");
    btnPlotDcr->click();
    pumpEvents(60);

    auto* vp = mw.findChild<Viewport3D*>();
    TEST_ASSERT(vp != nullptr, "Viewport3D deve existir em MainWindow");
    TEST_ASSERT(vp->postContourMode() == Viewport3D::PostContourMode::NbrComplianceDcr,
                "Botão de Mapa DCR deve resolver e ativar NbrComplianceDcr no Viewport3D");

    // Abrir Controles de Pós-Processamento e validar sincronização + troca para Displacement e retorno para DCR
    TEST_ASSERT(QMetaObject::invokeMethod(&mw, "onOpenPostControls", Qt::DirectConnection), "Abrir PostProcessControlDialog");
    pumpEvents(60);
    auto* postDlg = findVisibleTopLevel<PostProcessControlDialog>();
    TEST_ASSERT(postDlg != nullptr, "PostProcessControlDialog deve abrir");

    QComboBox* contourCombo = nullptr;
    QComboBox* clipCombo = nullptr;
    for (auto* cb : postDlg->findChildren<QComboBox*>()) {
        if (cb->count() == 3 && cb->findData(static_cast<int>(Viewport3D::PostContourMode::NbrComplianceDcr)) >= 0) {
            contourCombo = cb;
        } else if (cb->count() == 4 && cb->findData(static_cast<int>(Viewport3D::ClipAxis::Z)) >= 0) {
            clipCombo = cb;
        }
    }
    TEST_ASSERT(contourCombo != nullptr && clipCombo != nullptr,
                "PostProcessControlDialog deve conter combos de contorno e plano de corte");
    TEST_ASSERT(contourCombo->currentData().toInt() == static_cast<int>(Viewport3D::PostContourMode::NbrComplianceDcr),
                "Combo de contorno deve refletir modo NbrComplianceDcr ativo");

    contourCombo->setCurrentIndex(1); // Displacement
    pumpEvents(30);
    TEST_ASSERT(vp->postContourMode() == Viewport3D::PostContourMode::Displacement,
                "Contorno Displacement deve ser aplicado ao Viewport3D");

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
        (mw.*(ex.loader))();
        waitSolved();
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

    // Adaptação dinâmica de tema antes da execução (Claro <-> Escuro)
    const UiTheme originalTheme = ThemeManager::instance().currentTheme();
    ThemeManager::instance().setTheme(UiTheme::Light);
    pumpEvents(25);
    TEST_ASSERT(targetBanner->styleSheet().contains(QStringLiteral("#F1F5F9"), Qt::CaseInsensitive),
                "Banner da Sessão Guiada deve adaptar cores ao Tema Claro em tempo real");
    ThemeManager::instance().setTheme(UiTheme::Dark);
    pumpEvents(25);
    TEST_ASSERT(targetBanner->styleSheet().contains(QStringLiteral("#232A36"), Qt::CaseInsensitive),
                "Banner da Sessão Guiada deve adaptar cores ao Tema Escuro em tempo real");

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
    TEST_ASSERT(targetBanner->text().contains(QStringLiteral("Status:")) &&
                    targetBanner->text().contains(QStringLiteral("Control in Focus:")) &&
                    targetBanner->text().contains(QStringLiteral("How it works:")),
                "Cartão moderno inicial deve traduzir todos os identificadores unificados para inglês");

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

            // Verifica adaptação de tema ao vivo durante execução (etapa 14 no Exemplo 1)
            if (exNumber == 1 && step == 14) {
                ThemeManager::instance().setTheme(UiTheme::Light);
                pumpEvents(25);
                TEST_ASSERT(targetBanner->styleSheet().contains(QStringLiteral("#FFFBEB"), Qt::CaseInsensitive) &&
                                dlg->isTargetHighlightVisible(),
                            "Tema Claro ao vivo deve atualizar banner e manter destaque visual ativo");
                ThemeManager::instance().setTheme(UiTheme::Dark);
                pumpEvents(25);
                TEST_ASSERT(targetBanner->styleSheet().contains(QStringLiteral("#2A2112"), Qt::CaseInsensitive) &&
                                dlg->isTargetHighlightVisible(),
                            "Tema Escuro ao vivo deve atualizar banner e manter destaque visual ativo");
            }

            // Verifica que elevar MainWindow manualmente não tem seu Z-Order roubado pelo avanço da Sessão Guiada
            if (exNumber == 1 && step == 6) {
                zMgr->bringToFront(&mw, true);
                pumpEvents(20);
                TEST_ASSERT(zMgr->topWindow() == &mw, "MainWindow deve permanecer no topo do Z-Order");
            }

            // Avança da Fase 1 (Tooltip) -> Fase 2 (Execução simulada da etapa)
            dlg->advancePhaseNow();
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
            } else if (step == 25 || step == 26 || step == 27) {
                TEST_ASSERT(findVisibleTopLevel<AnalysisSuiteDialog>() != nullptr,
                            "Etapas 25-27 devem reutilizar continuamente a Suíte de Análises sem fechar/reabrir janelas fantasmas");
            } else if (step == 28) {
                TEST_ASSERT(findVisibleTopLevel<AnalysisSuiteDialog>() == nullptr &&
                                findVisibleTopLevel<PostProcessControlDialog>() != nullptr,
                            "Etapa 28 deve ocultar Suíte e manter Pós-Processamento 3D aberto");
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
                        vp->clipAxis() == Viewport3D::ClipAxis::None,
                    "Após concluir, contornos DCR, malha e plano de corte devem ser restaurados ao estado inicial no Viewport 3D");
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

    ThemeManager::instance().setTheme(originalTheme);
    TEST_ASSERT(i18n.definir_idioma(QStringLiteral("pt_BR")), "Restaura pt_BR ao finalizar teste");
    dlg->close();
    mw.close();
    pumpEvents(60);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    std::cout << "[Test Phase 9] 1/7 Janelas UI (1-clique), resize e Z-Order...\n";
    testSingleClickDialogsAndZOrder();

    std::cout << "[Test Phase 9] 2/7 Malha 2D/3D e BoundingBox...\n";
    testMeshBoundingBoxAndUiEnablement();

    std::cout << "[Test Phase 9] 3/7 Casos & Combinacoes NBR e preservacao de selecao...\n";
    testLoadCombinationsAutoCaptureAndSelection();

    std::cout << "[Test Phase 9] 4/7 AnalysisSuiteDialog (Combos/Modal/Flamb/Harm/Espec/Sism/NL)...\n";
    testAnalysisSuiteEndToEnd();

    std::cout << "[Test Phase 9] 5/7 PostProcess, Auditoria ABNT NBR, DCR e PDF...\n";
    testPostProcessNbrAndPdfWorkflow();

    std::cout << "[Test Phase 9] 6/7 Catalogos dos 3 Exemplos, Comboboxes do Inspetor e Reedicao Completa...\n";
    testExamplesCatalogComboAndReeditingWorkflow();

    std::cout << "[Test Phase 9] 7/7 Sessao Guiada (Ajuda > Sessao Guiada) nos 3 Exemplos e Traducao Dinamica...\n";
    testGuidedSessionWalkthroughAndTranslations();

    std::cout << "[OK] Todos os testes de UI e workflows passaram com sucesso!\n";
    return 0;
}
