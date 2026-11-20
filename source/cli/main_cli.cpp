#include "core/StructuralModel.hpp"
#include "io/DxfInterop.hpp"
#include "io/IfcInterop.hpp"
#include "io/PdfReportGenerator.hpp"
#include "io/ProjectSerializer.hpp"
#include "kernel/analysis/BucklingSolver.hpp"
#include "kernel/analysis/DynamicSolver.hpp"
#include "kernel/analysis/NonlinearSolver.hpp"
#include "solver/FemSolver3D.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace omniframe;

std::string toLowerAscii(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool endsWithCi(const std::string& str, const std::string& suffix) {
    if (str.size() < suffix.size()) return false;
    return toLowerAscii(str.substr(str.size() - suffix.size())) == toLowerAscii(suffix);
}

void printUsage() {
    std::cout << "OmniFrame 3D CLI (Kernel C++23 + Eigen3)\n"
              << "Uso:\n"
              << "  omniframe-cli --version\n"
              << "  omniframe-cli --help\n"
              << "  omniframe-cli --validate <arquivo.omniframe|dxf|ifc>\n"
              << "  omniframe-cli --analyze <arquivo.omniframe|dxf|ifc> [opcoes]\n\n"
              << "Opcoes de analise e saida:\n"
              << "  --analysis <static|modal|buckling|nonlinear|pdelta|harmonic|spectrum>\n"
              << "  --solver   <auto|ldlt|llt|sparselu|cg|bicgstab>\n"
              << "  --modes    <N>             Numero de modos (modal/buckling, padrao=6)\n"
              << "  --steps    <N>             Numero de passos incrementais (padrao=20)\n"
              << "  --method   <nr|arclength>  Metodo nao linear (padrao=nr)\n"
              << "  --export   <json|csv|vtk|dxf|ifc> <caminho>\n"
              << "  --report   <md|html|pdf> <caminho>\n"
              << "  --output   <caminho.json>  Salva resumo JSON estruturado\n"
              << "  --json                     Emite resultado JSON no stdout\n";
}

bool loadModelAnyFormat(const std::string& path, StructuralModel& model, std::string& errOut) {
    if (endsWithCi(path, ".dxf")) {
        io::DxfInteropReport rep;
        if (!io::DxfInterop::importFromFile(path, model, nullptr, io::DxfImportOptions{}, &rep)) {
            errOut = rep.errorMessage;
            return false;
        }
        return true;
    }
    if (endsWithCi(path, ".ifc")) {
        io::IfcInteropReport rep;
        if (!io::IfcInterop::importFromFile(path, model, nullptr, nullptr, &rep)) {
            errOut = rep.errorMessage;
            return false;
        }
        return true;
    }
    if (!ProjectSerializer::loadFromFile(QString::fromStdString(path), model)) {
        errOut = "Falha ao carregar projeto .omniframe: " + path;
        return false;
    }
    return true;
}

bool exportCsvFile(const std::string& path, const StructuralModel& model) {
    std::ofstream os(path, std::ios::binary);
    if (!os.is_open()) return false;
    os << "# OmniFrame 3D Nodal Displacements & Reactions (SI: m, rad, kN, kN*m)\n";
    os << "NodeID,X,Y,Z,UX,UY,UZ,RX,RY,RZ,FX_Reac,FY_Reac,FZ_Reac,MX_Reac,MY_Reac,MZ_Reac\n";
    for (const auto& n : model.getNodes()) {
        os << n.id << "," << std::setprecision(9)
           << n.position.x << "," << n.position.y << "," << n.position.z;
        for (int d = 0; d < 6; ++d) os << "," << n.displacement[d];
        for (int d = 0; d < 6; ++d) os << "," << n.reaction[d];
        os << "\n";
    }
    os << "\n# Element Local End Forces\n";
    os << "ElementID,NodeStart,NodeEnd,HasResults,N_i,Vy_i,Vz_i,Tx_i,My_i,Mz_i,N_j,Vy_j,Vz_j,Tx_j,My_j,Mz_j\n";
    for (const auto& e : model.getElements()) {
        os << e.id << "," << e.nodeStart << "," << e.nodeEnd << "," << (e.hasResults ? 1 : 0);
        for (int d = 0; d < 12; ++d) os << "," << (e.hasResults ? e.localEndForces(d) : 0.0);
        os << "\n";
    }
    return os.good();
}

bool exportVtkFile(const std::string& path, const StructuralModel& model) {
    std::ofstream os(path, std::ios::binary);
    if (!os.is_open()) return false;
    const auto& nodes = model.getNodes();
    const auto& elems = model.getElements();
    std::unordered_map<NodeId, int> idxMap;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) idxMap[nodes[i].id] = i;

    os << "# vtk DataFile Version 3.0\n"
       << "OmniFrame 3D Structural Analysis Export\n"
       << "ASCII\n"
       << "DATASET POLYDATA\n";
    os << "POINTS " << nodes.size() << " double\n";
    for (const auto& n : nodes) {
        os << std::setprecision(10) << n.position.x << " " << n.position.y << " " << n.position.z << "\n";
    }
    os << "LINES " << elems.size() << " " << (elems.size() * 3) << "\n";
    for (const auto& e : elems) {
        os << "2 " << idxMap[e.nodeStart] << " " << idxMap[e.nodeEnd] << "\n";
    }
    os << "POINT_DATA " << nodes.size() << "\n";
    os << "VECTORS Displacement double\n";
    for (const auto& n : nodes) {
        os << std::setprecision(10)
           << n.displacement[0] << " " << n.displacement[1] << " " << n.displacement[2] << "\n";
    }
    os << "SCALARS DisplacementMagnitude double 1\n"
       << "LOOKUP_TABLE default\n";
    for (const auto& n : nodes) {
        const double mag = std::sqrt(n.displacement[0] * n.displacement[0] +
                                     n.displacement[1] * n.displacement[1] +
                                     n.displacement[2] * n.displacement[2]);
        os << std::setprecision(10) << mag << "\n";
    }
    return os.good();
}

} // namespace

