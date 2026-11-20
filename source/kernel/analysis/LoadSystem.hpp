#pragma once

#include "../core/KernelTypes.hpp"
#include "../elements/IFiniteElement.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace omniframe::kernel {

enum class LoadCaseCategory : std::uint8_t {
    Dead = 0,
    Live = 1,
    Wind = 2,
    Snow = 3,
    Thermal = 4,
    Seismic = 5,
    SoilWater = 6,
    Prestress = 7,
    Custom = 8
};

enum class LimitStateType : std::uint8_t {
    ELU_Normal = 0,
    ELU_Special = 1,
    ELU_Exceptional = 2,
    ELS_Rare = 3,
    ELS_Frequent = 4,
    ELS_QuasiPermanent = 5,
    Custom = 6
};

struct NodalLoadPatternEntry {
    NodeId nodeId = -1;
    std::array<double, 6> forceMoment{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

struct ElementLoadPatternEntry {
    ElementId elementId = -1;
    ElementLoadInput load;
};

struct SelfWeightSpec {
    bool enabled = false;
    Vec3 direction{0.0, 0.0, -1.0};
    double factor = 1.0;
};

struct LoadCase {
    LoadCaseId id = 1;
    std::string name = "G1 - Permanente";
    LoadCaseCategory category = LoadCaseCategory::Dead;
    SelfWeightSpec selfWeight;
    std::vector<NodalLoadPatternEntry> nodalLoads;
    std::vector<ElementLoadPatternEntry> elementLoads;
};

struct LoadCombinationFactor {
    LoadCaseId loadCaseId = 1;
    double gamma = 1.0;
};

struct LoadCombination {
    LoadComboId id = 1;
    std::string name = "ELU-1";
    LimitStateType limitState = LimitStateType::ELU_Normal;
    bool pDeltaEnabled = false;
    std::vector<LoadCombinationFactor> factors;
};

struct LoadHistoryPoint {
    double timeOrStep = 0.0;
    double factor = 0.0;
};

struct LoadHistory {
    int32_t id = 1;
    std::string name = "Historico-1";
    LoadCaseId loadCaseId = 1;
    std::vector<LoadHistoryPoint> points;

    double evaluateAt(double t) const;
};

struct CaseDisplacementResult {
    int32_t caseOrComboId = 0;
    std::string name;
    bool isCombination = false;
    VecX U_full;
    VecX R_full;
    LinearSolverStats solverStats;
};

struct EnvelopeExtremes {
    std::unordered_map<NodeId, std::array<double, 6>> minNodeDisp;
    std::unordered_map<NodeId, std::array<double, 6>> maxNodeDisp;
    std::unordered_map<NodeId, std::array<double, 6>> minNodeReac;
    std::unordered_map<NodeId, std::array<double, 6>> maxNodeReac;
};

class LoadEnvelope {
public:
    static EnvelopeExtremes compute(const std::vector<CaseDisplacementResult>& results,
                                    const std::vector<std::pair<NodeId, std::array<int, 6>>>& nodeDofMap);
};

} // namespace omniframe::kernel
