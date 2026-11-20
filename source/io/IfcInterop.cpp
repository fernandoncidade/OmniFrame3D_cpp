#include "IfcInterop.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace omniframe::io {

namespace {

std::string trimStep(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toUpperStep(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

std::string escapeStepStr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\'') out += "''";
        else if (c != '\r' && c != '\n') out.push_back(c);
    }
    return out;
}

std::string unquoteStep(const std::string& tok) {
    std::string t = trimStep(tok);
    if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') {
        t = t.substr(1, t.size() - 2);
    }
    return t;
}

int parseEntityRef(const std::string& tok) {
    const std::string t = trimStep(tok);
    if (!t.empty() && t.front() == '#') {
        try { return std::stoi(t.substr(1)); } catch (...) { return -1; }
    }
    return -1;
}

std::vector<std::string> splitTopLevelArgs(const std::string& raw) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool inStr = false;
    for (size_t i = 0; i < raw.size(); ++i) {
        const char ch = raw[i];
        if (ch == '\'') {
            if (inStr && i + 1 < raw.size() && raw[i + 1] == '\'') {
                cur += "''";
                ++i;
                continue;
            }
            inStr = !inStr;
            cur.push_back(ch);
            continue;
        }
        if (!inStr) {
            if (ch == '(') depth++;
            else if (ch == ')') depth--;
            else if (ch == ',' && depth == 0) {
                out.push_back(trimStep(cur));
                cur.clear();
                continue;
            }
        }
        cur.push_back(ch);
    }
    if (!cur.empty() || !out.empty()) out.push_back(trimStep(cur));
    return out;
}

std::vector<std::string> parseListTokens(const std::string& rawList) {
    std::string s = trimStep(rawList);
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        s = s.substr(1, s.size() - 2);
        if (trimStep(s).empty()) return {};
        return splitTopLevelArgs(s);
    }
    return {};
}

double parseDoubleSafe(const std::string& s, double def = 0.0) {
    const std::string t = trimStep(s);
    if (t.empty() || t == "$" || t == "*") return def;
    try { return std::stod(t); } catch (...) { return def; }
}

bool parseBoolOrStepBool(const std::string& s) {
    const std::string u = toUpperStep(trimStep(s));
    return (u == ".T." || u == ".TRUE." || u == "TRUE");
}

struct StepEntity {
    int id = 0;
    std::string typeName;
    std::vector<std::string> args;
};

} // namespace

