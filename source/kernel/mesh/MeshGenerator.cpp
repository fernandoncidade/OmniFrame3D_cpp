#include "MeshGenerator.hpp"
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace omniframe::kernel {

namespace {

double orient2D(const Eigen::Vector2d& a, const Eigen::Vector2d& b, const Eigen::Vector2d& c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

bool inCircumcircle(const Eigen::Vector2d& a,
                    const Eigen::Vector2d& b,
                    const Eigen::Vector2d& c,
                    const Eigen::Vector2d& p)
{
    // Supõe (a, b, c) em ordem anti-horária (orient2D > 0)
    const double ax = a.x() - p.x(), ay = a.y() - p.y();
    const double bx = b.x() - p.x(), by = b.y() - p.y();
    const double cx = c.x() - p.x(), cy = c.y() - p.y();
    const double det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                       (bx * bx + by * by) * (ax * cy - cx * ay) +
                       (cx * cx + cy * cy) * (ax * by - bx * ay);
    return det > 1e-12;
}

bool pointInPolygon2D(const Eigen::Vector2d& pt, const std::vector<Eigen::Vector2d>& poly) {
    bool inside = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& pi = poly[i];
        const auto& pj = poly[j];
        const bool intersect = ((pi.y() > pt.y()) != (pj.y() > pt.y())) &&
                               (pt.x() < (pj.x() - pi.x()) * (pt.y() - pi.y()) / ((pj.y() - pi.y()) + 1e-30) + pi.x());
        if (intersect) inside = !inside;
    }
    return inside;
}

double pointSegmentDist2D(const Eigen::Vector2d& p, const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
    const Eigen::Vector2d ab = b - a;
    const double len2 = ab.squaredNorm();
    if (len2 <= 1e-24) return (p - a).norm();
    const double t = std::clamp((p - a).dot(ab) / len2, 0.0, 1.0);
    return (p - (a + t * ab)).norm();
}

void ensureCounterClockwiseTri(std::array<int, 3>& tri, const std::vector<Eigen::Vector2d>& pts) {
    if (orient2D(pts[tri[0]], pts[tri[1]], pts[tri[2]]) < 0.0) {
        std::swap(tri[1], tri[2]);
    }
}

double tetSignedDet(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    Mat3 M;
    M.row(0) = (b - a).transpose();
    M.row(1) = (c - a).transpose();
    M.row(2) = (d - a).transpose();
    return M.determinant();
}

MeshElement makeOrientedTet4(ElementId id,
                             NodeId n0, NodeId n1, NodeId n2, NodeId n3,
                             const std::vector<MeshNode>& nodes,
                             const std::string& tag)
{
    auto pos = [&](NodeId nid) -> const Vec3& { return nodes[nid - 1].position; };
    std::vector<NodeId> conn{n0, n1, n2, n3};
    if (tetSignedDet(pos(conn[0]), pos(conn[1]), pos(conn[2]), pos(conn[3])) < 0.0) {
        std::swap(conn[2], conn[3]);
    }
    return MeshElement{id, MeshCellType::Tet4, std::move(conn), tag};
}

} // namespace

MeshContainer MeshGenerator::generateLineMesh(const Vec3& p0,
                                              const Vec3& p1,
                                              int subdivisions,
                                              const std::string& groupTag)
{
    if (subdivisions < 1) throw std::invalid_argument("Subdivisões 1D deve ser >= 1.");
    MeshContainer m;
    for (int i = 0; i <= subdivisions; ++i) {
        const double t = static_cast<double>(i) / subdivisions;
        m.nodes.push_back({i + 1, p0 + t * (p1 - p0), (i == 0 || i == subdivisions), groupTag});
    }
    for (int i = 0; i < subdivisions; ++i) {
        m.elements.push_back({i + 1, MeshCellType::Line2, {i + 1, i + 2}, groupTag});
        m.elementGroups[groupTag].push_back(i + 1);
    }
    return m;
}

MeshContainer MeshGenerator::generateStructuredQuadGrid(const Vec3& origin,
                                                        const Vec3& uSpanVec,
                                                        const Vec3& vSpanVec,
                                                        int nu,
                                                        int nv,
                                                        bool quadraticQuad8,
                                                        const std::string& groupTag)
{
    if (nu < 1 || nv < 1) throw std::invalid_argument("Grid 2D requer nu >= 1 e nv >= 1.");
    MeshContainer m;

    const int nux = quadraticQuad8 ? (2 * nu) : nu;
    const int nvy = quadraticQuad8 ? (2 * nv) : nv;
    std::vector<std::vector<NodeId>> gridIds(nvy + 1, std::vector<NodeId>(nux + 1, -1));
    NodeId nextNid = 1;

    for (int iy = 0; iy <= nvy; ++iy) {
        const double v = static_cast<double>(iy) / nvy;
        for (int ix = 0; ix <= nux; ++ix) {
            if (quadraticQuad8 && (ix % 2 == 1) && (iy % 2 == 1)) continue;
            const double u = static_cast<double>(ix) / nux;
            const bool onB = (ix == 0 || ix == nux || iy == 0 || iy == nvy);
            gridIds[iy][ix] = nextNid;
            m.nodes.push_back({nextNid++, origin + u * uSpanVec + v * vSpanVec, onB, groupTag});
        }
    }

    ElementId nextEid = 1;
    for (int ey = 0; ey < nv; ++ey) {
        for (int ex = 0; ex < nu; ++ex) {
            if (!quadraticQuad8) {
                const ElementId eid = nextEid++;
                m.elements.push_back({
                    eid, MeshCellType::Quad4,
                    {gridIds[ey][ex], gridIds[ey][ex + 1], gridIds[ey + 1][ex + 1], gridIds[ey + 1][ex]},
                    groupTag
                });
                m.elementGroups[groupTag].push_back(eid);
            } else {
                const int ix = 2 * ex, iy = 2 * ey;
                const ElementId eid = nextEid++;
                m.elements.push_back({
                    eid, MeshCellType::Quad8,
                    {
                        gridIds[iy][ix], gridIds[iy][ix + 2], gridIds[iy + 2][ix + 2], gridIds[iy + 2][ix],
                        gridIds[iy][ix + 1], gridIds[iy + 1][ix + 2], gridIds[iy + 2][ix + 1], gridIds[iy + 1][ix]
                    },
                    groupTag
                });
                m.elementGroups[groupTag].push_back(eid);
            }
        }
    }
    return m;
}

MeshContainer MeshGenerator::generateStructuredTriGrid(const Vec3& origin,
                                                       const Vec3& uSpanVec,
                                                       const Vec3& vSpanVec,
                                                       int nu,
                                                       int nv,
                                                       bool alternateDiagonal,
                                                       const std::string& groupTag)
{
    MeshContainer quads = generateStructuredQuadGrid(origin, uSpanVec, vSpanVec, nu, nv, false, groupTag);
    MeshContainer tris;
    tris.nodes = std::move(quads.nodes);
    ElementId eid = 1;
    for (int ey = 0; ey < nv; ++ey) {
        for (int ex = 0; ex < nu; ++ex) {
            const auto& q = quads.elements[ey * nu + ex].nodeIds;
            const bool flip = alternateDiagonal && ((ex + ey) % 2 == 1);
            if (!flip) {
                tris.elements.push_back({eid, MeshCellType::Tri3, {q[0], q[1], q[2]}, groupTag});
                tris.elementGroups[groupTag].push_back(eid++);
                tris.elements.push_back({eid, MeshCellType::Tri3, {q[0], q[2], q[3]}, groupTag});
                tris.elementGroups[groupTag].push_back(eid++);
            } else {
                tris.elements.push_back({eid, MeshCellType::Tri3, {q[0], q[1], q[3]}, groupTag});
                tris.elementGroups[groupTag].push_back(eid++);
                tris.elements.push_back({eid, MeshCellType::Tri3, {q[1], q[2], q[3]}, groupTag});
                tris.elementGroups[groupTag].push_back(eid++);
            }
        }
    }
    return tris;
}

MeshContainer MeshGenerator::triangulatePolygonDelaunay(const std::vector<Eigen::Vector2d>& polygonXY,
                                                          double targetEdgeSize,
                                                          int smoothPasses,
                                                          const Vec3& origin3D,
                                                          const Vec3& e1,
                                                          const Vec3& e2,
                                                          const std::string& groupTag)
{
    if (polygonXY.size() < 3) {
        throw std::invalid_argument("Polígono para Delaunay requer ao menos 3 vértices.");
    }
    if (targetEdgeSize <= 0.0) {
        throw std::invalid_argument("targetEdgeSize deve ser > 0.");
    }

    // Normaliza orientação do polígono para CCW
    double signedArea2 = 0.0;
    for (size_t i = 0; i < polygonXY.size(); ++i) {
        const auto& a = polygonXY[i];
        const auto& b = polygonXY[(i + 1) % polygonXY.size()];
        signedArea2 += a.x() * b.y() - b.x() * a.y();
    }
    std::vector<Eigen::Vector2d> poly = polygonXY;
    if (signedArea2 < 0.0) std::reverse(poly.begin(), poly.end());

    // Subdivide contorno com passo próximo a targetEdgeSize
    std::vector<Eigen::Vector2d> pts;
    std::vector<bool> isBoundary;
    Eigen::Vector2d minXY = poly[0], maxXY = poly[0];
    for (const auto& p : poly) {
        minXY = minXY.cwiseMin(p);
        maxXY = maxXY.cwiseMax(p);
    }

    for (size_t i = 0; i < poly.size(); ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % poly.size()];
        const double len = (b - a).norm();
        const int nSeg = std::max(1, static_cast<int>(std::round(len / targetEdgeSize)));
        for (int k = 0; k < nSeg; ++k) {
            pts.push_back(a + (static_cast<double>(k) / nSeg) * (b - a));
            isBoundary.push_back(true);
        }
    }

    // Gera sementes interiores em rede hexagonal com afastamento seguro do contorno
    const double dy = targetEdgeSize * std::sqrt(3.0) * 0.5;
    const double clearance = 0.52 * targetEdgeSize;
    for (int iy = 0; ; ++iy) {
        const double y = minXY.y() + clearance + static_cast<double>(iy) * dy;
        if (y > maxXY.y() - clearance) break;
        const double xOffset = (iy % 2 == 1) ? 0.5 * targetEdgeSize : 0.0;
        for (int ix = 0; ; ++ix) {
            const double x = minXY.x() + clearance + xOffset + static_cast<double>(ix) * targetEdgeSize;
            if (x > maxXY.x() - clearance) break;
            const Eigen::Vector2d cand(x, y);
            if (!pointInPolygon2D(cand, poly)) continue;
            double minDist = std::numeric_limits<double>::max();
            for (size_t i = 0; i < poly.size(); ++i) {
                minDist = std::min(minDist, pointSegmentDist2D(cand, poly[i], poly[(i + 1) % poly.size()]));
            }
            if (minDist >= clearance) {
                pts.push_back(cand);
                isBoundary.push_back(false);
            }
        }
    }

    // Super-triângulo para Bowyer-Watson
    const int nRealPts = static_cast<int>(pts.size());
    const Eigen::Vector2d center = 0.5 * (minXY + maxXY);
    const double dMax = std::max(1.0, (maxXY - minXY).norm()) * 20.0;
    pts.emplace_back(center.x() - dMax, center.y() - dMax);
    pts.emplace_back(center.x() + 2.0 * dMax, center.y() - dMax);
    pts.emplace_back(center.x(), center.y() + 2.0 * dMax);

    std::vector<std::array<int, 3>> triangles;
    std::array<int, 3> superTri{nRealPts, nRealPts + 1, nRealPts + 2};
    ensureCounterClockwiseTri(superTri, pts);
    triangles.push_back(superTri);

    for (int ip = 0; ip < nRealPts; ++ip) {
        const auto& p = pts[ip];
        std::vector<std::array<int, 3>> badTris;
        std::vector<std::array<int, 3>> goodTris;
        goodTris.reserve(triangles.size());

        for (const auto& t : triangles) {
            if (inCircumcircle(pts[t[0]], pts[t[1]], pts[t[2]], p)) {
                badTris.push_back(t);
            } else {
                goodTris.push_back(t);
            }
        }

        // Borda poligonal da cavidade (arestas que aparecem exatamente uma vez em badTris)
        std::map<std::pair<int, int>, std::pair<int, int>> edgeCount;
        std::vector<std::pair<int, int>> polyHoleEdges;
        for (const auto& t : badTris) {
            for (int e = 0; e < 3; ++e) {
                const int u = t[e];
                const int v = t[(e + 1) % 3];
                const std::pair<int, int> key{std::min(u, v), std::max(u, v)};
                auto it = edgeCount.find(key);
                if (it == edgeCount.end()) {
                    edgeCount[key] = {1, static_cast<int>(polyHoleEdges.size())};
                    polyHoleEdges.emplace_back(u, v);
                } else {
                    it->second.first += 1;
                }
            }
        }

        triangles = std::move(goodTris);
        for (size_t k = 0; k < polyHoleEdges.size(); ++k) {
            const auto& dirEdge = polyHoleEdges[k];
            const std::pair<int, int> key{std::min(dirEdge.first, dirEdge.second),
                                          std::max(dirEdge.first, dirEdge.second)};
            if (edgeCount[key].first == 1) {
                std::array<int, 3> nt{dirEdge.first, dirEdge.second, ip};
                ensureCounterClockwiseTri(nt, pts);
                triangles.push_back(nt);
            }
        }
    }

    // Remove triângulos ligados ao super-triângulo ou externos ao polígono
    std::vector<std::array<int, 3>> finalTris;
    finalTris.reserve(triangles.size());
    for (auto t : triangles) {
        if (t[0] >= nRealPts || t[1] >= nRealPts || t[2] >= nRealPts) continue;
        ensureCounterClockwiseTri(t, pts);
        if (orient2D(pts[t[0]], pts[t[1]], pts[t[2]]) <= 1e-12) continue;
        const Eigen::Vector2d c = (pts[t[0]] + pts[t[1]] + pts[t[2]]) / 3.0;
        if (pointInPolygon2D(c, poly)) {
            finalTris.push_back(t);
        }
    }

    // Suavização Laplaciana protegida (nunca reduz ou inverte áreas de triângulos incidentes)
    if (smoothPasses > 0 && !finalTris.empty()) {
        std::vector<std::vector<int>> adjVerts(nRealPts);
        std::vector<std::vector<int>> adjTris(nRealPts);
        for (int ti = 0; ti < static_cast<int>(finalTris.size()); ++ti) {
            for (int k = 0; k < 3; ++k) {
                const int v = finalTris[ti][k];
                adjTris[v].push_back(ti);
                adjVerts[v].push_back(finalTris[ti][(k + 1) % 3]);
                adjVerts[v].push_back(finalTris[ti][(k + 2) % 3]);
            }
        }
        for (int pass = 0; pass < smoothPasses; ++pass) {
            for (int v = 0; v < nRealPts; ++v) {
                if (isBoundary[v] || adjVerts[v].empty()) continue;
                Eigen::Vector2d avg = Eigen::Vector2d::Zero();
                for (int nb : adjVerts[v]) avg += pts[nb];
                avg /= static_cast<double>(adjVerts[v].size());
                const Eigen::Vector2d cand = pts[v] + 0.5 * (avg - pts[v]);
                const Eigen::Vector2d old = pts[v];
                pts[v] = cand;
                bool ok = pointInPolygon2D(cand, poly);
                for (int ti : adjTris[v]) {
                    const auto& t = finalTris[ti];
                    if (orient2D(pts[t[0]], pts[t[1]], pts[t[2]]) <= 1e-10) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) pts[v] = old;
            }
        }
    }

    MeshContainer out;
    out.nodes.reserve(nRealPts);
    for (int i = 0; i < nRealPts; ++i) {
        const Vec3 pos = origin3D + pts[i].x() * e1 + pts[i].y() * e2;
        out.nodes.push_back({i + 1, pos, isBoundary[i], groupTag});
    }
    out.elements.reserve(finalTris.size());
    ElementId eid = 1;
    for (const auto& t : finalTris) {
        out.elements.push_back({eid, MeshCellType::Tri3, {t[0] + 1, t[1] + 1, t[2] + 1}, groupTag});
        out.elementGroups[groupTag].push_back(eid++);
    }
    return out;
}

