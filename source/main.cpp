#include "locale/GerenciadorTraducao.hpp"
#include "render/RenderSettings.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ThemeManager.hpp"

#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName("OmniFrame");
    QCoreApplication::setApplicationVersion("2026.11.14.0");
    QCoreApplication::setOrganizationName("OmniFrame");

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

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

    return app.exec();
}
