#include "Viewport3D.hpp"
#include "ShaderCollection.hpp"
#include "io/AppLogger.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QOpenGLContext>
#include <QTimerEvent>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

#ifndef GL_MAX_COLOR_TEXTURE_SAMPLES
#define GL_MAX_COLOR_TEXTURE_SAMPLES 0x910E
#endif
#ifndef GL_MAX_DEPTH_TEXTURE_SAMPLES
#define GL_MAX_DEPTH_TEXTURE_SAMPLES 0x910F
#endif
#ifndef GL_ALIASED_LINE_WIDTH_RANGE
#define GL_ALIASED_LINE_WIDTH_RANGE 0x846E
#endif

namespace omniframe {

namespace {

QVector4D dcrRatioToColor(double dcr) {
    if (dcr <= 0.50) {
        const float t = static_cast<float>(std::clamp(dcr / 0.50, 0.0, 1.0));
        return QVector4D(0.10f + 0.14f * t, 0.68f + 0.12f * t, 0.68f - 0.36f * t, 1.0f);
    }
    if (dcr <= 0.85) {
        const float t = static_cast<float>((dcr - 0.50) / 0.35);
        return QVector4D(0.24f + 0.71f * t, 0.80f - 0.02f * t, 0.32f - 0.24f * t, 1.0f);
    }
    if (dcr <= 1.00) {
        const float t = static_cast<float>((dcr - 0.85) / 0.15);
        return QVector4D(0.95f + 0.04f * t, 0.78f - 0.36f * t, 0.08f, 1.0f);
    }
    return QVector4D(0.95f, 0.14f, 0.22f, 1.0f);
}

QColor dcrQColor(double dcr) {
    const QVector4D c = dcrRatioToColor(dcr);
    return QColor::fromRgbF(c.x(), c.y(), c.z(), 1.0f);
}

double computeModelVisualDiagonal(const StructuralModel* model) {
    if (!model) return 10.0;
    if (model->getNodes().empty() && model->surfaceMesh().nodes.empty()) return 10.0;
    Vec3 minPt, maxPt;
    model->getBoundingBox(minPt, maxPt);
    return std::max(1.0, (maxPt - minPt).norm());
}

void expandSegmentsToAAQuads(const std::vector<float>& segmentVertices7f,
                             std::vector<float>& outQuadVerts) {
    const std::size_t numSegments = segmentVertices7f.size() / 14;
    outQuadVerts.clear();
    if (numSegments == 0) {
        return;
    }
    outQuadVerts.reserve(numSegments * 6 * 16);

    for (std::size_t i = 0; i < numSegments; ++i) {
        const float* seg = segmentVertices7f.data() + i * 14;
        const float ax = seg[0], ay = seg[1], az = seg[2];
        const float ar = seg[3], ag = seg[4], ab = seg[5], aa = seg[6];
        const float bx = seg[7], by = seg[8], bz = seg[9];
        const float br = seg[10], bg = seg[11], bb = seg[12], ba = seg[13];

        auto emitVertex = [&](float along, float perp) {
            outQuadVerts.insert(outQuadVerts.end(), {
                ax, ay, az,
                bx, by, bz,
                ar, ag, ab, aa,
                br, bg, bb, ba,
                along, perp
            });
        };

        emitVertex(0.0f, -1.0f);
        emitVertex(1.0f, -1.0f);
        emitVertex(1.0f,  1.0f);

        emitVertex(0.0f, -1.0f);
        emitVertex(1.0f,  1.0f);
        emitVertex(0.0f,  1.0f);
    }
}

bool clipSegment3DPlane(QVector3D& p1, QVector3D& p2, const QVector4D& plane) {
    const float d1 = plane.x() * p1.x() + plane.y() * p1.y() + plane.z() * p1.z() + plane.w();
    const float d2 = plane.x() * p2.x() + plane.y() * p2.y() + plane.z() * p2.z() + plane.w();
    if (d1 < -1e-5f && d2 < -1e-5f) {
        return false;
    }
    if (d1 < 0.0f) {
        const float denom = d2 - d1;
        if (std::abs(denom) < 1e-7f) return false;
        const float t = std::clamp((-d1) / denom, 0.0f, 1.0f);
        p1 = p1 + (p2 - p1) * t;
    } else if (d2 < 0.0f) {
        const float denom = d1 - d2;
        if (std::abs(denom) < 1e-7f) return false;
        const float t = std::clamp((-d2) / denom, 0.0f, 1.0f);
        p2 = p2 + (p1 - p2) * t;
    }
    return true;
}

bool clipSegment4DHalfSpace(QVector4D& c1, QVector4D& c2, float f1, float f2) {
    if (f1 < 0.0f && f2 < 0.0f) {
        return false;
    }
    if (f1 < 0.0f) {
        const float denom = f2 - f1;
        if (std::abs(denom) < 1e-7f) return false;
        const float t = std::clamp((-f1) / denom, 0.0f, 1.0f);
        c1 = c1 + (c2 - c1) * t;
    } else if (f2 < 0.0f) {
        const float denom = f1 - f2;
        if (std::abs(denom) < 1e-7f) return false;
        const float t = std::clamp((-f2) / denom, 0.0f, 1.0f);
        c2 = c2 + (c1 - c2) * t;
    }
    return true;
}

void renderGlFallbackNotice(QPainter& painter,
                            const QRect& targetRect,
                            const ActivePlanePalette& pal,
                            const QString& errorMessage) {
    painter.fillRect(targetRect, QColor::fromRgbF(pal.clearColor.x(), pal.clearColor.y(), pal.clearColor.z(), 1.0f));
    painter.setPen(pal.hudTextColor);
    const QString fallbackMsg = errorMessage.isEmpty()
        ? locale::GerenciadorTraducao::trText("OpenGL 3.3 Core indisponível nesta sessão.")
        : errorMessage;
    painter.drawText(targetRect.adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, fallbackMsg);
}

} // namespace

std::vector<Viewport3D::LegendGradientStop> Viewport3D::computeDcrLegendStops(double maxDcr) {
    const double safeMax = (std::isfinite(maxDcr) && maxDcr > 1e-6) ? maxDcr : 1.20;
    std::vector<LegendGradientStop> stops;
    stops.reserve(6);

    auto pushStop = [&](double normBottom, const QColor& col) {
        const double nb = std::clamp(normBottom, 0.0, 1.0);
        stops.push_back(LegendGradientStop{nb, 1.0 - nb, col});
    };

    pushStop(0.0, dcrQColor(0.0));

    const std::array<double, 3> thresholds = {0.50, 0.85, 1.00};
    for (double thr : thresholds) {
        if (thr <= safeMax + 1e-9) {
            pushStop(thr / safeMax, dcrQColor(thr));
        }
    }

    if (safeMax > 1.00 + 1e-4) {
        const double overFrac = std::min(1.0, (1.00 / safeMax) + 1e-4);
        pushStop(overFrac, dcrQColor(1.01));
        pushStop(1.0, dcrQColor(safeMax));
    } else if (stops.back().normalizedFromBottom < 1.0 - 1e-6) {
        pushStop(1.0, dcrQColor(safeMax));
    }

    return stops;
}

Viewport3D::Viewport3D(QWidget* parent)
    : QOpenGLWidget(parent),
      m_lineVBO(QOpenGLBuffer::VertexBuffer),
      m_aaLineVBO(QOpenGLBuffer::VertexBuffer),
      m_meshVBO(QOpenGLBuffer::VertexBuffer),
      m_diagVBO(QOpenGLBuffer::VertexBuffer),
      m_quadVBO(QOpenGLBuffer::VertexBuffer),
      m_renderSettings(loadRenderSettings()) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

Viewport3D::~Viewport3D() {
    if (m_animTimerId != 0) {
        killTimer(m_animTimerId);
        m_animTimerId = 0;
    }
    cleanupGL();
}

void Viewport3D::cleanupGL() {
    if (m_contextDestroyedConn) {
        disconnect(m_contextDestroyedConn);
        m_contextDestroyedConn = QMetaObject::Connection{};
    }

    auto resetShaders = [&]() {
        m_lineShader.reset();
        m_aaLineShader.reset();
        m_meshShader.reset();
        m_instancedMeshShader.reset();
        m_diagFillShader.reset();
        m_resolveShader.reset();
    };

    QOpenGLContext* ctx = context();
    if (ctx != nullptr && ctx->isValid() && m_glFunctionsReady) {
        if (QOpenGLContext::currentContext() != ctx) {
            makeCurrent();
        }
        if (QOpenGLContext::currentContext() == ctx) {
            destroyRenderTargets();

            m_lineVAO.destroy();
            m_lineVBO.destroy();
            m_aaLineVAO.destroy();
            m_aaLineVBO.destroy();
            m_meshVAO.destroy();
            m_meshVBO.destroy();
            m_diagVAO.destroy();
            m_diagVBO.destroy();
            m_quadVAO.destroy();
            m_quadVBO.destroy();

            m_nodeInstVAO.destroy();
            m_unitSphereVBO.destroy();
            m_nodeInstanceVBO.destroy();

            m_gridSurfaceCache.destroy();
            m_gridMinorCache.destroy();
            m_gridMajorCache.destroy();
            m_gridBorderCache.destroy();
            m_gridAxisCache.destroy();

            m_surfaceMeshTriCache.destroy();
            m_surfaceMeshEdgeCache.destroy();
            m_elemNormalCache.destroy();
            m_elemSelectedCache.destroy();
            m_elemAxisCache.destroy();
            m_supportMeshCache.destroy();
            m_loadLineCache.destroy();
            m_loadConeCache.destroy();
            m_deformedLineCache.destroy();
            m_diagramFillCache.destroy();
            m_diagramBorderCache.destroy();

            resetShaders();
            doneCurrent();
        } else {
            resetShaders();
        }
    } else {
        resetShaders();
    }

    m_glFunctionsReady = false;
    m_glInitialized = false;
    m_sceneCacheBuilt = false;
    m_geometryDirty = true;
    m_gridDirty = true;
    m_diagramCache.valid = false;
    m_unitSphereVertexCount = 0;
    m_cachedNodeInstanceCount = 0;
    m_fboLastRequestedW = 0;
    m_fboLastRequestedH = 0;
    m_fboLastRequestedDpr = 0.0;
    m_fboLastRequestedScale = 0.0f;
    m_fboLastRequestedSamples = -1;
    m_fboFallbackExhausted = false;
    m_fboAllocationAttemptCount = 0;
}

void Viewport3D::markGeometryDirty() {
    m_geometryDirty = true;
    m_diagramCache.valid = false;
}

QVector4D Viewport3D::modelClipPlaneVec() const {
    const float sign = m_clipInverted ? -1.0f : 1.0f;
    const float d = static_cast<float>(m_clipOffset);
    switch (m_clipAxis) {
        case ClipAxis::X: return QVector4D(sign, 0.0f, 0.0f, -sign * d);
        case ClipAxis::Y: return QVector4D(0.0f, sign, 0.0f, -sign * d);
        case ClipAxis::Z: return QVector4D(0.0f, 0.0f, sign, -sign * d);
        case ClipAxis::None:
        default:
            return QVector4D(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

void Viewport3D::setModel(StructuralModel* model) {
    m_model = model;
    m_geometryDirty = true;
    m_diagramCache.valid = false;
    update();
}

void Viewport3D::setInteractionMode(InteractionMode mode) {
    m_mode = mode;
    m_startNodeForElement = INVALID_ID;
    m_hasMeasureStart = false;
    update();
}

void Viewport3D::setWorkPlane(WorkPlane plane) {
    m_workPlane = plane;
    m_gridDirty = true;
    update();
}

void Viewport3D::setPlaneOffset(double offset) {
    if (!std::isfinite(offset)) return;
    m_planeOffset = offset;
    m_gridDirty = true;
    update();
}

void Viewport3D::setSnapToGrid(bool snap) {
    m_snapToGrid = snap;
    update();
}

void Viewport3D::setGridStep(double step) {
    if (!std::isfinite(step)) return;
    m_gridStep = std::clamp(step, 0.05, 1e6);
    m_gridDirty = true;
    update();
}

void Viewport3D::setShowLocalAxes(bool show) {
    m_showLocalAxes = show;
    m_geometryDirty = true;
    update();
}

void Viewport3D::setShowReactions(bool show) {
    m_showReactions = show;
    update();
}

void Viewport3D::setActiveDiagram(DiagramType diagram) {
    m_activeDiagram = diagram;
    m_geometryDirty = true;
    update();
}

void Viewport3D::setDiagramScale(double scale) {
    if (!std::isfinite(scale)) return;
    m_diagramScale = std::clamp(scale, 0.0, 1e6);
    m_geometryDirty = true;
    update();
}

void Viewport3D::setDeformedScale(double scale) {
    if (!std::isfinite(scale)) return;
    m_deformedScale = std::clamp(scale, 0.0, 1e6);
    m_geometryDirty = true;
    update();
}

void Viewport3D::setRenderQualitySettings(const RenderQualitySettings& settings) {
    const bool vsyncChanged = (m_renderSettings.vsyncEnabled != settings.vsyncEnabled);
    m_renderSettings = settings;
    if (vsyncChanged) {
        m_vsyncDirty = true;
    }
    m_geometryDirty = true;
    m_gridDirty = true;
    update();
}

GraphicsCapabilitiesInfo Viewport3D::graphicsCapabilities() const {
    GraphicsCapabilitiesInfo info;
    info.rendererName = m_glRenderer;
    info.vendorName = m_glVendor;
    info.glVersion = m_glVersion;
    info.maxSamples = m_glMaxSamples;
    info.maxTextureSize = m_glMaxTexSize;
    info.activeMsaaSamples = m_activeMsaaSamples;
    info.logicalWidth = width();
    info.logicalHeight = height();
    info.dpiScale = devicePixelRatioF();
    info.effectiveSupersample = m_glInitialized
        ? m_activeSupersample
        : computeEffectiveSupersample(width(), height(), devicePixelRatioF());
    info.renderWidthPx = m_fboWidth;
    info.renderHeightPx = m_fboHeight;
    return info;
}

void Viewport3D::viewIsometric() {
    m_camera.setViewIsometric();
    update();
}

void Viewport3D::viewFrontalXZ() {
    m_camera.setViewFrontalXZ();
    update();
}

void Viewport3D::viewTopXY() {
    m_camera.setViewTopXY();
    update();
}

void Viewport3D::viewSideYZ() {
    m_camera.setViewSideYZ();
    update();
}

void Viewport3D::fitAll() {
    if (!m_model) return;
    Vec3 minPt, maxPt;
    m_model->getBoundingBox(minPt, maxPt);
    m_camera.fitToBoundingBox(minPt, maxPt);
    update();
}

void Viewport3D::initializeGL() {
    if (m_glInitialized || m_glFunctionsReady) {
        cleanupGL();
    }
    if (m_contextDestroyedConn) {
        disconnect(m_contextDestroyedConn);
        m_contextDestroyedConn = QMetaObject::Connection{};
    }
    if (QOpenGLContext* ctx = context()) {
        m_contextDestroyedConn = connect(
            ctx, &QOpenGLContext::aboutToBeDestroyed, this, [this]() { cleanupGL(); }, Qt::DirectConnection);
    }

    m_glFunctionsReady = initializeOpenGLFunctions();
    if (!m_glFunctionsReady) {
        m_glFallbackMode = true;
        m_glInitialized = false;
        m_glErrorMessage = locale::GerenciadorTraducao::trText(
            "Contexto OpenGL 3.3 Core indisponível nesta sessão (driver ou área remota sem aceleração).");
        io::AppLogger::critical(QStringLiteral("Viewport3D"), m_glErrorMessage);
        return;
    }

    auto glStr = [this](GLenum name) -> QString {
        const GLubyte* s = glGetString(name);
        return s ? QString::fromUtf8(reinterpret_cast<const char*>(s)) : QStringLiteral("-");
    };
    m_glRenderer = glStr(GL_RENDERER);
    m_glVendor = glStr(GL_VENDOR);
    m_glVersion = glStr(GL_VERSION);

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    GLint maxColorSamples = 0;
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &maxColorSamples);
    GLint maxDepthSamples = 0;
    glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &maxDepthSamples);

    int combinedMaxSamples = maxSamples;
    if (maxColorSamples > 0) combinedMaxSamples = std::min(combinedMaxSamples, static_cast<int>(maxColorSamples));
    if (maxDepthSamples > 0) combinedMaxSamples = std::min(combinedMaxSamples, static_cast<int>(maxDepthSamples));
    m_glMaxSamples = std::max(0, combinedMaxSamples);

    GLint maxTex = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    m_glMaxTexSize = std::max(512, static_cast<int>(maxTex));

    GLfloat lineRange[2] = {1.0f, 1.0f};
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, lineRange);
    m_glMaxRawLineWidth = std::max(1.0f, lineRange[1]);

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    glClearColor(pal.clearColor.x(), pal.clearColor.y(), pal.clearColor.z(), 1.0f);

    initShaders();
    if (!m_lineShader || !m_lineShader->isLinked() || !m_meshShader || !m_meshShader->isLinked()) {
        m_glFallbackMode = true;
        m_glErrorMessage = locale::GerenciadorTraducao::trText(
            "Falha ao compilar shaders OpenGL 3.3 Core essenciais; renderizador 3D desabilitado.");
        cleanupGL();
        return;
    }

    initBuffers();
    m_glFallbackMode = false;
    m_glErrorMessage.clear();
    m_glInitialized = true;
    m_vsyncDirty = true;
    m_geometryDirty = true;
    m_gridDirty = true;
}

std::unique_ptr<QOpenGLShaderProgram> Viewport3D::compileShaderProgram(const char* name,
                                                                       const char* vertexSrc,
                                                                       const char* fragmentSrc) {
    auto prog = std::make_unique<QOpenGLShaderProgram>();
    if (!prog->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSrc)) {
        io::AppLogger::critical(
            QStringLiteral("Viewport3D"),
            QStringLiteral("Erro compilando Vertex Shader (%1): %2").arg(QLatin1StringView(name), prog->log()));
        return nullptr;
    }
    if (!prog->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSrc)) {
        io::AppLogger::critical(
            QStringLiteral("Viewport3D"),
            QStringLiteral("Erro compilando Fragment Shader (%1): %2").arg(QLatin1StringView(name), prog->log()));
        return nullptr;
    }
    if (!prog->link() || !prog->isLinked()) {
        io::AppLogger::critical(
            QStringLiteral("Viewport3D"),
            QStringLiteral("Erro linkando Shader Program (%1): %2").arg(QLatin1StringView(name), prog->log()));
        return nullptr;
    }
    return prog;
}

bool Viewport3D::validateVboByteSize(std::size_t elemCount,
                                       std::size_t elemBytes,
                                       int* outBytes) noexcept {
    if (outBytes) {
        *outBytes = 0;
    }
    if (elemCount == 0 || elemBytes == 0) {
        return true;
    }
    const std::size_t maxElems = static_cast<std::size_t>(std::numeric_limits<int>::max()) / elemBytes;
    if (elemCount > maxElems) {
        return false;
    }
    if (outBytes) {
        *outBytes = static_cast<int>(elemCount * elemBytes);
    }
    return true;
}

