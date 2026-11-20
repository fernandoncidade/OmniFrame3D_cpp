#include "RenderSettings.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace omniframe {

namespace {

QString renderSettingsFilePath() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(configDir).filePath(QStringLiteral("graphics.json"));
}

float clampf(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

int sanitizeMsaaSamples(int samples) {
    if (samples <= 0) return 0;
    if (samples <= 2) return 2;
    if (samples <= 4) return 4;
    if (samples <= 8) return 8;
    return 16;
}

float sanitizeResolutionScale(float scale) {
    return clampf(scale, 0.75f, 2.5f);
}

bool approxEq(float a, float b, float eps = 0.02f) {
    return std::abs(a - b) <= eps;
}

} // namespace

RenderQualitySettings RenderQualitySettings::fromPreset(QualityPreset preset,
                                                        ActivePlaneTheme keepTheme,
                                                        bool keepSurface,
                                                        bool keepVsync,
                                                        bool keepHud) {
    RenderQualitySettings s;
    s.planeTheme = keepTheme;
    s.showActivePlaneSurface = keepSurface;
    s.vsyncEnabled = keepVsync;
    s.showResolutionHud = keepHud;
    s.preset = preset;

    switch (preset) {
        case QualityPreset::AutoMax:
            s.autoMaxResolution = true;
            s.resolutionScale = 2.0f;
            s.autoMsaa = true;
            s.requestedMsaa = 16;
            s.shaderLineAA = true;
            s.lineWidthMultiplier = 1.15f;
            s.lineSoftness = 1.15f;
            s.sharpenStrength = 0.22f;
            s.downsampleFilter = DownsampleFilter::CatmullRomSharpen;
            s.planeGridContrast = 1.15f;
            break;

        case QualityPreset::UltraQuality:
            s.autoMaxResolution = false;
            s.resolutionScale = 2.0f;
            s.autoMsaa = true;
            s.requestedMsaa = 16;
            s.shaderLineAA = true;
            s.lineWidthMultiplier = 1.15f;
            s.lineSoftness = 1.20f;
            s.sharpenStrength = 0.25f;
            s.downsampleFilter = DownsampleFilter::CatmullRomSharpen;
            s.planeGridContrast = 1.15f;
            break;

        case QualityPreset::HighQuality:
            s.autoMaxResolution = false;
            s.resolutionScale = 1.5f;
            s.autoMsaa = false;
            s.requestedMsaa = 8;
            s.shaderLineAA = true;
            s.lineWidthMultiplier = 1.10f;
            s.lineSoftness = 1.10f;
            s.sharpenStrength = 0.18f;
            s.downsampleFilter = DownsampleFilter::CatmullRomSharpen;
            s.planeGridContrast = 1.10f;
            break;

        case QualityPreset::Balanced:
            s.autoMaxResolution = false;
            s.resolutionScale = 1.0f;
            s.autoMsaa = false;
            s.requestedMsaa = 4;
            s.shaderLineAA = true;
            s.lineWidthMultiplier = 1.00f;
            s.lineSoftness = 1.05f;
            s.sharpenStrength = 0.10f;
            s.downsampleFilter = DownsampleFilter::BicubicSmooth;
            s.planeGridContrast = 1.05f;
            break;

        case QualityPreset::Performance:
            s.autoMaxResolution = false;
            s.resolutionScale = 1.0f;
            s.autoMsaa = false;
            s.requestedMsaa = 0;
            s.shaderLineAA = true;
            s.lineWidthMultiplier = 1.00f;
            s.lineSoftness = 1.00f;
            s.sharpenStrength = 0.0f;
            s.downsampleFilter = DownsampleFilter::BilinearHardware;
            s.planeGridContrast = 1.00f;
            break;

        case QualityPreset::Custom:
            break;
    }

    return s;
}

