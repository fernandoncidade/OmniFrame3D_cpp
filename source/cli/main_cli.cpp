#include "core/StructuralModel.hpp"
#include "io/DxfInterop.hpp"
#include "io/IfcInterop.hpp"
#include "io/PdfReportGenerator.hpp"
#include "io/ProjectSerializer.hpp"
#include "io/Utf8Path.hpp"
#include "os/WindowsCliIntegration.hpp"
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
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef OMNIFRAME_APP_VERSION
#define OMNIFRAME_APP_VERSION "2026.11.15.0"
#endif

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

bool createBuiltInExample(const std::string& rawName, StructuralModel& outModel, std::string& canonicalName) {
    const std::string key = toLowerAscii(rawName);
    if (key == "1" || key == "building" || key == "portico" || key == "edificio" || key == "frame") {
        outModel = StructuralModel::createBuildingFrameExample();
        canonicalName = "building";
        return true;
    }
    if (key == "2" || key == "cantilever" || key == "balanco" || key == "lframe") {
        outModel = StructuralModel::createCantileverLFrameExample();
        canonicalName = "cantilever";
        return true;
    }
    if (key == "3" || key == "truss" || key == "trelica" || key == "passarela") {
        outModel = StructuralModel::createSpaceTrussExample();
        canonicalName = "truss";
        return true;
    }
    return false;
}

void printUsage() {
    std::cout << "OmniFrame 3D CLI v" << OMNIFRAME_APP_VERSION << " (Kernel C++23 / Eigen 3.4.0 / Schema 2.1.0)\n"
              << "Uso no Windows 10/11 (CMD e PowerShell):\n"
              << "  omniframe-cli --version | -v\n"
              << "  omniframe-cli --help | -h\n"
              << "  omniframe-cli --list-examples [--json] [--output <caminho.json>]\n"
              << "  omniframe-cli --validate <arquivo.omniframe|dxf|ifc> [--output <caminho.json>] [--json]\n"
              << "  omniframe-cli --validate --example <building|cantilever|truss|1|2|3> [--output <caminho.json>]\n"
              << "  omniframe-cli --analyze <arquivo.omniframe|dxf|ifc> [opções]\n"
              << "  omniframe-cli --example <building|cantilever|truss|1|2|3> [opções]\n"
              << "  omniframe-cli --nbr-audit [<arquivo.omniframe|dxf|ifc>] [opções]\n"
              << "  omniframe-cli --check-windows-integration [--json]\n"
              << "  omniframe-cli --install-windows-integration [--json]\n"
              << "  omniframe-cli --uninstall-windows-integration [--json]\n\n"
              << "Opções de modelo e entrada:\n"
              << "  --validate [<arquivo>]          Valida topologia, conectividade, finitude e malhas 2D/3D (0=válido, 2=inválido)\n"
              << "  --analyze  [<arquivo>]          Executa análise estrutural sobre arquivo de entrada ou --example\n"
              << "  --example  <nome|1|2|3>         Usa modelo embutido (1=building, 2=cantilever, 3=truss)\n"
              << "  --list-examples                 Lista os 3 exemplos oficiais embutidos do OmniFrame 3D\n"
              << "  --nbr-audit | --audit-nbr       Executa auditoria ABNT NBR (6118/8800/6120/6122/6123/7480)\n\n"
              << "Opções de análise, solver e não linearidade:\n"
              << "  --analysis <tipo>               static | modal | buckling | nonlinear | pdelta | harmonic |\n"
              << "                                  spectrum | transient | seismic (padrão=static)\n"
              << "  --solver   <tipo>               auto | ldlt | llt | sparselu | cg | bicgstab (padrão=auto)\n"
              << "  --modes    <N>                  Número de modos (modal/buckling/spectrum, padrão=6)\n"
              << "  --steps    <N>                  Passos incrementais ou temporais (padrão=20)\n"
              << "  --dt       <seg>                Passo de tempo para transient/seismic em segundos (padrão=0.02)\n"
              << "  --scheme   <newmark|hht>        Integrador transiente/sísmico (Newmark-beta ou HHT-alpha, padrão=newmark)\n"
              << "  --case     <id>                 ID do Caso de Carga (static/transient)\n"
              << "  --combo    <id>                 ID da Combinação NBR (static)\n"
              << "  --method   <nr|arclength>       Método não linear: Newton-Raphson ou Arc-Length Crisfield (padrão=nr)\n"
              << "  --load-factor <fator>           Fator de carga alvo para nonlinear/pdelta (padrão=1.0)\n"
              << "  --full-tangent                  Atualiza matriz tangente Kt a cada iteração (Full NR, padrão)\n"
              << "  --modified-newton               Mantém Kt constante por incremento (Modified Newton)\n"
              << "  --line-search                   Habilita line search com retrocesso em NR (padrão)\n"
              << "  --no-line-search                Desabilita line search em NR\n"
              << "  --imperfection <amp_m>          Perturba geometria pelo modo de flambagem antes de resolver/exportar\n"
              << "  --imperfection-mode <N>         Modo de flambagem base 1 usado em --imperfection (padrão=1)\n\n"
              << "Opções de exportação, memorial e integração Windows:\n"
              << "  --export <fmt> <caminho>        Exporta modelo/resultados: json | csv | vtk | dxf | ifc\n"
              << "  --report <fmt> <caminho>        Gera memorial técnico: md | html | pdf\n"
              << "  --output <caminho.json>         Grava resumo JSON estruturado no disco\n"
              << "  --json                          Emite resumo JSON no stdout\n"
              << "  --check-windows-integration     Consulta PATH, App Paths e associação .omniframe no Windows 10/11\n"
              << "  --install-windows-integration   Registra omniframe-cli e .omniframe no HKCU do Windows (sem admin)\n"
              << "  --uninstall-windows-integration Remove o registro HKCU da CLI e associação .omniframe\n";
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
    if (!ProjectSerializer::loadFromFile(QString::fromUtf8(path), model)) {
        errOut = "Falha ao carregar projeto .omniframe: " + path;
        return false;
    }
    return true;
}

