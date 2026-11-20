#include "RenderSettingsDialog.hpp"
#include "ThemeManager.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace omniframe {

using locale::GerenciadorTraducao;

RenderSettingsDialog::RenderSettingsDialog(const RenderQualitySettings& current,
                                           const GraphicsCapabilitiesInfo& hwInfo,
                                           QWidget* parent)
    : QDialog(parent),
      m_current(current),
      m_hwInfo(hwInfo) {
    setupUi();
    retranslateUi();
    syncControlsFromSettings();
}

void RenderSettingsDialog::setupUi() {
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);
    setMinimumWidth(560);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // 1. Perfil & Resolução Interna (SSAA / MSAA)
    m_groupResolution = new QGroupBox(this);
    auto* resForm = new QFormLayout(m_groupResolution);

    m_lblPreset = new QLabel(m_groupResolution);
    m_presetCombo = new QComboBox(m_groupResolution);
    m_presetCombo->addItem(QString(), static_cast<int>(QualityPreset::AutoMax));
    m_presetCombo->addItem(QString(), static_cast<int>(QualityPreset::UltraQuality));
    m_presetCombo->addItem(QString(), static_cast<int>(QualityPreset::HighQuality));
    m_presetCombo->addItem(QString(), static_cast<int>(QualityPreset::Balanced));
    m_presetCombo->addItem(QString(), static_cast<int>(QualityPreset::Performance));
    m_presetCombo->addItem(QString(), static_cast<int>(QualityPreset::Custom));
    resForm->addRow(m_lblPreset, m_presetCombo);

    m_lblResScale = new QLabel(m_groupResolution);
    m_resScaleCombo = new QComboBox(m_groupResolution);
    m_resScaleCombo->addItem(QString(), 0);    // AutoMax
    m_resScaleCombo->addItem(QString(), 75);   // 0.75x
    m_resScaleCombo->addItem(QString(), 100);  // 1.00x
    m_resScaleCombo->addItem(QString(), 125);  // 1.25x
    m_resScaleCombo->addItem(QString(), 150);  // 1.50x
    m_resScaleCombo->addItem(QString(), 200);  // 2.00x
    resForm->addRow(m_lblResScale, m_resScaleCombo);

    m_lblMsaa = new QLabel(m_groupResolution);
    m_msaaCombo = new QComboBox(m_groupResolution);
    m_msaaCombo->addItem(QString(), -1); // Auto Max GPU
    m_msaaCombo->addItem(QString(), 0);  // Off
    m_msaaCombo->addItem(QString(), 2);
    m_msaaCombo->addItem(QString(), 4);
    m_msaaCombo->addItem(QString(), 8);
    m_msaaCombo->addItem(QString(), 16);
    resForm->addRow(m_lblMsaa, m_msaaCombo);

    m_lblFilter = new QLabel(m_groupResolution);
    m_filterCombo = new QComboBox(m_groupResolution);
    m_filterCombo->addItem(QString(), static_cast<int>(DownsampleFilter::CatmullRomSharpen));
    m_filterCombo->addItem(QString(), static_cast<int>(DownsampleFilter::BicubicSmooth));
    m_filterCombo->addItem(QString(), static_cast<int>(DownsampleFilter::BilinearHardware));
    resForm->addRow(m_lblFilter, m_filterCombo);

    m_vsyncCheck = new QCheckBox(m_groupResolution);
    resForm->addRow(QString(), m_vsyncCheck);

    mainLayout->addWidget(m_groupResolution);

    // 2. Definição de Linhas & Nitidez
    m_groupLines = new QGroupBox(this);
    auto* linesForm = new QFormLayout(m_groupLines);

    m_shaderLineAaCheck = new QCheckBox(m_groupLines);
    linesForm->addRow(QString(), m_shaderLineAaCheck);

    auto makeSliderRow = [&](QLabel*& titleLbl, QSlider*& slider, QLabel*& valueLbl,
                             int minVal, int maxVal, int step) {
        titleLbl = new QLabel(m_groupLines);
        slider = new QSlider(Qt::Horizontal, m_groupLines);
        slider->setRange(minVal, maxVal);
        slider->setSingleStep(step);
        valueLbl = new QLabel(m_groupLines);
        valueLbl->setMinimumWidth(68);
        valueLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* row = new QHBoxLayout();
        row->addWidget(slider, 1);
        row->addWidget(valueLbl);
        linesForm->addRow(titleLbl, row);
    };

    makeSliderRow(m_lblLineWidthTitle, m_lineWidthSlider, m_lineWidthValue, 70, 200, 5);
    makeSliderRow(m_lblSoftnessTitle, m_lineSoftnessSlider, m_lineSoftnessValue, 50, 200, 5);
    makeSliderRow(m_lblSharpenTitle, m_sharpenSlider, m_sharpenValue, 0, 60, 2);

    mainLayout->addWidget(m_groupLines);

    // 3. Layout e Fundo do Plano Ativo
    m_groupPlane = new QGroupBox(this);
    auto* planeForm = new QFormLayout(m_groupPlane);

    m_lblPlaneTheme = new QLabel(m_groupPlane);
    m_planeThemeCombo = new QComboBox(m_groupPlane);
    m_planeThemeCombo->addItem(QString(), static_cast<int>(ActivePlaneTheme::ObsidianDark));
    m_planeThemeCombo->addItem(QString(), static_cast<int>(ActivePlaneTheme::PureNight));
    m_planeThemeCombo->addItem(QString(), static_cast<int>(ActivePlaneTheme::GraphiteDark));
    m_planeThemeCombo->addItem(QString(), static_cast<int>(ActivePlaneTheme::CleanLight));
    planeForm->addRow(m_lblPlaneTheme, m_planeThemeCombo);

    m_lblPlaneContrastTitle = new QLabel(m_groupPlane);
    m_planeContrastSlider = new QSlider(Qt::Horizontal, m_groupPlane);
    m_planeContrastSlider->setRange(50, 180);
    m_planeContrastSlider->setSingleStep(5);
    m_planeContrastValue = new QLabel(m_groupPlane);
    m_planeContrastValue->setMinimumWidth(68);
    m_planeContrastValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* contrastRow = new QHBoxLayout();
    contrastRow->addWidget(m_planeContrastSlider, 1);
    contrastRow->addWidget(m_planeContrastValue);
    planeForm->addRow(m_lblPlaneContrastTitle, contrastRow);

    m_planeSurfaceCheck = new QCheckBox(m_groupPlane);
    planeForm->addRow(QString(), m_planeSurfaceCheck);

    m_resHudCheck = new QCheckBox(m_groupPlane);
    planeForm->addRow(QString(), m_resHudCheck);

    mainLayout->addWidget(m_groupPlane);

    // 4. Diagnóstico Gráfico
    m_groupTelemetry = new QGroupBox(this);
    auto* telForm = new QFormLayout(m_groupTelemetry);
    m_lblTelemetryGpuTitle = new QLabel(m_groupTelemetry);
    m_telemetryGpuValue = new QLabel(m_groupTelemetry);
    m_telemetryGpuValue->setWordWrap(true);
    telForm->addRow(m_lblTelemetryGpuTitle, m_telemetryGpuValue);

    m_lblTelemetryNativeTitle = new QLabel(m_groupTelemetry);
    m_telemetryNativeValue = new QLabel(m_groupTelemetry);
    telForm->addRow(m_lblTelemetryNativeTitle, m_telemetryNativeValue);

    m_lblTelemetryEffectiveTitle = new QLabel(m_groupTelemetry);
    m_telemetryEffectiveValue = new QLabel(m_groupTelemetry);
    telForm->addRow(m_lblTelemetryEffectiveTitle, m_telemetryEffectiveValue);

    mainLayout->addWidget(m_groupTelemetry);

    // Botões inferiores
    auto* footerLayout = new QHBoxLayout();
    m_btnRestoreDefaults = new QPushButton(this);
    footerLayout->addWidget(m_btnRestoreDefaults);
    footerLayout->addStretch(1);

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    footerLayout->addWidget(m_buttonBox);
    mainLayout->addLayout(footerLayout);

    // Conexões
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RenderSettingsDialog::onPresetSelected);
    connect(m_resScaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_msaaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_vsyncCheck, &QCheckBox::toggled,
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_shaderLineAaCheck, &QCheckBox::toggled,
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_lineWidthSlider, &QSlider::valueChanged,
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_lineSoftnessSlider, &QSlider::valueChanged,
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_sharpenSlider, &QSlider::valueChanged,
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_planeThemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_planeContrastSlider, &QSlider::valueChanged,
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_planeSurfaceCheck, &QCheckBox::toggled,
            this, &RenderSettingsDialog::onControlsEdited);
    connect(m_resHudCheck, &QCheckBox::toggled,
            this, &RenderSettingsDialog::onControlsEdited);

    connect(m_btnRestoreDefaults, &QPushButton::clicked,
            this, &RenderSettingsDialog::onRestoreDefaults);
    if (auto* applyBtn = m_buttonBox->button(QDialogButtonBox::Apply)) {
        connect(applyBtn, &QPushButton::clicked, this, [this]() {
            emit settingsApplied(m_current);
        });
    }
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void RenderSettingsDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void RenderSettingsDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Configurações Avançadas de Resolução e Imagem"));

    m_groupResolution->setTitle(GerenciadorTraducao::trText("Perfil de Qualidade e Resolução Interna (SSAA / MSAA)"));
    m_lblPreset->setText(GerenciadorTraducao::trText("Perfil gráfico:"));
    m_lblResScale->setText(GerenciadorTraducao::trText("Escala de resolução (SSAA):"));
    m_lblMsaa->setText(GerenciadorTraducao::trText("Multisampling (MSAA):"));
    m_lblFilter->setText(GerenciadorTraducao::trText("Filtro de amostragem:"));
    m_vsyncCheck->setText(GerenciadorTraducao::trText("Sincronização Vertical (VSync)"));

    m_presetCombo->blockSignals(true);
    m_presetCombo->setItemText(0, GerenciadorTraducao::trText("Automático (Máxima Qualidade)"));
    m_presetCombo->setItemText(1, GerenciadorTraducao::trText("Ultra (SSAA 200% + MSAA Máx)"));
    m_presetCombo->setItemText(2, GerenciadorTraducao::trText("Alta (SSAA 150% + MSAA 8×)"));
    m_presetCombo->setItemText(3, GerenciadorTraducao::trText("Equilibrada (Nativa 100% + MSAA 4×)"));
    m_presetCombo->setItemText(4, GerenciadorTraducao::trText("Desempenho (Nativa 100% + Shader AA)"));
    m_presetCombo->setItemText(5, GerenciadorTraducao::trText("Personalizado"));
    m_presetCombo->blockSignals(false);

    m_resScaleCombo->blockSignals(true);
    m_resScaleCombo->setItemText(0, GerenciadorTraducao::trText("Automática (Máxima Suportada)"));
    m_resScaleCombo->setItemText(1, GerenciadorTraducao::trText("75% (Desempenho)"));
    m_resScaleCombo->setItemText(2, GerenciadorTraducao::trText("100% (Resolução Nativa)"));
    m_resScaleCombo->setItemText(3, GerenciadorTraducao::trText("125% (Alta Definição)"));
    m_resScaleCombo->setItemText(4, GerenciadorTraducao::trText("150% (Superamostragem)"));
    m_resScaleCombo->setItemText(5, GerenciadorTraducao::trText("200% (Ultra Definição)"));
    m_resScaleCombo->blockSignals(false);

    m_msaaCombo->blockSignals(true);
    m_msaaCombo->setItemText(0, GerenciadorTraducao::trText("Automático (Máximo da GPU)"));
    m_msaaCombo->setItemText(1, GerenciadorTraducao::trText("Desligado"));
    m_msaaCombo->setItemText(2, QStringLiteral("2× MSAA"));
    m_msaaCombo->setItemText(3, QStringLiteral("4× MSAA"));
    m_msaaCombo->setItemText(4, QStringLiteral("8× MSAA"));
    m_msaaCombo->setItemText(5, QStringLiteral("16× MSAA"));
    m_msaaCombo->blockSignals(false);

    m_filterCombo->blockSignals(true);
    m_filterCombo->setItemText(0, GerenciadorTraducao::trText("Catmull-Rom 16-tap + Nitidez (Máxima Definição)"));
    m_filterCombo->setItemText(1, GerenciadorTraducao::trText("Bicúbico Suave (Sem Anéis)"));
    m_filterCombo->setItemText(2, GerenciadorTraducao::trText("Bilinear Padrão (Rápido)"));
    m_filterCombo->blockSignals(false);

    m_groupLines->setTitle(GerenciadorTraducao::trText("Definição de Linhas e Pós-Processamento"));
    m_shaderLineAaCheck->setText(GerenciadorTraducao::trText("Suavização Analítica Subpixel de Linhas (Shader SDF)"));
    m_lblLineWidthTitle->setText(GerenciadorTraducao::trText("Espessura global das linhas:"));
    m_lblSoftnessTitle->setText(GerenciadorTraducao::trText("Suavização da borda (AA):"));
    m_lblSharpenTitle->setText(GerenciadorTraducao::trText("Nitidez pós-resolve (CAS):"));

    m_groupPlane->setTitle(GerenciadorTraducao::trText("Layout e Fundo do Plano Ativo"));
    m_lblPlaneTheme->setText(GerenciadorTraducao::trText("Fundo do Plano Ativo:"));
    m_planeThemeCombo->blockSignals(true);
    m_planeThemeCombo->setItemText(0, GerenciadorTraducao::trText("Obsidiana Escuro (Padrão)"));
    m_planeThemeCombo->setItemText(1, GerenciadorTraducao::trText("Noite Profunda (Ultra Escuro)"));
    m_planeThemeCombo->setItemText(2, GerenciadorTraducao::trText("Grafite Escuro"));
    m_planeThemeCombo->setItemText(3, GerenciadorTraducao::trText("Claro Técnico"));
    m_planeThemeCombo->blockSignals(false);

    m_lblPlaneContrastTitle->setText(GerenciadorTraducao::trText("Contraste da grade:"));
    m_planeSurfaceCheck->setText(GerenciadorTraducao::trText("Exibir superfície translúcida e moldura do Plano Ativo"));
    m_resHudCheck->setText(GerenciadorTraducao::trText("Exibir telemetria de resolução no viewport"));

    m_groupTelemetry->setTitle(GerenciadorTraducao::trText("Diagnóstico de Hardware e Definição Ativa"));
    m_lblTelemetryGpuTitle->setText(GerenciadorTraducao::trText("Placa / API OpenGL:"));
    m_lblTelemetryNativeTitle->setText(GerenciadorTraducao::trText("Viewport Nativo / DPI:"));
    m_lblTelemetryEffectiveTitle->setText(GerenciadorTraducao::trText("Buffer Efetivo (SSAA + MSAA):"));

    m_btnRestoreDefaults->setText(GerenciadorTraducao::trText("Restaurar Máxima Qualidade"));
    if (auto* okBtn = m_buttonBox->button(QDialogButtonBox::Ok)) {
        okBtn->setText(GerenciadorTraducao::trText("OK"));
    }
    if (auto* applyBtn = m_buttonBox->button(QDialogButtonBox::Apply)) {
        applyBtn->setText(GerenciadorTraducao::trText("Aplicar"));
    }
    if (auto* cancelBtn = m_buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setText(GerenciadorTraducao::trText("Cancelar"));
    }

    syncLabelsFromSettings();
    updateHardwareInfo(m_hwInfo);
}