void RenderQualitySettings::refreshPresetClassification() {
    auto matchesCore = [this](QualityPreset p) {
        const RenderQualitySettings ref = fromPreset(p, planeTheme, showActivePlaneSurface, vsyncEnabled, showResolutionHud);
        return autoMaxResolution == ref.autoMaxResolution &&
               (autoMaxResolution || approxEq(resolutionScale, ref.resolutionScale)) &&
               autoMsaa == ref.autoMsaa &&
               (autoMsaa || requestedMsaa == ref.requestedMsaa) &&
               shaderLineAA == ref.shaderLineAA &&
               downsampleFilter == ref.downsampleFilter &&
               approxEq(lineWidthMultiplier, ref.lineWidthMultiplier);
    };

    for (QualityPreset candidate : {
             QualityPreset::AutoMax,
             QualityPreset::UltraQuality,
             QualityPreset::HighQuality,
             QualityPreset::Balanced,
             QualityPreset::Performance}) {
        if (matchesCore(candidate)) {
            preset = candidate;
            return;
        }
    }
    preset = QualityPreset::Custom;
}

ActivePlanePalette activePlanePaletteForTheme(ActivePlaneTheme theme, float gridContrast) {
    const float c = clampf(gridContrast, 0.45f, 1.80f);
    ActivePlanePalette p;

    switch (theme) {
        case ActivePlaneTheme::PureNight:
            p.isLight           = false;
            p.clearColor        = QVector4D(0.016f, 0.022f, 0.032f, 1.0f); // #040608 Ultra escuro
            p.planeFillColor    = QVector4D(0.032f, 0.048f, 0.072f, 0.24f);
            p.planeBorderColor  = QVector4D(0.18f,  0.30f,  0.46f,  clampf(0.50f * c, 0.20f, 0.90f));
            p.minorGridColor    = QVector4D(0.12f,  0.17f,  0.24f,  clampf(0.45f * c, 0.15f, 0.85f));
            p.majorGridColor    = QVector4D(0.24f,  0.33f,  0.46f,  clampf(0.82f * c, 0.30f, 0.98f));
            p.hudBgColor        = QColor(5, 8, 13, 224);
            p.hudBorderColor    = QColor(42, 60, 88, 195);
            p.hudTextColor      = QColor(226, 232, 240);
            p.hudSecondaryTextColor = QColor(148, 163, 184);
            break;

        case ActivePlaneTheme::GraphiteDark:
            p.isLight           = false;
            p.clearColor        = QVector4D(0.052f, 0.066f, 0.088f, 1.0f); // #0d1116 Grafite escuro
            p.planeFillColor    = QVector4D(0.078f, 0.102f, 0.138f, 0.22f);
            p.planeBorderColor  = QVector4D(0.26f,  0.40f,  0.58f,  clampf(0.55f * c, 0.25f, 0.92f));
            p.minorGridColor    = QVector4D(0.18f,  0.23f,  0.31f,  clampf(0.46f * c, 0.18f, 0.85f));
            p.majorGridColor    = QVector4D(0.32f,  0.41f,  0.54f,  clampf(0.82f * c, 0.32f, 0.98f));
            p.hudBgColor        = QColor(12, 16, 24, 218);
            p.hudBorderColor    = QColor(58, 76, 102, 190);
            p.hudTextColor      = QColor(232, 237, 244);
            p.hudSecondaryTextColor = QColor(160, 174, 192);
            break;

        case ActivePlaneTheme::CleanLight:
            p.isLight           = true;
            p.clearColor        = QVector4D(0.933f, 0.953f, 0.976f, 1.0f); // #eef3f9 Claro técnico
            p.planeFillColor    = QVector4D(0.855f, 0.898f, 0.949f, 0.34f);
            p.planeBorderColor  = QVector4D(0.22f,  0.38f,  0.60f,  clampf(0.68f * c, 0.28f, 0.95f));
            p.minorGridColor    = QVector4D(0.68f,  0.75f,  0.83f,  clampf(0.62f * c, 0.25f, 0.92f));
            p.majorGridColor    = QVector4D(0.44f,  0.54f,  0.67f,  clampf(0.88f * c, 0.35f, 0.98f));
            p.hudBgColor        = QColor(255, 255, 255, 235);
            p.hudBorderColor    = QColor(186, 200, 218, 220);
            p.hudTextColor      = QColor(15, 23, 42);
            p.hudSecondaryTextColor = QColor(71, 85, 105);
            break;

        case ActivePlaneTheme::ObsidianDark:
        default:
            p.isLight           = false;
            p.clearColor        = QVector4D(0.027f, 0.037f, 0.055f, 1.0f); // #07090e Fundo mais escuro padrão
            p.planeFillColor    = QVector4D(0.048f, 0.070f, 0.106f, 0.24f);
            p.planeBorderColor  = QVector4D(0.22f,  0.36f,  0.54f,  clampf(0.56f * c, 0.22f, 0.92f));
            p.minorGridColor    = QVector4D(0.14f,  0.19f,  0.27f,  clampf(0.48f * c, 0.18f, 0.88f));
            p.majorGridColor    = QVector4D(0.27f,  0.37f,  0.51f,  clampf(0.84f * c, 0.32f, 0.98f));
            p.hudBgColor        = QColor(7, 11, 18, 222);
            p.hudBorderColor    = QColor(48, 68, 96, 195);
            p.hudTextColor      = QColor(229, 235, 243);
            p.hudSecondaryTextColor = QColor(152, 169, 192);
            break;
    }

    if (p.isLight) {
        p.axisXColor            = QVector4D(0.82f, 0.13f, 0.18f, 0.96f);
        p.axisYColor            = QVector4D(0.08f, 0.56f, 0.24f, 0.96f);
        p.axisZColor            = QVector4D(0.11f, 0.38f, 0.84f, 0.96f);
        p.memberColor           = QVector4D(0.10f, 0.16f, 0.25f, 1.00f);
        p.memberSelectedColor   = QVector4D(0.88f, 0.45f, 0.02f, 1.00f);
        p.memberGhostColor      = QVector4D(0.36f, 0.44f, 0.54f, 0.38f);
        p.nodeColor             = QVector4D(0.76f, 0.20f, 0.09f, 1.00f);
        p.nodeSelectedColor     = QVector4D(0.00f, 0.48f, 0.68f, 1.00f);
        p.supportFixedColor     = QVector4D(0.09f, 0.39f, 0.75f, 1.00f);
        p.supportPinnedColor    = QVector4D(0.09f, 0.55f, 0.25f, 1.00f);
        p.supportRollerColor    = QVector4D(0.78f, 0.42f, 0.03f, 1.00f);
        p.loadNodalColor        = QVector4D(0.82f, 0.12f, 0.18f, 1.00f);
        p.loadDistColor         = QVector4D(0.80f, 0.35f, 0.02f, 0.95f);
        p.diagramPosFillColor   = QVector4D(0.11f, 0.42f, 0.84f, 0.36f);
        p.diagramPosBorderColor = QVector4D(0.06f, 0.31f, 0.72f, 1.00f);
        p.diagramNegFillColor   = QVector4D(0.84f, 0.16f, 0.16f, 0.36f);
        p.diagramNegBorderColor = QVector4D(0.72f, 0.08f, 0.11f, 1.00f);
        p.cursorPreviewColor    = QVector4D(0.72f, 0.38f, 0.00f, 0.92f);
        p.localAxisXColor       = QVector4D(0.84f, 0.12f, 0.16f, 0.96f);
        p.localAxisYColor       = QVector4D(0.08f, 0.56f, 0.22f, 0.96f);
        p.localAxisZColor       = QVector4D(0.10f, 0.36f, 0.84f, 0.96f);
        p.labelShadowColor      = QColor(248, 250, 252, 230);
        p.reactionLabelColor    = QColor(12, 110, 48);
        p.loadLabelColor        = QColor(176, 68, 4);
    } else {
        p.axisXColor            = QVector4D(0.96f, 0.27f, 0.27f, 0.95f);
        p.axisYColor            = QVector4D(0.22f, 0.88f, 0.42f, 0.95f);
        p.axisZColor            = QVector4D(0.28f, 0.56f, 1.00f, 0.95f);
        p.memberColor           = QVector4D(0.80f, 0.85f, 0.92f, 1.00f);
        p.memberSelectedColor   = QVector4D(1.00f, 0.85f, 0.15f, 1.00f);
        p.memberGhostColor      = QVector4D(0.56f, 0.63f, 0.74f, 0.34f);
        p.nodeColor             = QVector4D(0.90f, 0.35f, 0.20f, 1.00f);
        p.nodeSelectedColor     = QVector4D(0.10f, 0.95f, 1.00f, 1.00f);
        p.supportFixedColor     = QVector4D(0.20f, 0.60f, 0.90f, 1.00f);
        p.supportPinnedColor    = QVector4D(0.30f, 0.80f, 0.40f, 1.00f);
        p.supportRollerColor    = QVector4D(0.90f, 0.60f, 0.20f, 1.00f);
        p.loadNodalColor        = QVector4D(1.00f, 0.30f, 0.30f, 1.00f);
        p.loadDistColor         = QVector4D(0.95f, 0.55f, 0.15f, 0.90f);
        p.diagramPosFillColor   = QVector4D(0.20f, 0.45f, 0.90f, 0.45f);
        p.diagramPosBorderColor = QVector4D(0.25f, 0.60f, 1.00f, 1.00f);
        p.diagramNegFillColor   = QVector4D(0.90f, 0.25f, 0.20f, 0.45f);
        p.diagramNegBorderColor = QVector4D(1.00f, 0.30f, 0.25f, 1.00f);
        p.cursorPreviewColor    = QVector4D(1.00f, 1.00f, 0.20f, 0.85f);
        p.localAxisXColor       = QVector4D(1.00f, 0.25f, 0.25f, 0.95f);
        p.localAxisYColor       = QVector4D(0.25f, 0.95f, 0.35f, 0.95f);
        p.localAxisZColor       = QVector4D(0.28f, 0.52f, 1.00f, 0.95f);
        p.labelShadowColor      = QColor(4, 6, 10, 210);
        p.reactionLabelColor    = QColor(110, 255, 132);
        p.loadLabelColor        = QColor(255, 185, 108);
    }
    return p;
}

