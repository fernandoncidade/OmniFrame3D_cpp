#include "LoadSystem.hpp"
#include <algorithm>
#include <limits>

namespace omniframe::kernel {

double LoadHistory::evaluateAt(double t) const {
    if (points.empty()) return 0.0;
    if (points.size() == 1 || t <= points.front().timeOrStep) return points.front().factor;
    if (t >= points.back().timeOrStep) return points.back().factor;

    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& p0 = points[i];
        const auto& p1 = points[i + 1];
        if (t >= p0.timeOrStep && t <= p1.timeOrStep) {
            const double dt = p1.timeOrStep - p0.timeOrStep;
            if (dt <= 1e-15) return p1.factor;
            const double alpha = (t - p0.timeOrStep) / dt;
            return p0.factor + alpha * (p1.factor - p0.factor);
        }
    }
    return points.back().factor;
}

EnvelopeExtremes LoadEnvelope::compute(
    const std::vector<CaseDisplacementResult>& results,
    const std::vector<std::pair<NodeId, std::array<int, 6>>>& nodeDofMap)
{
    EnvelopeExtremes env;
    if (results.empty()) return env;

    for (const auto& [nid, dofs] : nodeDofMap) {
        (void)dofs;
        std::array<double, 6> initMin, initMax;
        initMin.fill(std::numeric_limits<double>::max());
        initMax.fill(std::numeric_limits<double>::lowest());
        env.minNodeDisp[nid] = initMin;
        env.maxNodeDisp[nid] = initMax;
        env.minNodeReac[nid] = initMin;
        env.maxNodeReac[nid] = initMax;
    }

    for (const auto& r : results) {
        if (!r.solverStats.success) continue;
        for (const auto& [nid, dofs] : nodeDofMap) {
            auto& uMin = env.minNodeDisp[nid];
            auto& uMax = env.maxNodeDisp[nid];
            auto& rMin = env.minNodeReac[nid];
            auto& rMax = env.maxNodeReac[nid];
            for (int d = 0; d < 6; ++d) {
                const int g = dofs[d];
                if (g >= 0 && g < r.U_full.size()) {
                    uMin[d] = std::min(uMin[d], r.U_full(g));
                    uMax[d] = std::max(uMax[d], r.U_full(g));
                }
                if (g >= 0 && g < r.R_full.size()) {
                    rMin[d] = std::min(rMin[d], r.R_full(g));
                    rMax[d] = std::max(rMax[d], r.R_full(g));
                }
            }
        }
    }
    return env;
}

} // namespace omniframe::kernel