bool Viewport3D::safeAllocateVbo(QOpenGLBuffer& vbo,
                                 const void* srcData,
                                 std::size_t elemCount,
                                 std::size_t elemBytes) {
    if (elemCount == 0 || elemBytes == 0) {
        return true;
    }
    int byteCount = 0;
    if (!validateVboByteSize(elemCount, elemBytes, &byteCount)) {
        io::AppLogger::critical(
            QStringLiteral("Viewport3D"),
            QStringLiteral("Overflow de buffer VBO evitado: %1 elementos * %2 bytes excede INT_MAX.")
                .arg(static_cast<qulonglong>(elemCount))
                .arg(static_cast<qulonglong>(elemBytes)));
        return false;
    }
    if (srcData == nullptr) {
        return true;
    }
    vbo.allocate(srcData, byteCount);
    return true;
}

bool Viewport3D::canReuseCachedFboRequest(int lastW,
                                          int lastH,
                                          qreal lastDpr,
                                          float lastScale,
                                          int lastSamples,
                                          int reqW,
                                          int reqH,
                                          qreal reqDpr,
                                          float reqScale,
                                          int reqSamples) noexcept {
    return lastW == reqW &&
           lastH == reqH &&
           std::abs(lastDpr - reqDpr) < 1e-4 &&
           std::abs(lastScale - reqScale) < 1e-4f &&
           lastSamples == reqSamples;
}

void Viewport3D::initShaders() {
    m_lineShader = compileShaderProgram("line", shaders::LINE_VERTEX_SHADER, shaders::LINE_FRAGMENT_SHADER);
    m_aaLineShader = compileShaderProgram("aaLine", shaders::AA_LINE_VERTEX_SHADER, shaders::AA_LINE_FRAGMENT_SHADER);
    m_meshShader = compileShaderProgram("mesh", shaders::MESH_VERTEX_SHADER, shaders::MESH_FRAGMENT_SHADER);
    m_instancedMeshShader = compileShaderProgram(
        "instancedMesh", shaders::INSTANCED_MESH_VERTEX_SHADER, shaders::MESH_FRAGMENT_SHADER);
    m_diagFillShader = compileShaderProgram("diagFill", shaders::DIAGRAM_FILL_VERTEX_SHADER, shaders::DIAGRAM_FILL_FRAGMENT_SHADER);
    m_resolveShader = compileShaderProgram("resolve", shaders::RESOLVE_VERTEX_SHADER, shaders::RESOLVE_FRAGMENT_SHADER);
}

void Viewport3D::initBuffers() {
    // Line VAO/VBO (Pos 3f, Color 4f = 7 floats)
    m_lineVAO.create();
    m_lineVAO.bind();
    m_lineVBO.create();
    m_lineVBO.bind();
    m_lineVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    m_lineVAO.release();
    m_lineVBO.release();

    // AA Line Quad VAO/VBO (PosA 3f, PosB 3f, ColorA 4f, ColorB 4f, Corner 2f = 16 floats)
    m_aaLineVAO.create();
    m_aaLineVAO.bind();
    m_aaLineVBO.create();
    m_aaLineVBO.bind();
    m_aaLineVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void*>(10 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void*>(14 * sizeof(float)));
    m_aaLineVAO.release();
    m_aaLineVBO.release();

    // Mesh VAO/VBO (Pos 3f, Normal 3f, Color 4f = 10 floats)
    m_meshVAO.create();
    m_meshVAO.bind();
    m_meshVBO.create();
    m_meshVBO.bind();
    m_meshVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    m_meshVAO.release();
    m_meshVBO.release();

    // Instanced Node VAO (Unit sphere Pos3+Norm3 divisor 0 + Instance CenterRadius4+Color4 divisor 1)
    std::vector<float> unitSphereVerts;
    buildUnitSphereMesh(unitSphereVerts, 10, 12);
    m_unitSphereVertexCount = static_cast<GLsizei>(unitSphereVerts.size() / 6);

    m_nodeInstVAO.create();
    m_nodeInstVAO.bind();
    m_unitSphereVBO.create();
    m_unitSphereVBO.bind();
    m_unitSphereVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    safeAllocateVbo(m_unitSphereVBO, unitSphereVerts.data(), unitSphereVerts.size(), sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribDivisor(1, 0);

    m_nodeInstanceVBO.create();
    m_nodeInstanceVBO.bind();
    m_nodeInstanceVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));
    glVertexAttribDivisor(3, 1);
    m_nodeInstVAO.release();
    m_nodeInstanceVBO.release();
    m_unitSphereVBO.release();

    // Diagram Fill VAO/VBO (Pos 3f, Color 4f = 7 floats)
    m_diagVAO.create();
    m_diagVAO.bind();
    m_diagVBO.create();
    m_diagVBO.bind();
    m_diagVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    m_diagVAO.release();
    m_diagVBO.release();

    // Fullscreen Resolve Quad VAO/VBO (Pos 2f, UV 2f = 4 floats)
    static const std::array<float, 24> kQuadVertices = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f
    };
    m_quadVAO.create();
    m_quadVAO.bind();
    m_quadVBO.create();
    m_quadVBO.bind();
    m_quadVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    safeAllocateVbo(m_quadVBO, kQuadVertices.data(), kQuadVertices.size(), sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    m_quadVAO.release();
    m_quadVBO.release();
}

void Viewport3D::applyVsyncState() {
    m_vsyncDirty = false;
    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

#ifdef _WIN32
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
    using SwapIntervalExtFn = int (APIENTRY*)(int);
    if (auto proc = ctx->getProcAddress("wglSwapIntervalEXT")) {
        auto swapIntervalFn = reinterpret_cast<SwapIntervalExtFn>(proc);
        swapIntervalFn(m_renderSettings.vsyncEnabled ? 1 : 0);
    }
#endif
}

float Viewport3D::computeEffectiveSupersample(int logicalW, int logicalH, qreal dpr) const {
    if (!m_renderSettings.autoMaxResolution) {
        return std::clamp(m_renderSettings.resolutionScale, 0.75f, 2.5f);
    }

    const qreal nativeW = std::max(1, logicalW) * std::max<qreal>(1.0, dpr);
    const qreal nativeH = std::max(1, logicalH) * std::max<qreal>(1.0, dpr);
    const qreal maxDim = std::max(nativeW, nativeH);

    // Otimiza SSAA automatico para nitidez maxima dentro dos limites de VRAM/GPU
    if (maxDim <= 2560.0) {
        return 2.0f;
    }
    if (maxDim <= 3840.0) {
        return 1.5f;
    }
    return 1.25f;
}

int Viewport3D::resolveTargetMsaaSamples() const {
    if (m_glMaxSamples <= 1) {
        return 0;
    }
    const int wanted = m_renderSettings.autoMsaa ? 16 : m_renderSettings.requestedMsaa;
    if (wanted <= 1) {
        return 0;
    }
    return std::min(wanted, m_glMaxSamples);
}

void Viewport3D::destroyRenderTargets() {
    if (m_msaaFbo != 0) {
        glDeleteFramebuffers(1, &m_msaaFbo);
        m_msaaFbo = 0;
    }
    if (m_msaaColorRbo != 0) {
        glDeleteRenderbuffers(1, &m_msaaColorRbo);
        m_msaaColorRbo = 0;
    }
    if (m_msaaDepthRbo != 0) {
        glDeleteRenderbuffers(1, &m_msaaDepthRbo);
        m_msaaDepthRbo = 0;
    }
    if (m_resolveFbo != 0) {
        glDeleteFramebuffers(1, &m_resolveFbo);
        m_resolveFbo = 0;
    }
    if (m_resolveColorTex != 0) {
        glDeleteTextures(1, &m_resolveColorTex);
        m_resolveColorTex = 0;
    }
    if (m_resolveDepthRbo != 0) {
        glDeleteRenderbuffers(1, &m_resolveDepthRbo);
        m_resolveDepthRbo = 0;
    }
    m_fboWidth = 0;
    m_fboHeight = 0;
    m_activeMsaaSamples = 0;
}

bool Viewport3D::ensureRenderTargets(int logicalW, int logicalH, qreal dpr) {
    if (logicalW <= 0 || logicalH <= 0 || !m_resolveShader || !m_resolveShader->isLinked()) {
        destroyRenderTargets();
        m_fboLastRequestedW = 0;
        m_fboLastRequestedH = 0;
        m_fboLastRequestedDpr = 0.0;
        m_fboLastRequestedScale = 0.0f;
        m_fboLastRequestedSamples = -1;
        m_fboFallbackExhausted = false;
        return false;
    }

    const float desiredScale = computeEffectiveSupersample(logicalW, logicalH, dpr);
    const int desiredSamples = resolveTargetMsaaSamples();
    const int maxDim = std::min(m_glMaxTexSize, 7680);

    const int desiredW = std::clamp(static_cast<int>(std::lround(logicalW * dpr * desiredScale)), 1, maxDim);
    const int desiredH = std::clamp(static_cast<int>(std::lround(logicalH * dpr * desiredScale)), 1, maxDim);

    if (m_resolveFbo != 0 &&
        m_fboWidth == desiredW &&
        m_fboHeight == desiredH &&
        ((desiredSamples <= 1 && m_msaaFbo == 0) || (desiredSamples > 1 && m_msaaFbo != 0 && m_activeMsaaSamples == desiredSamples))) {
        m_activeSupersample = desiredScale;
        return true;
    }

    if (canReuseCachedFboRequest(m_fboLastRequestedW,
                                 m_fboLastRequestedH,
                                 m_fboLastRequestedDpr,
                                 m_fboLastRequestedScale,
                                 m_fboLastRequestedSamples,
                                 logicalW,
                                 logicalH,
                                 dpr,
                                 desiredScale,
                                 desiredSamples)) {
        return !m_fboFallbackExhausted && m_resolveFbo != 0;
    }

    const GLuint fallbackTargetFbo = defaultFramebufferObject();

    auto tryAllocate = [&](float tryScale, int trySamples) -> bool {
        destroyRenderTargets();

        const int w = std::clamp(static_cast<int>(std::lround(logicalW * dpr * tryScale)), 1, maxDim);
        const int h = std::clamp(static_cast<int>(std::lround(logicalH * dpr * tryScale)), 1, maxDim);

        glGenFramebuffers(1, &m_resolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);

        glGenTextures(1, &m_resolveColorTex);
        glBindTexture(GL_TEXTURE_2D, m_resolveColorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveColorTex, 0);

        if (trySamples <= 1) {
            glGenRenderbuffers(1, &m_resolveDepthRbo);
            glBindRenderbuffer(GL_RENDERBUFFER, m_resolveDepthRbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_resolveDepthRbo);
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, fallbackTargetFbo);
            destroyRenderTargets();
            return false;
        }

        if (trySamples > 1) {
            glGenFramebuffers(1, &m_msaaFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);

            glGenRenderbuffers(1, &m_msaaColorRbo);
            glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRbo);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, trySamples, GL_RGBA8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColorRbo);

            glGenRenderbuffers(1, &m_msaaDepthRbo);
            glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRbo);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, trySamples, GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRbo);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                glBindFramebuffer(GL_FRAMEBUFFER, fallbackTargetFbo);
                destroyRenderTargets();
                return false;
            }
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fallbackTargetFbo);
        m_fboWidth = w;
        m_fboHeight = h;
        m_activeMsaaSamples = (m_msaaFbo != 0) ? trySamples : 0;
        m_activeSupersample = tryScale;
        return true;
    };

    m_fboLastRequestedW = logicalW;
    m_fboLastRequestedH = logicalH;
    m_fboLastRequestedDpr = dpr;
    m_fboLastRequestedScale = desiredScale;
    m_fboLastRequestedSamples = desiredSamples;
    m_fboFallbackExhausted = false;
    ++m_fboAllocationAttemptCount;

    const std::array<float, 3> scalesToTry = {desiredScale, std::min(desiredScale, 1.5f), 1.0f};
    for (float sc : scalesToTry) {
        int s = desiredSamples;
        while (true) {
            if (tryAllocate(sc, s)) {
                return true;
            }
            if (s <= 0) break;
            s = (s >= 16) ? 8 : (s >= 8 ? 4 : (s >= 4 ? 2 : 0));
        }
    }

    m_activeSupersample = 1.0f;
    m_activeMsaaSamples = 0;
    m_fboFallbackExhausted = true;
    return false;
}

void Viewport3D::resizeGL(int w, int h) {
    if (!isOpenGLReady()) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0,
               std::max(1, static_cast<int>(std::lround(w * dpr))),
               std::max(1, static_cast<int>(std::lround(h * dpr))));
}

void Viewport3D::paintEvent(QPaintEvent* event) {
    QOpenGLWidget::paintEvent(event);
    const QOpenGLContext* ctx = context();
    if (!isOpenGLReady() && (ctx == nullptr || !ctx->isValid())) {
        const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
        QPainter painter(this);
        renderGlFallbackNotice(painter, rect(), pal, m_glErrorMessage);
        painter.end();
    }
}

void Viewport3D::paintGL() {
    const auto frameStart = std::chrono::steady_clock::now();
    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);

    if (!isOpenGLReady()) {
        QPainter painter(this);
        renderGlFallbackNotice(painter, rect(), pal, m_glErrorMessage);
        painter.end();
        m_lastPaintGlMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
        return;
    }

    QPainter painter(this);
    painter.beginNativePainting();

    if (m_vsyncDirty) {
        applyVsyncState();
    }

    // QPainter::beginNativePainting pode deixar scissor/stencil ativos no viewport lógico
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    const GLuint widgetFbo = defaultFramebufferObject();
    const qreal dpr = devicePixelRatioF();
    const int widgetPxW = std::max(1, static_cast<int>(std::lround(width() * dpr)));
    const int widgetPxH = std::max(1, static_cast<int>(std::lround(height() * dpr)));

    ensureGridCache();
    ensureSceneCache();

    m_legendVisible = m_cachedLegendVisible;
    m_legendTitle = m_cachedLegendTitle;
    m_legendUnit = m_cachedLegendUnit;
    m_legendMin = m_cachedLegendMin;
    m_legendMax = m_cachedLegendMax;
    m_legendIsDcr = m_cachedLegendIsDcr;

    const bool hasCustomFbo = ensureRenderTargets(width(), height(), dpr);
    if (hasCustomFbo) {
        const GLuint sceneFbo = (m_msaaFbo != 0) ? m_msaaFbo : m_resolveFbo;
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        glViewport(0, 0, m_fboWidth, m_fboHeight);
    } else {
        m_fboWidth = widgetPxW;
        m_fboHeight = widgetPxH;
        m_activeSupersample = 1.0f;
        glBindFramebuffer(GL_FRAMEBUFFER, widgetFbo);
        glViewport(0, 0, widgetPxW, widgetPxH);
    }

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(pal.clearColor.x(), pal.clearColor.y(), pal.clearColor.z(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_CLIP_DISTANCE0);

    const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
    const QMatrix4x4 mvp = m_camera.viewProjectionMatrix(aspect);
    const QMatrix4x4 view = m_camera.viewMatrix();

    // Grid e planos de trabalho nunca são recortados pelo plano de corte 3D
    m_suppressClipPlaneForDraw = true;
    renderGrid(mvp);
    m_suppressClipPlaneForDraw = false;

    const bool hasModelContent = m_model &&
        (!m_model->getNodes().empty() ||
         !m_model->getElements().empty() ||
         !m_model->surfaceMesh().elements.empty());
    const bool clipActive = hasModelContent && (m_clipAxis != ClipAxis::None);
    if (clipActive) {
        glEnable(GL_CLIP_DISTANCE0);
    }

    if (hasModelContent) {
        if (m_showSurfaceMesh && !m_model->surfaceMesh().elements.empty()) {
            renderSurfaceMesh(mvp);
        }

        if (m_activeDiagram == DiagramType::Deformed || hasPostAnimation()) {
            renderElements(mvp);
            renderDeformedShape(mvp);
        } else {
            if (m_activeDiagram != DiagramType::None) {
                renderDiagrams(mvp);
            }
            renderElements(mvp);
        }

        renderSupports(mvp, view);
        renderNodes(mvp);
        renderLoads(mvp, view);
    }

    if (clipActive) {
        glDisable(GL_CLIP_DISTANCE0);
    }
    m_suppressClipPlaneForDraw = true;
    renderCursorPreview(mvp);
    m_suppressClipPlaneForDraw = false;

    if (hasCustomFbo && m_resolveShader && m_resolveShader->isLinked()) {
        if (m_msaaFbo != 0) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
            glBlitFramebuffer(0, 0, m_fboWidth, m_fboHeight,
                              0, 0, m_fboWidth, m_fboHeight,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, widgetFbo);
        glViewport(0, 0, widgetPxW, widgetPxH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        m_resolveShader->bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_resolveColorTex);
        m_resolveShader->setUniformValue("uSceneTex", 0);
        m_resolveShader->setUniformValue("uTexSize", QVector2D(static_cast<float>(m_fboWidth),
                                                               static_cast<float>(m_fboHeight)));
        m_resolveShader->setUniformValue("uFilterMode", static_cast<int>(m_renderSettings.downsampleFilter));
        m_resolveShader->setUniformValue("uSharpen", m_renderSettings.sharpenStrength);

        m_quadVAO.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_quadVAO.release();

        glBindTexture(GL_TEXTURE_2D, 0);
        m_resolveShader->release();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, widgetFbo);
        glViewport(0, 0, widgetPxW, widgetPxH);
    }

    // Restaura estado OpenGL limpo antes de encerrar native painting e desenhar HUD via QPainter
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CLIP_DISTANCE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);

    painter.endNativePainting();
    renderHUD(painter);
    painter.end();

    m_lastPaintGlMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
}

