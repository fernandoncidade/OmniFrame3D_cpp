#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/DxfInterop.hpp"
#include "io/IfcInterop.hpp"
#include "io/ProjectSerializer.hpp"
#include "kernel/assembly/GlobalAssembler.hpp"
#include "kernel/core/DofManager.hpp"
#include "kernel/elements/SolidElements.hpp"
#include "kernel/materials/LinearElasticMaterial.hpp"
#include "kernel/mesh/MeshGenerator.hpp"
#include "kernel/solver/LinearSolverSuite.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace omniframe;
using namespace omniframe::kernel;
using KVec3 = omniframe::kernel::Vec3;

namespace {

void testMeshGenerator1D2D3DAndExtrusionSolve() {
    // 1. Linha 1D
    const auto line = MeshGenerator::generateLineMesh(KVec3(0.0, 0.0, 0.0), KVec3(6.0, 0.0, 0.0), 6);
    TEST_ASSERT(line.nodes.size() == 7, "Malha 1D com 6 subdivisoes deve ter 7 nos");
    TEST_ASSERT(line.elements.size() == 6, "Malha 1D deve ter 6 elementos");

    // 2. Malhas 2D estruturadas (Quad4, Quad8, Tri3) e metricas de qualidade
    const auto q4 = MeshGenerator::generateStructuredQuadGrid(
        KVec3::Zero(), KVec3(4.0, 0.0, 0.0), KVec3(0.0, 3.0, 0.0), 4, 3, false);
    const auto q4Qual = MeshGenerator::evaluateQuality(q4);
    TEST_ASSERT(q4Qual.isValid() && q4Qual.totalElements == 12 && std::abs(q4Qual.minJacobian - 0.25) < 1e-9,
                "Quad4 estruturada deve ter qualidade valida e Jacobiano positivo (0.25)");

    const auto q8 = MeshGenerator::generateStructuredQuadGrid(
        KVec3::Zero(), KVec3(4.0, 0.0, 0.0), KVec3(0.0, 3.0, 0.0), 2, 2, true);
    const auto q8Qual = MeshGenerator::evaluateQuality(q8);
    TEST_ASSERT(q8Qual.isValid() && q8.elements.size() == 4 && q8.elements[0].nodeIds.size() == 8,
                "Quad8 estruturada deve ter 8 nos por elemento e qualidade valida");

    const auto t3 = MeshGenerator::generateStructuredTriGrid(
        KVec3::Zero(), KVec3(4.0, 0.0, 0.0), KVec3(0.0, 3.0, 0.0), 4, 3, true);
    const auto t3Qual = MeshGenerator::evaluateQuality(t3);
    TEST_ASSERT(t3Qual.isValid() && t3Qual.totalElements == 24 && t3Qual.minJacobian > 0.25,
                "Tri3 estruturada deve ter 24 triangulos e Jacobiano positivo");

    // 3. Delaunay em poligono L-shape nao convexo + conjuntos de nos
    const std::vector<Eigen::Vector2d> lPoly = {
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 2.0}, {2.0, 2.0}, {2.0, 4.0}, {0.0, 4.0}
    };
    auto del = MeshGenerator::triangulatePolygonDelaunay(lPoly, 1.0, 4);
    const auto delQual = MeshGenerator::evaluateQuality(del);
    TEST_ASSERT(delQual.isValid() && delQual.totalElements >= 8 && delQual.invertedOrDegenerateCount == 0,
                "Delaunay 2D nao convexo deve gerar triangulos validos sem elementos invertidos");
    MeshGenerator::assignPlaneNodeSet(del, "BottomEdge", KVec3(0.0, 0.0, 0.0), KVec3(0.0, 1.0, 0.0), 1e-6);
    TEST_ASSERT(del.nodeSets["BottomEdge"].size() >= 3, "assignPlaneNodeSet deve capturar nos da borda inferior");

    // 4. Malhas 3D Hex8, Hex20, Tet4
    const auto h8 = MeshGenerator::generateStructuredHexGrid(KVec3::Zero(), KVec3(2.0, 2.0, 4.0), 2, 2, 2, false);
    const auto h8Qual = MeshGenerator::evaluateQuality(h8);
    TEST_ASSERT(h8Qual.isValid() && h8Qual.totalElements == 8 && h8Qual.minJacobian > 0.1,
                "Hex8 estruturada deve ter 8 hexaedros e qualidade valida");

