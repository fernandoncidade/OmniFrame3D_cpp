#pragma once

#include "core/StructuralModel.hpp"
#include "solver/FemSolver3D.hpp"
#include <QJsonObject>
#include <QString>

namespace omniframe {

class ProjectSerializer {
public:
    static constexpr qint64 kDefaultMaxFileSizeBytes = 128LL * 1024LL * 1024LL;

    static QJsonObject serializeToJsonObject(const StructuralModel& model);
    static bool deserializeFromJsonObject(const QJsonObject& rootObj, StructuralModel& model);

    static bool saveToFile(const QString& filePath, const StructuralModel& model);
    static bool loadFromFile(const QString& filePath,
                             StructuralModel& model,
                             qint64 maxFileSizeBytes = kDefaultMaxFileSizeBytes);

    static bool exportTechnicalReportHtml(const QString& filePath, const StructuralModel& model, const SolverResult& result);
    static bool exportTechnicalReportMarkdown(const QString& filePath, const StructuralModel& model, const SolverResult& result);
};

} // namespace omniframe

