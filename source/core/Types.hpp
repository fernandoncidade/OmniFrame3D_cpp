#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <numbers>
#include <algorithm>

namespace omniframe {

using NodeId = int;
using ElementId = int;
using MaterialId = int;
using SectionId = int;

constexpr int INVALID_ID = -1;
constexpr double PI = std::numbers::pi;
constexpr double EPSILON = 1e-9;

enum class WorkPlane {
    XZ_Frontal, // Frontal elevation (X: horizontal, Z: vertical)
    XY_Top,     // Top plan view (X: horizontal, Y: lateral/depth)
    YZ_Side     // Side elevation (Y: horizontal, Z: vertical)
};

enum class InteractionMode {
    Select,
    AddNode,
    AddElement,
    Measure,
    Delete
};

enum class DiagramType {
    None,
    Deformed,
    Normal_N,
    Shear_Vy,
    Shear_Vz,
    Torsion_Tx,
    Moment_My,
    Moment_Mz
};

enum class SectionShape {
    Rectangular = 0,
    CircularSolid = 1,
    CircularTube = 2,
    IShape = 3,
    Generic = 4,
    BoxRHS = 5,
    ChannelC = 6,
    AngleL = 7,
    TShape = 8,
    WeldedIShape = 9,
    Polygon = 10,
    Composite = 11
};

enum class ElementFormulation {
    EulerBernoulli3D = 0,
    Timoshenko3D = 1,
    Truss3D = 2
};

enum class CoordSystem {
    Global,
    Local
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3() = default;
    constexpr Vec3(double px, double py, double pz) : x(px), y(py), z(pz) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }

    constexpr double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    constexpr Vec3 cross(const Vec3& o) const {
        return {
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x
        };
    }

    double normSq() const { return x * x + y * y + z * z; }
    double norm() const { return std::sqrt(normSq()); }

    Vec3 normalized() const {
        double n = norm();
        if (n < 1e-12) return {0.0, 0.0, 0.0};
        return *this / n;
    }

    double distanceTo(const Vec3& o) const {
        return (*this - o).norm();
    }
};

