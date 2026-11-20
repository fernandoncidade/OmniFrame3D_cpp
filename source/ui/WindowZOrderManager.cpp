#include "WindowZOrderManager.hpp"

#include "ThemeManager.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QLayout>
#include <QRect>
#include <QScreen>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace omniframe {

WindowZOrderManager::WindowZOrderManager(QWidget* rootWindow, QObject* parent)
    : QObject(parent),
      m_rootWindow(rootWindow) {
    registerWindow(rootWindow);
    if (qApp) {
        qApp->installEventFilter(this);
    }
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [](UiTheme) {
        ThemeManager::instance().applyAllTopLevelWindowChrome();
    });
}

WindowZOrderManager::~WindowZOrderManager() {
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

void WindowZOrderManager::pruneInvalidWindows() {
    m_zOrderStack.erase(
        std::remove_if(m_zOrderStack.begin(), m_zOrderStack.end(),
                       [](const QPointer<QWidget>& ptr) { return ptr.isNull(); }),
        m_zOrderStack.end());

    m_docks.erase(
        std::remove_if(m_docks.begin(), m_docks.end(),
                       [](const QPointer<QDockWidget>& ptr) { return ptr.isNull(); }),
        m_docks.end());
}

void WindowZOrderManager::registerWindow(QWidget* window) {
    if (!window) {
        return;
    }
    pruneInvalidWindows();

    for (const auto& existing : m_zOrderStack) {
        if (existing.data() == window) {
            return;
        }
    }

    m_zOrderStack.append(window);
    ThemeManager::instance().applyWindowChrome(window);
    connect(window, &QObject::destroyed, this, [this](QObject*) {
        pruneInvalidWindows();
    });
}

void WindowZOrderManager::registerDockWidget(QDockWidget* dock) {
    if (!dock) {
        return;
    }
    pruneInvalidWindows();

    for (const auto& existing : m_docks) {
        if (existing.data() == dock) {
            return;
        }
    }

    m_docks.append(dock);
    if (dock->isFloating()) {
        registerWindow(dock);
    }
    connect(dock, &QDockWidget::topLevelChanged, this, [this, dockPtr = QPointer<QDockWidget>(dock)](bool floating) {
        if (!dockPtr) {
            return;
        }
        if (floating) {
            registerWindow(dockPtr.data());
            bringToFront(dockPtr.data(), true);
        } else {
            for (int i = m_zOrderStack.size() - 1; i >= 0; --i) {
                if (m_zOrderStack[i].data() == dockPtr.data()) {
                    m_zOrderStack.removeAt(i);
                }
            }
            if (m_rootWindow) {
                bringToFront(m_rootWindow.data(), false);
            }
        }
    });
}

void WindowZOrderManager::prepareModelessDialog(QDialog* dlg, bool resizable, bool deleteOnFinished) {
    if (!dlg) {
        return;
    }

    Qt::WindowFlags flags = Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint;
    if (resizable) {
        flags |= Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint;
    }

    // Remove o owner Win32 para permitir que a janela principal e as janelas filhas
    // alternem livremente sua profundidade no eixo Z (sobreposicao bidirecional).
    if (dlg->parentWidget() != nullptr) {
        dlg->setParent(nullptr, flags);
    } else if (dlg->windowFlags() != flags) {
        dlg->setWindowFlags(flags);
    }

    dlg->setModal(false);
    dlg->setWindowModality(Qt::NonModal);

    const QIcon icon = locale::GerenciadorTraducao::appIcon();
    if (!icon.isNull()) {
        dlg->setWindowIcon(icon);
    }

    if (deleteOnFinished) {
        connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    }

    positionDialogOverRoot(dlg);
    registerWindow(dlg);
}

void WindowZOrderManager::positionDialogOverRoot(QDialog* dlg) {
    if (!dlg) {
        return;
    }

    dlg->ensurePolished();
    if (dlg->layout()) {
        dlg->layout()->activate();
    }
    const QSize minHint = dlg->minimumSizeHint().expandedTo(dlg->minimumSize());
    const QSize layoutHint = dlg->sizeHint().expandedTo(minHint);
    QSize targetSize = dlg->testAttribute(Qt::WA_Resized)
        ? dlg->size().expandedTo(layoutHint)
        : layoutHint;

    QRect anchorRect;
    if (m_rootWindow && m_rootWindow->isVisible()) {
        anchorRect = m_rootWindow->frameGeometry();
    } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
        anchorRect = screen->availableGeometry();
    } else {
        dlg->resize(targetSize);
        return;
    }

    const QScreen* targetScreen = QGuiApplication::screenAt(anchorRect.center());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (targetScreen) {
        const QRect avail = targetScreen->availableGeometry();
        const int maxW = std::max(minHint.width(), avail.width() - 32);
        const int maxH = std::max(minHint.height(), avail.height() - 32);
        targetSize = QSize(std::min(targetSize.width(), maxW), std::min(targetSize.height(), maxH));
    }

    if (dlg->size() != targetSize) {
        dlg->resize(targetSize);
    }

    const int offset = (m_cascadeStep % 6) * 26;
    m_cascadeStep = (m_cascadeStep + 1) % 60;

    int x = anchorRect.center().x() - targetSize.width() / 2 + offset;
    int y = anchorRect.center().y() - targetSize.height() / 2 + offset;

    if (targetScreen) {
        const QRect avail = targetScreen->availableGeometry();
        x = std::clamp(x, avail.left() + 16, std::max(avail.left() + 16, avail.right() - targetSize.width() - 16));
        y = std::clamp(y, avail.top() + 16, std::max(avail.top() + 16, avail.bottom() - targetSize.height() - 16));
    }

    dlg->move(x, y);
}