RenderQualitySettings loadRenderSettings() {
    RenderQualitySettings s = RenderQualitySettings::fromPreset(QualityPreset::AutoMax);

    const QString path = renderSettingsFilePath();
    if (!QFile::exists(path)) {
        return s;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return s;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return s;
    }

    const QJsonObject obj = doc.object();
    const int presetInt = obj.value(QStringLiteral("preset")).toInt(static_cast<int>(QualityPreset::AutoMax));
    if (presetInt >= 0 && presetInt <= static_cast<int>(QualityPreset::Custom)) {
        s.preset = static_cast<QualityPreset>(presetInt);
    }

    s.autoMaxResolution = obj.value(QStringLiteral("autoMaxResolution")).toBool(s.autoMaxResolution);
    s.resolutionScale = sanitizeResolutionScale(
        static_cast<float>(obj.value(QStringLiteral("resolutionScale")).toDouble(s.resolutionScale)));
    s.autoMsaa = obj.value(QStringLiteral("autoMsaa")).toBool(s.autoMsaa);
    s.requestedMsaa = sanitizeMsaaSamples(obj.value(QStringLiteral("requestedMsaa")).toInt(s.requestedMsaa));

    s.shaderLineAA = obj.value(QStringLiteral("shaderLineAA")).toBool(s.shaderLineAA);
    s.lineWidthMultiplier = clampf(
        static_cast<float>(obj.value(QStringLiteral("lineWidthMultiplier")).toDouble(s.lineWidthMultiplier)),
        0.65f, 2.50f);
    s.lineSoftness = clampf(
        static_cast<float>(obj.value(QStringLiteral("lineSoftness")).toDouble(s.lineSoftness)),
        0.40f, 2.40f);
    s.sharpenStrength = clampf(
        static_cast<float>(obj.value(QStringLiteral("sharpenStrength")).toDouble(s.sharpenStrength)),
        0.0f, 0.65f);

    const int filterInt = obj.value(QStringLiteral("downsampleFilter")).toInt(static_cast<int>(s.downsampleFilter));
    if (filterInt >= 0 && filterInt <= static_cast<int>(DownsampleFilter::BilinearHardware)) {
        s.downsampleFilter = static_cast<DownsampleFilter>(filterInt);
    }

    const int themeInt = obj.value(QStringLiteral("planeTheme")).toInt(static_cast<int>(s.planeTheme));
    if (themeInt >= 0 && themeInt <= static_cast<int>(ActivePlaneTheme::CleanLight)) {
        s.planeTheme = static_cast<ActivePlaneTheme>(themeInt);
    }

    s.planeGridContrast = clampf(
        static_cast<float>(obj.value(QStringLiteral("planeGridContrast")).toDouble(s.planeGridContrast)),
        0.45f, 1.80f);
    s.showActivePlaneSurface = obj.value(QStringLiteral("showActivePlaneSurface")).toBool(s.showActivePlaneSurface);
    s.vsyncEnabled = obj.value(QStringLiteral("vsyncEnabled")).toBool(s.vsyncEnabled);
    s.showResolutionHud = obj.value(QStringLiteral("showResolutionHud")).toBool(s.showResolutionHud);

    s.refreshPresetClassification();
    return s;
}

