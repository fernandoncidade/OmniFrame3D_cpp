#include "GerenciadorTraducao.hpp"
#include "io/UserPreferencesStore.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QWidget>
#include <memory>
#include <utility>

namespace omniframe::locale
{

    namespace
    {
        QCoreApplication *g_hookedTranslationApp = nullptr;

        std::unique_ptr<GerenciadorTraducao> &translationInstanceStorage()
        {
            static std::unique_ptr<GerenciadorTraducao> instancePtr;
            return instancePtr;
        }

        void cleanupTranslationManagerAtAppExit()
        {
            translationInstanceStorage().reset();
            g_hookedTranslationApp = nullptr;
        }

        void ensureTranslationAppLifecycleHook()
        {
            if (auto *app = QCoreApplication::instance(); app != nullptr && app != g_hookedTranslationApp)
            {
                g_hookedTranslationApp = app;
                qAddPostRoutine(&cleanupTranslationManagerAtAppExit);
                QObject::connect(app, &QObject::destroyed, []() {
                    cleanupTranslationManagerAtAppExit();
                });
            }
        }

        void dispatchLanguageChangeToWidgets()
        {
            const QWidgetList topLevels = QApplication::topLevelWidgets();
            for (QWidget *w : topLevels)
            {
                if (w)
                {
                    QCoreApplication::postEvent(w, new QEvent(QEvent::LanguageChange));
                }
            }
        }

        bool isValidAssetsDir(const QDir &dir)
        {
            return dir.exists() &&
                   dir.exists(QStringLiteral("ABOUT")) &&
                   dir.exists(QStringLiteral("EULA")) &&
                   dir.exists(QStringLiteral("LICENSES"));
        }

        QString cleanCandidatePath(const QString &path)
        {
            if (path.startsWith(QLatin1String(":/")))
            {
                return path;
            }
            return QDir::cleanPath(path);
        }

    } // namespace

    GerenciadorTraducao &GerenciadorTraducao::instance()
    {
        ensureTranslationAppLifecycleHook();
        auto &storage = translationInstanceStorage();
        if (!storage)
        {
            storage.reset(new GerenciadorTraducao(nullptr));
        }
        return *storage;
    }

    void GerenciadorTraducao::shutdown()
    {
        cleanupTranslationManagerAtAppExit();
    }

    GerenciadorTraducao::GerenciadorTraducao(QObject *parent)
        : QObject(parent)
    {
        m_idiomasDisponiveis.insert(QStringLiteral("pt_BR"), QStringLiteral("Português (Brasil)"));
        m_idiomasDisponiveis.insert(QStringLiteral("en_US"), QStringLiteral("English (United States)"));
    }

    GerenciadorTraducao::~GerenciadorTraducao()
    {
        remover_tradutores();
        qDeleteAll(m_appTranslatorsCache);
        m_appTranslatorsCache.clear();
        qDeleteAll(m_qtTranslatorsCache);
        m_qtTranslatorsCache.clear();
    }

    void GerenciadorTraducao::inicializar()
    {
        carregar_configuracao_idioma();
        aplicar_traducao();
    }

    Idioma GerenciadorTraducao::idiomaAtual() const
    {
        if (m_idiomaAtual.startsWith(QStringLiteral("en"), Qt::CaseInsensitive))
        {
            return Idioma::InglesEstadosUnidos;
        }
        return Idioma::PortuguesBrasil;
    }

    void GerenciadorTraducao::mudarIdioma(Idioma novoIdioma)
    {
        if (novoIdioma == Idioma::InglesEstadosUnidos)
        {
            definir_idioma(QStringLiteral("en_US"));
        }
        else
        {
            definir_idioma(QStringLiteral("pt_BR"));
        }
    }

    bool GerenciadorTraducao::definir_idioma(const QString &codigo_idioma)
    {
        if (!m_idiomasDisponiveis.contains(codigo_idioma))
        {
            return false;
        }

        if (m_idiomaAtual == codigo_idioma && m_tradutorApp != nullptr)
        {
            return true;
        }

        m_idiomaAtual = codigo_idioma;
        salvar_configuracao_idioma();
        const bool res = aplicar_traducao();

        emit idioma_alterado(m_idiomaAtual);
        emit idiomaAlterado(idiomaAtual());

        return res;
    }

    QString GerenciadorTraducao::trText(const char *sourceText)
    {
        return QCoreApplication::translate("InterfaceGrafica", sourceText);
    }

    QString GerenciadorTraducao::trText(const QString &sourceText)
    {
        return QCoreApplication::translate("InterfaceGrafica", sourceText.toUtf8().constData());
    }