void Viewport3D::drawLinesAA(const QMatrix4x4& mvp,
                             const std::vector<float>& segmentVertices7f,
                             float logicalWidthPx,
                             bool depthWrite,
                             bool splitOpaquePasses) {
    const std::size_t numSegments = segmentVertices7f.size() / 14;
    if (numSegments == 0) return;

    const int curW = (m_fboWidth > 0) ? m_fboWidth : std::max(1, width());
    const int curH = (m_fboHeight > 0) ? m_fboHeight : std::max(1, height());
    const float pixelRatio = (width() > 0)
        ? (static_cast<float>(curW) / static_cast<float>(width()))
        : 1.0f;

    if (!m_renderSettings.shaderLineAA || !m_aaLineShader || !m_aaLineShader->isLinked()) {
        if (!m_lineShader || !m_lineShader->isLinked()) return;
        m_lineVAO.bind();
        m_lineVBO.bind();
        if (!safeAllocateVbo(m_lineVBO, segmentVertices7f.data(), numSegments * 14, sizeof(float))) {
            m_lineVAO.release();
            return;
        }
        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
        glLineWidth(std::clamp(logicalWidthPx * m_renderSettings.lineWidthMultiplier, 1.0f, m_glMaxRawLineWidth));
        m_lineShader->bind();
        m_lineShader->setUniformValue("uMVP", mvp);
        m_lineShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(numSegments * 2));
        m_lineVAO.release();
        m_lineShader->release();
        glLineWidth(1.0f);
        glDepthMask(GL_TRUE);
        return;
    }

    std::vector<float> quadVerts;
    expandSegmentsToAAQuads(segmentVertices7f, quadVerts);
    if (quadVerts.empty()) return;

    m_aaLineVAO.bind();
    m_aaLineVBO.bind();
    if (!safeAllocateVbo(m_aaLineVBO, quadVerts.data(), quadVerts.size(), sizeof(float))) {
        m_aaLineVAO.release();
        return;
    }

    const float strokePx = std::max(0.65f, logicalWidthPx * m_renderSettings.lineWidthMultiplier * pixelRatio);
    const float featherPx = std::max(0.65f, m_renderSettings.lineSoftness * std::max(1.0f, pixelRatio * 0.85f));
    const GLsizei vertexCount = static_cast<GLsizei>(numSegments * 6);

    m_aaLineShader->bind();
    m_aaLineShader->setUniformValue("uMVP", mvp);
    m_aaLineShader->setUniformValue("uResolution", QVector2D(static_cast<float>(curW), static_cast<float>(curH)));
    m_aaLineShader->setUniformValue("uStrokePx", strokePx);
    m_aaLineShader->setUniformValue("uFeatherPx", featherPx);
    m_aaLineShader->setUniformValue("uClipPlane", activeClipPlaneVec());

    if (splitOpaquePasses && depthWrite) {
        glDepthMask(GL_TRUE);
        m_aaLineShader->setUniformValue("uPassMode", 1);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);

        glDepthMask(GL_FALSE);
        m_aaLineShader->setUniformValue("uPassMode", 2);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);

        glDepthMask(GL_TRUE);
    } else {
        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
        m_aaLineShader->setUniformValue("uPassMode", 0);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        glDepthMask(GL_TRUE);
    }

    m_aaLineVAO.release();
    m_aaLineShader->release();
}

void Viewport3D::uploadLineCache(CachedLineBuffer& cache, const std::vector<float>& segVerts7f) {
    const std::size_t numSegments = segVerts7f.size() / 14;
    if (numSegments == 0) {
        cache.lineVertexCount = 0;
        cache.aaVertexCount = 0;
        cache.builtWithShaderAA = m_renderSettings.shaderLineAA;
        return;
    }

    if (m_renderSettings.shaderLineAA) {
        if (!cache.aaVao.isCreated()) {
            cache.aaVao.create();
            cache.aaVao.bind();
            cache.aaVbo.create();
            cache.aaVbo.bind();
            cache.aaVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16 * sizeof(float), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void*>(10 * sizeof(float)));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<void*>(14 * sizeof(float)));
            cache.aaVao.release();
            cache.aaVbo.release();
        }
        std::vector<float> quadVerts;
        expandSegmentsToAAQuads(segVerts7f, quadVerts);
        cache.aaVao.bind();
        cache.aaVbo.bind();
        if (safeAllocateVbo(cache.aaVbo, quadVerts.data(), quadVerts.size(), sizeof(float))) {
            cache.aaVertexCount = static_cast<GLsizei>(numSegments * 6);
        } else {
            cache.aaVertexCount = 0;
        }
        cache.aaVao.release();
        cache.aaVbo.release();
        cache.lineVertexCount = 0;
        cache.builtWithShaderAA = true;
    } else {
        if (!cache.lineVao.isCreated()) {
            cache.lineVao.create();
            cache.lineVao.bind();
            cache.lineVbo.create();
            cache.lineVbo.bind();
            cache.lineVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
            cache.lineVao.release();
            cache.lineVbo.release();
        }
        cache.lineVao.bind();
        cache.lineVbo.bind();
        if (safeAllocateVbo(cache.lineVbo, segVerts7f.data(), numSegments * 14, sizeof(float))) {
            cache.lineVertexCount = static_cast<GLsizei>(numSegments * 2);
        } else {
            cache.lineVertexCount = 0;
        }
        cache.lineVao.release();
        cache.lineVbo.release();
        cache.aaVertexCount = 0;
        cache.builtWithShaderAA = false;
    }
}

void Viewport3D::uploadMeshCache(CachedMeshBuffer& cache, const std::vector<float>& meshVerts10f) {
    if (meshVerts10f.empty()) {
        cache.vertexCount = 0;
        return;
    }
    if (!cache.vao.isCreated()) {
        cache.vao.create();
        cache.vao.bind();
        cache.vbo.create();
        cache.vbo.bind();
        cache.vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
        cache.vao.release();
        cache.vbo.release();
    }
    cache.vao.bind();
    cache.vbo.bind();
    if (safeAllocateVbo(cache.vbo, meshVerts10f.data(), meshVerts10f.size(), sizeof(float))) {
        cache.vertexCount = static_cast<GLsizei>(meshVerts10f.size() / 10);
    } else {
        cache.vertexCount = 0;
    }
    cache.vao.release();
    cache.vbo.release();
}

void Viewport3D::uploadDiagFillCache(CachedDiagFillBuffer& cache, const std::vector<float>& diagVerts7f) {
    if (diagVerts7f.empty()) {
        cache.vertexCount = 0;
        return;
    }
    if (!cache.vao.isCreated()) {
        cache.vao.create();
        cache.vao.bind();
        cache.vbo.create();
        cache.vbo.bind();
        cache.vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        cache.vao.release();
        cache.vbo.release();
    }
    cache.vao.bind();
    cache.vbo.bind();
    if (safeAllocateVbo(cache.vbo, diagVerts7f.data(), diagVerts7f.size(), sizeof(float))) {
        cache.vertexCount = static_cast<GLsizei>(diagVerts7f.size() / 7);
    } else {
        cache.vertexCount = 0;
    }
    cache.vao.release();
    cache.vbo.release();
}

void Viewport3D::drawCachedLines(const QMatrix4x4& mvp,
                                 CachedLineBuffer& cache,
                                 float logicalWidthPx,
                                 bool depthWrite,
                                 bool splitOpaquePasses) {
    const int curW = (m_fboWidth > 0) ? m_fboWidth : std::max(1, width());
    const int curH = (m_fboHeight > 0) ? m_fboHeight : std::max(1, height());
    const float pixelRatio = (width() > 0)
        ? (static_cast<float>(curW) / static_cast<float>(width()))
        : 1.0f;

    if (cache.builtWithShaderAA && cache.aaVertexCount > 0 && m_aaLineShader && m_aaLineShader->isLinked()) {
        const float strokePx = std::max(0.65f, logicalWidthPx * m_renderSettings.lineWidthMultiplier * pixelRatio);
        const float featherPx = std::max(0.65f, m_renderSettings.lineSoftness * std::max(1.0f, pixelRatio * 0.85f));

        m_aaLineShader->bind();
        m_aaLineShader->setUniformValue("uMVP", mvp);
        m_aaLineShader->setUniformValue("uResolution", QVector2D(static_cast<float>(curW), static_cast<float>(curH)));
        m_aaLineShader->setUniformValue("uStrokePx", strokePx);
        m_aaLineShader->setUniformValue("uFeatherPx", featherPx);
        m_aaLineShader->setUniformValue("uClipPlane", activeClipPlaneVec());

        cache.aaVao.bind();
        if (splitOpaquePasses && depthWrite) {
            glDepthMask(GL_TRUE);
            m_aaLineShader->setUniformValue("uPassMode", 1);
            glDrawArrays(GL_TRIANGLES, 0, cache.aaVertexCount);

            glDepthMask(GL_FALSE);
            m_aaLineShader->setUniformValue("uPassMode", 2);
            glDrawArrays(GL_TRIANGLES, 0, cache.aaVertexCount);

            glDepthMask(GL_TRUE);
        } else {
            glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
            m_aaLineShader->setUniformValue("uPassMode", 0);
            glDrawArrays(GL_TRIANGLES, 0, cache.aaVertexCount);
            glDepthMask(GL_TRUE);
        }
        cache.aaVao.release();
        m_aaLineShader->release();
        return;
    }

    if (cache.lineVertexCount > 0 && m_lineShader && m_lineShader->isLinked()) {
        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
        glLineWidth(std::clamp(logicalWidthPx * m_renderSettings.lineWidthMultiplier, 1.0f, m_glMaxRawLineWidth));
        m_lineShader->bind();
        m_lineShader->setUniformValue("uMVP", mvp);
        m_lineShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        cache.lineVao.bind();
        glDrawArrays(GL_LINES, 0, cache.lineVertexCount);
        cache.lineVao.release();
        m_lineShader->release();
        glLineWidth(1.0f);
        glDepthMask(GL_TRUE);
    }
}

void Viewport3D::drawCachedMesh(const QMatrix4x4& mvp, CachedMeshBuffer& cache) {
    if (cache.vertexCount <= 0 || !m_meshShader || !m_meshShader->isLinked()) {
        return;
    }
    m_meshShader->bind();
    m_meshShader->setUniformValue("uMVP", mvp);
    m_meshShader->setUniformValue("uModel", QMatrix4x4());
    m_meshShader->setUniformValue("uNormalMatrix", QMatrix3x3());
    m_meshShader->setUniformValue("uClipPlane", activeClipPlaneVec());
    m_meshShader->setUniformValue("uLightDir", QVector3D(0.4f, 0.5f, 1.0f).normalized());
    m_meshShader->setUniformValue("uViewPos", m_camera.eyePosition());

    cache.vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, cache.vertexCount);
    cache.vao.release();
    m_meshShader->release();
}

void Viewport3D::drawCachedDiagFill(const QMatrix4x4& mvp, CachedDiagFillBuffer& cache) {
    if (cache.vertexCount <= 0 || !m_diagFillShader || !m_diagFillShader->isLinked()) {
        return;
    }
    glDepthMask(GL_FALSE);
    m_diagFillShader->bind();
    m_diagFillShader->setUniformValue("uMVP", mvp);
    m_diagFillShader->setUniformValue("uClipPlane", activeClipPlaneVec());
    cache.vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, cache.vertexCount);
    cache.vao.release();
    m_diagFillShader->release();
    glDepthMask(GL_TRUE);
}

void Viewport3D::ensureGridCache() {
    const GridCacheKey key{
        m_workPlane,
        m_planeOffset,
        m_gridStep,
        m_renderSettings.planeTheme,
        m_renderSettings.planeGridContrast,
        m_renderSettings.showActivePlaneSurface,
        m_renderSettings.shaderLineAA
    };
    if (!m_gridDirty && m_gridCacheKey == key) {
        return;
    }
    m_gridCacheKey = key;
    m_gridDirty = false;

    const int gridExtent = 15;
    const float step = static_cast<float>(m_gridStep);
    const float offset = static_cast<float>(m_planeOffset);
    const float minC = -gridExtent * step;
    const float maxC = gridExtent * step;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);

    auto mapPlanePoint = [&](float u, float v) -> QVector3D {
        switch (m_workPlane) {
            case WorkPlane::XZ_Frontal: return QVector3D(u, offset, v);
            case WorkPlane::XY_Top:     return QVector3D(u, v, offset);
            case WorkPlane::YZ_Side:    return QVector3D(offset, u, v);
        }
        return QVector3D(u, offset, v);
    };

    if (m_renderSettings.showActivePlaneSurface) {
        const QVector3D c00 = mapPlanePoint(minC, minC);
        const QVector3D c10 = mapPlanePoint(maxC, minC);
        const QVector3D c11 = mapPlanePoint(maxC, maxC);
        const QVector3D c01 = mapPlanePoint(minC, maxC);
        const QVector4D fc = pal.planeFillColor;

        const std::vector<float> surfaceTris = {
            c00.x(), c00.y(), c00.z(), fc.x(), fc.y(), fc.z(), fc.w(),
            c10.x(), c10.y(), c10.z(), fc.x(), fc.y(), fc.z(), fc.w(),
            c11.x(), c11.y(), c11.z(), fc.x(), fc.y(), fc.z(), fc.w(),

            c00.x(), c00.y(), c00.z(), fc.x(), fc.y(), fc.z(), fc.w(),
            c11.x(), c11.y(), c11.z(), fc.x(), fc.y(), fc.z(), fc.w(),
            c01.x(), c01.y(), c01.z(), fc.x(), fc.y(), fc.z(), fc.w()
        };
        uploadDiagFillCache(m_gridSurfaceCache, surfaceTris);
    } else {
        m_gridSurfaceCache.vertexCount = 0;
    }

    std::vector<float> minorLines;
    std::vector<float> majorLines;
    std::vector<float> borderLines;
    std::vector<float> axisLines;

    minorLines.reserve((gridExtent * 4 + 4) * 14);
    majorLines.reserve((gridExtent + 4) * 14);

    auto appendSeg = [](std::vector<float>& dst, const QVector3D& a, const QVector3D& b, const QVector4D& c) {
        dst.insert(dst.end(), {
            a.x(), a.y(), a.z(), c.x(), c.y(), c.z(), c.w(),
            b.x(), b.y(), b.z(), c.x(), c.y(), c.z(), c.w()
        });
    };

    for (int i = -gridExtent; i <= gridExtent; ++i) {
        const float pos = static_cast<float>(i) * step;
        const bool isBoundary = (i == -gridExtent || i == gridExtent);
        const bool isMajor = (i % 5 == 0);

        const QVector3D p1 = mapPlanePoint(pos, minC);
        const QVector3D p2 = mapPlanePoint(pos, maxC);
        const QVector3D q1 = mapPlanePoint(minC, pos);
        const QVector3D q2 = mapPlanePoint(maxC, pos);

        if (isBoundary && m_renderSettings.showActivePlaneSurface) {
            appendSeg(borderLines, p1, p2, pal.planeBorderColor);
            appendSeg(borderLines, q1, q2, pal.planeBorderColor);
        } else if (isMajor) {
            appendSeg(majorLines, p1, p2, pal.majorGridColor);
            appendSeg(majorLines, q1, q2, pal.majorGridColor);
        } else {
            appendSeg(minorLines, p1, p2, pal.minorGridColor);
            appendSeg(minorLines, q1, q2, pal.minorGridColor);
        }
    }

    const float axLen = 3.2f;
    appendSeg(axisLines, {0.0f, 0.0f, 0.0f}, {axLen, 0.0f, 0.0f}, pal.axisXColor);
    appendSeg(axisLines, {0.0f, 0.0f, 0.0f}, {0.0f, axLen, 0.0f}, pal.axisYColor);
    appendSeg(axisLines, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, axLen}, pal.axisZColor);

    uploadLineCache(m_gridMinorCache, minorLines);
    uploadLineCache(m_gridMajorCache, majorLines);
    uploadLineCache(m_gridBorderCache, borderLines);
    uploadLineCache(m_gridAxisCache, axisLines);
}

void Viewport3D::rebuildDiagramEvalCache() {
    m_diagramCache.bars.clear();
    m_diagramCache.maxAbsVal = 1e-6;
    m_diagramCache.model = m_model;
    m_diagramCache.geometryRevision = m_model ? m_model->geometryRevision() : 0;
    m_diagramCache.diagramType = m_activeDiagram;
    m_diagramCache.valid = true;

    if (!m_model || m_activeDiagram == DiagramType::None || m_activeDiagram == DiagramType::Deformed) {
        return;
    }

    const auto& elems = m_model->getElements();
    m_diagramCache.bars.reserve(elems.size());

    for (const auto& elem : elems) {
        if (!elem.hasResults) continue;
        const Node* n1 = m_model->findNode(elem.nodeStart);
        const Node* n2 = m_model->findNode(elem.nodeEnd);
        if (!n1 || !n2) continue;

        const Eigen::Matrix3d R = elem.computeRotationMatrix3x3(*m_model);
        CachedBarDiagramData bar;
        bar.baseStart = n1->position;
        bar.baseEnd = n2->position;
        if (m_activeDiagram == DiagramType::Moment_My || m_activeDiagram == DiagramType::Shear_Vz) {
            bar.plotDir = {R(2, 0), R(2, 1), R(2, 2)};
        } else {
            bar.plotDir = {R(1, 0), R(1, 1), R(1, 2)};
        }

        for (int s = 0; s <= 32; ++s) {
            const double xi = static_cast<double>(s) / 32.0;
            const InternalForces f = elem.evaluateAt(xi, *m_model);
            double v = 0.0;
            switch (m_activeDiagram) {
                case DiagramType::Normal_N:   v = f.N; break;
                case DiagramType::Shear_Vy:  v = f.Vy; break;
                case DiagramType::Shear_Vz:  v = f.Vz; break;
                case DiagramType::Torsion_Tx: v = f.Tx; break;
                case DiagramType::Moment_My:  v = f.My; break;
                case DiagramType::Moment_Mz:  v = f.Mz; break;
                default: break;
            }
            if (!std::isfinite(v)) v = 0.0;
            bar.values[static_cast<std::size_t>(s)] = v;
            m_diagramCache.maxAbsVal = std::max(m_diagramCache.maxAbsVal, std::abs(v));
        }

        m_diagramCache.bars.push_back(bar);
    }
}

