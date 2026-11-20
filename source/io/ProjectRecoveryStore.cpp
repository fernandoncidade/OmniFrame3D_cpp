#include "io/ProjectRecoveryStore.hpp"

#include "io/ProjectSerializer.hpp"
#include "io/UserPreferencesStore.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <QSaveFile>
#include <algorithm>
#include <atomic>

namespace omniframe::io {

namespace {

QRecursiveMutex& recoveryMutex() {
    static QRecursiveMutex mtx;
    return mtx;
}

QString& currentSessionIdRef() {
    static QString sid;
    return sid;
}

QString sanitizeSessionId(const QString& raw) {
    const QString trimmed = raw.trimmed();
    QString clean;
    clean.reserve(trimmed.size());
    for (QChar ch : trimmed) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            clean.append(ch);
        } else {
            clean.append(QLatin1Char('_'));
        }
    }
    return clean;
}

QString resolveEffectiveSessionIdLocked(const QString& explicitSessionId) {
    if (!explicitSessionId.trimmed().isEmpty()) {
        return sanitizeSessionId(explicitSessionId);
    }
    return sanitizeSessionId(currentSessionIdRef());
}

QString recoveryFilePathLocked(const QString& sessionId) {
    const QString eff = resolveEffectiveSessionIdLocked(sessionId);
    const QString fileName = eff.isEmpty()
        ? QStringLiteral("autosave_recovery.omniframe")
        : QStringLiteral("autosave_recovery_%1.omniframe").arg(eff);
    return QDir(UserPreferencesStore::preferencesDirectory()).filePath(fileName);
}

QString legacyRecoveryMetaPathLocked(const QString& sessionId) {
    const QString eff = resolveEffectiveSessionIdLocked(sessionId);
    const QString fileName = eff.isEmpty()
        ? QStringLiteral("autosave_recovery_meta.json")
        : QStringLiteral("autosave_recovery_meta_%1.json").arg(eff);
    return QDir(UserPreferencesStore::preferencesDirectory()).filePath(fileName);
}

QString sessionOperationLockPathLocked(const QString& sessionId) {
    return recoveryFilePathLocked(sessionId) + QStringLiteral(".lock");
}