QJsonObject windowsStatusToJson(const os::CliIntegrationStatus& st) {
    QJsonObject j;
    j["isWindows"] = st.isWindows;
    j["cliBinaryExists"] = st.cliBinaryExists;
    j["guiBinaryExists"] = st.guiBinaryExists;
    j["installDirInUserPath"] = st.installDirInUserPath;
    j["installDirInSystemPath"] = st.installDirInSystemPath;
    j["installDirInProcessPath"] = st.installDirInProcessPath;
    j["appPathsCliRegistered"] = st.appPathsCliRegistered;
    j["appPathsGuiRegistered"] = st.appPathsGuiRegistered;
    j["fileAssocRegistered"] = st.fileAssocRegistered;
    j["readyForCmdAndPowerShell"] = st.isReadyForCmdAndPowerShell();
    j["installDirectory"] = st.installDirectory;
    j["cliBinaryPath"] = st.cliBinaryPath;
    j["guiBinaryPath"] = st.guiBinaryPath;
    return j;
}

QJsonObject nbrAuditToJson(const io::ModelNbrAudit& audit) {
    QJsonObject j;
    j["passedAll"] = audit.passedAll;
    j["maxDcr"] = audit.maxDcr;
    j["governingSummary"] = audit.governingSummary;
    j["steelMembersCount"] = static_cast<int>(audit.steelMembers.size());
    j["concreteBeamsCount"] = static_cast<int>(audit.concreteBeams.size());
    j["concreteColumnsCount"] = static_cast<int>(audit.concreteColumns.size());
    j["foundationsCount"] = static_cast<int>(audit.foundations.size());

    QJsonObject els;
    els["passed"] = audit.elsDisplacement.passed;
    els["dcrMax"] = audit.elsDisplacement.dcrMax;
    els["governingCriterion"] = QString::fromStdString(audit.elsDisplacement.governingCriterion);
    j["elsDisplacement"] = els;

    QJsonObject wind;
    wind["vk_ms"] = audit.sampleWind6123.vk_ms;
    wind["q_kPa"] = audit.sampleWind6123.q_kPa;
    j["wind6123"] = wind;

    QJsonArray covArr;
    for (const auto& c : audit.coverageMatrix) {
        QJsonObject cj;
        cj["code"] = QString::fromStdString(c.codeId);
        cj["title"] = QString::fromStdString(c.title);
        const char* statusStr = "outOfLocalPdfScope";
        if (c.status == kernel::codes::ComplianceScopeStatus::LocalPdfVerified) {
            statusStr = "localPdfVerified";
        } else if (c.status == kernel::codes::ComplianceScopeStatus::PartialReferenceOnly) {
            statusStr = "partialReferenceOnly";
        }
        cj["status"] = statusStr;
        cj["localPdfFile"] = QString::fromStdString(c.localPdfFile);
        cj["scopeNote"] = QString::fromStdString(c.scopeNote);
        covArr.append(cj);
    }
    j["coverageMatrix"] = covArr;
    return j;
}

const char* meshCellTypeName(kernel::MeshCellType t) {
    switch (t) {
        case kernel::MeshCellType::Line2: return "Line2";
        case kernel::MeshCellType::Tri3:  return "Tri3";
        case kernel::MeshCellType::Quad4: return "Quad4";
        case kernel::MeshCellType::Quad8: return "Quad8";
        case kernel::MeshCellType::Tet4:  return "Tet4";
        case kernel::MeshCellType::Tet10: return "Tet10";
        case kernel::MeshCellType::Hex8:  return "Hex8";
        case kernel::MeshCellType::Hex20: return "Hex20";
    }
    return "Unknown";
}

int vtkCellTypeCode(kernel::MeshCellType t) {
    switch (t) {
        case kernel::MeshCellType::Line2: return 3;  // VTK_LINE
        case kernel::MeshCellType::Tri3:  return 5;  // VTK_TRIANGLE
        case kernel::MeshCellType::Quad4: return 9;  // VTK_QUAD
        case kernel::MeshCellType::Quad8: return 23; // VTK_QUADRATIC_QUAD
        case kernel::MeshCellType::Tet4:  return 10; // VTK_TETRA
        case kernel::MeshCellType::Tet10: return 24; // VTK_QUADRATIC_TETRA
        case kernel::MeshCellType::Hex8:  return 12; // VTK_HEXAHEDRON
        case kernel::MeshCellType::Hex20: return 25; // VTK_QUADRATIC_HEXAHEDRON
    }
    return 1; // VTK_VERTEX
}

const char* issueSeverityName(IssueSeverity s) {
    switch (s) {
        case IssueSeverity::Error:   return "error";
        case IssueSeverity::Warning: return "warning";
        case IssueSeverity::Info:    return "info";
    }
    return "info";
}

