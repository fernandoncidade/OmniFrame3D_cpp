#include "DxfInterop.hpp"
#include "Utf8Path.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace omniframe::io {

namespace {

std::string trimCopy(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toUpperCopy(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

class SpatialNodeWelder {
public:
    explicit SpatialNodeWelder(double tol) : m_tol(std::max(1e-12, tol)) {}

    int mergedDuplicates() const { return m_duplicates; }

    NodeId findOrCreateNode(const Vec3& p, StructuralModel& model) {
        return findOrCreateGeneric(p, [&](const Vec3& pt) {
            return model.addNode(pt.x, pt.y, pt.z);
        });
    }

    kernel::NodeId findOrCreateMeshNode(const Vec3& p, kernel::MeshContainer& mesh, const std::string& tag) {
        return static_cast<kernel::NodeId>(findOrCreateGeneric(p, [&](const Vec3& pt) {
            const kernel::NodeId mid = static_cast<kernel::NodeId>(mesh.nodes.size() + 1);
            mesh.nodes.push_back({mid, {pt.x, pt.y, pt.z}, false, tag});
            return static_cast<NodeId>(mid);
        }));
    }

private:
    template <typename AddNodeFn>
    NodeId findOrCreateGeneric(const Vec3& p, AddNodeFn&& addFn) {
        const double inv = 1.0 / m_tol;
        const auto key = std::make_tuple(
            static_cast<long long>(std::llround(p.x * inv)),
            static_cast<long long>(std::llround(p.y * inv)),
            static_cast<long long>(std::llround(p.z * inv)));
        auto it = m_grid.find(key);
        if (it != m_grid.end()) {
            for (NodeId candId : it->second) {
                auto pit = m_positions.find(candId);
                if (pit != m_positions.end() && (pit->second - p).norm() <= m_tol) {
                    m_duplicates++;
                    return candId;
                }
            }
        }
        const NodeId nid = addFn(p);
        m_positions.emplace(nid, p);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto k2 = std::make_tuple(
                        std::get<0>(key) + dx,
                        std::get<1>(key) + dy,
                        std::get<2>(key) + dz);
                    m_grid[k2].push_back(nid);
                }
            }
        }
        return nid;
    }

    struct KeyHash {
        size_t operator()(const std::tuple<long long, long long, long long>& t) const noexcept {
            size_t h1 = std::hash<long long>{}(std::get<0>(t));
            size_t h2 = std::hash<long long>{}(std::get<1>(t));
            size_t h3 = std::hash<long long>{}(std::get<2>(t));
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)) ^ (h3 << 1);
        }
    };
    double m_tol;
    int m_duplicates = 0;
    std::unordered_map<NodeId, Vec3> m_positions;
    std::unordered_map<std::tuple<long long, long long, long long>, std::vector<NodeId>, KeyHash> m_grid;
};

void writeGroup(std::ostringstream& os, int code, const std::string& val) {
    os << code << "\n" << val << "\n";
}

void writeDouble(std::ostringstream& os, int code, double v) {
    os << code << "\n" << std::setprecision(12) << v << "\n";
}

} // namespace

