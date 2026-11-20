#include "UserPreferencesStore.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <QSaveFile>
#include <QStandardPaths>

namespace omniframe::io {

namespace {

QRecursiveMutex& storeMutex() {
    static QRecursiveMutex mtx;
    return mtx;
}

QString& directoryOverrideRef() {
    static QString overrideDir;
    return overrideDir;
}

struct PreferencesCacheState {
    QString dirPath;
    QJsonObject root;
    QDateTime fileMtime;
    qint64 fileSize = -1;
    bool valid = false;
    bool legacyChecked = false;
    int diskWrites = 0;
};

PreferencesCacheState& cacheState() {
    static PreferencesCacheState state;
    return state;
}

void invalidateCacheLocked() {
    PreferencesCacheState& st = cacheState();
    st.valid = false;
    st.legacyChecked = false;
    st.fileSize = -1;
    st.fileMtime = QDateTime();
    st.root = QJsonObject{};
}

QString preferencesDirectoryLocked() {
    if (!directoryOverrideRef().isEmpty()) {
        return QDir::cleanPath(directoryOverrideRef());
    }

    const QString genericLocal = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!genericLocal.isEmpty()) {
        return QDir::cleanPath(QDir(genericLocal).filePath(QStringLiteral("OmniFrame")));
    }

    const QString appLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appLocal.isEmpty()) {
        QDir appDir(appLocal);
        if (QCoreApplication::organizationName().compare(QStringLiteral("OmniFrame"), Qt::CaseInsensitive) == 0 &&
            QCoreApplication::applicationName().compare(QStringLiteral("OmniFrame"), Qt::CaseInsensitive) == 0 &&
            appDir.dirName().compare(QStringLiteral("OmniFrame"), Qt::CaseInsensitive) == 0) {
            appDir.cdUp();
            return QDir::cleanPath(appDir.absolutePath());
        }
        return QDir::cleanPath(appLocal);
    }

    return QDir::cleanPath(QDir::home().filePath(QStringLiteral(".omniframe")));
}

QJsonObject readJsonObjectFile(const QString& path,
                               bool* existsOut = nullptr,
                               bool* validOut = nullptr,
                               QDateTime* modifiedOut = nullptr) {
    const QFileInfo fi(path);
    const bool exists = fi.exists() && fi.isFile();
    if (existsOut != nullptr) {
        *existsOut = exists;
    }
    if (modifiedOut != nullptr) {
        *modifiedOut = exists ? fi.lastModified() : QDateTime();
    }
    if (!exists) {
        if (validOut != nullptr) {
            *validOut = false;
        }
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (validOut != nullptr) {
            *validOut = false;
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    const bool valid = (parseError.error == QJsonParseError::NoError && doc.isObject());
    if (validOut != nullptr) {
        *validOut = valid;
    }
    return valid ? doc.object() : QJsonObject{};
}

QJsonObject readBestLegacyJsonFile(const QDir& primaryDir,
                                   const QDir& nestedDir,
                                   const QString& fileName,
                                   bool* existsOut,
                                   bool* validOut,
                                   QDateTime* modifiedOut) {
    bool pExists = false, pValid = false;
    bool nExists = false, nValid = false;
    QDateTime pMtime, nMtime;

    QJsonObject pObj = readJsonObjectFile(primaryDir.filePath(fileName), &pExists, &pValid, &pMtime);
    QJsonObject nObj = readJsonObjectFile(nestedDir.filePath(fileName), &nExists, &nValid, &nMtime);

    if (existsOut != nullptr) {
        *existsOut = pExists || nExists;
    }

    if (pValid && nValid) {
        const bool usePrimary = !nMtime.isValid() || (pMtime.isValid() && pMtime >= nMtime);
        if (validOut != nullptr) *validOut = true;
        if (modifiedOut != nullptr) *modifiedOut = usePrimary ? pMtime : nMtime;
        return usePrimary ? pObj : nObj;
    }
    if (pValid) {
        if (validOut != nullptr) *validOut = true;
        if (modifiedOut != nullptr) *modifiedOut = pMtime;
        return pObj;
    }
    if (nValid) {
        if (validOut != nullptr) *validOut = true;
        if (modifiedOut != nullptr) *modifiedOut = nMtime;
        return nObj;
    }

    if (validOut != nullptr) *validOut = false;
    if (modifiedOut != nullptr) *modifiedOut = QDateTime();
    return {};
}

bool writeJsonAtomicLocked(const QString& path, const QJsonObject& root) {
    const QString parentDir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parentDir)) {
        return false;
    }

    QSaveFile saveFile(path);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        return false;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (saveFile.write(payload) != payload.size()) {
        saveFile.cancelWriting();
        return false;
    }
    if (!saveFile.commit()) {
        return false;
    }

    PreferencesCacheState& st = cacheState();
    ++st.diskWrites;
    const QFileInfo fi(path);
    st.dirPath = preferencesDirectoryLocked();
    st.root = root;
    st.fileMtime = fi.lastModified();
    st.fileSize = fi.exists() ? fi.size() : -1;
    st.valid = true;
    return true;
}