void saveRenderSettings(const RenderQualitySettings& settings) {
    const QString path = renderSettingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject obj;
    obj[QStringLiteral("preset")] = static_cast<int>(settings.preset);
    obj[QStringLiteral("autoMaxResolution")] = settings.autoMaxResolution;
    obj[QStringLiteral("resolutionScale")] = static_cast<double>(settings.resolutionScale);
    obj[QStringLiteral("autoMsaa")] = settings.autoMsaa;
    obj[QStringLiteral("requestedMsaa")] = settings.requestedMsaa;
    obj[QStringLiteral("shaderLineAA")] = settings.shaderLineAA;
    obj[QStringLiteral("lineWidthMultiplier")] = static_cast<double>(settings.lineWidthMultiplier);
    obj[QStringLiteral("lineSoftness")] = static_cast<double>(settings.lineSoftness);
    obj[QStringLiteral("sharpenStrength")] = static_cast<double>(settings.sharpenStrength);
    obj[QStringLiteral("downsampleFilter")] = static_cast<int>(settings.downsampleFilter);
    obj[QStringLiteral("planeTheme")] = static_cast<int>(settings.planeTheme);
    obj[QStringLiteral("planeGridContrast")] = static_cast<double>(settings.planeGridContrast);
    obj[QStringLiteral("showActivePlaneSurface")] = settings.showActivePlaneSurface;
    obj[QStringLiteral("vsyncEnabled")] = settings.vsyncEnabled;
    obj[QStringLiteral("showResolutionHud")] = settings.showResolutionHud;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

QSurfaceFormat buildPreferredSurfaceFormat(const RenderQualitySettings& settings) {
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setRedBufferSize(8);
    fmt.setGreenBufferSize(8);
    fmt.setBlueBufferSize(8);
    fmt.setAlphaBufferSize(8);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(settings.vsyncEnabled ? 1 : 0);
    // Solicita 4x no formato base da janela; Viewport3D gerencia MSAA ate o maximo da GPU + SSAA em FBO dedicado
    fmt.setSamples(settings.autoMsaa ? 4 : std::min(settings.requestedMsaa, 4));
    return fmt;
}

} // namespace omniframe

