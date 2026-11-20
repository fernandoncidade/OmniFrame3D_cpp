#include "Dialogs.hpp"
#include "WindowZOrderManager.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace omniframe {

namespace {

using locale::GerenciadorTraducao;

void configureModelessDialogWindow(QDialog* dlg, int minWidth) {
    dlg->setModal(false);
    dlg->setWindowModality(Qt::NonModal);
    dlg->setMinimumWidth(minWidth);
}

QDialogButtonBox* createOkCancelBox(QDialog* parent) {
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, parent);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, parent, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, parent, &QDialog::reject);
    return btnBox;
}

void retranslateOkCancelBox(QDialogButtonBox* btnBox) {
    if (!btnBox) return;
    if (auto* okBtn = btnBox->button(QDialogButtonBox::Ok)) {
        okBtn->setText(GerenciadorTraducao::trText("OK"));
    }
    if (auto* cancelBtn = btnBox->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setText(GerenciadorTraducao::trText("Cancelar"));
    }
}

void setBoldText(QLabel* label, const char* sourceText) {
    if (label) {
        label->setText(QStringLiteral("<b>%1</b>").arg(GerenciadorTraducao::trText(sourceText).toHtmlEscaped()));
    }
}

} // namespace

// ==========================================
// MaterialDialog
// ==========================================
MaterialDialog::MaterialDialog(QWidget* parent, const Material* mat)
    : QDialog(parent),
      m_isEditing(mat != nullptr) {
    configureModelessDialogWindow(this, 360);

    auto* mainLayout = new QVBoxLayout(this);

    auto* presetLayout = new QHBoxLayout();
    m_presetLabel = new QLabel(this);
    presetLayout->addWidget(m_presetLabel);

    m_presetCombo = new QComboBox(this);
    for (int i = 0; i < 5; ++i) {
        m_presetCombo->addItem(QString());
    }
    presetLayout->addWidget(m_presetCombo);
    mainLayout->addLayout(presetLayout);

    auto* formLayout = new QFormLayout();
    m_nameLabel = new QLabel(this);
    m_nameEdit = new QLineEdit(mat ? QString::fromStdString(mat->name) : GerenciadorTraducao::trText("Novo Material"), this);
    formLayout->addRow(m_nameLabel, m_nameEdit);

    m_eLabel = new QLabel(this);
    m_eSpin = new QDoubleSpinBox(this);
    m_eSpin->setRange(0.01, 2000.0);
    m_eSpin->setDecimals(2);
    m_eSpin->setSuffix(QStringLiteral(" GPa"));
    m_eSpin->setValue(mat ? (mat->E / 1.0e6) : 200.0);
    formLayout->addRow(m_eLabel, m_eSpin);

    m_nuLabel = new QLabel(this);
    m_nuSpin = new QDoubleSpinBox(this);
    m_nuSpin->setRange(0.0, 0.49);
    m_nuSpin->setDecimals(2);
    m_nuSpin->setSingleStep(0.05);
    m_nuSpin->setValue(mat ? mat->nu : 0.30);
    formLayout->addRow(m_nuLabel, m_nuSpin);

    m_alphaLabel = new QLabel(this);
    m_alphaSpin = new QDoubleSpinBox(this);
    m_alphaSpin->setRange(0.0, 100.0);
    m_alphaSpin->setDecimals(2);
    m_alphaSpin->setSuffix(QStringLiteral(" ×10⁻⁵ / °C"));
    m_alphaSpin->setValue(mat ? (mat->alpha * 1.0e5) : 1.2);
    formLayout->addRow(m_alphaLabel, m_alphaSpin);

    m_gammaLabel = new QLabel(this);
    m_gammaSpin = new QDoubleSpinBox(this);
    m_gammaSpin->setRange(0.0, 500.0);
    m_gammaSpin->setDecimals(1);
    m_gammaSpin->setSuffix(QStringLiteral(" kN/m³"));
    m_gammaSpin->setValue(mat ? mat->gamma : 78.5);
    formLayout->addRow(m_gammaLabel, m_gammaSpin);

    mainLayout->addLayout(formLayout);

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MaterialDialog::applyPreset);

    m_btnBox = createOkCancelBox(this);
    mainLayout->addWidget(m_btnBox);

    retranslateUi();
}

void MaterialDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void MaterialDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText(m_isEditing ? "Editar Material" : "Novo Material"));
    m_presetLabel->setText(GerenciadorTraducao::trText("Predefinidos:"));

    m_presetCombo->blockSignals(true);
    m_presetCombo->setItemText(0, GerenciadorTraducao::trText("Personalizado"));
    m_presetCombo->setItemText(1, GerenciadorTraducao::trText("Aço Estrutural (ASTM A36)"));
    m_presetCombo->setItemText(2, GerenciadorTraducao::trText("Concreto C30"));
    m_presetCombo->setItemText(3, GerenciadorTraducao::trText("Alumínio 6061-T6"));
    m_presetCombo->setItemText(4, GerenciadorTraducao::trText("Madeira Estrutural"));
    m_presetCombo->blockSignals(false);

    m_nameLabel->setText(GerenciadorTraducao::trText("Nome:"));
    m_eLabel->setText(GerenciadorTraducao::trText("Módulo de Elasticidade (E):"));
    m_nuLabel->setText(GerenciadorTraducao::trText("Coeficiente de Poisson (ν):"));
    m_alphaLabel->setText(GerenciadorTraducao::trText("Dilatação Térmica (α):"));
    m_gammaLabel->setText(GerenciadorTraducao::trText("Peso Específico (γ):"));
    retranslateOkCancelBox(m_btnBox);
}

void MaterialDialog::applyPreset(int index) {
    Material m;
    switch (index) {
        case 1: m = Material::makeStructuralSteel(); break;
        case 2: m = Material::makeConcreteC30(); break;
        case 3: m = Material::makeAluminum6061(); break;
        case 4: m = Material::makeTimber(); break;
        default: return;
    }

    m_nameEdit->setText(GerenciadorTraducao::trText(QString::fromStdString(m.name)));
    m_eSpin->setValue(m.E / 1.0e6);
    m_nuSpin->setValue(m.nu);
    m_alphaSpin->setValue(m.alpha * 1.0e5);
    m_gammaSpin->setValue(m.gamma);
}

Material MaterialDialog::getMaterial(MaterialId id) const {
    Material m;
    m.id = id;
    m.name = m_nameEdit->text().toStdString();
    m.E = m_eSpin->value() * 1.0e6; // Converte GPa para kN/m² (kPa)
    m.nu = m_nuSpin->value();
    m.alpha = m_alphaSpin->value() * 1.0e-5;
    m.gamma = m_gammaSpin->value();
    return m;
}

