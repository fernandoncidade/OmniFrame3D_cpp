#include "ShellElements.hpp"
#include <Eigen/Cholesky>
#include <Eigen/LU>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace omniframe::kernel {

namespace {

using Vec2 = Eigen::Vector2d;
using Mat2 = Eigen::Matrix2d;
using Mat4 = Eigen::Matrix4d;

struct QuadGaussPoint {
    double xi = 0.0;
    double eta = 0.0;
    double w = 0.0;
};

std::vector<QuadGaussPoint> gauss2D(int n1D) {
    std::vector<double> pts;
    std::vector<double> wts;
    if (n1D == 1) {
        pts = {0.0};
        wts = {2.0};
    } else if (n1D == 2) {
        const double p = 1.0 / std::sqrt(3.0);
        pts = {-p, p};
        wts = {1.0, 1.0};
    } else {
        const double p = std::sqrt(3.0 / 5.0);
        pts = {-p, 0.0, p};
        wts = {5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0};
    }
    std::vector<QuadGaussPoint> out;
    out.reserve(pts.size() * pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        for (size_t j = 0; j < pts.size(); ++j) {
            out.push_back({pts[i], pts[j], wts[i] * wts[j]});
        }
    }
    return out;
}

struct TriGaussPoint {
    double r = 0.0;
    double s = 0.0;
    double w = 0.0; // Peso na área de referência (área = 0.5)
};

const std::vector<TriGaussPoint>& triGauss3() {
    static const std::vector<TriGaussPoint> pts = {
        {1.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0},
        {2.0 / 3.0, 1.0 / 6.0, 1.0 / 6.0},
        {1.0 / 6.0, 2.0 / 3.0, 1.0 / 6.0}
    };
    return pts;
}

struct ShapeEval {
    VecX N;
    VecX dNdx;
    VecX dNdy;
    Mat2 J;
    double detJ = 0.0;
};

void quad4Shape(double xi, double eta,
                const std::vector<Eigen::Vector2d>& xy,
                ShapeEval& out)
{
    static constexpr double sx[4] = {-1.0, 1.0, 1.0, -1.0};
    static constexpr double sy[4] = {-1.0, -1.0, 1.0, 1.0};

    out.N.resize(4);
    Eigen::Vector4d dNdxi, dNdeta;
    for (int i = 0; i < 4; ++i) {
        out.N(i) = 0.25 * (1.0 + sx[i] * xi) * (1.0 + sy[i] * eta);
        dNdxi(i) = 0.25 * sx[i] * (1.0 + sy[i] * eta);
        dNdeta(i) = 0.25 * sy[i] * (1.0 + sx[i] * xi);
    }

    Mat2 J = Mat2::Zero();
    for (int i = 0; i < 4; ++i) {
        J(0, 0) += dNdxi(i) * xy[i].x();
        J(0, 1) += dNdxi(i) * xy[i].y();
        J(1, 0) += dNdeta(i) * xy[i].x();
        J(1, 1) += dNdeta(i) * xy[i].y();
    }
    out.J = J;
    out.detJ = J.determinant();
    if (out.detJ <= 1e-16) {
        throw std::runtime_error("Jacobiano não positivo em ShellElement Quad4.");
    }

    const Mat2 invJ = J.inverse();
    out.dNdx.resize(4);
    out.dNdy.resize(4);
    for (int i = 0; i < 4; ++i) {
        const Eigen::Vector2d g = invJ * Eigen::Vector2d(dNdxi(i), dNdeta(i));
        out.dNdx(i) = g.x();
        out.dNdy(i) = g.y();
    }
}

void quad8Shape(double xi, double eta,
                const std::vector<Eigen::Vector2d>& xy,
                ShapeEval& out)
{
    out.N.resize(8);
    Eigen::Matrix<double, 8, 1> dNdxi, dNdeta;

    static constexpr double cx[4] = {-1.0, 1.0, 1.0, -1.0};
    static constexpr double cy[4] = {-1.0, -1.0, 1.0, 1.0};
    for (int i = 0; i < 4; ++i) {
        const double px = 1.0 + cx[i] * xi;
        const double py = 1.0 + cy[i] * eta;
        const double f = cx[i] * xi + cy[i] * eta - 1.0;
        out.N(i) = 0.25 * px * py * f;
        dNdxi(i) = 0.25 * cx[i] * py * (f + px);
        dNdeta(i) = 0.25 * cy[i] * px * (f + py);
    }

    // Nó 4: (0, -1)
    out.N(4) = 0.5 * (1.0 - xi * xi) * (1.0 - eta);
    dNdxi(4) = -xi * (1.0 - eta);
    dNdeta(4) = -0.5 * (1.0 - xi * xi);

    // Nó 5: (1, 0)
    out.N(5) = 0.5 * (1.0 + xi) * (1.0 - eta * eta);
    dNdxi(5) = 0.5 * (1.0 - eta * eta);
    dNdeta(5) = -(1.0 + xi) * eta;

    // Nó 6: (0, 1)
    out.N(6) = 0.5 * (1.0 - xi * xi) * (1.0 + eta);
    dNdxi(6) = -xi * (1.0 + eta);
    dNdeta(6) = 0.5 * (1.0 - xi * xi);

    // Nó 7: (-1, 0)
    out.N(7) = 0.5 * (1.0 - xi) * (1.0 - eta * eta);
    dNdxi(7) = -0.5 * (1.0 - eta * eta);
    dNdeta(7) = -(1.0 - xi) * eta;

    Mat2 J = Mat2::Zero();
    for (int i = 0; i < 8; ++i) {
        J(0, 0) += dNdxi(i) * xy[i].x();
        J(0, 1) += dNdxi(i) * xy[i].y();
        J(1, 0) += dNdeta(i) * xy[i].x();
        J(1, 1) += dNdeta(i) * xy[i].y();
    }
    out.J = J;
    out.detJ = J.determinant();
    if (out.detJ <= 1e-16) {
        throw std::runtime_error("Jacobiano não positivo em ShellElement Quad8.");
    }

    const Mat2 invJ = J.inverse();
    out.dNdx.resize(8);
    out.dNdy.resize(8);
    for (int i = 0; i < 8; ++i) {
        const Eigen::Vector2d g = invJ * Eigen::Vector2d(dNdxi(i), dNdeta(i));
        out.dNdx(i) = g.x();
        out.dNdy(i) = g.y();
    }
}

void tri3Shape(double r, double s,
               const std::vector<Eigen::Vector2d>& xy,
               ShapeEval& out)
{
    out.N.resize(3);
    out.N(0) = 1.0 - r - s;
    out.N(1) = r;
    out.N(2) = s;

    Mat2 J;
    J(0, 0) = xy[1].x() - xy[0].x();
    J(0, 1) = xy[1].y() - xy[0].y();
    J(1, 0) = xy[2].x() - xy[0].x();
    J(1, 1) = xy[2].y() - xy[0].y();
    out.J = J;
    out.detJ = J.determinant();
    if (out.detJ <= 1e-16) {
        throw std::runtime_error("Jacobiano não positivo em ShellElement Tri3.");
    }

    const Mat2 invJ = J.inverse();
    static constexpr double dNdr[3] = {-1.0, 1.0, 0.0};
    static constexpr double dNds[3] = {-1.0, 0.0, 1.0};
    out.dNdx.resize(3);
    out.dNdy.resize(3);
    for (int i = 0; i < 3; ++i) {
        const Eigen::Vector2d g = invJ * Eigen::Vector2d(dNdr[i], dNds[i]);
        out.dNdx(i) = g.x();
        out.dNdy(i) = g.y();
    }
}

MatX buildMembraneB(int nNodes, const VecX& dNdx, const VecX& dNdy) {
    MatX Bm = MatX::Zero(3, 6 * nNodes);
    for (int i = 0; i < nNodes; ++i) {
        const int col = 6 * i;
        Bm(0, col + 0) = dNdx(i);
        Bm(1, col + 1) = dNdy(i);
        Bm(2, col + 0) = dNdy(i);
        Bm(2, col + 1) = dNdx(i);
    }
    return Bm;
}

// Convenção de mão direita compatível com BeamTruss1D:
//   u(z) = +z * theta_y, v(z) = -z * theta_x
//   kappa_x = d(theta_y)/dx, kappa_y = -d(theta_x)/dy, kappa_xy = d(theta_y)/dy - d(theta_x)/dx
MatX buildBendingB(int nNodes, const VecX& dNdx, const VecX& dNdy) {
    MatX Bb = MatX::Zero(3, 6 * nNodes);
    for (int i = 0; i < nNodes; ++i) {
        const int col = 6 * i;
        Bb(0, col + 4) = dNdx(i);
        Bb(1, col + 3) = -dNdy(i);
        Bb(2, col + 3) = -dNdx(i);
        Bb(2, col + 4) = dNdy(i);
    }
    return Bb;
}

MatX buildIsoparametricShearB(int nNodes, const VecX& N, const VecX& dNdx, const VecX& dNdy) {
    MatX Bs = MatX::Zero(2, 6 * nNodes);
    for (int i = 0; i < nNodes; ++i) {
        const int col = 6 * i;
        Bs(0, col + 2) = dNdx(i);
        Bs(0, col + 4) = N(i);
        Bs(1, col + 2) = dNdy(i);
        Bs(1, col + 3) = -N(i);
    }
    return Bs;
}

struct Tri3DstBasis {
    std::array<int, 3> edgeA{0, 1, 2};
    std::array<int, 3> edgeB{1, 2, 0};
    std::array<double, 3> c{0.0, 0.0, 0.0};
    std::array<double, 3> s{0.0, 0.0, 0.0};
    Eigen::Matrix<double, 3, 18> D_dkt = Eigen::Matrix<double, 3, 18>::Zero();
    Eigen::Matrix<double, 3, 2> S_e = Eigen::Matrix<double, 3, 2>::Zero();
    Eigen::Matrix<double, 2, 18> H_g = Eigen::Matrix<double, 2, 18>::Zero();
    Vec3 dNdx = Vec3::Zero();
    Vec3 dNdy = Vec3::Zero();