void RenderSettingsDialog::syncControlsFromSettings() {
    m_updatingControls = true;

    const int presetIdx = m_presetCombo->findData(static_cast<int>(m_current.preset));
    if (presetIdx >= 0) m_presetCombo->setCurrentIndex(presetIdx);

    const int scaleTarget = m_current.autoMaxResolution
        ? 0
        : static_cast<int>(std::lround(m_current.resolutionScale * 100.0f));
    const int scaleIdx = m_resScaleCombo->findData(scaleTarget);
    if (scaleIdx >= 0) m_resScaleCombo->setCurrentIndex(scaleIdx);

    const int msaaTarget = m_current.autoMsaa ? -1 : m_current.requestedMsaa;
    const int msaaIdx = m_msaaCombo->findData(msaaTarget);
    if (msaaIdx >= 0) m_msaaCombo->setCurrentIndex(msaaIdx);

    const int filterIdx = m_filterCombo->findData(static_cast<int>(m_current.downsampleFilter));
    if (filterIdx >= 0) m_filterCombo->setCurrentIndex(filterIdx);

    m_vsyncCheck->setChecked(m_current.vsyncEnabled);
    m_shaderLineAaCheck->setChecked(m_current.shaderLineAA);
    m_lineWidthSlider->setValue(std::clamp(static_cast<int>(std::lround(m_current.lineWidthMultiplier * 100.0f)), 70, 200));
    m_lineSoftnessSlider->setValue(std::clamp(static_cast<int>(std::lround(m_current.lineSoftness * 100.0f)), 50, 200));
    m_sharpenSlider->setValue(std::clamp(static_cast<int>(std::lround(m_current.sharpenStrength * 100.0f)), 0, 60));

    const int themeIdx = m_planeThemeCombo->findData(static_cast<int>(m_current.planeTheme));
    if (themeIdx >= 0) m_planeThemeCombo->setCurrentIndex(themeIdx);

    m_planeContrastSlider->setValue(std::clamp(static_cast<int>(std::lround(m_current.planeGridContrast * 100.0f)), 50, 180));
    m_planeSurfaceCheck->setChecked(m_current.showActivePlaneSurface);
    m_resHudCheck->setChecked(m_current.showResolutionHud);

    syncLabelsFromSettings();
    m_updatingControls = false;
}