// ==========================================
// SectionDialog
// ==========================================
SectionDialog::SectionDialog(QWidget* parent, const CrossSection* sec)
    : QDialog(parent),
      m_isEditing(sec != nullptr) {
    configureModelessDialogWindow(this, 400);

    auto* mainLayout = new QVBoxLayout(this);

    auto* formLayout = new QFormLayout();
    m_nameLabel = new QLabel(this);
    m_nameEdit = new QLineEdit(sec ? QString::fromStdString(sec->name) : GerenciadorTraducao::trText("Nova Seção"), this);
    formLayout->addRow(m_nameLabel, m_nameEdit);

    m_shapeLabel = new QLabel(this);
    m_shapeCombo = new QComboBox(this);
    m_shapeCombo->addItem(QString(), static_cast<int>(SectionShape::Rectangular));
    m_shapeCombo->addItem(QString(), static_cast<int>(SectionShape::CircularSolid));
    m_shapeCombo->addItem(QString(), static_cast<int>(SectionShape::CircularTube));
    m_shapeCombo->addItem(QString(), static_cast<int>(SectionShape::IShape));
    m_shapeCombo->addItem(QString(), static_cast<int>(SectionShape::Generic));
    if (sec) {
        const int idx = m_shapeCombo->findData(static_cast<int>(sec->shape));
        if (idx >= 0) m_shapeCombo->setCurrentIndex(idx);
    }
    formLayout->addRow(m_shapeLabel, m_shapeCombo);
    mainLayout->addLayout(formLayout);

    m_dimGroup = new QGroupBox(this);
    auto* dimLayout = new QFormLayout(m_dimGroup);

    m_dim1Label = new QLabel(m_dimGroup);
    m_dim1Spin = new QDoubleSpinBox(m_dimGroup);
    m_dim1Spin->setRange(0.001, 20.0);
    m_dim1Spin->setDecimals(4);
    m_dim1Spin->setSingleStep(0.05);
    m_dim1Spin->setValue(sec ? sec->dim1 : 0.20);
    dimLayout->addRow(m_dim1Label, m_dim1Spin);

    m_dim2Label = new QLabel(m_dimGroup);
    m_dim2Spin = new QDoubleSpinBox(m_dimGroup);
    m_dim2Spin->setRange(0.001, 20.0);
    m_dim2Spin->setDecimals(4);
    m_dim2Spin->setSingleStep(0.05);
    m_dim2Spin->setValue(sec ? sec->dim2 : 0.40);
    dimLayout->addRow(m_dim2Label, m_dim2Spin);

    m_dim3Label = new QLabel(m_dimGroup);
    m_dim3Spin = new QDoubleSpinBox(m_dimGroup);
    m_dim3Spin->setRange(0.0001, 5.0);
    m_dim3Spin->setDecimals(4);
    m_dim3Spin->setSingleStep(0.005);
    m_dim3Spin->setValue(sec ? sec->dim3 : 0.01);
    dimLayout->addRow(m_dim3Label, m_dim3Spin);

    m_dim4Label = new QLabel(m_dimGroup);
    m_dim4Spin = new QDoubleSpinBox(m_dimGroup);
    m_dim4Spin->setRange(0.0001, 5.0);
    m_dim4Spin->setDecimals(4);
    m_dim4Spin->setSingleStep(0.005);
    m_dim4Spin->setValue(sec ? sec->dim4 : 0.01);
    dimLayout->addRow(m_dim4Label, m_dim4Spin);

    mainLayout->addWidget(m_dimGroup);

    m_propGroup = new QGroupBox(this);
    auto* propLayout = new QFormLayout(m_propGroup);

    m_aLabel = new QLabel(m_propGroup);
    m_aSpin = new QDoubleSpinBox(m_propGroup);
    m_aSpin->setRange(1e-6, 100.0);
    m_aSpin->setDecimals(6);
    m_aSpin->setSuffix(QStringLiteral(" m²"));

    m_iyLabel = new QLabel(m_propGroup);
    m_iySpin = new QDoubleSpinBox(m_propGroup);
    m_iySpin->setRange(1e-10, 100.0);
    m_iySpin->setDecimals(8);
    m_iySpin->setSuffix(QStringLiteral(" m⁴"));

    m_izLabel = new QLabel(m_propGroup);
    m_izSpin = new QDoubleSpinBox(m_propGroup);
    m_izSpin->setRange(1e-10, 100.0);
    m_izSpin->setDecimals(8);
    m_izSpin->setSuffix(QStringLiteral(" m⁴"));

    m_jLabel = new QLabel(m_propGroup);
    m_jSpin = new QDoubleSpinBox(m_propGroup);
    m_jSpin->setRange(1e-10, 100.0);
    m_jSpin->setDecimals(8);
    m_jSpin->setSuffix(QStringLiteral(" m⁴"));

    if (sec) {
        m_aSpin->setValue(sec->A);
        m_iySpin->setValue(sec->Iy);
        m_izSpin->setValue(sec->Iz);
        m_jSpin->setValue(sec->J);
    }

    propLayout->addRow(m_aLabel, m_aSpin);
    propLayout->addRow(m_iyLabel, m_iySpin);
    propLayout->addRow(m_izLabel, m_izSpin);
    propLayout->addRow(m_jLabel, m_jSpin);

    mainLayout->addWidget(m_propGroup);

    connect(m_shapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SectionDialog::onShapeChanged);
    connect(m_dim1Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SectionDialog::updateCalculatedProps);
    connect(m_dim2Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SectionDialog::updateCalculatedProps);
    connect(m_dim3Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SectionDialog::updateCalculatedProps);
    connect(m_dim4Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SectionDialog::updateCalculatedProps);

    m_btnBox = createOkCancelBox(this);
    mainLayout->addWidget(m_btnBox);

    retranslateUi();
    onShapeChanged(m_shapeCombo->currentIndex());
}

void SectionDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void SectionDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText(m_isEditing ? "Editar Seção Transversal" : "Nova Seção Transversal"));
    m_nameLabel->setText(GerenciadorTraducao::trText("Nome:"));
    m_shapeLabel->setText(GerenciadorTraducao::trText("Geometria:"));

    m_shapeCombo->blockSignals(true);
    m_shapeCombo->setItemText(0, GerenciadorTraducao::trText("Retangular Sólida"));
    m_shapeCombo->setItemText(1, GerenciadorTraducao::trText("Circular Sólida"));
    m_shapeCombo->setItemText(2, GerenciadorTraducao::trText("Tubular Circular"));
    m_shapeCombo->setItemText(3, GerenciadorTraducao::trText("Perfil I Simétrico"));
    m_shapeCombo->setItemText(4, GerenciadorTraducao::trText("Genérica (Manual)"));
    m_shapeCombo->blockSignals(false);

    m_dimGroup->setTitle(GerenciadorTraducao::trText("Dimensões Geométricas"));
    m_propGroup->setTitle(GerenciadorTraducao::trText("Propriedades da Seção"));
    m_aLabel->setText(GerenciadorTraducao::trText("Área (A):"));
    m_iyLabel->setText(GerenciadorTraducao::trText("Inércia Iy (eixo y):"));
    m_izLabel->setText(GerenciadorTraducao::trText("Inércia Iz (eixo z):"));
    m_jLabel->setText(GerenciadorTraducao::trText("Torção de Saint-Venant (J):"));

    updateShapeDimensionUi(static_cast<SectionShape>(m_shapeCombo->currentData().toInt()));
    retranslateOkCancelBox(m_btnBox);
}