void backupCorruptFileIfNeededLocked(const QString& prefsPath) {
    const QFileInfo fi(prefsPath);
    if (!fi.exists() || !fi.isFile()) {
        return;
    }
    const QString backupPath = prefsPath + QStringLiteral(".corrupt.bak");
    QFile::remove(backupPath);
    QFile::copy(prefsPath, backupPath);
}

void removeLegacyFilesLocked(const QString& dirPath) {
    PreferencesCacheState& st = cacheState();
    if (st.legacyChecked && st.dirPath == dirPath) {
        return;
    }
    const QDir dir(dirPath);
    const QStringList legacyNames = UserPreferencesStore::legacyPreferenceFileNames();
    for (const QString& name : legacyNames) {
        const QString legacyPath = dir.filePath(name);
        if (QFileInfo::exists(legacyPath)) {
            QFile::remove(legacyPath);
        }
    }

    const QString nestedDirPath = dir.filePath(QStringLiteral("OmniFrame"));
    const QDir nestedDir(nestedDirPath);
    if (nestedDir.exists()) {
        for (const QString& name : legacyNames) {
            const QString p = nestedDir.filePath(name);
            if (QFileInfo::exists(p)) {
                QFile::remove(p);
            }
        }
        const QString nestedPrefs = nestedDir.filePath(QStringLiteral("user_preferences.json"));
        if (QFileInfo::exists(nestedPrefs)) {
            QFile::remove(nestedPrefs);
        }
        const QString nestedBak = nestedPrefs + QStringLiteral(".corrupt.bak");
        if (QFileInfo::exists(nestedBak)) {
            QFile::remove(nestedBak);
        }
        QDir(dirPath).rmdir(QStringLiteral("OmniFrame"));
    }
    st.dirPath = dirPath;
    st.legacyChecked = true;
}