void Viewport3D::ensureSceneCache() {
    const std::uint64_t modelRev = m_model ? m_model->geometryRevision() : 0;
    const SceneCacheKey key{
        m_model,
        modelRev,
        m_activeDiagram,
        m_diagramScale,
        m_deformedScale,
        m_postContourMode,
        m_showSurfaceMesh,
        m_showLocalAxes,
        m_renderSettings.planeTheme,
        m_renderSettings.planeGridContrast,
        m_renderSettings.shaderLineAA,
        m_dcrRevision,
        m_animRevision,
        m_selectionRevision
    };
    if (!m_geometryDirty && m_sceneCacheBuilt && m_sceneCacheKey == key) {
        return;
    }

    m_sceneCacheKey = key;
    m_sceneCacheBuilt = true;
    m_geometryDirty = false;

    m_cachedLegendVisible = false;
    m_cachedLegendIsDcr = false;
    m_cachedLegendTitle.clear();
    m_cachedLegendUnit.clear();
    m_cachedLegendMin = 0.0;
    m_cachedLegendMax = 1.0;

    const bool hasModelContent = m_model &&
        (!m_model->getNodes().empty() ||
         !m_model->getElements().empty() ||
         !m_model->surfaceMesh().elements.empty());
    if (!hasModelContent) {
        m_surfaceMeshTriCache.vertexCount = 0;
        m_surfaceMeshEdgeCache.lineVertexCount = 0;
        m_surfaceMeshEdgeCache.aaVertexCount = 0;
        m_elemNormalCache.lineVertexCount = 0;
        m_elemNormalCache.aaVertexCount = 0;
        m_elemSelectedCache.lineVertexCount = 0;
        m_elemSelectedCache.aaVertexCount = 0;
        m_elemAxisCache.lineVertexCount = 0;
        m_elemAxisCache.aaVertexCount = 0;
        m_supportMeshCache.vertexCount = 0;
        m_loadLineCache.lineVertexCount = 0;
        m_loadLineCache.aaVertexCount = 0;
        m_loadConeCache.vertexCount = 0;
        m_deformedLineCache.lineVertexCount = 0;
        m_deformedLineCache.aaVertexCount = 0;
        m_diagramFillCache.vertexCount = 0;
        m_diagramBorderCache.lineVertexCount = 0;
        m_diagramBorderCache.aaVertexCount = 0;
        m_cachedNodeInstanceCount = 0;
        return;
    }

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    const auto& nodes = m_model->getNodes();
    const auto& elems = m_model->getElements();
    const auto& mesh = m_model->surfaceMesh();
    const bool animActive = hasPostAnimation();
    const bool hasStepFrames = !m_nonlinearFrames.empty() || !m_nonlinearMeshFrames.empty();
    const bool hasModeAnim = !hasStepFrames && animActive;

    // 1. Malha 2D/3D (Cascas / Sólidos)
    if (m_showSurfaceMesh && !mesh.elements.empty() && !mesh.nodes.empty()) {
        std::unordered_map<NodeId, QVector3D> nodePosMap;
        nodePosMap.reserve(mesh.nodes.size());
        std::unordered_map<NodeId, double> nodeColorRatioMap;
        nodeColorRatioMap.reserve(mesh.nodes.size());

        const bool isDeformedView = hasStepFrames || hasModeAnim || (m_activeDiagram == DiagramType::Deformed);
        const bool useVonMisesContour = (!hasStepFrames && !hasModeAnim && m_postContourMode == PostContourMode::VonMisesStress);
        const bool useDispContour = !useVonMisesContour &&
            (hasStepFrames || hasModeAnim || m_postContourMode == PostContourMode::Displacement || m_activeDiagram == DiagramType::Deformed);

        double maxVonMises = 0.0;
        if (useVonMisesContour) {
            for (const auto& el : mesh.elements) {
                if (el.hasResults) {
                    maxVonMises = std::max(maxVonMises, el.vonMisesStress);
                }
            }
            if (maxVonMises > 1e-8) {
                m_cachedLegendVisible = true;
                m_cachedLegendIsDcr = false;
                m_cachedLegendTitle = locale::GerenciadorTraducao::trText("Tensão de von Mises");
                m_cachedLegendUnit = QStringLiteral("kPa");
                m_cachedLegendMin = 0.0;
                m_cachedLegendMax = maxVonMises;
            }
        }

        double maxDisp = 1e-9;
        double colorNormDenom = 1e-9;
        double animColorFactor = 1.0;
        double effScale = isDeformedView ? m_deformedScale : 0.0;
        const std::unordered_map<NodeId, Vec3>* animSourceMap = nullptr;

        if (hasStepFrames) {
            const int totalFrames = nonlinearStepCount();
            const int idx = std::clamp(m_nonlinearStepIdx, 0, std::max(0, totalFrames - 1));
            if (idx < static_cast<int>(m_nonlinearMeshFrames.size())) {
                animSourceMap = &m_nonlinearMeshFrames[static_cast<std::size_t>(idx)].second;
            } else if (idx < static_cast<int>(m_nonlinearFrames.size())) {
                animSourceMap = &m_nonlinearFrames[static_cast<std::size_t>(idx)].second;
            }
            if (animSourceMap) {
                for (const auto& [nid, u] : *animSourceMap) {
                    Q_UNUSED(nid);
                    const double un = u.norm();
                    if (std::isfinite(un)) maxDisp = std::max(maxDisp, un);
                }
            }
            double maxPeakAcrossFrames = maxDisp;
            for (const auto& frame : m_nonlinearFrames) {
                for (const auto& [nid, u] : frame.second) {
                    Q_UNUSED(nid);
                    const double un = u.norm();
                    if (std::isfinite(un)) maxPeakAcrossFrames = std::max(maxPeakAcrossFrames, un);
                }
            }
            for (const auto& frame : m_nonlinearMeshFrames) {
                for (const auto& [nid, u] : frame.second) {
                    Q_UNUSED(nid);
                    const double un = u.norm();
                    if (std::isfinite(un)) maxPeakAcrossFrames = std::max(maxPeakAcrossFrames, un);
                }
            }
            const double diag = computeModelVisualDiagonal(m_model);
            effScale = (m_animBaseScale <= 0.0)
                ? ((diag * 0.09) / std::max(1e-9, maxPeakAcrossFrames)) * (m_deformedScale / 50.0) * m_animAmplitude
                : (m_animBaseScale * m_deformedScale * m_animAmplitude);
            if (!std::isfinite(effScale)) effScale = 0.0;
            colorNormDenom = std::max(1e-9, maxDisp);
        } else if (hasModeAnim) {
            animSourceMap = !m_postAnimMeshDispl.empty() ? &m_postAnimMeshDispl : &m_postAnimNodeDispl;
            double maxPeak = 1e-9;
            for (const auto& [nid, u] : m_postAnimNodeDispl) {
                Q_UNUSED(nid);
                const double un = u.norm();
                if (std::isfinite(un)) maxPeak = std::max(maxPeak, un);
            }
            for (const auto& [nid, u] : m_postAnimMeshDispl) {
                Q_UNUSED(nid);
                const double un = u.norm();
                if (std::isfinite(un)) maxPeak = std::max(maxPeak, un);
            }
            const double diag = computeModelVisualDiagonal(m_model);
            const double phaseFactor = std::isfinite(m_animPhase)
                ? (m_animOscillating ? std::sin(m_animPhase) : (0.5 - 0.5 * std::cos(m_animPhase)))
                : 0.0;
            effScale = (m_animBaseScale <= 0.0)
                ? ((diag * 0.09) / std::max(1e-9, maxPeak)) * (m_deformedScale / 50.0) * m_animAmplitude * phaseFactor
                : (m_animBaseScale * m_deformedScale * m_animAmplitude * phaseFactor);
            if (!std::isfinite(effScale)) effScale = 0.0;
            colorNormDenom = std::max(1e-9, maxPeak);
            animColorFactor = std::isfinite(phaseFactor) ? std::abs(phaseFactor) : 0.0;
        } else if (useDispContour) {
            for (const auto& sn : nodes) {
                const double dn = sn.displacementNorm();
                if (std::isfinite(dn)) maxDisp = std::max(maxDisp, dn);
            }
            for (const auto& mn : mesh.nodes) {
                if (mn.hasResults) {
                    const double dn = mn.displacementNorm();
                    if (std::isfinite(dn)) maxDisp = std::max(maxDisp, dn);
                }
            }
            colorNormDenom = std::max(1e-9, maxDisp);
            if (maxDisp > 1e-8) {
                m_cachedLegendVisible = true;
                m_cachedLegendIsDcr = false;
                m_cachedLegendTitle = locale::GerenciadorTraducao::trText("Deslocamento |u|");
                m_cachedLegendUnit = QStringLiteral("mm");
                m_cachedLegendMin = 0.0;
                m_cachedLegendMax = maxDisp * 1000.0;
            }
        }

        for (const auto& mn : mesh.nodes) {
            kernel::Vec3 uNode = kernel::Vec3::Zero();
            if (animSourceMap) {
                if (const auto it = animSourceMap->find(mn.id); it != animSourceMap->end()) {
                    uNode = kernel::Vec3(it->second.x, it->second.y, it->second.z);
                } else if (!m_postAnimMeshDispl.empty()) {
                    if (const auto itF = m_postAnimNodeDispl.find(mn.id); itF != m_postAnimNodeDispl.end()) {
                        uNode = kernel::Vec3(itF->second.x, itF->second.y, itF->second.z);
                    }
                }
            } else if (mn.hasResults) {
                uNode = kernel::Vec3(mn.displacement[0], mn.displacement[1], mn.displacement[2]);
            } else if (const Node* sn = m_model->findNode(mn.id)) {
                const double dx = sn->x() - mn.position.x();
                const double dy = sn->y() - mn.position.y();
                const double dz = sn->z() - mn.position.z();
                if ((dx * dx + dy * dy + dz * dz) <= 1e-12) {
                    uNode = kernel::Vec3(sn->displacement[0], sn->displacement[1], sn->displacement[2]);
                }
            }
            if (!uNode.allFinite()) {
                uNode.setZero();
            }

            const QVector3D pt(
                static_cast<float>(mn.position.x() + (isDeformedView ? uNode.x() * effScale : 0.0)),
                static_cast<float>(mn.position.y() + (isDeformedView ? uNode.y() * effScale : 0.0)),
                static_cast<float>(mn.position.z() + (isDeformedView ? uNode.z() * effScale : 0.0)));
            nodePosMap[mn.id] = pt;
            if (useDispContour && colorNormDenom > 1e-8) {
                const double rawRatio = (uNode.norm() / colorNormDenom) * animColorFactor;
                nodeColorRatioMap[mn.id] = std::isfinite(rawRatio) ? std::clamp(rawRatio, 0.0, 1.0) : 0.0;
            }
        }

        const QVector4D baseFillCol = pal.isLight
            ? QVector4D(0.24f, 0.44f, 0.68f, 0.72f)
            : QVector4D(0.36f, 0.56f, 0.80f, 0.72f);
        const QVector4D wireCol = pal.isLight
            ? QVector4D(0.10f, 0.16f, 0.28f, 0.85f)
            : QVector4D(0.84f, 0.90f, 0.98f, 0.80f);

        std::vector<float> triVerts;
        std::vector<float> edgeLines;
        triVerts.reserve(mesh.elements.size() * 60);
        edgeLines.reserve(mesh.elements.size() * 32);

        struct UndirectedEdgeKey {
            NodeId a = 0;
            NodeId b = 0;
            bool operator==(const UndirectedEdgeKey&) const = default;
        };
        struct UndirectedEdgeHash {
            std::size_t operator()(const UndirectedEdgeKey& k) const noexcept {
                const std::size_t h1 = std::hash<NodeId>{}(k.a);
                const std::size_t h2 = std::hash<NodeId>{}(k.b);
                return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9ULL + (h1 << 6) + (h1 >> 2));
            }
        };
        std::unordered_set<UndirectedEdgeKey, UndirectedEdgeHash> seenEdges;
        seenEdges.reserve(mesh.elements.size() * 3);

        auto colorForNode = [&](NodeId nid, const QVector4D* faceOverride) -> QVector4D {
            if (faceOverride != nullptr) {
                return *faceOverride;
            }
            if (useDispContour && colorNormDenom > 1e-8) {
                const auto it = nodeColorRatioMap.find(nid);
                const double r = (it != nodeColorRatioMap.end()) ? it->second : 0.0;
                const ColorRGB c = ColorRGB::jetThemed(r, pal.isLight);
                return QVector4D(c.r, c.g, c.b, 0.84f);
            }
            return baseFillCol;
        };

        auto pushTri = [&](NodeId a, NodeId b, NodeId c, const QVector4D* faceOverride) {
            const auto ita = nodePosMap.find(a);
            const auto itb = nodePosMap.find(b);
            const auto itc = nodePosMap.find(c);
            if (ita == nodePosMap.end() || itb == nodePosMap.end() || itc == nodePosMap.end()) return;
            const QVector3D& pa = ita->second;
            const QVector3D& pb = itb->second;
            const QVector3D& pc = itc->second;
            QVector3D n = QVector3D::crossProduct(pb - pa, pc - pa);
            if (n.lengthSquared() < 1e-12f) n = QVector3D(0.0f, 0.0f, 1.0f);
            else n.normalize();

            const auto ca = colorForNode(a, faceOverride);
            const auto cb = colorForNode(b, faceOverride);
            const auto cc = colorForNode(c, faceOverride);
            auto emitV = [&](const QVector3D& p, const QVector4D& col) {
                triVerts.insert(triVerts.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), col.x(), col.y(), col.z(), col.w()});
            };
            emitV(pa, ca);
            emitV(pb, cb);
            emitV(pc, cc);
        };

        auto pushEdge = [&](NodeId a, NodeId b) {
            const UndirectedEdgeKey ek{std::min(a, b), std::max(a, b)};
            if (!seenEdges.insert(ek).second) {
                return;
            }
            const auto ita = nodePosMap.find(a);
            const auto itb = nodePosMap.find(b);
            if (ita == nodePosMap.end() || itb == nodePosMap.end()) return;
            const QVector3D& pa = ita->second;
            const QVector3D& pb = itb->second;
            edgeLines.insert(edgeLines.end(), {
                pa.x(), pa.y(), pa.z(), wireCol.x(), wireCol.y(), wireCol.z(), wireCol.w(),
                pb.x(), pb.y(), pb.z(), wireCol.x(), wireCol.y(), wireCol.z(), wireCol.w()
            });
        };

        for (const auto& el : mesh.elements) {
            QVector4D vmCol;
            const QVector4D* faceColorPtr = nullptr;
            if (useVonMisesContour && maxVonMises > 1e-8 && el.hasResults) {
                const double ratio = std::clamp(el.vonMisesStress / maxVonMises, 0.0, 1.0);
                const ColorRGB c = ColorRGB::jetThemed(ratio, pal.isLight);
                vmCol = QVector4D(c.r, c.g, c.b, 0.86f);
                faceColorPtr = &vmCol;
            }
            const auto& ids = el.nodeIds;
            switch (el.type) {
                case kernel::MeshCellType::Line2:
                    if (ids.size() >= 2) pushEdge(ids[0], ids[1]);
                    break;
                case kernel::MeshCellType::Tri3:
                    if (ids.size() >= 3) {
                        pushTri(ids[0], ids[1], ids[2], faceColorPtr);
                        pushEdge(ids[0], ids[1]);
                        pushEdge(ids[1], ids[2]);
                        pushEdge(ids[2], ids[0]);
                    }
                    break;
                case kernel::MeshCellType::Quad4:
                case kernel::MeshCellType::Quad8:
                    if (ids.size() >= 4) {
                        pushTri(ids[0], ids[1], ids[2], faceColorPtr);
                        pushTri(ids[0], ids[2], ids[3], faceColorPtr);
                        pushEdge(ids[0], ids[1]);
                        pushEdge(ids[1], ids[2]);
                        pushEdge(ids[2], ids[3]);
                        pushEdge(ids[3], ids[0]);
                    }
                    break;
                case kernel::MeshCellType::Tet4:
                case kernel::MeshCellType::Tet10:
                    if (ids.size() >= 4) {
                        pushTri(ids[0], ids[2], ids[1], faceColorPtr);
                        pushTri(ids[0], ids[1], ids[3], faceColorPtr);
                        pushTri(ids[1], ids[2], ids[3], faceColorPtr);
                        pushTri(ids[2], ids[0], ids[3], faceColorPtr);
                        pushEdge(ids[0], ids[1]); pushEdge(ids[1], ids[2]); pushEdge(ids[2], ids[0]);
                        pushEdge(ids[0], ids[3]); pushEdge(ids[1], ids[3]); pushEdge(ids[2], ids[3]);
                    }
                    break;
                case kernel::MeshCellType::Hex8:
                case kernel::MeshCellType::Hex20:
                    if (ids.size() >= 8) {
                        const std::array<std::array<int, 4>, 6> faces = {{
                            {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                            {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}
                        }};
                        for (const auto& f : faces) {
                            pushTri(ids[f[0]], ids[f[1]], ids[f[2]], faceColorPtr);
                            pushTri(ids[f[0]], ids[f[2]], ids[f[3]], faceColorPtr);
                        }
                        const std::array<std::pair<int, int>, 12> edges = {{
                            {0, 1}, {1, 2}, {2, 3}, {3, 0},
                            {4, 5}, {5, 6}, {6, 7}, {7, 4},
                            {0, 4}, {1, 5}, {2, 6}, {3, 7}
                        }};
                        for (const auto& [u, v] : edges) {
                            pushEdge(ids[u], ids[v]);
                        }
                    }
                    break;
            }
        }

        uploadMeshCache(m_surfaceMeshTriCache, triVerts);
        uploadLineCache(m_surfaceMeshEdgeCache, edgeLines);
    } else {
        m_surfaceMeshTriCache.vertexCount = 0;
        m_surfaceMeshEdgeCache.lineVertexCount = 0;
        m_surfaceMeshEdgeCache.aaVertexCount = 0;
    }

    // 2. Deformada 3D e Animações (inclui flechas internas de vão em maxDisp)
    const bool showDeformed = (m_activeDiagram == DiagramType::Deformed) || animActive;
    if (showDeformed && (!elems.empty() || !mesh.elements.empty())) {
        std::vector<float> lines;
        if (hasStepFrames) {
            const int totalFrames = nonlinearStepCount();
            const int idx = std::clamp(m_nonlinearStepIdx, 0, std::max(0, totalFrames - 1));
            const std::unordered_map<NodeId, Vec3> emptyMap;
            const auto& dispMap = (idx < static_cast<int>(m_nonlinearFrames.size()))
                ? m_nonlinearFrames[static_cast<std::size_t>(idx)].second
                : emptyMap;
            double maxDisp = 1e-9;
            for (const auto& [nid, u] : dispMap) {
                Q_UNUSED(nid);
                const double un = u.norm();
                if (std::isfinite(un)) maxDisp = std::max(maxDisp, un);
            }
            if (idx < static_cast<int>(m_nonlinearMeshFrames.size())) {
                for (const auto& [nid, u] : m_nonlinearMeshFrames[static_cast<std::size_t>(idx)].second) {
                    Q_UNUSED(nid);
                    const double un = u.norm();
                    if (std::isfinite(un)) maxDisp = std::max(maxDisp, un);
                }
            }
            double maxPeakAcrossFrames = maxDisp;
            for (const auto& frame : m_nonlinearFrames) {
                for (const auto& [nid, u] : frame.second) {
                    Q_UNUSED(nid);
                    const double un = u.norm();
                    if (std::isfinite(un)) maxPeakAcrossFrames = std::max(maxPeakAcrossFrames, un);
                }
            }
            for (const auto& frame : m_nonlinearMeshFrames) {
                for (const auto& [nid, u] : frame.second) {
                    Q_UNUSED(nid);
                    const double un = u.norm();
                    if (std::isfinite(un)) maxPeakAcrossFrames = std::max(maxPeakAcrossFrames, un);
                }
            }
            const double diag = computeModelVisualDiagonal(m_model);
            double effScale = (m_animBaseScale <= 0.0)
                ? ((diag * 0.09) / std::max(1e-9, maxPeakAcrossFrames)) * (m_deformedScale / 50.0) * m_animAmplitude
                : (m_animBaseScale * m_deformedScale * m_animAmplitude);
            if (!std::isfinite(effScale)) effScale = 0.0;

            lines.reserve(elems.size() * 16 * 14);
            for (const auto& elem : elems) {
                const Node* n1 = m_model->findNode(elem.nodeStart);
                const Node* n2 = m_model->findNode(elem.nodeEnd);
                if (!n1 || !n2) continue;

                auto finiteVecOrZero = [](const Vec3& v) -> Vec3 {
                    return (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)) ? v : Vec3{};
                };
                const Vec3 u1 = dispMap.contains(n1->id) ? finiteVecOrZero(dispMap.at(n1->id)) : Vec3{};
                const Vec3 u2 = dispMap.contains(n2->id) ? finiteVecOrZero(dispMap.at(n2->id)) : Vec3{};

                constexpr int Nsamples = 16;
                Vec3 prevPt;
                ColorRGB prevCol;
                for (int s = 0; s <= Nsamples; ++s) {
                    const double xi = static_cast<double>(s) / Nsamples;
                    const Vec3 posOrig = n1->position * (1.0 - xi) + n2->position * xi;
                    const Vec3 uLocal = u1 * (1.0 - xi) + u2 * xi;
                    const Vec3 posDef = posOrig + uLocal * effScale;
                    const double rawRatio = uLocal.norm() / std::max(1e-9, maxDisp);
                    const double ratio = std::isfinite(rawRatio) ? std::clamp(rawRatio, 0.0, 1.0) : 0.0;
                    const ColorRGB col = ColorRGB::jetThemed(ratio, pal.isLight);
                    if (s > 0) {
                        lines.insert(lines.end(), {
                            static_cast<float>(prevPt.x), static_cast<float>(prevPt.y), static_cast<float>(prevPt.z),
                            prevCol.r, prevCol.g, prevCol.b, 1.0f,
                            static_cast<float>(posDef.x), static_cast<float>(posDef.y), static_cast<float>(posDef.z),
                            col.r, col.g, col.b, 1.0f
                        });
                    }
                    prevPt = posDef;
                    prevCol = col;
                }
            }

            m_cachedLegendVisible = true;
            m_cachedLegendIsDcr = false;
            m_cachedLegendTitle = !m_postAnimLabel.isEmpty()
                ? m_postAnimLabel
                : locale::GerenciadorTraducao::trText("Não Linear |u|");
            m_cachedLegendUnit = QStringLiteral("mm");
            m_cachedLegendMin = 0.0;
            m_cachedLegendMax = maxDisp * 1000.0;
        } else if (hasModeAnim) {
            double maxPeak = 1e-9;
            for (const auto& [nid, u] : m_postAnimNodeDispl) {
                Q_UNUSED(nid);
                const double un = u.norm();
                if (std::isfinite(un)) maxPeak = std::max(maxPeak, un);
            }
            for (const auto& [nid, u] : m_postAnimMeshDispl) {
                Q_UNUSED(nid);
                const double un = u.norm();
                if (std::isfinite(un)) maxPeak = std::max(maxPeak, un);
            }
            const double diag = computeModelVisualDiagonal(m_model);
            const double phaseFactor = std::isfinite(m_animPhase)
                ? (m_animOscillating ? std::sin(m_animPhase) : (0.5 - 0.5 * std::cos(m_animPhase)))
                : 0.0;
            double effScale = (m_animBaseScale <= 0.0)
                ? ((diag * 0.09) / std::max(1e-9, maxPeak)) * (m_deformedScale / 50.0) * m_animAmplitude * phaseFactor
                : (m_animBaseScale * m_deformedScale * m_animAmplitude * phaseFactor);
            if (!std::isfinite(effScale)) effScale = 0.0;

            lines.reserve(elems.size() * 16 * 14);
            for (const auto& elem : elems) {
                const Node* n1 = m_model->findNode(elem.nodeStart);
                const Node* n2 = m_model->findNode(elem.nodeEnd);
                if (!n1 || !n2) continue;

                auto finiteVecOrZero = [](const Vec3& v) -> Vec3 {
                    return (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)) ? v : Vec3{};
                };
                const Vec3 u1 = m_postAnimNodeDispl.contains(n1->id) ? finiteVecOrZero(m_postAnimNodeDispl.at(n1->id)) : Vec3{};
                const Vec3 u2 = m_postAnimNodeDispl.contains(n2->id) ? finiteVecOrZero(m_postAnimNodeDispl.at(n2->id)) : Vec3{};

                constexpr int Nsamples = 16;
                Vec3 prevPt;
                ColorRGB prevCol;
                for (int s = 0; s <= Nsamples; ++s) {
                    const double xi = static_cast<double>(s) / Nsamples;
                    const Vec3 posOrig = n1->position * (1.0 - xi) + n2->position * xi;
                    const Vec3 uMode = u1 * (1.0 - xi) + u2 * xi;
                    const Vec3 posDef = posOrig + uMode * effScale;
                    const double rawRatio = (uMode.norm() / std::max(1e-9, maxPeak)) * std::abs(phaseFactor);
                    const double ratio = std::isfinite(rawRatio) ? std::clamp(rawRatio, 0.0, 1.0) : 0.0;
                    const ColorRGB col = ColorRGB::jetThemed(ratio, pal.isLight);
                    if (s > 0) {
                        lines.insert(lines.end(), {
                            static_cast<float>(prevPt.x), static_cast<float>(prevPt.y), static_cast<float>(prevPt.z),
                            prevCol.r, prevCol.g, prevCol.b, 1.0f,
                            static_cast<float>(posDef.x), static_cast<float>(posDef.y), static_cast<float>(posDef.z),
                            col.r, col.g, col.b, 1.0f
                        });
                    }
                    prevPt = posDef;
                    prevCol = col;
                }
            }

            m_cachedLegendVisible = true;
            m_cachedLegendIsDcr = false;
            m_cachedLegendTitle = !m_postAnimLabel.isEmpty()
                ? m_postAnimLabel
                : locale::GerenciadorTraducao::trText("Forma Modal / Flambagem");
            m_cachedLegendUnit = (m_animBaseScale <= 0.0) ? QStringLiteral("φ/φmax") : QStringLiteral("mm");
            m_cachedLegendMin = 0.0;
            m_cachedLegendMax = (m_animBaseScale <= 0.0) ? 1.0 : (maxPeak * 1000.0);
        } else {
            const bool showDcrContour = (m_postContourMode == PostContourMode::NbrComplianceDcr && !m_elementDcrMap.empty());
            double maxDisp = 1e-6;
            for (const auto& n : nodes) {
                const double dn = n.displacementNorm();
                if (std::isfinite(dn)) maxDisp = std::max(maxDisp, dn);
            }
            for (const auto& mn : mesh.nodes) {
                if (mn.hasResults) {
                    const double dn = mn.displacementNorm();
                    if (std::isfinite(dn)) maxDisp = std::max(maxDisp, dn);
                }
            }

            constexpr int Nsamples = 32;
            struct SampledBarDef {
                const Element* elem = nullptr;
                const Node* n1 = nullptr;
                const Node* n2 = nullptr;
                std::array<Vec3, Nsamples + 1> defs{};
            };
            std::vector<SampledBarDef> barSamples;
            barSamples.reserve(elems.size());
            for (const auto& elem : elems) {
                if (!elem.hasResults) continue;
                const Node* n1 = m_model->findNode(elem.nodeStart);
                const Node* n2 = m_model->findNode(elem.nodeEnd);
                if (!n1 || !n2) continue;
                SampledBarDef sb;
                sb.elem = &elem;
                sb.n1 = n1;
                sb.n2 = n2;
                for (int s = 0; s <= Nsamples; ++s) {
                    const double xi = static_cast<double>(s) / static_cast<double>(Nsamples);
                    const InternalForces f = elem.evaluateAt(xi, *m_model);
                    const Vec3 defSafe = (std::isfinite(f.defGlobal.x) && std::isfinite(f.defGlobal.y) && std::isfinite(f.defGlobal.z))
                        ? f.defGlobal : Vec3{};
                    sb.defs[static_cast<std::size_t>(s)] = defSafe;
                    const double uNorm = defSafe.norm();
                    if (std::isfinite(uNorm)) maxDisp = std::max(maxDisp, uNorm);
                }
                barSamples.push_back(sb);
            }

            if (!showDcrContour && m_postContourMode != PostContourMode::VonMisesStress && maxDisp > 1e-8) {
                m_cachedLegendVisible = true;
                m_cachedLegendIsDcr = false;
                m_cachedLegendTitle = locale::GerenciadorTraducao::trText("Deslocamento |u|");
                m_cachedLegendUnit = QStringLiteral("mm");
                m_cachedLegendMin = 0.0;
                m_cachedLegendMax = maxDisp * 1000.0;
            }

            lines.reserve(barSamples.size() * Nsamples * 14);
            for (const auto& sb : barSamples) {
                bool hasElemDcr = false;
                QVector4D dcrCol;
                if (showDcrContour) {
                    if (auto it = m_elementDcrMap.find(sb.elem->id); it != m_elementDcrMap.end()) {
                        hasElemDcr = true;
                        dcrCol = dcrRatioToColor(it->second);
                    }
                }
                Vec3 prevPt;
                ColorRGB prevCol;
                for (int s = 0; s <= Nsamples; ++s) {
                    const double xi = static_cast<double>(s) / static_cast<double>(Nsamples);
                    const Vec3 posOrig = sb.n1->position * (1.0 - xi) + sb.n2->position * xi;
                    const Vec3& defSafe = sb.defs[static_cast<std::size_t>(s)];
                    const Vec3 posDef = posOrig + defSafe * m_deformedScale;
                    const double normU = defSafe.norm();
                    const double ratioU = std::isfinite(normU / maxDisp) ? std::clamp(normU / maxDisp, 0.0, 1.0) : 0.0;
                    ColorRGB col = ColorRGB::jetThemed(ratioU, pal.isLight);
                    if (hasElemDcr) {
                        col = ColorRGB{dcrCol.x(), dcrCol.y(), dcrCol.z()};
                    }
                    if (s > 0) {
                        lines.insert(lines.end(), {
                            static_cast<float>(prevPt.x), static_cast<float>(prevPt.y), static_cast<float>(prevPt.z),
                            prevCol.r, prevCol.g, prevCol.b, 1.0f,
                            static_cast<float>(posDef.x), static_cast<float>(posDef.y), static_cast<float>(posDef.z),
                            col.r, col.g, col.b, 1.0f
                        });
                    }
                    prevPt = posDef;
                    prevCol = col;
                }
            }
        }
        uploadLineCache(m_deformedLineCache, lines);
    } else {
        m_deformedLineCache.lineVertexCount = 0;
        m_deformedLineCache.aaVertexCount = 0;
    }

    // 3. Elementos 1D (normais, selecionados e eixos locais)
    {
        const bool showDcrContour = (m_postContourMode == PostContourMode::NbrComplianceDcr
                                     && !m_elementDcrMap.empty()
                                     && !animActive);
        const bool showDispContour = (m_postContourMode == PostContourMode::Displacement
                                      && m_activeDiagram != DiagramType::Deformed
                                      && !animActive);
        const bool ghostReference = (m_activeDiagram == DiagramType::Deformed || animActive);

        double maxDcr = 0.0;
        if (showDcrContour) {
            for (const auto& [eid, val] : m_elementDcrMap) {
                Q_UNUSED(eid);
                maxDcr = std::max(maxDcr, val);
            }
            m_cachedLegendVisible = true;
            m_cachedLegendIsDcr = true;
            m_cachedLegendTitle = locale::GerenciadorTraducao::trText("Aproveitamento NBR (DCR)");
            m_cachedLegendUnit = QStringLiteral("D/C");
            m_cachedLegendMin = 0.0;
            m_cachedLegendMax = std::max(1.20, maxDcr);
        }

        constexpr int kSub = 8;
        double maxNodeDisp = 0.0;
        std::vector<std::array<double, kSub + 1>> contourMagByElem;
        if (showDispContour) {
            for (const auto& n : nodes) {
                const double dn = n.displacementNorm();
                if (std::isfinite(dn)) maxNodeDisp = std::max(maxNodeDisp, dn);
            }
            for (const auto& mn : mesh.nodes) {
                if (mn.hasResults) {
                    const double dn = mn.displacementNorm();
                    if (std::isfinite(dn)) maxNodeDisp = std::max(maxNodeDisp, dn);
                }
            }
            contourMagByElem.resize(elems.size());
            for (std::size_t ei = 0; ei < elems.size(); ++ei) {
                const auto& elem = elems[ei];
                const Node* n1 = m_model->findNode(elem.nodeStart);
                const Node* n2 = m_model->findNode(elem.nodeEnd);
                if (!n1 || !n2) continue;
                const double u1 = n1->displacementNorm();
                const double u2 = n2->displacementNorm();
                for (int s = 0; s <= kSub; ++s) {
                    const double t = static_cast<double>(s) / kSub;
                    const double v = elem.hasResults
                        ? elem.evaluateAt(t, *m_model).defGlobal.norm()
                        : (u1 * (1.0 - t) + u2 * t);
                    const double safeV = std::isfinite(v) ? v : 0.0;
                    contourMagByElem[ei][static_cast<std::size_t>(s)] = safeV;
                    maxNodeDisp = std::max(maxNodeDisp, safeV);
                }
            }
            if (maxNodeDisp > 1e-12) {
                m_cachedLegendVisible = true;
                m_cachedLegendIsDcr = false;
                m_cachedLegendTitle = locale::GerenciadorTraducao::trText("Deslocamento |u|");
                m_cachedLegendUnit = QStringLiteral("mm");
                m_cachedLegendMin = 0.0;
                m_cachedLegendMax = maxNodeDisp * 1000.0;
            }
        }

        std::vector<float> normalLines;
        std::vector<float> selectedLines;
        std::vector<float> axisLines;
        normalLines.reserve(elems.size() * 14);

        const auto& selectedElems = m_model->getSelectedElements();
        for (std::size_t ei = 0; ei < elems.size(); ++ei) {
            const auto& elem = elems[ei];
            const Node* n1 = m_model->findNode(elem.nodeStart);
            const Node* n2 = m_model->findNode(elem.nodeEnd);
            if (!n1 || !n2) continue;

            const bool isSelected = selectedElems.contains(elem.id);
            auto& target = isSelected ? selectedLines : normalLines;

            if (!isSelected && showDispContour && maxNodeDisp > 1e-12) {
                for (int s = 0; s < kSub; ++s) {
                    const double t0 = static_cast<double>(s) / kSub;
                    const double t1 = static_cast<double>(s + 1) / kSub;
                    const Vec3 p0 = n1->position * (1.0 - t0) + n2->position * t0;
                    const Vec3 p1 = n1->position * (1.0 - t1) + n2->position * t1;
                    const double v0 = contourMagByElem[ei][static_cast<std::size_t>(s)];
                    const double v1 = contourMagByElem[ei][static_cast<std::size_t>(s) + 1u];
                    const ColorRGB c0 = ColorRGB::jetThemed(std::clamp(v0 / maxNodeDisp, 0.0, 1.0), pal.isLight);
                    const ColorRGB c1 = ColorRGB::jetThemed(std::clamp(v1 / maxNodeDisp, 0.0, 1.0), pal.isLight);
                    target.insert(target.end(), {
                        static_cast<float>(p0.x), static_cast<float>(p0.y), static_cast<float>(p0.z),
                        c0.r, c0.g, c0.b, 1.0f,
                        static_cast<float>(p1.x), static_cast<float>(p1.y), static_cast<float>(p1.z),
                        c1.r, c1.g, c1.b, 1.0f
                    });
                }
            } else {
                QVector4D col = isSelected
                    ? pal.memberSelectedColor
                    : (ghostReference ? pal.memberGhostColor : pal.memberColor);
                if (!isSelected && showDcrContour && !ghostReference) {
                    if (auto it = m_elementDcrMap.find(elem.id); it != m_elementDcrMap.end()) {
                        col = dcrRatioToColor(it->second);
                    }
                }
                target.insert(target.end(), {
                    static_cast<float>(n1->x()), static_cast<float>(n1->y()), static_cast<float>(n1->z()),
                    col.x(), col.y(), col.z(), col.w(),
                    static_cast<float>(n2->x()), static_cast<float>(n2->y()), static_cast<float>(n2->z()),
                    col.x(), col.y(), col.z(), col.w()
                });
            }

            if (m_showLocalAxes) {
                const Vec3 mid = (n1->position + n2->position) * 0.5;
                const Eigen::Matrix3d R = elem.computeRotationMatrix3x3(*m_model);
                const float triadLen = 0.6f;

                const Eigen::Vector3d ex = R.row(0) * triadLen;
                const Eigen::Vector3d ey = R.row(1) * triadLen;
                const Eigen::Vector3d ez = R.row(2) * triadLen;

                auto addLocalAxis = [&](const Eigen::Vector3d& dir, const QVector4D& c) {
                    axisLines.insert(axisLines.end(), {
                        static_cast<float>(mid.x), static_cast<float>(mid.y), static_cast<float>(mid.z),
                        c.x(), c.y(), c.z(), c.w(),
                        static_cast<float>(mid.x + dir.x()), static_cast<float>(mid.y + dir.y()), static_cast<float>(mid.z + dir.z()),
                        c.x(), c.y(), c.z(), c.w()
                    });
                };
                addLocalAxis(ex, pal.localAxisXColor);
                addLocalAxis(ey, pal.localAxisYColor);
                addLocalAxis(ez, pal.localAxisZColor);
            }
        }

        uploadLineCache(m_elemNormalCache, normalLines);
        uploadLineCache(m_elemSelectedCache, selectedLines);
        uploadLineCache(m_elemAxisCache, axisLines);
    }

    // 4. Diagramas de Esforços Internos (com interpolação de raiz t0 no cruzamento por zero e escala adaptativa)
    if (!showDeformed && m_activeDiagram != DiagramType::None && !elems.empty()) {
        if (!m_diagramCache.valid ||
            m_diagramCache.model != m_model ||
            m_diagramCache.geometryRevision != modelRev ||
            m_diagramCache.diagramType != m_activeDiagram) {
            rebuildDiagramEvalCache();
        }

        const double diagDiag = computeModelVisualDiagonal(m_model);
        const double targetHeight = std::clamp(diagDiag * 0.12, 0.35, 15.0);
        const double rawScale = (targetHeight / std::max(1e-6, m_diagramCache.maxAbsVal)) * m_diagramScale;
        const float visualScale = std::isfinite(rawScale) ? static_cast<float>(rawScale) : 0.0f;
        const float sign = (m_activeDiagram == DiagramType::Moment_Mz) ? -1.0f : 1.0f;

        std::vector<float> fillTris;
        std::vector<float> borderLines;
        fillTris.reserve(m_diagramCache.bars.size() * 32 * 42);
        borderLines.reserve(m_diagramCache.bars.size() * 32 * 14);

        auto pushTri2 = [&](const Vec3& a, const Vec3& b, const Vec3& c, const QVector4D& col) {
            fillTris.insert(fillTris.end(), {
                static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z),
                col.x(), col.y(), col.z(), col.w(),
                static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z),
                col.x(), col.y(), col.z(), col.w(),
                static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z),
                col.x(), col.y(), col.z(), col.w()
            });
        };
        auto pushBorder = [&](const Vec3& a, const Vec3& b, const QVector4D& col) {
            borderLines.insert(borderLines.end(), {
                static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z),
                col.x(), col.y(), col.z(), col.w(),
                static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z),
                col.x(), col.y(), col.z(), col.w()
            });
        };

        for (const auto& bar : m_diagramCache.bars) {
            Vec3 prevBase = bar.baseStart;
            double prevVal = bar.values[0];
            Vec3 prevCurve = prevBase + bar.plotDir * (prevVal * visualScale * sign);

            for (int s = 1; s <= 32; ++s) {
                const double xi = static_cast<double>(s) / 32.0;
                const Vec3 currBase = bar.baseStart * (1.0 - xi) + bar.baseEnd * xi;
                const double currVal = bar.values[static_cast<std::size_t>(s)];
                const Vec3 currCurve = currBase + bar.plotDir * (currVal * visualScale * sign);

                if ((prevVal > 1e-12 && currVal < -1e-12) || (prevVal < -1e-12 && currVal > 1e-12)) {
                    const double denom = std::abs(prevVal) + std::abs(currVal);
                    const double t0 = std::clamp(std::abs(prevVal) / std::max(1e-12, denom), 0.0, 1.0);
                    const Vec3 zeroBase = prevBase * (1.0 - t0) + currBase * t0;

                    const bool firstPos = (prevVal >= 0.0);
                    const QVector4D fill1 = firstPos ? pal.diagramPosFillColor : pal.diagramNegFillColor;
                    const QVector4D border1 = firstPos ? pal.diagramPosBorderColor : pal.diagramNegBorderColor;
                    const QVector4D fill2 = firstPos ? pal.diagramNegFillColor : pal.diagramPosFillColor;
                    const QVector4D border2 = firstPos ? pal.diagramNegBorderColor : pal.diagramPosBorderColor;

                    pushTri2(prevBase, prevCurve, zeroBase, fill1);
                    pushBorder(prevCurve, zeroBase, border1);

                    pushTri2(zeroBase, currCurve, currBase, fill2);
                    pushBorder(zeroBase, currCurve, border2);
                } else {
                    const bool domPos = (std::abs(prevVal) >= std::abs(currVal)) ? (prevVal >= 0.0) : (currVal >= 0.0);
                    const QVector4D fillCol = domPos ? pal.diagramPosFillColor : pal.diagramNegFillColor;
                    const QVector4D borderCol = domPos ? pal.diagramPosBorderColor : pal.diagramNegBorderColor;

                    pushTri2(prevBase, prevCurve, currCurve, fillCol);
                    pushTri2(prevBase, currCurve, currBase, fillCol);
                    pushBorder(prevCurve, currCurve, borderCol);
                }

                prevBase = currBase;
                prevVal = currVal;
                prevCurve = currCurve;
            }
        }

        uploadDiagFillCache(m_diagramFillCache, fillTris);
        uploadLineCache(m_diagramBorderCache, borderLines);
    } else {
        m_diagramFillCache.vertexCount = 0;
        m_diagramBorderCache.lineVertexCount = 0;
        m_diagramBorderCache.aaVertexCount = 0;
    }

    // 5. Nós (Instancing em GPU: 8 floats por nó)
    if (!nodes.empty()) {
        std::vector<float> instances;
        instances.reserve(nodes.size() * 8);
        const auto& selNodes = m_model->getSelectedNodes();
        for (const auto& node : nodes) {
            const bool isSelected = selNodes.contains(node.id);
            const QVector4D col = isSelected ? pal.nodeSelectedColor : pal.nodeColor;
            const float radius = isSelected ? 0.14f : 0.09f;
            instances.insert(instances.end(), {
                static_cast<float>(node.x()),
                static_cast<float>(node.y()),
                static_cast<float>(node.z()),
                radius,
                col.x(), col.y(), col.z(), col.w()
            });
        }
        m_nodeInstVAO.bind();
        m_nodeInstanceVBO.bind();
        if (safeAllocateVbo(m_nodeInstanceVBO, instances.data(), instances.size(), sizeof(float))) {
            m_cachedNodeInstanceCount = static_cast<GLsizei>(nodes.size());
        } else {
            m_cachedNodeInstanceCount = 0;
        }
        m_nodeInstVAO.release();
        m_nodeInstanceVBO.release();
    } else {
        m_cachedNodeInstanceCount = 0;
    }

    // 6. Apoios (com SpatialPointHash O(1) para sobreposição barra/malha)
    {
        std::vector<float> supportMeshVerts;
        SpatialPointHash supportedFrameHash(1e-4);
        supportedFrameHash.reserve(nodes.size());

        for (std::size_t idx = 0; idx < nodes.size(); ++idx) {
            const auto& node = nodes[idx];
            if (!node.isSupported()) continue;
            supportedFrameHash.insert(node.x(), node.y(), node.z(), idx);

            const QVector3D pos(static_cast<float>(node.x()), static_cast<float>(node.y()), static_cast<float>(node.z()));
            if (node.support.isFixed()) {
                buildCube(supportMeshVerts, 0.28f, pos - QVector3D(0.0f, 0.0f, 0.14f), pal.supportFixedColor);
            } else if (node.support.isPinned()) {
                buildPyramid(supportMeshVerts, 0.35f, 0.35f, pos, pal.supportPinnedColor);
            } else {
                buildPyramid(supportMeshVerts, 0.32f, 0.30f, pos, pal.supportRollerColor);
            }
        }

        for (const auto& mn : mesh.nodes) {
            if (!mn.isSupported()) continue;
            if (supportedFrameHash.findNearestWithinSq(mn.position.x(), mn.position.y(), mn.position.z(), 1e-12)) {
                continue;
            }
            const QVector3D pos(static_cast<float>(mn.position.x()),
                                static_cast<float>(mn.position.y()),
                                static_cast<float>(mn.position.z()));
            const bool isFixed = mn.fixed[0] && mn.fixed[1] && mn.fixed[2]
                              && mn.fixed[3] && mn.fixed[4] && mn.fixed[5];
            const bool isPinned = mn.fixed[0] && mn.fixed[1] && mn.fixed[2];
            if (isFixed) {
                buildCube(supportMeshVerts, 0.24f, pos - QVector3D(0.0f, 0.0f, 0.12f), pal.supportFixedColor);
            } else if (isPinned) {
                buildPyramid(supportMeshVerts, 0.28f, 0.28f, pos, pal.supportPinnedColor);
            } else {
                buildPyramid(supportMeshVerts, 0.25f, 0.24f, pos, pal.supportRollerColor);
            }
        }
        uploadMeshCache(m_supportMeshCache, supportMeshVerts);
    }

    // 7. Cargas Nodais e Distribuídas
    {
        std::vector<float> loadLines;
        std::vector<float> loadCones;

        for (const auto& node : nodes) {
            if (!node.load.isActive()) continue;
            const QVector3D p(static_cast<float>(node.x()), static_cast<float>(node.y()), static_cast<float>(node.z()));

            if (node.load.force.normSq() > 1e-6) {
                const Vec3 fn = node.load.force.normalized();
                const float arrowLen = 1.2f;
                const QVector3D fDir(static_cast<float>(fn.x), static_cast<float>(fn.y), static_cast<float>(fn.z));
                const QVector3D tip = p;
                const QVector3D base = p - fDir * arrowLen;
                const QVector4D col = pal.loadNodalColor;

                loadLines.insert(loadLines.end(), {
                    base.x(), base.y(), base.z(), col.x(), col.y(), col.z(), col.w(),
                    tip.x(), tip.y(), tip.z(), col.x(), col.y(), col.z(), col.w()
                });
                buildCone(loadCones, 0.10f, 0.25f, tip - fDir * 0.25f, fDir, col);
            }

            if (node.load.moment.normSq() > 1e-6) {
                const Vec3 mn = node.load.moment.normalized();
                const float arrowLen = 1.0f;
                const QVector3D mDir(static_cast<float>(mn.x), static_cast<float>(mn.y), static_cast<float>(mn.z));
                const QVector3D tip = p;
                const QVector3D base = p - mDir * arrowLen;
                const QVector4D col = pal.loadNodalColor;

                loadLines.insert(loadLines.end(), {
                    base.x(), base.y(), base.z(), col.x(), col.y(), col.z(), col.w(),
                    tip.x(), tip.y(), tip.z(), col.x(), col.y(), col.z(), col.w()
                });
                buildCone(loadCones, 0.09f, 0.20f, tip - mDir * 0.20f, mDir, col);
                buildCone(loadCones, 0.09f, 0.20f, tip - mDir * 0.40f, mDir, col);
            }
        }

        for (const auto& elem : elems) {
            if (!elem.distLoad.active) continue;
            const Node* n1 = m_model->findNode(elem.nodeStart);
            const Node* n2 = m_model->findNode(elem.nodeEnd);
            if (!n1 || !n2) continue;

            Vec3 qGlob{0.0, 0.0, 0.0};
            if (elem.distLoad.coordSystem == CoordSystem::Global) {
                qGlob = elem.distLoad.w;
            } else {
                const Eigen::Matrix3d R = elem.computeRotationMatrix3x3(*m_model);
                const Eigen::Vector3d ql(elem.distLoad.w.x, elem.distLoad.w.y, elem.distLoad.w.z);
                const Eigen::Vector3d qg = R.transpose() * ql;
                qGlob = {qg.x(), qg.y(), qg.z()};
            }
            if (qGlob.normSq() < 1e-6) continue;

            const Vec3 qNorm = qGlob.normalized();
            const QVector3D qDir(static_cast<float>(qNorm.x), static_cast<float>(qNorm.y), static_cast<float>(qNorm.z));
            const float arrowLen = 0.8f;
            const QVector4D col = pal.loadDistColor;

            constexpr int numArrows = 6;
            for (int i = 0; i <= numArrows; ++i) {
                const double xi = static_cast<double>(i) / static_cast<double>(numArrows);
                const Vec3 pos = n1->position * (1.0 - xi) + n2->position * xi;
                const QVector3D tip(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));
                const QVector3D base = tip - qDir * arrowLen;

                loadLines.insert(loadLines.end(), {
                    base.x(), base.y(), base.z(), col.x(), col.y(), col.z(), col.w(),
                    tip.x(), tip.y(), tip.z(), col.x(), col.y(), col.z(), col.w()
                });
                buildCone(loadCones, 0.06f, 0.15f, tip - qDir * 0.15f, qDir, col);

                if (i > 0) {
                    const double prevXi = static_cast<double>(i - 1) / static_cast<double>(numArrows);
                    const Vec3 prevPos = n1->position * (1.0 - prevXi) + n2->position * prevXi;
                    const QVector3D prevBase = QVector3D(
                        static_cast<float>(prevPos.x), static_cast<float>(prevPos.y), static_cast<float>(prevPos.z)) - qDir * arrowLen;

                    loadLines.insert(loadLines.end(), {
                        prevBase.x(), prevBase.y(), prevBase.z(), col.x(), col.y(), col.z(), col.w(),
                        base.x(), base.y(), base.z(), col.x(), col.y(), col.z(), col.w()
                    });
                }
            }
        }

        uploadLineCache(m_loadLineCache, loadLines);
        uploadMeshCache(m_loadConeCache, loadCones);
    }
}

