#pragma once

#include "core/StructuralModel.hpp"

#include <QString>
#include <memory>

class QLockFile;

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
    class ScopedSessionIsolation {
    public:
        explicit ScopedSessionIsolation(const QString& explicitSessionId = QString());
        ~ScopedSessionIsolation();
        ScopedSessionIsolation(const ScopedSessionIsolation&) = delete;
        ScopedSessionIsolation& operator=(const ScopedSessionIsolation&) = delete;

        QString sessionId() const { return m_activeSessionId; }
        bool isPrimaryOwner() const { return m_isPrimaryOwner; }

    private:
        QString m_previousSessionId;
        QString m_activeSessionId;
        std::unique_ptr<QLockFile> m_primaryLock;
        bool m_isPrimaryOwner = false;
    };

    static QString currentSessionId();
    static void setCurrentSessionId(const QString& sessionId);

    static QString recoveryFilePath(const QString& sessionId = QString());
    static QString recoveryMetaPath(const QString& sessionId = QString());

    static bool saveSnapshot(const StructuralModel& model,
                             const QString& projectFilePath = QString(),
                             SessionSourceKind sourceKind = SessionSourceKind::UnsavedUntitled,
                             const QString& sessionId = QString());
    static bool saveCleanSessionState(const StructuralModel& model,
                                      const QString& projectFilePath,
                                      SessionSourceKind sourceKind,
                                      const QString& sessionId = QString());

    static ProjectRecoveryInfo inspectSnapshot(const QString& sessionId = QString());
    static ProjectRecoveryInfo inspectPreviousSession(const QString& sessionId = QString());

    static bool loadSnapshot(StructuralModel& outModel,
                             ProjectRecoveryInfo* outInfo = nullptr,
                             const QString& sessionId = QString());
    static void clearSnapshot(const QString& sessionId = QString());
};

} // namespace omniframe::io
