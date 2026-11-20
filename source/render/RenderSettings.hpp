#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QSurfaceFormat>
#include <QVector4D>

namespace omniframe {

enum class QualityPreset {
    AutoMax = 0,
    UltraQuality,
    HighQuality,
    Balanced,
    Performance,
    Custom
};

enum class ActivePlaneTheme {
    ObsidianDark = 0,
    PureNight,
    GraphiteDark,
    CleanLight
};

enum class DownsampleFilter {
    CatmullRomSharpen = 0,
    BicubicSmooth,
    BilinearHardware
};

struct ActivePlanePalette {
    bool isLight = false;
    QVector4D clearColor;
    QVector4D planeFillColor;
    QVector4D planeBorderColor;
    QVector4D minorGridColor;
    QVector4D majorGridColor;
    QVector4D axisXColor;
    QVector4D axisYColor;
    QVector4D axisZColor;
    QVector4D memberColor;
    QVector4D memberSelectedColor;
    QVector4D memberGhostColor;
    QVector4D nodeColor;
    QVector4D nodeSelectedColor;
    QVector4D supportFixedColor;
    QVector4D supportPinnedColor;
    QVector4D supportRollerColor;
    QVector4D loadNodalColor;
    QVector4D loadDistColor;
    QVector4D diagramPosFillColor;
    QVector4D diagramPosBorderColor;
    QVector4D diagramNegFillColor;
    QVector4D diagramNegBorderColor;
    QVector4D cursorPreviewColor;
    QVector4D localAxisXColor;
    QVector4D localAxisYColor;
    QVector4D localAxisZColor;
    QColor hudBgColor;
    QColor hudBorderColor;
    QColor hudTextColor;
    QColor hudSecondaryTextColor;
    QColor labelShadowColor;
    QColor reactionLabelColor;
    QColor loadLabelColor;
};

struct RenderQualitySettings {
    QualityPreset preset = QualityPreset::AutoMax;
    bool autoMaxResolution = true;
    float resolutionScale = 2.0f;       // Fator interno de superamostragem SSAA (0.75 a 2.0)
    bool autoMsaa = true;
    int requestedMsaa = 16;             // 0, 2, 4, 8 ou 16

    bool shaderLineAA = true;           // Anti-aliasing analítico subpixel por shader
    float lineWidthMultiplier = 1.15f;  // Fator global de espessura das linhas
    float lineSoftness = 1.15f;         // Suavização das bordas em pixels (0.5 a 2.2)
    float sharpenStrength = 0.22f;      // Nitidez adaptativa no resolve (0.0 a 0.6)
    DownsampleFilter downsampleFilter = DownsampleFilter::CatmullRomSharpen;

    ActivePlaneTheme planeTheme = ActivePlaneTheme::ObsidianDark;
    float planeGridContrast = 1.15f;    // Contraste das linhas da grade do Plano Ativo
    bool showActivePlaneSurface = true; // Superfície e moldura do Plano Ativo
    bool vsyncEnabled = true;           // Sincronização vertical (VSync)
    bool showResolutionHud = true;      // Mostra definição ativa no HUD do viewport

    static RenderQualitySettings fromPreset(QualityPreset preset,
                                            ActivePlaneTheme keepTheme = ActivePlaneTheme::ObsidianDark,
                                            bool keepSurface = true,
                                            bool keepVsync = true,
                                            bool keepHud = true);

    void refreshPresetClassification();
    bool operator==(const RenderQualitySettings& other) const = default;
};

struct GraphicsCapabilitiesInfo {
    QString rendererName;
    QString vendorName;
    QString glVersion;
    int maxSamples = 0;
    int maxTextureSize = 0;
    int activeMsaaSamples = 0;
    int logicalWidth = 0;
    int logicalHeight = 0;
    qreal dpiScale = 1.0;
    float effectiveSupersample = 1.0f;
    int renderWidthPx = 0;
    int renderHeightPx = 0;
};

ActivePlanePalette activePlanePaletteForTheme(ActivePlaneTheme theme, float gridContrast = 1.0f);
QJsonObject renderSettingsToJson(const RenderQualitySettings& settings);
RenderQualitySettings renderSettingsFromJson(const QJsonObject& obj);
RenderQualitySettings loadRenderSettings();
void saveRenderSettings(const RenderQualitySettings& settings);
QSurfaceFormat buildPreferredSurfaceFormat(const RenderQualitySettings& settings);

} // namespace omniframe