    MatX evalBendingB(const VecX& N) const {
        MatX Bb = buildBendingB(3, dNdx, dNdy);
        Eigen::Matrix<double, 3, 3> Bphi = Eigen::Matrix<double, 3, 3>::Zero();
        for (int e = 0; e < 3; ++e) {
            const int a = edgeA[e];
            const int b = edgeB[e];
            const double dphidx = 4.0 * (N(a) * dNdx(b) + N(b) * dNdx(a));
            const double dphidy = 4.0 * (N(a) * dNdy(b) + N(b) * dNdy(a));
            Bphi(0, e) = c[e] * dphidx;
            Bphi(1, e) = s[e] * dphidy;
            Bphi(2, e) = c[e] * dphidy + s[e] * dphidx;
        }
        Bb.noalias() += Bphi * (D_dkt + S_e * H_g);
        return Bb;
    }
};

// Formulação Discrete Shear Triangle (Batoz & Lardeur 1989) livre de travamento por cisalhamento.
// Interpola rotações da seção beta = (theta_y, -theta_x) com bolhas quadráticas nas 3 arestas
// eliminadas estaticamente via equilíbrio local constante div(M) = Q = C_s * gamma_0.
Tri3DstBasis buildTri3DstBasis(const std::vector<Eigen::Vector2d>& xy,
                               double tAvg,
                               const Mat3& Dps,
                               const Mat2& Gs)
{
    Tri3DstBasis basis;
    ShapeEval cSh;
    tri3Shape(1.0 / 3.0, 1.0 / 3.0, xy, cSh);
    for (int i = 0; i < 3; ++i) {
        basis.dNdx(i) = cSh.dNdx(i);
        basis.dNdy(i) = cSh.dNdy(i);
    }

    const Mat3 CbConst = (tAvg * tAvg * tAvg / 12.0) * Dps;
    const Mat2 CsConst = tAvg * Gs;

    Eigen::Matrix<double, 3, 3> Dx = Eigen::Matrix<double, 3, 3>::Zero();
    Eigen::Matrix<double, 3, 3> Dy = Eigen::Matrix<double, 3, 3>::Zero();

    for (int e = 0; e < 3; ++e) {
        const int a = basis.edgeA[e];
        const int b = basis.edgeB[e];
        const double dx = xy[b].x() - xy[a].x();
        const double dy = xy[b].y() - xy[a].y();
        const double len = std::hypot(dx, dy);
        if (len <= 1e-16) {
            throw std::runtime_error("Aresta degenerada em ShellElement Tri3 DST.");
        }
        const double ce = dx / len;
        const double se = dy / len;
        basis.c[e] = ce;
        basis.s[e] = se;

        const int ca = 6 * a;
        const int cb = 6 * b;
        // d_e^{DKT} * u = -1.5*(w_b - w_a)/L_e - 0.75*(beta_{t,a} + beta_{t,b}), com beta_t = c_e*theta_y - s_e*theta_x
        basis.D_dkt(e, ca + 2) += 1.5 / len;
        basis.D_dkt(e, cb + 2) -= 1.5 / len;
        basis.D_dkt(e, ca + 3) += 0.75 * se;
        basis.D_dkt(e, cb + 3) += 0.75 * se;
        basis.D_dkt(e, ca + 4) -= 0.75 * ce;
        basis.D_dkt(e, cb + 4) -= 0.75 * ce;

        basis.S_e(e, 0) = 1.5 * ce;
        basis.S_e(e, 1) = 1.5 * se;

        const double phi_xx = 8.0 * basis.dNdx(a) * basis.dNdx(b);
        const double phi_yy = 8.0 * basis.dNdy(a) * basis.dNdy(b);
        const double phi_xy = 4.0 * (basis.dNdx(a) * basis.dNdy(b) + basis.dNdy(a) * basis.dNdx(b));

        Dx(0, e) = ce * phi_xx;
        Dx(1, e) = se * phi_xy;
        Dx(2, e) = ce * phi_xy + se * phi_xx;

        Dy(0, e) = ce * phi_xy;
        Dy(1, e) = se * phi_yy;
        Dy(2, e) = ce * phi_yy + se * phi_xy;
    }

    const Eigen::Matrix<double, 3, 3> M_x = CbConst * Dx;
    const Eigen::Matrix<double, 3, 3> M_y = CbConst * Dy;
    Eigen::Matrix<double, 2, 3> R_div = Eigen::Matrix<double, 2, 3>::Zero();
    R_div.row(0) = M_x.row(0) + M_y.row(2);
    R_div.row(1) = M_x.row(2) + M_y.row(1);

    const Mat2 Ag = R_div * basis.S_e;
    const Mat2 Msys = CsConst - Ag;
    if (std::abs(Msys.determinant()) <= 1e-24) {
        basis.H_g.setZero();
    } else {
        basis.H_g = Msys.inverse() * (R_div * basis.D_dkt);
    }
    return basis;
}

MatX buildMITC4ShearB(double xi, double eta,
                      const std::vector<Eigen::Vector2d>& xy,
                      const Mat2& invJ)
{
    MatX G = MatX::Zero(2, 24); // [gamma_xi; gamma_eta] em coordenadas naturais

    auto edgeRow = [&](int i, int j) {
        Eigen::Matrix<double, 1, 24> row = Eigen::Matrix<double, 1, 24>::Zero();
        const double dx = xy[j].x() - xy[i].x();
        const double dy = xy[j].y() - xy[i].y();
        const int ci = 6 * i;
        const int cj = 6 * j;
        // Em coordenadas naturais [-1, 1], d()/dxi = 0.5*(val_j - val_i)
        row(ci + 2) -= 0.5;
        row(cj + 2) += 0.5;
        row(ci + 4) += 0.25 * dx;
        row(cj + 4) += 0.25 * dx;
        row(ci + 3) -= 0.25 * dy;
        row(cj + 3) -= 0.25 * dy;
        return row;
    };

    const auto g_A = edgeRow(0, 1); // xi=0, eta=-1
    const auto g_C = edgeRow(3, 2); // xi=0, eta=+1
    const auto g_D = edgeRow(0, 3); // xi=-1, eta=0
    const auto g_B = edgeRow(1, 2); // xi=+1, eta=0

    G.row(0) = 0.5 * (1.0 - eta) * g_A + 0.5 * (1.0 + eta) * g_C;
    G.row(1) = 0.5 * (1.0 - xi) * g_D + 0.5 * (1.0 + xi) * g_B;

    return invJ * G;
}

double planeStressVonMises(double sx, double sy, double txy, double txz = 0.0, double tyz = 0.0) {
    const double val = sx * sx - sx * sy + sy * sy + 3.0 * (txy * txy + txz * txz + tyz * tyz);
    return std::sqrt(std::max(0.0, val));
}

MatX addDrillingStabilization(int nNodes,
                              const std::vector<Eigen::Vector2d>& xy,
                              double area,
                              double tMean,
                              double G,
                              double scale)
{
    const int ndof = 6 * nNodes;
    MatX Kd = MatX::Zero(ndof, ndof);
    if (scale <= 0.0 || area <= 0.0 || G <= 0.0) return Kd;

    // Hughes-Brezzi no centroide: penaliza (theta_z_bar - 0.5*(dv/dx - du/dy)|0)
    ShapeEval center;
    if (nNodes == 3) {
        tri3Shape(1.0 / 3.0, 1.0 / 3.0, xy, center);
    } else if (nNodes == 4) {
        quad4Shape(0.0, 0.0, xy, center);
    } else {
        quad8Shape(0.0, 0.0, xy, center);
    }

    VecX bSpin = VecX::Zero(ndof);
    const double invN = 1.0 / static_cast<double>(nNodes);
    for (int i = 0; i < nNodes; ++i) {
        const int col = 6 * i;
        bSpin(col + 0) = +0.5 * center.dNdy(i);  // -0.5 * du/dy
        bSpin(col + 1) = -0.5 * center.dNdx(i);  // +0.5 * dv/dx (theta_z - omega_z)
        bSpin(col + 5) = invN;
    }

    const double kSpin = scale * G * tMean * area;
    Kd += kSpin * (bSpin * bSpin.transpose());

    // Estabilização suave de modos diferenciais (theta_z_i - theta_z_bar), nula em rotação rígida!
    const double kVar = 1e-2 * scale * G * tMean * area * invN;
    for (int i = 0; i < nNodes; ++i) {
        VecX bDiff = VecX::Zero(ndof);
        for (int j = 0; j < nNodes; ++j) {
            bDiff(6 * j + 5) = (i == j ? 1.0 : 0.0) - invN;
        }
        Kd += kVar * (bDiff * bDiff.transpose());
    }
    return Kd;
}

} // namespace