void Viewport3D::renderGrid(const QMatrix4x4& mvp) {
    drawCachedDiagFill(mvp, m_gridSurfaceCache);
    drawCachedLines(mvp, m_gridMinorCache, 0.90f, false, false);
    drawCachedLines(mvp, m_gridMajorCache, 1.35f, false, false);
    drawCachedLines(mvp, m_gridBorderCache, 1.65f, false, false);
    drawCachedLines(mvp, m_gridAxisCache, 2.35f, false, false);
}

void Viewport3D::renderElements(const QMatrix4x4& mvp) {
    const bool ghostReference = (m_activeDiagram == DiagramType::Deformed || hasPostAnimation());
    drawCachedLines(mvp, m_elemNormalCache, ghostReference ? 1.35f : 2.35f, !ghostReference, !ghostReference);
    drawCachedLines(mvp, m_elemSelectedCache, 3.10f, true, true);
    drawCachedLines(mvp, m_elemAxisCache, 1.85f, true, true);
}

void Viewport3D::renderNodes(const QMatrix4x4& mvp) {
    if (m_cachedNodeInstanceCount <= 0 || m_unitSphereVertexCount <= 0 ||
        !m_instancedMeshShader || !m_instancedMeshShader->isLinked()) {
        return;
    }

    m_instancedMeshShader->bind();
    m_instancedMeshShader->setUniformValue("uMVP", mvp);
    m_instancedMeshShader->setUniformValue("uClipPlane", activeClipPlaneVec());
    m_instancedMeshShader->setUniformValue("uLightDir", QVector3D(0.4f, 0.5f, 1.0f).normalized());
    m_instancedMeshShader->setUniformValue("uViewPos", m_camera.eyePosition());

    m_nodeInstVAO.bind();
    glDrawArraysInstanced(GL_TRIANGLES, 0, m_unitSphereVertexCount, m_cachedNodeInstanceCount);
    m_nodeInstVAO.release();
    m_instancedMeshShader->release();
}

