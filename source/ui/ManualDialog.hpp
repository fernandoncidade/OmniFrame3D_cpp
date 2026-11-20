#pragma once

#include <QDialog>
#include <QString>

class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

namespace omniframe::ui {

/**
 * @brief Visualizador modeless do Manual Completo de Operação e Engenharia.
 *        Analisa dinamicamente MANUAL_pt_BR.txt ou MANUAL_en_US.txt em índice
 *        hierárquico de capítulos/subcapítulos com busca textual e tradução ao vivo.
 */
class ManualDialog : public QDialog {
    Q_OBJECT

public:
    explicit ManualDialog(QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onIndexItemClicked(QTreeWidgetItem* item, int column);
    void onFindNext();
    void onFindPrevious();
    void onToggleExpandTree();
    void onJumpToCliSection();

private:
    void setupUi();
    void applyTheme();
    void retranslateUi();
    void loadManual();
    void findText(bool backward);
    QString buildManualHtml(const QString& rawText, QTreeWidget* indexTree) const;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_indexHeaderLabel = nullptr;
    QLabel* m_searchStatusLabel = nullptr;
    QLineEdit* m_searchInput = nullptr;
    QPushButton* m_btnPrev = nullptr;
    QPushButton* m_btnNext = nullptr;
    QPushButton* m_btnJumpCli = nullptr;
    QPushButton* m_btnToggleTree = nullptr;
    QPushButton* m_btnClose = nullptr;

    QTreeWidget* m_indexTree = nullptr;
    QTextBrowser* m_manualViewer = nullptr;
    bool m_treeExpanded = true;
    bool m_applyingTheme = false;
};

} // namespace omniframe::ui