ShellElement::ShellElement(ElementId eid,
                           ShellElementType shellType,
                           std::vector<NodeId> nodeIds,
                           std::vector<Vec3> nodeCoords,
                           std::shared_ptr<const IConstitutiveModel> material,
                           double uniformThickness,
                           std::vector<double> nodalThicknesses,
                           double drillingStiffnessScale)
    : m_id(eid)
    , m_type(shellType)
    , m_nodeIds(std::move(nodeIds))
    , m_coords(std::move(nodeCoords))
    , m_material(std::move(material))
    , m_thickness(uniformThickness)
    , m_nodalThick(std::move(nodalThicknesses))
    , m_drillingScale(drillingStiffnessScale)
{
    size_t expectedNodes = 4;
    if (m_type == ShellElementType::Tri3MITC3) expectedNodes = 3;
    else if (m_type == ShellElementType::Quad8Serendipity) expectedNodes = 8;

    if (m_nodeIds.size() != expectedNodes || m_coords.size() != expectedNodes) {
        throw std::invalid_argument("Número de nós/coordenadas incompatível com ShellElementType.");
    }
    if (!m_material) {
        throw std::invalid_argument("ShellElement requer IConstitutiveModel válido.");
    }
    if (m_thickness <= 0.0 && m_nodalThick.empty()) {
        throw std::invalid_argument("Espessura de casca deve ser estritamente positiva.");
    }
    initializeGeometry();
}

