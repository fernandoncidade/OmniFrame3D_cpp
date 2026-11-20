#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/AppLogger.hpp"
#include "io/ProjectSerializer.hpp"
#include "io/UserPreferencesStore.hpp"
#include "render/Camera3D.hpp"
#include "render/Viewport3D.hpp"
#include "solver/FemSolver3D.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QWidget>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

#include <chrono>
#include <climits>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace omniframe;

namespace {

void pumpEventsMs(int totalMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < totalMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

void buildLargeFrameAndShellModel(StructuralModel& model,
                                  int targetBars,
                                  int shellNx,
                                  int shellNy,
                                  bool addDiaphragm = false) {
    model.clearAll();
    model.populateStandardCatalog();

    StructuralModel::ScopedUndoSuppression batch(model);

    // Malha estrutural 3D com colunas e vigas horizontais até atingir targetBars
    const int bayNx = 26;
    const int bayNy = 20;
    const double dx = 2.0;
    const double dy = 2.0;
    const double hz = 3.0;

    std::vector<std::vector<NodeId>> ground(static_cast<std::size_t>(bayNx),
                                            std::vector<NodeId>(static_cast<std::size_t>(bayNy), INVALID_ID));
    std::vector<std::vector<NodeId>> roof(static_cast<std::size_t>(bayNx),
                                          std::vector<NodeId>(static_cast<std::size_t>(bayNy), INVALID_ID));

    for (int ix = 0; ix < bayNx; ++ix) {
        for (int iy = 0; iy < bayNy; ++iy) {
            const double x = static_cast<double>(ix) * dx;
            const double y = static_cast<double>(iy) * dy;
            const NodeId gid = model.addNode(x, y, 0.0);
            if (Node* gn = model.findNode(gid)) {
                gn->support.fixed = {true, true, true, true, true, true};
            }
            const NodeId rid = model.addNode(x, y, hz);
            ground[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy)] = gid;
            roof[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy)] = rid;
        }
    }

    int barCount = 0;
    auto pushBar = [&](NodeId a, NodeId b) {
        if (barCount >= targetBars) return;
        model.addElement(a, b, 1, 1);
        ++barCount;
    };

    // Pilares verticais
    for (int ix = 0; ix < bayNx && barCount < targetBars; ++ix) {
        for (int iy = 0; iy < bayNy && barCount < targetBars; ++iy) {
            pushBar(ground[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy)],
                    roof[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy)]);
        }
    }
    // Vigas na direcao X
    for (int iy = 0; iy < bayNy && barCount < targetBars; ++iy) {
        for (int ix = 0; ix + 1 < bayNx && barCount < targetBars; ++ix) {
            pushBar(roof[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy)],
                    roof[static_cast<std::size_t>(ix + 1)][static_cast<std::size_t>(iy)]);
        }
    }
    // Vigas na direcao Y
    for (int ix = 0; ix < bayNx && barCount < targetBars; ++ix) {
        for (int iy = 0; iy + 1 < bayNy && barCount < targetBars; ++iy) {
            pushBar(roof[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy)],
                    roof[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy + 1)]);
        }
    }
    // Contraventamentos diagonais de cobertura até completar exatamente targetBars
    for (int ix = 0; ix + 1 < bayNx && barCount < targetBars; ++ix) {
        for (int iy = 0; iy + 1 < bayNy && barCount < targetBars; ++iy) {
            pushBar(roof[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy)],
                    roof[static_cast<std::size_t>(ix + 1)][static_cast<std::size_t>(iy + 1)]);
        }
    }
    for (int ix = 0; ix + 1 < bayNx && barCount < targetBars; ++ix) {
        for (int iy = 0; iy + 1 < bayNy && barCount < targetBars; ++iy) {
            pushBar(roof[static_cast<std::size_t>(ix + 1)][static_cast<std::size_t>(iy)],
                    roof[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy + 1)]);
        }
    }

    if (addDiaphragm) {
        RigidDiaphragmSpec diag;
        diag.normalAxis = 2;
        diag.masterNode = roof[0][0];
        for (int ix = 0; ix < 6; ++ix) {
            for (int iy = 0; iy < 6; ++iy) {
                const NodeId nid = roof[static_cast<std::size_t>(ix)][static_cast<std::size_t>(iy)];
                if (nid != diag.masterNode) {
                    diag.slaveNodes.push_back(nid);
                }
            }
        }
        model.getRigidDiaphragms().push_back(std::move(diag));
    }

    // Malha de cascas Quad4 conectada no nível da cobertura (z = hz)
    auto& mesh = model.surfaceMesh();
    const int nPtsX = shellNx + 1;
    const int nPtsY = shellNy + 1;
    mesh.nodes.reserve(static_cast<std::size_t>(nPtsX) * static_cast<std::size_t>(nPtsY));
    mesh.elements.reserve(static_cast<std::size_t>(shellNx) * static_cast<std::size_t>(shellNy));

    const double sxStep = 0.5;
    const double syStep = 0.5;
    for (int iy = 0; iy < nPtsY; ++iy) {
        for (int ix = 0; ix < nPtsX; ++ix) {
            kernel::MeshNode mn;
            mn.id = static_cast<NodeId>(iy * nPtsX + ix + 1);
            mn.position = kernel::Vec3(static_cast<double>(ix) * sxStep,
                                       static_cast<double>(iy) * syStep,
                                       hz);
            mn.isBoundary = (ix == 0 || iy == 0 || ix == shellNx || iy == shellNy);
            if (!addDiaphragm) {
                if (ix == 0 && iy == 0) {
                    mn.fixed = {true, true, true, true, true, true};
                } else if (ix == 0 || iy == 0) {
                    mn.fixed = {true, true, true, false, false, false};
                }
            }
            mesh.nodes.push_back(mn);
        }
    }

    ElementId shellElemId = 1;
    for (int iy = 0; iy < shellNy; ++iy) {
        for (int ix = 0; ix < shellNx; ++ix) {
            const NodeId n00 = static_cast<NodeId>(iy * nPtsX + ix + 1);
            const NodeId n10 = static_cast<NodeId>(iy * nPtsX + (ix + 1) + 1);
            const NodeId n11 = static_cast<NodeId>((iy + 1) * nPtsX + (ix + 1) + 1);
            const NodeId n01 = static_cast<NodeId>((iy + 1) * nPtsX + ix + 1);
            kernel::MeshElement me;
            me.id = shellElemId++;
            me.type = kernel::MeshCellType::Quad4;
            me.nodeIds = {n00, n10, n11, n01};
            me.materialId = 1;
            me.thickness = 0.15;
            mesh.elements.push_back(std::move(me));
        }
    }
}

