#include "locale/GerenciadorTraducao.hpp"
#include "os/WindowsCliIntegration.hpp"
#include "render/RenderSettings.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ThemeManager.hpp"

#include <QApplication>
#include <QIcon>
#include <QStringList>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName("OmniFrame");
    QCoreApplication::setApplicationVersion("2026.11.15.0");
    QCoreApplication::setOrganizationName("OmniFrame");

    QStringList startupFiles;
    const int cliExitCode = omniframe::os::forwardGuiArgsToCliIfRequested(argc, argv, startupFiles);
    if (cliExitCode >= 0) {
        return cliExitCode;
    }

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
    window.showMaximized();
    if (!startupFiles.isEmpty()) {
        window.openProjectFileFromCommandLine(startupFiles.front());
    }

    return app.exec();
}