void Viewport3D::renderSupports(const QMatrix4x4& mvp, const QMatrix4x4& view) {
    (void)view;
    drawCachedMesh(mvp, m_supportMeshCache);
}

void Viewport3D::renderLoads(const QMatrix4x4& mvp, const QMatrix4x4& view) {
    (void)view;
    drawCachedLines(mvp, m_loadLineCache, 2.0f, true, true);
    drawCachedMesh(mvp, m_loadConeCache);
}

void Viewport3D::renderDeformedShape(const QMatrix4x4& mvp) {
    const float widthPx = hasPostAnimation() ? 2.95f : 2.85f;
    drawCachedLines(mvp, m_deformedLineCache, widthPx, true, true);
}

void Viewport3D::renderDiagrams(const QMatrix4x4& mvp) {
    drawCachedDiagFill(mvp, m_diagramFillCache);
    drawCachedLines(mvp, m_diagramBorderCache, 2.0f, true, true);
}

void Viewport3D::renderSurfaceMesh(const QMatrix4x4& mvp) {
    drawCachedMesh(mvp, m_surfaceMeshTriCache);
    drawCachedLines(mvp, m_surfaceMeshEdgeCache, 1.15f, true, false);
}

void Viewport3D::renderCursorPreview(const QMatrix4x4& mvp) {
    if (!m_hasCursor3D || !m_meshShader || !m_meshShader->isLinked()) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    std::vector<float> meshVertices;
    const QVector3D p(static_cast<float>(m_currentCursor3D.x),
                      static_cast<float>(m_currentCursor3D.y),
                      static_cast<float>(m_currentCursor3D.z));

    buildSphere(meshVertices, 0.12f, 14, 14, p, pal.cursorPreviewColor);

    m_meshVAO.bind();
    m_meshVBO.bind();
    if (safeAllocateVbo(m_meshVBO, meshVertices.data(), meshVertices.size(), sizeof(float))) {
        m_meshShader->bind();
        m_meshShader->setUniformValue("uMVP", mvp);
        m_meshShader->setUniformValue("uModel", QMatrix4x4());
        m_meshShader->setUniformValue("uNormalMatrix", QMatrix3x3());
        m_meshShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        m_meshShader->setUniformValue("uLightDir", QVector3D(0.4f, 0.5f, 1.0f).normalized());
        m_meshShader->setUniformValue("uViewPos", m_camera.eyePosition());

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(meshVertices.size() / 10));
        m_meshShader->release();
    }
    m_meshVAO.release();

    // Se estiver inserindo barra e já selecionou o nó inicial, desenha linha elástica guia
    if (m_mode == InteractionMode::AddElement && m_startNodeForElement != INVALID_ID && m_model) {
        const Node* nStart = m_model->findNode(m_startNodeForElement);
        if (nStart) {
            const QVector4D c = pal.cursorPreviewColor;
            std::vector<float> rubberLine = {
                static_cast<float>(nStart->x()), static_cast<float>(nStart->y()), static_cast<float>(nStart->z()),
                c.x(), c.y(), c.z(), c.w(),
                p.x(), p.y(), p.z(),
                c.x(), c.y(), c.z(), c.w()
            };
            drawLinesAA(mvp, rubberLine, 2.0f, false, false);
        }
    }
}