    QString GerenciadorTraducao::assetsDirPath()
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            QDir(appDir).filePath(QStringLiteral("assets")),
            QDir(appDir).filePath(QStringLiteral("../assets")),
            QDir(appDir).filePath(QStringLiteral("../../assets"))};

        for (const QString &cand : candidates)
        {
            const QString clean = cleanCandidatePath(cand);
            if (isValidAssetsDir(QDir(clean)))
            {
                return clean;
            }
        }

        QString embeddedAssets = QStringLiteral(":/omniframe/assets");
        if (isValidAssetsDir(QDir(embeddedAssets)))
        {
            return embeddedAssets;
        }

        return cleanCandidatePath(QDir(appDir).filePath(QStringLiteral("assets")));
    }

    QString GerenciadorTraducao::readAssetDocument(const QString &relativePath)
    {
        const QString assetsRoot = assetsDirPath();
        QStringList candidatePaths;
        if (!assetsRoot.isEmpty())
        {
            candidatePaths << QDir(assetsRoot).filePath(relativePath);
        }
        if (!assetsRoot.startsWith(QLatin1String(":/")))
        {
            candidatePaths << QDir(QStringLiteral(":/omniframe/assets")).filePath(relativePath);
        }

        for (const QString &fullPath : std::as_const(candidatePaths))
        {
            QFile file(fullPath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                return QString::fromUtf8(file.readAll());
            }
        }
        return trText("Documento nao encontrado: %1").arg(relativePath);
    }

    QIcon GerenciadorTraducao::appIcon()
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString assetsDir = assetsDirPath();
        const QStringList candidates = {
            QDir(assetsDir).filePath(QStringLiteral("icons/omniframe.ico")),
            QDir(assetsDir).filePath(QStringLiteral("icons/omniframe.png")),
            QDir(appDir).filePath(QStringLiteral("assets/icons/omniframe.ico")),
            QDir(appDir).filePath(QStringLiteral("assets/icons/omniframe.png")),
            QDir(appDir).filePath(QStringLiteral("icons/omniframe.ico")),
            QDir(appDir).filePath(QStringLiteral("icons/omniframe.png")),
            QDir(appDir).filePath(QStringLiteral("omniframe.ico")),
            QDir(appDir).filePath(QStringLiteral("omniframe.png")),
            QDir(appDir).filePath(QStringLiteral("../assets/icons/omniframe.ico")),
            QDir(appDir).filePath(QStringLiteral("../assets/icons/omniframe.png")),
            QStringLiteral(":/omniframe/assets/icons/omniframe.ico"),
            QStringLiteral(":/omniframe/assets/icons/omniframe.png")};

        for (const QString &cand : candidates)
        {
            if (QFileInfo::exists(cand))
            {
                QIcon icon(cand);
                if (!icon.isNull() && !icon.pixmap(32, 32).isNull())
                {
                    return icon;
                }
            }
        }
        return {};
    }

    void GerenciadorTraducao::remover_tradutores()
    {
        auto *app = QCoreApplication::instance();
        if (!app)
            return;

        if (m_tradutorApp)
        {
            app->removeTranslator(m_tradutorApp);
            m_tradutorApp = nullptr;
        }
        if (m_tradutorQt)
        {
            app->removeTranslator(m_tradutorQt);
            m_tradutorQt = nullptr;
        }
    }

    bool GerenciadorTraducao::aplicar_traducao()
    {
        auto *app = QCoreApplication::instance();
        if (!app)
            return false;

        remover_tradutores();

        const QString locale_code = QString(m_idiomaAtual).replace(QLatin1Char('_'), QLatin1Char('-'));
        QLocale::setDefault(QLocale(locale_code));

        // 1. Carregar tradutor do proprio Qt (se disponivel)
        if (m_qtTranslatorsCache.contains(m_idiomaAtual))
        {
            m_tradutorQt = m_qtTranslatorsCache.value(m_idiomaAtual);
            if (m_tradutorQt)
            {
                app->installTranslator(m_tradutorQt);
            }
        }
        else
        {
            const QString appDir = QCoreApplication::applicationDirPath();
            const QStringList qtTransDirs = {
                QDir(appDir).filePath(QStringLiteral("translations")),
                QDir(appDir).filePath(QStringLiteral("../translations")),
                QDir(appDir).filePath(QStringLiteral("../../translations")),
                QLibraryInfo::path(QLibraryInfo::TranslationsPath)};

            auto *qtTrans = new QTranslator(this);
            bool qtLoaded = false;
            for (const QString &dirPath : qtTransDirs)
            {
                if (dirPath.isEmpty() || !QDir(dirPath).exists())
                {
                    continue;
                }
                if (qtTrans->load(QLocale(locale_code), QStringLiteral("qtbase"), QStringLiteral("_"), dirPath) ||
                    qtTrans->load(QStringLiteral("qtbase_") + m_idiomaAtual, dirPath) ||
                    qtTrans->load(QStringLiteral("qt_") + m_idiomaAtual, dirPath))
                {
                    qtLoaded = true;
                    break;
                }
            }

            if (qtLoaded)
            {
                m_tradutorQt = qtTrans;
                app->installTranslator(m_tradutorQt);
                m_qtTranslatorsCache.insert(m_idiomaAtual, m_tradutorQt);
            }
            else
            {
                delete qtTrans;
                m_qtTranslatorsCache.insert(m_idiomaAtual, nullptr);
            }
        }

        // 2. Procurar arquivo de traducao da aplicacao (.qm)
        if (m_appTranslatorsCache.contains(m_idiomaAtual))
        {
            m_tradutorApp = m_appTranslatorsCache.value(m_idiomaAtual);
            if (m_tradutorApp)
            {
                app->installTranslator(m_tradutorApp);
                dispatchLanguageChangeToWidgets();
                return true;
            }
        }

        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidateDirectories = {
            QDir(appDir).filePath(QStringLiteral("translations")),
            QDir(appDir).filePath(QStringLiteral("locale/language")),
            QDir(appDir).filePath(QStringLiteral("../translations")),
            QDir(appDir).filePath(QStringLiteral("../../translations")),
            QStringLiteral(":/omniframe/translations")};

        const QStringList candidateFileNames = {
            QStringLiteral("omniframe_%1.qm").arg(m_idiomaAtual),
            QStringLiteral("omniframe_%1.qm").arg(locale_code),
            QStringLiteral("%1.qm").arg(m_idiomaAtual)};

        bool loaded = false;
        for (const QString &dirPath : candidateDirectories)
        {
            if (!QDir(dirPath).exists())
                continue;

            for (const QString &fileName : candidateFileNames)
            {
                const QString fullPath = QDir(dirPath).filePath(fileName);
                if (QFile::exists(fullPath))
                {
                    auto *newTrans = new QTranslator(this);
                    if (newTrans->load(fullPath))
                    {
                        m_tradutorApp = newTrans;
                        app->installTranslator(m_tradutorApp);
                        m_appTranslatorsCache.insert(m_idiomaAtual, m_tradutorApp);
                        loaded = true;
                        break;
                    }
                    else
                    {
                        delete newTrans;
                    }
                }
            }
            if (loaded)
                break;
        }

        dispatchLanguageChangeToWidgets();

        return loaded || (m_idiomaAtual == m_idiomaPadrao);
    }

    bool GerenciadorTraducao::reloadSavedLanguage()
    {
        bool exists = false;
        const QJsonObject appObj = io::UserPreferencesStore::readSection(QStringLiteral("app"), &exists);
        if (!exists)
        {
            return false;
        }
        const QString lang = appObj.value(QStringLiteral("language")).toString();
        if (!m_idiomasDisponiveis.contains(lang))
        {
            return false;
        }
        if (m_idiomaAtual == lang && m_tradutorApp != nullptr)
        {
            return true;
        }

        m_idiomaAtual = lang;
        const bool res = aplicar_traducao();
        emit idioma_alterado(m_idiomaAtual);
        emit idiomaAlterado(idiomaAtual());
        return res;
    }

    void GerenciadorTraducao::carregar_configuracao_idioma()
    {
        bool exists = false;
        const QJsonObject appObj = io::UserPreferencesStore::readSection(QStringLiteral("app"), &exists);
        if (exists)
        {
            const QString lang = appObj.value(QStringLiteral("language")).toString();
            if (m_idiomasDisponiveis.contains(lang))
            {
                m_idiomaAtual = lang;
                return;
            }
        }

        const QString sysLocale = QLocale::system().name();
        if (sysLocale.startsWith(QStringLiteral("pt"), Qt::CaseInsensitive))
        {
            m_idiomaAtual = QStringLiteral("pt_BR");
        }
        else
        {
            m_idiomaAtual = QStringLiteral("en_US");
        }
    }

    void GerenciadorTraducao::salvar_configuracao_idioma() const
    {
        io::UserPreferencesStore::updateSectionKey(
            QStringLiteral("app"), QStringLiteral("language"), m_idiomaAtual);
    }

} // namespace omniframe::locale