std::string IfcInterop::makeDeterministicGuid(const std::string& seedKey) {
    static constexpr char b64[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
    std::uint64_t h1 = 14695981039346656037ULL;
    std::uint64_t h2 = 0x9e3779b97f4a7c15ULL;
    for (unsigned char c : seedKey) {
        h1 = (h1 ^ c) * 1099511628211ULL;
        h2 = (h2 + c * 0xbf58476d1ce4e5b9ULL) * 0x94d049bb133111ebULL;
    }
    std::string guid(22, '0');
    for (int i = 0; i < 11; ++i) {
        guid[i] = b64[(h1 >> (i * 5)) & 63];
        guid[11 + i] = b64[(h2 >> (i * 5)) & 63];
    }
    guid[0] = b64[(h1 & 3)]; // IFC Base64: primeiro caractere em [0..3]
    return guid;
}

std::string IfcInterop::exportToString(const StructuralModel& model,
                                       const kernel::MeshContainer* shellMesh,
                                       IfcGuidMap* guidMap,
                                       IfcInteropReport* report)
{
    IfcInteropReport rep;
    std::ostringstream data;
    int nextId = 1;
    auto alloc = [&]() { return nextId++; };

    const int idPerson = alloc();
    const int idOrg = alloc();
    const int idPO = alloc();
    const int idApp = alloc();
    const int idOH = alloc();
    const int idUnitLen = alloc();
    const int idUnitForce = alloc();
    const int idUnitPress = alloc();
    const int idUnits = alloc();
    const int idOrigin0 = alloc();
    const int idDirZ = alloc();
    const int idDirX = alloc();
    const int idWCS = alloc();
    const int idCtx = alloc();
    const int idProj = alloc();
    const int idSite = alloc();
    const int idBldg = alloc();
    const int idSam = alloc();
    const int idPlaceWorld = alloc();

    data << "#" << idPerson << "=IFCPERSON($,'OmniFrame','Engineer',$,$,$,$,$);\n";
    data << "#" << idOrg << "=IFCORGANIZATION($,'OmniFrame 3D',$,$,$);\n";
    data << "#" << idPO << "=IFCPERSONANDORGANIZATION(#" << idPerson << ",#" << idOrg << ",$);\n";
    data << "#" << idApp << "=IFCAPPLICATION(#" << idOrg << ",'3.0','OmniFrame 3D','OmniFrame3D');\n";
    data << "#" << idOH << "=IFCOWNERHISTORY(#" << idPO << ",#" << idApp << ",$,.ADDED.,$,$,$,0);\n";
    data << "#" << idUnitLen << "=IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.);\n";
    data << "#" << idUnitForce << "=IFCSIUNIT(*,.FORCEUNIT.,.KILO.,.NEWTON.);\n";
    data << "#" << idUnitPress << "=IFCSIUNIT(*,.PRESSUREUNIT.,.KILO.,.PASCAL.);\n";
    data << "#" << idUnits << "=IFCUNITASSIGNMENT((#" << idUnitLen << ",#" << idUnitForce << ",#" << idUnitPress << "));\n";
    data << "#" << idOrigin0 << "=IFCCARTESIANPOINT((0.,0.,0.));\n";
    data << "#" << idDirZ << "=IFCDIRECTION((0.,0.,1.));\n";
    data << "#" << idDirX << "=IFCDIRECTION((1.,0.,0.));\n";
    data << "#" << idWCS << "=IFCAXIS2PLACEMENT3D(#" << idOrigin0 << ",#" << idDirZ << ",#" << idDirX << ");\n";
    data << "#" << idCtx << "=IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-06,#" << idWCS << ",$);\n";
    data << "#" << idPlaceWorld << "=IFCLOCALPLACEMENT($,#" << idWCS << ");\n";
    data << "#" << idProj << "=IFCPROJECT('" << makeDeterministicGuid("proj") << "',#" << idOH << ",'OmniFrame Project',$,$,$,$,(#" << idCtx << "),#" << idUnits << ");\n";
    data << "#" << idSite << "=IFCSITE('" << makeDeterministicGuid("site") << "',#" << idOH << ",'Site',$,$,#" << idPlaceWorld << ",$,$,.ELEMENT.,$,$,$,$,$);\n";
    data << "#" << idBldg << "=IFCBUILDING('" << makeDeterministicGuid("bldg") << "',#" << idOH << ",'Building',$,$,#" << idPlaceWorld << ",$,$,.ELEMENT.,$,$,$);\n";
    data << "#" << idSam << "=IFCSTRUCTURALANALYSISMODEL('" << makeDeterministicGuid("sam") << "',#" << idOH
         << ",'OmniFrame Structural Model',$,$,.THREE_DIMENSIONAL_3D.,#" << idWCS << ",$,$,$);\n";

    // Materiais e propriedades
    std::unordered_map<MaterialId, int> matStepId;
    for (const auto& mat : model.getMaterials()) {
        const int mid = alloc();
        matStepId[mat.id] = mid;
        if (guidMap) {
            guidMap->materialGuids[mat.id] = makeDeterministicGuid("mat_" + std::to_string(mat.id));
        }
        data << "#" << mid << "=IFCMATERIAL('" << escapeStepStr(mat.name) << "',$,$);\n";
        const int mpE = alloc(), mpNu = alloc(), mpRho = alloc(), mpProps = alloc();
        data << "#" << mpE << "=IFCPROPERTYSINGLEVALUE('YoungModulus',$,IFCREAL(" << std::setprecision(10) << mat.E << "),$);\n";
        data << "#" << mpNu << "=IFCPROPERTYSINGLEVALUE('PoissonRatio',$,IFCREAL(" << std::setprecision(10) << mat.nu << "),$);\n";
        data << "#" << mpRho << "=IFCPROPERTYSINGLEVALUE('MassDensity',$,IFCREAL(" << std::setprecision(10) << mat.rho << "),$);\n";
        data << "#" << mpProps << "=IFCMATERIALPROPERTIES('Pset_MaterialMechanical',(#" << mpE << ",#" << mpNu << ",#" << mpRho << "),#" << mid << ");\n";
    }

    // Seções / Perfis
    std::unordered_map<SectionId, int> secProfStepId;
    for (const auto& sec : model.getCrossSections()) {
        const int pid = alloc();
        secProfStepId[sec.id] = pid;
        if (guidMap) {
            guidMap->sectionGuids[sec.id] = makeDeterministicGuid("sec_" + std::to_string(sec.id));
        }
        if (sec.shape == SectionShape::CircularSolid) {
            data << "#" << pid << "=IFCCIRCLEPROFILEDEF(.AREA.,'" << escapeStepStr(sec.name)
                 << "',$, " << std::setprecision(8) << (0.5 * std::max(0.01, sec.dim1)) << ");\n";
        } else if (sec.shape == SectionShape::CircularTube) {
            data << "#" << pid << "=IFCCIRCLEHOLLOWPROFILEDEF(.AREA.,'" << escapeStepStr(sec.name)
                 << "',$, " << std::setprecision(8) << (0.5 * std::max(0.01, sec.dim1))
                 << "," << std::max(0.001, sec.dim2) << ");\n";
        } else if (sec.shape == SectionShape::IShape || sec.shape == SectionShape::WeldedIShape) {
            const double h = std::max(0.01, sec.dim1);
            const double bf = std::max(0.01, sec.dim2);
            const double tf = std::max(0.001, sec.dim3);
            const double tw = std::max(0.001, sec.dim4);
            data << "#" << pid << "=IFCISHAPEPROFILEDEF(.AREA.,'" << escapeStepStr(sec.name)
                 << "',$, " << std::setprecision(8) << bf << "," << h << ","
                 << tw << "," << tf << ",$);\n";
        } else {
            data << "#" << pid << "=IFCRECTANGLEPROFILEDEF(.AREA.,'" << escapeStepStr(sec.name)
                 << "',$, " << std::setprecision(8) << std::max(0.01, sec.dim1) << ","
                 << std::max(0.01, sec.dim2) << ");\n";
        }
    }

    // Nós -> IFCCARTESIANPOINT + IFCVERTEXPOINT + IFCBOUNDARYNODECONDITION + IFCSTRUCTURALPOINTCONNECTION
    std::unordered_map<NodeId, int> nodePtId;
    std::unordered_map<NodeId, int> nodeVtxId;
    std::unordered_map<NodeId, int> nodeConnId;
    std::vector<int> samMemberIds;

    for (const auto& n : model.getNodes()) {
        const int idPt = alloc();
        const int idVx = alloc();
        const int idConn = alloc();
        nodePtId[n.id] = idPt;
        nodeVtxId[n.id] = idVx;
        nodeConnId[n.id] = idConn;

        data << "#" << idPt << "=IFCCARTESIANPOINT((" << std::setprecision(12)
             << n.position.x << "," << n.position.y << "," << n.position.z << "));\n";
        data << "#" << idVx << "=IFCVERTEXPOINT(#" << idPt << ");\n";

        std::string bcRef = "$";
        if (n.support.hasFixedDofs() || n.support.hasSprings()) {
            const int idBc = alloc();
            auto bcToken = [&](int d) -> std::string {
                if (n.support.fixed[d]) return "IFCBOOLEAN(.T.)";
                if (n.support.springStiffness[d] > 0.0) {
                    std::ostringstream ss;
                    ss << (d < 3 ? "IFCLINEARSTIFFNESSMEASURE(" : "IFCROTATIONALSTIFFNESSMEASURE(")
                       << std::setprecision(10) << n.support.springStiffness[d] << ")";
                    return ss.str();
                }
                return "IFCBOOLEAN(.F.)";
            };
            data << "#" << idBc << "=IFCBOUNDARYNODECONDITION('Sup_" << n.id << "',"
                 << bcToken(0) << "," << bcToken(1) << "," << bcToken(2) << ","
                 << bcToken(3) << "," << bcToken(4) << "," << bcToken(5) << ");\n";
            bcRef = "#" + std::to_string(idBc);
        }

        std::string guid = makeDeterministicGuid("node_" + std::to_string(n.id));
        if (guidMap) {
            auto it = guidMap->nodeGuids.find(n.id);
            if (it != guidMap->nodeGuids.end() && !it->second.empty()) guid = it->second;
            else guidMap->nodeGuids[n.id] = guid;
        }
        data << "#" << idConn << "=IFCSTRUCTURALPOINTCONNECTION('" << guid << "',#" << idOH
             << ",'Node_" << n.id << "',$,$,#" << idPlaceWorld << ",#" << idVx << "," << bcRef << ",$);\n";
        samMemberIds.push_back(idConn);
    }

    // Elementos de pórtico/treliça 1D -> IFCEDGE + IFCSTRUCTURALCURVEMEMBER
    for (const auto& el : model.getElements()) {
        if (!nodeVtxId.count(el.nodeStart) || !nodeVtxId.count(el.nodeEnd)) continue;
        const int idEdge = alloc();
        const int idMem = alloc();
        data << "#" << idEdge << "=IFCEDGE(#" << nodeVtxId[el.nodeStart] << ",#" << nodeVtxId[el.nodeEnd] << ");\n";

        std::string guid = makeDeterministicGuid("elem_" + std::to_string(el.id));
        if (guidMap) {
            auto it = guidMap->elementGuids.find(el.id);
            if (it != guidMap->elementGuids.end() && !it->second.empty()) guid = it->second;
            else guidMap->elementGuids[el.id] = guid;
        }
        const char* predef = (el.formulation == ElementFormulation::Truss3D)
            ? ".PIN_JOINED_MEMBER."
            : ".RIGID_JOINED_MEMBER.";
        data << "#" << idMem << "=IFCSTRUCTURALCURVEMEMBER('" << guid << "',#" << idOH
             << ",'Elem_" << el.id << "',$,$,#" << idPlaceWorld << ",#" << idEdge << ","
             << predef << ",#" << idDirZ << ");\n";
        samMemberIds.push_back(idMem);

        if (matStepId.count(el.materialId) && secProfStepId.count(el.sectionId)) {
            const int idMp = alloc();
            const int idMps = alloc();
            const int idRelM = alloc();
            data << "#" << idMp << "=IFCMATERIALPROFILE('MP_" << el.id << "',$,#" << matStepId[el.materialId]
                 << ",#" << secProfStepId[el.sectionId] << ",$,$);\n";
            data << "#" << idMps << "=IFCMATERIALPROFILESET($,$,(#" << idMp << "),$);\n";
            data << "#" << idRelM << "=IFCRELASSOCIATESMATERIAL('" << makeDeterministicGuid("relm_" + std::to_string(el.id))
                 << "',#" << idOH << ",$,$,(#" << idMem << "),#" << idMps << ");\n";
        }
    }

    // Elementos de casca 2D opcionais (MeshContainer) -> IFCPOLYLOOP + IFCSTRUCTURALSURFACEMEMBER
    if (shellMesh && !shellMesh->elements.empty()) {
        std::unordered_map<kernel::NodeId, int> mPtId;
        for (const auto& mn : shellMesh->nodes) {
            const int idPt = alloc();
            mPtId[mn.id] = idPt;
            data << "#" << idPt << "=IFCCARTESIANPOINT((" << std::setprecision(12)
                 << mn.position.x() << "," << mn.position.y() << "," << mn.position.z() << "));\n";
        }
        for (const auto& cell : shellMesh->elements) {
            if (cell.type != kernel::MeshCellType::Tri3 && cell.type != kernel::MeshCellType::Quad4) continue;
            const int idLoop = alloc();
            const int idBound = alloc();
            const int idFace = alloc();
            const int idSurf = alloc();
            data << "#" << idLoop << "=IFCPOLYLOOP((";
            const size_t nLoop = (cell.type == kernel::MeshCellType::Tri3) ? 3 : 4;
            for (size_t k = 0; k < nLoop && k < cell.nodeIds.size(); ++k) {
                if (k > 0) data << ",";
                data << "#" << mPtId[cell.nodeIds[k]];
            }
            data << "));\n";
            data << "#" << idBound << "=IFCFACEOUTERBOUND(#" << idLoop << ",.T.);\n";
            data << "#" << idFace << "=IFCFACE((#" << idBound << "));\n";
            const std::string sGuid = makeDeterministicGuid("shell_" + std::to_string(cell.id));
            data << "#" << idSurf << "=IFCSTRUCTURALSURFACEMEMBER('" << sGuid << "',#" << idOH
                 << ",'Shell_" << cell.id << "',$,$,#" << idPlaceWorld << ",#" << idFace << ",.SHELL.,0.15);\n";
            samMemberIds.push_back(idSurf);
        }
    }

    if (!samMemberIds.empty()) {
        const int idRelGrp = alloc();
        data << "#" << idRelGrp << "=IFCRELASSIGNSTOGROUP('" << makeDeterministicGuid("relgrp")
             << "',#" << idOH << ",'AnalysisGroup',$,(";
        for (size_t k = 0; k < samMemberIds.size(); ++k) {
            if (k > 0) data << ",";
            data << "#" << samMemberIds[k];
        }
        data << "),$,#" << idSam << ");\n";
    }

    std::ostringstream full;
    full << "ISO-10303-21;\n"
         << "HEADER;\n"
         << "FILE_DESCRIPTION(('ViewDefinition [StructuralAnalysisView]'),'2;1');\n"
         << "FILE_NAME('omniframe_export.ifc','2026-01-01T00:00:00',('OmniFrame 3D'),('OmniFrame'),'OmniFrame IFC4','OmniFrame 3D','');\n"
         << "FILE_SCHEMA(('IFC4'));\n"
         << "ENDSEC;\n"
         << "DATA;\n"
         << data.str()
         << "ENDSEC;\n"
         << "END-ISO-10303-21;\n";

    rep.success = true;
    rep.schemaDetected = "IFC4";
    rep.entitiesExported = nextId - 1;
    if (report) *report = rep;
    return full.str();
}

bool IfcInterop::exportToFile(const std::string& filePath,
                              const StructuralModel& model,
                              const kernel::MeshContainer* shellMesh,
                              IfcGuidMap* guidMap,
                              IfcInteropReport* report)
{
    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs.is_open()) {
        if (report) {
            report->success = false;
            report->errorMessage = "Não foi possível abrir arquivo IFC para escrita: " + filePath;
        }
        return false;
    }
    const std::string text = exportToString(model, shellMesh, guidMap, report);
    ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
    return ofs.good();
}