void testCamera3DProjectionAndRoll() {
    Camera3D cam;
    cam.setViewFrontalXZ(); // eye em (0, -dist, 0) olhando para (0, 0, 0)
    cam.setDistance(20.0f);

    // 1) Ponto à frente vs ponto atrás da câmera em PERSPECTIVA
    cam.setOrthographic(false);
    const QPoint frontPersp = cam.worldToScreen(QVector3D(0.0f, 0.0f, 0.0f), 800, 600);
    const QPoint behindPersp = cam.worldToScreen(QVector3D(0.0f, -25.0f, 0.0f), 800, 600);
    TEST_ASSERT(frontPersp.x() > 350 && frontPersp.x() < 450 && frontPersp.y() > 250 && frontPersp.y() < 350,
                "Camera3D perspectiva deve projetar alvo no centro da tela");
    TEST_ASSERT(behindPersp == QPoint(-1, -1),
                "Camera3D::worldToScreen deve rejeitar ponto atras da camera em perspectiva");

    // 2) Ponto à frente vs ponto atrás da câmera em ORTOGRÁFICA ([AUD-027])
    cam.setOrthographic(true);
    const QPoint frontOrtho = cam.worldToScreen(QVector3D(0.0f, 0.0f, 0.0f), 800, 600);
    const QPoint behindOrtho = cam.worldToScreen(QVector3D(0.0f, -25.0f, 0.0f), 800, 600);
    TEST_ASSERT(frontOrtho.x() > 350 && frontOrtho.x() < 450 && frontOrtho.y() > 250 && frontOrtho.y() < 350,
                "Camera3D ortografica deve projetar alvo no centro da tela");
    TEST_ASSERT(behindOrtho == QPoint(-1, -1),
                "Camera3D::worldToScreen deve rejeitar ponto atras da camera tambem na projecao ortografica");

    // 3) Sem salto de roll ao cruzar |pitch| = 89 graus
    cam.setOrthographic(false);
    cam.setYaw(45.0f);
    cam.setPitch(88.9f);
    const QMatrix4x4 vBelow = cam.viewMatrix();
    cam.setPitch(89.1f);
    const QMatrix4x4 vAbove = cam.viewMatrix();
    const QVector3D rightBelow(vBelow(0, 0), vBelow(0, 1), vBelow(0, 2));
    const QVector3D rightAbove(vAbove(0, 0), vAbove(0, 1), vAbove(0, 2));
    const QVector3D upBelow(vBelow(1, 0), vBelow(1, 1), vBelow(1, 2));
    const QVector3D upAbove(vAbove(1, 0), vAbove(1, 1), vAbove(1, 2));
    TEST_ASSERT(QVector3D::dotProduct(rightBelow, rightAbove) > 0.99f,
                "Eixo X da camera nao deve sofrer salto/inversao de roll ao cruzar pitch=89 graus");
    TEST_ASSERT(QVector3D::dotProduct(upBelow, upAbove) > 0.99f,
                "Eixo Y da camera nao deve sofrer salto/inversao de roll ao cruzar pitch=89 graus");

    // 4) Escalamento dinamico de m_farPlane em fitToBoundingBox()
    cam.fitToBoundingBox(Vec3{-1000.0, -1000.0, 0.0}, Vec3{1000.0, 1000.0, 100.0});
    TEST_ASSERT(cam.farPlane() > 5000.0f,
                "fitToBoundingBox em estrutura de 2000 m deve ampliar m_farPlane dinamicamente");
    QPointF cornerProj;
    float cornerNdcZ = 2.0f;
    TEST_ASSERT(cam.projectWorldToScreenF(QVector3D(1000.0f, 1000.0f, 100.0f), 800, 600, cornerProj, &cornerNdcZ),
                "Canto distante de estrutura 2000 m nao deve ser cortado pelo farPlane");
}