std::string ShellElement::typeName() const {
    switch (m_type) {
        case ShellElementType::Tri3MITC3: return "ShellTri3MITC3";
        case ShellElementType::Quad4MITC4: return "ShellQuad4MITC4";
        case ShellElementType::Quad8Serendipity: return "ShellQuad8Serendipity";
    }
    return "Shell";
}

void ShellElement::initializeGeometry() {
    Vec3 centroid = Vec3::Zero();
    for (const auto& p : m_coords) centroid += p;
    centroid /= static_cast<double>(m_coords.size());

    Vec3 n = Vec3::Zero();
    if (m_type == ShellElementType::Tri3MITC3) {
        n = (m_coords[1] - m_coords[0]).cross(m_coords[2] - m_coords[0]);
    } else {
        // Normal média por diagonais para quadriláteros (exata no plano e robusta para leve empenamento)
        const Vec3 d1 = m_coords[2] - m_coords[0];
        const Vec3 d2 = m_coords[3] - m_coords[1];
        n = d1.cross(d2);
    }
    const double nNorm = n.norm();
    if (nNorm <= 1e-14) {
        throw std::runtime_error("Geometria degenerada (área nula) em ShellElement.");
    }
    const Vec3 e3 = n / nNorm;

    Vec3 v1 = m_coords[1] - m_coords[0];
    v1 -= v1.dot(e3) * e3;
    if (v1.norm() <= 1e-14) {
        v1 = (m_coords[2] - m_coords[0]);
        v1 -= v1.dot(e3) * e3;
    }
    const Vec3 e1 = v1.normalized();
    const Vec3 e2 = e3.cross(e1).normalized();

    m_localTriad.col(0) = e1;
    m_localTriad.col(1) = e2;
    m_localTriad.col(2) = e3;

    m_localXY.clear();
    m_localXY.reserve(m_coords.size());
    for (const auto& p : m_coords) {
        const Vec3 d = p - centroid;
        m_localXY.emplace_back(d.dot(e1), d.dot(e2));
    }
}

double ShellElement::thicknessAtWeights(const VecX& shapeN) const {
    if (m_nodalThick.size() == m_coords.size()) {
        double t = 0.0;
        for (int i = 0; i < shapeN.size(); ++i) t += shapeN(i) * m_nodalThick[i];
        return std::max(1e-9, t);
    }
    return m_thickness;
}