QJsonObject loadAndMigrateLocked(bool* okOut, bool persistMigration) {
    const QString dirPath = preferencesDirectoryLocked();
    const QDir dir(dirPath);
    const QDir nestedDir(dir.filePath(QStringLiteral("OmniFrame")));
    const QString prefsPath = dir.filePath(QStringLiteral("user_preferences.json"));

    PreferencesCacheState& st = cacheState();
    if (st.dirPath != dirPath) {
        invalidateCacheLocked();
        st.dirPath = dirPath;
    }

    const QFileInfo prefsFi(prefsPath);
    if (prefsFi.exists() && prefsFi.isFile() && st.valid &&
        st.fileSize == prefsFi.size() && st.fileMtime == prefsFi.lastModified()) {
        if (okOut != nullptr) {
            *okOut = true;
        }
        return st.root;
    }

    bool prefsExists = false;
    bool prefsValid = false;
    QDateTime prefsMtime;
    QJsonObject root = readJsonObjectFile(prefsPath, &prefsExists, &prefsValid, &prefsMtime);

    const int version = prefsValid ? root.value(QStringLiteral("version")).toInt(0) : 0;
    if (prefsValid && version >= UserPreferencesStore::kCurrentSchemaVersion) {
        st.root = root;
        st.fileMtime = prefsFi.lastModified();
        st.fileSize = prefsFi.size();
        st.valid = true;
        st.legacyChecked = true;
        if (okOut != nullptr) {
            *okOut = true;
        }
        return root;
    }

    bool nestedPrefsExists = false;
    bool nestedPrefsValid = false;
    QDateTime nestedPrefsMtime;
    const QJsonObject nestedRoot = readJsonObjectFile(
        nestedDir.filePath(QStringLiteral("user_preferences.json")),
        &nestedPrefsExists,
        &nestedPrefsValid,
        &nestedPrefsMtime);

    bool migratedAny = false;
    if (!prefsValid && nestedPrefsValid) {
        root = nestedRoot;
        prefsValid = true;
        prefsMtime = nestedPrefsMtime;
        migratedAny = true;
    }

    bool gExists = false, gValid = false;
    bool lExists = false, lValid = false;
    bool tExists = false, tValid = false;
    bool rExists = false, rValid = false;
    QDateTime gMtime, lMtime, tMtime, rMtime;

    const QJsonObject gObj = readBestLegacyJsonFile(
        dir, nestedDir, QStringLiteral("graphics.json"), &gExists, &gValid, &gMtime);
    const QJsonObject lObj = readBestLegacyJsonFile(
        dir, nestedDir, QStringLiteral("language.json"), &lExists, &lValid, &lMtime);
    const QJsonObject tObj = readBestLegacyJsonFile(
        dir, nestedDir, QStringLiteral("theme.json"), &tExists, &tValid, &tMtime);
    const QJsonObject rObj = readBestLegacyJsonFile(
        dir, nestedDir, QStringLiteral("results_tables.json"), &rExists, &rValid, &rMtime);

    const bool anyLegacyExists = gExists || lExists || tExists || rExists || nestedPrefsExists;

    if (gValid && !root.value(QStringLiteral("graphics")).isObject()) {
        root.insert(QStringLiteral("graphics"), gObj);
        migratedAny = true;
    }

    if (lValid) {
        const QString legacyLang = lObj.value(QStringLiteral("language")).toString().trimmed();
        if (!legacyLang.isEmpty()) {
            QJsonObject appObj = root.value(QStringLiteral("app")).toObject();
            const bool missingLang = appObj.value(QStringLiteral("language")).toString().trimmed().isEmpty();
            const bool legacyIsNewer =
                !prefsValid || (lMtime.isValid() && prefsMtime.isValid() && lMtime > prefsMtime);
            if (missingLang || legacyIsNewer) {
                appObj.insert(QStringLiteral("language"), legacyLang);
                root.insert(QStringLiteral("app"), appObj);
                migratedAny = true;
            }
        }
    }

    if (tValid) {
        const QString legacyTheme = tObj.value(QStringLiteral("theme")).toString().trimmed().toLower();
        if (legacyTheme == QLatin1String("dark") || legacyTheme == QLatin1String("light")) {
            QJsonObject appObj = root.value(QStringLiteral("app")).toObject();
            const bool missingTheme = appObj.value(QStringLiteral("theme")).toString().trimmed().isEmpty();
            const bool legacyIsNewer =
                !prefsValid || (tMtime.isValid() && prefsMtime.isValid() && tMtime > prefsMtime);
            if (missingTheme || legacyIsNewer) {
                appObj.insert(QStringLiteral("theme"), legacyTheme);
                root.insert(QStringLiteral("app"), appObj);
                migratedAny = true;
            }
        }
    }

    if (rValid) {
        const bool missingResults = !root.value(QStringLiteral("resultsPanel")).isObject();
        const bool legacyIsNewerOrSame =
            !prefsValid || (rMtime.isValid() && prefsMtime.isValid() && rMtime >= prefsMtime);
        if (missingResults || legacyIsNewerOrSame) {
            root.insert(QStringLiteral("resultsPanel"), rObj);
            migratedAny = true;
        }
    }

    const bool hasUsableData = prefsValid || migratedAny;
    if (!hasUsableData) {
        if (okOut != nullptr) {
            *okOut = false;
        }
        return {};
    }

    root.insert(QStringLiteral("version"), UserPreferencesStore::kCurrentSchemaVersion);

    if (persistMigration &&
        (migratedAny || anyLegacyExists || (prefsValid && version < UserPreferencesStore::kCurrentSchemaVersion))) {
        if (prefsExists && !prefsValid) {
            backupCorruptFileIfNeededLocked(prefsPath);
        }
        if (writeJsonAtomicLocked(prefsPath, root)) {
            removeLegacyFilesLocked(dirPath);
        }
    }

    if (okOut != nullptr) {
        *okOut = true;
    }
    return root;
}

} // namespace

