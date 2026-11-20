#include "EngineeringDialogs.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <numbers>

namespace omniframe {

namespace {

using locale::GerenciadorTraducao;

double maxTranslationalDisp(const StructuralModel& model, const kernel::VecX& uFull) {
    return FemSolver3D::extractNodalDisplacements(model, uFull).maxTranslationalNorm;
}

void applyFieldToViewport(Viewport3D* viewport,
                          const StructuralModel& model,
                          const kernel::VecX& uFull,
                          const QString& label,
                          double baseScale,
                          bool oscillating) {
    const auto field = FemSolver3D::extractNodalDisplacements(model, uFull);
    viewport->setPostAnimationField(label, field.frameNodes, baseScale, oscillating);
    viewport->setPostMeshAnimationField(field.meshNodes);
}

template <typename HistoryT, typename FactorGetter>
std::size_t applyStepFramesToViewport(Viewport3D* viewport,
                                      const StructuralModel& model,
                                      const HistoryT& history,
                                      std::size_t stride,
                                      const QString& label,
                                      FactorGetter&& factorFn) {
    std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>> frameNodes;
    std::vector<std::pair<double, std::unordered_map<NodeId, Vec3>>> meshFrames;
    for (std::size_t i = 0; i < history.size(); i += stride) {
        const auto& rec = history[i];
        const double fac = factorFn(rec);
        const auto field = FemSolver3D::extractNodalDisplacements(model, rec.U_full);
        frameNodes.emplace_back(fac, field.frameNodes);
        meshFrames.emplace_back(fac, field.meshNodes);
    }
    viewport->setNonlinearStepFrames(label, frameNodes);
    viewport->setNonlinearMeshStepFrames(meshFrames);
    return frameNodes.size();
}

void initReadOnlyTable(QTableWidget* tbl, const QStringList& headers) {
    tbl->setColumnCount(headers.size());
    tbl->setHorizontalHeaderLabels(headers);
    tbl->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    tbl->setSelectionMode(QAbstractItemView::SingleSelection);
}

QTableWidgetItem* makeItem(const QString& text, Qt::Alignment align = Qt::AlignCenter) {
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(align);
    return it;
}

QLabel* makeTrLabel(const char* ptKey, QWidget* parent) {
    const QString key = QString::fromUtf8(ptKey);
    auto* lbl = new QLabel(GerenciadorTraducao::trText(key), parent);
    lbl->setProperty("omniTrKey", key);
    return lbl;
}

QCheckBox* makeTrCheck(const char* ptKey, QWidget* parent) {
    const QString key = QString::fromUtf8(ptKey);
    auto* chk = new QCheckBox(GerenciadorTraducao::trText(key), parent);
    chk->setProperty("omniTrKey", key);
    return chk;
}

QGroupBox* makeTrGroup(const char* ptKey, QWidget* parent) {
    const QString key = QString::fromUtf8(ptKey);
    auto* grp = new QGroupBox(GerenciadorTraducao::trText(key), parent);
    grp->setProperty("omniTrKey", key);
    return grp;
}

template <typename WidgetT>
void addTrRow(QFormLayout* form, const char* ptKey, WidgetT* field, QWidget* parent) {
    form->addRow(makeTrLabel(ptKey, parent), field);
}

void setTrHeaders(QTableWidget* tbl, const std::initializer_list<const char*>& keys) {
    if (!tbl) return;
    QStringList list;
    list.reserve(static_cast<int>(keys.size()));
    for (const char* k : keys) {
        list.append(GerenciadorTraducao::trText(QString::fromUtf8(k)));
    }
    tbl->setHorizontalHeaderLabels(list);
}

void retranslateComboItems(QComboBox* combo, const std::initializer_list<const char*>& keys) {
    if (!combo) return;
    int idx = 0;
    for (const char* k : keys) {
        if (idx < combo->count()) {
            combo->setItemText(idx, GerenciadorTraducao::trText(QString::fromUtf8(k)));
        }
        ++idx;
    }
}

void retranslateTaggedWidgets(QWidget* root) {
    if (!root) return;
    for (QObject* obj : root->findChildren<QObject*>()) {
        const QVariant keyVar = obj->property("omniTrKey");
        if (!keyVar.isValid()) continue;
        const QString translated = GerenciadorTraducao::trText(keyVar.toString());
        if (auto* lbl = qobject_cast<QLabel*>(obj)) {
            lbl->setText(translated);
        } else if (auto* chk = qobject_cast<QCheckBox*>(obj)) {
            chk->setText(translated);
        } else if (auto* grp = qobject_cast<QGroupBox*>(obj)) {
            grp->setTitle(translated);
        }
    }
}

void copySolvedNodalElementResults(const StructuralModel& src, StructuralModel& dst) {
    const auto& srcNodes = src.getNodes();
    auto& dstNodes = dst.getNodes();
    for (std::size_t i = 0; i < srcNodes.size() && i < dstNodes.size(); ++i) {
        if (srcNodes[i].id == dstNodes[i].id) {
            dstNodes[i].displacement = srcNodes[i].displacement;
            dstNodes[i].reaction = srcNodes[i].reaction;
        }
    }
    const auto& srcElems = src.getElements();
    auto& dstElems = dst.getElements();
    for (std::size_t i = 0; i < srcElems.size() && i < dstElems.size(); ++i) {
        if (srcElems[i].id == dstElems[i].id) {
            dstElems[i].localEndForces = srcElems[i].localEndForces;
            dstElems[i].globalDisplacements = srcElems[i].globalDisplacements;
            dstElems[i].localDisplacements = srcElems[i].localDisplacements;
            dstElems[i].hasResults = srcElems[i].hasResults;
        }
    }
    const auto& srcMesh = src.surfaceMesh();
    auto& dstMesh = dst.surfaceMesh();
    if (srcMesh.nodes.size() == dstMesh.nodes.size()) {
        for (std::size_t i = 0; i < srcMesh.nodes.size(); ++i) {
            if (srcMesh.nodes[i].id == dstMesh.nodes[i].id) {
                dstMesh.nodes[i].hasResults = srcMesh.nodes[i].hasResults;
                dstMesh.nodes[i].displacement = srcMesh.nodes[i].displacement;
                dstMesh.nodes[i].reaction = srcMesh.nodes[i].reaction;
            }
        }
    }
    if (srcMesh.elements.size() == dstMesh.elements.size()) {
        for (std::size_t i = 0; i < srcMesh.elements.size(); ++i) {
            if (srcMesh.elements[i].id == dstMesh.elements[i].id) {
                dstMesh.elements[i].hasResults = srcMesh.elements[i].hasResults;
                dstMesh.elements[i].vonMisesStress = srcMesh.elements[i].vonMisesStress;
                dstMesh.elements[i].maxPrincipalStress = srcMesh.elements[i].maxPrincipalStress;
                dstMesh.elements[i].minPrincipalStress = srcMesh.elements[i].minPrincipalStress;
                dstMesh.elements[i].membraneResultants = srcMesh.elements[i].membraneResultants;
                dstMesh.elements[i].bendingResultants = srcMesh.elements[i].bendingResultants;
                dstMesh.elements[i].transverseShear = srcMesh.elements[i].transverseShear;
                dstMesh.elements[i].stressVoigt = srcMesh.elements[i].stressVoigt;
            }
        }
    }
}

template <typename ResultT, typename WorkFn, typename DoneFn>
void runAsyncJob(QObject* contextObj, WorkFn&& workFn, DoneFn&& doneFn) {
    auto payload = std::make_shared<ResultT>();
    QThread* worker = QThread::create([payload, fn = std::forward<WorkFn>(workFn)]() mutable {
        *payload = fn();
    });
    QObject::connect(worker, &QThread::finished, contextObj,
                     [payload, worker, onDone = std::forward<DoneFn>(doneFn)]() mutable {
                         onDone(*payload);
                         worker->deleteLater();
                     });
    worker->start();
}

} // namespace

// ==========================================
// AnalysisSuiteDialog
// ==========================================
AnalysisSuiteDialog::AnalysisSuiteDialog(
    QWidget* parent,
    StructuralModel* model,
    Viewport3D* viewport,
    const SolverResult* lastStaticResult,
    std::function<void(const SolverResult&)> onStaticOrNonlinearCompleted,
    std::function<void(const QString&)> onStatusMsg,
    std::function<void()> onModelMutated)
    : QDialog(parent),
      m_model(model),
      m_viewport(viewport),
      m_lastStatic(lastStaticResult),
      m_onCompleted(std::move(onStaticOrNonlinearCompleted)),
      m_onStatus(std::move(onStatusMsg)),
      m_onModelMutated(std::move(onModelMutated)) {
    setModal(false);
    setWindowModality(Qt::NonModal);
    resize(840, 620);

    auto* root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    // Aba 1: Casos & Combinações NBR
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* topRow = new QHBoxLayout();
        m_linearSolverCombo = new QComboBox(tab);
        m_linearSolverCombo->addItem(QStringLiteral("Auto"), static_cast<int>(kernel::LinearSolverType::Auto));
        m_linearSolverCombo->addItem(QStringLiteral("SimplicialLDLT (SPD Cholesky)"), static_cast<int>(kernel::LinearSolverType::SimplicialLDLT));
        m_linearSolverCombo->addItem(QStringLiteral("SparseLU (COLAMD)"), static_cast<int>(kernel::LinearSolverType::SparseLU));
        m_linearSolverCombo->addItem(QStringLiteral("ConjugateGradient (PCG)"), static_cast<int>(kernel::LinearSolverType::ConjugateGradient));
        m_btnRunCombos = new QPushButton(tab);
        m_btnPlotCombos = new QPushButton(tab);
        m_actionButtons.push_back(m_btnRunCombos);
        m_actionButtons.push_back(m_btnPlotCombos);
        connect(m_btnRunCombos, &QPushButton::clicked, this, &AnalysisSuiteDialog::onRunCombosAsync);
        connect(m_btnPlotCombos, &QPushButton::clicked, this, &AnalysisSuiteDialog::onPlotSelectedCombo);
        topRow->addWidget(makeTrLabel("Solver:", tab));
        topRow->addWidget(m_linearSolverCombo, 1);
        topRow->addWidget(m_btnRunCombos);
        topRow->addWidget(m_btnPlotCombos);
        lay->addLayout(topRow);

        m_combosTable = new QTableWidget(tab);
        initReadOnlyTable(m_combosTable, {QStringLiteral("ID"), QStringLiteral("Tipo"), QStringLiteral("Nome"), QStringLiteral("|U| máx (mm)"), QStringLiteral("Status")});
        connect(m_combosTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { onPlotSelectedCombo(); });
        lay->addWidget(m_combosTable, 1);
        m_tabs->addTab(tab, QString());
    }

    // Aba 2: Modal
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* topRow = new QHBoxLayout();
        m_modalModesSpin = new QSpinBox(tab);
        m_modalModesSpin->setRange(1, 30);
        m_modalModesSpin->setValue(6);
        m_btnRunModal = new QPushButton(tab);
        m_btnPlotModal = new QPushButton(tab);
        m_actionButtons.push_back(m_btnRunModal);
        m_actionButtons.push_back(m_btnPlotModal);
        connect(m_btnRunModal, &QPushButton::clicked, this, &AnalysisSuiteDialog::onRunModalAsync);
        connect(m_btnPlotModal, &QPushButton::clicked, this, &AnalysisSuiteDialog::onPlotSelectedMode);
        topRow->addWidget(makeTrLabel("Modos:", tab));
        topRow->addWidget(m_modalModesSpin);
        topRow->addWidget(m_btnRunModal);
        topRow->addWidget(m_btnPlotModal);
        topRow->addStretch();
        lay->addLayout(topRow);