MeshContainer MeshGenerator::generateStructuredHexGrid(const Vec3& origin,
                                                       const Vec3& sizeXYZ,
                                                       int nx,
                                                       int ny,
                                                       int nz,
                                                       bool quadraticHex20,
                                                       const std::string& groupTag)
{
    if (nx < 1 || ny < 1 || nz < 1) {
        throw std::invalid_argument("Grid 3D requer nx, ny, nz >= 1.");
    }
    MeshContainer m;
    const int nX = quadraticHex20 ? (2 * nx) : nx;
    const int nY = quadraticHex20 ? (2 * ny) : ny;
    const int nZ = quadraticHex20 ? (2 * nz) : nz;

    std::vector<std::vector<std::vector<NodeId>>> ids(
        nZ + 1, std::vector<std::vector<NodeId>>(nY + 1, std::vector<NodeId>(nX + 1, -1)));
    NodeId nextNid = 1;

    for (int iz = 0; iz <= nZ; ++iz) {
        const double z = origin.z() + sizeXYZ.z() * iz / nZ;
        for (int iy = 0; iy <= nY; ++iy) {
            const double y = origin.y() + sizeXYZ.y() * iy / nY;
            for (int ix = 0; ix <= nX; ++ix) {
                if (quadraticHex20) {
                    const int oddCount = (ix % 2) + (iy % 2) + (iz % 2);
                    if (oddCount >= 2) continue; // Somente cantos e meio-arestas
                }
                const double x = origin.x() + sizeXYZ.x() * ix / nX;
                const bool onB = (ix == 0 || ix == nX || iy == 0 || iy == nY || iz == 0 || iz == nZ);
                ids[iz][iy][ix] = nextNid;
                m.nodes.push_back({nextNid++, Vec3(x, y, z), onB, groupTag});
            }
        }
    }

    ElementId nextEid = 1;
    for (int ez = 0; ez < nz; ++ez) {
        for (int ey = 0; ey < ny; ++ey) {
            for (int ex = 0; ex < nx; ++ex) {
                if (!quadraticHex20) {
                    const ElementId eid = nextEid++;
                    m.elements.push_back({
                        eid, MeshCellType::Hex8,
                        {
                            ids[ez][ey][ex], ids[ez][ey][ex + 1], ids[ez][ey + 1][ex + 1], ids[ez][ey + 1][ex],
                            ids[ez + 1][ey][ex], ids[ez + 1][ey][ex + 1], ids[ez + 1][ey + 1][ex + 1], ids[ez + 1][ey + 1][ex]
                        },
                        groupTag
                    });
                    m.elementGroups[groupTag].push_back(eid);
                } else {
                    const int ix = 2 * ex, iy = 2 * ey, iz = 2 * ez;
                    const ElementId eid = nextEid++;
                    m.elements.push_back({
                        eid, MeshCellType::Hex20,
                        {
                            ids[iz][iy][ix], ids[iz][iy][ix + 2], ids[iz][iy + 2][ix + 2], ids[iz][iy + 2][ix],
                            ids[iz + 2][iy][ix], ids[iz + 2][iy][ix + 2], ids[iz + 2][iy + 2][ix + 2], ids[iz + 2][iy + 2][ix],
                            ids[iz][iy][ix + 1], ids[iz][iy + 1][ix + 2], ids[iz][iy + 2][ix + 1], ids[iz][iy + 1][ix],
                            ids[iz + 2][iy][ix + 1], ids[iz + 2][iy + 1][ix + 2], ids[iz + 2][iy + 2][ix + 1], ids[iz + 2][iy + 1][ix],
                            ids[iz + 1][iy][ix], ids[iz + 1][iy][ix + 2], ids[iz + 1][iy + 2][ix + 2], ids[iz + 1][iy + 2][ix]
                        },
                        groupTag
                    });
                    m.elementGroups[groupTag].push_back(eid);
                }
            }
        }
    }
    return m;
}

