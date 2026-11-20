#include "WindowZOrderManager.hpp"

#include "ThemeManager.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QWidget>

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
    registerWindow(dock);
    connect(dock, &QDockWidget::topLevelChanged, this, [this, dockPtr = QPointer<QDockWidget>(dock)](bool floating) {
        if (!dockPtr) {
            return;
        }
        if (floating) {
            bringToFront(dockPtr.data(), true);
        } else if (m_rootWindow) {
            bringToFront(m_rootWindow.data(), false);
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
    } else {
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
    ThemeManager::instance().applyWindowChrome(dlg);
}

void WindowZOrderManager::positionDialogOverRoot(QDialog* dlg) {
    if (!dlg) {
        return;
    }

    dlg->adjustSize();
    const QSize hint = dlg->size().expandedTo(dlg->sizeHint());

    QRect anchorRect;
    if (m_rootWindow && m_rootWindow->isVisible()) {
        anchorRect = m_rootWindow->frameGeometry();
    } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
        anchorRect = screen->availableGeometry();
    } else {
        return;
    }

    const int offset = (m_cascadeStep % 6) * 26;
    m_cascadeStep = (m_cascadeStep + 1) % 60;

    int x = anchorRect.center().x() - hint.width() / 2 + offset;
    int y = anchorRect.center().y() - hint.height() / 2 + offset;

    if (QScreen* targetScreen = QGuiApplication::screenAt(anchorRect.center())) {
        const QRect avail = targetScreen->availableGeometry();
        x = std::clamp(x, avail.left() + 16, std::max(avail.left() + 16, avail.right() - hint.width() - 16));
        y = std::clamp(y, avail.top() + 16, std::max(avail.top() + 16, avail.bottom() - hint.height() - 16));
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
        window->show();
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

    // Verifica se o clique ocorreu dentro de um QDockWidget flutuante
    for (QWidget* walk = widget; walk != nullptr; walk = walk->parentWidget()) {
        for (const auto& dockPtr : m_docks) {
            if (dockPtr && dockPtr.data() == walk && dockPtr->isFloating()) {
                return dockPtr.data();
            }
        }
    }

    QWidget* top = widget->window();
    for (const auto& ptr : m_zOrderStack) {
        if (ptr && (ptr.data() == top || ptr.data() == widget)) {
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
    if (t == QEvent::Show || t == QEvent::WinIdChange) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            if (widget->isWindow()) {
                ThemeManager::instance().applyWindowChrome(widget);
            }
        }
    }

    if (m_updatingZOrder) {
        return QObject::eventFilter(watched, event);
    }

    if (t == QEvent::MouseButtonPress ||
        t == QEvent::NonClientAreaMouseButtonPress ||
        t == QEvent::WindowActivate ||
        t == QEvent::Show) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            if (QWidget* managed = resolveManagedWindow(widget)) {
                const bool shouldActivate = (t == QEvent::MouseButtonPress ||
                                             t == QEvent::NonClientAreaMouseButtonPress);
                bringToFront(managed, shouldActivate);
            }
        }
    }

    return QObject::eventFilter(watched, event);
}

} // namespace omniframe

