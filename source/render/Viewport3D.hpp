#pragma once

#include "core/StructuralModel.hpp"
#include "render/Camera3D.hpp"
#include "render/RenderSettings.hpp"

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <memory>
#include <vector>

namespace omniframe {

class Viewport3D : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit Viewport3D(QWidget* parent = nullptr);
    ~Viewport3D() override;

    void setModel(StructuralModel* model);
    StructuralModel* model() const { return m_model; }

    Camera3D& camera() { return m_camera; }
    const Camera3D& camera() const { return m_camera; }

    // Configurações do Viewport
    void setInteractionMode(InteractionMode mode);
    InteractionMode interactionMode() const { return m_mode; }

    void setWorkPlane(WorkPlane plane);
    WorkPlane workPlane() const { return m_workPlane; }

    void setPlaneOffset(double offset);
    double planeOffset() const { return m_planeOffset; }

    void setSnapToGrid(bool snap);
    bool snapToGrid() const { return m_snapToGrid; }

    void setGridStep(double step);
    double gridStep() const { return m_gridStep; }

    void setShowLocalAxes(bool show);
    bool showLocalAxes() const { return m_showLocalAxes; }

    void setShowReactions(bool show);
    bool showReactions() const { return m_showReactions; }

    void setActiveDiagram(DiagramType diagram);
    DiagramType activeDiagram() const { return m_activeDiagram; }

    void setDiagramScale(double scale);
    double diagramScale() const { return m_diagramScale; }

    void setDeformedScale(double scale);
    double deformedScale() const { return m_deformedScale; }

    // Pós-processamento avançado 3D (contornos, malha superficial/sólida, clip plane e animação)
    enum class PostContourMode {
        None = 0,
        Displacement = 1,
        NbrComplianceDcr = 2,
        VonMisesStress = 3
    };

    enum class ClipAxis {
        None = 0,
        X = 1,
        Y = 2,
        Z = 3
    };

    void setPostContourMode(PostContourMode mode);
    PostContourMode postContourMode() const { return m_postContourMode; }

    void setShowSurfaceMesh(bool show);
    bool showSurfaceMesh() const { return m_showSurfaceMesh; }

    void setElementDcrMap(const std::unordered_map<ElementId, double>& dcrByElement);
    void clearElementDcrMap();

    void setClipPlane(ClipAxis axis, double offsetMeters, bool inverted = false);
    ClipAxis clipAxis() const { return m_clipAxis; }
    double clipOffset() const { return m_clipOffset; }
    bool clipInverted() const { return m_clipInverted; }
    QVector4D activeClipPlaneVec() const;

    void setPostAnimationField(const QString& label,
                               const std::unordered_map<NodeId, Vec3>& modeDisplByNode,
                               double baseScale = 1.0,
                               bool oscillating = true);
    void setPostMeshAnimationField(const std::unordered_map<NodeId, Vec3>& meshDisplByNode);
    void setNonlinearStepFrames(const QString& label,
                                const std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>>& stepFrames);
    void setNonlinearMeshStepFrames(
        const std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>>& meshStepFrames);
    void setNonlinearStepIndex(int idx);
    int nonlinearStepCount() const {
        return static_cast<int>(std::max(m_nonlinearFrames.size(), m_nonlinearMeshFrames.size()));
    }
    int nonlinearStepIndex() const { return m_nonlinearStepIdx; }
    bool hasPostAnimation() const {
        return !m_postAnimNodeDispl.empty() || !m_postAnimMeshDispl.empty()
            || !m_nonlinearFrames.empty() || !m_nonlinearMeshFrames.empty();
    }
    void setAnimationPlaying(bool playing);
    bool isAnimationPlaying() const { return m_animPlaying; }
    void setAnimationSpeed(double speed);
    double animationSpeed() const { return m_animSpeed; }
    void setAnimationAmplitude(double amp);
    double animationAmplitude() const { return m_animAmplitude; }
    void clearPostAnimation();

    // Configurações de Qualidade de Imagem, Resolução e Plano Ativo
    void setRenderQualitySettings(const RenderQualitySettings& settings);
    const RenderQualitySettings& renderQualitySettings() const { return m_renderSettings; }
    GraphicsCapabilitiesInfo graphicsCapabilities() const;

    // Vistas Rápidas
    void viewIsometric();
    void viewFrontalXZ();
    void viewTopXY();
    void viewSideYZ();
    void fitAll();

signals:
    void cursorCoordinatesChanged(double x, double y, double z);
    void selectionChanged();
    void modelModified();
    void statusMessage(const QString& msg);
    void animationFrameChanged(int stepIndex, int totalSteps, double factorOrPhase);
    void postAnimationStateChanged();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    // Compilação e inicialização de shaders e buffers
    void initShaders();
    void initBuffers();

    // Rotinas de renderização gráfica 3D
    void renderGrid(const QMatrix4x4& mvp);
    void renderNodes(const QMatrix4x4& mvp);
    void renderSupports(const QMatrix4x4& mvp, const QMatrix4x4& view);
    void renderElements(const QMatrix4x4& mvp);
    void renderSurfaceMesh(const QMatrix4x4& mvp);
    void renderLoads(const QMatrix4x4& mvp, const QMatrix4x4& view);
    void renderDeformedShape(const QMatrix4x4& mvp);
    void renderDiagrams(const QMatrix4x4& mvp);
    void renderCursorPreview(const QMatrix4x4& mvp);
    void renderHUD(QPainter& painter);
    void timerEvent(QTimerEvent* event) override;