        m_modalTable = new QTableWidget(tab);
        initReadOnlyTable(m_modalTable, {QStringLiteral("Modo"), QStringLiteral("f (Hz)"), QStringLiteral("T (s)"), QStringLiteral("ω (rad/s)"), QStringLiteral("Mx% / My% / Mz%")});
        connect(m_modalTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { onPlotSelectedMode(); });
        lay->addWidget(m_modalTable, 1);
        m_tabs->addTab(tab, QString());
    }

    // Aba 3: Flambagem
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* topRow = new QHBoxLayout();
        m_bucklingModesSpin = new QSpinBox(tab);
        m_bucklingModesSpin->setRange(1, 20);
        m_bucklingModesSpin->setValue(4);
        m_bucklingNegCheck = makeTrCheck("Incluir λ < 0", tab);
        m_btnRunBuckling = new QPushButton(tab);
        m_btnPlotBuckling = new QPushButton(tab);
        m_actionButtons.push_back(m_btnRunBuckling);
        m_actionButtons.push_back(m_btnPlotBuckling);
        connect(m_btnRunBuckling, &QPushButton::clicked, this, &AnalysisSuiteDialog::onRunBucklingAsync);
        connect(m_btnPlotBuckling, &QPushButton::clicked, this, &AnalysisSuiteDialog::onPlotSelectedBucklingMode);
        topRow->addWidget(makeTrLabel("Modos:", tab));
        topRow->addWidget(m_bucklingModesSpin);
        topRow->addWidget(m_bucklingNegCheck);
        topRow->addWidget(m_btnRunBuckling);
        topRow->addWidget(m_btnPlotBuckling);
        topRow->addStretch();
        lay->addLayout(topRow);

        auto* impRow = new QHBoxLayout();
        m_impAmpSpin = new QDoubleSpinBox(tab);
        m_impAmpSpin->setRange(-5.0, 5.0);
        m_impAmpSpin->setDecimals(4);
        m_impAmpSpin->setSingleStep(0.005);
        m_impAmpSpin->setValue(0.02);
        m_impAmpSpin->setSuffix(QStringLiteral(" m"));
        m_btnApplyImperfection = new QPushButton(tab);
        m_actionButtons.push_back(m_btnApplyImperfection);
        connect(m_btnApplyImperfection, &QPushButton::clicked, this, &AnalysisSuiteDialog::onApplyBucklingImperfection);
        impRow->addWidget(makeTrLabel("Imperfeição Inicial (amp):", tab));
        impRow->addWidget(m_impAmpSpin);
        impRow->addWidget(m_btnApplyImperfection);
        impRow->addStretch();
        lay->addLayout(impRow);

        m_bucklingTable = new QTableWidget(tab);
        initReadOnlyTable(m_bucklingTable, {QStringLiteral("Modo"), QStringLiteral("Fator Crítico λ_cr"), QStringLiteral("Situação")});
        connect(m_bucklingTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { onPlotSelectedBucklingMode(); });
        lay->addWidget(m_bucklingTable, 1);
        m_tabs->addTab(tab, QString());
    }

    // Aba 4: Harmônica
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* form = new QHBoxLayout();
        m_harmFMinSpin = new QDoubleSpinBox(tab);
        m_harmFMinSpin->setRange(0.05, 5000.0);
        m_harmFMinSpin->setValue(1.0);
        m_harmFMinSpin->setSuffix(QStringLiteral(" Hz"));
        m_harmFMaxSpin = new QDoubleSpinBox(tab);
        m_harmFMaxSpin->setRange(0.1, 10000.0);
        m_harmFMaxSpin->setValue(30.0);
        m_harmFMaxSpin->setSuffix(QStringLiteral(" Hz"));
        m_harmStepsSpin = new QSpinBox(tab);
        m_harmStepsSpin->setRange(2, 500);
        m_harmStepsSpin->setValue(30);
        m_harmEtaSpin = new QDoubleSpinBox(tab);
        m_harmEtaSpin->setRange(0.0, 0.5);
        m_harmEtaSpin->setDecimals(3);
        m_harmEtaSpin->setValue(0.02);
        m_btnRunHarmonic = new QPushButton(tab);
        m_btnPlotHarmonic = new QPushButton(tab);
        m_actionButtons.push_back(m_btnRunHarmonic);
        m_actionButtons.push_back(m_btnPlotHarmonic);
        connect(m_btnRunHarmonic, &QPushButton::clicked, this, &AnalysisSuiteDialog::onRunHarmonicAsync);
        connect(m_btnPlotHarmonic, &QPushButton::clicked, this, &AnalysisSuiteDialog::onPlotSelectedHarmonicStep);
        form->addWidget(new QLabel(QStringLiteral("f_min:"), tab));
        form->addWidget(m_harmFMinSpin);
        form->addWidget(new QLabel(QStringLiteral("f_max:"), tab));
        form->addWidget(m_harmFMaxSpin);
        form->addWidget(new QLabel(QStringLiteral("N:"), tab));
        form->addWidget(m_harmStepsSpin);
        form->addWidget(new QLabel(QStringLiteral("η:"), tab));
        form->addWidget(m_harmEtaSpin);
        form->addWidget(m_btnRunHarmonic);
        form->addWidget(m_btnPlotHarmonic);
        lay->addLayout(form);

        m_harmonicTable = new QTableWidget(tab);
        initReadOnlyTable(m_harmonicTable, {QStringLiteral("Passo"), QStringLiteral("f (Hz)"), QStringLiteral("ω (rad/s)"), QStringLiteral("|U|_máx (mm)")});
        connect(m_harmonicTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { onPlotSelectedHarmonicStep(); });
        lay->addWidget(m_harmonicTable, 1);
        m_tabs->addTab(tab, QString());
    }

    // Aba 5: Espectro de Resposta
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        m_specNoteLabel = new QLabel(tab);
        m_specNoteLabel->setWordWrap(true);
        lay->addWidget(m_specNoteLabel);

        auto* form = new QFormLayout();
        m_specComboRule = new QComboBox(tab);
        m_specComboRule->addItem(QStringLiteral("CQC (Der Kiureghian)"), static_cast<int>(kernel::ResponseSpectrumCombination::CQC));
        m_specComboRule->addItem(QStringLiteral("SRSS"), static_cast<int>(kernel::ResponseSpectrumCombination::SRSS));
        m_specComboRule->addItem(QStringLiteral("ABSSUM"), static_cast<int>(kernel::ResponseSpectrumCombination::ABSSUM));
        m_specZetaSpin = new QDoubleSpinBox(tab);
        m_specZetaSpin->setRange(0.0, 0.5);
        m_specZetaSpin->setDecimals(3);
        m_specZetaSpin->setValue(0.05);
        m_specAgSpin = new QDoubleSpinBox(tab);
        m_specAgSpin->setRange(0.01, 20.0);
        m_specAgSpin->setValue(1.47);
        m_specAgSpin->setSuffix(QStringLiteral(" m/s²"));
        m_specCaSpin = new QDoubleSpinBox(tab);
        m_specCaSpin->setRange(0.1, 5.0);
        m_specCaSpin->setValue(1.2);
        m_specCvSpin = new QDoubleSpinBox(tab);
        m_specCvSpin->setRange(0.1, 5.0);
        m_specCvSpin->setValue(1.8);
        m_specDirX = new QDoubleSpinBox(tab); m_specDirX->setRange(0.0, 1.0); m_specDirX->setValue(1.0);
        m_specDirY = new QDoubleSpinBox(tab); m_specDirY->setRange(0.0, 1.0); m_specDirY->setValue(0.3);
        m_specDirZ = new QDoubleSpinBox(tab); m_specDirZ->setRange(0.0, 1.0); m_specDirZ->setValue(0.0);

        addTrRow(form, "Regra de Combinação:", m_specComboRule, tab);
        addTrRow(form, "Amortecimento Modal (ζ):", m_specZetaSpin, tab);
        addTrRow(form, "Aceleração Sísmica (a_gs):", m_specAgSpin, tab);
        addTrRow(form, "Fator Ca (T=0,2s) / Cv (T=1,0s):", [tab, this]() {
            auto* w = new QWidget(tab);
            auto* h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(m_specCaSpin);
            h->addWidget(m_specCvSpin);
            return w;
        }(), tab);
        addTrRow(form, "Pesos Direcionais (X / Y / Z):", [tab, this]() {
            auto* w = new QWidget(tab);
            auto* h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(m_specDirX);
            h->addWidget(m_specDirY);
            h->addWidget(m_specDirZ);
            return w;
        }(), tab);
        lay->addLayout(form);

        auto* btnRow = new QHBoxLayout();
        m_btnRunSpec = new QPushButton(tab);
        m_btnPlotSpec = new QPushButton(tab);
        m_actionButtons.push_back(m_btnRunSpec);
        m_actionButtons.push_back(m_btnPlotSpec);
        connect(m_btnRunSpec, &QPushButton::clicked, this, &AnalysisSuiteDialog::onRunSpectrumAsync);
        connect(m_btnPlotSpec, &QPushButton::clicked, this, &AnalysisSuiteDialog::onPlotSpectrumResult);
        btnRow->addWidget(m_btnRunSpec);
        btnRow->addWidget(m_btnPlotSpec);
        btnRow->addStretch();
        lay->addLayout(btnRow);

        m_specResultLabel = new QLabel(tab);
        lay->addWidget(m_specResultLabel);
        lay->addStretch();
        m_tabs->addTab(tab, QString());
    }

    // Aba 6: Transiente / Sísmica
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* form = new QFormLayout();
        m_seisModeCombo = new QComboBox(tab);
        m_seisModeCombo->addItem(QStringLiteral("Acelerograma Sísmico (a_g)"), 0);
        m_seisModeCombo->addItem(QStringLiteral("Histórico de Força F(t)"), 1);
        m_seisLoadCaseCombo = new QComboBox(tab);
        refreshSeismicLoadCases();
        m_seisSchemeCombo = new QComboBox(tab);
        m_seisSchemeCombo->addItem(QStringLiteral("Newmark-Beta (γ=0,50, β=0,25)"), static_cast<int>(kernel::TransientScheme::NewmarkBeta));
        m_seisSchemeCombo->addItem(QStringLiteral("HHT-Alpha (α=-0,05)"), static_cast<int>(kernel::TransientScheme::HHTAlpha));
        m_seisDtSpin = new QDoubleSpinBox(tab);
        m_seisDtSpin->setRange(0.0005, 0.2);
        m_seisDtSpin->setDecimals(4);
        m_seisDtSpin->setValue(0.01);
        m_seisDtSpin->setSuffix(QStringLiteral(" s"));
        m_seisStepsSpin = new QSpinBox(tab);
        m_seisStepsSpin->setRange(10, 2000);
        m_seisStepsSpin->setValue(150);
        m_seisAmpSpin = new QDoubleSpinBox(tab);
        m_seisAmpSpin->setRange(0.01, 30.0);
        m_seisAmpSpin->setValue(1.5);
        m_seisAmpSpin->setSuffix(QStringLiteral(" m/s²"));
        m_seisFreqSpin = new QDoubleSpinBox(tab);
        m_seisFreqSpin->setRange(0.1, 50.0);
        m_seisFreqSpin->setValue(2.5);
        m_seisFreqSpin->setSuffix(QStringLiteral(" Hz"));
        m_seisDirCombo = new QComboBox(tab);
        m_seisDirCombo->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});

        m_seisAmpLabel = new QLabel(GerenciadorTraducao::trText("Pico do Sismo (a_g):"), tab);
        connect(m_seisModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            const bool isForceHistory = (idx == 1);
            if (m_seisLoadCaseCombo) m_seisLoadCaseCombo->setEnabled(isForceHistory);
            if (m_seisDirCombo) m_seisDirCombo->setEnabled(!isForceHistory);
            if (m_seisAmpLabel) {
                m_seisAmpLabel->setText(isForceHistory
                    ? GerenciadorTraducao::trText("Fator de Escala (A₀):")
                    : GerenciadorTraducao::trText("Pico do Sismo (a_g):"));
            }
            if (m_seisAmpSpin) {
                m_seisAmpSpin->setSuffix(isForceHistory ? QStringLiteral(" x") : QStringLiteral(" m/s²"));
                if (isForceHistory && std::abs(m_seisAmpSpin->value() - 1.5) < 1e-6) {
                    m_seisAmpSpin->setValue(1.0);
                }
            }
        });
        m_seisLoadCaseCombo->setEnabled(false);

        addTrRow(form, "Tipo de Excitação:", m_seisModeCombo, tab);
        addTrRow(form, "Caso Base F(t):", m_seisLoadCaseCombo, tab);
        addTrRow(form, "Integrador Temporal:", m_seisSchemeCombo, tab);
        addTrRow(form, "Passo de Tempo (Δt):", m_seisDtSpin, tab);
        addTrRow(form, "Número de Passos:", m_seisStepsSpin, tab);
        form->addRow(m_seisAmpLabel, m_seisAmpSpin);
        addTrRow(form, "Frequência Excitação:", m_seisFreqSpin, tab);
        addTrRow(form, "Direção da Excitação:", m_seisDirCombo, tab);
        lay->addLayout(form);

        auto* btnRow = new QHBoxLayout();
        m_btnRunSeis = new QPushButton(tab);
        m_btnPlaySeis = new QPushButton(tab);
        m_actionButtons.push_back(m_btnRunSeis);
        m_actionButtons.push_back(m_btnPlaySeis);
        connect(m_btnRunSeis, &QPushButton::clicked, this, &AnalysisSuiteDialog::onRunSeismicAsync);
        connect(m_btnPlaySeis, &QPushButton::clicked, this, &AnalysisSuiteDialog::onPlaySeismicHistory);
        btnRow->addWidget(m_btnRunSeis);
        btnRow->addWidget(m_btnPlaySeis);
        btnRow->addStretch();
        lay->addLayout(btnRow);

        m_seisResultLabel = new QLabel(tab);
        lay->addWidget(m_seisResultLabel);
        lay->addStretch();
        m_tabs->addTab(tab, QString());
    }

    // Aba 7: Não Linear (P-Delta / Newton-Raphson / Crisfield Arc-Length)
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* form = new QHBoxLayout();
        m_nlMethodCombo = new QComboBox(tab);
        m_nlMethodCombo->addItem(QStringLiteral("P-Delta (Iterativo)"), static_cast<int>(kernel::NonlinearMethod::PDelta));
        m_nlMethodCombo->addItem(QStringLiteral("Newton-Raphson"), static_cast<int>(kernel::NonlinearMethod::NewtonRaphson));
        m_nlMethodCombo->addItem(QStringLiteral("Arc-Length (Crisfield)"), static_cast<int>(kernel::NonlinearMethod::ArcLengthCrisfield));
        m_nlMethodCombo->setCurrentIndex(1);

        m_nlKinCombo = new QComboBox(tab);
        m_nlKinCombo->addItem(QStringLiteral("Pequenas Def."), static_cast<int>(kernel::NonlinearKinematics::SmallStrain));
        m_nlKinCombo->addItem(QStringLiteral("P-Delta 2ª Ordem"), static_cast<int>(kernel::NonlinearKinematics::PDeltaSecondOrder));
        m_nlKinCombo->addItem(QStringLiteral("Corrotacional 3D"), static_cast<int>(kernel::NonlinearKinematics::Corotational3D));
        m_nlKinCombo->setCurrentIndex(2);

        m_nlStepsSpin = new QSpinBox(tab);
        m_nlStepsSpin->setRange(1, 200);
        m_nlStepsSpin->setValue(10);
        m_nlFactorSpin = new QDoubleSpinBox(tab);
        m_nlFactorSpin->setRange(0.1, 20.0);
        m_nlFactorSpin->setValue(1.0);
        m_nlFullNewtonCheck = makeTrCheck("Tangente Completa", tab);
        m_nlFullNewtonCheck->setChecked(true);
        m_nlLineSearchCheck = new QCheckBox(QStringLiteral("Line Search"), tab);
        m_nlLineSearchCheck->setChecked(true);

        form->addWidget(m_nlMethodCombo);
        form->addWidget(m_nlKinCombo);
        form->addWidget(makeTrLabel("Passos:", tab));
        form->addWidget(m_nlStepsSpin);
        form->addWidget(makeTrLabel("λ_alvo:", tab));
        form->addWidget(m_nlFactorSpin);
        form->addWidget(m_nlFullNewtonCheck);
        form->addWidget(m_nlLineSearchCheck);
        lay->addLayout(form);

        auto* btnRow = new QHBoxLayout();
        m_btnRunNl = new QPushButton(tab);
        m_btnPlayNl = new QPushButton(tab);
        m_actionButtons.push_back(m_btnRunNl);
        m_actionButtons.push_back(m_btnPlayNl);
        connect(m_btnRunNl, &QPushButton::clicked, this, &AnalysisSuiteDialog::onRunNonlinearAsync);
        connect(m_btnPlayNl, &QPushButton::clicked, this, &AnalysisSuiteDialog::onPlayNonlinearHistory);
        btnRow->addWidget(m_btnRunNl);
        btnRow->addWidget(m_btnPlayNl);
        btnRow->addStretch();
        lay->addLayout(btnRow);

        m_nlTable = new QTableWidget(tab);
        initReadOnlyTable(m_nlTable, {QStringLiteral("Passo"), QStringLiteral("Fator λ"), QStringLiteral("Iterações"), QStringLiteral("||R||"), QStringLiteral("Plastificou J2")});
        lay->addWidget(m_nlTable, 1);
        m_tabs->addTab(tab, QString());
    }

    root->addWidget(m_tabs, 1);

    auto* bottomRow = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_progressBar->setMaximumWidth(180);
    m_statusLabel = new QLabel(this);
    m_btnClose = new QPushButton(this);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(m_progressBar);
    bottomRow->addWidget(m_statusLabel, 1);
    bottomRow->addWidget(m_btnClose);
    root->addLayout(bottomRow);

    retranslateUi();
}

void AnalysisSuiteDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void AnalysisSuiteDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Suite de Análises Avançadas (Assíncrona MEF 3D)"));
    retranslateTaggedWidgets(this);
    m_tabs->setTabText(0, GerenciadorTraducao::trText("Combinações & Envoltória"));
    m_tabs->setTabText(1, GerenciadorTraducao::trText("Modal"));
    m_tabs->setTabText(2, GerenciadorTraducao::trText("Flambagem"));
    m_tabs->setTabText(3, GerenciadorTraducao::trText("Harmônica FRF"));
    m_tabs->setTabText(4, GerenciadorTraducao::trText("Espectro Sísmico"));
    m_tabs->setTabText(5, GerenciadorTraducao::trText("Transiente / Sismo"));
    m_tabs->setTabText(6, GerenciadorTraducao::trText("Não Linear"));

    retranslateComboItems(m_linearSolverCombo, {
        "Auto",
        "SimplicialLDLT (SPD Cholesky)",
        "SparseLU (COLAMD)",
        "ConjugateGradient (PCG)"
    });
    retranslateComboItems(m_nlMethodCombo, {
        "P-Delta (Iterativo)",
        "Newton-Raphson",
        "Arc-Length (Crisfield)"
    });
    retranslateComboItems(m_nlKinCombo, {
        "Pequenas Def.",
        "P-Delta 2ª Ordem",
        "Corrotacional 3D"
    });
    setTrHeaders(m_combosTable, {"ID", "Tipo", "Nome", "|U| máx (mm)", "Status"});
    setTrHeaders(m_modalTable, {"Modo", "f (Hz)", "T (s)", "ω (rad/s)", "Mx% / My% / Mz%"});
    setTrHeaders(m_bucklingTable, {"Modo", "Fator Crítico λ_cr", "Situação"});
    setTrHeaders(m_harmonicTable, {"Passo", "f (Hz)", "ω (rad/s)", "|U|_máx (mm)"});
    setTrHeaders(m_nlTable, {"Passo", "Fator λ", "Iterações", "||R||", "Plastificou J2"});

    m_btnRunCombos->setText(GerenciadorTraducao::trText("Resolver Casos & Combinações"));
    m_btnPlotCombos->setText(GerenciadorTraducao::trText("Plotar Caso/Combinação no Viewport 3D"));
    m_btnRunModal->setText(GerenciadorTraducao::trText("Calcular Modos"));
    m_btnPlotModal->setText(GerenciadorTraducao::trText("Animar Modo no Viewport 3D"));
    m_btnRunBuckling->setText(GerenciadorTraducao::trText("Calcular Flambagem"));
    m_btnPlotBuckling->setText(GerenciadorTraducao::trText("Animar Flambagem no Viewport 3D"));
    if (m_btnApplyImperfection) {
        m_btnApplyImperfection->setText(GerenciadorTraducao::trText("Aplicar Imperfeição à Geometria"));
    }
    if (m_seisModeCombo) {
        retranslateComboItems(m_seisModeCombo, {
            "Acelerograma Sísmico (a_g)",
            "Histórico de Força F(t)"
        });
        if (m_seisAmpLabel) {
            const bool isForceHistory = (m_seisModeCombo->currentIndex() == 1);
            m_seisAmpLabel->setText(isForceHistory
                ? GerenciadorTraducao::trText("Fator de Escala (A₀):")
                : GerenciadorTraducao::trText("Pico do Sismo (a_g):"));
        }
    }
    m_btnRunHarmonic->setText(GerenciadorTraducao::trText("Varrimento Harmônico"));
    m_btnPlotHarmonic->setText(GerenciadorTraducao::trText("Animar Frequência no Viewport 3D"));
    m_specNoteLabel->setText(GerenciadorTraducao::trText(
        "Nota de rastreabilidade: curva parametrizada (ag_s, Ca, Cv). "
        "O texto completo da ABNT NBR 15421 não integra a biblioteca local (OutOfLocalPdfScope); confira os parâmetros sísmicos para o projeto."));
    m_btnRunSpec->setText(GerenciadorTraducao::trText("Combinar Modos (Espectro)"));
    m_btnPlotSpec->setText(GerenciadorTraducao::trText("Plotar Resposta no Viewport 3D"));
    m_btnRunSeis->setText(GerenciadorTraducao::trText("Integrar Histórico Temporal"));
    m_btnPlaySeis->setText(GerenciadorTraducao::trText("Reproduzir Histórico no Viewport 3D"));
    m_btnRunNl->setText(GerenciadorTraducao::trText("Resolver Trajetória Não Linear"));
    m_btnPlayNl->setText(GerenciadorTraducao::trText("Animar Passos no Viewport 3D"));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
    if (!m_busy) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Pronto."));
    }
}

void AnalysisSuiteDialog::refreshSeismicLoadCases() {
    if (!m_seisLoadCaseCombo) return;
    const QVariant prevData = m_seisLoadCaseCombo->currentData();
    m_seisLoadCaseCombo->clear();
    m_seisLoadCaseCombo->addItem(GerenciadorTraducao::trText("Cargas Ativas do Modelo (Todas)"), 0);
    if (m_model) {
        for (const auto& lc : m_model->getLoadCases()) {
            m_seisLoadCaseCombo->addItem(
                QStringLiteral("%1 - %2").arg(lc.id).arg(QString::fromStdString(lc.name)),
                lc.id);
        }
    }
    if (prevData.isValid()) {
        const int idx = m_seisLoadCaseCombo->findData(prevData);
        if (idx >= 0) m_seisLoadCaseCombo->setCurrentIndex(idx);
    }
}

void AnalysisSuiteDialog::refreshContext() {
    m_hasCombos = false;
    m_combosResults.clear();
    m_hasModal = false;
    m_hasBuckling = false;
    m_hasHarmonic = false;
    m_hasSpec = false;
    m_hasSeis = false;
    m_hasNonlinear = false;
    refreshSeismicLoadCases();
    if (m_combosTable) m_combosTable->setRowCount(0);
    if (m_modalTable) m_modalTable->setRowCount(0);
    if (m_bucklingTable) m_bucklingTable->setRowCount(0);
    if (m_harmonicTable) m_harmonicTable->setRowCount(0);
    if (m_nlTable) m_nlTable->setRowCount(0);
    if (m_specResultLabel) m_specResultLabel->setText(QStringLiteral("-"));
    if (m_seisResultLabel) m_seisResultLabel->setText(QStringLiteral("-"));
    if (!m_busy && m_statusLabel) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Modelo atualizado. Execute as análises desejadas para o estado atual."));
    }
}

void AnalysisSuiteDialog::setBusyState(bool busy, const QString& statusText) {
    m_busy = busy;
    if (busy) {
        QWidget* fw = focusWidget();
        for (QPushButton* btn : m_actionButtons) {
            if (btn && btn == fw && m_tabs) {
                m_tabs->setFocus(Qt::OtherFocusReason);
                break;
            }
        }
    }
    for (QPushButton* btn : m_actionButtons) {
        if (btn) btn->setEnabled(!busy);
    }
    if (busy) {
        m_progressBar->setRange(0, 0);
    } else {
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(1);
    }
    if (!statusText.isEmpty()) {
        m_statusLabel->setText(statusText);
        if (m_onStatus) m_onStatus(statusText);
    }
}

void AnalysisSuiteDialog::onRunCombosAsync() {
    if (!m_model || m_busy) return;
    kernel::LinearSolverOptions opts;
    opts.type = static_cast<kernel::LinearSolverType>(m_linearSolverCombo->currentData().toInt());

    StructuralModel snapshot = *m_model;
    setBusyState(true, GerenciadorTraducao::trText("Resolvendo casos de carga e combinações em segundo plano..."));

    struct ComboPack {
        bool ok = false;
        std::string err;
        std::vector<kernel::CaseDisplacementResult> cases;
        std::vector<kernel::CaseDisplacementResult> combos;
        kernel::EnvelopeExtremes env;
        SolverResult baseStatic;
        StructuralModel solvedModel;
    };

    QPointer<AnalysisSuiteDialog> self(this);
    runAsyncJob<ComboPack>(
        this,
        [snapshot = std::move(snapshot), opts]() mutable -> ComboPack {
            ComboPack pack;
            pack.baseStatic = FemSolver3D::solveWithOptions(snapshot, opts);
            pack.ok = FemSolver3D::solveLoadCasesAndCombinations(
                snapshot, opts, pack.cases, pack.combos, pack.env, &pack.err);
            if (!pack.ok && pack.err.empty()) {
                pack.err = pack.baseStatic.errorMessage;
            }
            pack.solvedModel = std::move(snapshot);
            return pack;
        },
        [self](const ComboPack& pack) {
            if (!self) return;
            self->m_combosTable->setRowCount(0);
            self->m_combosResults.clear();
            auto appendRows = [&](const QString& kind, const std::vector<kernel::CaseDisplacementResult>& vec) {
                for (const auto& r : vec) {
                    const int row = self->m_combosTable->rowCount();
                    self->m_combosTable->insertRow(row);
                    self->m_combosTable->setItem(row, 0, makeItem(QString::number(r.caseOrComboId)));
                    self->m_combosTable->setItem(row, 1, makeItem(kind));
                    self->m_combosTable->setItem(row, 2, makeItem(QString::fromStdString(r.name), Qt::AlignLeft | Qt::AlignVCenter));
                    self->m_combosTable->setItem(row, 3, makeItem(QString::number(maxTranslationalDisp(pack.solvedModel, r.U_full) * 1000.0, 'f', 3)));
                    self->m_combosTable->setItem(row, 4, makeItem(QStringLiteral("OK")));
                    self->m_combosResults.push_back(r);
                }
            };
            appendRows(GerenciadorTraducao::trText("Caso"), pack.cases);
            appendRows(GerenciadorTraducao::trText("Combinação"), pack.combos);
            self->m_hasCombos = pack.ok && !self->m_combosResults.empty();
            if (!self->m_combosResults.empty()) {
                self->m_combosTable->selectRow(0);
            }

            if (pack.baseStatic.success && self->m_model) {
                copySolvedNodalElementResults(pack.solvedModel, *self->m_model);
                if (self->m_onCompleted) self->m_onCompleted(pack.baseStatic);
            }
            self->setBusyState(false, pack.ok
                ? GerenciadorTraducao::trText("Combinações e estática resolvidas (%1 casos, %2 combinações).")
                      .arg(pack.cases.size()).arg(pack.combos.size())
                : GerenciadorTraducao::trText("Falha na análise: %1").arg(QString::fromStdString(pack.err)));
        });
}

void AnalysisSuiteDialog::onPlotSelectedCombo() {
    if (!m_hasCombos || !m_model || !m_viewport || m_combosResults.empty()) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Execute 'Resolver Casos & Combinações' antes de plotar no Viewport 3D."));
        return;
    }
    int row = m_combosTable->currentRow();
    if (row < 0 || row >= static_cast<int>(m_combosResults.size())) row = 0;
    const auto& r = m_combosResults[static_cast<std::size_t>(row)];
    const QString label = QString::fromStdString(r.name);
    const auto field = FemSolver3D::extractNodalDisplacements(*m_model, r.U_full);
    m_viewport->setPostAnimationField(label, field.frameNodes, -1.0, false);
    m_viewport->setPostMeshAnimationField(field.meshNodes);
    m_viewport->setAnimationPlaying(false);
    m_statusLabel->setText(GerenciadorTraducao::trText("Plotado no Viewport 3D: %1 (|u|máx = %2 mm)")
                               .arg(label)
                               .arg(field.maxTranslationalNorm * 1000.0, 0, 'f', 3));
}

