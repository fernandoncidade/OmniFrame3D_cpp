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
    void renderLoads(const QMatrix4x4& mvp, const QMatrix4x4& view);
    void renderDeformedShape(const QMatrix4x4& mvp);
    void renderDiagrams(const QMatrix4x4& mvp);
    void renderCursorPreview(const QMatrix4x4& mvp);
    void renderHUD(QPainter& painter);

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
    WorkPlane m_workPlane = WorkPlane::XZ_Frontal;
    double m_planeOffset = 0.0;
    bool m_snapToGrid = true;
    double m_gridStep = 0.5;
    bool m_showLocalAxes = false;
    bool m_showReactions = true;
    DiagramType m_activeDiagram = DiagramType::None;
    double m_diagramScale = 1.0;
    double m_deformedScale = 50.0;

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

