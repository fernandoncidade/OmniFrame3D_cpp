#include "TestHelper.hpp"
#include "core/StructuralModel.hpp"
#include "io/DxfInterop.hpp"
#include "io/IfcInterop.hpp"
#include "io/ProjectSerializer.hpp"
#include "os/WindowsCliIntegration.hpp"
#include "kernel/assembly/GlobalAssembler.hpp"
#include "kernel/core/DofManager.hpp"
#include "kernel/elements/SolidElements.hpp"
#include "kernel/materials/LinearElasticMaterial.hpp"
#include "kernel/mesh/MeshGenerator.hpp"
#include "kernel/solver/LinearSolverSuite.hpp"
#include "solver/FemSolver3D.hpp"

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

    StructuralModel src = StructuralModel::createBuildingFrameExample();
    // Garante variedade de apoios (engaste, rotula, apoio misto) para validar roundtrip de node.support.fixed
    if (src.getNodes().size() >= 2) {
        src.getNodes()[0].support = SupportRestraint::makeFixed();
        src.getNodes()[1].support = SupportRestraint::makePinned();
        src.getNodes()[1].support.fixed[4] = true;
    }
    auto shellMesh = MeshGenerator::generateStructuredQuadGrid(
        KVec3(0.0, 0.0, 3.0), KVec3(5.0, 0.0, 0.0), KVec3(0.0, 4.0, 0.0), 2, 2, false);
    for (auto& me : shellMesh.elements) {
        me.thickness = 0.275;
    }

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

    // [AUD-013] Verificar igualdade exata de node.support.fixed e me.thickness apos roundtrip IFC4
    for (const auto& sNode : src.getNodes()) {
        const Node* matched = nullptr;
        for (const auto& dNode : dst.getNodes()) {
            if ((dNode.position - sNode.position).norm() < 1e-6) {
                matched = &dNode;
                break;
            }
        }
        TEST_ASSERT(matched != nullptr, "No estrutural IFC4 deve ser reimportado na mesma coordenada");
        TEST_ASSERT(matched->support.fixed == sNode.support.fixed,
                    "Roundtrip IFC4 deve preservar exatamente node.support.fixed ([AUD-013])");
    }
    for (const auto& impEl : importedShell.elements) {
        TEST_ASSERT(std::abs(impEl.thickness - 0.275) < 1e-6,
                    "Roundtrip IFC4 deve preservar me.thickness real (0.275 m) em vez de 0.15 hardcoded ([AUD-013])");
    }

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

void testContinuumSerializationAndCliTransientCase() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser criado para E2E continuo/CLI");

    // 1. Roundtrip JSON .omniframe para malha 2D Quad4 (CCs, carregamentos e resultados)
    StructuralModel contModel;
    contModel.getMaterials().clear();
    const int matId = contModel.addMaterial(Material::makeConcreteC30(1));
    contModel.surfaceMesh() = MeshGenerator::generateStructuredQuadGrid(
        KVec3::Zero(), KVec3(2.0, 0.0, 0.0), KVec3(0.0, 2.0, 0.0), 2, 2, false, "ContSlab");

    for (auto& mn : contModel.surfaceMesh().nodes) {
        const bool onEdge = (std::abs(mn.position.x()) < 1e-6 || std::abs(mn.position.x() - 2.0) < 1e-6 ||
                             std::abs(mn.position.y()) < 1e-6 || std::abs(mn.position.y() - 2.0) < 1e-6);
        if (onEdge) {
            mn.fixed = {true, true, true, false, false, false};
        } else {
            mn.springStiffness[2] = 500.0;
            mn.nodalForce = KVec3(0.0, 0.0, -5.0);
        }
        if (std::abs(mn.position.x()) < 1e-6 && std::abs(mn.position.y() - 1.0) < 1e-6) {
            mn.prescribedDisplacement[2] = -0.001;
        }
    }
    for (size_t i = 0; i < contModel.surfaceMesh().elements.size(); ++i) {
        auto& me = contModel.surfaceMesh().elements[i];
        me.materialId = matId;
        me.thickness = 0.20;
        me.normalPressure = -10.0;
        if (i == 0) me.deltaT = 5.0;
    }

    const SolverResult contSolve = FemSolver3D::solve(contModel);
    TEST_ASSERT(contSolve.success && contSolve.maxShellVonMises > 0.0,
                "Modelo continuo 2D deve resolver antes da serializacao");

    const QString contPath = tmp.filePath("continuum_slab.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(contPath, contModel),
                "Salvar .omniframe com surfaceMesh e resultados deve suceder");

    StructuralModel reloadedCont;
    TEST_ASSERT(ProjectSerializer::loadFromFile(contPath, reloadedCont),
                "Recarregar .omniframe com surfaceMesh deve suceder");
    TEST_ASSERT(reloadedCont.surfaceMesh().nodes.size() == contModel.surfaceMesh().nodes.size() &&
                reloadedCont.surfaceMesh().elements.size() == contModel.surfaceMesh().elements.size(),
                "Contagens de nos e elementos da malha continua devem sobreviver ao roundtrip");

    bool foundSpring = false, foundPresc = false;
    for (const auto& mn : reloadedCont.surfaceMesh().nodes) {
        TEST_ASSERT(mn.hasResults, "Resultados nodais da malha continua devem sobreviver ao roundtrip");
        if (mn.springStiffness[2] > 499.0 && std::abs(mn.nodalForce.z() + 5.0) < 1e-6) {
            foundSpring = true;
        }
        if (mn.fixed[2] && std::abs(mn.prescribedDisplacement[2] + 0.001) < 1e-9) {
            foundPresc = true;
        }
    }
    TEST_ASSERT(foundSpring && foundPresc,
                "Molas nodais, forcas nodais e recalques prescritos de MeshNode devem preservar no JSON");
    TEST_ASSERT(std::abs(reloadedCont.surfaceMesh().elements[0].deltaT - 5.0) < 1e-9 &&
                std::abs(reloadedCont.surfaceMesh().elements[0].thickness - 0.20) < 1e-9 &&
                std::abs(reloadedCont.surfaceMesh().elements[0].normalPressure + 10.0) < 1e-9 &&
                reloadedCont.surfaceMesh().elements[0].hasResults &&
                reloadedCont.surfaceMesh().elements[0].vonMisesStress > 0.0,
                "Propriedades, cargas, deltaT e tensoes de MeshElement devem preservar no JSON");

