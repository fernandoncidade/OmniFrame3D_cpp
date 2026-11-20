#include "Viewport3D.hpp"
#include "ShaderCollection.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QOpenGLContext>
#include <QTimerEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <unordered_map>

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

double computeModelVisualDiagonal(const StructuralModel* model) {
    if (!model) return 10.0;
    if (model->getNodes().empty() && model->surfaceMesh().nodes.empty()) return 10.0;
    Vec3 minPt, maxPt;
    model->getBoundingBox(minPt, maxPt);
    return std::max(1.0, (maxPt - minPt).norm());
}

} // namespace

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
    if (context() && context()->isValid()) {
        makeCurrent();
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
        doneCurrent();
    }
}

void Viewport3D::setModel(StructuralModel* model) {
    m_model = model;
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
    update();
}

void Viewport3D::setPlaneOffset(double offset) {
    m_planeOffset = offset;
    update();
}

void Viewport3D::setSnapToGrid(bool snap) {
    m_snapToGrid = snap;
    update();
}

void Viewport3D::setGridStep(double step) {
    m_gridStep = std::max(step, 0.05);
    update();
}

void Viewport3D::setShowLocalAxes(bool show) {
    m_showLocalAxes = show;
    update();
}

void Viewport3D::setShowReactions(bool show) {
    m_showReactions = show;
    update();
}

void Viewport3D::setActiveDiagram(DiagramType diagram) {
    m_activeDiagram = diagram;
    update();
}

void Viewport3D::setDiagramScale(double scale) {
    m_diagramScale = scale;
    update();
}

void Viewport3D::setDeformedScale(double scale) {
    m_deformedScale = scale;
    update();
}

void Viewport3D::setRenderQualitySettings(const RenderQualitySettings& settings) {
    const bool vsyncChanged = (m_renderSettings.vsyncEnabled != settings.vsyncEnabled);
    m_renderSettings = settings;
    if (vsyncChanged) {
        m_vsyncDirty = true;
    }
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
    initializeOpenGLFunctions();

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
    initBuffers();
    m_glInitialized = true;
    m_vsyncDirty = true;
}

void Viewport3D::initShaders() {
    // 1. Line Shader (fallback)
    m_lineShader = std::make_unique<QOpenGLShaderProgram>();
    m_lineShader->addShaderFromSourceCode(QOpenGLShader::Vertex, shaders::LINE_VERTEX_SHADER);
    m_lineShader->addShaderFromSourceCode(QOpenGLShader::Fragment, shaders::LINE_FRAGMENT_SHADER);
    m_lineShader->link();

    // 1b. Analytical Anti-Aliased Line Shader (screen-space quad SDF)
    m_aaLineShader = std::make_unique<QOpenGLShaderProgram>();
    m_aaLineShader->addShaderFromSourceCode(QOpenGLShader::Vertex, shaders::AA_LINE_VERTEX_SHADER);
    m_aaLineShader->addShaderFromSourceCode(QOpenGLShader::Fragment, shaders::AA_LINE_FRAGMENT_SHADER);
    m_aaLineShader->link();

    // 2. Mesh Shader
    m_meshShader = std::make_unique<QOpenGLShaderProgram>();
    m_meshShader->addShaderFromSourceCode(QOpenGLShader::Vertex, shaders::MESH_VERTEX_SHADER);
    m_meshShader->addShaderFromSourceCode(QOpenGLShader::Fragment, shaders::MESH_FRAGMENT_SHADER);
    m_meshShader->link();

    // 3. Diagram Fill Shader
    m_diagFillShader = std::make_unique<QOpenGLShaderProgram>();
    m_diagFillShader->addShaderFromSourceCode(QOpenGLShader::Vertex, shaders::DIAGRAM_FILL_VERTEX_SHADER);
    m_diagFillShader->addShaderFromSourceCode(QOpenGLShader::Fragment, shaders::DIAGRAM_FILL_FRAGMENT_SHADER);
    m_diagFillShader->link();

    // 4. Fullscreen Supersampling Resolve Shader
    m_resolveShader = std::make_unique<QOpenGLShaderProgram>();
    m_resolveShader->addShaderFromSourceCode(QOpenGLShader::Vertex, shaders::RESOLVE_VERTEX_SHADER);
    m_resolveShader->addShaderFromSourceCode(QOpenGLShader::Fragment, shaders::RESOLVE_FRAGMENT_SHADER);
    m_resolveShader->link();
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
    m_quadVBO.allocate(kQuadVertices.data(), static_cast<int>(kQuadVertices.size() * sizeof(float)));
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
    using SwapIntervalExtFn = int (*)(int);
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

        glGenRenderbuffers(1, &m_resolveDepthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_resolveDepthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_resolveDepthRbo);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
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
                glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
                destroyRenderTargets();
                return false;
            }
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        m_fboWidth = w;
        m_fboHeight = h;
        m_activeMsaaSamples = (m_msaaFbo != 0) ? trySamples : 0;
        m_activeSupersample = tryScale;
        return true;
    };

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
    return false;
}

void Viewport3D::resizeGL(int w, int h) {
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0,
               std::max(1, static_cast<int>(std::lround(w * dpr))),
               std::max(1, static_cast<int>(std::lround(h * dpr))));
}

