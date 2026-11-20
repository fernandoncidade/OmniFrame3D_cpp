#include "SolidElements.hpp"
#include <Eigen/Eigenvalues>
#include <Eigen/LU>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace omniframe::kernel {

namespace {

using Mat6 = Eigen::Matrix<double, 6, 6>;
using Vec6 = Eigen::Matrix<double, 6, 1>;

struct GaussPt3D {
    double xi = 0.0;
    double eta = 0.0;
    double zeta = 0.0;
    double w = 0.0;
};

void get1DGauss(int n, std::vector<double>& pts, std::vector<double>& wts) {
    if (n == 1) {
        pts = {0.0};
        wts = {2.0};
    } else if (n == 2) {
        const double p = 1.0 / std::sqrt(3.0);
        pts = {-p, p};
        wts = {1.0, 1.0};
    } else if (n == 3) {
        const double p = std::sqrt(3.0 / 5.0);
        pts = {-p, 0.0, p};
        wts = {5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0};
    } else {
        const double r1 = std::sqrt((3.0 - 2.0 * std::sqrt(6.0 / 5.0)) / 7.0);
        const double r2 = std::sqrt((3.0 + 2.0 * std::sqrt(6.0 / 5.0)) / 7.0);
        const double w1 = (18.0 + std::sqrt(30.0)) / 36.0;
        const double w2 = (18.0 - std::sqrt(30.0)) / 36.0;
        pts = {-r2, -r1, r1, r2};
        wts = {w2, w1, w1, w2};
    }
}

std::vector<GaussPt3D> hexGaussPoints(int n1D) {
    std::vector<double> p, w;
    get1DGauss(n1D, p, w);
    std::vector<GaussPt3D> out;
    out.reserve(p.size() * p.size() * p.size());
    for (size_t i = 0; i < p.size(); ++i) {
        for (size_t j = 0; j < p.size(); ++j) {
            for (size_t k = 0; k < p.size(); ++k) {
                out.push_back({p[i], p[j], p[k], w[i] * w[j] * w[k]});
            }
        }
    }
    return out;
}

// Quadratura de Duffy colapsada (4x3x3 = 36 pts): todos os pesos > 0, exata para polinômios até grau >= 5 em (L0..L3)
const std::vector<GaussPt3D>& tetHighOrderGauss() {
    static const std::vector<GaussPt3D> pts = []() {
        std::vector<double> pa, wa, pb, wb;
        get1DGauss(4, pa, wa);
        get1DGauss(3, pb, wb);
        std::vector<GaussPt3D> res;
        res.reserve(36);
        for (size_t ia = 0; ia < pa.size(); ++ia) {
            const double a = pa[ia];
            const double L1 = 0.5 * (1.0 + a);
            const double om1 = 0.5 * (1.0 - a);
            for (size_t ib = 0; ib < pb.size(); ++ib) {
                const double b = pb[ib];
                const double L2 = om1 * 0.5 * (1.0 + b);
                const double om2 = om1 * 0.5 * (1.0 - b);
                for (size_t ic = 0; ic < pb.size(); ++ic) {
                    const double c = pb[ic];
                    const double L3 = om2 * 0.5 * (1.0 + c);
                    const double jacDuffy = 0.125 * om1 * om1 * 0.5 * (1.0 - b);
                    const double w = wa[ia] * wb[ib] * wb[ic] * jacDuffy;
                    res.push_back({L1, L2, L3, w});
                }
            }
        }
        return res;
    }();
    return pts;
}

const std::vector<GaussPt3D>& tetHammer4() {
    static const double a = (5.0 - std::sqrt(5.0)) / 20.0;
    static const double b = (5.0 + 3.0 * std::sqrt(5.0)) / 20.0;
    static const std::vector<GaussPt3D> pts = {
        {a, a, a, 1.0 / 24.0},
        {b, a, a, 1.0 / 24.0},
        {a, b, a, 1.0 / 24.0},
        {a, a, b, 1.0 / 24.0}
    };
    return pts;
}

struct SolidShapeEval {
    VecX N;
    VecX dNdx;
    VecX dNdy;
    VecX dNdz;
    double detJ = 0.0;
};

void evaluateTet4Shape(double r, double s, double t,
                       const std::vector<Vec3>& coords,
                       SolidShapeEval& out,
                       bool throwOnBadJac = true)
{
    out.N.resize(4);
    out.N(0) = 1.0 - r - s - t;
    out.N(1) = r;
    out.N(2) = s;
    out.N(3) = t;

    Mat3 J;
    for (int j = 0; j < 3; ++j) {
        J(0, j) = coords[1](j) - coords[0](j);
        J(1, j) = coords[2](j) - coords[0](j);
        J(2, j) = coords[3](j) - coords[0](j);
    }
    out.detJ = J.determinant();
    if (out.detJ <= 1e-16) {
        if (throwOnBadJac) throw std::runtime_error("Jacobiano invertido ou nulo em SolidElement Tet4.");
        out.dNdx = VecX::Zero(4);
        out.dNdy = VecX::Zero(4);
        out.dNdz = VecX::Zero(4);
        return;
    }

    const Mat3 invJ = J.inverse();
    static constexpr double dNr[4] = {-1.0, 1.0, 0.0, 0.0};
    static constexpr double dNs[4] = {-1.0, 0.0, 1.0, 0.0};
    static constexpr double dNt[4] = {-1.0, 0.0, 0.0, 1.0};
    out.dNdx.resize(4);
    out.dNdy.resize(4);
    out.dNdz.resize(4);
    for (int i = 0; i < 4; ++i) {
        const Vec3 g = invJ * Vec3(dNr[i], dNs[i], dNt[i]);
        out.dNdx(i) = g.x();
        out.dNdy(i) = g.y();
        out.dNdz(i) = g.z();
    }
}

void evaluateTet10Shape(double r, double s, double t,
                        const std::vector<Vec3>& coords,
                        SolidShapeEval& out,
                        bool throwOnBadJac = true)
{
    const double L[4] = {1.0 - r - s - t, r, s, t};
    out.N.resize(10);
    for (int i = 0; i < 4; ++i) out.N(i) = L[i] * (2.0 * L[i] - 1.0);
    out.N(4) = 4.0 * L[0] * L[1];
    out.N(5) = 4.0 * L[1] * L[2];
    out.N(6) = 4.0 * L[0] * L[2];
    out.N(7) = 4.0 * L[0] * L[3];
    out.N(8) = 4.0 * L[1] * L[3];
    out.N(9) = 4.0 * L[2] * L[3];

    // dN/dL[0..3]
    double dNdL[10][4]{};
    for (int i = 0; i < 4; ++i) dNdL[i][i] = 4.0 * L[i] - 1.0;
    dNdL[4][0] = 4.0 * L[1]; dNdL[4][1] = 4.0 * L[0];
    dNdL[5][1] = 4.0 * L[2]; dNdL[5][2] = 4.0 * L[1];
    dNdL[6][0] = 4.0 * L[2]; dNdL[6][2] = 4.0 * L[0];
    dNdL[7][0] = 4.0 * L[3]; dNdL[7][3] = 4.0 * L[0];
    dNdL[8][1] = 4.0 * L[3]; dNdL[8][3] = 4.0 * L[1];
    dNdL[9][2] = 4.0 * L[3]; dNdL[9][3] = 4.0 * L[2];

    Eigen::Matrix<double, 10, 1> dNdr, dNds, dNdt;
    Mat3 J = Mat3::Zero();
    for (int i = 0; i < 10; ++i) {
        dNdr(i) = dNdL[i][1] - dNdL[i][0];
        dNds(i) = dNdL[i][2] - dNdL[i][0];
        dNdt(i) = dNdL[i][3] - dNdL[i][0];
        J.row(0) += dNdr(i) * coords[i].transpose();
        J.row(1) += dNds(i) * coords[i].transpose();
        J.row(2) += dNdt(i) * coords[i].transpose();
    }

    out.detJ = J.determinant();
    if (out.detJ <= 1e-16) {
        if (throwOnBadJac) throw std::runtime_error("Jacobiano invertido ou nulo em SolidElement Tet10.");
        out.dNdx = VecX::Zero(10);
        out.dNdy = VecX::Zero(10);
        out.dNdz = VecX::Zero(10);
        return;
    }

    const Mat3 invJ = J.inverse();
    out.dNdx.resize(10);
    out.dNdy.resize(10);
    out.dNdz.resize(10);
    for (int i = 0; i < 10; ++i) {
        const Vec3 g = invJ * Vec3(dNdr(i), dNds(i), dNdt(i));
        out.dNdx(i) = g.x();
        out.dNdy(i) = g.y();
        out.dNdz(i) = g.z();
    }
}

void evaluateHex8Shape(double xi, double eta, double zeta,
                       const std::vector<Vec3>& coords,
                       SolidShapeEval& out,
                       bool throwOnBadJac = true)
{
    static constexpr double sx[8] = {-1, 1, 1, -1, -1, 1, 1, -1};
    static constexpr double sy[8] = {-1, -1, 1, 1, -1, -1, 1, 1};
    static constexpr double sz[8] = {-1, -1, -1, -1, 1, 1, 1, 1};

    out.N.resize(8);
    Eigen::Matrix<double, 8, 1> dNdxi, dNdeta, dNdzeta;
    Mat3 J = Mat3::Zero();
    for (int i = 0; i < 8; ++i) {
        const double px = 1.0 + sx[i] * xi;
        const double py = 1.0 + sy[i] * eta;
        const double pz = 1.0 + sz[i] * zeta;
        out.N(i) = 0.125 * px * py * pz;
        dNdxi(i) = 0.125 * sx[i] * py * pz;
        dNdeta(i) = 0.125 * px * sy[i] * pz;
        dNdzeta(i) = 0.125 * px * py * sz[i];
        J.row(0) += dNdxi(i) * coords[i].transpose();
        J.row(1) += dNdeta(i) * coords[i].transpose();
        J.row(2) += dNdzeta(i) * coords[i].transpose();
    }

    out.detJ = J.determinant();
    if (out.detJ <= 1e-16) {
        if (throwOnBadJac) throw std::runtime_error("Jacobiano invertido ou nulo em SolidElement Hex8.");
        out.dNdx = VecX::Zero(8);
        out.dNdy = VecX::Zero(8);
        out.dNdz = VecX::Zero(8);
        return;
    }

    const Mat3 invJ = J.inverse();
    out.dNdx.resize(8);
    out.dNdy.resize(8);
    out.dNdz.resize(8);
    for (int i = 0; i < 8; ++i) {
        const Vec3 g = invJ * Vec3(dNdxi(i), dNdeta(i), dNdzeta(i));
        out.dNdx(i) = g.x();
        out.dNdy(i) = g.y();
        out.dNdz(i) = g.z();
    }
}

void evaluateHex20Shape(double xi, double eta, double zeta,
                        const std::vector<Vec3>& coords,
                        SolidShapeEval& out,
                        bool throwOnBadJac = true)
{
    static constexpr double sx[8] = {-1, 1, 1, -1, -1, 1, 1, -1};
    static constexpr double sy[8] = {-1, -1, 1, 1, -1, -1, 1, 1};
    static constexpr double sz[8] = {-1, -1, -1, -1, 1, 1, 1, 1};

    out.N.resize(20);
    Eigen::Matrix<double, 20, 1> dNdxi, dNdeta, dNdzeta;

    for (int i = 0; i < 8; ++i) {
        const double px = 1.0 + sx[i] * xi;
        const double py = 1.0 + sy[i] * eta;
        const double pz = 1.0 + sz[i] * zeta;
        const double f = sx[i] * xi + sy[i] * eta + sz[i] * zeta - 2.0;
        out.N(i) = 0.125 * px * py * pz * f;
        dNdxi(i) = 0.125 * sx[i] * py * pz * (f + px);
        dNdeta(i) = 0.125 * px * sy[i] * pz * (f + py);
        dNdzeta(i) = 0.125 * px * py * sz[i] * (f + pz);
    }

    // 12 nós de meio-aresta:
    //  8:(0,-1,-1), 9:(+1,0,-1), 10:(0,+1,-1), 11:(-1,0,-1)
    // 12:(0,-1,+1), 13:(+1,0,+1), 14:(0,+1,+1), 15:(-1,0,+1)
    // 16:(-1,-1,0), 17:(+1,-1,0), 18:(+1,+1,0), 19:(-1,+1,0)
    static constexpr double ms[12][3] = {
        { 0, -1, -1}, { 1,  0, -1}, { 0,  1, -1}, {-1,  0, -1},
        { 0, -1,  1}, { 1,  0,  1}, { 0,  1,  1}, {-1,  0,  1},
        {-1, -1,  0}, { 1, -1,  0}, { 1,  1,  0}, {-1,  1,  0}
    };

    for (int m = 0; m < 12; ++m) {
        const int idx = 8 + m;
        const double mx = ms[m][0], my = ms[m][1], mz = ms[m][2];
        if (mx == 0.0) {
            const double py = 1.0 + my * eta;
            const double pz = 1.0 + mz * zeta;
            const double oneM = 1.0 - xi * xi;
            out.N(idx) = 0.25 * oneM * py * pz;
            dNdxi(idx) = -0.5 * xi * py * pz;
            dNdeta(idx) = 0.25 * oneM * my * pz;
            dNdzeta(idx) = 0.25 * oneM * py * mz;
        } else if (my == 0.0) {
            const double px = 1.0 + mx * xi;
            const double pz = 1.0 + mz * zeta;
            const double oneM = 1.0 - eta * eta;
            out.N(idx) = 0.25 * px * oneM * pz;
            dNdxi(idx) = 0.25 * mx * oneM * pz;
            dNdeta(idx) = -0.5 * px * eta * pz;
            dNdzeta(idx) = 0.25 * px * oneM * mz;
        } else {
            const double px = 1.0 + mx * xi;
            const double py = 1.0 + my * eta;
            const double oneM = 1.0 - zeta * zeta;
            out.N(idx) = 0.25 * px * py * oneM;
            dNdxi(idx) = 0.25 * mx * py * oneM;
            dNdeta(idx) = 0.25 * px * my * oneM;
            dNdzeta(idx) = -0.5 * px * py * zeta;
        }
    }

    Mat3 J = Mat3::Zero();
    for (int i = 0; i < 20; ++i) {
        J.row(0) += dNdxi(i) * coords[i].transpose();
        J.row(1) += dNdeta(i) * coords[i].transpose();
        J.row(2) += dNdzeta(i) * coords[i].transpose();
    }

    out.detJ = J.determinant();
    if (out.detJ <= 1e-16) {
        if (throwOnBadJac) throw std::runtime_error("Jacobiano invertido ou nulo em SolidElement Hex20.");
        out.dNdx = VecX::Zero(20);
        out.dNdy = VecX::Zero(20);
        out.dNdz = VecX::Zero(20);
        return;
    }

    const Mat3 invJ = J.inverse();
    out.dNdx.resize(20);
    out.dNdy.resize(20);
    out.dNdz.resize(20);
    for (int i = 0; i < 20; ++i) {
        const Vec3 g = invJ * Vec3(dNdxi(i), dNdeta(i), dNdzeta(i));
        out.dNdx(i) = g.x();
        out.dNdy(i) = g.y();
        out.dNdz(i) = g.z();
    }
}

void evaluateSolidShape(SolidElementType type, double r, double s, double t,
                        const std::vector<Vec3>& coords,
                        SolidShapeEval& out,
                        bool throwOnBadJac = true)
{
    switch (type) {
        case SolidElementType::Tet4: evaluateTet4Shape(r, s, t, coords, out, throwOnBadJac); break;
        case SolidElementType::Tet10: evaluateTet10Shape(r, s, t, coords, out, throwOnBadJac); break;
        case SolidElementType::Hex8BBar: evaluateHex8Shape(r, s, t, coords, out, throwOnBadJac); break;
        case SolidElementType::Hex20Serendipity: evaluateHex20Shape(r, s, t, coords, out, throwOnBadJac); break;
    }
}

MatX buildStandardSolidB(int nNodes, const VecX& dNdx, const VecX& dNdy, const VecX& dNdz) {
    MatX B = MatX::Zero(6, 3 * nNodes);
    for (int i = 0; i < nNodes; ++i) {
        const int c = 3 * i;
        B(0, c + 0) = dNdx(i);
        B(1, c + 1) = dNdy(i);
        B(2, c + 2) = dNdz(i);
        B(3, c + 0) = dNdy(i); B(3, c + 1) = dNdx(i);
        B(4, c + 1) = dNdz(i); B(4, c + 2) = dNdy(i);
        B(5, c + 0) = dNdz(i); B(5, c + 2) = dNdx(i);
    }
    return B;
}

void computeHex8VolumetricAverage(const std::vector<Vec3>& coords,
                                 VecX& outMeanGx,
                                 VecX& outMeanGy,
                                 VecX& outMeanGz,
                                 double& outVol)
{
    outMeanGx = VecX::Zero(8);
    outMeanGy = VecX::Zero(8);
    outMeanGz = VecX::Zero(8);
    outVol = 0.0;
    for (const auto& gp : hexGaussPoints(2)) {
        SolidShapeEval sh;
        evaluateHex8Shape(gp.xi, gp.eta, gp.zeta, coords, sh);
        const double dV = gp.w * sh.detJ;
        outVol += dV;
        outMeanGx += dV * sh.dNdx;
        outMeanGy += dV * sh.dNdy;
        outMeanGz += dV * sh.dNdz;
    }
    if (outVol > 1e-16) {
        outMeanGx /= outVol;
        outMeanGy /= outVol;
        outMeanGz /= outVol;
    }
}

MatX buildHex8BBar(const SolidShapeEval& sh,
                   const VecX& meanGx,
                   const VecX& meanGy,
                   const VecX& meanGz)
{
    MatX B = buildStandardSolidB(8, sh.dNdx, sh.dNdy, sh.dNdz);
    for (int i = 0; i < 8; ++i) {
        const int c = 3 * i;
        const double bbx = (meanGx(i) - sh.dNdx(i)) / 3.0;
        const double bby = (meanGy(i) - sh.dNdy(i)) / 3.0;
        const double bbz = (meanGz(i) - sh.dNdz(i)) / 3.0;
        for (int row = 0; row < 3; ++row) {
            B(row, c + 0) += bbx;
            B(row, c + 1) += bby;
            B(row, c + 2) += bbz;
        }
    }
    return B;
}

SolidStressState packSolidStress(const Vec6& sig) {
    SolidStressState st;
    st.sxx = sig(0);
    st.syy = sig(1);
    st.szz = sig(2);
    st.txy = sig(3);
    st.tyz = sig(4);
    st.tzx = sig(5);
    st.hydrostaticPressure = -(st.sxx + st.syy + st.szz) / 3.0;

    const double d0 = st.sxx - st.syy;
    const double d1 = st.syy - st.szz;
    const double d2 = st.szz - st.sxx;
    const double vm2 = 0.5 * (d0 * d0 + d1 * d1 + d2 * d2) +
                       3.0 * (st.txy * st.txy + st.tyz * st.tyz + st.tzx * st.tzx);
    st.vonMises = std::sqrt(std::max(0.0, vm2));

    Mat3 S;
    S << st.sxx, st.txy, st.tzx,
         st.txy, st.syy, st.tyz,
         st.tzx, st.tyz, st.szz;
    Eigen::SelfAdjointEigenSolver<Mat3> es(S);
    if (es.info() == Eigen::Success) {
        st.minorPrincipal = es.eigenvalues()(0);
        st.majorPrincipal = es.eigenvalues()(2);
    }
    return st;
}

std::vector<GaussPt3D> integrationRuleFor(SolidElementType t, bool forMass = false) {
    switch (t) {
        case SolidElementType::Tet4:
            return {{0.25, 0.25, 0.25, 1.0 / 6.0}};
        case SolidElementType::Tet10:
            return forMass ? tetHighOrderGauss() : tetHammer4();
        case SolidElementType::Hex8BBar:
            return hexGaussPoints(2);
        case SolidElementType::Hex20Serendipity:
            return hexGaussPoints(3);
    }
    return hexGaussPoints(2);
}

} // namespace

