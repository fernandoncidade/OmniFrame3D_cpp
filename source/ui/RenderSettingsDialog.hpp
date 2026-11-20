#pragma once

#include "render/RenderSettings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

namespace omniframe {

class RenderSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit RenderSettingsDialog(const RenderQualitySettings& current,
                                  const GraphicsCapabilitiesInfo& hwInfo,
                                  QWidget* parent = nullptr);

    RenderQualitySettings selectedSettings() const { return m_current; }
    void syncSettings(const RenderQualitySettings& settings, const GraphicsCapabilitiesInfo& hwInfo);
    void updateHardwareInfo(const GraphicsCapabilitiesInfo& hwInfo);

signals:
    void settingsPreviewChanged(const RenderQualitySettings& previewSettings);
    void settingsApplied(const RenderQualitySettings& appliedSettings);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onPresetSelected(int index);
    void onControlsEdited();
    void onRestoreDefaults();

private:
    void setupUi();
    void syncControlsFromSettings();
    void syncLabelsFromSettings();
    void retranslateUi();

    RenderQualitySettings m_current;
    GraphicsCapabilitiesInfo m_hwInfo;
    bool m_updatingControls = false;

    // Grupos e Labels
    QGroupBox* m_groupResolution = nullptr;
    QGroupBox* m_groupLines = nullptr;
    QGroupBox* m_groupPlane = nullptr;
    QGroupBox* m_groupTelemetry = nullptr;

    QLabel* m_lblPreset = nullptr;
    QLabel* m_lblResScale = nullptr;
    QLabel* m_lblMsaa = nullptr;
    QLabel* m_lblFilter = nullptr;

    QComboBox* m_presetCombo = nullptr;
    QComboBox* m_resScaleCombo = nullptr;
    QComboBox* m_msaaCombo = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QCheckBox* m_vsyncCheck = nullptr;

    QCheckBox* m_shaderLineAaCheck = nullptr;
    QLabel* m_lblLineWidthTitle = nullptr;
    QSlider* m_lineWidthSlider = nullptr;
    QLabel* m_lineWidthValue = nullptr;

    QLabel* m_lblSoftnessTitle = nullptr;
    QSlider* m_lineSoftnessSlider = nullptr;
    QLabel* m_lineSoftnessValue = nullptr;

    QLabel* m_lblSharpenTitle = nullptr;
    QSlider* m_sharpenSlider = nullptr;
    QLabel* m_sharpenValue = nullptr;

    QLabel* m_lblPlaneTheme = nullptr;
    QComboBox* m_planeThemeCombo = nullptr;
    QLabel* m_lblPlaneContrastTitle = nullptr;
    QSlider* m_planeContrastSlider = nullptr;
    QLabel* m_planeContrastValue = nullptr;
    QCheckBox* m_planeSurfaceCheck = nullptr;
    QCheckBox* m_resHudCheck = nullptr;

    QLabel* m_telemetryGpuValue = nullptr;
    QLabel* m_telemetryNativeValue = nullptr;
    QLabel* m_telemetryEffectiveValue = nullptr;
    QLabel* m_lblTelemetryGpuTitle = nullptr;
    QLabel* m_lblTelemetryNativeTitle = nullptr;
    QLabel* m_lblTelemetryEffectiveTitle = nullptr;

    QPushButton* m_btnRestoreDefaults = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

} // namespace omniframe