void RenderSettingsDialog::syncLabelsFromSettings() {
    m_lineWidthValue->setText(QStringLiteral("%1×").arg(static_cast<double>(m_current.lineWidthMultiplier), 0, 'f', 2));
    m_lineSoftnessValue->setText(QStringLiteral("%1 px").arg(static_cast<double>(m_current.lineSoftness), 0, 'f', 2));
    m_sharpenValue->setText(QStringLiteral("%1%").arg(static_cast<int>(std::lround(m_current.sharpenStrength * 100.0f))));
    m_planeContrastValue->setText(QStringLiteral("%1%").arg(static_cast<int>(std::lround(m_current.planeGridContrast * 100.0f))));

    const bool hasAaShader = m_current.shaderLineAA;
    m_lineSoftnessSlider->setEnabled(hasAaShader);
    const bool usesCubic = (m_current.downsampleFilter != DownsampleFilter::BilinearHardware);
    m_sharpenSlider->setEnabled(usesCubic);
}

void RenderSettingsDialog::updateHardwareInfo(const GraphicsCapabilitiesInfo& hwInfo) {
    m_hwInfo = hwInfo;
    if (!m_telemetryGpuValue) return;

    m_telemetryGpuValue->setText(
        GerenciadorTraducao::trText("%1 | OpenGL %2 | MSAA Máx. GPU: %3×")
            .arg(m_hwInfo.rendererName.isEmpty() ? QStringLiteral("OpenGL") : m_hwInfo.rendererName,
                 m_hwInfo.glVersion.isEmpty() ? QStringLiteral("3.3 Core") : m_hwInfo.glVersion)
            .arg(m_hwInfo.maxSamples));

    m_telemetryNativeValue->setText(
        GerenciadorTraducao::trText("%1×%2 lógico | Escala DPI: %3×")
            .arg(m_hwInfo.logicalWidth)
            .arg(m_hwInfo.logicalHeight)
            .arg(m_hwInfo.dpiScale, 0, 'f', 2));

    const QString msaaLabel = (m_hwInfo.activeMsaaSamples > 1)
        ? QStringLiteral("%1× MSAA").arg(m_hwInfo.activeMsaaSamples)
        : GerenciadorTraducao::trText("MSAA Desligado");

    m_telemetryEffectiveValue->setText(
        GerenciadorTraducao::trText("%1×%2 px | SSAA %3× | %4")
            .arg(m_hwInfo.renderWidthPx)
            .arg(m_hwInfo.renderHeightPx)
            .arg(static_cast<double>(m_hwInfo.effectiveSupersample), 0, 'f', 2)
            .arg(msaaLabel));
}