Eigen::Matrix<double, 2, 2> ShellElement::transverseShearModulusMatrix() const {
    const double G = m_material->shearModulus();
    const double ks = 5.0 / 6.0;
    Eigen::Matrix<double, 2, 2> Gs = Eigen::Matrix<double, 2, 2>::Zero();
    Gs(0, 0) = ks * G;
    Gs(1, 1) = ks * G;
    return Gs;
}

MatX ShellElement::buildLocalToGlobalBlock() const {
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 6 * nNodes;
    MatX T = MatX::Zero(ndof, ndof);

    Vec3 centroid = Vec3::Zero();
    for (const auto& p : m_coords) centroid += p;
    centroid /= static_cast<double>(m_coords.size());

    const Mat3 R = m_localTriad.transpose(); // R * v_global = v_local
    const Vec3 e3 = m_localTriad.col(2);

    for (int i = 0; i < nNodes; ++i) {
        const int off = 6 * i;
        // Compensação rígida exata de empenamento fora do plano médio (det = 1):
        // u_flat = u_node - z_i * theta_y, v_flat = v_node + z_i * theta_x
        const double zOff = (m_coords[i] - centroid).dot(e3);
        Eigen::Matrix<double, 6, 6> W = Eigen::Matrix<double, 6, 6>::Identity();
        W(0, 4) = -zOff;
        W(1, 3) = +zOff;

        Eigen::Matrix<double, 6, 6> R6 = Eigen::Matrix<double, 6, 6>::Zero();
        R6.block<3, 3>(0, 0) = R;
        R6.block<3, 3>(3, 3) = R;
        T.block<6, 6>(off, off) = W * R6;
    }
    return T;
}

double ShellElement::computeArea() const {
    if (m_type == ShellElementType::Tri3MITC3) {
        const double x1 = m_localXY[1].x() - m_localXY[0].x();
        const double y1 = m_localXY[1].y() - m_localXY[0].y();
        const double x2 = m_localXY[2].x() - m_localXY[0].x();
        const double y2 = m_localXY[2].y() - m_localXY[0].y();
        return 0.5 * std::abs(x1 * y2 - y1 * x2);
    }
    double area = 0.0;
    for (const auto& gp : gauss2D(2)) {
        ShapeEval sh;
        if (m_type == ShellElementType::Quad4MITC4) quad4Shape(gp.xi, gp.eta, m_localXY, sh);
        else quad8Shape(gp.xi, gp.eta, m_localXY, sh);
        area += gp.w * sh.detJ;
    }
    return area;
}

MatX ShellElement::stiffnessTri3() const {
    MatX Kloc = MatX::Zero(18, 18);
    const Mat3 Dps = m_material->constitutiveMatrixPlaneStress();
    const Mat2 Gs = transverseShearModulusMatrix();

    double area = 0.0;
    double tAvg = 0.0;
    for (const auto& gp : triGauss3()) {
        ShapeEval sh;
        tri3Shape(gp.r, gp.s, m_localXY, sh);
        const double dA = gp.w * sh.detJ;
        area += dA;
        tAvg += dA * thicknessAtWeights(sh.N);
    }
    tAvg = (area > 0.0) ? (tAvg / area) : m_thickness;

    const Tri3DstBasis dst = buildTri3DstBasis(m_localXY, tAvg, Dps, Gs);
    const MatX Bs = dst.H_g;

    // 3 pontos de Hammer integram exatamente curvatura linear (K quadrática), membrana e espessura variável
    for (const auto& gp : triGauss3()) {
        ShapeEval sh;
        tri3Shape(gp.r, gp.s, m_localXY, sh);
        const double dA = gp.w * sh.detJ;
        const double t = thicknessAtWeights(sh.N);

        const MatX Bm = buildMembraneB(3, sh.dNdx, sh.dNdy);
        const MatX Bb = dst.evalBendingB(sh.N);

        const Mat3 Cm = t * Dps;
        const Mat3 Cb = (t * t * t / 12.0) * Dps;
        const Mat2 Cs = t * Gs;

        Kloc.noalias() += dA * (Bm.transpose() * Cm * Bm);
        Kloc.noalias() += dA * (Bb.transpose() * Cb * Bb);
        Kloc.noalias() += dA * (Bs.transpose() * Cs * Bs);
    }

    Kloc += addDrillingStabilization(3, m_localXY, area, tAvg, m_material->shearModulus(), m_drillingScale);
    return Kloc;
}