SolidElement::SolidElement(ElementId eid,
                           SolidElementType solidType,
                           std::vector<NodeId> nodeIds,
                           std::vector<Vec3> nodeCoords,
                           std::shared_ptr<const IConstitutiveModel> material)
    : m_id(eid)
    , m_type(solidType)
    , m_nodeIds(std::move(nodeIds))
    , m_coords(std::move(nodeCoords))
    , m_material(std::move(material))
{
    size_t expected = 8;
    if (m_type == SolidElementType::Tet4) expected = 4;
    else if (m_type == SolidElementType::Tet10) expected = 10;
    else if (m_type == SolidElementType::Hex20Serendipity) expected = 20;

    if (m_nodeIds.size() != expected || m_coords.size() != expected) {
        throw std::invalid_argument("Número de nós/coordenadas incompatível com SolidElementType.");
    }
    if (!m_material) {
        throw std::invalid_argument("SolidElement requer IConstitutiveModel válido.");
    }
    validateGeometryOrThrow();
}

std::string SolidElement::typeName() const {
    switch (m_type) {
        case SolidElementType::Tet4: return "SolidTet4";
        case SolidElementType::Tet10: return "SolidTet10";
        case SolidElementType::Hex8BBar: return "SolidHex8BBar";
        case SolidElementType::Hex20Serendipity: return "SolidHex20Serendipity";
    }
    return "Solid";
}