void RenderSettingsDialog::onPresetSelected(int index) {
    if (m_updatingControls || index < 0) return;

    const auto preset = static_cast<QualityPreset>(m_presetCombo->itemData(index).toInt());
    if (preset == QualityPreset::Custom) {
        m_current.preset = QualityPreset::Custom;
        return;
    }

    m_current = RenderQualitySettings::fromPreset(
        preset,
        m_current.planeTheme,
        m_current.showActivePlaneSurface,
        m_current.vsyncEnabled,
        m_current.showResolutionHud);

    syncControlsFromSettings();
    emit settingsPreviewChanged(m_current);
}

void RenderSettingsDialog::onControlsEdited() {
    if (m_updatingControls) return;

    const int scaleData = m_resScaleCombo->currentData().toInt();
    m_current.autoMaxResolution = (scaleData == 0);
    if (scaleData > 0) {
        m_current.resolutionScale = static_cast<float>(scaleData) / 100.0f;
    } else {
        m_current.resolutionScale = 2.0f;
    }

    const int msaaData = m_msaaCombo->currentData().toInt();
    m_current.autoMsaa = (msaaData < 0);
    m_current.requestedMsaa = (msaaData < 0) ? 16 : msaaData;

    m_current.downsampleFilter = static_cast<DownsampleFilter>(m_filterCombo->currentData().toInt());
    m_current.vsyncEnabled = m_vsyncCheck->isChecked();
    m_current.shaderLineAA = m_shaderLineAaCheck->isChecked();
    m_current.lineWidthMultiplier = static_cast<float>(m_lineWidthSlider->value()) / 100.0f;
    m_current.lineSoftness = static_cast<float>(m_lineSoftnessSlider->value()) / 100.0f;
    m_current.sharpenStrength = static_cast<float>(m_sharpenSlider->value()) / 100.0f;
    m_current.planeTheme = static_cast<ActivePlaneTheme>(m_planeThemeCombo->currentData().toInt());
    m_current.planeGridContrast = static_cast<float>(m_planeContrastSlider->value()) / 100.0f;
    m_current.showActivePlaneSurface = m_planeSurfaceCheck->isChecked();
    m_current.showResolutionHud = m_resHudCheck->isChecked();

    m_current.refreshPresetClassification();

    m_updatingControls = true;
    const int presetIdx = m_presetCombo->findData(static_cast<int>(m_current.preset));
    if (presetIdx >= 0) m_presetCombo->setCurrentIndex(presetIdx);
    syncLabelsFromSettings();
    m_updatingControls = false;

    emit settingsPreviewChanged(m_current);
}

void RenderSettingsDialog::syncSettings(const RenderQualitySettings& settings,
                                        const GraphicsCapabilitiesInfo& hwInfo) {
    m_current = settings;
    m_hwInfo = hwInfo;
    syncControlsFromSettings();
    updateHardwareInfo(m_hwInfo);
}

void RenderSettingsDialog::onRestoreDefaults() {
    const ActivePlaneTheme defaultTheme = ThemeManager::instance().isLight()
        ? ActivePlaneTheme::CleanLight
        : ActivePlaneTheme::ObsidianDark;
    m_current = RenderQualitySettings::fromPreset(QualityPreset::AutoMax, defaultTheme);
    syncControlsFromSettings();
    emit settingsPreviewChanged(m_current);
}

} // namespace omniframe