void Viewport3D::renderHUD(QPainter& painter) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);

    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    auto drawShadowedText = [&](int x, int y, const QString& text, const QColor& color) {
        painter.setPen(pal.labelShadowColor);
        painter.drawText(x + 1, y + 1, text);
        painter.setPen(color);
        painter.drawText(x, y, text);
    };

    // 1. Rótulos de Reações de Apoio
    if (m_showReactions && m_model) {
        const bool anySolved = std::any_of(
            m_model->getElements().begin(),
            m_model->getElements().end(),
            [](const Element& e) { return e.hasResults; })
            || std::any_of(
                m_model->surfaceMesh().elements.begin(),
                m_model->surfaceMesh().elements.end(),
                [](const kernel::MeshElement& me) { return me.hasResults; });
        if (anySolved) {
            for (const auto& node : m_model->getNodes()) {
                if (!node.isSupported()) continue;
                QVector3D p(static_cast<float>(node.x()), static_cast<float>(node.y()), static_cast<float>(node.z()));
                QPoint sp = m_camera.worldToScreen(p, width(), height());
                if (sp.x() > 0 && sp.y() > 0) {
                    QStringList parts;
                    if (std::abs(node.reaction[0]) > 1e-3) parts << QString("Rx:%1").arg(node.reaction[0], 0, 'f', 1);
                    if (std::abs(node.reaction[1]) > 1e-3) parts << QString("Ry:%1").arg(node.reaction[1], 0, 'f', 1);
                    if (std::abs(node.reaction[2]) > 1e-3) parts << QString("Rz:%1").arg(node.reaction[2], 0, 'f', 1);
                    if (std::abs(node.reaction[3]) > 1e-3) parts << QString("Mx:%1").arg(node.reaction[3], 0, 'f', 1);
                    if (std::abs(node.reaction[4]) > 1e-3) parts << QString("My:%1").arg(node.reaction[4], 0, 'f', 1);
                    if (std::abs(node.reaction[5]) > 1e-3) parts << QString("Mz:%1").arg(node.reaction[5], 0, 'f', 1);
                    if (parts.isEmpty()) parts << QStringLiteral("R: 0.0 kN");
                    drawShadowedText(sp.x() + 8, sp.y() + 18, parts.join(' '), pal.reactionLabelColor);
                }
            }
        }
    }

    // 2. Rótulos de Cargas Concentradas
    if (m_model) {
        for (const auto& node : m_model->getNodes()) {
            if (!node.load.isActive()) continue;
            QVector3D p(static_cast<float>(node.x()), static_cast<float>(node.y()), static_cast<float>(node.z()));
            QPoint sp = m_camera.worldToScreen(p, width(), height());
            if (sp.x() > 0 && sp.y() > 0) {
                QStringList parts;
                if (std::abs(node.load.force.x) > 1e-3) parts << QString("Fx:%1kN").arg(node.load.force.x, 0, 'f', 1);
                if (std::abs(node.load.force.y) > 1e-3) parts << QString("Fy:%1kN").arg(node.load.force.y, 0, 'f', 1);
                if (std::abs(node.load.force.z) > 1e-3) parts << QString("Fz:%1kN").arg(node.load.force.z, 0, 'f', 1);
                if (std::abs(node.load.moment.x) > 1e-3) parts << QString("Mx:%1kNm").arg(node.load.moment.x, 0, 'f', 1);
                if (std::abs(node.load.moment.y) > 1e-3) parts << QString("My:%1kNm").arg(node.load.moment.y, 0, 'f', 1);
                if (std::abs(node.load.moment.z) > 1e-3) parts << QString("Mz:%1kNm").arg(node.load.moment.z, 0, 'f', 1);
                if (!parts.isEmpty()) {
                    drawShadowedText(sp.x() + 8, sp.y() - 10, parts.join(' '), pal.loadLabelColor);
                }
            }
        }
    }

    // 3. Indicador de Plano Ativo no canto superior esquerdo (em placa escura discreta)
    QString planeStr;
    switch (m_workPlane) {
        case WorkPlane::XZ_Frontal: planeStr = locale::GerenciadorTraducao::trText("Plano Ativo: XZ (Frontal)"); break;
        case WorkPlane::XY_Top:     planeStr = locale::GerenciadorTraducao::trText("Plano Ativo: XY (Planta)"); break;
        case WorkPlane::YZ_Side:    planeStr = locale::GerenciadorTraducao::trText("Plano Ativo: YZ (Lateral)"); break;
    }
    const QString hudInfo = locale::GerenciadorTraducao::trText("%1 | Cota = %2 m | Snap = %3")
                      .arg(planeStr)
                      .arg(m_planeOffset, 0, 'f', 2)
                      .arg(m_snapToGrid
                          ? locale::GerenciadorTraducao::trText("ATIVO")
                          : locale::GerenciadorTraducao::trText("DESLIGADO"));

    const QFontMetrics fm(font);
    const qreal pillW = static_cast<qreal>(fm.horizontalAdvance(hudInfo) + 24);
    const qreal pillH = static_cast<qreal>(fm.height() + 10);
    const QRectF pillRect(12.0, 10.0, pillW, pillH);
    painter.setPen(QPen(pal.hudBorderColor, 1.0));
    painter.setBrush(pal.hudBgColor);
    painter.drawRoundedRect(pillRect, 6.0, 6.0);
    painter.setPen(pal.hudTextColor);
    painter.drawText(pillRect, Qt::AlignCenter, hudInfo);

    // 3b. Indicadores ativos de Plano de Corte e Animação Modal/Não Linear
    QStringList statusBadges;
    if (m_clipAxis != ClipAxis::None) {
        const char* ax = (m_clipAxis == ClipAxis::X) ? "X" : (m_clipAxis == ClipAxis::Y ? "Y" : "Z");
        statusBadges << locale::GerenciadorTraducao::trText("Corte %1 %2 %3 m")
                            .arg(ax)
                            .arg(m_clipInverted ? QStringLiteral("<=") : QStringLiteral(">="))
                            .arg(m_clipOffset, 0, 'f', 2);
    }
    const int totalNlSteps = nonlinearStepCount();
    if (totalNlSteps > 0) {
        const int idx = std::clamp(m_nonlinearStepIdx, 0, totalNlSteps - 1);
        const double factor = (idx < static_cast<int>(m_nonlinearFrames.size()))
            ? m_nonlinearFrames[static_cast<std::size_t>(idx)].first
            : m_nonlinearMeshFrames[static_cast<std::size_t>(idx)].first;
        statusBadges << locale::GerenciadorTraducao::trText("%1 | Passo %2/%3 (λ=%4)")
                            .arg(m_postAnimLabel.isEmpty() ? locale::GerenciadorTraducao::trText("Não Linear") : m_postAnimLabel)
                            .arg(idx + 1)
                            .arg(totalNlSteps)
                            .arg(factor, 0, 'f', 3);
    } else if (!m_postAnimNodeDispl.empty() || !m_postAnimMeshDispl.empty()) {
        const int deg = static_cast<int>(std::lround(std::fmod(m_animPhase * 180.0 / std::numbers::pi, 360.0)));
        statusBadges << locale::GerenciadorTraducao::trText("%1 | %2 (%3°)")
                            .arg(m_postAnimLabel,
                                 m_animPlaying ? locale::GerenciadorTraducao::trText("Play")
                                               : locale::GerenciadorTraducao::trText("Pausa"))
                            .arg(deg);
    }
    if (!statusBadges.isEmpty()) {
        const QString badgeText = statusBadges.join(QStringLiteral("   •   "));
        const qreal bW = static_cast<qreal>(fm.horizontalAdvance(badgeText) + 22);
        const qreal bH = static_cast<qreal>(fm.height() + 8);
        const QRectF bRect(12.0, pillRect.bottom() + 6.0, bW, bH);
        painter.setPen(QPen(pal.hudBorderColor, 1.0));
        painter.setBrush(pal.hudBgColor);
        painter.drawRoundedRect(bRect, 5.0, 5.0);
        painter.setPen(pal.hudTextColor);
        painter.drawText(bRect, Qt::AlignCenter, badgeText);
    }

    // 3c. Barra de Cores (Legenda Vetorial de Deslocamentos ou Aproveitamento NBR)
    if (m_legendVisible && width() > 260 && height() > 280) {
        QFont smallFont = font;
        smallFont.setPointSize(8);
        painter.setFont(smallFont);
        const QFontMetrics lfm(smallFont);

        const qreal cardW = 132.0;
        const qreal cardH = 228.0;
        const qreal cardX = static_cast<qreal>(width()) - cardW - 14.0;
        const qreal cardY = std::max(54.0, (static_cast<qreal>(height()) - cardH) * 0.5);
        const QRectF cardRect(cardX, cardY, cardW, cardH);

        painter.setPen(QPen(pal.hudBorderColor, 1.0));
        painter.setBrush(pal.hudBgColor);
        painter.drawRoundedRect(cardRect, 7.0, 7.0);

        painter.setPen(pal.hudTextColor);
        const QRectF titleRect(cardX + 8.0, cardY + 6.0, cardW - 16.0, 18.0);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         lfm.elidedText(m_legendTitle, Qt::ElideRight, static_cast<int>(cardW - 16.0)));

        const QRectF barRect(cardX + 12.0, cardY + 30.0, 18.0, cardH - 48.0);
        QLinearGradient grad(barRect.topLeft(), barRect.bottomLeft());
        if (m_legendIsDcr) {
            for (const LegendGradientStop& stop : computeDcrLegendStops(m_legendMax)) {
                grad.setColorAt(std::clamp(stop.topDownPos, 0.0, 1.0), stop.color);
            }
        } else {
            const int nStops = 8;
            for (int i = 0; i <= nStops; ++i) {
                const double t = 1.0 - static_cast<double>(i) / nStops;
                const ColorRGB c = ColorRGB::jetThemed(t, pal.isLight);
                grad.setColorAt(static_cast<double>(i) / nStops,
                                QColor::fromRgbF(c.r, c.g, c.b));
            }
        }
        painter.setPen(QPen(pal.hudBorderColor, 1.0));
        painter.setBrush(grad);
        painter.drawRoundedRect(barRect, 3.0, 3.0);

        painter.setPen(pal.hudTextColor);
        for (int k = 0; k <= 4; ++k) {
            const double frac = 1.0 - static_cast<double>(k) / 4.0;
            const double val = m_legendMin + frac * (m_legendMax - m_legendMin);
            const qreal yTick = barRect.top() + (1.0 - frac) * barRect.height();
            painter.drawLine(QPointF(barRect.right() + 2.0, yTick), QPointF(barRect.right() + 6.0, yTick));
            const QString lbl = QStringLiteral("%1 %2").arg(val, 0, 'f', (m_legendIsDcr || m_legendMax < 10.0) ? 2 : 1).arg(m_legendUnit);
            painter.drawText(QRectF(barRect.right() + 9.0, yTick - 8.0, cardRect.right() - barRect.right() - 14.0, 16.0),
                             Qt::AlignLeft | Qt::AlignVCenter, lbl);
        }
        painter.setFont(font);
    }

    // 4. Telemetria discreta de Resolução e Definição no canto inferior esquerdo
    if (m_renderSettings.showResolutionHud && m_fboWidth > 0 && m_fboHeight > 0) {
        QFont smallFont = font;
        smallFont.setPointSize(8);
        painter.setFont(smallFont);
        const QString aaModeStr = m_renderSettings.shaderLineAA
            ? locale::GerenciadorTraducao::trText("Linhas AA: Shader SDF")
            : locale::GerenciadorTraducao::trText("Linhas AA: Hardware GL");
        const QString msaaStr = (m_activeMsaaSamples > 1)
            ? QStringLiteral("MSAA %1×").arg(m_activeMsaaSamples)
            : locale::GerenciadorTraducao::trText("MSAA Desligado");
        const QString resHud = locale::GerenciadorTraducao::trText("Resolução: %1×%2 px | SSAA %3× | %4 | %5")
            .arg(m_fboWidth)
            .arg(m_fboHeight)
            .arg(static_cast<double>(m_activeSupersample), 0, 'f', 2)
            .arg(msaaStr, aaModeStr);

        const QFontMetrics sfm(smallFont);
        const qreal resW = static_cast<qreal>(sfm.horizontalAdvance(resHud) + 22);
        const qreal resH = static_cast<qreal>(sfm.height() + 8);
        const qreal topY = std::max(44.0, static_cast<qreal>(height()) - resH - 12.0);
        const QRectF resPill(12.0, topY, resW, resH);
        painter.setPen(QPen(pal.hudBorderColor, 1.0));
        painter.setBrush(pal.hudBgColor);
        painter.drawRoundedRect(resPill, 5.0, 5.0);
        painter.setPen(pal.hudSecondaryTextColor);
        painter.drawText(resPill, Qt::AlignCenter, resHud);
    }
}

// Helpers de Geometria 3D Básica
void Viewport3D::buildCube(std::vector<float>& v, float s, const QVector3D& c, const QVector4D& col) {
    float h = s * 0.5f;
    QVector3D p[8] = {
        c + QVector3D(-h, -h, -h), c + QVector3D( h, -h, -h),
        c + QVector3D( h,  h, -h), c + QVector3D(-h,  h, -h),
        c + QVector3D(-h, -h,  h), c + QVector3D( h, -h,  h),
        c + QVector3D( h,  h,  h), c + QVector3D(-h,  h,  h)
    };

    auto addQuad = [&](int i0, int i1, int i2, int i3, const QVector3D& n) {
        auto addV = [&](const QVector3D& pt) {
            v.insert(v.end(), {pt.x(), pt.y(), pt.z(), n.x(), n.y(), n.z(), col.x(), col.y(), col.z(), col.w()});
        };
        addV(p[i0]); addV(p[i1]); addV(p[i2]);
        addV(p[i0]); addV(p[i2]); addV(p[i3]);
    };

    addQuad(0, 3, 2, 1, { 0,  0, -1}); // Bottom
    addQuad(4, 5, 6, 7, { 0,  0,  1}); // Top
    addQuad(0, 1, 5, 4, { 0, -1,  0}); // Front
    addQuad(2, 3, 7, 6, { 0,  1,  0}); // Back
    addQuad(0, 4, 7, 3, {-1,  0,  0}); // Left
    addQuad(1, 2, 6, 5, { 1,  0,  0}); // Right
}

void Viewport3D::buildPyramid(std::vector<float>& v, float b, float h, const QVector3D& apex, const QVector4D& col) {
    float hb = b * 0.5f;
    QVector3D pBase[4] = {
        apex + QVector3D(-hb, -hb, -h),
        apex + QVector3D( hb, -hb, -h),
        apex + QVector3D( hb,  hb, -h),
        apex + QVector3D(-hb,  hb, -h)
    };

    auto addTri = [&](const QVector3D& p0, const QVector3D& p1, const QVector3D& p2) {
        QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0).normalized();
        auto addV = [&](const QVector3D& pt) {
            v.insert(v.end(), {pt.x(), pt.y(), pt.z(), n.x(), n.y(), n.z(), col.x(), col.y(), col.z(), col.w()});
        };
        addV(p0); addV(p1); addV(p2);
    };

    addTri(apex, pBase[0], pBase[1]);
    addTri(apex, pBase[1], pBase[2]);
    addTri(apex, pBase[2], pBase[3]);
    addTri(apex, pBase[3], pBase[0]);
    addTri(pBase[0], pBase[2], pBase[1]);
    addTri(pBase[0], pBase[3], pBase[2]);
}

void Viewport3D::buildSphere(std::vector<float>& v, float r, int rings, int sectors, const QVector3D& c, const QVector4D& col) {
    for (int i = 0; i < rings; ++i) {
        float phi1 = static_cast<float>(PI * i / rings);
        float phi2 = static_cast<float>(PI * (i + 1) / rings);

        for (int j = 0; j < sectors; ++j) {
            float theta1 = static_cast<float>(2.0 * PI * j / sectors);
            float theta2 = static_cast<float>(2.0 * PI * (j + 1) / sectors);

            auto makePt = [&](float phi, float theta) -> std::pair<QVector3D, QVector3D> {
                float x = std::sin(phi) * std::cos(theta);
                float y = std::sin(phi) * std::sin(theta);
                float z = std::cos(phi);
                QVector3D norm(x, y, z);
                return {c + norm * r, norm};
            };

            auto p1 = makePt(phi1, theta1);
            auto p2 = makePt(phi2, theta1);
            auto p3 = makePt(phi2, theta2);
            auto p4 = makePt(phi1, theta2);

            auto addV = [&](const std::pair<QVector3D, QVector3D>& pt) {
                v.insert(v.end(), {pt.first.x(), pt.first.y(), pt.first.z(),
                                   pt.second.x(), pt.second.y(), pt.second.z(),
                                   col.x(), col.y(), col.z(), col.w()});
            };

            addV(p1); addV(p2); addV(p3);
            addV(p1); addV(p3); addV(p4);
        }
    }
}

void Viewport3D::buildUnitSphereMesh(std::vector<float>& vertices6f, int rings, int sectors) {
    vertices6f.clear();
    vertices6f.reserve(static_cast<std::size_t>(rings) * static_cast<std::size_t>(sectors) * 36u);
    for (int i = 0; i < rings; ++i) {
        const float phi1 = static_cast<float>(PI * i / rings);
        const float phi2 = static_cast<float>(PI * (i + 1) / rings);
        for (int j = 0; j < sectors; ++j) {
            const float theta1 = static_cast<float>(2.0 * PI * j / sectors);
            const float theta2 = static_cast<float>(2.0 * PI * (j + 1) / sectors);
            auto makeNorm = [](float phi, float theta) -> QVector3D {
                return QVector3D(std::sin(phi) * std::cos(theta),
                                 std::sin(phi) * std::sin(theta),
                                 std::cos(phi));
            };
            const QVector3D p1 = makeNorm(phi1, theta1);
            const QVector3D p2 = makeNorm(phi2, theta1);
            const QVector3D p3 = makeNorm(phi2, theta2);
            const QVector3D p4 = makeNorm(phi1, theta2);
            auto addV = [&](const QVector3D& pt) {
                vertices6f.insert(vertices6f.end(), {pt.x(), pt.y(), pt.z(), pt.x(), pt.y(), pt.z()});
            };
            addV(p1); addV(p2); addV(p3);
            addV(p1); addV(p3); addV(p4);
        }
    }
}

void Viewport3D::buildCone(std::vector<float>& v, float r, float h, const QVector3D& base, const QVector3D& dir, const QVector4D& col) {
    int segments = 20;
    QVector3D d = dir.normalized();
    QVector3D up = (std::abs(d.z()) < 0.9f) ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
    QVector3D u = QVector3D::crossProduct(d, up).normalized();
    QVector3D w = QVector3D::crossProduct(d, u).normalized();

    QVector3D apex = base + d * h;

    for (int i = 0; i < segments; ++i) {
        float a1 = static_cast<float>(2.0 * PI * i / segments);
        float a2 = static_cast<float>(2.0 * PI * (i + 1) / segments);

        QVector3D p1 = base + (u * std::cos(a1) + w * std::sin(a1)) * r;
        QVector3D p2 = base + (u * std::cos(a2) + w * std::sin(a2)) * r;

        QVector3D n = QVector3D::crossProduct(p2 - apex, p1 - apex).normalized();

        auto addV = [&](const QVector3D& pt) {
            v.insert(v.end(), {pt.x(), pt.y(), pt.z(), n.x(), n.y(), n.z(), col.x(), col.y(), col.z(), col.w()});
        };

        addV(apex); addV(p1); addV(p2);
    }
}

// Raycasting e Picking
bool Viewport3D::intersectRayWorkingPlane(const QVector3D& rayOrigin, const QVector3D& rayDir, Vec3& outPt) const {
    QVector3D planeNormal;
    QVector3D planePt;

    switch (m_workPlane) {
        case WorkPlane::XZ_Frontal:
            planeNormal = QVector3D(0.0f, 1.0f, 0.0f);
            planePt = QVector3D(0.0f, static_cast<float>(m_planeOffset), 0.0f);
            break;
        case WorkPlane::XY_Top:
            planeNormal = QVector3D(0.0f, 0.0f, 1.0f);
            planePt = QVector3D(0.0f, 0.0f, static_cast<float>(m_planeOffset));
            break;
        case WorkPlane::YZ_Side:
            planeNormal = QVector3D(1.0f, 0.0f, 0.0f);
            planePt = QVector3D(static_cast<float>(m_planeOffset), 0.0f, 0.0f);
            break;
    }

    float denom = QVector3D::dotProduct(planeNormal, rayDir);
    if (std::abs(denom) < 1e-6f) return false;

    float t = QVector3D::dotProduct(planePt - rayOrigin, planeNormal) / denom;
    if (t < 0.0f) return false;

    QVector3D hit = rayOrigin + rayDir * t;
    outPt = {hit.x(), hit.y(), hit.z()};
    return true;
}

Vec3 Viewport3D::applySnap(const Vec3& pt) const {
    if (!m_snapToGrid || m_gridStep < 1e-4) return pt;

    auto snapVal = [this](double val) {
        return std::round(val / m_gridStep) * m_gridStep;
    };

    switch (m_workPlane) {
        case WorkPlane::XZ_Frontal:
            return {snapVal(pt.x), m_planeOffset, snapVal(pt.z)};
        case WorkPlane::XY_Top:
            return {snapVal(pt.x), snapVal(pt.y), m_planeOffset};
        case WorkPlane::YZ_Side:
            return {m_planeOffset, snapVal(pt.y), snapVal(pt.z)};
    }
    return pt;
}

NodeId Viewport3D::pickNode(const QPoint& mousePos, double maxDistPx) const {
    if (!m_model || width() <= 0 || height() <= 0) return INVALID_ID;

    const bool hasClip = (m_clipAxis != ClipAxis::None);
    const QVector4D clipPlane = modelClipPlaneVec();

    struct NodeHit {
        NodeId id = INVALID_ID;
        double dist = 0.0;
        float ndcZ = 1.0f;
    };
    std::vector<NodeHit> hits;
    double minDist = maxDistPx + 1.0;

    for (const auto& n : m_model->getNodes()) {
        const QVector3D pos(static_cast<float>(n.x()), static_cast<float>(n.y()), static_cast<float>(n.z()));
        if (hasClip) {
            const float signedD = clipPlane.x() * pos.x() + clipPlane.y() * pos.y() + clipPlane.z() * pos.z() + clipPlane.w();
            if (signedD < -1e-5f) continue;
        }

        QPointF sp;
        float ndcZ = 1.0f;
        if (!m_camera.projectWorldToScreenF(pos, width(), height(), sp, &ndcZ)) continue;

        const double dist = std::hypot(sp.x() - mousePos.x(), sp.y() - mousePos.y());
        if (dist <= maxDistPx) {
            minDist = std::min(minDist, dist);
            hits.push_back({n.id, dist, ndcZ});
        }
    }

    if (hits.empty()) return INVALID_ID;

    const NodeHit* best = nullptr;
    for (const auto& h : hits) {
        if (h.dist > minDist + 2.0) continue;
        if (best == nullptr ||
            h.ndcZ < best->ndcZ - 1e-4f ||
            (std::abs(h.ndcZ - best->ndcZ) <= 1e-4f && h.dist < best->dist)) {
            best = &h;
        }
    }
    return best ? best->id : INVALID_ID;
}