#ifdef OMNIFRAME_CLI_PATH
    const QString cliExe = QString::fromUtf8(OMNIFRAME_CLI_PATH);
    auto runCli = [&](const QStringList& args, QByteArray& outBytes) -> int {
        QProcess proc;
        proc.start(cliExe, args);
        TEST_ASSERT(proc.waitForStarted(10000), "CLI deve iniciar");
        TEST_ASSERT(proc.waitForFinished(30000), "CLI deve finalizar no prazo");
        outBytes = proc.readAllStandardOutput();
        if (proc.exitCode() != 0) {
            std::cerr << "[CLI stderr]: " << proc.readAllStandardError().toStdString() << "\n";
        }
        return proc.exitCode();
    };

    // 2. CLI estatico continuo + verificacao de conteudo VTK UNSTRUCTURED_GRID e CSV continuo
    const QString contVtk = tmp.filePath("continuum.vtk");
    const QString contCsv = tmp.filePath("continuum.csv");
    QByteArray contCliOut;
    TEST_ASSERT(runCli({
        "--analyze", contPath,
        "--analysis", "static",
        "--export", "vtk", contVtk,
        "--export", "csv", contCsv,
        "--json"
    }, contCliOut) == 0, "CLI --analyze static para malha continua deve retornar 0");

    const QJsonDocument contDoc = QJsonDocument::fromJson(contCliOut);
    TEST_ASSERT(contDoc.isObject() && contDoc.object()["success"].toBool() &&
                contDoc.object()["totalSurfaceElements"].toInt() == 4 &&
                contDoc.object()["maxShellVonMises"].toDouble() > 0.0,
                "JSON da CLI deve reportar totalSurfaceElements e maxShellVonMises");

    {
        QFile vf(contVtk);
        TEST_ASSERT(vf.open(QIODevice::ReadOnly | QIODevice::Text), "Abrir VTK continuo gerado");
        const QByteArray vtkBytes = vf.readAll();
        TEST_ASSERT(vtkBytes.contains("DATASET UNSTRUCTURED_GRID") &&
                    vtkBytes.contains("CELL_TYPES") &&
                    vtkBytes.contains("POINT_DATA") &&
                    vtkBytes.contains("CELL_DATA") &&
                    vtkBytes.contains("von_mises_kPa"),
                    "VTK deve usar UNSTRUCTURED_GRID com campos escalares von_mises_kPa");
    }
    {
        QFile cf(contCsv);
        TEST_ASSERT(cf.open(QIODevice::ReadOnly | QIODevice::Text), "Abrir CSV continuo gerado");
        const QByteArray csvBytes = cf.readAll();
        TEST_ASSERT(csvBytes.contains("Continuum Surface/Solid Mesh Nodes") &&
                    csvBytes.contains("Continuum 2D/3D Element Stresses & Resultants"),
                    "CSV deve incluir secoes nodais e de tensoes continuas 2D/3D");
    }

    // 3. CLI --case, --combo, --analysis transient --dt e --analysis seismic --dt
    StructuralModel caseModel;
    caseModel.getMaterials().clear();
    caseModel.getCrossSections().clear();
    const int sMat = caseModel.addMaterial(Material::makeStructuralSteel(1));
    const int sSec = caseModel.addCrossSection(CrossSection::makeRectangular(1, "Viga 20x40", 0.20, 0.40));
    const int n1 = caseModel.addNode(0.0, 0.0, 0.0);
    const int n2 = caseModel.addNode(4.0, 0.0, 0.0);
    caseModel.findNode(n1)->support = SupportRestraint::makeFixed();
    caseModel.findNode(n2)->load.force = {0.0, 0.0, -15.0};
    caseModel.addElement(n1, n2, sMat, sSec);

    LoadCase lc;
    lc.id = 1;
    lc.name = "CargaPonta";
    NodalLoadPatternEntry nlEntry;
    nlEntry.nodeId = n2;
    nlEntry.forceMoment = {0.0, 0.0, -15.0, 0.0, 0.0, 0.0};
    lc.nodalLoads.push_back(nlEntry);
    caseModel.addLoadCase(lc);

    LoadCombination lcomb;
    lcomb.id = 1;
    lcomb.name = "ELU-Teste";
    lcomb.factors.push_back(LoadCombinationFactor{1, 1.4});
    caseModel.addLoadCombination(lcomb);

    const QString caseProjPath = tmp.filePath("case_beam.omniframe");
    TEST_ASSERT(ProjectSerializer::saveToFile(caseProjPath, caseModel), "Salvar projeto para CLI case/transient");

    QByteArray outCase;
    TEST_ASSERT(runCli({"--analyze", caseProjPath, "--analysis", "static", "--case", "1", "--json"}, outCase) == 0,
                "CLI --case 1 deve retornar 0");
    const QJsonObject objCase = QJsonDocument::fromJson(outCase).object();
    TEST_ASSERT(objCase["success"].toBool() && objCase["selectedCaseOrComboId"].toInt() == 1 &&
                objCase["selectedCaseOrComboMaxDisp"].toDouble() > 0.0,
                "CLI --case 1 deve resolver deslocamento maximo nao nulo");

    QByteArray outCombo;
    TEST_ASSERT(runCli({"--analyze", caseProjPath, "--analysis", "static", "--combo", "1", "--json"}, outCombo) == 0,
                "CLI --combo 1 deve retornar 0");
    const QJsonObject objCombo = QJsonDocument::fromJson(outCombo).object();
    TEST_ASSERT(objCombo["success"].toBool() && objCombo["selectedCaseOrComboId"].toInt() == 1 &&
                objCombo["selectedCaseOrComboMaxDisp"].toDouble() > objCase["selectedCaseOrComboMaxDisp"].toDouble() * 1.35,
                "CLI --combo 1 (gamma=1.4) deve escalar o deslocamento em 1.4x");

    QByteArray outTrans;
    TEST_ASSERT(runCli({"--analyze", caseProjPath, "--analysis", "transient", "--steps", "8", "--dt", "0.02", "--json"}, outTrans) == 0,
                "CLI --analysis transient deve retornar 0");
    const QJsonObject objTrans = QJsonDocument::fromJson(outTrans).object();
    TEST_ASSERT(objTrans["success"].toBool() && objTrans["steps"].toInt() == 9 &&
                std::abs(objTrans["dt"].toDouble() - 0.02) < 1e-9,
                "CLI transient deve executar 8 passos (+ t=0 => 9 registros) com dt=0.02");

    QByteArray outSeis;
    TEST_ASSERT(runCli({"--analyze", caseProjPath, "--analysis", "seismic", "--steps", "8", "--dt", "0.02", "--json"}, outSeis) == 0,
                "CLI --analysis seismic deve retornar 0");
    const QJsonObject objSeis = QJsonDocument::fromJson(outSeis).object();
    TEST_ASSERT(objSeis["success"].toBool() && objSeis["steps"].toInt() == 9 &&
                std::abs(objSeis["dt"].toDouble() - 0.02) < 1e-9,
                "CLI seismic deve executar 8 passos (+ t=0 => 9 registros) com dt=0.02");

    // 4. [AUD-007] --validate --json com no de malha inexistente 999 (Tri3/Quad4/Tet4/Hex8)
    for (int cIdx = 0; cIdx < 4; ++cIdx) {
        StructuralModel badMeshModel;
        badMeshModel.getMaterials().clear();
        badMeshModel.getCrossSections().clear();
        const int mId = badMeshModel.addMaterial(Material::makeConcreteC30(1));
        for (int nid = 1; nid <= 8; ++nid) {
            kernel::MeshNode mn;
            mn.id = nid;
            mn.position = kernel::Vec3((nid - 1) & 1, ((nid - 1) >> 1) & 1, ((nid - 1) >> 2) & 1);
            if (nid <= 4) mn.fixed = {true, true, true, true, true, true};
            badMeshModel.surfaceMesh().nodes.push_back(mn);
        }
        kernel::MeshElement me;
        me.id = 1;
        me.materialId = mId;
        me.thickness = 0.20;
        if (cIdx == 0) { me.type = kernel::MeshCellType::Tri3;  me.nodeIds = {1, 2, 999}; }
        if (cIdx == 1) { me.type = kernel::MeshCellType::Quad4; me.nodeIds = {1, 2, 3, 999}; }
        if (cIdx == 2) { me.type = kernel::MeshCellType::Tet4;  me.nodeIds = {1, 2, 3, 999}; }
        if (cIdx == 3) { me.type = kernel::MeshCellType::Hex8;  me.nodeIds = {1, 2, 3, 4, 5, 6, 7, 999}; }
        badMeshModel.surfaceMesh().elements.push_back(me);

        const QString badMeshPath = tmp.filePath(QString("missing_node_999_%1.omniframe").arg(cIdx));
        TEST_ASSERT(ProjectSerializer::saveToFile(badMeshPath, badMeshModel), "Salvar .omniframe com no 999 ausente");

        QProcess valProc;
        valProc.start(cliExe, {"--validate", badMeshPath, "--json"});
        TEST_ASSERT(valProc.waitForStarted(10000) && valProc.waitForFinished(30000),
                    "CLI --validate com no 999 deve concluir sem crash");
        TEST_ASSERT(valProc.exitStatus() == QProcess::NormalExit && valProc.exitCode() == 2,
                    "CLI --validate com no 999 deve sair com codigo 2");
        const QJsonDocument valDoc = QJsonDocument::fromJson(valProc.readAllStandardOutput());
        TEST_ASSERT(valDoc.isObject() && !valDoc.object()["valid"].toBool() && !valDoc.object()["success"].toBool(),
                    "CLI --validate JSON deve reportar valid=false");
        const QJsonArray issues = valDoc.object()["validationIssues"].toArray();
        bool hasErrCode = false;
        for (const QJsonValue& iv : issues) {
            if (iv.toObject()["code"].toString() == QStringLiteral("ERR_MESH_MISSING_NODE")) {
                hasErrCode = true;
                break;
            }
        }
        TEST_ASSERT(hasErrCode, "CLI --validate JSON deve reportar ERR_MESH_MISSING_NODE ([AUD-007])");
    }
