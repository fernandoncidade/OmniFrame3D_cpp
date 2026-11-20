#include "AboutDialog.hpp"

#include "ThemeManager.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>
#include <utility>

namespace omniframe {

namespace {
inline constexpr const char* kFallbackAppVersion = "2026.11.20.0";
}

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
    applyTheme();
    retranslateUi();
    loadDocuments();

    const QIcon icon = locale::GerenciadorTraducao::appIcon();
    if (!icon.isNull()) {
        setWindowIcon(icon);
    }
    resize(920, 680);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](UiTheme) {
        applyTheme();
        loadDocuments();
    });
    connect(&ThemeManager::instance(), &ThemeManager::uiStyleModeChanged, this, [this](UiStyleMode) {
        applyTheme();
        loadDocuments();
    });
}

void AboutDialog::setupUi() {
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(14);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(64, 64);
    m_iconLabel->setScaledContents(true);
    const QIcon icon = locale::GerenciadorTraducao::appIcon();
    if (!icon.isNull()) {
        m_iconLabel->setPixmap(icon.pixmap(64, 64));
    }
    headerLayout->addWidget(m_iconLabel);

    auto* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(3);
    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_subtitleLabel = new QLabel(this);
    m_authorLabel = new QLabel(this);
    m_authorLabel->setOpenExternalLinks(true);

    infoLayout->addWidget(m_titleLabel);
    infoLayout->addWidget(m_subtitleLabel);
    infoLayout->addWidget(m_authorLabel);
    headerLayout->addLayout(infoLayout, 1);
    mainLayout->addLayout(headerLayout);

    auto* searchLayout = new QHBoxLayout();
    m_searchLabel = new QLabel(this);
    m_searchEdit = new QLineEdit(this);
    m_findNextBtn = new QPushButton(this);
    searchLayout->addWidget(m_searchLabel);
    searchLayout->addWidget(m_searchEdit, 1);
    searchLayout->addWidget(m_findNextBtn);
    mainLayout->addLayout(searchLayout);

    m_tabs = new QTabWidget(this);
    auto createBrowserTab = [this](const QString& relPt, const QString& relEn) {
        auto* browser = new QTextBrowser(this);
        browser->setOpenExternalLinks(true);
        browser->setReadOnly(true);
        m_tabs->addTab(browser, QString());
        m_docTabs.push_back({relPt, relEn, browser});
    };

    createBrowserTab("ABOUT/ABOUT_pt_BR.txt", "ABOUT/ABOUT_en_US.txt");
    createBrowserTab("ABOUT/History_APP_pt_BR.txt", "ABOUT/History_APP_en_US.txt");
    createBrowserTab("RELEASE/RELEASE NOTES_pt_BR.txt", "RELEASE/RELEASE NOTES_en_US.txt");
    createBrowserTab("EULA/EULA_pt_BR.txt", "EULA/EULA_en_US.txt");
    createBrowserTab("CLC/CLC_pt_BR.txt", "CLC/CLC_en_US.txt");
    createBrowserTab("PRIVACY_POLICY/Privacy_Policy_pt_BR.txt", "PRIVACY_POLICY/Privacy_Policy_en_US.txt");
    createBrowserTab("COPYRIGHT/AVISO DE COPYRIGHT E MARCA REGISTRADA_pt_BR.txt", "COPYRIGHT/COPYRIGHT AND TRADEMARK NOTICE_en_US.txt");
    createBrowserTab("NOTICES/NOTICE_pt_BR.txt", "NOTICES/NOTICE_en_US.txt");

    auto* licensesContainer = new QWidget(this);
    auto* licensesLayout = new QVBoxLayout(licensesContainer);
    licensesLayout->setContentsMargins(8, 8, 8, 8);
    licensesLayout->setSpacing(8);
    auto* licBar = new QHBoxLayout();
    m_licenseComboLabel = new QLabel(licensesContainer);
    m_licenseCombo = new QComboBox(licensesContainer);
    licBar->addWidget(m_licenseComboLabel);
    licBar->addWidget(m_licenseCombo, 1);
    licensesLayout->addLayout(licBar);
    m_licenseBrowser = new QTextBrowser(licensesContainer);
    m_licenseBrowser->setOpenExternalLinks(true);
    licensesLayout->addWidget(m_licenseBrowser, 1);
    m_tabs->addTab(licensesContainer, QString());

    mainLayout->addWidget(m_tabs, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch(1);
    m_closeBtn = new QPushButton(this);
    m_closeBtn->setMinimumWidth(110);
    bottomRow->addWidget(m_closeBtn);
    mainLayout->addLayout(bottomRow);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &AboutDialog::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &AboutDialog::onFindNext);
    connect(m_findNextBtn, &QPushButton::clicked, this, &AboutDialog::onFindNext);
    connect(m_licenseCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AboutDialog::onLicenseSelectionChanged);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void AboutDialog::applyTheme() {
    if (m_applyingTheme) {
        return;
    }
    struct Guard {
        bool& flag;
        explicit Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(m_applyingTheme);

    if (ThemeManager::instance().isNativeUi()) {
        if (!styleSheet().isEmpty()) {
            setStyleSheet(QString());
        }
        if (m_subtitleLabel && !m_subtitleLabel->styleSheet().isEmpty()) {
            m_subtitleLabel->setStyleSheet(QString());
        }
        if (m_authorLabel && !m_authorLabel->styleSheet().isEmpty()) {
            m_authorLabel->setStyleSheet(QString());
        }
        const QList<QWidget*> children = findChildren<QWidget*>();
        for (QWidget* child : children) {
            if (!child) continue;
            child->setPalette(QPalette());
            child->setAttribute(Qt::WA_SetPalette, false);
            child->update();
        }
        ThemeManager::instance().applyWindowChrome(this);
        update();
        return;
    }

    if (ThemeManager::instance().isLight()) {
        setStyleSheet(QStringLiteral(
            "QDialog { background-color: #eef2f7; color: #0f172a; font-family: 'Segoe UI', sans-serif; }"
            "QLabel { color: #1e293b; }"
            "QLineEdit { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 5px 8px; }"
            "QPushButton { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 6px 14px; font-weight: 600; }"
            "QPushButton:hover { background-color: #e2e8f0; border-color: #94a3b8; }"
            "QTabWidget::pane { border: 1px solid #cbd5e1; background-color: #ffffff; border-radius: 4px; }"
            "QTabBar::tab { background-color: #e2e8f0; color: #334155; padding: 7px 12px; border: 1px solid #cbd5e1; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
            "QTabBar::tab:selected { background-color: #2563eb; color: #ffffff; border-color: #1d4ed8; font-weight: 600; }"
            "QTextBrowser { background-color: #ffffff; color: #1e293b; border: none; padding: 10px; font-family: 'Consolas', 'Segoe UI', monospace; font-size: 9.5pt; }"
        ));
        m_subtitleLabel->setStyleSheet(QStringLiteral("color: #475569;"));
        m_authorLabel->setStyleSheet(QStringLiteral("color: #334155;"));
    } else {
        setStyleSheet(QStringLiteral(
            "QDialog { background-color: #161a23; color: #e6ecf5; font-family: 'Segoe UI', sans-serif; }"
            "QLabel { color: #d8e1ee; }"
            "QLineEdit { background-color: #1e2432; color: #e6ecf5; border: 1px solid #334057; border-radius: 4px; padding: 5px 8px; }"
            "QPushButton { background-color: #243044; color: #e6ecf5; border: 1px solid #384a68; border-radius: 4px; padding: 6px 14px; font-weight: 600; }"
            "QPushButton:hover { background-color: #2f3e58; }"
            "QTabWidget::pane { border: 1px solid #2e384b; background-color: #191e29; border-radius: 4px; }"
            "QTabBar::tab { background-color: #202736; color: #b5c2d6; padding: 7px 12px; border: 1px solid #2e384b; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
            "QTabBar::tab:selected { background-color: #1f6feb; color: #ffffff; font-weight: 600; }"
            "QTextBrowser { background-color: #141821; color: #dfe7f2; border: none; padding: 10px; font-family: 'Consolas', 'Segoe UI', monospace; font-size: 9.5pt; }"
        ));
        m_subtitleLabel->setStyleSheet(QStringLiteral("color: #8ea0b8;"));
        m_authorLabel->setStyleSheet(QStringLiteral("color: #9cb3cf;"));
    }
    ThemeManager::instance().applyWindowChrome(this);
}

void AboutDialog::retranslateUi() {
    using locale::GerenciadorTraducao;
    const QString appVer = QCoreApplication::applicationVersion().isEmpty()
        ? QString::fromLatin1(kFallbackAppVersion)
        : QCoreApplication::applicationVersion();

    setWindowTitle(GerenciadorTraducao::trText("Sobre o OmniFrame 3D"));
    m_titleLabel->setText(QString("OmniFrame 3D — v%1").arg(appVer));
    m_subtitleLabel->setText(GerenciadorTraducao::trText(
        "Analisador Estrutural Espacial 3D por Elementos Finitos (Euler-Bernoulli 12 GDL)"
    ));
    m_authorLabel->setText(GerenciadorTraducao::trText(
        "Autor: Fernando Nillsson Cidade | Dupla Licenca: GNU AGPLv3 (GitHub) / EULA (Microsoft Store)"
    ));

    m_searchLabel->setText(GerenciadorTraducao::trText("Pesquisar no documento:"));
    m_searchEdit->setPlaceholderText(GerenciadorTraducao::trText("Digite um termo para localizar na aba atual..."));
    m_findNextBtn->setText(GerenciadorTraducao::trText("Proxima Ocorrencia"));
    m_closeBtn->setText(GerenciadorTraducao::trText("Fechar"));
    m_licenseComboLabel->setText(GerenciadorTraducao::trText("Documento de Licenca:"));

    m_tabs->setTabText(0, GerenciadorTraducao::trText("Sobre"));
    m_tabs->setTabText(1, GerenciadorTraducao::trText("Historico"));
    m_tabs->setTabText(2, GerenciadorTraducao::trText("Notas da Versao"));
    m_tabs->setTabText(3, GerenciadorTraducao::trText("EULA"));
    m_tabs->setTabText(4, GerenciadorTraducao::trText("CLC / Licenciamento"));
    m_tabs->setTabText(5, GerenciadorTraducao::trText("Privacidade"));
    m_tabs->setTabText(6, GerenciadorTraducao::trText("Copyright"));
    m_tabs->setTabText(7, GerenciadorTraducao::trText("Avisos de Terceiros"));
    m_tabs->setTabText(8, GerenciadorTraducao::trText("Licencas Completas"));
}

void AboutDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    const QEvent::Type t = event ? event->type() : QEvent::None;
    if (t == QEvent::LanguageChange) {
        retranslateUi();
        loadDocuments();
    }
}

void AboutDialog::loadDocuments() {
    const bool isEnglish =
        (locale::GerenciadorTraducao::instance().idiomaAtual() == locale::Idioma::InglesEstadosUnidos);

    for (const DocTabSpec& spec : std::as_const(m_docTabs)) {
        const QString rel = isEnglish ? spec.relPathEn : spec.relPathPt;
        const QString content = locale::GerenciadorTraducao::readAssetDocument(rel);
        spec.browser->setHtml(normalizeTextHtml(content));
    }
    populateThirdPartyLicenses();
}

void AboutDialog::populateThirdPartyLicenses() {
    const int previousIdx = m_licenseCombo->currentIndex();
    m_licenseCombo->blockSignals(true);
    m_licenseCombo->clear();

    const QString assetsRoot = locale::GerenciadorTraducao::assetsDirPath();
    auto loadLicensesFromDir = [this](const QDir& licDir) {
        if (!licDir.exists()) {
            return false;
        }
        const QStringList files = licDir.entryList(QStringList() << "*.txt", QDir::Files, QDir::Name);
        for (const QString& fileName : files) {
            QString display = fileName;
            display.chop(4);
            m_licenseCombo->addItem(display, QString("LICENSES/%1").arg(fileName));
        }
        return !files.isEmpty();
    };

    bool loadedLicenses = false;
    if (!assetsRoot.isEmpty()) {
        loadedLicenses = loadLicensesFromDir(QDir(QDir(assetsRoot).filePath("LICENSES")));
    }
    if (!loadedLicenses && !assetsRoot.startsWith(QLatin1String(":/"))) {
        loadLicensesFromDir(QDir(QStringLiteral(":/omniframe/assets/LICENSES")));
    }

    if (m_licenseCombo->count() > 0) {
        const int idx = (previousIdx >= 0 && previousIdx < m_licenseCombo->count()) ? previousIdx : 0;
        m_licenseCombo->setCurrentIndex(idx);
        m_licenseCombo->blockSignals(false);
        onLicenseSelectionChanged(idx);
    } else {
        m_licenseCombo->blockSignals(false);
        m_licenseBrowser->setHtml(normalizeTextHtml(locale::GerenciadorTraducao::trText(
            "Nenhum arquivo de licenca encontrado em assets/LICENSES."
        )));
    }
}

void AboutDialog::onLicenseSelectionChanged(int index) {
    if (index < 0) {
        m_licenseBrowser->clear();
        return;
    }
    const QString rel = m_licenseCombo->itemData(index).toString();
    m_licenseBrowser->setHtml(normalizeTextHtml(locale::GerenciadorTraducao::readAssetDocument(rel)));
}

QString AboutDialog::normalizeTextHtml(const QString& plainText) {
    QString textColor;
    QString linkColor;
    if (ThemeManager::instance().isNativeUi()) {
        const QPalette pal = QApplication::palette();
        textColor = pal.color(QPalette::Text).name();
        linkColor = pal.color(QPalette::Link).name();
    } else {
        const bool isLight = ThemeManager::instance().isLight();
        textColor = isLight ? QStringLiteral("#1e293b") : QStringLiteral("#dfe7f2");
        linkColor = isLight ? QStringLiteral("#0969da") : QStringLiteral("#6cb6ff");
    }

    QString escaped = plainText.toHtmlEscaped();
    static const QRegularExpression rxUrl(R"((https?://[^\s<>&"]+))");
    escaped.replace(rxUrl, QStringLiteral(R"(<a href="\1" style="color:%1;">\1</a>)").arg(linkColor));
    escaped.replace('\n', "<br>");
    return QStringLiteral(
        "<html><body style=\"color:%1; line-height:1.45; white-space:pre-wrap;\">"
        "%2</body></html>"
    ).arg(textColor, escaped);
}

void AboutDialog::onSearchTextChanged(const QString& text) {
    QWidget* current = m_tabs->currentWidget();
    auto* browser = qobject_cast<QTextBrowser*>(current);
    if (!browser) {
        browser = m_licenseBrowser;
    }
    if (!browser || text.isEmpty()) {
        return;
    }
    QTextCursor cursor = browser->textCursor();
    cursor.movePosition(QTextCursor::Start);
    browser->setTextCursor(cursor);
    browser->find(text);
}

void AboutDialog::onFindNext() {
    const QString text = m_searchEdit->text();
    if (text.isEmpty()) {
        return;
    }
    QWidget* current = m_tabs->currentWidget();
    auto* browser = qobject_cast<QTextBrowser*>(current);
    if (!browser) {
        browser = m_licenseBrowser;
    }
    if (!browser) {
        return;
    }
    if (!browser->find(text)) {
        QTextCursor cursor = browser->textCursor();
        cursor.movePosition(QTextCursor::Start);
        browser->setTextCursor(cursor);
        browser->find(text);
    }
}

} // namespace omniframe