void testDcrLegendStopsAlignment() {
    const auto stops12 = Viewport3D::computeDcrLegendStops(1.20);
    TEST_ASSERT(stops12.size() == 6, "Legenda DCR(1.20) deve conter 6 paradas exatas (0, 0.5, 0.85, 1.0, jump, max)");

    auto hasStopNear = [](const std::vector<Viewport3D::LegendGradientStop>& stops, double targetBottomFrac) -> bool {
        for (const auto& s : stops) {
            if (std::abs(s.normalizedFromBottom - targetBottomFrac) < 1e-6 &&
                std::abs((1.0 - s.topDownPos) - s.normalizedFromBottom) < 1e-9) {
                return true;
            }
        }
        return false;
    };

    TEST_ASSERT(hasStopNear(stops12, 0.50 / 1.20), "Parada 0.50/maxDcr deve corresponder exatamente a 0.50/1.20");
    TEST_ASSERT(hasStopNear(stops12, 0.85 / 1.20), "Parada 0.85/maxDcr deve corresponder exatamente a 0.85/1.20");
    TEST_ASSERT(hasStopNear(stops12, 1.00 / 1.20), "Parada 1.00/maxDcr deve corresponder exatamente a 1.00/1.20");

    const auto stops20 = Viewport3D::computeDcrLegendStops(2.00);
    TEST_ASSERT(hasStopNear(stops20, 0.50 / 2.00), "Parada 0.50/2.00 deve ser 0.25");
    TEST_ASSERT(hasStopNear(stops20, 0.85 / 2.00), "Parada 0.85/2.00 deve ser 0.425");
    TEST_ASSERT(hasStopNear(stops20, 1.00 / 2.00), "Parada 1.00/2.00 deve ser 0.50");
    TEST_ASSERT(stops20.back().color.red() > 230 && stops20.back().color.green() < 60,
                "Topo da legenda DCR > 1.00 deve permanecer vermelho alerta");
}