#endif

    std::cout << "[OK] ContinuumSerialization + CLI VTK/CSV/case/combo/transient/seismic\n";
}

void testCliOmniFrameIntegrationAndUtf8Paths() {
#ifndef OMNIFRAME_CLI_PATH
    TEST_ASSERT(false, "OMNIFRAME_CLI_PATH nao definido no CMake");
#else
    const QString cliExe = QString::fromUtf8(OMNIFRAME_CLI_PATH);
    auto runCli = [&](const QStringList& args, QByteArray& outBytes) -> int {
        QProcess proc;
        proc.start(cliExe, args);
        TEST_ASSERT(proc.waitForStarted(10000), "CLI deve iniciar");
        TEST_ASSERT(proc.waitForFinished(30000), "CLI deve finalizar no prazo");
        outBytes = proc.readAllStandardOutput();
        if (proc.exitCode() != 0) {
            std::cerr << "[CLI stderr]: " << proc.readAllStandardError().toStdString() << "\n";
        }
        return proc.exitCode();
    };

    // 1. --list-examples --json
    QByteArray exListOut;
    TEST_ASSERT(runCli({"--list-examples", "--json"}, exListOut) == 0,
                "CLI --list-examples --json deve retornar 0");
    const QJsonObject exListObj = QJsonDocument::fromJson(exListOut).object();
    TEST_ASSERT(exListObj["success"].toBool() && exListObj["examples"].toArray().size() == 3,
                "CLI deve listar os 3 exemplos embutidos do OmniFrame 3D");

    // 2. --validate --example truss
    QByteArray exValOut;
    TEST_ASSERT(runCli({"--validate", "--example", "truss"}, exValOut) == 0,
                "CLI --validate --example truss deve retornar 0");
    const QJsonObject exValObj = QJsonDocument::fromJson(exValOut).object();
    TEST_ASSERT(exValObj["success"].toBool() && exValObj["valid"].toBool() &&
                exValObj["exampleName"].toString() == "truss",
                "CLI --validate --example truss deve validar o exemplo 3");

    // 3. --example building --nbr-audit --json
    QByteArray nbrOut;
    TEST_ASSERT(runCli({"--example", "building", "--nbr-audit", "--json"}, nbrOut) == 0,
                "CLI --example building --nbr-audit deve retornar 0");
    const QJsonObject nbrObj = QJsonDocument::fromJson(nbrOut).object();
    TEST_ASSERT(nbrObj["success"].toBool() && nbrObj["nbrAudit"].isObject() &&
                !nbrObj["nbrAudit"].toObject()["coverageMatrix"].toArray().isEmpty(),
                "CLI --nbr-audit deve emitir matriz normativa e resumo NBR");

    // 4. --scheme hht + --imperfection + --modified-newton + --no-line-search
    QByteArray hhtOut;
    TEST_ASSERT(runCli({
        "--example", "building", "--analysis", "transient",
        "--scheme", "hht", "--steps", "6", "--dt", "0.015", "--json"
    }, hhtOut) == 0, "CLI --scheme hht deve retornar 0");
    const QJsonObject hhtObj = QJsonDocument::fromJson(hhtOut).object();
    TEST_ASSERT(hhtObj["success"].toBool() && hhtObj["scheme"].toString() == "hht",
                "CLI deve registrar scheme=hht");

    QByteArray nlOut;
    TEST_ASSERT(runCli({
        "--example", "building", "--analysis", "nonlinear",
        "--steps", "6", "--load-factor", "0.5",
        "--modified-newton", "--no-line-search",
        "--imperfection", "0.002", "--imperfection-mode", "1", "--json"
    }, nlOut) == 0, "CLI nonlinear com imperfeicao e opcoes avancadas deve retornar 0");
    const QJsonObject nlObj = QJsonDocument::fromJson(nlOut).object();
    TEST_ASSERT(nlObj["success"].toBool() &&
                nlObj["appliedImperfection"].isObject() &&
                !nlObj["fullNewtonTangent"].toBool() &&
                !nlObj["enableLineSearch"].toBool(),
                "CLI deve respeitar imperfection, modified-newton e no-line-search");

    // 5. --check-windows-integration --json
    QByteArray winOut;
    TEST_ASSERT(runCli({"--check-windows-integration", "--json"}, winOut) == 0,
                "CLI --check-windows-integration --json deve retornar 0");
    const QJsonObject winObj = QJsonDocument::fromJson(winOut).object();
    TEST_ASSERT(winObj["success"].toBool() && winObj["cliBinaryExists"].toBool(),
                "CLI --check-windows-integration deve localizar omniframe-cli");

    // 6. Caminhos UTF-8 com acentos e espacos (CMD/PowerShell Windows 10/11)
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser criado");
    const QString utf8DirPath = tmp.filePath(QString::fromUtf8("P\u00F3rtico S\u00E3o Paulo"));
    TEST_ASSERT(QDir().mkpath(utf8DirPath), "Diretorio UTF-8 com acentos e espacos deve ser criado");

    const QString utf8Proj = utf8DirPath + QString::fromUtf8("/modelo_t\u00E9cnico.omniframe");
    const QString utf8Dxf = utf8DirPath + QString::fromUtf8("/sa\u00EDda_cad.dxf");
    const QString utf8Ifc = utf8DirPath + QString::fromUtf8("/sa\u00EDda_bim.ifc");
    const QString utf8Csv = utf8DirPath + QString::fromUtf8("/resultados_n\u00F3s.csv");
    const QString utf8Vtk = utf8DirPath + QString::fromUtf8("/malha_t\u00E9rmica.vtk");
    const QString utf8Json = utf8DirPath + QString::fromUtf8("/resumo_s\u00EDntese.json");

    const StructuralModel ex = StructuralModel::createBuildingFrameExample();
    TEST_ASSERT(ProjectSerializer::saveToFile(utf8Proj, ex), "Salvar projeto em pasta UTF-8 deve suceder");

    QByteArray utf8Out;
    TEST_ASSERT(runCli({
        "--analyze", utf8Proj,
        "--export", "dxf", utf8Dxf,
        "--export", "ifc", utf8Ifc,
        "--export", "csv", utf8Csv,
        "--export", "vtk", utf8Vtk,
        "--output", utf8Json,
        "--json"
    }, utf8Out) == 0, "CLI com caminhos UTF-8 acentuados deve concluir com codigo 0");

    for (const QString& p : {utf8Dxf, utf8Ifc, utf8Csv, utf8Vtk, utf8Json}) {
        QFile f(p);
        TEST_ASSERT(f.exists() && f.size() > 32, "Arquivo exportado em caminho UTF-8 deve existir e nao estar vazio");
    }

    std::cout << "[OK] testCliOmniFrameIntegrationAndUtf8Paths\n";
#endif
}