MeshContainer MeshGenerator::generateStructuredTetGrid(const Vec3& origin,
                                                       const Vec3& sizeXYZ,
                                                       int nx,
                                                       int ny,
                                                       int nz,
                                                       const std::string& groupTag)
{
    MeshContainer hex = generateStructuredHexGrid(origin, sizeXYZ, nx, ny, nz, false, groupTag);
    MeshContainer tets;
    tets.nodes = std::move(hex.nodes);
    ElementId eid = 1;
    // Decomposição de Kuhn em 6 tetraedros conformes por hexaedro (todos com diagonal 0->6)
    static constexpr int kuhn[6][4] = {
        {0, 1, 2, 6},
        {0, 2, 3, 6},
        {0, 3, 7, 6},
        {0, 7, 4, 6},
        {0, 4, 5, 6},
        {0, 5, 1, 6}
    };
    for (const auto& h : hex.elements) {
        for (int k = 0; k < 6; ++k) {
            const ElementId id = eid++;
            tets.elements.push_back(makeOrientedTet4(
                id,
                h.nodeIds[kuhn[k][0]],
                h.nodeIds[kuhn[k][1]],
                h.nodeIds[kuhn[k][2]],
                h.nodeIds[kuhn[k][3]],
                tets.nodes,
                groupTag));
            tets.elementGroups[groupTag].push_back(id);
        }
    }
    return tets;
}

