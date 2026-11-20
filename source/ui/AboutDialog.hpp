#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

class QComboBox;
class QEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTextBrowser;

namespace omniframe {

class AboutDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog() override = default;

    void retranslateUi();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onFindNext();
    void onLicenseSelectionChanged(int index);

private:
    struct DocTabSpec {
        QString relPathPt;
        QString relPathEn;
        QTextBrowser* browser = nullptr;
    };

    void setupUi();
    void applyTheme();
    void loadDocuments();
    void populateThirdPartyLicenses();
    static QString normalizeTextHtml(const QString& plainText);

    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_authorLabel = nullptr;
    QLabel* m_searchLabel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_findNextBtn = nullptr;
    QTabWidget* m_tabs = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QComboBox* m_licenseCombo = nullptr;
    QLabel* m_licenseComboLabel = nullptr;
    QTextBrowser* m_licenseBrowser = nullptr;

    QVector<DocTabSpec> m_docTabs;
};

} // namespace omniframe