bool exportCsvFile(const std::string& path, const StructuralModel& model) {
    std::ofstream os(io::pathFromUtf8(path), std::ios::binary);
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
    if (!model.surfaceMesh().nodes.empty()) {
        os << "\n# Continuum Surface/Solid Mesh Nodes\n";
        os << "MeshNodeID,X,Y,Z,UX,UY,UZ,RX,RY,RZ,FX_Reac,FY_Reac,FZ_Reac,MX_Reac,MY_Reac,MZ_Reac\n";
        for (const auto& mn : model.surfaceMesh().nodes) {
            os << mn.id << "," << std::setprecision(9)
               << mn.position.x() << "," << mn.position.y() << "," << mn.position.z();
            for (int d = 0; d < 6; ++d) os << "," << mn.displacement[d];
            for (int d = 0; d < 6; ++d) os << "," << mn.reaction[d];
            os << "\n";
        }
    }
    if (!model.surfaceMesh().elements.empty()) {
        os << "\n# Continuum 2D/3D Element Stresses & Resultants\n";
        os << "ElementID,CellType,HasResults,VonMises_kPa,Sigma1_kPa,Sigma3_kPa,"
              "Nxx,Nyy,Nxy,Mxx,Myy,Mxy,Sxx,Syy,Szz,Txy,Tyz,Tzx\n";
        for (const auto& me : model.surfaceMesh().elements) {
            os << me.id << "," << meshCellTypeName(me.type) << "," << (me.hasResults ? 1 : 0) << ","
               << std::setprecision(9)
               << me.vonMisesStress << "," << me.maxPrincipalStress << "," << me.minPrincipalStress << ","
               << me.membraneResultants[0] << "," << me.membraneResultants[1] << "," << me.membraneResultants[2] << ","
               << me.bendingResultants[0] << "," << me.bendingResultants[1] << "," << me.bendingResultants[2] << ","
               << me.stressVoigt[0] << "," << me.stressVoigt[1] << "," << me.stressVoigt[2] << ","
               << me.stressVoigt[3] << "," << me.stressVoigt[4] << "," << me.stressVoigt[5] << "\n";
        }
    }
    return os.good();
}

bool exportVtkFile(const std::string& path, const StructuralModel& model) {
    std::ofstream os(io::pathFromUtf8(path), std::ios::binary);
    if (!os.is_open()) return false;

    struct VtkPoint {
        double x = 0.0, y = 0.0, z = 0.0;
        std::array<double, 3> disp{0.0, 0.0, 0.0};
        std::array<double, 3> reac{0.0, 0.0, 0.0};
        bool hasResults = false;
    };
    struct VtkCell {
        int vtkType = 3;
        std::vector<int> ptIds;
        double vonMises = 0.0;
        double sigma1 = 0.0;
        double sigma3 = 0.0;
    };

    std::vector<VtkPoint> points;
    const auto& frameNodes = model.getNodes();
    const auto& frameElems = model.getElements();
    const auto& mesh = model.surfaceMesh();
    points.reserve(frameNodes.size() + mesh.nodes.size());

    std::unordered_map<NodeId, int> frameIdxMap;
    for (int i = 0; i < static_cast<int>(frameNodes.size()); ++i) {
        const auto& n = frameNodes[i];
        frameIdxMap[n.id] = static_cast<int>(points.size());
        const bool nodeHasData = (n.displacement[0] != 0.0 || n.displacement[1] != 0.0 || n.displacement[2] != 0.0 ||
                                  n.reaction[0] != 0.0 || n.reaction[1] != 0.0 || n.reaction[2] != 0.0);
        points.push_back({
            n.position.x, n.position.y, n.position.z,
            {n.displacement[0], n.displacement[1], n.displacement[2]},
            {n.reaction[0], n.reaction[1], n.reaction[2]},
            nodeHasData
        });
    }

    std::unordered_map<NodeId, int> meshIdxMap;
    for (const auto& mn : mesh.nodes) {
        int matched = -1;
        for (int idx = 0; idx < static_cast<int>(frameNodes.size()); ++idx) {
            const auto& fn = frameNodes[idx];
            const double dx = mn.position.x() - fn.position.x;
            const double dy = mn.position.y() - fn.position.y;
            const double dz = mn.position.z() - fn.position.z;
            if (dx * dx + dy * dy + dz * dz <= 1e-12) {
                matched = idx;
                break;
            }
        }
        if (matched >= 0) {
            meshIdxMap[mn.id] = matched;
            if (mn.hasResults && !points[matched].hasResults) {
                points[matched].disp = {mn.displacement[0], mn.displacement[1], mn.displacement[2]};
                points[matched].reac = {mn.reaction[0], mn.reaction[1], mn.reaction[2]};
                points[matched].hasResults = true;
            }
        } else {
            const int newIdx = static_cast<int>(points.size());
            meshIdxMap[mn.id] = newIdx;
            points.push_back({
                mn.position.x(), mn.position.y(), mn.position.z(),
                {mn.displacement[0], mn.displacement[1], mn.displacement[2]},
                {mn.reaction[0], mn.reaction[1], mn.reaction[2]},
                mn.hasResults
            });
        }
    }

    std::vector<VtkCell> cells;
    cells.reserve(frameElems.size() + mesh.elements.size());
    int totalCellListInts = 0;

    for (const auto& e : frameElems) {
        const auto itA = frameIdxMap.find(e.nodeStart);
        const auto itB = frameIdxMap.find(e.nodeEnd);
        if (itA == frameIdxMap.end() || itB == frameIdxMap.end()) continue;
        cells.push_back({3, {itA->second, itB->second}, 0.0, 0.0, 0.0});
        totalCellListInts += 3;
    }

    for (const auto& me : mesh.elements) {
        std::vector<int> pts;
        pts.reserve(me.nodeIds.size());
        bool valid = !me.nodeIds.empty();
        for (NodeId nid : me.nodeIds) {
            const auto it = meshIdxMap.find(nid);
            if (it == meshIdxMap.end()) {
                valid = false;
                break;
            }
            pts.push_back(it->second);
        }
        if (!valid) continue;
        totalCellListInts += 1 + static_cast<int>(pts.size());
        cells.push_back({
            vtkCellTypeCode(me.type),
            std::move(pts),
            me.vonMisesStress,
            me.maxPrincipalStress,
            me.minPrincipalStress
        });
    }

    os << "# vtk DataFile Version 3.0\n"
       << "OmniFrame 3D Unified 1D/2D/3D Structural Analysis Export\n"
       << "ASCII\n"
       << "DATASET UNSTRUCTURED_GRID\n";
    os << "POINTS " << points.size() << " double\n";
    for (const auto& pt : points) {
        os << std::setprecision(10) << pt.x << " " << pt.y << " " << pt.z << "\n";
    }
    os << "CELLS " << cells.size() << " " << totalCellListInts << "\n";
    for (const auto& c : cells) {
        os << c.ptIds.size();
        for (int pid : c.ptIds) os << " " << pid;
        os << "\n";
    }
    os << "CELL_TYPES " << cells.size() << "\n";
    for (const auto& c : cells) {
        os << c.vtkType << "\n";
    }
    os << "POINT_DATA " << points.size() << "\n";
    os << "VECTORS Displacement double\n";
    for (const auto& pt : points) {
        os << std::setprecision(10) << pt.disp[0] << " " << pt.disp[1] << " " << pt.disp[2] << "\n";
    }
    os << "SCALARS DisplacementMagnitude double 1\n"
       << "LOOKUP_TABLE default\n";
    for (const auto& pt : points) {
        const double mag = std::sqrt(pt.disp[0] * pt.disp[0] + pt.disp[1] * pt.disp[1] + pt.disp[2] * pt.disp[2]);
        os << std::setprecision(10) << mag << "\n";
    }
    os << "VECTORS ReactionForce double\n";
    for (const auto& pt : points) {
        os << std::setprecision(10) << pt.reac[0] << " " << pt.reac[1] << " " << pt.reac[2] << "\n";
    }
    if (!cells.empty()) {
        os << "CELL_DATA " << cells.size() << "\n";
        os << "SCALARS von_mises_kPa double 1\nLOOKUP_TABLE default\n";
        for (const auto& c : cells) os << std::setprecision(10) << c.vonMises << "\n";
        os << "SCALARS sigma1_kPa double 1\nLOOKUP_TABLE default\n";
        for (const auto& c : cells) os << std::setprecision(10) << c.sigma1 << "\n";
        os << "SCALARS sigma3_kPa double 1\nLOOKUP_TABLE default\n";
        for (const auto& c : cells) os << std::setprecision(10) << c.sigma3 << "\n";
    }
    return os.good();
}

} // namespace

