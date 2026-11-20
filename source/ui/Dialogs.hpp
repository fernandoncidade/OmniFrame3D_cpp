#pragma once

#include "core/StructuralModel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <array>
#include <functional>

class QDialogButtonBox;
class QEvent;
class QGroupBox;

namespace omniframe {

class GuidedSessionDialog;
class WindowZOrderManager;

// Diálogo de Material
class MaterialDialog : public QDialog {
    Q_OBJECT
public:
    explicit MaterialDialog(QWidget* parent = nullptr, const Material* mat = nullptr);
    Material getMaterial(MaterialId id = INVALID_ID) const;

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void applyPreset(int index);

private:
    void retranslateUi();

    bool m_isEditing = false;
    QLabel* m_presetLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_eLabel = nullptr;
    QLabel* m_nuLabel = nullptr;
    QLabel* m_alphaLabel = nullptr;
    QLabel* m_gammaLabel = nullptr;
    QLabel* m_rhoLabel = nullptr;
    QGroupBox* m_plasticGroup = nullptr;
    QCheckBox* m_isPlasticCheck = nullptr;
    QLabel* m_sigmaYLabel = nullptr;
    QLabel* m_hIsoLabel = nullptr;
    QLabel* m_hKinLabel = nullptr;
    QDialogButtonBox* m_btnBox = nullptr;

    QLineEdit* m_nameEdit = nullptr;
    QDoubleSpinBox* m_eSpin = nullptr;      // GPa
    QDoubleSpinBox* m_nuSpin = nullptr;
    QDoubleSpinBox* m_alphaSpin = nullptr;  // 1e-5 / °C
    QDoubleSpinBox* m_gammaSpin = nullptr;  // kN/m³
    QDoubleSpinBox* m_rhoSpin = nullptr;    // kg/m³
    QDoubleSpinBox* m_sigmaYSpin = nullptr; // MPa
    QDoubleSpinBox* m_hIsoSpin = nullptr;   // MPa
    QDoubleSpinBox* m_hKinSpin = nullptr;   // MPa
    QComboBox* m_presetCombo = nullptr;
};

// Diálogo de Seção Transversal
class SectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit SectionDialog(QWidget* parent = nullptr, const CrossSection* sec = nullptr);
    CrossSection getSection(SectionId id = INVALID_ID) const;
    bool validateDimensions(QString* outError = nullptr) const;

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void applyPreset(int index);
    void onShapeChanged(int index);
    void updateCalculatedProps();

private:
    void retranslateUi();
    void updateShapeDimensionUi(SectionShape shape);

    bool m_isEditing = false;
    bool m_hasOriginalSection = false;
    CrossSection m_originalSection;
    QLabel* m_presetLabel = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_shapeLabel = nullptr;
    QGroupBox* m_dimGroup = nullptr;
    QGroupBox* m_propGroup = nullptr;
    QLabel* m_aLabel = nullptr;
    QLabel* m_iyLabel = nullptr;
    QLabel* m_izLabel = nullptr;
    QLabel* m_jLabel = nullptr;
    QDialogButtonBox* m_btnBox = nullptr;

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_shapeCombo = nullptr;

    QLabel* m_dim1Label = nullptr;
    QLabel* m_dim2Label = nullptr;
    QLabel* m_dim3Label = nullptr;
    QLabel* m_dim4Label = nullptr;
    QLabel* m_dim5Label = nullptr;
    QLabel* m_dim6Label = nullptr;

    QDoubleSpinBox* m_dim1Spin = nullptr; // b ou D ou h (m)
    QDoubleSpinBox* m_dim2Spin = nullptr; // h ou t ou bf (m)
    QDoubleSpinBox* m_dim3Spin = nullptr; // tf (m)
    QDoubleSpinBox* m_dim4Spin = nullptr; // tw (m)
    QDoubleSpinBox* m_dim5Spin = nullptr; // bf_inf (m)
    QDoubleSpinBox* m_dim6Spin = nullptr; // tf_inf (m)