void SectionDialog::updateShapeDimensionUi(SectionShape shape) {
    const bool isGeneric = (shape == SectionShape::Generic);

    m_aSpin->setReadOnly(!isGeneric);
    m_iySpin->setReadOnly(!isGeneric);
    m_izSpin->setReadOnly(!isGeneric);
    m_jSpin->setReadOnly(!isGeneric);

    auto setVis = [](QLabel* lbl, QDoubleSpinBox* sp, bool visible, const char* text = nullptr) {
        lbl->setVisible(visible);
        sp->setVisible(visible);
        if (visible && text) {
            lbl->setText(GerenciadorTraducao::trText(text));
        }
    };

    switch (shape) {
        case SectionShape::Rectangular:
            setVis(m_dim1Label, m_dim1Spin, true, "Largura b (m):");
            setVis(m_dim2Label, m_dim2Spin, true, "Altura h (m):");
            setVis(m_dim3Label, m_dim3Spin, false);
            setVis(m_dim4Label, m_dim4Spin, false);
            break;
        case SectionShape::CircularSolid:
            setVis(m_dim1Label, m_dim1Spin, true, "Diâmetro D (m):");
            setVis(m_dim2Label, m_dim2Spin, false);
            setVis(m_dim3Label, m_dim3Spin, false);
            setVis(m_dim4Label, m_dim4Spin, false);
            break;
        case SectionShape::CircularTube:
            setVis(m_dim1Label, m_dim1Spin, true, "Diâmetro Externo D (m):");
            setVis(m_dim2Label, m_dim2Spin, true, "Espessura t (m):");
            setVis(m_dim3Label, m_dim3Spin, false);
            setVis(m_dim4Label, m_dim4Spin, false);
            break;
        case SectionShape::IShape:
            setVis(m_dim1Label, m_dim1Spin, true, "Altura Total h (m):");
            setVis(m_dim2Label, m_dim2Spin, true, "Largura Mesa bf (m):");
            setVis(m_dim3Label, m_dim3Spin, true, "Espessura Mesa tf (m):");
            setVis(m_dim4Label, m_dim4Spin, true, "Espessura Alma tw (m):");
            break;
        case SectionShape::Generic:
            setVis(m_dim1Label, m_dim1Spin, false);
            setVis(m_dim2Label, m_dim2Spin, false);
            setVis(m_dim3Label, m_dim3Spin, false);
            setVis(m_dim4Label, m_dim4Spin, false);
            break;
    }
}

void SectionDialog::onShapeChanged(int index) {
    const auto shape = static_cast<SectionShape>(m_shapeCombo->itemData(index).toInt());
    updateShapeDimensionUi(shape);
    updateCalculatedProps();
}

void SectionDialog::updateCalculatedProps() {
    const auto shape = static_cast<SectionShape>(m_shapeCombo->currentData().toInt());
    if (shape == SectionShape::Generic) return;

    CrossSection temp(1, "temp", shape,
                      m_dim1Spin->value(), m_dim2Spin->value(),
                      m_dim3Spin->value(), m_dim4Spin->value());
    temp.computeProperties();

    m_aSpin->setValue(temp.A);
    m_iySpin->setValue(temp.Iy);
    m_izSpin->setValue(temp.Iz);
    m_jSpin->setValue(temp.J);
}

CrossSection SectionDialog::getSection(SectionId id) const {
    CrossSection sec;
    sec.id = id;
    sec.name = m_nameEdit->text().toStdString();
    sec.shape = static_cast<SectionShape>(m_shapeCombo->currentData().toInt());
    sec.dim1 = m_dim1Spin->value();
    sec.dim2 = m_dim2Spin->value();
    sec.dim3 = m_dim3Spin->value();
    sec.dim4 = m_dim4Spin->value();

    if (sec.shape == SectionShape::Generic) {
        sec.A = m_aSpin->value();
        sec.Iy = m_iySpin->value();
        sec.Iz = m_izSpin->value();
        sec.J = m_jSpin->value();
    } else {
        sec.computeProperties();
    }
    return sec;
}

// ==========================================
// SupportDialog
// ==========================================
SupportDialog::SupportDialog(QWidget* parent, const SupportRestraint* initial)
    : QDialog(parent) {
    configureModelessDialogWindow(this, 480);

    auto* mainLayout = new QVBoxLayout(this);

    auto* presetLayout = new QHBoxLayout();
    m_btnFixed = new QPushButton(this);
    m_btnPinned = new QPushButton(this);
    m_btnRoller = new QPushButton(this);
    m_btnFree = new QPushButton(this);

    presetLayout->addWidget(m_btnFixed);
    presetLayout->addWidget(m_btnPinned);
    presetLayout->addWidget(m_btnRoller);
    presetLayout->addWidget(m_btnFree);
    mainLayout->addLayout(presetLayout);

    connect(m_btnFixed, &QPushButton::clicked, this, &SupportDialog::setPresetFixed);
    connect(m_btnPinned, &QPushButton::clicked, this, &SupportDialog::setPresetPinned);
    connect(m_btnRoller, &QPushButton::clicked, this, &SupportDialog::setPresetRollerZ);
    connect(m_btnFree, &QPushButton::clicked, this, &SupportDialog::setPresetFree);

    auto* gridLayout = new QGridLayout();
    m_hdrDof = new QLabel(this);
    m_hdrFixed = new QLabel(this);
    m_hdrSpring = new QLabel(this);
    m_hdrPrescribed = new QLabel(this);
    m_hdrValue = new QLabel(this);

    gridLayout->addWidget(m_hdrDof, 0, 0);
    gridLayout->addWidget(m_hdrFixed, 0, 1);
    gridLayout->addWidget(m_hdrSpring, 0, 2);
    gridLayout->addWidget(m_hdrPrescribed, 0, 3);
    gridLayout->addWidget(m_hdrValue, 0, 4);

    for (int i = 0; i < 6; ++i) {
        m_dofLabels[i] = new QLabel(this);
        gridLayout->addWidget(m_dofLabels[i], i + 1, 0);

        m_fixedChecks[i] = new QCheckBox(this);
        gridLayout->addWidget(m_fixedChecks[i], i + 1, 1, Qt::AlignCenter);

        m_springSpins[i] = new QDoubleSpinBox(this);
        m_springSpins[i]->setRange(0.0, 1e9);
        gridLayout->addWidget(m_springSpins[i], i + 1, 2);

        m_prescribedChecks[i] = new QCheckBox(this);
        gridLayout->addWidget(m_prescribedChecks[i], i + 1, 3, Qt::AlignCenter);

        m_prescribedSpins[i] = new QDoubleSpinBox(this);
        m_prescribedSpins[i]->setRange(-100.0, 100.0);
        m_prescribedSpins[i]->setDecimals(5);
        gridLayout->addWidget(m_prescribedSpins[i], i + 1, 4);

        // Vínculo rígido e deslocamento prescrito são mutuamente exclusivos no mesmo DOF
        connect(m_fixedChecks[i], &QCheckBox::toggled, this, [this, i](bool checked) {
            if (checked) {
                QSignalBlocker blocker(m_prescribedChecks[i]);
                m_prescribedChecks[i]->setChecked(false);
            }
        });
        connect(m_prescribedChecks[i], &QCheckBox::toggled, this, [this, i](bool checked) {
            if (checked) {
                QSignalBlocker blocker(m_fixedChecks[i]);
                m_fixedChecks[i]->setChecked(false);
            }
        });
    }

    if (initial) {
        setSupport(*initial);
    }

    mainLayout->addLayout(gridLayout);

    m_btnBox = createOkCancelBox(this);
    mainLayout->addWidget(m_btnBox);

    retranslateUi();
}

void SupportDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void SupportDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Condições de Apoio (Vínculos)"));
    m_btnFixed->setText(GerenciadorTraducao::trText("Engastado"));
    m_btnPinned->setText(GerenciadorTraducao::trText("Articulado Fixo"));
    m_btnRoller->setText(GerenciadorTraducao::trText("Apoio Móvel (Z)"));
    m_btnFree->setText(GerenciadorTraducao::trText("Livre"));

    setBoldText(m_hdrDof, "Grau de Liberdade");
    setBoldText(m_hdrFixed, "Fixo");
    setBoldText(m_hdrSpring, "Mola (kN/m ou kNm/rad)");
    setBoldText(m_hdrPrescribed, "Prescrito?");
    setBoldText(m_hdrValue, "Valor Prescrito (m ou rad)");

    static constexpr const char* kDofKeys[6] = {
        "uX (Translação X)",
        "uY (Translação Y)",
        "uZ (Translação Z)",
        "θX (Rotação X)",
        "θY (Rotação Y)",
        "θZ (Rotação Z)"
    };
    for (int i = 0; i < 6; ++i) {
        m_dofLabels[i]->setText(GerenciadorTraducao::trText(kDofKeys[i]));
    }
    retranslateOkCancelBox(m_btnBox);
}

void SupportDialog::setPresetFixed() {
    for (int i = 0; i < 6; ++i) {
        m_fixedChecks[i]->setChecked(true);
        m_springSpins[i]->setValue(0.0);
        m_prescribedChecks[i]->setChecked(false);
    }
}

void SupportDialog::setPresetPinned() {
    for (int i = 0; i < 6; ++i) {
        m_fixedChecks[i]->setChecked(i < 3); // Apenas translações
        m_springSpins[i]->setValue(0.0);
        m_prescribedChecks[i]->setChecked(false);
    }
}

void SupportDialog::setPresetRollerZ() {
    for (int i = 0; i < 6; ++i) {
        m_fixedChecks[i]->setChecked(i == 2); // Apenas uZ
        m_springSpins[i]->setValue(0.0);
        m_prescribedChecks[i]->setChecked(false);
    }
}

void SupportDialog::setPresetFree() {
    for (int i = 0; i < 6; ++i) {
        m_fixedChecks[i]->setChecked(false);
        m_springSpins[i]->setValue(0.0);
        m_prescribedChecks[i]->setChecked(false);
    }
}

void SupportDialog::setSupport(const SupportRestraint& s) {
    for (int i = 0; i < 6; ++i) {
        QSignalBlocker bFix(m_fixedChecks[i]);
        QSignalBlocker bPre(m_prescribedChecks[i]);
        QSignalBlocker bSpr(m_springSpins[i]);
        QSignalBlocker bVal(m_prescribedSpins[i]);

        const bool prescribed = s.hasPrescribed[i];
        m_prescribedChecks[i]->setChecked(prescribed);
        m_fixedChecks[i]->setChecked(s.fixed[i] && !prescribed);
        m_springSpins[i]->setValue(s.springStiffness[i]);
        m_prescribedSpins[i]->setValue(s.prescribedValue[i]);
    }
}

SupportRestraint SupportDialog::getSupport() const {
    SupportRestraint s;
    for (int i = 0; i < 6; ++i) {
        s.fixed[i] = m_fixedChecks[i]->isChecked();
        s.springStiffness[i] = m_springSpins[i]->value();
        s.hasPrescribed[i] = m_prescribedChecks[i]->isChecked();
        s.prescribedValue[i] = m_prescribedSpins[i]->value();
    }
    return s;
}

// ==========================================
// ReleasesDialog
// ==========================================
ReleasesDialog::ReleasesDialog(QWidget* parent, const ElementReleases* initial)
    : QDialog(parent) {
    configureModelessDialogWindow(this, 380);

    auto* mainLayout = new QVBoxLayout(this);

    auto* presetLayout = new QHBoxLayout();
    m_btnNone = new QPushButton(this);
    m_btnStart = new QPushButton(this);
    m_btnEnd = new QPushButton(this);
    m_btnTruss = new QPushButton(this);

    presetLayout->addWidget(m_btnNone);
    presetLayout->addWidget(m_btnStart);
    presetLayout->addWidget(m_btnEnd);
    presetLayout->addWidget(m_btnTruss);
    mainLayout->addLayout(presetLayout);

    connect(m_btnNone, &QPushButton::clicked, this, &ReleasesDialog::setPresetNone);
    connect(m_btnStart, &QPushButton::clicked, this, &ReleasesDialog::setPresetStartHinge);
    connect(m_btnEnd, &QPushButton::clicked, this, &ReleasesDialog::setPresetEndHinge);
    connect(m_btnTruss, &QPushButton::clicked, this, &ReleasesDialog::setPresetTruss);

    auto* gridLayout = new QGridLayout();
    m_hdrDof = new QLabel(this);
    m_hdrStart = new QLabel(this);
    m_hdrEnd = new QLabel(this);

    gridLayout->addWidget(m_hdrDof, 0, 0);
    gridLayout->addWidget(m_hdrStart, 0, 1, Qt::AlignCenter);
    gridLayout->addWidget(m_hdrEnd, 0, 2, Qt::AlignCenter);

    for (int i = 0; i < 6; ++i) {
        m_dofLabels[i] = new QLabel(this);
        gridLayout->addWidget(m_dofLabels[i], i + 1, 0);

        m_startChecks[i] = new QCheckBox(this);
        if (initial) m_startChecks[i]->setChecked(initial->start[i]);
        gridLayout->addWidget(m_startChecks[i], i + 1, 1, Qt::AlignCenter);

        m_endChecks[i] = new QCheckBox(this);
        if (initial) m_endChecks[i]->setChecked(initial->end[i]);
        gridLayout->addWidget(m_endChecks[i], i + 1, 2, Qt::AlignCenter);
    }

    mainLayout->addLayout(gridLayout);

    m_btnBox = createOkCancelBox(this);
    mainLayout->addWidget(m_btnBox);

    retranslateUi();
}

void ReleasesDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void ReleasesDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Rótulas / Liberações de Extremidade"));
    m_btnNone->setText(GerenciadorTraducao::trText("Bi-Engastada"));
    m_btnStart->setText(GerenciadorTraducao::trText("Rótula Início"));
    m_btnEnd->setText(GerenciadorTraducao::trText("Rótula Fim"));
    m_btnTruss->setText(GerenciadorTraducao::trText("Barra Treliça"));

    setBoldText(m_hdrDof, "Grau de Liberdade");
    setBoldText(m_hdrStart, "Liberar Início (Nó 1)");
    setBoldText(m_hdrEnd, "Liberar Fim (Nó 2)");

    static constexpr const char* kDofKeys[6] = {
        "Axial (u)",
        "Cisalhamento (v)",
        "Cisalhamento (w)",
        "Torção (Tx)",
        "Fletor My",
        "Fletor Mz"
    };
    for (int i = 0; i < 6; ++i) {
        m_dofLabels[i]->setText(GerenciadorTraducao::trText(kDofKeys[i]));
    }
    retranslateOkCancelBox(m_btnBox);
}

void ReleasesDialog::setPresetNone() {
    for (int i = 0; i < 6; ++i) {
        m_startChecks[i]->setChecked(false);
        m_endChecks[i]->setChecked(false);
    }
}