    const auto h20 = MeshGenerator::generateStructuredHexGrid(KVec3::Zero(), KVec3(2.0, 2.0, 4.0), 1, 1, 2, true);
    const auto h20Qual = MeshGenerator::evaluateQuality(h20);
    TEST_ASSERT(h20Qual.isValid() && h20.elements.size() == 2 && h20.elements[0].nodeIds.size() == 20,
                "Hex20 estruturada deve ter 20 nos por elemento e qualidade valida");

    const auto tet4 = MeshGenerator::generateStructuredTetGrid(KVec3::Zero(), KVec3(2.0, 2.0, 4.0), 2, 2, 2);
    const auto tetQual = MeshGenerator::evaluateQuality(tet4);
    TEST_ASSERT(tetQual.isValid() && tetQual.totalElements == 48 && tetQual.invertedOrDegenerateCount == 0,
                "Tet4 Kuhn estruturada deve gerar 48 tetraedros conformes e positivos");

    // 5. Extrusao conforme 2D -> Tet4 3D + resolucao direta no GlobalAssembler sob tracao uniaxial
    const auto baseSurf = MeshGenerator::generateStructuredQuadGrid(
        KVec3::Zero(), KVec3(1.0, 0.0, 0.0), KVec3(0.0, 1.0, 0.0), 2, 2, false);
    auto extTet = MeshGenerator::extrudeSurfaceTo3D(baseSurf, KVec3(0.0, 0.0, 2.0), 2, true, "ExtrudedTet");
    const auto extQual = MeshGenerator::evaluateQuality(extTet);
    TEST_ASSERT(extQual.isValid() && extQual.invertedOrDegenerateCount == 0,
                "Malha Tet4 extrudada deve ser conforme e ter Jacobiano positivo");

    MeshGenerator::assignPlaneNodeSet(extTet, "BaseZ0", KVec3(0.0, 0.0, 0.0), KVec3(0.0, 0.0, 1.0), 1e-6);
    MeshGenerator::assignPlaneNodeSet(extTet, "TopZ2", KVec3(0.0, 0.0, 2.0), KVec3(0.0, 0.0, 1.0), 1e-6);
    TEST_ASSERT(extTet.nodeSets["BaseZ0"].size() == 9 && extTet.nodeSets["TopZ2"].size() == 9,
                "Extrusao 2D->3D deve preservar exatamente os 9 nos da base e do topo");

    // Resolve tracao uniaxial com nu=0 (estado homogeneo sigma_zz = 100 kPa => uZ(top) = 100 * 2 / E)
    auto mat = std::make_shared<LinearElasticIsotropic>(1, "Uniaxial0", 2.0e7, 0.0, 0.0, 2500.0, 0.0);
    DofManager dofMgr;
    for (const auto& n : extTet.nodes) {
        dofMgr.registerNode(n.id, {true, true, true, false, false, false, false});
    }
    for (NodeId nid : extTet.nodeSets["BaseZ0"]) {
        dofMgr.setBoundaryCondition(nid, 2, true);
    }
    // Trava translação X em x=0 e Y em y=0 para eliminar modos rígidos no plano XY mantendo estado uniaxial livre
    for (const auto& n : extTet.nodes) {
        if (std::abs(n.position.x()) < 1e-6) dofMgr.setBoundaryCondition(n.id, 0, true);
        if (std::abs(n.position.y()) < 1e-6) dofMgr.setBoundaryCondition(n.id, 1, true);
    }
    dofMgr.finalizeNumbering();

    std::unordered_map<NodeId, KVec3> coordMap;
    for (const auto& n : extTet.nodes) coordMap[n.id] = n.position;

    std::vector<std::shared_ptr<IFiniteElement>> solids;
    std::unordered_map<ElementId, ElementLoadInput> elLoads;
    std::unordered_map<NodeId, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>> nodLoads;
    for (const auto& me : extTet.elements) {
        std::vector<KVec3> pts;
        pts.reserve(me.nodeIds.size());
        for (NodeId nid : me.nodeIds) pts.push_back(coordMap[nid]);
        solids.push_back(std::make_shared<SolidElement>(
            me.id, SolidElementType::Tet4, me.nodeIds, std::move(pts), mat));
    }