MatX ShellElement::stiffnessQuad4() const {
    MatX Kloc = MatX::Zero(24, 24);
    const Mat3 Dps = m_material->constitutiveMatrixPlaneStress();
    const Mat2 Gs = transverseShearModulusMatrix();

    // Modos incompatíveis QM6 de Taylor-Beresford-Wilson (1976) para membrana livre de travamento
    // usando Jacobiano central J0 ponderado por (detJ0 * t0 / (detJ * t)) para exatidão no Patch Test
    ShapeEval centerSh;
    quad4Shape(0.0, 0.0, m_localXY, centerSh);
    const Mat2 invJ0 = centerSh.J.inverse();
    const double detJ0 = centerSh.detJ;
    const double t0 = thicknessAtWeights(centerSh.N);

    MatX Kua = MatX::Zero(24, 4);
    Mat4 Kaa = Mat4::Zero();

    double area = 0.0;
    double tAvg = 0.0;

    for (const auto& gp : gauss2D(2)) {
        ShapeEval sh;
        quad4Shape(gp.xi, gp.eta, m_localXY, sh);
        const double dA = gp.w * sh.detJ;
        area += dA;

        const double t = thicknessAtWeights(sh.N);
        tAvg += dA * t;

        const Mat3 Cm = t * Dps;
        const Mat3 Cb = (t * t * t / 12.0) * Dps;
        const Mat2 Cs = t * Gs;

        const MatX Bm = buildMembraneB(4, sh.dNdx, sh.dNdy);
        const MatX Bb = buildBendingB(4, sh.dNdx, sh.dNdy);
        const MatX Bs = buildMITC4ShearB(gp.xi, gp.eta, m_localXY, sh.J.inverse());

        Kloc.noalias() += dA * (Bm.transpose() * Cm * Bm);
        Kloc.noalias() += dA * (Bb.transpose() * Cb * Bb);
        Kloc.noalias() += dA * (Bs.transpose() * Cs * Bs);

        // Matriz de deformação dos 2 modos incompatíveis P1(xi)=1-xi^2, P2(eta)=1-eta^2
        const double wRatio = (detJ0 * t0) / (sh.detJ * t);
        const Eigen::Vector2d gP1 = wRatio * (invJ0 * Eigen::Vector2d(-2.0 * gp.xi, 0.0));
        const Eigen::Vector2d gP2 = wRatio * (invJ0 * Eigen::Vector2d(0.0, -2.0 * gp.eta));

        Eigen::Matrix<double, 3, 4> Ba = Eigen::Matrix<double, 3, 4>::Zero();
        // DOFs incompatíveis: [alpha_u1, alpha_v1, alpha_u2, alpha_v2]
        Ba(0, 0) = gP1.x();
        Ba(2, 0) = gP1.y();
        Ba(1, 1) = gP1.y();
        Ba(2, 1) = gP1.x();

        Ba(0, 2) = gP2.x();
        Ba(2, 2) = gP2.y();
        Ba(1, 3) = gP2.y();
        Ba(2, 3) = gP2.x();

        Kua.noalias() += dA * (Bm.transpose() * Cm * Ba);
        Kaa.noalias() += dA * (Ba.transpose() * Cm * Ba);
    }

    // Condensação estática dos modos internos incompatíveis QM6
    const double detKaa = Kaa.determinant();
    if (std::abs(detKaa) > 1e-24) {
        Kloc.noalias() -= Kua * Kaa.ldlt().solve(Kua.transpose());
    }

    tAvg = (area > 0.0) ? (tAvg / area) : m_thickness;
    Kloc += addDrillingStabilization(4, m_localXY, area, tAvg, m_material->shearModulus(), m_drillingScale);
    return Kloc;
}

MatX ShellElement::stiffnessQuad8() const {
    MatX Kloc = MatX::Zero(48, 48);
    const Mat3 Dps = m_material->constitutiveMatrixPlaneStress();
    const Mat2 Gs = transverseShearModulusMatrix();

    double area = 0.0;
    double tAvg = 0.0;

    // Membrana + flexão: quadratura completa 3x3 (sem modos espúrios de ampulheta)
    for (const auto& gp : gauss2D(3)) {
        ShapeEval sh;
        quad8Shape(gp.xi, gp.eta, m_localXY, sh);
        const double dA = gp.w * sh.detJ;
        area += dA;

        const double t = thicknessAtWeights(sh.N);
        tAvg += dA * t;

        const Mat3 Cm = t * Dps;
        const Mat3 Cb = (t * t * t / 12.0) * Dps;

        const MatX Bm = buildMembraneB(8, sh.dNdx, sh.dNdy);
        const MatX Bb = buildBendingB(8, sh.dNdx, sh.dNdy);

        Kloc.noalias() += dA * (Bm.transpose() * Cm * Bm);
        Kloc.noalias() += dA * (Bb.transpose() * Cb * Bb);
    }

    // Cisalhamento transversal: integração reduzida seletiva 2x2 contra shear locking
    for (const auto& gp : gauss2D(2)) {
        ShapeEval sh;
        quad8Shape(gp.xi, gp.eta, m_localXY, sh);
        const double dA = gp.w * sh.detJ;
        const double t = thicknessAtWeights(sh.N);
        const Mat2 Cs = t * Gs;
        const MatX Bs = buildIsoparametricShearB(8, sh.N, sh.dNdx, sh.dNdy);
        Kloc.noalias() += dA * (Bs.transpose() * Cs * Bs);
    }

    tAvg = (area > 0.0) ? (tAvg / area) : m_thickness;
    Kloc += addDrillingStabilization(8, m_localXY, area, tAvg, m_material->shearModulus(), m_drillingScale);
    return Kloc;
}

MatX ShellElement::computeGlobalStiffness() const {
    MatX Kloc;
    switch (m_type) {
        case ShellElementType::Tri3MITC3: Kloc = stiffnessTri3(); break;
        case ShellElementType::Quad4MITC4: Kloc = stiffnessQuad4(); break;
        case ShellElementType::Quad8Serendipity: Kloc = stiffnessQuad8(); break;
    }
    const MatX T = buildLocalToGlobalBlock();
    return 0.5 * (T.transpose() * Kloc * T + (T.transpose() * Kloc * T).transpose());
}