int main(int argc, char* argv[]) {
    os::configureConsoleUtf8();
    const std::vector<std::string> args = os::commandLineArgumentsUtf8(argc, argv);

    bool needsPdfGui = false;
    for (size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--report" && toLowerAscii(args[i + 1]) == "pdf") {
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
    QCoreApplication::setApplicationName(QStringLiteral("OmniFrame"));
    QCoreApplication::setOrganizationName(QStringLiteral("OmniFrame"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OMNIFRAME_APP_VERSION));

    if (args.size() <= 1) {
        printUsage();
        return 1;
    }

    bool doValidate = false;
    bool doAnalyze = false;
    bool doNbrAudit = false;
    bool listExamples = false;
    bool checkWinInt = false;
    bool installWinInt = false;
    bool uninstallWinInt = false;
    bool jsonStdout = false;
    bool fullNewtonTangent = true;
    bool enableLineSearch = true;
    std::string inputPath;
    std::string exampleName;
    std::string analysisType = "static";
    std::string solverStr = "auto";
    std::string nlMethodStr = "nr";
    std::string transientSchemeStr = "newmark";
    int modesCount = 6;
    int stepsCount = 20;
    double dtSec = 0.02;
    int caseId = -1;
    int comboId = -1;
    double targetLoadFactor = 1.0;
    double imperfectionAmp = 0.0;
    int imperfectionMode = 1;
    std::string outputJsonPath;
    std::vector<std::pair<std::string, std::string>> exports;
    std::vector<std::pair<std::string, std::string>> reports;

    auto nextIsValue = [&](size_t idx) -> bool {
        return idx + 1 < args.size() && !args[idx + 1].empty() && args[idx + 1][0] != '-';
    };

    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--version" || arg == "-v") {
            std::cout << "OmniFrame 3D CLI v" << OMNIFRAME_APP_VERSION
                      << " (C++23 / Eigen 3.4.0 / Schema 2.1.0)\n";
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--json") {
            jsonStdout = true;
        } else if (arg == "--list-examples") {
            listExamples = true;
        } else if (arg == "--check-windows-integration") {
            checkWinInt = true;
        } else if (arg == "--install-windows-integration") {
            installWinInt = true;
        } else if (arg == "--uninstall-windows-integration") {
            uninstallWinInt = true;
        } else if (arg == "--nbr-audit" || arg == "--audit-nbr") {
            doNbrAudit = true;
            if (nextIsValue(i) && inputPath.empty()) {
                inputPath = args[++i];
            }
        } else if (arg == "--example" && i + 1 < args.size()) {
            exampleName = args[++i];
        } else if (arg == "--validate") {
            doValidate = true;
            if (nextIsValue(i)) {
                inputPath = args[++i];
            }
        } else if (arg == "--analyze") {
            doAnalyze = true;
            if (nextIsValue(i)) {
                inputPath = args[++i];
            }
        } else if (arg == "--analysis" && i + 1 < args.size()) {
            analysisType = toLowerAscii(args[++i]);
        } else if (arg == "--solver" && i + 1 < args.size()) {
            solverStr = toLowerAscii(args[++i]);
        } else if (arg == "--method" && i + 1 < args.size()) {
            nlMethodStr = toLowerAscii(args[++i]);
        } else if (arg == "--scheme" && i + 1 < args.size()) {
            transientSchemeStr = toLowerAscii(args[++i]);
        } else if (arg == "--modes" && i + 1 < args.size()) {
            modesCount = std::max(1, std::stoi(args[++i]));
        } else if (arg == "--steps" && i + 1 < args.size()) {
            stepsCount = std::max(1, std::stoi(args[++i]));
        } else if (arg == "--dt" && i + 1 < args.size()) {
            dtSec = std::max(1e-5, std::stod(args[++i]));
        } else if (arg == "--case" && i + 1 < args.size()) {
            caseId = std::stoi(args[++i]);
        } else if (arg == "--combo" && i + 1 < args.size()) {
            comboId = std::stoi(args[++i]);
        } else if (arg == "--load-factor" && i + 1 < args.size()) {
            targetLoadFactor = std::stod(args[++i]);
        } else if (arg == "--full-tangent") {
            fullNewtonTangent = true;
        } else if (arg == "--modified-newton") {
            fullNewtonTangent = false;
        } else if (arg == "--line-search") {
            enableLineSearch = true;
        } else if (arg == "--no-line-search") {
            enableLineSearch = false;
        } else if (arg == "--imperfection" && i + 1 < args.size()) {
            imperfectionAmp = std::stod(args[++i]);
        } else if (arg == "--imperfection-mode" && i + 1 < args.size()) {
            imperfectionMode = std::max(1, std::stoi(args[++i]));
        } else if (arg == "--output" && i + 1 < args.size()) {
            outputJsonPath = args[++i];
        } else if (arg == "--export" && i + 2 < args.size()) {
            const std::string fmt = toLowerAscii(args[++i]);
            const std::string outP = args[++i];
            exports.emplace_back(fmt, outP);
        } else if (arg == "--report" && i + 2 < args.size()) {
            const std::string fmt = toLowerAscii(args[++i]);
            const std::string outP = args[++i];
            reports.emplace_back(fmt, outP);
        } else {
            std::cerr << "Argumento desconhecido ou incompleto: " << arg << "\n";
            return 1;
        }
    }

    auto writeJsonFile = [&](const QJsonObject& obj) {
        if (!outputJsonPath.empty()) {
            QFile f(QString::fromUtf8(outputJsonPath));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
            }
        }
    };

    if (listExamples) {
        const StructuralModel ex1 = StructuralModel::createBuildingFrameExample();
        const StructuralModel ex2 = StructuralModel::createCantileverLFrameExample();
        const StructuralModel ex3 = StructuralModel::createSpaceTrussExample();

        QJsonArray arr;
        auto addEx = [&](int id, const char* key, const char* label, const StructuralModel& m) {
            QJsonObject o;
            o["id"] = id;
            o["name"] = QString::fromLatin1(key);
            o["title"] = QString::fromUtf8(label);
            o["nodes"] = static_cast<int>(m.getNodes().size());
            o["elements"] = static_cast<int>(m.getElements().size());
            o["loadCases"] = static_cast<int>(m.getLoadCases().size());
            o["loadCombinations"] = static_cast<int>(m.getLoadCombinations().size());
            arr.append(o);
        };
        addEx(1, "building", "Pórtico Espacial de Edifício (2 Pavimentos, Vento e Sobrecarga)", ex1);
        addEx(2, "cantilever", "Balanço Espacial em L Sujeito a Flexo-Torção", ex2);
        addEx(3, "truss", "Treliça Espacial / Passarela Metálica 3D", ex3);

        QJsonObject out;
        out["success"] = true;
        out["schemaVersion"] = QStringLiteral("2.1.0");
        out["examples"] = arr;
        writeJsonFile(out);

        if (jsonStdout) {
            std::cout << QJsonDocument(out).toJson(QJsonDocument::Indented).toStdString();
        } else {
            std::cout << "Exemplos embutidos do OmniFrame 3D:\n"
                      << "  1 | building   — Pórtico Espacial de Edifício (" << ex1.getNodes().size()
                      << " nós, " << ex1.getElements().size() << " barras)\n"
                      << "  2 | cantilever — Balanço Espacial em L (" << ex2.getNodes().size()
                      << " nós, " << ex2.getElements().size() << " barras)\n"
                      << "  3 | truss      — Treliça Espacial 3D (" << ex3.getNodes().size()
                      << " nós, " << ex3.getElements().size() << " barras)\n";
        }
        return 0;
    }

    if (installWinInt || uninstallWinInt || checkWinInt) {
        QString details;
        bool ok = true;
        std::string action = "check";
        if (installWinInt) {
            action = "install";
            ok = os::installWindowsCliIntegration(QString(), &details);
        } else if (uninstallWinInt) {
            action = "uninstall";
            ok = os::uninstallWindowsCliIntegration(QString(), &details);
        }
        const os::CliIntegrationStatus st = os::queryWindowsCliIntegration();
        QJsonObject out = windowsStatusToJson(st);
        out["action"] = QString::fromLatin1(action.c_str());
        out["success"] = ok;
        if (!details.isEmpty()) {
            out["details"] = details;
        }
        writeJsonFile(out);

        if (jsonStdout) {
            std::cout << QJsonDocument(out).toJson(QJsonDocument::Indented).toStdString();
        } else {
            std::cout << "Integração Windows 10/11 (CMD & PowerShell) [" << action << "]: "
                      << (ok ? "OK" : "FALHA") << "\n"
                      << "  Diretório instalado:       " << st.installDirectory.toStdString() << "\n"
                      << "  omniframe-cli.exe existe:  " << (st.cliBinaryExists ? "Sim" : "Não") << "\n"
                      << "  OmniFrame.exe existe:      " << (st.guiBinaryExists ? "Sim" : "Não") << "\n"
                      << "  PATH do usuário (HKCU):    " << (st.installDirInUserPath ? "Sim" : "Não") << "\n"
                      << "  PATH do sistema (HKLM):    " << (st.installDirInSystemPath ? "Sim" : "Não") << "\n"
                      << "  App Paths omniframe-cli:   " << (st.appPathsCliRegistered ? "Sim" : "Não") << "\n"
                      << "  App Paths OmniFrame.exe:   " << (st.appPathsGuiRegistered ? "Sim" : "Não") << "\n"
                      << "  Associação .omniframe:     " << (st.fileAssocRegistered ? "Sim" : "Não") << "\n"
                      << "  Pronto no CMD/PowerShell:  " << (st.isReadyForCmdAndPowerShell() ? "Sim" : "Não") << "\n";
            if (!details.isEmpty()) {
                std::cout << "  Detalhes:                  " << details.toStdString() << "\n";
            }
        }
        return ok ? 0 : 1;
    }

    if (doNbrAudit && !doValidate && !doAnalyze && (!inputPath.empty() || !exampleName.empty())) {
        doAnalyze = true;
    }
    if (!exampleName.empty() && !doValidate && !doAnalyze) {
        doAnalyze = true;
    }

    if ((inputPath.empty() && exampleName.empty()) || (!doValidate && !doAnalyze)) {
        printUsage();
        return 1;
    }

    StructuralModel model;
    std::string canonicalExample;
    if (!exampleName.empty()) {
        if (!createBuiltInExample(exampleName, model, canonicalExample)) {
            std::cerr << "{\"success\":false,\"error\":\"Exemplo desconhecido: " << exampleName
                      << " (use building, cantilever, truss ou 1, 2, 3)\"}\n";
            return 1;
        }
    } else {
        std::string loadErr;
        if (!loadModelAnyFormat(inputPath, model, loadErr)) {
            std::cerr << "{\"success\":false,\"error\":\"" << loadErr << "\"}\n";
            return 1;
        }
    }

    const ModelValidation val = model.validate();
    QJsonObject root;
    root["schemaVersion"] = "2.1.0";
    root["inputFile"] = !inputPath.empty()
        ? QString::fromUtf8(inputPath)
        : QString("example:%1").arg(QString::fromStdString(canonicalExample));
    if (!canonicalExample.empty()) {
        root["exampleName"] = QString::fromStdString(canonicalExample);
    }
    root["nodes"] = static_cast<int>(model.getNodes().size());
    root["elements"] = static_cast<int>(model.getElements().size());
    root["surfaceNodes"] = static_cast<int>(model.surfaceMesh().nodes.size());
    root["surfaceElements"] = static_cast<int>(model.surfaceMesh().elements.size());
    root["valid"] = val.valid;

    QJsonArray errArr, warnArr, issuesArr;
    for (const auto& e : val.errors) errArr.append(QString::fromStdString(e));
    for (const auto& w : val.warnings) warnArr.append(QString::fromStdString(w));
    for (const auto& iss : val.issues) {
        QJsonObject ij;
        ij["severity"] = QString::fromLatin1(issueSeverityName(iss.severity));
        ij["code"] = QString::fromStdString(iss.code);
        ij["category"] = QString::fromStdString(iss.category);
        ij["entityId"] = QString::fromStdString(iss.entityId);
        ij["message"] = QString::fromStdString(iss.message);
        issuesArr.append(ij);
    }
    root["validationErrors"] = errArr;
    root["validationWarnings"] = warnArr;
    root["validationIssues"] = issuesArr;

    auto emitJson = [&](const QJsonObject& obj) {
        writeJsonFile(obj);
        if (jsonStdout || !doAnalyze) {
            const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
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

    if (std::abs(imperfectionAmp) > 0.0) {
        kernel::BucklingOptions preBo;
        preBo.numModes = std::max(modesCount, imperfectionMode);
        const auto preBRes = FemSolver3D::solveBuckling(model, preBo);
        std::string impErr;
        if (!FemSolver3D::applyBucklingImperfection(
                model, preBRes, imperfectionMode - 1, imperfectionAmp, &impErr)) {
            root["success"] = false;
            root["error"] = QString::fromStdString(
                impErr.empty() ? "Falha ao aplicar imperfeição geométrica de flambagem." : impErr);
            emitJson(root);
            return 2;
        }
        QJsonObject impObj;
        impObj["amplitudeM"] = imperfectionAmp;
        impObj["mode"] = imperfectionMode;
        if (imperfectionMode - 1 < static_cast<int>(preBRes.loadFactors.size())) {
            impObj["criticalLoadFactor"] = preBRes.loadFactors[static_cast<size_t>(imperfectionMode - 1)];
        }
        root["appliedImperfection"] = impObj;
    }

    SolverResult staticRes;
    std::optional<kernel::ModalResult> modalResOpt;
    std::optional<kernel::BucklingResult> bucklingResOpt;
    std::optional<kernel::NonlinearResult> nlResOpt;
    bool analysisSuccess = false;
    root["analysisType"] = QString::fromStdString(analysisType);

    if (analysisType == "static") {
        staticRes = FemSolver3D::solveWithOptions(model, linOpts);
        analysisSuccess = staticRes.success;
        root["success"] = staticRes.success;
        root["error"] = QString::fromStdString(staticRes.errorMessage);
        root["totalDofs"] = staticRes.totalDofs;
        root["restrainedDofs"] = staticRes.restrainedDofs;
        root["totalSurfaceNodes"] = staticRes.totalSurfaceNodes;
        root["totalSurfaceElements"] = staticRes.totalSurfaceElements;
        root["maxNodalDisplacement"] = staticRes.maxNodalDisplacement;
        root["maxShellVonMises"] = staticRes.maxShellVonMises;
        root["maxSolidVonMises"] = staticRes.maxSolidVonMises;
        root["totalStrainEnergy"] = staticRes.totalStrainEnergy;
        root["equilibriumBalanced"] = staticRes.equilibrium.isBalanced;
        root["relativeResidual"] = staticRes.solverStats.relativeResidual;
        root["solverMessage"] = QString::fromStdString(staticRes.solverStats.message);
        root["usedFallback"] = (staticRes.solverStats.message.find("Fallback") != std::string::npos ||
                                staticRes.solverStats.message.find("fallback") != std::string::npos);
        root["executionTimeMs"] = staticRes.executionTimeMs;

        QJsonArray contArr;
        for (const auto& cr : staticRes.continuumElementResults) {
            QJsonObject cj;
            cj["elementId"] = cr.elementId;
            cj["cellType"] = QString::fromStdString(cr.cellTypeName);
            cj["isShell"] = cr.isShell;
            cj["isSolid"] = cr.isSolid;
            cj["vonMisesStress"] = cr.vonMisesStress;
            cj["maxPrincipalStress"] = cr.maxPrincipalStress;
            cj["minPrincipalStress"] = cr.minPrincipalStress;
            contArr.append(cj);
        }
        root["continuumElements"] = contArr;

        if (staticRes.success && (caseId > 0 || comboId > 0)) {
            std::vector<kernel::CaseDisplacementResult> caseRes, comboRes;
            kernel::EnvelopeExtremes env;
            std::string ccErr;
            if (!FemSolver3D::solveLoadCasesAndCombinations(model, linOpts, caseRes, comboRes, env, &ccErr)) {
                analysisSuccess = false;
                root["success"] = false;
                root["error"] = QString::fromStdString(ccErr.empty() ? "Falha ao resolver casos/combinacoes" : ccErr);
            } else {
                const kernel::CaseDisplacementResult* chosen = nullptr;
                if (comboId > 0) {
                    for (const auto& c : comboRes) {
                        if (c.caseOrComboId == comboId) { chosen = &c; break; }
                    }
                } else {
                    for (const auto& c : caseRes) {
                        if (c.caseOrComboId == caseId) { chosen = &c; break; }
                    }
                }
                if (!chosen) {
                    analysisSuccess = false;
                    root["success"] = false;
                    root["error"] = QString("Caso/Combinacao especificado (%1) nao encontrado").arg(comboId > 0 ? comboId : caseId);
                } else {
                    const auto field = FemSolver3D::extractNodalDisplacements(model, chosen->U_full);
                    for (auto& fn : model.getNodes()) {
                        const auto it = field.frameNodes.find(fn.id);
                        if (it != field.frameNodes.end()) {
                            fn.displacement[0] = it->second.x;
                            fn.displacement[1] = it->second.y;
                            fn.displacement[2] = it->second.z;
                        }
                    }
                    for (auto& mn : model.surfaceMesh().nodes) {
                        const auto it = field.meshNodes.find(mn.id);
                        if (it != field.meshNodes.end()) {
                            mn.displacement[0] = it->second.x;
                            mn.displacement[1] = it->second.y;
                            mn.displacement[2] = it->second.z;
                            mn.hasResults = true;
                        }
                    }
                    root["selectedCaseOrComboId"] = chosen->caseOrComboId;
                    root["selectedCaseOrComboName"] = QString::fromStdString(chosen->name);
                    root["selectedCaseOrComboMaxDisp"] = field.maxTranslationalNorm;
                }
            }
        }
    } else if (analysisType == "modal") {
        kernel::ModalOptions mo;
        mo.numModes = modesCount;
        modalResOpt = FemSolver3D::solveModal(model, mo);
        const auto& mRes = *modalResOpt;
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
        bucklingResOpt = FemSolver3D::solveBuckling(model, bo);
        const auto& bRes = *bucklingResOpt;
        analysisSuccess = bRes.success;
        root["success"] = bRes.success;
        root["error"] = QString::fromStdString(bRes.errorMessage);
        QJsonArray factors;
        for (double lam : bRes.loadFactors) factors.append(lam);
        root["criticalLoadFactors"] = factors;
    } else if (analysisType == "nonlinear" || analysisType == "pdelta") {
        kernel::NonlinearOptions nlo;
        nlo.numSteps = stepsCount;
        nlo.targetLoadFactor = targetLoadFactor;
        nlo.fullNewtonTangent = fullNewtonTangent;
        nlo.enableLineSearch = enableLineSearch;
        nlo.linearSolver = linOpts;
        nlo.method = (analysisType == "pdelta")
            ? kernel::NonlinearMethod::PDelta
            : ((nlMethodStr == "arclength")
                ? kernel::NonlinearMethod::ArcLengthCrisfield
                : kernel::NonlinearMethod::NewtonRaphson);
        nlo.kinematics = (analysisType == "pdelta")
            ? kernel::NonlinearKinematics::PDeltaSecondOrder
            : kernel::NonlinearKinematics::Corotational3D;
        nlResOpt = FemSolver3D::solveNonlinear(model, nlo);
        const auto& nlRes = *nlResOpt;
        analysisSuccess = nlRes.success;
        root["success"] = nlRes.success;
        root["error"] = QString::fromStdString(nlRes.errorMessage);
        root["finalLoadFactor"] = nlRes.achievedLoadFactor;
        root["totalSteps"] = nlRes.totalSteps;
        root["totalIterations"] = nlRes.totalIterations;
        root["fullNewtonTangent"] = fullNewtonTangent;
        root["enableLineSearch"] = enableLineSearch;
        bool yieldedAny = false;
        for (const auto& stepRec : nlRes.history) {
            if (stepRec.anyYielded) { yieldedAny = true; break; }
        }
        root["anyYielded"] = yieldedAny;
        root["convergenceDiagnostic"] = QString::fromStdString(nlRes.convergenceDiagnostic);
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
    } else if (analysisType == "transient" || analysisType == "seismic") {
        kernel::TransientOptions topts;
        topts.dt = dtSec;
        topts.numSteps = stepsCount;
        topts.scheme = (transientSchemeStr == "hht")
            ? kernel::TransientScheme::HHTAlpha
            : kernel::TransientScheme::NewmarkBeta;
        kernel::TransientResult tRes;
        if (analysisType == "seismic") {
            tRes = FemSolver3D::solveSeismic(
                model,
                [](double t, int) {
                    return kernel::Vec3(
                        1.5 * std::sin(2.0 * 3.14159265358979323846 * 2.5 * t) * std::exp(-0.4 * t),
                        0.0, 0.0);
                },
                topts);
        } else if (!model.getLoadHistories().empty()) {
            const kernel::LoadHistory* selectedHist = &model.getLoadHistories().front();
            if (caseId > 0) {
                for (const auto& lh : model.getLoadHistories()) {
                    if (lh.loadCaseId == caseId || lh.id == caseId) {
                        selectedHist = &lh;
                        break;
                    }
                }
            }
            tRes = FemSolver3D::solveTransient(model, *selectedHist, topts);
        } else {
            const kernel::LoadCaseId targetCase = (caseId > 0) ? caseId : 0;
            tRes = FemSolver3D::solveTransient(
                model,
                [](double t, int) {
                    return std::sin(2.0 * 3.14159265358979323846 * 2.0 * t);
                },
                targetCase,
                topts);
        }
        analysisSuccess = tRes.success;
        root["success"] = tRes.success;
        root["error"] = QString::fromStdString(tRes.errorMessage);
        root["steps"] = static_cast<int>(tRes.history.size());
        root["dt"] = dtSec;
        root["scheme"] = QString::fromLatin1(
            topts.scheme == kernel::TransientScheme::HHTAlpha ? "hht" : "newmark");

        double peakDispNorm = 0.0;
        const kernel::VecX* peakVec = nullptr;
        for (const auto& rec : tRes.history) {
            const auto field = FemSolver3D::extractNodalDisplacements(model, rec.U_full);
            if (field.maxTranslationalNorm >= peakDispNorm) {
                peakDispNorm = field.maxTranslationalNorm;
                peakVec = &rec.U_full;
            }
        }
        if (peakVec == nullptr && tRes.peakDisplacementFull.size() > 0) {
            peakVec = &tRes.peakDisplacementFull;
            peakDispNorm = FemSolver3D::extractNodalDisplacements(model, *peakVec).maxTranslationalNorm;
        }
        if (peakVec != nullptr) {
            const auto peakField = FemSolver3D::extractNodalDisplacements(model, *peakVec);
            for (auto& fn : model.getNodes()) {
                const auto it = peakField.frameNodes.find(fn.id);
                if (it != peakField.frameNodes.end()) {
                    fn.displacement[0] = it->second.x;
                    fn.displacement[1] = it->second.y;
                    fn.displacement[2] = it->second.z;
                }
            }
            for (auto& mn : model.surfaceMesh().nodes) {
                const auto it = peakField.meshNodes.find(mn.id);
                if (it != peakField.meshNodes.end()) {
                    mn.displacement[0] = it->second.x;
                    mn.displacement[1] = it->second.y;
                    mn.displacement[2] = it->second.z;
                    mn.hasResults = true;
                }
            }
        }
        root["peakDispNorm"] = peakDispNorm;
    } else {
        std::cerr << "Tipo de analise desconhecido: " << analysisType << "\n";
        return 1;
    }

    if (!analysisSuccess) {
        emitJson(root);
        return 2;
    }

    // Preserva deslocamentos de transient/seismic ou --case/--combo ao exportar, resolvendo estatico em copia quando necessario para relatorio ou auditoria NBR
    StructuralModel reportModel = model;
    if (analysisType != "static" && (!reports.empty() || doNbrAudit)) {
        staticRes = FemSolver3D::solveWithOptions(reportModel, linOpts);
    }

    io::PdfReportOptions pdfOpts;
    {
        const auto& meta = reportModel.metadata();
        if (!meta.projectName.empty() && meta.projectName != "Projeto OmniFrame 3D") {
            pdfOpts.projectTitle = QString::fromStdString(meta.projectName);
        } else if (!inputPath.empty()) {
            pdfOpts.projectTitle = QString("Memorial de Calculo - %1")
                .arg(QFileInfo(QString::fromUtf8(inputPath)).fileName());
        } else if (!canonicalExample.empty()) {
            pdfOpts.projectTitle = QString("Memorial de Calculo - Exemplo %1")
                .arg(QString::fromStdString(canonicalExample));
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
        pdfOpts.modalResult = modalResOpt ? &(*modalResOpt) : nullptr;
        pdfOpts.bucklingResult = bucklingResOpt ? &(*bucklingResOpt) : nullptr;
        pdfOpts.nonlinearResult = nlResOpt ? &(*nlResOpt) : nullptr;
    }

    if (doNbrAudit) {
        const io::ModelNbrAudit audit =
            io::PdfReportGenerator::evaluateModelNbrCompliance(reportModel, staticRes, pdfOpts);
        root["nbrAudit"] = nbrAuditToJson(audit);
    }

    for (const auto& [fmt, outPath] : exports) {
        bool ok = false;
        if (fmt == "json") {
            ok = ProjectSerializer::saveToFile(QString::fromUtf8(outPath), model);
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
            ok = ProjectSerializer::exportTechnicalReportMarkdown(QString::fromUtf8(outPath), reportModel, staticRes);
        } else if (fmt == "html") {
            ok = ProjectSerializer::exportTechnicalReportHtml(QString::fromUtf8(outPath), reportModel, staticRes);
        } else if (fmt == "pdf") {
            ok = io::PdfReportGenerator::generateExecutivePdf(
                QString::fromUtf8(outPath), reportModel, staticRes, pdfOpts, &reportErr);
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