double SolidElement::computeMinJacobianDet() const {
    double minJ = 1e300;
    const auto pts = integrationRuleFor(m_type, false);
    for (const auto& gp : pts) {
        SolidShapeEval sh;
        evaluateSolidShape(m_type, gp.xi, gp.eta, gp.zeta, m_coords, sh, false);
        minJ = std::min(minJ, sh.detJ);
    }
    // Também verifica nos cantos para Hex8 / Hex20 para rejeitar distorções severas invertidas
    if (m_type == SolidElementType::Hex8BBar || m_type == SolidElementType::Hex20Serendipity) {
        static constexpr double s[8][3] = {
            {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
        };
        for (int c = 0; c < 8; ++c) {
            SolidShapeEval sh;
            evaluateSolidShape(m_type, s[c][0], s[c][1], s[c][2], m_coords, sh, false);
            minJ = std::min(minJ, sh.detJ);
        }
    }
    return minJ;
}

void SolidElement::validateGeometryOrThrow() const {
    const double minJ = computeMinJacobianDet();
    if (minJ <= 1e-16) {
        throw std::runtime_error("Geometria inválida (Jacobiano nulo ou invertido) em SolidElement.");
    }
}

double SolidElement::computeVolume() const {
    double vol = 0.0;
    for (const auto& gp : integrationRuleFor(m_type, true)) {
        SolidShapeEval sh;
        evaluateSolidShape(m_type, gp.xi, gp.eta, gp.zeta, m_coords, sh);
        vol += gp.w * sh.detJ;
    }
    return vol;
}

MatX SolidElement::stiffnessTet4() const {
    MatX K = MatX::Zero(12, 12);
    const Mat6 D = m_material->constitutiveMatrix3D();
    for (const auto& gp : integrationRuleFor(SolidElementType::Tet4)) {
        SolidShapeEval sh;
        evaluateTet4Shape(gp.xi, gp.eta, gp.zeta, m_coords, sh);
        const MatX B = buildStandardSolidB(4, sh.dNdx, sh.dNdy, sh.dNdz);
        K.noalias() += (gp.w * sh.detJ) * (B.transpose() * D * B);
    }
    return 0.5 * (K + K.transpose());
}

MatX SolidElement::stiffnessTet10() const {
    MatX K = MatX::Zero(30, 30);
    const Mat6 D = m_material->constitutiveMatrix3D();
    for (const auto& gp : tetHighOrderGauss()) {
        SolidShapeEval sh;
        evaluateTet10Shape(gp.xi, gp.eta, gp.zeta, m_coords, sh);
        const MatX B = buildStandardSolidB(10, sh.dNdx, sh.dNdy, sh.dNdz);
        K.noalias() += (gp.w * sh.detJ) * (B.transpose() * D * B);
    }
    return 0.5 * (K + K.transpose());
}

MatX SolidElement::stiffnessHex8BBar() const {
    MatX K = MatX::Zero(24, 24);
    const Mat6 D = m_material->constitutiveMatrix3D();

    VecX meanGx, meanGy, meanGz;
    double vol = 0.0;
    computeHex8VolumetricAverage(m_coords, meanGx, meanGy, meanGz, vol);

    for (const auto& gp : hexGaussPoints(2)) {
        SolidShapeEval sh;
        evaluateHex8Shape(gp.xi, gp.eta, gp.zeta, m_coords, sh);
        const MatX Bbar = buildHex8BBar(sh, meanGx, meanGy, meanGz);
        K.noalias() += (gp.w * sh.detJ) * (Bbar.transpose() * D * Bbar);
    }
    return 0.5 * (K + K.transpose());
}

MatX SolidElement::stiffnessHex20() const {
    MatX K = MatX::Zero(60, 60);
    const Mat6 D = m_material->constitutiveMatrix3D();
    for (const auto& gp : hexGaussPoints(3)) {
        SolidShapeEval sh;
        evaluateHex20Shape(gp.xi, gp.eta, gp.zeta, m_coords, sh);
        const MatX B = buildStandardSolidB(20, sh.dNdx, sh.dNdy, sh.dNdz);
        K.noalias() += (gp.w * sh.detJ) * (B.transpose() * D * B);
    }
    return 0.5 * (K + K.transpose());
}

MatX SolidElement::computeGlobalStiffness() const {
    switch (m_type) {
        case SolidElementType::Tet4: return stiffnessTet4();
        case SolidElementType::Tet10: return stiffnessTet10();
        case SolidElementType::Hex8BBar: return stiffnessHex8BBar();
        case SolidElementType::Hex20Serendipity: return stiffnessHex20();
    }
    return stiffnessHex8BBar();
}

MatX SolidElement::computeGlobalMass(bool consistent) const {
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 3 * nNodes;
    const double rho = m_material->consistentMassDensity();
    if (rho <= 0.0) return MatX::Zero(ndof, ndof);

    MatX M = MatX::Zero(ndof, ndof);
    double totalMass = 0.0;

    for (const auto& gp : integrationRuleFor(m_type, true)) {
        SolidShapeEval sh;
        evaluateSolidShape(m_type, gp.xi, gp.eta, gp.zeta, m_coords, sh);
        const double dm = rho * gp.w * sh.detJ;
        totalMass += dm;
        for (int i = 0; i < nNodes; ++i) {
            for (int j = 0; j < nNodes; ++j) {
                const double mij = dm * sh.N(i) * sh.N(j);
                M(3 * i + 0, 3 * j + 0) += mij;
                M(3 * i + 1, 3 * j + 1) += mij;
                M(3 * i + 2, 3 * j + 2) += mij;
            }
        }
    }

    if (!consistent) {
        MatX Mlump = MatX::Zero(ndof, ndof);
        for (int d = 0; d < 3; ++d) {
            double diagSum = 0.0;
            for (int i = 0; i < nNodes; ++i) diagSum += std::max(0.0, M(3 * i + d, 3 * i + d));
            const double scale = (diagSum > 1e-24) ? (totalMass / diagSum) : 0.0;
            for (int i = 0; i < nNodes; ++i) {
                Mlump(3 * i + d, 3 * i + d) = std::max(0.0, M(3 * i + d, 3 * i + d)) * scale;
            }
        }
        return Mlump;
    }
    return M;
}

MatX SolidElement::computeGlobalGeometricStiffness(const VecX& elementGlobalDisp) const {
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 3 * nNodes;
    if (elementGlobalDisp.size() != ndof) return MatX::Zero(ndof, ndof);

    const SolidStressState st = evaluateCenterStress(elementGlobalDisp, 0.0);
    Mat3 S;
    S << st.sxx, st.txy, st.tzx,
         st.txy, st.syy, st.tyz,
         st.tzx, st.tyz, st.szz;

    MatX Kg = MatX::Zero(ndof, ndof);
    for (const auto& gp : integrationRuleFor(m_type, false)) {
        SolidShapeEval sh;
        evaluateSolidShape(m_type, gp.xi, gp.eta, gp.zeta, m_coords, sh);
        const double dV = gp.w * sh.detJ;
        for (int i = 0; i < nNodes; ++i) {
            const Vec3 gi(sh.dNdx(i), sh.dNdy(i), sh.dNdz(i));
            const Vec3 Sgi = S * gi;
            for (int j = i; j < nNodes; ++j) {
                const Vec3 gj(sh.dNdx(j), sh.dNdy(j), sh.dNdz(j));
                const double val = dV * gj.dot(Sgi);
                for (int d = 0; d < 3; ++d) {
                    Kg(3 * i + d, 3 * j + d) += val;
                    if (i != j) Kg(3 * j + d, 3 * i + d) += val;
                }
            }
        }
    }
    return Kg;
}

VecX SolidElement::computeGlobalLoadVector(const ElementLoadInput& load) const {
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 3 * nNodes;
    VecX F = VecX::Zero(ndof);

    const double gamma = m_material->specificWeightKNM3();
    const Mat6 D = m_material->constitutiveMatrix3D();
    Vec6 epsTh = Vec6::Zero();
    if (load.hasThermal && std::abs(load.deltaT) > 0.0) {
        epsTh = m_material->thermalStrain3D(load.deltaT);
    }

    VecX meanGx, meanGy, meanGz;
    double hexVol = 0.0;
    if (m_type == SolidElementType::Hex8BBar && load.hasThermal && std::abs(load.deltaT) > 0.0) {
        computeHex8VolumetricAverage(m_coords, meanGx, meanGy, meanGz, hexVol);
    }

    for (const auto& gp : integrationRuleFor(m_type, true)) {
        SolidShapeEval sh;
        evaluateSolidShape(m_type, gp.xi, gp.eta, gp.zeta, m_coords, sh);
        const double dV = gp.w * sh.detJ;

        for (int i = 0; i < nNodes; ++i) {
            if (load.hasBodyGravity && gamma > 0.0) {
                F.segment<3>(3 * i) += (sh.N(i) * gamma * dV) * load.gravityAccel;
            }
            if (load.hasDistributed && load.distributedForce.squaredNorm() > 0.0) {
                F.segment<3>(3 * i) += (sh.N(i) * dV) * load.distributedForce;
            }
        }

        if (load.hasThermal && std::abs(load.deltaT) > 0.0) {
            const MatX B = (m_type == SolidElementType::Hex8BBar)
                ? buildHex8BBar(sh, meanGx, meanGy, meanGz)
                : buildStandardSolidB(nNodes, sh.dNdx, sh.dNdy, sh.dNdz);
            F.noalias() += dV * (B.transpose() * (D * epsTh));
        }
    }
    return F;
}

SolidStressState SolidElement::evaluateCenterStress(const VecX& elementGlobalDisp, double deltaT) const {
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 3 * nNodes;
    if (elementGlobalDisp.size() != ndof) return {};

    double r = 0.0, s = 0.0, t = 0.0;
    if (m_type == SolidElementType::Tet4 || m_type == SolidElementType::Tet10) {
        r = 0.25;
        s = 0.25;
        t = 0.25;
    }

    SolidShapeEval sh;
    evaluateSolidShape(m_type, r, s, t, m_coords, sh);
    MatX B;
    if (m_type == SolidElementType::Hex8BBar) {
        VecX gx, gy, gz;
        double vol = 0.0;
        computeHex8VolumetricAverage(m_coords, gx, gy, gz, vol);
        B = buildHex8BBar(sh, gx, gy, gz);
    } else {
        B = buildStandardSolidB(nNodes, sh.dNdx, sh.dNdy, sh.dNdz);
    }

    Vec6 eps = B * elementGlobalDisp;
    if (std::abs(deltaT) > 0.0) eps -= m_material->thermalStrain3D(deltaT);
    const Vec6 sig = m_material->constitutiveMatrix3D() * eps;
    return packSolidStress(sig);
}

std::vector<SolidStressState> SolidElement::evaluateGaussPointStresses(
    const VecX& elementGlobalDisp,
    double deltaT) const
{
    const int nNodes = static_cast<int>(m_nodeIds.size());
    const int ndof = 3 * nNodes;
    std::vector<SolidStressState> out;
    if (elementGlobalDisp.size() != ndof) return out;

    const Mat6 D = m_material->constitutiveMatrix3D();
    Vec6 epsTh = Vec6::Zero();
    if (std::abs(deltaT) > 0.0) epsTh = m_material->thermalStrain3D(deltaT);

    VecX meanGx, meanGy, meanGz;
    double hexVol = 0.0;
    if (m_type == SolidElementType::Hex8BBar) {
        computeHex8VolumetricAverage(m_coords, meanGx, meanGy, meanGz, hexVol);
    }

    const auto pts = integrationRuleFor(m_type, false);
    out.reserve(pts.size());
    for (const auto& gp : pts) {
        SolidShapeEval sh;
        evaluateSolidShape(m_type, gp.xi, gp.eta, gp.zeta, m_coords, sh);
        const MatX B = (m_type == SolidElementType::Hex8BBar)
            ? buildHex8BBar(sh, meanGx, meanGy, meanGz)
            : buildStandardSolidB(nNodes, sh.dNdx, sh.dNdy, sh.dNdz);
        const Vec6 eps = B * elementGlobalDisp - epsTh;
        out.push_back(packSolidStress(D * eps));
    }
    return out;
}

} // namespace omniframe::kernel