MatX ShellElement::computeGlobalMass(bool consistent) const {
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 6 * nNodes;
    const double rho = m_material->consistentMassDensity();
    if (rho <= 0.0) return MatX::Zero(ndof, ndof);

    MatX Mloc = MatX::Zero(ndof, ndof);
    double totalTransMass = 0.0;

    auto accumulatePoint = [&](const VecX& N, double dA) {
        const double t = thicknessAtWeights(N);
        const double mA = rho * t * dA;
        const double rI = rho * (t * t * t / 12.0) * dA;
        totalTransMass += mA;
        for (int i = 0; i < nNodes; ++i) {
            for (int j = 0; j < nNodes; ++j) {
                const double nij = N(i) * N(j);
                const int ri = 6 * i;
                const int cj = 6 * j;
                Mloc(ri + 0, cj + 0) += mA * nij;
                Mloc(ri + 1, cj + 1) += mA * nij;
                Mloc(ri + 2, cj + 2) += mA * nij;
                Mloc(ri + 3, cj + 3) += rI * nij;
                Mloc(ri + 4, cj + 4) += rI * nij;
            }
        }
    };

    if (m_type == ShellElementType::Tri3MITC3) {
        for (const auto& gp : triGauss3()) {
            ShapeEval sh;
            tri3Shape(gp.r, gp.s, m_localXY, sh);
            accumulatePoint(sh.N, gp.w * sh.detJ);
        }
    } else {
        const int order = (m_type == ShellElementType::Quad8Serendipity) ? 3 : 2;
        for (const auto& gp : gauss2D(order)) {
            ShapeEval sh;
            if (m_type == ShellElementType::Quad4MITC4) quad4Shape(gp.xi, gp.eta, m_localXY, sh);
            else quad8Shape(gp.xi, gp.eta, m_localXY, sh);
            accumulatePoint(sh.N, gp.w * sh.detJ);
        }
    }

    if (!consistent) {
        // Lumping HRZ (Hinton-Rock-Zienkiewicz) preservando massa translacional exata e sinais positivos
        MatX Mlump = MatX::Zero(ndof, ndof);
        for (int d = 0; d < 5; ++d) {
            double diagSum = 0.0;
            for (int i = 0; i < nNodes; ++i) diagSum += std::max(0.0, Mloc(6 * i + d, 6 * i + d));
            if (diagSum > 1e-24 && d < 3) {
                const double scale = totalTransMass / diagSum;
                for (int i = 0; i < nNodes; ++i) {
                    Mlump(6 * i + d, 6 * i + d) = std::max(0.0, Mloc(6 * i + d, 6 * i + d)) * scale;
                }
            } else {
                for (int i = 0; i < nNodes; ++i) {
                    Mlump(6 * i + d, 6 * i + d) = std::max(0.0, Mloc(6 * i + d, 6 * i + d));
                }
            }
        }
        Mloc = std::move(Mlump);
    }

    const MatX T = buildLocalToGlobalBlock();
    return T.transpose() * Mloc * T;
}

MatX ShellElement::computeGlobalGeometricStiffness(const VecX& elementGlobalDisp) const {
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 6 * nNodes;
    if (elementGlobalDisp.size() != ndof) return MatX::Zero(ndof, ndof);

    const ShellStressResultant sr = evaluateCenterStress(elementGlobalDisp, 0.0);
    Mat2 Nmat;
    Nmat(0, 0) = sr.Nxx;
    Nmat(0, 1) = sr.Nxy;
    Nmat(1, 0) = sr.Nxy;
    Nmat(1, 1) = sr.Nyy;

    MatX Kgloc = MatX::Zero(ndof, ndof);
    auto accumPoint = [&](const ShapeEval& sh, double dA) {
        MatX Gw = MatX::Zero(2, ndof);
        for (int i = 0; i < nNodes; ++i) {
            Gw(0, 6 * i + 2) = sh.dNdx(i);
            Gw(1, 6 * i + 2) = sh.dNdy(i);
        }
        Kgloc.noalias() += dA * (Gw.transpose() * Nmat * Gw);
    };

    if (m_type == ShellElementType::Tri3MITC3) {
        for (const auto& gp : triGauss3()) {
            ShapeEval sh;
            tri3Shape(gp.r, gp.s, m_localXY, sh);
            accumPoint(sh, gp.w * sh.detJ);
        }
    } else {
        const int order = (m_type == ShellElementType::Quad8Serendipity) ? 3 : 2;
        for (const auto& gp : gauss2D(order)) {
            ShapeEval sh;
            if (m_type == ShellElementType::Quad4MITC4) quad4Shape(gp.xi, gp.eta, m_localXY, sh);
            else quad8Shape(gp.xi, gp.eta, m_localXY, sh);
            accumPoint(sh, gp.w * sh.detJ);
        }
    }

    const MatX T = buildLocalToGlobalBlock();
    return T.transpose() * Kgloc * T;
}