MeshContainer MeshGenerator::extrudeSurfaceTo3D(const MeshContainer& surface2D,
                                                const Vec3& extrudeVector,
                                                int layers,
                                                bool convertQuadToTet,
                                                const std::string& groupTag)
{
    if (layers < 1 || surface2D.nodes.empty() || surface2D.elements.empty()) {
        throw std::invalid_argument("Parâmetros inválidos para extrusão 2D -> 3D.");
    }

    MeshContainer out;
    const int nBaseNodes = static_cast<int>(surface2D.nodes.size());
    std::unordered_map<NodeId, int> baseIndex;
    for (int i = 0; i < nBaseNodes; ++i) baseIndex[surface2D.nodes[i].id] = i;

    out.nodes.reserve(nBaseNodes * (layers + 1));
    NodeId nextNid = 1;
    for (int l = 0; l <= layers; ++l) {
        const double frac = static_cast<double>(l) / layers;
        for (int i = 0; i < nBaseNodes; ++i) {
            const bool onB = surface2D.nodes[i].isBoundary || l == 0 || l == layers;
            out.nodes.push_back({nextNid++, surface2D.nodes[i].position + frac * extrudeVector, onB, groupTag});
        }
    }

    auto nidLayer = [&](NodeId baseNid, int layer) -> NodeId {
        return static_cast<NodeId>(layer * nBaseNodes + baseIndex.at(baseNid) + 1);
    };

    ElementId eid = 1;
    for (int l = 0; l < layers; ++l) {
        for (const auto& el : surface2D.elements) {
            if (el.type == MeshCellType::Quad4) {
                const NodeId b[4] = {
                    nidLayer(el.nodeIds[0], l), nidLayer(el.nodeIds[1], l),
                    nidLayer(el.nodeIds[2], l), nidLayer(el.nodeIds[3], l)
                };
                const NodeId t[4] = {
                    nidLayer(el.nodeIds[0], l + 1), nidLayer(el.nodeIds[1], l + 1),
                    nidLayer(el.nodeIds[2], l + 1), nidLayer(el.nodeIds[3], l + 1)
                };
                if (!convertQuadToTet) {
                    const ElementId id = eid++;
                    out.elements.push_back({
                        id, MeshCellType::Hex8,
                        {b[0], b[1], b[2], b[3], t[0], t[1], t[2], t[3]},
                        groupTag
                    });
                    out.elementGroups[groupTag].push_back(id);
                } else {
                    static constexpr int kuhn[6][4] = {
                        {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6},
                        {0, 7, 4, 6}, {0, 4, 5, 6}, {0, 5, 1, 6}
                    };
                    const NodeId c[8] = {b[0], b[1], b[2], b[3], t[0], t[1], t[2], t[3]};
                    for (int k = 0; k < 6; ++k) {
                        const ElementId id = eid++;
                        out.elements.push_back(makeOrientedTet4(
                            id, c[kuhn[k][0]], c[kuhn[k][1]], c[kuhn[k][2]], c[kuhn[k][3]],
                            out.nodes, groupTag));
                        out.elementGroups[groupTag].push_back(id);
                    }
                }
            } else if (el.type == MeshCellType::Tri3) {
                // Regra conforme de Dompierre (1999): rotaciona base de modo que b0 tenha o menor ID
                std::array<NodeId, 3> origB{el.nodeIds[0], el.nodeIds[1], el.nodeIds[2]};
                int minPos = 0;
                if (origB[1] < origB[minPos]) minPos = 1;
                if (origB[2] < origB[minPos]) minPos = 2;

                const NodeId b0 = nidLayer(origB[minPos], l);
                const NodeId b1 = nidLayer(origB[(minPos + 1) % 3], l);
                const NodeId b2 = nidLayer(origB[(minPos + 2) % 3], l);
                const NodeId t0 = nidLayer(origB[minPos], l + 1);
                const NodeId t1 = nidLayer(origB[(minPos + 1) % 3], l + 1);
                const NodeId t2 = nidLayer(origB[(minPos + 2) % 3], l + 1);

                if (origB[(minPos + 1) % 3] < origB[(minPos + 2) % 3]) {
                    // Split conforme caso I: diagonais t0-b1, t0-b2, t1-b2
                    out.elements.push_back(makeOrientedTet4(eid, b0, b1, b2, t0, out.nodes, groupTag));
                    out.elementGroups[groupTag].push_back(eid++);
                    out.elements.push_back(makeOrientedTet4(eid, b1, b2, t0, t1, out.nodes, groupTag));
                    out.elementGroups[groupTag].push_back(eid++);
                    out.elements.push_back(makeOrientedTet4(eid, b2, t0, t1, t2, out.nodes, groupTag));
                    out.elementGroups[groupTag].push_back(eid++);
                } else {
                    // Split conforme caso II: diagonais t0-b1, t0-b2, t2-b1
                    out.elements.push_back(makeOrientedTet4(eid, b0, b1, b2, t0, out.nodes, groupTag));
                    out.elementGroups[groupTag].push_back(eid++);
                    out.elements.push_back(makeOrientedTet4(eid, b1, b2, t0, t2, out.nodes, groupTag));
                    out.elementGroups[groupTag].push_back(eid++);
                    out.elements.push_back(makeOrientedTet4(eid, b1, t0, t1, t2, out.nodes, groupTag));
                    out.elementGroups[groupTag].push_back(eid++);
                }
            }
        }
    }
    return out;
}