void testSecureTerminalLaunchSpec() {
    const std::filesystem::path hostileCli(
        L"C:\\Users\\O'Connor (Test)\\OmniFrame $(calc) \\omniframe-cli.exe");
    const std::filesystem::path hostileWorkDir(
        L"D:\\Projetos (2026)\\O'Brien & Co");

    const auto psSpec = os::buildInteractiveCliTerminalSpec(
        os::TerminalShell::PowerShell, hostileCli, hostileWorkDir);
    const auto cmdSpec = os::buildInteractiveCliTerminalSpec(
        os::TerminalShell::Cmd, hostileCli, hostileWorkDir);

#ifdef _WIN32
    const QString psAppQ = QString::fromStdWString(psSpec.applicationPath);
    const QString cmdAppQ = QString::fromStdWString(cmdSpec.applicationPath);
    TEST_ASSERT(psAppQ.contains(QStringLiteral("\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"),
                                Qt::CaseInsensitive),
                "PowerShell deve resolver absolutamente dentro de System32 ([AUD-008])");
    TEST_ASSERT(cmdAppQ.contains(QStringLiteral("\\System32\\cmd.exe"), Qt::CaseInsensitive),
                "CMD deve resolver absolutamente dentro de System32 ([AUD-008])");
#endif

    // PowerShell deve usar aspas simples literais (duplicando apostrofos '') e nao interpolar $(calc)
    const QString psCmdQ = QString::fromStdWString(psSpec.commandLine);
    TEST_ASSERT(psCmdQ.contains(QStringLiteral("& 'C:\\Users\\O''Connor (Test)\\OmniFrame $(calc) \\omniframe-cli.exe' --version")),
                "PowerShell deve qualificar caminho completo entre aspas simples literais com escape '' ([AUD-008])");
    TEST_ASSERT(!psCmdQ.contains(QStringLiteral("\"& ")),
                "PowerShell nao pode envolver script ou $(...) sem quoting literal");

    const QString cmdCmdQ = QString::fromStdWString(cmdSpec.commandLine);
    TEST_ASSERT(cmdCmdQ.contains(QStringLiteral("\"C:\\Users\\O'Connor (Test)\\OmniFrame $(calc) \\omniframe-cli.exe\" --version")),
                "CMD deve qualificar caminho completo entre aspas ([AUD-008])");

    std::cout << "[OK] testSecureTerminalLaunchSpec\n";
}

