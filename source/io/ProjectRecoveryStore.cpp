#include "io/ProjectRecoveryStore.hpp"

#include "io/ProjectSerializer.hpp"
#include "io/UserPreferencesStore.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <algorithm>

namespace omniframe::io {

namespace {

bool writeRecoveryMetaJson(const StructuralModel& model,
                           const QString& projectFilePath,
                           SessionSourceKind sourceKind,
                           bool hasUnsavedChanges) {
    const QString dirPath = UserPreferencesStore::preferencesDirectory();
    if (dirPath.isEmpty()) {
        return false;
    }
    QDir().mkpath(dirPath);

    QJsonObject meta;
    meta.insert(QStringLiteral("version"), 2);
    meta.insert(QStringLiteral("sourceKind"), static_cast<int>(sourceKind));
    meta.insert(QStringLiteral("hasUnsavedChanges"), hasUnsavedChanges);
    meta.insert(QStringLiteral("savedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    meta.insert(QStringLiteral("projectFilePath"), projectFilePath);
    meta.insert(QStringLiteral("projectName"), QString::fromStdString(model.metadata().projectName).trimmed());
    meta.insert(QStringLiteral("nodeCount"), static_cast<int>(model.getNodes().size()));
    meta.insert(QStringLiteral("elementCount"), static_cast<int>(model.getElements().size()));
    meta.insert(QStringLiteral("meshNodeCount"), static_cast<int>(model.surfaceMesh().nodes.size()));
    meta.insert(QStringLiteral("meshElementCount"), static_cast<int>(model.surfaceMesh().elements.size()));

    QSaveFile metaFile(ProjectRecoveryStore::recoveryMetaPath());
    if (!metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(meta).toJson(QJsonDocument::Indented);
    if (metaFile.write(payload) != payload.size()) {
        metaFile.cancelWriting();
        return false;
    }
    return metaFile.commit();
}

ProjectRecoveryInfo parseMetaFile(bool* okOut = nullptr) {
    ProjectRecoveryInfo info;
    if (okOut) *okOut = false;

    QFile metaFile(ProjectRecoveryStore::recoveryMetaPath());
    if (!metaFile.exists() || !metaFile.open(QIODevice::ReadOnly)) {
        return info;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return info;
    }
    const QJsonObject obj = doc.object();
    const int version = obj.value(QStringLiteral("version")).toInt(1);
    info.hasUnsavedChanges = obj.value(QStringLiteral("hasUnsavedChanges")).toBool(false);
    info.savedAtUtc = obj.value(QStringLiteral("savedAtUtc")).toString();
    info.projectFilePath = obj.value(QStringLiteral("projectFilePath")).toString();
    info.projectName = obj.value(QStringLiteral("projectName")).toString();
    info.nodeCount = obj.value(QStringLiteral("nodeCount")).toInt(0);
    info.elementCount = obj.value(QStringLiteral("elementCount")).toInt(0);
    info.meshNodeCount = obj.value(QStringLiteral("meshNodeCount")).toInt(0);
    info.meshElementCount = obj.value(QStringLiteral("meshElementCount")).toInt(0);

    if (obj.contains(QStringLiteral("sourceKind"))) {
        const int sk = std::clamp(
            obj.value(QStringLiteral("sourceKind")).toInt(0),
            static_cast<int>(SessionSourceKind::Empty),
            static_cast<int>(SessionSourceKind::ExampleSpaceTruss));
        info.sourceKind = static_cast<SessionSourceKind>(sk);
    } else if (version == 1 && info.hasUnsavedChanges) {
        info.sourceKind = info.projectFilePath.isEmpty()
            ? SessionSourceKind::UnsavedUntitled
            : SessionSourceKind::SavedProject;
    }

    if (okOut) *okOut = true;
    return info;
}

} // namespace

QString ProjectRecoveryStore::recoveryFilePath() {
    return QDir(UserPreferencesStore::preferencesDirectory()).filePath(QStringLiteral("autosave_recovery.omniframe"));
}

QString ProjectRecoveryStore::recoveryMetaPath() {
    return QDir(UserPreferencesStore::preferencesDirectory()).filePath(QStringLiteral("autosave_recovery_meta.json"));
}

bool ProjectRecoveryStore::saveSnapshot(const StructuralModel& model,
                                        const QString& projectFilePath,
                                        SessionSourceKind sourceKind) {
    if (sourceKind == SessionSourceKind::Empty) {
        sourceKind = projectFilePath.trimmed().isEmpty()
            ? SessionSourceKind::UnsavedUntitled
            : SessionSourceKind::SavedProject;
    }
    const QString dirPath = UserPreferencesStore::preferencesDirectory();
    if (dirPath.isEmpty()) {
        return false;
    }
    QDir().mkpath(dirPath);

    if (!ProjectSerializer::saveToFile(recoveryFilePath(), model)) {
        return false;
    }
    return writeRecoveryMetaJson(model, projectFilePath, sourceKind, true);
}

bool ProjectRecoveryStore::saveCleanSessionState(const StructuralModel& model,
                                                 const QString& projectFilePath,
                                                 SessionSourceKind sourceKind) {
    QFile::remove(recoveryFilePath());
    if (sourceKind == SessionSourceKind::Empty || sourceKind == SessionSourceKind::UnsavedUntitled) {
        QFile::remove(recoveryMetaPath());
        return true;
    }
    if (sourceKind == SessionSourceKind::SavedProject && projectFilePath.trimmed().isEmpty()) {
        QFile::remove(recoveryMetaPath());
        return true;
    }
    return writeRecoveryMetaJson(model, projectFilePath, sourceKind, false);
}

ProjectRecoveryInfo ProjectRecoveryStore::inspectSnapshot() {
    bool parsed = false;
    ProjectRecoveryInfo info = parseMetaFile(&parsed);
    if (!parsed || !info.hasUnsavedChanges || !QFileInfo::exists(recoveryFilePath())) {
        info.available = false;
        return info;
    }
    info.available = true;
    return info;
}

ProjectRecoveryInfo ProjectRecoveryStore::inspectPreviousSession() {
    bool parsed = false;
    ProjectRecoveryInfo info = parseMetaFile(&parsed);
    if (!parsed) {
        return info;
    }

    if (info.hasUnsavedChanges) {
        info.available = QFileInfo::exists(recoveryFilePath());
        return info;
    }

    switch (info.sourceKind) {
        case SessionSourceKind::SavedProject:
            info.available = !info.projectFilePath.trimmed().isEmpty() &&
                             QFileInfo::exists(info.projectFilePath);
            break;
        case SessionSourceKind::ExampleBuilding:
        case SessionSourceKind::ExampleCantilever:
        case SessionSourceKind::ExampleSpaceTruss:
            info.available = true;
            break;
        case SessionSourceKind::Empty:
        case SessionSourceKind::UnsavedUntitled:
        default:
            info.available = false;
            break;
    }
    return info;
}

bool ProjectRecoveryStore::loadSnapshot(StructuralModel& outModel, ProjectRecoveryInfo* outInfo) {
    const ProjectRecoveryInfo info = inspectSnapshot();
    if (!info.available) {
        return false;
    }
    StructuralModel restored;
    if (!ProjectSerializer::loadFromFile(recoveryFilePath(), restored)) {
        return false;
    }
    outModel = std::move(restored);
    if (outInfo) {
        *outInfo = info;
    }
    return true;
}

void ProjectRecoveryStore::clearSnapshot() {
    QFile::remove(recoveryFilePath());
    QFile::remove(recoveryMetaPath());
}

} // namespace omniframe::io