VecX ShellElement::computeGlobalLoadVector(const ElementLoadInput& load) const {
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 6 * nNodes;
    VecX Fglob = VecX::Zero(ndof);
    VecX FlocThermal = VecX::Zero(ndof);

    const Mat3 Dps = m_material->constitutiveMatrixPlaneStress();
    const double gamma = m_material->specificWeightKNM3();
    Vec3 epsTh = Vec3::Zero();
    if (load.hasThermal && std::abs(load.deltaT) > 0.0) {
        const auto eps3 = m_material->thermalStrain3D(load.deltaT);
        epsTh = {eps3(0), eps3(1), 0.0};
    }

    const Vec3 e3 = m_localTriad.col(2);
    auto accum = [&](const ShapeEval& sh, double dA) {
        const double t = thicknessAtWeights(sh.N);
        for (int i = 0; i < nNodes; ++i) {
            const int off = 6 * i;
            if (std::abs(load.normalPressure) > 0.0) {
                Fglob.segment<3>(off) += (sh.N(i) * load.normalPressure * dA) * e3;
            }
            if (load.hasBodyGravity && gamma > 0.0) {
                Fglob.segment<3>(off) += (sh.N(i) * gamma * t * dA) * load.gravityAccel;
            }
            if (load.hasDistributed && load.distributedForce.squaredNorm() > 0.0) {
                if (load.coordSystem == 1) {
                    const Vec3 fGlob = m_localTriad * load.distributedForce;
                    Fglob.segment<3>(off) += (sh.N(i) * dA) * fGlob;
                } else {
                    Fglob.segment<3>(off) += (sh.N(i) * dA) * load.distributedForce;
                }
            }
        }
        if (load.hasThermal && std::abs(load.deltaT) > 0.0) {
            const MatX Bm = buildMembraneB(nNodes, sh.dNdx, sh.dNdy);
            const Mat3 Cm = t * Dps;
            FlocThermal.noalias() += dA * (Bm.transpose() * (Cm * epsTh));
        }
    };

    if (m_type == ShellElementType::Tri3MITC3) {
        for (const auto& gp : triGauss3()) {
            ShapeEval sh;
            tri3Shape(gp.r, gp.s, m_localXY, sh);
            accum(sh, gp.w * sh.detJ);
        }
    } else {
        const int order = (m_type == ShellElementType::Quad8Serendipity) ? 3 : 2;
        for (const auto& gp : gauss2D(order)) {
            ShapeEval sh;
            if (m_type == ShellElementType::Quad4MITC4) quad4Shape(gp.xi, gp.eta, m_localXY, sh);
            else quad8Shape(gp.xi, gp.eta, m_localXY, sh);
            accum(sh, gp.w * sh.detJ);
        }
    }

    if (load.hasThermal && std::abs(load.deltaT) > 0.0) {
        const MatX T = buildLocalToGlobalBlock();
        Fglob.noalias() += T.transpose() * FlocThermal;
    }
    return Fglob;
}

ShellStressResultant ShellElement::evaluateCenterStress(const VecX& elementGlobalDisp, double deltaT) const {
    ShellStressResultant res;
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 6 * nNodes;
    if (elementGlobalDisp.size() != ndof) return res;

    const MatX T = buildLocalToGlobalBlock();
    const VecX uLoc = T * elementGlobalDisp;

    const Mat3 Dps = m_material->constitutiveMatrixPlaneStress();
    const Mat2 Gs = transverseShearModulusMatrix();

    ShapeEval cSh;
    MatX Bm;
    MatX Bb;
    MatX Bs;
    double t = m_thickness;
    if (m_type == ShellElementType::Tri3MITC3) {
        tri3Shape(1.0 / 3.0, 1.0 / 3.0, m_localXY, cSh);
        t = thicknessAtWeights(cSh.N);
        const Tri3DstBasis dst = buildTri3DstBasis(m_localXY, t, Dps, Gs);
        Bm = buildMembraneB(3, cSh.dNdx, cSh.dNdy);
        Bb = dst.evalBendingB(cSh.N);
        Bs = dst.H_g;
    } else if (m_type == ShellElementType::Quad4MITC4) {
        quad4Shape(0.0, 0.0, m_localXY, cSh);
        t = thicknessAtWeights(cSh.N);
        Bm = buildMembraneB(nNodes, cSh.dNdx, cSh.dNdy);
        Bb = buildBendingB(nNodes, cSh.dNdx, cSh.dNdy);
        Bs = buildMITC4ShearB(0.0, 0.0, m_localXY, cSh.J.inverse());
    } else {
        quad8Shape(0.0, 0.0, m_localXY, cSh);
        t = thicknessAtWeights(cSh.N);
        Bm = buildMembraneB(nNodes, cSh.dNdx, cSh.dNdy);
        Bb = buildBendingB(nNodes, cSh.dNdx, cSh.dNdy);
        Bs = buildIsoparametricShearB(8, cSh.N, cSh.dNdx, cSh.dNdy);
    }

    Vec3 epsM = Bm * uLoc;
    if (std::abs(deltaT) > 0.0) {
        const auto eps3 = m_material->thermalStrain3D(deltaT);
        epsM(0) -= eps3(0);
        epsM(1) -= eps3(1);
    }
    const Vec3 kappa = Bb * uLoc;
    const Vec2 gamma = Bs * uLoc;

    const Vec3 Nvec = (t * Dps) * epsM;
    const Vec3 Mvec = ((t * t * t / 12.0) * Dps) * kappa;
    const Vec2 Qvec = (t * Gs) * gamma;

    res.Nxx = Nvec(0);
    res.Nyy = Nvec(1);
    res.Nxy = Nvec(2);
    res.Mxx = Mvec(0);
    res.Myy = Mvec(1);
    res.Mxy = Mvec(2);
    res.Qxz = Qvec(0);
    res.Qyz = Qvec(1);

    const double sxMem = res.Nxx / t;
    const double syMem = res.Nyy / t;
    const double txyMem = res.Nxy / t;

    const double invW = 6.0 / (t * t);
    const double sxBend = res.Mxx * invW;
    const double syBend = res.Myy * invW;
    const double txyBend = res.Mxy * invW;

    const double txzMid = 1.5 * res.Qxz / t;
    const double tyzMid = 1.5 * res.Qyz / t;

    res.sigmaVonMisesTop = planeStressVonMises(sxMem + sxBend, syMem + syBend, txyMem + txyBend);
    res.sigmaVonMisesMid = planeStressVonMises(sxMem, syMem, txyMem, txzMid, tyzMid);
    res.sigmaVonMisesBot = planeStressVonMises(sxMem - sxBend, syMem - syBend, txyMem - txyBend);
    res.maxVonMises = std::max({res.sigmaVonMisesTop, res.sigmaVonMisesMid, res.sigmaVonMisesBot});
    return res;
}

} // namespace omniframe::kernel
