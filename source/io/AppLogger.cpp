#include "AppLogger.hpp"

#include "UserPreferencesStore.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QTimeZone>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace omniframe::io {

namespace {

std::atomic<int> g_emergencyLogFd{-1};
std::atomic<std::terminate_handler> g_previousTerminateHandler{nullptr};

void writeEmergencyBytes(int fd, const char* data, std::size_t len) noexcept {
    if (fd < 0 || data == nullptr || len == 0) return;
#ifdef _WIN32
    (void)_write(fd, data, static_cast<unsigned int>(len));
#else
    (void)::write(fd, data, len);
#endif
}

struct LoggerState {
    bool initialized = false;
    bool qtHandlerInstalled = false;
    bool forwardToPrevious = true;
    QtMessageHandler previousQtHandler = nullptr;
    std::unique_ptr<QFile> activeFile;
    QString activeDir;
    QDate activeDate;
    QString activeFilePath;
    QString dirOverride;
    QDate dateOverride;
    std::terminate_handler previousTerminateHandler = nullptr;
    bool terminateHandlerInstalled = false;
};

QRecursiveMutex& logMutex() {
    static QRecursiveMutex mtx;
    return mtx;
}

LoggerState& loggerState() {
    static LoggerState state;
    return state;
}

QDate effectiveDateLocked() {
    const LoggerState& st = loggerState();
    if (st.dateOverride.isValid()) {
        return st.dateOverride;
    }
    return QDate::currentDate();
}

QString resolveLogDirectoryLocked() {
    const LoggerState& st = loggerState();
    if (!st.dirOverride.trimmed().isEmpty()) {
        return QDir::cleanPath(st.dirOverride);
    }
    return QDir::cleanPath(QDir(UserPreferencesStore::preferencesDirectory()).filePath(QStringLiteral("log")));
}

QDate extractDateFromLogFileName(const QString& fileName) {
    static const QRegularExpression rxDateLog(
        QStringLiteral("^omniframe_(\\d{4}-\\d{2}-\\d{2})\\.log$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = rxDateLog.match(fileName);
    if (!match.hasMatch()) {
        return {};
    }
    return QDate::fromString(match.captured(1), Qt::ISODate);
}

struct LogFileEntry {
    QFileInfo info;
    QDate parsedDate;
    qint64 effectiveSortMs = 0;
    qint64 modifiedMs = 0;
};

std::vector<LogFileEntry> collectSortedLogEntriesLocked(const QString& dirPath, bool newestFirst) {
    const QDir dir(dirPath);
    if (!dir.exists()) {
        return {};
    }
    const QFileInfoList rawFiles = dir.entryInfoList(
        QStringList{QStringLiteral("*.log")},
        QDir::Files | QDir::NoSymLinks);

    std::vector<LogFileEntry> entries;
    entries.reserve(static_cast<std::size_t>(rawFiles.size()));
    for (const QFileInfo& fi : rawFiles) {
        LogFileEntry e;
        e.info = fi;
        e.parsedDate = extractDateFromLogFileName(fi.fileName());
        e.modifiedMs = fi.lastModified().toMSecsSinceEpoch();
        if (e.parsedDate.isValid()) {
            e.effectiveSortMs = QDateTime(e.parsedDate, QTime(0, 0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
        } else {
            e.effectiveSortMs = e.modifiedMs;
        }
        entries.push_back(std::move(e));
    }

    std::sort(entries.begin(), entries.end(), [newestFirst](const LogFileEntry& a, const LogFileEntry& b) {
        if (a.effectiveSortMs != b.effectiveSortMs) {
            return newestFirst ? (a.effectiveSortMs > b.effectiveSortMs) : (a.effectiveSortMs < b.effectiveSortMs);
        }
        if (a.modifiedMs != b.modifiedMs) {
            return newestFirst ? (a.modifiedMs > b.modifiedMs) : (a.modifiedMs < b.modifiedMs);
        }
        return newestFirst
            ? (a.info.fileName() > b.info.fileName())
            : (a.info.fileName() < b.info.fileName());
    });
    return entries;
}

int enforceRetentionLimitLocked(const QString& dirPath, int maxFiles, const QString& protectedFilePath) {
    if (maxFiles < 0) {
        return 0;
    }
    std::vector<LogFileEntry> oldestFirst = collectSortedLogEntriesLocked(dirPath, false);
    if (static_cast<int>(oldestFirst.size()) <= maxFiles) {
        return 0;
    }

    const QString cleanedProtected = protectedFilePath.isEmpty() ? QString() : QDir::cleanPath(protectedFilePath);
    int removed = 0;
    int remaining = static_cast<int>(oldestFirst.size());

    for (const LogFileEntry& entry : oldestFirst) {
        if (remaining <= maxFiles) {
            break;
        }
        const QString absPath = QDir::cleanPath(entry.info.absoluteFilePath());
        if (!cleanedProtected.isEmpty() &&
            QString::compare(absPath, cleanedProtected, Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (QFile::remove(entry.info.absoluteFilePath())) {
            ++removed;
            --remaining;
        }
    }

    // Caso o arquivo protegido fosse o único excedente com maxFiles == 0
    if (remaining > maxFiles && maxFiles == 0 && !cleanedProtected.isEmpty()) {
        if (QFile::remove(cleanedProtected)) {
            ++removed;
        }
    }
    return removed;
}

bool writeRawLineLocked(QFile* file, const QString& line) {
    if (file == nullptr || !file->isOpen()) {
        return false;
    }
    file->seek(file->size());
    QByteArray utf8 = line.toUtf8();
    utf8.append('\n');
    file->write(utf8);
    file->flush();
    return file->error() == QFileDevice::NoError;
}

bool ensureActiveFileOpenLocked() {
    LoggerState& st = loggerState();
    const QString targetDir = resolveLogDirectoryLocked();
    const QDate targetDate = effectiveDateLocked();
    const QString targetPath = QDir::cleanPath(
        QDir(targetDir).filePath(AppLogger::logFileNameForDate(targetDate)));

    const bool needsReopen = !st.activeFile ||
                             !st.activeFile->isOpen() ||
                             st.activeFile->error() != QFileDevice::NoError ||
                             st.activeDate != targetDate ||
                             QString::compare(QDir::cleanPath(st.activeDir), targetDir, Qt::CaseInsensitive) != 0 ||
                             QString::compare(QDir::cleanPath(st.activeFilePath), targetPath, Qt::CaseInsensitive) != 0;

    if (!needsReopen) {
        return true;
    }

    g_emergencyLogFd.store(-1, std::memory_order_release);
    if (st.activeFile) {
        if (st.activeFile->isOpen()) {
            st.activeFile->flush();
            st.activeFile->close();
        }
        st.activeFile.reset();
    }

    if (!QDir().mkpath(targetDir)) {
        return false;
    }

    const bool isBrandNewFile = !QFileInfo::exists(targetPath);
    auto file = std::make_unique<QFile>(targetPath);
    if (!file->open(QIODevice::ReadWrite | QIODevice::Append)) {
        if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
            return false;
        }
    }

    st.activeFile = std::move(file);
    g_emergencyLogFd.store(st.activeFile->handle(), std::memory_order_release);
    st.activeDir = targetDir;
    st.activeDate = targetDate;
    st.activeFilePath = targetPath;

    const int pruned = enforceRetentionLimitLocked(targetDir, AppLogger::kMaxLogFiles, targetPath);

    if (isBrandNewFile) {
        const QString appVer = QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("2026.11.17.0")
            : QCoreApplication::applicationVersion();
        const QString appName = QCoreApplication::applicationName().isEmpty()
            ? QStringLiteral("OmniFrame")
            : QCoreApplication::applicationName();
        const QString banner = QStringLiteral("[%1] [INFO    ] [Logger] === %2 v%3 | Log diario %4 iniciado ===")
                                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                        appName,
                                        appVer,
                                        targetDate.toString(Qt::ISODate));
        writeRawLineLocked(st.activeFile.get(), banner);
        if (pruned > 0) {
            const QString pruneNote = QStringLiteral("[%1] [INFO    ] [Logger] Retencao aplicada: %2 arquivo(s) de log antigo(s) removido(s) (limite=%3).")
                                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                                          .arg(pruned)
                                          .arg(AppLogger::kMaxLogFiles);
            writeRawLineLocked(st.activeFile.get(), pruneNote);
        }
    }

    return true;
}

QString formatLogEntry(LogLevel level,
                       const QString& category,
                       const QString& message,
                       const char* file,
                       int line,
                       const char* function) {
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString tag = AppLogger::levelToTag(level);
    const quintptr tid = reinterpret_cast<quintptr>(QThread::currentThreadId());
    const QString cat = category.trimmed().isEmpty() ? QStringLiteral("General") : category.trimmed();

    QString sanitizedMsg = message;
    sanitizedMsg.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    sanitizedMsg.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (sanitizedMsg.endsWith(QLatin1Char('\n'))) {
        sanitizedMsg.chop(1);
    }
    sanitizedMsg.replace(QLatin1Char('\n'), QStringLiteral("\n    "));

    QString out = QStringLiteral("[%1] [%2] [tid:0x%3] [%4] %5")
                      .arg(ts,
                           tag,
                           QString::number(tid, 16),
                           cat,
                           sanitizedMsg);

    if (file != nullptr && file[0] != '\0' && line > 0) {
        const QString fileBase = QFileInfo(QString::fromUtf8(file)).fileName();
        out += QStringLiteral(" (%1:%2").arg(fileBase).arg(line);
        if (function != nullptr && function[0] != '\0') {
            out += QStringLiteral(" | %1").arg(QString::fromUtf8(function));
        }
        out += QLatin1Char(')');
    }
    return out;
}

void qtMessageHandlerBridge(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    static thread_local bool t_inQtHandler = false;
    if (t_inQtHandler) {
        return;
    }
    struct ReentrancyGuard {
        bool& flag;
        ~ReentrancyGuard() { flag = false; }
    };
    t_inQtHandler = true;
    ReentrancyGuard guard{t_inQtHandler};

    LogLevel lvl = LogLevel::Info;
    switch (type) {
        case QtDebugMsg:
            lvl = LogLevel::Debug;
            break;
        case QtInfoMsg:
            lvl = LogLevel::Info;
            break;
        case QtWarningMsg:
            lvl = LogLevel::Warning;
            break;
        case QtCriticalMsg:
            lvl = LogLevel::Critical;
            break;
        case QtFatalMsg:
            lvl = LogLevel::Fatal;
            break;
    }

    const QString cat = (context.category != nullptr && context.category[0] != '\0')
        ? QStringLiteral("Qt:%1").arg(QString::fromUtf8(context.category))
        : QStringLiteral("Qt");

    QtMessageHandler forwardHandler = nullptr;
    bool shouldForward = false;
    {
        QMutexLocker locker(&logMutex());
        if (ensureActiveFileOpenLocked()) {
            writeRawLineLocked(loggerState().activeFile.get(),
                               formatLogEntry(lvl, cat, msg, context.file, context.line, context.function));
        }
        shouldForward = loggerState().forwardToPrevious;
        forwardHandler = loggerState().previousQtHandler;
    }

    if (shouldForward && forwardHandler != nullptr && forwardHandler != &qtMessageHandlerBridge) {
        forwardHandler(type, context, msg);
    }

    if (type == QtFatalMsg) {
        AppLogger::flush();
        if (!shouldForward || forwardHandler == nullptr) {
            std::abort();
        }
    }
}

[[noreturn]] void terminateHandlerBridge() {
    static constexpr char kPrefix[] = "[FATAL   ] [Runtime] Terminacao anormal (std::terminate): ";
    static constexpr char kUnknown[] = "Excecao C++ desconhecida ou terminate direto\n";
    char buf[512];
    std::size_t pos = 0;
    auto appendStr = [&](const char* s) noexcept {
        if (!s) return;
        while (*s != '\0' && pos + 2 < sizeof(buf)) {
            buf[pos++] = *s++;
        }
    };

    appendStr(kPrefix);
    try {
        if (const std::exception_ptr eptr = std::current_exception()) {
            std::rethrow_exception(eptr);
        }
        appendStr(kUnknown);
    } catch (const std::exception& ex) {
        appendStr(ex.what());
        buf[pos++] = '\n';
    } catch (...) {
        appendStr(kUnknown);
    }
    if (pos == 0 || buf[pos - 1] != '\n') {
        buf[pos++] = '\n';
    }

    writeEmergencyBytes(2, buf, pos);
    const int logFd = g_emergencyLogFd.load(std::memory_order_acquire);
    if (logFd >= 0) {
        writeEmergencyBytes(logFd, buf, pos);
    }

    const std::terminate_handler prev = g_previousTerminateHandler.load(std::memory_order_acquire);
    if (prev != nullptr && prev != &terminateHandlerBridge) {
        prev();
    }
    std::abort();
}

} // namespace

AppLogger::ScopedDirectoryOverride::ScopedDirectoryOverride(const QString& directoryPath) {
    QMutexLocker locker(&logMutex());
    m_previousOverride = loggerState().dirOverride;
    loggerState().dirOverride = directoryPath.trimmed().isEmpty()
        ? QString()
        : QDir::cleanPath(directoryPath);
    g_emergencyLogFd.store(-1, std::memory_order_release);
    if (loggerState().activeFile) {
        loggerState().activeFile->flush();
        loggerState().activeFile->close();
        loggerState().activeFile.reset();
    }
    loggerState().activeDir.clear();
    loggerState().activeFilePath.clear();
}

AppLogger::ScopedDirectoryOverride::~ScopedDirectoryOverride() {
    QMutexLocker locker(&logMutex());
    g_emergencyLogFd.store(-1, std::memory_order_release);
    if (loggerState().activeFile) {
        loggerState().activeFile->flush();
        loggerState().activeFile->close();
        loggerState().activeFile.reset();
    }
    loggerState().dirOverride = m_previousOverride;
    loggerState().activeDir.clear();
    loggerState().activeFilePath.clear();
}

AppLogger::ScopedDateOverride::ScopedDateOverride(const QDate& date) {
    QMutexLocker locker(&logMutex());
    m_previousOverride = loggerState().dateOverride;
    loggerState().dateOverride = date;
}

AppLogger::ScopedDateOverride::~ScopedDateOverride() {
    QMutexLocker locker(&logMutex());
    g_emergencyLogFd.store(-1, std::memory_order_release);
    if (loggerState().activeFile) {
        loggerState().activeFile->flush();
        loggerState().activeFile->close();
        loggerState().activeFile.reset();
    }
    loggerState().dateOverride = m_previousOverride;
    loggerState().activeDate = QDate();
    loggerState().activeFilePath.clear();
}

void AppLogger::initialize(bool installQtHandler, bool forwardToPreviousQtHandler) {
    QMutexLocker locker(&logMutex());
    LoggerState& st = loggerState();
    st.forwardToPrevious = forwardToPreviousQtHandler;
    st.initialized = true;
    ensureActiveFileOpenLocked();

    if (installQtHandler && !st.qtHandlerInstalled) {
        st.previousQtHandler = qInstallMessageHandler(&qtMessageHandlerBridge);
        st.qtHandlerInstalled = true;
    }
}

void AppLogger::shutdown() {
    QMutexLocker locker(&logMutex());
    LoggerState& st = loggerState();
    if (st.qtHandlerInstalled) {
        qInstallMessageHandler(st.previousQtHandler);
        st.previousQtHandler = nullptr;
        st.qtHandlerInstalled = false;
    }
    if (st.terminateHandlerInstalled) {
        std::set_terminate(st.previousTerminateHandler);
        st.previousTerminateHandler = nullptr;
        st.terminateHandlerInstalled = false;
        g_previousTerminateHandler.store(nullptr, std::memory_order_release);
    }
    g_emergencyLogFd.store(-1, std::memory_order_release);
    if (st.activeFile) {
        if (st.activeFile->isOpen()) {
            st.activeFile->flush();
            st.activeFile->close();
        }
        st.activeFile.reset();
    }
    st.activeDir.clear();
    st.activeDate = QDate();
    st.activeFilePath.clear();
    st.initialized = false;
}

bool AppLogger::isInitialized() {
    QMutexLocker locker(&logMutex());
    return loggerState().initialized;
}

void AppLogger::installTerminateHandler() {
    QMutexLocker locker(&logMutex());
    LoggerState& st = loggerState();
    if (!st.terminateHandlerInstalled) {
        st.previousTerminateHandler = std::set_terminate(&terminateHandlerBridge);
        g_previousTerminateHandler.store(st.previousTerminateHandler, std::memory_order_release);
        st.terminateHandlerInstalled = true;
    }
}

QString AppLogger::logDirectory() {
    QMutexLocker locker(&logMutex());
    return resolveLogDirectoryLocked();
}

QString AppLogger::logFileNameForDate(const QDate& date) {
    const QDate effective = date.isValid() ? date : QDate::currentDate();
    return QStringLiteral("omniframe_%1.log").arg(effective.toString(Qt::ISODate));
}

QString AppLogger::logFilePathForDate(const QDate& date) {
    QMutexLocker locker(&logMutex());
    return QDir::cleanPath(QDir(resolveLogDirectoryLocked()).filePath(logFileNameForDate(date)));
}

QString AppLogger::currentLogFilePath() {
    QMutexLocker locker(&logMutex());
    return QDir::cleanPath(
        QDir(resolveLogDirectoryLocked()).filePath(logFileNameForDate(effectiveDateLocked())));
}

QStringList AppLogger::existingLogFiles() {
    QMutexLocker locker(&logMutex());
    const QString dirPath = resolveLogDirectoryLocked();
    const std::vector<LogFileEntry> newestFirst = collectSortedLogEntriesLocked(dirPath, true);
    QStringList out;
    out.reserve(static_cast<qsizetype>(newestFirst.size()));
    for (const LogFileEntry& entry : newestFirst) {
        out.append(QDir::cleanPath(entry.info.absoluteFilePath()));
    }
    return out;
}

int AppLogger::enforceRetentionLimit(int maxFiles) {
    QMutexLocker locker(&logMutex());
    const QString dirPath = resolveLogDirectoryLocked();
    const QString protectedPath = loggerState().activeFilePath;
    return enforceRetentionLimitLocked(dirPath, maxFiles, protectedPath);
}

void AppLogger::log(LogLevel level,
                    const QString& category,
                    const QString& message,
                    const char* file,
                    int line,
                    const char* function) {
    QMutexLocker locker(&logMutex());
    if (!ensureActiveFileOpenLocked()) {
        return;
    }
    const QString entry = formatLogEntry(level, category, message, file, line, function);
    if (!writeRawLineLocked(loggerState().activeFile.get(), entry)) {
        g_emergencyLogFd.store(-1, std::memory_order_release);
        loggerState().activeFile.reset();
        if (ensureActiveFileOpenLocked()) {
            (void)writeRawLineLocked(loggerState().activeFile.get(), entry);
        }
    }
}

void AppLogger::trace(const QString& category, const QString& message) {
    log(LogLevel::Trace, category, message);
}

void AppLogger::debug(const QString& category, const QString& message) {
    log(LogLevel::Debug, category, message);
}

void AppLogger::info(const QString& category, const QString& message) {
    log(LogLevel::Info, category, message);
}

void AppLogger::warning(const QString& category, const QString& message) {
    log(LogLevel::Warning, category, message);
}

void AppLogger::critical(const QString& category, const QString& message) {
    log(LogLevel::Critical, category, message);
}

void AppLogger::fatal(const QString& category, const QString& message) {
    log(LogLevel::Fatal, category, message);
}

QString AppLogger::levelToTag(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return QStringLiteral("TRACE   ");
        case LogLevel::Debug:
            return QStringLiteral("DEBUG   ");
        case LogLevel::Info:
            return QStringLiteral("INFO    ");
        case LogLevel::Warning:
            return QStringLiteral("WARNING ");
        case LogLevel::Critical:
            return QStringLiteral("CRITICAL");
        case LogLevel::Fatal:
            return QStringLiteral("FATAL   ");
        default:
            return QStringLiteral("INFO    ");
    }
}

QString AppLogger::readRecentLines(int maxLines) {
    if (maxLines <= 0) {
        return {};
    }

    QMutexLocker locker(&logMutex());
    ensureActiveFileOpenLocked();

    QFile localFile;
    QFile* fPtr = nullptr;
    if (loggerState().activeFile && loggerState().activeFile->isOpen() &&
        (loggerState().activeFile->openMode() & QIODevice::ReadOnly)) {
        loggerState().activeFile->flush();
        fPtr = loggerState().activeFile.get();
    } else {
        if (loggerState().activeFile && loggerState().activeFile->isOpen()) {
            loggerState().activeFile->flush();
        }
        const QString targetPath = QDir::cleanPath(
            QDir(resolveLogDirectoryLocked()).filePath(logFileNameForDate(effectiveDateLocked())));
        localFile.setFileName(targetPath);
        if (!localFile.exists() || !localFile.open(QIODevice::ReadOnly)) {
            return {};
        }
        fPtr = &localFile;
    }

    const qint64 fileSize = fPtr->size();
    if (fileSize <= 0) {
        return {};
    }

    constexpr qint64 kChunkSize = 64LL * 1024LL;
    qint64 cursor = fileSize;
    qint64 startOffset = 0;
    int newlineCount = 0;
    bool foundStart = false;

    while (cursor > 0 && !foundStart) {
        const qint64 readStart = std::max<qint64>(0, cursor - kChunkSize);
        const qint64 toRead = cursor - readStart;
        if (!fPtr->seek(readStart)) {
            break;
        }
        const QByteArray chunk = fPtr->read(toRead);
        if (chunk.isEmpty()) {
            break;
        }
        for (qsizetype i = chunk.size() - 1; i >= 0; --i) {
            const qint64 absPos = readStart + static_cast<qint64>(i);
            if (chunk[i] == '\n') {
                if (absPos + 1 == fileSize) {
                    continue; // Ignora '\n' final do arquivo
                }
                ++newlineCount;
                if (newlineCount >= maxLines) {
                    startOffset = absPos + 1;
                    foundStart = true;
                    break;
                }
            }
        }
        cursor = readStart;
    }

    if (!fPtr->seek(startOffset)) {
        fPtr->seek(fileSize);
        return {};
    }
    QByteArray tailBytes = fPtr->read(fileSize - startOffset);
    fPtr->seek(fileSize);
    tailBytes.replace("\r\n", "\n");
    tailBytes.replace('\r', '\n');
    while (!tailBytes.isEmpty() && tailBytes.endsWith('\n')) {
        tailBytes.chop(1);
    }
    if (tailBytes.isEmpty()) {
        return {};
    }

    QStringList lines = QString::fromUtf8(tailBytes).split(QLatin1Char('\n'));
    if (lines.size() > maxLines) {
        lines = lines.mid(lines.size() - maxLines);
    }
    return lines.join(QLatin1Char('\n'));
}

void AppLogger::flush() {
    QMutexLocker locker(&logMutex());
    if (loggerState().activeFile && loggerState().activeFile->isOpen()) {
        loggerState().activeFile->flush();
    }
}

} // namespace omniframe::io
