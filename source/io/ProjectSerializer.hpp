#pragma once

#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include <QString>

namespace omniframe {

class ProjectSerializer {
public:
    static bool saveToFile(const QString& filePath, const StructuralModel& model);
    static bool loadFromFile(const QString& filePath, StructuralModel& model);

    static bool exportTechnicalReportHtml(const QString& filePath, const StructuralModel& model, const SolverResult& result);
    static bool exportTechnicalReportMarkdown(const QString& filePath, const StructuralModel& model, const SolverResult& result);
};

} // namespace omniframe

