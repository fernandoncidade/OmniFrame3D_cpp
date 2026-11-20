#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace omniframe::io {

/**
 * @brief Repositório central e atômico de preferências e configurações do usuário
 *        concentrado exclusivamente no arquivo único `user_preferences.json`.
 *
 * Responsabilidades:
 * - Resolver de forma uniforme o diretório AppData/Local/OmniFrame em
 *   executáveis principais e suítes de teste (incluindo isolamento em pacotes MSIX);
 * - Executar migração automática e segura dos arquivos legados
 *   (`graphics.json`, `language.json`, `results_tables.json`, `theme.json`), do
 *   subdiretório legado `AppData/Local/OmniFrame/OmniFrame/` e do esquema v1 para
 *   o esquema unificado v2, removendo os artefatos legados somente após
 *   confirmação de gravação atômica;
 * - Suportar atualizações parciais de seção/chave (idioma, tema, gráficos, tabelas)
 *   sem sobrescrever as demais seções;
 * - Proteger a integridade do JSON via escrita atômica (`QSaveFile`) e backup de
 *   arquivos corrompidos (`user_preferences.json.corrupt.bak`).
 */
class UserPreferencesStore {
public:
    static constexpr int kCurrentSchemaVersion = 2;

    class ScopedDirectoryOverride {
    public:
        explicit ScopedDirectoryOverride(const QString& directoryPath);
        ~ScopedDirectoryOverride();
        ScopedDirectoryOverride(const ScopedDirectoryOverride&) = delete;
        ScopedDirectoryOverride& operator=(const ScopedDirectoryOverride&) = delete;

    private:
        QString m_previousOverride;
    };

    static QString preferencesDirectory();
    static QString userPreferencesFilePath();
    static QString legacyNestedPreferencesDirectory();
    static QStringList legacyPreferenceFileNames();
    static QStringList legacyPreferenceFilePaths();

    static QJsonObject readRootObject(bool* ok = nullptr);
    static bool writeRootObject(const QJsonObject& root);

    static QJsonObject readSection(const QString& sectionKey, bool* exists = nullptr);
    static bool updateSection(const QString& sectionKey, const QJsonObject& sectionObj);
    static bool updateSectionKey(const QString& sectionKey,
                                 const QString& key,
                                 const QJsonValue& value);
    static bool hasAllSections(const QStringList& sectionKeys);

    static bool migrateLegacyFilesIfNeeded();
};

} // namespace omniframe::io