void testMeshOnlyAnimationTimer() {
    StructuralModel meshOnly;
    meshOnly.clearAll();
    meshOnly.populateStandardCatalog();

    auto& mesh = meshOnly.surfaceMesh();
    for (int i = 0; i < 4; ++i) {
        kernel::MeshNode mn;
        mn.id = static_cast<NodeId>(i + 1);
        mn.position = kernel::Vec3((i == 1 || i == 2) ? 2.0 : 0.0, (i >= 2) ? 2.0 : 0.0, 0.0);
        mesh.nodes.push_back(mn);
    }
    kernel::MeshElement me;
    me.id = 1;
    me.type = kernel::MeshCellType::Quad4;
    me.nodeIds = {1, 2, 3, 4};
    mesh.elements.push_back(me);
    TEST_ASSERT(meshOnly.getElements().empty(), "Pre-condicao: modelo puramente 2D sem barras");

    Viewport3D vp;
    vp.resize(640, 480);
    vp.setModel(&meshOnly);

    // 1) Passos nao lineares somente-malha
    std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>> nlMeshFrames;
    for (int step = 0; step < 3; ++step) {
        std::unordered_map<NodeId, Vec3> disp;
        disp[3] = Vec3{0.0, 0.0, -0.01 * (step + 1)};
        nlMeshFrames.emplace_back(0.25 * (step + 1), std::move(disp));
    }
    vp.setNonlinearMeshStepFrames(nlMeshFrames);
    TEST_ASSERT(vp.hasPostAnimation() && vp.nonlinearStepCount() == 3,
                "Viewport3D deve reconhecer animacao nao linear em modelo somente-malha");

    int frameSignals = 0;
    QObject::connect(&vp, &Viewport3D::animationFrameChanged, &vp, [&](int, int, double) {
        ++frameSignals;
    });

    vp.setAnimationSpeed(5.0);
    vp.setAnimationPlaying(true);
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (frameSignals < 2 && waitTimer.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    vp.setAnimationPlaying(false);
    TEST_ASSERT(frameSignals >= 2,
                "Viewport3D::timerEvent deve avancar animacao nao linear em modelo somente-malha");

    // 2) Animacao modal somente-malha
    vp.clearPostAnimation();
    std::unordered_map<NodeId, Vec3> modalMesh;
    modalMesh[3] = Vec3{0.0, 0.0, -0.05};
    vp.setPostAnimationField(QStringLiteral("Modo 1 Malha"), {}, -1.0, true);
    vp.setPostMeshAnimationField(modalMesh);
    TEST_ASSERT(vp.hasPostAnimation(), "Viewport3D deve reconhecer animacao modal somente-malha");

    int modalSignals = 0;
    QObject::connect(&vp, &Viewport3D::animationFrameChanged, &vp, [&](int, int, double) {
        ++modalSignals;
    });
    vp.setAnimationPlaying(true);
    waitTimer.restart();
    while (modalSignals < 2 && waitTimer.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    vp.setAnimationPlaying(false);
    TEST_ASSERT(modalSignals >= 2,
                "Viewport3D::timerEvent deve avancar animacao modal em modelo somente-malha");
}

void testPickingAndGuards() {
    StructuralModel mdl;
    mdl.clearAll();
    mdl.populateStandardCatalog();

    // Barra parcialmente fora da borda esquerda e nós em profundidades distintas
    const NodeId nLeftOut = mdl.addNode(-80.0, 0.0, 0.0);
    const NodeId nMid = mdl.addNode(0.5, 0.0, 0.0);
    const ElementId eSpan = mdl.addElement(nLeftOut, nMid, 1, 1);

    // Par de nós alinhados ao eixo Y (adiciona nBack antes para validar desempate real por Z)
    const NodeId nBack = mdl.addNode(0.0, +5.0, 3.0);
    const NodeId nFront = mdl.addNode(0.0, -5.0, 3.0);

    // Par de barras paralelas em profundidades distintas na altura Z = 1.5
    const NodeId nF1 = mdl.addNode(-1.0, -5.0, 1.5);
    const NodeId nF2 = mdl.addNode(+1.0, -5.0, 1.5);
    const NodeId nB1 = mdl.addNode(-1.0, +5.0, 1.5);
    const NodeId nB2 = mdl.addNode(+1.0, +5.0, 1.5);
    const ElementId eBack = mdl.addElement(nB1, nB2, 1, 1);
    const ElementId eFront = mdl.addElement(nF1, nF2, 1, 1);

    Viewport3D vp;
    vp.resize(800, 600);
    vp.setModel(&mdl);
    vp.camera().setOrthographic(true);
    vp.camera().setViewFrontalXZ();
    vp.camera().setTarget(QVector3D(0.0f, 0.0f, 1.5f));
    vp.camera().setDistance(20.0f);

    // Verifica nLeftOut realmente fora da borda esquerda da tela
    QPointF leftScreen;
    const bool leftProjected = vp.camera().projectWorldToScreenF(QVector3D(-80.0f, 0.0f, 0.0f), 800, 600, leftScreen, nullptr);
    TEST_ASSERT(leftProjected && leftScreen.x() < 0.0,
                "Pre-condicao: no esquerdo da barra deve estar fora da borda esquerda da tela");

    const QPoint pClickBar = vp.camera().worldToScreen(QVector3D(0.0f, 0.0f, 0.0f), 800, 600);
    TEST_ASSERT(pClickBar.x() > 0 && pClickBar.y() > 0, "Centro da porcao visivel da barra deve projetar na tela");
    TEST_ASSERT(vp.pickElement(pClickBar) == eSpan,
                "pickElement deve capturar barra cujo no inicial esta fora da borda esquerda");

    // Desempate por Z: nFront vs nBack (em ordem de iteracao nBack e eBack nao devem vencer nFront/eFront)
    const QPoint pNodeOverlap = vp.camera().worldToScreen(QVector3D(0.0f, -5.0f, 3.0f), 800, 600);
    TEST_ASSERT(vp.pickNode(pNodeOverlap) == nFront && vp.pickNode(pNodeOverlap) != nBack,
                "pickNode deve desempatar nos alinhados em profundidade a favor do mais proximo da camera");

    const QPoint pElemOverlap = vp.camera().worldToScreen(QVector3D(0.0f, -5.0f, 1.5f), 800, 600);
    TEST_ASSERT(vp.pickElement(pElemOverlap) == eFront && vp.pickElement(pElemOverlap) != eBack,
                "pickElement deve desempatar barras sobrepostas a favor da mais proxima da camera");

    // Clip plane em Z >= 2.0 oculta nBack/eSpan? Teste Z >= 2.5 (sign*z - offset >= 0 => z >= 2.5)
    vp.setClipPlane(Viewport3D::ClipAxis::Z, 2.5, false);
    TEST_ASSERT(vp.pickNode(pNodeOverlap) == nFront, "No acima do plano de corte deve permanecer selecionavel");
    TEST_ASSERT(vp.pickElement(pElemOverlap) == INVALID_ID,
                "Barra em Z=1.5 recortada por Z>=2.5 nao deve ser selecionada pelo picking");
    vp.setClipPlane(Viewport3D::ClipAxis::None, 0.0, false);

    // Guarda !m_hasCursor3D nos modos AddElement e Measure (antes de qualquer mouseMoveEvent)
    Viewport3D freshVp;
    freshVp.resize(800, 600);
    freshVp.setModel(&mdl);
    freshVp.camera().setViewIsometric();

    const std::size_t nodeCountBefore = mdl.getNodes().size();
    const std::size_t elemCountBefore = mdl.getElements().size();

    freshVp.setInteractionMode(InteractionMode::AddElement);
    QMouseEvent clickEmptyAdd(QEvent::MouseButtonPress,
                              QPointF(25.0, 25.0),
                              QPointF(25.0, 25.0),
                              Qt::LeftButton,
                              Qt::LeftButton,
                              Qt::NoModifier);
    QCoreApplication::sendEvent(&freshVp, &clickEmptyAdd);
    TEST_ASSERT(mdl.getNodes().size() == nodeCountBefore && mdl.getElements().size() == elemCountBefore,
                "Modo AddElement com !m_hasCursor3D nao deve criar nos ou barras fantasmas");

    int measureMsgs = 0;
    QObject::connect(&freshVp, &Viewport3D::statusMessage, &freshVp, [&](const QString&) {
        ++measureMsgs;
    });
    freshVp.setInteractionMode(InteractionMode::Measure);
    QMouseEvent clickEmptyMeasure(QEvent::MouseButtonPress,
                                  QPointF(25.0, 25.0),
                                  QPointF(25.0, 25.0),
                                  Qt::LeftButton,
                                  Qt::LeftButton,
                                  Qt::NoModifier);
    QCoreApplication::sendEvent(&freshVp, &clickEmptyMeasure);
    TEST_ASSERT(measureMsgs == 0,
                "Modo Measure com !m_hasCursor3D nao deve registrar medicao na origem (0,0,0)");
}

void testOpenGlLifecycleAndBenchmarks() {
    QTemporaryDir tempDir;
    TEST_ASSERT(tempDir.isValid(), "QTemporaryDir invalido para benchmark");
    const QString projectPath = tempDir.filePath(QStringLiteral("bench_2k_bars_10k_shells.omniframe"));

    // 1. Modelo 2000 barras + 10000 cascas Quad4 (100x100)
    StructuralModel largeModel;
    buildLargeFrameAndShellModel(largeModel, 2000, 100, 100, false);
    TEST_ASSERT(largeModel.getElements().size() == 2000,
                "Modelo benchmark deve ter exatamente 2000 barras");
    TEST_ASSERT(largeModel.surfaceMesh().elements.size() == 10000,
                "Modelo benchmark deve ter exatamente 10000 cascas");

    TEST_ASSERT(ProjectSerializer::saveToFile(projectPath, largeModel),
                "Falha ao salvar projeto benchmark 2000 barras + 10000 cascas");

    StructuralModel loadedModel;
    const auto tLoadStart = std::chrono::steady_clock::now();
    const bool loadedOk = ProjectSerializer::loadFromFile(projectPath, loadedModel);
    const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tLoadStart).count();
    TEST_ASSERT(loadedOk, "Falha ao carregar projeto benchmark 2000 barras + 10000 cascas");
    TEST_ASSERT(loadedModel.getElements().size() == 2000 && loadedModel.surfaceMesh().elements.size() == 10000,
                "Contagens apos loadFromFile devem preservar 2000 barras e 10000 cascas");
    std::cout << "[Benchmark] loadFromFile (2000 bars + 10000 shells): " << loadMs << " ms\n";
#if !defined(__SANITIZE_ADDRESS__)
    TEST_ASSERT(loadMs < 200.0, "Meta de carregamento (< 200 ms em Release) excedida para 2000 barras + 10000 cascas");
#else
    TEST_ASSERT(loadMs < 2500.0, "Limite ASan de carregamento excedido para 2000 barras + 10000 cascas");
#endif

    // 2. cloneWithoutHistory() com historico de desfazer preenchido
    loadedModel.pushUndo();
    loadedModel.pushUndo();
    loadedModel.pushUndo();
    TEST_ASSERT(loadedModel.canUndo(), "Modelo original deve possuir historico undo antes do snapshot");

    const auto tCloneStart = std::chrono::steady_clock::now();
    StructuralModel snapshot = loadedModel.cloneWithoutHistory();
    const double cloneMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tCloneStart).count();
    TEST_ASSERT(!snapshot.canUndo() && !snapshot.canRedo(),
                "cloneWithoutHistory() nao deve copiar pilha de historico undo/redo");
    TEST_ASSERT(snapshot.getElements().size() == 2000 && snapshot.surfaceMesh().elements.size() == 10000,
                "cloneWithoutHistory() deve preservar geometria e malha completas");
    std::cout << "[Benchmark] cloneWithoutHistory (2000 bars + 10000 shells): " << cloneMs << " ms\n";

    // 3. 10 casos de carga com reutilizacao de K_red e fatoracao esparsa ([AUD-022])
    StructuralModel multiCaseModel;
    buildLargeFrameAndShellModel(multiCaseModel, 2000, 10, 10, true);
    multiCaseModel.getLoadCases().clear();
    multiCaseModel.getLoadCombinations().clear();

    NodeId loadedNodeId = INVALID_ID;
    for (const auto& n : multiCaseModel.getNodes()) {
        if (n.z() > 2.5 && !n.isSupported()) {
            loadedNodeId = n.id;
            break;
        }
    }
    TEST_ASSERT(loadedNodeId != INVALID_ID, "Deve existir no livre no topo para casos de carga");

    for (int c = 1; c <= 10; ++c) {
        kernel::LoadCase lc;
        lc.id = c;
        lc.name = "LC-" + std::to_string(c);
        lc.nodalLoads.push_back(kernel::NodalLoadPatternEntry{loadedNodeId, {1.0 * c, 0.0, -5.0 * c, 0.0, 0.0, 0.0}});
        multiCaseModel.addLoadCase(lc);
    }
    kernel::LoadCombination comb;
    comb.id = 1;
    comb.name = "COMB-1";
    comb.factors.push_back({1, 1.0});
    comb.factors.push_back({2, 1.0});
    multiCaseModel.addLoadCombination(comb);

    std::vector<kernel::CaseDisplacementResult> caseResults;
    std::vector<kernel::CaseDisplacementResult> comboResults;
    kernel::EnvelopeExtremes env;
    std::string solveErr;
    const auto tSolveStart = std::chrono::steady_clock::now();
    const bool solvedOk = FemSolver3D::solveLoadCasesAndCombinations(
        multiCaseModel, multiCaseModel.solverOptions(), caseResults, comboResults, env, &solveErr);
    const double solve10Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tSolveStart).count();
    TEST_ASSERT(solvedOk, ("Falha em solveLoadCasesAndCombinations (10 casos): " + solveErr).c_str());
    TEST_ASSERT(caseResults.size() == 10 && comboResults.size() == 1,
                "Devem ser retornados os 10 casos de carga e 1 combinacao");
    const double ratioNorm = caseResults[9].U_full.norm() / std::max(1e-12, caseResults[0].U_full.norm());
    TEST_ASSERT(std::abs(ratioNorm - 10.0) < 1e-4,
                "Superposicao e reutilizacao de K_red/fatoracao devem preservar resposta linear exata nos 10 casos");
    std::cout << "[Benchmark] solveLoadCasesAndCombinations (2000 bars + 100 shells + diaphragm, 10 cases + 1 combo): "
              << solve10Ms << " ms\n";

    // 4. Ciclo de vida OpenGL (criacao -> render -> destruicao -> recriacao) + Benchmark paintGL hover/orbit
    {
        auto vp1 = std::make_unique<Viewport3D>();
        vp1->resize(800, 600);
        vp1->setModel(&loadedModel);
        vp1->show();
        pumpEventsMs(30);
        const QImage img1 = vp1->grabFramebuffer();
        if (vp1->isOpenGLReady()) {
            TEST_ASSERT(!img1.isNull() && img1.width() > 0 && img1.height() > 0,
                        "Primeiro Viewport3D deve renderizar framebuffer valido");
        } else {
            const QPixmap pm1 = vp1->grab();
            TEST_ASSERT(!pm1.isNull() && pm1.width() > 0 && pm1.height() > 0,
                        "Primeiro Viewport3D em modo offscreen/fallback deve renderizar via QPainter");
        }
        vp1->hide();
        vp1.reset();
        pumpEventsMs(20);
    }

    {
        auto vp2 = std::make_unique<Viewport3D>();
        vp2->resize(1024, 720);
        vp2->setModel(&loadedModel);
        vp2->fitAll();
        vp2->show();
        pumpEventsMs(30);

        // Warmup de GPU e construcao inicial do cache VBO/VAO
        const QImage warm1 = vp2->grabFramebuffer();
        const QImage warm2 = vp2->grabFramebuffer();
        if (vp2->isOpenGLReady()) {
            TEST_ASSERT(!warm1.isNull() && !warm2.isNull(),
                        "Segundo Viewport3D apos destruicao/recriacao deve renderizar sem falhas");
        } else {
            const QPixmap pmWarm = vp2->grab();
            TEST_ASSERT(!pmWarm.isNull(),
                        "Segundo Viewport3D em fallback offscreen deve renderizar via QPainter sem falhas");
        }

        double sumFrameMs = 0.0;
        const int benchFrames = 10;
        for (int f = 0; f < benchFrames; ++f) {
            if (f % 2 == 0) {
                vp2->camera().rotate(2.5f, 0.8f);
            } else {
                const QPointF pt(250.0 + f * 20.0, 240.0 + f * 12.0);
                QMouseEvent moveEv(QEvent::MouseMove, pt, pt, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(vp2.get(), &moveEv);
            }
            if (vp2->isOpenGLReady()) {
                const QImage frameImg = vp2->grabFramebuffer();
                TEST_ASSERT(!frameImg.isNull(), "Frame benchmark paintGL invalido");
                sumFrameMs += vp2->lastPaintGlMs();
            } else {
                const QPixmap pm = vp2->grab();
                TEST_ASSERT(!pm.isNull(), "Frame fallback Viewport3D::grab invalido");
            }
        }
        const double avgFrameMs = sumFrameMs / static_cast<double>(benchFrames);
        std::cout << "[Benchmark] paintGL hover/orbit (2000 bars + 10000 shells, glReady="
                  << (vp2->isOpenGLReady() ? "true" : "false")
                  << "): avg = " << avgFrameMs << " ms\n";
        if (vp2->isOpenGLReady()) {
#if !defined(__SANITIZE_ADDRESS__)
            TEST_ASSERT(avgFrameMs < 16.0,
                        "Meta de frame paintGL (< 16 ms em hover/orbit Release) excedida");
#else
            TEST_ASSERT(avgFrameMs < 120.0,
                        "Meta de frame paintGL em modo ASan excedida");
#endif
        }

        vp2->hide();
        vp2.reset();
        pumpEventsMs(20);
    }
}