int main(int argc, char* argv[]) {
    bool needsPdfGui = false;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--report" && toLowerAscii(argv[i + 1]) == "pdf") {
            needsPdfGui = true;
            break;
        }
    }
    if (needsPdfGui && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    std::unique_ptr<QCoreApplication> app = needsPdfGui
        ? std::make_unique<QGuiApplication>(argc, argv)
        : std::make_unique<QCoreApplication>(argc, argv);

    if (argc <= 1) {
        printUsage();
        return 1;
    }

    bool doValidate = false;
    bool doAnalyze = false;
    bool jsonStdout = false;
    std::string inputPath;
    std::string analysisType = "static";
    std::string solverStr = "auto";
    std::string nlMethodStr = "nr";
    int modesCount = 6;
    int stepsCount = 20;
    std::string outputJsonPath;
    std::vector<std::pair<std::string, std::string>> exports;
    std::vector<std::pair<std::string, std::string>> reports;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") {
            std::cout << "OmniFrame 3D CLI v3.0.0 (C++23 / Eigen 3.4 / Schema 2.0)\n";
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--json") {
            jsonStdout = true;
        } else if (arg == "--validate" && i + 1 < argc) {
            doValidate = true;
            inputPath = argv[++i];
        } else if (arg == "--analyze" && i + 1 < argc) {
            doAnalyze = true;
            inputPath = argv[++i];
        } else if (arg == "--analysis" && i + 1 < argc) {
            analysisType = toLowerAscii(argv[++i]);
        } else if (arg == "--solver" && i + 1 < argc) {
            solverStr = toLowerAscii(argv[++i]);
        } else if (arg == "--method" && i + 1 < argc) {
            nlMethodStr = toLowerAscii(argv[++i]);
        } else if (arg == "--modes" && i + 1 < argc) {
            modesCount = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--steps" && i + 1 < argc) {
            stepsCount = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--output" && i + 1 < argc) {
            outputJsonPath = argv[++i];
        } else if (arg == "--export" && i + 2 < argc) {
            const std::string fmt = toLowerAscii(argv[++i]);
            const std::string outP = argv[++i];
            exports.emplace_back(fmt, outP);
        } else if (arg == "--report" && i + 2 < argc) {
            const std::string fmt = toLowerAscii(argv[++i]);
            const std::string outP = argv[++i];
            reports.emplace_back(fmt, outP);
        } else {
            std::cerr << "Argumento desconhecido ou incompleto: " << arg << "\n";
            return 1;
        }
    }

    if (inputPath.empty() || (!doValidate && !doAnalyze)) {
        printUsage();
        return 1;
    }

    StructuralModel model;
    std::string loadErr;
    if (!loadModelAnyFormat(inputPath, model, loadErr)) {
        std::cerr << "{\"success\":false,\"error\":\"" << loadErr << "\"}\n";
        return 1;
    }

    const ModelValidation val = model.validate();
    QJsonObject root;
    root["schemaVersion"] = "2.0.0";
    root["inputFile"] = QString::fromStdString(inputPath);
    root["nodes"] = static_cast<int>(model.getNodes().size());
    root["elements"] = static_cast<int>(model.getElements().size());
    root["valid"] = val.valid;

    QJsonArray errArr, warnArr;
    for (const auto& e : val.errors) errArr.append(QString::fromStdString(e));
    for (const auto& w : val.warnings) warnArr.append(QString::fromStdString(w));
    root["validationErrors"] = errArr;
    root["validationWarnings"] = warnArr;

    auto emitJson = [&](const QJsonObject& obj) {
        const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
        if (!outputJsonPath.empty()) {
            QFile f(QString::fromStdString(outputJsonPath));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(bytes);
            }
        }
        if (jsonStdout || !doAnalyze) {
            std::cout << bytes.toStdString();
        }
    };

    if (doValidate && !doAnalyze) {
        root["success"] = val.valid;
        emitJson(root);
        return val.valid ? 0 : 2;
    }

    if (!val.valid) {
        root["success"] = false;
        emitJson(root);
        return 2;
    }

    kernel::LinearSolverOptions linOpts = model.solverOptions();
    if (solverStr == "ldlt") linOpts.type = kernel::LinearSolverType::SimplicialLDLT;
    else if (solverStr == "llt") linOpts.type = kernel::LinearSolverType::SimplicialLLT;
    else if (solverStr == "sparselu") linOpts.type = kernel::LinearSolverType::SparseLU;
    else if (solverStr == "cg") linOpts.type = kernel::LinearSolverType::ConjugateGradient;
    else if (solverStr == "bicgstab") linOpts.type = kernel::LinearSolverType::BiCGSTAB;
    else linOpts.type = kernel::LinearSolverType::Auto;

    SolverResult staticRes;
    bool analysisSuccess = false;
    root["analysisType"] = QString::fromStdString(analysisType);

    if (analysisType == "static") {
        staticRes = FemSolver3D::solveWithOptions(model, linOpts);
        analysisSuccess = staticRes.success;
        root["success"] = staticRes.success;
        root["error"] = QString::fromStdString(staticRes.errorMessage);
        root["totalDofs"] = staticRes.totalDofs;
        root["restrainedDofs"] = staticRes.restrainedDofs;
        root["maxNodalDisplacement"] = staticRes.maxNodalDisplacement;
        root["totalStrainEnergy"] = staticRes.totalStrainEnergy;
        root["equilibriumBalanced"] = staticRes.equilibrium.isBalanced;
        root["relativeResidual"] = staticRes.solverStats.relativeResidual;
        root["executionTimeMs"] = staticRes.executionTimeMs;
    } else if (analysisType == "modal") {
        kernel::ModalOptions mo;
        mo.numModes = modesCount;
        const auto mRes = FemSolver3D::solveModal(model, mo);
        analysisSuccess = mRes.success;
        root["success"] = mRes.success;
        root["error"] = QString::fromStdString(mRes.errorMessage);
        QJsonArray freqs;
        for (const auto& md : mRes.modes) {
            QJsonObject mj;
            mj["mode"] = md.modeIndex;
            mj["frequencyHz"] = md.frequencyHz;
            mj["periodSec"] = md.periodSec;
            mj["omegaRadS"] = md.omega;
            freqs.append(mj);
        }
        root["modes"] = freqs;
    } else if (analysisType == "buckling") {
        kernel::BucklingOptions bo;
        bo.numModes = modesCount;
        const auto bRes = FemSolver3D::solveBuckling(model, bo);
        analysisSuccess = bRes.success;
        root["success"] = bRes.success;
        root["error"] = QString::fromStdString(bRes.errorMessage);
        QJsonArray factors;
        for (double lam : bRes.loadFactors) factors.append(lam);
        root["criticalLoadFactors"] = factors;
    } else if (analysisType == "nonlinear" || analysisType == "pdelta") {
        kernel::NonlinearOptions nlo;
        nlo.numSteps = stepsCount;
        nlo.method = (analysisType == "pdelta")
            ? kernel::NonlinearMethod::PDelta
            : ((nlMethodStr == "arclength")
                ? kernel::NonlinearMethod::ArcLengthCrisfield
                : kernel::NonlinearMethod::NewtonRaphson);
        nlo.kinematics = (analysisType == "pdelta")
            ? kernel::NonlinearKinematics::PDeltaSecondOrder
            : kernel::NonlinearKinematics::Corotational3D;
        const auto nlRes = FemSolver3D::solveNonlinear(model, nlo);
        analysisSuccess = nlRes.success;
        root["success"] = nlRes.success;
        root["error"] = QString::fromStdString(nlRes.errorMessage);
        root["finalLoadFactor"] = nlRes.achievedLoadFactor;
        root["totalSteps"] = nlRes.totalSteps;
        bool yieldedAny = false;
        for (const auto& stepRec : nlRes.history) {
            if (stepRec.anyYielded) { yieldedAny = true; break; }
        }
        root["anyYielded"] = yieldedAny;
    } else if (analysisType == "harmonic") {
        kernel::HarmonicOptions ho;
        ho.frequenciesHz = {1.0, 2.0, 5.0, 10.0, 20.0};
        ho.structuralLossEta = 0.04;
        const auto hRes = FemSolver3D::solveHarmonic(model, ho);
        analysisSuccess = hRes.success;
        root["success"] = hRes.success;
        root["error"] = QString::fromStdString(hRes.errorMessage);
        root["sweepPoints"] = static_cast<int>(hRes.sweep.size());
    } else if (analysisType == "spectrum") {
        kernel::ResponseSpectrumOptions so;
        so.spectrumCurve = kernel::DynamicSolver::buildNbr15421SpectrumCurve(1.47, 1.0, 1.0);
        kernel::ModalOptions mo;
        mo.numModes = modesCount;
        const auto sRes = FemSolver3D::solveResponseSpectrum(model, so, mo);
        analysisSuccess = sRes.success;
        root["success"] = sRes.success;
        root["error"] = QString::fromStdString(sRes.errorMessage);
        root["combinedDispNorm"] = sRes.combinedDisplacementFull.norm();
    } else {
        std::cerr << "Tipo de analise desconhecido: " << analysisType << "\n";
        return 1;
    }

    if (!analysisSuccess) {
        emitJson(root);
        return 2;
    }

    // Executa solução estática caso não tenha sido a primária e usuário pediu relatórios/exportação estática
    if (analysisType != "static" && (!reports.empty() || !exports.empty())) {
        staticRes = FemSolver3D::solveWithOptions(model, linOpts);
    }

    for (const auto& [fmt, outPath] : exports) {
        bool ok = false;
        if (fmt == "json") {
            ok = ProjectSerializer::saveToFile(QString::fromStdString(outPath), model);
        } else if (fmt == "csv") {
            ok = exportCsvFile(outPath, model);
        } else if (fmt == "vtk") {
            ok = exportVtkFile(outPath, model);
        } else if (fmt == "dxf") {
            ok = io::DxfInterop::exportToFile(outPath, model);
        } else if (fmt == "ifc") {
            ok = io::IfcInterop::exportToFile(outPath, model);
        } else {
            std::cerr << "Formato de exportacao nao suportado: " << fmt << "\n";
            return 1;
        }
        if (!ok) {
            std::cerr << "Falha ao exportar " << fmt << " para: " << outPath << "\n";
            return 1;
        }
    }

    for (const auto& [fmt, outPath] : reports) {
        bool ok = false;
        QString reportErr;
        if (fmt == "md" || fmt == "markdown") {
            ok = ProjectSerializer::exportTechnicalReportMarkdown(QString::fromStdString(outPath), model, staticRes);
        } else if (fmt == "html") {
            ok = ProjectSerializer::exportTechnicalReportHtml(QString::fromStdString(outPath), model, staticRes);
        } else if (fmt == "pdf") {
            io::PdfReportOptions pdfOpts;
            const auto& meta = model.metadata();
            if (!meta.projectName.empty() && meta.projectName != "Projeto OmniFrame 3D") {
                pdfOpts.projectTitle = QString::fromStdString(meta.projectName);
            } else if (!inputPath.empty()) {
                pdfOpts.projectTitle = QString("Memorial de Calculo - %1")
                    .arg(QFileInfo(QString::fromStdString(inputPath)).fileName());
            }
            pdfOpts.engineerName = QString::fromStdString(meta.engineerName);
            pdfOpts.clientName = QString::fromStdString(meta.clientName);
            pdfOpts.siteLocation = QString::fromStdString(meta.siteLocation);
            pdfOpts.soilSigmaAdm_kPa = meta.soilSigmaAdm_kPa;
            pdfOpts.footingB_m = meta.footingB_m;
            pdfOpts.footingL_m = meta.footingL_m;
            pdfOpts.windV0_ms = meta.windV0_ms;
            pdfOpts.windS1 = meta.windS1;
            pdfOpts.windS2 = meta.windS2;
            pdfOpts.windS3 = meta.windS3;
            ok = io::PdfReportGenerator::generateExecutivePdf(
                QString::fromStdString(outPath), model, staticRes, pdfOpts, &reportErr);
        } else {
            std::cerr << "Formato de relatorio nao suportado via CLI: " << fmt << "\n";
            return 1;
        }
        if (!ok) {
            if (!reportErr.isEmpty()) {
                std::cerr << "Detalhes do erro no relatorio: " << reportErr.toStdString() << "\n";
            }
            std::cerr << "Falha ao gerar relatorio " << fmt << " em: " << outPath << "\n";
            return 1;
        }
    }

    emitJson(root);
    return 0;
}