void WindowZOrderManager::showAndRaise(QWidget* window) {
    if (!window) {
        return;
    }
    registerWindow(window);

    if (window->isMinimized()) {
        window->setWindowState((window->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    }
    if (!window->isVisible()) {
        if (window != m_rootWindow.data()) {
            window->ensurePolished();
            if (window->layout()) {
                window->layout()->activate();
            }
            const QSize minHint = window->minimumSizeHint().expandedTo(window->minimumSize());
            const QSize layoutHint = window->sizeHint().expandedTo(minHint);
            if (!window->testAttribute(Qt::WA_Resized)) {
                window->resize(layoutHint);
            } else if (window->width() < minHint.width() || window->height() < minHint.height()) {
                window->resize(window->size().expandedTo(minHint));
            }
        }

        const QString platform = QGuiApplication::platformName();
        const bool prepareHiddenNativeChrome =
            (window->isWindow() &&
             window != m_rootWindow.data() &&
             !window->testAttribute(Qt::WA_DontShowOnScreen) &&
             platform != QLatin1String("offscreen") &&
             platform != QLatin1String("minimal"));

        struct CloakGuard {
            QPointer<QWidget> target;
            bool active = false;
            ~CloakGuard() {
                if (active && target) {
                    ThemeManager::instance().setWindowCloaked(target.data(), false);
                }
            }
        } cloakGuard;

        if (prepareHiddenNativeChrome) {
            // Cria o HWND ainda oculto, aplica tema/chrome, desativa zoom DWM e aplica cloak
            // para evitar janela branca/fantasma em frações de segundo antes do primeiro paint.
            (void)window->winId();
            ThemeManager::instance().applyWindowChrome(window);
            ThemeManager::instance().setWindowTransitionsDisabled(window, true);
            ThemeManager::instance().setWindowCloaked(window, true);
            cloakGuard.target = window;
            cloakGuard.active = true;
        }

        window->show();

        if (prepareHiddenNativeChrome) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::PolishRequest);
            QCoreApplication::sendPostedEvents(window, QEvent::Show);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            if (window->layout()) {
                window->layout()->activate();
            }
            window->repaint();
#ifdef Q_OS_WIN
            if (const WId wid = window->internalWinId(); wid != 0) {
                ::UpdateWindow(reinterpret_cast<HWND>(wid));
            }
#endif
            ThemeManager::instance().setWindowCloaked(window, false);
            cloakGuard.active = false;
        }
    }
    bringToFront(window, true);
}