std::string DxfInterop::exportToString(const StructuralModel& model,
                                       const kernel::MeshContainer* surfaceOrSolidMesh,
                                       DxfInteropReport* report)
{
    DxfInteropReport localRep;
    std::ostringstream os;
    writeGroup(os, 0, "SECTION");
    writeGroup(os, 2, "HEADER");
    writeGroup(os, 9, "$ACADVER");
    writeGroup(os, 1, "AC1015");
    writeGroup(os, 9, "$INSUNITS");
    writeGroup(os, 70, "6"); // Metros
    writeGroup(os, 0, "ENDSEC");

    writeGroup(os, 0, "SECTION");
    writeGroup(os, 2, "ENTITIES");

    for (const auto& el : model.getElements()) {
        const Node* n1 = model.findNode(el.nodeStart);
        const Node* n2 = model.findNode(el.nodeEnd);
        if (!n1 || !n2) continue;
        writeGroup(os, 0, "LINE");
        writeGroup(os, 8, "OMNIFRAME_FRAME");
        writeDouble(os, 10, n1->position.x);
        writeDouble(os, 20, n1->position.y);
        writeDouble(os, 30, n1->position.z);
        writeDouble(os, 11, n2->position.x);
        writeDouble(os, 21, n2->position.y);
        writeDouble(os, 31, n2->position.z);
        localRep.lineCount++;
    }

    if (surfaceOrSolidMesh) {
        std::unordered_map<kernel::NodeId, kernel::Vec3> npos;
        for (const auto& n : surfaceOrSolidMesh->nodes) npos[n.id] = n.position;
        for (const auto& cell : surfaceOrSolidMesh->elements) {
            if (cell.type == kernel::MeshCellType::Line2 && cell.nodeIds.size() == 2) {
                const auto& p0 = npos[cell.nodeIds[0]];
                const auto& p1 = npos[cell.nodeIds[1]];
                writeGroup(os, 0, "LINE");
                writeGroup(os, 8, cell.groupTag.empty() ? "OMNIFRAME_MESH" : cell.groupTag);
                writeDouble(os, 10, p0.x()); writeDouble(os, 20, p0.y()); writeDouble(os, 30, p0.z());
                writeDouble(os, 11, p1.x()); writeDouble(os, 21, p1.y()); writeDouble(os, 31, p1.z());
                localRep.lineCount++;
            } else if ((cell.type == kernel::MeshCellType::Tri3 ||
                        cell.type == kernel::MeshCellType::Quad4 ||
                        cell.type == kernel::MeshCellType::Quad8) && cell.nodeIds.size() >= 3) {
                const auto& p0 = npos[cell.nodeIds[0]];
                const auto& p1 = npos[cell.nodeIds[1]];
                const auto& p2 = npos[cell.nodeIds[2]];
                const auto& p3 = (cell.nodeIds.size() >= 4) ? npos[cell.nodeIds[3]] : p2;
                writeGroup(os, 0, "3DFACE");
                writeGroup(os, 8, cell.groupTag.empty() ? "OMNIFRAME_SHELL" : cell.groupTag);
                writeDouble(os, 10, p0.x()); writeDouble(os, 20, p0.y()); writeDouble(os, 30, p0.z());
                writeDouble(os, 11, p1.x()); writeDouble(os, 21, p1.y()); writeDouble(os, 31, p1.z());
                writeDouble(os, 12, p2.x()); writeDouble(os, 22, p2.y()); writeDouble(os, 32, p2.z());
                writeDouble(os, 13, p3.x()); writeDouble(os, 23, p3.y()); writeDouble(os, 33, p3.z());
                localRep.faceCount++;
            }
        }
    }

    writeGroup(os, 0, "ENDSEC");
    writeGroup(os, 0, "EOF");
    localRep.success = true;
    localRep.entitiesRead = localRep.lineCount + localRep.faceCount;
    if (report) *report = localRep;
    return os.str();
}

bool DxfInterop::exportToFile(const std::string& filePath,
                              const StructuralModel& model,
                              const kernel::MeshContainer* surfaceOrSolidMesh,
                              DxfInteropReport* report)
{
    std::ofstream ofs(pathFromUtf8(filePath), std::ios::binary);
    if (!ofs.is_open()) {
        if (report) {
            report->success = false;
            report->errorMessage = "Não foi possível abrir arquivo DXF para escrita: " + filePath;
        }
        return false;
    }
    const std::string content = exportToString(model, surfaceOrSolidMesh, report);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return ofs.good();
}