void ReleasesDialog::setPresetStartHinge() {
    setPresetNone();
    m_startChecks[4]->setChecked(true); // My
    m_startChecks[5]->setChecked(true); // Mz
}

void ReleasesDialog::setPresetEndHinge() {
    setPresetNone();
    m_endChecks[4]->setChecked(true); // My
    m_endChecks[5]->setChecked(true); // Mz
}

void ReleasesDialog::setPresetTruss() {
    for (int i = 0; i < 6; ++i) {
        m_startChecks[i]->setChecked(i >= 3);
        m_endChecks[i]->setChecked(i >= 3);
    }
}

void ReleasesDialog::setReleases(const ElementReleases& r) {
    for (int i = 0; i < 6; ++i) {
        QSignalBlocker bS(m_startChecks[i]);
        QSignalBlocker bE(m_endChecks[i]);
        m_startChecks[i]->setChecked(r.start[i]);
        m_endChecks[i]->setChecked(r.end[i]);
    }
}

ElementReleases ReleasesDialog::getReleases() const {
    ElementReleases r;
    for (int i = 0; i < 6; ++i) {
        r.start[i] = m_startChecks[i]->isChecked();
        r.end[i] = m_endChecks[i]->isChecked();
    }
    return r;
}

// ==========================================
// NodalLoadDialog
// ==========================================
NodalLoadDialog::NodalLoadDialog(QWidget* parent, const NodalLoad* initial)
    : QDialog(parent) {
    configureModelessDialogWindow(this, 320);

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    auto makeSpin = [this](const char* suffix) {
        auto* sp = new QDoubleSpinBox(this);
        sp->setRange(-1e6, 1e6);
        sp->setDecimals(2);
        sp->setSuffix(QString::fromUtf8(suffix));
        return sp;
    };

    m_fxSpin = makeSpin(" kN");
    m_fySpin = makeSpin(" kN");
    m_fzSpin = makeSpin(" kN");
    m_mxSpin = makeSpin(" kNm");
    m_mySpin = makeSpin(" kNm");
    m_mzSpin = makeSpin(" kNm");

    if (initial) {
        m_fxSpin->setValue(initial->force.x);
        m_fySpin->setValue(initial->force.y);
        m_fzSpin->setValue(initial->force.z);
        m_mxSpin->setValue(initial->moment.x);
        m_mySpin->setValue(initial->moment.y);
        m_mzSpin->setValue(initial->moment.z);
    }

    m_fxLabel = new QLabel(this);
    m_fyLabel = new QLabel(this);
    m_fzLabel = new QLabel(this);
    m_mxLabel = new QLabel(this);
    m_myLabel = new QLabel(this);
    m_mzLabel = new QLabel(this);

    formLayout->addRow(m_fxLabel, m_fxSpin);
    formLayout->addRow(m_fyLabel, m_fySpin);
    formLayout->addRow(m_fzLabel, m_fzSpin);
    formLayout->addRow(m_mxLabel, m_mxSpin);
    formLayout->addRow(m_myLabel, m_mySpin);
    formLayout->addRow(m_mzLabel, m_mzSpin);

    mainLayout->addLayout(formLayout);

    m_btnBox = createOkCancelBox(this);
    mainLayout->addWidget(m_btnBox);

    retranslateUi();
}

void NodalLoadDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void NodalLoadDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Carga Nodal Concentrada"));
    m_fxLabel->setText(GerenciadorTraducao::trText("Força Fx:"));
    m_fyLabel->setText(GerenciadorTraducao::trText("Força Fy:"));
    m_fzLabel->setText(GerenciadorTraducao::trText("Força Fz:"));
    m_mxLabel->setText(GerenciadorTraducao::trText("Momento Mx:"));
    m_myLabel->setText(GerenciadorTraducao::trText("Momento My:"));
    m_mzLabel->setText(GerenciadorTraducao::trText("Momento Mz:"));
    retranslateOkCancelBox(m_btnBox);
}

void NodalLoadDialog::setLoad(const NodalLoad& ld) {
    QSignalBlocker bFx(m_fxSpin);
    QSignalBlocker bFy(m_fySpin);
    QSignalBlocker bFz(m_fzSpin);
    QSignalBlocker bMx(m_mxSpin);
    QSignalBlocker bMy(m_mySpin);
    QSignalBlocker bMz(m_mzSpin);
    m_fxSpin->setValue(ld.force.x);
    m_fySpin->setValue(ld.force.y);
    m_fzSpin->setValue(ld.force.z);
    m_mxSpin->setValue(ld.moment.x);
    m_mySpin->setValue(ld.moment.y);
    m_mzSpin->setValue(ld.moment.z);
}

NodalLoad NodalLoadDialog::getLoad() const {
    NodalLoad ld;
    ld.force = {m_fxSpin->value(), m_fySpin->value(), m_fzSpin->value()};
    ld.moment = {m_mxSpin->value(), m_mySpin->value(), m_mzSpin->value()};
    return ld;
}

// ==========================================
// DistributedLoadDialog
// ==========================================
DistributedLoadDialog::DistributedLoadDialog(QWidget* parent, const DistributedLoad* initial)
    : QDialog(parent) {
    configureModelessDialogWindow(this, 340);

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    m_activeCheck = new QCheckBox(this);
    m_activeCheck->setChecked(initial ? initial->active : true);
    formLayout->addRow(QString(), m_activeCheck);

    m_coordLabel = new QLabel(this);
    m_coordCombo = new QComboBox(this);
    m_coordCombo->addItem(QString(), static_cast<int>(CoordSystem::Global));
    m_coordCombo->addItem(QString(), static_cast<int>(CoordSystem::Local));
    if (initial && initial->coordSystem == CoordSystem::Local) {
        m_coordCombo->setCurrentIndex(1);
    }
    formLayout->addRow(m_coordLabel, m_coordCombo);

    auto makeDistSpin = [this]() {
        auto* sp = new QDoubleSpinBox(this);
        sp->setRange(-1e5, 1e5);
        sp->setDecimals(2);
        sp->setSuffix(QStringLiteral(" kN/m"));
        return sp;
    };

    m_qxSpin = makeDistSpin();
    m_qySpin = makeDistSpin();
    m_qzSpin = makeDistSpin();

    if (initial) {
        m_qxSpin->setValue(initial->w.x);
        m_qySpin->setValue(initial->w.y);
        m_qzSpin->setValue(initial->w.z);
    } else {
        m_qzSpin->setValue(-10.0); // Carga gravitacional padrão
    }

    m_qxLabel = new QLabel(this);
    m_qyLabel = new QLabel(this);
    m_qzLabel = new QLabel(this);

    formLayout->addRow(m_qxLabel, m_qxSpin);
    formLayout->addRow(m_qyLabel, m_qySpin);
    formLayout->addRow(m_qzLabel, m_qzSpin);

    mainLayout->addLayout(formLayout);

    m_btnBox = createOkCancelBox(this);
    mainLayout->addWidget(m_btnBox);

    retranslateUi();
}

void DistributedLoadDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void DistributedLoadDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Carga Uniformemente Distribuída"));
    m_activeCheck->setText(GerenciadorTraducao::trText("Carga Distribuída Ativa"));
    m_coordLabel->setText(GerenciadorTraducao::trText("Sistema de Coordenadas:"));

    m_coordCombo->blockSignals(true);
    m_coordCombo->setItemText(0, GerenciadorTraducao::trText("Sistema Global (X, Y, Z)"));
    m_coordCombo->setItemText(1, GerenciadorTraducao::trText("Sistema Local (x, y, z da barra)"));
    m_coordCombo->blockSignals(false);

    m_qxLabel->setText(GerenciadorTraducao::trText("Componente qX / qx:"));
    m_qyLabel->setText(GerenciadorTraducao::trText("Componente qY / qy:"));
    m_qzLabel->setText(GerenciadorTraducao::trText("Componente qZ / qz:"));
    retranslateOkCancelBox(m_btnBox);
}

void DistributedLoadDialog::setLoad(const DistributedLoad& dl) {
    QSignalBlocker bAct(m_activeCheck);
    QSignalBlocker bCoord(m_coordCombo);
    QSignalBlocker bQx(m_qxSpin);
    QSignalBlocker bQy(m_qySpin);
    QSignalBlocker bQz(m_qzSpin);
    m_activeCheck->setChecked(dl.active);
    m_coordCombo->setCurrentIndex(dl.coordSystem == CoordSystem::Local ? 1 : 0);
    m_qxSpin->setValue(dl.w.x);
    m_qySpin->setValue(dl.w.y);
    m_qzSpin->setValue(dl.w.z);
}

DistributedLoad DistributedLoadDialog::getLoad() const {
    DistributedLoad dl;
    dl.active = m_activeCheck->isChecked();
    dl.coordSystem = static_cast<CoordSystem>(m_coordCombo->currentData().toInt());
    dl.w = {m_qxSpin->value(), m_qySpin->value(), m_qzSpin->value()};
    return dl;
}

// ==========================================
// RollAngleDialog
// ==========================================
RollAngleDialog::RollAngleDialog(QWidget* parent, double currentAngleDeg)
    : QDialog(parent) {
    configureModelessDialogWindow(this, 300);

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    m_angleLabel = new QLabel(this);
    m_angleSpin = new QDoubleSpinBox(this);
    m_angleSpin->setRange(-360.0, 360.0);
    m_angleSpin->setDecimals(1);
    m_angleSpin->setSuffix(QStringLiteral(" °"));
    m_angleSpin->setValue(currentAngleDeg);
    formLayout->addRow(m_angleLabel, m_angleSpin);

    mainLayout->addLayout(formLayout);

    m_btnBox = createOkCancelBox(this);
    mainLayout->addWidget(m_btnBox);

    retranslateUi();
}

void RollAngleDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void RollAngleDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Ângulo de Giro da Seção (Roll γ)"));
    m_angleLabel->setText(GerenciadorTraducao::trText("Ângulo de Roll (γ):"));
    retranslateOkCancelBox(m_btnBox);
}

void RollAngleDialog::setAngleDegrees(double deg) {
    QSignalBlocker b(m_angleSpin);
    m_angleSpin->setValue(deg);
}

double RollAngleDialog::getAngleRadians() const {
    return m_angleSpin->value() * PI / 180.0;
}

// ==========================================
// ThermalLoadDialog
// ==========================================
ThermalLoadDialog::ThermalLoadDialog(QWidget* parent, const ThermalLoad* initial)
    : QDialog(parent) {
    configureModelessDialogWindow(this, 320);

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    m_activeCheck = new QCheckBox(this);
    m_activeCheck->setChecked(initial ? initial->active : true);
    formLayout->addRow(QString(), m_activeCheck);

    m_deltaTLabel = new QLabel(this);
    m_deltaTSpin = new QDoubleSpinBox(this);
    m_deltaTSpin->setRange(-500.0, 500.0);
    m_deltaTSpin->setDecimals(1);
    m_deltaTSpin->setSuffix(QStringLiteral(" °C"));
    m_deltaTSpin->setValue(initial ? initial->deltaT : 0.0);
    formLayout->addRow(m_deltaTLabel, m_deltaTSpin);

    mainLayout->addLayout(formLayout);
    m_btnBox = createOkCancelBox(this);
    mainLayout->addWidget(m_btnBox);
    retranslateUi();
}

void ThermalLoadDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void ThermalLoadDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Variação Térmica Uniforme (ΔT)"));
    m_activeCheck->setText(GerenciadorTraducao::trText("Carga Térmica Ativa"));
    m_deltaTLabel->setText(GerenciadorTraducao::trText("Variação de Temperatura (ΔT):"));
    retranslateOkCancelBox(m_btnBox);
}

void ThermalLoadDialog::setThermalLoad(const ThermalLoad& th) {
    QSignalBlocker bAct(m_activeCheck);
    QSignalBlocker bDt(m_deltaTSpin);
    m_activeCheck->setChecked(th.active);
    m_deltaTSpin->setValue(th.deltaT);
}

ThermalLoad ThermalLoadDialog::getThermalLoad() const {
    ThermalLoad th;
    th.active = m_activeCheck->isChecked();
    th.deltaT = m_deltaTSpin->value();
    return th;
}

namespace {

void buildCatalogUi(QDialog* dlg,
                    QLabel*& infoLabel,
                    QListWidget*& listWidget,
                    QPushButton*& btnNew,
                    QPushButton*& btnEdit,
                    QPushButton*& btnDelete,
                    QPushButton*& btnAssign,
                    QPushButton*& btnClose) {
    configureModelessDialogWindow(dlg, 520);
    dlg->resize(560, 400);

    auto* mainLayout = new QVBoxLayout(dlg);
    infoLabel = new QLabel(dlg);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    listWidget = new QListWidget(dlg);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(listWidget, 1);

    auto* btnRow = new QHBoxLayout();
    btnNew = new QPushButton(dlg);
    btnEdit = new QPushButton(dlg);
    btnDelete = new QPushButton(dlg);
    btnAssign = new QPushButton(dlg);
    btnRow->addWidget(btnNew);
    btnRow->addWidget(btnEdit);
    btnRow->addWidget(btnDelete);
    btnRow->addWidget(btnAssign);
    mainLayout->addLayout(btnRow);

    auto* closeRow = new QHBoxLayout();
    closeRow->addStretch();
    btnClose = new QPushButton(dlg);
    closeRow->addWidget(btnClose);
    mainLayout->addLayout(closeRow);

    QObject::connect(btnClose, &QPushButton::clicked, dlg, &QDialog::accept);
}

void showCatalogWarning(WindowZOrderManager* wm, const QString& title, const QString& msg) {
    if (wm) {
        wm->showModelessMessage(QMessageBox::Warning, title, msg);
    } else {
        QMessageBox::warning(nullptr, title, msg);
    }
}

} // namespace

