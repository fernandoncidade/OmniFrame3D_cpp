#pragma once

#include "core/StructuralModel.hpp"
#include "kernel/analysis/BucklingSolver.hpp"
#include "kernel/analysis/DynamicSolver.hpp"
#include "kernel/analysis/NonlinearSolver.hpp"
#include "kernel/codes/NbrCodeChecks.hpp"
#include "solver/FemSolver3D.hpp"

#include <QImage>
#include <QString>
#include <memory>
#include <vector>

namespace omniframe::io {

struct SteelMemberAuditEntry {
    ElementId elementId = INVALID_ID;
    QString sectionName;
    QString materialName;
    double lengthM = 0.0;
    kernel::codes::SteelDesignForces forces;
    kernel::codes::SteelCheckResult result;
};

struct ConcreteBeamAuditEntry {
    ElementId elementId = INVALID_ID;
    QString sectionName;
    QString materialName;
    double lengthM = 0.0;
    kernel::codes::ConcreteBeamForces forces;
    kernel::codes::ConcreteBeamCheckResult result;
};

struct ConcreteColumnAuditEntry {
    ElementId elementId = INVALID_ID;
    QString sectionName;
    QString materialName;
    double lengthM = 0.0;
    kernel::codes::ConcreteColumnForces forces;
    kernel::codes::ConcreteColumnCheckResult result;
};

struct FoundationAuditEntry {
    NodeId nodeId = INVALID_ID;
    double reactionVert_kN = 0.0;
    double momentX_kNm = 0.0;
    double momentY_kNm = 0.0;
    double B_m = 1.50;
    double L_m = 1.50;
    kernel::codes::ShallowFoundationResult result;
};

struct ModelNbrAudit {
    std::vector<SteelMemberAuditEntry> steelMembers;
    std::vector<ConcreteBeamAuditEntry> concreteBeams;
    std::vector<ConcreteColumnAuditEntry> concreteColumns;
    std::vector<FoundationAuditEntry> foundations;
    kernel::codes::ServiceabilityDisplacementResult elsDisplacement;
    kernel::codes::WindResult6123 sampleWind6123;
    std::vector<kernel::codes::StandardCoverageInfo> coverageMatrix;

    double maxDcr = 0.0;
    bool passedAll = true;
    QString governingSummary;
};

struct PdfReportOptions {
    QString projectTitle = "OmniFrame 3D - Memorial de Calculo Estrutural";
    QString engineerName = "Engenheiro Responsavel";
    QString clientName = "Cliente / Empreendimento";
    QString siteLocation = "Brasil";

    kernel::codes::CombinationSituation situation = kernel::codes::CombinationSituation::Normal;
    kernel::codes::EnvironmentalAggressivenessClass caa = kernel::codes::EnvironmentalAggressivenessClass::CAA_II;
    kernel::codes::RebarGrade7480 rebarGrade = kernel::codes::RebarGrade7480::CA50;

    double concreteFckOverrideMPa = 0.0; ///< <= 0 infere de nome/E do material
    double steelFyOverrideMPa = 0.0;     ///< <= 0 infere de sigmaY/nome (padrao 250 MPa)
    double footingB_m = 1.50;
    double footingL_m = 1.50;
    double soilSigmaAdm_kPa = 200.0;
    double windV0_ms = 35.0;
    double windS1 = 1.0;
    double windS2 = 1.0;
    double windS3 = 1.0;

    bool includeDiagrams = true;
    bool includeNbrAudit = true;
    QImage viewportSnapshot;

    // Snapshots imutáveis com posse compartilhada para uso seguro em diálogos não modais ([AUD-005])
    std::shared_ptr<const kernel::ModalResult> modalResult;
    std::shared_ptr<const kernel::BucklingResult> bucklingResult;
    std::shared_ptr<const kernel::NonlinearResult> nonlinearResult;
};

class PdfReportGenerator {
public:
    /// Avalia o modelo resolvido frente as normas locais ABNT NBR (6118, 6120, 6122, 6123, 7480, 8800).
    static ModelNbrAudit evaluateModelNbrCompliance(const StructuralModel& model,
                                                    const SolverResult& result,
                                                    const PdfReportOptions& opts = {});

    /// Gera PDF executivo multipagina A4 (24 secoes tecnicas + diagramas vetoriais N/V/M/w + tabelas NBR).
    static bool generateExecutivePdf(const QString& filePath,
                                     const StructuralModel& model,
                                     const SolverResult& result,
                                     const PdfReportOptions& opts = {},
                                     QString* errorMsg = nullptr);
};

} // namespace omniframe::io
