#pragma once

#include "core/StructuralModel.hpp"

#include <QString>

namespace omniframe::io {

enum class SessionSourceKind {
    Empty = 0,
    UnsavedUntitled = 1,
    SavedProject = 2,
    ExampleBuilding = 3,
    ExampleCantilever = 4,
    ExampleSpaceTruss = 5
};

enum class StartupSessionBehavior {
    AskOnStartup = 0,
    AutoLoadPrevious = 1,
    AlwaysStartNew = 2
};

inline QString startupSessionBehaviorToKey(StartupSessionBehavior behavior) {
    switch (behavior) {
        case StartupSessionBehavior::AutoLoadPrevious:
            return QStringLiteral("auto_load");
        case StartupSessionBehavior::AlwaysStartNew:
            return QStringLiteral("new_project");
        case StartupSessionBehavior::AskOnStartup:
        default:
            return QStringLiteral("ask");
    }
}

inline StartupSessionBehavior startupSessionBehaviorFromKey(const QString& key, int fallbackInt = 0) {
    const QString norm = key.trimmed().toLower();
    if (norm == QStringLiteral("auto_load") || norm == QStringLiteral("auto") || norm == QStringLiteral("resume")) {
        return StartupSessionBehavior::AutoLoadPrevious;
    }
    if (norm == QStringLiteral("new_project") || norm == QStringLiteral("new") || norm == QStringLiteral("empty")) {
        return StartupSessionBehavior::AlwaysStartNew;
    }
    if (norm == QStringLiteral("ask") || norm == QStringLiteral("prompt") || norm == QStringLiteral("dialog")) {
        return StartupSessionBehavior::AskOnStartup;
    }
    if (fallbackInt == static_cast<int>(StartupSessionBehavior::AutoLoadPrevious)) {
        return StartupSessionBehavior::AutoLoadPrevious;
    }
    if (fallbackInt == static_cast<int>(StartupSessionBehavior::AlwaysStartNew)) {
        return StartupSessionBehavior::AlwaysStartNew;
    }
    return StartupSessionBehavior::AskOnStartup;
}

struct ProjectRecoveryInfo {
    bool available = false;
    bool hasUnsavedChanges = false;
    SessionSourceKind sourceKind = SessionSourceKind::Empty;
    QString savedAtUtc;
    QString projectFilePath;
    QString projectName;
    int nodeCount = 0;
    int elementCount = 0;
    int meshNodeCount = 0;
    int meshElementCount = 0;
};

/**
 * @brief Persistencia atomica de recuperacao e continuidade de sessao do projeto estrutural.
 */
class ProjectRecoveryStore {
public:
    static QString recoveryFilePath();
    static QString recoveryMetaPath();

    static bool saveSnapshot(const StructuralModel& model,
                             const QString& projectFilePath = QString(),
                             SessionSourceKind sourceKind = SessionSourceKind::UnsavedUntitled);
    static bool saveCleanSessionState(const StructuralModel& model,
                                      const QString& projectFilePath,
                                      SessionSourceKind sourceKind);

    static ProjectRecoveryInfo inspectSnapshot();
    static ProjectRecoveryInfo inspectPreviousSession();

    static bool loadSnapshot(StructuralModel& outModel, ProjectRecoveryInfo* outInfo = nullptr);
    static void clearSnapshot();
};

} // namespace omniframe::io