void WindowZOrderManager::bringToFront(QWidget* window, bool activateFocus) {
    QWidget* target = resolveManagedWindow(window);
    if (!target || m_updatingZOrder) {
        return;
    }

    // Docks acoplados dentro da janela principal pertencem a camada da RootWindow.
    if (auto* dock = qobject_cast<QDockWidget*>(target)) {
        if (!dock->isFloating() && m_rootWindow) {
            target = m_rootWindow.data();
        }
    }

    m_updatingZOrder = true;
    pruneInvalidWindows();

    const bool alreadyOnTop = (!m_zOrderStack.isEmpty() && m_zOrderStack.last().data() == target);
    if (alreadyOnTop && (!activateFocus || target->isActiveWindow())) {
        m_updatingZOrder = false;
        return;
    }

    for (int i = m_zOrderStack.size() - 1; i >= 0; --i) {
        if (m_zOrderStack[i].data() == target) {
            m_zOrderStack.removeAt(i);
        }
    }
    m_zOrderStack.append(target);

    if (target->isVisible()) {
        target->raise();
        if (activateFocus && !target->isActiveWindow()) {
            target->activateWindow();
        }
    }

    m_updatingZOrder = false;
    emit zOrderChanged(zOrderStack());
}

void WindowZOrderManager::closeAllChildWindows() {
    pruneInvalidWindows();
    const QList<QPointer<QWidget>> snapshot = m_zOrderStack;
    for (const auto& ptr : snapshot) {
        if (!ptr || ptr.data() == m_rootWindow.data()) {
            continue;
        }
        if (auto* dock = qobject_cast<QDockWidget*>(ptr.data())) {
            if (!dock->isFloating()) {
                continue;
            }
        }
        ptr->close();
    }
    pruneInvalidWindows();
}

QMessageBox* WindowZOrderManager::showModelessMessage(
    QMessageBox::Icon icon,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons,
    std::function<void(int)> onFinished) {
    auto* box = new QMessageBox(icon, title, text, buttons, nullptr);
    prepareModelessDialog(box, false, true);

    if (onFinished) {
        connect(box, &QDialog::finished, this, [cb = std::move(onFinished)](int result) {
            cb(result);
        });
    }

    showAndRaise(box);
    return box;
}

QFileDialog* WindowZOrderManager::showModelessFileDialog(
    QFileDialog::AcceptMode acceptMode,
    QFileDialog::FileMode fileMode,
    const QString& title,
    const QString& initialPath,
    const QString& nameFilter,
    std::function<void(const QString&)> onFileSelected) {
    auto* dlg = new QFileDialog(nullptr, title, initialPath, nameFilter);
    dlg->setOption(QFileDialog::DontUseNativeDialog, true);
    dlg->setAcceptMode(acceptMode);
    dlg->setFileMode(fileMode);

    if (acceptMode == QFileDialog::AcceptSave && !initialPath.isEmpty()) {
        const QFileInfo fi(initialPath);
        if (!fi.fileName().isEmpty()) {
            dlg->selectFile(fi.fileName());
        }
    }

    prepareModelessDialog(dlg, true, true);

    if (onFileSelected) {
        connect(dlg, &QFileDialog::fileSelected, this, std::move(onFileSelected));
    }

    showAndRaise(dlg);
    return dlg;
}

QList<QWidget*> WindowZOrderManager::zOrderStack() const {
    QList<QWidget*> result;
    for (const auto& ptr : m_zOrderStack) {
        if (ptr) {
            result.append(ptr.data());
        }
    }
    return result;
}

QWidget* WindowZOrderManager::topWindow() const {
    for (int i = m_zOrderStack.size() - 1; i >= 0; --i) {
        if (m_zOrderStack[i]) {
            return m_zOrderStack[i].data();
        }
    }
    return nullptr;
}