void Viewport3D::paintGL() {
    if (m_vsyncDirty) {
        applyVsyncState();
    }

    const GLuint widgetFbo = defaultFramebufferObject();
    const qreal dpr = devicePixelRatioF();
    const int widgetPxW = std::max(1, static_cast<int>(std::lround(width() * dpr)));
    const int widgetPxH = std::max(1, static_cast<int>(std::lround(height() * dpr)));

    const bool hasCustomFbo = ensureRenderTargets(width(), height(), dpr);
    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);

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

    m_legendVisible = false;
    const bool clipActive = (m_clipAxis != ClipAxis::None);
    if (clipActive) {
        glEnable(GL_CLIP_DISTANCE0);
    } else {
        glDisable(GL_CLIP_DISTANCE0);
    }

    const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
    const QMatrix4x4 mvp = m_camera.viewProjectionMatrix(aspect);
    const QMatrix4x4 view = m_camera.viewMatrix();

    renderGrid(mvp);

    if (m_model) {
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

    renderCursorPreview(mvp);
    if (clipActive) {
        glDisable(GL_CLIP_DISTANCE0);
    }

    if (hasCustomFbo) {
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
    }

    // Restaura estado OpenGL limpo antes do QPainter (HUD e anotações)
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);

    QPainter painter(this);
    renderHUD(painter);
    painter.end();
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
        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
        glLineWidth(std::clamp(logicalWidthPx * m_renderSettings.lineWidthMultiplier, 1.0f, m_glMaxRawLineWidth));
        m_lineShader->bind();
        m_lineShader->setUniformValue("uMVP", mvp);
        m_lineShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        m_lineVAO.bind();
        m_lineVBO.bind();
        m_lineVBO.allocate(segmentVertices7f.data(), static_cast<int>(numSegments * 14 * sizeof(float)));
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(numSegments * 2));
        m_lineVAO.release();
        m_lineShader->release();
        glLineWidth(1.0f);
        glDepthMask(GL_TRUE);
        return;
    }

    std::vector<float> quadVerts;
    quadVerts.reserve(numSegments * 6 * 16);

    for (std::size_t i = 0; i < numSegments; ++i) {
        const float* seg = segmentVertices7f.data() + i * 14;
        const float ax = seg[0], ay = seg[1], az = seg[2];
        const float ar = seg[3], ag = seg[4], ab = seg[5], aa = seg[6];
        const float bx = seg[7], by = seg[8], bz = seg[9];
        const float br = seg[10], bg = seg[11], bb = seg[12], ba = seg[13];

        auto emitVertex = [&](float along, float perp) {
            quadVerts.insert(quadVerts.end(), {
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

    const float strokePx = std::max(0.65f, logicalWidthPx * m_renderSettings.lineWidthMultiplier * pixelRatio);
    const float featherPx = std::max(0.65f, m_renderSettings.lineSoftness * std::max(1.0f, pixelRatio * 0.85f));
    const GLsizei vertexCount = static_cast<GLsizei>(numSegments * 6);

    m_aaLineShader->bind();
    m_aaLineShader->setUniformValue("uMVP", mvp);
    m_aaLineShader->setUniformValue("uResolution", QVector2D(static_cast<float>(curW), static_cast<float>(curH)));
    m_aaLineShader->setUniformValue("uStrokePx", strokePx);
    m_aaLineShader->setUniformValue("uFeatherPx", featherPx);
    m_aaLineShader->setUniformValue("uClipPlane", activeClipPlaneVec());

    m_aaLineVAO.bind();
    m_aaLineVBO.bind();
    m_aaLineVBO.allocate(quadVerts.data(), static_cast<int>(quadVerts.size() * sizeof(float)));

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

void Viewport3D::renderGrid(const QMatrix4x4& mvp) {
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

    // Superfície translúcida discreta do Plano Ativo (ajuda a destacar o fundo escuro e delimitar o plano de trabalho)
    if (m_renderSettings.showActivePlaneSurface && m_diagFillShader && m_diagFillShader->isLinked()) {
        const QVector3D c00 = mapPlanePoint(minC, minC);
        const QVector3D c10 = mapPlanePoint(maxC, minC);
        const QVector3D c11 = mapPlanePoint(maxC, maxC);
        const QVector3D c01 = mapPlanePoint(minC, maxC);
        const QVector4D fc = pal.planeFillColor;

        const std::array<float, 42> surfaceTris = {
            c00.x(), c00.y(), c00.z(), fc.x(), fc.y(), fc.z(), fc.w(),
            c10.x(), c10.y(), c10.z(), fc.x(), fc.y(), fc.z(), fc.w(),
            c11.x(), c11.y(), c11.z(), fc.x(), fc.y(), fc.z(), fc.w(),

            c00.x(), c00.y(), c00.z(), fc.x(), fc.y(), fc.z(), fc.w(),
            c11.x(), c11.y(), c11.z(), fc.x(), fc.y(), fc.z(), fc.w(),
            c01.x(), c01.y(), c01.z(), fc.x(), fc.y(), fc.z(), fc.w()
        };

        glDepthMask(GL_FALSE);
        m_diagFillShader->bind();
        m_diagFillShader->setUniformValue("uMVP", mvp);
        m_diagFillShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        m_diagVAO.bind();
        m_diagVBO.bind();
        m_diagVBO.allocate(surfaceTris.data(), static_cast<int>(surfaceTris.size() * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_diagVAO.release();
        m_diagFillShader->release();
        glDepthMask(GL_TRUE);
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
        const float pos = i * step;
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

    // Eixos Principais do Mundo
    const float axLen = 3.2f;
    appendSeg(axisLines, {0.0f, 0.0f, 0.0f}, {axLen, 0.0f, 0.0f}, pal.axisXColor);
    appendSeg(axisLines, {0.0f, 0.0f, 0.0f}, {0.0f, axLen, 0.0f}, pal.axisYColor);
    appendSeg(axisLines, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, axLen}, pal.axisZColor);

    drawLinesAA(mvp, minorLines, 0.90f, false, false);
    drawLinesAA(mvp, majorLines, 1.35f, false, false);
    drawLinesAA(mvp, borderLines, 1.65f, false, false);
    drawLinesAA(mvp, axisLines, 2.35f, false, false);
}

void Viewport3D::renderElements(const QMatrix4x4& mvp) {
    if (!m_model) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    const bool showDcrContour = (m_postContourMode == PostContourMode::NbrComplianceDcr
                                 && !m_elementDcrMap.empty()
                                 && !hasPostAnimation());
    const bool showDispContour = (m_postContourMode == PostContourMode::Displacement
                                  && m_activeDiagram != DiagramType::Deformed
                                  && !hasPostAnimation());
    const bool ghostReference = (m_activeDiagram == DiagramType::Deformed || hasPostAnimation());

    double maxDcr = 0.0;
    if (showDcrContour) {
        for (const auto& [eid, val] : m_elementDcrMap) {
            Q_UNUSED(eid);
            maxDcr = std::max(maxDcr, val);
        }
        m_legendVisible = true;
        m_legendIsDcr = true;
        m_legendTitle = locale::GerenciadorTraducao::trText("Aproveitamento NBR (DCR)");
        m_legendUnit = QStringLiteral("D/C");
        m_legendMin = 0.0;
        m_legendMax = std::max(1.20, maxDcr);
    }

    double maxNodeDisp = 0.0;
    if (showDispContour) {
        for (const auto& n : m_model->getNodes()) {
            maxNodeDisp = std::max(maxNodeDisp, n.displacementNorm());
        }
        for (const auto& mn : m_model->surfaceMesh().nodes) {
            if (mn.hasResults) {
                maxNodeDisp = std::max(maxNodeDisp, mn.displacementNorm());
            }
        }
        if (maxNodeDisp > 1e-12) {
            m_legendVisible = true;
            m_legendIsDcr = false;
            m_legendTitle = locale::GerenciadorTraducao::trText("Deslocamento |u|");
            m_legendUnit = QStringLiteral("mm");
            m_legendMin = 0.0;
            m_legendMax = maxNodeDisp * 1000.0;
        }
    }

    std::vector<float> normalLines;
    std::vector<float> selectedLines;
    std::vector<float> axisLines;
    normalLines.reserve(m_model->getElements().size() * 14);

    for (const auto& elem : m_model->getElements()) {
        const Node* n1 = m_model->findNode(elem.nodeStart);
        const Node* n2 = m_model->findNode(elem.nodeEnd);
        if (!n1 || !n2) continue;

        const bool isSelected = m_model->getSelectedElements().contains(elem.id);
        auto& target = isSelected ? selectedLines : normalLines;

        if (!isSelected && showDispContour && maxNodeDisp > 1e-12) {
            constexpr int kSub = 8;
            const double u1 = n1->displacementNorm();
            const double u2 = n2->displacementNorm();
            for (int s = 0; s < kSub; ++s) {
                const double t0 = static_cast<double>(s) / kSub;
                const double t1 = static_cast<double>(s + 1) / kSub;
                const Vec3 p0 = n1->position * (1.0 - t0) + n2->position * t0;
                const Vec3 p1 = n1->position * (1.0 - t1) + n2->position * t1;
                const double v0 = elem.hasResults ? elem.evaluateAt(t0, *m_model).defGlobal.norm() : (u1 * (1.0 - t0) + u2 * t0);
                const double v1 = elem.hasResults ? elem.evaluateAt(t1, *m_model).defGlobal.norm() : (u1 * (1.0 - t1) + u2 * t1);
                const ColorRGB c0 = ColorRGB::jetThemed(v0 / maxNodeDisp, pal.isLight);
                const ColorRGB c1 = ColorRGB::jetThemed(v1 / maxNodeDisp, pal.isLight);
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

        // Eixos Locais da Barra (RGB no centro da barra)
        if (m_showLocalAxes) {
            Vec3 mid = (n1->position + n2->position) * 0.5;
            Eigen::Matrix3d R = elem.computeRotationMatrix3x3(*m_model);
            float triadLen = 0.6f;

            Eigen::Vector3d ex = R.row(0) * triadLen;
            Eigen::Vector3d ey = R.row(1) * triadLen;
            Eigen::Vector3d ez = R.row(2) * triadLen;

            auto addLocalAxis = [&](const Eigen::Vector3d& dir, const QVector4D& c) {
                axisLines.insert(axisLines.end(), {
                    static_cast<float>(mid.x), static_cast<float>(mid.y), static_cast<float>(mid.z),
                    c.x(), c.y(), c.z(), c.w(),
                    static_cast<float>(mid.x + dir.x()), static_cast<float>(mid.y + dir.y()), static_cast<float>(mid.z + dir.z()),
                    c.x(), c.y(), c.z(), c.w()
                });
            };
            addLocalAxis(ex, pal.localAxisXColor); // x local
            addLocalAxis(ey, pal.localAxisYColor); // y local
            addLocalAxis(ez, pal.localAxisZColor); // z local
        }
    }

    drawLinesAA(mvp, normalLines, ghostReference ? 1.35f : 2.35f, !ghostReference, !ghostReference);
    drawLinesAA(mvp, selectedLines, 3.10f, true, true);
    drawLinesAA(mvp, axisLines, 1.85f, true, true);
}

void Viewport3D::renderNodes(const QMatrix4x4& mvp) {
    if (!m_model) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    std::vector<float> meshVertices;
    meshVertices.reserve(m_model->getNodes().size() * 360);

    for (const auto& node : m_model->getNodes()) {
        bool isSelected = m_model->getSelectedNodes().contains(node.id);
        QVector4D col = isSelected ? pal.nodeSelectedColor : pal.nodeColor;
        float radius = isSelected ? 0.14f : 0.09f;

        QVector3D center(static_cast<float>(node.x()), static_cast<float>(node.y()), static_cast<float>(node.z()));
        buildSphere(meshVertices, radius, 16, 16, center, col);
    }

    if (!meshVertices.empty()) {
        m_meshShader->bind();
        m_meshShader->setUniformValue("uMVP", mvp);
        m_meshShader->setUniformValue("uModel", QMatrix4x4());
        m_meshShader->setUniformValue("uNormalMatrix", QMatrix3x3());
        m_meshShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        m_meshShader->setUniformValue("uLightDir", QVector3D(0.4f, 0.5f, 1.0f).normalized());
        m_meshShader->setUniformValue("uViewPos", m_camera.eyePosition());

        m_meshVAO.bind();
        m_meshVBO.bind();
        m_meshVBO.allocate(meshVertices.data(), static_cast<int>(meshVertices.size() * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(meshVertices.size() / 10));
        m_meshVAO.release();
        m_meshShader->release();
    }
}

void Viewport3D::renderSupports(const QMatrix4x4& mvp, const QMatrix4x4& view) {
    (void)view;
    if (!m_model) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    std::vector<float> meshVertices;

    for (const auto& node : m_model->getNodes()) {
        if (!node.isSupported()) continue;

        QVector3D pos(static_cast<float>(node.x()), static_cast<float>(node.y()), static_cast<float>(node.z()));

        if (node.support.isFixed()) {
            // Apoio Engastado: Cubo ancorado abaixo do nó
            buildCube(meshVertices, 0.28f, pos - QVector3D(0.0f, 0.0f, 0.14f), pal.supportFixedColor);
        } else if (node.support.isPinned()) {
            // Apoio Articulado Fixo: Pirâmide 3D
            buildPyramid(meshVertices, 0.35f, 0.35f, pos, pal.supportPinnedColor);
        } else {
            // Apoio Móvel (Rolete): Pirâmide com cilindro/base
            buildPyramid(meshVertices, 0.32f, 0.30f, pos, pal.supportRollerColor);
        }
    }

    for (const auto& mn : m_model->surfaceMesh().nodes) {
        if (!mn.isSupported()) continue;
        // Evita duplicar apoio se já existe nó de barra suportado exatamente na mesma coordenada
        bool overlapsFrameSupport = false;
        for (const auto& node : m_model->getNodes()) {
            if (!node.isSupported()) continue;
            const double dx = node.x() - mn.position.x();
            const double dy = node.y() - mn.position.y();
            const double dz = node.z() - mn.position.z();
            if ((dx * dx + dy * dy + dz * dz) <= 1e-12) {
                overlapsFrameSupport = true;
                break;
            }
        }
        if (overlapsFrameSupport) continue;

        const QVector3D pos(static_cast<float>(mn.position.x()),
                            static_cast<float>(mn.position.y()),
                            static_cast<float>(mn.position.z()));
        const bool isFixed = mn.fixed[0] && mn.fixed[1] && mn.fixed[2]
                          && mn.fixed[3] && mn.fixed[4] && mn.fixed[5];
        const bool isPinned = mn.fixed[0] && mn.fixed[1] && mn.fixed[2];
        if (isFixed) {
            buildCube(meshVertices, 0.24f, pos - QVector3D(0.0f, 0.0f, 0.12f), pal.supportFixedColor);
        } else if (isPinned) {
            buildPyramid(meshVertices, 0.28f, 0.28f, pos, pal.supportPinnedColor);
        } else {
            buildPyramid(meshVertices, 0.25f, 0.24f, pos, pal.supportRollerColor);
        }
    }

    if (!meshVertices.empty()) {
        m_meshShader->bind();
        m_meshShader->setUniformValue("uMVP", mvp);
        m_meshShader->setUniformValue("uModel", QMatrix4x4());
        m_meshShader->setUniformValue("uNormalMatrix", QMatrix3x3());
        m_meshShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        m_meshShader->setUniformValue("uLightDir", QVector3D(0.4f, 0.5f, 1.0f).normalized());
        m_meshShader->setUniformValue("uViewPos", m_camera.eyePosition());

        m_meshVAO.bind();
        m_meshVBO.bind();
        m_meshVBO.allocate(meshVertices.data(), static_cast<int>(meshVertices.size() * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(meshVertices.size() / 10));
        m_meshVAO.release();
        m_meshShader->release();
    }
}

void Viewport3D::renderLoads(const QMatrix4x4& mvp, const QMatrix4x4& view) {
    (void)view;
    if (!m_model) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    std::vector<float> lines;
    std::vector<float> cones;

    // 1. Cargas Nodais Concentradas (Forças e Momentos)
    for (const auto& node : m_model->getNodes()) {
        if (!node.load.isActive()) continue;

        QVector3D p(static_cast<float>(node.x()), static_cast<float>(node.y()), static_cast<float>(node.z()));

        if (node.load.force.normSq() > 1e-6) {
            Vec3 fn = node.load.force.normalized();
            float arrowLen = 1.2f;
            QVector3D fDir(static_cast<float>(fn.x), static_cast<float>(fn.y), static_cast<float>(fn.z));
            QVector3D tip = p;
            QVector3D base = p - fDir * arrowLen;
            const QVector4D col = pal.loadNodalColor;

            lines.insert(lines.end(), {
                base.x(), base.y(), base.z(), col.x(), col.y(), col.z(), col.w(),
                tip.x(), tip.y(), tip.z(), col.x(), col.y(), col.z(), col.w()
            });

            buildCone(cones, 0.10f, 0.25f, tip - fDir * 0.25f, fDir, col);
        }

        if (node.load.moment.normSq() > 1e-6) {
            Vec3 mn = node.load.moment.normalized();
            float arrowLen = 1.0f;
            QVector3D mDir(static_cast<float>(mn.x), static_cast<float>(mn.y), static_cast<float>(mn.z));
            QVector3D tip = p;
            QVector3D base = p - mDir * arrowLen;
            const QVector4D col = pal.loadNodalColor;

            lines.insert(lines.end(), {
                base.x(), base.y(), base.z(), col.x(), col.y(), col.z(), col.w(),
                tip.x(), tip.y(), tip.z(), col.x(), col.y(), col.z(), col.w()
            });

            // Duplo cone característico da regra da mão direita para momentos nodais concentrados
            buildCone(cones, 0.09f, 0.20f, tip - mDir * 0.20f, mDir, col);
            buildCone(cones, 0.09f, 0.20f, tip - mDir * 0.40f, mDir, col);
        }
    }

    // 2. Cargas Distribuídas em Barras ("Cortina" de setas paralelas)
    for (const auto& elem : m_model->getElements()) {
        if (!elem.distLoad.active) continue;

        const Node* n1 = m_model->findNode(elem.nodeStart);
        const Node* n2 = m_model->findNode(elem.nodeEnd);
        if (!n1 || !n2) continue;

        Vec3 qGlob{0.0, 0.0, 0.0};
        if (elem.distLoad.coordSystem == CoordSystem::Global) {
            qGlob = elem.distLoad.w;
        } else {
            Eigen::Matrix3d R = elem.computeRotationMatrix3x3(*m_model);
            Eigen::Vector3d ql(elem.distLoad.w.x, elem.distLoad.w.y, elem.distLoad.w.z);
            Eigen::Vector3d qg = R.transpose() * ql;
            qGlob = {qg.x(), qg.y(), qg.z()};
        }

        if (qGlob.normSq() < 1e-6) continue;

        Vec3 qNorm = qGlob.normalized();
        QVector3D qDir(static_cast<float>(qNorm.x), static_cast<float>(qNorm.y), static_cast<float>(qNorm.z));
        float arrowLen = 0.8f;
        const QVector4D col = pal.loadDistColor;

        int numArrows = 6;
        for (int i = 0; i <= numArrows; ++i) {
            double xi = static_cast<double>(i) / static_cast<double>(numArrows);
            Vec3 pos = n1->position * (1.0 - xi) + n2->position * xi;
            QVector3D tip(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));
            QVector3D base = tip - qDir * arrowLen;

            lines.insert(lines.end(), {
                base.x(), base.y(), base.z(), col.x(), col.y(), col.z(), col.w(),
                tip.x(), tip.y(), tip.z(), col.x(), col.y(), col.z(), col.w()
            });

            buildCone(cones, 0.06f, 0.15f, tip - qDir * 0.15f, qDir, col);

            // Linha conectando o topo das setas
            if (i > 0) {
                double prevXi = static_cast<double>(i - 1) / static_cast<double>(numArrows);
                Vec3 prevPos = n1->position * (1.0 - prevXi) + n2->position * prevXi;
                QVector3D prevBase = QVector3D(static_cast<float>(prevPos.x), static_cast<float>(prevPos.y), static_cast<float>(prevPos.z)) - qDir * arrowLen;

                lines.insert(lines.end(), {
                    prevBase.x(), prevBase.y(), prevBase.z(), col.x(), col.y(), col.z(), col.w(),
                    base.x(), base.y(), base.z(), col.x(), col.y(), col.z(), col.w()
                });
            }
        }
    }

    drawLinesAA(mvp, lines, 2.0f, true, true);

    if (!cones.empty()) {
        m_meshShader->bind();
        m_meshShader->setUniformValue("uMVP", mvp);
        m_meshShader->setUniformValue("uModel", QMatrix4x4());
        m_meshShader->setUniformValue("uNormalMatrix", QMatrix3x3());
        m_meshShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        m_meshShader->setUniformValue("uLightDir", QVector3D(0.4f, 0.5f, 1.0f).normalized());
        m_meshShader->setUniformValue("uViewPos", m_camera.eyePosition());

        m_meshVAO.bind();
        m_meshVBO.bind();
        m_meshVBO.allocate(cones.data(), static_cast<int>(cones.size() * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(cones.size() / 10));
        m_meshVAO.release();
        m_meshShader->release();
    }
}

void Viewport3D::renderDeformedShape(const QMatrix4x4& mvp) {
    if (!m_model) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    std::vector<float> lines;

    if (!m_nonlinearFrames.empty() || !m_nonlinearMeshFrames.empty()) {
        const int totalFrames = nonlinearStepCount();
        const int idx = std::clamp(m_nonlinearStepIdx, 0, std::max(0, totalFrames - 1));
        const std::unordered_map<NodeId, Vec3> emptyMap;
        const auto& dispMap = (idx < static_cast<int>(m_nonlinearFrames.size()))
            ? m_nonlinearFrames[static_cast<std::size_t>(idx)].second
            : emptyMap;
        double maxDisp = 1e-9;
        for (const auto& [nid, u] : dispMap) {
            Q_UNUSED(nid);
            maxDisp = std::max(maxDisp, u.norm());
        }
        if (idx < static_cast<int>(m_nonlinearMeshFrames.size())) {
            for (const auto& [nid, u] : m_nonlinearMeshFrames[static_cast<std::size_t>(idx)].second) {
                Q_UNUSED(nid);
                maxDisp = std::max(maxDisp, u.norm());
            }
        }
        double maxPeakAcrossFrames = maxDisp;
        for (const auto& frame : m_nonlinearFrames) {
            for (const auto& [nid, u] : frame.second) {
                Q_UNUSED(nid);
                maxPeakAcrossFrames = std::max(maxPeakAcrossFrames, u.norm());
            }
        }
        for (const auto& frame : m_nonlinearMeshFrames) {
            for (const auto& [nid, u] : frame.second) {
                Q_UNUSED(nid);
                maxPeakAcrossFrames = std::max(maxPeakAcrossFrames, u.norm());
            }
        }
        const double diag = computeModelVisualDiagonal(m_model);
        const double effScale = (m_animBaseScale <= 0.0)
            ? ((diag * 0.09) / maxPeakAcrossFrames) * (m_deformedScale / 50.0) * m_animAmplitude
            : (m_animBaseScale * m_deformedScale * m_animAmplitude);

        for (const auto& elem : m_model->getElements()) {
            const Node* n1 = m_model->findNode(elem.nodeStart);
            const Node* n2 = m_model->findNode(elem.nodeEnd);
            if (!n1 || !n2) continue;

            const Vec3 u1 = dispMap.contains(n1->id) ? dispMap.at(n1->id) : Vec3{};
            const Vec3 u2 = dispMap.contains(n2->id) ? dispMap.at(n2->id) : Vec3{};

            constexpr int Nsamples = 16;
            Vec3 prevPt;
            ColorRGB prevCol;
            for (int s = 0; s <= Nsamples; ++s) {
                const double xi = static_cast<double>(s) / Nsamples;
                const Vec3 posOrig = n1->position * (1.0 - xi) + n2->position * xi;
                const Vec3 uLocal = u1 * (1.0 - xi) + u2 * xi;
                const Vec3 posDef = posOrig + uLocal * effScale;
                const ColorRGB col = ColorRGB::jetThemed(uLocal.norm() / maxDisp, pal.isLight);
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

        m_legendVisible = true;
        m_legendIsDcr = false;
        m_legendTitle = !m_postAnimLabel.isEmpty()
            ? m_postAnimLabel
            : locale::GerenciadorTraducao::trText("Não Linear |u|");
        m_legendUnit = QStringLiteral("mm");
        m_legendMin = 0.0;
        m_legendMax = maxDisp * 1000.0;

        if (!lines.empty()) {
            drawLinesAA(mvp, lines, 2.95f, true, true);
        }
        return;
    }

    if (!m_postAnimNodeDispl.empty() || !m_postAnimMeshDispl.empty()) {
        double maxPeak = 1e-9;
        for (const auto& [nid, u] : m_postAnimNodeDispl) {
            Q_UNUSED(nid);
            maxPeak = std::max(maxPeak, u.norm());
        }
        for (const auto& [nid, u] : m_postAnimMeshDispl) {
            Q_UNUSED(nid);
            maxPeak = std::max(maxPeak, u.norm());
        }

        const double diag = computeModelVisualDiagonal(m_model);

        const double phaseFactor = m_animOscillating
            ? std::sin(m_animPhase)
            : (0.5 - 0.5 * std::cos(m_animPhase));
        const double effScale = (m_animBaseScale <= 0.0)
            ? ((diag * 0.09) / maxPeak) * (m_deformedScale / 50.0) * m_animAmplitude * phaseFactor
            : (m_animBaseScale * m_deformedScale * m_animAmplitude * phaseFactor);

        for (const auto& elem : m_model->getElements()) {
            const Node* n1 = m_model->findNode(elem.nodeStart);
            const Node* n2 = m_model->findNode(elem.nodeEnd);
            if (!n1 || !n2) continue;

            const Vec3 u1 = m_postAnimNodeDispl.contains(n1->id) ? m_postAnimNodeDispl.at(n1->id) : Vec3{};
            const Vec3 u2 = m_postAnimNodeDispl.contains(n2->id) ? m_postAnimNodeDispl.at(n2->id) : Vec3{};

            constexpr int Nsamples = 16;
            Vec3 prevPt;
            ColorRGB prevCol;
            for (int s = 0; s <= Nsamples; ++s) {
                const double xi = static_cast<double>(s) / Nsamples;
                const Vec3 posOrig = n1->position * (1.0 - xi) + n2->position * xi;
                const Vec3 uMode = u1 * (1.0 - xi) + u2 * xi;
                const Vec3 posDef = posOrig + uMode * effScale;
                const double ratio = std::clamp((uMode.norm() / maxPeak) * std::abs(phaseFactor), 0.0, 1.0);
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

        m_legendVisible = true;
        m_legendIsDcr = false;
        m_legendTitle = !m_postAnimLabel.isEmpty()
            ? m_postAnimLabel
            : locale::GerenciadorTraducao::trText("Forma Modal / Flambagem");
        m_legendUnit = (m_animBaseScale <= 0.0) ? QStringLiteral("φ/φmax") : QStringLiteral("mm");
        m_legendMin = 0.0;
        m_legendMax = (m_animBaseScale <= 0.0) ? 1.0 : (maxPeak * 1000.0);

        if (!lines.empty()) {
            drawLinesAA(mvp, lines, 2.95f, true, true);
        }
        return;
    }

    const bool showDcrContour = (m_postContourMode == PostContourMode::NbrComplianceDcr && !m_elementDcrMap.empty());

    double maxDisp = 1e-6;
    for (const auto& n : m_model->getNodes()) {
        maxDisp = std::max(maxDisp, n.displacementNorm());
    }
    for (const auto& mn : m_model->surfaceMesh().nodes) {
        if (mn.hasResults) {
            maxDisp = std::max(maxDisp, mn.displacementNorm());
        }
    }
    if (!showDcrContour && m_postContourMode != PostContourMode::VonMisesStress && maxDisp > 1e-8) {
        m_legendVisible = true;
        m_legendIsDcr = false;
        m_legendTitle = locale::GerenciadorTraducao::trText("Deslocamento |u|");
        m_legendUnit = QStringLiteral("mm");
        m_legendMin = 0.0;
        m_legendMax = maxDisp * 1000.0;
    }

    for (const auto& elem : m_model->getElements()) {
        if (!elem.hasResults) continue;
        const Node* n1 = m_model->findNode(elem.nodeStart);
        const Node* n2 = m_model->findNode(elem.nodeEnd);
        if (!n1 || !n2) continue;

        bool hasElemDcr = false;
        QVector4D dcrCol;
        if (showDcrContour) {
            if (auto it = m_elementDcrMap.find(elem.id); it != m_elementDcrMap.end()) {
                hasElemDcr = true;
                dcrCol = dcrRatioToColor(it->second);
            }
        }

        int Nsamples = 32;
        Vec3 prevPt;
        ColorRGB prevCol;

        for (int s = 0; s <= Nsamples; ++s) {
            double xi = static_cast<double>(s) / static_cast<double>(Nsamples);
            Vec3 posOrig = n1->position * (1.0 - xi) + n2->position * xi;
            InternalForces f = elem.evaluateAt(xi, *m_model);

            Vec3 posDef = posOrig + f.defGlobal * m_deformedScale;
            double normU = f.defGlobal.norm();
            ColorRGB col = ColorRGB::jetThemed(normU / maxDisp, pal.isLight);
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

    drawLinesAA(mvp, lines, 2.85f, true, true);
}

void Viewport3D::renderDiagrams(const QMatrix4x4& mvp) {
    if (!m_model) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    std::vector<float> fillTris;
    std::vector<float> borderLines;

    // Encontra valor máximo para normalização de escala
    double maxVal = 1e-6;
    for (const auto& elem : m_model->getElements()) {
        if (!elem.hasResults) continue;
        for (int s = 0; s <= 10; ++s) {
            double xi = s / 10.0;
            InternalForces f = elem.evaluateAt(xi, *m_model);
            double v = 0.0;
            switch (m_activeDiagram) {
                case DiagramType::Normal_N: v = std::abs(f.N); break;
                case DiagramType::Shear_Vy: v = std::abs(f.Vy); break;
                case DiagramType::Shear_Vz: v = std::abs(f.Vz); break;
                case DiagramType::Torsion_Tx: v = std::abs(f.Tx); break;
                case DiagramType::Moment_My: v = std::abs(f.My); break;
                case DiagramType::Moment_Mz: v = std::abs(f.Mz); break;
                default: break;
            }
            maxVal = std::max(maxVal, v);
        }
    }

    float visualScale = static_cast<float>((1.5 / maxVal) * m_diagramScale);

    for (const auto& elem : m_model->getElements()) {
        if (!elem.hasResults) continue;
        const Node* n1 = m_model->findNode(elem.nodeStart);
        const Node* n2 = m_model->findNode(elem.nodeEnd);
        if (!n1 || !n2) continue;

        Eigen::Matrix3d R = elem.computeRotationMatrix3x3(*m_model);
        Vec3 vBase1 = n1->position;
        Vec3 vBase2 = n2->position;

        // Determina vetor diretor perpendicular para plotar o diagrama
        Vec3 plotDir;
        if (m_activeDiagram == DiagramType::Moment_Mz || m_activeDiagram == DiagramType::Shear_Vy) {
            // Plano local xy: diagrama plotado ao longo do eixo local y
            plotDir = {R(1, 0), R(1, 1), R(1, 2)};
        } else if (m_activeDiagram == DiagramType::Moment_My || m_activeDiagram == DiagramType::Shear_Vz) {
            // Plano local xz: diagrama plotado ao longo do eixo local z
            plotDir = {R(2, 0), R(2, 1), R(2, 2)};
        } else {
            // Normal ou Torsor: plota no plano local principal y
            plotDir = {R(1, 0), R(1, 1), R(1, 2)};
        }

        int Nsamples = 32;
        Vec3 prevBase, prevCurve;

        for (int s = 0; s <= Nsamples; ++s) {
            double xi = static_cast<double>(s) / static_cast<double>(Nsamples);
            Vec3 currBase = vBase1 * (1.0 - xi) + vBase2 * xi;
            InternalForces f = elem.evaluateAt(xi, *m_model);

            double val = 0.0;
            switch (m_activeDiagram) {
                case DiagramType::Normal_N:   val = f.N; break;
                case DiagramType::Shear_Vy:  val = f.Vy; break;
                case DiagramType::Shear_Vz:  val = f.Vz; break;
                case DiagramType::Torsion_Tx: val = f.Tx; break;
                case DiagramType::Moment_My:  val = f.My; break;
                case DiagramType::Moment_Mz:  val = f.Mz; break;
                default: break;
            }

            // Convenção: Mz traciona no lado -y local (sign=-1), enquanto My < 0 corresponde à tração em -z local (sign=+1)
            float sign = (m_activeDiagram == DiagramType::Moment_Mz) ? -1.0f : 1.0f;
            Vec3 currCurve = currBase + plotDir * (val * visualScale * sign);

            QVector4D fillColor = (val >= 0.0) ? pal.diagramPosFillColor
                                               : pal.diagramNegFillColor;

            QVector4D borderCol = (val >= 0.0) ? pal.diagramPosBorderColor
                                               : pal.diagramNegBorderColor;

            if (s > 0) {
                // Triângulo 1
                fillTris.insert(fillTris.end(), {
                    static_cast<float>(prevBase.x), static_cast<float>(prevBase.y), static_cast<float>(prevBase.z),
                    fillColor.x(), fillColor.y(), fillColor.z(), fillColor.w(),
                    static_cast<float>(prevCurve.x), static_cast<float>(prevCurve.y), static_cast<float>(prevCurve.z),
                    fillColor.x(), fillColor.y(), fillColor.z(), fillColor.w(),
                    static_cast<float>(currCurve.x), static_cast<float>(currCurve.y), static_cast<float>(currCurve.z),
                    fillColor.x(), fillColor.y(), fillColor.z(), fillColor.w()
                });

                // Triângulo 2
                fillTris.insert(fillTris.end(), {
                    static_cast<float>(prevBase.x), static_cast<float>(prevBase.y), static_cast<float>(prevBase.z),
                    fillColor.x(), fillColor.y(), fillColor.z(), fillColor.w(),
                    static_cast<float>(currCurve.x), static_cast<float>(currCurve.y), static_cast<float>(currCurve.z),
                    fillColor.x(), fillColor.y(), fillColor.z(), fillColor.w(),
                    static_cast<float>(currBase.x), static_cast<float>(currBase.y), static_cast<float>(currBase.z),
                    fillColor.x(), fillColor.y(), fillColor.z(), fillColor.w()
                });

                // Linha de contorno externa
                borderLines.insert(borderLines.end(), {
                    static_cast<float>(prevCurve.x), static_cast<float>(prevCurve.y), static_cast<float>(prevCurve.z),
                    borderCol.x(), borderCol.y(), borderCol.z(), borderCol.w(),
                    static_cast<float>(currCurve.x), static_cast<float>(currCurve.y), static_cast<float>(currCurve.z),
                    borderCol.x(), borderCol.y(), borderCol.z(), borderCol.w()
                });
            }

            prevBase = currBase;
            prevCurve = currCurve;
        }
    }

    if (!fillTris.empty()) {
        glDepthMask(GL_FALSE);
        m_diagFillShader->bind();
        m_diagFillShader->setUniformValue("uMVP", mvp);
        m_diagFillShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        m_diagVAO.bind();
        m_diagVBO.bind();
        m_diagVBO.allocate(fillTris.data(), static_cast<int>(fillTris.size() * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fillTris.size() / 7));
        m_diagVAO.release();
        m_diagFillShader->release();
        glDepthMask(GL_TRUE);
    }

    drawLinesAA(mvp, borderLines, 2.0f, true, true);
}

void Viewport3D::renderCursorPreview(const QMatrix4x4& mvp) {
    if (!m_hasCursor3D) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);
    std::vector<float> meshVertices;
    QVector3D p(static_cast<float>(m_currentCursor3D.x),
                static_cast<float>(m_currentCursor3D.y),
                static_cast<float>(m_currentCursor3D.z));

    buildSphere(meshVertices, 0.12f, 14, 14, p, pal.cursorPreviewColor);

    m_meshShader->bind();
    m_meshShader->setUniformValue("uMVP", mvp);
    m_meshShader->setUniformValue("uModel", QMatrix4x4());
    m_meshShader->setUniformValue("uNormalMatrix", QMatrix3x3());
    m_meshShader->setUniformValue("uClipPlane", activeClipPlaneVec());
    m_meshShader->setUniformValue("uLightDir", QVector3D(0.4f, 0.5f, 1.0f).normalized());
    m_meshShader->setUniformValue("uViewPos", m_camera.eyePosition());

    m_meshVAO.bind();
    m_meshVBO.bind();
    m_meshVBO.allocate(meshVertices.data(), static_cast<int>(meshVertices.size() * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(meshVertices.size() / 10));
    m_meshVAO.release();
    m_meshShader->release();

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
            [](const Element& e) { return e.hasResults; });
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
    if (!m_nonlinearFrames.empty()) {
        const int idx = std::clamp(m_nonlinearStepIdx, 0, static_cast<int>(m_nonlinearFrames.size()) - 1);
        const double factor = m_nonlinearFrames[static_cast<std::size_t>(idx)].first;
        statusBadges << locale::GerenciadorTraducao::trText("%1 | Passo %2/%3 (λ=%4)")
                            .arg(m_postAnimLabel.isEmpty() ? locale::GerenciadorTraducao::trText("Não Linear") : m_postAnimLabel)
                            .arg(idx + 1)
                            .arg(static_cast<int>(m_nonlinearFrames.size()))
                            .arg(factor, 0, 'f', 3);
    } else if (!m_postAnimNodeDispl.empty()) {
        const int deg = static_cast<int>(std::lround(std::fmod(m_animPhase * 180.0 / std::numbers::pi, 360.0)));
        statusBadges << locale::GerenciadorTraducao::trText("%1 | %2 (%3°)")
                            .arg(m_postAnimLabel)
                            .arg(m_animPlaying ? locale::GerenciadorTraducao::trText("Play") : locale::GerenciadorTraducao::trText("Pausa"))
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
            grad.setColorAt(0.00, QColor(242, 36, 56));   // > 1.00 Falha/Alerta
            grad.setColorAt(0.20, QColor(250, 107, 20));  // ~ 0.90-1.00
            grad.setColorAt(0.45, QColor(242, 199, 31));  // ~ 0.70
            grad.setColorAt(0.75, QColor(61, 204, 82));   // ~ 0.35
            grad.setColorAt(1.00, QColor(26, 173, 173));  // 0.00 Seguro
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
            .arg(msaaStr)
            .arg(aaModeStr);

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
    if (!m_model) return INVALID_ID;

    NodeId bestNode = INVALID_ID;
    double bestDist = maxDistPx;

    for (const auto& n : m_model->getNodes()) {
        QPoint sp = m_camera.worldToScreen(QVector3D(static_cast<float>(n.x()),
                                                     static_cast<float>(n.y()),
                                                     static_cast<float>(n.z())),
                                           width(), height());
        if (sp.x() < 0 || sp.y() < 0) continue;

        double dist = std::hypot(sp.x() - mousePos.x(), sp.y() - mousePos.y());
        if (dist < bestDist) {
            bestDist = dist;
            bestNode = n.id;
        }
    }
    return bestNode;
}

ElementId Viewport3D::pickElement(const QPoint& mousePos, double maxDistPx) const {
    if (!m_model) return INVALID_ID;

    ElementId bestElem = INVALID_ID;
    double bestDist = maxDistPx;

    for (const auto& e : m_model->getElements()) {
        const Node* n1 = m_model->findNode(e.nodeStart);
        const Node* n2 = m_model->findNode(e.nodeEnd);
        if (!n1 || !n2) continue;

        QPoint sp1 = m_camera.worldToScreen(QVector3D(static_cast<float>(n1->x()), static_cast<float>(n1->y()), static_cast<float>(n1->z())), width(), height());
        QPoint sp2 = m_camera.worldToScreen(QVector3D(static_cast<float>(n2->x()), static_cast<float>(n2->y()), static_cast<float>(n2->z())), width(), height());

        if (sp1.x() < 0 || sp2.x() < 0) continue;

        double dx = sp2.x() - sp1.x();
        double dy = sp2.y() - sp1.y();
        double lenSq = dx * dx + dy * dy;

        double dist = maxDistPx + 1.0;
        if (lenSq < 1e-4) {
            dist = std::hypot(mousePos.x() - sp1.x(), mousePos.y() - sp1.y());
        } else {
            double t = std::clamp(((mousePos.x() - sp1.x()) * dx + (mousePos.y() - sp1.y()) * dy) / lenSq, 0.0, 1.0);
            double projX = sp1.x() + t * dx;
            double projY = sp1.y() + t * dy;
            dist = std::hypot(mousePos.x() - projX, mousePos.y() - projY);
        }

        if (dist < bestDist) {
            bestDist = dist;
            bestElem = e.id;
        }
    }
    return bestElem;
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
                    emit selectionChanged();
                } else {
                    ElementId pickedE = pickElement(event->pos());
                    if (pickedE != INVALID_ID) {
                        if (event->modifiers() & Qt::ControlModifier) {
                            m_model->toggleElementSelection(pickedE);
                        } else {
                            m_model->selectElement(pickedE, true);
                        }
                        emit selectionChanged();
                    } else if (!(event->modifiers() & Qt::ControlModifier)) {
                        m_model->clearSelection();
                        emit selectionChanged();
                    }
                }
                update();
                break;
            }

            case InteractionMode::AddNode: {
                if (m_hasCursor3D) {
                    m_model->addNode(m_currentCursor3D.x, m_currentCursor3D.y, m_currentCursor3D.z);
                    emit modelModified();
                    update();
                }
                break;
            }

            case InteractionMode::AddElement: {
                NodeId clickedNode = pickNode(event->pos());
                if (clickedNode == INVALID_ID && m_hasCursor3D) {
                    clickedNode = m_model->addNode(m_currentCursor3D.x, m_currentCursor3D.y, m_currentCursor3D.z);
                }

                if (m_startNodeForElement == INVALID_ID) {
                    m_startNodeForElement = clickedNode;
                    emit statusMessage(locale::GerenciadorTraducao::trText("Nó inicial selecionado (ID: %1). Clique no nó final.").arg(m_startNodeForElement));
                } else {
                    if (clickedNode != m_startNodeForElement) {
                        m_model->addElement(m_startNodeForElement, clickedNode);
                        emit modelModified();
                        emit statusMessage(locale::GerenciadorTraducao::trText("Barra criada entre nó %1 e nó %2.").arg(m_startNodeForElement).arg(clickedNode));
                        m_startNodeForElement = clickedNode; // Permite cadeia contínua de barras
                    }
                }
                update();
                break;
            }

            case InteractionMode::Measure: {
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
                    emit modelModified();
                } else {
                    ElementId pickedE = pickElement(event->pos());
                    if (pickedE != INVALID_ID) {
                        m_model->removeElement(pickedE);
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
        m_camera.rotate(dx * 0.4f, dy * 0.4f);
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
    float delta = event->angleDelta().y();
    if (std::abs(delta) > 1e-3f) {
        m_camera.zoom(delta > 0 ? 1.0f : -1.0f);
        update();
    }
}

void Viewport3D::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        if (m_model) {
            m_model->deleteSelected();
            emit modelModified();
            emit selectionChanged();
            update();
        }
    } else if (event->key() == Qt::Key_Escape) {
        m_startNodeForElement = INVALID_ID;
        m_hasMeasureStart = false;
        if (m_model) {
            m_model->clearSelection();
            emit selectionChanged();
        }
        update();
    } else {
        QOpenGLWidget::keyPressEvent(event);
    }
}

void Viewport3D::setPostContourMode(PostContourMode mode) {
    m_postContourMode = mode;
    update();
}

void Viewport3D::setShowSurfaceMesh(bool show) {
    m_showSurfaceMesh = show;
    update();
}

void Viewport3D::setElementDcrMap(const std::unordered_map<ElementId, double>& dcrByElement) {
    m_elementDcrMap = dcrByElement;
    update();
}

void Viewport3D::clearElementDcrMap() {
    m_elementDcrMap.clear();
    if (m_postContourMode == PostContourMode::NbrComplianceDcr) {
        m_postContourMode = PostContourMode::None;
    }
    update();
}

void Viewport3D::setClipPlane(ClipAxis axis, double offsetMeters, bool inverted) {
    m_clipAxis = axis;
    m_clipOffset = offsetMeters;
    m_clipInverted = inverted;
    update();
}

QVector4D Viewport3D::activeClipPlaneVec() const {
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
    m_animBaseScale = baseScale;
    m_animOscillating = oscillating;
    m_animPhase = std::numbers::pi * 0.5;
    emit animationFrameChanged(0, 1, 1.0);
    emit postAnimationStateChanged();
    update();
}

void Viewport3D::setPostMeshAnimationField(const std::unordered_map<NodeId, Vec3>& meshDisplByNode) {
    m_postAnimMeshDispl = meshDisplByNode;
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
    m_animSpeed = std::clamp(speed, 0.1, 5.0);
}

void Viewport3D::setAnimationAmplitude(double amp) {
    m_animAmplitude = std::clamp(amp, 0.1, 10.0);
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
    emit postAnimationStateChanged();
    update();
}

void Viewport3D::timerEvent(QTimerEvent* event) {
    if (event->timerId() != m_animTimerId || !m_animPlaying) {
        QOpenGLWidget::timerEvent(event);
        return;
    }

    if (!m_nonlinearFrames.empty()) {
        m_animPhase += 0.28 * m_animSpeed;
        if (m_animPhase >= 1.0) {
            m_animPhase = 0.0;
            m_nonlinearStepIdx = (m_nonlinearStepIdx + 1) % static_cast<int>(m_nonlinearFrames.size());
            emit animationFrameChanged(
                m_nonlinearStepIdx,
                static_cast<int>(m_nonlinearFrames.size()),
                m_nonlinearFrames[static_cast<std::size_t>(m_nonlinearStepIdx)].first);
            update();
        }
        return;
    }

    if (!m_postAnimNodeDispl.empty()) {
        m_animPhase += 0.085 * m_animSpeed;
        if (m_animPhase > 2.0 * std::numbers::pi) {
            m_animPhase = std::fmod(m_animPhase, 2.0 * std::numbers::pi);
        }
        emit animationFrameChanged(0, 1, std::sin(m_animPhase));
        update();
    }
}

void Viewport3D::renderSurfaceMesh(const QMatrix4x4& mvp) {
    if (!m_model) return;
    const auto& mesh = m_model->surfaceMesh();
    if (mesh.elements.empty() || mesh.nodes.empty()) return;

    const ActivePlanePalette pal = activePlanePaletteForTheme(m_renderSettings.planeTheme, m_renderSettings.planeGridContrast);

    std::unordered_map<NodeId, QVector3D> nodePosMap;
    nodePosMap.reserve(mesh.nodes.size());
    std::unordered_map<NodeId, double> nodeColorRatioMap;
    nodeColorRatioMap.reserve(mesh.nodes.size());

    const bool hasStepFrames = !m_nonlinearFrames.empty() || !m_nonlinearMeshFrames.empty();
    const bool hasModeAnim = !hasStepFrames && (!m_postAnimNodeDispl.empty() || !m_postAnimMeshDispl.empty());
    const bool isDeformedView = hasStepFrames || hasModeAnim || (m_activeDiagram == DiagramType::Deformed);

    const bool useVonMisesContour = (!hasStepFrames && !hasModeAnim && m_postContourMode == PostContourMode::VonMisesStress);
    const bool useDispContour = !useVonMisesContour && (hasStepFrames || hasModeAnim || m_postContourMode == PostContourMode::Displacement || m_activeDiagram == DiagramType::Deformed);

    double maxVonMises = 0.0;
    if (useVonMisesContour) {
        for (const auto& el : mesh.elements) {
            if (el.hasResults) {
                maxVonMises = std::max(maxVonMises, el.vonMisesStress);
            }
        }
        if (maxVonMises > 1e-8) {
            m_legendVisible = true;
            m_legendIsDcr = false;
            m_legendTitle = locale::GerenciadorTraducao::trText("Tensão de von Mises");
            m_legendUnit = QStringLiteral("kPa");
            m_legendMin = 0.0;
            m_legendMax = maxVonMises;
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
                maxDisp = std::max(maxDisp, u.norm());
            }
        }
        double maxPeakAcrossFrames = maxDisp;
        for (const auto& frame : m_nonlinearFrames) {
            for (const auto& [nid, u] : frame.second) {
                Q_UNUSED(nid);
                maxPeakAcrossFrames = std::max(maxPeakAcrossFrames, u.norm());
            }
        }
        for (const auto& frame : m_nonlinearMeshFrames) {
            for (const auto& [nid, u] : frame.second) {
                Q_UNUSED(nid);
                maxPeakAcrossFrames = std::max(maxPeakAcrossFrames, u.norm());
            }
        }
        const double diag = computeModelVisualDiagonal(m_model);
        effScale = (m_animBaseScale <= 0.0)
            ? ((diag * 0.09) / maxPeakAcrossFrames) * (m_deformedScale / 50.0) * m_animAmplitude
            : (m_animBaseScale * m_deformedScale * m_animAmplitude);
        colorNormDenom = std::max(1e-9, maxDisp);
    } else if (hasModeAnim) {
        animSourceMap = !m_postAnimMeshDispl.empty() ? &m_postAnimMeshDispl : &m_postAnimNodeDispl;
        double maxPeak = 1e-9;
        for (const auto& [nid, u] : m_postAnimNodeDispl) {
            Q_UNUSED(nid);
            maxPeak = std::max(maxPeak, u.norm());
        }
        for (const auto& [nid, u] : m_postAnimMeshDispl) {
            Q_UNUSED(nid);
            maxPeak = std::max(maxPeak, u.norm());
        }
        const double diag = computeModelVisualDiagonal(m_model);
        const double phaseFactor = m_animOscillating
            ? std::sin(m_animPhase)
            : (0.5 - 0.5 * std::cos(m_animPhase));
        effScale = (m_animBaseScale <= 0.0)
            ? ((diag * 0.09) / maxPeak) * (m_deformedScale / 50.0) * m_animAmplitude * phaseFactor
            : (m_animBaseScale * m_deformedScale * m_animAmplitude * phaseFactor);
        colorNormDenom = std::max(1e-9, maxPeak);
        animColorFactor = std::abs(phaseFactor);
    } else if (useDispContour) {
        for (const auto& sn : m_model->getNodes()) {
            maxDisp = std::max(maxDisp, sn.displacementNorm());
        }
        for (const auto& mn : mesh.nodes) {
            if (mn.hasResults) {
                maxDisp = std::max(maxDisp, mn.displacementNorm());
            }
        }
        colorNormDenom = std::max(1e-9, maxDisp);
        if (maxDisp > 1e-8) {
            m_legendVisible = true;
            m_legendIsDcr = false;
            m_legendTitle = locale::GerenciadorTraducao::trText("Deslocamento |u|");
            m_legendUnit = QStringLiteral("mm");
            m_legendMin = 0.0;
            m_legendMax = maxDisp * 1000.0;
        }
    }

    for (const auto& mn : mesh.nodes) {
        kernel::Vec3 uNode = kernel::Vec3::Zero();
        if (animSourceMap) {
            if (const auto it = animSourceMap->find(mn.id); it != animSourceMap->end()) {
                uNode = kernel::Vec3(it->second.x, it->second.y, it->second.z);
            } else if (!m_postAnimMeshDispl.empty()) {
                // Nó acoplado sem duplicação em m_postAnimMeshDispl
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

        const QVector3D pt(
            static_cast<float>(mn.position.x() + (isDeformedView ? uNode.x() * effScale : 0.0)),
            static_cast<float>(mn.position.y() + (isDeformedView ? uNode.y() * effScale : 0.0)),
            static_cast<float>(mn.position.z() + (isDeformedView ? uNode.z() * effScale : 0.0)));
        nodePosMap[mn.id] = pt;
        if (useDispContour && colorNormDenom > 1e-8) {
            nodeColorRatioMap[mn.id] = std::clamp((uNode.norm() / colorNormDenom) * animColorFactor, 0.0, 1.0);
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
    edgeLines.reserve(mesh.elements.size() * 56);

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

    if (!triVerts.empty() && m_meshShader && m_meshShader->isLinked()) {
        m_meshShader->bind();
        m_meshShader->setUniformValue("uMVP", mvp);
        m_meshShader->setUniformValue("uModel", QMatrix4x4());
        m_meshShader->setUniformValue("uNormalMatrix", QMatrix3x3());
        m_meshShader->setUniformValue("uClipPlane", activeClipPlaneVec());
        m_meshShader->setUniformValue("uLightDir", QVector3D(0.4f, 0.5f, 1.0f).normalized());
        m_meshShader->setUniformValue("uViewPos", m_camera.eyePosition());

        m_meshVAO.bind();
        m_meshVBO.bind();
        m_meshVBO.allocate(triVerts.data(), static_cast<int>(triVerts.size() * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triVerts.size() / 10));
        m_meshVAO.release();
        m_meshShader->release();
    }

    if (!edgeLines.empty()) {
        drawLinesAA(mvp, edgeLines, 1.15f, true, true);
    }
}

} // namespace omniframe