bool IfcInterop::importFromString(const std::string& stepText,
                                  StructuralModel& outModel,
                                  kernel::MeshContainer* outShellMesh,
                                  IfcGuidMap* outGuidMap,
                                  IfcInteropReport* report)
{
    IfcInteropReport rep;
    outModel.clear();
    if (outShellMesh) *outShellMesh = kernel::MeshContainer{};
    if (outGuidMap) *outGuidMap = IfcGuidMap{};

    // Remove comentários /* ... */ de estilo STEP
    std::string cleaned;
    cleaned.reserve(stepText.size());
    for (size_t i = 0; i < stepText.size(); ++i) {
        if (i + 1 < stepText.size() && stepText[i] == '/' && stepText[i + 1] == '*') {
            i += 2;
            while (i + 1 < stepText.size() && !(stepText[i] == '*' && stepText[i + 1] == '/')) ++i;
            ++i;
            continue;
        }
        cleaned.push_back(stepText[i]);
    }

    if (toUpperStep(cleaned).find("IFC2X3") != std::string::npos) rep.schemaDetected = "IFC2X3";
    else if (toUpperStep(cleaned).find("IFC4") != std::string::npos) rep.schemaDetected = "IFC4";
    else rep.schemaDetected = "STEP";

    // Divide em sentenças terminadas por ';' fora de strings '...'
    std::vector<std::string> statements;
    {
        std::string cur;
        bool inStr = false;
        for (size_t i = 0; i < cleaned.size(); ++i) {
            const char c = cleaned[i];
            if (c == '\'') {
                if (inStr && i + 1 < cleaned.size() && cleaned[i + 1] == '\'') {
                    cur += "''";
                    ++i;
                    continue;
                }
                inStr = !inStr;
            }
            if (!inStr && c == ';') {
                const std::string st = trimStep(cur);
                if (!st.empty()) statements.push_back(st);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
    }

    std::unordered_map<int, StepEntity> entities;
    std::vector<int> entityOrder;
    for (const auto& st : statements) {
        if (st.empty() || st.front() != '#') continue;
        const size_t eq = st.find('=');
        const size_t op = st.find('(', eq == std::string::npos ? 0 : eq);
        const size_t cl = st.rfind(')');
        if (eq == std::string::npos || op == std::string::npos || cl == std::string::npos || cl <= op) continue;

        int id = 0;
        try { id = std::stoi(trimStep(st.substr(1, eq - 1))); } catch (...) { continue; }
        StepEntity ent;
        ent.id = id;
        ent.typeName = toUpperStep(trimStep(st.substr(eq + 1, op - eq - 1)));
        ent.args = splitTopLevelArgs(st.substr(op + 1, cl - op - 1));
        entities[id] = ent;
        entityOrder.push_back(id);
    }

    if (entities.empty()) {
        rep.success = false;
        rep.errorMessage = "Nenhuma entidade STEP/IFC encontrada no conteúdo.";
        if (report) *report = rep;
        return false;
    }

    auto getPointCoords = [&](int ptId) -> Vec3 {
        auto it = entities.find(ptId);
        if (it == entities.end() || it->second.args.empty()) return {0, 0, 0};
        const auto toks = parseListTokens(it->second.args[0]);
        const double x = toks.size() > 0 ? parseDoubleSafe(toks[0]) : 0.0;
        const double y = toks.size() > 1 ? parseDoubleSafe(toks[1]) : 0.0;
        const double z = toks.size() > 2 ? parseDoubleSafe(toks[2]) : 0.0;
        return {x, y, z};
    };

    auto getVertexPointCoords = [&](int vtxId) -> Vec3 {
        auto it = entities.find(vtxId);
        if (it == entities.end()) return {0, 0, 0};
        if (it->second.typeName == "IFCCARTESIANPOINT") return getPointCoords(vtxId);
        if (it->second.typeName == "IFCVERTEXPOINT" && !it->second.args.empty()) {
            return getPointCoords(parseEntityRef(it->second.args[0]));
        }
        return {0, 0, 0};
    };

    // Mapa espacial de vértices/pontos para nós do StructuralModel
    std::unordered_map<int, NodeId> stepVertexToNode;
    auto ensureNodeAt = [&](const Vec3& p, int stepRefId = -1) -> NodeId {
        if (stepRefId > 0 && stepVertexToNode.count(stepRefId)) return stepVertexToNode[stepRefId];
        for (const auto& existing : outModel.getNodes()) {
            if ((existing.position - p).norm() <= 1e-6) {
                if (stepRefId > 0) stepVertexToNode[stepRefId] = existing.id;
                return existing.id;
            }
        }
        const NodeId nid = outModel.addNode(p.x, p.y, p.z);
        if (stepRefId > 0) stepVertexToNode[stepRefId] = nid;
        return nid;
    };

    // Primeiro lê conexões pontuais estruturais e condições de apoio
    for (int id : entityOrder) {
        const auto& ent = entities[id];
        if (ent.typeName == "IFCSTRUCTURALPOINTCONNECTION" && ent.args.size() >= 7) {
            const std::string guid = unquoteStep(ent.args[0]);
            const int vtxRef = parseEntityRef(ent.args[6]);
            const Vec3 p = getVertexPointCoords(vtxRef);
            const NodeId nid = ensureNodeAt(p, vtxRef);
            if (outGuidMap && !guid.empty()) outGuidMap->nodeGuids[nid] = guid;

            if (ent.args.size() >= 8) {
                const int bcRef = parseEntityRef(ent.args[7]);
                if (bcRef > 0 && entities.count(bcRef)) {
                    const auto& bc = entities[bcRef];
                    Node* nd = outModel.findNode(nid);
                    if (nd && bc.typeName == "IFCBOUNDARYNODECONDITION") {
                        for (int d = 0; d < 6 && (1 + d) < static_cast<int>(bc.args.size()); ++d) {
                            const std::string tok = trimStep(bc.args[1 + d]);
                            if (toUpperStep(tok).find("BOOLEAN") != std::string::npos || tok == ".T.") {
                                if (parseBoolOrStepBool(tok)) nd->support.fixed[d] = true;
                            } else if (toUpperStep(tok).find("STIFFNESSMEASURE") != std::string::npos) {
                                const size_t lp = tok.find('('), rp = tok.rfind(')');
                                if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
                                    nd->support.springStiffness[d] = parseDoubleSafe(tok.substr(lp + 1, rp - lp - 1), 0.0);
                                }
                            }
                        }
                    }
                }
            }
            rep.structuralNodesImported++;
        }
    }

    // Lê membros lineares estruturais (IFCSTRUCTURALCURVEMEMBER)
    for (int id : entityOrder) {
        const auto& ent = entities[id];
        if (ent.typeName == "IFCSTRUCTURALCURVEMEMBER" && ent.args.size() >= 7) {
            const std::string guid = unquoteStep(ent.args[0]);
            const int repRef = parseEntityRef(ent.args[6]);
            if (repRef <= 0 || !entities.count(repRef)) continue;
            const auto& edgeEnt = entities[repRef];
            if (edgeEnt.typeName == "IFCEDGE" && edgeEnt.args.size() >= 2) {
                const int v1Ref = parseEntityRef(edgeEnt.args[0]);
                const int v2Ref = parseEntityRef(edgeEnt.args[1]);
                const NodeId n1 = ensureNodeAt(getVertexPointCoords(v1Ref), v1Ref);
                const NodeId n2 = ensureNodeAt(getVertexPointCoords(v2Ref), v2Ref);
                if (n1 != n2) {
                    const ElementId eid = outModel.addElement(n1, n2, 1, 1);
                    Element* el = outModel.findElement(eid);
                    if (el && ent.args.size() >= 8 &&
                        toUpperStep(ent.args[7]).find("PIN_JOINED") != std::string::npos) {
                        el->formulation = ElementFormulation::Truss3D;
                    }
                    if (outGuidMap && !guid.empty()) outGuidMap->elementGuids[eid] = guid;
                    rep.curveMembersImported++;
                }
            }
        }
    }

    // Lê membros superficiais estruturais (IFCSTRUCTURALSURFACEMEMBER) -> MeshContainer
    if (outShellMesh) {
        auto toMeshNode = [&](const Vec3& p) -> kernel::NodeId {
            const kernel::Vec3 kp(p.x, p.y, p.z);
            for (const auto& existing : outShellMesh->nodes) {
                if ((existing.position - kp).norm() <= 1e-6) return existing.id;
            }
            const kernel::NodeId kid = static_cast<kernel::NodeId>(outShellMesh->nodes.size() + 1);
            outShellMesh->nodes.push_back({kid, kp, false, "IFC_SHELL"});
            return kid;
        };

        for (int id : entityOrder) {
            const auto& ent = entities[id];
            if (ent.typeName == "IFCSTRUCTURALSURFACEMEMBER" && ent.args.size() >= 7) {
                const int faceRef = parseEntityRef(ent.args[6]);
                if (faceRef <= 0 || !entities.count(faceRef)) continue;
                const auto& faceEnt = entities[faceRef];
                if (faceEnt.typeName == "IFCFACE" && !faceEnt.args.empty()) {
                    const auto bToks = parseListTokens(faceEnt.args[0]);
                    if (bToks.empty()) continue;
                    const int boundRef = parseEntityRef(bToks[0]);
                    if (boundRef <= 0 || !entities.count(boundRef)) continue;
                    const auto& boundEnt = entities[boundRef];
                    if (boundEnt.args.empty()) continue;
                    const int loopRef = parseEntityRef(boundEnt.args[0]);
                    if (loopRef <= 0 || !entities.count(loopRef)) continue;
                    const auto& loopEnt = entities[loopRef];
                    if (loopEnt.args.empty()) continue;
                    const auto ptToks = parseListTokens(loopEnt.args[0]);
                    std::vector<kernel::NodeId> nids;
                    for (const auto& ptTok : ptToks) {
                        const int ptRef = parseEntityRef(ptTok);
                        if (ptRef > 0) nids.push_back(toMeshNode(getPointCoords(ptRef)));
                    }
                    if (nids.size() == 3 || nids.size() == 4) {
                        const kernel::ElementId keid = static_cast<kernel::ElementId>(outShellMesh->elements.size() + 1);
                        const kernel::MeshCellType ct = (nids.size() == 3)
                            ? kernel::MeshCellType::Tri3
                            : kernel::MeshCellType::Quad4;
                        outShellMesh->elements.push_back({keid, ct, std::move(nids), "IFC_SHELL"});
                        rep.surfaceMembersImported++;
                    }
                }
            }
        }
    }

    rep.success = (rep.structuralNodesImported > 0 || rep.curveMembersImported > 0 || rep.surfaceMembersImported > 0);
    if (!rep.success) {
        rep.errorMessage = "Nenhum membro estrutural IFC reconhecido no arquivo.";
    }
    if (report) *report = rep;
    return rep.success;
}

bool IfcInterop::importFromFile(const std::string& filePath,
                                StructuralModel& outModel,
                                kernel::MeshContainer* outShellMesh,
                                IfcGuidMap* outGuidMap,
                                IfcInteropReport* report)
{
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) {
        if (report) {
            report->success = false;
            report->errorMessage = "Não foi possível abrir arquivo IFC: " + filePath;
        }
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return importFromString(ss.str(), outModel, outShellMesh, outGuidMap, report);
}

} // namespace omniframe::io