QWidget* WindowZOrderManager::resolveManagedWindow(QWidget* widget) const {
    if (!widget) {
        return nullptr;
    }

    // Sobe na cadeia de ancestrais para resolver inclusive Qt::Popup / QComboBox
    // e controles internos de QDockWidget flutuantes para sua janela dona correta.
    for (QWidget* walk = widget; walk != nullptr; walk = walk->parentWidget()) {
        for (const auto& dockPtr : m_docks) {
            if (dockPtr && dockPtr.data() == walk) {
                return dockPtr->isFloating() ? static_cast<QWidget*>(dockPtr.data()) : m_rootWindow.data();
            }
        }
        for (const auto& ptr : m_zOrderStack) {
            if (ptr && ptr.data() == walk) {
                if (auto* dock = qobject_cast<QDockWidget*>(walk)) {
                    if (!dock->isFloating()) {
                        return m_rootWindow.data();
                    }
                }
                return ptr.data();
            }
        }
    }

    QWidget* top = widget->window();
    for (const auto& dockPtr : m_docks) {
        if (dockPtr && dockPtr.data() == top) {
            return dockPtr->isFloating() ? static_cast<QWidget*>(dockPtr.data()) : m_rootWindow.data();
        }
    }
    for (const auto& ptr : m_zOrderStack) {
        if (ptr && (ptr.data() == top || ptr.data() == widget)) {
            if (auto* dock = qobject_cast<QDockWidget*>(ptr.data())) {
                if (!dock->isFloating()) {
                    return m_rootWindow.data();
                }
            }
            return ptr.data();
        }
    }
    return nullptr;
}

bool WindowZOrderManager::eventFilter(QObject* watched, QEvent* event) {
    if (!watched || !event) {
        return QObject::eventFilter(watched, event);
    }

    const QEvent::Type t = event->type();
    if (t == QEvent::WinIdChange) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            if (widget->isWindow()) {
                ThemeManager::instance().applyWindowChrome(widget);
                if (widget != m_rootWindow.data()) {
                    ThemeManager::instance().setWindowTransitionsDisabled(widget, true);
                }
            }
        }
    }

    if (m_updatingZOrder) {
        return QObject::eventFilter(watched, event);
    }

    if (t == QEvent::MouseButtonPress ||
        t == QEvent::NonClientAreaMouseButtonPress ||
        t == QEvent::WindowActivate) {
        if (QApplication::activePopupWidget() != nullptr) {
            return QObject::eventFilter(watched, event);
        }
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            QWidget* topWin = widget->window();
            const Qt::WindowType topType = topWin ? topWin->windowType() : Qt::Widget;
            if (topType == Qt::Popup || topType == Qt::ToolTip || topType == Qt::SplashScreen) {
                return QObject::eventFilter(watched, event);
            }
            if (t == QEvent::WindowActivate && (!widget->isWindow() || widget != topWin)) {
                return QObject::eventFilter(watched, event);
            }
            bool insideComboBox = false;
            for (QWidget* w = widget; w != nullptr; w = w->parentWidget()) {
                if (qobject_cast<QComboBox*>(w)) {
                    insideComboBox = true;
                    break;
                }
                if (w->isWindow()) {
                    break;
                }
            }
            if (QWidget* managed = resolveManagedWindow(widget)) {
                if (insideComboBox && t == QEvent::MouseButtonPress && managed->isActiveWindow()) {
                    m_updatingZOrder = true;
                    pruneInvalidWindows();
                    const bool alreadyOnTop = (!m_zOrderStack.isEmpty() && m_zOrderStack.last().data() == managed);
                    if (!alreadyOnTop) {
                        for (int i = m_zOrderStack.size() - 1; i >= 0; --i) {
                            if (m_zOrderStack[i].data() == managed) {
                                m_zOrderStack.removeAt(i);
                            }
                        }
                        m_zOrderStack.append(managed);
                        m_updatingZOrder = false;
                        emit zOrderChanged(zOrderStack());
                    } else {
                        m_updatingZOrder = false;
                    }
                    return QObject::eventFilter(watched, event);
                }
                const bool shouldActivate = (t == QEvent::MouseButtonPress ||
                                             t == QEvent::NonClientAreaMouseButtonPress);
                bringToFront(managed, shouldActivate);
            }
        }
    }

    return QObject::eventFilter(watched, event);
}

} // namespace omniframe