void testCliMeshExportAndTauXzReport() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser criado");

    // 1. CLI --export ifc e --export dxf deve preservar model.surfaceMesh() ([AUD-013])
#ifdef OMNIFRAME_CLI_PATH
    {
        StructuralModel surfModel;
        surfModel.getMaterials().clear();
        surfModel.getCrossSections().clear();
        const int matId = surfModel.addMaterial(Material::makeConcreteC30(1));
        const int secId = surfModel.addCrossSection(CrossSection::makeRectangular(1, "Pilar", 0.30, 0.30));
        const int b1 = surfModel.addNode(0.0, 0.0, 0.0);
        const int t1 = surfModel.addNode(0.0, 0.0, 3.0);
        surfModel.findNode(b1)->support = SupportRestraint::makeFixed();
        surfModel.addElement(b1, t1, matId, secId);

        surfModel.surfaceMesh() = MeshGenerator::generateStructuredQuadGrid(
            KVec3(0.0, 0.0, 3.0), KVec3(4.0, 0.0, 0.0), KVec3(0.0, 4.0, 0.0), 2, 2, false);
        for (auto& me : surfModel.surfaceMesh().elements) {
            me.materialId = matId;
            me.thickness = 0.275;
            me.normalPressure = -5.0;
        }

        const QString projPath = tmp.filePath("cli_mesh_export.omniframe");
        const QString outIfc = tmp.filePath("cli_mesh_out.ifc");
        const QString outDxf = tmp.filePath("cli_mesh_out.dxf");
        TEST_ASSERT(ProjectSerializer::saveToFile(projPath, surfModel), "Salvar modelo com malha");

        QProcess proc;
        proc.start(QString::fromUtf8(OMNIFRAME_CLI_PATH), {
            "--analyze", projPath, "--analysis", "static",
            "--export", "ifc", outIfc,
            "--export", "dxf", outDxf,
            "--json"
        });
        TEST_ASSERT(proc.waitForStarted(10000) && proc.waitForFinished(30000) && proc.exitCode() == 0,
                    "CLI --export ifc/dxf com surfaceMesh deve retornar 0");

        StructuralModel reIfc;
        MeshContainer meshIfc;
        io::IfcInteropReport repIfc;
        TEST_ASSERT(io::IfcInterop::importFromFile(outIfc.toStdString(), reIfc, &meshIfc, nullptr, &repIfc) &&
                        meshIfc.elements.size() == 4 &&
                        std::abs(meshIfc.elements[0].thickness - 0.275) < 1e-6,
                    "CLI --export ifc deve exportar model.surfaceMesh() com espessura real ([AUD-013])");

        StructuralModel reDxf;
        MeshContainer meshDxf;
        io::DxfInteropReport repDxf;
        TEST_ASSERT(io::DxfInterop::importFromFile(outDxf.toStdString(), reDxf, &meshDxf, io::DxfImportOptions{}, &repDxf) &&
                        meshDxf.elements.size() == 4,
                    "CLI --export dxf deve exportar model.surfaceMesh() como 3DFACE ([AUD-013])");
    }