ElementId Viewport3D::pickElement(const QPoint& mousePos, double maxDistPx) const {
    if (!m_model || width() <= 0 || height() <= 0) return INVALID_ID;

    const int wPx = width();
    const int hPx = height();
    const float aspect = static_cast<float>(wPx) / static_cast<float>(hPx);
    const QMatrix4x4 vp = m_camera.viewProjectionMatrix(aspect);
    const bool hasClip = (m_clipAxis != ClipAxis::None);
    const QVector4D clipPlane = modelClipPlaneVec();

    struct ElemHit {
        ElementId id = INVALID_ID;
        double dist = 0.0;
        float ndcZ = 1.0f;
    };
    std::vector<ElemHit> hits;
    double minDist = maxDistPx + 1.0;

    for (const auto& e : m_model->getElements()) {
        const Node* n1 = m_model->findNode(e.nodeStart);
        const Node* n2 = m_model->findNode(e.nodeEnd);
        if (!n1 || !n2) continue;

        QVector3D p1(static_cast<float>(n1->x()), static_cast<float>(n1->y()), static_cast<float>(n1->z()));
        QVector3D p2(static_cast<float>(n2->x()), static_cast<float>(n2->y()), static_cast<float>(n2->z()));

        if (hasClip && !clipSegment3DPlane(p1, p2, clipPlane)) {
            continue;
        }

        QVector4D c1 = vp.map(QVector4D(p1, 1.0f));
        QVector4D c2 = vp.map(QVector4D(p2, 1.0f));

        if (!clipSegment4DHalfSpace(c1, c2, c1.z() + c1.w(), c2.z() + c2.w())) continue;
        if (!clipSegment4DHalfSpace(c1, c2, c1.w() - c1.z(), c2.w() - c2.z())) continue;
        if (!clipSegment4DHalfSpace(c1, c2, c1.w() - 1e-5f, c2.w() - 1e-5f)) continue;
        if (c1.w() <= 1e-6f || c2.w() <= 1e-6f) continue;

        const float invW1 = 1.0f / c1.w();
        const float invW2 = 1.0f / c2.w();
        const double sx1 = (static_cast<double>(c1.x() * invW1) + 1.0) * 0.5 * static_cast<double>(wPx);
        const double sy1 = (1.0 - static_cast<double>(c1.y() * invW1)) * 0.5 * static_cast<double>(hPx);
        const float ndcZ1 = c1.z() * invW1;

        const double sx2 = (static_cast<double>(c2.x() * invW2) + 1.0) * 0.5 * static_cast<double>(wPx);
        const double sy2 = (1.0 - static_cast<double>(c2.y() * invW2)) * 0.5 * static_cast<double>(hPx);
        const float ndcZ2 = c2.z() * invW2;

        if (!std::isfinite(sx1) || !std::isfinite(sy1) || !std::isfinite(sx2) || !std::isfinite(sy2)) {
            continue;
        }

        const double dx = sx2 - sx1;
        const double dy = sy2 - sy1;
        const double lenSq = dx * dx + dy * dy;

        const double t = (lenSq < 1e-4)
            ? 0.0
            : std::clamp(((mousePos.x() - sx1) * dx + (mousePos.y() - sy1) * dy) / lenSq, 0.0, 1.0);
        const double dist = (lenSq < 1e-4)
            ? std::hypot(mousePos.x() - sx1, mousePos.y() - sy1)
            : std::hypot(mousePos.x() - (sx1 + t * dx), mousePos.y() - (sy1 + t * dy));

        if (dist <= maxDistPx) {
            const float ndcZ = ndcZ1 + static_cast<float>(t) * (ndcZ2 - ndcZ1);
            minDist = std::min(minDist, dist);
            hits.push_back({e.id, dist, ndcZ});
        }
    }

    if (hits.empty()) return INVALID_ID;

    const ElemHit* best = nullptr;
    for (const auto& h : hits) {
        if (h.dist > minDist + 2.0) continue;
        if (best == nullptr ||
            h.ndcZ < best->ndcZ - 1e-4f ||
            (std::abs(h.ndcZ - best->ndcZ) <= 1e-4f && h.dist < best->dist)) {
            best = &h;
        }
    }
    return best ? best->id : INVALID_ID;
}

// Eventos de Mouse e Teclado
void Viewport3D::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();

    if (event->button() == Qt::MiddleButton || (event->button() == Qt::RightButton && (event->modifiers() & Qt::ShiftModifier))) {
        if (event->modifiers() & Qt::ShiftModifier) {
            m_isPanning = true;
        } else {
            m_isRotating = true;
        }
        return;
    } else if (event->button() == Qt::RightButton) {
        m_isRotating = true;
        return;
    }

    if (event->button() == Qt::LeftButton && m_model) {
        switch (m_mode) {
            case InteractionMode::Select: {
                NodeId pickedN = pickNode(event->pos());
                if (pickedN != INVALID_ID) {
                    if (event->modifiers() & Qt::ControlModifier) {
                        m_model->toggleNodeSelection(pickedN);
                    } else {
                        m_model->selectNode(pickedN, true);
                    }
                    ++m_selectionRevision;
                    m_geometryDirty = true;
                    emit selectionChanged();
                } else {
                    ElementId pickedE = pickElement(event->pos());
                    if (pickedE != INVALID_ID) {
                        if (event->modifiers() & Qt::ControlModifier) {
                            m_model->toggleElementSelection(pickedE);
                        } else {
                            m_model->selectElement(pickedE, true);
                        }
                        ++m_selectionRevision;
                        m_geometryDirty = true;
                        emit selectionChanged();
                    } else if (!(event->modifiers() & Qt::ControlModifier)) {
                        m_model->clearSelection();
                        ++m_selectionRevision;
                        m_geometryDirty = true;
                        emit selectionChanged();
                    }
                }
                update();
                break;
            }

            case InteractionMode::AddNode: {
                if (m_hasCursor3D) {
                    m_model->addNode(m_currentCursor3D.x, m_currentCursor3D.y, m_currentCursor3D.z);
                    m_geometryDirty = true;
                    emit modelModified();
                    update();
                }
                break;
            }

            case InteractionMode::AddElement: {
                NodeId clickedNode = pickNode(event->pos());
                if (clickedNode == INVALID_ID) {
                    if (!m_hasCursor3D) {
                        break;
                    }
                    clickedNode = m_model->addNode(m_currentCursor3D.x, m_currentCursor3D.y, m_currentCursor3D.z);
                    m_geometryDirty = true;
                }

                if (m_startNodeForElement == INVALID_ID) {
                    m_startNodeForElement = clickedNode;
                    emit statusMessage(locale::GerenciadorTraducao::trText("Nó inicial selecionado (ID: %1). Clique no nó final.").arg(m_startNodeForElement));
                } else {
                    if (clickedNode != m_startNodeForElement) {
                        m_model->addElement(m_startNodeForElement, clickedNode);
                        m_geometryDirty = true;
                        emit modelModified();
                        emit statusMessage(locale::GerenciadorTraducao::trText("Barra criada entre nó %1 e nó %2.").arg(m_startNodeForElement).arg(clickedNode));
                        m_startNodeForElement = clickedNode; // Permite cadeia contínua de barras
                    }
                }
                update();
                break;
            }

            case InteractionMode::Measure: {
                if (!m_hasCursor3D) {
                    break;
                }
                if (!m_hasMeasureStart) {
                    m_measureStartPt = m_currentCursor3D;
                    m_hasMeasureStart = true;
                    emit statusMessage(locale::GerenciadorTraducao::trText("Ponto inicial: (%1, %2, %3). Clique no ponto final.")
                                       .arg(m_measureStartPt.x, 0, 'f', 2)
                                       .arg(m_measureStartPt.y, 0, 'f', 2)
                                       .arg(m_measureStartPt.z, 0, 'f', 2));
                } else {
                    double dist = m_measureStartPt.distanceTo(m_currentCursor3D);
                    emit statusMessage(locale::GerenciadorTraducao::trText("Distância medida: %1 m (ΔX=%2, ΔY=%3, ΔZ=%4)")
                                       .arg(dist, 0, 'f', 3)
                                       .arg(m_currentCursor3D.x - m_measureStartPt.x, 0, 'f', 2)
                                       .arg(m_currentCursor3D.y - m_measureStartPt.y, 0, 'f', 2)
                                       .arg(m_currentCursor3D.z - m_measureStartPt.z, 0, 'f', 2));
                    m_hasMeasureStart = false;
                }
                update();
                break;
            }

            case InteractionMode::Delete: {
                NodeId pickedN = pickNode(event->pos());
                if (pickedN != INVALID_ID) {
                    m_model->removeNode(pickedN);
                    m_geometryDirty = true;
                    emit modelModified();
                } else {
                    ElementId pickedE = pickElement(event->pos());
                    if (pickedE != INVALID_ID) {
                        m_model->removeElement(pickedE);
                        m_geometryDirty = true;
                        emit modelModified();
                    }
                }
                update();
                break;
            }
        }
    }
}

void Viewport3D::mouseMoveEvent(QMouseEvent* event) {
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();

    if (m_isRotating) {
        m_camera.rotate(static_cast<float>(dx) * 0.4f, static_cast<float>(dy) * 0.4f);
        update();
    } else if (m_isPanning) {
        m_camera.pan(static_cast<float>(-dx), static_cast<float>(dy));
        update();
    } else {
        // Atualiza raycasting e snapping
        QVector3D rayOrigin, rayDir;
        m_camera.screenPointToRay(event->pos().x(), event->pos().y(), width(), height(), rayOrigin, rayDir);

        NodeId hoverNode = pickNode(event->pos());
        if (hoverNode != INVALID_ID && m_model) {
            const Node* n = m_model->findNode(hoverNode);
            if (n) {
                m_currentCursor3D = n->position;
                m_hasCursor3D = true;
            }
        } else {
            Vec3 planeHit;
            if (intersectRayWorkingPlane(rayOrigin, rayDir, planeHit)) {
                m_currentCursor3D = applySnap(planeHit);
                m_hasCursor3D = true;
            } else {
                m_hasCursor3D = false;
            }
        }

        if (m_hasCursor3D) {
            emit cursorCoordinatesChanged(m_currentCursor3D.x, m_currentCursor3D.y, m_currentCursor3D.z);
        }
        update();
    }

    m_lastMousePos = event->pos();
}

void Viewport3D::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_isRotating = false;
        m_isPanning = false;
    }
}

void Viewport3D::wheelEvent(QWheelEvent* event) {
    float delta = static_cast<float>(event->angleDelta().y());
    if (std::abs(delta) > 1e-3f) {
        m_camera.zoom(delta > 0 ? 1.0f : -1.0f);
        update();
    }
}

void Viewport3D::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        if (m_model) {
            m_model->deleteSelected();
            ++m_selectionRevision;
            m_geometryDirty = true;
            emit modelModified();
            emit selectionChanged();
            update();
        }
    } else if (event->key() == Qt::Key_Escape) {
        m_startNodeForElement = INVALID_ID;
        m_hasMeasureStart = false;
        if (m_model) {
            m_model->clearSelection();
            ++m_selectionRevision;
            m_geometryDirty = true;
            emit selectionChanged();
        }
        update();
    } else {
        QOpenGLWidget::keyPressEvent(event);
    }
}

void Viewport3D::setPostContourMode(PostContourMode mode) {
    m_postContourMode = mode;
    m_geometryDirty = true;
    update();
}

void Viewport3D::setShowSurfaceMesh(bool show) {
    m_showSurfaceMesh = show;
    m_geometryDirty = true;
    update();
}

void Viewport3D::setElementDcrMap(const std::unordered_map<ElementId, double>& dcrByElement) {
    m_elementDcrMap = dcrByElement;
    ++m_dcrRevision;
    m_geometryDirty = true;
    update();
}

void Viewport3D::clearElementDcrMap() {
    m_elementDcrMap.clear();
    if (m_postContourMode == PostContourMode::NbrComplianceDcr) {
        m_postContourMode = PostContourMode::None;
    }
    ++m_dcrRevision;
    m_geometryDirty = true;
    update();
}

void Viewport3D::setClipPlane(ClipAxis axis, double offsetMeters, bool inverted) {
    if (!std::isfinite(offsetMeters)) return;
    m_clipAxis = axis;
    m_clipOffset = offsetMeters;
    m_clipInverted = inverted;
    update();
}

QVector4D Viewport3D::activeClipPlaneVec() const {
    if (m_suppressClipPlaneForDraw) {
        return QVector4D(0.0f, 0.0f, 0.0f, 1.0f);
    }
    return modelClipPlaneVec();
}

void Viewport3D::setPostAnimationField(const QString& label,
                                       const std::unordered_map<NodeId, Vec3>& modeDisplByNode,
                                       double baseScale,
                                       bool oscillating) {
    m_nonlinearFrames.clear();
    m_nonlinearMeshFrames.clear();
    m_nonlinearStepIdx = 0;
    m_postAnimLabel = label;
    m_postAnimNodeDispl = modeDisplByNode;
    m_postAnimMeshDispl.clear();
    m_animBaseScale = std::isfinite(baseScale) ? baseScale : -1.0;
    m_animOscillating = oscillating;
    m_animPhase = std::numbers::pi * 0.5;
    ++m_animRevision;
    m_geometryDirty = true;
    emit animationFrameChanged(0, 1, 1.0);
    emit postAnimationStateChanged();
    update();
}

void Viewport3D::setPostMeshAnimationField(const std::unordered_map<NodeId, Vec3>& meshDisplByNode) {
    m_postAnimMeshDispl = meshDisplByNode;
    ++m_animRevision;
    m_geometryDirty = true;
    emit postAnimationStateChanged();
    update();
}

void Viewport3D::setNonlinearStepFrames(
    const QString& label,
    const std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>>& stepFrames) {
    m_postAnimNodeDispl.clear();
    m_postAnimMeshDispl.clear();
    m_postAnimLabel = label;
    m_animBaseScale = -1.0;
    m_nonlinearFrames = stepFrames;
    m_nonlinearMeshFrames.clear();
    m_nonlinearStepIdx = m_nonlinearFrames.empty() ? 0 : static_cast<int>(m_nonlinearFrames.size()) - 1;
    const double fac = m_nonlinearFrames.empty()
        ? 0.0
        : m_nonlinearFrames[static_cast<std::size_t>(m_nonlinearStepIdx)].first;
    ++m_animRevision;
    m_geometryDirty = true;
    emit animationFrameChanged(m_nonlinearStepIdx, static_cast<int>(m_nonlinearFrames.size()), fac);
    emit postAnimationStateChanged();
    update();
}

void Viewport3D::setNonlinearMeshStepFrames(
    const std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>>& meshStepFrames) {
    m_nonlinearMeshFrames = meshStepFrames;
    const int total = nonlinearStepCount();
    if (total > 0) {
        m_nonlinearStepIdx = total - 1;
        const double fac = (m_nonlinearStepIdx < static_cast<int>(m_nonlinearFrames.size()))
            ? m_nonlinearFrames[static_cast<std::size_t>(m_nonlinearStepIdx)].first
            : m_nonlinearMeshFrames[static_cast<std::size_t>(m_nonlinearStepIdx)].first;
        emit animationFrameChanged(m_nonlinearStepIdx, total, fac);
    }
    ++m_animRevision;
    m_geometryDirty = true;
    emit postAnimationStateChanged();
    update();
}

void Viewport3D::setNonlinearStepIndex(int idx) {
    const int total = nonlinearStepCount();
    if (total <= 0) return;
    m_nonlinearStepIdx = std::clamp(idx, 0, total - 1);
    const double fac = (m_nonlinearStepIdx < static_cast<int>(m_nonlinearFrames.size()))
        ? m_nonlinearFrames[static_cast<std::size_t>(m_nonlinearStepIdx)].first
        : ((m_nonlinearStepIdx < static_cast<int>(m_nonlinearMeshFrames.size()))
            ? m_nonlinearMeshFrames[static_cast<std::size_t>(m_nonlinearStepIdx)].first
            : 0.0);
    ++m_animRevision;
    m_geometryDirty = true;
    emit animationFrameChanged(m_nonlinearStepIdx, total, fac);
    update();
}

void Viewport3D::setAnimationPlaying(bool playing) {
    if (playing && !hasPostAnimation()) {
        playing = false;
    }
    m_animPlaying = playing;
    if (m_animPlaying) {
        if (m_animTimerId == 0) {
            m_animTimerId = startTimer(33);
        }
    } else if (m_animTimerId != 0) {
        killTimer(m_animTimerId);
        m_animTimerId = 0;
    }
    emit postAnimationStateChanged();
    update();
}

void Viewport3D::setAnimationSpeed(double speed) {
    if (!std::isfinite(speed)) return;
    m_animSpeed = std::clamp(speed, 0.1, 5.0);
}

void Viewport3D::setAnimationAmplitude(double amp) {
    if (!std::isfinite(amp)) return;
    m_animAmplitude = std::clamp(amp, 0.1, 10.0);
    ++m_animRevision;
    m_geometryDirty = true;
    update();
}

void Viewport3D::clearPostAnimation() {
    m_animPlaying = false;
    if (m_animTimerId != 0) {
        killTimer(m_animTimerId);
        m_animTimerId = 0;
    }
    m_postAnimLabel.clear();
    m_postAnimNodeDispl.clear();
    m_postAnimMeshDispl.clear();
    m_nonlinearFrames.clear();
    m_nonlinearMeshFrames.clear();
    m_nonlinearStepIdx = 0;
    m_animPhase = std::numbers::pi * 0.5;
    ++m_animRevision;
    m_geometryDirty = true;
    emit postAnimationStateChanged();
    update();
}

void Viewport3D::timerEvent(QTimerEvent* event) {
    if (event->timerId() != m_animTimerId || !m_animPlaying) {
        QOpenGLWidget::timerEvent(event);
        return;
    }

    const int totalNlSteps = nonlinearStepCount();
    if (totalNlSteps > 0) {
        m_animPhase += 0.28 * m_animSpeed;
        if (m_animPhase >= 1.0) {
            m_animPhase = 0.0;
            m_nonlinearStepIdx = (m_nonlinearStepIdx + 1) % totalNlSteps;
            const double fac = (m_nonlinearStepIdx < static_cast<int>(m_nonlinearFrames.size()))
                ? m_nonlinearFrames[static_cast<std::size_t>(m_nonlinearStepIdx)].first
                : m_nonlinearMeshFrames[static_cast<std::size_t>(m_nonlinearStepIdx)].first;
            ++m_animRevision;
            m_geometryDirty = true;
            emit animationFrameChanged(m_nonlinearStepIdx, totalNlSteps, fac);
            update();
        }
        return;
    }

    if (!m_postAnimNodeDispl.empty() || !m_postAnimMeshDispl.empty()) {
        m_animPhase += 0.085 * m_animSpeed;
        if (m_animPhase > 2.0 * std::numbers::pi) {
            m_animPhase = std::fmod(m_animPhase, 2.0 * std::numbers::pi);
        }
        ++m_animRevision;
        m_geometryDirty = true;
        emit animationFrameChanged(0, 1, std::sin(m_animPhase));
        update();
    }
}

} // namespace omniframe