void AnalysisSuiteDialog::onRunModalAsync() {
    if (!m_model || m_busy) return;
    kernel::ModalOptions opts;
    opts.numModes = m_modalModesSpin->value();
    StructuralModel snapshot = *m_model;
    setBusyState(true, GerenciadorTraducao::trText("Calculando modos naturais de vibração..."));

    QPointer<AnalysisSuiteDialog> self(this);
    runAsyncJob<kernel::ModalResult>(
        this,
        [snapshot = std::move(snapshot), opts]() -> kernel::ModalResult {
            return FemSolver3D::solveModal(snapshot, opts);
        },
        [self](const kernel::ModalResult& res) {
            if (!self) return;
            self->m_modalRes = res;
            self->m_hasModal = res.success;
            self->m_modalTable->setRowCount(0);
            for (const auto& m : res.modes) {
                const int row = self->m_modalTable->rowCount();
                self->m_modalTable->insertRow(row);
                self->m_modalTable->setItem(row, 0, makeItem(QString::number(m.modeIndex)));
                self->m_modalTable->setItem(row, 1, makeItem(QString::number(m.frequencyHz, 'f', 3)));
                self->m_modalTable->setItem(row, 2, makeItem(QString::number(m.periodSec, 'f', 4)));
                self->m_modalTable->setItem(row, 3, makeItem(QString::number(m.omega, 'f', 2)));
                self->m_modalTable->setItem(row, 4, makeItem(
                    QStringLiteral("%1% / %2% / %3%")
                        .arg(m.effectiveMassRatio[0] * 100.0, 0, 'f', 1)
                        .arg(m.effectiveMassRatio[1] * 100.0, 0, 'f', 1)
                        .arg(m.effectiveMassRatio[2] * 100.0, 0, 'f', 1)));
            }
            if (!res.modes.empty()) {
                self->m_modalTable->selectRow(0);
            }
            self->setBusyState(false, res.success
                ? GerenciadorTraducao::trText("Análise modal concluída (%1 modos).").arg(res.modes.size())
                : GerenciadorTraducao::trText("Erro modal: %1").arg(QString::fromStdString(res.errorMessage)));
        });
}

void AnalysisSuiteDialog::onPlotSelectedMode() {
    if (!m_hasModal || !m_model || !m_viewport || m_modalRes.modes.empty()) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Execute a análise modal antes de animar um modo."));
        return;
    }
    int row = m_modalTable->currentRow();
    if (row < 0 || row >= static_cast<int>(m_modalRes.modes.size())) row = 0;
    const auto& mode = m_modalRes.modes[static_cast<std::size_t>(row)];
    const QString label = GerenciadorTraducao::trText("Modo %1 (f = %2 Hz)")
                              .arg(mode.modeIndex)
                              .arg(mode.frequencyHz, 0, 'f', 2);
    applyFieldToViewport(m_viewport, *m_model, mode.phi_full, label, -1.0, true);
    m_viewport->setAnimationPlaying(true);
    m_statusLabel->setText(GerenciadorTraducao::trText("Animando %1 no Viewport 3D.").arg(label));
}

void AnalysisSuiteDialog::onRunBucklingAsync() {
    if (!m_model || m_busy) return;
    kernel::BucklingOptions opts;
    opts.numModes = m_bucklingModesSpin->value();
    opts.includeNegativeFactors = m_bucklingNegCheck->isChecked();
    StructuralModel snapshot = *m_model;
    setBusyState(true, GerenciadorTraducao::trText("Calculando fatores críticos de flambagem linear..."));

    QPointer<AnalysisSuiteDialog> self(this);
    runAsyncJob<kernel::BucklingResult>(
        this,
        [snapshot = std::move(snapshot), opts]() mutable -> kernel::BucklingResult {
            return FemSolver3D::solveBuckling(snapshot, opts);
        },
        [self](const kernel::BucklingResult& res) {
            if (!self) return;
            self->m_bucklingRes = res;
            self->m_hasBuckling = res.success;
            self->m_bucklingTable->setRowCount(0);
            for (std::size_t i = 0; i < res.loadFactors.size(); ++i) {
                const int row = self->m_bucklingTable->rowCount();
                const double lf = res.loadFactors[i];
                self->m_bucklingTable->insertRow(row);
                self->m_bucklingTable->setItem(row, 0, makeItem(QString::number(i + 1)));
                self->m_bucklingTable->setItem(row, 1, makeItem(QString::number(lf, 'f', 4)));
                const bool crit = (lf > 0.0 && lf < 1.0);
                self->m_bucklingTable->setItem(row, 2, makeItem(
                    crit ? GerenciadorTraducao::trText("CRÍTICO (λ < 1)")
                         : GerenciadorTraducao::trText("Estável para carga unitária")));
            }
            if (!res.loadFactors.empty()) {
                self->m_bucklingTable->selectRow(0);
            }
            self->setBusyState(false, res.success
                ? GerenciadorTraducao::trText("Flambagem concluída (%1 modos).").arg(res.loadFactors.size())
                : GerenciadorTraducao::trText("Erro flambagem: %1").arg(QString::fromStdString(res.errorMessage)));
        });
}

void AnalysisSuiteDialog::onPlotSelectedBucklingMode() {
    if (!m_hasBuckling || !m_model || !m_viewport || m_bucklingRes.modeShapesFull.empty()) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Execute a análise de flambagem antes de animar um modo."));
        return;
    }
    int row = m_bucklingTable->currentRow();
    if (row < 0 || row >= static_cast<int>(m_bucklingRes.modeShapesFull.size())) row = 0;
    const double lf = (row < static_cast<int>(m_bucklingRes.loadFactors.size())) ? m_bucklingRes.loadFactors[static_cast<std::size_t>(row)] : 0.0;
    const QString label = GerenciadorTraducao::trText("Flambagem %1 (λ = %2)").arg(row + 1).arg(lf, 0, 'f', 3);
    applyFieldToViewport(m_viewport, *m_model, m_bucklingRes.modeShapesFull[static_cast<std::size_t>(row)], label, -1.0, false);
    m_viewport->setAnimationPlaying(true);
    m_statusLabel->setText(GerenciadorTraducao::trText("Animando %1 no Viewport 3D.").arg(label));
}

void AnalysisSuiteDialog::onApplyBucklingImperfection() {
    if (!m_hasBuckling || !m_model || m_bucklingRes.modeShapesFull.empty()) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Execute a análise de flambagem antes de aplicar uma imperfeição geométrica."));
        return;
    }
    int row = m_bucklingTable ? m_bucklingTable->currentRow() : 0;
    if (row < 0 || row >= static_cast<int>(m_bucklingRes.modeShapesFull.size())) row = 0;
    const double amp = m_impAmpSpin ? m_impAmpSpin->value() : 0.02;
    std::string errMsg;
    if (!FemSolver3D::applyBucklingImperfection(*m_model, m_bucklingRes, row, amp, &errMsg)) {
        const QString msg = GerenciadorTraducao::trText("Falha ao aplicar imperfeição: %1")
                                .arg(QString::fromStdString(errMsg));
        m_statusLabel->setText(msg);
        if (m_onStatus) m_onStatus(msg);
        return;
    }
    m_model->pushUndo();
    if (m_viewport) {
        m_viewport->clearPostAnimation();
        m_viewport->update();
    }
    if (m_onModelMutated) m_onModelMutated();
    const QString okMsg = GerenciadorTraducao::trText("Imperfeição geométrica (Modo %1, amplitude = %2 m) aplicada ao modelo.")
                              .arg(row + 1)
                              .arg(amp, 0, 'f', 4);
    m_statusLabel->setText(okMsg);
    if (m_onStatus) m_onStatus(okMsg);
}

void AnalysisSuiteDialog::onRunHarmonicAsync() {
    if (!m_model || m_busy) return;
    kernel::HarmonicOptions opts;
    opts.structuralLossEta = m_harmEtaSpin->value();
    const double f0 = m_harmFMinSpin->value();
    const double f1 = std::max(f0 + 0.01, m_harmFMaxSpin->value());
    const int nSteps = m_harmStepsSpin->value();
    for (int i = 0; i < nSteps; ++i) {
        const double t = (nSteps > 1) ? static_cast<double>(i) / (nSteps - 1) : 0.0;
        opts.frequenciesHz.push_back(f0 + t * (f1 - f0));
    }
    StructuralModel snapshot = *m_model;
    setBusyState(true, GerenciadorTraducao::trText("Executando varredura em frequência harmônica (FRF)..."));

    QPointer<AnalysisSuiteDialog> self(this);
    runAsyncJob<kernel::HarmonicResult>(
        this,
        [snapshot = std::move(snapshot), opts]() -> kernel::HarmonicResult {
            return FemSolver3D::solveHarmonic(snapshot, opts);
        },
        [self](const kernel::HarmonicResult& res) {
            if (!self || !self->m_model) return;
            self->m_harmonicRes = res;
            self->m_hasHarmonic = res.success;
            self->m_harmonicTable->setRowCount(0);
            int peakRow = 0;
            double peakDisp = -1.0;
            for (std::size_t i = 0; i < res.sweep.size(); ++i) {
                const auto& st = res.sweep[i];
                const double uMax = maxTranslationalDisp(*self->m_model, st.amplitudeFull) * 1000.0;
                if (uMax > peakDisp) {
                    peakDisp = uMax;
                    peakRow = static_cast<int>(i);
                }
                const int row = self->m_harmonicTable->rowCount();
                self->m_harmonicTable->insertRow(row);
                self->m_harmonicTable->setItem(row, 0, makeItem(QString::number(i + 1)));
                self->m_harmonicTable->setItem(row, 1, makeItem(QString::number(st.frequencyHz, 'f', 2)));
                self->m_harmonicTable->setItem(row, 2, makeItem(QString::number(st.omega, 'f', 2)));
                self->m_harmonicTable->setItem(row, 3, makeItem(QString::number(uMax, 'f', 4)));
            }
            if (!res.sweep.empty()) {
                self->m_harmonicTable->selectRow(peakRow);
            }
            self->setBusyState(false, res.success
                ? GerenciadorTraducao::trText("Harmônica concluída (pico em f = %1 Hz, %2 mm).")
                      .arg(res.sweep.empty() ? 0.0 : res.sweep[static_cast<std::size_t>(peakRow)].frequencyHz, 0, 'f', 2)
                      .arg(std::max(0.0, peakDisp), 0, 'f', 3)
                : GerenciadorTraducao::trText("Erro harmônica: %1").arg(QString::fromStdString(res.errorMessage)));
        });
}

void AnalysisSuiteDialog::onPlotSelectedHarmonicStep() {
    if (!m_hasHarmonic || !m_model || !m_viewport || m_harmonicRes.sweep.empty()) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Execute a varredura harmônica antes de animar uma frequência."));
        return;
    }
    int row = m_harmonicTable->currentRow();
    if (row < 0 || row >= static_cast<int>(m_harmonicRes.sweep.size())) row = 0;
    const auto& st = m_harmonicRes.sweep[static_cast<std::size_t>(row)];
    const QString label = GerenciadorTraducao::trText("FRF f = %1 Hz").arg(st.frequencyHz, 0, 'f', 2);
    applyFieldToViewport(m_viewport, *m_model, st.amplitudeFull, label, -1.0, true);
    m_viewport->setAnimationPlaying(true);
    m_statusLabel->setText(GerenciadorTraducao::trText("Animando %1 no Viewport 3D.").arg(label));
}

void AnalysisSuiteDialog::onRunSpectrumAsync() {
    if (!m_model || m_busy) return;
    kernel::ModalOptions mOpts;
    mOpts.numModes = m_modalModesSpin->value();
    kernel::ResponseSpectrumOptions sOpts;
    sOpts.combination = static_cast<kernel::ResponseSpectrumCombination>(m_specComboRule->currentData().toInt());
    sOpts.modalDampingZeta = m_specZetaSpin->value();
    sOpts.directionScale = {m_specDirX->value(), m_specDirY->value(), m_specDirZ->value()};
    sOpts.spectrumCurve = kernel::DynamicSolver::buildNbr15421SpectrumCurve(
        m_specAgSpin->value(), m_specCaSpin->value(), m_specCvSpin->value(), 4.0, 100);

    StructuralModel snapshot = *m_model;
    setBusyState(true, GerenciadorTraducao::trText("Calculando resposta sísmica espectral..."));

    QPointer<AnalysisSuiteDialog> self(this);
    runAsyncJob<kernel::ResponseSpectrumResult>(
        this,
        [snapshot = std::move(snapshot), sOpts, mOpts]() -> kernel::ResponseSpectrumResult {
            return FemSolver3D::solveResponseSpectrum(snapshot, sOpts, mOpts);
        },
        [self](const kernel::ResponseSpectrumResult& res) {
            if (!self || !self->m_model) return;
            self->m_specRes = res;
            self->m_hasSpec = res.success;
            if (res.success) {
                const double uPeakMm = maxTranslationalDisp(*self->m_model, res.combinedDisplacementFull) * 1000.0;
                self->m_specResultLabel->setText(
                    GerenciadorTraducao::trText("Deslocamento espectral máximo combinado: %1 mm").arg(uPeakMm, 0, 'f', 3));
            } else {
                self->m_specResultLabel->setText(
                    GerenciadorTraducao::trText("Erro espectro: %1").arg(QString::fromStdString(res.errorMessage)));
            }
            self->setBusyState(false, res.success
                ? GerenciadorTraducao::trText("Espectro de resposta concluído.")
                : GerenciadorTraducao::trText("Falha no espectro de resposta."));
        });
}

void AnalysisSuiteDialog::onPlotSpectrumResult() {
    if (!m_hasSpec || !m_model || !m_viewport) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Calcule a combinação espectral antes de plotar no Viewport 3D."));
        return;
    }
    applyFieldToViewport(
        m_viewport,
        *m_model,
        m_specRes.combinedDisplacementFull,
        GerenciadorTraducao::trText("Espectro CQC/SRSS"),
        -1.0,
        true);
    m_viewport->setAnimationPlaying(false);
    m_statusLabel->setText(GerenciadorTraducao::trText("Envoltória espectral plotada no Viewport 3D."));
}

void AnalysisSuiteDialog::onRunSeismicAsync() {
    if (!m_model || m_busy) return;
    kernel::TransientOptions opts;
    opts.scheme = static_cast<kernel::TransientScheme>(m_seisSchemeCombo->currentData().toInt());
    opts.dt = m_seisDtSpin->value();
    opts.numSteps = m_seisStepsSpin->value();
    const bool isForceHistory = (m_seisModeCombo && m_seisModeCombo->currentIndex() == 1);
    const kernel::LoadCaseId baseCaseId = m_seisLoadCaseCombo ? m_seisLoadCaseCombo->currentData().toInt() : 0;
    const double amp = m_seisAmpSpin->value();
    const double omega = 2.0 * std::numbers::pi * m_seisFreqSpin->value();
    const int dir = m_seisDirCombo->currentIndex();

    StructuralModel snapshot = *m_model;
    setBusyState(true, isForceHistory
        ? GerenciadorTraducao::trText("Integrando histórico temporal de forças F(t) (Newmark / HHT)...")
        : GerenciadorTraducao::trText("Integrando histórico temporal sísmico (Newmark / HHT)..."));

    QPointer<AnalysisSuiteDialog> self(this);
    runAsyncJob<kernel::TransientResult>(
        this,
        [snapshot = std::move(snapshot), opts, isForceHistory, baseCaseId, amp, omega, dir]() -> kernel::TransientResult {
            if (isForceHistory) {
                auto scaleFn = [amp, omega](double t, int /*step*/) -> double {
                    return amp * std::sin(omega * t);
                };
                return FemSolver3D::solveTransient(snapshot, scaleFn, baseCaseId, opts);
            }
            auto accelFn = [amp, omega, dir](double t, int /*step*/) -> kernel::Vec3 {
                const double env = std::exp(-0.4 * t);
                const double a = amp * env * std::sin(omega * t);
                kernel::Vec3 v = kernel::Vec3::Zero();
                v[std::clamp(dir, 0, 2)] = a;
                return v;
            };
            return FemSolver3D::solveSeismic(snapshot, accelFn, opts);
        },
        [self](const kernel::TransientResult& res) {
            if (!self || !self->m_model) return;
            self->m_seisRes = res;
            self->m_hasSeis = res.success;
            if (res.success) {
                const double peakMm = maxTranslationalDisp(*self->m_model, res.peakDisplacementFull) * 1000.0;
                self->m_seisResultLabel->setText(
                    GerenciadorTraducao::trText("Passos integrados: %1 | Pico de deslocamento: %2 mm")
                        .arg(res.history.size()).arg(peakMm, 0, 'f', 3));
            } else {
                self->m_seisResultLabel->setText(
                    GerenciadorTraducao::trText("Erro transiente: %1").arg(QString::fromStdString(res.errorMessage)));
            }
            self->setBusyState(false, res.success
                ? GerenciadorTraducao::trText("Integração temporal concluída.")
                : GerenciadorTraducao::trText("Falha na integração temporal."));
        });
}

void AnalysisSuiteDialog::onPlaySeismicHistory() {
    if (!m_hasSeis || !m_model || !m_viewport || m_seisRes.history.empty()) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Execute a integração sísmica antes de reproduzir o histórico."));
        return;
    }
    const std::size_t stride = std::max<std::size_t>(1, m_seisRes.history.size() / 60);
    const std::size_t frameCount = applyStepFramesToViewport(
        m_viewport,
        *m_model,
        m_seisRes.history,
        stride,
        GerenciadorTraducao::trText("Transiente Sísmico"),
        [](const auto& rec) { return rec.time; });
    m_viewport->setAnimationPlaying(true);
    m_statusLabel->setText(GerenciadorTraducao::trText("Reproduzindo histórico transiente (%1 frames) no Viewport 3D.").arg(frameCount));
}

void AnalysisSuiteDialog::onRunNonlinearAsync() {
    if (!m_model || m_busy) return;
    kernel::NonlinearOptions opts;
    opts.method = static_cast<kernel::NonlinearMethod>(m_nlMethodCombo->currentData().toInt());
    opts.kinematics = static_cast<kernel::NonlinearKinematics>(m_nlKinCombo->currentData().toInt());
    opts.numSteps = m_nlStepsSpin->value();
    opts.targetLoadFactor = m_nlFactorSpin->value();
    opts.fullNewtonTangent = m_nlFullNewtonCheck->isChecked();
    opts.enableLineSearch = m_nlLineSearchCheck->isChecked();

    StructuralModel snapshot = *m_model;
    setBusyState(true, GerenciadorTraducao::trText("Executando solver não linear incremental em segundo plano..."));

    struct NlPack {
        kernel::NonlinearResult nlRes;
        StructuralModel solvedModel;
    };

    QPointer<AnalysisSuiteDialog> self(this);
    runAsyncJob<NlPack>(
        this,
        [snapshot = std::move(snapshot), opts]() mutable -> NlPack {
            NlPack p;
            p.nlRes = FemSolver3D::solveNonlinear(snapshot, opts);
            p.solvedModel = std::move(snapshot);
            return p;
        },
        [self](const NlPack& p) {
            if (!self) return;
            self->m_nonlinearRes = p.nlRes;
            self->m_hasNonlinear = p.nlRes.success;
            self->m_nlTable->setRowCount(0);
            for (const auto& step : p.nlRes.history) {
                const int row = self->m_nlTable->rowCount();
                self->m_nlTable->insertRow(row);
                self->m_nlTable->setItem(row, 0, makeItem(QString::number(step.stepIndex)));
                self->m_nlTable->setItem(row, 1, makeItem(QString::number(step.loadFactor, 'f', 4)));
                self->m_nlTable->setItem(row, 2, makeItem(QString::number(step.iterations)));
                self->m_nlTable->setItem(row, 3, makeItem(QString::number(step.residualNorm, 'e', 2)));
                self->m_nlTable->setItem(row, 4, makeItem(step.anyYielded ? GerenciadorTraducao::trText("SIM") : GerenciadorTraducao::trText("Não")));
            }
            if (p.nlRes.success && self->m_model) {
                copySolvedNodalElementResults(p.solvedModel, *self->m_model);
                SolverResult sr;
                sr.success = true;
                sr.totalNodes = static_cast<int>(self->m_model->getNodes().size());
                sr.totalElements = static_cast<int>(self->m_model->getElements().size());
                sr.totalDofs = static_cast<int>(p.nlRes.finalU_full.size() > 0 ? p.nlRes.finalU_full.size() : p.solvedModel.getNodes().size() * 6);
                sr.equilibrium.isBalanced = true;
                sr.maxNodalDisplacement = maxTranslationalDisp(p.solvedModel, p.nlRes.finalU_full);
                for (const auto& el : p.solvedModel.getElements()) {
                    if (!el.hasResults) continue;
                    ElementExtremes ex;
                    ex.elementId = el.id;
                    for (int k = 0; k <= 10; ++k) {
                        const auto f = el.evaluateAt(static_cast<double>(k) / 10.0, p.solvedModel);
                        if (k == 0) {
                            ex.N_min = ex.N_max = f.N;
                            ex.Vy_min = ex.Vy_max = f.Vy;
                            ex.Vz_min = ex.Vz_max = f.Vz;
                            ex.Tx_min = ex.Tx_max = f.Tx;
                            ex.My_min = ex.My_max = f.My;
                            ex.Mz_min = ex.Mz_max = f.Mz;
                        } else {
                            ex.N_min = std::min(ex.N_min, f.N);
                            ex.N_max = std::max(ex.N_max, f.N);
                            ex.Vy_min = std::min(ex.Vy_min, f.Vy);
                            ex.Vy_max = std::max(ex.Vy_max, f.Vy);
                            ex.Vz_min = std::min(ex.Vz_min, f.Vz);
                            ex.Vz_max = std::max(ex.Vz_max, f.Vz);
                            ex.Tx_min = std::min(ex.Tx_min, f.Tx);
                            ex.Tx_max = std::max(ex.Tx_max, f.Tx);
                            ex.My_min = std::min(ex.My_min, f.My);
                            ex.My_max = std::max(ex.My_max, f.My);
                            ex.Mz_min = std::min(ex.Mz_min, f.Mz);
                            ex.Mz_max = std::max(ex.Mz_max, f.Mz);
                        }
                        ex.maxDeflection = std::max(ex.maxDeflection, f.defGlobal.norm());
                    }
                    sr.elementExtremes.push_back(ex);
                }
                if (self->m_onCompleted) self->m_onCompleted(sr);
            }
            self->setBusyState(false, p.nlRes.success
                ? GerenciadorTraducao::trText("Análise não linear concluída (λ = %1, %2 iterações).")
                      .arg(p.nlRes.achievedLoadFactor, 0, 'f', 3)
                      .arg(p.nlRes.totalIterations)
                : GerenciadorTraducao::trText("Erro não linear: %1").arg(QString::fromStdString(p.nlRes.errorMessage)));
        });
}