    QDoubleSpinBox* m_aSpin = nullptr;
    QDoubleSpinBox* m_iySpin = nullptr;
    QDoubleSpinBox* m_izSpin = nullptr;
    QDoubleSpinBox* m_jSpin = nullptr;
};

// Diálogo de Condições de Apoio
class SupportDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    explicit SupportDialog(QWidget* parent = nullptr, const SupportRestraint* initial = nullptr);
    void setSupport(const SupportRestraint& s);
    SupportRestraint getSupport() const;

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void setPresetFixed();
    void setPresetPinned();
    void setPresetRollerZ();
    void setPresetFree();

private:
    void retranslateUi();

    QPushButton* m_btnFixed = nullptr;
    QPushButton* m_btnPinned = nullptr;
    QPushButton* m_btnRoller = nullptr;
    QPushButton* m_btnFree = nullptr;
    QLabel* m_hdrDof = nullptr;
    QLabel* m_hdrFixed = nullptr;
    QLabel* m_hdrSpring = nullptr;
    QLabel* m_hdrPrescribed = nullptr;
    QLabel* m_hdrValue = nullptr;
    std::array<QLabel*, 6> m_dofLabels{};
    QDialogButtonBox* m_btnBox = nullptr;

    std::array<QCheckBox*, 6> m_fixedChecks{};
    std::array<QDoubleSpinBox*, 6> m_springSpins{};
    std::array<QDoubleSpinBox*, 6> m_prescribedSpins{};
    std::array<QCheckBox*, 6> m_prescribedChecks{};
};

// Diálogo de Rótulas / Liberações de Extremidade
class ReleasesDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    explicit ReleasesDialog(QWidget* parent = nullptr, const ElementReleases* initial = nullptr);
    void setReleases(const ElementReleases& r);
    ElementReleases getReleases() const;

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void setPresetNone();
    void setPresetStartHinge();
    void setPresetEndHinge();
    void setPresetTruss();

private:
    void retranslateUi();

    QPushButton* m_btnNone = nullptr;
    QPushButton* m_btnStart = nullptr;
    QPushButton* m_btnEnd = nullptr;
    QPushButton* m_btnTruss = nullptr;
    QLabel* m_hdrDof = nullptr;
    QLabel* m_hdrStart = nullptr;
    QLabel* m_hdrEnd = nullptr;
    std::array<QLabel*, 6> m_dofLabels{};
    QDialogButtonBox* m_btnBox = nullptr;

    std::array<QCheckBox*, 6> m_startChecks{};
    std::array<QCheckBox*, 6> m_endChecks{};
};

// Diálogo de Carga Nodal Concentrada
class NodalLoadDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    explicit NodalLoadDialog(QWidget* parent = nullptr, const NodalLoad* initial = nullptr);
    void setLoad(const NodalLoad& load);
    NodalLoad getLoad() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();

    QLabel* m_fxLabel = nullptr;
    QLabel* m_fyLabel = nullptr;
    QLabel* m_fzLabel = nullptr;
    QLabel* m_mxLabel = nullptr;
    QLabel* m_myLabel = nullptr;
    QLabel* m_mzLabel = nullptr;
    QDialogButtonBox* m_btnBox = nullptr;

    QDoubleSpinBox* m_fxSpin = nullptr;
    QDoubleSpinBox* m_fySpin = nullptr;
    QDoubleSpinBox* m_fzSpin = nullptr;
    QDoubleSpinBox* m_mxSpin = nullptr;
    QDoubleSpinBox* m_mySpin = nullptr;
    QDoubleSpinBox* m_mzSpin = nullptr;
};