#endif

    // 2. Relatorios HTML/Markdown de solido sob cisalhamento puro tau_xz (Voigt index 5) ([AUD-013])
    {
        StructuralModel solidModel;
        solidModel.getMaterials().clear();
        solidModel.getCrossSections().clear();
        const int matId = solidModel.addMaterial(Material::makeConcreteC30(1));
        solidModel.surfaceMesh() = MeshGenerator::generateStructuredHexGrid(
            KVec3::Zero(), KVec3(1.0, 1.0, 1.0), 1, 1, 1, false, "BlocoHex");
        for (auto& me : solidModel.surfaceMesh().elements) me.materialId = matId;

        SolverResult res;
        res.success = true;
        res.totalSurfaceElements = 1;
        ContinuumElementResult ce;
        ce.elementId = 1;
        ce.isShell = false;
        ce.isSolid = true;
        ce.cellTypeName = "Hex8";
        ce.vonMisesStress = 213.82;
        ce.maxPrincipalStress = 123.45;
        ce.minPrincipalStress = -123.45;
        // Voigt: [sxx, syy, szz, tau_xy, tau_yz, tau_xz]
        ce.stressVoigt = {0.0, 0.0, 0.0, 0.0, 11.11, 123.45};
        res.continuumElementResults.push_back(ce);
        res.maxSolidVonMises = 213.82;

        const QString htmlPath = tmp.filePath("tau_xz.html");
        const QString mdPath = tmp.filePath("tau_xz.md");
        TEST_ASSERT(ProjectSerializer::exportTechnicalReportHtml(htmlPath, solidModel, res),
                    "Exportar HTML com tau_xz deve suceder");
        TEST_ASSERT(ProjectSerializer::exportTechnicalReportMarkdown(mdPath, solidModel, res),
                    "Exportar Markdown com tau_xz deve suceder");

        QFile hf(htmlPath), mf(mdPath);
        TEST_ASSERT(hf.open(QIODevice::ReadOnly | QIODevice::Text) && mf.open(QIODevice::ReadOnly | QIODevice::Text),
                    "Abrir relatorios HTML e MD");
        const QString htmlText = QString::fromUtf8(hf.readAll());
        const QString mdText = QString::fromUtf8(mf.readAll());
        TEST_ASSERT(htmlText.contains(QStringLiteral("123.45 kPa")) && !htmlText.contains(QStringLiteral("11.11 kPa")),
                    "Relatorio HTML deve reportar tau_xz = stressVoigt[5] = 123.45 kPa ([AUD-013])");
        TEST_ASSERT(mdText.contains(QStringLiteral("123.45 kPa")) && !mdText.contains(QStringLiteral("11.11 kPa")),
                    "Relatorio Markdown deve reportar tau_xz = stressVoigt[5] = 123.45 kPa ([AUD-013])");
    }

    std::cout << "[OK] testCliMeshExportAndTauXzReport\n";
}

