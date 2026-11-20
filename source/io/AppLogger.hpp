#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

namespace omniframe::io {

/**
 * @brief Níveis suportados pelo registrador unificado de logs da aplicação.
 */
enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Critical = 4,
    Fatal = 5
};

/**
 * @brief Sistema central, thread-safe e robusto de geração, rotação diária e
 *        retenção de logs em disco no diretório `AppData/Local/OmniFrame/log`.
 *
 * Formato de arquivo diário:
 *   `omniframe_aaaa-mm-dd.log` (ex.: `omniframe_2026-11-17.log`)
 *
 * Política de retenção:
 *   Mantém no máximo `kMaxLogFiles` (10) arquivos `.log` no diretório de logs;
 *   sempre que um arquivo mais recente é criado, os arquivos mais antigos
 *   excedentes são excluídos automaticamente.
 */
class AppLogger {
public:
    static constexpr int kMaxLogFiles = 10;

    class ScopedDirectoryOverride {
    public:
        explicit ScopedDirectoryOverride(const QString& directoryPath);
        ~ScopedDirectoryOverride();
        ScopedDirectoryOverride(const ScopedDirectoryOverride&) = delete;
        ScopedDirectoryOverride& operator=(const ScopedDirectoryOverride&) = delete;

    private:
        QString m_previousOverride;
    };

    class ScopedDateOverride {
    public:
        explicit ScopedDateOverride(const QDate& date);
        ~ScopedDateOverride();
        ScopedDateOverride(const ScopedDateOverride&) = delete;
        ScopedDateOverride& operator=(const ScopedDateOverride&) = delete;

    private:
        QDate m_previousOverride;
    };

    static void initialize(bool installQtHandler = true, bool forwardToPreviousQtHandler = true);
    static void shutdown();
    static bool isInitialized();
    static void installTerminateHandler();

    static QString logDirectory();
    static QString logFileNameForDate(const QDate& date);
    static QString logFilePathForDate(const QDate& date = QDate::currentDate());
    static QString currentLogFilePath();

    /**
     * @brief Lista os arquivos `.log` existentes no diretório de logs, ordenados
     *        cronologicamente do mais recente para o mais antigo.
     */
    static QStringList existingLogFiles();

    /**
     * @brief Garante o limite máximo de arquivos de log no diretório (`maxFiles`, padrão 10),
     *        excluindo os mais antigos sempre que houver excedente.
     * @return Quantidade de arquivos antigos excluídos.
     */
    static int enforceRetentionLimit(int maxFiles = kMaxLogFiles);

    static void log(LogLevel level,
                    const QString& category,
                    const QString& message,
                    const char* file = nullptr,
                    int line = 0,
                    const char* function = nullptr);

    static void trace(const QString& category, const QString& message);
    static void debug(const QString& category, const QString& message);
    static void info(const QString& category, const QString& message);
    static void warning(const QString& category, const QString& message);
    static void critical(const QString& category, const QString& message);
    static void fatal(const QString& category, const QString& message);

    static QString levelToTag(LogLevel level);
    static QString readRecentLines(int maxLines = 300);
    static void flush();
};

} // namespace omniframe::io