void testAud019AndAud022Hardening() {
    // 1) [AUD-019] Reparenting do mesmo Viewport3D entre pais distintos
    {
        StructuralModel reparentModel;
        reparentModel.populateStandardCatalog();
        const NodeId n1 = reparentModel.addNode(0.0, 0.0, 0.0);
        const NodeId n2 = reparentModel.addNode(4.0, 0.0, 3.0);
        reparentModel.addElement(n1, n2, 1, 1);

        QWidget hostA;
        QWidget hostB;
        hostA.resize(320, 240);
        hostB.resize(320, 240);

        auto* vp = new Viewport3D(&hostA);
        vp->resize(320, 240);
        vp->setModel(&reparentModel);
        vp->fitAll();
        hostA.show();
        vp->show();
        pumpEventsMs(50);

        if (vp->isOpenGLReady()) {
            const QImage frameA = vp->grabFramebuffer();
            TEST_ASSERT(!frameA.isNull() && frameA.width() > 0 && frameA.height() > 0,
                        "Viewport3D deve renderizar antes de reparenting");
        } else {
            const QPixmap pmA = vp->grab();
            TEST_ASSERT(!pmA.isNull() && pmA.width() > 0 && pmA.height() > 0,
                        "Viewport3D fallback deve renderizar antes de reparenting");
        }

        vp->setParent(&hostB);
        vp->resize(320, 240);
        hostB.show();
        vp->show();
        pumpEventsMs(50);
        vp->camera().rotate(12.0f, 5.0f);
        vp->update();
        pumpEventsMs(20);

        if (vp->isOpenGLReady()) {
            const QImage frameB = vp->grabFramebuffer();
            TEST_ASSERT(!frameB.isNull() && frameB.width() > 0 && frameB.height() > 0,
                        "Mesmo Viewport3D deve continuar renderizando após reparenting entre janelas");
        } else {
            const QPixmap pmB = vp->grab();
            TEST_ASSERT(!pmB.isNull() && pmB.width() > 0 && pmB.height() > 0,
                        "Mesmo Viewport3D fallback deve continuar renderizando após reparenting");
        }

        vp->hide();
        hostB.hide();
        hostA.hide();
    }
    pumpEventsMs(20);

    // 2) [AUD-019] Guarda de overflow INT_MAX na alocação de VBO
    {
        int outBytes = -1;
        TEST_ASSERT(Viewport3D::validateVboByteSize(0, sizeof(float), &outBytes) && outBytes == 0,
                    "validateVboByteSize(0) deve retornar true e 0 bytes");
        TEST_ASSERT(Viewport3D::validateVboByteSize(static_cast<std::size_t>(INT_MAX), 1u, &outBytes) &&
                        outBytes == INT_MAX,
                    "validateVboByteSize no limite exato INT_MAX deve ser aceito");
        TEST_ASSERT(!Viewport3D::validateVboByteSize(static_cast<std::size_t>(INT_MAX) + 1u, 1u),
                    "validateVboByteSize acima de INT_MAX deve retornar false");
        TEST_ASSERT(!Viewport3D::validateVboByteSize(
                        static_cast<std::size_t>(INT_MAX) / sizeof(float) + 1u, sizeof(float)),
                    "validateVboByteSize em elementos float excedendo INT_MAX deve retornar false");
        TEST_ASSERT(!Viewport3D::validateVboByteSize(std::numeric_limits<std::size_t>::max(), 16u),
                    "validateVboByteSize deve bloquear overflow multiplicativo size_t");

        QOpenGLBuffer dummyBuf(QOpenGLBuffer::VertexBuffer);
        float sentinelValue = 0.0f;
        TEST_ASSERT(!Viewport3D::safeAllocateVbo(
                        dummyBuf,
                        nullptr,
                        static_cast<std::size_t>(INT_MAX) / sizeof(float) + 1u,
                        sizeof(float)),
                    "safeAllocateVbo deve recusar alocação com srcData==nullptr quando tamanho excede INT_MAX bytes");
        TEST_ASSERT(!Viewport3D::safeAllocateVbo(
                        dummyBuf,
                        &sentinelValue,
                        static_cast<std::size_t>(INT_MAX) / sizeof(float) + 1u,
                        sizeof(float)),
                    "safeAllocateVbo deve recusar alocação que exceda INT_MAX bytes");
    }

    // 3) [AUD-019] Estabilidade do cache de fallback de FBO (sem thrashing frame a frame)
    {
        TEST_ASSERT(Viewport3D::canReuseCachedFboRequest(800, 600, 1.0, 2.0f, 16, 800, 600, 1.0, 2.0f, 16),
                    "Parâmetros idênticos de FBO devem reutilizar resolução em cache");
        TEST_ASSERT(!Viewport3D::canReuseCachedFboRequest(800, 600, 1.0, 2.0f, 16, 801, 600, 1.0, 2.0f, 16) &&
                        !Viewport3D::canReuseCachedFboRequest(800, 600, 1.0, 2.0f, 16, 800, 601, 1.0, 2.0f, 16) &&
                        !Viewport3D::canReuseCachedFboRequest(800, 600, 1.0, 2.0f, 16, 800, 600, 1.5, 2.0f, 16) &&
                        !Viewport3D::canReuseCachedFboRequest(800, 600, 1.0, 2.0f, 16, 800, 600, 1.0, 1.5f, 16) &&
                        !Viewport3D::canReuseCachedFboRequest(800, 600, 1.0, 2.0f, 16, 800, 600, 1.0, 2.0f, 8),
                    "Mudança em qualquer dimensão/DPR/escala/MSAA deve invalidar cache de FBO");

        StructuralModel fboModel;
        fboModel.populateStandardCatalog();
        const NodeId n1 = fboModel.addNode(0.0, 0.0, 0.0);
        const NodeId n2 = fboModel.addNode(3.0, 0.0, 2.0);
        fboModel.addElement(n1, n2, 1, 1);

        auto vpFbo = std::make_unique<Viewport3D>();
        vpFbo->resize(480, 320);
        vpFbo->setModel(&fboModel);
        vpFbo->fitAll();

        RenderQualitySettings qs = vpFbo->renderQualitySettings();
        qs.autoMaxResolution = false;
        qs.resolutionScale = 2.0f;
        qs.autoMsaa = false;
        qs.requestedMsaa = 16;
        vpFbo->setRenderQualitySettings(qs);
        vpFbo->show();
        pumpEventsMs(40);

        if (vpFbo->isOpenGLReady()) {
            const QImage warm = vpFbo->grabFramebuffer();
            TEST_ASSERT(!warm.isNull(), "Warmup FBO deve gerar imagem válida");
            const std::uint64_t attemptsAfterWarmup = vpFbo->fboAllocationAttemptCount();
            for (int f = 0; f < 8; ++f) {
                vpFbo->camera().rotate(3.0f, 1.0f);
                const QImage img = vpFbo->grabFramebuffer();
                TEST_ASSERT(!img.isNull(), "Frame órbita FBO deve renderizar");
            }
            TEST_ASSERT(vpFbo->fboAllocationAttemptCount() == attemptsAfterWarmup,
                        "Órbita em tamanho constante não pode disparar realocações repetidas de FBO");
        } else {
            const QPixmap pm = vpFbo->grab();
            TEST_ASSERT(!pm.isNull(), "Renderização offscreen sem OpenGL deve suceder");
        }
        vpFbo->hide();
        vpFbo.reset();
        pumpEventsMs(20);
    }

    // 4) [AUD-022] Medição de pico de RAM em cloneWithoutHistory() vs histórico de 15 estados
    {
        StructuralModel heavyModel;
        buildLargeFrameAndShellModel(heavyModel, 2000, 100, 100, false);
        for (int i = 0; i < 15; ++i) {
            heavyModel.addNode(500.0 + static_cast<double>(i), 0.0, 0.0);
            heavyModel.pushUndo();
        }
        const std::size_t totalBytes = heavyModel.estimatedMemoryBytes();
        const std::size_t histBytes = heavyModel.estimatedHistoryMemoryBytes();
        const std::size_t activeBytes = (totalBytes > histBytes) ? (totalBytes - histBytes) : 0u;
        TEST_ASSERT(activeBytes > 0u && histBytes >= 10u * activeBytes,
                    "Modelo com 15 estados de undo deve possuir histórico >= 10x do tamanho ativo");

#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmcBefore{};
        PROCESS_MEMORY_COUNTERS pmcAfter{};
        K32GetProcessMemoryInfo(GetCurrentProcess(), &pmcBefore, sizeof(pmcBefore));
#endif
        const StructuralModel snap = heavyModel.cloneWithoutHistory();
#ifdef _WIN32
        K32GetProcessMemoryInfo(GetCurrentProcess(), &pmcAfter, sizeof(pmcAfter));
        const std::size_t wsDeltaBytes =
            (pmcAfter.WorkingSetSize > pmcBefore.WorkingSetSize)
                ? static_cast<std::size_t>(pmcAfter.WorkingSetSize - pmcBefore.WorkingSetSize)
                : 0u;
#else
        const std::size_t wsDeltaBytes = 0u;
#endif
        TEST_ASSERT(snap.estimatedHistoryMemoryBytes() == 0u,
                    "cloneWithoutHistory() deve produzir cópia com 0 bytes de histórico undo/redo");
        TEST_ASSERT(histBytes >= 10u * snap.estimatedMemoryBytes(),
                    "Histórico retido no modelo deve ser >= 10x do snapshot sem histórico");
        TEST_ASSERT(snap.estimatedMemoryBytes() <= activeBytes + 4096u,
                    "cloneWithoutHistory() deve preservar apenas o tamanho ativo do modelo");
        std::cout << "[Benchmark] cloneWithoutHistory RAM: active=" << activeBytes
                  << " B, snap=" << snap.estimatedMemoryBytes()
                  << " B, history=" << histBytes
                  << " B, workingSetDelta=" << wsDeltaBytes << " B\n";
#if !defined(__SANITIZE_ADDRESS__)
        TEST_ASSERT(wsDeltaBytes < static_cast<std::size_t>(0.5 * static_cast<double>(histBytes)),
                    "Delta de Working Set em cloneWithoutHistory() deve ficar bem abaixo do tamanho do histórico");
#endif
    }
}

} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    QTemporaryDir isolatedPrefsRoot;
    TEST_ASSERT(isolatedPrefsRoot.isValid(), "QTemporaryDir invalido");
    omniframe::io::UserPreferencesStore::ScopedDirectoryOverride prefsOverride(
        isolatedPrefsRoot.filePath(QStringLiteral("OmniFrame")));

    std::cout << "[Test Phase 10] 1/6 Camera3D (worldToScreen orto/persp, roll continuo e farPlane)...\n";
    testCamera3DProjectionAndRoll();

    std::cout << "[Test Phase 10] 2/6 Paradas exatas da legenda DCR...\n";
    testDcrLegendStopsAlignment();

    std::cout << "[Test Phase 10] 3/6 timerEvent em animacao somente-malha...\n";
    testMeshOnlyAnimationTimer();

    std::cout << "[Test Phase 10] 4/6 Picking (borda esquerda, Z-depth, clip plane) e guardas !hasCursor3D...\n";
    testPickingAndGuards();

    std::cout << "[Test Phase 10] 5/6 Lifecycle OpenGL 3.3 Core + benchmarks 2000 barras e 10000 cascas...\n";
    testOpenGlLifecycleAndBenchmarks();

    std::cout << "[Test Phase 10] 6/6 Reparenting Viewport3D, guarda VBO INT_MAX, cache FBO e pico RAM cloneWithoutHistory ([AUD-019/022])...\n";
    testAud019AndAud022Hardening();

    omniframe::io::AppLogger::shutdown();
    std::cout << "[OK] Todos os testes de renderizacao e desempenho da Fase 3 passaram com sucesso!\n";
    return 0;
}