void testCliStrictParsingAndJsonErrors() {
#ifndef OMNIFRAME_CLI_PATH
    TEST_ASSERT(false, "OMNIFRAME_CLI_PATH nao definido no CMake");
#else
    const QString cliExe = QString::fromUtf8(OMNIFRAME_CLI_PATH);
    auto expectFailJson = [&](const QStringList& args, const char* label) {
        QProcess proc;
        proc.start(cliExe, args);
        TEST_ASSERT(proc.waitForStarted(10000) && proc.waitForFinished(30000), label);
        TEST_ASSERT(proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 1,
                    std::string(label) + " (esperado exit code 1)");
        const QByteArray out = proc.readAllStandardOutput().trimmed();
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(out, &perr);
        TEST_ASSERT(perr.error == QJsonParseError::NoError && doc.isObject() &&
                        doc.object().contains("success") && !doc.object()["success"].toBool() &&
                        !doc.object()["error"].toString().isEmpty(),
                    std::string(label) + " (JSON invalido ou success != false)");
    };

    expectFailJson({"--example", "building", "--analysis", "modal", "--modes", "abc", "--json"},
                   "CLI --modes abc");
    expectFailJson({"--example", "building", "--analysis", "modal", "--modes", "4xyz", "--json"},
                   "CLI --modes 4xyz");
    expectFailJson({"--example", "building", "--analysis", "transient", "--dt", "nan", "--json"},
                   "CLI --dt nan");
    expectFailJson({"--example", "building", "--analysis", "transient", "--dt", "-1", "--json"},
                   "CLI --dt -1");
    expectFailJson({"--example", "building", "--analysis", "nonlinear", "--load-factor", "inf", "--json"},
                   "CLI --load-factor inf");
    expectFailJson({"--analyze", "C:\\inexistente\\pasta\"especial\\modelo.omniframe", "--json"},
                   "CLI caminho Windows com barras invertidas e aspas");

    std::cout << "[OK] testCliStrictParsingAndJsonErrors\n";
#endif
}