QJsonObject buildRecoveryMetaObjectLocked(const StructuralModel& model,
                                          const QString& projectFilePath,
                                          SessionSourceKind sourceKind,
                                          bool hasUnsavedChanges) {
    QJsonObject meta;
    meta.insert(QStringLiteral("version"), 3);
    meta.insert(QStringLiteral("sourceKind"), static_cast<int>(sourceKind));
    meta.insert(QStringLiteral("hasUnsavedChanges"), hasUnsavedChanges);
    meta.insert(QStringLiteral("savedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    meta.insert(QStringLiteral("projectFilePath"), projectFilePath);
    meta.insert(QStringLiteral("projectName"), QString::fromStdString(model.metadata().projectName).trimmed());
    meta.insert(QStringLiteral("nodeCount"), static_cast<int>(model.getNodes().size()));
    meta.insert(QStringLiteral("elementCount"), static_cast<int>(model.getElements().size()));
    meta.insert(QStringLiteral("meshNodeCount"), static_cast<int>(model.surfaceMesh().nodes.size()));
    meta.insert(QStringLiteral("meshElementCount"), static_cast<int>(model.surfaceMesh().elements.size()));
    return meta;
}

ProjectRecoveryInfo parseMetaObject(const QJsonObject& obj, bool* okOut = nullptr) {
    ProjectRecoveryInfo info;
    if (okOut) *okOut = false;

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

bool readJsonObjectBounded(const QString& path, QJsonObject& outObj) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile() || fi.size() < 0 || fi.size() > ProjectSerializer::kDefaultMaxFileSizeBytes) {
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = f.read(ProjectSerializer::kDefaultMaxFileSizeBytes + 1);
    f.close();
    if (bytes.size() > ProjectSerializer::kDefaultMaxFileSizeBytes) {
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    outObj = doc.object();
    return true;
}

bool writeRecoveryBundleAtomicLocked(const StructuralModel& model,
                                     const QString& projectFilePath,
                                     SessionSourceKind sourceKind,
                                     bool hasUnsavedChanges,
                                     bool includeModel,
                                     const QString& sessionId) {
    const QString dirPath = UserPreferencesStore::preferencesDirectory();
    if (dirPath.isEmpty()) {
        return false;
    }
    QDir().mkpath(dirPath);

    QJsonObject bundle;
    bundle.insert(QStringLiteral("format"), QStringLiteral("omniframe_recovery_bundle"));
    bundle.insert(QStringLiteral("version"), 3);
    bundle.insert(QStringLiteral("recoveryMeta"),
                  buildRecoveryMetaObjectLocked(model, projectFilePath, sourceKind, hasUnsavedChanges));
    if (includeModel) {
        bundle.insert(QStringLiteral("model"), ProjectSerializer::serializeToJsonObject(model));
    }

    QSaveFile bundleFile(recoveryFilePathLocked(sessionId));
    if (!bundleFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(bundle).toJson(QJsonDocument::Indented);
    if (bundleFile.write(payload) != payload.size()) {
        bundleFile.cancelWriting();
        return false;
    }
    if (!bundleFile.commit()) {
        return false;
    }
    QFile::remove(legacyRecoveryMetaPathLocked(sessionId));
    return true;
}

ProjectRecoveryInfo parseRecoveryStateLocked(const QString& sessionId,
                                             bool* okOut = nullptr,
                                             bool* hasEmbeddedModelOut = nullptr,
                                             QJsonObject* embeddedModelOut = nullptr) {
    if (okOut) *okOut = false;
    if (hasEmbeddedModelOut) *hasEmbeddedModelOut = false;

    QJsonObject bundleObj;
    if (readJsonObjectBounded(recoveryFilePathLocked(sessionId), bundleObj)) {
        const auto formatIt = bundleObj.constFind(QStringLiteral("format"));
        const auto metaIt = bundleObj.constFind(QStringLiteral("recoveryMeta"));
        if (formatIt != bundleObj.constEnd() &&
            formatIt->toString() == QStringLiteral("omniframe_recovery_bundle") &&
            metaIt != bundleObj.constEnd() &&
            metaIt->isObject()) {
            ProjectRecoveryInfo info = parseMetaObject(metaIt->toObject(), okOut);
            const auto modelIt = bundleObj.constFind(QStringLiteral("model"));
            if (modelIt != bundleObj.constEnd() && modelIt->isObject()) {
                if (hasEmbeddedModelOut) *hasEmbeddedModelOut = true;
                if (embeddedModelOut) *embeddedModelOut = modelIt->toObject();
            }
            return info;
        }
    }

    // Fallback retrocompativel para formato legado em 2 arquivos separados
    QJsonObject legacyMetaObj;
    if (readJsonObjectBounded(legacyRecoveryMetaPathLocked(sessionId), legacyMetaObj)) {
        ProjectRecoveryInfo info = parseMetaObject(legacyMetaObj, okOut);
        if (info.hasUnsavedChanges && QFileInfo::exists(recoveryFilePathLocked(sessionId))) {
            QJsonObject legacyModelObj;
            if (readJsonObjectBounded(recoveryFilePathLocked(sessionId), legacyModelObj)) {
                if (hasEmbeddedModelOut) *hasEmbeddedModelOut = true;
                if (embeddedModelOut) *embeddedModelOut = std::move(legacyModelObj);
            }
        }
        return info;
    }
    return {};
}

} // namespace

ProjectRecoveryStore::ScopedSessionIsolation::ScopedSessionIsolation(const QString& explicitSessionId) {
    QMutexLocker locker(&recoveryMutex());
    m_previousSessionId = currentSessionIdRef();

    if (!explicitSessionId.trimmed().isEmpty()) {
        m_activeSessionId = sanitizeSessionId(explicitSessionId);
        m_isPrimaryOwner = false;
        currentSessionIdRef() = m_activeSessionId;
        return;
    }

    const QString dirPath = UserPreferencesStore::preferencesDirectory();
    QDir().mkpath(dirPath);
    const QString primaryLockPath = QDir(dirPath).filePath(QStringLiteral("omniframe_primary_session.lock"));
    m_primaryLock = std::make_unique<QLockFile>(primaryLockPath);
    m_primaryLock->setStaleLockTime(0);
    if (m_primaryLock->tryLock(0)) {
        m_isPrimaryOwner = true;
        m_activeSessionId.clear();
    } else {
        m_isPrimaryOwner = false;
        m_primaryLock.reset();
        static std::atomic<int> s_secondaryCounter{0};
        const int seq = ++s_secondaryCounter;
        const qint64 pid = QCoreApplication::applicationPid();
        m_activeSessionId = QStringLiteral("pid_%1_inst_%2").arg(pid).arg(seq);
    }
    currentSessionIdRef() = m_activeSessionId;
}

ProjectRecoveryStore::ScopedSessionIsolation::~ScopedSessionIsolation() {
    QMutexLocker locker(&recoveryMutex());
    if (m_primaryLock && m_isPrimaryOwner) {
        m_primaryLock->unlock();
    }
    m_primaryLock.reset();
    currentSessionIdRef() = m_previousSessionId;
}

QString ProjectRecoveryStore::currentSessionId() {
    QMutexLocker locker(&recoveryMutex());
    return currentSessionIdRef();
}

void ProjectRecoveryStore::setCurrentSessionId(const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
    currentSessionIdRef() = sanitizeSessionId(sessionId);
}

QString ProjectRecoveryStore::recoveryFilePath(const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
    return recoveryFilePathLocked(sessionId);
}

QString ProjectRecoveryStore::recoveryMetaPath(const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
    return recoveryFilePathLocked(sessionId);
}

bool ProjectRecoveryStore::saveSnapshot(const StructuralModel& model,
                                        const QString& projectFilePath,
                                        SessionSourceKind sourceKind,
                                        const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
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

    QLockFile opLock(sessionOperationLockPathLocked(sessionId));
    opLock.setStaleLockTime(5000);
    (void)opLock.tryLock(1000);

    return writeRecoveryBundleAtomicLocked(model, projectFilePath, sourceKind, true, true, sessionId);
}

bool ProjectRecoveryStore::saveCleanSessionState(const StructuralModel& model,
                                                 const QString& projectFilePath,
                                                 SessionSourceKind sourceKind,
                                                 const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
    const QString dirPath = UserPreferencesStore::preferencesDirectory();
    if (!dirPath.isEmpty()) {
        QDir().mkpath(dirPath);
    }
    QLockFile opLock(sessionOperationLockPathLocked(sessionId));
    opLock.setStaleLockTime(5000);
    (void)opLock.tryLock(1000);

    if (sourceKind == SessionSourceKind::Empty || sourceKind == SessionSourceKind::UnsavedUntitled ||
        (sourceKind == SessionSourceKind::SavedProject && projectFilePath.trimmed().isEmpty())) {
        QFile::remove(recoveryFilePathLocked(sessionId));
        QFile::remove(legacyRecoveryMetaPathLocked(sessionId));
        return true;
    }
    return writeRecoveryBundleAtomicLocked(model, projectFilePath, sourceKind, false, false, sessionId);
}

ProjectRecoveryInfo ProjectRecoveryStore::inspectSnapshot(const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
    bool parsed = false;
    bool hasEmbeddedModel = false;
    ProjectRecoveryInfo info = parseRecoveryStateLocked(sessionId, &parsed, &hasEmbeddedModel);
    if (!parsed || !info.hasUnsavedChanges || !hasEmbeddedModel) {
        info.available = false;
        return info;
    }
    info.available = true;
    return info;
}

ProjectRecoveryInfo ProjectRecoveryStore::inspectPreviousSession(const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
    bool parsed = false;
    bool hasEmbeddedModel = false;
    ProjectRecoveryInfo info = parseRecoveryStateLocked(sessionId, &parsed, &hasEmbeddedModel);
    if (!parsed) {
        return info;
    }

    if (info.hasUnsavedChanges) {
        info.available = hasEmbeddedModel;
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

bool ProjectRecoveryStore::loadSnapshot(StructuralModel& outModel,
                                        ProjectRecoveryInfo* outInfo,
                                        const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
    QLockFile opLock(sessionOperationLockPathLocked(sessionId));
    opLock.setStaleLockTime(5000);
    (void)opLock.tryLock(1000);

    bool parsed = false;
    bool hasEmbeddedModel = false;
    QJsonObject modelObj;
    ProjectRecoveryInfo info = parseRecoveryStateLocked(sessionId, &parsed, &hasEmbeddedModel, &modelObj);
    if (!parsed || !info.hasUnsavedChanges || !hasEmbeddedModel) {
        return false;
    }
    info.available = true;

    StructuralModel restored;
    if (!ProjectSerializer::deserializeFromJsonObject(modelObj, restored)) {
        return false;
    }
    outModel = std::move(restored);
    if (outInfo) {
        *outInfo = info;
    }
    return true;
}

void ProjectRecoveryStore::clearSnapshot(const QString& sessionId) {
    QMutexLocker locker(&recoveryMutex());
    QFile::remove(recoveryFilePathLocked(sessionId));
    QFile::remove(legacyRecoveryMetaPathLocked(sessionId));
}

} // namespace omniframe::io