struct SupportRestraint {
    // Indices: 0: uX, 1: uY, 2: uZ, 3: rotX, 4: rotY, 5: rotZ
    std::array<bool, 6> fixed{false, false, false, false, false, false};
    std::array<double, 6> springStiffness{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, 6> prescribedValue{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<bool, 6> hasPrescribed{false, false, false, false, false, false};

    bool isRestrained() const {
        for (int i = 0; i < 6; ++i) {
            if (fixed[i] || springStiffness[i] > 1e-9 || hasPrescribed[i]) return true;
        }
        return false;
    }

    bool hasFixedDofs() const {
        for (int i = 0; i < 6; ++i) {
            if (fixed[i]) return true;
        }
        return false;
    }

    bool hasSprings() const {
        for (int i = 0; i < 6; ++i) {
            if (springStiffness[i] > 1e-9) return true;
        }
        return false;
    }

    bool hasPrescribedDisplacements() const {
        for (int i = 0; i < 6; ++i) {
            if (hasPrescribed[i]) return true;
        }
        return false;
    }

    bool isFixed() const {
        return fixed[0] && fixed[1] && fixed[2] && fixed[3] && fixed[4] && fixed[5];
    }

    bool isPinned() const {
        return fixed[0] && fixed[1] && fixed[2] && !fixed[3] && !fixed[4] && !fixed[5];
    }

    bool isRollerZ() const {
        return fixed[2] && !fixed[0] && !fixed[1] && !fixed[3] && !fixed[4] && !fixed[5];
    }

    static SupportRestraint makeFixed() {
        SupportRestraint s;
        s.fixed.fill(true);
        return s;
    }

    static SupportRestraint makePinned() {
        SupportRestraint s;
        s.fixed[0] = s.fixed[1] = s.fixed[2] = true;
        return s;
    }

    static SupportRestraint makeRoller(int dofAxis = 2) {
        SupportRestraint s;
        if (dofAxis >= 0 && dofAxis < 3) {
            s.fixed[dofAxis] = true;
        }
        return s;
    }
};

struct ElementReleases {
    // DOFs: 0: u, 1: v, 2: w, 3: rot_x (Tx), 4: rot_y (My), 5: rot_z (Mz)
    std::array<bool, 6> start{false, false, false, false, false, false};
    std::array<bool, 6> end{false, false, false, false, false, false};

    bool hasAnyRelease() const {
        for (int i = 0; i < 6; ++i) {
            if (start[i] || end[i]) return true;
        }
        return false;
    }

    static ElementReleases makeTrussReleases() {
        ElementReleases r;
        // Release My, Mz, and Tx at both ends
        r.start[3] = r.start[4] = r.start[5] = true;
        r.end[3] = r.end[4] = r.end[5] = true;
        return r;
    }
};

struct NodalLoad {
    Vec3 force{0.0, 0.0, 0.0};  // Fx, Fy, Fz in kN
    Vec3 moment{0.0, 0.0, 0.0}; // Mx, My, Mz in kN*m

    bool isActive() const {
        return force.normSq() > 1e-12 || moment.normSq() > 1e-12;
    }
};

struct DistributedLoad {
    bool active = false;
    CoordSystem coordSystem = CoordSystem::Global;
    Vec3 w{0.0, 0.0, 0.0}; // qx, qy, qz in kN/m
};

struct ThermalLoad {
    bool active = false;
    double deltaT = 0.0; // °C
};

struct InternalForces {
    double N = 0.0;    // Axial force (kN)
    double Vy = 0.0;   // Shear force along local y (kN)
    double Vz = 0.0;   // Shear force along local z (kN)
    double Tx = 0.0;   // Torsional moment (kN*m)
    double My = 0.0;   // Bending moment around local y (kN*m)
    double Mz = 0.0;   // Bending moment around local z (kN*m)
    Vec3 defLocal{0.0, 0.0, 0.0};  // Local displacements (m)
    Vec3 defGlobal{0.0, 0.0, 0.0}; // Global displacements (m)
};

struct ColorRGB {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    static ColorRGB jet(double val) {
        // val in [0, 1]
        if (!std::isfinite(val)) val = 0.0;
        double t = std::clamp(val, 0.0, 1.0);
        float r = static_cast<float>(std::clamp(1.5 - std::abs(4.0 * t - 3.0), 0.0, 1.0));
        float g = static_cast<float>(std::clamp(1.5 - std::abs(4.0 * t - 2.0), 0.0, 1.0));
        float b = static_cast<float>(std::clamp(1.5 - std::abs(4.0 * t - 1.0), 0.0, 1.0));
        return {r, g, b, 1.0f};
    }

    static ColorRGB jetThemed(double val, bool isLightCanvas) {
        if (!std::isfinite(val)) val = 0.0;
        if (!isLightCanvas) {
            return jet(val);
        }
        // Rampa de alto contraste para fundo claro (azul profundo -> ciano escuro -> verde -> âmbar -> carmesim)
        constexpr std::array<std::array<float, 3>, 5> stops = {{
            {0.05f, 0.25f, 0.72f},
            {0.00f, 0.48f, 0.72f},
            {0.07f, 0.54f, 0.22f},
            {0.84f, 0.44f, 0.00f},
            {0.78f, 0.08f, 0.12f}
        }};
        const double t = std::clamp(val, 0.0, 1.0);
        const double scaled = t * 4.0;
        const int idx = std::clamp(static_cast<int>(std::floor(scaled)), 0, 3);
        const float f = std::clamp(static_cast<float>(scaled - static_cast<double>(idx)), 0.0f, 1.0f);
        const auto& c0 = stops[idx];
        const auto& c1 = stops[idx + 1];
        return {
            c0[0] + (c1[0] - c0[0]) * f,
            c0[1] + (c1[1] - c0[1]) * f,
            c0[2] + (c1[2] - c0[2]) * f,
            1.0f
        };
    }

    static ColorRGB viridis(double val) {
        if (!std::isfinite(val)) val = 0.0;
        double t = std::clamp(val, 0.0, 1.0);
        // Polynomial approximation to Viridis
        float r = static_cast<float>(std::clamp(0.267 + t * (0.0048 + t * (2.82 - t * 2.09)), 0.0, 1.0));
        float g = static_cast<float>(std::clamp(0.004 + t * (1.404 - t * 0.41), 0.0, 1.0));
        float b = static_cast<float>(std::clamp(0.329 + t * (1.427 - t * (2.19 - t * 0.44)), 0.0, 1.0));
        return {r, g, b, 1.0f};
    }
};

} // namespace omniframe