    // Forca consistente equivalente a tensao uniforme sigma_z = 100 kPa sobre as faces triangulares no topo (z = 2)
    for (const auto& me : extTet.elements) {
        std::vector<NodeId> topFace;
        for (NodeId nid : me.nodeIds) {
            if (std::abs(coordMap[nid].z() - 2.0) < 1e-6) topFace.push_back(nid);
        }
        if (topFace.size() == 3) {
            const KVec3& p0 = coordMap[topFace[0]];
            const KVec3& p1 = coordMap[topFace[1]];
            const KVec3& p2 = coordMap[topFace[2]];
            const double areaTri = 0.5 * (p1 - p0).cross(p2 - p0).norm();
            for (NodeId nid : topFace) {
                auto [kv, inserted] = nodLoads.try_emplace(
                    nid, Eigen::Matrix<double, NODAL_MAX_DOFS, 1>::Zero());
                (void)inserted;
                kv->second(2) += (100.0 * areaTri) / 3.0;
            }
        }
    }

    SpMat K;
    VecX F;
    std::string err;
    TEST_ASSERT(GlobalAssembler::assembleStiffnessAndLoad(dofMgr, solids, elLoads, nodLoads, K, F, &err), err);
    const auto cls = dofMgr.classifyDofs(K, F, &err);
    ReducedLinearSystem red;
    TEST_ASSERT(GlobalAssembler::buildReducedSystem(dofMgr, K, F, cls, red, &err), err);
    LinearSolverStats stats;
    const VecX Uf = LinearSolverSuite::solve(red.K_ff, red.rhs_f, LinearSolverOptions{}, stats);
    TEST_ASSERT(stats.success, "Malha Tet4 extrudada deve convergir no solver linear");
    const VecX U = GlobalAssembler::reconstructFullDisplacements(red, Uf);

    const double uExact = (100.0 * 2.0) / 2.0e7;
    for (NodeId nid : extTet.nodeSets["TopZ2"]) {
        const double uz = U(dofMgr.getNodeGlobalDof(nid, 2));
        TEST_ASSERT(std::abs(uz - uExact) < 1e-4 * uExact,
                    "Deslocamento axial da malha Tet4 extrudada deve coincidir exatamente com P*L/(E*A)");
    }

    std::cout << "[OK] testMeshGenerator1D2D3DAndExtrusionSolve (uExact=" << uExact
              << ", minJacTet=" << extQual.minJacobian << ")\n";
}

void testDxfRoundtripAndMeshExport() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser criado");
    const std::string dxfPath = tmp.filePath("building_frame.dxf").toStdString();

    const StructuralModel src = StructuralModel::createBuildingFrameExample();
    const auto plate = MeshGenerator::generateStructuredTriGrid(
        KVec3::Zero(), KVec3(4.0, 0.0, 0.0), KVec3(0.0, 4.0, 0.0), 2, 2);

    io::DxfInteropReport expRep;
    TEST_ASSERT(io::DxfInterop::exportToFile(dxfPath, src, &plate, &expRep) && expRep.success,
                "Exportacao DXF deve suceder: " + expRep.errorMessage);
    TEST_ASSERT(expRep.lineCount == static_cast<int>(src.getElements().size()),
                "DXF deve exportar exatamente todas as barras como LINE");
    TEST_ASSERT(expRep.faceCount == static_cast<int>(plate.elements.size()),
                "DXF deve exportar faces da malha 2D como 3DFACE");

    StructuralModel dst;
    MeshContainer importedPlate;
    io::DxfInteropReport impRep;
    TEST_ASSERT(io::DxfInterop::importFromFile(dxfPath, dst, &importedPlate, io::DxfImportOptions{}, &impRep) && impRep.success,
                "Importacao DXF deve suceder: " + impRep.errorMessage);
    TEST_ASSERT(dst.getElements().size() == src.getElements().size(),
                "Importacao DXF deve preservar numero exato de barras");
    TEST_ASSERT(dst.getNodes().size() == src.getNodes().size(),
                "Soldagem espacial DXF deve preservar numero exato de nos");
    TEST_ASSERT(importedPlate.elements.size() == plate.elements.size(),
                "Importacao DXF deve recuperar todas as faces 3DFACE");

    std::cout << "[OK] testDxfRoundtripAndMeshExport (lines=" << impRep.lineCount
              << ", faces=" << impRep.faceCount << ")\n";
}