// ==========================================
// MaterialCatalogDialog
// ==========================================
MaterialCatalogDialog::MaterialCatalogDialog(QWidget* parent,
                                             StructuralModel* model,
                                             WindowZOrderManager* windowManager,
                                             std::function<void()> onModelEdited)
    : QDialog(parent),
      m_model(model),
      m_windowManager(windowManager),
      m_onModelEdited(std::move(onModelEdited)) {
    buildCatalogUi(this, m_selectionInfoLabel, m_listWidget,
                   m_btnNew, m_btnEdit, m_btnDelete, m_btnAssign, m_btnClose);

    connect(m_btnNew, &QPushButton::clicked, this, &MaterialCatalogDialog::onNewMaterial);
    connect(m_btnEdit, &QPushButton::clicked, this, &MaterialCatalogDialog::onEditMaterial);
    connect(m_btnDelete, &QPushButton::clicked, this, &MaterialCatalogDialog::onDeleteMaterial);
    connect(m_btnAssign, &QPushButton::clicked, this, &MaterialCatalogDialog::onAssignToSelected);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { onEditMaterial(); });

    retranslateUi();
    refreshList();
}

void MaterialCatalogDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
        refreshList();
    }
}

void MaterialCatalogDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Catálogo de Materiais"));
    m_btnNew->setText(GerenciadorTraducao::trText("Novo..."));
    m_btnEdit->setText(GerenciadorTraducao::trText("Editar..."));
    m_btnDelete->setText(GerenciadorTraducao::trText("Excluir"));
    m_btnAssign->setText(GerenciadorTraducao::trText("Atribuir às Barras Selecionadas"));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
}

MaterialId MaterialCatalogDialog::selectedMaterialId() const {
    auto* item = m_listWidget->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : INVALID_ID;
}

void MaterialCatalogDialog::refreshList() {
    if (!m_model) return;
    const MaterialId prevId = selectedMaterialId();
    m_listWidget->clear();

    const size_t selCount = m_model->getSelectedElements().size();
    m_selectionInfoLabel->setText(
        selCount > 0
            ? GerenciadorTraducao::trText("Barras selecionadas: %1. Edite um material no catálogo (atualiza todas as barras vinculadas) ou atribua às selecionadas.").arg(selCount)
            : GerenciadorTraducao::trText("Nenhuma barra selecionada. Selecione um material para editar suas propriedades ou clique em Novo.")
    );
    m_btnAssign->setEnabled(selCount > 0 && !m_model->getMaterials().empty());

    MaterialId focusId = prevId;
    if (focusId == INVALID_ID && selCount > 0) {
        if (const Element* e = m_model->findElement(*m_model->getSelectedElements().begin())) {
            focusId = e->materialId;
        }
    }

    int targetRow = 0;
    const auto& mats = m_model->getMaterials();
    for (int i = 0; i < static_cast<int>(mats.size()); ++i) {
        const auto& m = mats[i];
        QString label = QString("#%1 — %2 | E=%3 GPa | ν=%4 | α=%5×10⁻⁵/°C | γ=%6 kN/m³")
                            .arg(m.id)
                            .arg(GerenciadorTraducao::trText(QString::fromStdString(m.name)))
                            .arg(m.E / 1.0e6, 0, 'f', 2)
                            .arg(m.nu, 0, 'f', 2)
                            .arg(m.alpha * 1.0e5, 0, 'f', 2)
                            .arg(m.gamma, 0, 'f', 1);
        auto* item = new QListWidgetItem(label, m_listWidget);
        item->setData(Qt::UserRole, m.id);
        if (m.id == focusId) targetRow = i;
    }
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(targetRow);
    }
    const bool hasItem = (m_listWidget->count() > 0);
    m_btnEdit->setEnabled(hasItem);
    m_btnDelete->setEnabled(mats.size() > 1 && hasItem);
}

void MaterialCatalogDialog::onNewMaterial() {
    if (!m_model) return;
    if (m_editorDialog) {
        if (m_windowManager) m_windowManager->showAndRaise(m_editorDialog.data());
        return;
    }
    auto* dlg = new MaterialDialog(nullptr, nullptr);
    m_editorDialog = dlg;
    if (m_windowManager) {
        m_windowManager->prepareModelessDialog(dlg, false, true);
    } else {
        dlg->setAttribute(Qt::WA_DeleteOnClose);
    }
    connect(dlg, &QDialog::accepted, this, [this, ptr = QPointer<MaterialDialog>(dlg)]() {
        if (!ptr || !m_model) return;
        Material mat = ptr->getMaterial();
        const MaterialId mid = m_model->addMaterial(mat);
        for (ElementId eid : m_model->getSelectedElements()) {
            if (Element* e = m_model->findElement(eid)) e->materialId = mid;
        }
        m_model->pushUndo();
        refreshList();
        for (int i = 0; i < m_listWidget->count(); ++i) {
            if (m_listWidget->item(i)->data(Qt::UserRole).toInt() == mid) {
                m_listWidget->setCurrentRow(i);
                break;
            }
        }
        if (m_onModelEdited) m_onModelEdited();
    });
    if (m_windowManager) m_windowManager->showAndRaise(dlg);
    else dlg->show();
}

void MaterialCatalogDialog::onEditMaterial() {
    if (!m_model) return;
    const MaterialId mid = selectedMaterialId();
    const Material* currentMat = m_model->findMaterial(mid);
    if (!currentMat) return;
    if (m_editorDialog) {
        if (m_windowManager) m_windowManager->showAndRaise(m_editorDialog.data());
        return;
    }
    auto* dlg = new MaterialDialog(nullptr, currentMat);
    m_editorDialog = dlg;
    if (m_windowManager) {
        m_windowManager->prepareModelessDialog(dlg, false, true);
    } else {
        dlg->setAttribute(Qt::WA_DeleteOnClose);
    }
    connect(dlg, &QDialog::accepted, this, [this, mid, ptr = QPointer<MaterialDialog>(dlg)]() {
        if (!ptr || !m_model) return;
        Material* target = m_model->findMaterial(mid);
        if (!target) return;
        *target = ptr->getMaterial(mid);
        m_model->pushUndo();
        refreshList();
        if (m_onModelEdited) m_onModelEdited();
    });
    if (m_windowManager) m_windowManager->showAndRaise(dlg);
    else dlg->show();
}

void MaterialCatalogDialog::onDeleteMaterial() {
    if (!m_model) return;
    const MaterialId mid = selectedMaterialId();
    if (mid == INVALID_ID || m_model->getMaterials().size() <= 1) return;

    for (const auto& e : m_model->getElements()) {
        if (e.materialId == mid) {
            showCatalogWarning(
                m_windowManager,
                GerenciadorTraducao::trText("Material em Uso"),
                GerenciadorTraducao::trText("Não é possível excluir o material #%1 porque ele está atribuído à Barra %2.")
                    .arg(mid).arg(e.id));
            return;
        }
    }
    if (m_model->removeMaterial(mid)) {
        m_model->pushUndo();
        refreshList();
        if (m_onModelEdited) m_onModelEdited();
    }
}

void MaterialCatalogDialog::onAssignToSelected() {
    if (!m_model) return;
    const MaterialId mid = selectedMaterialId();
    if (mid == INVALID_ID || m_model->getSelectedElements().empty()) return;
    for (ElementId eid : m_model->getSelectedElements()) {
        if (Element* e = m_model->findElement(eid)) {
            e->materialId = mid;
        }
    }
    m_model->pushUndo();
    refreshList();
    if (m_onModelEdited) m_onModelEdited();
}