void AnalysisSuiteDialog::onPlayNonlinearHistory() {
    if (!m_hasNonlinear || !m_model || !m_viewport || m_nonlinearRes.history.empty()) {
        m_statusLabel->setText(GerenciadorTraducao::trText("Execute a análise não linear antes de animar os passos."));
        return;
    }
    const std::size_t frameCount = applyStepFramesToViewport(
        m_viewport,
        *m_model,
        m_nonlinearRes.history,
        1,
        GerenciadorTraducao::trText("Não Linear |u|"),
        [](const auto& step) { return step.loadFactor; });
    m_viewport->setAnimationPlaying(true);
    m_statusLabel->setText(GerenciadorTraducao::trText("Animando trajetória não linear (%1 passos) no Viewport 3D.").arg(frameCount));
}

// ==========================================
// LoadCombinationsDialog
// ==========================================
LoadCombinationsDialog::LoadCombinationsDialog(
    QWidget* parent,
    StructuralModel* model,
    std::function<void()> onModelUpdated)
    : QDialog(parent),
      m_model(model),
      m_onModelUpdated(std::move(onModelUpdated)) {
    setModal(false);
    setWindowModality(Qt::NonModal);
    resize(780, 560);

    auto* root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    // Tab 1: Casos de Carga
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* top = new QHBoxLayout();
        m_caseListCombo = new QComboBox(tab);
        m_btnCaseAdd = new QPushButton(tab);
        m_btnCaseDel = new QPushButton(tab);
        connect(m_caseListCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadCombinationsDialog::populateCaseFields);
        connect(m_btnCaseAdd, &QPushButton::clicked, this, &LoadCombinationsDialog::onAddLoadCase);
        connect(m_btnCaseDel, &QPushButton::clicked, this, &LoadCombinationsDialog::onDeleteLoadCase);
        top->addWidget(m_caseListCombo, 1);
        top->addWidget(m_btnCaseAdd);
        top->addWidget(m_btnCaseDel);
        lay->addLayout(top);

        auto* form = new QFormLayout();
        m_caseNameEdit = new QLineEdit(tab);
        m_caseCatCombo = new QComboBox(tab);
        m_caseCatCombo->addItem(QStringLiteral("Dead (G - Permanente)"), static_cast<int>(kernel::LoadCaseCategory::Dead));
        m_caseCatCombo->addItem(QStringLiteral("Live (Q - Sobrecarga)"), static_cast<int>(kernel::LoadCaseCategory::Live));
        m_caseCatCombo->addItem(QStringLiteral("Wind (W - Vento)"), static_cast<int>(kernel::LoadCaseCategory::Wind));
        m_caseCatCombo->addItem(QStringLiteral("Thermal (T - Temperatura)"), static_cast<int>(kernel::LoadCaseCategory::Thermal));
        m_caseCatCombo->addItem(QStringLiteral("Seismic (E - Sismo)"), static_cast<int>(kernel::LoadCaseCategory::Seismic));
        m_caseCatCombo->addItem(QStringLiteral("Custom (Outro)"), static_cast<int>(kernel::LoadCaseCategory::Custom));
        m_caseSelfWeightCheck = new QCheckBox(tab);
        m_caseSwFactorSpin = new QDoubleSpinBox(tab);
        m_caseSwFactorSpin->setRange(-5.0, 5.0);
        m_caseSwFactorSpin->setValue(1.0);
        addTrRow(form, "Nome do Caso:", m_caseNameEdit, tab);
        addTrRow(form, "Categoria:", m_caseCatCombo, tab);
        addTrRow(form, "Peso Próprio (-Z):", m_caseSelfWeightCheck, tab);
        addTrRow(form, "Fator Peso Próprio:", m_caseSwFactorSpin, tab);
        lay->addLayout(form);

        auto applyCaseMeta = [this]() {
            if (!m_model || m_caseListCombo->currentIndex() < 0) return;
            const auto cid = static_cast<kernel::LoadCaseId>(m_caseListCombo->currentData().toInt());
            if (auto* lc = m_model->findLoadCase(cid)) {
                lc->name = m_caseNameEdit->text().toStdString();
                lc->category = static_cast<kernel::LoadCaseCategory>(m_caseCatCombo->currentData().toInt());
                lc->selfWeight.enabled = m_caseSelfWeightCheck->isChecked();
                lc->selfWeight.factor = m_caseSwFactorSpin->value();
                m_caseListCombo->setItemText(m_caseListCombo->currentIndex(),
                                             QStringLiteral("#%1 %2").arg(lc->id).arg(QString::fromStdString(lc->name)));
                if (m_onModelUpdated) m_onModelUpdated();
            }
        };
        connect(m_caseNameEdit, &QLineEdit::editingFinished, this, applyCaseMeta);
        connect(m_caseCatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyCaseMeta);
        connect(m_caseSelfWeightCheck, &QCheckBox::toggled, this, applyCaseMeta);
        connect(m_caseSwFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyCaseMeta);

        auto* actRow = new QHBoxLayout();
        m_btnCaseCapture = new QPushButton(tab);
        m_btnCaseApplyModel = new QPushButton(tab);
        connect(m_btnCaseCapture, &QPushButton::clicked, this, &LoadCombinationsDialog::onCaptureModelLoadsToCase);
        connect(m_btnCaseApplyModel, &QPushButton::clicked, this, &LoadCombinationsDialog::onLoadCaseToModel);
        actRow->addWidget(m_btnCaseCapture);
        actRow->addWidget(m_btnCaseApplyModel);
        lay->addLayout(actRow);

        m_caseSummaryLabel = new QLabel(tab);
        m_caseSummaryLabel->setWordWrap(true);
        lay->addWidget(m_caseSummaryLabel);
        lay->addStretch();
        m_tabs->addTab(tab, QString());
    }

    // Tab 2: Combinações NBR
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* top = new QHBoxLayout();
        m_comboListCombo = new QComboBox(tab);
        m_btnComboAdd = new QPushButton(tab);
        m_btnComboDel = new QPushButton(tab);
        connect(m_comboListCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadCombinationsDialog::populateComboFactorsTable);
        connect(m_btnComboAdd, &QPushButton::clicked, this, &LoadCombinationsDialog::onAddCombination);
        connect(m_btnComboDel, &QPushButton::clicked, this, &LoadCombinationsDialog::onDeleteCombination);
        top->addWidget(m_comboListCombo, 1);
        top->addWidget(m_btnComboAdd);
        top->addWidget(m_btnComboDel);
        lay->addLayout(top);

        auto* meta = new QHBoxLayout();
        m_comboNameEdit = new QLineEdit(tab);
        m_comboLimitCombo = new QComboBox(tab);
        m_comboLimitCombo->addItem(QStringLiteral("ELU Normal"), static_cast<int>(kernel::LimitStateType::ELU_Normal));
        m_comboLimitCombo->addItem(QStringLiteral("ELU Especial"), static_cast<int>(kernel::LimitStateType::ELU_Special));
        m_comboLimitCombo->addItem(QStringLiteral("ELU Excepcional"), static_cast<int>(kernel::LimitStateType::ELU_Exceptional));
        m_comboLimitCombo->addItem(QStringLiteral("ELS Rara"), static_cast<int>(kernel::LimitStateType::ELS_Rare));
        m_comboLimitCombo->addItem(QStringLiteral("ELS Frequente"), static_cast<int>(kernel::LimitStateType::ELS_Frequent));
        m_comboLimitCombo->addItem(QStringLiteral("ELS Quase-Permanente"), static_cast<int>(kernel::LimitStateType::ELS_QuasiPermanent));
        m_comboPDeltaCheck = makeTrCheck("P-Delta (2ª Ordem)", tab);
        meta->addWidget(m_comboNameEdit, 1);
        meta->addWidget(m_comboLimitCombo);
        meta->addWidget(m_comboPDeltaCheck);
        lay->addLayout(meta);

        auto applyComboMeta = [this]() {
            if (!m_model || m_comboListCombo->currentIndex() < 0) return;
            const auto cid = static_cast<kernel::LoadComboId>(m_comboListCombo->currentData().toInt());
            if (auto* c = m_model->findLoadCombination(cid)) {
                c->name = m_comboNameEdit->text().toStdString();
                c->limitState = static_cast<kernel::LimitStateType>(m_comboLimitCombo->currentData().toInt());
                c->pDeltaEnabled = m_comboPDeltaCheck->isChecked();
                m_comboListCombo->setItemText(m_comboListCombo->currentIndex(),
                                              QStringLiteral("#%1 %2").arg(c->id).arg(QString::fromStdString(c->name)));
                if (m_onModelUpdated) m_onModelUpdated();
            }
        };
        connect(m_comboNameEdit, &QLineEdit::editingFinished, this, applyComboMeta);
        connect(m_comboLimitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyComboMeta);
        connect(m_comboPDeltaCheck, &QCheckBox::toggled, this, applyComboMeta);

        m_comboFactorsTable = new QTableWidget(tab);
        initReadOnlyTable(m_comboFactorsTable, {QStringLiteral("Caso ID"), QStringLiteral("Nome"), QStringLiteral("Coeficiente γ_f")});
        lay->addWidget(m_comboFactorsTable, 1);

        auto* facRow = new QHBoxLayout();
        m_factorCaseCombo = new QComboBox(tab);
        m_factorGammaSpin = new QDoubleSpinBox(tab);
        m_factorGammaSpin->setRange(-5.0, 5.0);
        m_factorGammaSpin->setValue(1.40);
        m_btnFactorAdd = new QPushButton(tab);
        m_btnFactorClear = new QPushButton(tab);
        connect(m_btnFactorAdd, &QPushButton::clicked, this, &LoadCombinationsDialog::onAddComboFactorRow);
        connect(m_btnFactorClear, &QPushButton::clicked, this, &LoadCombinationsDialog::onClearComboFactors);
        facRow->addWidget(m_factorCaseCombo, 1);
        facRow->addWidget(m_factorGammaSpin);
        facRow->addWidget(m_btnFactorAdd);
        facRow->addWidget(m_btnFactorClear);
        lay->addLayout(facRow);

        auto* nbrRow = new QHBoxLayout();
        m_autoMaterialCombo = new QComboBox(tab);
        m_autoMaterialCombo->addItems({QStringLiteral("Concreto Armado (ABNT NBR 6118:2023)"),
                                       QStringLiteral("Aço Estrutural (ABNT NBR 8800:2024)")});
        m_autoOccCombo = new QComboBox(tab);
        m_autoOccCombo->addItem(QStringLiteral("Residencial (ψ0=0,5 / ψ2=0,3)"), static_cast<int>(kernel::codes::OccupancyCategory::Residential));
        m_autoOccCombo->addItem(QStringLiteral("Comercial / Público (ψ0=0,7 / ψ2=0,4)"), static_cast<int>(kernel::codes::OccupancyCategory::CommercialOrPublic));
        m_autoOccCombo->addItem(QStringLiteral("Biblioteca / Garagem (ψ0=0,8 / ψ2=0,6)"), static_cast<int>(kernel::codes::OccupancyCategory::LibraryArchiveGarageRoof));
        m_btnAutoNbr = new QPushButton(tab);
        connect(m_btnAutoNbr, &QPushButton::clicked, this, &LoadCombinationsDialog::onGenerateNbrCombos);
        nbrRow->addWidget(m_autoMaterialCombo);
        nbrRow->addWidget(m_autoOccCombo);
        nbrRow->addWidget(m_btnAutoNbr);
        lay->addLayout(nbrRow);
        m_tabs->addTab(tab, QString());
    }

    // Tab 3: Assistentes NBR 6123 & NBR 6120
    {
        auto* tab = new QWidget(this);
        auto* lay = new QVBoxLayout(tab);
        auto* gWind = makeTrGroup("ABNT NBR 6123:2023 - Pressão Dinâmica de Vento (Sec. 4.2)", tab);
        auto* wf = new QFormLayout(gWind);
        const auto& md = model ? model->metadata() : ProjectMetadata{};
        m_v0Spin = new QDoubleSpinBox(gWind); m_v0Spin->setRange(10.0, 90.0); m_v0Spin->setValue(md.windV0_ms); m_v0Spin->setSuffix(QStringLiteral(" m/s"));
        m_s1Spin = new QDoubleSpinBox(gWind); m_s1Spin->setRange(0.5, 2.5); m_s1Spin->setDecimals(3); m_s1Spin->setValue(md.windS1);
        m_s2Spin = new QDoubleSpinBox(gWind); m_s2Spin->setRange(0.3, 2.5); m_s2Spin->setDecimals(3); m_s2Spin->setValue(md.windS2);
        m_s3Spin = new QDoubleSpinBox(gWind); m_s3Spin->setRange(0.5, 2.0); m_s3Spin->setDecimals(3); m_s3Spin->setValue(md.windS3);
        m_windAreaSpin = new QDoubleSpinBox(gWind); m_windAreaSpin->setRange(0.1, 1000.0); m_windAreaSpin->setValue(12.0); m_windAreaSpin->setSuffix(QStringLiteral(" m²"));
        m_windCpSpin = new QDoubleSpinBox(gWind); m_windCpSpin->setRange(-3.0, 3.0); m_windCpSpin->setValue(0.8);
        m_windDirCombo = new QComboBox(gWind);
        m_windDirCombo->addItems({QStringLiteral("+X"), QStringLiteral("-X"), QStringLiteral("+Y"), QStringLiteral("-Y")});

        addTrRow(wf, "V0 (Velocidade Básica):", m_v0Spin, gWind);
        addTrRow(wf, "S1 (Topográfico):", m_s1Spin, gWind);
        addTrRow(wf, "S2 (Rugosidade/Altura):", m_s2Spin, gWind);
        addTrRow(wf, "S3 (Estatístico):", m_s3Spin, gWind);
        addTrRow(wf, "Área Tributária / Cp:", [gWind, this]() {
            auto* w = new QWidget(gWind);
            auto* h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(m_windAreaSpin);
            h->addWidget(m_windCpSpin);
            h->addWidget(m_windDirCombo);
            return w;
        }(), gWind);
        connect(m_v0Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LoadCombinationsDialog::onComputeWind6123);
        connect(m_s1Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LoadCombinationsDialog::onComputeWind6123);
        connect(m_s2Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LoadCombinationsDialog::onComputeWind6123);
        connect(m_s3Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LoadCombinationsDialog::onComputeWind6123);
        connect(m_windAreaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LoadCombinationsDialog::onComputeWind6123);
        connect(m_windCpSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LoadCombinationsDialog::onComputeWind6123);
        connect(m_windDirCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadCombinationsDialog::onComputeWind6123);

        m_btnCalcWind = new QPushButton(gWind);
        m_btnApplyWind = new QPushButton(gWind);
        connect(m_btnCalcWind, &QPushButton::clicked, this, &LoadCombinationsDialog::onComputeWind6123);
        connect(m_btnApplyWind, &QPushButton::clicked, this, &LoadCombinationsDialog::onApplyWindToSelectedNodes);
        auto* wBtns = new QHBoxLayout();
        wBtns->addWidget(m_btnCalcWind);
        wBtns->addWidget(m_btnApplyWind);
        wf->addRow(wBtns);
        m_windResultLabel = new QLabel(gWind);
        m_windResultLabel->setWordWrap(true);
        wf->addRow(m_windResultLabel);
        lay->addWidget(gWind);

        auto* gPart = makeTrGroup("ABNT NBR 6120:2019 - Divisórias Sem Posição Definida (Tab. 11)", tab);
        auto* pf = new QFormLayout(gPart);
        m_partWeightSpin = new QDoubleSpinBox(gPart);
        m_partWeightSpin->setRange(0.0, 10.0);
        m_partWeightSpin->setValue(1.80);
        m_partWeightSpin->setSuffix(QStringLiteral(" kN/m"));
        connect(m_partWeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &LoadCombinationsDialog::onComputePartition6120);
        m_btnCalcPart = new QPushButton(gPart);
        connect(m_btnCalcPart, &QPushButton::clicked, this, &LoadCombinationsDialog::onComputePartition6120);
        addTrRow(pf, "Peso Linear da Divisória:", m_partWeightSpin, gPart);
        pf->addRow(m_btnCalcPart);
        m_partResultLabel = new QLabel(gPart);
        m_partResultLabel->setWordWrap(true);
        pf->addRow(m_partResultLabel);
        lay->addWidget(gPart);
        lay->addStretch();
        m_tabs->addTab(tab, QString());
    }

    root->addWidget(m_tabs, 1);
    auto* bottom = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_btnClose = new QPushButton(this);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(m_statusLabel, 1);
    bottom->addWidget(m_btnClose);
    root->addLayout(bottom);

    retranslateUi();
    refreshLists();
    onComputeWind6123();
    onComputePartition6120();
    setStatusFeedback(GerenciadorTraducao::trText("Pronto."));
}

void LoadCombinationsDialog::setStatusFeedback(const QString& msg) {
    if (m_statusLabel) {
        m_statusLabel->setText(msg);
    }
}

void LoadCombinationsDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void LoadCombinationsDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Casos de Carga, Combinações NBR & Assistentes 6120/6123"));
    retranslateTaggedWidgets(this);
    retranslateComboItems(m_caseCatCombo, {
        "Dead (G - Permanente)",
        "Live (Q - Sobrecarga)",
        "Wind (W - Vento)",
        "Thermal (T - Temperatura)",
        "Seismic (E - Sismo)",
        "Custom (Outro)"
    });
    retranslateComboItems(m_comboLimitCombo, {
        "ELU Normal",
        "ELU Especial",
        "ELU Excepcional",
        "ELS Rara",
        "ELS Frequente",
        "ELS Quase-Permanente"
    });
    retranslateComboItems(m_autoMaterialCombo, {
        "Concreto Armado (ABNT NBR 6118:2023)",
        "Aço Estrutural (ABNT NBR 8800:2024)"
    });
    retranslateComboItems(m_autoOccCombo, {
        "Residencial (ψ0=0,5 / ψ2=0,3)",
        "Comercial / Público (ψ0=0,7 / ψ2=0,4)",
        "Biblioteca / Garagem (ψ0=0,8 / ψ2=0,6)"
    });
    setTrHeaders(m_comboFactorsTable, {"Caso ID", "Nome", "Coeficiente γ_f"});
    m_tabs->setTabText(0, GerenciadorTraducao::trText("Casos de Carga"));
    m_tabs->setTabText(1, GerenciadorTraducao::trText("Combinações NBR"));
    m_tabs->setTabText(2, GerenciadorTraducao::trText("Vento (6123) & Cargas (6120)"));
    m_btnCaseAdd->setText(GerenciadorTraducao::trText("+ Novo Caso"));
    m_btnCaseDel->setText(GerenciadorTraducao::trText("Excluir"));
    m_btnCaseCapture->setText(GerenciadorTraducao::trText("Capturar Cargas do Viewport -> Caso"));
    m_btnCaseApplyModel->setText(GerenciadorTraducao::trText("Aplicar Cargas do Caso -> Viewport"));
    m_btnComboAdd->setText(GerenciadorTraducao::trText("+ Nova Combinação"));
    m_btnComboDel->setText(GerenciadorTraducao::trText("Excluir"));
    m_btnFactorAdd->setText(GerenciadorTraducao::trText("Adicionar/Atualizar Fator"));
    m_btnFactorClear->setText(GerenciadorTraducao::trText("Limpar Fatores"));
    m_btnAutoNbr->setText(GerenciadorTraducao::trText("Gerar Combinações NBR"));
    m_btnCalcWind->setText(GerenciadorTraducao::trText("Calcular Vk e Pressão q (NBR 6123)"));
    m_btnApplyWind->setText(GerenciadorTraducao::trText("Aplicar Força nos Nós Selecionados"));
    m_btnCalcPart->setText(GerenciadorTraducao::trText("Calcular Sobrecarga Equivalente (Tab. 11)"));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
}

void LoadCombinationsDialog::refreshLists() {
    if (!m_model || m_refreshingLists) return;
    m_refreshingLists = true;

    const int prevCaseId = (m_caseListCombo->currentIndex() >= 0) ? m_caseListCombo->currentData().toInt() : -1;
    const int prevComboId = (m_comboListCombo->currentIndex() >= 0) ? m_comboListCombo->currentData().toInt() : -1;

    {
        QSignalBlocker b1(m_caseListCombo);
        QSignalBlocker b2(m_factorCaseCombo);
        m_caseListCombo->clear();
        m_factorCaseCombo->clear();
        for (const auto& lc : m_model->getLoadCases()) {
            const QString lbl = QStringLiteral("#%1 %2").arg(lc.id).arg(QString::fromStdString(lc.name));
            m_caseListCombo->addItem(lbl, lc.id);
            m_factorCaseCombo->addItem(lbl, lc.id);
        }
        if (prevCaseId >= 0) {
            const int foundIdx = m_caseListCombo->findData(prevCaseId);
            if (foundIdx >= 0) m_caseListCombo->setCurrentIndex(foundIdx);
        }
    }
    {
        QSignalBlocker b(m_comboListCombo);
        m_comboListCombo->clear();
        for (const auto& comb : m_model->getLoadCombinations()) {
            m_comboListCombo->addItem(
                QStringLiteral("#%1 %2").arg(comb.id).arg(QString::fromStdString(comb.name)),
                comb.id);
        }
        if (prevComboId >= 0) {
            const int foundIdx = m_comboListCombo->findData(prevComboId);
            if (foundIdx >= 0) m_comboListCombo->setCurrentIndex(foundIdx);
        }
    }
    if (m_model->getLoadCombinations().empty() && m_autoMaterialCombo && !m_model->getElements().empty()) {
        int steelCount = 0;
        int totalElements = 0;
        for (const auto& el : m_model->getElements()) {
            const Material* mat = m_model->findMaterial(el.materialId);
            if (!mat) continue;
            ++totalElements;
            const QString mName = QString::fromStdString(mat->name);
            if (mName.contains(QStringLiteral("aço"), Qt::CaseInsensitive) ||
                mName.contains(QStringLiteral("steel"), Qt::CaseInsensitive) ||
                mName.contains(QStringLiteral("ASTM"), Qt::CaseInsensitive) ||
                mName.contains(QStringLiteral("A36"), Qt::CaseInsensitive) ||
                mName.contains(QStringLiteral("A572"), Qt::CaseInsensitive)) {
                ++steelCount;
            }
        }
        if (totalElements > 0) {
            QSignalBlocker bMat(m_autoMaterialCombo);
            m_autoMaterialCombo->setCurrentIndex((steelCount * 2 > totalElements) ? 1 : 0);
        }
    }
    m_refreshingLists = false;
    populateCaseFields();
    populateComboFactorsTable();
}

void LoadCombinationsDialog::populateCaseFields() {
    const bool hasCase = (m_model && m_caseListCombo->currentIndex() >= 0);
    m_caseNameEdit->setEnabled(hasCase);
    m_caseCatCombo->setEnabled(hasCase);
    m_caseSelfWeightCheck->setEnabled(hasCase);
    m_caseSwFactorSpin->setEnabled(hasCase);
    m_btnCaseDel->setEnabled(hasCase);
    m_btnCaseCapture->setEnabled(hasCase);
    m_btnCaseApplyModel->setEnabled(hasCase);

    if (!hasCase) {
        QSignalBlocker b1(m_caseNameEdit), b2(m_caseCatCombo), b3(m_caseSelfWeightCheck), b4(m_caseSwFactorSpin);
        m_caseNameEdit->clear();
        m_caseSelfWeightCheck->setChecked(false);
        m_caseSwFactorSpin->setValue(1.0);
        m_caseSummaryLabel->setText(GerenciadorTraducao::trText("Nenhum caso de carga definido."));
        return;
    }
    const auto cid = static_cast<kernel::LoadCaseId>(m_caseListCombo->currentData().toInt());
    const auto* lc = m_model->findLoadCase(cid);
    if (!lc) return;
    QSignalBlocker b1(m_caseNameEdit), b2(m_caseCatCombo), b3(m_caseSelfWeightCheck), b4(m_caseSwFactorSpin);
    m_caseNameEdit->setText(QString::fromStdString(lc->name));
    const int catIdx = m_caseCatCombo->findData(static_cast<int>(lc->category));
    if (catIdx >= 0) m_caseCatCombo->setCurrentIndex(catIdx);
    m_caseSelfWeightCheck->setChecked(lc->selfWeight.enabled);
    m_caseSwFactorSpin->setValue(lc->selfWeight.factor);
    m_caseSummaryLabel->setText(
        GerenciadorTraducao::trText("Cargas armazenadas no caso: %1 nodais, %2 em barras.")
            .arg(lc->nodalLoads.size()).arg(lc->elementLoads.size()));
}

void LoadCombinationsDialog::populateComboFactorsTable() {
    m_comboFactorsTable->setRowCount(0);
    const bool hasCombo = (m_model && m_comboListCombo->currentIndex() >= 0);
    const bool hasFactorCases = hasCombo && (m_factorCaseCombo->count() > 0);
    m_comboNameEdit->setEnabled(hasCombo);
    m_comboLimitCombo->setEnabled(hasCombo);
    m_comboPDeltaCheck->setEnabled(hasCombo);
    m_btnComboDel->setEnabled(hasCombo);
    m_factorCaseCombo->setEnabled(hasFactorCases);
    m_factorGammaSpin->setEnabled(hasFactorCases);
    m_btnFactorAdd->setEnabled(hasFactorCases);
    m_btnFactorClear->setEnabled(hasCombo);

    if (!hasCombo) {
        QSignalBlocker b1(m_comboNameEdit), b2(m_comboLimitCombo), b3(m_comboPDeltaCheck);
        m_comboNameEdit->clear();
        m_comboPDeltaCheck->setChecked(false);
        return;
    }
    const auto cid = static_cast<kernel::LoadComboId>(m_comboListCombo->currentData().toInt());
    const auto* comb = m_model->findLoadCombination(cid);
    if (!comb) return;
    QSignalBlocker b1(m_comboNameEdit), b2(m_comboLimitCombo), b3(m_comboPDeltaCheck);
    m_comboNameEdit->setText(QString::fromStdString(comb->name));
    const int limIdx = m_comboLimitCombo->findData(static_cast<int>(comb->limitState));
    if (limIdx >= 0) m_comboLimitCombo->setCurrentIndex(limIdx);
    m_comboPDeltaCheck->setChecked(comb->pDeltaEnabled);

    for (const auto& f : comb->factors) {
        const int row = m_comboFactorsTable->rowCount();
        m_comboFactorsTable->insertRow(row);
        const auto* lc = m_model->findLoadCase(f.loadCaseId);
        m_comboFactorsTable->setItem(row, 0, makeItem(QString::number(f.loadCaseId)));
        m_comboFactorsTable->setItem(row, 1, makeItem(lc ? QString::fromStdString(lc->name) : QStringLiteral("?"), Qt::AlignLeft | Qt::AlignVCenter));
        m_comboFactorsTable->setItem(row, 2, makeItem(QString::number(f.gamma, 'f', 2)));
    }
}