void testIfc4RoundtripAndDeterministicGuids() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser criado");
    const std::string ifcPath1 = tmp.filePath("model_a.ifc").toStdString();
    const std::string ifcPath2 = tmp.filePath("model_b.ifc").toStdString();

    const StructuralModel src = StructuralModel::createBuildingFrameExample();
    const auto shellMesh = MeshGenerator::generateStructuredQuadGrid(
        KVec3(0.0, 0.0, 3.0), KVec3(5.0, 0.0, 0.0), KVec3(0.0, 4.0, 0.0), 2, 2, false);

    io::IfcGuidMap guids1, guids2;
    io::IfcInteropReport rep1, rep2;
    TEST_ASSERT(io::IfcInterop::exportToFile(ifcPath1, src, &shellMesh, &guids1, &rep1) && rep1.success,
                "Exportacao IFC4 #1 deve suceder: " + rep1.errorMessage);
    TEST_ASSERT(io::IfcInterop::exportToFile(ifcPath2, src, &shellMesh, &guids2, &rep2) && rep2.success,
                "Exportacao IFC4 #2 deve suceder");

    TEST_ASSERT(guids1.elementGuids.size() == src.getElements().size(),
                "IFC4 deve gerar GUIDs para todos os elementos");
    for (const auto& [eid, guid] : guids1.elementGuids) {
        TEST_ASSERT(guid.size() == 22, "IfcGloballyUniqueId compactado deve ter 22 caracteres");
        TEST_ASSERT(guids2.elementGuids[eid] == guid, "GUIDs IFC4 devem ser deterministicos entre exportacoes");
    }

    StructuralModel dst;
    MeshContainer importedShell;
    io::IfcGuidMap guidsIn;
    io::IfcInteropReport impRep;
    TEST_ASSERT(io::IfcInterop::importFromFile(ifcPath1, dst, &importedShell, &guidsIn, &impRep) && impRep.success,
                "Importacao IFC4 deve suceder: " + impRep.errorMessage);
    TEST_ASSERT(impRep.schemaDetected == "IFC4", "Importacao deve detectar esquema IFC4");
    TEST_ASSERT(dst.getNodes().size() == src.getNodes().size(),
                "Importacao IFC4 deve restaurar todos os nos estruturais");
    TEST_ASSERT(dst.getElements().size() == src.getElements().size(),
                "Importacao IFC4 deve restaurar todos os elementos de barra");
    TEST_ASSERT(importedShell.elements.size() == shellMesh.elements.size(),
                "Importacao IFC4 deve restaurar superficies analiticas");

    std::cout << "[OK] testIfc4RoundtripAndDeterministicGuids (nodes=" << impRep.structuralNodesImported
              << ", curves=" << impRep.curveMembersImported
              << ", surfaces=" << impRep.surfaceMembersImported << ")\n";
}