// Diálogo de Carga Distribuída em Barra
class DistributedLoadDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    explicit DistributedLoadDialog(QWidget* parent = nullptr, const DistributedLoad* initial = nullptr);
    void setLoad(const DistributedLoad& load);
    DistributedLoad getLoad() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();

    QLabel* m_coordLabel = nullptr;
    QLabel* m_qxLabel = nullptr;
    QLabel* m_qyLabel = nullptr;
    QLabel* m_qzLabel = nullptr;
    QDialogButtonBox* m_btnBox = nullptr;

    QCheckBox* m_activeCheck = nullptr;
    QComboBox* m_coordCombo = nullptr;
    QDoubleSpinBox* m_qxSpin = nullptr;
    QDoubleSpinBox* m_qySpin = nullptr;
    QDoubleSpinBox* m_qzSpin = nullptr;
};

// Diálogo de Ângulo de Roll da Seção
class RollAngleDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    explicit RollAngleDialog(QWidget* parent = nullptr, double currentAngleDeg = 0.0);
    void setAngleDegrees(double deg);
    double getAngleRadians() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();

    QLabel* m_angleLabel = nullptr;
    QDialogButtonBox* m_btnBox = nullptr;
    QDoubleSpinBox* m_angleSpin = nullptr; // Graus
};

// Diálogo de Variação Térmica (Carga Térmica Uniforme ΔT)
class ThermalLoadDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    explicit ThermalLoadDialog(QWidget* parent = nullptr, const ThermalLoad* initial = nullptr);
    void setThermalLoad(const ThermalLoad& th);
    ThermalLoad getThermalLoad() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();

    QCheckBox* m_activeCheck = nullptr;
    QLabel* m_deltaTLabel = nullptr;
    QDoubleSpinBox* m_deltaTSpin = nullptr;
    QDialogButtonBox* m_btnBox = nullptr;
};

// Diálogo Gerenciador de Catálogo de Materiais (Criar, Editar em Loco, Excluir e Atribuir)
class MaterialCatalogDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    MaterialCatalogDialog(QWidget* parent,
                          StructuralModel* model,
                          WindowZOrderManager* windowManager,
                          std::function<void()> onModelEdited);
    void refreshList();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onNewMaterial();
    void onEditMaterial();
    void onDeleteMaterial();
    void onAssignToSelected();

private:
    void retranslateUi();
    MaterialId selectedMaterialId() const;

    StructuralModel* m_model = nullptr;
    WindowZOrderManager* m_windowManager = nullptr;
    std::function<void()> m_onModelEdited;

    QLabel* m_selectionInfoLabel = nullptr;
    QListWidget* m_listWidget = nullptr;
    QPushButton* m_btnNew = nullptr;
    QPushButton* m_btnEdit = nullptr;
    QPushButton* m_btnDelete = nullptr;
    QPushButton* m_btnAssign = nullptr;
    QPushButton* m_btnClose = nullptr;
    QPointer<MaterialDialog> m_editorDialog;
};

// Diálogo Gerenciador de Catálogo de Seções Transversais (Criar, Editar em Loco, Excluir e Atribuir)
class SectionCatalogDialog : public QDialog {
    Q_OBJECT
    friend class GuidedSessionDialog;
public:
    SectionCatalogDialog(QWidget* parent,
                         StructuralModel* model,
                         WindowZOrderManager* windowManager,
                         std::function<void()> onModelEdited);
    void refreshList();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onNewSection();
    void onEditSection();
    void onDeleteSection();
    void onAssignToSelected();

private:
    void retranslateUi();
    SectionId selectedSectionId() const;

    StructuralModel* m_model = nullptr;
    WindowZOrderManager* m_windowManager = nullptr;
    std::function<void()> m_onModelEdited;

    QLabel* m_selectionInfoLabel = nullptr;
    QListWidget* m_listWidget = nullptr;
    QPushButton* m_btnNew = nullptr;
    QPushButton* m_btnEdit = nullptr;
    QPushButton* m_btnDelete = nullptr;
    QPushButton* m_btnAssign = nullptr;
    QPushButton* m_btnClose = nullptr;
    QPointer<SectionDialog> m_editorDialog;
};

} // namespace omniframe