void LoadCombinationsDialog::onAddLoadCase() {
    if (!m_model) return;
    kernel::LoadCase lc;
    lc.id = m_model->getNextLoadCaseId();
    lc.name = QStringLiteral("Caso %1").arg(lc.id).toStdString();
    lc.category = kernel::LoadCaseCategory::Live;
    m_model->addLoadCase(lc);
    refreshLists();
    m_caseListCombo->setCurrentIndex(m_caseListCombo->count() - 1);
    setStatusFeedback(GerenciadorTraducao::trText("Caso de carga #%1 criado.").arg(lc.id));
    m_model->pushUndo();
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onDeleteLoadCase() {
    if (!m_model || m_caseListCombo->currentIndex() < 0) return;
    const auto cid = static_cast<kernel::LoadCaseId>(m_caseListCombo->currentData().toInt());
    auto& vec = m_model->getLoadCases();
    vec.erase(std::remove_if(vec.begin(), vec.end(), [cid](const auto& c) { return c.id == cid; }), vec.end());
    for (auto& comb : m_model->getLoadCombinations()) {
        comb.factors.erase(
            std::remove_if(comb.factors.begin(), comb.factors.end(), [cid](const auto& f) { return f.loadCaseId == cid; }),
            comb.factors.end());
    }
    refreshLists();
    setStatusFeedback(GerenciadorTraducao::trText("Caso de carga #%1 excluído.").arg(cid));
    m_model->pushUndo();
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onCaptureModelLoadsToCase() {
    if (!m_model || m_caseListCombo->currentIndex() < 0) return;
    const auto cid = static_cast<kernel::LoadCaseId>(m_caseListCombo->currentData().toInt());
    auto* lc = m_model->findLoadCase(cid);
    if (!lc) return;
    lc->nodalLoads.clear();
    lc->elementLoads.clear();
    for (const auto& n : m_model->getNodes()) {
        if (n.load.isActive()) {
            kernel::NodalLoadPatternEntry e;
            e.nodeId = n.id;
            e.forceMoment = {n.load.force.x, n.load.force.y, n.load.force.z,
                             n.load.moment.x, n.load.moment.y, n.load.moment.z};
            lc->nodalLoads.push_back(e);
        }
    }
    for (const auto& el : m_model->getElements()) {
        if (el.distLoad.active || el.thermalLoad.active) {
            kernel::ElementLoadPatternEntry e;
            e.elementId = el.id;
            e.load.hasDistributed = el.distLoad.active;
            e.load.coordSystem = (el.distLoad.coordSystem == CoordSystem::Local) ? 1 : 0;
            e.load.distributedForce = {el.distLoad.w.x, el.distLoad.w.y, el.distLoad.w.z};
            e.load.hasThermal = el.thermalLoad.active;
            e.load.deltaT = el.thermalLoad.deltaT;
            lc->elementLoads.push_back(e);
        }
    }
    populateCaseFields();
    m_model->pushUndo();
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onLoadCaseToModel() {
    if (!m_model || m_caseListCombo->currentIndex() < 0) return;
    const auto cid = static_cast<kernel::LoadCaseId>(m_caseListCombo->currentData().toInt());
    const auto* lc = m_model->findLoadCase(cid);
    if (!lc) return;
    for (auto& n : m_model->getNodes()) n.load = NodalLoad{};
    for (auto& el : m_model->getElements()) {
        el.distLoad = DistributedLoad{};
        el.thermalLoad = ThermalLoad{};
    }
    for (const auto& nl : lc->nodalLoads) {
        if (Node* n = m_model->findNode(nl.nodeId)) {
            n->load.force = {nl.forceMoment[0], nl.forceMoment[1], nl.forceMoment[2]};
            n->load.moment = {nl.forceMoment[3], nl.forceMoment[4], nl.forceMoment[5]};
        }
    }
    for (const auto& elp : lc->elementLoads) {
        if (Element* el = m_model->findElement(elp.elementId)) {
            el->distLoad.active = elp.load.hasDistributed;
            el->distLoad.coordSystem = (elp.load.coordSystem == 1) ? CoordSystem::Local : CoordSystem::Global;
            el->distLoad.w = {elp.load.distributedForce.x(), elp.load.distributedForce.y(), elp.load.distributedForce.z()};
            el->thermalLoad.active = elp.load.hasThermal;
            el->thermalLoad.deltaT = elp.load.deltaT;
        }
    }
    m_model->pushUndo();
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onAddCombination() {
    if (!m_model) return;
    kernel::LoadCombination comb;
    comb.id = m_model->getNextLoadCombinationId();
    comb.name = QStringLiteral("Comb-%1").arg(comb.id).toStdString();
    m_model->addLoadCombination(comb);
    refreshLists();
    m_comboListCombo->setCurrentIndex(m_comboListCombo->count() - 1);
    m_model->pushUndo();
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onDeleteCombination() {
    if (!m_model || m_comboListCombo->currentIndex() < 0) return;
    const auto cid = static_cast<kernel::LoadComboId>(m_comboListCombo->currentData().toInt());
    auto& vec = m_model->getLoadCombinations();
    vec.erase(std::remove_if(vec.begin(), vec.end(), [cid](const auto& c) { return c.id == cid; }), vec.end());
    refreshLists();
    m_model->pushUndo();
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onAddComboFactorRow() {
    if (!m_model || m_comboListCombo->currentIndex() < 0 || m_factorCaseCombo->currentIndex() < 0) return;
    const auto combId = static_cast<kernel::LoadComboId>(m_comboListCombo->currentData().toInt());
    const auto caseId = static_cast<kernel::LoadCaseId>(m_factorCaseCombo->currentData().toInt());
    auto* comb = m_model->findLoadCombination(combId);
    if (!comb) return;
    for (auto& f : comb->factors) {
        if (f.loadCaseId == caseId) {
            f.gamma = m_factorGammaSpin->value();
            populateComboFactorsTable();
            m_model->pushUndo();
            if (m_onModelUpdated) m_onModelUpdated();
            return;
        }
    }
    comb->factors.push_back({caseId, m_factorGammaSpin->value()});
    populateComboFactorsTable();
    m_model->pushUndo();
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onClearComboFactors() {
    if (!m_model || m_comboListCombo->currentIndex() < 0) return;
    const auto combId = static_cast<kernel::LoadComboId>(m_comboListCombo->currentData().toInt());
    if (auto* comb = m_model->findLoadCombination(combId)) {
        comb->factors.clear();
        populateComboFactorsTable();
        m_model->pushUndo();
        if (m_onModelUpdated) m_onModelUpdated();
    }
}

void LoadCombinationsDialog::onGenerateNbrCombos() {
    if (!m_model) return;
    m_model->syncAutoCapturedViewportLoadCase();
    bool autoCapturedCase = false;
    if (m_model->getLoadCases().empty()) {
        kernel::LoadCase baseCase;
        baseCase.id = m_model->getNextLoadCaseId();
        baseCase.category = kernel::LoadCaseCategory::Dead;
        for (const auto& n : m_model->getNodes()) {
            if (n.load.isActive()) {
                kernel::NodalLoadPatternEntry e;
                e.nodeId = n.id;
                e.forceMoment = {n.load.force.x, n.load.force.y, n.load.force.z,
                                 n.load.moment.x, n.load.moment.y, n.load.moment.z};
                baseCase.nodalLoads.push_back(e);
            }
        }
        for (const auto& el : m_model->getElements()) {
            if (el.distLoad.active || el.thermalLoad.active) {
                kernel::ElementLoadPatternEntry e;
                e.elementId = el.id;
                e.load.hasDistributed = el.distLoad.active;
                e.load.coordSystem = (el.distLoad.coordSystem == CoordSystem::Local) ? 1 : 0;
                e.load.distributedForce = {el.distLoad.w.x, el.distLoad.w.y, el.distLoad.w.z};
                e.load.hasThermal = el.thermalLoad.active;
                e.load.deltaT = el.thermalLoad.deltaT;
                baseCase.elementLoads.push_back(e);
            }
        }
        if (baseCase.nodalLoads.empty() && baseCase.elementLoads.empty()) {
            baseCase.name = "G1 - Peso Próprio (NBR 6118)";
            baseCase.selfWeight.enabled = true;
            baseCase.selfWeight.factor = 1.0;
        } else {
            baseCase.name = "G1 - Cargas do Modelo (Viewport)";
        }
        m_model->addLoadCase(baseCase);
        autoCapturedCase = true;
    }

    auto& existingCombos = m_model->getLoadCombinations();
    existingCombos.erase(
        std::remove_if(existingCombos.begin(), existingCombos.end(), [](const auto& c) {
            return c.name.find("ELU-1 NBR") != std::string::npos ||
                   c.name.find("ELU-2 NBR") != std::string::npos ||
                   c.name == "ELS Quase-Permanente";
        }),
        existingCombos.end());

    const bool isSteel = (m_autoMaterialCombo->currentIndex() == 1);
    const auto occ = static_cast<kernel::codes::OccupancyCategory>(m_autoOccCombo->currentData().toInt());
    const auto cf = isSteel
        ? kernel::codes::NbrCodeChecks::steelFactors(kernel::codes::CombinationSituation::Normal)
        : kernel::codes::NbrCodeChecks::concreteFactors(kernel::codes::CombinationSituation::Normal);
    const auto qPsi = kernel::codes::NbrCodeChecks::occupancyPsi(occ);
    const auto wPsi = kernel::codes::NbrCodeChecks::windPsi();

    auto buildElu = [&](const QString& name, bool windPrimary, bool pDelta) {
        kernel::LoadCombination comb;
        comb.id = m_model->getNextLoadCombinationId();
        comb.name = name.toStdString();
        comb.limitState = kernel::LimitStateType::ELU_Normal;
        comb.pDeltaEnabled = pDelta;
        for (const auto& lc : m_model->getLoadCases()) {
            double g = 1.0;
            switch (lc.category) {
                case kernel::LoadCaseCategory::Dead: g = cf.gamma_g_unfav; break;
                case kernel::LoadCaseCategory::Live: g = windPrimary ? (cf.gamma_q * qPsi.psi0) : cf.gamma_q; break;
                case kernel::LoadCaseCategory::Wind: g = windPrimary ? cf.gamma_w : (cf.gamma_w * wPsi.psi0); break;
                case kernel::LoadCaseCategory::Thermal: g = cf.gamma_t * 0.6; break;
                default: g = cf.gamma_q; break;
            }
            comb.factors.push_back({lc.id, g});
        }
        m_model->addLoadCombination(comb);
    };

    buildElu(isSteel ? QStringLiteral("ELU-1 NBR8800 (Q Principal)") : QStringLiteral("ELU-1 NBR6118 (Q Principal)"), false, false);
    buildElu(isSteel ? QStringLiteral("ELU-2 NBR8800 (W Vento Principal + P-Delta)") : QStringLiteral("ELU-2 NBR6118 (W Vento Principal + P-Delta)"), true, true);

    kernel::LoadCombination els;
    els.id = m_model->getNextLoadCombinationId();
    els.name = "ELS Quase-Permanente";
    els.limitState = kernel::LimitStateType::ELS_QuasiPermanent;
    for (const auto& lc : m_model->getLoadCases()) {
        double g = 1.0;
        if (lc.category == kernel::LoadCaseCategory::Live) g = qPsi.psi2;
        else if (lc.category == kernel::LoadCaseCategory::Wind) g = wPsi.psi2;
        else if (lc.category == kernel::LoadCaseCategory::Thermal) g = 0.3;
        els.factors.push_back({lc.id, g});
    }
    m_model->addLoadCombination(els);

    refreshLists();
    if (m_comboListCombo->count() > 0) {
        m_comboListCombo->setCurrentIndex(m_comboListCombo->count() - 1);
    }
    setStatusFeedback(autoCapturedCase
        ? GerenciadorTraducao::trText("Cargas do viewport capturadas em Caso G1 e 3 combinações NBR geradas (ELU-1, ELU-2 P-Delta, ELS).")
        : GerenciadorTraducao::trText("3 combinações NBR geradas automaticamente (ELU-1, ELU-2 P-Delta, ELS Quase-Permanente)."));
    m_model->pushUndo();
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onComputeWind6123() {
    kernel::codes::WindInput6123 in;
    in.v0_ms = m_v0Spin->value();
    in.s1 = m_s1Spin->value();
    in.s2 = m_s2Spin->value();
    in.s3 = m_s3Spin->value();
    const auto r = kernel::codes::NbrCodeChecks::evaluateWind6123(in);
    const double fWind = r.q_kPa * m_windAreaSpin->value() * m_windCpSpin->value();
    if (m_model) {
        auto& md = m_model->metadata();
        md.windV0_ms = in.v0_ms;
        md.windS1 = in.s1;
        md.windS2 = in.s2;
        md.windS3 = in.s3;
    }
    m_windResultLabel->setText(
        GerenciadorTraducao::trText("Vk = %1 m/s  |  q = %2 Pa (%3 kN/m²)  |  F_eq = %4 kN")
            .arg(r.vk_ms, 0, 'f', 2)
            .arg(r.q_Pa, 0, 'f', 1)
            .arg(r.q_kPa, 0, 'f', 3)
            .arg(fWind, 0, 'f', 2));
}

void LoadCombinationsDialog::onApplyWindToSelectedNodes() {
    if (!m_model || m_model->getSelectedNodes().empty()) {
        setStatusFeedback(GerenciadorTraducao::trText("Selecione ao menos 1 nó no Viewport 3D para aplicar a força equivalente de vento."));
        return;
    }
    kernel::codes::WindInput6123 in{m_v0Spin->value(), m_s1Spin->value(), m_s2Spin->value(), m_s3Spin->value()};
    const auto r = kernel::codes::NbrCodeChecks::evaluateWind6123(in);
    const double fMag = r.q_kPa * m_windAreaSpin->value() * m_windCpSpin->value();
    const int dir = m_windDirCombo->currentIndex();
    Vec3 fv{0.0, 0.0, 0.0};
    if (dir == 0) fv.x = fMag;
    else if (dir == 1) fv.x = -fMag;
    else if (dir == 2) fv.y = fMag;
    else fv.y = -fMag;

    const auto selNodes = m_model->getSelectedNodes();
    for (NodeId nid : selNodes) {
        if (Node* n = m_model->findNode(nid)) {
            n->load.force = n->load.force + fv;
        }
    }
    m_model->pushUndo();
    setStatusFeedback(GerenciadorTraducao::trText("Força de vento (%1 kN) aplicada em %2 nó(s) selecionado(s).")
                          .arg(fMag, 0, 'f', 2)
                          .arg(selNodes.size()));
    if (m_onModelUpdated) m_onModelUpdated();
}

void LoadCombinationsDialog::onComputePartition6120() {
    const double w = m_partWeightSpin->value();
    const double eq = kernel::codes::NbrCodeChecks::unpositionedPartitionSurcharge6120_kPa(w);
    if (eq < 0.0) {
        m_partResultLabel->setText(GerenciadorTraducao::trText(
            "Divisória > 3,0 kN/m: ABNT NBR 6120 Tab. 11 NÃO permite distribuição uniforme; aplique como carga linear localizada."));
    } else {
        m_partResultLabel->setText(
            GerenciadorTraducao::trText("Sobrecarga equivalente uniforme (ABNT NBR 6120 Tab. 11): %1 kN/m²").arg(eq, 0, 'f', 2));
    }
}

// ==========================================
// MeshGeneratorDialog
// ==========================================
MeshGeneratorDialog::MeshGeneratorDialog(
    QWidget* parent,
    StructuralModel* model,
    Viewport3D* viewport,
    std::function<void()> onModelUpdated)
    : QDialog(parent),
      m_model(model),
      m_viewport(viewport),
      m_onModelUpdated(std::move(onModelUpdated)) {
    setModal(false);
    setWindowModality(Qt::NonModal);
    setMinimumWidth(450);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_meshTypeCombo = new QComboBox(this);
    m_meshTypeCombo->addItems({
        QStringLiteral("1D Linha Refinada (Line2)"),
        QStringLiteral("2D Laje/Casca Quad4 Estruturada"),
        QStringLiteral("2D Laje/Casca Quad8 Quadrática"),
        QStringLiteral("2D Tri3 Estruturada"),
        QStringLiteral("2D Polígono Delaunay (Tri3)"),
        QStringLiteral("3D Bloco Hex8 Estruturado"),
        QStringLiteral("3D Bloco Hex20 Quadrático"),
        QStringLiteral("3D Bloco Tet4 Estruturado"),
        QStringLiteral("Extrusão 2D -> 3D Hex8")
    });
    m_meshTypeCombo->setCurrentIndex(1);

    m_origXSpin = new QDoubleSpinBox(this); m_origXSpin->setRange(-500.0, 500.0); m_origXSpin->setSuffix(" m");
    m_origYSpin = new QDoubleSpinBox(this); m_origYSpin->setRange(-500.0, 500.0); m_origYSpin->setSuffix(" m");
    m_origZSpin = new QDoubleSpinBox(this); m_origZSpin->setRange(-500.0, 500.0); m_origZSpin->setValue(3.0); m_origZSpin->setSuffix(" m");
    m_lxSpin = new QDoubleSpinBox(this); m_lxSpin->setRange(0.1, 200.0); m_lxSpin->setValue(6.0); m_lxSpin->setSuffix(" m");
    m_lySpin = new QDoubleSpinBox(this); m_lySpin->setRange(0.1, 200.0); m_lySpin->setValue(4.0); m_lySpin->setSuffix(" m");
    m_lzSpin = new QDoubleSpinBox(this); m_lzSpin->setRange(0.05, 50.0); m_lzSpin->setValue(0.25); m_lzSpin->setSuffix(" m");
    m_nxSpin = new QSpinBox(this); m_nxSpin->setRange(1, 80); m_nxSpin->setValue(8);
    m_nySpin = new QSpinBox(this); m_nySpin->setRange(1, 80); m_nySpin->setValue(6);
    m_nzSpin = new QSpinBox(this); m_nzSpin->setRange(1, 40); m_nzSpin->setValue(2);
    m_delaunayEdgeSpin = new QDoubleSpinBox(this); m_delaunayEdgeSpin->setRange(0.05, 20.0); m_delaunayEdgeSpin->setValue(0.60); m_delaunayEdgeSpin->setSuffix(" m");

    m_matCombo = new QComboBox(this);
    refreshMaterialCombo();
    m_thicknessSpin = new QDoubleSpinBox(this);
    m_thicknessSpin->setRange(0.005, 10.0);
    m_thicknessSpin->setDecimals(3);
    m_thicknessSpin->setSingleStep(0.02);
    m_thicknessSpin->setValue(0.20);
    m_thicknessSpin->setSuffix(" m");
    m_bcPresetCombo = new QComboBox(this);
    m_bcPresetCombo->addItems({
        QStringLiteral("Sem apoios (Livre)"),
        QStringLiteral("Bordas externas engastadas"),
        QStringLiteral("Bordas externas apoiadas (uX,uY,uZ=0)"),
        QStringLiteral("Base inferior (Zmin) engastada"),
        QStringLiteral("Face esquerda (Xmin) engastada")
    });
    m_bcPresetCombo->setCurrentIndex(1);
    m_pressureSpin = new QDoubleSpinBox(this);
    m_pressureSpin->setRange(-10000.0, 10000.0);
    m_pressureSpin->setDecimals(2);
    m_pressureSpin->setSingleStep(1.0);
    m_pressureSpin->setValue(-5.0);
    m_pressureSpin->setSuffix(" kPa");

    addTrRow(form, "Tipo de Malha:", m_meshTypeCombo, this);
    addTrRow(form, "Material da Malha:", m_matCombo, this);
    addTrRow(form, "Espessura (Casca 2D):", m_thicknessSpin, this);
    addTrRow(form, "Condição de Contorno:", m_bcPresetCombo, this);
    addTrRow(form, "Pressão / Carga Z (2D/3D):", m_pressureSpin, this);
    addTrRow(form, "Origem (X / Y / Z):", [this]() {
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_origXSpin); h->addWidget(m_origYSpin); h->addWidget(m_origZSpin);
        return w;
    }(), this);
    addTrRow(form, "Dimensões (Lx / Ly / Lz):", [this]() {
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_lxSpin); h->addWidget(m_lySpin); h->addWidget(m_lzSpin);
        return w;
    }(), this);
    addTrRow(form, "Subdivisões (nx / ny / nz):", [this]() {
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_nxSpin); h->addWidget(m_nySpin); h->addWidget(m_nzSpin);
        return w;
    }(), this);
    addTrRow(form, "Tamanho Alvo (Delaunay):", m_delaunayEdgeSpin, this);
    root->addLayout(form);

    m_qualityLabel = new QLabel(this);
    m_qualityLabel->setWordWrap(true);
    root->addWidget(m_qualityLabel);

    auto* btns = new QHBoxLayout();
    m_btnGenerate = new QPushButton(this);
    m_btnFitBounds = new QPushButton(this);
    m_btnClear = new QPushButton(this);
    m_btnClose = new QPushButton(this);
    connect(m_btnGenerate, &QPushButton::clicked, this, &MeshGeneratorDialog::onGenerateMesh);
    connect(m_btnFitBounds, &QPushButton::clicked, this, &MeshGeneratorDialog::onFitToModelBounds);
    connect(m_btnClear, &QPushButton::clicked, this, &MeshGeneratorDialog::onClearMesh);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_meshTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshGeneratorDialog::updateParameterEnablement);
    btns->addWidget(m_btnGenerate);
    btns->addWidget(m_btnFitBounds);
    btns->addWidget(m_btnClear);
    btns->addStretch();
    btns->addWidget(m_btnClose);
    root->addLayout(btns);

    retranslateUi();
    updateParameterEnablement();
    updateQualityReport();
}

void MeshGeneratorDialog::refreshMaterialCombo() {
    if (!m_matCombo) return;
    const QVariant prev = m_matCombo->currentData();
    m_matCombo->clear();
    if (m_model) {
        for (const auto& mat : m_model->getMaterials()) {
            m_matCombo->addItem(
                QStringLiteral("%1 - %2").arg(mat.id).arg(QString::fromStdString(mat.name)),
                mat.id);
        }
    }
    if (prev.isValid()) {
        const int idx = m_matCombo->findData(prev);
        if (idx >= 0) m_matCombo->setCurrentIndex(idx);
    }
}

void MeshGeneratorDialog::refreshFromModel() {
    refreshMaterialCombo();
    updateParameterEnablement();
    updateQualityReport();
}

void MeshGeneratorDialog::onFitToModelBounds() {
    if (m_model && !m_model->getNodes().empty()) {
        Vec3 minPt;
        Vec3 maxPt;
        m_model->getBoundingBox(minPt, maxPt);
        const double dx = maxPt.x - minPt.x;
        const double dy = maxPt.y - minPt.y;
        const double dz = maxPt.z - minPt.z;
        m_origXSpin->setValue(minPt.x);
        m_origYSpin->setValue(minPt.y);
        m_origZSpin->setValue((m_meshTypeCombo && m_meshTypeCombo->currentIndex() <= 4) ? maxPt.z : minPt.z);
        m_lxSpin->setValue(std::max(1.0, dx));
        m_lySpin->setValue(std::max(1.0, dy > 1e-3 ? dy : dz));
        m_lzSpin->setValue(std::max(0.25, dz));
    } else {
        m_origXSpin->setValue(0.0);
        m_origYSpin->setValue(0.0);
        m_origZSpin->setValue(3.0);
        m_lxSpin->setValue(6.0);
        m_lySpin->setValue(4.0);
        m_lzSpin->setValue(0.25);
    }
    if (m_qualityLabel) {
        m_qualityLabel->setText(GerenciadorTraducao::trText("Dimensões ajustadas ao contorno do modelo estrutural."));
    }
}

void MeshGeneratorDialog::updateParameterEnablement() {
    const int idx = m_meshTypeCombo ? m_meshTypeCombo->currentIndex() : 1;
    const bool is1D = (idx == 0);
    const bool is2D = (idx >= 1 && idx <= 4);
    const bool isDelaunay = (idx == 4);
    const bool is3D = (idx >= 5 && idx <= 7);
    const bool isExtrude = (idx == 8);
    const bool needsBaseForExtrude = isExtrude && (!m_model || m_model->surfaceMesh().elements.empty());

    m_lxSpin->setEnabled(is1D || is2D || is3D || needsBaseForExtrude);
    m_lySpin->setEnabled(is2D || is3D || needsBaseForExtrude);
    m_lzSpin->setEnabled(is3D || isExtrude);

    m_nxSpin->setEnabled(is1D || (is2D && !isDelaunay) || is3D || needsBaseForExtrude);
    m_nySpin->setEnabled((is2D && !isDelaunay) || is3D || needsBaseForExtrude);
    m_nzSpin->setEnabled(is3D || isExtrude);
    m_delaunayEdgeSpin->setEnabled(isDelaunay);
    if (m_thicknessSpin) {
        m_thicknessSpin->setEnabled(is2D);
    }
}

void MeshGeneratorDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void MeshGeneratorDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Gerador de Malha 1D/2D/3D & Qualidade"));
    retranslateTaggedWidgets(this);
    retranslateComboItems(m_meshTypeCombo, {
        "1D Linha Refinada (Line2)",
        "2D Laje/Casca Quad4 Estruturada",
        "2D Laje/Casca Quad8 Quadrática",
        "2D Tri3 Estruturada",
        "2D Polígono Delaunay (Tri3)",
        "3D Bloco Hex8 Estruturado",
        "3D Bloco Hex20 Quadrático",
        "3D Bloco Tet4 Estruturado",
        "Extrusão 2D -> 3D Hex8"
    });
    retranslateComboItems(m_bcPresetCombo, {
        "Sem apoios (Livre)",
        "Bordas externas engastadas",
        "Bordas externas apoiadas (uX,uY,uZ=0)",
        "Base inferior (Zmin) engastada",
        "Face esquerda (Xmin) engastada"
    });
    m_btnGenerate->setText(GerenciadorTraducao::trText("Gerar e Exibir no Viewport 3D"));
    m_btnFitBounds->setText(GerenciadorTraducao::trText("Ajustar ao Contorno do Modelo"));
    m_btnClear->setText(GerenciadorTraducao::trText("Limpar Malha"));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
    updateQualityReport();
}

void MeshGeneratorDialog::onGenerateMesh() {
    if (!m_model) return;
    const kernel::Vec3 orig(m_origXSpin->value(), m_origYSpin->value(), m_origZSpin->value());
    const double lx = m_lxSpin->value();
    const double ly = m_lySpin->value();
    const double lz = m_lzSpin->value();
    const int nx = m_nxSpin->value();
    const int ny = m_nySpin->value();
    const int nz = m_nzSpin->value();

    kernel::MeshContainer mesh;
    switch (m_meshTypeCombo->currentIndex()) {
        case 0:
            mesh = kernel::MeshGenerator::generateLineMesh(orig, orig + kernel::Vec3(lx, 0.0, 0.0), nx);
            break;
        case 1:
            mesh = kernel::MeshGenerator::generateStructuredQuadGrid(orig, kernel::Vec3(lx, 0.0, 0.0), kernel::Vec3(0.0, ly, 0.0), nx, ny, false);
            break;
        case 2:
            mesh = kernel::MeshGenerator::generateStructuredQuadGrid(orig, kernel::Vec3(lx, 0.0, 0.0), kernel::Vec3(0.0, ly, 0.0), nx, ny, true);
            break;
        case 3:
            mesh = kernel::MeshGenerator::generateStructuredTriGrid(orig, kernel::Vec3(lx, 0.0, 0.0), kernel::Vec3(0.0, ly, 0.0), nx, ny, true);
            break;
        case 4: {
            const std::vector<Eigen::Vector2d> poly = {
                {0.0, 0.0}, {lx, 0.0}, {lx, ly}, {0.0, ly}
            };
            mesh = kernel::MeshGenerator::triangulatePolygonDelaunay(
                poly, m_delaunayEdgeSpin->value(), 4, orig, kernel::Vec3::UnitX(), kernel::Vec3::UnitY());
            break;
        }
        case 5:
            mesh = kernel::MeshGenerator::generateStructuredHexGrid(orig, kernel::Vec3(lx, ly, lz), nx, ny, nz, false);
            break;
        case 6:
            mesh = kernel::MeshGenerator::generateStructuredHexGrid(orig, kernel::Vec3(lx, ly, lz), nx, ny, nz, true);
            break;
        case 7:
            mesh = kernel::MeshGenerator::generateStructuredTetGrid(orig, kernel::Vec3(lx, ly, lz), nx, ny, nz);
            break;
        case 8: {
            kernel::MeshContainer base = m_model->surfaceMesh();
            if (base.elements.empty()) {
                base = kernel::MeshGenerator::generateStructuredQuadGrid(orig, kernel::Vec3(lx, 0.0, 0.0), kernel::Vec3(0.0, ly, 0.0), nx, ny, false);
            }
            mesh = kernel::MeshGenerator::extrudeSurfaceTo3D(base, kernel::Vec3(0.0, 0.0, lz), nz, false);
            break;
        }
        default: break;
    }

    const int matId = (m_matCombo && m_matCombo->currentData().isValid())
        ? m_matCombo->currentData().toInt()
        : 1;
    const double thickness = m_thicknessSpin ? m_thicknessSpin->value() : 0.20;
    const double pressure = m_pressureSpin ? m_pressureSpin->value() : 0.0;
    const int bcPreset = m_bcPresetCombo ? m_bcPresetCombo->currentIndex() : 0;

    if (!mesh.nodes.empty() && bcPreset > 0) {
        double minX = mesh.nodes.front().position.x();
        double minZ = mesh.nodes.front().position.z();
        for (const auto& mn : mesh.nodes) {
            minX = std::min(minX, mn.position.x());
            minZ = std::min(minZ, mn.position.z());
        }
        for (auto& mn : mesh.nodes) {
            if (bcPreset == 1 && mn.isBoundary) {
                mn.fixed = {true, true, true, true, true, true};
            } else if (bcPreset == 2 && mn.isBoundary) {
                mn.fixed = {true, true, true, false, false, false};
            } else if (bcPreset == 3 && std::abs(mn.position.z() - minZ) <= 1e-6) {
                mn.fixed = {true, true, true, true, true, true};
            } else if (bcPreset == 4 && std::abs(mn.position.x() - minX) <= 1e-6) {
                mn.fixed = {true, true, true, true, true, true};
            }
        }
    }

    for (auto& el : mesh.elements) {
        el.materialId = matId;
        if (el.isShell()) {
            el.thickness = thickness;
            el.normalPressure = pressure;
        } else {
            el.distributedLoad = kernel::Vec3(0.0, 0.0, pressure);
        }
    }

    m_model->surfaceMesh() = std::move(mesh);
    m_model->pushUndo();
    if (m_viewport) {
        m_viewport->setShowSurfaceMesh(true);
        m_viewport->fitAll();
    }
    updateParameterEnablement();
    updateQualityReport();
    if (m_onModelUpdated) m_onModelUpdated();
}

void MeshGeneratorDialog::onClearMesh() {
    if (!m_model) return;
    m_model->surfaceMesh() = kernel::MeshContainer{};
    m_model->pushUndo();
    if (m_viewport) m_viewport->update();
    updateParameterEnablement();
    updateQualityReport();
    if (m_onModelUpdated) m_onModelUpdated();
}

void MeshGeneratorDialog::updateQualityReport() {
    if (!m_model || m_model->surfaceMesh().elements.empty()) {
        m_qualityLabel->setText(GerenciadorTraducao::trText("Nenhuma malha 1D/2D/3D ativa no modelo."));
        return;
    }
    const auto q = kernel::MeshGenerator::evaluateQuality(m_model->surfaceMesh());
    m_qualityLabel->setText(
        GerenciadorTraducao::trText("Nós: %1 | Elementos: %2 (Válidos: %3, Degenerados: %4) | J_min: %5 | Aspect: [%6 .. %7] | Skewness máx: %8")
            .arg(m_model->surfaceMesh().nodes.size())
            .arg(q.totalElements)
            .arg(q.validElements)
            .arg(q.invertedOrDegenerateCount)
            .arg(q.minJacobian, 0, 'f', 4)
            .arg(q.minAspectRatio, 0, 'f', 2)
            .arg(q.maxAspectRatio, 0, 'f', 2)
            .arg(q.maxSkewness, 0, 'f', 3));
}

// ==========================================
// PostProcessControlDialog
// ==========================================
PostProcessControlDialog::PostProcessControlDialog(
    QWidget* parent,
    Viewport3D* viewport,
    std::function<bool()> onEnsureDcrMap)
    : QDialog(parent),
      m_viewport(viewport),
      m_onEnsureDcrMap(std::move(onEnsureDcrMap)) {
    setModal(false);
    setWindowModality(Qt::NonModal);
    setMinimumWidth(420);

    auto* root = new QVBoxLayout(this);

    auto* contourGrp = makeTrGroup("Contornos e Malha 3D", this);
    auto* cf = new QFormLayout(contourGrp);
    m_contourCombo = new QComboBox(contourGrp);
    m_contourCombo->setObjectName(QStringLiteral("postContourCombo"));
    m_contourCombo->addItem(QStringLiteral("Padrão (Cores por Seleção/Tema)"), static_cast<int>(Viewport3D::PostContourMode::None));
    m_contourCombo->addItem(QStringLiteral("Mapa Vetorial de Deslocamentos |u|"), static_cast<int>(Viewport3D::PostContourMode::Displacement));
    m_contourCombo->addItem(QStringLiteral("Mapa de Calor DCR (ABNT NBR)"), static_cast<int>(Viewport3D::PostContourMode::NbrComplianceDcr));
    m_contourCombo->addItem(QStringLiteral("Tensão de von Mises (Casca/Sólido)"), static_cast<int>(Viewport3D::PostContourMode::VonMisesStress));
    connect(m_contourCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PostProcessControlDialog::onContourChanged);

    m_showMeshCheck = makeTrCheck("Exibir Malha 2D/3D (Superfícies/Sólidos)", contourGrp);
    connect(m_showMeshCheck, &QCheckBox::toggled, this, &PostProcessControlDialog::onMeshVisibilityToggled);
    addTrRow(cf, "Contorno:", m_contourCombo, contourGrp);
    cf->addRow(m_showMeshCheck);
    root->addWidget(contourGrp);

    auto* clipGrp = makeTrGroup("Plano de Corte Interativo (OpenGL Clip Plane)", this);
    auto* clf = new QFormLayout(clipGrp);
    m_clipAxisCombo = new QComboBox(clipGrp);
    m_clipAxisCombo->setObjectName(QStringLiteral("clipAxisCombo"));
    m_clipAxisCombo->addItem(QStringLiteral("Desligado"), static_cast<int>(Viewport3D::ClipAxis::None));
    m_clipAxisCombo->addItem(QStringLiteral("Plano X = cota"), static_cast<int>(Viewport3D::ClipAxis::X));
    m_clipAxisCombo->addItem(QStringLiteral("Plano Y = cota"), static_cast<int>(Viewport3D::ClipAxis::Y));
    m_clipAxisCombo->addItem(QStringLiteral("Plano Z = cota"), static_cast<int>(Viewport3D::ClipAxis::Z));
    m_clipOffsetSpin = new QDoubleSpinBox(clipGrp);
    m_clipOffsetSpin->setRange(-500.0, 500.0);
    m_clipOffsetSpin->setSingleStep(0.25);
    m_clipOffsetSpin->setSuffix(QStringLiteral(" m"));
    m_clipInvertCheck = makeTrCheck("Inverter Lado do Corte", clipGrp);
    connect(m_clipAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PostProcessControlDialog::onClipChanged);
    connect(m_clipOffsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PostProcessControlDialog::onClipChanged);
    connect(m_clipInvertCheck, &QCheckBox::toggled, this, &PostProcessControlDialog::onClipChanged);
    addTrRow(clf, "Eixo:", m_clipAxisCombo, clipGrp);
    addTrRow(clf, "Posição:", m_clipOffsetSpin, clipGrp);
    clf->addRow(m_clipInvertCheck);
    root->addWidget(clipGrp);

    auto* animGrp = makeTrGroup("Animação (Modal / Flambagem / Não Linear)", this);
    auto* af = new QVBoxLayout(animGrp);
    m_animInfoLabel = new QLabel(animGrp);
    af->addWidget(m_animInfoLabel);

    m_stepSlider = new QSlider(Qt::Horizontal, animGrp);
    m_stepSlider->setRange(0, 0);
    connect(m_stepSlider, &QSlider::valueChanged, this, &PostProcessControlDialog::onStepSliderMoved);
    af->addWidget(m_stepSlider);

    auto* ctrlRow = new QHBoxLayout();
    m_btnPlayPause = new QPushButton(animGrp);
    m_btnPlayPause->setCheckable(true);
    m_btnClearAnim = new QPushButton(animGrp);
    m_speedSpin = new QDoubleSpinBox(animGrp); m_speedSpin->setRange(0.2, 5.0); m_speedSpin->setValue(1.0); m_speedSpin->setSuffix(QStringLiteral("x"));
    m_ampSpin = new QDoubleSpinBox(animGrp); m_ampSpin->setRange(0.1, 10.0); m_ampSpin->setValue(1.0); m_ampSpin->setSuffix(QStringLiteral("x"));
    connect(m_btnPlayPause, &QPushButton::toggled, this, &PostProcessControlDialog::onPlayPauseToggled);
    connect(m_btnClearAnim, &QPushButton::clicked, this, &PostProcessControlDialog::onClearAnimation);
    connect(m_speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PostProcessControlDialog::onSpeedChanged);
    connect(m_ampSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PostProcessControlDialog::onAmpChanged);
    ctrlRow->addWidget(m_btnPlayPause);
    ctrlRow->addWidget(makeTrLabel("Vel:", animGrp));
    ctrlRow->addWidget(m_speedSpin);
    ctrlRow->addWidget(makeTrLabel("Amp:", animGrp));
    ctrlRow->addWidget(m_ampSpin);
    ctrlRow->addWidget(m_btnClearAnim);
    af->addLayout(ctrlRow);
    root->addWidget(animGrp);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    m_btnClose = new QPushButton(this);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);
    root->addWidget(m_btnClose, 0, Qt::AlignRight);

    if (m_viewport) {
        connect(m_viewport, &Viewport3D::animationFrameChanged, this, [this](int idx, int total, double fac) {
            QSignalBlocker b(m_stepSlider);
            m_stepSlider->setRange(0, std::max(0, total - 1));
            m_stepSlider->setValue(idx);
            m_animInfoLabel->setText(
                GerenciadorTraducao::trText("Frame %1 / %2 (Fator/Fase = %3)").arg(idx + 1).arg(std::max(1, total)).arg(fac, 0, 'f', 3));
        });
        connect(m_viewport, &Viewport3D::postAnimationStateChanged, this, &PostProcessControlDialog::syncFromViewport);
    }

    retranslateUi();
    syncFromViewport();
}

void PostProcessControlDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void PostProcessControlDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Controles de Pós-Processamento 3D"));
    retranslateTaggedWidgets(this);
    retranslateComboItems(m_contourCombo, {
        "Padrão (Cores por Seleção/Tema)",
        "Mapa Vetorial de Deslocamentos |u|",
        "Mapa de Calor DCR (ABNT NBR)",
        "Tensão de von Mises (Casca/Sólido)"
    });
    retranslateComboItems(m_clipAxisCombo, {
        "Desligado",
        "Plano X = cota",
        "Plano Y = cota",
        "Plano Z = cota"
    });
    m_btnPlayPause->setText(m_btnPlayPause->isChecked() ? GerenciadorTraducao::trText("Pausar") : GerenciadorTraducao::trText("Play / Pausar"));
    m_btnClearAnim->setText(GerenciadorTraducao::trText("Encerrar Animação"));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
}

void PostProcessControlDialog::syncFromViewport() {
    if (!m_viewport) return;
    {
        QSignalBlocker b1(m_contourCombo), b2(m_showMeshCheck), b3(m_clipAxisCombo), b4(m_clipOffsetSpin), b5(m_clipInvertCheck);
        m_contourCombo->setCurrentIndex(m_contourCombo->findData(static_cast<int>(m_viewport->postContourMode())));
        m_showMeshCheck->setChecked(m_viewport->showSurfaceMesh());
        m_clipAxisCombo->setCurrentIndex(m_clipAxisCombo->findData(static_cast<int>(m_viewport->clipAxis())));
        m_clipOffsetSpin->setValue(m_viewport->clipOffset());
        m_clipInvertCheck->setChecked(m_viewport->clipInverted());
    }
    {
        QSignalBlocker b1(m_btnPlayPause), b2(m_stepSlider), b3(m_speedSpin), b4(m_ampSpin);
        const bool hasAnim = m_viewport->hasPostAnimation();
        const int stepCount = m_viewport->nonlinearStepCount();
        m_btnPlayPause->setChecked(m_viewport->isAnimationPlaying());
        m_btnPlayPause->setText(m_viewport->isAnimationPlaying()
            ? GerenciadorTraducao::trText("Pausar")
            : GerenciadorTraducao::trText("Play / Pausar"));
        m_btnPlayPause->setEnabled(hasAnim);
        m_btnClearAnim->setEnabled(hasAnim);
        m_stepSlider->setRange(0, std::max(0, stepCount - 1));
        m_stepSlider->setValue(m_viewport->nonlinearStepIndex());
        m_stepSlider->setEnabled(hasAnim && stepCount > 1);
        m_speedSpin->setValue(m_viewport->animationSpeed());
        m_speedSpin->setEnabled(hasAnim);
        m_ampSpin->setValue(m_viewport->animationAmplitude());
        m_ampSpin->setEnabled(hasAnim);
        m_animInfoLabel->setText(hasAnim
            ? GerenciadorTraducao::trText("Campo animado ativo no Viewport 3D.")
            : GerenciadorTraducao::trText("Nenhuma animação modal/não linear carregada."));
    }
}

void PostProcessControlDialog::onContourChanged(int idx) {
    if (!m_viewport) return;
    const auto mode = static_cast<Viewport3D::PostContourMode>(m_contourCombo->itemData(idx).toInt());
    if (mode == Viewport3D::PostContourMode::NbrComplianceDcr) {
        const bool ok = m_onEnsureDcrMap ? m_onEnsureDcrMap() : true;
        m_viewport->setPostContourMode(mode);
        if (m_statusLabel) {
            m_statusLabel->setText(ok
                ? GerenciadorTraducao::trText("Mapa de Calor DCR (ABNT NBR) aplicado ao Viewport 3D.")
                : GerenciadorTraducao::trText("Aviso: execute a Análise Estática (F5) ou adicione barras antes de visualizar o Mapa DCR."));
        }
        return;
    }
    m_viewport->setPostContourMode(mode);
    if (m_statusLabel) {
        if (mode == Viewport3D::PostContourMode::VonMisesStress) {
            m_statusLabel->setText(GerenciadorTraducao::trText("Mapa de Tensão de von Mises (Casca/Sólido) aplicado ao Viewport 3D."));
        } else {
            m_statusLabel->clear();
        }
    }
}

void PostProcessControlDialog::onMeshVisibilityToggled(bool checked) {
    if (m_viewport) m_viewport->setShowSurfaceMesh(checked);
}

void PostProcessControlDialog::onClipChanged() {
    if (!m_viewport) return;
    m_viewport->setClipPlane(
        static_cast<Viewport3D::ClipAxis>(m_clipAxisCombo->currentData().toInt()),
        m_clipOffsetSpin->value(),
        m_clipInvertCheck->isChecked());
}

void PostProcessControlDialog::onPlayPauseToggled(bool checked) {
    if (m_viewport) m_viewport->setAnimationPlaying(checked);
    m_btnPlayPause->setText(checked ? GerenciadorTraducao::trText("Pausar") : GerenciadorTraducao::trText("Play / Pausar"));
}

void PostProcessControlDialog::onStepSliderMoved(int val) {
    if (m_viewport) m_viewport->setNonlinearStepIndex(val);
}

void PostProcessControlDialog::onSpeedChanged(double val) {
    if (m_viewport) m_viewport->setAnimationSpeed(val);
}

void PostProcessControlDialog::onAmpChanged(double val) {
    if (m_viewport) m_viewport->setAnimationAmplitude(val);
}

void PostProcessControlDialog::onClearAnimation() {
    if (m_viewport) m_viewport->clearPostAnimation();
    syncFromViewport();
}

// ==========================================
// NbrComplianceDialog
// ==========================================
namespace {
std::unordered_map<ElementId, double> buildElementDcrMapFromAudit(const io::ModelNbrAudit& audit) {
    std::unordered_map<ElementId, double> dcrMap;
    for (const auto& s : audit.steelMembers) dcrMap[s.elementId] = s.result.dcrMax;
    for (const auto& b : audit.concreteBeams) dcrMap[b.elementId] = b.result.dcrMax;
    for (const auto& c : audit.concreteColumns) dcrMap[c.elementId] = c.result.dcrMax;
    return dcrMap;
}
} // namespace

NbrComplianceDialog::NbrComplianceDialog(
    QWidget* parent,
    StructuralModel* model,
    Viewport3D* viewport,
    const SolverResult* lastSolverResult,
    std::function<void()> onRequestPdfExport,
    std::function<bool()> onRequestStaticSolve)
    : QDialog(parent),
      m_model(model),
      m_viewport(viewport),
      m_lastSolver(lastSolverResult),
      m_onPdfExport(std::move(onRequestPdfExport)),
      m_onRequestStaticSolve(std::move(onRequestStaticSolve)) {
    setModal(false);
    setWindowModality(Qt::NonModal);
    resize(920, 640);

    auto* root = new QVBoxLayout(this);

    auto* paramRow = new QHBoxLayout();
    const auto& md = model ? model->metadata() : ProjectMetadata{};
    m_caaCombo = new QComboBox(this);
    m_caaCombo->addItem(QStringLiteral("CAA I"), static_cast<int>(kernel::codes::EnvironmentalAggressivenessClass::CAA_I));
    m_caaCombo->addItem(QStringLiteral("CAA II"), static_cast<int>(kernel::codes::EnvironmentalAggressivenessClass::CAA_II));
    m_caaCombo->addItem(QStringLiteral("CAA III"), static_cast<int>(kernel::codes::EnvironmentalAggressivenessClass::CAA_III));
    m_caaCombo->addItem(QStringLiteral("CAA IV"), static_cast<int>(kernel::codes::EnvironmentalAggressivenessClass::CAA_IV));
    m_caaCombo->setCurrentIndex(1);

    m_rebarCombo = new QComboBox(this);
    m_rebarCombo->addItem(QStringLiteral("CA-25"), static_cast<int>(kernel::codes::RebarGrade7480::CA25));
    m_rebarCombo->addItem(QStringLiteral("CA-50"), static_cast<int>(kernel::codes::RebarGrade7480::CA50));
    m_rebarCombo->addItem(QStringLiteral("CA-60"), static_cast<int>(kernel::codes::RebarGrade7480::CA60));
    m_rebarCombo->setCurrentIndex(1);

    m_sitCombo = new QComboBox(this);
    m_sitCombo->addItem(QStringLiteral("Normal"), static_cast<int>(kernel::codes::CombinationSituation::Normal));
    m_sitCombo->addItem(QStringLiteral("Especial"), static_cast<int>(kernel::codes::CombinationSituation::SpecialOrConstruction));
    m_sitCombo->addItem(QStringLiteral("Excepcional"), static_cast<int>(kernel::codes::CombinationSituation::Exceptional));

    m_sigmaAdmSpin = new QDoubleSpinBox(this); m_sigmaAdmSpin->setRange(20.0, 2000.0); m_sigmaAdmSpin->setValue(md.soilSigmaAdm_kPa); m_sigmaAdmSpin->setSuffix(" kPa");
    m_footBSpin = new QDoubleSpinBox(this); m_footBSpin->setRange(0.4, 20.0); m_footBSpin->setValue(md.footingB_m); m_footBSpin->setSuffix(" m");
    m_footLSpin = new QDoubleSpinBox(this); m_footLSpin->setRange(0.4, 20.0); m_footLSpin->setValue(md.footingL_m); m_footLSpin->setSuffix(" m");
    m_v0Spin = new QDoubleSpinBox(this); m_v0Spin->setRange(15.0, 80.0); m_v0Spin->setValue(md.windV0_ms); m_v0Spin->setSuffix(" m/s");
    m_btnReevaluate = new QPushButton(this);
    connect(m_btnReevaluate, &QPushButton::clicked, this, &NbrComplianceDialog::runAudit);
    connect(m_caaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NbrComplianceDialog::runAudit);
    connect(m_rebarCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NbrComplianceDialog::runAudit);
    connect(m_sitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NbrComplianceDialog::runAudit);
    connect(m_sigmaAdmSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NbrComplianceDialog::runAudit);
    connect(m_footBSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NbrComplianceDialog::runAudit);
    connect(m_footLSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NbrComplianceDialog::runAudit);
    connect(m_v0Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NbrComplianceDialog::runAudit);

    paramRow->addWidget(m_caaCombo);
    paramRow->addWidget(m_rebarCombo);
    paramRow->addWidget(m_sitCombo);
    paramRow->addWidget(makeTrLabel("σ_adm:", this));
    paramRow->addWidget(m_sigmaAdmSpin);
    paramRow->addWidget(makeTrLabel("Sapata B×L:", this));
    paramRow->addWidget(m_footBSpin);
    paramRow->addWidget(m_footLSpin);
    paramRow->addWidget(new QLabel(QStringLiteral("V0:"), this));
    paramRow->addWidget(m_v0Spin);
    paramRow->addWidget(m_btnReevaluate);
    root->addLayout(paramRow);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    root->addWidget(m_summaryLabel);

    m_tabs = new QTabWidget(this);
    m_coverageTable = new QTableWidget(this);
    initReadOnlyTable(m_coverageTable, {QStringLiteral("Norma"), QStringLiteral("Título"), QStringLiteral("Status Local"), QStringLiteral("PDF / Escopo")});
    m_steelTable = new QTableWidget(this);
    initReadOnlyTable(m_steelTable, {QStringLiteral("Barra"), QStringLiteral("Seção"), QStringLiteral("L (m)"), QStringLiteral("DCR Máx"), QStringLiteral("Estado Limite Regente"), QStringLiteral("Status")});
    m_beamTable = new QTableWidget(this);
    initReadOnlyTable(m_beamTable, {QStringLiteral("Viga"), QStringLiteral("Seção"), QStringLiteral("As req (cm²)"), QStringLiteral("V_Rd3 (kN)"), QStringLiteral("w_k (mm)"), QStringLiteral("DCR Máx"), QStringLiteral("Status")});
    m_colTable = new QTableWidget(this);
    initReadOnlyTable(m_colTable, {QStringLiteral("Pilar"), QStringLiteral("Seção"), QStringLiteral("ρ (%)"), QStringLiteral("DCR P-Mx-My"), QStringLiteral("DCR Máx"), QStringLiteral("Status")});
    m_foundTable = new QTableWidget(this);
    initReadOnlyTable(m_foundTable, {QStringLiteral("Nó Apoio"), QStringLiteral("N_k (kN)"), QStringLiteral("σ_max (kPa)"), QStringLiteral("σ_lim (kPa)"), QStringLiteral("DCR"), QStringLiteral("Status")});
    m_elsWindTable = new QTableWidget(this);
    initReadOnlyTable(m_elsWindTable, {QStringLiteral("Verificação"), QStringLiteral("Valor / Referência"), QStringLiteral("DCR"), QStringLiteral("Status")});

    m_tabs->addTab(m_coverageTable, QString());
    m_tabs->addTab(m_steelTable, QString());
    m_tabs->addTab(m_beamTable, QString());
    m_tabs->addTab(m_colTable, QString());
    m_tabs->addTab(m_foundTable, QString());
    m_tabs->addTab(m_elsWindTable, QString());
    root->addWidget(m_tabs, 1);

    auto* btns = new QHBoxLayout();
    m_btnApplyDcrViewport = new QPushButton(this);
    m_btnExportPdf = new QPushButton(this);
    m_btnClose = new QPushButton(this);
    connect(m_btnApplyDcrViewport, &QPushButton::clicked, this, [this]() {
        if (!m_viewport) return;
        if ((!m_lastSolver || !m_lastSolver->success) && m_onRequestStaticSolve) {
            m_onRequestStaticSolve();
            runAudit();
        }
        const auto dcrMap = buildElementDcrMapFromAudit(m_lastAudit);
        m_viewport->setElementDcrMap(dcrMap);
        if (!dcrMap.empty()) {
            m_viewport->setPostContourMode(Viewport3D::PostContourMode::NbrComplianceDcr);
        } else {
            m_summaryLabel->setText(
                m_summaryLabel->text() + QStringLiteral("\n") +
                GerenciadorTraducao::trText("Nenhum elemento elegível (barra de aço/concreto resolvida) para plotar Mapa DCR."));
        }
    });
    connect(m_btnExportPdf, &QPushButton::clicked, this, [this]() {
        if (m_onPdfExport) m_onPdfExport();
    });
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);
    btns->addWidget(m_btnApplyDcrViewport);
    btns->addWidget(m_btnExportPdf);
    btns->addStretch();
    btns->addWidget(m_btnClose);
    root->addLayout(btns);

    retranslateUi();
    runAudit();
}

void NbrComplianceDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void NbrComplianceDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Auditoria Normativa ABNT NBR (6118 / 6120 / 6122 / 6123 / 7480 / 8800)"));
    retranslateTaggedWidgets(this);
    retranslateComboItems(m_sitCombo, {"Normal", "Especial", "Excepcional"});
    setTrHeaders(m_coverageTable, {"Norma", "Título", "Status Local", "PDF / Escopo"});
    setTrHeaders(m_steelTable, {"Barra", "Seção", "L (m)", "DCR Máx", "Estado Limite Regente", "Status"});
    setTrHeaders(m_beamTable, {"Viga", "Seção", "As req (cm²)", "V_Rd3 (kN)", "w_k (mm)", "DCR Máx", "Status"});
    setTrHeaders(m_colTable, {"Pilar", "Seção", "ρ (%)", "DCR P-Mx-My", "DCR Máx", "Status"});
    setTrHeaders(m_foundTable, {"Nó Apoio", "N_k (kN)", "σ_max (kPa)", "σ_lim (kPa)", "DCR", "Status"});
    setTrHeaders(m_elsWindTable, {"Verificação", "Valor / Referência", "DCR", "Status"});
    m_tabs->setTabText(0, GerenciadorTraducao::trText("Cobertura Normativa Local"));
    m_tabs->setTabText(1, GerenciadorTraducao::trText("Aço (NBR 8800)"));
    m_tabs->setTabText(2, GerenciadorTraducao::trText("Vigas CA (NBR 6118)"));
    m_tabs->setTabText(3, GerenciadorTraducao::trText("Pilares CA (NBR 6118)"));
    m_tabs->setTabText(4, GerenciadorTraducao::trText("Fundações (NBR 6122)"));
    m_tabs->setTabText(5, GerenciadorTraducao::trText("ELS & Vento (6123)"));
    m_btnReevaluate->setText(GerenciadorTraducao::trText("Recalcular Auditoria"));
    m_btnApplyDcrViewport->setText(GerenciadorTraducao::trText("Plotar Mapa DCR no Viewport 3D"));
    m_btnExportPdf->setText(GerenciadorTraducao::trText("Exportar Memorial PDF (A4)..."));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
}