UserPreferencesStore::ScopedDirectoryOverride::ScopedDirectoryOverride(const QString& directoryPath) {
    QMutexLocker locker(&storeMutex());
    m_previousOverride = directoryOverrideRef();
    directoryOverrideRef() = directoryPath;
    invalidateCacheLocked();
}

UserPreferencesStore::ScopedDirectoryOverride::~ScopedDirectoryOverride() {
    QMutexLocker locker(&storeMutex());
    directoryOverrideRef() = m_previousOverride;
    invalidateCacheLocked();
}

QString UserPreferencesStore::preferencesDirectory() {
    QMutexLocker locker(&storeMutex());
    return preferencesDirectoryLocked();
}

QString UserPreferencesStore::userPreferencesFilePath() {
    QMutexLocker locker(&storeMutex());
    return QDir(preferencesDirectoryLocked()).filePath(QStringLiteral("user_preferences.json"));
}

QString UserPreferencesStore::legacyNestedPreferencesDirectory() {
    QMutexLocker locker(&storeMutex());
    return QDir(preferencesDirectoryLocked()).filePath(QStringLiteral("OmniFrame"));
}

QStringList UserPreferencesStore::legacyPreferenceFileNames() {
    return {
        QStringLiteral("graphics.json"),
        QStringLiteral("language.json"),
        QStringLiteral("results_tables.json"),
        QStringLiteral("theme.json")
    };
}

QStringList UserPreferencesStore::legacyPreferenceFilePaths() {
    QMutexLocker locker(&storeMutex());
    const QDir dir(preferencesDirectoryLocked());
    QStringList paths;
    const QStringList names = legacyPreferenceFileNames();
    paths.reserve(names.size());
    for (const QString& name : names) {
        paths.append(dir.filePath(name));
    }
    return paths;
}

QJsonObject UserPreferencesStore::readRootObject(bool* ok) {
    QMutexLocker locker(&storeMutex());
    return loadAndMigrateLocked(ok, /*persistMigration=*/true);
}

bool UserPreferencesStore::writeRootObject(const QJsonObject& root) {
    QMutexLocker locker(&storeMutex());
    const QString dirPath = preferencesDirectoryLocked();
    const QString prefsPath = QDir(dirPath).filePath(QStringLiteral("user_preferences.json"));

    PreferencesCacheState& st = cacheState();
    const QFileInfo fi(prefsPath);
    const bool cachedKnownValid =
        st.valid && st.dirPath == dirPath && fi.exists() &&
        st.fileSize == fi.size() && st.fileMtime == fi.lastModified();
    if (!cachedKnownValid && fi.exists() && fi.isFile()) {
        bool prefsExists = false;
        bool prefsValid = false;
        (void)readJsonObjectFile(prefsPath, &prefsExists, &prefsValid);
        if (prefsExists && !prefsValid) {
            backupCorruptFileIfNeededLocked(prefsPath);
        }
    }

    QJsonObject normalized = root;
    normalized.insert(QStringLiteral("version"), kCurrentSchemaVersion);
    if (!writeJsonAtomicLocked(prefsPath, normalized)) {
        return false;
    }

    removeLegacyFilesLocked(dirPath);
    return true;
}