    // Helpers de geometria 3D básica
    void buildCube(std::vector<float>& vertices, float size, const QVector3D& center, const QVector4D& color);
    void buildPyramid(std::vector<float>& vertices, float baseSize, float height, const QVector3D& apex, const QVector4D& color);
    void buildSphere(std::vector<float>& vertices, float radius, int rings, int sectors, const QVector3D& center, const QVector4D& color);
    void buildCone(std::vector<float>& vertices, float radius, float height, const QVector3D& base, const QVector3D& dir, const QVector4D& color);

    // Raycasting e Picking
    bool intersectRayWorkingPlane(const QVector3D& rayOrigin, const QVector3D& rayDir, Vec3& outPt) const;
    Vec3 applySnap(const Vec3& pt) const;
    NodeId pickNode(const QPoint& mousePos, double maxDistPx = 14.0) const;
    ElementId pickElement(const QPoint& mousePos, double maxDistPx = 8.0) const;

    StructuralModel* m_model = nullptr;
    Camera3D m_camera;

    // Modos e flags
    InteractionMode m_mode = InteractionMode::Select;
    WorkPlane m_workPlane = WorkPlane::XY_Top;
    double m_planeOffset = 0.0;
    bool m_snapToGrid = true;
    double m_gridStep = 0.5;
    bool m_showLocalAxes = false;
    bool m_showReactions = true;
    DiagramType m_activeDiagram = DiagramType::None;
    double m_diagramScale = 1.0;
    double m_deformedScale = 50.0;

    PostContourMode m_postContourMode = PostContourMode::None;
    bool m_showSurfaceMesh = true;
    std::unordered_map<ElementId, double> m_elementDcrMap;

    ClipAxis m_clipAxis = ClipAxis::None;
    double m_clipOffset = 0.0;
    bool m_clipInverted = false;
    bool m_suppressClipPlaneForDraw = false;

    int m_animTimerId = 0;
    bool m_animPlaying = false;
    bool m_animOscillating = true;
    double m_animPhase = 0.0;
    double m_animSpeed = 1.0;
    double m_animAmplitude = 1.0;
    double m_animBaseScale = 1.0;
    QString m_postAnimLabel;
    std::unordered_map<NodeId, Vec3> m_postAnimNodeDispl;
    std::unordered_map<NodeId, Vec3> m_postAnimMeshDispl;
    std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>> m_nonlinearFrames;
    std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>> m_nonlinearMeshFrames;
    int m_nonlinearStepIdx = 0;

    mutable bool m_legendVisible = false;
    mutable QString m_legendTitle;
    mutable QString m_legendUnit;
    mutable double m_legendMin = 0.0;
    mutable double m_legendMax = 1.0;
    mutable bool m_legendIsDcr = false;

    // Estado do mouse
    QPoint m_lastMousePos;
    bool m_isRotating = false;
    bool m_isPanning = false;

    // Estado de construção interativa
    Vec3 m_currentCursor3D{0.0, 0.0, 0.0};
    bool m_hasCursor3D = false;
    NodeId m_startNodeForElement = INVALID_ID;
    Vec3 m_measureStartPt{0.0, 0.0, 0.0};
    bool m_hasMeasureStart = false;

    // Shaders OpenGL
    std::unique_ptr<QOpenGLShaderProgram> m_lineShader;
    std::unique_ptr<QOpenGLShaderProgram> m_aaLineShader;
    std::unique_ptr<QOpenGLShaderProgram> m_meshShader;
    std::unique_ptr<QOpenGLShaderProgram> m_diagFillShader;
    std::unique_ptr<QOpenGLShaderProgram> m_resolveShader;

    // VAOs e VBOs dinâmicos
    QOpenGLVertexArrayObject m_lineVAO;
    QOpenGLBuffer m_lineVBO;

    QOpenGLVertexArrayObject m_aaLineVAO;
    QOpenGLBuffer m_aaLineVBO;

    QOpenGLVertexArrayObject m_meshVAO;
    QOpenGLBuffer m_meshVBO;

    QOpenGLVertexArrayObject m_diagVAO;
    QOpenGLBuffer m_diagVBO;

    QOpenGLVertexArrayObject m_quadVAO;
    QOpenGLBuffer m_quadVBO;

    // Helpers de Renderização Alta Definição & FBO Offscreen (SSAA + MSAA)
    void drawLinesAA(const QMatrix4x4& mvp,
                     const std::vector<float>& segmentVertices7f,
                     float logicalWidthPx,
                     bool depthWrite = true,
                     bool splitOpaquePasses = false);
    bool ensureRenderTargets(int logicalW, int logicalH, qreal dpr);
    void destroyRenderTargets();
    void applyVsyncState();
    float computeEffectiveSupersample(int logicalW, int logicalH, qreal dpr) const;
    int resolveTargetMsaaSamples() const;

    RenderQualitySettings m_renderSettings;
    bool m_vsyncDirty = true;

    // Framebuffers Offscreen (MSAA -> Resolve Texture -> Fullscreen Catmull-Rom Pass)
    GLuint m_msaaFbo = 0;
    GLuint m_msaaColorRbo = 0;
    GLuint m_msaaDepthRbo = 0;

    GLuint m_resolveFbo = 0;
    GLuint m_resolveColorTex = 0;
    GLuint m_resolveDepthRbo = 0;

    int m_fboWidth = 0;
    int m_fboHeight = 0;
    int m_activeMsaaSamples = 0;
    float m_activeSupersample = 1.0f;

    // Capacidades de Hardware da GPU
    bool m_glInitialized = false;
    QString m_glRenderer;
    QString m_glVendor;
    QString m_glVersion;
    int m_glMaxSamples = 0;
    int m_glMaxTexSize = 4096;
    float m_glMaxRawLineWidth = 1.0f;
};

} // namespace omniframe