io::PdfReportOptions NbrComplianceDialog::buildOptions() const {
    io::PdfReportOptions opts;
    if (m_model) {
        auto& md = m_model->metadata();
        md.soilSigmaAdm_kPa = m_sigmaAdmSpin->value();
        md.footingB_m = m_footBSpin->value();
        md.footingL_m = m_footLSpin->value();
        md.windV0_ms = m_v0Spin->value();
        opts.projectTitle = QString::fromStdString(md.projectName);
        opts.engineerName = QString::fromStdString(md.engineerName);
        opts.clientName = QString::fromStdString(md.clientName);
        opts.siteLocation = QString::fromStdString(md.siteLocation);
        opts.windS1 = md.windS1;
        opts.windS2 = md.windS2;
        opts.windS3 = md.windS3;
    }
    opts.caa = static_cast<kernel::codes::EnvironmentalAggressivenessClass>(m_caaCombo->currentData().toInt());
    opts.rebarGrade = static_cast<kernel::codes::RebarGrade7480>(m_rebarCombo->currentData().toInt());
    opts.situation = static_cast<kernel::codes::CombinationSituation>(m_sitCombo->currentData().toInt());
    opts.soilSigmaAdm_kPa = m_sigmaAdmSpin->value();
    opts.footingB_m = m_footBSpin->value();
    opts.footingL_m = m_footLSpin->value();
    opts.windV0_ms = m_v0Spin->value();
    return opts;
}

void NbrComplianceDialog::runAudit() {
    if (!m_model) return;
    const bool hasSolved = (m_lastSolver && m_lastSolver->success);
    const SolverResult emptyStatic;
    const SolverResult& staticRef = hasSolved ? *m_lastSolver : emptyStatic;
    m_lastAudit = io::PdfReportGenerator::evaluateModelNbrCompliance(*m_model, staticRef, buildOptions());

    QString summaryText = GerenciadorTraducao::trText("DCR Máximo Global: %1  |  Parecer: %2  |  %3")
        .arg(m_lastAudit.maxDcr, 0, 'f', 3)
        .arg(m_lastAudit.passedAll ? GerenciadorTraducao::trText("APROVADO (DCR <= 1,00)")
                                   : GerenciadorTraducao::trText("ALERTA (Existem elementos com DCR > 1,00)"))
        .arg(m_lastAudit.governingSummary);
    if (!hasSolved) {
        summaryText += QStringLiteral("\n") +
            GerenciadorTraducao::trText("Aviso: Modelo ainda não resolvido estaticamente (F5). Valores refletem apenas esforços nulos / geometria.");
    }
    m_summaryLabel->setText(summaryText);

    if (m_viewport && m_viewport->postContourMode() == Viewport3D::PostContourMode::NbrComplianceDcr) {
        m_viewport->setElementDcrMap(buildElementDcrMapFromAudit(m_lastAudit));
    }

    m_coverageTable->setRowCount(0);
    for (const auto& c : m_lastAudit.coverageMatrix) {
        const int r = m_coverageTable->rowCount();
        m_coverageTable->insertRow(r);
        m_coverageTable->setItem(r, 0, makeItem(QString::fromStdString(c.codeId)));
        m_coverageTable->setItem(r, 1, makeItem(QString::fromStdString(c.title), Qt::AlignLeft | Qt::AlignVCenter));
        QString stText = QStringLiteral("OutOfLocalPdfScope");
        QColor bg(180, 120, 20, 70);
        if (c.status == kernel::codes::ComplianceScopeStatus::LocalPdfVerified) {
            stText = QStringLiteral("LocalPdfVerified");
            bg = QColor(34, 160, 90, 70);
        } else if (c.status == kernel::codes::ComplianceScopeStatus::PartialReferenceOnly) {
            stText = QStringLiteral("PartialReferenceOnly");
            bg = QColor(40, 120, 200, 70);
        }
        auto* stItem = makeItem(stText);
        stItem->setBackground(bg);
        m_coverageTable->setItem(r, 2, stItem);
        m_coverageTable->setItem(r, 3, makeItem(QString::fromStdString(c.scopeNote), Qt::AlignLeft | Qt::AlignVCenter));
    }

    m_steelTable->setRowCount(0);
    for (const auto& s : m_lastAudit.steelMembers) {
        const int r = m_steelTable->rowCount();
        m_steelTable->insertRow(r);
        m_steelTable->setItem(r, 0, makeItem(QString::number(s.elementId)));
        m_steelTable->setItem(r, 1, makeItem(s.sectionName));
        m_steelTable->setItem(r, 2, makeItem(QString::number(s.lengthM, 'f', 2)));
        m_steelTable->setItem(r, 3, makeItem(QString::number(s.result.dcrMax, 'f', 3)));
        m_steelTable->setItem(r, 4, makeItem(QString::fromStdString(s.result.governingLimitState), Qt::AlignLeft | Qt::AlignVCenter));
        m_steelTable->setItem(r, 5, makeItem(s.result.passed ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }

    m_beamTable->setRowCount(0);
    for (const auto& b : m_lastAudit.concreteBeams) {
        const int r = m_beamTable->rowCount();
        m_beamTable->insertRow(r);
        m_beamTable->setItem(r, 0, makeItem(QString::number(b.elementId)));
        m_beamTable->setItem(r, 1, makeItem(b.sectionName));
        m_beamTable->setItem(r, 2, makeItem(QString::number(b.result.requiredAs_cm2, 'f', 2)));
        m_beamTable->setItem(r, 3, makeItem(QString::number(b.result.V_Rd3_kN, 'f', 1)));
        m_beamTable->setItem(r, 4, makeItem(QString::number(b.result.wk_mm, 'f', 3)));
        m_beamTable->setItem(r, 5, makeItem(QString::number(b.result.dcrMax, 'f', 3)));
        m_beamTable->setItem(r, 6, makeItem(b.result.passed ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }

    m_colTable->setRowCount(0);
    for (const auto& c : m_lastAudit.concreteColumns) {
        const int r = m_colTable->rowCount();
        m_colTable->insertRow(r);
        m_colTable->setItem(r, 0, makeItem(QString::number(c.elementId)));
        m_colTable->setItem(r, 1, makeItem(c.sectionName));
        m_colTable->setItem(r, 2, makeItem(QString::number(c.result.rhoPercent, 'f', 2)));
        m_colTable->setItem(r, 3, makeItem(QString::number(c.result.dcrPMM, 'f', 3)));
        m_colTable->setItem(r, 4, makeItem(QString::number(c.result.dcrMax, 'f', 3)));
        m_colTable->setItem(r, 5, makeItem(c.result.passed ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }

    m_foundTable->setRowCount(0);
    for (const auto& f : m_lastAudit.foundations) {
        const int r = m_foundTable->rowCount();
        m_foundTable->insertRow(r);
        m_foundTable->setItem(r, 0, makeItem(QString::number(f.nodeId)));
        m_foundTable->setItem(r, 1, makeItem(QString::number(f.reactionVert_kN, 'f', 1)));
        m_foundTable->setItem(r, 2, makeItem(QString::number(f.result.sigmaMax_kPa, 'f', 1)));
        m_foundTable->setItem(r, 3, makeItem(QString::number(f.result.sigmaLimit_kPa, 'f', 1)));
        m_foundTable->setItem(r, 4, makeItem(QString::number(f.result.dcrBearing, 'f', 3)));
        m_foundTable->setItem(r, 5, makeItem(f.result.passed ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    }

    m_elsWindTable->setRowCount(0);
    const auto addElsRow = [&](const QString& label, const QString& val, double dcr, bool ok) {
        const int r = m_elsWindTable->rowCount();
        m_elsWindTable->insertRow(r);
        m_elsWindTable->setItem(r, 0, makeItem(label, Qt::AlignLeft | Qt::AlignVCenter));
        m_elsWindTable->setItem(r, 1, makeItem(val));
        m_elsWindTable->setItem(r, 2, makeItem(QString::number(dcr, 'f', 3)));
        m_elsWindTable->setItem(r, 3, makeItem(ok ? QStringLiteral("OK") : QStringLiteral("FAIL")));
    };
    addElsRow(GerenciadorTraducao::trText("ELS Deslocamentos (NBR 6118 Tab. 13.3)"),
              QString::fromStdString(m_lastAudit.elsDisplacement.governingCriterion),
              m_lastAudit.elsDisplacement.dcrMax,
              m_lastAudit.elsDisplacement.passed);
    addElsRow(GerenciadorTraducao::trText("Pressão Característica de Vento (NBR 6123 Sec. 4.2)"),
              QStringLiteral("Vk=%1 m/s | q=%2 kN/m²")
                  .arg(m_lastAudit.sampleWind6123.vk_ms, 0, 'f', 2)
                  .arg(m_lastAudit.sampleWind6123.q_kPa, 0, 'f', 3),
              0.0, true);
}

// ==========================================
// PdfReportOptionsDialog
// ==========================================
PdfReportOptionsDialog::PdfReportOptionsDialog(QWidget* parent, StructuralModel* model)
    : QDialog(parent),
      m_model(model) {
    setMinimumWidth(480);
    const auto& md = model ? model->metadata() : ProjectMetadata{};

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_projectEdit = new QLineEdit(QString::fromStdString(md.projectName), this);
    m_engineerEdit = new QLineEdit(QString::fromStdString(md.engineerName), this);
    m_clientEdit = new QLineEdit(QString::fromStdString(md.clientName), this);
    m_locationEdit = new QLineEdit(QString::fromStdString(md.siteLocation), this);

    m_caaCombo = new QComboBox(this);
    m_caaCombo->addItem(QStringLiteral("CAA I"), static_cast<int>(kernel::codes::EnvironmentalAggressivenessClass::CAA_I));
    m_caaCombo->addItem(QStringLiteral("CAA II"), static_cast<int>(kernel::codes::EnvironmentalAggressivenessClass::CAA_II));
    m_caaCombo->addItem(QStringLiteral("CAA III"), static_cast<int>(kernel::codes::EnvironmentalAggressivenessClass::CAA_III));
    m_caaCombo->addItem(QStringLiteral("CAA IV"), static_cast<int>(kernel::codes::EnvironmentalAggressivenessClass::CAA_IV));
    m_caaCombo->setCurrentIndex(1);

    m_rebarCombo = new QComboBox(this);
    m_rebarCombo->addItem(QStringLiteral("CA-25"), static_cast<int>(kernel::codes::RebarGrade7480::CA25));
    m_rebarCombo->addItem(QStringLiteral("CA-50"), static_cast<int>(kernel::codes::RebarGrade7480::CA50));
    m_rebarCombo->addItem(QStringLiteral("CA-60"), static_cast<int>(kernel::codes::RebarGrade7480::CA60));
    m_rebarCombo->setCurrentIndex(1);

    m_sitCombo = new QComboBox(this);
    m_sitCombo->addItem(QStringLiteral("Normal"), static_cast<int>(kernel::codes::CombinationSituation::Normal));
    m_sitCombo->addItem(QStringLiteral("Especial"), static_cast<int>(kernel::codes::CombinationSituation::SpecialOrConstruction));
    m_sitCombo->addItem(QStringLiteral("Excepcional"), static_cast<int>(kernel::codes::CombinationSituation::Exceptional));

    m_sigmaAdmSpin = new QDoubleSpinBox(this); m_sigmaAdmSpin->setRange(20.0, 2000.0); m_sigmaAdmSpin->setValue(md.soilSigmaAdm_kPa); m_sigmaAdmSpin->setSuffix(" kPa");
    m_footBSpin = new QDoubleSpinBox(this); m_footBSpin->setRange(0.4, 20.0); m_footBSpin->setValue(md.footingB_m); m_footBSpin->setSuffix(" m");
    m_footLSpin = new QDoubleSpinBox(this); m_footLSpin->setRange(0.4, 20.0); m_footLSpin->setValue(md.footingL_m); m_footLSpin->setSuffix(" m");
    m_v0Spin = new QDoubleSpinBox(this); m_v0Spin->setRange(15.0, 80.0); m_v0Spin->setValue(md.windV0_ms); m_v0Spin->setSuffix(" m/s");
    m_s1Spin = new QDoubleSpinBox(this); m_s1Spin->setRange(0.5, 2.5); m_s1Spin->setDecimals(3); m_s1Spin->setValue(md.windS1);
    m_s2Spin = new QDoubleSpinBox(this); m_s2Spin->setRange(0.3, 2.5); m_s2Spin->setDecimals(3); m_s2Spin->setValue(md.windS2);
    m_s3Spin = new QDoubleSpinBox(this); m_s3Spin->setRange(0.5, 2.0); m_s3Spin->setDecimals(3); m_s3Spin->setValue(md.windS3);

    m_includeDiagCheck = new QCheckBox(this); m_includeDiagCheck->setChecked(true);
    m_includeNbrCheck = new QCheckBox(this); m_includeNbrCheck->setChecked(true);
    m_captureViewportCheck = new QCheckBox(this); m_captureViewportCheck->setChecked(true);

    addTrRow(form, "Empreendimento / Título:", m_projectEdit, this);
    addTrRow(form, "Engenheiro Responsável:", m_engineerEdit, this);
    addTrRow(form, "Cliente:", m_clientEdit, this);
    addTrRow(form, "Local / Cidade:", m_locationEdit, this);
    addTrRow(form, "Agressividade CAA / Aço / Situação:", [this]() {
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_caaCombo); h->addWidget(m_rebarCombo); h->addWidget(m_sitCombo);
        return w;
    }(), this);
    addTrRow(form, "σ_adm Solo / Sapata B×L:", [this]() {
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_sigmaAdmSpin); h->addWidget(m_footBSpin); h->addWidget(m_footLSpin);
        return w;
    }(), this);
    addTrRow(form, "Vento V0 / S1 / S2 / S3:", [this]() {
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_v0Spin); h->addWidget(m_s1Spin); h->addWidget(m_s2Spin); h->addWidget(m_s3Spin);
        return w;
    }(), this);
    form->addRow(m_includeDiagCheck);
    form->addRow(m_includeNbrCheck);
    form->addRow(m_captureViewportCheck);
    root->addLayout(form);

    auto* btns = new QHBoxLayout();
    m_btnGenerate = new QPushButton(this);
    m_btnCancel = new QPushButton(this);
    connect(m_btnGenerate, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btns->addStretch();
    btns->addWidget(m_btnGenerate);
    btns->addWidget(m_btnCancel);
    root->addLayout(btns);

    retranslateUi();
}

void PdfReportOptionsDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void PdfReportOptionsDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Configurar Memorial de Cálculo Executivo (PDF A4)"));
    retranslateTaggedWidgets(this);
    retranslateComboItems(m_sitCombo, {"Normal", "Especial", "Excepcional"});
    m_includeDiagCheck->setText(GerenciadorTraducao::trText("Incluir Diagramas Vetoriais N / V / M"));
    m_includeNbrCheck->setText(GerenciadorTraducao::trText("Incluir Auditoria Normativa ABNT NBR Completa"));
    m_captureViewportCheck->setText(GerenciadorTraducao::trText("Capturar Modelo 3D Atual na Figura do Memorial"));
    m_btnGenerate->setText(GerenciadorTraducao::trText("Escolher Destino & Gerar PDF..."));
    m_btnCancel->setText(GerenciadorTraducao::trText("Cancelar"));
}

io::PdfReportOptions PdfReportOptionsDialog::collectOptionsAndSaveMetadata() {
    io::PdfReportOptions opts;
    opts.projectTitle = m_projectEdit->text().trimmed();
    opts.engineerName = m_engineerEdit->text().trimmed();
    opts.clientName = m_clientEdit->text().trimmed();
    opts.siteLocation = m_locationEdit->text().trimmed();
    opts.caa = static_cast<kernel::codes::EnvironmentalAggressivenessClass>(m_caaCombo->currentData().toInt());
    opts.rebarGrade = static_cast<kernel::codes::RebarGrade7480>(m_rebarCombo->currentData().toInt());
    opts.situation = static_cast<kernel::codes::CombinationSituation>(m_sitCombo->currentData().toInt());
    opts.soilSigmaAdm_kPa = m_sigmaAdmSpin->value();
    opts.footingB_m = m_footBSpin->value();
    opts.footingL_m = m_footLSpin->value();
    opts.windV0_ms = m_v0Spin->value();
    opts.windS1 = m_s1Spin->value();
    opts.windS2 = m_s2Spin->value();
    opts.windS3 = m_s3Spin->value();
    opts.includeDiagrams = m_includeDiagCheck->isChecked();
    opts.includeNbrAudit = m_includeNbrCheck->isChecked();

    if (m_model) {
        auto& md = m_model->metadata();
        md.projectName = opts.projectTitle.toStdString();
        md.engineerName = opts.engineerName.toStdString();
        md.clientName = opts.clientName.toStdString();
        md.siteLocation = opts.siteLocation.toStdString();
        md.soilSigmaAdm_kPa = opts.soilSigmaAdm_kPa;
        md.footingB_m = opts.footingB_m;
        md.footingL_m = opts.footingL_m;
        md.windV0_ms = opts.windV0_ms;
        md.windS1 = opts.windS1;
        md.windS2 = opts.windS2;
        md.windS3 = opts.windS3;
    }
    return opts;
}

bool PdfReportOptionsDialog::includeViewportCapture() const {
    return m_captureViewportCheck->isChecked();
}

} // namespace omniframe