void testExtremeCoordinatesNoUb() {
    QTemporaryDir tmp;
    TEST_ASSERT(tmp.isValid(), "QTemporaryDir deve ser criado para teste de coordenadas extremas");

    // 1. DXF com coordenadas 1e15 nao pode causar overflow UB em std::llround ([AUD-021])
    const std::string badDxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\n0\n10\n1e15\n20\n0.0\n30\n0.0\n11\n1e15\n21\n5.0\n31\n0.0\n"
        "0\nENDSEC\n0\nEOF\n";
    const QString dxfPath = tmp.filePath("extreme_1e15.dxf");
    {
        QFile f(dxfPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Gravar DXF 1e15");
        f.write(badDxf.data(), static_cast<qint64>(badDxf.size()));
    }
    StructuralModel dxfModel;
    MeshContainer dxfMesh;
    io::DxfInteropReport dxfRep;
    const bool impOk = io::DxfInterop::importFromFile(dxfPath.toStdString(), dxfModel, &dxfMesh, io::DxfImportOptions{}, &dxfRep);
    TEST_ASSERT(!impOk && dxfModel.getNodes().empty(),
                "Importacao DXF com coordenadas 1e15 deve falhar de forma segura sem UB ([AUD-021])");

    // 2. StructuralModel com coordenada 1e15 deve falhar em validate() e solve() sem UB
    StructuralModel extModel;
    const int n1 = extModel.addNode(0.0, 0.0, 0.0);
    const int n2 = extModel.addNode(1e15, 0.0, 0.0);
    extModel.findNode(n1)->support = SupportRestraint::makeFixed();
    extModel.addElement(n1, n2, 1, 1);
    const ModelValidation val = extModel.validate();
    TEST_ASSERT(!val.valid, "Modelo com coordenada 1e15 deve ser invalido em validate()");
    const SolverResult sr = FemSolver3D::solve(extModel);
    TEST_ASSERT(!sr.success, "Solver deve rejeitar coordenadas 1e15 sem overflow de llround");

    std::cout << "[OK] testExtremeCoordinatesNoUb\n";
}

void testAud017DxfAndIfcNanInfAndSizeQuotaRejection() {
    // 1. DXF com NaN / Inf ou excedendo maxFileSizeBytes deve ser rejeitado imediatamente
    {
        const std::string nanDxf =
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLINE\n8\n0\n10\nNaN\n20\n0.0\n30\n0.0\n11\n5.0\n21\n0.0\n31\n0.0\n"
            "0\nENDSEC\n0\nEOF\n";
        StructuralModel mDxf;
        io::DxfInteropReport rep;
        TEST_ASSERT(!io::DxfInterop::importFromString(nanDxf, mDxf, nullptr, io::DxfImportOptions{}, &rep),
                    "DXF com coordenada NaN deve ser rejeitado");

        const std::string validDxf =
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n5.0\n21\n0.0\n31\n0.0\n"
            "0\nENDSEC\n0\nEOF\n";
        io::DxfImportOptions quotaOpts;
        quotaOpts.maxFileSizeBytes = 10; // Menor que validDxf.size()
        TEST_ASSERT(!io::DxfInterop::importFromString(validDxf, mDxf, nullptr, quotaOpts, &rep),
                    "DXF excedendo maxFileSizeBytes deve ser rejeitado");
    }

    // 2. IFC com NaN / Inf em IFCCARTESIANPOINT ou excedendo maxFileSizeBytes deve ser rejeitado
    {
        const std::string nanIfc =
            "ISO-10303-21;\nHEADER;\nFILE_SCHEMA(('IFC4'));\nENDSEC;\nDATA;\n"
            "#10=IFCCARTESIANPOINT((0.0,NaN,0.0));\n"
            "#11=IFCVERTEXPOINT(#10);\n"
            "#12=IFCSTRUCTURALPOINTCONNECTION('0001',$,'N1',$,$,$,#11,$);\n"
            "ENDSEC;\nEND-ISO-10303-21;\n";
        StructuralModel mIfc;
        io::IfcInteropReport rep;
        TEST_ASSERT(!io::IfcInterop::importFromString(nanIfc, mIfc, nullptr, nullptr, &rep),
                    "IFC com coordenada NaN em IFCCARTESIANPOINT deve ser rejeitado");

        const std::string validIfc =
            "ISO-10303-21;\nHEADER;\nFILE_SCHEMA(('IFC4'));\nENDSEC;\nDATA;\n"
            "#10=IFCCARTESIANPOINT((0.0,0.0,0.0));\n"
            "#11=IFCVERTEXPOINT(#10);\n"
            "#12=IFCSTRUCTURALPOINTCONNECTION('0001',$,'N1',$,$,$,#11,$);\n"
            "ENDSEC;\nEND-ISO-10303-21;\n";
        TEST_ASSERT(!io::IfcInterop::importFromString(validIfc, mIfc, nullptr, nullptr, &rep, 16),
                    "IFC excedendo maxFileSizeBytes deve ser rejeitado");
    }

    std::cout << "[OK] testAud017DxfAndIfcNanInfAndSizeQuotaRejection\n";
}

} // namespace

int main() {
    testMeshGenerator1D2D3DAndExtrusionSolve();
    testDxfRoundtripAndMeshExport();
    testIfc4RoundtripAndDeterministicGuids();
    testHeadlessCliEndToEnd();
    testContinuumSerializationAndCliTransientCase();
    testCliOmniFrameIntegrationAndUtf8Paths();
    testSecureTerminalLaunchSpec();
    testCliMeshExportAndTauXzReport();
    testCliStrictParsingAndJsonErrors();
    testExtremeCoordinatesNoUb();
    testAud017DxfAndIfcNanInfAndSizeQuotaRejection();
    std::cout << "\nTodos os testes da Fase 7 (Malha 1D/2D/3D, DXF, IFC4 BIM e CLI headless) passaram!\n";
    return 0;
}