// ==========================================
// SectionCatalogDialog
// ==========================================
SectionCatalogDialog::SectionCatalogDialog(QWidget* parent,
                                           StructuralModel* model,
                                           WindowZOrderManager* windowManager,
                                           std::function<void()> onModelEdited)
    : QDialog(parent),
      m_model(model),
      m_windowManager(windowManager),
      m_onModelEdited(std::move(onModelEdited)) {
    buildCatalogUi(this, m_selectionInfoLabel, m_listWidget,
                   m_btnNew, m_btnEdit, m_btnDelete, m_btnAssign, m_btnClose);

    connect(m_btnNew, &QPushButton::clicked, this, &SectionCatalogDialog::onNewSection);
    connect(m_btnEdit, &QPushButton::clicked, this, &SectionCatalogDialog::onEditSection);
    connect(m_btnDelete, &QPushButton::clicked, this, &SectionCatalogDialog::onDeleteSection);
    connect(m_btnAssign, &QPushButton::clicked, this, &SectionCatalogDialog::onAssignToSelected);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { onEditSection(); });

    retranslateUi();
    refreshList();
}

void SectionCatalogDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
        refreshList();
    }
}

void SectionCatalogDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Catálogo de Seções Transversais"));
    m_btnNew->setText(GerenciadorTraducao::trText("Nova..."));
    m_btnEdit->setText(GerenciadorTraducao::trText("Editar..."));
    m_btnDelete->setText(GerenciadorTraducao::trText("Excluir"));
    m_btnAssign->setText(GerenciadorTraducao::trText("Atribuir às Barras Selecionadas"));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
}

SectionId SectionCatalogDialog::selectedSectionId() const {
    auto* item = m_listWidget->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : INVALID_ID;
}

void SectionCatalogDialog::refreshList() {
    if (!m_model) return;
    const SectionId prevId = selectedSectionId();
    m_listWidget->clear();

    const size_t selCount = m_model->getSelectedElements().size();
    m_selectionInfoLabel->setText(
        selCount > 0
            ? GerenciadorTraducao::trText("Barras selecionadas: %1. Edite uma seção no catálogo (atualiza todas as barras vinculadas) ou atribua às selecionadas.").arg(selCount)
            : GerenciadorTraducao::trText("Nenhuma barra selecionada. Selecione uma seção para editar suas dimensões/inércias ou clique em Nova.")
    );
    m_btnAssign->setEnabled(selCount > 0 && !m_model->getCrossSections().empty());

    SectionId focusId = prevId;
    if (focusId == INVALID_ID && selCount > 0) {
        if (const Element* e = m_model->findElement(*m_model->getSelectedElements().begin())) {
            focusId = e->sectionId;
        }
    }

    int targetRow = 0;
    const auto& secs = m_model->getCrossSections();
    for (int i = 0; i < static_cast<int>(secs.size()); ++i) {
        const auto& s = secs[i];
        QString label = QString("#%1 — %2 | A=%3 cm² | Iy=%4 m⁴ | Iz=%5 m⁴ | J=%6 m⁴")
                            .arg(s.id)
                            .arg(GerenciadorTraducao::trText(QString::fromStdString(s.name)))
                            .arg(s.A * 1.0e4, 0, 'f', 1)
                            .arg(s.Iy, 0, 'e', 2)
                            .arg(s.Iz, 0, 'e', 2)
                            .arg(s.J, 0, 'e', 2);
        auto* item = new QListWidgetItem(label, m_listWidget);
        item->setData(Qt::UserRole, s.id);
        if (s.id == focusId) targetRow = i;
    }
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(targetRow);
    }
    const bool hasItem = (m_listWidget->count() > 0);
    m_btnEdit->setEnabled(hasItem);
    m_btnDelete->setEnabled(secs.size() > 1 && hasItem);
}

void SectionCatalogDialog::onNewSection() {
    if (!m_model) return;
    if (m_editorDialog) {
        if (m_windowManager) m_windowManager->showAndRaise(m_editorDialog.data());
        return;
    }
    auto* dlg = new SectionDialog(nullptr, nullptr);
    m_editorDialog = dlg;
    if (m_windowManager) {
        m_windowManager->prepareModelessDialog(dlg, false, true);
    } else {
        dlg->setAttribute(Qt::WA_DeleteOnClose);
    }
    connect(dlg, &QDialog::accepted, this, [this, ptr = QPointer<SectionDialog>(dlg)]() {
        if (!ptr || !m_model) return;
        CrossSection sec = ptr->getSection();
        const SectionId sid = m_model->addCrossSection(sec);
        for (ElementId eid : m_model->getSelectedElements()) {
            if (Element* e = m_model->findElement(eid)) e->sectionId = sid;
        }
        m_model->pushUndo();
        refreshList();
        for (int i = 0; i < m_listWidget->count(); ++i) {
            if (m_listWidget->item(i)->data(Qt::UserRole).toInt() == sid) {
                m_listWidget->setCurrentRow(i);
                break;
            }
        }
        if (m_onModelEdited) m_onModelEdited();
    });
    if (m_windowManager) m_windowManager->showAndRaise(dlg);
    else dlg->show();
}

void SectionCatalogDialog::onEditSection() {
    if (!m_model) return;
    const SectionId sid = selectedSectionId();
    const CrossSection* currentSec = m_model->findCrossSection(sid);
    if (!currentSec) return;
    if (m_editorDialog) {
        if (m_windowManager) m_windowManager->showAndRaise(m_editorDialog.data());
        return;
    }
    auto* dlg = new SectionDialog(nullptr, currentSec);
    m_editorDialog = dlg;
    if (m_windowManager) {
        m_windowManager->prepareModelessDialog(dlg, false, true);
    } else {
        dlg->setAttribute(Qt::WA_DeleteOnClose);
    }
    connect(dlg, &QDialog::accepted, this, [this, sid, ptr = QPointer<SectionDialog>(dlg)]() {
        if (!ptr || !m_model) return;
        CrossSection* target = m_model->findCrossSection(sid);
        if (!target) return;
        *target = ptr->getSection(sid);
        m_model->pushUndo();
        refreshList();
        if (m_onModelEdited) m_onModelEdited();
    });
    if (m_windowManager) m_windowManager->showAndRaise(dlg);
    else dlg->show();
}

void SectionCatalogDialog::onDeleteSection() {
    if (!m_model) return;
    const SectionId sid = selectedSectionId();
    if (sid == INVALID_ID || m_model->getCrossSections().size() <= 1) return;

    for (const auto& e : m_model->getElements()) {
        if (e.sectionId == sid) {
            showCatalogWarning(
                m_windowManager,
                GerenciadorTraducao::trText("Seção em Uso"),
                GerenciadorTraducao::trText("Não é possível excluir a seção #%1 porque ela está atribuída à Barra %2.")
                    .arg(sid).arg(e.id));
            return;
        }
    }
    if (m_model->removeCrossSection(sid)) {
        m_model->pushUndo();
        refreshList();
        if (m_onModelEdited) m_onModelEdited();
    }
}

void SectionCatalogDialog::onAssignToSelected() {
    if (!m_model) return;
    const SectionId sid = selectedSectionId();
    if (sid == INVALID_ID || m_model->getSelectedElements().empty()) return;
    for (ElementId eid : m_model->getSelectedElements()) {
        if (Element* e = m_model->findElement(eid)) {
            e->sectionId = sid;
        }
    }
    m_model->pushUndo();
    refreshList();
    if (m_onModelEdited) m_onModelEdited();
}

} // namespace omniframe