QJsonObject UserPreferencesStore::readSection(const QString& sectionKey, bool* exists) {
    if (sectionKey.isEmpty()) {
        if (exists != nullptr) {
            *exists = false;
        }
        return {};
    }

    bool ok = false;
    const QJsonObject root = readRootObject(&ok);
    if (!ok) {
        if (exists != nullptr) {
            *exists = false;
        }
        return {};
    }

    const QJsonValue val = root.value(sectionKey);
    const bool hasObj = val.isObject();
    if (exists != nullptr) {
        *exists = hasObj;
    }
    return hasObj ? val.toObject() : QJsonObject{};
}

bool UserPreferencesStore::updateSection(const QString& sectionKey, const QJsonObject& sectionObj) {
    if (sectionKey.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&storeMutex());
    const QString dirPath = preferencesDirectoryLocked();
    const QString prefsPath = QDir(dirPath).filePath(QStringLiteral("user_preferences.json"));

    bool loadedOk = false;
    QJsonObject root = loadAndMigrateLocked(&loadedOk, /*persistMigration=*/false);

    if (!loadedOk) {
        bool prefsExists = false;
        bool prefsValid = false;
        (void)readJsonObjectFile(prefsPath, &prefsExists, &prefsValid);
        if (prefsExists && !prefsValid) {
            backupCorruptFileIfNeededLocked(prefsPath);
        }
    }

    root.insert(sectionKey, sectionObj);
    root.insert(QStringLiteral("version"), kCurrentSchemaVersion);
    if (!writeJsonAtomicLocked(prefsPath, root)) {
        return false;
    }

    removeLegacyFilesLocked(dirPath);
    return true;
}

bool UserPreferencesStore::updateSectionKey(const QString& sectionKey,
                                            const QString& key,
                                            const QJsonValue& value) {
    if (sectionKey.isEmpty() || key.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&storeMutex());
    const QString dirPath = preferencesDirectoryLocked();
    const QString prefsPath = QDir(dirPath).filePath(QStringLiteral("user_preferences.json"));

    bool loadedOk = false;
    QJsonObject root = loadAndMigrateLocked(&loadedOk, /*persistMigration=*/false);

    if (!loadedOk) {
        bool prefsExists = false;
        bool prefsValid = false;
        (void)readJsonObjectFile(prefsPath, &prefsExists, &prefsValid);
        if (prefsExists && !prefsValid) {
            backupCorruptFileIfNeededLocked(prefsPath);
        }
    }

    QJsonObject sectionObj = root.value(sectionKey).toObject();
    sectionObj.insert(key, value);
    root.insert(sectionKey, sectionObj);
    root.insert(QStringLiteral("version"), kCurrentSchemaVersion);

    if (!writeJsonAtomicLocked(prefsPath, root)) {
        return false;
    }

    removeLegacyFilesLocked(dirPath);
    return true;
}

bool UserPreferencesStore::hasAllSections(const QStringList& sectionKeys) {
    bool ok = false;
    const QJsonObject root = readRootObject(&ok);
    if (!ok) {
        return false;
    }
    for (const QString& key : sectionKeys) {
        if (!root.value(key).isObject()) {
            return false;
        }
    }
    return true;
}

bool UserPreferencesStore::migrateLegacyFilesIfNeeded() {
    bool ok = false;
    (void)readRootObject(&ok);
    return ok;
}

int UserPreferencesStore::diskWriteCount() {
    QMutexLocker locker(&storeMutex());
    return cacheState().diskWrites;
}

void UserPreferencesStore::resetDiskWriteCount() {
    QMutexLocker locker(&storeMutex());
    cacheState().diskWrites = 0;
}

} // namespace omniframe::io