void MeshGenerator::assignPlaneNodeSet(MeshContainer& mesh,
                                       const std::string& setName,
                                       const Vec3& pointOnPlane,
                                       const Vec3& unitNormal,
                                       double tolerance)
{
    const Vec3 n = unitNormal.normalized();
    auto& set = mesh.nodeSets[setName];
    set.clear();
    for (const auto& node : mesh.nodes) {
        if (std::abs((node.position - pointOnPlane).dot(n)) <= tolerance) {
            set.push_back(node.id);
        }
    }
}

MeshQualityMetrics MeshGenerator::evaluateQuality(const MeshContainer& mesh) {
    MeshQualityMetrics qm;
    qm.totalElements = static_cast<int>(mesh.elements.size());
    if (qm.totalElements == 0) return qm;

    std::unordered_map<NodeId, Vec3> posMap;
    posMap.reserve(mesh.nodes.size());
    for (const auto& n : mesh.nodes) posMap[n.id] = n.position;

    qm.minJacobian = std::numeric_limits<double>::max();
    qm.minAspectRatio = std::numeric_limits<double>::max();
    qm.maxAspectRatio = 0.0;
    qm.maxSkewness = 0.0;
    bool anyMetric = false;

    auto requiredCornerCount = [](MeshCellType t) -> std::size_t {
        switch (t) {
            case MeshCellType::Line2: return 2;
            case MeshCellType::Tri3:  return 3;
            case MeshCellType::Quad4: return 4;
            case MeshCellType::Quad8: return 8;
            case MeshCellType::Tet4:  return 4;
            case MeshCellType::Tet10: return 10;
            case MeshCellType::Hex8:  return 8;
            case MeshCellType::Hex20: return 20;
        }
        return 0;
    };

    auto isFinitePos = [](const Vec3& p) -> bool {
        return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z());
    };

    for (const auto& el : mesh.elements) {
        const std::size_t reqNodes = requiredCornerCount(el.type);
        if (reqNodes == 0 || el.nodeIds.size() < reqNodes) {
            qm.invertedOrDegenerateCount++;
            continue;
        }

        // Busca segura via find(): nunca lança std::out_of_range se houver nó ausente ou não finito ([AUD-007])
        bool allNodesValid = true;
        std::array<Vec3, 8> corners{};
        for (std::size_t i = 0; i < el.nodeIds.size(); ++i) {
            const auto it = posMap.find(el.nodeIds[i]);
            if (it == posMap.end() || !isFinitePos(it->second)) {
                allNodesValid = false;
                break;
            }
            if (i < corners.size()) {
                corners[i] = it->second;
            }
        }
        if (!allNodesValid) {
            qm.invertedOrDegenerateCount++;
            continue;
        }

        double jac = 0.0;
        double minEdge = std::numeric_limits<double>::max();
        double maxEdge = 0.0;
        double skew = 0.0;

        auto edgeUpdate = [&](int a, int b) {
            const double d = (corners[static_cast<std::size_t>(a)] - corners[static_cast<std::size_t>(b)]).norm();
            minEdge = std::min(minEdge, d);
            maxEdge = std::max(maxEdge, d);
        };

        if (el.type == MeshCellType::Line2) {
            jac = (corners[1] - corners[0]).norm();
            minEdge = maxEdge = jac;
        } else if (el.type == MeshCellType::Tri3) {
            const Vec3& a = corners[0];
            const Vec3& b = corners[1];
            const Vec3& c = corners[2];
            jac = (b - a).cross(c - a).norm();
            edgeUpdate(0, 1); edgeUpdate(1, 2); edgeUpdate(2, 0);
            const double ab = (b - a).norm(), bc = (c - b).norm(), ca = (a - c).norm();
            const double area = 0.5 * jac;
            const double eqArea = (std::sqrt(3.0) / 4.0) * std::pow((ab + bc + ca) / 3.0, 2);
            skew = (eqArea > 1e-18) ? std::clamp(1.0 - area / eqArea, 0.0, 1.0) : 1.0;
        } else if (el.type == MeshCellType::Quad4 || el.type == MeshCellType::Quad8) {
            const Vec3& a = corners[0];
            const Vec3& b = corners[1];
            const Vec3& c = corners[2];
            const Vec3& d = corners[3];
            const Vec3 c0 = (b - a).cross(d - a);
            const Vec3 c1 = (c - b).cross(a - b);
            const Vec3 c2 = (d - c).cross(b - c);
            const Vec3 c3 = (a - d).cross(c - d);
            const Vec3 nRef = (c - a).cross(d - b);
            const double nRefNorm = nRef.norm();
            if (nRefNorm <= 1e-15 || c0.dot(c2) <= 0.0 || c1.dot(c3) <= 0.0) {
                jac = -1.0;
            } else {
                const Vec3 nUnit = nRef / nRefNorm;
                jac = 0.25 * std::min({c0.dot(nUnit), c1.dot(nUnit), c2.dot(nUnit), c3.dot(nUnit)});
            }
            edgeUpdate(0, 1); edgeUpdate(1, 2); edgeUpdate(2, 3); edgeUpdate(3, 0);
            const Vec3 e1 = b - a;
            const Vec3 e2 = d - a;
            const double e1n = e1.norm();
            const double e2n = e2.norm();
            const double cosA = (e1n > 1e-14 && e2n > 1e-14) ? std::abs((e1 / e1n).dot(e2 / e2n)) : 1.0;
            skew = std::clamp(cosA, 0.0, 1.0);
        } else if (el.type == MeshCellType::Tet4 || el.type == MeshCellType::Tet10) {
            jac = tetSignedDet(corners[0], corners[1], corners[2], corners[3]);
            static constexpr int edges[6][2] = {{0,1},{1,2},{2,0},{0,3},{1,3},{2,3}};
            for (const auto& ed : edges) edgeUpdate(ed[0], ed[1]);
            const double vol = jac / 6.0;
            const double avgE = 0.5 * (minEdge + maxEdge);
            const double regVol = (avgE * avgE * avgE) / (6.0 * std::sqrt(2.0));
            skew = (regVol > 1e-18) ? std::clamp(1.0 - vol / regVol, 0.0, 1.0) : 1.0;
        } else if (el.type == MeshCellType::Hex8 || el.type == MeshCellType::Hex20) {
            const Vec3& p0 = corners[0];
            const Vec3& p1 = corners[1];
            const Vec3& p2 = corners[2];
            const Vec3& p3 = corners[3];
            const Vec3& p4 = corners[4];
            const Vec3& p5 = corners[5];
            const Vec3& p6 = corners[6];
            const Vec3& p7 = corners[7];
            const double j0 = (p1 - p0).dot((p3 - p0).cross(p4 - p0));
            const double j1 = (p2 - p1).dot((p0 - p1).cross(p5 - p1));
            const double j2 = (p3 - p2).dot((p1 - p2).cross(p6 - p2));
            const double j3 = (p0 - p3).dot((p2 - p3).cross(p7 - p3));
            const double j4 = (p7 - p4).dot((p5 - p4).cross(p0 - p4));
            const double j5 = (p4 - p5).dot((p6 - p5).cross(p1 - p5));
            const double j6 = (p5 - p6).dot((p7 - p6).cross(p2 - p6));
            const double j7 = (p6 - p7).dot((p4 - p7).cross(p3 - p7));
            jac = 0.125 * std::min({j0, j1, j2, j3, j4, j5, j6, j7});
            static constexpr int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
            };
            for (const auto& ed : edges) edgeUpdate(ed[0], ed[1]);
        }

        const double ar = (minEdge > 1e-14) ? (maxEdge / minEdge) : 1e9;
        qm.minJacobian = std::min(qm.minJacobian, jac);
        qm.minAspectRatio = std::min(qm.minAspectRatio, ar);
        qm.maxAspectRatio = std::max(qm.maxAspectRatio, ar);
        qm.maxSkewness = std::max(qm.maxSkewness, skew);
        anyMetric = true;

        if (!std::isfinite(jac) || jac <= 1e-14 || !std::isfinite(minEdge) || minEdge <= 1e-14) {
            qm.invertedOrDegenerateCount++;
        } else {
            qm.validElements++;
        }
    }
    if (!anyMetric) {
        qm.minJacobian = 0.0;
        qm.minAspectRatio = 0.0;
    }
    return qm;
}

} // namespace omniframe::kernel
