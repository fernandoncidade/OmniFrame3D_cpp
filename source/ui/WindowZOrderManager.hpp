#pragma once

#include <QFileDialog>
#include <QList>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QWidget>

#include <functional>

class QDialog;
class QDockWidget;
class QEvent;
class QWidget;

namespace omniframe {

/**
 * @brief Gerenciador central de pilha Z-Order e janelas Modeless (nao-modais).
 *
 * Mantem a ordem explicita no eixo Z entre a janela principal e todas as janelas,
 * dialogos e paineis flutuantes filhos, garantindo que qualquer janela clicada ou
 * ativada seja elevada ao topo (`raise()`) sem bloquear a janela principal.
 */
class WindowZOrderManager : public QObject {
    Q_OBJECT

public:
    explicit WindowZOrderManager(QWidget* rootWindow, QObject* parent = nullptr);
    ~WindowZOrderManager() override;

    void registerWindow(QWidget* window);
    void registerDockWidget(QDockWidget* dock);

    void prepareModelessDialog(QDialog* dlg, bool resizable = false, bool deleteOnFinished = true);
    void showAndRaise(QWidget* window);
    void bringToFront(QWidget* window, bool activateFocus = true);
    void closeAllChildWindows();

    QMessageBox* showModelessMessage(
        QMessageBox::Icon icon,
        const QString& title,
        const QString& text,
        QMessageBox::StandardButtons buttons = QMessageBox::Ok,
        std::function<void(int)> onFinished = nullptr);

    QFileDialog* showModelessFileDialog(
        QFileDialog::AcceptMode acceptMode,
        QFileDialog::FileMode fileMode,
        const QString& title,
        const QString& initialPath,
        const QString& nameFilter,
        std::function<void(const QString&)> onFileSelected);

    QList<QWidget*> zOrderStack() const;
    QWidget* topWindow() const;

signals:
    void zOrderChanged(const QWidgetList& stackBottomToTop);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* resolveManagedWindow(QWidget* widget) const;
    void pruneInvalidWindows();
    void positionDialogOverRoot(QDialog* dlg);

    QPointer<QWidget> m_rootWindow;
    QList<QPointer<QWidget>> m_zOrderStack;
    QList<QPointer<QDockWidget>> m_docks;
    bool m_updatingZOrder = false;
    int m_cascadeStep = 0;
};

} // namespace omniframe