bool DxfInterop::importFromString(const std::string& dxfText,
                                  StructuralModel& outModel,
                                  kernel::MeshContainer* outSurfaceMesh,
                                  const DxfImportOptions& opts,
                                  DxfInteropReport* report)
{
    DxfInteropReport rep;
    outModel.clear();
    if (outSurfaceMesh) *outSurfaceMesh = kernel::MeshContainer{};

    std::istringstream iss(dxfText);
    std::string codeLine, valLine;
    std::vector<std::pair<int, std::string>> pairs;
    while (std::getline(iss, codeLine) && std::getline(iss, valLine)) {
        codeLine = trimCopy(codeLine);
        valLine = trimCopy(valLine);
        if (!valLine.empty() && valLine.back() == '\r') valLine.pop_back();
        int code = 0;
        try { code = std::stoi(codeLine); } catch (...) { continue; }
        pairs.emplace_back(code, valLine);
    }

    bool inEntities = false;
    std::set<std::string> layers;
    SpatialNodeWelder welder(opts.nodeMergeTolerance);
    SpatialNodeWelder meshWelder(opts.nodeMergeTolerance);

    auto parseDouble = [](const std::string& s, double def = 0.0) -> double {
        try { return std::stod(s); } catch (...) { return def; }
    };

    size_t i = 0;
    while (i < pairs.size()) {
        if (pairs[i].first == 0 && toUpperCopy(pairs[i].second) == "SECTION") {
            if (i + 1 < pairs.size() && pairs[i + 1].first == 2 && toUpperCopy(pairs[i + 1].second) == "ENTITIES") {
                inEntities = true;
                i += 2;
                continue;
            }
        }
        if (pairs[i].first == 0 && toUpperCopy(pairs[i].second) == "ENDSEC") {
            inEntities = false;
            i++;
            continue;
        }
        if (!inEntities || pairs[i].first != 0) {
            i++;
            continue;
        }

        const std::string entType = toUpperCopy(pairs[i].second);
        ++i;
        std::vector<std::pair<int, std::string>> props;
        while (i < pairs.size() && pairs[i].first != 0) {
            props.push_back(pairs[i]);
            ++i;
        }

        std::string layer = "0";
        for (const auto& [c, v] : props) {
            if (c == 8) layer = v;
        }
        if (!opts.layerFilter.empty() && layer != opts.layerFilter) continue;
        layers.insert(layer);

        if (entType == "LINE") {
            Vec3 p0{0, 0, 0}, p1{0, 0, 0};
            for (const auto& [c, v] : props) {
                if (c == 10) p0.x = parseDouble(v);
                else if (c == 20) p0.y = parseDouble(v);
                else if (c == 30) p0.z = parseDouble(v);
                else if (c == 11) p1.x = parseDouble(v);
                else if (c == 21) p1.y = parseDouble(v);
                else if (c == 31) p1.z = parseDouble(v);
            }
            if ((p1 - p0).norm() > opts.nodeMergeTolerance) {
                const NodeId n0 = welder.findOrCreateNode(p0, outModel);
                const NodeId n1 = welder.findOrCreateNode(p1, outModel);
                if (n0 != n1) {
                    outModel.addElement(n0, n1, opts.defaultMaterialId, opts.defaultSectionId);
                    rep.lineCount++;
                    rep.entitiesRead++;
                }
            }
        } else if (entType == "3DFACE") {
            Vec3 vtx[4]{{0,0,0},{0,0,0},{0,0,0},{0,0,0}};
            for (const auto& [c, v] : props) {
                if (c == 10) vtx[0].x = parseDouble(v);
                else if (c == 20) vtx[0].y = parseDouble(v);
                else if (c == 30) vtx[0].z = parseDouble(v);
                else if (c == 11) vtx[1].x = parseDouble(v);
                else if (c == 21) vtx[1].y = parseDouble(v);
                else if (c == 31) vtx[1].z = parseDouble(v);
                else if (c == 12) vtx[2].x = parseDouble(v);
                else if (c == 22) vtx[2].y = parseDouble(v);
                else if (c == 32) vtx[2].z = parseDouble(v);
                else if (c == 13) vtx[3].x = parseDouble(v);
                else if (c == 23) vtx[3].y = parseDouble(v);
                else if (c == 33) vtx[3].z = parseDouble(v);
            }
            const bool isTri = ((vtx[3] - vtx[2]).norm() <= opts.nodeMergeTolerance);
            if (outSurfaceMesh) {
                const kernel::NodeId m0 = meshWelder.findOrCreateMeshNode(vtx[0], *outSurfaceMesh, "DXF");
                const kernel::NodeId m1 = meshWelder.findOrCreateMeshNode(vtx[1], *outSurfaceMesh, "DXF");
                const kernel::NodeId m2 = meshWelder.findOrCreateMeshNode(vtx[2], *outSurfaceMesh, "DXF");
                const kernel::ElementId eid = static_cast<kernel::ElementId>(outSurfaceMesh->elements.size() + 1);
                if (isTri) {
                    outSurfaceMesh->elements.push_back({eid, kernel::MeshCellType::Tri3, {m0, m1, m2}, layer});
                } else {
                    const kernel::NodeId m3 = meshWelder.findOrCreateMeshNode(vtx[3], *outSurfaceMesh, "DXF");
                    outSurfaceMesh->elements.push_back({eid, kernel::MeshCellType::Quad4, {m0, m1, m2, m3}, layer});
                }
                outSurfaceMesh->elementGroups[layer].push_back(eid);
            } else {
                welder.findOrCreateNode(vtx[0], outModel);
                welder.findOrCreateNode(vtx[1], outModel);
                welder.findOrCreateNode(vtx[2], outModel);
                if (!isTri) welder.findOrCreateNode(vtx[3], outModel);
            }
            rep.faceCount++;
            rep.entitiesRead++;
        } else if (entType == "LWPOLYLINE" && opts.importPolylinesAsFrames) {
            std::vector<Vec3> pts;
            double elev = 0.0;
            bool closed = false;
            bool haveX = false;
            Vec3 cur{0, 0, 0};
            for (const auto& [c, v] : props) {
                if (c == 38) elev = parseDouble(v);
                else if (c == 70) closed = ((static_cast<int>(parseDouble(v)) & 1) != 0);
                else if (c == 10) {
                    if (haveX) pts.push_back(cur);
                    cur = {parseDouble(v), 0.0, elev};
                    haveX = true;
                } else if (c == 20) {
                    cur.y = parseDouble(v);
                }
            }
            if (haveX) pts.push_back(cur);
            for (auto& pt : pts) pt.z = elev;
            for (size_t k = 0; k + 1 < pts.size(); ++k) {
                const NodeId na = welder.findOrCreateNode(pts[k], outModel);
                const NodeId nb = welder.findOrCreateNode(pts[k + 1], outModel);
                if (na != nb) {
                    outModel.addElement(na, nb, opts.defaultMaterialId, opts.defaultSectionId);
                    rep.lineCount++;
                }
            }
            if (closed && pts.size() >= 3) {
                const NodeId na = welder.findOrCreateNode(pts.back(), outModel);
                const NodeId nb = welder.findOrCreateNode(pts.front(), outModel);
                if (na != nb) {
                    outModel.addElement(na, nb, opts.defaultMaterialId, opts.defaultSectionId);
                    rep.lineCount++;
                }
            }
            rep.entitiesRead++;
        } else if (entType == "POLYLINE" && opts.importPolylinesAsFrames) {
            bool closed = false;
            for (const auto& [c, v] : props) {
                if (c == 70) closed = ((static_cast<int>(parseDouble(v)) & 1) != 0);
            }
            std::vector<Vec3> pts;
            while (i < pairs.size()) {
                if (pairs[i].first == 0) {
                    const std::string sub = toUpperCopy(pairs[i].second);
                    ++i;
                    if (sub == "SEQEND") break;
                    if (sub == "VERTEX") {
                        Vec3 vp{0, 0, 0};
                        while (i < pairs.size() && pairs[i].first != 0) {
                            if (pairs[i].first == 10) vp.x = parseDouble(pairs[i].second);
                            else if (pairs[i].first == 20) vp.y = parseDouble(pairs[i].second);
                            else if (pairs[i].first == 30) vp.z = parseDouble(pairs[i].second);
                            ++i;
                        }
                        pts.push_back(vp);
                        continue;
                    }
                }
                ++i;
            }
            for (size_t k = 0; k + 1 < pts.size(); ++k) {
                const NodeId na = welder.findOrCreateNode(pts[k], outModel);
                const NodeId nb = welder.findOrCreateNode(pts[k + 1], outModel);
                if (na != nb) {
                    outModel.addElement(na, nb, opts.defaultMaterialId, opts.defaultSectionId);
                    rep.lineCount++;
                }
            }
            if (closed && pts.size() >= 3) {
                const NodeId na = welder.findOrCreateNode(pts.back(), outModel);
                const NodeId nb = welder.findOrCreateNode(pts.front(), outModel);
                if (na != nb) {
                    outModel.addElement(na, nb, opts.defaultMaterialId, opts.defaultSectionId);
                    rep.lineCount++;
                }
            }
            rep.entitiesRead++;
        }
    }

    rep.mergedNodes = welder.mergedDuplicates() + meshWelder.mergedDuplicates();
    rep.layersFound.assign(layers.begin(), layers.end());
    rep.success = (rep.entitiesRead > 0);
    if (!rep.success) {
        rep.errorMessage = "Nenhuma entidade geométrica suportada encontrada no arquivo DXF.";
    }
    if (report) *report = rep;
    return rep.success;
}

bool DxfInterop::importFromFile(const std::string& filePath,
                                StructuralModel& outModel,
                                kernel::MeshContainer* outSurfaceMesh,
                                const DxfImportOptions& opts,
                                DxfInteropReport* report)
{
    std::ifstream ifs(pathFromUtf8(filePath), std::ios::binary);
    if (!ifs.is_open()) {
        if (report) {
            report->success = false;
            report->errorMessage = "Não foi possível abrir arquivo DXF: " + filePath;
        }
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return importFromString(ss.str(), outModel, outSurfaceMesh, opts, report);
}

} // namespace omniframe::io