void testHeadlessCliEndToEnd() {
#ifndef OMNIFRAME_CLI_PATH
    TEST_ASSERT(false, "OMNIFRAME_CLI_PATH nao definido no CMake");
#else
    const QString cliExe = QString::fromUtf8(OMNIFRAME_CLI_PATH);
    TEST_ASSERT(QFile::exists(cliExe), "Binario omniframe-cli deve existir");

    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser criado");

    const StructuralModel model = StructuralModel::createBuildingFrameExample();
    const QString projPath = tmp.filePath("cli_frame.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(projPath, model), "Salvar projeto para CLI deve suceder");

    auto runCli = [&](const QStringList& args, QByteArray& outBytes) -> int {
        QProcess proc;
        proc.start(cliExe, args);
        TEST_ASSERT(proc.waitForStarted(10000), "CLI deve iniciar");
        TEST_ASSERT(proc.waitForFinished(30000), "CLI deve concluir dentro do timeout");
        outBytes = proc.readAllStandardOutput();
        if (proc.exitCode() != 0) {
            std::cerr << "[CLI stderr]: " << proc.readAllStandardError().toStdString() << "\n";
        }
        return proc.exitCode();
    };

    // 1. --version
    QByteArray verOut;
    TEST_ASSERT(runCli({"--version"}, verOut) == 0 && verOut.contains("OmniFrame 3D CLI"),
                "CLI --version deve retornar codigo 0 e identificar OmniFrame 3D CLI");

    // 2. --validate
    QByteArray valOut;
    TEST_ASSERT(runCli({"--validate", projPath}, valOut) == 0, "CLI --validate deve retornar 0");
    const QJsonDocument valDoc = QJsonDocument::fromJson(valOut);
    TEST_ASSERT(valDoc.isObject() && valDoc.object()["valid"].toBool(),
                "CLI --validate deve retornar JSON valido=true");

    // 3. --analyze static + exportacoes + relatorios
    const QString outJson = tmp.filePath("summary.json");
    const QString expJson = tmp.filePath("out.omniframe");
    const QString expCsv = tmp.filePath("out.csv");
    const QString expVtk = tmp.filePath("out.vtk");
    const QString expDxf = tmp.filePath("out.dxf");
    const QString expIfc = tmp.filePath("out.ifc");
    const QString repMd = tmp.filePath("report.md");
    const QString repHtml = tmp.filePath("report.html");

    QByteArray stOut;
    const int stCode = runCli({
        "--analyze", projPath,
        "--analysis", "static",
        "--solver", "ldlt",
        "--export", "json", expJson,
        "--export", "csv", expCsv,
        "--export", "vtk", expVtk,
        "--export", "dxf", expDxf,
        "--export", "ifc", expIfc,
        "--report", "md", repMd,
        "--report", "html", repHtml,
        "--output", outJson,
        "--json"
    }, stOut);
    TEST_ASSERT(stCode == 0, "CLI --analyze static completo deve retornar 0");
    const QJsonDocument stDoc = QJsonDocument::fromJson(stOut);
    TEST_ASSERT(stDoc.isObject() && stDoc.object()["success"].toBool() &&
                stDoc.object()["equilibriumBalanced"].toBool(),
                "CLI estatico deve atingir equilibrio global");

    for (const QString& p : {outJson, expJson, expCsv, expVtk, expDxf, expIfc, repMd, repHtml}) {
        QFile f(p);
        TEST_ASSERT(f.exists() && f.size() > 64, "Artefato exportado pela CLI deve existir e nao estar vazio");
    }

    // 4. --analyze modal
    QByteArray modOut;
    TEST_ASSERT(runCli({"--analyze", projPath, "--analysis", "modal", "--modes", "4", "--json"}, modOut) == 0,
                "CLI --analysis modal deve retornar 0");
    const QJsonDocument modDoc = QJsonDocument::fromJson(modOut);
    TEST_ASSERT(modDoc.isObject() && modDoc.object()["success"].toBool() &&
                modDoc.object()["modes"].toArray().size() == 4,
                "CLI modal deve extrair 4 modos");

    // 5. --analyze buckling
    QByteArray bckOut;
    TEST_ASSERT(runCli({"--analyze", projPath, "--analysis", "buckling", "--modes", "2", "--json"}, bckOut) == 0,
                "CLI --analysis buckling deve retornar 0");
    const QJsonDocument bckDoc = QJsonDocument::fromJson(bckOut);
    TEST_ASSERT(bckDoc.isObject() && bckDoc.object()["success"].toBool() &&
                bckDoc.object()["criticalLoadFactors"].toArray().size() == 2,
                "CLI buckling deve retornar 2 fatores criticos");

    std::cout << "[OK] testHeadlessCliEndToEnd (static/modal/buckling/exports/reports)\n";
#endif
}

} // namespace

int main() {
    testMeshGenerator1D2D3DAndExtrusionSolve();
    testDxfRoundtripAndMeshExport();
    testIfc4RoundtripAndDeterministicGuids();
    testHeadlessCliEndToEnd();
    std::cout << "\nTodos os testes da Fase 7 (Malha 1D/2D/3D, DXF, IFC4 BIM e CLI headless) passaram!\n";
    return 0;
}
