#include "io/AppLogger.hpp"
#include "locale/GerenciadorTraducao.hpp"
#include "os/WindowsCliIntegration.hpp"
#include "render/RenderSettings.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ThemeManager.hpp"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QStringList>
#include <QSurfaceFormat>

namespace {
struct AppLoggerScope {
    ~AppLoggerScope() {
        omniframe::io::AppLogger::info(QStringLiteral("App"), QStringLiteral("Encerrando aplicacao GUI OmniFrame 3D."));
        omniframe::io::AppLogger::shutdown();
    }
};
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName("OmniFrame");
    QCoreApplication::setApplicationVersion("2026.11.18.0");
    QCoreApplication::setOrganizationName("OmniFrame");

    QStringList startupFiles;
    const int cliExitCode = omniframe::os::forwardGuiArgsToCliIfRequested(argc, argv, startupFiles);
    if (cliExitCode >= 0) {
        return cliExitCode;
    }

    omniframe::io::AppLogger::initialize(true, true);
    omniframe::io::AppLogger::installTerminateHandler();
    AppLoggerScope loggerScope;
    omniframe::io::AppLogger::info(
        QStringLiteral("App"),
        QStringLiteral("Iniciando aplicacao GUI OmniFrame 3D | argumentos=%1").arg(argc - 1));

#ifdef Q_OS_WIN
    if (qgetenv("QT_QPA_PLATFORM").compare("offscreen", Qt::CaseInsensitive) == 0 &&
        qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
        const QByteArray winDir = qgetenv("WINDIR");
        const QString baseWin = winDir.isEmpty()
            ? QStringLiteral("C:/Windows")
            : QDir::fromNativeSeparators(QString::fromLocal8Bit(winDir));
        const QString sysFontsDir = QDir(baseWin).filePath(QStringLiteral("Fonts"));
        if (QDir(sysFontsDir).exists()) {
            qputenv("QT_QPA_FONTDIR", QDir::toNativeSeparators(sysFontsDir).toLocal8Bit());
        }
    }
#endif

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    const omniframe::RenderQualitySettings initialRenderSettings = omniframe::loadRenderSettings();
    QSurfaceFormat::setDefaultFormat(omniframe::buildPreferredSurfaceFormat(initialRenderSettings));

    QApplication app(argc, argv);

    const QIcon appIcon = omniframe::locale::GerenciadorTraducao::appIcon();
    if (!appIcon.isNull()) {
        app.setWindowIcon(appIcon);
    }

    omniframe::locale::GerenciadorTraducao::instance().inicializar();
    omniframe::ThemeManager::instance().inicializar();

    omniframe::MainWindow window;
    if (window.preferredStartMaximized()) {
        window.showMaximized();
    } else {
        window.show();
    }
    if (!startupFiles.isEmpty()) {
        omniframe::io::AppLogger::info(
            QStringLiteral("App"),
            QStringLiteral("Abrindo arquivo recebido pela linha de comando: %1").arg(startupFiles.front()));
        window.openProjectFileFromCommandLine(startupFiles.front());
    }

    const int exitCode = app.exec();
    omniframe::io::AppLogger::info(
        QStringLiteral("App"),
        QStringLiteral("Loop principal Qt encerrado com codigo=%1").arg(exitCode));
    return exitCode;
}
